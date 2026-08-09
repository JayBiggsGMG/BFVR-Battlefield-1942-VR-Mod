#pragma once

#include "stereo/StereoMath.h"

#include <cstdint>
#include <optional>

namespace bfvr::stereo
{

// Captures the game's native camera in the occupied weapon station's local
// coordinate frame. Both inputs are rigid camera/object-to-world transforms
// in BF1942's D3D8 row-vector convention.
[[nodiscard]] std::optional<Matrix4> CaptureD3D8MountedCameraAnchor(
    const Matrix4& sourceCameraWorld,
    const Matrix4& stationWorld) noexcept;

// Reconstructs a camera-to-world transform from one captured station-local
// anchor and the occupied station's current world transform. Turret children
// may continue to rotate independently without carrying this camera, while
// movement and rotation inherited by the station itself remain intact.
[[nodiscard]] std::optional<Matrix4> ComposeD3D8MountedCameraFromAnchor(
    const Matrix4& cameraInStation,
    const Matrix4& stationWorld) noexcept;

// Selects the temporary vertical-FOV multiplier used only while BF1942 builds
// its cached visibility frustum. Ordinary VR retains the headset-accepted
// margin. A decoupled mounted camera receives a larger vertical envelope so
// nearby dynamic-object roots below an elevated turret camera are not rejected
// while their separately culled weapon children remain visible. The renderer's
// real projection is restored before either eye is drawn.
[[nodiscard]] float SelectD3D8VisibilityFrustumVerticalScale(
    bool mountedCameraDecoupled) noexcept;

struct MountedCameraControlState
{
    std::uintptr_t stationIdentity = 0;
    std::uint32_t lastToggleSequence = 0;
    bool decoupled = false;
};

struct MountedCameraControlTransition
{
    bool stationChanged = false;
    bool decouplingChanged = false;
    bool toggleApplied = false;
    bool toggleIgnored = false;
};

// Applies monotonic UI toggle edges to exactly one occupied station. A null
// station, death/exit, or any station identity change always restores native
// coupling before a later explicit edge may enable the new station.
[[nodiscard]] MountedCameraControlTransition UpdateMountedCameraControl(
    MountedCameraControlState& state,
    std::uintptr_t stationIdentity,
    std::uint32_t toggleSequence) noexcept;

} // namespace bfvr::stereo
