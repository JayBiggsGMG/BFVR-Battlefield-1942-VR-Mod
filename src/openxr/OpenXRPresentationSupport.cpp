#include "openxr/OpenXRPresentationSupport.h"

#include <cmath>

namespace bfvr
{
const wchar_t* DescribeOpenXRResult(XrResult result) noexcept
{
    switch (result)
    {
    case XR_SUCCESS:
        return L"XR_SUCCESS";
    case XR_EVENT_UNAVAILABLE:
        return L"XR_EVENT_UNAVAILABLE";
    case XR_ERROR_FORM_FACTOR_UNAVAILABLE:
        return L"XR_ERROR_FORM_FACTOR_UNAVAILABLE";
    case XR_ERROR_RUNTIME_UNAVAILABLE:
        return L"XR_ERROR_RUNTIME_UNAVAILABLE";
    case XR_ERROR_API_VERSION_UNSUPPORTED:
        return L"XR_ERROR_API_VERSION_UNSUPPORTED";
    case XR_ERROR_INITIALIZATION_FAILED:
        return L"XR_ERROR_INITIALIZATION_FAILED";
    case XR_ERROR_SESSION_NOT_RUNNING:
        return L"XR_ERROR_SESSION_NOT_RUNNING";
    case XR_ERROR_SESSION_NOT_STOPPING:
        return L"XR_ERROR_SESSION_NOT_STOPPING";
    case XR_ERROR_GRAPHICS_DEVICE_INVALID:
        return L"XR_ERROR_GRAPHICS_DEVICE_INVALID";
    case XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING:
        return L"XR_ERROR_GRAPHICS_REQUIREMENTS_CALL_MISSING";
    case XR_SESSION_LOSS_PENDING:
        return L"XR_SESSION_LOSS_PENDING";
    default:
        return L"unclassified OpenXR result";
    }
}

std::wstring BuildOpenXRLoaderPath(const wchar_t* payloadDirectory)
{
    if (payloadDirectory == nullptr || *payloadDirectory == L'\0')
    {
        return {};
    }
#if defined(_WIN64)
    return std::wstring(payloadDirectory) + L"\\runtime\\openxr\\win64\\openxr_loader.dll";
#else
    return std::wstring(payloadDirectory) + L"\\runtime\\openxr\\win32\\openxr_loader.dll";
#endif
}

bool EqualLuid(const LUID& left, const LUID& right) noexcept
{
    return left.HighPart == right.HighPart && left.LowPart == right.LowPart;
}

bool IsFiniteInRange(float value, float minimum, float maximum) noexcept
{
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

bool IsFiniteUnitPose(const OpenXRPresentationPose& pose) noexcept
{
    const float quaternionLengthSquared =
        pose.orientationX * pose.orientationX +
        pose.orientationY * pose.orientationY +
        pose.orientationZ * pose.orientationZ +
        pose.orientationW * pose.orientationW;
    return
        std::isfinite(pose.orientationX) &&
        std::isfinite(pose.orientationY) &&
        std::isfinite(pose.orientationZ) &&
        std::isfinite(pose.orientationW) &&
        std::isfinite(pose.positionX) &&
        std::isfinite(pose.positionY) &&
        std::isfinite(pose.positionZ) &&
        quaternionLengthSquared >= 0.25F &&
        quaternionLengthSquared <= 2.25F;
}
} // namespace bfvr
