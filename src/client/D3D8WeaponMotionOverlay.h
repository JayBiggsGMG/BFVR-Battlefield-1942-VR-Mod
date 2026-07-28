#pragma once

#include <windows.h>

namespace bfvr
{

struct D3D8WeaponMotionMatrix
{
    float values[4][4] = {};
};
static_assert(sizeof(D3D8WeaponMotionMatrix) == sizeof(float) * 16);

// Direct functions from the currently verified D3D8 device route. The overlay
// calls only these APIs around the one classified draw; it never hooks them,
// creates a resource, or calls a BF1942 method.
struct D3D8WeaponMotionD3D8Api
{
    HRESULT(WINAPI* setTransform)(void*, DWORD, const void*) = nullptr;
    HRESULT(WINAPI* getTransform)(void*, DWORD, void*) = nullptr;
    HRESULT(WINAPI* getRenderState)(void*, DWORD, DWORD*) = nullptr;
    HRESULT(WINAPI* getVertexShader)(void*, DWORD*) = nullptr;
};

struct D3D8WeaponMotionRestore
{
    bool changedWorld = false;
    D3D8WeaponMotionMatrix sourceWorld = {};
    // Captured with the classified source draw for stereo replay. It is the
    // same rigid attachment used by the temporary flat-game draw; no model
    // scale or projection-derived morph is permitted here.
    D3D8WeaponMotionMatrix worldSpaceAttachment = {};
};

// Produces the World transform for stereo replay. The attachment is converted
// through the current body frame, then post-multiplied
// directly after the source World and shared unchanged by both eyes.
[[nodiscard]] bool BuildD3D8WeaponMotionReplayWorld(
    const D3D8WeaponMotionMatrix& sourceWorld,
    const D3D8WeaponMotionMatrix& worldSpaceAttachment,
    D3D8WeaponMotionMatrix& adjustedWorld) noexcept;

// The continuous OpenXR presentation path calls this once after its controller
// transport is live. The overlay remains inert unless the launcher explicitly
// sets BFVR_ENABLE_WEAPON_MOTION=1 for that child process.
void StartD3D8WeaponMotionOverlay(
    void (*appendLog)(const wchar_t* message));
void StopD3D8WeaponMotionOverlay();

// Called immediately before/after the original DrawIndexedPrimitive. It
// applies the right controller's tracked OpenXR grip pose only to the shared
// fixed-function weapon candidate, and restores the exact source World before
// the draw detour returns. A false result requires no restore call.
[[nodiscard]] bool BeginD3D8WeaponMotionOverlayDraw(
    void* device,
    const D3D8WeaponMotionD3D8Api& api,
    const void* const* returnAddressSlot,
    D3D8WeaponMotionRestore& restore) noexcept;
void EndD3D8WeaponMotionOverlayDraw(
    void* device,
    const D3D8WeaponMotionD3D8Api& api,
    D3D8WeaponMotionRestore& restore) noexcept;

} // namespace bfvr
