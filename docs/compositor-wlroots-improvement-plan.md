# Compositor wlroots improvement plan

**Last updated:** 2026-05-31
**Status:** Active implementation plan.
**Scope:** Compare Lambda Window Manager protocol and compositor workflows against wlroots, then implement the highest-value fixes in priority order without importing wlroots as a dependency.

## Operating Rules

- Work from the highest priority item to the lowest priority item.
- Update this document before and after each implementation step so it remains the source of truth.
- Run targeted automated tests for each change. Run the broader feasible test set when a change touches shared compositor behavior.
- If automated tests fully verify the change, commit and push that change before moving to the next step.
- If a change needs target-hardware or visual validation, mark the item as waiting for user validation, state the exact manual scenario, and stop until the result is known.
- Keep unrelated UI, Shell, app, and refactor work out of these commits unless it is required by the compositor fix being implemented.

## Priority Table

| Priority | Workstream | Status | Current step | Automated gate | Manual gate |
| --- | --- | --- | --- | --- | --- |
| P0 | WM-COMP-1 Surface commit state core | Deferred | Manual validation still shows momentary non-synced Settings resize frames on DP-1 HiDPI; failed buffer-retention experiment was dropped | Existing committed state-transition tests pass | Deferred by user request before deeper rendering-architecture work |
| P1 | WM-COMP-2 Layer shell configure and state correctness | Verified | Implemented pending/current layer state, configure serial validation, mapped/unmapped state, initial commit enforcement, and layer-shell v4 binding | `lambda_tests` layer/compositor suites and broader feasible suite pass | No manual gate needed for this protocol-state slice |
| P2 | WM-COMP-3 Subsurface state, order, and synchronized commits | Verified | Implemented pending/current position, committed sibling ordering, synchronized commit caching, and desync cache release | `lambda_tests` subsurface/compositor suites and broader feasible suite pass | No manual gate needed for this protocol-state slice |
| P3 | WM-COMP-4 Scene and output damage architecture | Planned | Starts after WM-COMP-3 commit is pushed | Snapshot/damage tests plus render scheduler tests | DP-1 resize trace, real-app flicker check, video/browser pacing |
| P4 | WM-COMP-5 Seat serials and grab model | Planned | Starts after WM-COMP-4 reaches its gate | Seat serial, popup grab, selection, and focus tests | Popups, drag/drop, clipboard, and menu behavior in real apps |
| P5 | WM-COMP-6 DMABUF feedback and buffer lifetime | Planned | Starts after WM-COMP-5 reaches its gate | DMABUF validation and buffer release tests where possible | GPU-client/video validation on target hardware |

## WM-COMP-1 Surface Commit State Core

**Why this is first:** wlroots keeps protocol mutations in pending surface state and applies them atomically on `wl_surface.commit`. Lambda has already moved xdg configure acknowledgement closer to that model, but core surface state is still spread across direct fields and role-specific helpers. That makes resize synchronization harder to reason about and leaves room for titlebar/content mismatches when one part of the frame observes newer state than another.

**Goal:** introduce a single explicit surface commit path where pending protocol state moves to current state as one transaction, and where role state used for window geometry, compositor chrome, frame callbacks, and snapshots is derived from committed state.

**Expected code areas:**

- `apps/lambda-window-manager/Compositor/Wayland/WaylandServerImpl.hpp`
- `apps/lambda-window-manager/Compositor/Wayland/Globals/Core.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/Globals/XdgShell.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/Globals/LayerShell.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/SubsurfaceState.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/Snapshots.cpp`
- `tests/`

**Implementation steps:**

1. Done: inventory existing pending/current surface fields and update this plan with the exact migration order.
2. Done: add a small state object or transaction helper for core `wl_surface` commit data without changing behavior.
3. Done: move buffer, scale, transform, offset, damage, opaque region, input region, viewport source, and viewport destination into the explicit pending/current path.
4. Done: move role-synchronized xdg state reads so snapshots and compositor chrome consume one committed view of a surface.
5. Partially done: added automated tests for pending/current viewport, region, damage, buffer, and xdg geometry consumers. Frame callback and configure-ack coverage remains a follow-up if manual validation finds a related regression.
6. Done: run targeted compositor tests and the feasible full test suite.
7. Validation failed: user saw momentary non-synced frames without flicker during Settings resize on DP-1 HiDPI.
8. Deferred: a frame-aware buffer-retention experiment was tested and did not remove the momentary non-synced frames, so that uncommitted patch was dropped.
9. Deferred by user request: move on to WM-COMP-2 and return later for deeper rendering-architecture work.

**Step 1 inventory:**

- Core `wl_surface` commit handling already enters through `surfaceCommit` in `Globals/Core.cpp`, with helpers for surface protocol state, viewport state, background effect state, xdg role state, configure state, damage, and buffer refresh.
- At inventory time, current and pending fields were stored directly on `WaylandServer::Impl::Surface`: `currentBuffer`/`pendingBuffer`, scale, transform, attach offset, viewport source/destination, opaque/input regions, and pending damage.
- Snapshot and hit-test paths read committed fields directly from `Surface`, especially in `Snapshots.cpp` and `WindowManagerInternal.hpp`.
- Resize-sensitive frame/chrome geometry is driven by `frameWidth`/`frameHeight`, xdg configure state, xdg window geometry, and committed viewport/buffer dimensions. The migration must make those consumers observe one committed transaction rather than independently updated field groups.
- Migration order: first add a testable commit-state helper around the existing fields, then migrate viewport state, then regions and damage, then buffer metadata and buffer attachment, then xdg role-synchronized state.

**Acceptance criteria:**

- Pending protocol requests do not affect rendering, hit testing, chrome geometry, or frame callbacks before `wl_surface.commit`.
- A surface commit exposes one coherent committed state to snapshots, frame scheduling, xdg role logic, and compositor chrome.
- The system-titlebar width and the content width cannot diverge because different parts of the compositor read different resize states.
- Existing resize and configure tests continue to pass.
- New automated tests cover the state transitions that previously depended on manual resize observations.

## WM-COMP-2 Layer Shell Configure and State Correctness

**Why this matters:** wlroots treats layer-shell configuration like xdg-shell: configure serials are tracked, acknowledgements are validated, and pending layer properties become effective on commit. Lambda's dock and topbar depend on layer-shell correctness, so this is the next protocol-level stability step.

**Goal:** make layer-shell configure, acknowledgement, and committed geometry semantics explicit and testable.

**Expected code areas:**

- `apps/lambda-window-manager/Compositor/Wayland/Globals/LayerShell.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/WaylandServerImpl.hpp`
- `apps/lambda-window-manager/Compositor/Wayland/LayerShellState.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/LayerShellZones.cpp`
- `tests/`

**Implementation steps:**

1. Done: added layer configure serial tracking and `ack_configure` validation.
2. Done: moved `set_size`, `set_anchor`, `set_margin`, `set_exclusive_zone`, `set_keyboard_interactivity`, and `set_layer` into pending layer state.
3. Done: apply pending layer state on `wl_surface.commit`; initial buffer commits before configure acknowledgement are rejected.
4. Done: recompute exclusive zones and layer placement from committed state.
5. Done: track mapped/unmapped state; null-buffer unmap clears configure state and reserved zones ignore unmapped layer surfaces.
6. Done: updated the local wlr-layer-shell XML to v4, including `on_demand` keyboard interactivity and `set_layer` v2, and bind server/client resources up to v4.
7. Done: added tests for invalid serials, pending property changes, omitted dimensions without opposing anchors, configure acknowledgement state, and map/unmap reset behavior.
8. Done: build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*layer*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, and `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"` passed.

**Acceptance criteria:**

- Invalid layer-shell ack serials are rejected.
- Layer placement and exclusive zones update from committed state, not request-time state.
- Layer-shell geometry, reserved-zone, configure, and map/unmap tests pass without relying on timing side effects.

## WM-COMP-3 Subsurface State, Order, and Synchronized Commits

**Why this matters:** wlroots' subsurface implementation keeps parent and child state coherent through synchronized commit rules. Missing or partial subsurface semantics can cause popover, embedded view, and hit-test bugs in real clients.

**Goal:** implement wl_subsurface pending/current position, stacking order, and sync/desync commit behavior.

**Expected code areas:**

- `apps/lambda-window-manager/Compositor/Wayland/Globals/Core.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/Snapshots.cpp`
- `apps/lambda-window-manager/Compositor/Window/PointerRouter.cpp`
- `tests/`

**Implementation steps:**

1. Done: inventory current subsurface support and missing protocol requests. Current position mutates live state immediately, `place_above`/`place_below` are no-ops, `set_sync`/`set_desync` are no-ops, and snapshots/input traverse creation order.
2. Done: added pending/current position and sibling order state.
3. Done: implemented `place_above` and `place_below` against pending sibling order and wired `set_sync`/`set_desync` to role state.
4. Done: cache synchronized subsurface commits until the parent commits; `set_desync` releases cached commits immediately when no synchronized ancestor remains.
5. Done: updated snapshots and hit testing to use committed subsurface order.
6. Done: added tests for pending position, pending sibling placement, committed ordering, effective synchronization through ancestors, synchronized commit caching, and existing coordinate hit testing.
7. Done: moved frame callbacks into pending commit state so synchronized commits cannot emit callbacks before release.
8. Done: build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*subsurface*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, and `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"` passed.

**Acceptance criteria:**

- Synchronized subsurface state is not visible before the parent commit.
- Desynchronized subsurfaces commit immediately.
- Snapshot traversal and input routing agree on committed subsurface order.

## WM-COMP-4 Scene and Output Damage Architecture

**Why this matters:** wlroots uses a scene/output-damage model so rendering decisions are driven by persistent committed state and dirty regions. Lambda has snapshot rendering and damage plumbing, but resize performance and flicker sensitivity still need a stronger architecture.

**Goal:** create a persistent compositor scene/damage path that redraws only the regions affected by committed surface, chrome, cursor, and layer changes while keeping full-frame fallback behavior correct.

**Expected code areas:**

- `apps/lambda-window-manager/Compositor/Wayland/Snapshots.cpp`
- `apps/lambda-window-manager/Compositor/CompositorRenderFrame.cpp`
- `apps/lambda-window-manager/Compositor/Surface/SurfaceRenderer.cpp`
- `apps/lambda-window-manager/Compositor/FrameScheduler.cpp`
- KMS presenter code under `apps/lambda-window-manager/Compositor/`
- `tests/`

**Implementation steps:**

1. Inventory current damage sources and full-redraw fallbacks.
2. Add explicit dirty-region propagation for surface commits, layer geometry, chrome changes, cursor movement, and output changes.
3. Make frame scheduling consume damage information and avoid redraws when no visible committed state changed.
4. Ensure frame callbacks are sent only when a committed visible surface participates in the frame.
5. Add tests for damage propagation, full-redraw fallback, invisible-surface callback behavior, and resize damage.
6. Run target-hardware resize traces before considering this complete.

**Acceptance criteria:**

- Resize no longer forces unnecessary full redraws when bounded damage is sufficient.
- Damage tracking is conservative: correctness wins over missed redraws.
- DP-1 resize traces show improvement without reintroducing flicker or titlebar/content desynchronization.

## WM-COMP-5 Seat Serials and Grab Model

**Why this matters:** wlroots maintains per-seat serials and grab state for pointer, keyboard, popups, drag/drop, cursor requests, and selection. Lambda has pieces of this, but hardening it reduces protocol edge cases in real applications.

**Goal:** introduce a coherent per-seat serial ledger and grab model that protocol handlers can validate against.

**Expected code areas:**

- `apps/lambda-window-manager/Compositor/Wayland/Globals/Seat.cpp`
- `apps/lambda-window-manager/Compositor/Window/PointerRouter.cpp`
- `apps/lambda-window-manager/Compositor/Window/FocusStack.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/Globals/Selection.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/Globals/XdgShell.cpp`
- `tests/`

**Implementation steps:**

1. Inventory all serial producers and consumers.
2. Add per-client or per-seat serial validation helpers.
3. Route popup grabs, pointer grabs, keyboard focus changes, cursor requests, data-device requests, and primary selection through the shared validation model.
4. Add tests for stale serials, wrong-client serials, popup grab lifetime, and selection validation.

**Acceptance criteria:**

- Protocol requests that require a valid input serial reject stale or wrong-client serials.
- Popup/menu behavior is governed by seat grab state rather than ad hoc focus checks.
- Clipboard and primary-selection paths remain functional under the stricter validation.

## WM-COMP-6 DMABUF Feedback and Buffer Lifetime

**Why this matters:** wlroots tracks buffer lifetime precisely and advertises dmabuf feedback based on renderer and scanout capabilities. Lambda already imports dmabufs, but stronger validation and release timing will make GPU clients and video paths safer.

**Goal:** make dmabuf feedback, import validation, and buffer release timing explicit enough for demanding clients.

**Expected code areas:**

- `apps/lambda-window-manager/Compositor/Wayland/Globals/LinuxDmabuf.cpp`
- `apps/lambda-window-manager/Compositor/Surface/SurfaceRenderer.cpp`
- `apps/lambda-window-manager/Compositor/CompositorRenderFrame.cpp`
- KMS presenter code under `apps/lambda-window-manager/Compositor/`
- `tests/`

**Implementation steps:**

1. Inventory renderer-supported formats/modifiers and scanout-supported formats/modifiers.
2. Distinguish renderer feedback from direct-scanout feedback where the protocol allows it.
3. Validate dmabuf params before creating committed buffers.
4. Tie `wl_buffer.release` to the actual render or scanout lifetime.
5. Add tests for invalid dmabuf params and buffer release ordering where local test infrastructure can exercise it.

**Acceptance criteria:**

- Unsupported dmabuf formats or modifiers fail before they can affect rendering.
- Buffer release timing does not allow clients to reuse buffers still needed by rendering or scanout.
- Video and GPU-client validation passes on target hardware.

## Current Implementation Log

| Date | Workstream | Status | Notes |
| --- | --- | --- | --- |
| 2026-05-31 | WM-COMP-1 | Planned | Created the ordered plan. Next step is the surface pending/current state inventory and migration skeleton. |
| 2026-05-31 | WM-COMP-1 | In progress | Completed Step 1 inventory. Implementing Step 2 as a narrow, automated migration skeleton before moving protocol fields. |
| 2026-05-31 | WM-COMP-1 | Verified | Migrated viewport source/destination into explicit committed and pending state, centralized committed display-size helpers, and added an automated test that pending viewport state cannot affect committed display size. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*Compositor*"` and full `./build/tests/lambda_tests` passed. |
| 2026-05-31 | WM-COMP-1 | In progress | Starting the region and damage state migration: opaque region, input region, pending surface damage, pending buffer damage, and committed buffer damage. |
| 2026-05-31 | WM-COMP-1 | Verified | Migrated opaque/input regions and surface/buffer damage into explicit committed and pending state objects. Added automated tests that pending region and pending damage state do not affect committed consumers before commit. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*Compositor*"` and full `./build/tests/lambda_tests` passed. |
| 2026-05-31 | WM-COMP-1 | In progress | Starting buffer attachment, scale, transform, and attach-offset state migration. |
| 2026-05-31 | WM-COMP-1 | Verified | Migrated current/pending buffer attachment, scale, transform, and attach-offset state into explicit state objects. Added an automated test that pending buffer state cannot affect committed display size or offsets before commit. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*Compositor*"` and full `./build/tests/lambda_tests` passed. |
| 2026-05-31 | WM-COMP-1 | In progress | Starting xdg role-state migration for committed xdg window geometry. |
| 2026-05-31 | WM-COMP-1 | Waiting for user validation | Migrated committed xdg window geometry into explicit role state consumed by snapshots, compositor chrome geometry, and surface-local input helpers. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*Compositor*"` and full `./build/tests/lambda_tests` passed. Manual validation needed: resize the Settings app on DP-1 HiDPI and confirm system titlebar width, borders, background, and content stay in sync without flicker. |
| 2026-05-31 | WM-COMP-1 | Validation failed | User validation found momentary non-synced titlebar/content frames during resize, with no flicker. Next step is to trace and remove the remaining geometry split in the snapshot/render path. |
| 2026-05-31 | WM-COMP-1 | Deferred | User requested deferring the remaining titlebar/content non-synced-frame issue and moving to the next plan item. The failed buffer-retention experiment was dropped before continuing. |
| 2026-05-31 | WM-COMP-2 | Verified | Implemented wlroots-style layer-shell pending/current state, configure serial validation, invalid initial buffer commit rejection, mapped/unmapped state reset on null-buffer unmap, committed-state exclusive-zone recomputation, and layer-shell v4 binding. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*layer*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, and `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"` passed. |
| 2026-05-31 | WM-COMP-3 | In progress | Inventory found immediate live subsurface position updates, no-op sibling ordering requests, no-op sync/desync requests, and creation-order snapshot/input traversal. Starting with pending/current position and committed sibling ordering because it is automatable. |
| 2026-05-31 | WM-COMP-3 | Verified slice | Added pending/current subsurface position, pending/current sibling stack order, `place_above`/`place_below` validation, committed-order snapshot/input traversal, ancestor-parent rejection, and tests for position/order behavior. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*subsurface*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, and `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"` passed. |
| 2026-05-31 | WM-COMP-3 | Verified | Added synchronized subsurface commit caching, recursive parent-commit release through nested subsurfaces, desync release when no synchronized ancestor remains, pending frame callbacks, cached-state cleanup on destroy, and tests for effective synchronization and cached pending state. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*subsurface*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, and `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"` passed. |
