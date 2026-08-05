#include "openxr/OpenXRTrackingBasis.h"

#include <array>
#include <cmath>
#include <cstdarg>
#include <cwchar>

namespace bfvr
{

bool OpenXRTrackingBasis::Initialize(
    XrSession session,
    const OpenXRTrackingBasisApi& api,
    bool localFloorExtensionEnabled,
    OpenXRLogCallback logCallback,
    void* logContext)
{
    Shutdown();
    session_ = session;
    api_ = api;
    logCallback_ = logCallback;
    logContext_ = logContext;
    if (session_ == XR_NULL_HANDLE || api_.createReferenceSpace == nullptr ||
        api_.destroySpace == nullptr || api_.locateSpace == nullptr)
    {
        return false;
    }
    XrReferenceSpaceCreateInfo createInfo{
        XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    createInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    createInfo.poseInReferenceSpace.orientation.w = 1.0F;
    XrResult result = api_.createReferenceSpace(
        session_, &createInfo, &baseLocalSpace_);
    if (XR_FAILED(result) || baseLocalSpace_ == XR_NULL_HANDLE)
    {
        WriteLog(
            L"OpenXR could not create BFVR's immutable base LOCAL space (result=%ld).",
            static_cast<long>(result));
        Shutdown();
        return false;
    }
    result = api_.createReferenceSpace(
        session_, &createInfo, &applicationSpace_);
    if (XR_FAILED(result) || applicationSpace_ == XR_NULL_HANDLE)
    {
        WriteLog(
            L"OpenXR could not create BFVR's application LOCAL space (result=%ld).",
            static_cast<long>(result));
        Shutdown();
        return false;
    }
    if (localFloorExtensionEnabled)
    {
        createInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL_FLOOR;
        result = api_.createReferenceSpace(session_, &createInfo, &floorSpace_);
        if (XR_SUCCEEDED(result) && floorSpace_ != XR_NULL_HANDLE)
        {
            WriteLog(
                L"OpenXR standing height uses LOCAL_FLOOR; this supports floor-relative placement without depending on a room-scale STAGE boundary.");
        }
        else
        {
            floorSpace_ = XR_NULL_HANDLE;
        }
    }
    if (floorSpace_ == XR_NULL_HANDLE)
    {
        createInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
        result = api_.createReferenceSpace(session_, &createInfo, &floorSpace_);
    }
    if (XR_FAILED(result) || floorSpace_ == XR_NULL_HANDLE)
    {
        floorSpace_ = XR_NULL_HANDLE;
        WriteLog(
            L"OpenXR runtime exposes neither LOCAL_FLOOR nor STAGE (result=%ld); Standing safely falls back to a neutral current posture, manual height remains available, and Auto Height reports unavailable.",
            static_cast<long>(result));
    }
    return true;
}

void OpenXRTrackingBasis::Shutdown() noexcept
{
    if (api_.destroySpace != nullptr)
    {
        if (floorSpace_ != XR_NULL_HANDLE) api_.destroySpace(floorSpace_);
        if (applicationSpace_ != XR_NULL_HANDLE)
            api_.destroySpace(applicationSpace_);
        if (baseLocalSpace_ != XR_NULL_HANDLE)
            api_.destroySpace(baseLocalSpace_);
    }
    api_ = {};
    session_ = XR_NULL_HANDLE;
    baseLocalSpace_ = XR_NULL_HANDLE;
    applicationSpace_ = XR_NULL_HANDLE;
    floorSpace_ = XR_NULL_HANDLE;
    logCallback_ = nullptr;
    logContext_ = nullptr;
}

bool OpenXRTrackingBasis::LocateStandingHeight(
    XrSpace viewSpace,
    XrTime displayTime,
    float& heightMeters) const noexcept
{
    heightMeters = 0.0F;
    if (viewSpace == XR_NULL_HANDLE || floorSpace_ == XR_NULL_HANDLE)
    {
        return false;
    }
    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    const XrResult result = api_.locateSpace(
        viewSpace, floorSpace_, displayTime, &location);
    if (XR_FAILED(result) ||
        (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) == 0 ||
        !std::isfinite(location.pose.position.y) ||
        location.pose.position.y <= 0.5F || location.pose.position.y >= 2.5F)
    {
        return false;
    }
    heightMeters = location.pose.position.y;
    return true;
}

XrSpace OpenXRTrackingBasis::ApplicationSpace() const noexcept
{
    return applicationSpace_;
}

void OpenXRTrackingBasis::WriteLog(const wchar_t* format, ...) const
{
    if (logCallback_ == nullptr || format == nullptr)
    {
        return;
    }
    std::array<wchar_t, 900> message = {};
    va_list arguments;
    va_start(arguments, format);
    _vsnwprintf_s(
        message.data(), message.size(), _TRUNCATE, format, arguments);
    va_end(arguments);
    logCallback_(logContext_, message.data());
}

} // namespace bfvr
