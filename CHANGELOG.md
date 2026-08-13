# BFVR Changelog

This file records user-visible changes to BFVR. GitHub Release notes should use
a shorter version of the same information.

## [Unreleased] - potential 1.0.2

### Added

- Added a live **Show** selector with Arms & Hands, Hands Only, and No
  Hands/Arms choices, plus a default-on **Death Camera Comfort** option to VR
  Settings. The visibility selector leaves native animation and pose solving
  active. Death comfort uses a tighter muted dark-red vignette during the
  native death-camera flight without flattening or freezing VR.
- Added an independent **Knives / Throwables / Gadgets** 3D crosshair mode
  with On, Hit Marker Only, and Off choices. It defaults to On.
- Added an eight-choice **3D Crosshair Color** selector with White, Green,
  Blue, Purple, Red, Pink, Orange, and Yellow choices for the crosshair and hit
  marker. The existing per-eye world renderer now tints neutral grayscale art
  without changing endpoint calculation, placement, or angular size.
- Added a second Graphics page with **Original**, **Filmic**, and **Vibrant**
  color profiles plus centered exposure, contrast, and saturation sliders and
  a dedicated color reset action.

### Changed

- World color treatment is fused into BFVR's existing final D3D11 composite.
  It affects both stereo world eyes and the world-drawn 3D crosshair, while
  native HUD/menu pixels, scopes, Quick Menu, VR Settings, and other separate
  interface layers retain their original colors.
- Exact scoped hit feedback now follows the selected **3D Crosshair Color**.
  BFVR temporarily synchronizes BF1942's native crosshair color while scoped
  and restores the original flat-game color afterward; scope artwork and
  saved profile files are unchanged. Owner headset testing confirms the scoped
  hit marker uses the selected color correctly.
- The movement and death comfort effects now share one prioritized vignette
  compositor, so enabling both cannot stack duplicate OpenXR layers.
- Reorganized VR Settings so both comfort-vignette controls are adjacent on
  the live-presentation page, while height adjustment, standing calibration,
  and recentering share a separate physical-calibration page.

### Fixed

- Fixed hidden first-person parts incorrectly hiding other soldiers' skinned bodies through a
  scope. Arm suppression now fails closed during scope activation and scoped
  world rendering; helmets, attachments, weapons, and full soldier bodies
  remain visible.

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
