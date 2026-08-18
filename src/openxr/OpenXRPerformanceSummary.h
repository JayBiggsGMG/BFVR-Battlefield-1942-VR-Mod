#pragma once

#include "diagnostics/PerformanceSummary.h"
#include "openxr/OpenXRPresentation.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace bfvr
{
enum class OpenXRPerformanceStage : std::size_t
{
    WaitFrame,
    BeginFrame,
    SwapchainAcquire,
    LeftWorldSwapchainWait,
    RightWorldSwapchainWait,
    UiSwapchainWait,
    LeftWorldCopyEnqueue,
    RightWorldCopyEnqueue,
    UiCopyEnqueue,
    SwapchainRelease,
    EndFrame,
    Count
};

enum class OpenXRSwapchainPerformanceSlot
{
    LeftWorld,
    RightWorld,
    Ui
};

class OpenXRPerformanceSummary
{
public:
    OpenXRPerformanceSummary() noexcept;

    [[nodiscard]] bool IsEnabled() const noexcept;
    [[nodiscard]] std::int64_t BeginSample() const noexcept;
    void EndSample(
        OpenXRPerformanceStage stage,
        std::int64_t startedAt) noexcept;
    void ReportIfDue(
        DWORD now,
        OpenXRLogCallback logCallback,
        void* logContext);
    void ReportFinal(OpenXRLogCallback logCallback, void* logContext);
    void Reset() noexcept;

private:
    void Report(
        const wchar_t* label,
        OpenXRLogCallback logCallback,
        void* logContext) const;

    bool enabled_ = false;
    DWORD lastReportAt_ = 0;
    std::array<diagnostics::PerformanceAggregate,
        static_cast<std::size_t>(OpenXRPerformanceStage::Count)> stages_ = {};
};
} // namespace bfvr
