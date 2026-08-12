# BFVR Changelog

This file records user-visible changes to BFVR. GitHub Release notes should use
a shorter version of the same information.

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
