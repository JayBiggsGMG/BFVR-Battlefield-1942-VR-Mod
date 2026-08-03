# BFVR vendor notes

- Upstream: `crosire/d3d8to9` tag `v1.15.1`
- Source archive SHA-256:
  `D1EA974E3A3A0F58E3956B217D4034FA4E2E8DF9B3066DEA1887183837EDCD82`
- License: BSD-2-Clause; see `LICENSE`.

## Local build patch

The upstream CMake target defined `_DEBUG` for `RelWithDebInfo` while CMake
selected the release MSVC runtime for that configuration. That mixed debug CRT
headers with release CRT libraries and produced unresolved `_dbg` allocation
and reporting symbols at link time.

BFVR retains `D3D_DEBUG_INFO` for `RelWithDebInfo` but only defines `_DEBUG` in
the actual `Debug` configuration. No translator behavior is otherwise changed.

The debug logger also brackets D3DX vertex-shader assembly, native D3D9
vertex-shader/declaration creation, and the returned emulated D3D8 handle.
These markers compile out of BFVR's continuous `D3D8TO9NOLOG` build and exist
only to localize flat-compatibility failures.

## D3D9Ex managed-resource compatibility

D3D8 permits `D3DPOOL_MANAGED`, but D3D9Ex does not. On an Ex-backed device,
BFVR therefore translates managed textures, volume textures, cube textures,
vertex buffers, and index buffers to lockable `D3DPOOL_DEFAULT` resources
with `D3DUSAGE_DYNAMIC`. Their D3D8 wrappers retain the original pool and
usage for `GetDesc`/`GetLevelDesc`, including child surfaces and volumes.
`ResourceManagerDiscardBytes` is a successful no-op on the Ex path. The
original behavior remains unchanged when the translator falls back to classic
D3D9.

A version-1 read-only diagnostics export counts each translated resource
family, failures, Reset calls, and the last Reset HRESULT. The standalone
probe verifies managed texture/surface and vertex/index-buffer descriptor and
locking behavior. In the bounded BF1942 Aberdeen gate, the translator counted
21 managed textures, 9 managed vertex buffers, 1 managed index buffer, zero
translation failures, and one successful inherited `IDirect3DDevice9::Reset`
call (`S_OK`). BFVR did not replace that call with `ResetEx`.

## BFVR shared-surface extension

BFVR's opt-in GPU-resident path requires the translated D3D8 device to create
resources that D3D11 can open. The local patch therefore:

- prefers `Direct3DCreate9Ex` and `IDirect3D9Ex::CreateDeviceEx`, with the
  original D3D9 creation path retained as a fallback;
- adds a versioned, dynamically resolved C ABI for creating a one-level,
  default-pool D3D9 shared render-target texture and returning its wrapped
  D3D8 level-zero surface;
- validates that the supplied D3D8 device is this translator's wrapper through
  a private interface GUID before accessing its D3D9 device; and
- exposes a bounded D3D9 event-query wait used by diagnostics and explicit
  producer/consumer synchronization.

The standalone BFVR probe first proved that classic D3D9 rejects shared-handle
textures on the test system. The D3D9Ex path then created a
`D3DFMT_A2B10G10R10` target, cleared it through D3D8, synchronized it, opened
the same allocation as `DXGI_FORMAT_R10G10B10A2_UNORM` through D3D11, and
verified the exact logical RGBA clear color. No game-directory proxy is
installed, and the extension remains unavailable unless BFVR explicitly loads
this renamed translator.

The current shared bridge ABI is version 6. In addition to target creation and
the bounded producer event-query wait, it exposes read-only diagnostics for:

- whether the translated device is D3D9Ex-backed and cooperative;
- direct/helper allocation attempts and successful helper-device creations;
- the last helper stage and its `CreateDeviceEx`, `CreateTexture`, and
  game-device-open HRESULTs.

ABI 5 added stable programmable-vertex-shader identity across the
translator's pointer-derived emulated handles. At successful D3D8 shader
creation it records a 64-bit FNV-1a hash and byte count of the original shader
function plus a creation ordinal. A validated read-only export returns that
metadata for a live emulated handle; native/foreign devices and stale handles
fail closed. BFVR uses the metadata for exact semantic policies and does not
infer shader identity from the high handle bit.

ABI 6 adds a capability-gated texture-backed `INTZ` depth/stencil constructor
and a bounded fullscreen depth-export operation. The translator recognizes
the FOURCC as a logical 24-bit format so D3D8 Z-bias conversion does not fall
back to a zero-bit depth buffer. The export accepts only a validated translator
device and its own matching surfaces, samples depth only after unbinding it,
and restores the captured D3D9 state plus render target, depth target, and
viewport before returning. Embedded ps_3_0 shaders export either packed
`A8R8G8B8` or `A16B16G16R16F`, and optional D3D9 timestamp queries measure the
GPU interval.

`BFVRAmbientOcclusionDepthProbe` validates ABI 6 without launching BF1942. On
the RTX 4070 Ti at 1872x2016 it proved depth, stencil, clear/overlap values,
logical Z bias, D3D8-visible state restoration, and D3D11 reconstruction. The
64-iteration packed path measured 0.0256 ms/eye p95 and the float path 0.0410
ms/eye p95. Shared `A8B8G8R8` was rejected by the adapter; the proven packed
mapping is D3D9 `A8R8G8B8` to D3D11 `B8G8R8A8_UNORM`. Live BF1942 uses this
ABI only when `BFVR_OPENXR_AO=1` requests the default-off prototype. The
extended D3D8 cross-process control proves both optional packed depths can be
opened, evaluated, composited, and acknowledged by the x64 consumer; live
headset gameplay validation remains pending.

Target creation first uses the translated game device. A diagnostic fallback
can create the allocation on a temporary windowed D3D9Ex helper device and
open the resulting legacy handle on the game device, as permitted by D3D9's
`pSharedHandle` contract. `BFVR_D3D8TO9_FORCE_SHARED_HELPER=1` forces that
fallback only for standalone validation. Legacy D3D9 shared handles are
resource tokens, not NT handles, and BFVR never passes them to `CloseHandle`.

The translated runtime also exports presentation diagnostics and recognizes
the child-only `BFVR_D3D8TO9_FORCE_WINDOWED=1` control. The BFVR loader uses
that control only for the opt-in shared/OpenXR presentation requests so
`CreateDevice`, `Reset`, and additional-swap-chain parameters are translated
to windowed mode without changing normal game launches.

The live BF1942 bring-up exposed one client-side format bug rather than a
driver rejection: numeric D3D9 format `35` is `A2R10G10B10`, while the bridge's
validated `A2B10G10R10` format is `31`. Correcting the producer constant let
the game device create all three shared targets directly; the helper fallback
was not needed. The no-HMD 60-second run transported 1,276 frames through the
x64 D3D11 consumer with zero failures and no CPU readback.

The following x64 OpenXR run used the same direct allocations for 2,999
successfully presented frames. It reported zero transport failures, zero CPU
readback, and clean requested-exit/STOPPING teardown; the helper fallback again
remained unused.

After GPU transport validation, BFVR's shared/OpenXR loader requests began
defaulting world source eyes to the runtime-recommended dimensions unless the
user supplied `BFVR_OPENXR_WORLD_RENDER_SCALE`. Two 1872x2016 no-HMD controls
transported 1,288/1,288 frames apiece with zero failures and zero CPU readback.
