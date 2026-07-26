# Offline Stereo and Head-Pose Math

`BFVRStereoMath` is a small, runtime-independent C++ boundary. It has no
OpenXR loader, D3D device, BF1942 hook, or game-memory dependency. It exists so
the project can test the coordinate and projection rules before a headset
session or a safe render-camera override exists.

## Conventions

Inputs use the OpenXR pose convention:

- metres;
- `+X` right, `+Y` up, `-Z` forward;
- an orientation rotates eye-local coordinates into its reference space.

The exported view/projection matrices use the D3D8 fixed-function convention:

- D3DMATRIX-compatible row-major storage;
- row-vector multiplication: `clip = position * matrix`;
- left-handed view space, with `+Z` forward;
- projection depth maps the near/far planes to normalized depth `0` and `1`.

The coordinate change is `C = diag(1, 1, -1)`. For an OpenXR eye pose with
rotation `R` and position `t`, the module constructs the D3D8 view transform as
the row-vector form of `inverse(C * [R,t] * C)`. It does not assume anything
about the unresolved BF1942 camera object or attempt to write a matrix into the
game.

Eye locations are `headPosition + headOrientation * (+/-IPD/2, 0, 0)`: the
left eye is negative head-local X and the right eye is positive head-local X.
Per-eye FOV inputs are tangents matching OpenXR's left/right/up/down angles;
the module creates an asymmetric D3D8 off-centre perspective matrix from them.

## Deterministic coverage

`BFVRStereoMathTests` verifies:

- identity and yawed head poses place both eyes at the expected physical
  offsets;
- OpenXR forward (`-Z`) maps to D3D8 forward (`+Z`), including after yaw;
- an eye's D3D8 view matrix maps its own position to the view origin;
- an asymmetric FOV maps each near-plane edge to the expected NDC edge and
  maps near/far depth to `0/1`;
- zero quaternions, negative IPD, degenerate FOV, and invalid depth ranges are
  rejected.

This is offline mathematical evidence only. It does not prove BF1942 accepts a
replacement view/projection, that its world units equal metres, or that an
OpenXR runtime supplies particular poses/FOV values. Those remain headset and
renderer-boundary work.
