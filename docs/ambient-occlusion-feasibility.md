# BFVR Ambient-Occlusion Feasibility and Opt-In Prototype

Research snapshot: 2026-08-03; prototype and hardware proofs updated 2026-08-03

## Conclusion

A performant, world-only screen-space ambient-occlusion path is feasible and
now exists as a default-off live prototype. Both isolated native-resolution
GPU controls and a no-game x86-to-x64 end-to-end control pass. This is still
not a presenter-only shader change: the x86 replay must use sampleable depth
and export it across the D3D9Ex-to-D3D11 boundary.

The implemented path:

1. use a capability-gated `INTZ` texture as each D3D9 eye depth target;
2. after stereo replay, sample each depth texture once into a BFVR-owned,
   D3D9Ex-shareable colour texture;
3. open the two additional textures in the x64 D3D11 presenter;
4. run a native-resolution, 8-direction spatial depth-only pass plus a
   3x3 depth-aware denoise independently for each eye; and
5. sample the AO term in the existing world conversion draw, before Ref2 UI is
   submitted as its separate OpenXR layer.

This reuses the depth work BFVR already performs. It does not add a geometry
prepass and does not process menus, HUD, or other Ref2 pixels.

Live allocation and shaders are requested only by `BFVR_OPENXR_AO=1`; the
default path neither allocates depth-export/AO resources nor compiles the AO
composite variants. The current application is scheduled at about 45 Hz by a
90 Hz Oculus runtime, with a typical new-source interval near 21 ms. AO cannot
solve that cadence and must not become a default until same-build headset
quality and live pacing comparisons pass.

## Current BFVR facts

- Stereo replay owns one colour and one depth surface per eye. It uses ordinary
  D24S8 by default and validated texture-backed `INTZ` only for the opt-in AO
  path; both are cleared and filled beside the corresponding world colour.
- Protocol v11 preserves the mandatory two world-colour textures and one UI
  texture and adds two optional packed-depth descriptions, an encoding tag,
  an exact per-eye projection payload, and a per-frame validity bit.
- The active zero-CPU-readback path is translated D3D8 -> D3D9Ex shared
  render-target textures -> x64 D3D11. The two native-size world inputs are
  R10G10B10A2, while UI is R16G16B16A16 float.
- The x64 device is feature level 12_1 on the owner's GeForce RTX 4070 Ti, so
  the D3D11 side has ample pixel- and compute-shader support. That does not
  remove the need for capability checks on the D3D9 depth side.
- The most recent pre-AO headset measurement attributes about 1.67 ms per source to
  x64 colour conversion/FXAA/UI work and GPU completion. Any AO cost will sit
  on this already-serialized source-consumption boundary unless a later
  pipeline change overlaps it.
- `BFVRAmbientOcclusionDepthProbe` proves the `INTZ` mechanism on the owner's
  RTX 4070 Ti at 1872x2016. `BFVRAmbientOcclusionGpuProbe` separately executes
  and measures the real x64 AO and composite shaders at that size. The
  extended shared-surface probe proves the complete process boundary at
  1404x1512 without a game or headset.

## Why depth needs an explicit export

Microsoft's D3D9-to-D3D11 sharing contract permits only 2D, single-mip,
default-pool, non-MSAA, write-only render-target textures in
`R10G10B10A2_UNORM`, `R16G16B16A16_FLOAT`, or `R8G8B8A8_UNORM`. A D24S8 depth
surface cannot be opened directly by the x64 D3D11 device.

`IDirect3DDevice9::StretchRect` does not provide a portable escape hatch:
depth/stencil copies must remain between ordinary depth/stencil surfaces, use
the whole surface and the same format, and occur outside a
`BeginScene`/`EndScene` pair. It cannot convert D24S8 into a shared colour
texture.

NVIDIA documents `INTZ` as a D3D9 FOURCC depth format that can be used for
depth testing and later sampled as a texture. It is a driver extension, not a
portable D3D9 guarantee, so BFVR must query it on the actual adapter and fail
closed to the current D24S8/no-AO route if any creation, compatibility, clear,
draw, stencil, or sampling check fails.

Primary references:

- [Microsoft: ID3D11Device::OpenSharedResource](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-opensharedresource)
- [Microsoft: IDirect3DDevice9::StretchRect](https://learn.microsoft.com/en-us/windows/win32/api/d3d9helper/nf-d3d9helper-idirect3ddevice9-stretchrect)
- [Microsoft: determining D3D9 format support](https://learn.microsoft.com/en-us/windows/win32/direct3d9/determining-hardware-support)
- [NVIDIA: Direct DepthBuffer Access (`INTZ` and `RAWZ`), section 6.4.2](https://developer.download.nvidia.com/GPU_Programming_Guide/GPU_Programming_Guide_G80.pdf)

## Depth bridge

### 1. Standalone capability proof -- passed

Extend the existing translated shared-surface probe before touching live BF1942
rendering. On the exact adapter and device path used by BFVR, the probe should:

- query `INTZ` as a `D3DRTYPE_TEXTURE` with `D3DUSAGE_DEPTHSTENCIL`;
- verify that it matches the world render-target format;
- create a full-size texture-backed depth/stencil surface;
- clear and draw known overlapping geometry through the wrapped D3D8 surface;
- unbind it as depth, sample it in a D3D9 fullscreen pass, and write a known
  depth encoding into an allowed shared colour format;
- wait through the existing D3D9 event-query mechanism;
- open the shared texture in D3D11 and verify foreground, background, clear
  depth, orientation, and precision; and
- restore every D3D9/D3D8 state and release every resource.

The implemented probe tests both candidate export encodings:

- `A16B16G16R16F` device depth is simple and that cross-API format is
  already proven by BFVR's UI transport. Full-size stereo export costs about
  57.6 MiB (60.4 MB) of additional resident texture storage at 1872x2016 per
  eye.
- `A8R8G8B8` packed device depth halves that storage and write bandwidth. It
  opens in D3D11 as `B8G8R8A8_UNORM`; the shader/readback path accounts for
  the channel mapping. The initially proposed `A8B8G8R8` shared allocation was
  rejected by this adapter with `D3DERR_INVALIDCALL` and is no longer a
  candidate.

The probe chooses only from measured correctness and GPU timestamps. A
half-resolution export can be evaluated later, but full-resolution depth keeps
thin edges available while the x64 shader chooses its own half-resolution
sample footprint.

The final 64-iteration native-size run passed all of the following on the
GeForce RTX 4070 Ti (vendor `0x10DE`, device `0x2782`):

- `INTZ` capability, A2B10G10R10 render-target matching, creation, depth clear,
  overlapping D3D8 draws, and stencil rejection;
- translator treatment of the FOURCC as a logical 24-bit buffer, including
  the expected D3D8 Z-bias conversion;
- sampling only after unbinding depth, exact D3D8-visible render-target,
  viewport, render-state, and sampler-state restoration, and D3D11
  reconstruction of 0.25, 0.50, and clear-depth values; and
- the existing shared A2B10G10R10 probe, the complete x86 build, and all 15
  deterministic CTest suites as regressions.

GPU timestamp results for the full resolver GPU interval were:

| Encoding | Per-eye min / median / p95 / max | Estimated stereo p95 | Stereo storage |
| --- | --- | --- | --- |
| Packed `A8R8G8B8` | 0.0205 / 0.0214 / 0.0256 / 0.0328 ms | 0.0512 ms | 28.79 MiB |
| `A16B16G16R16F` | 0.0358 / 0.0369 / 0.0410 / 0.0430 ms | 0.0819 ms | 57.59 MiB |

These numbers measure depth export, not AO preparation, evaluation, denoise,
application, CPU setup, cross-process consumption, or frame pacing. Packed
`A8R8G8B8` is the live prototype choice because it passed the same correctness
checks with less GPU time, storage, and bandwidth. Live D3D11 must decode the
packed device depth and reconstruct view-space position from the matching eye
projection.

### 2. Live resource ownership -- implemented, headset validation pending

Translator bridge ABI 6 owns the D3D9 texture-backed depth and fullscreen
conversion work while returning validated opaque D3D8 surfaces/legacy handles
to the client. The client creates these resources only when live AO is
requested and the exact capability/creation sequence succeeds.

The current replay depth surfaces should be replaced only when the AO
capability gate succeeds. Otherwise `CreateAndClearFrameResources` must keep
creating the ordinary D24S8 surfaces it uses now. The translator must preserve
the wrapper's logical 24-bit depth and stencil behavior, including depth-bias
calculation; an unknown FOURCC must not accidentally make the wrapper treat
the surface as a zero-bit Z buffer.

Resolve both eyes after all mirrored world draws have completed and before
`PublishGpuFrame` issues its producer-completion query. The conversion must
save and restore the underlying D3D9 state without desynchronizing d3d8to9's
emulated D3D8 state.

The current hook enters `CompletePresentationFrame` only after the original
D3D8 `Present` returns, so finalization should already be outside the game's
active scene. Treat that as a code observation rather than an API-state
assumption: the standalone probe and live diagnostics must still prove the
resolve path and state restoration.

Protocol v11 adds two optional depth descriptions without changing the
semantics of the existing three colour/UI slots. Startup menu, CPU fallback,
offline colour-only controls, and failed depth capability/AO setup checks
remain valid no-AO routes. Exact D3D8-visible target, viewport, render, texture,
and sampler state is restored around every resolve.

## Implemented first AO algorithm

The bounded first implementation is an ASSAO-inspired but BFVR-owned compact
shader rather than a wholesale import of Intel's preset system. It decodes the
packed full-resolution device depth directly, reconstructs view position and
normals from the exact row-major projection used for that eye, evaluates eight
fixed spatial directions at half resolution, and applies a 3x3 bilateral
denoise. It has no temporal history, random frame rotation, motion-vector
dependency, or normal geometry replay. Intel ASSAO remains the quality
reference for a later headset-driven preset comparison if this smaller path
proves too limited.

This fits BFVR better than the alternatives:

| Candidate | BFVR assessment |
| --- | --- |
| Extra stereo geometry depth pass | Reject. It would replay hundreds of draws again, while measured replay cost already correlates almost exactly with draw count. |
| Generic ReShade/MXAO injection | Reject as the production design. It observes a generic Present boundary rather than BFVR's two owned eye targets, adds another hook owner, and the old root ReShade proxy is intentionally disabled. |
| Colour-only darkening/unsharp-mask "AO" | Reject. It cannot distinguish geometry from texture contrast and creates halos that are especially objectionable in stereo. |
| One-eye AO copied to both eyes | Reject. Disocclusions and silhouettes differ by eye; the AO term would disagree binocularly. |
| Temporal/checkerboard AO | Defer. BFVR has no motion vectors or TAA, currently submits at half the display refresh, and should not add history shimmer or ghosting to a first prototype. |
| XeGTAO | Good later comparison. It has strong quality/performance evidence and a spatial denoiser, but its reference sample is D3D12/Shader Model 6.3 and takes more adaptation than the native D3D11 ASSAO package. |

Intel reports the following historical ASSAO costs with supplied normals and a
two-pass blur: at 3840x2160 on GTX 1080, Lowest/Low/Medium cost approximately
0.59/1.17/2.12 ms; at 1920x1080 they cost 0.15/0.26/0.46 ms. BFVR's two
1872x2016 eyes contain about 7.55 million pixels, roughly 91% of a 4K frame,
but BFVR must reconstruct normals and add the D3D9 depth export. These numbers
show plausibility only; they are not a prediction for BFVR.

XeGTAO reports 0.56 ms for its full-resolution High preset at 1920x1080 on an
RTX 2060 and 1.4 ms at 3840x2160 on an RTX 3070. It explicitly recommends
lower resolution plus bilateral upsampling when still more speed is needed.

Primary references:

- [Intel: Adaptive Screen Space Ambient Occlusion](https://www.intel.com/content/www/us/en/developer/articles/technical/adaptive-screen-space-ambient-occlusion.html)
- [GameTechDev ASSAO source](https://github.com/GameTechDev/ASSAO)
- [GameTechDev XeGTAO source and measurements](https://github.com/GameTechDev/XeGTAO)
- [Jimenez et al.: Practical Realtime Strategies for Accurate Indirect Occlusion](https://www.activision.com/cdn/research/PracticalRealtimeStrategiesTRfinal.pdf)
- [AMD FidelityFX CACAO integration manual](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/combined-adaptive-compute-ambient-occlusion/)
- [NVIDIA HBAO+: full-resolution rationale](https://developer.nvidia.com/rendering-technologies/horizon-based-ambient-occlusion-plus)

## Composition and stereo policy

- Compute AO independently from each eye's matching depth and projection.
- Keep AO world-only. Ref2 UI remains a separate OpenXR layer and must not be
  darkened, blurred, or used as an occluder.
- Reconstruct normals from depth for the first prototype; BF1942 does not
  provide a normal G-buffer. Do not add a normal-only geometry replay.
- Use spatially stable sampling and a depth-aware spatial filter. Do not use a
  frame-varying random kernel without a temporal resolve.
- Multiply conservatively into the linear world colour during the existing
  final conversion draw, before its sRGB output encoding. This is a visual
  approximation because BFVR cannot insert AO into BF1942's original ambient
  lighting term after lighting has already been resolved.
- Fade AO only as its projected footprint becomes too small to resolve, and keep
  the radius small enough to emphasize contact and crevice shading rather than
  producing large halos. Do not introduce an arbitrary constant-view-depth
  boundary merely to limit the effect.
- Treat clear/sky depth as unoccluded. Inspect first-person weapons, foliage,
  smoke, water, terrain silhouettes, vehicle interiors, and near-plane edges
  explicitly during the A/B.

## Prototype measurements and controls

`BFVRAmbientOcclusionGpuProbe` ran 64 iterations at 1872x2016 per eye on the
RTX 4070 Ti. The real half-resolution shaders produced a non-trivial 8-bit AO
range of 202..255 on synthetic depth discontinuities. GPU timestamps measured
0.0451 ms and 0.0399 ms per-eye p95, or 0.0840 ms stereo p95, for evaluation
plus denoise. The existing stereo world conversion measured 0.1085 ms p95
with its original no-AO shader and 0.1208 ms with the separate AO-composite
shader, a 0.0123 ms p95 increment. A one-time first-use shader/target outlier
reached about 5.2 ms; it does not affect the 64-sample p95 but must be watched
for a visible first-frame hitch.

Combining the isolated packed-depth export p95 (0.0512 ms), x64 evaluation and
denoise p95 (0.0840 ms), and measured application increment (0.0123 ms) gives a
conservative component sum of about 0.148 ms. This is well inside the 1.0 ms
GPU target, but it is not a live-game percentile: the probes exclude game
content, x86 CPU setup, cross-process wait interaction, OpenXR scheduling, and
headset reprojection.

The extended `BFVRD3D8To9SharedSurfaceProbe --ambient-occlusion` completes the
whole transport without a game. At 1404x1512 it creates and clears two `INTZ`
depths, resolves them into shared `A8R8G8B8`, publishes protocol-v11 depth and
projection data, opens them in the x64 process, evaluates and applies AO, and
acknowledges the frame. The control retained exact world/UI pixels for its
uniform-depth scene. The matching no-AO D3D9 control and 193-frame named
keyed-mutex color control also passed.

Live diagnostics collect D3D9 export GPU time per eye; x64 evaluation/denoise
per eye and stereo; AO-applied world-conversion time; their combined interval;
existing total x64 source-consumption QPC time; and the producer's existing
`consumeWait`/publish timing. AO input rejection is logged for the first three
frames and falls back per frame. Initialization, capability, or allocation
failure disables the optional path without failing mandatory color/UI
presentation.

The first owner headset run exposed and resolved one process-lifetime resource
reuse defect. Oculus OpenXR, both packed-depth resources, D3D11 AO, and source
frame 1 initialized and presented. The reusable-frame reset preserved the INTZ
depths but omitted both packed depth-export surface pointers; frame 2 therefore
failed the transport identity check and stopped presentation. The reset now
preserves those COM identities, and its deterministic regression test covers
both exports. The repaired x86 build and all 15 CTest suites pass; live quality
and pacing acceptance remain pending.

The subsequent sustained owner run completed 7,767 timed AO frames with zero
depth-resolve failures. Packed depth export measured about 0.081 ms stereo p95,
and x64 AO evaluation, denoise, and AO-applied world conversion measured
0.273 ms p95. Its first quality defect is a plane-like distance transition and
horizontal bands crossing both the hangar floor and distant hill, accompanied
by an overly thin near-field footprint. Crossing unrelated surfaces makes this
a view-depth contour artifact, not a floor-orientation problem. The present
probe shader uses a fixed eight-pixel radius, a hard
`clamp(abs(viewZ) * 0.045, 0.10, 3.0)` view-distance range, and eight coherent
unrotated directions at half resolution, and two R8 UNORM AO targets. Those
choices explain the observation: the physical footprint shrinks near the
camera, while range knees, sparse screen-aligned sampling, and 8-bit AO
quantization become equal-view-distance contours across receding geometry. The next
quality iteration should project one view-space radius into per-eye pixels,
fade smoothly at distance, and rotate/interleave the spatial kernel before
retuning strength or power.

The first follow-up comparison deliberately changes only result precision and
owner-requested intensity: both half-resolution AO targets are R16_FLOAT rather
than R8 UNORM, and final world-composite intensity is 1.0 rather than 0.75.
Packed depth, radius/range logic, kernel, and denoise remain identical. The
native-size 64-iteration GPU probe passes with mapped output 201..255 and
0.0860 ms stereo evaluation/denoise p95. This does not prove the contour cause;
if the same bands remain in the hangar, R8 result quantization is rejected as
the primary explanation.

That comparison rejected R8 quantization: the bands remained and higher
intensity made them more visible. The owner describes a head-locked central
quasi-rectangle containing exact groups of four horizontal lines separated by
gaps; it translates with head pitch and crosses unrelated world geometry. The
next staged presenter replaces the coherent taps with a per-pixel-rotated
eight-sample disk, projects a 0.60 m view-space radius to a 2..48-pixel range,
and fades smoothly over 90..180 m. It also installs explicit opaque-blend,
depth-disabled, default-rasterizer, full-scissor, and viewport state before AO
evaluation. The native-size probe passes with mapped output 136..255 and
0.1943 ms stereo evaluation/denoise p95. If the live pattern survives this
kernel/state correction, capture the per-eye AO result before further tuning.

The owner then tested both the original 45..90 m fade and a 90..180 m interval.
The banding became subtler after the rotated kernel/state correction, but the
rectangle remained at the same apparent distance after the fade interval was
doubled. That observation rejects the explicit far fade as the rectangle's
source. A complete audit found no four-row viewport, pitch, or packing operation
in the full-resolution D3D9 depth export. It did find two later screen-domain
discontinuities:

1. AO was evaluated at 936x1008 and enlarged to 1872x2016 by the color scaler's
   ordinary linear sampler. It had no depth-aware bilateral upsample. AMD CACAO
   uses a 5x5 depth-guided bilateral upsampler for downsampled modes, XeGTAO
   likewise recommends a bilateral upsample when reducing resolution, and
   NVIDIA explicitly identifies reduced-resolution HBAO as a source of
   difficult-to-hide instability while promoting full-resolution HBAO+.
2. The 0.60 m radius used a hard `clamp` at 48 projected pixels. At this eye
   resolution the equality occurs at a nearby constant view depth. On either
   side of that surface the point-sampled packed-depth taps follow different
   radius rules, providing both the head-locked rectangle geometry and discrete
   row advancement needed for the reported bands.

The evidence-backed replacement eliminates both boundaries rather than moving
another distance parameter. AO intermediates now match the full 1872x2016 eye,
so final application is one-to-one and has no low-resolution stretch. A C1
quadratic transition eases projected radii from the exact value into the
48-pixel safety cap over 24..72 pixels, eliminating the clamp derivative. The
90..180 m fade is removed; only a smooth 0.5..2-pixel footprint fade remains at
distances where SSAO is no longer resolvable. The native GPU probe asserts the
AO resource dimensions, produces mapped output 128..255, and measures about
0.645 ms median for stereo evaluation plus denoise; compositing adds roughly
0.02..0.03 ms p95 in repeated controls. Periodic GPU scheduling outliers make
the short offline AO p95 non-representative, so the existing live timestamp
instrumentation remains the acceptance authority. The full 1404x1512
cross-process INTZ-to-AO control also passes with exact world/UI transport. The
staged presenter SHA-256 is
`61E2B2F7D682DE382132A9DF4E11CF3A7F2A10B7604D27BC8BB26735AD28128E`.

The owner's same-hangar headset comparison confirms that the native-resolution
path removes the exact groups of four horizontal bands. The lighter head-locked
rectangle remains, so the bands and rectangle were not one artifact: ordinary
half-resolution color upscaling caused the former, while the latter survives
removal of that path, the explicit far fade, and the hard projected-radius cap.
The run collected 3378 valid live samples. Stereo AO evaluation/denoise measured
0.4813 ms median and 1.6507 ms p95; AO-applied world conversion measured 0.1782
ms median and 0.1894 ms p95, or 0.6604/1.8757 ms combined. Packed depth export
measured 0.0348/0.0389 ms median/p95 per eye. A conservative sum therefore puts
the complete instrumented path near 0.73 ms median and 1.95 ms p95. No timing
queries failed and the presenter ended healthy, but the p95 result does not meet
the provisional 1.0 ms gate.

The owner's refined report places the remaining geometry-deformed region about
10..15 ft from the head and establishes that the former four-row bands existed
only inside it. The exact active shader then revealed a closer distance match
than the discarded far fade: the bilateral denoiser used
`max(abs(centerZ) * 0.02, 0.05)`. Its fixed and relative terms meet at 2.5 game
metres (8.2 ft), changing filter-neighbour acceptance on a head-locked
constant-view-depth surface. Intel's production XeGTAO instead calculates
edges from depth differences relative to centre view depth and supplies those
edge weights to its denoiser; it has no equivalent absolute-distance floor.
See the [XeGTAO implementation](https://github.com/GameTechDev/XeGTAO) and its
[edge/denoise shader](https://raw.githubusercontent.com/GameTechDev/XeGTAO/master/Source/Rendering/Shaders/XeGTAO.hlsli).

The staged correction makes BFVR's denoise tolerance purely relative at all
visible distances. It also replaces the remaining piecewise projected-radius
cap with the branchless analytic limiter `r / sqrt(1 + (r / 48)^2)`, which
approaches the cache-safety bound without a branch, equality surface, or
derivative knee. The native 1872x2016 GPU probe passes with output 124..255;
the full 1404x1512 cross-process INTZ/AO control and all 15 CTest suites pass.
The staged presenter SHA-256 is
`ADB51010B22FACAD713E77DA9540C5C31DC86B5F1A5EDAA74B5B62D84A9937B9`.
Only the same-hangar headset comparison can accept or reject the visual fix.

That comparison rejected this candidate as a complete fix. The rectangle
changed but retained the same hard-edged apparent distance across floors,
walls, and other geometry, and AO remained materially stronger on its near
side. This rules out the former denoiser knee as the primary source and exposes
a consistency error in the remaining analytic limiter: it compresses the
nearby screen-space sample footprint below the projection of 0.60 m, but the
range weight still divides reconstructed sample distance by the full 0.60 m.
The near side therefore receives larger range weights than the far side even
though the limiter itself has no branch.

The XeGTAO reference path computes `screenspaceRadius` directly from the full
world-space `effectRadius` and the view-space size of a pixel, then uses that
same `effectRadius` for near-field falloff. It has no maximum-pixel radius. The
next correction removes BFVR's 48-pixel limiter entirely so the sampling
footprint and attenuation describe the same world-space neighbourhood at every
view distance. The fixed eight-tap count means this does not add shader samples;
native resolution, relative-depth denoise, and the subpixel-only distant fade
remain unchanged.

That correction is now implemented and staged without changing strength or
other quality settings. The native 1872x2016 GPU probe passes with output
221..255 and approximately 1.103 ms median stereo evaluation plus denoise. The
wider nearby fetch footprint costs more cache locality even with the same eight
taps. The 1404x1512 cross-process INTZ/AO control and all 15 CTest suites pass.
Both launcher-facing presenter copies have SHA-256
`8C3361B0F6663803A1C5243798B3FA3F04FB49E2B5CCE2CF5A0CFA3B7E0110D1`.
Headset validation must determine whether the hard-edged strength transition is
gone before strength is retuned.

The headset result partially supports but rejects uncapping as a complete fix:
the edge became subtler, but useful AO became effectively invisible. The
remaining eight-tap disk uses `sqrt(x)` radii, which is area-uniform but pushes
samples outward. At a 0.60 m radius, the nearest tap is 0.15 m away and six of
eight taps are beyond 0.33 m. The former limiter had accidentally compressed
those taps into the nearby geometry that produces visible crevice/contact AO.
XeGTAO instead uses approximately `x^2` sample-distance distribution to
concentrate samples near the evaluated pixel, plus a minimum pixel-sized offset,
while retaining its full world-space effect radius. The next candidate will
adopt that distribution without changing strength or restoring a distance
limiter.

That kernel is now staged. It squares normalized tap distance, adds a minimum
1.3-pixel resolved offset, and leaves the uncapped 0.60 m projection, eight-tap
budget, strength, and relative-depth denoiser unchanged. At 1872x2016 per eye,
probe output expands from the rejected kernel's 221..255 to 148..255, while
median stereo evaluation plus denoise improves from approximately 1.103 ms to
0.843 ms. The complete 1404x1512 cross-process INTZ/AO control and all 15 CTest
suites pass. Both presenter copies have SHA-256
`6AA46671452DA44DD58CD2AC1DB4EA698FFF293606878A3899B17F99AC6E56B6`.
The same-hangar headset comparison must now prove that useful AO is visible
without restoring the hard-edged near/far strength boundary.

The owner rejected that comparison build: the rectangle remained and looked
worse than in the preceding uncapped build, while AO stayed too subtle to be
useful. Further cutoff experiments are stopped at the owner's request. The
rollback target is the last accepted band-removal state: native-resolution AO,
rotated `sqrt(x)` disk taps, the C1-continuous transition over 24..72 projected
pixels into a 48-pixel cap, and the former bilateral denoiser. That state is
known to remove the four-row bands and retain useful AO; its separate rectangle
artifact remains unresolved.

The rebuilt rollback reproduces the accepted build's native-probe signature:
output 128..255 and 0.643 ms median stereo evaluation plus denoise, versus the
earlier documented 128..255 and approximately 0.645 ms. The cross-process AO
control and all 15 CTest suites pass. Both presenter copies have SHA-256
`53BBA3A9568D950A20A4C56A92B4B266C7574371DC79D2224C9F5B6AA092F209`.
This intentionally restores the known rectangle together with the useful AO;
no later cutoff experiment is retained.

## Performance and acceptance gates

Add D3D9 and D3D11 GPU timestamp queries around, at minimum:

- left/right depth export;
- AO depth preparation, if a later algorithm adds a separate preparation pass;
- AO evaluation and spatial denoise;
- AO application in the world conversion draw; and
- total x64 source consumption and producer `consumeWait`.

CPU wall time is not an adequate substitute for these GPU stages.

The prototype defaults off. Isolated component measurements leave most of the
provisional budget available, but live gameplay still has to prove that result.
A reasonable first acceptance gate
on the owner's native 1872x2016-per-eye path is no more than 1.0 ms p95 added
GPU time for the complete live stereo depth-export-plus-AO path, with no meaningful
regression in new-source rate, median/p95/worst pacing, failed frames, or
producer-consumer synchronization. This is a target to test, not a claim that
the implementation already meets it.

Headset acceptance also requires:

- visible improvement in grounding and small-scale depth on more than one map;
- no binocular mismatch;
- no head-motion shimmer or reprojection-amplified crawling;
- no dark halo around weapons, foliage, sky, smoke, or water;
- unchanged HUD/menu legibility and alpha; and
- a true off/on A/B from the same build and scene.

Only after those gates pass should an owner-approved quality level become a
default. Bloom remains fully disabled and is a separate future experiment; it
should not be coupled to AO resource allocation, shaders, or controls.
