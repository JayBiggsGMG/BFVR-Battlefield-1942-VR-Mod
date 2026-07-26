# Early D3D8 observer prototype

This is BFVR's first in-process prototype. It exists only to prove that the
supported BF1942 build can be entered before startup, that its D3D8 import can
be observed without changing the returned D3D8 interface.

## What it does

`BFVRLoader.exe` starts `BF1942.exe` suspended and loads `BFVRClient.dll` from
the single `BFVR` folder. After `LoadLibrary` has returned, it invokes the
client's exported initializer on a second remote thread while the game remains
suspended, and only then resumes the game. This keeps the IAT mutation outside
`DllMain` and the Windows loader lock. The initializer replaces only the game
executable's in-memory `Direct3DCreate8` IAT entry and returns the original
`IDirect3D8` interface unchanged.

On the same game thread, it can arm chained one-shot hardware execution
breakpoints on the original `IDirect3D8::CreateDevice`, `Clear`, `BeginScene`,
`EndScene`, first `Present`, and then `Reset` target. Each handler copies only
entry values and disables each pass target after its first hit. If startup
reaches Reset before complete scene-phase evidence, the observer reuses the
three freed slots for one `SetRenderTarget`, `SetTransform`, and `BeginScene`
entry each. It reports partial results if any target remains absent before the
bounded diagnostic child exits. A worker writes the result to
`BFVR\logs\observer.log`. This does not replace the interface, change its
vtable, or patch D3D8 code.

After the first observed Reset, the diagnostic re-arms the creation thread's
first post-Reset `Present` entry. A companion ownership pass can use a one-shot
`Present` execution breakpoint on other existing game threads, but only after
saving and later restoring a thread's unused debug-register context. Threads
that already have an active debug-register setting are skipped. In a controlled
local-map run, post-Reset `Present` remained on the device-creation thread and
no other eligible existing thread presented during the 90-second pass. This
changes neither D3D8 objects nor game code.

The diagnostic source retains two bounded, execution-only BFPlayer probes: the
direct `+0x68` setter at `0x00409480`, and the vehicle/camera transaction at
`0x00408010`. They are compiled but disabled in ordinary runs. The attempted
local-player probe at `0x0040762A` is deliberately absent: it is a hot
null-return path and froze a map-load diagnostic session.

A separate 4 Hz worker reads the known player-manager singleton only. It first
records the address of its `vtable +0x20` getter, then reads the getter's
confirmed backing field at manager `+0x54` to find the local BFPlayer without
invoking game code. It validates the local `+0x68` camera interface against
the profiled `world::Camera` vtable, then copies at most eight 4x4 candidate
matrices from interface `+0x58` (camera-root `+0x1B4`). It performs no virtual
calls, sets no camera breakpoint, and writes no game memory.

## What it explicitly does not do

- replace or proxy `d3d8.dll`, `dxgi.dll`, `dsound.dll`, or any game DLL;
- patch the game executable on disk;
- modify the returned D3D8 object's vtable (the earlier experiments remain disabled);
- hook `Present`, `Reset`, camera methods, scene calls, input, or simulation
  in its default passive mode;
- create an OpenXR session, graphics binding, swapchain, textures, input action,
  or VR view;
- modify any camera, player, weapon, or vehicle state.

After the original `Direct3DCreate8` returns, the client starts one background
worker that creates and destroys an OpenXR instance through BFVR's pinned x86
loader, then performs only a head-mounted-display system query. If an HMD is
available and the runtime advertises `XR_KHR_D3D11_enable`, BFVR destroys that
baseline instance and recreates one short-lived D3D11-extension instance only
to read `xrGetD3D11GraphicsRequirementsKHR` (adapter LUID and minimum feature
level). It never creates a D3D11 device, graphics binding, session, swapchain,
input action, composition layer, or VR view. A missing headset is logged as a
flat-safe, expected state and does not block the game.

It is a diagnostic build and not a playable VR build. The IAT observer has
successfully recorded `Direct3DCreate8` with SDK version 220 in controlled,
elevated offline sessions. A first attempt to clone the returned 16-slot D3D8
vtable and replace `CreateDevice` produced an access-violation exit
(`0xC0000005`) before device creation. That substitution is deliberately
disabled in the current build. The one-shot hardware-breakpoint probe is the
safe alternative: it captured the original `CreateDevice` entry without a
crash, with adapter 0, device type 1, behavior flags `0x52`, a 1920x1080
full-screen back buffer (format 22), depth enabled with format 75 (D24S8),
discard swap effect, and immediate presentation interval (`0x80000000`). The
first `Present` call was observed without interface or code replacement. An
earlier session resolved `Present` to system `d3d8.dll` and `CreateDevice` /
`Reset` to `apphelp.dll` compatibility shims. The corrected normal 90-second
run then captured a real post-Present `Reset` entry with unchanged creation
parameters; all three targets resolved directly to system `d3d8.dll` in that
path. BFVR must preserve either routing path and treat both as runtime evidence,
not as a DLL substitution target.

An exact ABI audit found that the earlier scene map had also omitted
`UpdateTexture` at slot 29. The correct contiguous tail is
`UpdateTexture=29`, `GetFrontBuffer=30`, `SetRenderTarget=31`,
`GetRenderTarget=32`, `GetDepthStencilSurface=33`, `BeginScene=34`,
`EndScene=35`, `Clear=36`, and `SetTransform=37`. The map-gated combined run
captured a six-argument entry from `FUN_00667680` at slot 36, which is the
`Clear` ABI and proves that the former `SetTransform` label was false. Its
other corrected observations are `GetDepthStencilSurface`, `BeginScene`,
`EndScene`, `Clear`, and `Present` on the device-creation/presentation
thread; the hardware-register state was restored afterward. One subsequent
map-gated run also reached the true `SetRenderTarget` and `SetTransform`
slots: the render target was `0x121A2BC0` with a null replacement depth
surface, and transform state `3` supplied a readable projection matrix. Eight
`Present` samples measured `0.050 ms` by `QueryPerformanceCounter`, proving a
tight breakpoint-entry burst rather than frame cadence. The attempted static
View/World-wrapper follow-up did not recur after that projection burst, so the
observer now takes one bounded, read-only snapshot of the already confirmed
renderer transaction caches instead: World `+0x284`, View `+0x2c4`, and
Projection `+0x304`. `FUN_00606100` proves `DAT_009c017c` is a pointer global
to the active transaction buffer, so the observer reads the pointer then its
caches. The entry-aligned version validated the boundary: the cached Projection
matrix exactly matched the state-3 `SetTransform` argument, while World and
View were non-identity in that same transaction. A later transaction contained
default identity World/View state, confirming that this is per-transaction
renderer state rather than one permanent camera object. An exact same-boundary
comparison also showed the local `world::Camera +0x58` candidate matrix does
not equal the live renderer World/View pair, so it is not used as a direct
render-camera bridge. The observer also resolved stack word 6 at the D3D entry
to `0x00466F56`, immediately after `FUN_0045fdc0` in the normal branch of
renderer coordinator `FUN_00466d80`. That branch applies Projection, then
View, before proceeding into main scene-render setup. The bounded trace was
captured before any user vehicle interaction, so it identifies the ordinary
on-foot world-render submission, not a vehicle-specific camera path. It neither calls
renderer code nor writes D3D/game state.

The current observer adds one automatic five-second, execution-only follow-up
after the combined trace. It captures the nesting
`FUN_0044abc0 → FUN_00466d80 → FUN_00466cd0 → FUN_004662c0` and dynamically
uses the entered view function's stack return as its one return breakpoint. In
the verified map run it returned to `0x00466D1C`, the view-list loop, after
`1.042 ms` by QPC; all debug registers were restored. This identifies a
per-view world-render scope only. It does not hook or rerender that scope, and
it does not yet classify UI/overlay passes or claim a complete frame interval.
These observations do not establish world/UI boundaries, a frame order, a
per-frame hook, or any stereo-rendering claim.

One subsequent automatic subpass trace mapped four virtual-call sites inside
that same `FUN_004662c0` view. The first (`0x00466AD0`) resolves to
`FUN_00569640`, which only replaces a retained object reference and is not a
draw. `0x00466B05` resolves to `0x0062CC70`, the
`Shaders/SkinningShader2Bones` scene path in mode 0. `0x00466B89` resolves to
the mode-1 scene-context dispatcher at `0x0062CDD0`; `0x00466C5A` resolves to
the same skinning-shader scene path in mode 2 at `0x0062CB00`. All four fired
in one ordinary view with debug registers restored.

A final four-point layer trace then confirmed the handoff out of that view
loop. It observed `FUN_004662c0`, then `0x00466FD9` after `FUN_00466cd0`
returned to its caller, and `FUN_0046e900` immediately afterward (return
`0x0046703B`). Static code shows that function iterates a flagged post-view
callback list. The trace also reached `FUN_00461400` at return `0x004674CB`;
that function queues fixed-pixel text through `FUN_004611d0`. All debug
registers were restored. The scene-view candidate is therefore
`FUN_004662c0 -> 0x00466D1C`, ending before known post-view callbacks and text
overlay work. It still includes an unresolved extension callback list, so it
is not yet an approved UI-free replay boundary.

The current source then runs one 1.5-second frame-model trace after this
handoff trace. It records one execution entry per target per 16 ms sampling
period for `FUN_0044abc0`, `FUN_00466d80`, `FUN_004662c0`, and the original
device `Present` target on the known device thread. It reports their sampled
counts, sampled Present cadence, and the mean number of sampled game boundaries
between sampled Present calls. The first uncapped version trapped repeatedly in
the hot renderer coordinator and never let the game progress to the other
targets; each target is therefore disarmed after a sample and rearmed by the
worker at the next period. The original debug-register state is restored in
either case. This is still execution-only instrumentation: it replaces no
interface, calls no game function, and writes no game/D3D state.

On 2026-07-22, a direct eight-second launch of untouched `BF1942.exe` stayed
alive, as did an eight-second launch with an injected inert BFVR DLL that did
not modify the IAT. By contrast, the former build intermittently exited with
`0xFFFFFFFF` when it rewrote the import from `DllMain`. Moving that exact IAT
write to the post-`LoadLibrary` initializer produced native, uninstrumented
12-, 80-, and 100-second observer runs. This is strong evidence that the
loader-lock timing of the old attach path was unsafe; it is not evidence for a
DirectX-wrapper requirement or a game-renderer incompatibility.

The final 80-second verification reached the frame-model trace and retained
61 samples of each target in its 1.5-second window. Sampled Present intervals
averaged 24.691 ms (minimum 12.458, maximum 35.842), with 1.00 sampled frame
coordinator, 0.98 sampled renderer coordinator, and 0.98 sampled per-view
renderer entries between Presents. Because each target is intentionally
one-shot per 16 ms period, these are coarse ordering and cadence samples, not
an unaliased frame-time measurement.

## External native Present timing

An external 60-second PresentMon ETW capture of untouched `BF1942.exe` then
recorded 12,296 process-present events across 53.174 seconds. For the 12,286
steady events at or below 100 ms, `FrameTime` had a 5.676 ms median, 8.156 ms
p95, 9.142 ms p99, and 3.909 ms arithmetic mean; 52 events exceeded 16.667 ms
and 47 exceeded 33.333 ms. Ten transition events exceeded 100 ms, led by one
3,030.344 ms stall. This gives an unaliased native Present-call baseline and
explains why the 16-ms in-process sampler was not a frame-time source.

PresentMon classified every event as `Other / Composed: Copy with GPU GDI`,
with no display-latency or displayed-time metric available. The capture is
therefore valid for process Present-call cadence but not display-latched timing
or OpenXR motion-to-photon latency.

## Map-gated auxiliary render-target anchor

The map-gated `SetRenderTarget` trace now reads the bound color argument's COM
metadata without invoking D3D8. One native run recorded a color surface at
`0x12E71C80`, vtable `0x5B691318`, and `IDirect3DSurface8::GetDesc` entry
`0x5B6C6130`, all in the active system D3D8 module. A five-second follow-up
watched only for BF1942 itself to call `GetDesc` on that exact surface; it made
no matching call and the original debug registers were restored.

The later descriptor probe corrected the interpretation of this anchor. At the
same `SetRenderTarget` wrapper return (`0x00667732`), the surface was 128x128,
format 21, type 1, render-target usage, pool 0, and had no multisampling. It is
therefore an auxiliary render target, not the full-size world-color target.
Any active resource bridge for a future copy or OpenXR submission path must
still execute on the game device thread rather than from a worker thread.

## Opt-in same-device-thread Present bridge

`BFVRLoader --present-bridge-probe` is that separate safety test. It is off by
default and passes an explicit initializer flag to the injected client; no
environment variable, proxy DLL, or game file is involved. The worker waits
until the existing original-API lifecycle trace has observed `CreateDevice`,
`Reset`, and the first post-Reset `Present`, and has restored the device
thread's debug registers. It also verifies that the resolved target belongs to
the active `d3d8.dll` before doing anything active.

Only then, the client uses pinned MinHook v1.3.4 source to put a temporary
detour on that resolved `IDirect3DDevice8::Present` implementation. The
detour records atomic call/thread counters and QPC values, then immediately
calls MinHook's original trampoline. It performs no D3D8, OpenXR, game, COM,
or logging call. It exists only for the diagnostic process lifetime; normal
observer runs do not patch `Present`.

The first bounded 60-second verification (2026-07-22) enabled the detour at
system-D3D8 address `0x5B6BA1E0` after the lifecycle trace. In its five-second
observation period it forwarded 720 genuine calls. Both the first and final
calls ran on thread `24128`, exactly matching the independently observed D3D
device/creation thread. The loader then terminated only its own diagnostic
child at the timeout. This proves a narrow same-thread callback bridge; it is
not yet permission to query a surface, create a resource, copy pixels, or
submit OpenXR images.

An earlier revision of `BFVRLoader --surface-descriptor-probe` was a separate,
one-shot post-`Present` test. After the original `Present` returned, it called
`GetRenderTarget`, called `GetDesc` on the returned surface, and balanced the
temporary reference with `Release` on the same device thread. It wrote only a
result record; it did not copy, lock, create, or replace a D3D resource.

The corrected 100-second verification (2026-07-22) completed that sequence on
device thread `27988`: both D3D calls returned `S_OK`, no structured exception
occurred, and the returned current target described a 1920x1080, format-22,
type-1, render-target-usage surface in pool 0 with no multisampling. The
temporary target (`0x0A806B9C`) did not equal the earlier map `SetRenderTarget`
color argument (`0x12A5D520`). That difference means this post-`Present`
location proves same-thread resource ownership and descriptor ABI, but not the
world-render timing of that full-size target.

An intermediate `--surface-descriptor-probe` revision moved the descriptor
query to the map-classified `SetRenderTarget` path. It installed the forwarding
detour only after the verified CreateDevice/Reset/post-Reset-Present lifecycle
had released the device thread. After original `SetRenderTarget` succeeded, the
detour balanced `AddRef`/`GetDesc`/`Release` on that same thread. A bounded
follow-up allowed at most 32 descriptor sequences while looking for a target
matching the 1920x1080 backbuffer dimensions.

The exact-boundary run completed on device thread `7968`: original
`SetRenderTarget`, `GetDesc`, and the balanced reference operations succeeded
without a structured exception. `AddRef` changed the reported count to 3 and
`Release` returned it to 2. The classified target at `0x00667732` was 128x128,
format 21, type 1, usage `0x1`, pool 0, 65,536 bytes, and no multisampling. A
later 32-call bound completed without an exception but found no 1920x1080
surface in that stream. The old “world color surface” label is rejected.

Static analysis agrees with the dynamic boundary: `FUN_006676c0` is a generic
renderer-target binding wrapper. It resolves an optional texture-backed color
surface and depth surface, falls back to its cached default color surface when
the color source is null, gets the active renderer/device interface, and calls
its vtable `+0x7c`; the dynamic return after that call is `0x00667732`.

The current probe resolves that ambiguity at the evidence-backed ordinary-world
Projection boundary. After the verified lifecycle releases the device thread,
it temporarily detours system-D3D8 `SetTransform`. Every call forwards to the
original; only state 3 returning to `0x0045FE21`, with external caller
`0x00466F56`, a profiled local-camera interface present, and execution on the
verified device thread can perform one `GetRenderTarget`/`GetDesc`/`Release`
transaction.

The decisive run completed on device thread `30220`. Original `SetTransform`,
`GetRenderTarget`, and `GetDesc` all returned `S_OK`; `Release` returned the
temporary reference count to 1 and no structured exception occurred. At that
exact world Projection submission, current surface `0x0642753C` described a
1920x1080, format-22, type-1, usage-`0x1`, pool-0 render target with no
multisampling. This is the confirmed ordinary-world color/backbuffer boundary.

`BFVRLoader --surface-copy-probe` is the separately opt-in next step. At the
same exact boundary it first requires the source to match the active
presentation size/format, render-target usage, pool 0, and no multisampling. It
also verifies that `CreateRenderTarget` and `CopyRects` resolve to system
`d3d8.dll`. It then creates one matching BFVR-owned render target, copies one
explicit full-surface rectangle, and releases both the owned target and the
temporary game-target reference before returning.

The bounded local-host run completed on device thread `8480`. The 1920x1080
format-22 source passed every gate; `CreateRenderTarget`, the owned-surface
`GetDesc`, and `CopyRects` all returned `S_OK`. The owned descriptor exactly
matched the source. Its release returned 0, the temporary source release
returned 1, both outstanding-reference flags returned to zero,
`resetSafeAtReturn=1`, and no structured exception occurred. This proves one
fully balanced same-thread copy. It does not yet authorize a retained or
repeated default-pool resource across `Reset`, OpenXR submission, pixel-content
claims, or cross-thread D3D calls.

`BFVRLoader --surface-stream-probe` extends this to a bounded 60-copy sequence
that retains only the BFVR-owned destination between exact Projection calls.
The automated local-host run completed 60/60 `CopyRects` calls on device thread
`22336`; one owned target was created and released to 0, and every temporary
game-target reference released to 1. All creation, descriptor, and copy calls
returned `S_OK`, both descriptors matched, `resetSafeAtReturn=1`, and no
exception occurred.

`BFVRLoader --surface-readback-probe` is a separate one-shot transfer check. At
the exact Projection boundary it created a transient `CreateImageSurface`
system-memory destination, copied the 1920x1080 format-22 target, locked five
pixels, and released both the destination and temporary game target. All calls
returned `S_OK`, lock pitch was 7680, and releases were balanced. The five
samples were the same non-zero clear color, so this proves D3D8 readback
mechanics only: the Projection hook is before the normal world draw sequence,
not yet a scene-content capture point.

`BFVRLoader --surface-scene-readback-probe` fixes only that timing: the exact
Projection call borrows the device pointer until the following original
`EndScene` returns on the same thread, then performs the same transient image
copy/lock/release sequence. The resulting five samples differed and every D3D8
call returned `S_OK`; the image released to 0 and the game target to 1. This is
non-uniform post-scene content evidence, not a saved screenshot, map identity,
UI classification, OpenXR submission, or stereo output.

The corresponding directly launched client remained alive until the requested
100-second diagnostic timeout, at which point the loader explicitly closed it;
there was no repeat of the earlier early `0xFFFFFFFF` status in that control.

## Completed one-frame graphics-API inventory and state census

`BFVRLoader --d3d8-call-inventory-probe --diagnostic-timeout-ms 180000` is the
first diagnostic for the revised graphics-API stereo route. It is deliberately
separate from the rejected D3D8-object proxy and from the resource-copy probes.
After the existing lifecycle observation has completed, it reads the live
device's known D3D8 method entries and uses MinHook only when every target
resolves directly to system `d3d8.dll`.

The camera candidate was not a reliable short-session spawn boundary. The
probe instead reads the already-confirmed local BFPlayer from player-manager
`+0x54`, then requires eight consecutive non-zero reads of its confirmed
`+0xa9 isAlive` byte (two seconds at the existing 4 Hz cadence). It then
brackets exactly one `Present`-to-`Present` window on the verified device
thread. Its post-lifecycle wait is bounded to 45 seconds. It
forwards `Clear`, `SetRenderTarget`, `SetTransform`, `DrawPrimitive`,
`DrawIndexedPrimitive`, both `*UP` draw variants, `BeginScene`, `EndScene`,
and `Present` to their original trampolines before recording any data. For each
render-target change it makes only a post-forward `IDirect3DSurface8::GetDesc`
read to classify full-size color, smaller render-to-texture, and other targets.
At the opening Present it also uses the previously proven same-thread
`GetRenderTarget`/`GetDesc`/balanced-`Release` transaction once, which seeds
the target before the frame's first draw. It creates, copies, locks, redirects,
and presents no surface, and creates no OpenXR object.

The final log will call full-size draws preceded by both View and Projection
submissions a *world candidate*, smaller color targets a *render-to-texture
candidate*, and remaining full-size draws a *UI/overlay candidate*. These are
call-structure labels rather than image-content proof. The initial 2026-07-24
run proved the hook ABI and bounded recorder without a crash: it recorded 684
events over 4.674 ms, including 483 draws, three BeginScene/EndScene/Clear
groups, a 128x128 format-21 render-to-texture target, and a 1920x1080 format-22
full-size target. It started before the passive camera sampler's first non-zero
transform, however, so it is frontend/render-path evidence rather than an
active-map classification. A second run armed on one non-zero transition but
the next passive sample was zero again; it also cannot establish an active-map
classification. The implementation has passed the Win32 compile, the isolated
stereo-math regression, and loader dry run. The camera gate was then replaced
by the independently evidenced local-isAlive gate, which completed the active
spawned-frame validation below.

The first sustained-camera inventory recorded 648 events over 6.132 ms, with
429 draws, 16 draws after a 128x128 render-to-texture target, and 198 full-size
View/Projection candidates. It left 215 earlier draws target-unknown because
the first target binding predated the Present-to-Present window. The current
build seeds that existing binding through the separately established balanced
post-Present descriptor transaction. Its first 90-second validation attempt
failed closed before that transaction: the candidate transform did not remain
non-zero for two seconds, so the probe made no D3D call. The camera-side
candidate alone is therefore not a reliable short-session active-map gate;
the replacement is the already passive local-player path: manager `+0x54`
returns the confirmed local BFPlayer, and its confirmed `+0xa9 isAlive` byte
can be debounced without calling a game method. The resulting 90-second run
observed `isAlive` change from `0` to `1`, debounced it for eight samples, and
captured the next frame: 636 events in 8.804 ms, including a seeded initial
1920x1080 format-22 target, 437 draws (362 indexed, 58 UP), 132 World, 7 View,
and 31 Projection changes. All 437 draws were classified structurally: 430
full-size View/Projection world candidates and 7 128x128 format-21
render-to-texture draws; no UI/overlay or unknown-target draw occurred in this
frame. This completes the inventory, but remains call-structure—not
image-content—classification.

`BFVRLoader --d3d8-state-census-probe --diagnostic-timeout-ms 90000` is a
separate one-frame follow-up, not a second instance of the draw inventory.
Its hooks live in `client/D3D8StateCensus.cpp` and share only the already
observed lifecycle, local-`isAlive` gate, and logging bridge from
`client/D3D8ObserverBridge.h`. Each hook forwards the original system-D3D8
setter first, then records only that HRESULT plus the original scalar values,
opaque argument pointer, and count. It does not query state, dereference a
game resource, create a resource, change a D3D8 state, duplicate a draw, or
create an OpenXR object; it removes every forwarding hook after completion.

The 2026-07-24 spawned-frame run completed in 7.178 ms with 2,072 records and
no overflow. Every forwarded call returned `S_OK`: 3 `SetViewport`, 289
`SetRenderState` calls across 21 state IDs, 356 `SetTexture` calls on stages 0
and 1, 630 `SetTextureStageState` calls across 35 stage/type pairs, 30
`SetVertexShader`/FVF calls spanning 7 values, 8 vertex-constant writes, 410
stream-source binds, 344 index-buffer binds, 2 pixel-shader selections, and
no pixel-shader-constant write. The eight vertex-constant calls used only
register/count/pointer metadata (register 0 count 9; register 9 counts 15,
27, and 54); their data was not read. This is the bounded per-frame state
write inventory needed to design a restoration contract, not restoration,
mirroring, or stereo proof.

The one-frame inventory and census now signal a loader-created private
completion event only after their own bounded cleanup. This is an internal
process handshake, not a user input or chat mechanism. The loader uses the
diagnostic timeout only as a fail-safe, then closes the directly launched game
as soon as the event arrives. A live validation captured a second all-`S_OK`
state census (1,358 records over 5.865 ms) and ended the full session in
26.186 seconds, rather than waiting through the 45-second fail-safe.

`BFVRLoader --surface-d3d11-upload-probe` is the next, still opt-in bridge
step. It reuses that exact post-`EndScene` transient readback only when the
source is the observed format-22 (`D3DFMT_X8R8G8B8`) target. In the same device
callback BFVR creates a separate hardware D3D11 device, a BFVR-owned
`DXGI_FORMAT_B8G8R8X8_UNORM` default texture, and a staging texture; it uploads
the locked rows once, maps the staging texture, compares the same five pixels,
then releases every D3D11 object before unlocking/releasing the D3D8 surfaces.
The Win32 client/loader build and the loader's non-launching dry run passed.
In the next user-assisted map run, the exact post-`EndScene` transaction on
device thread `8472` created a hardware feature-level-11_0 D3D11 device, both
textures, and completed staging `Map` with `S_OK`; all five uploaded pixels
exactly matched (`00E7E7DE`, `00DEDCD1`, `0000FE00`, `00645B48`, `00605B47`),
and its cleanup flag confirmed every BFVR-owned D3D8/D3D11 object was released.
The instrumentation does not record a map identifier. This creates no OpenXR
graphics binding, session, swapchain, composition layer, or headset output.
The directly launched process remained live through the loader's requested
150-second window; the loader explicitly closed that child at timeout. This is
a bounded no-early-exit control, not repeated-reset or general gameplay
stability evidence.

The stream installs a forwarding `Reset` hook that releases the owned default-
pool target before invoking the original and captures successful-reset
presentation parameters for later recreation. The separate
`--surface-reset-probe` held one target until a real post-hook Reset on device
thread `28220`, released it before the original Reset, observed `S_OK`, then
created a second matching target and copied successfully at the next exact
Projection boundary. Both owned targets released to 0, both temporary source
references released to 1, and no exception occurred. BF1942 then exited with
`0xFFFFFFFF` during the diagnostic window. That is BF1942's explicit
`exit(-1)` status, not a Windows exception code: its renderer message pump
sets an exit flag after `WM_QUIT` and the following loop helper exits with
`-1`; a critical-error dialog also uses that status. This run did not capture
the terminating stack, but the loader did not terminate the child and no
Application/WER/CrashDumps report exists. Repeated-reset/full-game stability
therefore remains a separate concern.

## Standalone OpenXR presentation boundary

`BFVROpenXRProbe --presentation --duration-ms 10000` now exercises the next
OpenXR boundary without loading or modifying BF1942. It first creates a
baseline instance solely to confirm an HMD system, then recreates the instance
with `XR_KHR_D3D11_enable`, queries
`xrGetD3D11GraphicsRequirementsKHR`, finds the exact DXGI adapter by the
returned LUID, and creates its D3D11 device only on that adapter. It does not
fall back to a default adapter, WARP, or a device previously created by the
D3D8 upload diagnostic.

When an HMD is available, the probe creates a primary-stereo session, a LOCAL
reference space, one runtime-format world swapchain per eye, and an independent
runtime-format Ref2 UI swapchain. It submits three BFVR-owned D3D11 test
textures: the two world images form an `XrCompositionLayerProjection`, while
the UI image forms an alpha-blended quad layer. `--ui-cylinder` requests
`XR_KHR_composition_layer_cylinder`; runtimes without that extension log an
explicit safe fallback to the quad layer. The boundary requires sources with
the exact dimensions, format, single-sample count, and array size of its
runtime-created swapchains. It intentionally does no scaling, no D3D8 access,
and no retention of game-owned resources.

The 2026-07-24 build passed, along with `BFVRStereoMathTests`. On this machine
the bounded presentation probe enumerated 23 extensions (including D3D11 and
cylinder layers) but the baseline instance returned `XR_ERROR_RUNTIME_UNAVAILABLE`.
It therefore created no D3D11 device, OpenXR session, swapchain, or layer and
exited flat. This is the physical-headset/runtime test gate: the next live run
must validate the reported adapter, READY/STOPPING transitions, acquired and
released images, and headset-visible projection with both UI-layer modes
before any BF1942 client integration is considered. The subsequent active-runtime
run exposed an external x86 compatibility blocker: both BFVROpenXRProbe and an
independently built Khronos OpenXR-SDK-Source 1.1.61 `hello_xr --graphics D3D11`
control crash before rendering in `RuntimeIPCServiceClient_32.dll` version
`205.0.167.543`, access violation `0xC0000005` at offset `0x20681`. The control
uses no BFVR or BF1942 resource, so presentation work must wait for a repaired
or upgraded Oculus PC runtime that can create a standard x86 D3D11 session.

The authorized replacement boundary retains the injected client as x86 and
moves only presentation into `BFVRPresenter`, an x64 companion. The same
OpenXR module selects `runtime/openxr/win64/openxr_loader.dll`. A versioned
pointer-free mapping publishes adapter, dimensions, format, state, sequence,
and three resource names. The x86 producer creates left-world, right-world,
and Ref2-UI textures with `SHARED_NTHANDLE | SHARED_KEYEDMUTEX`; the x64 side
opens them by name, copies each completed set into local textures, and releases
the producer before it enters any OpenXR frame wait.

The headset-free cross-bitness control first passed 193 synchronized 320x240
frames. The scale control then used three `1872x2016` format-87 resources,
matching the observed Quest 3 recommendation, and completed 322 transports in
five seconds. Both sides read exact first-copy pixels `FF151F3D`,
`FF192442`, and `9E1452EB`; both children exited zero and all producer
resources released. This proves transport and cleanup only. Synthetic x64
projection plus quad/cylinder submission remains the physical-headset gate
before any live D3D8 capture is connected.

For local-host investigation, do not give `BFVRLoader` the game's `+restart 1`
argument: it makes the initially injected process exit cleanly before D3D8
initialization, so a replacement process is outside this loader's ownership.
An earlier no-restart `Aberdeen / GPM_CQ / BF1942` attempt also exited before
`Direct3DCreate8`. The later owned-copy and bounded-stream runs reached the
active ordinary-world path only because the user loaded Battle of Britain
manually; they do not validate the Aberdeen candidate. The archive-backed
baseline and its full gameplay regression remain open in the matching profile.

A full forwarding `IDirect3D8` proxy was also tested and produced the same
access violation immediately after returning to the client. BF1942 therefore
requires the original D3D8 interface identity; BFVR will not use interface
replacement or forwarding proxies for this boundary.

This checkout also launches a short-lived second BF1942 process that loads the
root `dxgi.dll` ReShade 5.7.0 proxy. It is separate from the main observed D3D8
process and is excluded from BFVR's future runtime path; BFVR has not renamed,
replaced, or configured the user's ReShade files during these tests.

## One-shot RenderView single-eye transform experiment

`BFVRLoader --render-view-single-eye-probe --diagnostic-timeout-ms <ms>` is
the first deliberately active camera-transform test. Its controlled offline
map validation is complete, but it is not stereo. It hooks only the profiled `RenderView` setter at
`0x005B7E00`; all other calls are forwarded unchanged. The altered call must
have both the known ordinary-world return `0x004668D1` and the current active
`DAT_009AB868` object.

For that one call, BFVR copies the incoming row-vector 4x4, verifies it is
finite, and moves its translation along local right (row 0) by `-0.032` engine
units. It forwards this copied matrix through the original setter and verifies
the setter stored the same value at `RenderView +0x3C`. The next matching game
setter call is forwarded unchanged and must restore the original engine matrix
at the same field. Only then does BFVR remove the hook. The local game-world
scale has not been calibrated to a physical IPD.

The command requires a loader diagnostic timeout. If no matching call occurs,
the test ends after 30 seconds without changing a matrix. Once it has changed
one copied matrix, it remains narrowly detoured until a matching normal setter
restores it; the bounded child process prevents a stranded persistent session.
Success requires `state=3`, `adjustedStored=1`, `restorationStored=1`, and
zero MinHook cleanup statuses in the observer log. Any timeout, failed matrix
comparison, or missing record is not a stereo result. The experiment creates
no second configured view, render target, OpenXR session, swapchain, or
head-tracking path.

The first 55-second default-launch control was deliberately a no-map safety
check. It enabled the hook, observed no eligible call, and reported
`state=4`, `matchingCalls=0`, unreadable matrices, and zero cleanup statuses.
Therefore it changed no transform; this proves only fail-closed behavior when
the normal world-render seam is absent, not the map validation.

The subsequent user-assisted Battle of Britain map run passed the required
control result: two matching calls on `RenderView 0x26742C80`, `state=3`, both
stored-matrix flags set, and zero disable/remove/uninitialize statuses. The
first copied local-right transform moved translation from
`(1257.410, 115.311, 1292.850)` to `(1257.378, 115.311, 1292.850)`; the next
unmodified engine setter restored the original translation exactly. This
proves a one-frame, reversible camera offset only—not a second render view,
stereo schedule, headset pose, perceptual VR result, or gameplay stability.

## Configured-view startup probe

`BFVRLoader --configured-view-list-writer-probe` is a separate, one-shot
read-only startup diagnostic. The loader supplies the suspended primary-thread
ID to the observer. Before that thread resumes, the observer places an execute
breakpoint at `0x00456FF4`, immediately after `FUN_004568B0` publishes the
configured-view owner. At that boundary it reads the still-empty
`owner+0x228/+0x22C/+0x230` vector and changes the same DR0 slot to a four-byte
write watch on the end pointer at `owner+0x22C`.

Three earlier starts reproduced the first write at post-store `0x0083176E`.
The inserted value was handle `0xFF`; the captured return chain was
`0x0044D181` in the generic DWORD append wrapper, `0x0044D9FA` in the
configured-view builder, and `0x0044ED4D` in its startup caller. The latest
capture also read, without calling, the first three active registry methods;
static follow-up on that live vtable identified `+0x54`. The observer now logs
all four targets on its next run:

- `DAT_0095F8D4 + 0x1C` -> `0x0048FA10` (registration)
- `DAT_0095F8D4 + 0x20` -> `0x004067C0` (`ret 4`, not removal)
- `DAT_0095F8D4 + 0x24` -> `0x0040BD90` (handle-resolution thunk)
- `DAT_0095F8D4 + 0x54` -> `0x004884E0` (registered-object destroy)

Static follow-up shows that registration builds a
`MultiPlayerFreeCamera`/entry-point object graph before returning the handle;
it is not a safe RenderView-clone API. The native per-entry cleanup contract is
now identifiable without invoking it: resolve the handle through `+0x24`,
erase it from the owner `+0x224` DWORD vector through engine helper
`FUN_004F53A0`, then destroy the resolved object through `+0x54`. The builder's
inputs and a native owner `+0x168` active-index restoration operation remain
unbounded, so the observer does not call the builder, insert another handle,
or change game data. On every completed capture it restores DR0-DR3, DR6, and
DR7 to their prior values.

A full-corpus search found only `FUN_0044D7D0` directly invoking the
registration slot `DAT_0095F8D4+0x1C`; there is no separately recovered native
multi-view registration route to reuse. This does not exclude an undiscovered
indirect call, but it rules out a direct-builder shortcut.

The later paired DR0/DR1 version also watches owner `+0x168`. It observed the
normal initial value `-1`, the final one-handle vector
`[0x0516F598, 0x0516F59C)`, and the builder's `+0x168: -1 -> 0` store at
post-instruction `0x0044DA1A`; `0` is the appended slot index. This bounds the
natural startup selection rule, but does not reveal a native setter that could
restore the previous index after removal. BFVR still must not write that field
or create a second entry.

## Per-view replay boundary

The normal `FUN_00466CD0` loop invokes `FUN_004662C0` once for each registered
configuration, but the latter is not a simple view draw helper. Its static body
updates global camera/smoothing and renderer state, writes the configuration
record, and runs multiple stateful render/callback paths in addition to the
View/Projection handoff. BFVR must not call it again with the same configuration
as a shortcut to a second eye: that would not establish a single simulation
tick, isolated target state, or safe replay.

Static disassembly further divides the later scene submission. The observed
mode-0 `0x0062CC70`, mode-1 `0x0062CDD0`, and mode-2 `0x0062CB00` entries are
batch dispatchers. They manipulate a global render-mode field and consume
submitted item lists through `0x0062C930` and `0x0062B8D0`. The per-item router
then branches into code that submits D3D8 World-transform state and other
renderer operations. This is useful for a passive ownership/state probe, but
it is not permission to replay a batch: target binding, state restoration,
queue semantics, and side effects remain unproven.

## Scene-batch ownership probe

`BFVRLoader --scene-batch-probe --diagnostic-timeout-ms <ms>` is a bounded,
read-only follow-up for a local map. It waits for the known device thread to
have no active debug-register owner, then observes the mode-0 `0x0062CC70`,
mode-1 `0x0062CDD0`, and mode-2 `0x0062CB00` batch entries. At the first such
entry it also arms `0x0062B8D0`, so the single captured item route follows an
observed scene batch rather than an arbitrary earlier draw. The default probe
stops at one router entry and copies receiver/list/item metadata, stack data,
and current RenderView/renderer-transaction pointers. It does not chase a
type-query return: the captured batch's second router argument is zero, which
takes the `0x0062B2F0` fallback, and re-arming this hot path caused an
unacceptable game freeze during development. It invokes no game method, D3D
method, or OpenXR code and restores all pre-existing debug registers on
completion or timeout.

The implementation compiled in the Win32 Debug build, the independent stereo
math test passed, and the loader dry-run passed. Its direct local run also
completed with `state=2`, no structured exception, and the original debug
registers restored. All three modes shared receiver `0x00B0DB10` and active
`RenderView 0x26EE4EB0`; their main lists held 75, 7, and 7 items respectively
(with mode-0/mode-2 prepass lists of 7 and 3). The router's observed second
argument was zero, which sends that batch item to `0x0062B2F0`, not the nearby
virtual type-query branch. A separate passive trace did observe the primary
`0x0062AC50` path on the device thread, but that hit is not evidence that this
batch item selected it. This remains observation only, not a replay or stereo
result; the batch-owned fallback, target/state, and queue-restore contracts
are still unproven.

## One-draw owned-target stereo-pair proof

`BFVRLoader --d3d8-stereo-pair-probe --diagnostic-timeout-ms <ms>` is the
first active graphics-level stereo command. Its implementation is isolated in
`client/D3D8StereoPairProbe.cpp`; `BFVRClient.cpp` supplies only the existing
read-only lifecycle, sustained local-player `isAlive`, logging, and completion
callbacks. The module hooks only system-D3D8 `Present` and
`DrawIndexedPrimitive`.

After forwarding BF1942's original draw, the probe accepts a large indexed
candidate only when the current color/depth surfaces match the full
presentation size and the active View/Projection matrices are finite. It
creates independent transient left/right color and depth surfaces, clears
them, applies opposite diagnostic View translations and asymmetric Projection
centres, and calls the original draw once per eye. It then restores and
re-queries the original target, depth, viewport, View, and Projection before
copying either owned color to system memory. Empty or identical images are
discarded only after all state and resources for that attempt have been
restored and released.

The successful local run selected a 1,263-triangle, 1,141-vertex batch on
1920x1080 format-22 color and format-75 depth surfaces. The game, left, and
right draws returned `S_OK`. Left readback contained 104 changed pixels and
right contained 105; hashes `8BBBBF51B486B03B` and `D33AEC2923526CE0`
differed. All four restoration calls returned `S_OK`, and exact post-restore
queries matched target, depth, viewport, View, and Projection. Source
references released to 1; both owned colors, both owned depths, and both
readback images released to 0. Neither eye was presented to BF1942, both hooks
were removed, and the complete bounded launch ended in 23.1 seconds.

This proves one real, differing owned-target geometry pair and its
same-callback restoration contract. The offsets remain diagnostic and
uncalibrated. Full-frame draw classification, RTT/sky/particle/HUD policies,
headset pose, OpenXR swapchains, and stereo presentation remain separate work.

## Present-to-Present indexed stereo stream

`BFVRLoader --d3d8-stereo-frame-probe --diagnostic-timeout-ms <ms>` retains
the owned left/right color and depth targets for one bounded indexed frame.
The module hooks only system-D3D8 `Reset`, `Present`, and
`DrawIndexedPrimitive`. It forwards every game draw first, excludes targets
whose live descriptor does not match the full presentation color/depth pair,
and mirrors each eligible indexed draw with the current per-eye transforms.
After every mirror it restores and re-queries target, depth, viewport, View,
and Projection before releasing the temporary game references. The next
Present reads back and releases the eyes; Reset releases them before forwarding
if it occurs first.

The final local run mirrored the complete observed indexed stream: 437
full-size draws containing 63,843 primitives. It excluded 53
non-presentation-target draws and skipped none at the 2,048-draw safety bound.
All 438 restoration checks passed exactly. All 927 acquired game-surface
references released without failure; both owned colors, both owned depths, and
both readback images released to 0.

Both 1920x1080 eye images contained 2,073,600 non-clear pixels and differed:
left hash `DA9A80D56A64B764`, right hash `D831CCE90C1612C8`. Neither eye was
presented to BF1942. The probe removed all three hooks and the complete launch
ended in 23.5 seconds.

This is the complete indexed geometry stream, not the complete D3D8 frame.
`DrawPrimitive`, `DrawPrimitiveUP`, and `DrawIndexedPrimitiveUP` are not yet
mirrored, and the final sky/particle/HUD/crosshair policies remain unresolved.

## Full draw-family stereo frame

The same `--d3d8-stereo-frame-probe` request now hooks all four D3D8 draw
families in full-frame mode. `DrawPrimitive`, `DrawIndexedPrimitive`,
`DrawPrimitiveUP`, and `DrawIndexedPrimitiveUP` all forward BF1942 first and
then use one shared eligible-target, eye-transform, restoration, and reference
accounting path. UP vertex/index pointers are replayed immediately inside their
callback and are never retained. The frame remains bounded to 4,096 mirrored
draws, and one-draw mode remains indexed-only.

The first local all-family run mirrored 186 draws / 28,063 primitives: 1
`DrawPrimitive`, 131 `DrawIndexedPrimitive`, 54 `DrawPrimitiveUP`, and 0
`DrawIndexedPrimitiveUP` in that sampled frame. It had no target exclusion or
bounded skip. All 187 target/depth/viewport/View/Projection restoration checks
were exact, and all 372 borrowed game-surface references released without
failure.

Both 1920x1080 eyes contained 2,073,600 non-clear pixels and differed: left hash
`FB68E904B69883D0`, right hash `8C736237A73A7F9A`. Both owned colors, both
depths, and both readbacks released to 0. Neither eye was presented to BF1942;
the completion event ended the entire launched session in about 23.5 seconds.

This covers every draw family active in the sampled frame. The installed
`DrawIndexedPrimitiveUP` path still needs a live-hit sample, and semantic
sky/particle/HUD/crosshair policy remains unresolved.

## Transform-gated stereo and monoscopic draws

`stereo/D3D8DrawPolicy.cpp` is a small pure classifier with deterministic unit
tests. Perspective, non-pretransformed work receives the diagnostic stereo
View/Projection pair. Fixed-function `XYZRHW` and non-perspective work is drawn
into both owned eyes with BF1942's exact View/Projection, preventing artificial
eye offsets on explicit screen-space geometry. Vertex-shader handles are
excluded before checking the FVF position mask.

The frame probe reads `GetVertexShader` and Projection after the successful game
draw and retains no shader/FVF or caller buffer. The local validation accounted
for all 266 mirrored draws / 43,871 primitives as 216 stereo-perspective, 3
monoscopic-pretransformed, and 47 monoscopic-non-perspective. There were zero
shader/FVF read failures, target exclusions, or bounded skips.

All 267 restoration checks were exact and all 532 borrowed surface references
released correctly. Both 1920x1080 eyes were fully populated and differed:
left hash `91C94BE7D517CCC2`, right hash `D38FA560BA1133C0`. Every owned/readback
surface released to 0, and the completion event closed the launched session.

At this stage this was transform policy, not semantic HUD recognition. The
later provenance work below resolves the shared Ref2 menu-quad family. Those
quads are still duplicated into the eye images rather than composed on a
separate VR layer.

## Bounded draw provenance and skybox fingerprint

The full-frame probe now aggregates at most 64 unique draw provenance records:
the BF1942 executable return stack, draw ABI, transform class, FVF/shader, five
render states, draw count, and primitive count. A spawned frame recorded 19
records for 483 mirrored draws / 63,119 primitives with no overflow or state
read failure. All 484 restoration checks and 971 borrowed-reference releases
passed; both full eye targets differed.

One perspective `DrawPrimitive` return, `0x0064D84C`, lies inside the function
whose own error text identifies a billboard vertex buffer. Those 12 calls are
confirmed billboard batching and remain stereo effect geometry.

One unique `DrawIndexedPrimitive` record contains six draws / 12 triangles,
FVF `0x112`, and disabled depth, blending, fog, and lighting. It returns at the
generic mesh-loop site `0x0062B83F`, so the caller address alone is insufficient.
Static code independently initializes a literal `SkyBox` with six named faces:
`up`, `down`, `front`, `back`, `left`, and `right`. Together with BF1942's
textured-cube sky design, this is a high-confidence, deliberately narrow
skybox-face signature. The next proof applies no diagnostic eye translation to
only that exact conjunction and must observe exactly six matches in another
spawned frame.

The captured non-perspective FVF `0x142` returns were initially retained as
unresolved renderer batching helpers. Cross-build correlation later resolves
them exactly as the shared Ref2 menu-quad family described below.

The rule now lives in the small pure
`stereo/D3D8SemanticDrawPolicy.cpp` module and requires every observed field;
unit-tested near misses remain unclassified. A second spawned frame reported
exactly six `skybox-cube-face` draws / 12 triangles and no false positive among
174 mirrored draws / 38,715 primitives. All 175 restoration checks and 348
borrowed-reference releases passed. Both full 1920x1080 eyes differed
(`F9467C9F19688E26` / `62B1377077B031E5`) and every owned resource released to
zero. For this no-HMD diagnostic pair, matched faces use BF1942's exact source
View/Projection in both eyes, eliminating artificial sky translation. Real
head-rotation-only sky rendering remains part of OpenXR integration.

The same frame exposed a classification mistake. Its FVF `0x144`
`DrawPrimitive` return `0x0065D140` was initially attributed to weather based
on nearby `WPart.*` strings. Raw executable disassembly still correctly fixes
the decompiler boundary: `0x0065CD90` is a destructor, while vtable
`0x009190AC` slot `+0x10` points to the actual method at `0x0065CE10`. But the
semantic Mac corpus names the exact matching algorithm
`dice::ref2::fx::NewRendFont::draw(float,float,string const&)`. Both
implementations allocate by string length, special-case space, consult 256
glyph metrics, and emit six 28-byte `XYZRHW` vertices per non-space character.

A bounded live entry trace confirmed the correction: all retained calls came
from `0x0044179F`, carried literal pixel coordinates, and passed string lengths
12-19. Caller disassembly pushes the string and two converted floats before
calling vtable `+0x10`. The earlier three-draw / 102-primitive validation was
therefore Ref2 font output, not weather output. `Ref2FontGlyphBatch` is now an
exact fail-closed semantic class and is routed to the transparent UI layer.
Community modding references describe stock BF1942 weather as dormant/unused,
so it is not being pursued without direct live evidence.

## Confirmed Ref2 menu/HUD quad family

The original Battlefield MDT HUD documentation says that HUD elements are 2D
images overlaid on the 3D scene, with content under `Menu.rfa/menu/Texture`.
It separates in-game HUD, minimap, radio, soldier, vehicle, weapon, ammo,
scope/binocular, text, and spawn/menu content. That content taxonomy matches a
concrete Mac `dice::bf::menu::HudManager` composed of separate ammo, soldier,
vehicle, crosshair, radio, tooltip, spawn, briefing, scoreboard, and related
components.

The same semantic Mac corpus names the common renderer routines. WinPC
`FUN_00664C50`, `FUN_00664560`, `FUN_00665320`, and `FUN_00664DB0` match
`dice::ref2::menu::QuadDrawer::flush`, `QuadDrawer::setStates`,
`QuadDrawer::addQuad`, and `QuadOutputCache::render`. The matches cover the
exact flush diagnostic, `+0x90008` cached state, texture/blend/depth state
transaction, half-texel adjustment, four-plane clipping, buffered triangle
list, immediate triangle fan, cached index traversal, and restore-state set.
Their captured WinPC D3D8 returns are exactly `0x00664CF6`, `0x00666018`, and
`0x00665098`.

The semantic policy therefore labels only the complete profiled signatures as
`ref2-menu-quad`, including the exact nested-wrapper stack
`0x00667DFD -> 0x007EBFF6 -> 0x00664CF6`. It deliberately does not call every
quad "gameplay HUD": crosshair, scope, ammo, minimap, spawn UI, text, and menus
share the same low-level renderer. That distinction requires higher-level
`HudManager` or Meme state.

The final spawned validation classified all 65 monoscopic-non-perspective
draws as `ref2-menu-quad`, while preserving exactly six skybox faces and two
now-confirmed `ref2-font-glyph-batch` draws as separate classes. The complete
534-draw / 63,301-primitive
frame passed 535 exact state restorations and 1,080 balanced borrowed
references with zero read or provenance overflow failure. The complete eye
hashes differed (`450F53AEBBCB0E3D` / `E84F6BA03BF05337`), every resource
released, and the loader closed only its launched game. A dedicated
configurable OpenXR HUD/menu composition path remains the next presentation
step.

## Separate transparent Ref2 UI-layer extraction

The probe now uses a small pure `D3D8FrameCompositionPolicy` module to route
confirmed `ref2-menu-quad` and `ref2-font-glyph-batch` draws away from both
world-eye replays. It renders those draws once, with the game's exact
View/Projection, into a BFVR-owned full-size `A8R8G8B8` target cleared to
transparent. World, skybox, and billboard classes remain in the left/right
world targets.

The 2026-07-24 live validation (PID 2436) finished in 22.7 seconds and
partitioned every eligible draw: 167 world-eye draws plus 53 UI-layer draws
equaled all 220 draws. The semantic totals were exactly 53 menu quads, six
skybox faces, and two draws now known to be font batches. The initial run
routed only the 53 menu quads; a follow-up must validate the corrected policy
with both UI classes. Both 1920x1080 world targets were complete
and differed (`04A9D78C14DB56F9` / `30AB4B0C95207F59`). The transparent menu
surface was independently readable and nonempty: 198,989 non-clear pixels,
279,982 nonzero-alpha pixels, and hash `CDD4A97EF19AE00E`. All 221 restoration
checks were exact, all 461 borrowed references balanced, and every target
released.

The corrected two-class UI policy was then live-validated on PID 10748 in
24.4 seconds. All 386 mirrored draws partitioned exactly into 315 world-eye
draws and 71 UI-layer draws, with `71 = 66 ref2-menu-quad + 5
ref2-font-glyph-batch`; those five font batches contained 138 primitives.
Both complete world targets differed (`635E56BE21472DAA` /
`9D83317A937CEF48`). The transparent UI readback was nonempty (hash
`C8464B62DF9F7EAA`, 198,850 non-clear pixels, 279,839 nonzero-alpha pixels).
All 387 restoration checks and 789 borrowed-reference releases passed, no draw
hit the safety bound, and all owned/readback surfaces released to zero. The
temporary font-entry observation hook was not present in this run.

This proves a usable extraction boundary. The separate x64 companion first
completed the synthetic presentation gate: through Oculus Link it created the
exact runtime-adapter D3D11 session and three 1872x2016 swapchains, transported
distinct left/right world textures plus a transparent UI texture from an x86
producer, and submitted them as a projection layer plus either a quad or
cylinder layer. The user saw the different per-eye world colors and blue panel
in both modes, and explicitly confirmed that the cylinder panel appeared
curved. The corrected shutdown path requested exit, observed STOPPING, ended
the session without error, exited both processes zero, and released all
producer resources.

The real BF1942 handoff passed next. Opt-in request 19 launched the owned x64
presenter, received one runtime-timed view request, rendered two native
1872x2016 D3D8 world targets and one logical 1920x1080 transparent Ref2 target,
and completed the publish/consume/present acknowledgement. PID 23960 captured
344 eligible draws: 280 world-eye draws and 64 UI draws, with the UI split
exactly into 60 Ref2 menu quads and four font batches. All 345 restoration
checks and 694 borrowed-reference releases passed; both complete world hashes
differed, and the UI readback contained nonzero alpha. The x64 presenter
reached FOCUSED, consumed sequence 1, submitted projection plus quad layers,
then exited cleanly through STOPPING with `healthy=1`.

The user saw recognizable BF1942 world geometry in stereo after spawning and
saw the HUD remain separate from head motion. Because request 19 intentionally
holds one bounded frame and does not yet compose full HMD orientation or
translation, game input and firing continued audibly without changing the
visible VR image; lateral head motion exposed doubled world edges. The
first-person gun and arms also had incorrect inter-eye alignment. These are
the next presentation boundaries: continuous pose-timed rendering and a
separate evidence-backed near-field weapon/view-model policy. UI
classification still preserves the distinction between low-level menu-quad
rendering and higher-level gameplay HUD, scope, spawn, and menu state.

The next 15-second continuous run completed 168 exact timed sequences
(`11.2 FPS`). Across the run, 58,594 draws partitioned into 47,926 world and
10,668 UI draws, all 58,762 restorations passed, no frame failed, and the x64
presenter independently reported sequence/frame 168 before clean STOPPING
shutdown. The user could look and move around, confirming continuous
pose-responsive output rather than a held frame.

That run is not a visual-quality pass. The user reported very poor frame rate,
invisible terrain and other elements, and other soldiers whose main bodies
appeared partly sunk while their hands and helmets remained at expected
locations. The 15-second window was also too short for a thorough inventory.
The leading unconfirmed explanation for missing geometry is that head rotation
is currently added at the D3D8 draw-replay boundary after BF1942 has already
culled scene objects for its original renderer camera. The next experiment
must correlate the same pose at the proven renderer-camera transformation
boundary and avoid applying it twice. Per-stage timing must also separate
extra-eye rendering cost from the synchronous three-surface readback and
cross-process upload cost. Future visual-quality windows should run for at
least 60 seconds.

The first 60-second window using that boundary ran on 2026-07-25. PID 3812
completed 1,298 runtime-timed frames with no failed frame, 451,185 exact D3D8
restorations, and a clean x64 STOPPING shutdown. The new exact
`RenderView::setTransformation` hook matched/applied 1,300 centre-head poses
from the confirmed caller with no rejection; D3D8 used only the residual eye
poses/FOV afterward. Mean time per frame was 4.287 ms replay, 22.080 ms
readback, 3.465 ms upload, 0.005 ms presentation acknowledgement, and 8.411
ms next-request wait. This validates the camera boundary and identifies
synchronous readback as the largest BFVR stage, but the user's visual report
is still required to determine whether culling/geometry actually improved.

The user confirmed that terrain and ordinary world geometry did render
properly in this run; the HUD and gun also appeared good. This supports the
renderer-camera boundary as the fix for broad post-cull world coverage.
Remaining visual defects are distinct: cadence improved but remained poor with
apparent reprojection smearing; first-person hands were duplicated, detached
from the gun, and camera-dependent; soldiers still looked stuck in the ground;
and some tree billboards and flag-cloth meshes were doubled/mispositioned.
Treat those as separate draw-family/transform investigations rather than
regressing the renderer-camera pose path.

## Build and first test

Configure the native project for `Win32` with Visual Studio's CMake and stage
both outputs into `BFVR` only for a local observation run. The loader supports
`--dry-run` for path validation. Do not use the observer on public or
anti-cheat-protected multiplayer servers.

```powershell
cmake -S .\BFVR\src -B .\build\bfvr-observer -G 'Visual Studio 17 2022' -A Win32
cmake --build .\build\bfvr-observer --config Debug
```

The current source has compiled successfully with the local MSVC Win32 toolset.
It vendors the official MinHook v1.3.4 source under `BFVR/third_party` for the
explicit bridge probe; MinHook is statically linked into the test client and
does not add a game-root DLL or DirectX wrapper. The BF1942 client requests
Windows elevation. For a bounded offline test, pass
`--diagnostic-timeout-ms 90000`; it closes the directly launched process after
that 90-second window. The observer now records its first post-Present `Reset`
entry automatically; alt-tabbing out of BF1942 and returning after the first
frame is visible remains a useful additional lifecycle exercise. The loader
accepts values from 1 second through 5 minutes. The current bootstrap
investigation shows that a future diagnostic must also track the short-lived
second BF1942 process.

The future runtime will retain the same dedicated-folder model, but no release
package or installer is part of this prototype.
