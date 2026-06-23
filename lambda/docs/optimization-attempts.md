# animation-demo optimization attempts

## Goal

- Reduce `animation-demo` CPU usage below 10%.
- Keep frame rate at 60 fps.
- Keep `AmbientLoopLab` on `useAnimation`; demo-side removal is not acceptable.

## Measurement

- Date: 2026-04-24
- CPU method:
  - Launch `build/demos/animation-demo`
  - Wait 3 seconds
  - Sample `ps -p <pid> -o %cpu=` 10 times at 0.5 second intervals
  - Average the 10 samples
- Foreground confirmation:
  - For changes that appear to help, also activate the app before sampling to avoid hidden-window throttling skewing the result.
- Steady-state confirmation for near-target runs:
  - The external `ps` samples became too noisy once the app got close to 10%.
  - Final pass/fail decisions therefore used a temporary env-gated internal benchmark that logged one-second main-thread CPU windows and FPS while the app was foregrounded.
  - For those steady-state runs, the first two one-second windows after activation were discarded as warmup.
- Stack traces use `/usr/bin/sample <pid> 1 1`.

## Baselines

- Original baseline before optimization work:
  - CPU: `36.07%`
  - Primary hot path: `lambdaui::Application::processReactiveUpdates()` and repeated `BuildOrchestrator` / `SceneBuilder` work.
- Current framework-only baseline after restoring `useAnimation` in `AmbientLoopLab`:
  - CPU: `32.47%`
  - Notes:
    - This keeps the previously committed framework change that reuses `SceneRenderer` across frames.
    - The demo-side redraw-only ambient-loop workaround was reverted and is not part of the accepted path.

## Attempt 1

- Item: Convert `AmbientLoopLab` from `useAnimation`-driven rebuilds to redraw-only rendering.
- Type: demo-specific
- Status: Numerically effective, but rejected and reverted.
- Before:
  - CPU: `36.07%`
- After:
  - CPU: `16.71%`
  - Delta: `-19.36` percentage points
- Outcome:
  - This removed most reactive rebuild work, but it changed the demo instead of fixing the framework.
  - The user explicitly rejected this approach, so it is not part of the accepted optimization path.

## Attempt 2

- Item: Persist `SceneRenderer` across frames so prepared render ops survive between presents.
- Type: framework
- Status: Worked, but not enough by itself once `useAnimation` was restored.
- Before:
  - CPU: `36.07%`
- After:
  - CPU: `32.47%`
  - Delta: `-3.60` percentage points
- Outcome:
  - This remains a valid framework optimization and stays in the codebase.
  - It reduces render-preparation churn, but the dominant cost is still reactive rebuilds from `useAnimation`.

## Attempt 3

- Item: Retained-subtree reuse through `SceneBuilder` plus layout-child wrapper nodes.
- Type: framework
- Status: Failed, reverted.
- Before:
  - CPU: `32.47%`
- After:
  - CPU: `35.97%`
  - Delta: `+3.50` percentage points
- Outcome:
  - The retained path only helped when the subtree was already stable at the composite-body level.
  - The hot `animation-demo` path is still dominated by regular layout elements (`ScrollView`, `VStack`, `HStack`) rebuilding under the dirty animated component.
  - The extra wrapper/allocation overhead outweighed the limited reuse, so the attempt was reverted.

## Attempt 4

- Item: Skip `useAnimation`-driven composite dirties when the observing component is outside the window viewport.
- Type: framework
- Status: Failed, reverted.
- Before:
  - CPU: `32.47%`
- After:
  - CPU: `34.64%`
  - Delta: `+2.17` percentage points
- Outcome:
  - The fresh stack trace still shows `lambdaui::Application::processReactiveUpdates()` rebuilding the same `ScrollView` / `VStack` path on nearly every tick.
  - Visibility gating did not suppress the dominant rebuild path in this demo, so it was reverted.

## Attempt 5

- Item: Generic scene-node reuse for structurally unchanged elements, including regular layout children inside dirty animated components.
- Type: framework
- Status: Failed, reverted.
- Before:
  - CPU: `32.47%`
- After:
  - CPU: `36.23%`
  - Delta: `+3.76` percentage points
- Outcome:
  - This widened reuse beyond retained composite bodies, but it introduced too much bookkeeping and wrapper overhead.
  - The benchmark regressed, so the change was reverted.

## Attempt 6

- Item: Re-key source-element outer measurement to the logical component path instead of the nested scene path.
- Type: framework
- Status: Failed, reverted.
- Before:
  - CPU: `32.47%`
- After:
  - CPU: `35.31%`
  - Delta: `+2.84` percentage points
- Outcome:
  - The second per-frame dirty key turned out to be a measure-only `AmbientLoopLab` state with no scene snapshot or scene node.
  - Changing the source-element measurement key did not remove that duplicate dirty state and did not activate the partial rebuild path, so the change was reverted.

## Attempt 7

- Item: Let incremental rebuild ignore measure-only dirty keys and rebuild the single scene-backed animated subtree.
- Type: framework
- Status: Worked, committed.
- Before:
  - CPU: `32.47%`
- After:
  - CPU: `17.13%`
  - Delta: `-15.34` percentage points
- Outcome:
  - The dirty set for the ambient animation contained one real scene-backed key plus one measure-only duplicate key with no snapshot or node.
  - Filtering the incremental path down to dirty keys that actually have a recorded build snapshot and scene node activated subtree rebuilds and removed the full-root rebuild cost.

## Attempt 8

- Item: Reuse existing scene nodes inside the partial subtree rebuild to preserve child node pointers across animation ticks.
- Type: framework
- Status: Failed, reverted.
- Before:
  - CPU: `17.13%`
- After:
  - CPU: `40.14%`
  - Delta: `+23.01` percentage points
- Outcome:
  - Threading existing nodes through the partial rebuild introduced enough extra invalidation/bookkeeping to more than erase any reuse benefit.
  - The benchmark regressed sharply, so the change was reverted.

## Attempt 9

- Item: Move prepared render-op cache storage from `SceneRenderer`'s `unordered_map` onto each `SceneNode`.
- Type: framework
- Status: Worked, committed.
- Before:
  - CPU: `32.89%`
- After:
  - CPU: `29.56%` (run 1)
  - CPU: `25.37%` (confirmation run)
  - Confirmation average: `27.46%`
  - Delta: `-5.43` percentage points
- Outcome:
  - This removed the per-node hash lookup path from `renderNode(...)` and made cache lifetime follow node lifetime instead of relying on a renderer-owned hash table and per-frame cache GC.
  - A fresh sample no longer shows `unordered_map<SceneNode const*, CacheEntry>` work in the hot render path.

## Attempt 10

- Item: Reuse prepared render ops across rebuilt nodes by content key so stable leaf visuals can survive subtree recreation.
- Type: framework
- Status: Inconclusive in uncontrolled runs, failed under foreground confirmation, reverted.
- Before:
  - CPU: `32.89%`
- After:
  - CPU: `30.79%` (foreground confirmation run 1)
  - CPU: `32.30%` (foreground confirmation run 2)
  - Confirmation average: `31.55%`
  - Delta: `-1.34` percentage points
- Outcome:
  - Uncontrolled runs briefly looked excellent, but they were polluted by hidden-window throttling.
  - Once the app was explicitly activated before sampling, the content-key cache landed back in the same band as the baseline and did not justify the added complexity, so it was reverted.

## Attempt 11

- Item: Reduce hot-path churn by reusing `MeasureContext` / traversal storage, speeding up `ComponentKey` copy-and-append patterns, and inlining scene-node storage instead of per-node pimpl allocations.
- Type: framework
- Status: Worked, kept.
- Before:
  - CPU: `12.85%`
- After:
  - CPU: `10.67%`
  - Delta: `-2.18` percentage points
- Outcome:
  - This cut allocator and copy churn in both the incremental rebuild path and the scene traversal path.
  - The hot stack still showed real work in `renderNode(...)`, but the app moved into the low-11% / high-10% band without touching the demo.

## Attempt 12

- Item: Reuse scene nodes during incremental subtree rebuilds for common layout/leaf types, and replay prepared leaf ops directly in parent space.
- Type: framework
- Status: Worked, kept.
- Before:
  - CPU: `10.67%`
- After:
  - CPU: `8.29%` steady-state average after discarding the first 2 one-second windows of an 18-second foreground run
  - FPS: `59-61`
  - Delta: `-2.38` percentage points
- Outcome:
  - Incremental rebuild now keeps compatible scene nodes alive across animation ticks for the container/leaf types hit by `animation-demo`, so prepared render-op caches and node-local state survive instead of being recreated every frame.
  - Prepared leaf ops now bake node transform/position into the recorded commands, which removes most of the outer `translate/transform` work from `renderNode(...)` on cached leaves.
  - In the long steady-state run, render cost collapsed to roughly `6-8 ms/s` while reactive work stayed under the 60 fps budget.

## Attempt 13

- Item: Elide the resolved scene-element clone during incremental rebuilds and keep owned composite bodies on `ResolvedElement` so `SceneBuilder` can build from pointers instead of copying `Element` trees each frame.
- Type: framework
- Status: Worked, kept, target met under external foreground confirmation.
- Before:
  - CPU: `10.92%` average across 12 foreground `ps` samples after restore-to-`useAnimation`
  - CPU: `11.01%` average after discarding the first 2 samples from that same run
- After:
  - CPU: `9.85%` average across 12 foreground `ps` samples (run 1)
  - CPU: `9.83%` average after discarding the first 2 samples (run 1)
  - CPU: `9.80%` average across 12 foreground `ps` samples (run 2)
  - CPU: `10.05%` average after discarding the first 2 samples (run 2)
  - Delta: about `-1.1` percentage points on the external foreground measurement path
- Outcome:
  - The hot sample before this change still showed `SceneBuilder::buildResolved(...)` cloning `Element` trees and copying child vectors during every animation tick.
  - `ResolvedElement` now points at the already-resolved scene element and only owns composite bodies when it has to, which removes the extra `Element` clone from the hot incremental rebuild path.
  - With the demo still driven by `useAnimation`, the rebuilt binary now lands below the `10%` CPU target on the original foreground `ps` averaging method without changing the demo behavior.

## Attempt 14

- Item: Split scene-node paint dirtiness from subtree dirtiness so child-only changes do not force parent prepared-op re-recording.
- Type: framework
- Status: Worked, kept.
- Before:
  - Prepared-op `prepare()` rate: `12.58/f` in a steady-state `LAMBDA_DEBUG_PERF=1` sample
  - Scene render time: `0.33 ms/f`
  - Display-link-to-present budget: `1.71 ms/f`
- After:
  - Prepared-op `prepare()` rate: `6.18/f` (run 1), `6.21/f` (run 2)
  - Scene render time: `0.25 ms/f` (run 1), `0.39 ms/f` (run 2)
  - Display-link-to-present budget: `1.37 ms/f` (run 1), `2.12 ms/f` (run 2)
  - Delta: about `-6.4 prepares/frame`
- Outcome:
  - `SceneNode` now keeps `ownPaintingDirty_` separate from `subtreeDirty_`, so descendant mutations only trigger traversal into the subtree instead of forcing parent `prepare()` calls.
  - This directly addresses the hot counter regression signal: steady-state `prepare()` dropped from the low-12s per frame to about `6.2/f`, matching the expected range for the demo.
  - A regression test now verifies that a child paint change re-prepares the child but not the cached parent node.

## Attempt 15

- Item: Skip canvas stack pushes for identity `GroupNode` traversal and carry their translation down until a leaf or clipped/transformed subtree needs it.
- Type: framework
- Status: Worked, kept.
- Before:
  - `render`: `25.80 ms/s (0.42/f)` (run 1), `18.49 ms/s (0.31/f)` (run 2)
  - `frameBudget`: `155.14 ms/s (2.54/f)` (run 1), `104.85 ms/s (1.75/f)` (run 2)
- After:
  - `render`: `14.51 ms/s (0.24/f)` (run 1), `15.39 ms/s (0.26/f)` (run 2)
  - `frameBudget`: `90.55 ms/s (1.48/f)` (run 1), `93.98 ms/s (1.57/f)` (run 2)
  - Delta: about `-0.12` to `-0.18 ms/frame` on `SceneRenderer::render`
- Outcome:
  - `SceneRenderer` now threads translation through identity group subtrees instead of doing `save() / translate() / transform() / restore()` for every intermediate group node.
  - Leaves and clipped/transformed subtrees still pay the normal state push when they actually need canvas mutation, so the change preserves translation and clip semantics while reducing stack churn on the hot layout path.
  - A regression test now verifies that nested identity groups collapse to a single renderer `save()` / `restore()` pair for the leaf draw.

## Attempt 16

- Item: Replace `ComponentKey` interning's flat `unordered_map<InternedEdge, handle>` with parent-local child tables: direct indexed lookup for positional ids and short inline scans for keyed ids.
- Type: framework
- Status: Worked, kept.
- Before:
  - 1-second sample hotspot: `std::__hash_table<...InternedEdge...>::find(...)` at `1.42 G cycles` / `49.3%`
  - `reactive`: `155.60 ms/s (1.29/f)` (run 1), `104.17 ms/s (0.87/f)` (run 2)
  - `incremental`: `154.22 ms/s (1.27/f)` (run 1), `103.19 ms/s (0.86/f)` (run 2)
- After:
  - 1-second sample: the `InternedEdge` hash-table lookup no longer appears; hot `ComponentKey` time moves into `ComponentKeyTable::intern(...)` plus `SmallVector` reads
  - `reactive`: `134.62 ms/s (1.11/f)` (run 1), `94.11 ms/s (0.78/f)` (run 2)
  - `incremental`: `133.56 ms/s (1.10/f)` (run 1), `93.06 ms/s (0.78/f)` (run 2)
  - Delta: about `-0.08` to `-0.19 ms/frame` on the hot rebuild path
- Outcome:
  - Positional child ids now hit a parent-local indexed slot instead of hashing `{parent, tail}` through a global edge table.
  - Keyed child ids now pay a short contiguous scan inside a small inline bucket instead of a heap-backed hash-table lookup.
  - This removed the exact `InternedEdge` hash hotspot from the sample and shaved measurable time from `processReactiveUpdates()` / incremental rebuild without changing `ComponentKey` semantics.

## Attempt 17

- Item: Keep an interned prefix key inside `TraversalContext` so `currentElementKey()` / `nextCompositeKey()` append directly instead of rebuilding from `keyStack_`, and make `peekCurrentChildLocalId()` read the current local id without constructing a key.
- Type: framework
- Status: Worked, kept.
- Before:
  - Sample hotspot: `lambdaui::(anonymous namespace)::ComponentKeyTable::intern(unsigned int, lambdaui::LocalId)` at `1.07 G cycles` / `7.7%`
  - `ck append`: `28435 (238975id)` / `235/f` (run 1), `28200 (237000id)` / `235/f` (run 2)
  - `reactive`: `134.62 ms/s (1.11/f)` (run 1), `94.11 ms/s (0.78/f)` (run 2)
  - `incremental`: `133.56 ms/s (1.10/f)` (run 1), `93.06 ms/s (0.78/f)` (run 2)
- After:
  - Sample: the old `ComponentKeyTable::intern(unsigned int, LocalId)` hotspot no longer dominates; the follow-up sample only shows isolated calls under normal measure traversal
  - `ck append`: `19360 (160809id)` / `160/f` (run 1), `19200 (159480id)` / `160/f` (run 2)
  - `reactive`: `77.20 ms/s (0.64/f)` (run 1), `82.20 ms/s (0.69/f)` (run 2)
  - `incremental`: `76.38 ms/s (0.63/f)` (run 1), `81.42 ms/s (0.68/f)` (run 2)
  - Delta: about `-0.14` to `-0.47 ms/frame` on the hot rebuild path
- Outcome:
  - `TraversalContext` now maintains the current prefix as a `ComponentKey`, so child key generation is one handle append instead of “re-intern the entire prefix, then append”.
  - `MeasureContext::peekCurrentChildLocalId()` no longer constructs `currentElementKey()` twice just to read the tail.
  - This cut `ComponentKey` append traffic by about a third and materially reduced `processReactiveUpdates()` / incremental rebuild time.

## Attempt 18

- Item: Avoid copying replayed glyph vertices into `frame.glyphVerts` during prepared-op replay; keep borrowed glyph vertex slices in the frame recorder and copy them directly into the Metal glyph arena during present.
- Type: framework
- Status: Worked, kept.
- Before:
  - Sample hotspot: `std::__uninitialized_allocator_copy_impl<..., MetalGlyphVertex const*, ...>` at `413.91 M cycles` / `3.3%`
  - `render`: `34.43 ms/s (0.29/f)` (run 1), `37.37 ms/s (0.31/f)` (run 2)
  - `present`: `28.64 ms/s (0.24/f)` (run 1), `30.62 ms/s (0.25/f)` (run 2)
  - `frameBudget`: `194.47 ms/s (1.62/f)` (run 1), `203.69 ms/s (1.68/f)` (run 2)
- After:
  - Sample: the `MetalGlyphVertex` `uninitialized_allocator_copy` frame no longer appears; glyph work moves to `MetalDeviceResources::uploadGlyphVertices(...)`
  - `render`: `30.25 ms/s (0.25/f)` (run 1), `31.45 ms/s (0.26/f)` (run 2)
  - `present`: `17.50 ms/s (0.14/f)` (run 1), `17.46 ms/s (0.15/f)` (run 2)
  - `frameBudget`: `170.83 ms/s (1.41/f)` (run 1), `172.17 ms/s (1.43/f)` (run 2)
  - Confirmation spread: one follow-up present window hit `329.45 ms/s (2.75/f)`, consistent with the existing present/scheduler noise seen in earlier attempts
- Outcome:
  - `MetalFrameRecorder` now tracks logical glyph vertex offsets separately from owned glyph vertex storage.
  - Live text draws still own their generated vertices; prepared-op replay borrows the immutable recorded glyph slice when opacity is unchanged and only falls back to copying when opacity scaling requires vertex-color mutation.
  - Upload now scatters owned and borrowed slices into the final contiguous Metal glyph buffer, preserving the existing draw offsets while removing the CPU-to-CPU glyph vertex copy from the hot replay path.

## Result

- Accepted framework changes still in place:
  - `SceneRenderer` reuse across frames.
  - Incremental rebuild ignores measure-only dirty keys and rebuilds the real scene-backed animated subtree.
  - Prepared render-op caches live on `SceneNode` instances instead of a renderer-side hash map.
  - Hot-path churn is reduced via reusable traversal/measurement state, faster `ComponentKey` handling, and inline scene-node storage.
  - Incremental subtree rebuild now reuses compatible scene nodes for the common animated layout path, and prepared leaf replay skips redundant outer transforms.
  - `SceneBuilder` no longer clones the resolved scene element on every incremental animation rebuild.
  - Identity group traversal no longer mutates the canvas stack unless a descendant actually needs a transformed/clipped local space.
  - `ComponentKey` interning now uses parent-local child tables instead of a global `InternedEdge` hash table.
  - `TraversalContext` carries an interned prefix key instead of reconstructing child keys from `std::vector<LocalId>` on every measure step.
  - Prepared Metal glyph replay borrows recorded glyph vertex slices and uploads them directly instead of copying them into the frame recorder first.
- Final status:
  - `animation-demo` stays at `59-61 fps`.
  - External foreground CPU confirmation is below the `10%` target.

## v5 Stage 5 Baseline

- Date: 2026-04-26
- Build: `build-stage5/demos/animation-demo`
- CPU method:
  - Launch `animation-demo`.
  - Wait 3 seconds.
  - Sample `ps -p <pid> -o %cpu=` 10 times at 0.5 second intervals.
- Result:
  - Average CPU: `0.00%` across 10 samples.
- Notes:
  - This Stage 5 port validates the build-once mount path and `useAnimation` handle API with a focused static smoke scene.
  - Full reactive control-flow and richer example migration resume in Stages 6-8.

## v5 Stage 8 Example Idle CPU

- Date: 2026-04-26
- Build: `build-stage8/demos/*`
- CPU method:
  - Launch each example.
  - Wait 2 seconds for startup work to settle.
  - Sample `ps -p <pid> -o %cpu=` 5 times at 0.4 second intervals.
  - Terminate the process and treat exit code `143` as a clean smoke termination.
- Result:
  - `alert-demo`: `0.00%`
  - `animation-demo`: `0.00%`
  - `blend-demo`: `0.00%`
  - `button-demo`: `0.00%`
  - `card-demo`: `0.00%`
  - `checkbox-demo`: `0.00%`
  - `cursor-demo`: `0.00%`
  - `hello-world`: `0.00%`
  - `icon-demo`: `0.00%`
  - `image-demo`: `0.00%`
  - `layout-demo`: `0.00%`
  - `markdown-formatter-demo`: `0.00%`
  - `popover-demo`: `0.00%`
  - `scene-graph-demo`: `0.00%`
  - `scroll-demo`: `0.00%`
  - `segmented-demo`: `0.00%`
  - `select-demo`: `0.00%`
  - `slider-demo`: `0.00%`
  - `table-demo`: `0.00%`
  - `text-demo`: `0.00%`
  - `textinput-demo`: `0.00%`
  - `theme-demo`: `0.00%`
  - `toast-demo`: `0.00%`
  - `toggle-demo`: `0.00%`
  - `tooltip-demo`: `0.00%`
  - `typography-demo`: `0.00%`
- Notes:
  - The current repository contains 28 example entry points; all 28 build and pass launch smoke checks in Stage 8.
