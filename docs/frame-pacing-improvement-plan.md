# Frame Pacing Improvement Plan

**Status:** Code implementation complete; retained as the verification follow-up checklist for manual, validation-layer, hardware input-driver, and macOS checks. Tracked as TODO-019 in [TODO.md](../TODO.md).
**Scope:** `apps/lambda-window-manager/Compositor/`, `src/Platform/Linux/` (KMS + Wayland), `src/Graphics/Vulkan/`, `src/Graphics/Metal/`, `src/Platform/Mac/`, `src/SceneGraph/`, and `src/UI/Application.*`.
**Source:** Frame-pacing review of commit `ca4466cd` (four parallel code audits; the highest-impact claims verified directly in source). Severity reflects expected impact on smoothness and pacing, not crash risk.

Most items require a Linux machine (Wayland and/or KMS from a TTY) to verify. FP-14 and FP-16 are macOS work and can be done on a Mac. The implementation pass completed on Linux on 2026-06-11; keep this document until the remaining manual/macOS verification is complete, then delete the document and TODO-019.

## Verification Snapshot: 2026-06-11

- [x] Clean normal and KMS rebuilds completed with zero warnings and zero errors.
- [x] Focused normal tests passed: Vulkan image-recorder replay test (1 case, 12 assertions), compositor/Vulkan/presentation/damage/cursor/pointer slice (88 cases, 602 assertions), and reactive tests (22 cases, 126 assertions).
- [x] Focused KMS tests passed: Vulkan image-recorder replay test (1 case, 12 assertions), compositor/Vulkan/presentation/damage/cursor/pointer slice (89 cases, 606 assertions), and reactive tests (22 cases, 126 assertions).
- [x] KMS compositor runtime smoke completed with normal-build `lambda-shell`, scripted `lambda-terminal`, and `lambda-editor`; logs had zero fatal/error matches.
- [x] Runtime traces captured 20 CPU samples, 37 KMS timing windows, 10,712 pacing events, and 900 terminal `vulkan-present-detail` samples. Summary: compositor CPU avg/max 12.54%/15.60%, compositor surface avg/max 0.471/0.632 ms, present avg/max 0.395/0.582 ms, terminal atlas avg 0.004 ms, terminal `waitImage` avg 0.000 ms.
- [x] Added `scripts/verify-frame-pacing-linux.sh` and `lambda-presentation-feedback-check`. Latest run passed both presenters: atomic-KMS reported `CLOCK_MONOTONIC`, refresh 16,666,666 ns, nonzero sequence, and `VSYNC|HW_CLOCK|HW_COMPLETION`; Vulkan-display reported `CLOCK_MONOTONIC`, refresh 16,666,666 ns, and nonzero sequence. Atomic runtime summary: 20 CPU samples, 31 KMS timing windows, 10,540 pacing events, 919 terminal `vulkan-present-detail` samples, zero fatal matches, CPU avg/max 12.14%/15.60%, surface avg/max 0.385/1.047 ms, present avg/max 0.412/0.523 ms, terminal `waitImage` avg 0.000 ms, terminal atlas avg 0.004 ms.
- [x] Added synthetic KMS raw-input pointer-motion verification to `scripts/verify-frame-pacing-linux.sh`. Latest run: 180 synthetic pointer events, 180 hardware-cursor fast-path moves, zero fallback/unavailable moves, zero runtime failures, no pointer-triggered full redraw loops, and sampled CPU trace output without a new compositor coredump; atomic app smoke and Vulkan-display timestamp checks also passed.
- [x] Added static decorated-SHM surface cache verification to `scripts/verify-frame-pacing-linux.sh`. Latest run: 501 surface draw-cache hits, 1 miss, zero transient-chrome blocks, 524 rendered frames, CPU avg/max 7.80%/9.00%, surface avg/max 0.012/0.012 ms, present avg/max 0.219/0.232 ms.
- [x] Added server-side chrome hover/press verification to `scripts/verify-frame-pacing-linux.sh`. Latest run: synthetic close-button move, press, move-away, release, and completion events all observed; surface draw-cache reported 4 hits, 1 miss, zero transient-chrome blocks, CPU avg/max 1.40%/2.40%, and surface avg/max 0.276/0.710 ms.
- [x] Added scripted resize-storm verification to `scripts/verify-frame-pacing-linux.sh`. Latest run: 19 resize/configure events, 560 sizing cache-block samples, 565 rendered frames, zero fatal matches, CPU avg/max 12.85%/16.40%, surface avg/max 0.964/1.187 ms, and present avg/max 0.318/0.366 ms.
- [ ] Remaining local gap: no external pointer-driving tool (`ydotool`, `wtype`, or equivalent) was available, so real hardware input-driver and manual cursor visual validation still require a prepared host.
- [ ] Remaining environment gap: broad Wayland-display-dependent tests and real-app smoke cases need an already-running external Wayland session or manual app interaction.
- [ ] Remaining system-tool gap: `vulkan-validation-layers`, `weston`, `wayland-utils`, `ydotool`, and `evemu` are not installed and noninteractive `sudo` is unavailable in this session, so validation-layer and hardware input-driver verification still require a prepared host.
- [ ] Remaining platform gap: Metal source is not compiled on Linux; macOS compile/runtime verification is still required.

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
| Vulkan present phases | `LAMBDA_RESIZE_TRACE=1`, `vulkan-present-detail` trace (`VulkanCanvasLifecycle.inc:602-628`) | Per-phase ms: frame fence, acquire, atlas, backdrop prep, upload, record, submit, present |
| Compositor frame profile CSV | `recordFrame` in `CompositorRuntime.cpp:529-567` | input→render, render-ahead, snapshot/surface/present phase ms |
| Flip interval error | `intervalErrorMs` trace at `CompositorRuntime.cpp:1634` | Render-ahead lead accuracy vs real flips |
| Metal counters | `debug::perf` — `CanvasDrawableWait`, `CanvasPresent`, `DisplayLinkToPresent` | Drawable stalls vs present cost on macOS |
| Validation layers | `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` | Sync/lifetime regressions from any of these changes |

Suggested order: Phase 1 (FP-1 … FP-3) first — small, low-risk, removes the two verified hot-path stalls. Then Phase 2 (FP-4 … FP-6) for interactive smoothness, Phase 3 (FP-7 … FP-9) for steady-state cost, Phase 4 (FP-10 … FP-16) for pipeline depth and latent correctness.

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
- [ ] ASan run of capture-heavy tests is clean.

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

## Exit criteria

- [ ] All FP items above deleted as completed (each completion should also delete its section here).
- [ ] `vulkan-present-detail`, the compositor frame CSV, and `debug::perf` counters show: no `vkQueueWaitIdle`/`vkDeviceWaitIdle` on any per-frame path, atlas/idle/blur phases within budget, pointer motion produces no full frames.
- [ ] Remove this document and TODO-019 from `TODO.md`, and drop the row from `docs/roadmap.md`.
