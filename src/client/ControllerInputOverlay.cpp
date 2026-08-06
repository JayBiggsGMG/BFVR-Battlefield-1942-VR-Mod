#include "client/ControllerInputOverlay.h"

#include "client/ControllerInputCache.h"
#include "client/ScopeViewOverlay.h"
#include "presenter/SharedPresentationProtocol.h"
#include "settings/UserSettings.h"
#include "stereo/DirectionalLocomotion.h"
#include "stereo/SurfaceVehicleDriveMath.h"
#include "stereo/VehicleMotionAimMath.h"

#include <MinHook.h>

#include <intrin.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
constexpr std::ptrdiff_t kFrameBuilderRva = 0x000B6A30;
constexpr std::ptrdiff_t kPlayerActionEncoderRva = 0x00083E70;
constexpr std::ptrdiff_t kNormalizerRva = 0x000913A0;
constexpr std::ptrdiff_t kMultiplayerEncoderCallSiteRva = 0x000B8E45;
constexpr std::ptrdiff_t kMultiplayerEncoderReturnRva = 0x000B8E53;
constexpr std::ptrdiff_t kMultiplayerNormalizerCallSiteRva = 0x000B8E53;
constexpr std::ptrdiff_t kMultiplayerNormalizerReturnRva = 0x000B8E61;
constexpr std::ptrdiff_t kGameInputQueryRva = 0x00046490;
constexpr std::ptrdiff_t kSetupScoreboardQueryReturnRva = 0x000469A2;
constexpr std::ptrdiff_t kHudManagerSetShowScoreboardRva = 0x002A9C90;
constexpr std::ptrdiff_t kHudManagerGlobalRva = 0x0065F1A8;
constexpr std::ptrdiff_t kPlayerManagerGlobalRva = 0x0057D76C;
constexpr std::size_t kPlayerManagerLocalPlayerOffset = 0x54;
constexpr std::size_t kBFPlayerIsAliveOffset = 0xA9;
constexpr std::size_t kBFPlayerCurrentControlObjectOffset = 0x64;
constexpr std::size_t kBFPlayerDefaultControlObjectOffset = 0x98;
constexpr std::size_t kPlayerControlObjectTemplateOffset = 0x124;
constexpr std::size_t kPlayerControlObjectTemplateVehicleCategoryOffset = 0x2EC;
constexpr std::size_t kLogicalInputEnableMaskLowOffset = 0xE0;
constexpr std::size_t kLogicalInputEnableMaskHighOffset = 0xE4;
constexpr DWORD kLogicalInputYaw = 0;
constexpr DWORD kLogicalInputPitch = 1;
constexpr DWORD kLogicalInputRoll = 2;
constexpr DWORD kLogicalInputThrottle = 3;
constexpr DWORD kLogicalInputMouseLookX = 4;
constexpr DWORD kLogicalInputMouseLookY = 5;
constexpr DWORD kLogicalInputFire = 8;
constexpr DWORD kLogicalInputAction = 9;
constexpr DWORD kLogicalInputUse = 10;
constexpr DWORD kLogicalInputMouseLook = 11;
constexpr DWORD kLogicalInputWalk = 12;
constexpr DWORD kLogicalInputMenuSelect9 = 22;
constexpr DWORD kLogicalInputAltFire = 23;
constexpr DWORD kLogicalInputReload = 24;
constexpr DWORD kLogicalInputProne = 28;
constexpr DWORD kLogicalInputCrouch = 29;
constexpr DWORD kLogicalInputCount = 55;
constexpr DWORD kGameInputShowScoreboard = 35;
constexpr DWORD kControllerHandLeft = 0;
constexpr DWORD kControllerHandRight = 1;
constexpr DWORD kControllerSampleMaximumAgeMs = 125;
constexpr float kControllerTriggerPressThreshold = 0.60F;
constexpr float kControllerTriggerReleaseThreshold = 0.45F;
constexpr float kControllerSqueezePressThreshold = 0.60F;
constexpr float kControllerSqueezeReleaseThreshold = 0.45F;
constexpr float kThumbstickDeadzone = 0.20F;
constexpr float kThumbstickDirectionThreshold = 0.72F;
// Below this post-deadzone radial deflection, hold BF1942's native Walk
// action.  Above it, submit the normal full-speed keyboard-equivalent axes.
constexpr float kWalkStickMagnitudeThreshold = 0.60F;
constexpr float kStickTurnInputPerFrame = 0.70F;
constexpr ULONGLONG kUserSettingsPollIntervalMs = 250;
constexpr float kTurnStickResponseExponent = 1.65F;
constexpr float kVehicleAimStickResponseExponent = 1.35F;
constexpr bfvr::stereo::VehicleMotionAimConfiguration
    kSurfaceVehicleMotionAimConfiguration = {};

constexpr DWORD kVehicleCategoryAir = 2;

enum class ControllerControlMode : BYTE
{
    Unknown,
    Infantry,
    SurfaceVehicle,
    AirVehicle,
};

constexpr BYTE kFrameBuilderPrefix[] = {
    0x81, 0xEC, 0x34, 0x01, 0x00, 0x00, 0x53, 0x55,
    0x8B, 0xAC, 0x24, 0x40, 0x01, 0x00, 0x00, 0x33};
constexpr BYTE kPlayerActionEncoderPrefix[] = {
    0x81, 0xEC, 0xFC, 0x00, 0x00, 0x00, 0x56, 0x8B,
    0xB4, 0x24, 0x04, 0x01, 0x00, 0x00, 0x8B, 0x86,
    0xE0, 0x00, 0x00, 0x00, 0x57, 0x8B, 0xF9};
constexpr BYTE kNormalizerPrefix[] = {
    0x83, 0xEC, 0x10, 0x89, 0x4C, 0x24, 0x00, 0x33,
    0xC0, 0x8D, 0xA4, 0x24, 0x00, 0x00, 0x00, 0x00};
// The multiplayer loop must receive BFVR input before this encoder converts
// six logical axes and the button mask to its compact PlayerAction record.
constexpr BYTE kMultiplayerEncoderCallSitePrefix[] = {
    0x8D, 0x4C, 0x24, 0x30, 0x51, 0x8D, 0x4C,
    0x24, 0x1C, 0xE8, 0x1D, 0xB0, 0xFC, 0xFF};
// FUN_004B8D70 resolves the current BFPlayer into EBX, prepares its temporary
// logical frame, and calls the shared normalizer through this exact sequence.
constexpr BYTE kMultiplayerNormalizerCallSitePrefix[] = {
    0x8D, 0x54, 0x24, 0x30, 0x52, 0x8D, 0x4C,
    0x24, 0x1C, 0xE8, 0x3F, 0x85, 0xFD, 0xFF};
constexpr BYTE kGameInputQueryPrefix[] = {
    0x56, 0x57, 0x8B, 0x7C, 0x24, 0x0C, 0x83, 0xFF,
    0x2F, 0x8B, 0xF1, 0x73, 0x43, 0xB8, 0x01, 0x00};
constexpr BYTE kHudManagerSetShowScoreboardPrefix[] = {
    0x8B, 0x44, 0x24, 0x04, 0x53, 0x56, 0x33, 0xDB,
    0x3B, 0xC3, 0x57, 0x8B, 0x7C, 0x24, 0x14, 0x8B,
    0xF1};

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

float ApplyThumbstickDeadzone(float value) noexcept
{
    if (!std::isfinite(value))
    {
        return 0.0F;
    }
    const float clamped = std::clamp(value, -1.0F, 1.0F);
    const float magnitude = std::fabs(clamped);
    if (magnitude <= kThumbstickDeadzone)
    {
        return 0.0F;
    }
    return std::copysign(
        (magnitude - kThumbstickDeadzone) / (1.0F - kThumbstickDeadzone),
        clamped);
}

float ApplyThumbstickResponse(float value, float exponent) noexcept
{
    const float deflected = ApplyThumbstickDeadzone(value);
    if (deflected == 0.0F || !std::isfinite(exponent) || exponent <= 0.0F)
    {
        return deflected;
    }
    return std::copysign(
        std::pow(std::fabs(deflected), exponent),
        deflected);
}

int ThumbstickDirection(float value) noexcept
{
    if (!std::isfinite(value))
    {
        return 0;
    }
    if (value >= kThumbstickDirectionThreshold)
    {
        return 1;
    }
    if (value <= -kThumbstickDirectionThreshold)
    {
        return -1;
    }
    return 0;
}

class ControllerInputOverlay;
void ControllerInputPlayerActionEncodeHook();
void __stdcall ControllerInputPlayerActionEncodeHookImpl(
    void* encodedDestination,
    float* logicalFrame,
    void* callerEbx,
    const void* returnAddress);
DWORD ControllerInputNormalizeHook();
DWORD __stdcall ControllerInputNormalizeHookImpl(
    void* rawSource,
    float* destination,
    void* callerEbx,
    const void* returnAddress);

class ControllerInputOverlay
{
public:
    using FrameBuilderFn = DWORD(__thiscall*)(void* owner, void* player, void* context);
    using PlayerActionEncodeFn = void(__thiscall*)(
        void* encodedDestination,
        float* logicalFrame);
    using NormalizeFn = DWORD(__thiscall*)(void* rawSource, float* destination);
    using GameInputQueryFn = DWORD(__thiscall*)(void* gameInput, DWORD inputId);
    using BFPlayerGetCurrentControlObjectFn = void*(__thiscall*)(void* player);
    using HudManagerSetShowScoreboardFn =
        void(__thiscall*)(void* hudManager, int source, int state);

    void Start(void* image, void (*log)(const wchar_t* message))
    {
        if (InterlockedCompareExchange(&started, 1, 0) != 0)
        {
            WriteLog(L"Controller input overlay ignored a duplicate start request.");
            return;
        }
        gameImage = static_cast<std::byte*>(image);
        appendLog = log;
        userSettings = bfvr::settings::DecodeUserSettings(
            bfvr::settings::ProcessUserSettingsRuntime().Current());
        nextUserSettingsPollAt = 0;
        frameBuilderTarget = gameImage == nullptr
            ? nullptr
            : gameImage + kFrameBuilderRva;
        playerActionEncoderTarget = gameImage == nullptr
            ? nullptr
            : gameImage + kPlayerActionEncoderRva;
        normalizerTarget = gameImage == nullptr
            ? nullptr
            : gameImage + kNormalizerRva;
        multiplayerEncoderCallSite = gameImage == nullptr
            ? nullptr
            : gameImage + kMultiplayerEncoderCallSiteRva;
        multiplayerNormalizerCallSite = gameImage == nullptr
            ? nullptr
            : gameImage + kMultiplayerNormalizerCallSiteRva;
        gameInputQueryTarget = gameImage == nullptr
            ? nullptr
            : gameImage + kGameInputQueryRva;
        hudManagerSetShowScoreboard = gameImage == nullptr
            ? nullptr
            : reinterpret_cast<HudManagerSetShowScoreboardFn>(
                gameImage + kHudManagerSetShowScoreboardRva);
        const bool frameBuilderPrefixMatches = HasExpectedPrefix(
            frameBuilderTarget,
            kFrameBuilderPrefix,
            sizeof(kFrameBuilderPrefix));
        const bool playerActionEncoderPrefixMatches = HasExpectedPrefix(
            playerActionEncoderTarget,
            kPlayerActionEncoderPrefix,
            sizeof(kPlayerActionEncoderPrefix));
        const bool normalizerPrefixMatches = HasExpectedPrefix(
            normalizerTarget,
            kNormalizerPrefix,
            sizeof(kNormalizerPrefix));
        const bool multiplayerEncoderCallSitePrefixMatches = HasExpectedPrefix(
            multiplayerEncoderCallSite,
            kMultiplayerEncoderCallSitePrefix,
            sizeof(kMultiplayerEncoderCallSitePrefix));
        const bool multiplayerNormalizerCallSitePrefixMatches = HasExpectedPrefix(
            multiplayerNormalizerCallSite,
            kMultiplayerNormalizerCallSitePrefix,
            sizeof(kMultiplayerNormalizerCallSitePrefix));
        const bool gameInputQueryPrefixMatches = HasExpectedPrefix(
            gameInputQueryTarget,
            kGameInputQueryPrefix,
            sizeof(kGameInputQueryPrefix));
        const bool scoreboardPrefixMatches = HasExpectedPrefix(
            reinterpret_cast<const void*>(hudManagerSetShowScoreboard),
            kHudManagerSetShowScoreboardPrefix,
            sizeof(kHudManagerSetShowScoreboardPrefix));
        if (!frameBuilderPrefixMatches ||
            !playerActionEncoderPrefixMatches ||
            !normalizerPrefixMatches ||
            !multiplayerEncoderCallSitePrefixMatches ||
            !multiplayerNormalizerCallSitePrefixMatches ||
            !gameInputQueryPrefixMatches ||
            !scoreboardPrefixMatches)
        {
            WriteLog(
                L"Controller input overlay rejected profiled targets because prefix checks failed: frameBuilder=%d encoder=%d multiplayerEncoderCallSite=%d normalizer=%d multiplayerNormalizerCallSite=%d gameInputQuery=%d setShowScoreboard=%d (1=match, 0=mismatch).",
                frameBuilderPrefixMatches ? 1 : 0,
                playerActionEncoderPrefixMatches ? 1 : 0,
                multiplayerEncoderCallSitePrefixMatches ? 1 : 0,
                normalizerPrefixMatches ? 1 : 0,
                multiplayerNormalizerCallSitePrefixMatches ? 1 : 0,
                gameInputQueryPrefixMatches ? 1 : 0,
                scoreboardPrefixMatches ? 1 : 0);
            WriteLog(
                L"Controller input overlay rejected addresses frameBuilder=%p encoder=%p multiplayerEncoderCallSite=%p normalizer=%p multiplayerNormalizerCallSite=%p gameInputQuery=%p setShowScoreboard=%p.",
                frameBuilderTarget,
                playerActionEncoderTarget,
                multiplayerEncoderCallSite,
                normalizerTarget,
                multiplayerNormalizerCallSite,
                gameInputQueryTarget,
                reinterpret_cast<void*>(hudManagerSetShowScoreboard));
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
                L"Controller input overlay could not initialize MinHook (status=%d).",
                static_cast<int>(initializeStatus));
            return;
        }
        const MH_STATUS createFrameBuilder = MH_CreateHook(
            frameBuilderTarget,
            reinterpret_cast<LPVOID>(&ControllerInputOverlay::FrameBuilderHook),
            reinterpret_cast<LPVOID*>(&originalFrameBuilder));
        const MH_STATUS createPlayerActionEncoder = createFrameBuilder == MH_OK
            ? MH_CreateHook(
                playerActionEncoderTarget,
                reinterpret_cast<LPVOID>(&ControllerInputPlayerActionEncodeHook),
                reinterpret_cast<LPVOID*>(&originalPlayerActionEncode))
            : MH_ERROR_NOT_CREATED;
        const MH_STATUS createNormalizer = createPlayerActionEncoder == MH_OK
            ? MH_CreateHook(
                normalizerTarget,
                reinterpret_cast<LPVOID>(&ControllerInputNormalizeHook),
                reinterpret_cast<LPVOID*>(&originalNormalize))
            : MH_ERROR_NOT_CREATED;
        const MH_STATUS createGameInputQuery = createNormalizer == MH_OK
            ? MH_CreateHook(
                gameInputQueryTarget,
                reinterpret_cast<LPVOID>(&ControllerInputOverlay::GameInputQueryHook),
                reinterpret_cast<LPVOID*>(&originalGameInputQuery))
            : MH_ERROR_NOT_CREATED;
        frameBuilderCreated = createFrameBuilder == MH_OK;
        playerActionEncoderCreated = createPlayerActionEncoder == MH_OK;
        normalizerCreated = createNormalizer == MH_OK;
        gameInputQueryCreated = createGameInputQuery == MH_OK;
        if (createFrameBuilder != MH_OK || createPlayerActionEncoder != MH_OK ||
            createNormalizer != MH_OK ||
            createGameInputQuery != MH_OK ||
            originalFrameBuilder == nullptr ||
            originalPlayerActionEncode == nullptr || originalNormalize == nullptr ||
            originalGameInputQuery == nullptr)
        {
            WriteLog(
                L"Controller input overlay could not create its native-frame hooks (frameBuilder=%d encoder=%d normalizer=%d gameInputQuery=%d).",
                static_cast<int>(createFrameBuilder),
                static_cast<int>(createPlayerActionEncoder),
                static_cast<int>(createNormalizer),
                static_cast<int>(createGameInputQuery));
            RemoveHooks();
            return;
        }
        active = this;
        const MH_STATUS enableFrameBuilder = MH_EnableHook(frameBuilderTarget);
        const MH_STATUS enablePlayerActionEncoder = enableFrameBuilder == MH_OK
            ? MH_EnableHook(playerActionEncoderTarget)
            : MH_ERROR_ENABLED;
        const MH_STATUS enableNormalizer = enablePlayerActionEncoder == MH_OK
            ? MH_EnableHook(normalizerTarget)
            : MH_ERROR_ENABLED;
        const MH_STATUS enableGameInputQuery = enableNormalizer == MH_OK
            ? MH_EnableHook(gameInputQueryTarget)
            : MH_ERROR_ENABLED;
        frameBuilderEnabled = enableFrameBuilder == MH_OK;
        playerActionEncoderEnabled = enablePlayerActionEncoder == MH_OK;
        normalizerEnabled = enableNormalizer == MH_OK;
        gameInputQueryEnabled = enableGameInputQuery == MH_OK;
        if (enableFrameBuilder != MH_OK || enablePlayerActionEncoder != MH_OK ||
            enableNormalizer != MH_OK ||
            enableGameInputQuery != MH_OK)
        {
            WriteLog(
                L"Controller input overlay could not enable its native-frame hooks (frameBuilder=%d encoder=%d normalizer=%d gameInputQuery=%d).",
                static_cast<int>(enableFrameBuilder),
                static_cast<int>(enablePlayerActionEncoder),
                static_cast<int>(enableNormalizer),
                static_cast<int>(enableGameInputQuery));
            RemoveHooks();
            return;
        }
        WriteLog(
            L"Controller input overlay armed at local/offline scope 0x004B6A30, multiplayer PlayerAction encoder 0x00483E70/call 0x004B8E45, shared normalizer 0x004913A0/call 0x004B8E53, native GameInput action query 0x00446490, and HudManager scoreboard state machine 0x006A9C90. Multiplayer controller values are admitted only before encoding at return 0x004B8E53 when preserved EBX exactly equals the current manager local BFPlayer; the post-encode normalizer is observation-only for that route. Keyboard/mouse bindings remain native.");
    }

    void Stop()
    {
        RemoveHooks();
        InterlockedExchange(&started, 0);
    }

private:
    friend void __stdcall ControllerInputPlayerActionEncodeHookImpl(
        void* encodedDestination,
        float* logicalFrame,
        void* callerEbx,
        const void* returnAddress);
    friend DWORD __stdcall ControllerInputNormalizeHookImpl(
        void* rawSource,
        float* destination,
        void* callerEbx,
        const void* returnAddress);

    static DWORD __fastcall FrameBuilderHook(
        void* owner,
        void*,
        void* player,
        void* context)
    {
        ControllerInputOverlay* const overlay = active;
        if (overlay == nullptr || overlay->originalFrameBuilder == nullptr)
        {
            return 0;
        }
        InterlockedIncrement(&overlay->frameBuilderCalls);
        void* const previousPlayer = scopedPlayer;
        scopedPlayer = player;
        const DWORD result = overlay->originalFrameBuilder(owner, player, context);
        scopedPlayer = previousPlayer;
        return result;
    }

    static DWORD __fastcall GameInputQueryHook(
        void* gameInput,
        void*,
        DWORD inputId)
    {
        ControllerInputOverlay* const overlay = active;
        if (overlay == nullptr || overlay->originalGameInputQuery == nullptr)
        {
            return 0;
        }

        const void* const returnAddress = _ReturnAddress();
        const DWORD nativeResult =
            overlay->originalGameInputQuery(gameInput, inputId);
        if (inputId != kGameInputShowScoreboard || overlay->gameImage == nullptr ||
            returnAddress !=
                overlay->gameImage + kSetupScoreboardQueryReturnRva)
        {
            return nativeResult;
        }

        bool controllerScoreboardHeld = false;
        bfvr::D3D8RuntimeControllerSample sample = {};
        LONG generation = 0;
        if (bfvr::ReadFreshAcceptedControllerInput(
                sample,
                generation,
                kControllerSampleMaximumAgeMs))
        {
            const bfvr::D3D8RuntimeControllerHand& left =
                sample.hands[kControllerHandLeft];
            const bfvr::D3D8RuntimeControllerHand& right =
                sample.hands[kControllerHandRight];
            const bool quickMenuHeld = IsHandButtonPressed(
                right,
                bfvr::shared::kControllerHandButtonPrimary);
            controllerScoreboardHeld =
                !quickMenuHeld && IsHandButtonPressed(
                    left,
                    bfvr::shared::kControllerHandButtonSecondary);
        }

        overlay->ApplyControllerScoreboardPlayerState(
            controllerScoreboardHeld,
            nativeResult != 0);
        if (!controllerScoreboardHeld)
        {
            return nativeResult;
        }

        if (InterlockedCompareExchange(
                &overlay->firstScoreboardQueryOverrideLogged,
                1,
                0) == 0)
        {
            overlay->WriteLog(
                L"Controller Y made native GameInput action query 35 true at exact Setup::processGameInput return 0x004469A2. BF1942 will now execute its ordinary Tab scoreboard branch and HudManager timing.");
        }
        return 1;
    }

    void ApplyControllerScoreboardPlayerState(
        bool controllerHeld,
        bool nativeGlobalHeld) noexcept
    {
        const bool controllerReleased =
            controllerScoreboardWasHeld && !controllerHeld;
        controllerScoreboardWasHeld = controllerHeld;
        if (!controllerHeld && !controllerReleased)
        {
            return;
        }

        if (hudManagerSetShowScoreboard == nullptr || gameImage == nullptr)
        {
            return;
        }
        __try
        {
            void* const hudManager = *reinterpret_cast<void**>(
                gameImage + kHudManagerGlobalRva);
            if (hudManager == nullptr)
            {
                return;
            }

            const bool requestedVisible = controllerHeld || nativeGlobalHeld;
            // Tab is bound to both c_PIShowScoreBoard and
            // c_GIShowScoreBoard.  The player-side HudManager::scoreBoard
            // wrapper calls this exact function with source 0; the global
            // Setup branch that follows uses source 1.
            hudManagerSetShowScoreboard(
                hudManager,
                0,
                requestedVisible ? 1 : 0);
            if (controllerHeld &&
                InterlockedCompareExchange(
                    &firstPlayerScoreboardCallLogged,
                    1,
                    0) == 0)
            {
                const BYTE managerShow =
                    *(reinterpret_cast<const BYTE*>(hudManager) + 0x50);
                void* const scoreboard = *reinterpret_cast<void**>(
                    reinterpret_cast<std::byte*>(hudManager) + 0x3C);
                const BYTE scoreboardShow = scoreboard == nullptr
                    ? 0xFF
                    : *(reinterpret_cast<const BYTE*>(scoreboard) + 0xF8);
                WriteLog(
                    L"Controller Y submitted the missing player-side scoreboard state through HudManager::setShowScoreboard(0,1) before the native global branch: hudManager=%p managerShow=%u scoreboardShow=%u.",
                    hudManager,
                    static_cast<unsigned int>(managerShow),
                    static_cast<unsigned int>(scoreboardShow));
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            if (InterlockedCompareExchange(
                    &firstPlayerScoreboardFaultLogged,
                    1,
                    0) == 0)
            {
                WriteLog(
                    L"Controller Y could not submit the prefix-verified player-side scoreboard state because the live HudManager was unreadable.");
            }
        }
    }

    bool IsCurrentLocalAlivePlayer(void* candidatePlayer) noexcept
    {
        if (gameImage == nullptr || candidatePlayer == nullptr)
        {
            InterlockedIncrement(&missingScopedPlayerFrames);
            return false;
        }
        __try
        {
            auto* const manager = *reinterpret_cast<void* const*>(
                gameImage + kPlayerManagerGlobalRva);
            if (manager == nullptr)
            {
                InterlockedIncrement(&missingManagerFrames);
                return false;
            }
            const auto* const localPlayer = *reinterpret_cast<void* const*>(
                static_cast<const std::byte*>(manager) +
                kPlayerManagerLocalPlayerOffset);
            if (localPlayer == nullptr || localPlayer != candidatePlayer)
            {
                InterlockedIncrement(&nonLocalPlayerFrames);
                return false;
            }
            const auto* const playerBytes =
                static_cast<const std::byte*>(localPlayer);
            if (std::to_integer<BYTE>(playerBytes[kBFPlayerIsAliveOffset]) == 0)
            {
                InterlockedIncrement(&notAliveFrames);
                ResetControllerState();
                return false;
            }
            InterlockedIncrement(&eligibleLocalPlayerFrames);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            InterlockedIncrement(&unreadablePlayerFrames);
            return false;
        }
    }

    ControllerControlMode ReadCurrentControlMode(
        void* player,
        bool multiplayerRoute) noexcept
    {
        if (player == nullptr)
        {
            return ControllerControlMode::Unknown;
        }

        void* currentControlObject = nullptr;
        void* defaultControlObject = nullptr;
        __try
        {
            const auto* const playerBytes =
                static_cast<const std::byte*>(player);
            if (multiplayerRoute)
            {
                auto* const vtable = *reinterpret_cast<void***>(player);
                if (vtable == nullptr)
                {
                    return ControllerControlMode::Unknown;
                }
                const auto getCurrentControlObject =
                    reinterpret_cast<BFPlayerGetCurrentControlObjectFn>(
                        vtable[0x38 / sizeof(void*)]);
                if (getCurrentControlObject == nullptr)
                {
                    return ControllerControlMode::Unknown;
                }
                // FUN_004B7FC0 is the consumer immediately downstream of the
                // recovered multiplayer frame. It obtains the current object
                // through this exact virtual +0x38 call before comparing it
                // with BFPlayer +0x98 at 0x004B8092..0x004B80A1.
                currentControlObject = getCurrentControlObject(player);
            }
            else
            {
                currentControlObject = *reinterpret_cast<void* const*>(
                    playerBytes + kBFPlayerCurrentControlObjectOffset);
            }
            defaultControlObject = *reinterpret_cast<void* const*>(
                playerBytes + kBFPlayerDefaultControlObjectOffset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return ControllerControlMode::Unknown;
        }

        ControllerControlMode mode = ControllerControlMode::Unknown;
        if (currentControlObject != nullptr && defaultControlObject != nullptr)
        {
            mode = currentControlObject == defaultControlObject
                ? ControllerControlMode::Infantry
                : ControllerControlMode::SurfaceVehicle;
        }
        if (multiplayerRoute &&
            InterlockedCompareExchange(
                &firstMultiplayerControlModeLogged,
                1,
                0) == 0)
        {
            WriteLog(
                L"Multiplayer controller mode used BFPlayer virtual +0x38 exactly like native FUN_004B7FC0: player=%p current=%p default(+0x98)=%p initialMode=%s.",
                player,
                currentControlObject,
                defaultControlObject,
                mode == ControllerControlMode::Infantry
                    ? L"infantry"
                    : (mode == ControllerControlMode::SurfaceVehicle
                        ? L"surface/non-default"
                        : L"unknown"));
        }
        if (mode == ControllerControlMode::Unknown ||
            mode == ControllerControlMode::Infantry)
        {
            return mode;
        }

        // PlayerControlObjectTemplate::vehicleCategory uses
        // 0=land, 1=sea, 2=air. A non-default object whose category cannot be
        // read safely remains on the surface mapping rather than acquiring
        // aircraft-only roll/yaw controls.
        __try
        {
            const auto* const controlBytes =
                static_cast<const std::byte*>(currentControlObject);
            const auto* const objectTemplate =
                *reinterpret_cast<const std::byte* const*>(
                    controlBytes + kPlayerControlObjectTemplateOffset);
            if (objectTemplate != nullptr)
            {
                const DWORD vehicleCategory =
                    *reinterpret_cast<const DWORD*>(
                        objectTemplate +
                        kPlayerControlObjectTemplateVehicleCategoryOffset);
                if (vehicleCategory == kVehicleCategoryAir)
                {
                    return ControllerControlMode::AirVehicle;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            // The current/default-object comparison has already established
            // vehicle control. Fail closed only on the air specialization.
        }
        return mode;
    }

    static bool IsHandButtonPressed(
        const bfvr::D3D8RuntimeControllerHand& hand,
        DWORD button) noexcept
    {
        return (hand.buttons & button) != 0;
    }

    static bool TakeRisingEdge(bool down, bool& wasDown) noexcept
    {
        const bool rising = down && !wasDown;
        wasDown = down;
        return rising;
    }

    static bool UpdateAnalogHeld(
        bool isActive,
        float value,
        float pressThreshold,
        float releaseThreshold,
        bool& held) noexcept
    {
        if (!isActive || !std::isfinite(value))
        {
            held = false;
            return false;
        }
        if (!held && value >= pressThreshold)
        {
            held = true;
        }
        else if (held && value <= releaseThreshold)
        {
            held = false;
        }
        return held;
    }

    static void EnableInput(float* destination, DWORD input) noexcept
    {
        if (input >= kLogicalInputCount)
        {
            return;
        }
        auto* const destinationBytes = reinterpret_cast<std::byte*>(destination);
        if (input < 32)
        {
            auto* const lowMask = reinterpret_cast<DWORD*>(
                destinationBytes + kLogicalInputEnableMaskLowOffset);
            *lowMask |= 1U << input;
        }
        else
        {
            auto* const highMask = reinterpret_cast<DWORD*>(
                destinationBytes + kLogicalInputEnableMaskHighOffset);
            *highMask |= 1U << (input - 32);
        }
    }

    static bool IsInputActive(
        const float* source,
        DWORD input) noexcept
    {
        if (source == nullptr || input >= kLogicalInputCount ||
            !std::isfinite(source[input]) || source[input] <= 0.5F)
        {
            return false;
        }
        const auto* const sourceBytes =
            reinterpret_cast<const std::byte*>(source);
        if (input < 32)
        {
            const DWORD lowMask = *reinterpret_cast<const DWORD*>(
                sourceBytes + kLogicalInputEnableMaskLowOffset);
            return (lowMask & (1U << input)) != 0;
        }
        const DWORD highMask = *reinterpret_cast<const DWORD*>(
            sourceBytes + kLogicalInputEnableMaskHighOffset);
        return (highMask & (1U << (input - 32))) != 0;
    }

    static void AddAxisInput(
        float* destination,
        DWORD input,
        float value) noexcept
    {
        if (input >= kLogicalInputCount || !std::isfinite(value) || value == 0.0F)
        {
            return;
        }
        const float current = std::isfinite(destination[input])
            ? destination[input]
            : 0.0F;
        destination[input] = std::clamp(current + value, -1.0F, 1.0F);
        EnableInput(destination, input);
    }

    static void AddInfantryTurnInput(
        float* destination,
        float value) noexcept
    {
        if (!std::isfinite(value) || value == 0.0F)
        {
            return;
        }
        const float current = std::isfinite(
            destination[kLogicalInputMouseLookX])
            ? destination[kLogicalInputMouseLookX]
            : 0.0F;
        constexpr float maximumConfiguredTurnInput =
            kStickTurnInputPerFrame *
            (static_cast<float>(
                 bfvr::settings::kMaximumInfantryTurnSpeedPercent) /
             100.0F);
        destination[kLogicalInputMouseLookX] = std::clamp(
            current + value,
            -maximumConfiguredTurnInput,
            maximumConfiguredTurnInput);
        EnableInput(destination, kLogicalInputMouseLookX);
    }

    static void AddInfantrySnapTurnInput(
        float* destination,
        float degrees) noexcept
    {
        if (!std::isfinite(degrees) || degrees == 0.0F)
        {
            return;
        }
        // The semantic Mac infantry handler applies native mouse-look yaw
        // through a factor-three scale after simulation-time normalization;
        // retail WinPC has already accepted these same logical look slots in
        // headset play. A snap must use that character-owned route: rotating
        // only OpenXR leaves the soldier, arms, and weapon on their old yaw.
        constexpr float kNativeDegreesPerMouseLookUnit = 3.0F;
        constexpr float kMaximumSnapTurnDegrees = 90.0F;
        const float current = std::isfinite(
            destination[kLogicalInputMouseLookX])
            ? destination[kLogicalInputMouseLookX]
            : 0.0F;
        const float boundedDegrees = std::clamp(
            degrees, -kMaximumSnapTurnDegrees, kMaximumSnapTurnDegrees);
        destination[kLogicalInputMouseLookX] = current +
            boundedDegrees / kNativeDegreesPerMouseLookUnit;
        EnableInput(destination, kLogicalInputMouseLookX);
    }

    static bool ExtractOpenXRYaw(
        float x,
        float y,
        float z,
        float w,
        float& yaw) noexcept
    {
        const float norm = x * x + y * y + z * z + w * w;
        if (!std::isfinite(norm) || norm < 0.5F || norm > 1.5F)
        {
            return false;
        }
        yaw = std::atan2(
            2.0F * (w * y + x * z),
            1.0F - 2.0F * (y * y + z * z));
        return std::isfinite(yaw);
    }

    static void SetHeldInput(
        float* destination,
        DWORD input,
        bool held) noexcept
    {
        if (!held || input >= kLogicalInputCount)
        {
            return;
        }
        destination[input] = std::max(destination[input], 1.0F);
        EnableInput(destination, input);
    }

    static void SetPulseInput(
        float* destination,
        DWORD input,
        bool pressed) noexcept
    {
        SetHeldInput(destination, input, pressed);
    }

    void UpdateControllerControlMode(
        ControllerControlMode controlMode) noexcept
    {
        if (controlMode == activeControlMode)
        {
            return;
        }
        rightStickVerticalDirection = 0;
        crouchToggled = false;
        nativeAltFireWasDown = false;
        bfvr::stereo::ResetDigitalLocomotion(infantryMovementDirection);
        bfvr::stereo::ResetVehicleMotionAim(surfaceVehicleMotionAim);
        activeControlMode = controlMode;

        if ((controlMode == ControllerControlMode::SurfaceVehicle ||
             controlMode == ControllerControlMode::AirVehicle) &&
            InterlockedCompareExchange(&firstVehicleModeLogged, 1, 0) == 0)
        {
            WriteLog(
                L"Controller input switched from the default infantry control object to a non-default vehicle or mounted PlayerControlObject. Vehicle-only stick mapping is active; infantry stick state was cleared.");
        }
        if (controlMode == ControllerControlMode::AirVehicle &&
            InterlockedCompareExchange(&firstAirModeLogged, 1, 0) == 0)
        {
            WriteLog(
                L"Controller input read PlayerControlObjectTemplate vehicleCategory=VCAir. Aircraft stick mapping is active: left throttle/roll, right yaw/pitch.");
        }
    }

    void ApplyControllerControls(
        float* destination,
        const bfvr::D3D8RuntimeControllerHand& left,
        const bfvr::D3D8RuntimeControllerHand& right,
        const bfvr::D3D8RuntimeView& matchingHead,
        bool matchingHeadTracked,
        ControllerControlMode controlMode,
        std::int64_t predictedDisplayTime,
        bool multiplayerRoute) noexcept
    {
        RefreshUserSettings();
        const bool quickMenuHeld = IsHandButtonPressed(
            right,
            bfvr::shared::kControllerHandButtonPrimary);
        const bool nativeAltFireActive = IsInputActive(
            destination,
            kLogicalInputAltFire);
        const bool nativeAltFirePressed =
            nativeAltFireActive && !nativeAltFireWasDown;
        nativeAltFireWasDown = nativeAltFireActive;
        bool jumpAndParachutePressed = false;
        bool crouchTogglePressed = false;
        bool mouseLookEnabled = false;

        constexpr DWORD requiredMotionAimFlags =
            bfvr::shared::kControllerHandFlagGripActive |
            bfvr::shared::kControllerHandFlagGripPositionValid |
            bfvr::shared::kControllerHandFlagGripPositionTracked;
        const bool motionAimTracked =
            (right.flags & requiredMotionAimFlags) == requiredMotionAimFlags;
        const bfvr::stereo::VehicleMotionAimOutput motionAim =
            bfvr::stereo::UpdateVehicleMotionAim(
                surfaceVehicleMotionAim,
                controlMode == ControllerControlMode::SurfaceVehicle &&
                    !quickMenuHeld,
                motionAimTracked,
                {
                    right.gripPose.positionX,
                    right.gripPose.positionY,
                    right.gripPose.positionZ},
                predictedDisplayTime,
                kSurfaceVehicleMotionAimConfiguration);
        const bfvr::stereo::VehicleAimInputSigns vehicleAimSigns =
            bfvr::stereo::CalibratedVehicleAimInputSigns(
                userSettings.invertTurretPitch,
                userSettings.invertTurretYaw);
        if (motionAim.trackingAccepted &&
            InterlockedCompareExchange(
                &firstSurfaceMotionAimLogged,
                1,
                0) == 0)
        {
            WriteLog(
                L"Surface/sea/mounted right-grip motion aim acquired a tracked zero-input reference at %.1f native-input units/metre. Relative hand movement now complements right-stick traverse/elevation with matching physical directions on both axes. Quick Menu hold, tracking loss, and control-mode changes rebaseline without a turret jump.",
                kSurfaceVehicleMotionAimConfiguration.inputPerMetre);
        }

        if (controlMode == ControllerControlMode::Infantry)
        {
            if ((left.flags &
                    bfvr::shared::kControllerHandFlagThumbstickActive) != 0)
            {
                // The active Infantry profile binds D/A to c_PIYaw and W/S
                // to c_PIThrottle, agreeing with OpenXR +X right / +Y
                // forward. BF1942 does not consume them as analogue walking
                // speed, so the light band uses c_PIWalk and directional
                // motion remains the game's ordinary full-strength input.
                float movementX =
                    ApplyThumbstickDeadzone(left.thumbstickX);
                float movementY =
                    ApplyThumbstickDeadzone(left.thumbstickY);
                const float movementMagnitude = std::min(
                    std::hypot(movementX, movementY),
                    1.0F);
                float movementYaw = 0.0F;
                bool quantizeRotatedMovement = false;
                if (userSettings.movementDirection ==
                        bfvr::settings::MovementDirection::Head &&
                    matchingHeadTracked)
                {
                    float openXrYaw = 0.0F;
                    if (ExtractOpenXRYaw(
                            matchingHead.orientationX,
                            matchingHead.orientationY,
                            matchingHead.orientationZ,
                            matchingHead.orientationW,
                            openXrYaw))
                    {
                        movementYaw = -openXrYaw;
                        quantizeRotatedMovement = true;
                    }
                }
                else if (userSettings.movementDirection ==
                         bfvr::settings::MovementDirection::OffHandController)
                {
                    constexpr DWORD requiredAimFlags =
                        bfvr::shared::kControllerHandFlagAimActive |
                        bfvr::shared::kControllerHandFlagAimOrientationValid |
                        bfvr::shared::kControllerHandFlagAimOrientationTracked;
                    float openXrYaw = 0.0F;
                    if ((left.flags & requiredAimFlags) == requiredAimFlags &&
                        ExtractOpenXRYaw(
                            left.aimPose.orientationX,
                            left.aimPose.orientationY,
                            left.aimPose.orientationZ,
                            left.aimPose.orientationW,
                            openXrYaw))
                    {
                        movementYaw = -openXrYaw;
                        quantizeRotatedMovement = true;
                    }
                }
                bfvr::stereo::DigitalLocomotionDirection direction = {};
                if (quantizeRotatedMovement)
                {
                    direction = bfvr::stereo::QuantizeDigitalLocomotion(
                        movementX,
                        movementY,
                        movementYaw,
                        infantryMovementDirection);
                }
                else
                {
                    bfvr::stereo::ResetDigitalLocomotion(
                        infantryMovementDirection);
                    direction.horizontal = movementX == 0.0F
                        ? 0
                        : (movementX > 0.0F ? 1 : -1);
                    direction.forward = movementY == 0.0F
                        ? 0
                        : (movementY > 0.0F ? 1 : -1);
                }
                const bool moving = movementMagnitude != 0.0F;
                SetHeldInput(
                    destination,
                    kLogicalInputWalk,
                    moving &&
                        movementMagnitude < kWalkStickMagnitudeThreshold);
                if (direction.horizontal != 0)
                {
                    AddAxisInput(
                        destination,
                        kLogicalInputYaw,
                        static_cast<float>(direction.horizontal));
                }
                if (direction.forward != 0)
                {
                    AddAxisInput(
                        destination,
                        kLogicalInputThrottle,
                        static_cast<float>(direction.forward));
                }
            }
            else
            {
                bfvr::stereo::ResetDigitalLocomotion(
                    infantryMovementDirection);
            }

            if ((right.flags &
                    bfvr::shared::kControllerHandFlagThumbstickActive) != 0)
            {
                if (userSettings.artificialTurnMode ==
                    bfvr::settings::ArtificialTurnMode::Snap)
                {
                    const float snapAxis = right.thumbstickX;
                    if (std::fabs(snapAxis) < 0.35F)
                    {
                        snapTurnArmed = true;
                    }
                    else if (snapTurnArmed && std::fabs(snapAxis) >= 0.72F &&
                        !quickMenuHeld)
                    {
                        const float degrees = std::copysign(
                            static_cast<float>(
                                userSettings.snapTurnAngleDegrees),
                            snapAxis);
                        AddInfantrySnapTurnInput(destination, degrees);
                        mouseLookEnabled = true;
                        snapTurnArmed = false;
                    }
                }
                else
                {
                    snapTurnArmed = true;
                }
                const float turnInput =
                    userSettings.artificialTurnMode ==
                        bfvr::settings::ArtificialTurnMode::Smooth
                    ?
                    ApplyThumbstickResponse(
                        right.thumbstickX,
                        kTurnStickResponseExponent) *
                    kStickTurnInputPerFrame *
                    (static_cast<float>(
                         userSettings.infantryTurnSpeedPercent) /
                     100.0F)
                    : 0.0F;
                if (turnInput != 0.0F)
                {
                    AddInfantryTurnInput(destination, turnInput);
                    mouseLookEnabled = true;
                }

                const int verticalDirection = quickMenuHeld
                    ? 0
                    : ThumbstickDirection(right.thumbstickY);
                if (!quickMenuHeld && verticalDirection != 0 &&
                    verticalDirection != rightStickVerticalDirection)
                {
                    jumpAndParachutePressed = verticalDirection > 0;
                    crouchTogglePressed = verticalDirection < 0;
                }
                rightStickVerticalDirection = verticalDirection;
            }
            else
            {
                rightStickVerticalDirection = 0;
                snapTurnArmed = true;
            }
        }
        else
        {
            rightStickVerticalDirection = 0;
            if (controlMode == ControllerControlMode::SurfaceVehicle)
            {
                if ((left.flags &
                        bfvr::shared::kControllerHandFlagThumbstickActive) != 0)
                {
                    // Ground and sea engines bind c_PIYaw as steering and
                    // c_PIThrottle as gas/reverse. Preserve the proven full
                    // throttle command, but widen steering into a smooth
                    // curve: 11/1 o'clock is gentle and 10/2 o'clock reaches
                    // full steering without releasing full gas.
                    const bfvr::stereo::SurfaceVehicleDriveInput drive =
                        bfvr::stereo::MapSurfaceVehicleDrive(
                            left.thumbstickX,
                            left.thumbstickY);
                    AddAxisInput(
                        destination,
                        kLogicalInputYaw,
                        drive.steering);
                    AddAxisInput(
                        destination,
                        kLogicalInputThrottle,
                        drive.throttle);
                    if ((drive.steering != 0.0F ||
                         drive.throttle != 0.0F) &&
                        InterlockedCompareExchange(
                            &firstSurfaceDriveInputLogged,
                            1,
                            0) == 0)
                    {
                        WriteLog(
                            L"Surface vehicle controller drive submitted its first non-zero curved axes before PlayerAction encoding: rawStick=(%.3f,%.3f) yaw/steer=%.3f throttle=%.1f; steering reaches full at 10/2 o'clock while retaining full throttle; multiplayerRoute=%d.",
                            left.thumbstickX,
                            left.thumbstickY,
                            drive.steering,
                            drive.throttle,
                            multiplayerRoute ? 1 : 0);
                    }
                }
                if ((right.flags &
                        bfvr::shared::kControllerHandFlagThumbstickActive) != 0)
                {
                    // Stock tank, AA, naval-station, and mounted-gun object
                    // templates bind traverse/elevation to mouse-look X/Y.
                    AddAxisInput(
                        destination,
                        kLogicalInputMouseLookX,
                        ApplyThumbstickResponse(
                            right.thumbstickX,
                            kVehicleAimStickResponseExponent) *
                            vehicleAimSigns.stickYaw);
                    AddAxisInput(
                        destination,
                        kLogicalInputMouseLookY,
                        ApplyThumbstickResponse(
                            right.thumbstickY,
                            kVehicleAimStickResponseExponent) *
                            vehicleAimSigns.stickPitch);
                }
                // Relative grip movement is mouse-like: movement contributes a
                // bounded fine delta, while holding the hand still contributes
                // zero. It adds to, rather than replaces, unrestricted stick
                // traverse/elevation. Live owner validation found the former
                // virtual-pivot yaw opposed the stick, while pitch already
                // agreed. The calibrated raw-axis signs preserve that split,
                // then apply each inversion toggle to its complete pair.
                AddAxisInput(
                    destination,
                    kLogicalInputMouseLookX,
                    motionAim.mouseLookX * vehicleAimSigns.motionYaw);
                AddAxisInput(
                    destination,
                    kLogicalInputMouseLookY,
                    motionAim.mouseLookY * vehicleAimSigns.motionPitch);
            }
            else if (controlMode == ControllerControlMode::AirVehicle)
            {
                if ((left.flags &
                        bfvr::shared::kControllerHandFlagThumbstickActive) != 0)
                {
                    AddAxisInput(
                        destination,
                        kLogicalInputRoll,
                        ApplyThumbstickDeadzone(left.thumbstickX));
                    AddAxisInput(
                        destination,
                        kLogicalInputThrottle,
                        ApplyThumbstickDeadzone(left.thumbstickY));
                }
                if ((right.flags &
                        bfvr::shared::kControllerHandFlagThumbstickActive) != 0)
                {
                    // OpenXR +Y is stick-up. BF1942 positive c_PIPitch is
                    // the stock dive/point-down direction, matching the
                    // requested up=dive and down=climb layout.
                    AddAxisInput(
                        destination,
                        kLogicalInputYaw,
                        ApplyThumbstickDeadzone(right.thumbstickX));
                    AddAxisInput(
                        destination,
                        kLogicalInputPitch,
                        ApplyThumbstickDeadzone(right.thumbstickY) *
                            (userSettings.invertFlightPitch ? -1.0F : 1.0F));
                }
            }
        }
        if (mouseLookEnabled)
        {
            SetHeldInput(destination, kLogicalInputMouseLook, true);
        }

        // Right A belongs exclusively to the BFVR Quick Menu. During its
        // hold, retain stick locomotion/vehicle control; all combat and
        // face-button submissions remain absent from the native frame.
        if (quickMenuHeld)
        {
            // Crouch toggle is a persistent gameplay state, not a face-button
            // action. Continue submitting it while A owns the Quick Menu;
            // otherwise the early return looks like a crouch release and the
            // soldier stands until A is released.
            SetHeldInput(
                destination,
                kLogicalInputCrouch,
                crouchToggled);
            triggerHeld = false;
            leftTriggerHeld = false;
            rightSqueezeHeld = false;
            leftPrimaryWasDown = IsHandButtonPressed(
                left,
                bfvr::shared::kControllerHandButtonPrimary);
            rightSecondaryWasDown = IsHandButtonPressed(
                right,
                bfvr::shared::kControllerHandButtonSecondary);
            return;
        }

        const bool triggerActive =
            (right.flags & bfvr::shared::kControllerHandFlagTriggerActive) != 0;
        SetHeldInput(
            destination,
            kLogicalInputFire,
            UpdateAnalogHeld(
                triggerActive,
                right.triggerValue,
                kControllerTriggerPressThreshold,
                kControllerTriggerReleaseThreshold,
                triggerHeld));

        const bool leftTriggerActive =
            (left.flags & bfvr::shared::kControllerHandFlagTriggerActive) != 0;
        SetHeldInput(
            destination,
            kLogicalInputUse,
            UpdateAnalogHeld(
                leftTriggerActive,
                left.triggerValue,
                kControllerTriggerPressThreshold,
                kControllerTriggerReleaseThreshold,
                leftTriggerHeld));

        // Left squeeze remains reserved exclusively for off-hand support.

        const bool rightSqueezeActive =
            (right.flags & bfvr::shared::kControllerHandFlagSqueezeActive) != 0;
        const bool rightSqueezeWasHeld = rightSqueezeHeld;
        const bool rightSqueezeIsHeld = UpdateAnalogHeld(
            rightSqueezeActive,
            right.squeezeValue,
            kControllerSqueezePressThreshold,
            kControllerSqueezeReleaseThreshold,
            rightSqueezeHeld);
        const bool multiplayerInfantryAltFire =
            multiplayerRoute &&
            controlMode == ControllerControlMode::Infantry;
        if (multiplayerInfantryAltFire)
        {
            const bool altFirePressed =
                rightSqueezeIsHeld && !rightSqueezeWasHeld;
            if (nativeAltFirePressed)
            {
                bfvr::NotifyMultiplayerNativeAltFireInput();
            }
            SetPulseInput(
                destination,
                kLogicalInputAltFire,
                altFirePressed);
            if (altFirePressed &&
                InterlockedCompareExchange(
                    &firstMultiplayerInfantryAltFirePulseLogged,
                    1,
                    0) == 0)
            {
                WriteLog(
                    L"Multiplayer infantry right grip emitted one alt-fire action pulse on its hysteresis-filtered press edge. Native BF1942 scope/secondary-fire state now owns the lifetime; holding the grip cannot redispatch the toggle every PlayerAction frame.");
            }
            if (altFirePressed)
            {
                bfvr::NotifyMultiplayerInfantryAltFirePulse();
            }
        }
        else
        {
            SetHeldInput(
                destination,
                kLogicalInputAltFire,
                rightSqueezeIsHeld);
        }

        const float vehiclePitch =
            (IsHandButtonPressed(
                left,
                bfvr::shared::kControllerHandButtonThumbstick)
                ? 1.0F
                : 0.0F) +
            (IsHandButtonPressed(
                right,
                bfvr::shared::kControllerHandButtonThumbstick)
                ? -1.0F
                : 0.0F);
        AddAxisInput(destination, kLogicalInputPitch, vehiclePitch);

        const bool pronePressed = TakeRisingEdge(
            IsHandButtonPressed(
                left,
                bfvr::shared::kControllerHandButtonPrimary),
            leftPrimaryWasDown);
        if (crouchTogglePressed)
        {
            crouchToggled = !crouchToggled;
        }
        if (jumpAndParachutePressed || pronePressed)
        {
            crouchToggled = false;
        }
        SetPulseInput(
            destination,
            kLogicalInputAction,
            jumpAndParachutePressed);
        SetPulseInput(
            destination,
            kLogicalInputMenuSelect9,
            jumpAndParachutePressed);
        SetPulseInput(destination, kLogicalInputProne, pronePressed);
        SetHeldInput(destination, kLogicalInputCrouch, crouchToggled);
        SetPulseInput(
            destination,
            kLogicalInputReload,
            TakeRisingEdge(
                IsHandButtonPressed(right, bfvr::shared::kControllerHandButtonSecondary),
                rightSecondaryWasDown));
    }

    void OverlayCurrentLocalAliveFrame(
        float* destination,
        void* candidatePlayer,
        bool multiplayerRoute) noexcept
    {
        if (destination == nullptr)
        {
            InterlockedIncrement(&missingDestinationFrames);
            ResetControllerState();
            ReportGateDiagnostics();
            return;
        }
        if (!IsCurrentLocalAlivePlayer(candidatePlayer))
        {
            // The native loop also builds remote-player frames between local
            // frames. They must not reset the local controller button/axis
            // state while the local player remains active.
            ReportGateDiagnostics();
            return;
        }
        bfvr::D3D8RuntimeControllerSample sample = {};
        bfvr::D3D8RuntimeView matchingHead = {};
        LONG generation = 0;
        if (!bfvr::ReadFreshAcceptedControllerInput(
                sample,
                generation,
                kControllerSampleMaximumAgeMs))
        {
            InterlockedIncrement(&staleOrMissingControllerFrames);
            ResetControllerState();
            ReportGateDiagnostics();
            return;
        }
        bfvr::D3D8RuntimeControllerSample trackingSample = {};
        LONG trackingGeneration = 0;
        const bool matchingHeadTracked =
            bfvr::ReadFreshAcceptedWeaponTracking(
                trackingSample,
                matchingHead,
                trackingGeneration,
                kControllerSampleMaximumAgeMs) &&
            trackingGeneration == generation;
        InterlockedIncrement(&freshControllerFrames);
        const bfvr::D3D8RuntimeControllerHand& left =
            sample.hands[kControllerHandLeft];
        const bfvr::D3D8RuntimeControllerHand& right =
            sample.hands[kControllerHandRight];
        const ControllerControlMode controlMode =
            ReadCurrentControlMode(candidatePlayer, multiplayerRoute);
        UpdateControllerControlMode(controlMode);
        __try
        {
            ApplyControllerControls(
                destination,
                left,
                right,
                matchingHead,
                matchingHeadTracked,
                controlMode,
                sample.predictedDisplayTime,
                multiplayerRoute);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            InterlockedIncrement(&unwritableDestinationFrames);
            ResetControllerState();
            ReportGateDiagnostics();
            return;
        }

        InterlockedIncrement(&appliedFrames);
        if (multiplayerRoute)
        {
            InterlockedIncrement(&multiplayerAppliedFrames);
            if (InterlockedCompareExchange(
                    &firstMultiplayerEligibleFrameLogged,
                    1,
                    0) == 0)
            {
                WriteLog(
                    L"Controller input overlay accepted its first prefix-verified multiplayer local frame before PlayerAction encoding: callerReturn=0x004B8E53 candidate=%p matched manager local BFPlayer, alive byte +0xA9 was set, and a fresh focused controller sample was applied to the temporary 55-slot frame before native axis/button quantization.",
                    candidatePlayer);
            }
        }

        if (InterlockedCompareExchange(&firstEligibleFrameLogged, 1, 0) == 0)
        {
            WriteLog(
                L"Controller input overlay accepted its first fresh focused local frame. Infantry layout remains: left stick move; right stick smooth-turn, up jump+parachute, down crouch toggle. Non-default surface control objects use full-direction left throttle/steer plus complementary right-stick and mirrored right-grip-motion turret/station aim; VCAir objects use analogue left throttle/roll and right yaw/pitch (stick up dives). Stick clicks retain vehicle pitch hatch/dive actions. Right trigger fire; right grip emits one multiplayer-infantry alt-fire pulse per press while offline and non-default vehicle/mounted routes retain held alt-fire; left trigger use; A Quick Menu; B reload; X prone; Y native paired scoreboard; left grip off-hand support. Unknown control transitions enable no vehicle-only stick mapping. Logical actions remain independent of configurable keyboard/mouse bindings.");
        }
    }

    void ReportGateDiagnostics() noexcept
    {
        if (InterlockedCompareExchange(&firstEligibleFrameLogged, 0, 0) != 0)
        {
            return;
        }
        const DWORD now = GetTickCount();
        const LONG previousTick =
            InterlockedCompareExchange(&lastGateDiagnosticsAt, 0, 0);
        if (static_cast<DWORD>(now - static_cast<DWORD>(previousTick)) < 1000 ||
            InterlockedCompareExchange(
                &lastGateDiagnosticsAt,
                static_cast<LONG>(now),
                previousTick) != previousTick)
        {
            return;
        }
        const LONG normalizers =
            InterlockedExchange(&normalizerCalls, 0);
        if (normalizers == 0)
        {
            return;
        }
        WriteLog(
            L"Controller input overlay gate report (one second): frameBuilders=%ld multiplayerEncoders=%ld normalizers=%ld multiplayerNormalizers=%ld localPlayer=%ld missingScope=%ld missingManager=%ld otherPlayer=%ld dead=%ld unreadable=%ld noDestination=%ld controllerUnavailable=%ld freshController=%ld writeFault=%ld applied=%ld multiplayerApplied=%ld.",
            InterlockedExchange(&frameBuilderCalls, 0),
            InterlockedExchange(&multiplayerEncoderCalls, 0),
            normalizers,
            InterlockedExchange(&multiplayerNormalizerCalls, 0),
            InterlockedExchange(&eligibleLocalPlayerFrames, 0),
            InterlockedExchange(&missingScopedPlayerFrames, 0),
            InterlockedExchange(&missingManagerFrames, 0),
            InterlockedExchange(&nonLocalPlayerFrames, 0),
            InterlockedExchange(&notAliveFrames, 0),
            InterlockedExchange(&unreadablePlayerFrames, 0),
            InterlockedExchange(&missingDestinationFrames, 0),
            InterlockedExchange(&staleOrMissingControllerFrames, 0),
            InterlockedExchange(&freshControllerFrames, 0),
            InterlockedExchange(&unwritableDestinationFrames, 0),
            InterlockedExchange(&appliedFrames, 0),
            InterlockedExchange(&multiplayerAppliedFrames, 0));
    }

    void ResetControllerState() noexcept
    {
        triggerHeld = false;
        leftTriggerHeld = false;
        rightSqueezeHeld = false;
        nativeAltFireWasDown = false;
        leftPrimaryWasDown = false;
        rightSecondaryWasDown = false;
        rightStickVerticalDirection = 0;
        crouchToggled = false;
        bfvr::stereo::ResetVehicleMotionAim(surfaceVehicleMotionAim);
        bfvr::stereo::ResetDigitalLocomotion(infantryMovementDirection);
        activeControlMode = ControllerControlMode::Unknown;
        snapTurnArmed = true;
    }

    void RemoveHooks()
    {
        if (gameInputQueryEnabled)
        {
            MH_DisableHook(gameInputQueryTarget);
            gameInputQueryEnabled = false;
        }
        if (normalizerEnabled)
        {
            MH_DisableHook(normalizerTarget);
            normalizerEnabled = false;
        }
        if (playerActionEncoderEnabled)
        {
            MH_DisableHook(playerActionEncoderTarget);
            playerActionEncoderEnabled = false;
        }
        if (frameBuilderEnabled)
        {
            MH_DisableHook(frameBuilderTarget);
            frameBuilderEnabled = false;
        }
        if (normalizerCreated)
        {
            MH_RemoveHook(normalizerTarget);
            normalizerCreated = false;
        }
        if (playerActionEncoderCreated)
        {
            MH_RemoveHook(playerActionEncoderTarget);
            playerActionEncoderCreated = false;
        }
        if (gameInputQueryCreated)
        {
            MH_RemoveHook(gameInputQueryTarget);
            gameInputQueryCreated = false;
        }
        if (frameBuilderCreated)
        {
            MH_RemoveHook(frameBuilderTarget);
            frameBuilderCreated = false;
        }
        if (active == this)
        {
            active = nullptr;
        }
        originalFrameBuilder = nullptr;
        originalPlayerActionEncode = nullptr;
        originalNormalize = nullptr;
        originalGameInputQuery = nullptr;
        hudManagerSetShowScoreboard = nullptr;
        ResetControllerState();
        controllerScoreboardWasHeld = false;
        if (ownsMinHook)
        {
            MH_Uninitialize();
            ownsMinHook = false;
        }
    }

    void RefreshUserSettings() noexcept
    {
        const ULONGLONG now = GetTickCount64();
        if (now < nextUserSettingsPollAt)
        {
            return;
        }
        nextUserSettingsPollAt = now + kUserSettingsPollIntervalMs;
        auto& runtime = bfvr::settings::ProcessUserSettingsRuntime();
        if (!runtime.ReloadIfChanged())
        {
            return;
        }
        const bfvr::settings::UserSettingsValues updated =
            bfvr::settings::DecodeUserSettings(runtime.Current());
        if (updated == userSettings)
        {
            return;
        }
        if (updated.movementDirection != userSettings.movementDirection)
        {
            bfvr::stereo::ResetDigitalLocomotion(
                infantryMovementDirection);
        }
        userSettings = updated;
        WriteLog(
            L"Controller input applied updated UserConfig values: turnMode=%ls infantryTurnSpeed=%lu%% movementDirection=%lu invertFlightPitch=%d invertTurretPitch=%d invertTurretYaw=%d.",
            userSettings.artificialTurnMode ==
                    bfvr::settings::ArtificialTurnMode::Snap
                ? L"snap"
                : L"smooth",
            static_cast<unsigned long>(
                userSettings.infantryTurnSpeedPercent),
            static_cast<unsigned long>(userSettings.movementDirection),
            userSettings.invertFlightPitch ? 1 : 0,
            userSettings.invertTurretPitch ? 1 : 0,
            userSettings.invertTurretYaw ? 1 : 0);
    }

    void WriteLog(const wchar_t* format, ...) const
    {
        if (appendLog == nullptr)
        {
            return;
        }
        std::array<wchar_t, 900> message = {};
        va_list arguments;
        va_start(arguments, format);
        _vsnwprintf_s(
            message.data(),
            message.size(),
            _TRUNCATE,
            format,
            arguments);
        va_end(arguments);
        appendLog(message.data());
    }

    static ControllerInputOverlay* active;
    static __declspec(thread) void* scopedPlayer;

    std::byte* gameImage = nullptr;
    void (*appendLog)(const wchar_t* message) = nullptr;
    void* frameBuilderTarget = nullptr;
    void* playerActionEncoderTarget = nullptr;
    void* normalizerTarget = nullptr;
    const std::byte* multiplayerEncoderCallSite = nullptr;
    const std::byte* multiplayerNormalizerCallSite = nullptr;
    void* gameInputQueryTarget = nullptr;
    FrameBuilderFn originalFrameBuilder = nullptr;
    PlayerActionEncodeFn originalPlayerActionEncode = nullptr;
    NormalizeFn originalNormalize = nullptr;
    GameInputQueryFn originalGameInputQuery = nullptr;
    HudManagerSetShowScoreboardFn hudManagerSetShowScoreboard = nullptr;
    volatile LONG started = 0;
    volatile LONG firstEligibleFrameLogged = 0;
    volatile LONG firstMultiplayerEligibleFrameLogged = 0;
    volatile LONG firstMultiplayerControlModeLogged = 0;
    volatile LONG firstSurfaceDriveInputLogged = 0;
    volatile LONG firstMultiplayerInfantryAltFirePulseLogged = 0;
    volatile LONG firstScoreboardQueryOverrideLogged = 0;
    volatile LONG firstPlayerScoreboardCallLogged = 0;
    volatile LONG firstPlayerScoreboardFaultLogged = 0;
    volatile LONG firstVehicleModeLogged = 0;
    volatile LONG firstAirModeLogged = 0;
    volatile LONG firstSurfaceMotionAimLogged = 0;
    volatile LONG lastGateDiagnosticsAt = 0;
    volatile LONG frameBuilderCalls = 0;
    volatile LONG normalizerCalls = 0;
    volatile LONG multiplayerEncoderCalls = 0;
    volatile LONG multiplayerNormalizerCalls = 0;
    volatile LONG eligibleLocalPlayerFrames = 0;
    volatile LONG missingScopedPlayerFrames = 0;
    volatile LONG missingManagerFrames = 0;
    volatile LONG nonLocalPlayerFrames = 0;
    volatile LONG notAliveFrames = 0;
    volatile LONG unreadablePlayerFrames = 0;
    volatile LONG missingDestinationFrames = 0;
    volatile LONG staleOrMissingControllerFrames = 0;
    volatile LONG freshControllerFrames = 0;
    volatile LONG unwritableDestinationFrames = 0;
    volatile LONG appliedFrames = 0;
    volatile LONG multiplayerAppliedFrames = 0;
    bool triggerHeld = false;
    bool leftTriggerHeld = false;
    bool rightSqueezeHeld = false;
    bool nativeAltFireWasDown = false;
    bool leftPrimaryWasDown = false;
    bool rightSecondaryWasDown = false;
    int rightStickVerticalDirection = 0;
    bool crouchToggled = false;
    bfvr::stereo::VehicleMotionAimTracker surfaceVehicleMotionAim = {};
    bfvr::stereo::DigitalLocomotionState infantryMovementDirection = {};
    ControllerControlMode activeControlMode = ControllerControlMode::Unknown;
    bool controllerScoreboardWasHeld = false;
    bool snapTurnArmed = true;
    bfvr::settings::UserSettingsValues userSettings = {};
    ULONGLONG nextUserSettingsPollAt = 0;
    bool ownsMinHook = false;
    bool frameBuilderCreated = false;
    bool playerActionEncoderCreated = false;
    bool normalizerCreated = false;
    bool gameInputQueryCreated = false;
    bool frameBuilderEnabled = false;
    bool playerActionEncoderEnabled = false;
    bool normalizerEnabled = false;
    bool gameInputQueryEnabled = false;
};

ControllerInputOverlay* ControllerInputOverlay::active = nullptr;
__declspec(thread) void* ControllerInputOverlay::scopedPlayer = nullptr;

void __declspec(naked) ControllerInputPlayerActionEncodeHook()
{
    __asm
    {
        // Original __thiscall entry: ECX=compact PlayerAction destination,
        // [ESP+4]=logical PlayerInput frame, and the verified multiplayer
        // caller preserves its current BFPlayer in EBX.
        mov eax, dword ptr [esp]
        mov edx, dword ptr [esp + 4]
        push eax
        push ebx
        push edx
        push ecx
        call ControllerInputPlayerActionEncodeHookImpl
        ret 4
    }
}

void __stdcall ControllerInputPlayerActionEncodeHookImpl(
    void* encodedDestination,
    float* logicalFrame,
    void* callerEbx,
    const void* returnAddress)
{
    ControllerInputOverlay* const overlay = ControllerInputOverlay::active;
    if (overlay == nullptr || overlay->originalPlayerActionEncode == nullptr)
    {
        return;
    }
    const bool multiplayerRoute =
        overlay->gameImage != nullptr &&
        returnAddress == overlay->gameImage + kMultiplayerEncoderReturnRva;
    if (multiplayerRoute)
    {
        InterlockedIncrement(&overlay->multiplayerEncoderCalls);
        // This must happen before the original encoder. FUN_00483E70 converts
        // the first six axes and discrete button mask to the compact record
        // that FUN_004913A0 decodes immediately afterward.
        overlay->OverlayCurrentLocalAliveFrame(
            logicalFrame,
            callerEbx,
            true);
    }
    overlay->originalPlayerActionEncode(encodedDestination, logicalFrame);
}

DWORD __declspec(naked) ControllerInputNormalizeHook()
{
    __asm
    {
        // Original __thiscall entry: ECX=raw source, [ESP]=caller return,
        // [ESP+4]=temporary logical-frame destination. FUN_004B8D70 keeps
        // its currently resolved BFPlayer in callee-saved EBX across the
        // normalizer call. Preserve that register as evidence; the C++
        // implementation admits it only for the exact verified caller.
        mov eax, dword ptr [esp]
        mov edx, dword ptr [esp + 4]
        push eax
        push ebx
        push edx
        push ecx
        call ControllerInputNormalizeHookImpl
        ret 4
    }
}

DWORD __stdcall ControllerInputNormalizeHookImpl(
    void* rawSource,
    float* destination,
    void* callerEbx,
    const void* returnAddress)
{
    static_cast<void>(callerEbx);
    ControllerInputOverlay* const overlay = ControllerInputOverlay::active;
    if (overlay == nullptr || overlay->originalNormalize == nullptr)
    {
        return 0;
    }
    InterlockedIncrement(&overlay->normalizerCalls);
    const DWORD result = overlay->originalNormalize(rawSource, destination);
    const bool multiplayerRoute =
        overlay->gameImage != nullptr &&
        returnAddress ==
            overlay->gameImage + kMultiplayerNormalizerReturnRva;
    if (multiplayerRoute)
    {
        InterlockedIncrement(&overlay->multiplayerNormalizerCalls);
    }
    if (!multiplayerRoute)
    {
        overlay->OverlayCurrentLocalAliveFrame(
            destination,
            ControllerInputOverlay::scopedPlayer,
            false);
    }
    return result;
}

ControllerInputOverlay g_controllerInputOverlay = {};
} // namespace

namespace bfvr
{
void StartControllerInputOverlay(
    void* gameImage,
    void (*appendLog)(const wchar_t* message))
{
    g_controllerInputOverlay.Start(gameImage, appendLog);
}

void StopControllerInputOverlay()
{
    g_controllerInputOverlay.Stop();
}
} // namespace bfvr
