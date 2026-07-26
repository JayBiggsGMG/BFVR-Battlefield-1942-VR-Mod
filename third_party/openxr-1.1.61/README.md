# Khronos OpenXR SDK 1.1.61

BFVR vendors the official generated C headers needed by the native x86
bootstrap. The headers and the paired `runtime\openxr\win32\openxr_loader.dll`
are from the stable `release-1.1.61` package published by Khronos on 2026-07-06.

- Source: `https://github.com/KhronosGroup/OpenXR-SDK-Source/releases/tag/release-1.1.61`
- License: Apache-2.0; copied to `BFVR\licenses\OpenXR-Loader-1.1.61.txt`.
- `openxr_loader.dll` SHA-256:
  `D60C68DCF66C8DAD31CA5F251DA6423C94631BBD7AD3B57F70A487E8C918C86A`
- `openxr.h` SHA-256:
  `942D4756DBEA43288DF8CA722E6B4E704B83F5593C819676D096C87BD67C7F51`

BFVR loads the x86 loader by an explicit path below its own folder. The loader
selects the user-configured system OpenXR runtime; BFVR does not ship, register,
or alter a headset runtime.

The bootstrap intentionally creates no session, graphics binding, swapchain,
input action, or composition layer. It is safe to run without a headset and is
only an API/lifecycle readiness check.
