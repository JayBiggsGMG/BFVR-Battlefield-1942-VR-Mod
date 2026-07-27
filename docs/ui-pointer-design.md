# VR menu-pointer coordinate contract

## Current boundary

BFVR does not currently drive the BF1942 menu cursor from either controller.
`ControllerInputOverlay` is restricted to an alive local infantry
`PlayerInput` instance, and the BFVR source contains no `SetCursorPos`,
`WM_MOUSEMOVE`, menu-cursor write, or equivalent controller-to-cursor route.
Any controller pointer visible in the present build therefore comes from
outside this BFVR input overlay. It must not be "corrected" by layering
relative mouse deltas onto it.

The live render evidence exposes three distinct coordinate spaces:

1. BF1942 Ref2 menu draws use an orthographic projection with
   `m00=0.0025` and `m11=0.003333...`, identifying an 800x600 native menu
   coordinate canvas.
2. BFVR captures those draws into a 1920x1080 UI raster.
3. The presenter aspect-fits that 16:9 raster into a 1872x2016 OpenXR UI
   texture and places the carrier at `z=-1.5 m` with width `1.6 m`. The visible
   content is the centred 1.6x0.9 m region; the rest is transparent padding.

Conflating the native canvas, source raster, or padded OpenXR texture makes
error vary with pointer position. Mixing relative mouse/controller deltas with
an absolute panel point makes it vary with movement history as well.

## Implemented offline mapping

`stereo/UiPointerMath` is a pure, stateless **quad-only** mapping boundary. It:

1. casts the OpenXR `aim` pose's `-Z` ray into the UI quad's local space;
2. intersects that ray with the panel plane and rejects hits behind the
   controller or outside the carrier;
3. removes the exact aspect-fit padding using the source-raster and OpenXR
   texture dimensions; and
4. converts the remaining normalized point to BF1942's separate 800x600
   logical menu coordinates.

The same ray and quad always return the same point, regardless of earlier
mouse or controller movement. Deterministic tests cover centre and off-centre
hits, the 1920x1080 to 1872x2016 padding, 800x600 logical output, transparent
padding rejection, and a ray facing away from the panel.

BFVR can also present UI as a cylinder, but this mapper intentionally does not
claim to map a ray onto cylinder geometry. Controller interaction must remain
disabled in that mode until a separate cylinder-ray intersection and
aspect/unwrapping test suite exists; reusing the flat-quad equation there would
create position-dependent cursor drift.

This follows the standard VR split: OpenXR defines `aim` as the pointing pose,
and held geometry uses `grip`; UI systems raycast from the pointing pose into
the presented surface. See the
[OpenXR pose semantics](https://registry.khronos.org/OpenXR/specs/1.1/html/xrspec.html),
[Microsoft motion-controller guidance](https://learn.microsoft.com/en-us/windows/mixed-reality/design/motion-controllers),
and Unreal's
[Widget Interaction component](https://dev.epicgames.com/documentation/unreal-engine/widget-interaction-component?application_version=4.27).

## Activation requirement

The mapping is deliberately not wired to synthetic Windows mouse input.
Before activation, BFVR must recover and validate BF1942's native menu cursor
write/read boundary, then feed it one absolute 800x600 coordinate while menus
are active. Mouse movement must either retake ownership explicitly or update
the same absolute state; two independently accumulated cursors must never be
blended.
