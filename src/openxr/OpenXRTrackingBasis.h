#pragma once

#include "openxr/OpenXRPresentation.h"

#ifndef XR_NO_PROTOTYPES
#define XR_NO_PROTOTYPES
#endif
#include <openxr/openxr.h>

#include <cstdint>

namespace bfvr
{

struct OpenXRTrackingBasisApi
{
    PFN_xrCreateReferenceSpace createReferenceSpace = nullptr;
    PFN_xrDestroySpace destroySpace = nullptr;
    PFN_xrLocateSpace locateSpace = nullptr;
};

// Owns BFVR's immutable runtime LOCAL reference and an optional LOCAL_FLOOR
// (preferred) or STAGE floor.
// Gameplay recentering, height, and seat anchoring belong to the local x86
// camera/hand mapping and never rewrite this OpenXR space.
class OpenXRTrackingBasis
{
public:
    bool Initialize(
        XrSession session,
        const OpenXRTrackingBasisApi& api,
        bool localFloorExtensionEnabled,
        OpenXRLogCallback logCallback,
        void* logContext);
    void Shutdown() noexcept;

    [[nodiscard]] bool LocateStandingHeight(
        XrSpace viewSpace,
        XrTime displayTime,
        float& heightMeters) const noexcept;

    [[nodiscard]] XrSpace ApplicationSpace() const noexcept;

private:
    void WriteLog(const wchar_t* format, ...) const;

    OpenXRTrackingBasisApi api_ = {};
    XrSession session_ = XR_NULL_HANDLE;
    XrSpace baseLocalSpace_ = XR_NULL_HANDLE;
    XrSpace applicationSpace_ = XR_NULL_HANDLE;
    XrSpace floorSpace_ = XR_NULL_HANDLE;
    OpenXRLogCallback logCallback_ = nullptr;
    void* logContext_ = nullptr;
};

} // namespace bfvr
