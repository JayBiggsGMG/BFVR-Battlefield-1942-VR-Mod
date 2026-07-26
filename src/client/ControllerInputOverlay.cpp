#include "client/ControllerInputOverlay.h"

#include "client/ControllerInputCache.h"
#include "presenter/SharedPresentationProtocol.h"

#include <MinHook.h>

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
constexpr std::ptrdiff_t kPlayerManagerGlobalRva = 0x0057D76C;
constexpr std::size_t kPlayerManagerLocalPlayerOffset = 0x54;
constexpr std::size_t kBFPlayerIsAliveOffset = 0xA9;
constexpr std::size_t kLogicalInputEnableMaskLowOffset = 0xE0;
constexpr std::size_t kLogicalInputEnableMaskHighOffset = 0xE4;
constexpr DWORD kLogicalInputYaw = 0;
constexpr DWORD kLogicalInputThrottle = 3;
constexpr DWORD kLogicalInputMouseLookX = 4;
constexpr DWORD kLogicalInputMouseLookY = 5;
constexpr DWORD kLogicalInputFire = 8;
constexpr DWORD kLogicalInputAction = 9;
constexpr DWORD kLogicalInputUse = 10;
constexpr DWORD kLogicalInputMouseLook = 11;
constexpr DWORD kLogicalInputWalk = 12;
constexpr DWORD kLogicalInputAltFire = 23;
constexpr DWORD kLogicalInputReload = 24;
constexpr DWORD kLogicalInputProne = 28;
constexpr DWORD kLogicalInputCrouch = 29;
constexpr DWORD kLogicalInputCount = 55;
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
// First live aim validation found the native mouse-look sign opposite to the
// controller orientation convention and the initial gain too weak for normal
// right-hand aiming. These remain local temporary PlayerInput calibration.
constexpr float kMaximumAxisDelta = 1.0F;
constexpr float kAxisRadiansToInput = 10.67F;
constexpr float kAxisDeadzoneRadians = 0.0015F;

constexpr BYTE kFrameBuilderPrefix[] = {
    0x81, 0xEC, 0x34, 0x01, 0x00, 0x00, 0x53, 0x55,
    0x8B, 0xAC, 0x24, 0x40, 0x01, 0x00, 0x00, 0x33};
constexpr BYTE kNormalizerPrefix[] = {
    0x83, 0xEC, 0x10, 0x89, 0x4C, 0x24, 0x00, 0x33,
    0xC0, 0x8D, 0xA4, 0x24, 0x00, 0x00, 0x00, 0x00};

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

struct Quaternion
{
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;
};

bool NormalizeQuaternion(Quaternion& value) noexcept
{
    const float lengthSquared =
        value.x * value.x + value.y * value.y +
        value.z * value.z + value.w * value.w;
    if (!std::isfinite(lengthSquared) || lengthSquared < 0.25F ||
        lengthSquared > 2.25F)
    {
        return false;
    }
    const float reciprocalLength = 1.0F / std::sqrt(lengthSquared);
    value.x *= reciprocalLength;
    value.y *= reciprocalLength;
    value.z *= reciprocalLength;
    value.w *= reciprocalLength;
    return true;
}

Quaternion Conjugate(const Quaternion& value) noexcept
{
    return {-value.x, -value.y, -value.z, value.w};
}

Quaternion Multiply(const Quaternion& first, const Quaternion& second) noexcept
{
    return {
        first.w * second.x + first.x * second.w +
            first.y * second.z - first.z * second.y,
        first.w * second.y - first.x * second.z +
            first.y * second.w + first.z * second.x,
        first.w * second.z + first.x * second.y -
            first.y * second.x + first.z * second.w,
        first.w * second.w - first.x * second.x -
            first.y * second.y - first.z * second.z};
}

float ClampAxis(float value) noexcept
{
    if (!std::isfinite(value))
    {
        return 0.0F;
    }
    return std::clamp(value, -kMaximumAxisDelta, kMaximumAxisDelta);
}

float ApplyDeadzone(float value) noexcept
{
    return std::fabs(value) < kAxisDeadzoneRadians ? 0.0F : value;
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
        if (!HasExpectedPrefix(
                frameBuilderTarget,
                kFrameBuilderPrefix,
                sizeof(kFrameBuilderPrefix)) ||
            !HasExpectedPrefix(
                normalizerTarget,
                kNormalizerPrefix,
                sizeof(kNormalizerPrefix)))
        {
            WriteLog(
                L"Controller input overlay rejected profiled targets frameBuilder=%p normalizer=%p: one or both prefixes differ.",
                frameBuilderTarget,
                normalizerTarget);
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
        frameBuilderCreated = createFrameBuilder == MH_OK;
        normalizerCreated = createNormalizer == MH_OK;
        if (createFrameBuilder != MH_OK || createNormalizer != MH_OK ||
            originalFrameBuilder == nullptr || originalNormalize == nullptr)
        {
            WriteLog(
                L"Controller input overlay could not create its native-frame hooks (frameBuilder=%d normalizer=%d).",
                static_cast<int>(createFrameBuilder),
                static_cast<int>(createNormalizer));
            RemoveHooks();
            return;
        }
        active = this;
        const MH_STATUS enableFrameBuilder = MH_EnableHook(frameBuilderTarget);
        const MH_STATUS enableNormalizer = enableFrameBuilder == MH_OK
            ? MH_EnableHook(normalizerTarget)
            : MH_ERROR_ENABLED;
        frameBuilderEnabled = enableFrameBuilder == MH_OK;
        normalizerEnabled = enableNormalizer == MH_OK;
        if (enableFrameBuilder != MH_OK || enableNormalizer != MH_OK)
        {
            WriteLog(
                L"Controller input overlay could not enable its native-frame hooks (frameBuilder=%d normalizer=%d).",
                static_cast<int>(enableFrameBuilder),
                static_cast<int>(enableNormalizer));
            RemoveHooks();
            return;
        }
        WriteLog(
            L"Controller input overlay armed at 0x004B6A30/0x004913A0. It changes only the current alive local PlayerInput frame after normal construction, and only with a fresh focused OpenXR sample; keyboard/mouse and all game action dispatch remain native.");
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

    bool GetRightAimDelta(
        const bfvr::D3D8RuntimeControllerHand& right,
        LONG generation,
        float& yawInput,
        float& pitchInput) noexcept
    {
        yawInput = 0.0F;
        pitchInput = 0.0F;
        const DWORD requiredAimFlags =
            bfvr::shared::kControllerHandFlagAimActive |
            bfvr::shared::kControllerHandFlagAimOrientationValid;
        if ((right.flags & requiredAimFlags) != requiredAimFlags)
        {
            InterlockedIncrement(&rightAimUnavailableFrames);
            ResetAimState();
            return false;
        }

        Quaternion orientation = {
            right.aimPose.orientationX,
            right.aimPose.orientationY,
            right.aimPose.orientationZ,
            right.aimPose.orientationW};
        if (!NormalizeQuaternion(orientation))
        {
            InterlockedIncrement(&invalidOrientationFrames);
            ResetAimState();
            return false;
        }

        if (lastControllerGeneration != generation || !hasLastOrientation)
        {
            if (hasLastOrientation && lastControllerGeneration != generation)
            {
                Quaternion delta = Multiply(orientation, Conjugate(lastOrientation));
                if (NormalizeQuaternion(delta))
                {
                    const float yawRadians = std::atan2(
                        2.0F * (delta.w * delta.y + delta.x * delta.z),
                        1.0F - 2.0F * (delta.y * delta.y + delta.z * delta.z));
                    const float pitchRadians = std::asin(std::clamp(
                        2.0F * (delta.w * delta.x - delta.y * delta.z),
                        -1.0F,
                        1.0F));
                    yawInput = -ClampAxis(
                        ApplyDeadzone(yawRadians) * kAxisRadiansToInput);
                    pitchInput = -ClampAxis(
                        ApplyDeadzone(pitchRadians) * kAxisRadiansToInput);
                }
            }
            lastOrientation = orientation;
            lastControllerGeneration = generation;
            hasLastOrientation = true;
        }
        return true;
    }

    void ApplyInfantryControls(
        float* destination,
        const bfvr::D3D8RuntimeControllerHand& left,
        const bfvr::D3D8RuntimeControllerHand& right,
        bool rightAimAvailable,
        float yawInput,
        float pitchInput) noexcept
    {
        if ((left.flags & bfvr::shared::kControllerHandFlagThumbstickActive) != 0)
        {
            // The active Infantry profile binds D/A to c_PIYaw and W/S to
            // c_PIThrottle, which agrees with OpenXR's +X right / +Y forward.
            // BF1942 does not consume these two axes as analogue walking
            // speed.  Use the verified native c_PIWalk action for the light
            // deflection band and otherwise submit ordinary full-strength
            // directional input, matching the game's keyboard semantics.
            const float movementX = ApplyThumbstickDeadzone(left.thumbstickX);
            const float movementY = ApplyThumbstickDeadzone(left.thumbstickY);
            const float movementMagnitude = std::min(
                std::hypot(movementX, movementY),
                1.0F);
            const bool moving = movementMagnitude != 0.0F;
            SetHeldInput(
                destination,
                kLogicalInputWalk,
                moving && movementMagnitude < kWalkStickMagnitudeThreshold);
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

        bool mouseLookEnabled = false;
        if (rightAimAvailable)
        {
            AddAxisInput(destination, kLogicalInputMouseLookX, yawInput);
            AddAxisInput(destination, kLogicalInputMouseLookY, pitchInput);
            mouseLookEnabled = true;
        }
        if ((right.flags & bfvr::shared::kControllerHandFlagThumbstickActive) != 0)
        {
            const float turnInput =
                ApplyThumbstickResponse(
                    right.thumbstickX,
                    kTurnStickResponseExponent) *
                kStickTurnInputPerFrame;
            if (turnInput != 0.0F)
            {
                AddAxisInput(destination, kLogicalInputMouseLookX, turnInput);
                mouseLookEnabled = true;
            }

            const int verticalDirection = ThumbstickDirection(right.thumbstickY);
            if (verticalDirection != 0 &&
                verticalDirection != rightStickVerticalDirection)
            {
                SetPulseInput(
                    destination,
                    verticalDirection > 0
                        ? kLogicalInputAction
                        : kLogicalInputProne,
                    true);
            }
            rightStickVerticalDirection = verticalDirection;
        }
        else
        {
            rightStickVerticalDirection = 0;
        }
        if (mouseLookEnabled)
        {
            SetHeldInput(destination, kLogicalInputMouseLook, true);
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

        const bool leftSqueezeActive =
            (left.flags & bfvr::shared::kControllerHandFlagSqueezeActive) != 0;
        const bool wasLeftSqueezeHeld = leftSqueezeHeld;
        UpdateAnalogHeld(
            leftSqueezeActive,
            left.squeezeValue,
            kControllerSqueezePressThreshold,
            kControllerSqueezeReleaseThreshold,
            leftSqueezeHeld);
        SetPulseInput(
            destination,
            kLogicalInputProne,
            leftSqueezeHeld && !wasLeftSqueezeHeld);

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

        SetHeldInput(
            destination,
            kLogicalInputUse,
            IsHandButtonPressed(left, bfvr::shared::kControllerHandButtonPrimary));
        SetHeldInput(
            destination,
            kLogicalInputCrouch,
            IsHandButtonPressed(left, bfvr::shared::kControllerHandButtonSecondary));
        SetPulseInput(
            destination,
            kLogicalInputAction,
            TakeRisingEdge(
                IsHandButtonPressed(right, bfvr::shared::kControllerHandButtonPrimary),
                rightPrimaryWasDown));
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
            // frames. They must not clear the local hand orientation baseline,
            // or every local frame becomes a zero-delta first sample.
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
        float yawInput = 0.0F;
        float pitchInput = 0.0F;
        const bool rightAimAvailable = GetRightAimDelta(
            right,
            generation,
            yawInput,
            pitchInput);

        __try
        {
            ApplyInfantryControls(
                destination,
                left,
                right,
                rightAimAvailable,
                yawInput,
                pitchInput);
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
                L"Controller input overlay accepted its first fresh focused local frame. Layout: left stick move; right stick smooth-turn with up jump/action and down prone; right trigger fire; right grip alt-fire; A jump/action; B reload; X use; Y crouch; left grip press prone. All values remain temporary native PlayerInput fields.");
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
            L"Controller input overlay gate report (one second): frameBuilders=%ld normalizers=%ld localPlayer=%ld missingScope=%ld missingManager=%ld otherPlayer=%ld dead=%ld unreadable=%ld noDestination=%ld controllerUnavailable=%ld freshController=%ld rightAimUnavailable=%ld invalidOrientation=%ld writeFault=%ld applied=%ld.",
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
            InterlockedExchange(&rightAimUnavailableFrames, 0),
            InterlockedExchange(&invalidOrientationFrames, 0),
            InterlockedExchange(&unwritableDestinationFrames, 0),
            InterlockedExchange(&appliedFrames, 0));
    }

    void ResetAimState() noexcept
    {
        hasLastOrientation = false;
        lastControllerGeneration = 0;
    }

    void ResetControllerState() noexcept
    {
        ResetAimState();
        triggerHeld = false;
        leftSqueezeHeld = false;
        rightSqueezeHeld = false;
        rightPrimaryWasDown = false;
        rightSecondaryWasDown = false;
        rightStickVerticalDirection = 0;
    }

    void RemoveHooks()
    {
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
        ResetControllerState();
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
    FrameBuilderFn originalFrameBuilder = nullptr;
    NormalizeFn originalNormalize = nullptr;
    Quaternion lastOrientation = {};
    LONG lastControllerGeneration = 0;
    volatile LONG started = 0;
    volatile LONG firstEligibleFrameLogged = 0;
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
    volatile LONG rightAimUnavailableFrames = 0;
    volatile LONG invalidOrientationFrames = 0;
    volatile LONG unwritableDestinationFrames = 0;
    volatile LONG appliedFrames = 0;
    bool hasLastOrientation = false;
    bool triggerHeld = false;
    bool leftSqueezeHeld = false;
    bool rightSqueezeHeld = false;
    bool rightPrimaryWasDown = false;
    bool rightSecondaryWasDown = false;
    int rightStickVerticalDirection = 0;
    bool ownsMinHook = false;
    bool frameBuilderCreated = false;
    bool normalizerCreated = false;
    bool frameBuilderEnabled = false;
    bool normalizerEnabled = false;
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
