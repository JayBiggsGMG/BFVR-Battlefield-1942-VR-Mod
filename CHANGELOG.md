# BFVR Changelog

This file records user-visible changes to BFVR. GitHub Release notes should use
the v1.0.2 section exactly as written below.

## [1.0.3] - 2026-09-11

-Added DSOAL/HRTF audio support
-Fixed compatibility issues with bf42++ v2.0 (fixAnimatedMeshLighting feature)

## [1.0.2] - 2026-08-23

-Increased left hand grab radius from 12cm to 18cm.
-Increased scope aim smoothing radius from 0.35 degrees to 1.5 degrees.
-Doubled controller-motion turret/cannon sensitivity for land vehicles, sea vehicles, and mounted weapons compared to the original implementation and raised its input allowance to BF1942’s native maximum. There is now a sensitivity slider for it as well.
-Reduced the time it takes to trigger a view recenter when holding the reload button from 2.5 seconds to 2.
-Added a Show 'hands+arms, hands only, none' setting.
-Added a death cam comfort vignette effect with independent toggle.
-Added configurable 3D crosshair colors: White, Red, Blue, Green, Pink, Purple, Orange, and Yellow.
-Added a 3D crosshair toggle for knives/throwables/gadgets (off, on, hitmarker only).
-Added an opacity slider for 3D crosshairs.
-Added mouse cursor smoothing in menus to reduce shakiness.
-Added Color profiles (Original/Filmic/Vibrant).
-Added Exposure, Contrast, and Saturation sliders.
-Added optional Battlefield-style 'kill sound' with toggle (very satisfying!).
-Added Broadcast messages "Roger," "Negative," and "Go go go!" to the Quick Menu.
-VR Quick menu, settings menu, and main menu UI now use the same UI sounds BF and its mods do.
-Added the ability to toggle the native game HUD on/off.
-Added a toggle to keep the HUD upright or to follow your sideways head tilt.
-A fix was implemented to allow WMR Headset users using the Oasis driver to play (hopefully!).
-Fixed the left hand weapon grip/socket positions on the Russian DP, the MP18, Japanese Type5 rifle, Chinese AK47, and Saiga12k.
-Removed ability for controller motion to move your camera/head view in a non-gunner passenger position in vehicles. You can still turn your view with the right stick. It may still happen in some vehicles, especially in mods.
-Fixed ground shadow rendering for vehicles and soldiers so that they now render correctly.
-Slightly improved the way foliage between you and the water renders with SSR Water enabled.
-Potential improvement in performance when looking in the direction of large groups of soldiers.
-Redid the arm IK. Elbows should now be far less likely to flail wildly in random directions.
-Attempted to align the hands with the controllers better, but I admit it is inconsistent weapon to weapon. I have also added some sliders that allow you to align them to your liking as well.
-Fixed an offset in the wrists that would cause the ingame hands to swing out of alignment when you twisted your controllers.
-Fixed a bug that would leave your weapon/arms out of alignment when landing after a paradrop.
-Changed the relationship between head tilt and controller tilt when it comes to scoped weapon views. It should feel more natural now.
-Fixed an ambient occlusion rendering artifact that created a visible box-shaped brightness cutoff across floors, walls, and ceilings. AO now renders consistently across the full view.

## [Unreleased]

### Added

- **3D audio now follows your head.** Previously BFVR replaced only the render
  camera, so Battlefield 1942 kept deriving its audio listener from the
  untouched game camera: turning your head moved the picture but not the
  sound, and a fixed source drifted away from where you were looking. BFVR now
  gives the listener the same camera the world is rendered with, so sounds stay
  where they belong. This works with whichever `dsound.dll` the package uses,
  including OpenAL-Soft/DSOAL and BF42++ proxies, and it leaves Battlefield's
  own audio untouched in menus, during loading, and outside VR. Two new
  `UserConfig.txt` settings control it: `head_tracked_audio_enabled` (default
  `true`) turns the correction off, and `head_tracked_audio_position_enabled`
  (default `false`) additionally moves the listener to your head so leaning
  changes which sounds are nearest. Both take effect when BFVR starts.
- Added an opt-in DirectSound listener trace (`BFVR_DSOUND_LISTENER_PROBE=1`,
  alongside `BFVR_DIAGNOSTICS=1`) recording how Battlefield 1942 and BFVR each
  drive the 3D listener, including the basis submitted on every sampled call.
- Added `BFVR_OPENXR_EYE_RESOLUTION_SCALE` (0.25 through 2.00, default 1.00),
  which multiplies the OpenXR runtime's recommended per-eye resolution before
  BFVR creates its swapchains. Lowering it reduces every destination-side cost
  at once — the eye and UI swapchains, each frame's swapchain copies, and the
  presenter's ambient-occlusion, water-reflection, and bloom targets — without
  changing the headset's own resolution setting. It complements the existing
  `BFVR_OPENXR_WORLD_RENDER_SCALE`, which instead changes how large an image
  Battlefield 1942 renders before it is scaled into those swapchains. Values
  are clamped to the runtime's maximum supported rectangle; an unparseable or
  out-of-range value is logged and ignored.

### Fixed

- Fixed animated meshes rendering in the wrong place when BF42++'s
  `fixAnimatedMeshLighting` option is enabled. Tank tracks stayed locked to a
  camera that never moved, so they swung off the vehicle as you turned your
  head, and Battlefield's own first-person hands were drawn alongside the
  tracked VR ones, detached and mis-scaled. BFVR recognized Battlefield's
  skinned draws by hashing the vertex shader's bytecode, and that option
  substitutes a shader of its own, so those draws went unrecognized and lost
  both their per-eye transform and the first-person suppression. Skinned draws
  are now identified by their call site, their render states, and whether the
  draw uses a programmable shader at all, so a substituted shader no longer
  hides them.
- Fixed the stereo renderer refusing to start whenever another BFVR module had
  already initialized the shared hooking library. The stereo probe treated
  "already initialized" as a fatal error and skipped installing its Direct3D
  hooks, leaving the headset with no image, and it also tore the shared library
  down on paths where other modules were still using it. It now tolerates a
  prior initialization and only shuts the library down when it owns it.
- Fixed the world appearing to swim with the head during rotation whenever
  Battlefield 1942 could not deliver a new stereo pair in time. Repeated frames
  were submitted describing the current head pose instead of the pose they were
  rendered with, so the OpenXR runtime applied no reprojection and the stale
  image became head-locked until the next fresh frame snapped it back. BFVR now
  submits the camera each image was actually rendered with, letting the runtime
  reproject repeated frames and keep them world-locked. Missed frames now read
  as ordinary judder rather than snapping.

## [1.0.1] - 2026-08-11

### Added

- Added compatibility for SteamVR OpenXR and Virtual Desktop through both its
  VDXR runtime and SteamVR mode.
- Added two saved aircraft control options: **Aircraft Pitch + Roll on Same
  Stick** and **Swap Aircraft Sticks**. Together they allow pitch/yaw or
  pitch/roll on either physical stick. Both default off, preserving the v1.0.0
  layout of left-stick throttle/roll and right-stick pitch/yaw.
- Added structural recognition of community packages that bundle BF42++ as a
  `dsound.dll` proxy, without restricting support to one exact proxy hash.

### Changed

- BFVR now supports either separately installed `bf42++.dll` or a recognized
  bundled BF42++ `dsound.dll`. When both are present, BFVR uses the bundled
  proxy and does not inject the second copy.
- BFVR disables Battlefield 1942's internal frame limiter only inside the
  BFVR-launched process, producing **far smoother gameplay** across the tested
  packages without editing `VideoDefault.con` or changing ordinary flat-game
  launches.

### Fixed

- Fixed OpenXR instance startup on runtimes that require applications to
  request OpenXR 1.0, including the tested SteamVR and VDXR paths.
- Fixed SteamVR presentation synchronization so BFVR's shared-image GPU work
  overlaps the runtime's normal frame pacing instead of creating an additional
  serial wait.
- Isolated BFVR's D3D8-to-D3D9 presentation path from package-local dgVoodoo
  and DXVK/Vulkan renderer files while leaving those package files unchanged
  for ordinary Battlefield launches.

### Compatibility validated

- Meta Quest Link using the Meta OpenXR runtime.
- SteamVR OpenXR.
- Virtual Desktop using VDXR and using SteamVR mode.
- A Battlefield 1942 Anthology non-Vulkan/dgVoodoo installation.
- The Moongamers dgVoodoo package, which already bundles BF42++ as
  `dsound.dll`.
- A Battlefield 1942 Anthology VK/Vulkan installation with separately
  installed BF42++.
- Confirmed that BFVR will work on at least 4-6 different available versions of BF1942. The likelihood is that it will work for you.

## [1.0.0] - 2026-08-10

- Initial public BFVR release.
