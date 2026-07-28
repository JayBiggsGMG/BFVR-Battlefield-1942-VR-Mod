#pragma once

#include "client/D3D8SharedPresentationBridge.h"

#include <memory>

namespace bfvr
{

using D3D8RenderViewPoseLogCallback = void (*)(const wchar_t* message);

// Owns the verified RenderView frustum-query and transformation MinHook
// entries used by the OpenXR presentation probe. The caller owns MinHook
// initialization and must remove these hooks before calling MH_Uninitialize.
class D3D8RenderViewPoseHook
{
public:
    D3D8RenderViewPoseHook();
    ~D3D8RenderViewPoseHook();

    D3D8RenderViewPoseHook(const D3D8RenderViewPoseHook&) = delete;
    D3D8RenderViewPoseHook& operator=(const D3D8RenderViewPoseHook&) = delete;

    bool Create(void* gameImage, D3D8RenderViewPoseLogCallback logCallback);
    bool Enable();
    void UpdatePose(
        const D3D8RuntimeView& referenceHead,
        const D3D8RuntimeRenderRequest& request);
    // Clears only transient per-request hook state. This is used after a D3D8
    // Reset so no pre-reset HMD pose can be replayed before the next request.
    void ClearPose() noexcept;
    [[nodiscard]] bool WasApplied(LONG sequence) const noexcept;
    void DisableAndRemove();
    void LogSummary() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace bfvr
