#pragma once

#include "client/D3D8StereoProbeRecords.h"

#include <cstddef>
#include <vector>

namespace bfvr::d3d8probe
{

using CreateImageSurfaceCallback = HRESULT(WINAPI*)(
    void* device,
    UINT width,
    UINT height,
    UINT format,
    void** returnedSurface);
using CopyRectsCallback = HRESULT(WINAPI*)(
    void* device,
    void* sourceSurface,
    const RECT* sourceRectangles,
    UINT rectangleCount,
    void* destinationSurface,
    const POINT* destinationPoints);
using ReleaseUnknownCallback = ULONG (*)(void*& unknown);
using AcceptSurfaceMethodCallback = bool (*)(const void* target);

struct D3D8StereoReadbackApi
{
    CreateImageSurfaceCallback createImageSurface = nullptr;
    CopyRectsCallback copyRects = nullptr;
    ReleaseUnknownCallback releaseUnknown = nullptr;
    AcceptSurfaceMethodCallback acceptSurfaceMethod = nullptr;
};

struct D3D8LockedReadback
{
    void* surface = nullptr;
    const void* data = nullptr;
    UINT rowPitch = 0;
    UINT width = 0;
    UINT height = 0;
    HRESULT(WINAPI* unlockSurface)(void* surface) = nullptr;
};

ReadbackResult BeginReusableReadback(
    const D3D8StereoReadbackApi& api,
    void* device,
    void* sourceSurface,
    const D3DSurfaceDescription& description,
    DWORD clearColor,
    void*& reusableImageSurface,
    D3DSurfaceDescription& reusableDescription,
    D3D8LockedReadback& lockedReadback,
    bool analyzePixels);

bool FinishReusableReadback(
    D3D8LockedReadback& lockedReadback,
    ReadbackResult& result) noexcept;

ULONG ReleaseReusableReadback(
    const D3D8StereoReadbackApi& api,
    void*& reusableImageSurface) noexcept;

ReadbackResult ReadbackOwnedTarget(
    const D3D8StereoReadbackApi& api,
    void* device,
    void* sourceSurface,
    const D3DSurfaceDescription& description,
    DWORD clearColor,
    std::vector<std::byte>* capturedPixels,
    bool analyzePixels);

[[nodiscard]] bool IsReadbackComplete(
    const ReadbackResult& result) noexcept;

[[nodiscard]] bool IsReadbackTransferComplete(
    const ReadbackResult& result) noexcept;

} // namespace bfvr::d3d8probe
