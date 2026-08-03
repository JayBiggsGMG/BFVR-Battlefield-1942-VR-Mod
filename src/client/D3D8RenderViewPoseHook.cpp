#include "client/D3D8RenderViewPoseHook.h"

#include "client/MountedWeaponAimResolver.h"
#include "client/ScopeViewOverlay.h"

#include "stereo/MountedCameraMath.h"
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
constexpr std::size_t kRenderViewFrustumDirtyOffset = 0x312;
constexpr float kWorldUnitsPerMeter = 1.0F;
// The scope path needs this verified boundary even though the older proposed
// ordinary-camera caller gate never matched. Runtime work remains gated to an
// exact active RenderView and a fresh useScope-enabled local handweapon.
constexpr bool kEnableScopeFrustumHook = true;

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
        if constexpr (kEnableScopeFrustumHook)
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
        if constexpr (kEnableScopeFrustumHook)
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
        const D3D8RuntimeRenderRequest& request)
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
        MemoryBarrier();
        InterlockedExchange(&requestedSequence, request.sequence);
    }

    void ClearPose() noexcept
    {
        referenceHead = {};
        currentHead = {};
        lastSource = {};
        lastSourceValid = false;
        ResetMountedCameraAnchor(false);
        MemoryBarrier();
        InterlockedExchange(&requestedSequence, 0);
        InterlockedExchange(&appliedSequence, 0);
        InterlockedExchange(&appliedFrustumSequence, 0);
    }

    bool WasApplied(LONG sequence) const noexcept
    {
        return sequence > 0 &&
            InterlockedCompareExchange(
                const_cast<volatile LONG*>(&appliedSequence),
                0,
                0) == sequence;
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
        lastSourceValid = false;
        ResetMountedCameraAnchor(false);
        mountedCameraControl = {};
        InterlockedExchange(&requestedMountedCameraToggleSequence, 0);
        InterlockedExchange(&mountedCameraDecoupled, 0);
    }

    void LogSummary() const
    {
        WriteLog(
            L"RenderView pose/frustum-hook summary: setterMatches=%ld setterApplied=%ld setterRejected=%ld scopedApplied=%ld scopedRejected=%ld frustumMatches=%ld frustumApplied=%ld frustumNoSource=%ld frustumRejected=%ld mountedAnchorCaptures=%ld mountedDecoupled=%ld mountedRejected=%ld mountedResets=%ld mountedToggles=%ld mountedToggleIgnored=%ld lastRequested=%ld lastApplied=%ld lastFrustum=%ld.",
            matchingCalls,
            appliedCalls,
            rejectedTransforms,
            scopedCalls,
            scopedRejected,
            frustumMatchingCalls,
            frustumAppliedCalls,
            frustumNoSource,
            frustumRejected,
            mountedAnchorCaptures,
            mountedDecoupledCalls,
            mountedRejected,
            mountedResets,
            mountedToggles,
            mountedToggleIgnored,
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

        stereo::Matrix4 cameraSource = source;
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
        const stereo::MountedCameraControlTransition transition =
            stereo::UpdateMountedCameraControl(
                mountedCameraControl,
                stationValid
                    ? reinterpret_cast<std::uintptr_t>(
                        station.controlObject)
                    : 0,
                toggleSequence);
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
            const auto scoped =
                stereo::MakeD3D8WeaponDirectedScopeCamera(
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

        original(renderView, &finalCamera);
        InterlockedIncrement(&appliedCalls);
        InterlockedExchange(&appliedSequence, sequence);
    }

    void* DispatchFrustum(
        void* renderView,
        const void* callerReturn)
    {
        (void)callerReturn;
        const LONG sequence =
            InterlockedCompareExchange(&requestedSequence, 0, 0);
        ScopeViewFrameState scope = {};
        if (sequence <= 0 ||
            renderView == nullptr ||
            renderView != ActiveRenderView() ||
            !ReadScopeViewFrameState(scope) ||
            !std::isfinite(scope.normalFov) ||
            scope.normalFov <= 0.0F)
        {
            return originalGetFrustum(renderView);
        }

        InterlockedIncrement(&frustumMatchingCalls);
        bool poseApplied = false;
        if (lastSourceValid)
        {
            const auto adjusted = stereo::ComposeRuntimeHeadWithD3D8Camera(
                lastSource,
                ToPose(referenceHead),
                ToPose(currentHead),
                kWorldUnitsPerMeter);
            const auto scoped = adjusted.has_value()
                ? stereo::MakeD3D8WeaponDirectedScopeCamera(
                    *adjusted,
                    scope.controllerGunWorld)
                : std::nullopt;
            if (scoped.has_value())
            {
                // BF1942 performs this visibility query before its ordinary
                // per-view camera setter. Advance the verified active
                // RenderView to the current gun direction here so its scope
                // frustum and the later rendered camera share one basis.
                original(renderView, &*scoped);
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
        bool fovCaptured = false;
        bool fovPrepared = false;
        __try
        {
            auto* const fieldOfView = reinterpret_cast<float*>(
                renderViewBytes + kRenderViewFieldOfViewOffset);
            nativeFov = *fieldOfView;
            if (std::isfinite(nativeFov) && nativeFov > 0.0F)
            {
                fovCaptured = true;
                *fieldOfView = scope.normalFov;
                renderViewBytes[kRenderViewFrustumDirtyOffset] =
                    std::byte{1};
                fovPrepared = true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            fovPrepared = false;
        }

        void* const frustum = originalGetFrustum(renderView);
        bool fovRestored = !fovCaptured;
        if (fovCaptured)
        {
            __try
            {
                // Only the cached visibility volume is intentionally broad.
                // Restore BF1942's scope FOV before the projection query so
                // BFVR can preserve the weapon's exact requested zoom.
                *reinterpret_cast<float*>(
                    renderViewBytes + kRenderViewFieldOfViewOffset) =
                        nativeFov;
                fovRestored = true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                fovRestored = false;
            }
        }

        if (!fovPrepared || !fovRestored)
        {
            InvalidateScopeViewFrameState(scope.weapon);
            InterlockedIncrement(&frustumRejected);
            return frustum;
        }

        InterlockedIncrement(&frustumAppliedCalls);
        InterlockedExchange(&appliedFrustumSequence, sequence);
        if (InterlockedCompareExchange(
                &firstScopedFrustumLogged,
                1,
                0) == 0)
        {
            WriteLog(
                L"Scoped pre-cull correction reached verified RenderView::getFrustum: currentGunPose=%d nativeScopeFov=%.6f broadVisibilityFov=%.6f. Exact magnification remains in the per-eye projection.",
                poseApplied ? 1 : 0,
                nativeFov,
                scope.normalFov);
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
    D3D8RuntimeView referenceHead = {};
    D3D8RuntimeView currentHead = {};
    stereo::Matrix4 lastSource = {};
    stereo::Matrix4 mountedCameraInStation = {};
    const void* mountedControlObject = nullptr;
    bool lastSourceValid = false;
    bool mountedCameraAnchorValid = false;
    volatile LONG requestedSequence = 0;
    volatile LONG appliedSequence = 0;
    volatile LONG matchingCalls = 0;
    volatile LONG appliedCalls = 0;
    volatile LONG rejectedTransforms = 0;
    volatile LONG scopedCalls = 0;
    volatile LONG scopedRejected = 0;
    volatile LONG frustumMatchingCalls = 0;
    volatile LONG frustumAppliedCalls = 0;
    volatile LONG frustumNoSource = 0;
    volatile LONG frustumRejected = 0;
    volatile LONG mountedAnchorCaptures = 0;
    volatile LONG mountedDecoupledCalls = 0;
    volatile LONG mountedRejected = 0;
    volatile LONG mountedResets = 0;
    volatile LONG mountedToggles = 0;
    volatile LONG mountedToggleIgnored = 0;
    volatile LONG requestedMountedCameraToggleSequence = 0;
    volatile LONG mountedCameraDecoupled = 0;
    volatile LONG appliedFrustumSequence = 0;
    volatile LONG firstScopedFrustumLogged = 0;
    stereo::MountedCameraControlState mountedCameraControl = {};
    bool created = false;
    bool frustumCreated = false;
    bool enabled = false;
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
    const D3D8RuntimeRenderRequest& request)
{
    if (impl_ != nullptr)
    {
        impl_->UpdatePose(referenceHead, request);
    }
}

void D3D8RenderViewPoseHook::ClearPose() noexcept
{
    if (impl_ != nullptr)
    {
        impl_->ClearPose();
    }
}

bool D3D8RenderViewPoseHook::WasApplied(LONG sequence) const noexcept
{
    return impl_ != nullptr && impl_->WasApplied(sequence);
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
