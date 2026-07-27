#pragma once

#include "stereo/StereoMath.h"

#include <cstdint>

namespace bfvr::stereo
{

// These values are captured from the raw game-image route in the isolated
// D3D8 probe. They are not D3D8 handles and must never be inferred from the
// immediate system-d3d8 wrapper return.
enum class WeaponRendererRoute : std::uint32_t
{
    Unknown = 0,
    AnimatedMesh = 1,
    GenericMesh = 2
};

enum class WeaponDrawDisposition : std::uint32_t
{
    Unclassified = 0,
    SharedFixedFunctionWeaponCandidate = 1
};

// Minimal, presentation-only input for classifying the generic static
// first-person weapon route. It deliberately contains no weapon object,
// projectile, input, or network state.
struct WeaponDrawPolicyInput
{
    bool indexedDraw = false;
    WeaponRendererRoute rendererRoute = WeaponRendererRoute::Unknown;
    std::uint32_t vertexShaderOrFvf = 0;
    bool alphaBlendEnabled = false;
    bool zEnabled = false;
    bool firstPersonProjection = false;
    bool worldKnown = false;
    bool viewKnown = false;
    Matrix4 world = {};
    Matrix4 view = {};
};

// Recognizes the current evidence-backed shared static weapon candidate:
// generic-mesh FVF 0x112, alpha-disabled, first-person projection, and a
// World origin inside the compact camera-local held-object envelope obtained
// from the rigid D3D8 View matrix. This rejects nearby world/vehicle meshes;
// it is a candidate classification, not authorization to alter a draw.
[[nodiscard]] WeaponDrawDisposition ClassifyWeaponDraw(
    const WeaponDrawPolicyInput& input,
    float maximumCameraDistance) noexcept;

} // namespace bfvr::stereo
