# BFVR Changelog

This file records user-visible changes to BFVR. GitHub Release notes should use
a shorter version of the same information.

## [Unreleased] - potential 1.0.2

### Added

- Added a saved **3D Crosshair Opacity** slider beside all other 3D crosshair
  controls. It ranges from 5% to 100% in 5% steps and affects both the aiming
  crosshair and confirmed-hit marker; enabled feedback can never be made fully
  invisible.
- Added default-on **Menu Pointer Smoothing** for Battlefield's native menus,
  the Quick Menu, and the VR Settings dialog. The saved VR Settings toggle
  suppresses controller tremor while keeping deliberate cursor movement
  responsive, and can restore direct unfiltered pointing without a restart.
- VR Settings now shows `BFVR v1.0.2 - JayBiggsGaming` in the selected 3D
  crosshair green beneath the menu. The displayed version, launcher text,
  Windows file metadata, and installer version now share one release-version
  definition.
- Added a saved **Turret Motion Sensitivity** slider to Controls for tank
  cannons, other land/sea vehicle turrets, AA, and mounted weapons. It ranges
  from 50% to 300%, defaults to the accepted 200% response, applies after Save
  without restarting, and does not alter right-stick sensitivity.
- Added an opt-in, aggregate-only `BFVR_PERFORMANCE_SUMMARY=1` diagnostic for
  isolating runtime slowdown without re-enabling the broad trace stream. It
  reports x86 replay/skinning/pacing, x64 source/effect enqueue stages,
  swapchain API calls, and actual `xrEndFrame` at 30-second intervals and
  shutdown; it does not change launch or frame scheduling.
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
- Added a default-on **Kill Sound** option on a dedicated Graphics / Audio
  page. Battlefield's authoritative score handling drives the supplied sound
  in single-player and multiplayer. Separate kills use independent overlapping
  voices instead of restarting the previous sound, while confirmations within
  300 ms collapse into one grenade-style multi-kill sound.

### Changed

- Physical right-controller movement now aims only an occupied land/sea or
  mounted station that exposes a native weapon. Unarmed driver and passenger
  freelook positions ignore controller movement while retaining head look,
  right-stick turning, mouse look, and their normal vehicle controls.
- VR Settings now keeps separate Right and Left hand Left/Right, Down/Up, and
  Back/Forward alignment sliders as permanent player options. The accepted
  defaults are right `(-7,+4,-5)` cm and left `(-2,+6,-2)` cm, and a same-page
  hand-only reset restores them without resetting unrelated settings.
- Consolidated the three 3D crosshair mode selectors, color, and opacity onto
  one Controls page. The vacated VR Settings row now owns Menu Pointer
  Smoothing.
- Reduced the Right B reload/recenter hold from 2.5 seconds to 2 seconds;
  reload still triggers on the initial press and each hold recenters only once.
- Changed Water SSR from replacing world color to adding a restrained
  reflection into the pixel's remaining brightness. This preserves foreground
  tree-card color when BF1942's depth-write-disabled foliage leaves the water
  receiver mask visible behind it, without adding render passes or resources.
- Increased the sniper aim-smoothing boundary from 0.40 to 1.5 degrees so
  runtime pose variation is less likely to bypass stabilization during fine
  aiming.
- Doubled land/sea/mounted controller-motion aim sensitivity and its matching
  per-sample allowance, reducing required hand
  travel without changing BF1942's native turret controls or tracking-jump
  safeguards.
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
- BFVR's Quick Menu, VR Settings controls, and Back-to-Game button now reuse
  Battlefield's native highlight, confirm, and cancel menu sounds. Active
  game/mod menu-sound replacements and the game's normal menu-audio path are
  preserved; there is no separate BFVR menu-sounds toggle.

### Fixed

- Fixed controller twisting making the visible hand and attached gun swing
  around a point outside the wrist. Hand rotation now turns the wrist in place;
  the saved alignment sliders retain deliberate resting-position adjustment
  without changing aim, firing, projectiles, or the crosshair.
- Fixed projected soldier and vehicle shadows sometimes losing their stereo
  correction for an entire launch. Both proven Battlefield terrain-shadow
  routes are now recognized immediately; the retained compatibility search is
  restricted to the shared terrain-cell path instead of scanning unrelated
  blended draws.
- Reduced animated-soldier CPU overhead from the Hands Only classifier. Mesh
  template names are now inspected lazily only for proven first-person
  arm/hand candidates, and repeated classifications are cached. Ordinary world
  soldiers no longer trigger template-name classification in any Show mode.
- Corrected Kill Sound delivery in single-player. The identity-only correction
  did not fix SP because local play does not deliver confirmed kills through
  the same received-client boundary as remote MP. BFVR now also observes the
  native local-server score handler, accepts only ordinary kill type 3 by
  PlayerManager's current player, rejects teamkill type 6, and suppresses an
  identical cross-source duplicate in listen-server play. Owner testing confirms
  correct SP and MP playback, teamkill silence, and multikill grouping.
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
