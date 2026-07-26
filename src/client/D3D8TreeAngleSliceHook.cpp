#include "client/D3D8TreeAngleSliceHook.h"

#include "stereo/TreeAngleSlicePolicy.h"

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
constexpr std::ptrdiff_t kTreeMeshDrawBlocksRva = 0x0027C830;
constexpr std::ptrdiff_t kNormalGroupZeroCallerReturnRva = 0x0027DDB3;
constexpr std::uint32_t kAngleSelectedAlphaGroup = 0;

bool IsVerifiedDrawBlocksTarget(const void* target)
{
    constexpr BYTE kExpectedPrefix[] = {
        0x83, 0xEC, 0x1C, 0x53, 0x55, 0x56, 0x8B, 0x74,
        0x24, 0x30, 0x85, 0xF6, 0x57, 0x8B, 0xE9};
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
class D3D8TreeAngleSliceHook::Impl
{
public:
    using DrawBlocksFn = void*(__thiscall*)(
        void* treeMesh,
        std::uint32_t group,
        void* renderContext,
        std::uint32_t angleIndex);

    struct ActiveContext
    {
        const Impl* owner = nullptr;
        stereo::BF1942TreeAngleSliceContext angle = {};
        bool valid = false;
    };

    bool Create(void* image, D3D8TreeAngleSliceLogCallback callback)
    {
        gameImage = static_cast<std::byte*>(image);
        logCallback = callback;
        target = gameImage == nullptr
            ? nullptr
            : gameImage + kTreeMeshDrawBlocksRva;
        normalGroupZeroCallerReturn = gameImage == nullptr
            ? nullptr
            : gameImage + kNormalGroupZeroCallerReturnRva;
        if (!IsVerifiedDrawBlocksTarget(target))
        {
            WriteLog(
                L"TreeMesh angle-slice hook rejected target=%p: the profiled 0x0067C830 prefix did not match.",
                target);
            return false;
        }
        const MH_STATUS status = MH_CreateHook(
            target,
            reinterpret_cast<LPVOID>(&Impl::Hook),
            reinterpret_cast<LPVOID*>(&original));
        created = status == MH_OK && original != nullptr;
        if (!created)
        {
            WriteLog(
                L"TreeMesh angle-slice hook could not create target=%p status=%d trampoline=%p.",
                target,
                static_cast<int>(status),
                reinterpret_cast<void*>(original));
            return false;
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
        enabled = status == MH_OK;
        if (!enabled)
        {
            WriteLog(
                L"TreeMesh angle-slice hook could not enable target=%p status=%d.",
                target,
                static_cast<int>(status));
        }
        return enabled;
    }

    std::optional<std::uint32_t> RemapEyeStartIndex(
        const stereo::Vec3& sourceCamera,
        const stereo::Vec3& eyeCamera,
        std::uint32_t originalStartIndex,
        std::uint32_t primitiveCount) noexcept
    {
        const ActiveContext context = threadContext;
        if (!enabled || context.owner != this || !context.valid)
        {
            return std::nullopt;
        }
        InterlockedIncrement(&examinedEyeDraws);
        const auto sourceIndex =
            stereo::SelectBF1942TreeAngleSlice(
                context.angle,
                sourceCamera);
        if (!sourceIndex.has_value() ||
            *sourceIndex != context.angle.centreAngleIndex)
        {
            InterlockedIncrement(&sourceIndexMismatches);
            InterlockedIncrement(&rejectedEyeDraws);
            return std::nullopt;
        }
        const auto eyeIndex =
            stereo::SelectBF1942TreeAngleSlice(context.angle, eyeCamera);
        if (!eyeIndex.has_value())
        {
            InterlockedIncrement(&rejectedEyeDraws);
            return std::nullopt;
        }
        const auto eyeStart = stereo::RemapBF1942TreeAngleSliceStartIndex(
            originalStartIndex,
            primitiveCount,
            context.angle.centreAngleIndex,
            *eyeIndex);
        if (!eyeStart.has_value())
        {
            InterlockedIncrement(&rejectedEyeDraws);
            return std::nullopt;
        }
        if (*eyeStart != originalStartIndex)
        {
            InterlockedIncrement(&remappedEyeDraws);
        }
        else
        {
            InterlockedIncrement(&unchangedEyeDraws);
        }
        return eyeStart;
    }

    void DisableAndRemove()
    {
        if (enabled)
        {
            MH_DisableHook(target);
            enabled = false;
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
        target = nullptr;
        normalGroupZeroCallerReturn = nullptr;
        gameImage = nullptr;
        threadContext = {};
    }

    void LogSummary() const
    {
        WriteLog(
            L"TreeMesh per-eye angle-slice summary: exactGroupZeroCalls=%ld rejectedContexts=%ld eyeDraws=%ld sourceIndexMismatches=%ld remapped=%ld unchanged=%ld rejectedEyeDraws=%ld.",
            exactGroupZeroCalls,
            rejectedContexts,
            examinedEyeDraws,
            sourceIndexMismatches,
            remappedEyeDraws,
            unchangedEyeDraws,
            rejectedEyeDraws);
    }

private:
    static void* __fastcall Hook(
        void* treeMesh,
        void*,
        std::uint32_t group,
        void* renderContext,
        std::uint32_t angleIndex)
    {
        Impl* const self = active;
        if (self == nullptr || self->original == nullptr)
        {
            return nullptr;
        }

        const ActiveContext previous = threadContext;
        threadContext = self->CaptureContext(
            treeMesh,
            group,
            angleIndex,
            _ReturnAddress());
        void* const result =
            self->original(treeMesh, group, renderContext, angleIndex);
        threadContext = previous;
        return result;
    }

    ActiveContext CaptureContext(
        void* treeMesh,
        std::uint32_t group,
        std::uint32_t angleIndex,
        const void* callerReturn)
    {
        ActiveContext context = {};
        context.owner = this;
        if (group != kAngleSelectedAlphaGroup ||
            callerReturn != normalGroupZeroCallerReturn ||
            treeMesh == nullptr)
        {
            return context;
        }

        bool readable = false;
        __try
        {
            const auto* const tree =
                static_cast<const std::byte*>(treeMesh);
            const auto* const treeTemplate =
                *reinterpret_cast<const std::byte* const*>(tree + 0x18);
            const auto* const transformation =
                *reinterpret_cast<const std::byte* const*>(tree + 0x9C);
            if (treeTemplate != nullptr && transformation != nullptr)
            {
                context.angle.origin = {
                    *reinterpret_cast<const float*>(transformation + 0x30),
                    *reinterpret_cast<const float*>(transformation + 0x34),
                    *reinterpret_cast<const float*>(transformation + 0x38)};
                context.angle.referenceAxisX =
                    *reinterpret_cast<const float*>(transformation);
                context.angle.referenceAxisZ =
                    *reinterpret_cast<const float*>(transformation + 0x08);
                context.angle.angleAxisX =
                    *reinterpret_cast<const float*>(tree + 0x178);
                context.angle.angleAxisZ =
                    *reinterpret_cast<const float*>(tree + 0x17C);
                context.angle.angleCount =
                    *reinterpret_cast<const std::uint32_t*>(
                        treeTemplate + 0x100);
                context.angle.centreAngleIndex = angleIndex;
                readable = true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            readable = false;
        }

        const stereo::Vec3 validationCamera = {
            context.angle.origin.x + 1.0F,
            context.angle.origin.y,
            context.angle.origin.z};
        context.valid =
            readable &&
            stereo::SelectBF1942TreeAngleSlice(
                context.angle,
                validationCamera)
                .has_value();
        if (context.valid)
        {
            InterlockedIncrement(&exactGroupZeroCalls);
        }
        else
        {
            InterlockedIncrement(&rejectedContexts);
        }
        return context;
    }

    void WriteLog(const wchar_t* format, ...) const
    {
        if (logCallback == nullptr)
        {
            return;
        }
        std::array<wchar_t, 512> message = {};
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
    static thread_local ActiveContext threadContext;
    std::byte* gameImage = nullptr;
    void* target = nullptr;
    void* normalGroupZeroCallerReturn = nullptr;
    DrawBlocksFn original = nullptr;
    D3D8TreeAngleSliceLogCallback logCallback = nullptr;
    volatile LONG exactGroupZeroCalls = 0;
    volatile LONG rejectedContexts = 0;
    volatile LONG examinedEyeDraws = 0;
    volatile LONG sourceIndexMismatches = 0;
    volatile LONG remappedEyeDraws = 0;
    volatile LONG unchangedEyeDraws = 0;
    volatile LONG rejectedEyeDraws = 0;
    bool created = false;
    bool enabled = false;
};

D3D8TreeAngleSliceHook::Impl* D3D8TreeAngleSliceHook::Impl::active = nullptr;
thread_local D3D8TreeAngleSliceHook::Impl::ActiveContext
    D3D8TreeAngleSliceHook::Impl::threadContext = {};

D3D8TreeAngleSliceHook::D3D8TreeAngleSliceHook()
    : impl_(std::make_unique<Impl>())
{
}

D3D8TreeAngleSliceHook::~D3D8TreeAngleSliceHook() = default;

bool D3D8TreeAngleSliceHook::Create(
    void* gameImage,
    D3D8TreeAngleSliceLogCallback logCallback)
{
    return impl_ != nullptr && impl_->Create(gameImage, logCallback);
}

bool D3D8TreeAngleSliceHook::Enable()
{
    return impl_ != nullptr && impl_->Enable();
}

std::optional<std::uint32_t> D3D8TreeAngleSliceHook::RemapEyeStartIndex(
    const stereo::Vec3& sourceCamera,
    const stereo::Vec3& eyeCamera,
    std::uint32_t originalStartIndex,
    std::uint32_t primitiveCount) noexcept
{
    return impl_ == nullptr
        ? std::nullopt
        : impl_->RemapEyeStartIndex(
            sourceCamera,
            eyeCamera,
            originalStartIndex,
            primitiveCount);
}

void D3D8TreeAngleSliceHook::DisableAndRemove()
{
    if (impl_ != nullptr)
    {
        impl_->DisableAndRemove();
    }
}

void D3D8TreeAngleSliceHook::LogSummary() const
{
    if (impl_ != nullptr)
    {
        impl_->LogSummary();
    }
}

} // namespace bfvr
