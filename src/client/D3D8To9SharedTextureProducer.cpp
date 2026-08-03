#include "client/D3D8To9SharedTextureProducer.h"

#include <dxgiformat.h>

namespace
{
constexpr DWORD kD3DFormatA2B10G10R10 = 31;
constexpr DWORD kD3DFormatA8R8G8B8 = 21;
constexpr DWORD kD3DFormatA16B16G16R16F = 113;

void ReleaseSurface(void*& surface)
{
    if (surface != nullptr)
    {
        reinterpret_cast<IUnknown*>(surface)->Release();
        surface = nullptr;
    }
}
} // namespace

namespace bfvr
{
bool D3D8To9SharedTextureProducer::Resolve() noexcept
{
    createSharedTarget_ = nullptr;
    createDepthTarget_ = nullptr;
    resolveDepthTarget_ = nullptr;
    waitForGpu_ = nullptr;
    const HMODULE translator = GetModuleHandleW(L"BFVRD3D8To9.dll");
    if (translator == nullptr)
    {
        return false;
    }
    const auto getVersion =
        reinterpret_cast<BFVRD3D8To9GetSharedBridgeVersionFn>(
            GetProcAddress(
                translator,
                "BFVRD3D8To9GetSharedBridgeVersion"));
    createSharedTarget_ =
        reinterpret_cast<BFVRD3D8To9CreateSharedRenderTargetFn>(
            GetProcAddress(
                translator,
                "BFVRD3D8To9CreateSharedRenderTarget"));
    createDepthTarget_ =
        reinterpret_cast<BFVRD3D8To9CreateTextureBackedDepthStencilFn>(
            GetProcAddress(
                translator,
                "BFVRD3D8To9CreateTextureBackedDepthStencil"));
    resolveDepthTarget_ =
        reinterpret_cast<BFVRD3D8To9ResolveDepthToSharedTargetFn>(
            GetProcAddress(
                translator,
                "BFVRD3D8To9ResolveDepthToSharedTarget"));
    waitForGpu_ =
        reinterpret_cast<BFVRD3D8To9WaitForGpuFn>(
            GetProcAddress(
                translator,
                "BFVRD3D8To9WaitForGpu"));
    getDeviceDiagnostics_ =
        reinterpret_cast<BFVRD3D8To9GetSharedDeviceDiagnosticsFn>(
            GetProcAddress(
                translator,
                "BFVRD3D8To9GetSharedDeviceDiagnostics"));
    if (getVersion == nullptr ||
        getVersion() != BFVR_D3D8TO9_SHARED_BRIDGE_VERSION ||
        createSharedTarget_ == nullptr ||
        createDepthTarget_ == nullptr ||
        resolveDepthTarget_ == nullptr ||
        waitForGpu_ == nullptr ||
        getDeviceDiagnostics_ == nullptr)
    {
        createSharedTarget_ = nullptr;
        createDepthTarget_ = nullptr;
        resolveDepthTarget_ = nullptr;
        waitForGpu_ = nullptr;
        getDeviceDiagnostics_ = nullptr;
        return false;
    }
    return true;
}

bool D3D8To9SharedTextureProducer::CreateDepthTargets(
    void* d3d8Device,
    const shared::SharedTextureRequirements& requirements,
    std::array<void*, shared::kDepthTextureCount>& depthSurfaces,
    std::array<void*, shared::kDepthTextureCount>& exportSurfaces,
    std::array<
        shared::SharedTextureDescription,
        shared::kDepthTextureCount>& descriptions) const
{
    depthSurfaces = {};
    exportSurfaces = {};
    descriptions = {};
    lastDepthCreateResult_ = E_PENDING;
    if (!IsAvailable() || d3d8Device == nullptr)
    {
        lastDepthCreateResult_ = E_NOINTERFACE;
        return false;
    }

    const std::array<UINT, shared::kDepthTextureCount> widths = {
        requirements.leftWorldWidth,
        requirements.rightWorldWidth};
    const std::array<UINT, shared::kDepthTextureCount> heights = {
        requirements.leftWorldHeight,
        requirements.rightWorldHeight};
    bool created = true;
    for (std::size_t index = 0; index < depthSurfaces.size(); ++index)
    {
        HANDLE handle = nullptr;
        HRESULT result = createSharedTarget_(
            d3d8Device,
            widths[index],
            heights[index],
            kD3DFormatA8R8G8B8,
            &handle,
            &exportSurfaces[index]);
        result = SUCCEEDED(result)
            ? createDepthTarget_(
                d3d8Device,
                widths[index],
                heights[index],
                kD3DFormatA2B10G10R10,
                &depthSurfaces[index])
            : result;
        lastDepthCreateResult_ = result;
        created = created &&
            SUCCEEDED(result) &&
            handle != nullptr &&
            exportSurfaces[index] != nullptr &&
            depthSurfaces[index] != nullptr;
        if (!created)
        {
            if (SUCCEEDED(lastDepthCreateResult_))
                lastDepthCreateResult_ = E_FAIL;
            break;
        }

        shared::SharedTextureDescription& description = descriptions[index];
        description.width = widths[index];
        description.height = heights[index];
        description.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.transport = static_cast<DWORD>(
            shared::SharedTextureTransport::D3D9LegacyHandle);
        shared::StoreLegacySharedHandle(description, handle);
    }
    if (!created)
    {
        for (void*& surface : depthSurfaces)
            ReleaseSurface(surface);
        for (void*& surface : exportSurfaces)
            ReleaseSurface(surface);
        descriptions = {};
    }
    return created;
}

bool D3D8To9SharedTextureProducer::ResolveDepthTargets(
    void* d3d8Device,
    const std::array<void*, shared::kDepthTextureCount>& depthSurfaces,
    const std::array<void*, shared::kDepthTextureCount>& exportSurfaces,
    std::array<
        BFVRD3D8To9DepthExportTiming,
        shared::kDepthTextureCount>& timings) const
{
    timings = {};
    lastDepthResolveResult_ = E_PENDING;
    if (!IsAvailable() || d3d8Device == nullptr)
    {
        lastDepthResolveResult_ = E_NOINTERFACE;
        return false;
    }
    for (std::size_t eye = 0; eye < depthSurfaces.size(); ++eye)
    {
        timings[eye].size = sizeof(timings[eye]);
        lastDepthResolveResult_ = resolveDepthTarget_(
            d3d8Device,
            depthSurfaces[eye],
            exportSurfaces[eye],
            static_cast<DWORD>(
                BFVRD3D8To9DepthExportEncoding::PackedRgba8),
            &timings[eye]);
        if (FAILED(lastDepthResolveResult_))
            return false;
    }
    return true;
}

bool D3D8To9SharedTextureProducer::CreateTargets(
    void* d3d8Device,
    const shared::SharedTextureRequirements& requirements,
    std::array<void*, shared::kTextureCount>& surfaces,
    std::array<shared::SharedTextureDescription, shared::kTextureCount>&
        descriptions) const
{
    surfaces = {};
    descriptions = {};
    lastCreateResult_ = S_OK;
    smallProbeResult_ = E_PENDING;
    failedTargetIndex_ = shared::kTextureCount;
    deviceDiagnostics_ = {};
    deviceDiagnostics_.size = sizeof(deviceDiagnostics_);
    if (!IsAvailable() || d3d8Device == nullptr)
    {
        lastCreateResult_ = E_NOINTERFACE;
        return false;
    }

    constexpr std::array<DWORD, shared::kTextureCount> d3dFormats = {
        kD3DFormatA2B10G10R10,
        kD3DFormatA2B10G10R10,
        kD3DFormatA16B16G16R16F};
    constexpr std::array<DXGI_FORMAT, shared::kTextureCount> dxgiFormats = {
        DXGI_FORMAT_R10G10B10A2_UNORM,
        DXGI_FORMAT_R10G10B10A2_UNORM,
        DXGI_FORMAT_R16G16B16A16_FLOAT};
    const std::array<UINT, shared::kTextureCount> widths = {
        requirements.leftWorldWidth,
        requirements.rightWorldWidth,
        requirements.uiWidth};
    const std::array<UINT, shared::kTextureCount> heights = {
        requirements.leftWorldHeight,
        requirements.rightWorldHeight,
        requirements.uiHeight};

    bool created = true;
    for (std::size_t index = 0; index < surfaces.size(); ++index)
    {
        HANDLE handle = nullptr;
        const HRESULT result = createSharedTarget_(
            d3d8Device,
            widths[index],
            heights[index],
            d3dFormats[index],
            &handle,
            &surfaces[index]);
        lastCreateResult_ = result;
        created = created &&
            SUCCEEDED(result) &&
            handle != nullptr &&
            surfaces[index] != nullptr;
        if (!created)
        {
            failedTargetIndex_ = index;
            if (SUCCEEDED(lastCreateResult_))
            {
                lastCreateResult_ = E_FAIL;
            }
            HANDLE smallHandle = nullptr;
            void* smallSurface = nullptr;
            smallProbeResult_ = createSharedTarget_(
                d3d8Device,
                64,
                64,
                d3dFormats[index],
                &smallHandle,
                &smallSurface);
            ReleaseSurface(smallSurface);
            getDeviceDiagnostics_(
                d3d8Device,
                &deviceDiagnostics_);
            break;
        }
        shared::SharedTextureDescription& description =
            descriptions[index];
        description.width = widths[index];
        description.height = heights[index];
        description.format = static_cast<DWORD>(dxgiFormats[index]);
        description.transport = static_cast<DWORD>(
            shared::SharedTextureTransport::D3D9LegacyHandle);
        shared::StoreLegacySharedHandle(description, handle);
    }
    if (!created)
    {
        for (void*& surface : surfaces)
        {
            ReleaseSurface(surface);
        }
        descriptions = {};
    }
    else
    {
        getDeviceDiagnostics_(
            d3d8Device,
            &deviceDiagnostics_);
    }
    return created;
}

bool D3D8To9SharedTextureProducer::WaitForGpu(
    void* d3d8Device,
    DWORD timeoutMs) const
{
    return IsAvailable() &&
        d3d8Device != nullptr &&
        SUCCEEDED(waitForGpu_(d3d8Device, timeoutMs));
}

bool D3D8To9SharedTextureProducer::IsAvailable() const noexcept
{
    return createSharedTarget_ != nullptr &&
        createDepthTarget_ != nullptr &&
        resolveDepthTarget_ != nullptr &&
        waitForGpu_ != nullptr;
}

HRESULT D3D8To9SharedTextureProducer::LastDepthCreateResult() const noexcept
{
    return lastDepthCreateResult_;
}

HRESULT D3D8To9SharedTextureProducer::LastDepthResolveResult() const noexcept
{
    return lastDepthResolveResult_;
}

HRESULT D3D8To9SharedTextureProducer::LastCreateResult() const noexcept
{
    return lastCreateResult_;
}

HRESULT D3D8To9SharedTextureProducer::SmallProbeResult() const noexcept
{
    return smallProbeResult_;
}

std::size_t D3D8To9SharedTextureProducer::FailedTargetIndex() const noexcept
{
    return failedTargetIndex_;
}

const BFVRD3D8To9SharedDeviceDiagnostics&
D3D8To9SharedTextureProducer::DeviceDiagnostics() const noexcept
{
    return deviceDiagnostics_;
}
} // namespace bfvr
