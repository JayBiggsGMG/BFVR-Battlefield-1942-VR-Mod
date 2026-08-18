#include "client/D3D8RenderViewPoseHook.h"

#include "client/BF1942HudToggle.h"
#include "client/BFSoldierVrMotionFilter.h"
#include "client/InfantryAuthoritativeAimRuntime.h"
#include "client/MountedWeaponAimResolver.h"
#include "client/D3D8RuntimeDiagnostics.h"
#include "client/ScopeViewOverlay.h"

#include "stereo/MountedCameraMath.h"
#include "stereo/InfantryCameraMath.h"
#include "stereo/ScopeViewMath.h"
#include "stereo/StereoMath.h"

#include <MinHook.h>

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <intrin.h>

namespace
{
constexpr std::ptrdiff_t kActiveRenderViewGlobalRva = 0x005AB868;
constexpr std::ptrdiff_t kSetTransformationRva = 0x001B7E00;
constexpr std::ptrdiff_t kExpectedCallerReturnRva = 0x000668D1;
constexpr std::ptrdiff_t kGetFrustumRva = 0x001B7E40;
constexpr std::size_t kRenderViewFieldOfViewOffset = 0x24;
constexpr std::size_t kRenderViewAspectOffset = 0x30;
constexpr std::size_t kRenderViewProjectionDirtyOffset = 0x311;
constexpr std::size_t kRenderViewFrustumDirtyOffset = 0x312;
constexpr float kWorldUnitsPerMeter = 1.0F;
// The scoped path proved this exact target after the older proposed ordinary
// caller-return gate never matched. Runtime work is now gated to an active
// BFVR request and the exact active RenderView; scope-specific camera/FOV work
// still additionally requires a fresh useScope-enabled local handweapon.
constexpr bool kEnableFrustumHook = true;
constexpr DWORD kFireCameraTraceWindowMs = 2500;
constexpr DWORD kFireCameraTraceSampleIntervalMs = 50;
constexpr LONG kMaximumFireCameraTraceFramesPerShot = 48;
constexpr float kRadiansToDegrees = 57.29577951308232F;

bfvr::stereo::Pose ToPose(const bfvr::D3D8RuntimeView& view)
{
    return {
        {view.positionX, view.positionY, view.positionZ},
        {
            view.orientationX,
            view.orientationY,
            view.orientationZ,
            view.orientationW}};
}

float VectorAngleDegrees(
    float ax,
    float ay,
    float az,
    float bx,
    float by,
    float bz) noexcept
{
    const float aLength = std::sqrt(ax * ax + ay * ay + az * az);
    const float bLength = std::sqrt(bx * bx + by * by + bz * bz);
    if (!std::isfinite(aLength) || !std::isfinite(bLength) ||
        aLength <= 0.000001F || bLength <= 0.000001F)
    {
        return -1.0F;
    }
    const float cosine = std::clamp(
        (ax * bx + ay * by + az * bz) / (aLength * bLength),
        -1.0F,
        1.0F);
    return std::acos(cosine) * kRadiansToDegrees;
}

float MatrixBasisAngleDegrees(
    const bfvr::stereo::Matrix4& a,
    const bfvr::stereo::Matrix4& b,
    std::size_t row) noexcept
{
    return VectorAngleDegrees(
        a.values[row][0],
        a.values[row][1],
        a.values[row][2],
        b.values[row][0],
        b.values[row][1],
        b.values[row][2]);
}

float MatrixTranslationDistance(
    const bfvr::stereo::Matrix4& a,
    const bfvr::stereo::Matrix4& b) noexcept
{
    const float x = a.values[3][0] - b.values[3][0];
    const float y = a.values[3][1] - b.values[3][1];
    const float z = a.values[3][2] - b.values[3][2];
    return std::sqrt(x * x + y * y + z * z);
}

float ViewPositionDistance(
    const bfvr::D3D8RuntimeView& a,
    const bfvr::D3D8RuntimeView& b) noexcept
{
    const float x = a.positionX - b.positionX;
    const float y = a.positionY - b.positionY;
    const float z = a.positionZ - b.positionZ;
    return std::sqrt(x * x + y * y + z * z);
}

float ViewOrientationAngleDegrees(
    const bfvr::D3D8RuntimeView& a,
    const bfvr::D3D8RuntimeView& b) noexcept
{
    const float aLength = std::sqrt(
        a.orientationX * a.orientationX +
        a.orientationY * a.orientationY +
        a.orientationZ * a.orientationZ +
        a.orientationW * a.orientationW);
    const float bLength = std::sqrt(
        b.orientationX * b.orientationX +
        b.orientationY * b.orientationY +
        b.orientationZ * b.orientationZ +
        b.orientationW * b.orientationW);
    if (!std::isfinite(aLength) || !std::isfinite(bLength) ||
        aLength <= 0.000001F || bLength <= 0.000001F)
    {
        return -1.0F;
    }
    const float dot = std::fabs(
        (a.orientationX * b.orientationX +
         a.orientationY * b.orientationY +
         a.orientationZ * b.orientationZ +
         a.orientationW * b.orientationW) /
        (aLength * bLength));
    return 2.0F * std::acos(std::clamp(dot, 0.0F, 1.0F)) *
        kRadiansToDegrees;
}

bfvr::stereo::Matrix4 MatrixFromFlatArray(
    const std::array<float, 16>& values) noexcept
{
    bfvr::stereo::Matrix4 matrix = {};
    std::memcpy(&matrix, values.data(), sizeof(matrix));
    return matrix;
}

bfvr::stereo::Matrix4 IdentityMatrix() noexcept
{
    bfvr::stereo::Matrix4 identity = {};
    identity.values[0][0] = 1.0F;
    identity.values[1][1] = 1.0F;
    identity.values[2][2] = 1.0F;
    identity.values[3][3] = 1.0F;
    return identity;
}

bool IsVerifiedSetterTarget(const void* target)
{
    constexpr BYTE kExpectedPrefix[] = {
        0x56, 0x8B, 0x74, 0x24, 0x08, 0x8B, 0xC1, 0x57, 0x8D,
        0x78, 0x3C, 0xB9, 0x10, 0x00, 0x00, 0x00, 0xF3, 0xA5};
    if (target == nullptr)
    {
        return false;
    }
    __try
    {
        return std::memcmp(
            target,
            kExpectedPrefix,
            sizeof(kExpectedPrefix)) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool IsVerifiedFrustumTarget(const void* target)
{
    constexpr BYTE kExpectedPrefix[] = {
        0x56, 0x8B, 0xF1, 0x8A, 0x86, 0x12, 0x03,
        0x00, 0x00, 0x84, 0xC0, 0x74};
    if (target == nullptr)
    {
        return false;
    }
    __try
    {
        return std::memcmp(
            target,
            kExpectedPrefix,
            sizeof(kExpectedPrefix)) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}
} // namespace

namespace bfvr
{
class D3D8RenderViewPoseHook::Impl
{
public:
    using SetTransformationFn =
        void(__thiscall*)(void* renderView, const void* transformation);
    using GetFrustumFn =
        void*(__thiscall*)(void* renderView);

    bool Create(
        void* image,
        D3D8RenderViewPoseLogCallback callback)
    {
        gameImage = static_cast<std::byte*>(image);
        logCallback = callback;
        (void)hudToggle.Initialize(
            reinterpret_cast<HMODULE>(image),
            callback);
        diagnosticsEnabled = IsD3D8RuntimeDiagnosticsEnabled(
            ReadD3D8RuntimeDiagnosticLevel());
        target = gameImage == nullptr
            ? nullptr
            : gameImage + kSetTransformationRva;
        frustumTarget = gameImage == nullptr
            ? nullptr
            : gameImage + kGetFrustumRva;
        if (!IsVerifiedSetterTarget(target))
        {
            WriteLog(
                L"RenderView pose hook rejected target=%p: the profiled 0x005B7E00 prefix did not match.",
                target);
            return false;
        }
        if constexpr (kEnableFrustumHook)
        {
            if (!IsVerifiedFrustumTarget(frustumTarget))
            {
                WriteLog(
                    L"RenderView frustum hook rejected target=%p: the profiled 0x005B7E40 prefix did not match.",
                    frustumTarget);
                return false;
            }
        }
        const MH_STATUS status = MH_CreateHook(
            target,
            reinterpret_cast<LPVOID>(&Impl::Hook),
            reinterpret_cast<LPVOID*>(&original));
        created = status == MH_OK && original != nullptr;
        if (!created)
        {
            WriteLog(
                L"RenderView pose hook could not create target=%p status=%d trampoline=%p.",
                target,
                static_cast<int>(status),
                reinterpret_cast<void*>(original));
            return false;
        }
        if constexpr (kEnableFrustumHook)
        {
            const MH_STATUS frustumStatus = MH_CreateHook(
                frustumTarget,
                reinterpret_cast<LPVOID>(&Impl::FrustumHook),
                reinterpret_cast<LPVOID*>(&originalGetFrustum));
            frustumCreated =
                frustumStatus == MH_OK && originalGetFrustum != nullptr;
            if (!frustumCreated)
            {
                WriteLog(
                    L"RenderView frustum hook could not create target=%p status=%d trampoline=%p.",
                    frustumTarget,
                    static_cast<int>(frustumStatus),
                    reinterpret_cast<void*>(originalGetFrustum));
                MH_RemoveHook(target);
                created = false;
                original = nullptr;
                return false;
            }
        }
        active = this;
        return true;
    }

    bool Enable()
    {
        if (!created || active != this)
        {
            return false;
        }
        const MH_STATUS status = MH_EnableHook(target);
        const MH_STATUS frustumStatus = status == MH_OK && frustumCreated
            ? MH_EnableHook(frustumTarget)
            : MH_OK;
        enabled = status == MH_OK && frustumStatus == MH_OK;
        if (!enabled)
        {
            if (status == MH_OK && frustumCreated)
            {
                MH_DisableHook(target);
            }
            WriteLog(
                L"RenderView pose/frustum hooks could not enable setter=%p status=%d frustum=%p status=%d.",
                target,
                static_cast<int>(status),
                frustumTarget,
                static_cast<int>(frustumStatus));
        }
        return enabled;
    }

    void UpdatePose(
        const D3D8RuntimeView& newReferenceHead,
        const D3D8RuntimeRenderRequest& request,
        const std::uint32_t trackingContextGeneration,
        const bool committedInfantryTrackingContext,
        const bool infantryPresentationYawValid,
        const float infantryPresentationYawRadians)
    {
        referenceHead = newReferenceHead;
        currentHead = MakeD3D8RuntimeHeadReference(request);
        if (request.controllerInput.valid &&
            request.controllerInput.mountedCameraToggleSequence >= 0)
        {
            InterlockedExchange(
                &requestedMountedCameraToggleSequence,
                request.controllerInput.mountedCameraToggleSequence);
        }
        if (request.controllerInput.valid &&
            request.controllerInput.hudToggleSequence >= 0)
        {
            hudToggle.Consume(request.controllerInput.hudToggleSequence);
        }
        InterlockedExchange(
            &requestedTrackingContextGeneration,
            static_cast<LONG>(trackingContextGeneration));
        InterlockedExchange(
            &requestedInfantryTrackingContext,
            committedInfantryTrackingContext ? 1 : 0);
        requestedInfantryPresentationYawRadians =
            infantryPresentationYawRadians;
        InterlockedExchange(
            &requestedInfantryPresentationYawValid,
            infantryPresentationYawValid ? 1 : 0);
        MemoryBarrier();
        InterlockedExchange(&requestedSequence, request.sequence);
    }

    void ClearPose() noexcept
    {
        bfvr::ClearInfantryNativeAimCamera();
        referenceHead = {};
        currentHead = {};
        lastSource = {};
        lastCameraSource = {};
        appliedSourceCamera = {};
        lastSourceValid = false;
        lastCameraSourceValid = false;
        lastPresentedHead = {};
        lastPresentedHeadValid = false;
        tracedFireSequence = 0;
        tracedFireFrames = 0;
        tracedFireLastLoggedAt = 0;
        tracedFireBaselineSource = {};
        tracedFireBaselineBody = {};
        tracedFireBaselineBodyValid = false;
        tracedFireBaselineHead = {};
        InterlockedExchange(&requestedTrackingContextGeneration, 0);
        InterlockedExchange(&appliedTrackingContextGeneration, 0);
        InterlockedExchange(&requestedInfantryTrackingContext, 0);
        requestedInfantryPresentationYawRadians = 0.0F;
        InterlockedExchange(&requestedInfantryPresentationYawValid, 0);
        ResetMountedCameraAnchor(false);
        MemoryBarrier();
        InterlockedExchange(&requestedSequence, 0);
        InterlockedExchange(&appliedSequence, 0);
        InterlockedExchange(&appliedSourceSequence, 0);
        InterlockedExchange(&appliedFrustumSequence, 0);
    }

    void RequestScopeNormalFovRestore(const float normalFov) noexcept
    {
        if (!std::isfinite(normalFov) || normalFov <= 0.0F)
        {
            return;
        }
        pendingScopeNormalFov = normalFov;
        MemoryBarrier();
        InterlockedExchange(&scopeNormalFovRestorePending, 1);
        (void)TryApplyScopeNormalFovRestore(ActiveRenderView());
    }

    bool WasApplied(LONG sequence) const noexcept
    {
        return sequence > 0 &&
            InterlockedCompareExchange(
                const_cast<volatile LONG*>(&appliedSequence),
                0,
                0) == sequence;
    }

    bool TryGetAppliedSourceCamera(
        LONG sequence,
        stereo::Matrix4& sourceCamera) const noexcept
    {
        if (sequence <= 0 ||
            InterlockedCompareExchange(
                const_cast<volatile LONG*>(&appliedSourceSequence),
                0,
                0) != sequence)
        {
            return false;
        }
        MemoryBarrier();
        const stereo::Matrix4 candidate = appliedSourceCamera;
        MemoryBarrier();
        if (InterlockedCompareExchange(
                const_cast<volatile LONG*>(&appliedSourceSequence),
                0,
                0) != sequence)
        {
            return false;
        }
        sourceCamera = candidate;
        return true;
    }

    bool IsMountedCameraDecoupled() const noexcept
    {
        return InterlockedCompareExchange(
            const_cast<volatile LONG*>(&mountedCameraDecoupled),
            0,
            0) != 0;
    }

    void DisableAndRemove()
    {
        if (enabled)
        {
            if (frustumCreated)
            {
                MH_DisableHook(frustumTarget);
            }
            MH_DisableHook(target);
            enabled = false;
        }
        if (frustumCreated)
        {
            MH_RemoveHook(frustumTarget);
            frustumCreated = false;
        }
        if (created)
        {
            MH_RemoveHook(target);
            created = false;
        }
        if (active == this)
        {
            active = nullptr;
        }
        original = nullptr;
        originalGetFrustum = nullptr;
        target = nullptr;
        frustumTarget = nullptr;
        gameImage = nullptr;
        appliedSourceCamera = {};
        lastSourceValid = false;
        lastCameraSourceValid = false;
        InterlockedExchange(&requestedTrackingContextGeneration, 0);
        InterlockedExchange(&appliedTrackingContextGeneration, 0);
        InterlockedExchange(&requestedInfantryTrackingContext, 0);
        requestedInfantryPresentationYawRadians = 0.0F;
        InterlockedExchange(&requestedInfantryPresentationYawValid, 0);
        ResetMountedCameraAnchor(false);
        MemoryBarrier();
        InterlockedExchange(&appliedSourceSequence, 0);
        mountedCameraControl = {};
        InterlockedExchange(&requestedMountedCameraToggleSequence, 0);
        InterlockedExchange(&mountedCameraDecoupled, 0);
        InterlockedExchange(&scopeNormalFovRestorePending, 0);
        pendingScopeNormalFov = -1.0F;
    }

    void LogSummary() const
    {
        hudToggle.LogSummary();
        WriteLog(
            L"RenderView pose/frustum-hook summary: setterMatches=%ld setterApplied=%ld setterRejected=%ld infantryComfortApplied=%ld infantryComfortRejected=%ld trackingContextChanges=%ld scopedApplied=%ld scopedRejected=%ld scopeDeathFovRestores=%ld scopeDeathFovRestoreFailures=%ld frustumMatches=%ld frustumApplied=%ld mountedFrustumApplied=%ld frustumNoSource=%ld frustumRejected=%ld mountedAnchorCaptures=%ld mountedDecoupled=%ld mountedRejected=%ld mountedResets=%ld mountedToggles=%ld mountedToggleIgnored=%ld fireCameraTraceFrames=%ld lastRequested=%ld lastApplied=%ld lastFrustum=%ld.",
            matchingCalls,
            appliedCalls,
            rejectedTransforms,
            infantryComfortApplied,
            infantryComfortRejected,
            trackingContextChanges,
            scopedCalls,
            scopedRejected,
            scopeNormalFovRestores,
            scopeNormalFovRestoreFailures,
            frustumMatchingCalls,
            frustumAppliedCalls,
            mountedFrustumAppliedCalls,
            frustumNoSource,
            frustumRejected,
            mountedAnchorCaptures,
            mountedDecoupledCalls,
            mountedRejected,
            mountedResets,
            mountedToggles,
            mountedToggleIgnored,
            fireCameraTraceFrames,
            requestedSequence,
            appliedSequence,
            appliedFrustumSequence);
    }

private:
    static void __fastcall Hook(
        void* renderView,
        void*,
        const void* transformation)
    {
        Impl* const self = active;
        if (self == nullptr || self->original == nullptr)
        {
            return;
        }
        self->Dispatch(
            renderView,
            transformation,
            _ReturnAddress());
    }

    static void* __fastcall FrustumHook(
        void* renderView,
        void*)
    {
        Impl* const self = active;
        if (self == nullptr || self->originalGetFrustum == nullptr)
        {
            return nullptr;
        }
        return self->DispatchFrustum(
            renderView,
            _ReturnAddress());
    }

    void Dispatch(
        void* renderView,
        const void* transformation,
        const void* callerReturn)
    {
        void* const activeRenderView = ActiveRenderView();
        const void* const expectedCaller = gameImage == nullptr
            ? nullptr
            : gameImage + kExpectedCallerReturnRva;
        if (renderView == nullptr ||
            renderView != activeRenderView ||
            callerReturn != expectedCaller)
        {
            original(renderView, transformation);
            return;
        }

        InterlockedIncrement(&matchingCalls);
        stereo::Matrix4 source = {};
        const stereo::Matrix4 previousSource = lastSource;
        const bool previousSourceValid = lastSourceValid;
        bool readable = false;
        __try
        {
            std::memcpy(&source, transformation, sizeof(source));
            readable = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            readable = false;
        }
        if (readable)
        {
            lastSource = source;
            lastSourceValid = true;
            MemoryBarrier();
        }
        const LONG sequence =
            InterlockedCompareExchange(&requestedSequence, 0, 0);
        if (sequence <= 0)
        {
            original(renderView, transformation);
            return;
        }

        const LONG trackingContextGeneration =
            InterlockedCompareExchange(
                &requestedTrackingContextGeneration,
                0,
                0);
        const LONG priorTrackingContextGeneration =
            InterlockedCompareExchange(
                &appliedTrackingContextGeneration,
                0,
                0);
        if (trackingContextGeneration > 0 &&
            trackingContextGeneration != priorTrackingContextGeneration)
        {
            InterlockedExchange(
                &appliedTrackingContextGeneration,
                trackingContextGeneration);
            if (priorTrackingContextGeneration > 0)
            {
                InterlockedIncrement(&trackingContextChanges);
                WriteLog(
                    L"Infantry presentation context synchronized to committed handoff generation=%ld (previous=%ld). The headset and controllers change anchors on this boundary; native soldier aim can no longer change the request-matched local presentation heading.",
                    trackingContextGeneration,
                    priorTrackingContextGeneration);
            }
        }

        const bool committedInfantryTrackingContext =
            InterlockedCompareExchange(
                &requestedInfantryTrackingContext,
                0,
                0) != 0;
        const bool infantryPresentationYawValid =
            InterlockedCompareExchange(
                &requestedInfantryPresentationYawValid,
                0,
                0) != 0;
        const float infantryPresentationYawRadians =
            requestedInfantryPresentationYawRadians;
        stereo::Matrix4 cameraSource = source;
        LocalInfantryBodyPose infantryBody = {};
        const bool infantryBodyRead =
            readable && committedInfantryTrackingContext &&
            ReadLocalInfantryBodyPose(infantryBody);
        if (infantryBodyRead)
        {
            // Publish the untouched Battlefield camera before presentation
            // yaw/HMD composition. Accepted-shot traces prove this forward is
            // the local native WeaponFire direction; the controller consumes
            // it only as short-lived exact-soldier feedback.
            bfvr::PublishInfantryNativeAimCamera(
                infantryBody.controlObject,
                source,
                sequence);
        }
        else
        {
            bfvr::ClearInfantryNativeAimCamera();
        }
        if (infantryBodyRead && infantryPresentationYawValid)
        {
            const auto presentationCamera =
                stereo::MakeD3D8InfantryPresentationCamera(
                    source,
                    infantryPresentationYawRadians);
            if (presentationCamera.has_value())
            {
                cameraSource = *presentationCamera;
                InterlockedIncrement(&infantryComfortApplied);
                if (InterlockedCompareExchange(
                        &firstInfantryComfortLogged,
                        1,
                        0) == 0)
                {
                    WriteLog(
                        L"Infantry VR camera now uses BF1942's source position plus a request-matched local presentation heading. Native hand-driven soldier yaw, camera pitch/roll, and view-only recoil are excluded from the HMD view in both SP and MP; Smooth/Snap intent remains the only artificial presentation-yaw owner. Native soldier aim, firing, damage, networking, movement, models, and IK remain game-owned. controlObject=%p.",
                        infantryBody.controlObject);
                }
            }
            else
            {
                InterlockedIncrement(&infantryComfortRejected);
            }
        }
        MountedWeaponStationPose station = {};
        const bool stationValid = readable &&
            ReadOccupiedMountedWeaponStationPose(station);
        const std::uint32_t toggleSequence = static_cast<std::uint32_t>(
            (std::max)(
                InterlockedCompareExchange(
                    &requestedMountedCameraToggleSequence,
                    0,
                    0),
                0L));
        const std::uintptr_t previousMountedStation =
            mountedCameraControl.stationIdentity;
        const stereo::MountedCameraControlTransition transition =
            stereo::UpdateMountedCameraControl(
                mountedCameraControl,
                stationValid
                    ? reinterpret_cast<std::uintptr_t>(
                        station.controlObject)
                    : 0,
                toggleSequence);
        if (transition.stationChanged)
        {
            WriteLog(
                L"Mounted station resolver transition observed previousStation=%p currentStation=%p. The independently committed three-sample tracking context still owns headset/controller anchoring; infantry camera heading retains no cross-frame state.",
                reinterpret_cast<const void*>(previousMountedStation),
                station.controlObject);
        }
        if (transition.stationChanged || transition.decouplingChanged)
        {
            ResetMountedCameraAnchor(
                mountedCameraAnchorValid &&
                transition.decouplingChanged);
        }
        if (transition.toggleApplied)
        {
            InterlockedIncrement(&mountedToggles);
            WriteLog(
                mountedCameraControl.decoupled
                    ? L"Mounted-camera decoupling enabled for current station=%p; right-stick and controller motion still drive BF1942's native weapon axes while the VR camera remains in the station frame."
                    : L"Mounted-camera decoupling disabled for current station=%p; BF1942's native coupled camera is restored.",
                station.controlObject);
        }
        if (transition.toggleIgnored)
        {
            InterlockedIncrement(&mountedToggleIgnored);
            WriteLog(
                L"Mounted-camera decouple toggle was ignored because no occupied weapon station resolved; default native coupling remains active.");
        }
        InterlockedExchange(
            &mountedCameraDecoupled,
            mountedCameraControl.decoupled ? 1 : 0);

        if (mountedCameraControl.decoupled && stationValid)
        {
            if (!mountedCameraAnchorValid ||
                mountedControlObject != station.controlObject)
            {
                const auto captured =
                    stereo::CaptureD3D8MountedCameraAnchor(
                        source,
                        station.stationWorld);
                if (captured.has_value())
                {
                    mountedCameraInStation = *captured;
                    mountedControlObject = station.controlObject;
                    mountedCameraAnchorValid = true;
                    InterlockedIncrement(&mountedAnchorCaptures);
                    WriteLog(
                        L"Mounted-camera decoupling captured station=%p cameraLocal=(%.3f,%.3f,%.3f); gun yaw/pitch and pivot motion are now excluded until this occupied control object changes or the user disables decoupling.",
                        station.controlObject,
                        mountedCameraInStation.values[3][0],
                        mountedCameraInStation.values[3][1],
                        mountedCameraInStation.values[3][2]);
                }
                else
                {
                    InterlockedIncrement(&mountedRejected);
                    mountedCameraControl.decoupled = false;
                    InterlockedExchange(&mountedCameraDecoupled, 0);
                    WriteLog(
                        L"Mounted-camera decoupling failed closed to native coupling because the station-relative camera anchor was invalid.");
                }
            }

            if (mountedCameraAnchorValid &&
                mountedControlObject == station.controlObject)
            {
                const auto decoupled =
                    stereo::ComposeD3D8MountedCameraFromAnchor(
                        mountedCameraInStation,
                        station.stationWorld);
                if (decoupled.has_value())
                {
                    cameraSource = *decoupled;
                    InterlockedIncrement(&mountedDecoupledCalls);
                }
                else
                {
                    InterlockedIncrement(&mountedRejected);
                    ResetMountedCameraAnchor(true);
                    mountedCameraControl.decoupled = false;
                    InterlockedExchange(&mountedCameraDecoupled, 0);
                }
            }
        }

        lastCameraSource = cameraSource;
        lastCameraSourceValid = true;

        const auto adjusted = readable
            ? stereo::ComposeRuntimeHeadWithD3D8Camera(
                cameraSource,
                ToPose(referenceHead),
                ToPose(currentHead),
                kWorldUnitsPerMeter)
            : std::nullopt;
        if (!adjusted.has_value())
        {
            InterlockedIncrement(&rejectedTransforms);
            original(renderView, transformation);
            return;
        }

        stereo::Matrix4 finalCamera = *adjusted;
        ScopeViewFrameState scope = {};
        if (ReadScopeViewFrameState(scope))
        {
            // The exact filtered aim ray drives both scoped presentation and
            // BF1942's native-authority target. Ordinary controller motion is
            // direct; only tiny reversing jitter receives a small correction.
            // WeaponFire_Core itself remains on the stock authority path.
            const auto scoped = stereo::MakeD3D8WeaponDirectedScopeCamera(
                finalCamera,
                scope.controllerGunWorld);
            if (scoped.has_value())
            {
                finalCamera = *scoped;
                InterlockedIncrement(&scopedCalls);
            }
            else
            {
                InvalidateScopeViewFrameState(scope.weapon);
                InterlockedIncrement(&scopedRejected);
            }
        }

        TraceFireCameraFrame(
            source,
            finalCamera,
            previousSource,
            previousSourceValid,
            infantryBodyRead ? &infantryBody.world : nullptr);
        lastPresentedHead = currentHead;
        lastPresentedHeadValid = true;

        original(renderView, &finalCamera);
        appliedSourceCamera = source;
        MemoryBarrier();
        InterlockedExchange(&appliedSourceSequence, sequence);
        InterlockedIncrement(&appliedCalls);
        InterlockedExchange(&appliedSequence, sequence);
    }

    void* DispatchFrustum(
        void* renderView,
        const void* callerReturn)
    {
        (void)callerReturn;
        (void)TryApplyScopeNormalFovRestore(renderView);
        const LONG sequence =
            InterlockedCompareExchange(&requestedSequence, 0, 0);
        if (sequence <= 0 ||
            renderView == nullptr ||
            renderView != ActiveRenderView())
        {
            return originalGetFrustum(renderView);
        }

        ScopeViewFrameState scope = {};
        const bool scoped = ReadScopeViewFrameState(scope) &&
            std::isfinite(scope.normalFov) && scope.normalFov > 0.0F;
        const bool mountedDecoupled =
            InterlockedCompareExchange(
                &mountedCameraDecoupled,
                0,
                0) != 0;
        const float verticalFrustumScale =
            stereo::SelectD3D8VisibilityFrustumVerticalScale(
                mountedDecoupled);
        InterlockedIncrement(&frustumMatchingCalls);
        bool poseApplied = false;
        if (lastCameraSourceValid)
        {
            const auto adjusted = stereo::ComposeRuntimeHeadWithD3D8Camera(
                lastCameraSource,
                ToPose(referenceHead),
                ToPose(currentHead),
                kWorldUnitsPerMeter);
            std::optional<stereo::Matrix4> visibilityCamera = adjusted;
            if (adjusted.has_value() && scoped)
            {
                visibilityCamera =
                    stereo::MakeD3D8WeaponDirectedScopeCamera(
                        *adjusted,
                        scope.controllerGunWorld);
            }
            if (visibilityCamera.has_value())
            {
                // BF1942 performs this visibility query before its ordinary
                // per-view camera setter. Advance the verified active view to
                // the current HMD basis (or scoped gun basis) so culling and
                // the later rendered camera share one orientation.
                original(renderView, &*visibilityCamera);
                poseApplied = true;
            }
            else
            {
                InterlockedIncrement(&frustumRejected);
            }
        }
        else
        {
            InterlockedIncrement(&frustumNoSource);
        }

        auto* const renderViewBytes = static_cast<std::byte*>(renderView);
        float nativeFov = -1.0F;
        float nativeAspect = -1.0F;
        float visibilityFov = -1.0F;
        float visibilityAspect = -1.0F;
        bool fieldsCaptured = false;
        bool fieldsPrepared = false;
        __try
        {
            auto* const fieldOfView = reinterpret_cast<float*>(
                renderViewBytes + kRenderViewFieldOfViewOffset);
            auto* const aspect = reinterpret_cast<float*>(
                renderViewBytes + kRenderViewAspectOffset);
            nativeFov = *fieldOfView;
            nativeAspect = *aspect;
            visibilityFov =
                (scoped ? scope.normalFov : nativeFov) *
                verticalFrustumScale;
            visibilityAspect =
                nativeAspect * verticalFrustumScale;
            if (std::isfinite(nativeFov) && nativeFov > 0.0F &&
                std::isfinite(nativeAspect) && nativeAspect > 0.0F &&
                std::isfinite(visibilityFov) && visibilityFov > 0.0F &&
                std::isfinite(visibilityAspect) && visibilityAspect > 0.0F)
            {
                fieldsCaptured = true;
                *fieldOfView = visibilityFov;
                *aspect = visibilityAspect;
                renderViewBytes[kRenderViewFrustumDirtyOffset] =
                    std::byte{1};
                fieldsPrepared = true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            fieldsPrepared = false;
        }

        void* const frustum = originalGetFrustum(renderView);
        bool fieldsRestored = !fieldsCaptured;
        if (fieldsCaptured)
        {
            __try
            {
                // Only the cached visibility volume is intentionally broad.
                // Restore BF1942's FOV/aspect before the projection query so
                // ordinary rendering and exact scope zoom remain unchanged.
                *reinterpret_cast<float*>(
                    renderViewBytes + kRenderViewFieldOfViewOffset) =
                        nativeFov;
                *reinterpret_cast<float*>(
                    renderViewBytes + kRenderViewAspectOffset) =
                        nativeAspect;
                fieldsRestored = true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                fieldsRestored = false;
            }
        }

        if (!fieldsPrepared || !fieldsRestored)
        {
            if (scoped)
            {
                InvalidateScopeViewFrameState(scope.weapon);
            }
            InterlockedIncrement(&frustumRejected);
            return frustum;
        }

        InterlockedIncrement(&frustumAppliedCalls);
        if (mountedDecoupled)
        {
            InterlockedIncrement(&mountedFrustumAppliedCalls);
        }
        InterlockedExchange(&appliedFrustumSequence, sequence);
        if (mountedDecoupled && InterlockedCompareExchange(
                &firstMountedFrustumLogged,
                1,
                0) == 0)
        {
            WriteLog(
                L"Mounted-decoupled pre-cull margin reached verified RenderView::getFrustum: currentHeadPose=%d cullVerticalFov=%.6f nativeAspect=%.6f cullAspect=%.6f verticalScale=%.2f. Nearby dynamic-object roots receive a broader top/bottom visibility envelope; horizontal coverage and rendered per-eye projections are unchanged.",
                poseApplied ? 1 : 0,
                visibilityFov,
                nativeAspect,
                visibilityAspect,
                verticalFrustumScale);
        }
        else if (scoped && InterlockedCompareExchange(
                &firstScopedFrustumLogged,
                1,
                0) == 0)
        {
            WriteLog(
                L"Scoped pre-cull correction reached verified RenderView::getFrustum: currentGunPose=%d nativeScopeFov=%.6f broadVisibilityFov=%.6f nativeAspect=%.6f cullAspect=%.6f verticalScale=%.2f. Exact magnification remains in the per-eye projection.",
                poseApplied ? 1 : 0,
                nativeFov,
                visibilityFov,
                nativeAspect,
                visibilityAspect,
                verticalFrustumScale);
        }
        else if (!scoped && InterlockedCompareExchange(
                     &firstOrdinaryFrustumLogged,
                     1,
                     0) == 0)
        {
            WriteLog(
                L"Ordinary VR pre-cull correction reached verified RenderView::getFrustum: currentHeadPose=%d cullVerticalFov=%.6f nativeAspect=%.6f cullAspect=%.6f verticalScale=%.2f. Horizontal culling coverage and rendered per-eye projections are unchanged.",
                poseApplied ? 1 : 0,
                visibilityFov,
                nativeAspect,
                visibilityAspect,
                verticalFrustumScale);
        }
        return frustum;
    }

    void* ActiveRenderView() const noexcept
    {
        __try
        {
            return gameImage == nullptr
                ? nullptr
                : *reinterpret_cast<void**>(
                    gameImage + kActiveRenderViewGlobalRva);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool TryApplyScopeNormalFovRestore(void* renderView) noexcept
    {
        if (InterlockedCompareExchange(
                &scopeNormalFovRestorePending,
                0,
                0) == 0 ||
            renderView == nullptr || renderView != ActiveRenderView())
        {
            return false;
        }
        MemoryBarrier();
        const float normalFov = pendingScopeNormalFov;
        if (!std::isfinite(normalFov) || normalFov <= 0.0F)
        {
            InterlockedExchange(&scopeNormalFovRestorePending, 0);
            InterlockedIncrement(&scopeNormalFovRestoreFailures);
            return false;
        }

        bool applied = false;
        __try
        {
            auto* const renderViewBytes =
                static_cast<std::byte*>(renderView);
            *reinterpret_cast<float*>(
                renderViewBytes + kRenderViewFieldOfViewOffset) = normalFov;
            renderViewBytes[kRenderViewProjectionDirtyOffset] = std::byte{1};
            renderViewBytes[kRenderViewFrustumDirtyOffset] = std::byte{1};
            applied = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            applied = false;
        }
        if (!applied)
        {
            InterlockedIncrement(&scopeNormalFovRestoreFailures);
            return false;
        }

        InterlockedExchange(&scopeNormalFovRestorePending, 0);
        InterlockedIncrement(&scopeNormalFovRestores);
        WriteLog(
            L"Scoped-death RenderView restoration applied: normalFov=%.6f projectionDirty=1 frustumDirty=1. The deploy menu and next soldier lifetime cannot inherit the stopped 0.1-radian scope transition.",
            normalFov);
        return true;
    }

    void TraceFireCameraFrame(
        const stereo::Matrix4& source,
        const stereo::Matrix4& finalCamera,
        const stereo::Matrix4& previousSource,
        bool previousSourceValid,
        const stereo::Matrix4* infantryBodyWorld) noexcept
    {
        if (!diagnosticsEnabled)
        {
            return;
        }
        BFSoldierVrFireCameraTrace trace = {};
        if (!ReadActiveBFSoldierVrFireCameraTrace(
                trace,
                kFireCameraTraceWindowMs))
        {
            return;
        }

        if (trace.fireSequence != tracedFireSequence)
        {
            tracedFireSequence = trace.fireSequence;
            tracedFireFrames = 0;
            tracedFireLastLoggedAt = 0;
            tracedFireBaselineSource = previousSourceValid
                ? previousSource
                : source;
            tracedFireBaselineBodyValid = infantryBodyWorld != nullptr;
            tracedFireBaselineBody = tracedFireBaselineBodyValid
                ? *infantryBodyWorld
                : stereo::Matrix4{};
            tracedFireBaselineHead = lastPresentedHeadValid
                ? lastPresentedHead
                : currentHead;
            WriteLog(
                L"Weapon fire-camera trace opened for accepted local shot=%ld soldier=%p. Up to %ld frames over %lu ms will correlate source camera, absolute infantry body heading, raw OpenXR head motion, native recoil, and the generated pre-neutralization shake matrix.",
                trace.fireSequence,
                trace.soldier,
                kMaximumFireCameraTraceFramesPerShot,
                kFireCameraTraceWindowMs);
        }
        if (tracedFireFrames >= kMaximumFireCameraTraceFramesPerShot)
        {
            return;
        }

        const DWORD now = GetTickCount();
        if (tracedFireFrames > 0 &&
            now - tracedFireLastLoggedAt < kFireCameraTraceSampleIntervalMs)
        {
            return;
        }
        tracedFireLastLoggedAt = now;
        const stereo::Matrix4 generatedShake = trace.shakeValid
            ? MatrixFromFlatArray(trace.generatedShake)
            : IdentityMatrix();
        const stereo::Matrix4 identity = IdentityMatrix();
        const LONG shakeAge = trace.shakeValid
            ? static_cast<LONG>(now - trace.shakeUpdatedAt)
            : -1;
        const LONG frame = ++tracedFireFrames;
        const bool bodyValid = tracedFireBaselineBodyValid &&
            infantryBodyWorld != nullptr;
        InterlockedIncrement(&fireCameraTraceFrames);
        WriteLog(
            L"Weapon fire-camera trace shot=%ld frame=%ld dt=%lu ms soldier=%p sourceDelta(pos=%.6f basisDeg=%.5f/%.5f/%.5f) body(valid=%d deltaDeg=%.5f fwd=%.6f/%.6f/%.6f) hmdDelta(pos=%.6f angleDeg=%.5f) recoil(valid=%d/%d pitch=%.7f yaw=%.7f) generatedShake(valid=%d ageMs=%ld pos=%.6f basisDeg=%.5f/%.5f/%.5f) sourceFwd=(%.6f,%.6f,%.6f) finalFwd=(%.6f,%.6f,%.6f).",
            trace.fireSequence,
            frame,
            static_cast<unsigned long>(now - trace.firedAt),
            trace.soldier,
            MatrixTranslationDistance(source, tracedFireBaselineSource),
            MatrixBasisAngleDegrees(source, tracedFireBaselineSource, 0),
            MatrixBasisAngleDegrees(source, tracedFireBaselineSource, 1),
            MatrixBasisAngleDegrees(source, tracedFireBaselineSource, 2),
            bodyValid ? 1 : 0,
            bodyValid
                ? MatrixBasisAngleDegrees(
                    *infantryBodyWorld,
                    tracedFireBaselineBody,
                    2)
                : 0.0F,
            bodyValid ? infantryBodyWorld->values[2][0] : 0.0F,
            bodyValid ? infantryBodyWorld->values[2][1] : 0.0F,
            bodyValid ? infantryBodyWorld->values[2][2] : 0.0F,
            ViewPositionDistance(currentHead, tracedFireBaselineHead),
            ViewOrientationAngleDegrees(currentHead, tracedFireBaselineHead),
            trace.pitchValid ? 1 : 0,
            trace.yawValid ? 1 : 0,
            trace.pitch,
            trace.yaw,
            trace.shakeValid ? 1 : 0,
            shakeAge,
            MatrixTranslationDistance(generatedShake, identity),
            MatrixBasisAngleDegrees(generatedShake, identity, 0),
            MatrixBasisAngleDegrees(generatedShake, identity, 1),
            MatrixBasisAngleDegrees(generatedShake, identity, 2),
            source.values[2][0],
            source.values[2][1],
            source.values[2][2],
            finalCamera.values[2][0],
            finalCamera.values[2][1],
            finalCamera.values[2][2]);
    }

    void ResetMountedCameraAnchor(bool logReset) noexcept
    {
        if (mountedCameraAnchorValid)
        {
            InterlockedIncrement(&mountedResets);
            if (logReset)
            {
                WriteLog(
                    L"Mounted-camera decoupling released station=%p and returned to BF1942's native coupled camera.",
                    mountedControlObject);
            }
        }
        mountedControlObject = nullptr;
        mountedCameraInStation = {};
        mountedCameraAnchorValid = false;
    }

    void WriteLog(const wchar_t* format, ...) const
    {
        if (logCallback == nullptr)
        {
            return;
        }
        std::array<wchar_t, 700> message = {};
        va_list arguments;
        va_start(arguments, format);
        _vsnwprintf_s(
            message.data(),
            message.size(),
            _TRUNCATE,
            format,
            arguments);
        va_end(arguments);
        logCallback(message.data());
    }

    static Impl* active;
    SetTransformationFn original = nullptr;
    GetFrustumFn originalGetFrustum = nullptr;
    std::byte* gameImage = nullptr;
    void* target = nullptr;
    void* frustumTarget = nullptr;
    D3D8RenderViewPoseLogCallback logCallback = nullptr;
    BF1942HudToggle hudToggle = {};
    D3D8RuntimeView referenceHead = {};
    D3D8RuntimeView currentHead = {};
    stereo::Matrix4 lastSource = {};
    stereo::Matrix4 lastCameraSource = {};
    stereo::Matrix4 tracedFireBaselineSource = {};
    stereo::Matrix4 tracedFireBaselineBody = {};
    stereo::Matrix4 appliedSourceCamera = {};
    stereo::Matrix4 mountedCameraInStation = {};
    const void* mountedControlObject = nullptr;
    bool lastSourceValid = false;
    bool lastCameraSourceValid = false;
    D3D8RuntimeView lastPresentedHead = {};
    D3D8RuntimeView tracedFireBaselineHead = {};
    bool lastPresentedHeadValid = false;
    bool tracedFireBaselineBodyValid = false;
    bool mountedCameraAnchorValid = false;
    volatile LONG requestedSequence = 0;
    volatile LONG appliedSequence = 0;
    volatile LONG appliedSourceSequence = 0;
    volatile LONG matchingCalls = 0;
    volatile LONG appliedCalls = 0;
    volatile LONG rejectedTransforms = 0;
    volatile LONG scopedCalls = 0;
    volatile LONG scopedRejected = 0;
    volatile LONG scopeNormalFovRestores = 0;
    volatile LONG scopeNormalFovRestoreFailures = 0;
    volatile LONG infantryComfortApplied = 0;
    volatile LONG infantryComfortRejected = 0;
    volatile LONG trackingContextChanges = 0;
    volatile LONG requestedTrackingContextGeneration = 0;
    volatile LONG appliedTrackingContextGeneration = 0;
    volatile LONG requestedInfantryTrackingContext = 0;
    volatile LONG requestedInfantryPresentationYawValid = 0;
    volatile LONG frustumMatchingCalls = 0;
    volatile LONG frustumAppliedCalls = 0;
    volatile LONG mountedFrustumAppliedCalls = 0;
    volatile LONG frustumNoSource = 0;
    volatile LONG frustumRejected = 0;
    volatile LONG mountedAnchorCaptures = 0;
    volatile LONG mountedDecoupledCalls = 0;
    volatile LONG mountedRejected = 0;
    volatile LONG mountedResets = 0;
    volatile LONG mountedToggles = 0;
    volatile LONG mountedToggleIgnored = 0;
    volatile LONG fireCameraTraceFrames = 0;
    volatile LONG requestedMountedCameraToggleSequence = 0;
    volatile LONG mountedCameraDecoupled = 0;
    volatile LONG appliedFrustumSequence = 0;
    volatile LONG firstScopedFrustumLogged = 0;
    volatile LONG firstOrdinaryFrustumLogged = 0;
    volatile LONG firstMountedFrustumLogged = 0;
    volatile LONG firstInfantryComfortLogged = 0;
    volatile LONG scopeNormalFovRestorePending = 0;
    float pendingScopeNormalFov = -1.0F;
    float requestedInfantryPresentationYawRadians = 0.0F;
    LONG tracedFireSequence = 0;
    LONG tracedFireFrames = 0;
    DWORD tracedFireLastLoggedAt = 0;
    stereo::MountedCameraControlState mountedCameraControl = {};
    bool created = false;
    bool frustumCreated = false;
    bool enabled = false;
    bool diagnosticsEnabled = true;
};

D3D8RenderViewPoseHook::Impl*
    D3D8RenderViewPoseHook::Impl::active = nullptr;

D3D8RenderViewPoseHook::D3D8RenderViewPoseHook()
    : impl_(std::make_unique<Impl>())
{
}

D3D8RenderViewPoseHook::~D3D8RenderViewPoseHook() = default;

bool D3D8RenderViewPoseHook::Create(
    void* gameImage,
    D3D8RenderViewPoseLogCallback logCallback)
{
    return impl_ != nullptr &&
        impl_->Create(gameImage, logCallback);
}

bool D3D8RenderViewPoseHook::Enable()
{
    return impl_ != nullptr && impl_->Enable();
}

void D3D8RenderViewPoseHook::UpdatePose(
    const D3D8RuntimeView& referenceHead,
    const D3D8RuntimeRenderRequest& request,
    const std::uint32_t trackingContextGeneration,
    const bool committedInfantryTrackingContext,
    const bool infantryPresentationYawValid,
    const float infantryPresentationYawRadians)
{
    if (impl_ != nullptr)
    {
        impl_->UpdatePose(
            referenceHead,
            request,
            trackingContextGeneration,
            committedInfantryTrackingContext,
            infantryPresentationYawValid,
            infantryPresentationYawRadians);
    }
}

void D3D8RenderViewPoseHook::ClearPose() noexcept
{
    if (impl_ != nullptr)
    {
        impl_->ClearPose();
    }
}

void D3D8RenderViewPoseHook::RequestScopeNormalFovRestore(
    const float normalFov) noexcept
{
    if (impl_ != nullptr)
    {
        impl_->RequestScopeNormalFovRestore(normalFov);
    }
}

bool D3D8RenderViewPoseHook::WasApplied(LONG sequence) const noexcept
{
    return impl_ != nullptr && impl_->WasApplied(sequence);
}

bool D3D8RenderViewPoseHook::TryGetAppliedSourceCamera(
    LONG sequence,
    stereo::Matrix4& sourceCamera) const noexcept
{
    return impl_ != nullptr &&
        impl_->TryGetAppliedSourceCamera(sequence, sourceCamera);
}

bool D3D8RenderViewPoseHook::IsMountedCameraDecoupled() const noexcept
{
    return impl_ != nullptr && impl_->IsMountedCameraDecoupled();
}

void D3D8RenderViewPoseHook::DisableAndRemove()
{
    if (impl_ != nullptr)
    {
        impl_->DisableAndRemove();
    }
}

void D3D8RenderViewPoseHook::LogSummary() const
{
    if (impl_ != nullptr)
    {
        impl_->LogSummary();
    }
}

} // namespace bfvr
