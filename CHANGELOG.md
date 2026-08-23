# BFVR Changelog

This file records user-visible changes to BFVR. GitHub Release notes should use
the v1.0.2 section exactly as written below.

## [1.0.2]

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
