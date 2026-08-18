#include "presenter/SharedTexturePerformanceSummary.h"

#include <windows.h>

#include <array>
#include <cstdio>

namespace bfvr::shared
{
namespace
{
constexpr DWORD kReportPeriodMs = 30000;
}

SharedTexturePerformanceSummary::SharedTexturePerformanceSummary() noexcept
    : enabled_(diagnostics::ReadAggregatePerformanceEnabled())
{}

std::int64_t SharedTexturePerformanceSummary::BeginSample() const noexcept
{
    return enabled_ ? diagnostics::ReadPerformanceCounter() : 0;
}

void SharedTexturePerformanceSummary::EndSample(
    SharedTexturePerformanceStage stage,
    std::int64_t startedAt) noexcept
{
    if (!enabled_ || startedAt == 0)
    {
        return;
    }
    stages_[static_cast<std::size_t>(stage)].Add(
        diagnostics::ReadPerformanceCounter() - startedAt);
}

void SharedTexturePerformanceSummary::ReportIfDue(
    DWORD now,
    SharedTextureLogCallback logCallback,
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

void SharedTexturePerformanceSummary::ReportFinal(
    SharedTextureLogCallback logCallback,
    void* logContext)
{
    if (enabled_ &&
        stages_[static_cast<std::size_t>(
            SharedTexturePerformanceStage::CompositeEnqueue)].count != 0)
    {
        Report(L"Final", logCallback, logContext);
    }
}

void SharedTexturePerformanceSummary::Reset() noexcept
{
    lastReportAt_ = 0;
    stages_ = {};
}

void SharedTexturePerformanceSummary::Report(
    const wchar_t* label,
    SharedTextureLogCallback logCallback,
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
    const auto& acquire = stages_[static_cast<std::size_t>(
        SharedTexturePerformanceStage::SharedAcquire)];
    const auto& ssgi = stages_[static_cast<std::size_t>(
        SharedTexturePerformanceStage::ScreenSpaceGlobalIlluminationEnqueue)];
    const auto& water = stages_[static_cast<std::size_t>(
        SharedTexturePerformanceStage::WaterReflectionEnqueue)];
    const auto& ao = stages_[static_cast<std::size_t>(
        SharedTexturePerformanceStage::AmbientOcclusionEnqueue)];
    const auto& composite = stages_[static_cast<std::size_t>(
        SharedTexturePerformanceStage::CompositeEnqueue)];
    const auto& overlay = stages_[static_cast<std::size_t>(
        SharedTexturePerformanceStage::MenuOverlayEnqueue)];
    const auto& flush = stages_[static_cast<std::size_t>(
        SharedTexturePerformanceStage::Flush)];
    std::array<wchar_t, 1400> message = {};
    _snwprintf_s(
        message.data(),
        message.size(),
        _TRUNCATE,
        L"BFVR_PERFORMANCE_SUMMARY %s x64 source CPU timing avg/max: sharedAcquire=%.3f/%.3f ms (n=%llu) SSGIEnqueue=%.3f/%.3f ms (n=%llu) waterSSREnqueue=%.3f/%.3f ms (n=%llu) AOEnqueue=%.3f/%.3f ms (n=%llu) eyeUiCompositeEnqueue=%.3f/%.3f ms (n=%llu) menuOverlayEnqueue=%.3f/%.3f ms (n=%llu) D3D11Flush=%.3f/%.3f ms (n=%llu). These are CPU command-submission times; sourceFinalize and GPU timestamp summaries show completion cost.",
        label,
        acquire.AverageMilliseconds(millisecondsPerTick),
        acquire.MaximumMilliseconds(millisecondsPerTick),
        static_cast<unsigned long long>(acquire.count),
        ssgi.AverageMilliseconds(millisecondsPerTick),
        ssgi.MaximumMilliseconds(millisecondsPerTick),
        static_cast<unsigned long long>(ssgi.count),
        water.AverageMilliseconds(millisecondsPerTick),
        water.MaximumMilliseconds(millisecondsPerTick),
        static_cast<unsigned long long>(water.count),
        ao.AverageMilliseconds(millisecondsPerTick),
        ao.MaximumMilliseconds(millisecondsPerTick),
        static_cast<unsigned long long>(ao.count),
        composite.AverageMilliseconds(millisecondsPerTick),
        composite.MaximumMilliseconds(millisecondsPerTick),
        static_cast<unsigned long long>(composite.count),
        overlay.AverageMilliseconds(millisecondsPerTick),
        overlay.MaximumMilliseconds(millisecondsPerTick),
        static_cast<unsigned long long>(overlay.count),
        flush.AverageMilliseconds(millisecondsPerTick),
        flush.MaximumMilliseconds(millisecondsPerTick),
        static_cast<unsigned long long>(flush.count));
    logCallback(logContext, message.data());
}
} // namespace bfvr::shared
