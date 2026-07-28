#include "client/D3D8RenderViewPoseHook.h"

#include "stereo/StereoMath.h"

#include <MinHook.h>

#include <windows.h>

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <intrin.h>

namespace
{
constexpr std::ptrdiff_t kActiveRenderViewGlobalRva = 0x005AB868;
constexpr std::ptrdiff_t kSetTransformationRva = 0x001B7E00;
constexpr std::ptrdiff_t kExpectedCallerReturnRva = 0x000668D1;
constexpr std::ptrdiff_t kGetFrustumRva = 0x001B7E40;
constexpr std::ptrdiff_t kExpectedFrustumCallerReturnRva = 0x000665B8;
constexpr float kWorldUnitsPerMeter = 1.0F;
// PID 22844 proved the proposed caller gate never matched a live query.
// Keep the code for static/dynamic reconciliation, but do not detour every
// RenderView frustum query until the live call boundary is verified.
constexpr bool kEnableExperimentalFrustumTimingHook = false;

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
        if constexpr (kEnableExperimentalFrustumTimingHook)
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
        if constexpr (kEnableExperimentalFrustumTimingHook)
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
        MemoryBarrier();
        InterlockedExchange(&requestedSequence, request.sequence);
    }

    void ClearPose() noexcept
    {
        referenceHead = {};
        currentHead = {};
        lastSource = {};
        lastSourceValid = false;
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
    }

    void LogSummary() const
    {
        WriteLog(
            L"RenderView pose/frustum-hook summary: setterMatches=%ld setterApplied=%ld setterRejected=%ld frustumMatches=%ld frustumApplied=%ld frustumNoSource=%ld frustumRejected=%ld lastRequested=%ld lastApplied=%ld lastFrustum=%ld.",
            matchingCalls,
            appliedCalls,
            rejectedTransforms,
            frustumMatchingCalls,
            frustumAppliedCalls,
            frustumNoSource,
            frustumRejected,
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
        const auto adjusted = readable
            ? stereo::ComposeRuntimeHeadWithD3D8Camera(
                source,
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

        original(renderView, &*adjusted);
        InterlockedIncrement(&appliedCalls);
        InterlockedExchange(&appliedSequence, sequence);
    }

    void* DispatchFrustum(
        void* renderView,
        const void* callerReturn)
    {
        const void* const expectedCaller = gameImage == nullptr
            ? nullptr
            : gameImage + kExpectedFrustumCallerReturnRva;
        const LONG sequence =
            InterlockedCompareExchange(&requestedSequence, 0, 0);
        if (sequence <= 0 ||
            renderView == nullptr ||
            renderView != ActiveRenderView() ||
            callerReturn != expectedCaller)
        {
            return originalGetFrustum(renderView);
        }

        InterlockedIncrement(&frustumMatchingCalls);
        if (!lastSourceValid)
        {
            InterlockedIncrement(&frustumNoSource);
            return originalGetFrustum(renderView);
        }

        const auto adjusted = stereo::ComposeRuntimeHeadWithD3D8Camera(
            lastSource,
            ToPose(referenceHead),
            ToPose(currentHead),
            kWorldUnitsPerMeter);
        if (!adjusted.has_value())
        {
            InterlockedIncrement(&frustumRejected);
            return originalGetFrustum(renderView);
        }

        // BF1942 asks for this cached frustum before its ordinary renderer
        // supplies the current source transform. Reapply the last known
        // source with the current centre-head pose only for this verified
        // active culling query; the later setter receives the game's current
        // source transform and remains responsible for the render camera.
        original(renderView, &*adjusted);
        InterlockedIncrement(&frustumAppliedCalls);
        InterlockedExchange(&appliedFrustumSequence, sequence);
        return originalGetFrustum(renderView);
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
    bool lastSourceValid = false;
    volatile LONG requestedSequence = 0;
    volatile LONG appliedSequence = 0;
    volatile LONG matchingCalls = 0;
    volatile LONG appliedCalls = 0;
    volatile LONG rejectedTransforms = 0;
    volatile LONG frustumMatchingCalls = 0;
    volatile LONG frustumAppliedCalls = 0;
    volatile LONG frustumNoSource = 0;
    volatile LONG frustumRejected = 0;
    volatile LONG appliedFrustumSequence = 0;
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
