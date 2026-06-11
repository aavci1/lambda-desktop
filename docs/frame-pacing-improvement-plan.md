# Frame Pacing Improvement Plan

**Status:** FP-1 … FP-16 code landed (`50d65831..HEAD`); post-implementation code review complete (2026-06-11). This document now tracks **open verification gaps** (manual, validation-layer, hardware input) and the **post-implementation review backlog** (REV-*). Tracked as TODO-019 in [TODO.md](../TODO.md).
**Scope:** `apps/lambda-window-manager/Compositor/`, `src/Platform/Linux/` (KMS + Wayland), `src/Graphics/Vulkan/`, `src/Graphics/Metal/`, `src/Platform/Mac/`, `src/SceneGraph/`, and `src/UI/Application.*`.
**Source:** Original frame-pacing audit at `ca4466cd`; implementation review of `50d65831..HEAD` with citation verification against current sources. Severity reflects expected impact on smoothness, pacing, and correctness.

Line numbers in the FP-* sections below describe the **original audit context** and are often stale after the implementation pass. Use the **Citation errata** table and the **REV-*** anchors for current file:line references. Delete each REV item as it is fixed; delete this document and TODO-019 when all REV items and verification gaps are closed.

## Verification Snapshot: 2026-06-11

- [x] Clean normal and KMS rebuilds completed with zero warnings and zero errors.
- [x] Focused normal tests passed: Vulkan image-recorder replay test (1 case, 12 assertions), compositor/Vulkan/presentation/damage/cursor/pointer slice (88 cases, 602 assertions), and reactive tests (22 cases, 126 assertions).
- [x] Focused KMS tests passed: Vulkan image-recorder replay test (1 case, 12 assertions), compositor/Vulkan/presentation/damage/cursor/pointer slice (89 cases, 606 assertions), and reactive tests (22 cases, 126 assertions).
- [x] KMS compositor runtime smoke completed with normal-build `lambda-shell`, scripted `lambda-terminal`, and `lambda-editor`; logs had zero fatal/error matches.
- [x] Runtime traces captured 20 CPU samples, 37 KMS timing windows, 10,712 pacing events, and 900 terminal `vulkan-present-detail` samples. Summary: compositor CPU avg/max 12.54%/15.60%, compositor surface avg/max 0.471/0.632 ms, present avg/max 0.395/0.582 ms, terminal atlas avg 0.004 ms, terminal `waitImage` avg 0.000 ms.
- [x] Added `scripts/verify-frame-pacing-linux.sh` and `lambda-presentation-feedback-check`. Latest run passed both presenters: atomic-KMS reported `CLOCK_MONOTONIC`, refresh 16,666,666 ns, nonzero sequence, and `VSYNC|HW_CLOCK|HW_COMPLETION`; Vulkan-display reported `CLOCK_MONOTONIC`, refresh 16,666,666 ns, and nonzero sequence. Atomic runtime summary: 20 CPU samples, 31 KMS timing windows, 9,914 pacing events, 826 terminal `vulkan-present-detail` samples, zero fatal matches, CPU avg/max 12.38%/16.50%, surface avg/max 0.359/0.552 ms, present avg/max 0.506/1.069 ms, terminal `waitImage` avg 0.000 ms, terminal atlas avg 0.008 ms.
- [x] Added synthetic KMS raw-input pointer-motion verification to `scripts/verify-frame-pacing-linux.sh`. Latest run: 180 synthetic pointer events, 180 hardware-cursor fast-path moves, zero fallback/unavailable moves, zero runtime failures, no pointer-triggered full redraw loops, and sampled CPU trace output without a new compositor coredump; atomic app smoke and Vulkan-display timestamp checks also passed.
- [x] Added static decorated-SHM surface cache verification to `scripts/verify-frame-pacing-linux.sh`. Latest run: 500 surface draw-cache hits, 1 miss, zero transient-chrome blocks, 524 rendered frames, CPU avg/max 9.13%/11.10%, surface avg/max 0.016/0.017 ms, present avg/max 0.258/0.264 ms.
- [x] Added server-side chrome hover/press verification to `scripts/verify-frame-pacing-linux.sh`. Latest run: synthetic close-button move, press, move-away, release, and completion events all observed; surface draw-cache reported 4 hits, 1 miss, zero transient-chrome blocks, CPU avg/max 1.00%/2.20%, and surface avg/max 0.315/0.811 ms.
- [x] Added scripted resize-storm verification to `scripts/verify-frame-pacing-linux.sh`. Latest run: 18 resize/configure events, 522 sizing cache-block samples, 558 rendered frames, zero fatal matches, CPU avg/max 15.47%/16.70%, surface avg/max 1.117/1.299 ms, and present avg/max 0.433/0.446 ms.
- [x] Added ASan capture-heavy verification for Vulkan recorder/render-target tests. A fresh `build-asan` now builds `lambda_tests` with the generated Wayland server protocol headers ordered correctly, and the ASan slice passed 22 cases/170 assertions including recorded image replay after the recording canvas is destroyed.
- [x] Perf-wrapped full Linux verifier passed on 2026-06-11 (`.debug-logs/frame-pacing-verify/20260611-102353`): atomic, pointer-fast-path, surface-cache, chrome hover/press, resize-storm, and Vulkan-display cases all passed with zero fatal matches. `perf stat` captured 26,854.53 ms task-clock, 340,670 page faults, 30,434,613,748 cycles, and 64,869,277,919 instructions for the verifier script.
- [x] Standard external compositor smoke partially passed: Weston 15.0.1 headless with kiosk shell/fake seat ran `weston-smoke` until a controlled 8-second timeout with no Weston runtime errors. The Lambda presentation-feedback checker also connected far enough to report that Weston headless advertises `CLOCK_MONOTONIC_RAW`, so it remains unsuitable for validating Lambda's stricter `CLOCK_MONOTONIC` presentation contract.
- [ ] Remaining validation-layer gap: `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` Vulkan render tests still pass, but `VK_LOADER_DEBUG=layer` reports `Layer "VK_LAYER_KHRONOS_validation" was not found`; `vulkan-validation-layers` and `vulkan-tools` are still not installed.
- [ ] Remaining local input gap: `ydotool` and `wtype` are installed and `/dev/input/event0` has an ACL for `aavci`, but `ydotoold` cannot start because `/dev/uinput` is still `root:root` mode `0600`; `evemu-event` is still missing. Real hardware input-driver and manual cursor visual validation still require a prepared host.
- [ ] Remaining environment gap: broad real-app visual smoke cases still require manual interaction with a running Lambda compositor session, especially move/drag/resize and cursor visual checks.
- [ ] Remaining system-tool gap: `wayland-utils` and `evemu` are not installed; `aavci` is in `wheel`, but noninteractive `sudo` still prompts for a password, so this session cannot repair `/dev/uinput` permissions or install the remaining packages.
- [x] macOS compile verification: `lambda_tests` built cleanly including `MetalCanvasTests.mm` (2026-06-11).
- [x] macOS focused tests: `*Metal*,*SceneGraph*` — 20 cases, 133 assertions, all passed (2026-06-11).
- [ ] Remaining macOS runtime gap: `debug::perf` counters (`CanvasDrawableWait`, atlas-grow hitch), full `ctest`, and backdrop-blur visual comparison.
- [ ] Post-implementation review backlog (REV-*) below — 3 high, 9 medium, 11 low/nit items remain open.

## Working Environment

```sh
# Wayland client path
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLAMBDA_PLATFORM=LINUX_WAYLAND -DLAMBDA_BUILD_TESTS=ON
# Compositor / KMS path (run from a TTY; see docs/compositor-user-guide.md)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLAMBDA_PLATFORM=LINUX_KMS -DLAMBDA_BUILD_TESTS=ON

cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Use an ASan build for the resource-lifetime items (FP-10, FP-11, FP-12, FP-13):

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DLAMBDA_ENABLE_ASAN=ON -DLAMBDA_BUILD_TESTS=ON
```

Measurement tooling that already exists — use it before and after every change:

| Tool | How | What it shows |
| --- | --- | --- |
| Vulkan present phases | `LAMBDA_RESIZE_TRACE=1`, `vulkan-present-detail` trace (`VulkanCanvasLifecycle.inc:633-660`) | Per-phase ms: frame fence, acquire, atlas, backdrop prep, upload, record, submit, present |
| Compositor frame profile CSV | `SnapAnimationTrace::recordFrame` in `CompositorRuntime.cpp:760-838` (header at `747-754`) | input→render, render-ahead, snapshot/surface/present phase ms |
| Flip interval error | `intervalErrorMs` trace at `CompositorRuntime.cpp:1883-1887` | Render-ahead lead accuracy vs real flips |
| Metal counters | `debug::perf` — `CanvasDrawableWait`, `CanvasPresent`, `DisplayLinkToPresent` | Drawable stalls vs present cost on macOS |
| Validation layers | `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` | Sync/lifetime regressions from any of these changes |

**Suggested fix order for open REV items:** REV-V1 → REV-K1 → REV-M1/M2/M3 (high-severity regressions), then medium REV-V2 … REV-W2, then lows/nits. Original FP workstreams (FP-1 … FP-16) are marked complete above; remaining manual verification checkboxes under each FP section still apply where unchecked.

---

# Phase 1 — Verified hot-path stalls (quick wins)

## FP-1: Record glyph-atlas uploads into the frame command buffer

**Severity: Critical (full GPU queue drain mid-present whenever text changes).** Verified in source.

`uploadAtlasIfNeeded()` runs inside `presentImpl()`. Any frame with a dirty atlas (new glyph, eviction, rebuild) goes through:

- `VulkanGlyphAtlas.inc:122-132` — `uploadAtlasIfNeeded()` → `uploadTexture(res.atlas, …)`
- `VulkanImages.inc:248-265` — `uploadTexture()` uses `beginImmediate()` / `endImmediate()`
- `VulkanImages.inc:309-318` — `endImmediate()` calls `vkQueueSubmit2` then **`vkQueueWaitIdle`**

That is a synchronous submit plus a full queue drain on the hot path, before the frame's own command buffer is recorded.

What to do:

- [x] [Auto] Add an atlas-upload path that records the staging-buffer copy and layout transitions into the **frame** command buffer, following the existing `recordPendingTextureUploads` pattern (`VulkanImages.inc:186-205`): stage pixels into a `Buffer`, queue a `PendingTextureUpload`-style job for the atlas texture, retire the staging buffer through `pendingBufferDestroys_` with `kMaxFramesInFlight + 1`.
- [x] [Auto] The atlas is sampled by draws in the same frame — record the copy before the main render pass begins (same place `recordPendingTextureUploads` runs) with a transfer→shader-read barrier.
- [x] [Auto] Same treatment for imported-image layout transitions: `transitionImmediate` in `ensureImageTexture` (`VulkanImages.inc:17` and `:320-324`) stalls the queue on first draw of an external image. Record the transition into the frame command buffer via a pre-pass barrier batch instead.
- [x] [Auto] After this, `endImmediate()` should have no callers on the per-frame path. Keep it for genuine one-off initialization, and add a comment stating it must not be called from `beginFrame`/`present`.

Verification on Linux:

- [x] `LAMBDA_RESIZE_TRACE=1` — the `atlasMs` phase in `vulkan-present-detail` should drop to ~0 on frames that add new glyphs (type rapidly in `lambda-terminal` / `lambda-editor` with a cold atlas).
- [x] `./build/tests/lambda_tests --test-case="*Vulkan*,Compositor*"` (lavapipe OK) — glyph text test (`Vulkan RenderTarget renders glyph atlas text`) still passes.
- [ ] Validation layers report no missing-barrier or layout errors while scrolling text-heavy content.

## FP-2: Composite partial-damage frames once, not once per damage rect

**Severity: Critical (N damage rects ≈ N× full scene draw recording).** Verified in source.

`CompositorRenderFrame.cpp:1024-1033`: when `partialDamageFrame` is true, the code loops `sceneDamage.rects` and calls the full `drawFrameContent()` (wallpaper + all surfaces + cursor + overlays) inside each clip. GPU work is bounded by the clip; CPU draw recording and scene traversal are multiplied by the rect count.

What to do:

- [x] [Auto] Replace the per-rect loop with a single pass: push all damage rects as one clip region (add a multi-rect clip helper on `Canvas` if needed — Vulkan scissor-list or a stencil/union-clip; a bounding-union `clipRect` is an acceptable first step when rect count is small) and call `drawFrameContent()` once.
- [x] [Auto] Merge overlapping/adjacent damage rects before use: both damage systems cap rect count with a cliff to full-output damage (`SceneDamage.cpp:51-53` at >64, `CompositorSceneGraph.cpp:173-175` at >96). Add a union/merge step (merge rects whose union area is within ~20% of the sum) so busy scenes degrade to fewer larger rects instead of full-screen damage.
- [x] [Auto] Keep the KMS `damageRects` passed to the presenter unchanged — only the compositing pass collapses; scanout damage stays per-rect.

Verification on Linux (KMS TTY):

- [ ] Frame profile CSV: `surface_ms` for partial frames with multiple dirty windows should drop roughly by the old rect count.
- [ ] Visual: drag two windows with animated content; no stale pixels outside damage, no flicker at rect seams.
- [x] `tests/` damage suites still pass (`computeSceneDamage` unit tests).

## FP-3: Remove the remaining queue-drain fallback and noisy recreate log

**Severity: Medium (catastrophic when hit; trivial fixes).**

- `KmsOutput.cpp:883-886` — `markFrameRendered()` falls back to `vkQueueWaitIdle` when `buffer.renderFinished == VK_NULL_HANDLE`. The async-fence path normally creates the semaphore, so this is rare — but it fully drains the GPU queue when hit.
- `VulkanSwapchain.inc:53-70` — unconditional `fprintf` to stderr on every swapchain recreate (every resize step).

What to do:

- [x] [Auto] Guarantee `renderFinished` exists by the time `prepareFrame()` returns (create it eagerly with the buffer); replace the `vkQueueWaitIdle` fallback with a logged error + per-buffer fence wait so a misconfigured driver degrades per-buffer instead of device-wide.
- [x] [Auto] Gate the swapchain-extent `fprintf` behind `detail::resizeTraceEnabled()` like the other resize traces.

Verification:

- [x] [Auto] Grep: no `vkQueueWaitIdle` outside `endImmediate` teardown comments; no unconditional stderr writes in the resize path.
- [ ] [Manual] Resize a Wayland window continuously — stderr stays quiet without `LAMBDA_RESIZE_TRACE`.

---

# Phase 2 — Input smoothness

## FP-4: Cursor-only atomic commits (no recomposition, no queue dependency)

**Severity: High (cursor lag under load; wasted full frames per pointer event).**

Three related problems:

- `KmsOutput.cpp:3636-3643` — `moveCursor()` on the atomic path only stores `cursorX_/cursorY_`; the cursor plane is applied by `addAtomicCursorProperties()` during the **next full commit**.
- `CompositorRuntime.cpp:797-806` + `2131-2149` — cursor-only dirtiness (`inputHardwareCursorFrameRequired`) is not part of `renderNeeded`, so when the present queue is blocked (flip pending, acquire fence unsignaled) cursor motion can wait multiple frames.
- Pointer motion that *is* consumed still forces a full `renderAtomicFrame()` even though the hardware cursor lives on its own DRM plane (full snapshot, surface draw, offscreen→scanout copy, schedule).

What to do:

- [x] [Auto] Add a cursor-only commit path in `KmsOutput`: an atomic request containing only cursor-plane properties (`CRTC_X/CRTC_Y`, FB if the image changed), `DRM_MODE_ATOMIC_NONBLOCK`, no page-flip event, retry on `EBUSY`/`EAGAIN` like the existing cursor commit. It must be schedulable while a primary-plane flip is pending.
- [x] [Auto] In the compositor loop, when the only dirtiness is hardware-cursor motion, call the cursor-only commit and **skip** `renderAtomicFrame()` entirely (keep the existing full-frame path for software cursor).
- [x] [Auto] Handle hardware-cursor motion in input dispatch so cursor updates are not starved behind a blocked present queue.

Verification on Linux (KMS TTY):

- [x] Frame profile/pacing trace: pointer-motion-only periods produce no compositor frames. `scripts/verify-frame-pacing-linux.sh` reported 180 synthetic raw-input pointer events, 180 hardware-cursor fast-path moves, zero fallback/unavailable moves, and no `input=1 render=1` redraw loops after the pointer stream began.
- [ ] Cursor stays responsive while a heavy client animates (GPU saturated) — previously multi-frame lag.
- [ ] No flicker or cursor tearing across primary-plane flips (cursor plane and primary commit interleave safely).

## FP-5: Stop chrome hover from busting the surface draw-op cache

**Severity: High (continuous re-recording of full surface draws during normal mouse use).**

- `Snapshots.cpp:152-158` recomputes `closeButtonHovered` from the pointer each snapshot.
- `SurfaceRenderer.cpp:125-132` — `hasTransientChromeState()` returns true for any hover/press state, which blocks `VulkanFrameRecorder` replay (`SurfaceRenderer.cpp:465-485`), so the whole surface (client content + chrome) is re-recorded every frame the pointer is near a titlebar.
- `CompositorRuntime.cpp:797-806` — pointer motion sets `inputRenderRequiredThisLoop` even when no visual state changed.

What to do:

- [x] [Auto] Split server-side decoration chrome into its own recorded layer (or own scene node) so the client-content op cache stays valid across hover changes; only the titlebar layer re-records on hover/press transitions.
- [x] [Auto] Emit damage only for the titlebar bounds when hover state changes; pointer motion that changes no hover state and moves only the hardware cursor must not set `inputRenderRequired` (pairs with FP-4).
- [x] [Auto] Add a diagnostics counter delta check: `recordSurfaceDrawCacheBlock(…TransientChrome)` should stop incrementing during plain pointer sweeps over a window.

Verification on Linux (KMS TTY):

- [x] Automated static decorated-SHM client check keeps the surface draw cache hot while the compositor drives window movement: 500 hits, 1 miss, zero transient-chrome cache blocks, and surface avg/max 0.011/0.012 ms.
- [x] Automated server-side chrome hover/press check drives KMS pointer position and button events over the close control without closing the window: move, press, move-away, release, and complete events observed; cache stayed hot with 4 hits, 1 miss, and zero transient-chrome cache blocks.
- [ ] Sweep the pointer across a window with static content: surface draw cache hit-rate (diagnostics) stays high; `surface_ms` in the CSV stays near the idle baseline.
- [ ] Hover/press visuals on close buttons still update correctly (manual).

## FP-6: Pace popover rendering through the frame scheduler

**Severity: Medium (unthrottled renders from pointer handlers).**

`WaylandWindow.cpp:1628-1636` — `renderPopover()` does `beginFrame`/`render`/`present` synchronously from pointer handlers (call sites at 1647, 1666, …) with no `wl_surface_frame` gate. Bursts of pointer events produce unpaced presents on the popover surface.

What to do:

- [x] [Auto] Route popover redraws through `requestAnimationFrame()` / the shared redraw coalescing used by the main surface (a per-popover `redrawRequested` flag drained by the same frame pump). Linux Wayland popovers now coalesce redraws through a per-popover `wl_surface_frame` callback; the first configured paint remains immediate.
- [ ] [Manual] Verify menu/tooltip popovers still feel instant (first paint may render immediately; only subsequent updates need pacing).

---

# Phase 3 — Damage & blur cost

## FP-7: Make partial damage coexist with backdrop blur

**Severity: High (glass-themed desktops never use the partial path).**

- `CompositorRenderFrame.cpp:922-928` — `partialDamageCandidate` requires `!sceneUsesBackdropSampling(…)`; `sceneUsesBackdropSampling` (`:486-497`) is true whenever titlebar glass blur or any client `background-blur` is active. One tiny client damage rect then forces full-output compositing.
- `CommittedSurfacePainter.cpp:326` — `drawBackdropBlur` runs per `backgroundBlurRects` entry inside whatever clip is active; blur itself is the expensive part even with correct damage.

What to do:

- [x] [Auto] Use the existing `inflateDamageForBackdropSampling` (`CompositorSceneGraph.cpp`) to *expand* damage by the blur sampling radius around affected blur regions instead of disabling the partial path. Only fall back to full output when an inflated rect actually covers most of the screen.
- [x] [Auto] Cache blurred backdrop results per region: key by (region rect, backdrop content signature) — the Vulkan canvas already has `backdropBlurCache_` keyed by signature; ensure compositor-side damage that does not intersect a blur region's *sample area* leaves its cache entry valid, so unrelated client updates don't re-blur every glass titlebar.
- [ ] [Manual] Measure: with one glass titlebar and a terminal printing output, partial frames should activate and `background_ms`/`surface_ms` in the CSV should drop versus today's full-output frames.

Verification on Linux (KMS TTY):

- [ ] No visual artifacts at blur-region edges when neighboring content updates (sampling halo fully inside inflated damage).
- [ ] Frame profile CSV shows partial frames active with glass chrome enabled.

## FP-8: Region-limited scanout copies in KmsOutput

**Severity: Medium (fixed full-screen copy bandwidth every frame; large at 4K).**

- `KmsOutput.cpp:2776-2805` — `finishRenderCommandBuffer` always `vkCmdCopyImage`s the full mode extent from offscreen to scanout on the non-direct-scanout path.
- `KmsOutput.cpp:807, 859-863` — partial frames first copy the **full** displayed primary into the render target (`copyDisplayedPrimaryToRenderTarget` uses the full extent), so partial damage saves fragment shading but no copy bandwidth.
- `KmsOutput.cpp:755-758` — `canPreparePartialFrame` rejects partial while `pendingBuffer_ >= 0`, i.e. for the entire flip window, even though only one flip is in flight.

What to do:

- [x] [Auto] When `buffer.damageRects` is non-empty, issue one `VkImageCopy` region per (merged) damage rect for both the displayed-primary preservation copy and the offscreen→scanout copy, instead of full-extent copies.
- [x] [Auto] Relax `canPreparePartialFrame` to gate on `pageFlipPending_` only, provided the partial source buffer is not the pending one.
- [x] [Auto] Prefer direct scanout (`directScanoutRender_`) when modifiers allow — audit why it is not chosen on the test hardware and log the reason once at startup.
- [x] [Auto] Low priority, same file: reuse the `drmModeAtomicAlloc` request across commits if the driver allows (allocation per schedule at `CompositorRuntime.cpp:1496-1534`).

Verification on Linux (KMS TTY, ideally 4K):

- [ ] GPU copy time (validation/perf trace or `present_ms`) drops for small-damage frames.
- [ ] No stale-pixel artifacts when alternating partial and full frames; VT switch and mode changes still work.

## FP-9: Cut per-frame snapshot, string, and SHM copy overhead

**Severity: Medium (steady CPU tax scaling with window count).**

- `Snapshots.cpp:367-392` — `committedSurfaces()` builds the full snapshot vector by value (titles, appIds, vectors) and is called at least twice per loop (`CompositorRenderFrame.cpp:667`, empty-damage check at `CompositorRuntime.cpp:1851`).
- `CompositorSceneGraph.cpp:448-454, 834-835` — retained scene state copies full snapshots including strings every frame.
- `CommittedSurfacePainter.cpp:467-468` (also `SurfaceRenderer.cpp:457-458, 495-496`) — `visual.lastSnapshot = surface` copies the full snapshot per draw.
- `SurfaceRenderer.cpp:174-224` — SHM damage upload memcpys each region into a freshly sized `regionPixels` vector before `updatePixelsRegion`.
- `TextNode.cpp:32-34` — `canPrepareRenderOps()` returns false, so static text never uses the PreparedRenderOps cache.

What to do:

- [x] [Auto] Build `committedSurfaces()` once per loop iteration and pass it to both consumers; longer term, keep a persistent snapshot store updated on `contentSerial` change and expose `std::span<CommittedSurfaceSnapshot const>`.
- [x] [Auto] Replace retained scene-state snapshots and `visual.lastSnapshot` with a trimmed struct (id, serial, geometry, mapping, chrome hash) — no strings, no blur-rect vectors.
- [x] [Auto] Upload SHM damage directly from the mapped buffer with a row stride (extend `Image::updatePixelsRegion` to take stride) and reuse a persistent staging buffer per surface; coalesce adjacent rects first (shares the merge helper from FP-2).
- [x] [Auto] Allow `TextNode` to prepare render ops for static layouts (key the cache on layout pointer + dpiScale; invalidate via the existing glyph-atlas generation check).
- [x] [Auto] Add a one-shot log/metric when the dmabuf import fallback path (`SurfaceRenderer.cpp:399-425`, CPU `copyDmabufToRgba`) triggers, so unaccelerated clients are visible instead of silently slow.

Verification on Linux:

- [ ] CSV `snapshot_ms` drops with ~10 windows open; heap profile (or ASan malloc stats) shows per-frame allocation count reduction.
- [ ] `tests/` snapshot/damage suites still pass; text rendering unchanged (compare frame captures before/after for a text-heavy scene).

---

# Phase 4 — Pipeline depth & latent correctness

## FP-10: Asynchronous frame-capture and screenshot readbacks

**Severity: High when capture is active (serializes the pipeline); prerequisite for cheap parity testing.**

`VulkanCanvasLifecycle.inc:537-538` — `flushFrameCapture()` / `flushScreenshot()` run after submit and **before** `vkQueuePresentKHR`, waiting on the frame fence (`VulkanImages.inc:388-391, 443-447`). The KMS render-target path has the same shape, and `VulkanCanvasLifecycle.inc:814-816` makes every render-target present fully synchronous when no external semaphore is provided.

What to do:

- [x] [Auto] Move readback completion off the present path: keep recording the copy into the frame command buffer (`captureFrameIfRequested`), but collect results on a *later* frame (or a worker thread) once the recorded fence has signaled — never between submit and present.
- [x] [Auto] `takeCapturedFrameForCanvas` and the screenshot API may then need a "pending" state; callers (window-manager screenshots, tests) poll or block explicitly rather than implicitly stalling every present.
- [x] [Auto] Document the render-target fence wait at `:814-816` as intentional for offscreen rasterization, or offer the exported-fence variant for callers that can overlap.

Verification on Linux:

- [ ] With `LAMBDA_DEBUG_SCREENSHOT_PATH` active, `vulkan-present-detail` shows present no longer blocked by readback waits; captured images remain correct (existing frame-capture parity test still passes under lavapipe).
- [x] ASan run of capture-heavy Vulkan recorder/render-target tests is clean: `build-asan` passed 22 cases/170 assertions with leak detection enabled.

## FP-11: Re-enable the frame-recorder prepared-geometry fast path safely

**Severity: High for the compositor's per-surface cache (double copy + upload every replayed frame today).**

- `VulkanCanvasDrawOps.inc:904-907` — `canUsePreparedGeometry` is hard-coded `false && …` citing a RADV crash; replay copies recorded CPU geometry into the live vectors and re-uploads, while `prepareRecorderBuffers()` still allocates VMA buffers + descriptors that are never bound.
- `VulkanCanvasDrawOps.inc:760-761` — `ensureRecorderBuffer` returns early when capacity suffices and **skips the content upload**: latent stale-data bug that becomes live the moment the fast path is re-enabled.

What to do:

- [x] [Auto] Fix the upload-skip first: always `uploadRecorderBuffer` when `data` is provided, or skip only when the recorded geometry signature is unchanged (signatures already exist — `prepareRecordedGeometrySignatures`).
- [x] [Auto] While the fast path stays off, stop allocating the unused prepared buffers/descriptors in `prepareRecorderBuffers()` (alloc only when the fast path will bind them).
- [x] [Auto + Manual] Scope the prepared-geometry workaround to the affected driver via `VkPhysicalDeviceDriverProperties::driverID`, keep `LAMBDA_VULKAN_PREPARED_GEOMETRY=1` as a RADV reproduction override, and flip `canUsePreparedGeometry` on for unaffected drivers (lavapipe, ANV, NVIDIA).
- [x] [Auto] Add a lavapipe regression test that replays a recorder twice with mutated geometry between replays (catches the stale-upload bug) — extend `tests/VulkanRenderTargetTests.cpp`.

Verification on Linux:

- [ ] Compositor CSV `surface_ms` drops for cached windows; validation layers clean under RADV (or workaround documented per driver).

## FP-12: Tie deferred destruction and swapchain acquire to real frame completion

**Severity: Medium (latent use-after-free under resize storms; acquire leak on a defensive path).**

- `VulkanCanvasLifecycle.inc:432-434` — `destroyDeferred*(false)` decrements `framesRemaining` once per `presentImpl()` entry, **including** the OUT_OF_DATE early return (`:441-443`) and the missing-`imageRenderFinished` early return (`:455-458`) where nothing was submitted. The render-target cache-hit path (`:721-727`) also decrements without GPU work. Countdown is `kMaxFramesInFlight + 1` (`VulkanCanvasDrawOps.inc:471, 482`; `VulkanImages.inc:118, 192`).
- `VulkanCanvasLifecycle.inc:455-458` — returns after a **successful** `vkAcquireNextImageKHR` without presenting or releasing the image; repeated hits can exhaust acquirable images.

What to do:

- [x] [Auto] Move the deferred-destroy decrement to immediately after a successful `vkQueueSubmit2` (and after the render-target submit), so aborted presents and cache hits no longer age the queues.
- [x] [Auto] Validate `imageRenderFinished_` sizing *before* calling `vkAcquireNextImageKHR`; if the defensive condition can still trigger post-acquire, recreate the swapchain immediately (which releases acquired images) rather than returning.
- [x] [Auto] Add a debug assertion (validation builds) that a deferred entry's countdown only reaches zero after at least `kMaxFramesInFlight` successful submits.

Verification on Linux:

- [x] Automated resize-storm smoke drives compositor-side configure changes under CPU/KMS/pacing traces; latest run had 19 resize/configure events, 560 sizing cache-block samples, and zero fatal matches.
- [ ] ASan + validation-layer run while resizing a window violently (configure storms) — no use-after-free, no `vkAcquireNextImageKHR` returning `VK_NOT_READY`/timeout from image exhaustion.

## FP-13: Cheaper swapchain recreation and present-fence policy

**Severity: Medium (full pipeline drain per resize step; extra sync per present).**

- `VulkanSwapchain.inc:10-13` — `recreateSwapchain()` waits **all** frame fences on the present thread before rebuilding (called from `present()` at `VulkanCanvasLifecycle.inc:386-388`).
- `VulkanCanvasLifecycle.inc:551-572` — with `swapchainMaintenance1`, every present first waits (up to 1 s) on the per-image present fence; fences are created signaled (`VulkanSwapchain.inc:105-106`). This adds a CPU sync per image reuse on top of acquire back-pressure.

What to do:

- [x] [Auto] Use the existing `oldSwapchain` handoff (`VulkanSwapchain.inc:89`) without draining all frame fences: keep the old swapchain alive in a retire list and destroy it when its images' present fences (maintenance1) or frame fences signal. Only the first-ever create needs no wait at all.
- [x] [Auto] Restrict the present-fence wait to swapchain retirement and resource destruction; steady-state presents should rely on acquire back-pressure alone. Keep `presentFenceRuntimeDisabled_` as the escape hatch.
- [ ] [Manual] Interactive resize on Wayland: trace `vulkan-recreate-swapchain` and confirm recreate cost no longer scales with frames in flight; extent-headroom growth (only-grow logic at `VulkanCanvasLifecycle.inc:217-224`) still avoids most recreates.

## FP-14: Client-side pacing — flush on frameDone, avoid double throttling, align platforms

**Severity: Medium (added latency per frame on Wayland clients; platform asymmetry).**

- `WaylandWindow.cpp:1844-1861` — `frameDone` posts a `FrameEvent` and wakes the loop, but `presentRequestedWindows` already ran this exec iteration (`Application.cpp:838-839`), so the present happens one loop trip later. macOS flushes inline from the display-link tick (`Application.mm:737-739`, `MacMetalWindow.mm:2002-2010`).
- `WaylandWindow.cpp:1302-1321` + `VulkanSwapchain.inc:33-38` — rendering is gated on `wl_surface_frame` **and** blocks in acquire/present. Fine under MAILBOX; on FIFO-only drivers the throttles stack.
- `MacMetalWindow.mm:2002-2010` — legacy off-main-thread CVDisplayLink only posts + wakes (extra latency vs the modern path).
- `Application.mm:737-739` vs `Application.cpp:838-839` — macOS `flushRedraw` skips the frame-ready gate; Linux exec requires it.

What to do:

- [x] [Auto] Call `Application::flushRedraw()` from `frameDone` after dispatch (mirroring macOS), or re-run the present pass after Wayland dispatch within the same loop iteration.
- [x] [Auto] When the active present mode is FIFO (no MAILBOX), drop the `wl_frame` gate and let FIFO pace presentation; keep the gate under MAILBOX. Consider `VK_PRESENT_MODE_FIFO_RELAXED_KHR` where available.
- [x] [Auto] Legacy CVDisplayLink path: `CFRunLoopPerformBlock` to the main run loop with `flushRedraw`, matching the modern CADisplayLink path.
- [x] [Auto] Document (or unify) the `requireFrameReady` asymmetry between `Application.cpp` exec and `Application.mm flushRedraw` so future platform work doesn't diverge further.

Verification:

- [ ] [Manual] Wayland: `LAMBDA_RESIZE_TRACE=1 app-render` traces show render starting in the same loop iteration as `frameDone`; animation demos hold refresh rate on a FIFO-only driver (e.g. lavapipe window).
- [ ] [Auto] Existing input/animation tests pass on both platforms.

## FP-15: Presenter-level pacing correctness (KMS + Vulkan-display fallback)

**Severity: Medium (fallback presenter mispaces clients; feedback protocol inaccuracies).**

- `KmsOutput.cpp:655-665` + `CompositorRuntime.cpp:1302-1311` — KMS `IN_FENCE_FD` is opt-in via `LAMBDA_COMPOSITOR_USE_KMS_IN_FENCE`; without it the loop waits for the render sync-fd before committing instead of letting the kernel wait GPU-side.
- `CompositorRuntime.cpp:2268-2286` — Vulkan-display fallback blocks on `waitForVblank()` **and** then on swapchain fences inside `canvas.present()` (double pacing).
- `CompositorRenderFrame.cpp:1122-1127` — the fallback sends `wl_frame` callbacks immediately after `canvas.present()`, before scanout (atomic path correctly defers to flip: `CompositorRuntime.cpp:1591-1594`).
- `FrameScheduler.cpp:386-396` — presentation-feedback fallback (500 ms) reports `presented` with render-time estimates instead of `discarded`; `normalizePresentationTiming` (`:93-98`) substitutes "now" for zero timestamps; `wl_callback_send_done` uses compositor monotonic ms, not flip time (`:327-329`).
- `KmsOutput.cpp:3505-3530` — one `drmWaitVBlank` failure permanently degrades `waitForVblank()` to an unanchored `sleep_for(refresh)`.
- `KmsOutput.cpp:2965-2973` — blocking `waitForPageFlip()` remains in the synchronous `present()` API (compositor uses the async dispatch; keep apps off the sync path).

What to do:

- [x] [Auto] Probe and enable `IN_FENCE_FD` by default when the primary plane supports it; keep the env var as an opt-out; extend `allowRenderFence` to non-render-ahead frames.
- [x] [Auto] Vulkan-display fallback: pick one pacing source (drop the vblank wait and let swapchain pacing rule, or use `VK_GOOGLE_display_timing` when available); buffer `wl_frame` callbacks per presentId and dispatch them from the present-timing poll, mirroring the atomic path.
- [x] [Auto] Presentation feedback: send `discarded` on fallback expiry (shorten to ~2 refresh periods); never substitute render-time for zero timestamps on HW-completion feedback; pass flip/vblank ms into `sendFrameCallbacksOnly`.
- [x] [Auto] Anchor the vblank sleep fallback to the last flip timestamp (phase-locked) and retry `drmWaitVBlank` per-CRTC instead of disabling it forever.
- [x] [Auto] Add the schedule→flip→feedback integration test that `tests/CompositorPresentationFeedbackTests.cpp` currently lacks (assert `tv_sec/nsec`, `refresh`, `seq`, `HW_COMPLETION` flags through a mock presenter).

Verification on Linux (KMS TTY):

- [x] `weston-presentation-shm` (or equivalent client) reports sane presentation timestamps on both presenters.
- [x] With IN_FENCE enabled, CSV `present_ms` shows commit no longer waiting for render-fence readiness.

## FP-16: Metal path improvements (macOS — can be done on this machine)

**Severity: Medium-high in aggregate (atlas-grow hitches, backdrop blur cost, end-of-frame drawable stalls).**

Implementation note: the code changes are in place, but macOS compile/runtime verification is still pending
because the current implementation pass ran on Linux.

- `GlyphAtlas.mm:68-107` — `grow()` clears all entries, doubles dimensions, and synchronously re-rasterizes/re-uploads every glyph; `prepareForFrameBegin`/`afterPresent` (`:56-66`) trigger it. `uploadR8` (`:23-29`) does a synchronous `replaceRegion` per glyph.
- `MetalCanvas.mm:1992-2000` — backdrop frames allocate `finalUniformBuffer`/`finalClipBuffer` with `newBufferWithLength` every present; `encodeBackdropBlur` (`:1944-1956`) runs 3 iterations × 2 passes at full resolution (Vulkan downsamples; Metal does not); backdrop frames disable MSAA (`:637-649`).
- `MetalCanvas.mm:597-634` — `nextDrawable` is acquired after all CPU uploads; with `allowsNextDrawableTimeout = YES` a nil drawable abandons the fully recorded frame.
- `MacMetalWindow.mm:99-106` — `displaySyncEnabled` never set explicitly.
- `MetalCanvas.mm:2791-2795` — render-target mode without a shared event uses `waitUntilCompleted` (prefer `MTLSharedEvent`, already supported via `targetSpec_.signalValue`).

What to do:

- [x] [Auto + Manual] Copy-preserving atlas grow: blit the old atlas texture into the new one and keep entries; batch new-glyph uploads through a blit encoder; grow only in `afterPresent` with a headroom threshold.
- [x] [Auto] Pool the two backdrop buffers grow-only on `MetalCanvas` (like the existing triple-buffered arenas), and add a downsampled blur chain mirroring Vulkan's `backdropBlurDownsample()`.
- [x] [Auto] Acquire the drawable right after the in-flight semaphore wait in `beginFrame` (hold it through encode) and retry `nextDrawable` once before dropping a frame.
- [x] [Auto] Set `metalLayer.displaySyncEnabled = YES` explicitly with a comment documenting the intent.
- [x] [Auto] Prefer `MTLSharedEvent` for render-target consumers instead of `waitUntilCompleted`.

Verification on macOS:

- [ ] `debug::perf` — `CanvasDrawableWait` p95 drops; no atlas-grow hitch when pasting large unicode-heavy text into `lambda-editor`.
- [ ] Full `ctest` suite passes (includes `MetalCanvasTests.mm`); backdrop blur visuals unchanged (frame-capture comparison).

---

# Post-implementation code review backlog

Items found during the 2026-06-11 review of commits `50d65831..HEAD`. Each item has current file:line anchors. Delete the item when fixed and verified.

## High severity

### REV-V1: External-command-buffer render targets never age deferred destroys (KMS memory leak)

**Severity: High (unbounded ~16 MB staging-buffer growth on the compositor output path).**

`retireDeferredResourcesAfterSubmit()` (`VulkanImages.inc:265-269`) runs only after the canvas's own `vkQueueSubmit2` (`VulkanCanvasLifecycle.inc:549-558`). The KMS atomic presenter always supplies an external command buffer (`KmsOutput.cpp:2870`), so `presentRenderTarget()` returns at `VulkanCanvasLifecycle.inc:803` before line 838 — deferred texture/buffer/image destroys and staging-buffer recycling never run. Every glyph-atlas upload queues a ~16 MB staging buffer (`recordPendingTextureUploads`, `VulkanImages.inc:243-254`).

What to do:

- [ ] [Auto] Call `retireDeferredResourcesAfterSubmit()` (or equivalent aging) on the external-command-buffer path after the external queue submit that consumed the recorded commands.
- [ ] [Auto] Alternatively, tie deferred-destroy aging to the exported render-fence / frame-fence completion on the KMS path.
- [ ] [Auto + Manual] Run compositor with heavy text churn; monitor RSS / VMA allocator stats — should plateau, not grow linearly with frames.

### REV-K1: Discarded frames poison partial-frame content preservation

**Severity: High (ghost pixels on partial scanout reuse).**

Region-limited copies (`KmsOutput.cpp:3040-3063`) assume buffer pixels equal last-presented content plus `staleRects` (`markPrimaryDamagePresented`, `2066-2084`). A frame rendered then discarded from the mailbox (`releasePreparedBuffer`, `2051-2064`) overwrites offscreen/scanout without updating `primaryContentsValid` / `staleRects`. The next partial reuse of that buffer can scan out never-presented pixels outside `staleRects ∪ new damage`.

What to do:

- [ ] [Auto] In `releasePreparedBuffer`, set `buffer.primaryContentsValid = false` or reset `staleRects` to full-output so the next partial use forces a full preserved-copy.
- [ ] [Manual] Drag a window with intermediate discarded frames; verify no ghost at revert positions (caret blink, hover flicker, discontiguous moves).

### REV-M1: Metal atlas grow blit races with in-flight render-queue uploads

**Severity: High (permanent garbage glyphs after grow).**

`GlyphAtlas::grow()` (`GlyphAtlas.mm:111-172`) blits old→new on the atlas private queue and waits only on that command buffer. Glyph uploads are encoded on the render queue via `flushUploads()` (`MetalCanvas.mm:642`). `grow()` runs from `afterPresent()` immediately after `[cmdBuf_ commit]` (`MetalCanvas.mm:873-877`) and from `prepareForFrameBegin()` after only a 3-frame semaphore wait — cross-queue execution is unordered, so the grow blit can read the old atlas before in-flight upload blits finish. Copy-preserving grow retains `entries_`, so corrupted glyphs persist.

What to do:

- [ ] [Auto] Encode the grow blit on `metal_.queue()` after waiting on a `MTLSharedEvent` signaled by the last upload frame, or block the grow until the render queue is idle for atlas writes.
- [ ] [Manual] Paste large unicode-heavy text into `lambda-editor` past atlas pressure; verify no missing/corrupt glyphs.

### REV-M2: Deferred mid-frame grow caches empty `AtlasEntry` forever

**Severity: High (glyph permanently missing for font/size).**

When shelf is full mid-frame, `beforeGrow_` blocks grow, `grow()` sets `pendingGrow_` and returns false, and `allocateAndUpload()` returns `AtlasEntry{}` which `getOrUpload()` emplaces into `entries_` (`GlyphAtlas.mm:197-201`, `220-227`). Copy-preserving grow no longer clears entries, so the empty sentinel survives after the deferred grow at `prepareForFrameBegin()` (`GlyphAtlas.mm:99-103`). `drawTextLayout` skips `entry.width == 0` (`MetalCanvas.mm:1445`).

What to do:

- [ ] [Auto] Do not insert the empty entry when grow is deferred; retry allocation after grow completes, or erase the sentinel entry in `prepareForFrameBegin()` before grow.
- [ ] [Auto] Add a unit test: force deferred grow mid-frame, then verify the glyph renders on the next frame.

### REV-M3: Metal backdrop uniform/clip buffers are CPU-rewritten while GPU still reads them

**Severity: High (wrong blur transforms/clips under load).**

`backdropUniformBuffer_` / `backdropClipBuffer_` (`MetalCanvas.mm:1930-1957`) are single persistent shared buffers, not per-frame-slot. `encodeRecorderOps` CPU-writes slots at encode time (`MetalCanvas.mm:410-426`, `2183-2186`) while prior frames' command buffers may still execute (`kFramesInFlight = 3`). Old code allocated fresh buffers each frame (safe via command-buffer retention).

What to do:

- [ ] [Auto] Ring-buffer or triple-buffer backdrop uniform/clip buffers per frame slot (like `metal_.advanceFrame()` arenas).
- [ ] [Manual] Stress backdrop-blur frames under GPU load; compare visual stability to pre-change captures.

---

## Medium severity

### REV-V2: Prepared-geometry replay omits clip intersection

**Severity: Medium (draws outside parent clip on non-RADV drivers).**

CPU-copy replay intersects clip (`VulkanCanvasDrawOps.inc:1100`); prepared-geometry path only translates (`:1019`). Scissor comes from `op.clip` (`VulkanCommandRecording.inc:409-414`). Enabled on non-RADV since `recorderPreparedGeometryFastPathEnabled()` (`VulkanCanvasDrawOps.inc:978-988`).

What to do:

- [ ] [Auto] Apply `intersectRects(translatedRect(op.clip, dx, dy), state_.clip)` in the prepared path too.
- [ ] [Auto] Add a clipped-replay regression test under `LAMBDA_VULKAN_PREPARED_GEOMETRY=1` or on a non-RADV CI target.

### REV-V3: Recorder GPU buffers overwritten every replay without in-flight guard

**Severity: Medium (torn geometry; extra map/memcpy per cached node).**

`ensureRecorderBuffer` re-uploads on every `prepareRecorderBuffers` call (`VulkanCanvasDrawOps.inc:806-809`) while prior frames may still bind the buffer. Identical content is benign; mutated geometry (the new test case) can race. Content-hash skip or per-frame/fence-guarded buffers would fix both perf and correctness.

What to do:

- [ ] [Auto] Skip upload when recorded geometry signature unchanged; or use per-frame recorder buffer slots.
- [ ] [Auto] Extend `VulkanRenderTargetTests.cpp` to exercise in-flight overwrite (two overlapping presents with geometry mutation).

### REV-V4: `evictImageTexture` cancels first-use pending upload/transition

**Severity: Medium (undefined layout + garbage on same-frame draw+destroy).**

`evictImageTexturesFor` erases pending uploads and transitions (`VulkanCanvasDrawOps.inc:466-487`) that `ensureImageTexture` queued (`VulkanImages.inc:22-23`, `31-33`). Texture survives in `pendingTextureDestroys_` with ops still referencing its descriptor.

What to do:

- [ ] [Auto] Do not erase pending upload/transition for textures still referenced by current-frame ops; or flush pending work before eviction.
- [ ] [Auto] Add a same-frame draw-then-destroy test under validation layers.

### REV-V5: `vkDestroyFence` on present fence after failed present

**Severity: Medium (validation-layer fault under OUT_OF_DATE).**

`VulkanCanvasLifecycle.inc:614-619` destroys the present fence immediately when `vkQueuePresentKHR` fails. Under swapchain-maintenance1 the present may still be enqueued. Use `retirePresentFence()` (pattern at `:589`) instead.

What to do:

- [ ] [Auto] Replace immediate destroy with `retirePresentFence(presentFence)` and a fresh signaled replacement.
- [ ] [Auto] Resize-storm test under validation layers — no `VUID-vkDestroyFence-fence-01120`.

### REV-K2: EBUSY-deferred cursor-only commit can strand the cursor

**Severity: Medium (cursor stops short of final position on idle screen).**

`commitAtomicCursor` defers on EBUSY/EAGAIN (`KmsOutput.cpp:3612-3616`) but replays only from `setAtomicPageFlipPending(false)` (`3551-3558`) — cursor-only commits have no page-flip event. On an otherwise idle screen the last move of a burst may never replay. Opt-in via `LAMBDA_COMPOSITOR_ENABLE_HARDWARE_CURSOR_MOTION_FAST_PATH=1`.

What to do:

- [ ] [Auto] Add a timer, vblank, or retry loop to replay `atomicCursorMoveDeferred_` without waiting for a primary-plane flip.
- [ ] [Manual] 1000 Hz mouse on idle desktop — cursor reaches final position.

### REV-K3: Scene damage under-covers after rendered-but-unscheduled frame

**Severity: Medium (stale pixels on glass; amplified by relaxed partial guard).**

Client buffer damage is consumed at render time (`SurfaceRenderer.cpp:311-348`); scene-graph baseline advances only at schedule time (`CompositorRuntime.cpp:1417-1420`). If frame N renders (consuming damage M→N), is replaced in the mailbox before schedule, and the client commits again, frame N+1 diffs against M but `bufferDamageRects` only has post-N commits.

What to do:

- [ ] [Auto] Advance `surfaceRenderState.sceneGraph` when a frame is rendered, not only when scheduled; or roll back consumed damage when a prepared frame is discarded.
- [ ] [Manual] Rapid glass-window updates with mailbox replacement — no stale regions outside damage.

### REV-W1: FIFO path drops `wl_surface_frame` throttling for occluded windows

**Severity: Medium (event-loop stall or spin when occluded).**

Non-MAILBOX `requestAnimationFrame` posts `FrameEvent` immediately (`WaylandWindow.cpp:1324-1325`) without `wl_surface_frame`. Occluded surfaces no longer pause cheaply; FIFO present may block the shared event loop. `usesMailboxPresentMode()` is false until first swapchain exists.

What to do:

- [ ] [Auto] Keep `wl_frame` gating when surface is occluded/minimized even under FIFO; or detect occlusion and skip present.
- [ ] [Manual] Hide a FIFO-paced window behind another — CPU/event-loop should stay responsive.

### REV-W2: Popover coalescing can stall on idle compositor without frame callbacks

**Severity: Medium (frozen popover after pointer event).**

`requestPopoverFrame` commits without buffer damage (`WaylandWindow.cpp:1665-1673`). Some compositors only deliver `wl_surface_frame` when they repaint; hardware-cursor-only motion may not trigger it. Event-driven paints now wait ≥1 compositor frame vs synchronous before.

What to do:

- [ ] [Auto] Render first paint after an event immediately; pace only subsequent updates. Or attach minimal damage / add fallback timeout.
- [ ] [Manual] Open popover menu on idle KMS desktop; hover/click updates remain responsive.

### REV-M4: Backdrop segments overwrite each other's uniform slots (pre-existing)

**Severity: Medium (incorrect blur transforms when segments differ).**

`encodeFrameWithBackdropBlur` calls `encodeRecorderOps` per segment with the same `finalUniformBuffer`/`finalClipBuffer`, restarting indices at 0 each call (`MetalCanvas.mm:2119-2152`, `2185-2186`). Pre-existing; persistent buffers (REV-M3) make fixing here natural.

What to do:

- [ ] [Auto] Continue `uniformIndex`/`clipIndex` across segments within one backdrop frame.
- [ ] [Manual] Multi-segment backdrop frame with differing transforms — visual parity check.

### REV-F4: Cursor deferred during primary page flip instead of interleaved commit

**Severity: Medium (fast path skips render but cursor may not move until flip completes).**

`commitAtomicCursor` returns success while deferring when `atomicPageFlipPending_` (`KmsOutput.cpp:3576-3579`). Compositor skips full frame but on-screen cursor may lag one flip period.

What to do:

- [ ] [Auto] Attempt cursor-only atomic commit even during page flip pending (separate from primary-plane IN_FENCE), or document as accepted trade-off.
- [ ] [Manual] Cursor responsiveness during active primary-plane flips under GPU load.

### REV-F2: Partial damage uses bounding-union clip (over-draw follow-up)

**Severity: Medium (GPU over-shade when damage rects are sparse).**

`logicalDamageBounds` + single `clipRect` (`CompositorRenderFrame.cpp:1106-1118`) fixes CPU N× recording but can shade the union of distant rects. Multi-rect scissor or stencil clip remains a follow-up.

What to do:

- [ ] [Auto] Add multi-rect clip on `Canvas` when rect count > 1 and union area ≫ sum of areas.
- [ ] [Manual] Two distant dirty windows — `surface_ms` and GPU time should beat old per-rect loop without excess over-draw vs per-rect KMS damage.

---

## Low severity

### REV-V6: Recorder `sourceImage` raw pointer — undocumented keep-alive contract

**Severity: Low (use-after-free if caller replays after image destruction).**

`VulkanCanvasTypes.hpp:76`, replay at `VulkanCanvasDrawOps.inc:947`. In-tree usage is safe (`ImageNode` retains `shared_ptr`); test keeps image alive deliberately. Frame signature hashes raw pointer (ABA-prone in theory).

What to do:

- [ ] [Auto] Document keep-alive requirement on `VulkanFrameRecorder`, or store `shared_ptr<Image>` in recorded ops.
- [ ] [Auto] Add test that replay after image destruction is detected (fail gracefully).

### REV-V7: Prepared text ops not refreshed after atlas rebuild

**Severity: Low (permanent live-render fallback after one atlas rebuild).**

`recordedGlyphAtlasCurrent` rejects stale replay (`VulkanCanvasDrawOps.inc:761-767`) but leaves stale `PreparedRenderOps` in place (`SceneRenderer.cpp:688-697`). Node is not re-prepared because key unchanged.

What to do:

- [ ] [Auto] Clear `preparedRenderOps_` when replay fails due to atlas generation mismatch.
- [ ] [Auto] Scene-graph test: rebuild atlas between renders, assert prepare count increases.

### REV-V8: Destructor leaks `pendingTextureUploads_` staging buffers

**Severity: Low (teardown leak when uploads queued but never presented).**

Destructor (`VulkanCanvasLifecycle.inc:164-193`) frees pools and deferred queues but not `pendingTextureUploads_` entries. More likely since FP-1 made queued uploads common.

What to do:

- [ ] [Auto] Drain `pendingTextureUploads_` in destructor (recycle or destroy staging buffers).

### REV-V9: Post-acquire incomplete-image branch unreachable

**Severity: Low (dead code; would corrupt acquire semaphore if reached).**

Pre-acquire `swapchainImageStateReadyForAcquire()` (`VulkanCanvasLifecycle.inc:444-447`) makes post-acquire recreate branch (`460-468`) unreachable. If hit, would leave `imageAvailable` semaphore in bad state.

What to do:

- [ ] [Auto] Remove dead branch or rotate semaphore on that path.

### REV-K4: `HW_COMPLETION` OR'd onto software-estimated flip completions

**Severity: Low (wp_presentation semantics; pre-existing, preserved by refactor).**

`resolvePresentationFeedbackCompletion` (`CompositorPresentation.hpp:145`, `FrameScheduler.cpp:409-416`) adds `HW_COMPLETION` even when completion lacks `HW_CLOCK`. Runtime sets hardware flags only for real flips (`CompositorRuntime.cpp:1892-1900`).

What to do:

- [ ] [Auto] Pass `hardwareCompletionFlag = 0` when completion flags lack `HW_CLOCK`.
- [ ] [Auto] Extend `CompositorPresentationFeedbackTests.cpp` to assert flag semantics for software vs hardware completions.

### REV-K5: Cursor hide dropped while page flip pending

**Severity: Low (atomic plane may stay visible until legacy ioctl rescues).**

`commitAtomicCursor(false)` while `atomicPageFlipPending_` sets `atomicCursorMoveDeferred_ = false` and returns success (`KmsOutput.cpp:3576-3579`). Disable never replays; relies on legacy `drmModeSetCursor` in `hideCursor` (`4003-4011`).

What to do:

- [ ] [Auto] Defer hides with `atomicCursorMoveDeferred_ = true` regardless of visible flag; replay hide on flip completion.

### REV-K6: Transient chrome double-draws all three buttons

**Severity: Low (cosmetic — darker/thicker glyphs, ghost under translucent hover).**

Cached base layer already contains non-hover controls (`stableChromeSnapshot`); `drawTransientChromeControls` redraws all three (`SurfaceRenderer.cpp:553`, `572` → `WindowChromeRenderer.cpp:230-232`).

What to do:

- [ ] [Auto] Draw only hovered/pressed button(s), or erase base glyph rect before hover overlay.

### REV-M5: Drawable retry can block main thread up to ~2 s

**Severity: Low (stall at `beginFrame` when drawable pool exhausted).**

`allowsNextDrawableTimeout = YES` on layer (`MacMetalWindow.mm:101`); retry in `acquireDrawableForFrame` (`MetalCanvas.mm:1584-1597`). Failure path re-signs semaphore and requests redraw — no deadlock.

What to do:

- [ ] [Auto] Consider shorter timeout or async drawable acquisition; measure `CanvasDrawableWait` p95.
- [ ] [Manual] Exhaust drawable pool (rapid resize) — app remains interactive within bounded stall.

### REV-M6: `grow()` synchronous `waitUntilCompleted` on frame-begin path

**Severity: Low (pacing hitch on grow frames).**

`prepareForFrameBegin()` may call `grow()` after drawable acquired (`MetalCanvas.mm:547-572`, `GlyphAtlas.mm:99-103`). Holds drawable during full GPU round-trip.

What to do:

- [ ] [Auto] Defer grow to `afterPresent` only, or use async handoff (pairs with REV-M1).

### REV-W3: `frameDone` `flushRedraw()` bypasses dispatch deferral during resize

**Severity: Low (one stale-size frame possible; self-correcting).**

Configure path defers redraw while `dispatchingWaylandEvents_` (`WaylandWindow.cpp:2077`); `frameDone` renders inline (`1908-1927`).

What to do:

- [ ] [Auto] Optionally defer `flushRedraw` from `frameDone` when `dispatchingWaylandEvents_` is set, matching configure behavior.
- [ ] [Manual] Interactive resize — no visible one-frame size mismatch.

### REV-F5: Hardware cursor fast path permanently disables after first failure

**Severity: Low (session-long fallback to full redraws).**

`hardwareCursorMotionFastPathDisabled = true` after first failed fast-path attempt (`CompositorRuntime.cpp:1057`). Transient EBUSY could permanently disable.

What to do:

- [ ] [Auto] Retry fast path after successful flip or on a timer instead of permanent disable.

### REV-F13: Present fences still attached on steady-state presents

**Severity: Low (non-blocking status poll, not 1 s wait — reduced vs plan fear).**

Steady-state presents still use `SwapchainPresentFenceInfoKHR` when maintenance1 available (`VulkanCanvasLifecycle.inc:575-608`). Uses `vkGetFenceStatus` + fence replacement on `NOT_READY`, not blocking wait. Retire-list handles swapchain recreation.

What to do:

- [ ] [Auto] Optional: disable steady-state present fences entirely; rely on acquire back-pressure only (keep `LAMBDA_VULKAN_PRESENT_FENCES=0` escape hatch).
- [ ] [Manual] Resize trace — present phase ms stable under interactive resize.

---

## Nits

### REV-K7: Fragile errno classification in `commitAtomicCursor`

`KmsOutput.cpp:3589-3591` — if `addAtomicCursorProperties` returns false without setting errno, stale `EBUSY` could false-defer. Currently unreachable given pre-checks at `3571-3572`.

- [ ] [Auto] Distinguish "props not added" from "commit failed" explicitly.

### REV-K8: Stray tab indentation

Tabs mixed into space-indented files, e.g. `KmsOutput.cpp:2024` (`Buffer` struct), `CompositorRuntime.cpp:2489`.

- [ ] [Auto] Strip leading tabs on affected lines.

### REV-W4: `renderPopover` throw through libwayland C callback

`popoverFrameDone` (`WaylandWindow.cpp:1905`) calls `renderPopover` which rethrows (`1643-1658`). Undefined behavior if exception crosses C callback boundary.

- [ ] [Auto] Catch and log at callback boundary (`popoverFrameDone`, `popoverXdgSurfaceConfigure`).

### REV-V10: Redundant double `resolveRecordedImageTexture` in `appendRecordedOps`

Pre-loop resolution (`VulkanCanvasDrawOps.inc:990-994`) plus in-loop calls in both branches — dead weight.

- [ ] [Auto] Remove in-loop duplicate resolution.

### REV-V11: `endImmediate` / `transitionImmediate` effectively dead code

No per-frame callers remain; `uploadTexture(uploadNow=true)` has no callers; `transitionImmediate` has zero call sites. FP-1 goal achieved.

- [ ] [Auto] Delete `endImmediate`, `transitionImmediate`, and the `uploadNow` branch, or mark `@deprecated` with assert if called.

### REV-V12: `rasterize()` temporary canvas calls `vkDeviceWaitIdle` mid-session

`VulkanCanvasLifecycle.inc:167` reachable via raster-cache builds — pre-existing whole-device stall, not introduced by frame-pacing work.

- [ ] [Auto] Optional follow-up: defer raster-cache readback like FP-10 readback path.

---

# Citation errata (original FP audit → current sources)

| Original claim | Current status | Correct anchor |
| --- | --- | --- |
| Atlas upload via `uploadTexture` + `vkQueueWaitIdle` on hot path | Fixed — uses `queueAtlasUploadIfNeeded` → `queueTextureUpload` | `VulkanGlyphAtlas.inc:127-132`, `VulkanImages.inc:151+`, `243-254` |
| `transitionImmediate` on imported images | Fixed — uses `queueTextureTransition` | `VulkanImages.inc:31-33` |
| Per-rect `drawFrameContent` loop on partial damage | Fixed — single bounding-union clip | `CompositorRenderFrame.cpp:1106-1118` |
| `sceneUsesBackdropSampling` blocks partial damage | Removed — uses `inflateDamageForBackdropSampling` | `CompositorSceneGraph.cpp:216-243`, `CompositorRenderFrame.cpp:1005-1010` |
| `markFrameRendered` `vkQueueWaitIdle` fallback | Fixed — no `vkQueueWaitIdle` in `KmsOutput.cpp` | `KmsOutput.cpp:1087-1095` |
| `canUsePreparedGeometry = false &&` | Fixed — driver-gated `recorderPreparedGeometryFastPathEnabled()` | `VulkanCanvasDrawOps.inc:830-838`, `978-988` |
| `ensureRecorderBuffer` skips upload | Fixed — always uploads when data provided | `VulkanCanvasDrawOps.inc:806-808` |
| Deferred destroy before acquire / on early return | Fixed — `retireDeferredResourcesAfterSubmit` after submit only | `VulkanCanvasLifecycle.inc:558`, `VulkanImages.inc:265-269` |
| Present-fence 1 s blocking wait | Fixed — `vkGetFenceStatus` + replacement | `VulkanCanvasLifecycle.inc:575-597` |
| `recreateSwapchain` waits all frame fences | Fixed — retire list, no frame-fence drain | `VulkanSwapchain.inc:131-152` |
| Swapchain recreate unconditional stderr | Fixed — gated by `resizeTraceEnabled()` | `VulkanSwapchain.inc:184-203` |
| `flushFrameCapture` before present | Fixed — `pollReadbacks(false)` after submit | `VulkanCanvasLifecycle.inc:561`, `VulkanImages.inc:613-616` |
| Transient chrome blocks recorder replay | Fixed — `stableChromeSnapshot` + `drawTransientChromeControls` | `SurfaceRenderer.cpp:137-145`, `544-572` |
| SHM upload memcpy into `regionPixels` vector | Fixed — span subview with stride | `SurfaceRenderer.cpp:236-243` |
| `TextNode::canPrepareRenderOps` always false | Fixed — returns `layout_ != nullptr` | `TextNode.cpp:50-52` |
| `frameDone` present one loop late | Fixed — inline `flushRedraw()` | `WaylandWindow.cpp:1908-1927` |
| Metal `grow()` clears and re-rasterizes all glyphs | Fixed — copy-preserving blit grow | `GlyphAtlas.mm:111-172` |
| Metal backdrop full-res 3×2 blur, per-frame buffer alloc | Fixed — downsample + pooled buffers | `MetalCanvas.mm:1930-1957`, `2028-2040`, `2087-2090` |
| `displaySyncEnabled` never set | Fixed — `YES` at create | `MacMetalWindow.mm:104` |
| Presentation feedback 500 ms fake-presented fallback | Fixed — ~2 refresh periods, `discarded` on expiry | `CompositorPresentation.hpp:96-97`, `FrameScheduler.cpp:137-149` |

---

## Exit criteria

- [ ] All REV items above deleted as fixed and verified.
- [ ] All unchecked manual verification boxes under FP-1 … FP-16 closed.
- [ ] `vulkan-present-detail`, compositor frame CSV, and `debug::perf` counters within budget; no `vkQueueWaitIdle` on per-frame paths (teardown/rasterize exceptions documented in REV-V11/V12).
- [ ] Validation-layer run clean on Linux (resize storm, text scroll, capture paths).
- [ ] Remove this document and TODO-019 from `TODO.md`, and drop the row from `docs/roadmap.md`.
