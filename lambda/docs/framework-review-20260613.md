# Lambda (flux-v4) — framework review

Reviewed at HEAD `2590d63` (2026-06-13). Scope per request: the **framework** core under `src/` and `include/` — layout, rendering, and input paths — for correctness and performance. Apps, demos, and the compositor were not reviewed except where the framework touches them. Findings come from reading the end-state source, not commit history.

The repo is named `flux-v4`; the code is namespaced `lambda`. I use the in-tree names below.

---

## Summary

This is a mature, internally consistent codebase. The reactive core and the lifetime model are correct and well-built; the rendering pipeline is sophisticated (multi-tier display-list/raster caching, on-demand vsync-paced redraw, driver-aware Vulkan). The findings below are mostly **latent** — real defects that are currently masked by surrounding behavior, plus one performance issue that is live on any animated frame. Nothing here is a "the framework is broken" bug. In severity order:

1. **[Perf, live] `subtreeLocalVisualBounds` is uncached and recomputed super-linearly every redraw.** Rendering hot path.
2. **[Correctness, latent] Vulkan prepared-recorder buffers are destroyed synchronously** while potentially in flight — GPU use-after-free on non-RADV drivers. Not triggered on your RADV hardware.
3. **[Correctness, latent] `setBounds` ignores position-only changes for dirty marking,** unlike `setPosition`. Masked today.
4. **[Correctness, latent] `PointerMove` capture path calls the handler without copying it,** unlike every other dispatch path. Narrow re-entrancy/UAF window.
5. Smaller perf/robustness items (hit-test inverse, no flush iteration guard, eager computeds).

---

## Rendering path

### R1 — `subtreeLocalVisualBounds` is uncached; super-linear per redraw  *(performance, live)*

`src/SceneGraph/SceneBounds.hpp` — `subtreeLocalVisualBounds(node)` recursively walks the **entire** subtree (union of every descendant's transformed local bounds) on every call, with no memoization.

It is called from the renderer 2–3 times per group node per redraw:
- `prepareNodeCache` → `canReplayPreparedGroup(node)` computes it (`src/SceneGraph/SceneRenderer.cpp`).
- `renderNode` → `canReplayPreparedGroup(node)` again, plus a direct call for `visualBounds` quick-reject, plus the `rejectLocalBounds` line for groups.

Because it is invoked at *every group level* and each invocation walks that group's whole subtree, the aggregate cost across a nested tree is super-linear — up to O(N²) for deep group chains, and O(N · average-subtree-size) for typical wide/shallow trees. The prepared-group cache short-circuits the *replay*, but the *quick-reject bounds* needed to decide whether to replay are recomputed from scratch for every on-screen group, every frame.

This is dormant on a static UI (no redraw requested → `renderNode` not called), but it is on the hot path during **any** animation or scroll, when every requested frame re-walks the whole on-screen tree's group bounds.

**Fix:** memoize the subtree visual bounds on the node, invalidated by the existing dirty machinery. The node already carries `mutable` cache fields and a working `subtreeDirty_` flag (`src/SceneGraph/SceneNode.hpp`). A `mutable std::optional<Rect> cachedSubtreeBounds_`, cleared in `markSubtreeDirty()` and recomputed lazily, makes this O(1) amortized. This is the single highest-value change in the rendering path.

### R2 — Vulkan prepared-recorder buffers destroyed synchronously while possibly in flight  *(correctness, latent; not on your hardware)*

`VulkanFrameRecorder::clear()` (`src/Graphics/Vulkan/VulkanFrameRecorder.cpp`) destroys its GPU buffers and descriptor sets **synchronously** via `vmaDestroyBuffer` / `vkFreeDescriptorSets`. The canvas has a deferred-retirement system for its *own* resources (`retireDeferredResourcesAfterSubmit` → `destroyDeferredBuffers/…`, per-frame fences, `kMaxFramesInFlight`, `src/Graphics/Vulkan/VulkanGpuCanvasParts/VulkanImages.inc` / `VulkanCanvasLifecycle.inc`), but the recorder does **not** route through it.

On the prepared-geometry fast path, `appendRecordedOps` binds the recorder's own buffers/descriptors directly into the current frame's draw ops (`op.externalStorageDescriptor = recorded.preparedRectDescriptor`, `op.externalVertexBuffer = recorded.preparedPathVertexBuffer`, `src/Graphics/Vulkan/VulkanGpuCanvasParts/VulkanCanvasDrawOps.inc`). When a cached subtree later invalidates, the owning `SceneNode::preparedRenderOps_` is `reset()` during the *next* frame's `prepareNodeCache`/`renderNode`. `beginFrame` only waits on the fence of the slot being reused (frame N − `kMaxFramesInFlight`), not on frame N−1. So the previous in-flight frame can still reference buffers that `reset()` just destroyed → GPU-side use-after-free / validation errors / corruption.

**This does not affect your machine.** The fast path is gated off for RADV: `recorderPreparedGeometryFastPathEnabled()` returns `shared_->driverId != VK_DRIVER_ID_MESA_RADV` (`VulkanCanvasDrawOps.inc:937`), and `prepareRecorderBuffers` early-returns when the fast path is disabled, so on RADV (your Radeon Vega / Mesa) the recorder never creates GPU buffers and replay uses the CPU-copy path. The hazard is real only on Intel ANV, NVIDIA, and other non-RADV drivers — and on KMS builds on such GPUs.

**Fix:** funnel recorder buffer/descriptor destruction (both in `clear()` and the resize branch of `ensureRecorderBuffer`) through the canvas's existing deferred-retirement queue keyed on the frame fence, instead of destroying inline.

Note: the Metal path (`MetalCanvasPreparedRenderOps` holding a `MetalFrameRecorder` by value) is not exposed to this class of bug, because Metal command buffers retain their bound resources until completion. The asymmetry is inherent to Vulkan's manual lifetime model.

### R3 — `setBounds` ignores position-only changes; asymmetric with `setPosition`  *(correctness, latent)*

`src/SceneGraph/SceneNode.cpp:127` `setBounds` marks dirty **only when size changes**:

```cpp
bool const sizeChanged = bounds.width != bounds_.width || bounds.height != bounds_.height;
bounds_ = bounds;
if (sizeChanged) { markDirty(); }
```

A position-only `setBounds` updates `bounds_` but marks nothing. `setPosition` (`:139`) correctly calls `markSubtreeDirty()` on an x/y change. For the live render path this is harmless — `renderNode` reads `bounds()` fresh each frame and re-applies translation, and prepared *leaf* ops carry no baked position. But the **group prepared-cache** (`canReplayPreparedGroup`) bakes child positions into one display list keyed on `!subtreeDirty`. A position-only `setBounds` on a node inside such a group would leave `subtreeDirty` false and replay the child at its **old** position.

Currently masked: the only position-only `setBounds` reachable in practice is the scroll indicator (`src/UI/Views/ScrollView.cpp:137`, `setIndicatorBounds`), and it is saved because the sibling content group's `setPosition` (`ScrollView.cpp:283`/`374`/etc.) dirties the shared ancestor on every scroll. The other callers are resizes at a fixed origin (`PopoverCalloutShape.cpp:96`, `TextInput.cpp:215/234/856/857`). So no live bug — but the correctness of the indicator depends on a sibling's behavior, which is fragile, and any future position-only `setBounds` inside a cacheable group will render stale.

**Fix:** make `setBounds` mirror `setPosition` — detect the position delta and `markSubtreeDirty()` in addition to `markDirty()` on a size delta.

### R4 — Minor rendering notes

- **Per-redraw full re-traversal + GPU re-encode is by design.** `renderNode` walks the whole tree and re-submits every requested frame; the prepared-ops/group/raster caches reduce per-node CPU but not the traversal/encode itself. This is the correct trade for this architecture and is well mitigated by the on-demand, vsync-paced redraw (idle windows do zero work — `Application::presentRequestedWindows`, `requestWindowRedraw` coalescing). Flagging only so the cost model is explicit: R1 sits inside this per-frame walk, which is why it matters.
- **Group-cache opacity composition.** In the group replay path, `setOpacity(nodeOpacity)` is applied around a display list recorded with `inheritedOpacity = 1.f` and child opacities baked relative to 1.0 (`prepareSubtree`). This composes correctly only if the backend multiplies group opacity into the recorded per-op alpha at replay. The CPU-copy path's `opacityScale` in `translate*Instance` does this; worth confirming the same holds on the fast path before relying on nested-group opacity there.

---

## Input path

### I1 — `PointerMove` capture path doesn't copy the handler  *(correctness, latent)*

`src/UI/Runtime.cpp:957`, captured-pointer move:

```cpp
if (d->input.pressTarget->onPointerMove) {
  Point const local = localPointForTarget(*d->input.pressTarget, d->window, point).value_or(point);
  d->input.pressTarget->onPointerMove(local);   // called through the live optional
}
```

The handler is invoked directly through `d->input.pressTarget`. If that handler causes `pressTarget` to be reset (anything that runs `clearPointerTransientTargets`, an unmount, a focus change that tears down the target, etc.), the `SmallFn` currently executing is destroyed mid-call → use-after-free. The `PointerUp` path explicitly guards against exactly this, copying handlers out first with the comment at `:987` ("a handler may unmount the hit node, destroying the interaction (and the running closure) mid-call"). The captured `PointerMove` path is the one place that doesn't follow the rule.

**Fix:** copy the snapshot (or at least the `onPointerMove` SmallFn) to a local before invoking, matching the `PointerUp` pattern.

### I2 — Hit-test recurses non-clipping subtrees regardless of point; full Mat3 inverse per level  *(performance)*

`include/Lambda/SceneGraph/SceneTraversal.hpp` — `hitTestNode` recurses into a node's children **before** checking whether the point is inside the node's own bounds, and only clipping `RectNode`s gate their subtree. This is *correct* (non-clipping groups must let overflow content be hit), but it means a pointer over empty space can walk a large portion of the tree on every `PointerMove`. There's already a `gHitTestTraversalCountForTesting` counter, so the cost is on the radar.

Also, `pointInChildSpace` builds and inverts a full 3×3 matrix (`Mat3::translate(pos) * transform`, then `.inverse()`) at every level, even though the overwhelmingly common transform is translation-only (the codebase already has `Mat3::isTranslationOnly()` and uses it in the renderer).

**Fix (optional):** (a) fast-path translation-only transforms in `pointInChildSpace` to skip the general inverse; (b) reuse the (to-be-cached, see R1) subtree visual bounds to early-reject subtrees the point can't fall into when the node doesn't overflow. Neither is urgent; current hit-testing is correct.

### I3 — Input correctness notes (these are fine, recorded for completeness)

- Press-capture model, 8px tap slop, tap-vs-drag distinction, window resize/drag-edge short-circuiting before `onPointerDown`, scroll routed to the nearest `onScroll` ancestor, line-delta scaling for non-precise wheels — all correct and sensibly ordered.
- Keyboard precedence (Escape → Tab focus cycle → menu shortcut → command shortcut → focus-target handler → hit-test fallback) is reasonable. The hit-test fallback for unfocused key events uses the last pointer point, which is a slightly odd routing but rarely reached.
- Input coordinates are logical pixels end-to-end (Wayland delivers surface-logical via `logicalPointFromFixed`, the canvas applies DPI at the GPU level), so hit-testing matches layout. No logical/physical mismatch.

---

## Framework layout & reactive core

### F1 — Reactive graph is correct (strength)

`include/Lambda/Reactive/Detail/Core.hpp` is a proper two-color (Dirty/Pending) push-pull fine-grained reactive system in the Solid/reactively family: intrusive doubly-linked `Link`s in both the source's subscriber list and the observer's source list, per-`Computation` link pooling (`spareLinks`/`acquireLink`/`retireLink`), and dynamic dependency mark-and-sweep via `runVersion` (`beginTrackingRun`/`sweepStaleSources`). Diamonds collapse correctly (direct subscribers go Dirty, deeper nodes go Pending, `pollSourcesChanged` pulls). `Computed::get()` pulls fresh before returning (no glitches). Effects are depth-ordered (`scheduleEffect` upper_bound on `depth`), and the `flushEffects` re-entrancy guard with the outer drain loop is correct and well-commented (`:846`). The `staleDuringRun` check in `propagatePending`/`propagateDirty` correctly avoids marking edges that a running recompute is about to sweep. `SmallFn` (SBO functor) and `BindingFn` (192-byte inline budget) over-align to heap correctly. This is the strongest part of the codebase.

### F2 — Lifetime/teardown is correct (strength)

The Scope owner tree is the Solid model and is wired correctly: `MountContext::childWithOwnScope` (`src/UI/MountContext.cpp:337`) registers child-scope disposal as a parent `onCleanup`, so unmounting a component disposes the reactive effects/signals it created — no effect leak, which is the principal failure mode for reactive + retained scene graphs. `ScopeState::dispose` runs cleanups in reverse then disposes owned nodes in reverse. Good.

### F3 — Reactive robustness/perf notes (minor)

- **No iteration guard in `flushEffects`** (`Core.hpp:853`). An effect that writes a signal it depends on to a *new* value each pass loops forever with no dev-mode warning or cap. The `Signal::set` equality short-circuit catches the common "writes the same value" accident, but a monotonic self-write (e.g. `count.set(count.peek()+1)` in an effect) hangs the thread. Consider a debug-build iteration cap that logs the offending cycle, as Solid/MobX do.
- **Computeds evaluate eagerly on creation and subscribe permanently.** `ComputedState` runs `recompute()` in its constructor, so a `Computed` created but never read still costs one evaluation and stays subscribed to its sources (it will keep receiving Dirty/Pending marks, though it won't recompute until read). For view-body computeds that are almost always read this is fine; just noting it differs from lazy memos.
- **Global atomic link counters** (`gLiveLinks`/`gTotalLinks`) increment on every `allocateLink`/`freeLink`. With pooling these are off the steady-state hot path; negligible, but they're debug-only state paid for in release builds.

### F4 — Layout structure

Clean layering: `Reactive` → `SceneGraph` (pure retained tree) → `UI` (Element/Component/Views/mount), with `Graphics` backends (Metal, Vulkan, KMS) behind a `Canvas` interface and platform input behind `Application`/`Window`. The single retained post-layout `SceneGraph` with `ComponentKey`-addressable geometry, two-phase relayout with stored-constraint skipping (`SceneNode::relayout` + `sameLayoutConstraints` epsilon, `src/SceneGraph/SceneNode.cpp`), and the transient-relayout depth guard are coherent. No layering violations spotted in the core.

---

## What's good

- **Correct fine-grained reactive engine** (F1) and **correct hierarchical lifetime/teardown** (F2) — the two things most frameworks in this space get subtly wrong.
- **On-demand, coalesced, vsync-paced redraw** — idle windows do literally no work; this is the right model and is implemented cleanly.
- **Genuinely sophisticated render caching** — prepared display lists per leaf, whole-group display-list capture with clip/raster-cache/transform guards, and a raster-cache node tier, all keyed off a working dirty/subtree-dirty split.
- **Driver-aware Vulkan** — the RADV fast-path exclusion, fractional-scaling support, glyph-atlas-generation invalidation on replay, and a real deferred-retirement system show hard-won platform knowledge.
- **Defensive input dispatch** — handler-copy-before-dispatch (mostly, see I1), press capture with tap slop, and clean window-drag/resize hand-off.
- **Hit-testing is correct** — reverse-order DFS, clip-gated subtrees, proper child-space transforms, rounded-rect containment.
- **Large, targeted test suite** (~72 test files covering runtime input, mount, Vulkan render targets, and the compositor) and consistent style discipline (`.clang-format`, `.editorconfig`).

## Lower-priority / housekeeping

- **No `-Wall -Wextra` (let alone `-Werror`)** in `CMakeLists.txt` / `cmake/`; only opt-in ASan (`cmake/LambdaApp.cmake:181`). For a codebase this disciplined, turning these on (even non-fatal) is cheap insurance.
- The `prepare()`/`prepareSubtree()` Vulkan/Metal blocks in `SceneRenderer.cpp` share a fair amount of structure behind `#if LAMBDA_METAL` / `#if LAMBDA_VULKAN`; not a bug, but a candidate for a small backend-agnostic capture helper if it keeps growing.

---

### Suggested order of attack

1. **R1** (cache subtree bounds) — only one with a live, measurable cost; smallest change-to-impact ratio.
2. **R3** + **I1** — trivial, close two latent correctness gaps with a few lines each.
3. **R2** — only if/when you target a non-RADV GPU; route recorder destruction through the existing deferred queue.
4. **F3** flush guard — debug-build only, cheap safety net.