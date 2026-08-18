#pragma once

#include "diagnostics/PerformanceSummary.h"
#include "presenter/SharedPresentationProtocol.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace bfvr::shared
{
enum class SharedTexturePerformanceStage : std::size_t
{
    SharedAcquire,
    ScreenSpaceGlobalIlluminationEnqueue,
    WaterReflectionEnqueue,
    AmbientOcclusionEnqueue,
    CompositeEnqueue,
    MenuOverlayEnqueue,
    Flush,
    Count
};

class SharedTexturePerformanceSummary
{
public:
    SharedTexturePerformanceSummary() noexcept;

    [[nodiscard]] std::int64_t BeginSample() const noexcept;
    void EndSample(
        SharedTexturePerformanceStage stage,
        std::int64_t startedAt) noexcept;
    void ReportIfDue(
        DWORD now,
        SharedTextureLogCallback logCallback,
        void* logContext);
    void ReportFinal(
        SharedTextureLogCallback logCallback,
        void* logContext);
    void Reset() noexcept;

private:
    void Report(
        const wchar_t* label,
        SharedTextureLogCallback logCallback,
        void* logContext) const;

    bool enabled_ = false;
    DWORD lastReportAt_ = 0;
    std::array<diagnostics::PerformanceAggregate,
        static_cast<std::size_t>(
            SharedTexturePerformanceStage::Count)> stages_ = {};
};
} // namespace bfvr::shared
