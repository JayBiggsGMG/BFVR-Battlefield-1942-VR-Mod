#include "client/MenuPointerOverlay.h"

#include "client/ControllerInputCache.h"
#include "presenter/SharedPresentationProtocol.h"
#include "stereo/UiPointerMath.h"

#include <MinHook.h>

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace
{

constexpr wchar_t kEnableVrInteractionEnvironment[] =
    L"BFVR_ENABLE_WEAPON_MOTION";
constexpr std::ptrdiff_t kBfMenuSetGameInputRva = 0x0005DE60;
constexpr std::size_t kBfMenuActiveIndexOffset = 0xFC;
constexpr std::size_t kBfMenuRef2SystemOffset = 0x144;
constexpr std::size_t kBfMenuMouseEnabledOffset = 0x148;
constexpr std::size_t kRef2PointerXOffset = 0x34;
constexpr std::size_t kRef2PointerYOffset = 0x38;
constexpr std::size_t kGameInputEnableMaskLowOffset = 0xC0;
constexpr DWORD kGameInputOk = 20;
constexpr DWORD kGameInputMouseLookX = 26;
constexpr DWORD kGameInputMouseLookY = 27;
constexpr DWORD kControllerHandRight = 1;
constexpr DWORD kControllerSampleMaximumAgeMs = 125;
constexpr float kControllerTriggerPressThreshold = 0.60F;
constexpr float kControllerTriggerReleaseThreshold = 0.45F;
constexpr float kNativeMouseScale = 40.0F;
constexpr float kUiDistanceMeters = 1.5F;
constexpr float kUiWidthMeters = 1.6F;
constexpr UINT kLogicalCanvasWidth = 800;
constexpr UINT kLogicalCanvasHeight = 600;
constexpr BYTE kBfMenuSetGameInputPrefix[] = {
    0x83, 0xEC, 0x20, 0x53, 0x8B, 0xD9, 0x83, 0xBB,
    0xFC, 0x00, 0x00, 0x00, 0xFF, 0x0F, 0x84, 0xDC};
volatile LONG g_nativeMenuActiveState = 0;
SRWLOCK g_menuAnchorLock = SRWLOCK_INIT;
bfvr::stereo::Pose g_menuWorldAnchor = {};
bool g_menuWorldAnchorValid = false;

bool HasExpectedPrefix(
    const void* target,
    const BYTE* expected,
    std::size_t expectedLength) noexcept
{
    if (target == nullptr || expected == nullptr || expectedLength == 0)
    {
        return false;
    }
    __try
    {
        return std::memcmp(target, expected, expectedLength) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bfvr::stereo::Pose ToPose(
    const bfvr::D3D8RuntimeControllerPose& source) noexcept
{
    return {
        {source.positionX, source.positionY, source.positionZ},
        {
            source.orientationX,
            source.orientationY,
            source.orientationZ,
            source.orientationW}};
}

bfvr::stereo::Pose ToPose(
    const bfvr::D3D8RuntimeView& source) noexcept
{
    return {
        {source.positionX, source.positionY, source.positionZ},
        {
            source.orientationX,
            source.orientationY,
            source.orientationZ,
            source.orientationW}};
}

class MenuPointerOverlay
{
public:
    using SetGameInputFn =
        void(__thiscall*)(void* menu, float deltaTime, void* gameInput);

    void Start(
        void* image,
        UINT runtimeWidth,
        UINT runtimeHeight,
        UINT sourceWidth,
        UINT sourceHeight,
        void (*log)(const wchar_t* message))
    {
        if (InterlockedCompareExchange(&started, 1, 0) != 0)
        {
            return;
        }
        appendLog = log;
        wchar_t enabled[2] = {};
        if (GetEnvironmentVariableW(
                kEnableVrInteractionEnvironment,
                enabled,
                static_cast<DWORD>(std::size(enabled))) != 1 ||
            enabled[0] != L'1')
        {
            InterlockedExchange(&started, 0);
            return;
        }
        if (runtimeWidth == 0 || runtimeHeight == 0 ||
            sourceWidth == 0 || sourceHeight == 0)
        {
            WriteLog(
                L"Controller menu pointer rejected invalid UI dimensions: runtime=%ux%u source=%ux%u.",
                runtimeWidth,
                runtimeHeight,
                sourceWidth,
                sourceHeight);
            InterlockedExchange(&started, 0);
            return;
        }

        runtimeUiWidth = runtimeWidth;
        runtimeUiHeight = runtimeHeight;
        sourceUiWidth = sourceWidth;
        sourceUiHeight = sourceHeight;
        gameImage = static_cast<std::byte*>(image);
        setGameInputTarget = gameImage == nullptr
            ? nullptr
            : gameImage + kBfMenuSetGameInputRva;
        if (!HasExpectedPrefix(
                setGameInputTarget,
                kBfMenuSetGameInputPrefix,
                sizeof(kBfMenuSetGameInputPrefix)))
        {
            WriteLog(
                L"Controller menu pointer rejected profiled target %p: the WinPC BfMenu::setGameInput prefix differs.",
                setGameInputTarget);
            InterlockedExchange(&started, 0);
            return;
        }

        const MH_STATUS initializeStatus = MH_Initialize();
        if (initializeStatus == MH_OK)
        {
            ownsMinHook = true;
        }
        else if (initializeStatus != MH_ERROR_ALREADY_INITIALIZED)
        {
            WriteLog(
                L"Controller menu pointer could not initialize MinHook (status=%d).",
                static_cast<int>(initializeStatus));
            InterlockedExchange(&started, 0);
            return;
        }
        const MH_STATUS createStatus = MH_CreateHook(
            setGameInputTarget,
            reinterpret_cast<LPVOID>(&MenuPointerOverlay::SetGameInputHook),
            reinterpret_cast<LPVOID*>(&originalSetGameInput));
        if (createStatus != MH_OK || originalSetGameInput == nullptr)
        {
            WriteLog(
                L"Controller menu pointer could not create its BfMenu::setGameInput hook (status=%d).",
                static_cast<int>(createStatus));
            RemoveHook();
            InterlockedExchange(&started, 0);
            return;
        }
        hookCreated = true;
        InterlockedExchangePointer(&active, this);
        const MH_STATUS enableStatus = MH_EnableHook(setGameInputTarget);
        if (enableStatus != MH_OK)
        {
            WriteLog(
                L"Controller menu pointer could not enable its BfMenu::setGameInput hook (status=%d).",
                static_cast<int>(enableStatus));
            RemoveHook();
            InterlockedExchange(&started, 0);
            return;
        }
        hookEnabled = true;
        WriteLog(
            L"Controller menu pointer armed at 0x0045DE60 for world-locked native menus: runtime=%ux%u source=%ux%u logical=800x600. The presentation path supplies the yaw-only LOCAL anchor shared by this mapper; a fresh tracked right aim ray supplies native c_GIMouseLookX/Y, and right trigger supplies native c_GIOk with hysteresis.",
            runtimeUiWidth,
            runtimeUiHeight,
            sourceUiWidth,
            sourceUiHeight);
    }

    void Stop()
    {
        if (InterlockedCompareExchange(&started, 0, 0) == 0)
        {
            return;
        }
        if (hookEnabled)
        {
            MH_DisableHook(setGameInputTarget);
            hookEnabled = false;
        }
        while (InterlockedCompareExchange(&callbackEntrants, 0, 0) != 0)
        {
            Sleep(0);
        }
        InterlockedCompareExchangePointer(&active, nullptr, this);
        InterlockedExchange(&g_nativeMenuActiveState, 0);
        WriteLog(
            L"Controller menu pointer report: calls=%ld nativeMenuFrames=%ld freshTracking=%ld rayHits=%ld applied=%ld readFaults=%ld restoreFaults=%ld.",
            observedCalls,
            nativeMenuFrames,
            freshTrackingFrames,
            rayHits,
            appliedFrames,
            readFaults,
            restoreFaults);
        RemoveHook();
        InterlockedExchange(&started, 0);
    }

private:
    static void __fastcall SetGameInputHook(
        void* menu,
        void*,
        float deltaTime,
        void* gameInput)
    {
        InterlockedIncrement(&callbackEntrants);
        MenuPointerOverlay* const overlay =
            static_cast<MenuPointerOverlay*>(
                InterlockedCompareExchangePointer(
                    &active,
                    nullptr,
                    nullptr));
        if (overlay != nullptr && overlay->originalSetGameInput != nullptr)
        {
            overlay->Dispatch(menu, deltaTime, gameInput);
        }
        InterlockedDecrement(&callbackEntrants);
    }

    void Dispatch(
        void* menu,
        float deltaTime,
        void* gameInput) noexcept
    {
        InterlockedIncrement(&observedCalls);
        bool nativeMenuActive = false;
        float currentPointerX = 0.0F;
        float currentPointerY = 0.0F;
        __try
        {
            if (menu != nullptr && gameInput != nullptr)
            {
                void* const ref2System =
                    *reinterpret_cast<void**>(
                        static_cast<std::byte*>(menu) +
                        kBfMenuRef2SystemOffset);
                nativeMenuActive =
                    *reinterpret_cast<int*>(
                        static_cast<std::byte*>(menu) +
                        kBfMenuActiveIndexOffset) != -1 &&
                    *reinterpret_cast<int*>(
                        static_cast<std::byte*>(menu) +
                        kBfMenuMouseEnabledOffset) != 0 &&
                    ref2System != nullptr;
                if (nativeMenuActive)
                {
                    currentPointerX =
                        *reinterpret_cast<float*>(
                            static_cast<std::byte*>(ref2System) +
                            kRef2PointerXOffset);
                    currentPointerY =
                        *reinterpret_cast<float*>(
                            static_cast<std::byte*>(ref2System) +
                            kRef2PointerYOffset);
                    nativeMenuActive =
                        std::isfinite(currentPointerX) &&
                        std::isfinite(currentPointerY);
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            nativeMenuActive = false;
            InterlockedIncrement(&readFaults);
        }

        bfvr::D3D8RuntimeControllerSample sample = {};
        bfvr::D3D8RuntimeView matchingHead = {};
        LONG generation = 0;
        const bool fresh =
            nativeMenuActive &&
            bfvr::ReadFreshAcceptedWeaponTracking(
                sample,
                matchingHead,
                generation,
                kControllerSampleMaximumAgeMs);
        const bfvr::D3D8RuntimeControllerHand* right = fresh
            ? &sample.hands[kControllerHandRight]
            : nullptr;
        constexpr DWORD requiredAimFlags =
            bfvr::shared::kControllerHandFlagAimActive |
            bfvr::shared::kControllerHandFlagAimPositionValid |
            bfvr::shared::kControllerHandFlagAimOrientationValid |
            bfvr::shared::kControllerHandFlagAimPositionTracked |
            bfvr::shared::kControllerHandFlagAimOrientationTracked;
        const bool trackedAim =
            right != nullptr &&
            (right->flags & requiredAimFlags) == requiredAimFlags;
        if (trackedAim)
        {
            InterlockedIncrement(&freshTrackingFrames);
        }

        bfvr::stereo::UiCanvasPoint canvasPoint = {};
        bool rayHit = false;
        if (trackedAim)
        {
            bfvr::stereo::Pose publishedAnchor = {};
            if (bfvr::TryGetActiveMenuWorldAnchor(publishedAnchor))
            {
                menuAnchorHead = publishedAnchor;
                menuAnchorValid = true;
            }
            else if (!menuAnchorValid)
            {
                const auto yawOnlyAnchor =
                    bfvr::stereo::MakeYawOnlyUiAnchor(
                        ToPose(matchingHead));
                if (yawOnlyAnchor.has_value())
                {
                    menuAnchorHead = *yawOnlyAnchor;
                    menuAnchorValid = true;
                }
            }
            const auto relativeAim = menuAnchorValid
                ? bfvr::stereo::MakePoseRelativeToReference(
                    menuAnchorHead,
                    ToPose(right->aimPose))
                : std::nullopt;
            const float quadHeight =
                kUiWidthMeters *
                static_cast<float>(runtimeUiHeight) /
                static_cast<float>(runtimeUiWidth);
            const bfvr::stereo::Pose quadPose = {
                {0.0F, 0.0F, -kUiDistanceMeters},
                {0.0F, 0.0F, 0.0F, 1.0F}};
            const auto mapped = relativeAim.has_value()
                ? bfvr::stereo::MapOpenXRAimPoseToAspectFitUiCanvas(
                    *relativeAim,
                    quadPose,
                    kUiWidthMeters,
                    quadHeight,
                    runtimeUiWidth,
                    runtimeUiHeight,
                    sourceUiWidth,
                    sourceUiHeight,
                    kLogicalCanvasWidth,
                    kLogicalCanvasHeight)
                : std::nullopt;
            if (mapped.has_value())
            {
                canvasPoint = *mapped;
                rayHit = true;
                InterlockedIncrement(&rayHits);
            }
        }

        float backupOk = 0.0F;
        float backupMouseX = 0.0F;
        float backupMouseY = 0.0F;
        DWORD backupMask = 0;
        bool modified = false;
        if (rayHit)
        {
            __try
            {
                float* const values =
                    reinterpret_cast<float*>(gameInput);
                DWORD* const enableMask =
                    reinterpret_cast<DWORD*>(
                        static_cast<std::byte*>(gameInput) +
                        kGameInputEnableMaskLowOffset);
                backupOk = values[kGameInputOk];
                backupMouseX = values[kGameInputMouseLookX];
                backupMouseY = values[kGameInputMouseLookY];
                backupMask = *enableMask;

                const float desiredX =
                    canvasPoint.pixelX -
                    static_cast<float>(kLogicalCanvasWidth) * 0.5F;
                const float desiredY =
                    canvasPoint.pixelY -
                    static_cast<float>(kLogicalCanvasHeight) * 0.5F;
                values[kGameInputMouseLookX] =
                    (desiredX - currentPointerX) / kNativeMouseScale;
                values[kGameInputMouseLookY] =
                    (desiredY - currentPointerY) / kNativeMouseScale;
                *enableMask |=
                    (1UL << kGameInputMouseLookX) |
                    (1UL << kGameInputMouseLookY);

                if ((right->flags &
                     bfvr::shared::kControllerHandFlagTriggerActive) != 0)
                {
                    if (triggerPressed)
                    {
                        triggerPressed =
                            right->triggerValue >
                            kControllerTriggerReleaseThreshold;
                    }
                    else
                    {
                        triggerPressed =
                            right->triggerValue >=
                            kControllerTriggerPressThreshold;
                    }
                    const bool nativeOkActive =
                        (backupMask & (1UL << kGameInputOk)) != 0 &&
                        std::isfinite(backupOk) &&
                        backupOk > 0.0F;
                    values[kGameInputOk] =
                        triggerPressed || nativeOkActive ? 1.0F : 0.0F;
                    *enableMask |= 1UL << kGameInputOk;
                }
                modified = true;
                InterlockedIncrement(&appliedFrames);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                modified = false;
                InterlockedIncrement(&readFaults);
            }
        }
        if (nativeMenuActive)
        {
            InterlockedIncrement(&nativeMenuFrames);
        }
        else
        {
            menuAnchorValid = false;
            bfvr::ClearActiveMenuWorldAnchor();
        }
        InterlockedExchange(
            &g_nativeMenuActiveState,
            nativeMenuActive ? 1 : 0);

        originalSetGameInput(menu, deltaTime, gameInput);

        if (modified)
        {
            __try
            {
                float* const values =
                    reinterpret_cast<float*>(gameInput);
                values[kGameInputOk] = backupOk;
                values[kGameInputMouseLookX] = backupMouseX;
                values[kGameInputMouseLookY] = backupMouseY;
                *reinterpret_cast<DWORD*>(
                    static_cast<std::byte*>(gameInput) +
                    kGameInputEnableMaskLowOffset) = backupMask;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                InterlockedIncrement(&restoreFaults);
            }
        }
    }

    void RemoveHook()
    {
        InterlockedCompareExchangePointer(&active, nullptr, this);
        if (hookEnabled)
        {
            MH_DisableHook(setGameInputTarget);
            hookEnabled = false;
        }
        if (hookCreated)
        {
            MH_RemoveHook(setGameInputTarget);
            hookCreated = false;
        }
        originalSetGameInput = nullptr;
        if (ownsMinHook)
        {
            MH_Uninitialize();
            ownsMinHook = false;
        }
    }

    void WriteLog(const wchar_t* format, ...) const
    {
        if (appendLog == nullptr)
        {
            return;
        }
        wchar_t message[1200] = {};
        va_list arguments;
        va_start(arguments, format);
        _vsnwprintf_s(
            message,
            std::size(message),
            _TRUNCATE,
            format,
            arguments);
        va_end(arguments);
        appendLog(message);
    }

    static PVOID volatile active;
    static volatile LONG callbackEntrants;
    volatile LONG started = 0;
    volatile LONG observedCalls = 0;
    volatile LONG nativeMenuFrames = 0;
    volatile LONG freshTrackingFrames = 0;
    volatile LONG rayHits = 0;
    volatile LONG appliedFrames = 0;
    volatile LONG readFaults = 0;
    volatile LONG restoreFaults = 0;
    std::byte* gameImage = nullptr;
    void* setGameInputTarget = nullptr;
    SetGameInputFn originalSetGameInput = nullptr;
    UINT runtimeUiWidth = 0;
    UINT runtimeUiHeight = 0;
    UINT sourceUiWidth = 0;
    UINT sourceUiHeight = 0;
    void (*appendLog)(const wchar_t* message) = nullptr;
    bool triggerPressed = false;
    bool menuAnchorValid = false;
    bfvr::stereo::Pose menuAnchorHead = {};
    bool ownsMinHook = false;
    bool hookCreated = false;
    bool hookEnabled = false;
};

PVOID volatile MenuPointerOverlay::active = nullptr;
volatile LONG MenuPointerOverlay::callbackEntrants = 0;
MenuPointerOverlay g_menuPointerOverlay = {};

} // namespace

namespace bfvr
{

void StartMenuPointerOverlay(
    void* gameImage,
    UINT runtimeUiWidth,
    UINT runtimeUiHeight,
    UINT sourceUiWidth,
    UINT sourceUiHeight,
    void (*appendLog)(const wchar_t* message))
{
    g_menuPointerOverlay.Start(
        gameImage,
        runtimeUiWidth,
        runtimeUiHeight,
        sourceUiWidth,
        sourceUiHeight,
        appendLog);
}

void StopMenuPointerOverlay()
{
    g_menuPointerOverlay.Stop();
}

bool IsMenuPointerOverlayActive() noexcept
{
    return InterlockedCompareExchange(
        &g_nativeMenuActiveState,
        0,
        0) != 0;
}

void PublishActiveMenuWorldAnchor(const stereo::Pose& anchor) noexcept
{
    AcquireSRWLockExclusive(&g_menuAnchorLock);
    g_menuWorldAnchor = anchor;
    g_menuWorldAnchorValid = true;
    ReleaseSRWLockExclusive(&g_menuAnchorLock);
}

void ClearActiveMenuWorldAnchor() noexcept
{
    AcquireSRWLockExclusive(&g_menuAnchorLock);
    g_menuWorldAnchor = {};
    g_menuWorldAnchorValid = false;
    ReleaseSRWLockExclusive(&g_menuAnchorLock);
}

bool TryGetActiveMenuWorldAnchor(stereo::Pose& anchor) noexcept
{
    AcquireSRWLockShared(&g_menuAnchorLock);
    const bool valid = g_menuWorldAnchorValid;
    if (valid)
    {
        anchor = g_menuWorldAnchor;
    }
    ReleaseSRWLockShared(&g_menuAnchorLock);
    return valid;
}

} // namespace bfvr
