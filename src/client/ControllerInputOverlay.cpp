#include "client/ControllerInputOverlay.h"

#include "client/ControllerInputCache.h"
#include "presenter/SharedPresentationProtocol.h"

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
constexpr std::ptrdiff_t kNormalizerRva = 0x000913A0;
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
constexpr float kTurnStickResponseExponent = 1.65F;
constexpr float kVehicleAimStickResponseExponent = 1.35F;

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
constexpr BYTE kNormalizerPrefix[] = {
    0x83, 0xEC, 0x10, 0x89, 0x4C, 0x24, 0x00, 0x33,
    0xC0, 0x8D, 0xA4, 0x24, 0x00, 0x00, 0x00, 0x00};
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

class ControllerInputOverlay
{
public:
    using FrameBuilderFn = DWORD(__thiscall*)(void* owner, void* player, void* context);
    using NormalizeFn = DWORD(__thiscall*)(void* rawSource, float* destination);
    using GameInputQueryFn = DWORD(__thiscall*)(void* gameInput, DWORD inputId);
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
        frameBuilderTarget = gameImage == nullptr
            ? nullptr
            : gameImage + kFrameBuilderRva;
        normalizerTarget = gameImage == nullptr
            ? nullptr
            : gameImage + kNormalizerRva;
        gameInputQueryTarget = gameImage == nullptr
            ? nullptr
            : gameImage + kGameInputQueryRva;
        hudManagerSetShowScoreboard = gameImage == nullptr
            ? nullptr
            : reinterpret_cast<HudManagerSetShowScoreboardFn>(
                gameImage + kHudManagerSetShowScoreboardRva);
        if (!HasExpectedPrefix(
                frameBuilderTarget,
                kFrameBuilderPrefix,
                sizeof(kFrameBuilderPrefix)) ||
            !HasExpectedPrefix(
                normalizerTarget,
                kNormalizerPrefix,
                sizeof(kNormalizerPrefix)) ||
            !HasExpectedPrefix(
                gameInputQueryTarget,
                kGameInputQueryPrefix,
                sizeof(kGameInputQueryPrefix)) ||
            !HasExpectedPrefix(
                reinterpret_cast<const void*>(hudManagerSetShowScoreboard),
                kHudManagerSetShowScoreboardPrefix,
                sizeof(kHudManagerSetShowScoreboardPrefix)))
        {
            WriteLog(
                L"Controller input overlay rejected profiled targets frameBuilder=%p normalizer=%p gameInputQuery=%p setShowScoreboard=%p: one or more prefixes differ.",
                frameBuilderTarget,
                normalizerTarget,
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
        const MH_STATUS createNormalizer = createFrameBuilder == MH_OK
            ? MH_CreateHook(
                normalizerTarget,
                reinterpret_cast<LPVOID>(&ControllerInputOverlay::NormalizeHook),
                reinterpret_cast<LPVOID*>(&originalNormalize))
            : MH_ERROR_NOT_CREATED;
        const MH_STATUS createGameInputQuery = createNormalizer == MH_OK
            ? MH_CreateHook(
                gameInputQueryTarget,
                reinterpret_cast<LPVOID>(&ControllerInputOverlay::GameInputQueryHook),
                reinterpret_cast<LPVOID*>(&originalGameInputQuery))
            : MH_ERROR_NOT_CREATED;
        frameBuilderCreated = createFrameBuilder == MH_OK;
        normalizerCreated = createNormalizer == MH_OK;
        gameInputQueryCreated = createGameInputQuery == MH_OK;
        if (createFrameBuilder != MH_OK || createNormalizer != MH_OK ||
            createGameInputQuery != MH_OK ||
            originalFrameBuilder == nullptr || originalNormalize == nullptr ||
            originalGameInputQuery == nullptr)
        {
            WriteLog(
                L"Controller input overlay could not create its native-frame hooks (frameBuilder=%d normalizer=%d gameInputQuery=%d).",
                static_cast<int>(createFrameBuilder),
                static_cast<int>(createNormalizer),
                static_cast<int>(createGameInputQuery));
            RemoveHooks();
            return;
        }
        active = this;
        const MH_STATUS enableFrameBuilder = MH_EnableHook(frameBuilderTarget);
        const MH_STATUS enableNormalizer = enableFrameBuilder == MH_OK
            ? MH_EnableHook(normalizerTarget)
            : MH_ERROR_ENABLED;
        const MH_STATUS enableGameInputQuery = enableNormalizer == MH_OK
            ? MH_EnableHook(gameInputQueryTarget)
            : MH_ERROR_ENABLED;
        frameBuilderEnabled = enableFrameBuilder == MH_OK;
        normalizerEnabled = enableNormalizer == MH_OK;
        gameInputQueryEnabled = enableGameInputQuery == MH_OK;
        if (enableFrameBuilder != MH_OK || enableNormalizer != MH_OK ||
            enableGameInputQuery != MH_OK)
        {
            WriteLog(
                L"Controller input overlay could not enable its native-frame hooks (frameBuilder=%d normalizer=%d gameInputQuery=%d).",
                static_cast<int>(enableFrameBuilder),
                static_cast<int>(enableNormalizer),
                static_cast<int>(enableGameInputQuery));
            RemoveHooks();
            return;
        }
        WriteLog(
            L"Controller input overlay armed at 0x004B6A30/0x004913A0, native GameInput action query 0x00446490, and HudManager scoreboard state machine 0x006A9C90. At the exact Setup scoreboard branch, controller Y submits the missing player-side source-0 state, then makes global logical action 35 true so BF1942 performs its ordinary paired dispatch; keyboard/mouse bindings remain native.");
    }

    void Stop()
    {
        RemoveHooks();
        InterlockedExchange(&started, 0);
    }

private:
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

    static DWORD __fastcall NormalizeHook(
        void* rawSource,
        void*,
        float* destination)
    {
        ControllerInputOverlay* const overlay = active;
        if (overlay == nullptr || overlay->originalNormalize == nullptr)
        {
            return 0;
        }
        InterlockedIncrement(&overlay->normalizerCalls);
        const DWORD result = overlay->originalNormalize(rawSource, destination);
        overlay->OverlayCurrentLocalAliveFrame(destination);
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

    bool IsCurrentLocalAlivePlayer() noexcept
    {
        if (gameImage == nullptr || scopedPlayer == nullptr)
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
            if (localPlayer == nullptr || localPlayer != scopedPlayer)
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

    ControllerControlMode ReadCurrentControlMode() noexcept
    {
        if (scopedPlayer == nullptr)
        {
            return ControllerControlMode::Unknown;
        }

        void* currentControlObject = nullptr;
        void* defaultControlObject = nullptr;
        __try
        {
            const auto* const playerBytes =
                static_cast<const std::byte*>(scopedPlayer);
            currentControlObject = *reinterpret_cast<void* const*>(
                playerBytes + kBFPlayerCurrentControlObjectOffset);
            defaultControlObject = *reinterpret_cast<void* const*>(
                playerBytes + kBFPlayerDefaultControlObjectOffset);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return ControllerControlMode::Unknown;
        }

        if (currentControlObject == nullptr || defaultControlObject == nullptr)
        {
            return ControllerControlMode::Unknown;
        }
        if (currentControlObject == defaultControlObject)
        {
            return ControllerControlMode::Infantry;
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
        return ControllerControlMode::SurfaceVehicle;
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
        ControllerControlMode controlMode) noexcept
    {
        const bool quickMenuHeld = IsHandButtonPressed(
            right,
            bfvr::shared::kControllerHandButtonPrimary);
        bool jumpAndParachutePressed = false;
        bool crouchTogglePressed = false;
        bool mouseLookEnabled = false;

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
                const float movementX =
                    ApplyThumbstickDeadzone(left.thumbstickX);
                const float movementY =
                    ApplyThumbstickDeadzone(left.thumbstickY);
                const float movementMagnitude = std::min(
                    std::hypot(movementX, movementY),
                    1.0F);
                const bool moving = movementMagnitude != 0.0F;
                SetHeldInput(
                    destination,
                    kLogicalInputWalk,
                    moving &&
                        movementMagnitude < kWalkStickMagnitudeThreshold);
                if (movementX != 0.0F)
                {
                    AddAxisInput(
                        destination,
                        kLogicalInputYaw,
                        std::copysign(1.0F, movementX));
                }
                if (movementY != 0.0F)
                {
                    AddAxisInput(
                        destination,
                        kLogicalInputThrottle,
                        std::copysign(1.0F, movementY));
                }
            }

            if ((right.flags &
                    bfvr::shared::kControllerHandFlagThumbstickActive) != 0)
            {
                const float turnInput =
                    ApplyThumbstickResponse(
                        right.thumbstickX,
                        kTurnStickResponseExponent) *
                    kStickTurnInputPerFrame;
                if (turnInput != 0.0F)
                {
                    AddAxisInput(
                        destination,
                        kLogicalInputMouseLookX,
                        turnInput);
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
                    // c_PIThrottle as gas/reverse. Preserve the post-deadzone
                    // analogue magnitude instead of reducing either axis to
                    // an on/off key value.
                    AddAxisInput(
                        destination,
                        kLogicalInputYaw,
                        ApplyThumbstickDeadzone(left.thumbstickX));
                    AddAxisInput(
                        destination,
                        kLogicalInputThrottle,
                        ApplyThumbstickDeadzone(left.thumbstickY));
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
                            kVehicleAimStickResponseExponent));
                    AddAxisInput(
                        destination,
                        kLogicalInputMouseLookY,
                        ApplyThumbstickResponse(
                            right.thumbstickY,
                            kVehicleAimStickResponseExponent));
                }
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
                        ApplyThumbstickDeadzone(right.thumbstickY));
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
        SetHeldInput(
            destination,
            kLogicalInputAltFire,
            UpdateAnalogHeld(
                rightSqueezeActive,
                right.squeezeValue,
                kControllerSqueezePressThreshold,
                kControllerSqueezeReleaseThreshold,
                rightSqueezeHeld));

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

    void OverlayCurrentLocalAliveFrame(float* destination) noexcept
    {
        if (destination == nullptr)
        {
            InterlockedIncrement(&missingDestinationFrames);
            ResetControllerState();
            ReportGateDiagnostics();
            return;
        }
        if (!IsCurrentLocalAlivePlayer())
        {
            // The native loop also builds remote-player frames between local
            // frames. They must not reset the local controller button/axis
            // state while the local player remains active.
            ReportGateDiagnostics();
            return;
        }
        bfvr::D3D8RuntimeControllerSample sample = {};
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
        InterlockedIncrement(&freshControllerFrames);
        const bfvr::D3D8RuntimeControllerHand& left =
            sample.hands[kControllerHandLeft];
        const bfvr::D3D8RuntimeControllerHand& right =
            sample.hands[kControllerHandRight];
        const ControllerControlMode controlMode = ReadCurrentControlMode();
        UpdateControllerControlMode(controlMode);
        __try
        {
            ApplyControllerControls(
                destination,
                left,
                right,
                controlMode);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            InterlockedIncrement(&unwritableDestinationFrames);
            ResetControllerState();
            ReportGateDiagnostics();
            return;
        }

        InterlockedIncrement(&appliedFrames);

        if (InterlockedCompareExchange(&firstEligibleFrameLogged, 1, 0) == 0)
        {
            WriteLog(
                L"Controller input overlay accepted its first fresh focused local frame. Infantry layout remains: left stick move; right stick smooth-turn, up jump+parachute, down crouch toggle. Non-default surface control objects use analogue left throttle/steer and right turret/station aim; VCAir objects use analogue left throttle/roll and right yaw/pitch (stick up dives). Stick clicks retain vehicle pitch hatch/dive actions. Right trigger fire; right grip alt-fire; left trigger use; A Quick Menu; B reload; X prone; Y native paired scoreboard; left grip off-hand support. Unknown control transitions enable no vehicle-only stick mapping. Logical actions remain independent of configurable keyboard/mouse bindings.");
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
            L"Controller input overlay gate report (one second): frameBuilders=%ld normalizers=%ld localPlayer=%ld missingScope=%ld missingManager=%ld otherPlayer=%ld dead=%ld unreadable=%ld noDestination=%ld controllerUnavailable=%ld freshController=%ld writeFault=%ld applied=%ld.",
            InterlockedExchange(&frameBuilderCalls, 0),
            normalizers,
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
            InterlockedExchange(&appliedFrames, 0));
    }

    void ResetControllerState() noexcept
    {
        triggerHeld = false;
        leftTriggerHeld = false;
        rightSqueezeHeld = false;
        leftPrimaryWasDown = false;
        rightSecondaryWasDown = false;
        rightStickVerticalDirection = 0;
        crouchToggled = false;
        activeControlMode = ControllerControlMode::Unknown;
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
    void* normalizerTarget = nullptr;
    void* gameInputQueryTarget = nullptr;
    FrameBuilderFn originalFrameBuilder = nullptr;
    NormalizeFn originalNormalize = nullptr;
    GameInputQueryFn originalGameInputQuery = nullptr;
    HudManagerSetShowScoreboardFn hudManagerSetShowScoreboard = nullptr;
    volatile LONG started = 0;
    volatile LONG firstEligibleFrameLogged = 0;
    volatile LONG firstScoreboardQueryOverrideLogged = 0;
    volatile LONG firstPlayerScoreboardCallLogged = 0;
    volatile LONG firstPlayerScoreboardFaultLogged = 0;
    volatile LONG firstVehicleModeLogged = 0;
    volatile LONG firstAirModeLogged = 0;
    volatile LONG lastGateDiagnosticsAt = 0;
    volatile LONG frameBuilderCalls = 0;
    volatile LONG normalizerCalls = 0;
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
    bool triggerHeld = false;
    bool leftTriggerHeld = false;
    bool rightSqueezeHeld = false;
    bool leftPrimaryWasDown = false;
    bool rightSecondaryWasDown = false;
    int rightStickVerticalDirection = 0;
    bool crouchToggled = false;
    ControllerControlMode activeControlMode = ControllerControlMode::Unknown;
    bool controllerScoreboardWasHeld = false;
    bool ownsMinHook = false;
    bool frameBuilderCreated = false;
    bool normalizerCreated = false;
    bool gameInputQueryCreated = false;
    bool frameBuilderEnabled = false;
    bool normalizerEnabled = false;
    bool gameInputQueryEnabled = false;
};

ControllerInputOverlay* ControllerInputOverlay::active = nullptr;
__declspec(thread) void* ControllerInputOverlay::scopedPlayer = nullptr;
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
