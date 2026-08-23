#include "openxr/OpenXRPerformanceSummary.h"

#include <windows.h>

#include <array>
#include <cstdio>
#include <iterator>

namespace bfvr
{
namespace
{
constexpr DWORD kReportPeriodMs = 30000;
}

OpenXRPerformanceSummary::OpenXRPerformanceSummary() noexcept
    : enabled_(diagnostics::ReadAggregatePerformanceEnabled())
{}

bool OpenXRPerformanceSummary::IsEnabled() const noexcept
{
    return enabled_;
}

std::int64_t OpenXRPerformanceSummary::BeginSample() const noexcept
{
    return enabled_ ? diagnostics::ReadPerformanceCounter() : 0;
}

void OpenXRPerformanceSummary::EndSample(
    OpenXRPerformanceStage stage,
    std::int64_t startedAt) noexcept
{
    if (!enabled_ || startedAt == 0)
    {
        return;
    }
    stages_[static_cast<std::size_t>(stage)].Add(
        diagnostics::ReadPerformanceCounter() - startedAt);
}

void OpenXRPerformanceSummary::ReportIfDue(
    DWORD now,
    OpenXRLogCallback logCallback,
    void* logContext)
{
    if (!enabled_)
    {
        return;
    }
    if (lastReportAt_ == 0)
    {
        lastReportAt_ = now;
        return;
    }
    if (now - lastReportAt_ < kReportPeriodMs)
    {
        return;
    }
    lastReportAt_ = now;
    Report(L"Periodic", logCallback, logContext);
}

void OpenXRPerformanceSummary::ReportFinal(
    OpenXRLogCallback logCallback,
    void* logContext)
{
    if (enabled_ &&
        stages_[static_cast<std::size_t>(
            OpenXRPerformanceStage::EndFrame)].count != 0)
    {
        Report(L"Final", logCallback, logContext);
    }
}

void OpenXRPerformanceSummary::Reset() noexcept
{
    lastReportAt_ = 0;
    stages_ = {};
}

void OpenXRPerformanceSummary::Report(
    const wchar_t* label,
    OpenXRLogCallback logCallback,
    void* logContext) const
{
    if (logCallback == nullptr)
    {
        return;
    }
    LARGE_INTEGER frequency = {};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
    {
        return;
    }
    const double millisecondsPerTick =
        1000.0 / static_cast<double>(frequency.QuadPart);
    const auto& waitFrame = stages_[static_cast<std::size_t>(
        OpenXRPerformanceStage::WaitFrame)];
    const auto& beginFrame = stages_[static_cast<std::size_t>(
        OpenXRPerformanceStage::BeginFrame)];
    const auto& acquire = stages_[static_cast<std::size_t>(
        OpenXRPerformanceStage::SwapchainAcquire)];
    const auto& leftWait = stages_[static_cast<std::size_t>(
        OpenXRPerformanceStage::LeftWorldSwapchainWait)];
    const auto& rightWait = stages_[static_cast<std::size_t>(
        OpenXRPerformanceStage::RightWorldSwapchainWait)];
    const auto& uiWait = stages_[static_cast<std::size_t>(
        OpenXRPerformanceStage::UiSwapchainWait)];
    const auto& leftCopy = stages_[static_cast<std::size_t>(
        OpenXRPerformanceStage::LeftWorldCopyEnqueue)];
    const auto& rightCopy = stages_[static_cast<std::size_t>(
        OpenXRPerformanceStage::RightWorldCopyEnqueue)];
    const auto& uiCopy = stages_[static_cast<std::size_t>(
        OpenXRPerformanceStage::UiCopyEnqueue)];
    const auto& release = stages_[static_cast<std::size_t>(
        OpenXRPerformanceStage::SwapchainRelease)];
    const auto& endFrame = stages_[static_cast<std::size_t>(
        OpenXRPerformanceStage::EndFrame)];
    std::array<wchar_t, 1400> message = {};
    _snwprintf_s(
        message.data(),
        message.size(),
        _TRUNCATE,
        L"BFVR_PERFORMANCE_SUMMARY %s OpenXR API timing avg/max: xrWaitFrame=%.3f/%.3f ms (n=%llu) xrBeginFrame=%.3f/%.3f ms (n=%llu) swapchainAcquire=%.3f/%.3f ms (n=%llu) imageWait[L/R/UI]=%.3f/%.3f,%.3f/%.3f,%.3f/%.3f ms copyEnqueue[L/R/UI]=%.3f/%.3f,%.3f/%.3f,%.3f/%.3f ms swapchainRelease=%.3f/%.3f ms (n=%llu) xrEndFrame=%.3f/%.3f ms (n=%llu). CopyResource is CPU enqueue time; sourceFinalize and effect GPU timestamp summaries identify GPU completion cost.",
        label,
        waitFrame.AverageMilliseconds(millisecondsPerTick),
        waitFrame.MaximumMilliseconds(millisecondsPerTick),
        static_cast<unsigned long long>(waitFrame.count),
        beginFrame.AverageMilliseconds(millisecondsPerTick),
        beginFrame.MaximumMilliseconds(millisecondsPerTick),
        static_cast<unsigned long long>(beginFrame.count),
        acquire.AverageMilliseconds(millisecondsPerTick),
        acquire.MaximumMilliseconds(millisecondsPerTick),
        static_cast<unsigned long long>(acquire.count),
        leftWait.AverageMilliseconds(millisecondsPerTick),
        leftWait.MaximumMilliseconds(millisecondsPerTick),
        rightWait.AverageMilliseconds(millisecondsPerTick),
        rightWait.MaximumMilliseconds(millisecondsPerTick),
        uiWait.AverageMilliseconds(millisecondsPerTick),
        uiWait.MaximumMilliseconds(millisecondsPerTick),
        leftCopy.AverageMilliseconds(millisecondsPerTick),
        leftCopy.MaximumMilliseconds(millisecondsPerTick),
        rightCopy.AverageMilliseconds(millisecondsPerTick),
        rightCopy.MaximumMilliseconds(millisecondsPerTick),
        uiCopy.AverageMilliseconds(millisecondsPerTick),
        uiCopy.MaximumMilliseconds(millisecondsPerTick),
        release.AverageMilliseconds(millisecondsPerTick),
        release.MaximumMilliseconds(millisecondsPerTick),
        static_cast<unsigned long long>(release.count),
        endFrame.AverageMilliseconds(millisecondsPerTick),
        endFrame.MaximumMilliseconds(millisecondsPerTick),
        static_cast<unsigned long long>(endFrame.count));
    logCallback(logContext, message.data());
}
} // namespace bfvr
