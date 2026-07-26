#include "client/D3D8StereoReadback.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace
{
constexpr std::size_t kLockRectSlot = 9;
constexpr std::size_t kUnlockRectSlot = 10;
constexpr DWORD kLockReadOnly = 0x10;
constexpr UINT kA8R8G8B8 = 21;
constexpr UINT kX8R8G8B8 = 22;

struct D3DLockedRect
{
    INT pitch;
    void* bits;
};
static_assert(sizeof(D3DLockedRect) == sizeof(void*) * 2);

using LockRectFn = HRESULT(WINAPI*)(
    void* surface,
    D3DLockedRect* lockedRect,
    const RECT* rectangle,
    DWORD flags);
using UnlockRectFn = HRESULT(WINAPI*)(void* surface);

bool MatchesReadbackDescription(
    const bfvr::d3d8probe::D3DSurfaceDescription& left,
    const bfvr::d3d8probe::D3DSurfaceDescription& right) noexcept
{
    return left.width == right.width &&
        left.height == right.height &&
        left.format == right.format;
}
} // namespace

namespace bfvr::d3d8probe
{

ReadbackResult BeginReusableReadback(
    const D3D8StereoReadbackApi& api,
    void* device,
    void* sourceSurface,
    const D3DSurfaceDescription& description,
    DWORD clearColor,
    void*& reusableImageSurface,
    D3DSurfaceDescription& reusableDescription,
    D3D8LockedReadback& lockedReadback,
    bool analyzePixels)
{
    ReadbackResult result = {};
    lockedReadback = {};
    if (api.createImageSurface == nullptr ||
        api.copyRects == nullptr ||
        api.releaseUnknown == nullptr ||
        api.acceptSurfaceMethod == nullptr ||
        device == nullptr ||
        sourceSurface == nullptr ||
        description.width == 0 ||
        description.height == 0)
    {
        return result;
    }

    if (reusableImageSurface != nullptr &&
        !MatchesReadbackDescription(reusableDescription, description))
    {
        if (api.releaseUnknown(reusableImageSurface) != 0)
        {
            reusableImageSurface = nullptr;
            reusableDescription = {};
            return result;
        }
        reusableDescription = {};
    }

    if (reusableImageSurface == nullptr)
    {
        result.createResult = api.createImageSurface(
            device,
            description.width,
            description.height,
            description.format,
            &reusableImageSurface);
        if (FAILED(result.createResult) || reusableImageSurface == nullptr)
        {
            return result;
        }
        reusableDescription = description;
    }
    else
    {
        result.createResult = S_OK;
    }

    result.copyResult = api.copyRects(
        device,
        sourceSurface,
        nullptr,
        0,
        reusableImageSurface,
        nullptr);
    if (FAILED(result.copyResult) ||
        (description.format != kA8R8G8B8 &&
         description.format != kX8R8G8B8))
    {
        return result;
    }

    auto** const vtable = *reinterpret_cast<void***>(reusableImageSurface);
    const auto lockRect = vtable == nullptr
        ? nullptr
        : reinterpret_cast<LockRectFn>(vtable[kLockRectSlot]);
    const auto unlockRect = vtable == nullptr
        ? nullptr
        : reinterpret_cast<UnlockRectFn>(vtable[kUnlockRectSlot]);
    if (lockRect == nullptr ||
        unlockRect == nullptr ||
        !api.acceptSurfaceMethod(reinterpret_cast<void*>(lockRect)) ||
        !api.acceptSurfaceMethod(reinterpret_cast<void*>(unlockRect)))
    {
        return result;
    }

    D3DLockedRect locked = {};
    result.lockResult =
        lockRect(reusableImageSurface, &locked, nullptr, kLockReadOnly);
    if (FAILED(result.lockResult) || locked.bits == nullptr || locked.pitch <= 0)
    {
        return result;
    }

    lockedReadback.surface = reusableImageSurface;
    lockedReadback.data = locked.bits;
    lockedReadback.rowPitch = static_cast<UINT>(locked.pitch);
    lockedReadback.width = description.width;
    lockedReadback.height = description.height;
    lockedReadback.unlockSurface = unlockRect;

    if (analyzePixels)
    {
        constexpr std::uint64_t kFnvOffset = 1469598103934665603ULL;
        constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
        const DWORD clearRgb = clearColor & 0x00FFFFFF;
        result.hash = kFnvOffset;
        for (UINT y = 0; y < description.height; ++y)
        {
            const auto* const row = reinterpret_cast<const DWORD*>(
                static_cast<const std::byte*>(locked.bits) +
                static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(locked.pitch));
            for (UINT x = 0; x < description.width; ++x)
            {
                const DWORD pixel = row[x];
                result.hash ^= pixel;
                result.hash *= kFnvPrime;
                if ((pixel & 0x00FFFFFF) != clearRgb)
                {
                    ++result.nonClearPixels;
                }
                if ((pixel & 0xFF000000) != 0)
                {
                    ++result.nonZeroAlphaPixels;
                }
            }
        }
    }
    return result;
}

bool FinishReusableReadback(
    D3D8LockedReadback& lockedReadback,
    ReadbackResult& result) noexcept
{
    if (lockedReadback.surface == nullptr ||
        lockedReadback.unlockSurface == nullptr)
    {
        lockedReadback = {};
        return false;
    }
    result.unlockResult =
        lockedReadback.unlockSurface(lockedReadback.surface);
    lockedReadback = {};
    return SUCCEEDED(result.unlockResult);
}

ULONG ReleaseReusableReadback(
    const D3D8StereoReadbackApi& api,
    void*& reusableImageSurface) noexcept
{
    return api.releaseUnknown == nullptr
        ? static_cast<ULONG>(-1)
        : api.releaseUnknown(reusableImageSurface);
}

ReadbackResult ReadbackOwnedTarget(
    const D3D8StereoReadbackApi& api,
    void* device,
    void* sourceSurface,
    const D3DSurfaceDescription& description,
    DWORD clearColor,
    std::vector<std::byte>* capturedPixels,
    bool analyzePixels)
{
    void* imageSurface = nullptr;
    D3DSurfaceDescription imageDescription = {};
    D3D8LockedReadback lockedReadback = {};
    ReadbackResult result = BeginReusableReadback(
        api,
        device,
        sourceSurface,
        description,
        clearColor,
        imageSurface,
        imageDescription,
        lockedReadback,
        analyzePixels);
    if (SUCCEEDED(result.lockResult) && capturedPixels != nullptr)
    {
        capturedPixels->resize(
            static_cast<std::size_t>(description.width) *
            static_cast<std::size_t>(description.height) *
            sizeof(DWORD));
        for (UINT y = 0; y < description.height; ++y)
        {
            std::memcpy(
                capturedPixels->data() +
                    static_cast<std::size_t>(y) *
                        static_cast<std::size_t>(description.width) *
                        sizeof(DWORD),
                static_cast<const std::byte*>(lockedReadback.data) +
                    static_cast<std::size_t>(y) * lockedReadback.rowPitch,
                static_cast<std::size_t>(description.width) * sizeof(DWORD));
        }
    }
    FinishReusableReadback(lockedReadback, result);
    result.releaseResult = ReleaseReusableReadback(api, imageSurface);
    return result;
}

bool IsReadbackComplete(const ReadbackResult& result) noexcept
{
    return IsReadbackTransferComplete(result) &&
        result.releaseResult == 0;
}

bool IsReadbackTransferComplete(const ReadbackResult& result) noexcept
{
    return SUCCEEDED(result.createResult) &&
        SUCCEEDED(result.copyResult) &&
        SUCCEEDED(result.lockResult) &&
        SUCCEEDED(result.unlockResult);
}

} // namespace bfvr::d3d8probe
