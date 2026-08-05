#pragma once

#include "openxr/OpenXRPresentation.h"

#include <windows.h>

#define XR_NO_PROTOTYPES
#include <openxr/openxr.h>

#include <string>

namespace bfvr
{
[[nodiscard]] const wchar_t* DescribeOpenXRResult(XrResult result) noexcept;
[[nodiscard]] std::wstring BuildOpenXRLoaderPath(const wchar_t* payloadDirectory);
[[nodiscard]] bool EqualLuid(const LUID& left, const LUID& right) noexcept;
[[nodiscard]] bool IsFiniteInRange(float value, float minimum, float maximum) noexcept;
[[nodiscard]] bool IsFiniteUnitPose(const OpenXRPresentationPose& pose) noexcept;
} // namespace bfvr
