# BFVR spatial screen-space global illumination prototype

## Current status: rejected and disabled

After testing the working colour-bounce and spatial-denoise revisions, the owner
determined that SSGI is not worth retaining in the active BFVR configuration.
The current launcher explicitly sets `BFVR_OPENXR_SSGI=0` and intensity `0.0`.
The x86 producer's fail-closed gate accepts only the exact value `1`; `0` omits
the SSGI protocol request. The x64 consumer therefore does not initialize the
SSGI shader set, allocate its per-eye guide/radiance resources, select an SSGI
composite variant, or execute SSGI frame work. AO and water SSR may still request
the shared packed-depth export for their own independent paths. A final
disabled-path cross-process control retained exact left/right/UI pixels
`[FF123456,FF654321,9E1452EB]`; the x64 log contained only the ordinary world/UI
scaler and no SSGI initialization, allocation, frame, or timing entry.

## Scope and evidence

BFVR already owns the inputs needed for a bounded SSGI experiment: matching
per-eye final world colour, packed device depth, exact projection matrices, and
proven view-position/normal reconstruction. Ref2 UI is transported separately,
so it can remain outside indirect lighting. No additional BF1942 geometry replay
or reverse-engineered memory boundary is required.

The prototype follows the screen-space directional-occlusion/radiosity family,
which generalizes depth-buffer neighbourhood sampling from occlusion to one-bounce
colour transfer. Ritschel, Grosch, and Seidel describe one-bounce indirect colour
bleeding as a small extension over SSAO. McGuire et al. likewise gather radiosity
from colour and geometry buffers, while emphasizing that screen-space results
remain view-dependent and incomplete. AMD's current screen-space traversal
guidance reinforces spatial denoising and explicit screen-edge limitations; its
full implementation targets newer D3D12/Vulkan shader models and is not copied
into BFVR's D3D11 path.

Primary references:

- [Ritschel et al., Approximating Dynamic Global Illumination in Image Space](https://doi.org/10.1145/1507149.1507161)
- [Mara et al., Fast Global Illumination Approximations on Deep G-Buffers](https://research.nvidia.com/publication/2014-06_fast-global-illumination-approximations-deep-g-buffers)
- [McGuire et al., Scalable Ambient Obscurance](https://research.nvidia.com/publication/2012-06_scalable-ambient-obscurance)
- [AMD FidelityFX Stochastic Screen-Space Reflections integration manual](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/stochastic-screen-space-reflections/)

## Implemented path

`BFVR_OPENXR_SSGI=1` makes protocol v14 request the existing optional two-eye
packed-depth export even when AO and water SSR are off. The x64 consumer then
processes each eye independently:

1. At native eye resolution, reconstruct view position and a camera-facing
   normal from full-resolution packed depth. This one-to-one screen domain
   removes the half-resolution row grouping observed in the first live build.
2. Gather four per-pixel-rotated spatial samples over a 4.0 m view-space
   neighbourhood. Preserve directional receiver/emitter bounce, and add a
   bounded finite-patch term only for colour that is brighter than the receiver.
   This broadens colour/light transfer without brightening a uniform flat-plane
   transport control. A C1-continuous 320-pixel projected-radius safety limit
   avoids a hard screen-domain clamp. Convert legacy sRGB colour to linear.
3. Apply three ping-pong native-resolution 3x3 spatial denoise passes using
   relative view-depth and normal agreement. There is no history buffer or
   frame-varying sample sequence.
4. Add the matching eye's radiance in the existing linear world conversion at
   `BFVR_OPENXR_SSGI_INTENSITY` (default `0.65`, range `0..2`). AO remains an
   independent multiplicative input, water SSR remains a later reflection blend,
   and Ref2 UI remains separate.

`BFVR_OPENXR_SSGI_DEBUG=1` bypasses the ordinary world composite and displays
the SSGI result with 32x exposure. Valid pixels with zero bounced radiance are
black; invalid packed-depth/reconstructed-normal guide pixels are magenta.
Value `2` displays guide coverage directly (white valid, black invalid).
These are evidence controls, not quality modes, and never replace Ref2 UI.

Shader/resource/configuration failure disables only SSGI. Invalid per-frame
depth or projection data presents the ordinary colour/UI frame. The implementation
allocates one native-resolution RGBA16F guide plus two native-resolution RGBA16F
radiance targets per eye and records GPU timestamps for the complete spatial stage.

## Current validation

The first 2026-08-03 live owner run reported no perceptible lighting or colour
bounce indoors or outdoors. Its presenter log proves SSGI initialization and
5,941 successful stereo executions with packed depth, but does not prove that
real BF1942 frames produced nonzero radiance. The live timing was `0.4147 ms`
median / `0.6922 ms` p95. The diagnostic views above are the next evidence gate;
energy and radius must not be tuned until they distinguish zero output from a
weak output.

The subsequent 32x-radiance evidence view resolved that ambiguity. Most finite-
depth geometry was black, radiance appeared only at mesh/ground junctions, and
the skybox was magenta. Thus scene geometry generally has a valid guide and the
sky correctly lacks finite depth, while the first endpoint/form-factor gather is
effectively contact-only on real BF1942 content. Groups of head-relative
horizontal bands also matched the failure class of BFVR's first rejected SSAO
sampling pattern. This is an estimator/sampling failure, not evidence for merely
raising the composite intensity; guide-only debug mode 2 is unnecessary before
the next revision.

The replacement removes the rejected half-resolution screen domain, evaluates
four rotated taps natively, expands the view-space radius from 2.0 m to 4.0 m,
and supplements strict directional form factors with brighter-neighbour finite-
patch contrast transfer. Its strengthened 1872x2016 synthetic control now
requires broad receiver coverage: `28.73%` of receiver pixels exceed `0.002`
red lift, mean red lift is `0.016338`, and the maximum is `0.285028` with
negligible green/blue lift. The 64-sample offline timing was `1.6097 ms` stereo
median / `4.4933 ms` p95; scheduling outliers make the live headset summary the
performance authority. This is staged as a quality experiment, not an accepted
performance result.

The owner then confirmed that this replacement makes SSGI clearly visible, but
rejected its quality because illumination appears as extremely noisy, moving
clouds of dots. That establishes successful real-scene colour/light transfer
and identifies spatial variance from the four-tap estimator as the next defect.
The native screen domain, contrast-only finite-patch model, radius, and `0.65`
composite intensity remain fixed while stronger depth/normal-aware spatial
reconstruction is evaluated. Temporal history remains out of scope until this
spatial path is exhausted.

The staged spatial-noise revision runs the same edge-aware 3x3 filter three
times through the two existing radiance targets. The strengthened native-size
probe now checks local high-frequency residual as well as colour/coverage:
receiver coverage is `29.17%`, mean red lift is `0.016327`, maximum red lift is
`0.246442`, and high-frequency p95 is `0.003359` against a `0.01` ceiling.
The final 64-sample offline repeat measures `1.6783 ms` stereo median /
`2.5528 ms` p95; an earlier run measured `1.8831 ms` / `4.9603 ms`, demonstrating enough
scheduling variance that live quality and timing remain the authority. No
temporal state was added.

The rejected first revision's 64-iteration native-size control measured only
its maximum contact/corner pixel (`redLift=0.013490`) and reported `0.4782 ms`
median / `0.6562 ms` p95 for half-resolution work. The later live evidence
showed why that maximum-only assertion was insufficient.

The 1404x1512 D3D8 -> D3D9Ex -> packed depth -> x64 SSGI control passed with
exact left/right/UI centre pixels and a healthy acknowledgement. Existing native
AO and bloom GPU probes still pass after the additive SSGI composite input was
introduced. These are synthetic controls, not headset acceptance.

## Headset acceptance gate

Keep SSGI owner-comparison-only until same-build `BFVR_OPENXR_SSGI=0/1` runs
establish all of the following:

- visible, soft colour transfer in corners/interiors on more than one map;
- no bright leakage across silhouettes, thin walls, weapons, foliage, smoke,
  water, or the sky;
- no screen-edge flash, head-motion crawl, reprojection-amplified noise, or
  binocular disagreement;
- unchanged Ref2 UI/menu legibility and alpha;
- no failed frames or material source-rate, pacing, consumption-wait, or OpenXR
  regression; and
- a complete live packed-depth-plus-SSGI p95 cost that remains acceptable to the
  owner. A provisional target is at most 1.5 ms at 1872x2016 per eye.

Screen-space data cannot represent off-screen, occluded, or back-face emitters.
BFVR also samples already-lit LDR scene colour rather than separate albedo/direct
lighting, so this is a controlled visual approximation, not energy-conserving
global illumination. Temporal accumulation should not be added without proven
motion vectors and a stereo/reprojection-specific history rejection design.
