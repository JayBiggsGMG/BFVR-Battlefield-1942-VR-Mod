#pragma once

#include "presenter/SharedPresentationProtocol.h"
#include "presenter/SharedTextureProducer.h"

#include "bfvr_shared_bridge.hpp"

#include <array>
#include <cstddef>

namespace bfvr
{

class D3D8To9SharedTextureProducer
{
public:
    bool Resolve() noexcept;
    bool CreateTargets(
        void* d3d8Device,
        const shared::SharedTextureRequirements& requirements,
        std::array<void*, shared::kTextureCount>& surfaces,
        std::array<shared::SharedTextureDescription, shared::kTextureCount>&
            descriptions) const;
    bool CreateDepthTargets(
        void* d3d8Device,
        const shared::SharedTextureRequirements& requirements,
        std::array<void*, shared::kDepthTextureCount>& depthSurfaces,
        std::array<void*, shared::kDepthTextureCount>& exportSurfaces,
        std::array<
            shared::SharedTextureDescription,
            shared::kDepthTextureCount>& descriptions) const;
    bool ResolveDepthTargets(
        void* d3d8Device,
        const std::array<void*, shared::kDepthTextureCount>& depthSurfaces,
        const std::array<void*, shared::kDepthTextureCount>& exportSurfaces,
        std::array<
            BFVRD3D8To9DepthExportTiming,
            shared::kDepthTextureCount>& timings) const;
    bool WaitForGpu(void* d3d8Device, DWORD timeoutMs) const;

    [[nodiscard]] bool IsAvailable() const noexcept;
    [[nodiscard]] HRESULT LastCreateResult() const noexcept;
    [[nodiscard]] HRESULT SmallProbeResult() const noexcept;
    [[nodiscard]] HRESULT LastDepthCreateResult() const noexcept;
    [[nodiscard]] HRESULT LastDepthResolveResult() const noexcept;
    [[nodiscard]] std::size_t FailedTargetIndex() const noexcept;
    [[nodiscard]] const BFVRD3D8To9SharedDeviceDiagnostics&
        DeviceDiagnostics() const noexcept;

private:
    BFVRD3D8To9CreateSharedRenderTargetFn createSharedTarget_ = nullptr;
    BFVRD3D8To9CreateTextureBackedDepthStencilFn createDepthTarget_ = nullptr;
    BFVRD3D8To9ResolveDepthToSharedTargetFn resolveDepthTarget_ = nullptr;
    BFVRD3D8To9WaitForGpuFn waitForGpu_ = nullptr;
    BFVRD3D8To9GetSharedDeviceDiagnosticsFn
        getDeviceDiagnostics_ = nullptr;
    mutable HRESULT lastCreateResult_ = S_OK;
    mutable HRESULT smallProbeResult_ = E_PENDING;
    mutable HRESULT lastDepthCreateResult_ = E_PENDING;
    mutable HRESULT lastDepthResolveResult_ = E_PENDING;
    mutable std::size_t failedTargetIndex_ = shared::kTextureCount;
    mutable BFVRD3D8To9SharedDeviceDiagnostics deviceDiagnostics_ = {};
};

} // namespace bfvr
