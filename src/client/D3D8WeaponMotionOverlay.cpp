#include "client/D3D8WeaponMotionOverlay.h"

#include "client/BFSoldierVrMotionFilter.h"
#include "client/ControllerInputCache.h"
#include "client/WeaponPoseRuntimeCache.h"
#include "presenter/SharedPresentationProtocol.h"
#include "stereo/D3D8WeaponDrawPolicy.h"
#include "stereo/WeaponPoseMath.h"

#include <array>
#include <cstdarg>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <intrin.h>

namespace
{

constexpr DWORD kTransformView = 2;
constexpr DWORD kTransformProjection = 3;
constexpr DWORD kTransformWorld = 0x100;
constexpr DWORD kRenderStateZEnable = 7;
constexpr DWORD kRenderStateAlphaBlendEnable = 27;
constexpr DWORD kGenericMeshDrawReturn = 0x0062B83FU;
constexpr DWORD kControllerHandLeft = 0;
constexpr DWORD kControllerHandRight = 1;
constexpr DWORD kControllerSampleMaximumAgeMs = 125;
constexpr DWORD kLegacyRecoilMaximumAgeMs = 125;
// The BF1942 Mod Development Toolkit defines map/object coordinates in metres.
constexpr float kBf1942WorldUnitsPerMeter = 1.0F;
constexpr wchar_t kEnableWeaponMotionEnvironment[] =
    L"BFVR_ENABLE_WEAPON_MOTION";
constexpr wchar_t kEnableNativeArmIkEnvironment[] =
    L"BFVR_ENABLE_NATIVE_1P_ARMS_IK";
constexpr wchar_t kEnableWeaponCalibrationEnvironment[] =
    L"BFVR_ENABLE_WEAPON_CALIBRATION";

enum class CalibrationState : std::uint8_t
{
    Tracking,
    FrozenTarget
};

struct OverlayRecord
{
    volatile LONG enabled = 0;
    volatile LONG classifiedCandidates = 0;
    volatile LONG calibratedFrames = 0;
    volatile LONG appliedDraws = 0;
    volatile LONG sourceStateReadFailures = 0;
    volatile LONG staleTrackingSamples = 0;
    volatile LONG rejectedTrackingSamples = 0;
    volatile LONG adjustedSetFailures = 0;
    volatile LONG restoreFailures = 0;
    volatile LONG calibrationBegins = 0;
    volatile LONG calibrationCommits = 0;
    volatile LONG legacyRecoilSteps = 0;
    volatile LONG loggedLegacyRecoilSteps = 0;
    LONG lastCalibrationGeneration = 0;
    LONG lastLegacyRecoilSequence = 0;
    const void* legacyRecoilSoldier = nullptr;
    bfvr::stereo::WeaponRecoilAngles accumulatedLegacyRecoil = {};
    bool leftMenuWasDown = false;
    bool developmentCalibrationEnabled = false;
    CalibrationState calibrationState = CalibrationState::Tracking;
    // Portable A in Dcurrent = A * Gcurrent. Gcurrent is the controller's
    // absolute OpenXR LOCAL grip, not a head-relative pose. The current body
    // frame is derived per draw from the same source View/current head pair.
    bfvr::stereo::Matrix4 controllerToWeaponAttachment = {};
    bool hasControllerToWeaponAttachment = false;
    // The optional calibration target is a current body-frame offset. Each
    // draw reconstructs its world attachment from the current body frame.
    bfvr::stereo::Matrix4 frozenWeaponViewOffset = {};
    void (*appendLog)(const wchar_t* message) = nullptr;
};

OverlayRecord g_overlay = {};

void AppendLog(const wchar_t* format, ...) noexcept
{
    if (g_overlay.appendLog == nullptr)
    {
        return;
    }
    std::array<wchar_t, 1400> message = {};
    va_list arguments;
    va_start(arguments, format);
    _vsnwprintf_s(message.data(), message.size(), _TRUNCATE, format, arguments);
    va_end(arguments);
    g_overlay.appendLog(message.data());
}

bool IsEnabled() noexcept
{
    return InterlockedCompareExchange(&g_overlay.enabled, 0, 0) != 0;
}

bool SafeCopy(void* destination, const void* source, std::size_t size) noexcept
{
    if (destination == nullptr || source == nullptr || size == 0)
    {
        return false;
    }
    __try
    {
        std::memcpy(destination, source, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool StackSlotsContainAddress(
    const void* const* returnAddressSlot,
    DWORD expectedAddress) noexcept
{
    if (returnAddressSlot == nullptr)
    {
        return false;
    }
    constexpr std::size_t kMaximumStackSlots = 32;
    for (std::size_t index = 0; index < kMaximumStackSlots; ++index)
    {
        const void* candidate = nullptr;
        if (!SafeCopy(&candidate, returnAddressSlot + index, sizeof(candidate)))
        {
            return false;
        }
        if (reinterpret_cast<DWORD_PTR>(candidate) == expectedAddress)
        {
            return true;
        }
    }
    return false;
}

bfvr::stereo::Matrix4 ToStereoMatrix(
    const bfvr::D3D8WeaponMotionMatrix& source) noexcept
{
    bfvr::stereo::Matrix4 result = {};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            result.values[row][column] = source.values[row][column];
        }
    }
    return result;
}

bfvr::D3D8WeaponMotionMatrix ToD3D8Matrix(
    const bfvr::stereo::Matrix4& source) noexcept
{
    bfvr::D3D8WeaponMotionMatrix result = {};
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            result.values[row][column] = source.values[row][column];
        }
    }
    return result;
}

bool IsFirstPersonProjection(
    const bfvr::D3D8WeaponMotionMatrix& projection) noexcept
{
    const float horizontal = projection.values[0][0];
    const float vertical = projection.values[1][1];
    return std::isfinite(horizontal) && std::isfinite(vertical) &&
        horizontal >= 2.0F && vertical >= 3.5F;
}

bool IsNearlyIdentity(const bfvr::stereo::Matrix4& matrix) noexcept
{
    constexpr float kEpsilon = 0.00001F;
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            const float expected = row == column ? 1.0F : 0.0F;
            if (!std::isfinite(matrix.values[row][column]) ||
                std::fabs(matrix.values[row][column] - expected) > kEpsilon)
            {
                return false;
            }
        }
    }
    return true;
}

void AppendCalibrationTransform(
    const bfvr::stereo::Matrix4& targetViewOffset,
    const bfvr::stereo::Matrix4& controllerAttachment,
    const bfvr::stereo::Pose& head,
    const bfvr::stereo::Pose& grip) noexcept
{
    AppendLog(
        L"Weapon calibration values: headLocal p=(%.4f,%.4f,%.4f) q=(%.5f,%.5f,%.5f,%.5f); rightGripLocal p=(%.4f,%.4f,%.4f) q=(%.5f,%.5f,%.5f,%.5f); targetWeaponView=[%.5f %.5f %.5f %.5f | %.5f %.5f %.5f %.5f | %.5f %.5f %.5f %.5f | %.5f %.5f %.5f %.5f]; controllerToWeaponAttachment=[%.5f %.5f %.5f %.5f | %.5f %.5f %.5f %.5f | %.5f %.5f %.5f %.5f | %.5f %.5f %.5f %.5f].",
        head.position.x,
        head.position.y,
        head.position.z,
        head.orientation.x,
        head.orientation.y,
        head.orientation.z,
        head.orientation.w,
        grip.position.x,
        grip.position.y,
        grip.position.z,
        grip.orientation.x,
        grip.orientation.y,
        grip.orientation.z,
        grip.orientation.w,
        targetViewOffset.values[0][0], targetViewOffset.values[0][1], targetViewOffset.values[0][2], targetViewOffset.values[0][3],
        targetViewOffset.values[1][0], targetViewOffset.values[1][1], targetViewOffset.values[1][2], targetViewOffset.values[1][3],
        targetViewOffset.values[2][0], targetViewOffset.values[2][1], targetViewOffset.values[2][2], targetViewOffset.values[2][3],
        targetViewOffset.values[3][0], targetViewOffset.values[3][1], targetViewOffset.values[3][2], targetViewOffset.values[3][3],
        controllerAttachment.values[0][0], controllerAttachment.values[0][1], controllerAttachment.values[0][2], controllerAttachment.values[0][3],
        controllerAttachment.values[1][0], controllerAttachment.values[1][1], controllerAttachment.values[1][2], controllerAttachment.values[1][3],
        controllerAttachment.values[2][0], controllerAttachment.values[2][1], controllerAttachment.values[2][2], controllerAttachment.values[2][3],
        controllerAttachment.values[3][0], controllerAttachment.values[3][1], controllerAttachment.values[3][2], controllerAttachment.values[3][3]);
}

bfvr::stereo::Matrix4 IdentityMatrix() noexcept
{
    bfvr::stereo::Matrix4 result = {};
    for (std::size_t index = 0; index < 4; ++index)
    {
        result.values[index][index] = 1.0F;
    }
    return result;
}

std::optional<bfvr::stereo::Matrix4>
ProvisionalDefaultControllerToWeaponAttachment() noexcept
{
    // Owner-accepted PID 6632 calibration. Its target was recorded in the old
    // head-including source View frame. Convert that one known calibration
    // tuple into a body-frame target, then store A = targetBody * inverse(G).
    const bfvr::stereo::Pose head = {
        {0.0513F, 0.0102F, 0.0445F},
        {-0.15199F, 0.00706F, 0.01019F, -0.98830F}};
    const bfvr::stereo::Pose grip = {
        {0.1297F, -0.0037F, -0.4444F},
        {0.66393F, 0.04390F, -0.16858F, 0.72722F}};
    bfvr::stereo::Matrix4 targetView = IdentityMatrix();
    targetView.values[0][0] = 0.99928F;
    targetView.values[0][1] = -0.03700F;
    targetView.values[0][2] = 0.00866F;
    targetView.values[1][0] = 0.03710F;
    targetView.values[1][1] = 0.99923F;
    targetView.values[1][2] = -0.01242F;
    targetView.values[2][0] = -0.00819F;
    targetView.values[2][1] = 0.01274F;
    targetView.values[2][2] = 0.99988F;
    targetView.values[3][0] = 0.10175F;
    targetView.values[3][1] = 0.11276F;
    targetView.values[3][2] = -0.10742F;
    const auto headView = bfvr::stereo::MakeD3D8ViewFromOpenXRPose(head);
    const auto targetBody = headView.has_value()
        ? bfvr::stereo::MakeD3D8WorldSpaceWeaponDelta(*headView, targetView)
        : std::nullopt;
    return targetBody.has_value()
        ? bfvr::stereo::MakeD3D8AbsoluteGripToWeaponAttachment(
            grip,
            *targetBody,
            kBf1942WorldUnitsPerMeter)
        : std::nullopt;
}

bfvr::stereo::Pose ToHeadPose(const bfvr::D3D8RuntimeView& head) noexcept
{
    return {
        {head.positionX, head.positionY, head.positionZ},
        {head.orientationX, head.orientationY, head.orientationZ, head.orientationW}};
}

bfvr::stereo::Pose ToGripPose(
    const bfvr::D3D8RuntimeControllerHand& hand) noexcept
{
    return {
        {
            hand.gripPose.positionX,
            hand.gripPose.positionY,
            hand.gripPose.positionZ},
        {
            hand.gripPose.orientationX,
            hand.gripPose.orientationY,
            hand.gripPose.orientationZ,
            hand.gripPose.orientationW}};
}

bool TakeCalibrationAction(
    const bfvr::D3D8RuntimeControllerSample& sample,
    LONG generation) noexcept
{
    if (!g_overlay.developmentCalibrationEnabled)
    {
        return false;
    }
    // The same accepted sample can classify several parts of one view model.
    // Process its button edge only once, otherwise one Menu press would change
    // calibration state separately for each draw.
    if (generation == g_overlay.lastCalibrationGeneration)
    {
        return false;
    }
    g_overlay.lastCalibrationGeneration = generation;

    const bool leftMenuDown =
        (sample.hands[kControllerHandLeft].buttons &
         bfvr::shared::kControllerHandButtonMenu) != 0;
    const bool requested = leftMenuDown && !g_overlay.leftMenuWasDown;
    g_overlay.leftMenuWasDown = leftMenuDown;
    return requested;
}

bool BuildWorldAndReplayAttachments(
    const bfvr::stereo::Matrix4& sourceView,
    const bfvr::stereo::Matrix4& weaponViewOffset,
    bfvr::stereo::Matrix4& worldSpaceAttachment,
    bfvr::stereo::Matrix4& replayWorldSpaceAttachment) noexcept
{
    const auto world =
        bfvr::stereo::MakeD3D8WorldSpaceWeaponDelta(
            sourceView,
            weaponViewOffset);
    if (!world.has_value())
    {
        return false;
    }
    worldSpaceAttachment = *world;
    replayWorldSpaceAttachment = *world;
    return true;
}

bool ReadGripDelta(
    const bfvr::stereo::Matrix4& sourceView,
    bfvr::stereo::Matrix4& worldSpaceAttachment,
    bfvr::stereo::Matrix4& replayWorldSpaceAttachment) noexcept
{
    bfvr::D3D8RuntimeControllerSample sample = {};
    bfvr::D3D8RuntimeView head = {};
    LONG generation = 0;
    if (!bfvr::ReadFreshAcceptedWeaponTracking(
            sample,
            head,
            generation,
            kControllerSampleMaximumAgeMs))
    {
        bfvr::ClearWeaponViewOffset();
        InterlockedIncrement(&g_overlay.staleTrackingSamples);
        return false;
    }

    const bfvr::D3D8RuntimeControllerHand& right =
        sample.hands[kControllerHandRight];
    constexpr DWORD kRequiredGripFlags =
        bfvr::shared::kControllerHandFlagGripActive |
        bfvr::shared::kControllerHandFlagGripPositionValid |
        bfvr::shared::kControllerHandFlagGripOrientationValid |
        bfvr::shared::kControllerHandFlagGripPositionTracked |
        bfvr::shared::kControllerHandFlagGripOrientationTracked;
    const bool gripTrackingValid =
        (right.flags & kRequiredGripFlags) == kRequiredGripFlags;
    if (!gripTrackingValid)
    {
        bfvr::ClearWeaponViewOffset();
        InterlockedIncrement(&g_overlay.rejectedTrackingSamples);
        return false;
    }
    const bfvr::stereo::Pose currentHead = ToHeadPose(head);
    const bfvr::stereo::Pose currentGrip = ToGripPose(right);

    if (!g_overlay.hasControllerToWeaponAttachment)
    {
        // A controller sample must never establish neutral placement.
        bfvr::ClearWeaponViewOffset();
        return false;
    }

    const auto bodyView = bfvr::stereo::MakeD3D8CurrentBodyView(
        sourceView,
        currentHead,
        kBf1942WorldUnitsPerMeter);
    if (!bodyView.has_value())
    {
        bfvr::ClearWeaponViewOffset();
        InterlockedIncrement(&g_overlay.rejectedTrackingSamples);
        return false;
    }

    const auto makeWorldAttachment = [&](
        const bfvr::stereo::Matrix4& viewOffset,
        bfvr::stereo::Matrix4& worldAttachment,
        bfvr::stereo::Matrix4& replayAttachment)
    {
        return BuildWorldAndReplayAttachments(
            *bodyView,
            viewOffset,
            worldAttachment,
            replayAttachment);
    };

    const bool calibrationAction = TakeCalibrationAction(sample, generation);
    if (g_overlay.calibrationState == CalibrationState::FrozenTarget)
    {
        bfvr::stereo::Matrix4 frozenWorld = {};
        bfvr::stereo::Matrix4 frozenReplayWorld = {};
        const bool frozenBuilt = makeWorldAttachment(
            g_overlay.frozenWeaponViewOffset,
            frozenWorld,
            frozenReplayWorld);
        if (!frozenBuilt)
        {
            bfvr::ClearWeaponViewOffset();
            InterlockedIncrement(&g_overlay.rejectedTrackingSamples);
            return false;
        }
        if (calibrationAction)
        {
            // The controller is now physically placed at the fixed weapon.
            // The frozen target is already in the current body frame, so the
            // new portable attachment needs only the absolute LOCAL grip.
            const auto committedAttachment =
                bfvr::stereo::MakeD3D8AbsoluteGripToWeaponAttachment(
                    currentGrip,
                    g_overlay.frozenWeaponViewOffset,
                    kBf1942WorldUnitsPerMeter);
            if (!committedAttachment.has_value())
            {
                InterlockedIncrement(&g_overlay.rejectedTrackingSamples);
                AppendLog(
                    L"Weapon calibration commit ignored because the frozen body-frame target could not be expressed as a portable absolute-grip attachment; the weapon remains frozen.");
                bfvr::PublishWeaponVisualPose(
                    g_overlay.frozenWeaponViewOffset,
                    frozenWorld,
                    generation);
                worldSpaceAttachment = frozenWorld;
                replayWorldSpaceAttachment = frozenReplayWorld;
                return !IsNearlyIdentity(worldSpaceAttachment);
            }
            g_overlay.controllerToWeaponAttachment = *committedAttachment;
            g_overlay.hasControllerToWeaponAttachment = true;
            g_overlay.calibrationState = CalibrationState::Tracking;
            InterlockedIncrement(&g_overlay.calibrationCommits);
            AppendLog(
                L"Weapon calibration committed for development: the frozen target is now a portable absolute LOCAL-grip-to-weapon attachment in the current body frame. Native camera and fire state are unchanged.");
            AppendCalibrationTransform(
                g_overlay.frozenWeaponViewOffset,
                g_overlay.controllerToWeaponAttachment,
                currentHead,
                currentGrip);
        }
        bfvr::PublishWeaponVisualPose(
            g_overlay.frozenWeaponViewOffset,
            frozenWorld,
            generation);
        worldSpaceAttachment = frozenWorld;
        replayWorldSpaceAttachment = frozenReplayWorld;
        return !IsNearlyIdentity(worldSpaceAttachment);
    }

    const auto currentViewOffset =
        bfvr::stereo::MakeD3D8AbsoluteGripWeaponDelta(
            g_overlay.controllerToWeaponAttachment,
            currentGrip,
            kBf1942WorldUnitsPerMeter);
    if (!currentViewOffset.has_value())
    {
        bfvr::ClearWeaponViewOffset();
        InterlockedIncrement(&g_overlay.rejectedTrackingSamples);
        return false;
    }
    bfvr::stereo::Matrix4 recoilAdjustedViewOffset = *currentViewOffset;
    bfvr::BFSoldierVrLegacyRecoil legacyRecoil = {};
    if (bfvr::ReadFreshBFSoldierVrLegacyRecoil(
            legacyRecoil,
            kLegacyRecoilMaximumAgeMs))
    {
        if (g_overlay.legacyRecoilSoldier != legacyRecoil.soldier)
        {
            // A new BFSoldier is a respawn/new local-player lifetime. The
            // native flat camera receives a fresh recoil state too, so the
            // controller-held weapon must not inherit the prior soldier's
            // accumulated angle.
            g_overlay.legacyRecoilSoldier = legacyRecoil.soldier;
            g_overlay.lastLegacyRecoilSequence = 0;
            g_overlay.accumulatedLegacyRecoil = {};
        }
        if (legacyRecoil.sequence != 0 &&
            legacyRecoil.sequence != g_overlay.lastLegacyRecoilSequence)
        {
            const auto accumulated = bfvr::stereo::AccumulateD3D8WeaponRecoil(
                g_overlay.accumulatedLegacyRecoil,
                legacyRecoil.pitch,
                legacyRecoil.yaw);
            if (!accumulated.has_value())
            {
                bfvr::ClearWeaponViewOffset();
                InterlockedIncrement(&g_overlay.rejectedTrackingSamples);
                return false;
            }
            g_overlay.accumulatedLegacyRecoil = *accumulated;
            g_overlay.lastLegacyRecoilSequence = legacyRecoil.sequence;
            InterlockedIncrement(&g_overlay.legacyRecoilSteps);
            if (InterlockedIncrement(&g_overlay.loggedLegacyRecoilSteps) <= 8)
            {
                AppendLog(
                    L"Applied native weapon recoil step sequence=%ld impulse=(%.7f,%.7f) accumulated=(%.7f,%.7f) soldier=%p.",
                    legacyRecoil.sequence,
                    legacyRecoil.pitch,
                    legacyRecoil.yaw,
                    g_overlay.accumulatedLegacyRecoil.pitch,
                    g_overlay.accumulatedLegacyRecoil.yaw,
                    legacyRecoil.soldier);
            }
        }
        const auto recoilAdjusted =
            bfvr::stereo::MakeD3D8AbsoluteGripWeaponRecoilDelta(
                g_overlay.controllerToWeaponAttachment,
                currentGrip,
                kBf1942WorldUnitsPerMeter,
                g_overlay.accumulatedLegacyRecoil.pitch,
                g_overlay.accumulatedLegacyRecoil.yaw);
        if (!recoilAdjusted.has_value())
        {
            bfvr::ClearWeaponViewOffset();
            InterlockedIncrement(&g_overlay.rejectedTrackingSamples);
            return false;
        }
        recoilAdjustedViewOffset = *recoilAdjusted;
    }

    bfvr::stereo::Matrix4 currentWorldAttachment = {};
    bfvr::stereo::Matrix4 currentReplayWorldAttachment = {};
    const bool currentAttachmentsBuilt = makeWorldAttachment(
        recoilAdjustedViewOffset,
        currentWorldAttachment,
        currentReplayWorldAttachment);
    if (!currentAttachmentsBuilt)
    {
        bfvr::ClearWeaponViewOffset();
        InterlockedIncrement(&g_overlay.rejectedTrackingSamples);
        return false;
    }
    bfvr::PublishWeaponVisualPose(
        recoilAdjustedViewOffset,
        currentWorldAttachment,
        generation);
    worldSpaceAttachment = currentWorldAttachment;
    replayWorldSpaceAttachment = currentReplayWorldAttachment;
    if (calibrationAction)
    {
        // Preserve the current visible weapon placement while raw controller
        // tracking is paused. The player can now move the physical controller
        // to that fixed target instead of chasing a controller-driven gun.
        g_overlay.frozenWeaponViewOffset = *currentViewOffset;
        g_overlay.calibrationState = CalibrationState::FrozenTarget;
        InterlockedIncrement(&g_overlay.calibrationBegins);
        AppendLog(
            L"Weapon calibration started: the rendered weapon is frozen at its current pose while the raw right controller is free. Move the controller to the fixed weapon, then press left Menu again to commit the shared offset.");
    }
    return !IsNearlyIdentity(worldSpaceAttachment);
}

bool IsSharedWeaponCandidate(
    const bfvr::D3D8WeaponMotionD3D8Api& api,
    void* device,
    const void* const* returnAddressSlot,
    bfvr::D3D8WeaponMotionMatrix& world,
    bfvr::D3D8WeaponMotionMatrix& view,
    bfvr::D3D8WeaponMotionMatrix& projection) noexcept
{
    if (!StackSlotsContainAddress(returnAddressSlot, kGenericMeshDrawReturn) ||
        api.getTransform == nullptr || api.getRenderState == nullptr ||
        api.getVertexShader == nullptr ||
        FAILED(api.getTransform(device, kTransformWorld, &world)) ||
        FAILED(api.getTransform(device, kTransformView, &view)))
    {
        return false;
    }

    DWORD zEnable = 0;
    DWORD alphaBlendEnable = 0;
    DWORD vertexShaderOrFvf = 0;
    if (FAILED(api.getTransform(device, kTransformProjection, &projection)) ||
        FAILED(api.getRenderState(device, kRenderStateZEnable, &zEnable)) ||
        FAILED(api.getRenderState(
            device,
            kRenderStateAlphaBlendEnable,
            &alphaBlendEnable)) ||
        FAILED(api.getVertexShader(device, &vertexShaderOrFvf)))
    {
        InterlockedIncrement(&g_overlay.sourceStateReadFailures);
        return false;
    }

    bfvr::stereo::WeaponDrawPolicyInput input = {};
    input.indexedDraw = true;
    input.rendererRoute = bfvr::stereo::WeaponRendererRoute::GenericMesh;
    input.vertexShaderOrFvf = vertexShaderOrFvf;
    input.alphaBlendEnabled = alphaBlendEnable != 0;
    input.zEnabled = zEnable != 0;
    input.firstPersonProjection = IsFirstPersonProjection(projection);
    input.worldKnown = true;
    input.viewKnown = true;
    input.world = ToStereoMatrix(world);
    input.view = ToStereoMatrix(view);
    return bfvr::stereo::ClassifyWeaponDraw(input, 1.25F) ==
        bfvr::stereo::WeaponDrawDisposition::SharedFixedFunctionWeaponCandidate;
}

} // namespace

namespace bfvr
{

bool BuildD3D8WeaponMotionReplayWorld(
    const D3D8WeaponMotionMatrix& sourceWorld,
    const D3D8WeaponMotionMatrix& worldSpaceAttachment,
    D3D8WeaponMotionMatrix& adjustedWorld) noexcept
{
    const auto adjusted = bfvr::stereo::ApplyWorldSpaceWeaponDeltaToD3D8World(
        ToStereoMatrix(sourceWorld),
        ToStereoMatrix(worldSpaceAttachment));
    if (!adjusted.has_value())
    {
        return false;
    }
    adjustedWorld = ToD3D8Matrix(*adjusted);
    return true;
}

void StartD3D8WeaponMotionOverlay(
    void (*appendLog)(const wchar_t* message))
{
    g_overlay = {};
    bfvr::ClearWeaponViewOffset();
    g_overlay.appendLog = appendLog;
    wchar_t enabled[2] = {};
    if (GetEnvironmentVariableW(
            kEnableWeaponMotionEnvironment,
            enabled,
            static_cast<DWORD>(std::size(enabled))) != 1 ||
        enabled[0] != L'1')
    {
        return;
    }
    wchar_t nativeArmIkEnabled[2] = {};
    if (GetEnvironmentVariableW(
            kEnableNativeArmIkEnvironment,
            nativeArmIkEnabled,
            static_cast<DWORD>(std::size(nativeArmIkEnabled))) == 1 &&
        nativeArmIkEnabled[0] == L'1')
    {
        AppendLog(
            L"Visual weapon-motion overlay remains disabled because native 1P arm IK owns the game-selected hand and rendered weapon transform. The controller-fire overlay separately applies the solved hand's complete world attachment to BF1942's native fire matrix.");
        return;
    }
    const auto provisionalAttachment =
        ProvisionalDefaultControllerToWeaponAttachment();
    if (!provisionalAttachment.has_value())
    {
        AppendLog(
            L"Weapon motion overlay disabled: the fixed development attachment could not be reconstructed.");
        return;
    }
    g_overlay.controllerToWeaponAttachment = *provisionalAttachment;
    g_overlay.hasControllerToWeaponAttachment = true;
    wchar_t calibrationEnabled[2] = {};
    g_overlay.developmentCalibrationEnabled =
        GetEnvironmentVariableW(
            kEnableWeaponCalibrationEnvironment,
            calibrationEnabled,
            static_cast<DWORD>(std::size(calibrationEnabled))) == 1 &&
        calibrationEnabled[0] == L'1';
    g_overlay.frozenWeaponViewOffset = IdentityMatrix();
    InterlockedExchange(&g_overlay.enabled, 1);
    AppendLog(
        g_overlay.developmentCalibrationEnabled
            ? L"Weapon motion overlay armed with explicit development calibration enabled. Visual and fire alignment use the current body frame and absolute LOCAL controller grip; fresh legacy recoil rotates only the held weapon and matching fire direction, while Left Menu may only change the attachment when BFVR_ENABLE_WEAPON_CALIBRATION=1 was deliberately set."
            : L"Weapon motion overlay armed with a fixed PID 6632 attachment converted to the current body frame. The controller delta uses absolute LOCAL grip with no headset input, so no spawn, head, controller, menu, or reset sample can create a weapon basis. Fresh legacy recoil rotates only the held weapon and matching fire direction; controller reach is not clamped and no viewmodel scale/perspective morph is applied. Invalid tracking leaves the game draw untouched.");
}

void StopD3D8WeaponMotionOverlay()
{
    if (!IsEnabled())
    {
        bfvr::ClearWeaponViewOffset();
        g_overlay = {};
        return;
    }
    InterlockedExchange(&g_overlay.enabled, 0);
    bfvr::ClearWeaponViewOffset();
    AppendLog(
        L"Weapon motion overlay stopped: candidates=%ld calibrated=%ld applied=%ld legacyRecoilSteps=%ld accumulatedRecoil=(%.6f,%.6f) sourceReadFailures=%ld stale=%ld rejectedTracking=%ld adjustedSetFailures=%ld restoreFailures=%ld calibrationBegins=%ld calibrationCommits=%ld.",
        InterlockedCompareExchange(&g_overlay.classifiedCandidates, 0, 0),
        InterlockedCompareExchange(&g_overlay.calibratedFrames, 0, 0),
        InterlockedCompareExchange(&g_overlay.appliedDraws, 0, 0),
        InterlockedCompareExchange(&g_overlay.legacyRecoilSteps, 0, 0),
        g_overlay.accumulatedLegacyRecoil.pitch,
        g_overlay.accumulatedLegacyRecoil.yaw,
        InterlockedCompareExchange(&g_overlay.sourceStateReadFailures, 0, 0),
        InterlockedCompareExchange(&g_overlay.staleTrackingSamples, 0, 0),
        InterlockedCompareExchange(&g_overlay.rejectedTrackingSamples, 0, 0),
        InterlockedCompareExchange(&g_overlay.adjustedSetFailures, 0, 0),
        InterlockedCompareExchange(&g_overlay.restoreFailures, 0, 0),
        InterlockedCompareExchange(&g_overlay.calibrationBegins, 0, 0),
        InterlockedCompareExchange(&g_overlay.calibrationCommits, 0, 0));
}

bool BeginD3D8WeaponMotionOverlayDraw(
    void* device,
    const D3D8WeaponMotionD3D8Api& api,
    const void* const* returnAddressSlot,
    D3D8WeaponMotionRestore& restore) noexcept
{
    restore = {};
    if (!IsEnabled() || device == nullptr || api.setTransform == nullptr)
    {
        return false;
    }

    D3D8WeaponMotionMatrix world = {};
    D3D8WeaponMotionMatrix view = {};
    D3D8WeaponMotionMatrix projection = {};
    if (!IsSharedWeaponCandidate(
            api,
            device,
            returnAddressSlot,
            world,
            view,
            projection))
    {
        return false;
    }
    InterlockedIncrement(&g_overlay.classifiedCandidates);

    bfvr::stereo::Matrix4 worldSpaceAttachment = {};
    bfvr::stereo::Matrix4 replayWorldSpaceAttachment = {};
    if (!ReadGripDelta(
            ToStereoMatrix(view),
            worldSpaceAttachment,
            replayWorldSpaceAttachment))
    {
        return false;
    }
    const auto adjusted = bfvr::stereo::ApplyWorldSpaceWeaponDeltaToD3D8World(
        ToStereoMatrix(world),
        worldSpaceAttachment);
    if (!adjusted.has_value())
    {
        InterlockedIncrement(&g_overlay.rejectedTrackingSamples);
        return false;
    }
    const D3D8WeaponMotionMatrix adjustedWorld = ToD3D8Matrix(*adjusted);
    if (FAILED(api.setTransform(device, kTransformWorld, &adjustedWorld)))
    {
        InterlockedIncrement(&g_overlay.adjustedSetFailures);
        return false;
    }
    restore.changedWorld = true;
    restore.sourceWorld = world;
    restore.worldSpaceAttachment =
        ToD3D8Matrix(replayWorldSpaceAttachment);
    InterlockedIncrement(&g_overlay.appliedDraws);
    return true;
}

void EndD3D8WeaponMotionOverlayDraw(
    void* device,
    const D3D8WeaponMotionD3D8Api& api,
    D3D8WeaponMotionRestore& restore) noexcept
{
    if (!restore.changedWorld)
    {
        return;
    }
    restore.changedWorld = false;
    if (api.setTransform == nullptr ||
        FAILED(api.setTransform(device, kTransformWorld, &restore.sourceWorld)))
    {
        InterlockedIncrement(&g_overlay.restoreFailures);
        InterlockedExchange(&g_overlay.enabled, 0);
        AppendLog(
            L"Weapon motion overlay disabled after a World-transform restore failure; later draws are left untouched.");
    }
}

} // namespace bfvr
