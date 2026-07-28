#pragma once

#include "client/D3D8StereoProbeRecords.h"

namespace bfvr::d3d8probe
{

using FormattedLogCallback = void (*)(const wchar_t* format, ...);

const wchar_t* DescribeDrawKind(FrameDrawKind kind);
const wchar_t* DescribeDrawPolicy(stereo::D3D8DrawPolicy policy);
const wchar_t* DescribeSemanticClass(stereo::D3D8SemanticDrawClass semanticClass);

void ReportStereoPairResult(
    FormattedLogCallback appendLog,
    StereoPairRecord& record,
    float diagnosticHalfEyeOffset,
    float diagnosticConvergenceDistance);

void ReportStereoFrameResult(
    FormattedLogCallback appendLog,
    StereoPairRecord& record,
    StereoFrameRecord& frame);

[[nodiscard]] std::int64_t ReadPerformanceCounter() noexcept;

[[nodiscard]] bool IsContinuousPresentationTimingReportDue(
    DWORD now,
    DWORD& lastReportAt) noexcept;

void ReportContinuousPresentationResult(
    FormattedLogCallback appendLog,
    const PresentationRunRecord& run,
    UINT worldWidth,
    UINT worldHeight);

} // namespace bfvr::d3d8probe
