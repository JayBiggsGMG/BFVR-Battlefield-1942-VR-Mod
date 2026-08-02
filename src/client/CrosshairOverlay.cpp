#include "client/CrosshairOverlay.h"

#include "client/D3D8WorldCrosshairRenderer.h"
#include "client/MountedWeaponAimResolver.h"
#include "client/WeaponPoseRuntimeCache.h"
#include "stereo/WorldCrosshairMath.h"

#include <MinHook.h>

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cwchar>
#include <optional>

namespace
{

constexpr std::ptrdiff_t kHudManagerSetShowCrosshairRva = 0x002A97B0;
constexpr std::ptrdiff_t kPlayerManagerGlobalRva = 0x0057D76C;
constexpr std::size_t kPlayerManagerLocalPlayerOffset = 0x54;
constexpr std::size_t kBFPlayerIsAliveOffset = 0xA9;
constexpr std::size_t kBFPlayerCurrentControlObjectOffset = 0x64;
constexpr std::size_t kBFPlayerDefaultControlObjectOffset = 0x98;
constexpr std::size_t kBFPlayerHitIndicationTimerOffset = 0x1CC;
constexpr DWORD kNativeArmPoseMaximumAgeMs = 125;
constexpr float kDefaultMaximumDistance = 50.0F;
constexpr float kMinimumMaximumDistance = 2.0F;
constexpr float kMaximumMaximumDistance = 500.0F;
constexpr float kDefaultAngularDiameterDegrees = 2.0F;
constexpr float kMinimumAngularDiameterDegrees = 0.25F;
constexpr float kMaximumAngularDiameterDegrees = 8.0F;
constexpr wchar_t kMaximumDistanceEnvironment[] =
    L"BFVR_CROSSHAIR_MAX_DISTANCE_METRES";
constexpr wchar_t kAngularDiameterEnvironment[] =
    L"BFVR_CROSSHAIR_ANGULAR_DIAMETER_DEGREES";
constexpr std::array<BYTE, 25> kExpectedPrefix = {
    0x8B, 0x41, 0x08, 0x8B, 0x50, 0x0C, 0x8A, 0x44,
    0x24, 0x04, 0x88, 0x42, 0x08, 0x8B, 0x49, 0x0C,
    0x8B, 0x51, 0x0C, 0x88, 0x42, 0x08, 0xC2, 0x04,
    0x00};

class CrosshairOverlay
{
public:
    using SetShowCrosshairFn = void(__thiscall*)(void* manager, BOOL show);

    void Start(
        void* gameImage,
        void (*log)(const wchar_t* message))
    {
        if (InterlockedCompareExchange(&started_, 1, 0) != 0)
        {
            return;
        }
        appendLog_ = log;
        gameImage_ = static_cast<std::byte*>(gameImage);
        maximumDistance_ = ReadBoundedFloatEnvironment(
            kMaximumDistanceEnvironment,
            kDefaultMaximumDistance,
            kMinimumMaximumDistance,
            kMaximumMaximumDistance);
        angularDiameterDegrees_ = ReadBoundedFloatEnvironment(
            kAngularDiameterEnvironment,
            kDefaultAngularDiameterDegrees,
            kMinimumAngularDiameterDegrees,
            kMaximumAngularDiameterDegrees);
        target_ = gameImage == nullptr
            ? nullptr
            : static_cast<std::byte*>(gameImage) +
                kHudManagerSetShowCrosshairRva;
        if (!HasExpectedPrefix())
        {
            WriteLog(
                L"Native crosshair suppression rejected target %p: the profiled WinPC HudManager::setShowCrossHair bytes differ.",
                target_);
            InterlockedExchange(&started_, 0);
            return;
        }

        const MH_STATUS initializeStatus = MH_Initialize();
        if (initializeStatus == MH_OK)
        {
            ownsMinHook_ = true;
        }
        else if (initializeStatus != MH_ERROR_ALREADY_INITIALIZED)
        {
            WriteLog(
                L"Native crosshair suppression could not initialize MinHook (status=%d).",
                static_cast<int>(initializeStatus));
            InterlockedExchange(&started_, 0);
            return;
        }

        const MH_STATUS createStatus = MH_CreateHook(
            target_,
            reinterpret_cast<LPVOID>(&CrosshairOverlay::Hook),
            reinterpret_cast<LPVOID*>(&original_));
        if (createStatus != MH_OK || original_ == nullptr)
        {
            WriteLog(
                L"Native crosshair suppression could not create its HudManager hook (status=%d).",
                static_cast<int>(createStatus));
            RemoveHook();
            InterlockedExchange(&started_, 0);
            return;
        }
        hookCreated_ = true;
        InterlockedExchangePointer(&active_, this);
        const MH_STATUS enableStatus = MH_EnableHook(target_);
        if (enableStatus != MH_OK)
        {
            WriteLog(
                L"Native crosshair suppression could not enable its HudManager hook (status=%d).",
                static_cast<int>(enableStatus));
            RemoveHook();
            InterlockedExchange(&started_, 0);
            return;
        }
        hookEnabled_ = true;
        bfvr::InitializeD3D8WorldCrosshairRenderer(log);
        WriteLog(
            L"Native flat crosshair suppression and 3D reticle state armed at 0x006A97B0; maximumDistance=%.2f m angularDiameter=%.2f degrees. HudManager requests are observed before being forced off; global HUD and unrelated Ref2 draw families remain unchanged.",
            maximumDistance_,
            angularDiameterDegrees_);
    }

    void Stop()
    {
        if (InterlockedCompareExchange(&started_, 0, 0) == 0)
        {
            return;
        }
        if (hookEnabled_)
        {
            MH_DisableHook(target_);
            hookEnabled_ = false;
        }
        InterlockedCompareExchangePointer(&active_, nullptr, this);
        while (InterlockedCompareExchange(&callbackEntrants_, 0, 0) != 0)
        {
            Sleep(0);
        }
        WriteLog(
            L"Native crosshair suppression stopped: visibilityRequests=%ld forcedHidden=%ld.",
            InterlockedCompareExchange(&visibilityRequests_, 0, 0),
            InterlockedCompareExchange(&forcedHidden_, 0, 0));
        RemoveHook();
        bfvr::ShutdownD3D8WorldCrosshairRenderer();
        gameImage_ = nullptr;
        InterlockedExchange(&requestedVisible_, 0);
        InterlockedExchange(&started_, 0);
    }

    bool ReadFrameState(bfvr::WorldCrosshairFrameState& state) noexcept
    {
        state = {};
        state.angularDiameterDegrees = angularDiameterDegrees_;
        if (gameImage_ == nullptr ||
            InterlockedCompareExchange(&started_, 0, 0) == 0)
        {
            return false;
        }

        __try
        {
            void* const manager = *reinterpret_cast<void* const*>(
                gameImage_ + kPlayerManagerGlobalRva);
            auto* const player = manager == nullptr
                ? nullptr
                : *reinterpret_cast<std::byte* const*>(
                    static_cast<const std::byte*>(manager) +
                    kPlayerManagerLocalPlayerOffset);
            if (player == nullptr ||
                std::to_integer<BYTE>(player[kBFPlayerIsAliveOffset]) == 0)
            {
                return false;
            }

            void* const currentControlObject =
                *reinterpret_cast<void* const*>(
                    player + kBFPlayerCurrentControlObjectOffset);
            void* const defaultControlObject =
                *reinterpret_cast<void* const*>(
                    player + kBFPlayerDefaultControlObjectOffset);
            const bool controlsReadable =
                currentControlObject != nullptr &&
                defaultControlObject != nullptr;
            bfvr::NativeArmWeaponVisualPose nativeArmPose = {};
            const bool nativeArmPoseFresh =
                controlsReadable &&
                currentControlObject == defaultControlObject &&
                bfvr::ReadFreshNativeArmWeaponVisualPose(
                    nativeArmPose,
                    kNativeArmPoseMaximumAgeMs) &&
                nativeArmPose.soldier == currentControlObject &&
                nativeArmPose.activeItem != nullptr;
            const bool nativeCrosshairRequested =
                InterlockedCompareExchange(
                    &requestedVisible_, 0, 0) != 0;
            bfvr::stereo::Matrix4 mountedFirePose = {};
            const bool mountedFirePoseReadable =
                controlsReadable &&
                currentControlObject != defaultControlObject &&
                nativeCrosshairRequested &&
                bfvr::ReadMountedWeaponFirePose(
                    currentControlObject,
                    mountedFirePose);
            const bfvr::stereo::WorldCrosshairEligibility eligibility = {
                true,
                controlsReadable,
                controlsReadable &&
                    currentControlObject == defaultControlObject,
                nativeCrosshairRequested,
                nativeArmPoseFresh,
                mountedFirePoseReadable,
                nativeArmPoseFresh
                    ? nativeArmPose.activeItemIndex
                    : -1};
            const bfvr::stereo::WorldCrosshairAimSource source =
                bfvr::stereo::SelectWorldCrosshairAimSource(eligibility);
            const auto endpoint =
                source == bfvr::stereo::WorldCrosshairAimSource::GadgetController
                ? bfvr::stereo::MakeWorldCrosshairEndpointFromFirePose(
                    nativeArmPose.controllerGunWorld,
                    maximumDistance_)
                : source == bfvr::stereo::WorldCrosshairAimSource::MountedWeapon
                ? bfvr::stereo::MakeWorldCrosshairEndpointFromFirePose(
                    mountedFirePose,
                    maximumDistance_)
                : std::optional<bfvr::stereo::Vec3>{};
            if (!endpoint.has_value())
            {
                return false;
            }

            const float hitTimer = *reinterpret_cast<const float*>(
                player + kBFPlayerHitIndicationTimerOffset);
            state.endpoint = *endpoint;
            state.angularDiameterDegrees = angularDiameterDegrees_;
            state.hitMarkerVisible =
                std::isfinite(hitTimer) && hitTimer > 0.0F;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

private:
    static void __fastcall Hook(
        void* manager,
        void*,
        BOOL requestedVisible)
    {
        InterlockedIncrement(&callbackEntrants_);
        CrosshairOverlay* const overlay =
            static_cast<CrosshairOverlay*>(
                InterlockedCompareExchangePointer(
                    &active_,
                    nullptr,
                    nullptr));
        if (overlay != nullptr && overlay->original_ != nullptr)
        {
            InterlockedIncrement(&overlay->visibilityRequests_);
            InterlockedExchange(
                &overlay->requestedVisible_,
                requestedVisible ? 1 : 0);
            if (requestedVisible)
            {
                InterlockedIncrement(&overlay->forcedHidden_);
            }
            overlay->original_(manager, FALSE);
        }
        InterlockedDecrement(&callbackEntrants_);
    }

    static float ReadBoundedFloatEnvironment(
        const wchar_t* name,
        float fallback,
        float minimum,
        float maximum) noexcept
    {
        std::array<wchar_t, 64> value = {};
        const DWORD length = GetEnvironmentVariableW(
            name,
            value.data(),
            static_cast<DWORD>(value.size()));
        if (length == 0 || length >= value.size())
        {
            return fallback;
        }
        wchar_t* end = nullptr;
        const float parsed = std::wcstof(value.data(), &end);
        return end != value.data() && end != nullptr && *end == L'\0' &&
                std::isfinite(parsed)
            ? std::clamp(parsed, minimum, maximum)
            : fallback;
    }

    bool HasExpectedPrefix() const noexcept
    {
        if (target_ == nullptr)
        {
            return false;
        }
        __try
        {
            return std::memcmp(
                       target_,
                       kExpectedPrefix.data(),
                       kExpectedPrefix.size()) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void RemoveHook()
    {
        InterlockedCompareExchangePointer(&active_, nullptr, this);
        if (hookCreated_)
        {
            MH_RemoveHook(target_);
            hookCreated_ = false;
        }
        original_ = nullptr;
        if (ownsMinHook_)
        {
            MH_Uninitialize();
            ownsMinHook_ = false;
        }
    }

    void WriteLog(const wchar_t* format, ...) const
    {
        if (appendLog_ == nullptr)
        {
            return;
        }
        std::array<wchar_t, 600> message = {};
        va_list arguments;
        va_start(arguments, format);
        _vsnwprintf_s(
            message.data(),
            message.size(),
            _TRUNCATE,
            format,
            arguments);
        va_end(arguments);
        appendLog_(message.data());
    }

    volatile LONG started_ = 0;
    volatile LONG visibilityRequests_ = 0;
    volatile LONG forcedHidden_ = 0;
    volatile LONG requestedVisible_ = 0;
    std::byte* gameImage_ = nullptr;
    float maximumDistance_ = kDefaultMaximumDistance;
    float angularDiameterDegrees_ = kDefaultAngularDiameterDegrees;
    void* target_ = nullptr;
    SetShowCrosshairFn original_ = nullptr;
    bool hookCreated_ = false;
    bool hookEnabled_ = false;
    bool ownsMinHook_ = false;
    void (*appendLog_)(const wchar_t* message) = nullptr;

    static void* volatile active_;
    static volatile LONG callbackEntrants_;
};

void* volatile CrosshairOverlay::active_ = nullptr;
volatile LONG CrosshairOverlay::callbackEntrants_ = 0;
CrosshairOverlay g_crosshairOverlay;

} // namespace

namespace bfvr
{

void StartCrosshairOverlay(
    void* gameImage,
    void (*appendLog)(const wchar_t* message))
{
    g_crosshairOverlay.Start(gameImage, appendLog);
}

void StopCrosshairOverlay()
{
    g_crosshairOverlay.Stop();
}

bool ReadWorldCrosshairFrameState(
    WorldCrosshairFrameState& state) noexcept
{
    return g_crosshairOverlay.ReadFrameState(state);
}

} // namespace bfvr
