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
| P3 | WM-COMP-4 Scene and output damage architecture | Verified | Conservative scene damage, damage-aware atomic dirty skipping, and presented-scene frame callbacks are implemented | Snapshot/damage tests plus render scheduler tests pass | DP-1 resize, cursor, dock, and flicker validation passed |
| P4 | WM-COMP-5 Seat serials and grab model | Verified | Data-device, clipboard, primary-selection, xdg grab, cursor serial validation, and dock-menu layer-shell lifecycle fixes are implemented and manually validated | Seat serial, popup grab, selection, layer-shell, and focus tests pass | Dock item right-click menu actions, app menus/popups, drag/drop, clipboard, and primary selection passed |
| P5 | WM-COMP-6 DMABUF feedback and buffer lifetime | Verified | Automated protocol hardening is complete and target-hardware GPU-client/video validation passed | DMABUF validation and buffer release tests pass | User validation passed |
| P6 | WM-COMP-7 XDG popup and positioner completeness | Verified | Positioner validation, popup lifecycle checks, and wlroots-style popup constraint adjustment are implemented and covered by automated tests | Popup lifecycle, positioner validation, popup geometry, compositor suite, and broader feasible suite pass | No manual gate needed for this protocol/geometry workstream |
| P7 | WM-COMP-8 XDG surface role and configure lifecycle | Verified | XDG surface role sequencing, base-role creation checks, and buffer commit ordering now follow the wlroots rules covered by automated tests | XDG surface lifecycle, xdg popup, layer role, compositor suite, and broader feasible suite pass | No manual gate needed for this protocol-lifecycle workstream |
| P8 | WM-COMP-9 XDG toplevel request and configure parity | Verified slice | XDG toplevel client-owned title/app-id/parent state now resets on null-buffer unmap | XDG toplevel reset tests plus compositor suite pass | No manual gate needed for this protocol-lifecycle slice |

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

1. Done: inventory current damage sources and full-redraw fallbacks. Current rendering uses `contentSerial_` as the coarse dirty bit, records committed buffer damage for texture uploads, and then redraws the full compositor canvas. Atomic KMS already supports prepared-frame reuse, direct scanout, and overlay-only commits, but there is no scene/output damage object that describes which logical output regions changed.
2. Partially done: added a conservative snapshot-to-snapshot scene damage calculator and render trace hook without changing presentation behavior. It handles full-output fallback, surface add/remove/move, relative stacking changes, stable buffer damage mapping, software cursor movement, and avoids retaining old pixel storage in the damage state.
3. Done: frame scheduling now consumes damage information by carrying scene-damage state on `AtomicReadyFrame`, committing the damage baseline only when a frame is scheduled, and clearing atomic dirty state when the current committed scene has empty damage and no explicit redraw reason. DP-1 HiDPI validation passed after treating hardware cursor movement as an explicit atomic update reason.
4. Done: frame callbacks and presentation feedback now use the surface IDs in the presented scene, including atomic render-ahead frames, direct scanout, overlay-only commits, and non-atomic presentation.
5. Done for this gate: damage propagation/full-output tests cover the conservative damage model, and frame-callback selection tests cover unpresented/invisible-surface filtering.
6. Done: target-hardware validation passed on DP-1 HiDPI for the damage-aware scheduler slice.

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

1. Done: inventory all serial producers and consumers.
2. Done for pointer/cursor slice: add per-client/per-seat serial validation helpers, record input serial producers in a compact ledger, and validate `wl_pointer.set_cursor` plus cursor-shape `set_shape` against pointer enter/button serials for the currently focused pointer surface.
3. Done: xdg popup grabs, xdg move/resize/menu requests, data-device drag start, data-device selection, and primary-selection requests now use the shared serial validation model.
4. Done for automated coverage: added tests for stale serials, wrong-client serials, wrong-surface serials, press-vs-release kind checks, record trimming, destroyed-surface cleanup, and selection-eligible serial kinds.
5. Validation failed: dock item right-click menu disconnected the shell because the menu surface sent a `0x0` layer-shell size before switching to opposing anchors.
6. Validation failed again: dock item right-click menu opened, but choosing New Window disconnected the shell via a stale layer-shell `ack_configure` after menu hide.
7. Verified: user validation passed for dock item right-click menu actions and the previous clipboard/menu checks.

**Step 1 inventory:**

- Serial producers: pointer enter, pointer button, keyboard enter, keyboard key, keyboard modifiers, data-device DnD enter, configure serials, and activation token serial setters.
- Existing consumers: `wl_pointer.set_cursor`, cursor-shape `set_shape`, xdg popup `grab`, xdg toplevel `show_window_menu`, `move`, `resize`, data-device `start_drag`, data-device `set_selection`, primary-selection `set_selection`, and xdg-activation token/activate.
- Current gaps: cursor requests do not share validation; `wl_pointer.set_cursor` ignores the serial entirely; cursor-shape accepts only the latest pointer-enter serial; popup/move/resize use ad hoc pointer-enter/button fields; DnD and selection ignore their supplied serials.
- Migration order: introduce a compact seat serial ledger, use it for cursor requests first, then move xdg move/resize/popup grabs onto the helper, then validate data-device and primary-selection serials.

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

1. Done: inventory renderer-supported formats/modifiers and scanout-supported formats/modifiers.
2. Verified slice: renderer-importable format/modifier pairs remain advertised as the fallback tranche, while KMS scanout-preferred pairs are sent first as a `scanout` tranche when they are also renderer-importable.
3. Verified slice: added reusable single-plane dmabuf layout validation, overflow-safe required byte-span calculation, fd-size bounds checks when available, unsupported flag rejection, and automated tests for invalid dimensions, formats, flags, planes, strides, and bounds.
4. Verified slice: retained dmabuf releases survive surface destruction via a server-level orphaned release queue and are emitted only after KMS no longer reports the buffer in use.
5. Done: added tests for invalid dmabuf params, feedback tranche construction, and buffer release planning where local test infrastructure can exercise it.
6. Done: target-hardware GPU-client/video scenarios were validated by the user.

**Step 1 inventory:**

- Lambda currently advertises/imports only single-plane RGB dmabufs: `DRM_FORMAT_ARGB8888`, `DRM_FORMAT_XRGB8888`, `DRM_FORMAT_ABGR8888`, and `DRM_FORMAT_XBGR8888`.
- Renderer feedback is derived from Vulkan sampled-image modifier support plus compositor modifier preferences. Direct scanout/overlay support is queried separately in the KMS presenter, but the feedback path does not yet distinguish renderer-safe formats from scanout-preferred formats.
- Parameter validation already rejects reused params, non-positive dimensions, unsupported formats, missing plane 0, multiple planes, too-small stride, and unsupported advertised modifiers.
- Current validation gaps: no reusable layout validator, no overflow-safe `offset + stride * height` calculation, no fd-size bounds check before creating the `wl_buffer`, and no automated dmabuf validation tests.
- Buffer release already defers retained dmabufs while KMS reports overlay buffers in use, but release ordering still needs dedicated tests after the validation slice.

**Acceptance criteria:**

- Unsupported dmabuf formats or modifiers fail before they can affect rendering.
- Buffer release timing does not allow clients to reuse buffers still needed by rendering or scanout.
- Video and GPU-client validation passes on target hardware.

## WM-COMP-7 XDG Popup and Positioner Completeness

**Why this matters:** wlroots treats xdg positioners as explicit protocol objects with validated inputs, copied popup positioning rules, and completeness checks on both popup creation and reposition. Lambda has popup positioning and grab handling, but currently accepts some invalid positioner state too late, ignores `set_reactive`, `set_parent_size`, and `set_parent_configure`, and repositions popups without checking that the new positioner is complete.

**Goal:** bring Lambda's xdg popup/positioner protocol behavior closer to wlroots while keeping the visual placement code isolated and testable.

**Expected code areas:**

- `apps/lambda-window-manager/Compositor/Wayland/Globals/XdgShell.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/WaylandServerImpl.hpp`
- `apps/lambda-window-manager/Compositor/Window/WindowGeometry.cpp`
- `tests/`

**Implementation steps:**

1. Done: added wlroots-aligned xdg positioner input validation, stored reactive/parent metadata, created positioner resources at the bound xdg-shell version, and validated complete positioners on popup creation and reposition.
2. Done: compared popup parent/topmost/destroy lifecycle semantics against wlroots and added parent-role plus topmost-destroy checks with automated coverage.
3. Done: compared popup constraint adjustment behavior against wlroots' flip/slide/resize rules and replaced Lambda's clamp-only policy with compatible geometry behavior.
4. Done: ran targeted popup/positioner tests and the broader feasible compositor suite after each slice.

**Step 1 inventory:**

- wlroots validates `set_size` immediately (`width > 0 && height > 0`) and posts `XDG_POSITIONER_ERROR_INVALID_INPUT` for invalid values. Lambda currently stores the values and only rejects some invalid state later during popup creation.
- wlroots validates `set_anchor_rect` immediately (`width >= 0 && height >= 0`) and separately treats the positioner as incomplete until size and anchor width have been set. Lambda currently requires a positive anchor-rect height at popup creation, which is stricter than wlroots' current completeness check.
- wlroots validates anchor, gravity, and constraint-adjustment enum values with the generated protocol validators. Lambda currently stores enum values directly.
- wlroots stores `reactive`, `parent_size`, and `parent_configure_serial` in the positioner rules and copies them onto scheduled popup configure state. Lambda currently ignores those requests.
- wlroots rejects incomplete positioners on both `get_popup` and `reposition`. Lambda currently validates only `get_popup`.

**Acceptance criteria:**

- Invalid xdg positioner setter inputs are rejected at request time.
- Popup creation and reposition reject incomplete positioners consistently.
- `set_reactive`, `set_parent_size`, and `set_parent_configure` state is preserved for popup configure decisions.
- Automated tests cover positioner validation/completeness rules and existing popup placement behavior remains unchanged.

## WM-COMP-8 XDG Surface Role and Configure Lifecycle

**Why this matters:** wlroots treats `xdg_surface` as a base role object with strict sequencing. Requests that depend on a toplevel/popup role are rejected before the role object exists, and the base `xdg_surface` cannot be destroyed while its role object is still alive. Lambda currently accepts some of these requests and tears down role links more permissively.

**Goal:** make xdg_surface role-object and configure lifecycle rules explicit enough to prevent clients from entering states wlroots rejects.

**Expected code areas:**

- `apps/lambda-window-manager/Compositor/Wayland/Globals/XdgShell.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/WaylandServerImpl.hpp`
- `apps/lambda-window-manager/Compositor/Wayland/Globals/Core.cpp`
- `tests/`

**Implementation steps:**

1. Done: reject `ack_configure` and `set_window_geometry` before a toplevel/popup role object exists, and reject `xdg_surface.destroy` while the role object still exists.
2. Done: compared wlroots' `get_xdg_surface` creation-time checks and added existing-buffer rejection plus explicit xdg_surface base-role assignment.
3. Done: compared commit-before-configure behavior against wlroots and rejected buffer commits before role construction or configure acknowledgement.
4. Done: ran targeted xdg lifecycle tests and the broader feasible compositor suite after each slice.

**Step 1 inventory:**

- wlroots rejects `xdg_surface.ack_configure` with `XDG_SURFACE_ERROR_NOT_CONSTRUCTED` if no role object exists. Lambda currently proceeds to serial lookup.
- wlroots rejects `xdg_surface.set_window_geometry` with `XDG_SURFACE_ERROR_NOT_CONSTRUCTED` if no role object exists. Lambda currently stores pending geometry.
- wlroots rejects `xdg_surface.destroy` with `XDG_SURFACE_ERROR_DEFUNCT_ROLE_OBJECT` when a toplevel/popup role object is still alive. Lambda currently destroys/reset-links permissively.

**Acceptance criteria:**

- Role-dependent xdg_surface requests cannot mutate state before a role object exists.
- The xdg_surface base object cannot be destroyed before its live role object.
- Existing xdg popup, toplevel, and compositor tests continue to pass.

## WM-COMP-9 XDG Toplevel Request and Configure Parity

**Why this matters:** wlroots keeps xdg_toplevel requests tied to the configured xdg_surface lifecycle and validates request enums before compositor policy runs. Lambda already has the main toplevel operations, but several request handlers still accept or ignore protocol-invalid states before the xdg_surface is configured.

**Goal:** align xdg_toplevel request sequencing and configure-adjacent state with wlroots while keeping window-management policy behavior unchanged where Lambda intentionally differs.

**Expected code areas:**

- `apps/lambda-window-manager/Compositor/Wayland/Globals/XdgShell.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/WaylandServerImpl.hpp`
- `apps/lambda-window-manager/Compositor/Wayland/XdgToplevelState.hpp`
- `tests/`

**Implementation steps:**

1. Done: add configured-surface gating for `show_window_menu`, `move`, and `resize`, validate resize edges with the generated protocol validator, and advertise `window_menu` in xdg_toplevel WM capabilities.
2. Done: move min/max size-hint validation from `set_min_size`/`set_max_size` request time to the following `wl_surface.commit`, matching wlroots synced toplevel state.
3. Done: validate `set_title` input as UTF-8 like wlroots while continuing to treat `set_app_id` as opaque.
4. Done: retain xdg_toplevel parents only while the requested parent is mapped, track toplevel mapped state, and reparent children when a parent unmaps.
5. Done: reset xdg_surface configured/configure-list state on null-buffer unmap and issue a fresh toplevel configure for remap.
6. Done: reset xdg_toplevel client-owned title/app-id/parent state on null-buffer unmap, matching wlroots role-object reset.
7. Planned: compare remaining scheduled/pending/current toplevel configure fields against wlroots and decide whether Lambda needs a larger synced-state migration after the deferred titlebar/content issue.

**Step 1 inventory:**

- wlroots rejects `xdg_toplevel.show_window_menu` and `xdg_toplevel.move` with `XDG_SURFACE_ERROR_NOT_CONSTRUCTED` if the base xdg_surface has not been configured yet. Lambda currently validates the input serial first and otherwise ignores the request.
- wlroots validates `xdg_toplevel.resize` edges with the generated `xdg_toplevel_resize_edge_is_valid` helper and posts `XDG_TOPLEVEL_ERROR_INVALID_RESIZE_EDGE` before checking the configured state. Lambda currently accepts invalid bit patterns as compositor resize edges.
- wlroots includes `window_menu`, `maximize`, `fullscreen`, and `minimize` in the initial WM capabilities when supported by the client. Lambda currently omits `window_menu` even though it handles `show_window_menu`.
- wlroots stores min/max size requests as pending synced state and validates them on client commit. Lambda currently validates these at request time; this should be treated as a separate follow-up because changing it affects commit rejection behavior.

**Acceptance criteria:**

- Toplevel interactive requests cannot run before the base xdg_surface has been configured.
- Invalid resize-edge enum values produce the protocol error wlroots produces.
- WM capabilities accurately advertise the toplevel operations Lambda supports.
- Automated tests cover the new toplevel lifecycle helper behavior and existing xdg/compositor tests continue to pass.

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
| 2026-05-31 | WM-COMP-4 | In progress | Inventory found coarse `contentSerial_` scheduling plus per-surface buffer damage for texture upload, but no wlroots-style scene/output damage object. Starting with an automated conservative scene-damage calculator and trace hook before changing partial presentation or frame scheduling. |
| 2026-05-31 | WM-COMP-4 | Verified slice | Added `SceneDamageState` and conservative scene damage computation for full-output fallback, old/new frame rects, stable buffer damage mapping, relative order changes, software cursor rects, and lightweight retained metadata. Wired render-frame tracing to the damage result without changing presentation behavior. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*scene damage*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, and `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"` passed. |
| 2026-05-31 | WM-COMP-4 | Verified slice | Implemented damage-aware atomic dirty-frame skipping. Render-ahead damage state is now attached to `AtomicReadyFrame` and committed only when scheduled, so discarded prepared frames do not advance the damage baseline. The runtime clears `atomicFrameDirty` instead of rendering when scene damage is empty and there is no explicit animation, screenshot, snap-preview, config, software-input redraw, or hardware-cursor atomic update reason. User validation found cursor movement did not update until VT switch; fixed by treating hardware cursor movement as an explicit no-skip reason because atomic cursor coordinates are committed with the next KMS update. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*scene damage*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, and `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"` passed after the cursor no-skip fix. Manual DP-1 HiDPI validation then passed for cursor movement, Settings resize, and dock/window flicker checks. |
| 2026-05-31 | WM-COMP-4 | Verified | Carried presented surface IDs through `AtomicReadyFrame` and the non-atomic render path, then used those IDs for presentation feedback and frame callbacks so unpresented or invisible surfaces do not receive frame completion. Direct-scanout repeat frames clear the presented-surface set, avoiding synthetic callbacks while waiting for acquire fences. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*frame callbacks*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, and `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"` passed. |
| 2026-05-31 | WM-COMP-5 | In progress | Inventory found pointer enter/button, keyboard enter/key/modifier, DnD enter, configure, and activation serial producers. Serial consumers are currently split across cursor requests, cursor shape, xdg popup grabs, move/resize/menu, DnD, selection, primary selection, and activation. Starting with a compact seat serial ledger and cursor request validation because it is narrow and automatable. |
| 2026-05-31 | WM-COMP-5 | Verified slice | Added a compact seat serial ledger, routed pointer enter/button, keyboard enter/key/modifier, and DnD enter serial production through it, cleared surface-owned serial records on destroy, and validated `wl_pointer.set_cursor` plus cursor-shape `set_shape` against pointer enter/button serials for the currently focused pointer surface. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*seat serial*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, and `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"` passed. |
| 2026-05-31 | WM-COMP-5 | Verified slice | Split pointer button serials into press/release kinds, removed the ad hoc `pointerEnterSerial_` and `lastPointerButtonSerial_` fields, and moved xdg toplevel move/resize/menu plus xdg popup grab validation onto the seat serial ledger. Toplevel grabs now require a pointer-button press serial for the same client and surface; popup grabs accept pointer-button press or keyboard-key serials for the popup parent/client. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*seat serial*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, and `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"` passed. |
| 2026-05-31 | WM-COMP-5 | Waiting for user validation | Routed data-device drag start, data-device selection, and primary-selection `set_selection` through the seat serial ledger. Drag start now requires a pointer-button press serial for the origin surface/client; selection changes require the current keyboard-focus client with a keyboard-enter, keyboard-key, or pointer-button press serial. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*seat serial*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, and `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"` passed. Manual validation needed before commit: app menus/popups, drag/drop, clipboard copy/paste, and primary selection. |
| 2026-05-31 | WM-COMP-5 | Waiting for user validation | User validation found that right-clicking dock items disconnected the shell while opening the menu. Root cause was a dock-menu layer-shell transition that sent size `0x0` while the surface still had non-opposing hidden anchors, which wlroots-style layer-shell validation correctly rejects. Fixed the shell to switch to the visible opposing anchors before requesting compositor-sized `0x0`, and hardened the Wayland client backend to send `1px` on any axis where zero would be invalid for the current anchors. Manual validation needed before commit: dock item right-click menu plus the previous clipboard/menu/DnD checks. |
| 2026-05-31 | WM-COMP-5 | Waiting for user validation | Follow-up validation found that clicking New Window from the dock menu opened the requested app, then disconnected the shell with `unknown layer-shell configure serial`. Root cause was hiding the menu by attaching a null buffer, which unmapped the layer surface and cleared outstanding configures while a previously sent configure could still be queued to the shell. Fixed the shell client to keep hidden menu surfaces mapped as transparent, empty-input `1x1` surfaces, and made the compositor tolerate already-issued stale layer-shell acks while still rejecting never-issued future serials. Manual validation needed before commit: dock item right-click menu actions plus the previous clipboard/menu/DnD checks. |
| 2026-05-31 | WM-COMP-5 | Verified | User validation passed after the dock-menu lifecycle fixes. Build passed for `lambda_tests`, `lambda-window-manager`, and `lambda-shell`; `./build/tests/lambda_tests --test-case="*layer shell*"`, `./build/tests/lambda_tests --test-case="*seat serial*"`, `./build/tests/lambda_tests --test-case="LambdaDock*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, and `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"` passed. |
| 2026-05-31 | WM-COMP-6 | Verified slice | Inventory found single-plane RGB dmabuf support, renderer-derived feedback, separate KMS scanout capability checks, and missing layout/fd-size validation. Added `DmabufValidation` with overflow-safe layout checks and fd-size bounds validation before `wl_buffer` creation. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*dmabuf*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, and `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"` passed. |
| 2026-05-31 | WM-COMP-6 | Verified slice | Split dmabuf feedback into scanout-preferred and renderer fallback tranches, stopped filtering legacy modifier events by KMS preferences, and fixed feedback event ordering to send tranche flags before tranche formats. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*dmabuf*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, and `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"` passed. |
| 2026-05-31 | WM-COMP-6 | Verified slice | Release-lifetime inventory found that live surface queues respect KMS-retained dmabuf IDs, but `destroySurface` released pending/current buffers immediately. Added a server-level orphaned release queue so destroyed-surface retained dmabufs are held until KMS drops them, with tests for release planning. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*buffer release*"`, `./build/tests/lambda_tests --test-case="*dmabuf*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, and `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"` passed. |
| 2026-05-31 | WM-COMP-6 | Verified | Rejected non-zero dmabuf buffer flags because the renderer does not yet implement `y_invert` or interlaced semantics; accepting them would produce incorrect output. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*dmabuf*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, and `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"` passed. User validation passed for target-hardware GPU-client/video and normal window use scenarios. |
| 2026-05-31 | WM-COMP-7 | In progress | Inventory found missing wlroots-style xdg positioner setter validation, ignored reactive/parent metadata requests, hardcoded xdg_positioner resource versioning, and missing reposition completeness validation. Starting with the automated protocol-validation slice. |
| 2026-05-31 | WM-COMP-7 | Verified slice | Added xdg_positioner request-time validation for size, anchor rectangle, anchor, gravity, and constraint-adjustment values; stored `set_reactive`, `set_parent_size`, and `set_parent_configure`; inherited the xdg_positioner resource version from the bound xdg_wm_base; and rejected incomplete positioners during popup reposition. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*xdg positioner*"`, `./build/tests/lambda_tests --test-case="*popup*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, and `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"` passed. |
| 2026-05-31 | WM-COMP-7 | In progress | Lifecycle comparison found that wlroots rejects popup parents with no constructed xdg role and rejects destroying a popup while it still has child popups. Implementing those checks as the next automated slice. |
| 2026-05-31 | WM-COMP-7 | Verified slice | Added xdg popup lifecycle helpers, rejected `get_popup` when the parent xdg_surface has no xdg toplevel/popup role, and rejected `xdg_popup.destroy` when the popup still has live child popups. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*xdg popup*"`, `./build/tests/lambda_tests --test-case="*popup*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, and `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"` passed. |
| 2026-05-31 | WM-COMP-7 | In progress | Constraint-adjustment comparison found Lambda always clamped popups to the output, while wlroots computes the requested box first and only applies flip, slide, and resize according to `constraint_adjustment`. Implementing the compatible geometry slice. |
| 2026-05-31 | WM-COMP-7 | Verified | Replaced unconditional popup output clamping with wlroots-style requested geometry plus ordered flip, slide, and resize constraint adjustment based on xdg_positioner flags. Existing popup placement remains unchanged when unconstrained; constrained popups now obey the client's requested adjustment policy. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*popup geometry*"`, `./build/tests/lambda_tests --test-case="*popup*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, and `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"` passed. |
| 2026-05-31 | WM-COMP-8 | In progress | Inventory found missing wlroots-style xdg_surface role-object checks for `ack_configure`, `set_window_geometry`, and `xdg_surface.destroy`. Starting with the automatable request sequencing slice. |
| 2026-05-31 | WM-COMP-8 | Verified slice | Added xdg_surface role-object validation helpers, rejected role-dependent `ack_configure` and `set_window_geometry` requests before a constructed xdg toplevel/popup role exists, and rejected `xdg_surface.destroy` before destroying the role object. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*xdg surface*"`, `./build/tests/lambda_tests --test-case="*xdg popup*"`, `./build/tests/lambda_tests --test-case="*popup*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, and `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"` passed. |
| 2026-05-31 | WM-COMP-8 | In progress | Creation-time comparison found that wlroots claims the wl_surface role during `get_xdg_surface` and rejects creation if the surface already has a buffer. Implementing a distinct Lambda `XdgSurface` base role so later toplevel/popup construction transitions from that base role. |
| 2026-05-31 | WM-COMP-8 | Verified slice | Added a distinct `SurfaceRole::XdgSurface` base role, assigned it during `get_xdg_surface`, required toplevel/popup role construction to transition from that base role, released it when the base xdg_surface is destroyed without a role object, and rejected `get_xdg_surface` for surfaces with an existing committed buffer. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*xdg surface*"`, `./build/tests/lambda_tests --test-case="*xdg popup*"`, `./build/tests/lambda_tests --test-case="*layer*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, and `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"` passed. |
| 2026-05-31 | WM-COMP-8 | In progress | Commit-order comparison found that wlroots rejects buffer commits on a base xdg_surface with no role object and rejects toplevel/popup buffer commits before configure acknowledgement. Implementing this check in the shared `wl_surface.commit` path. |
| 2026-05-31 | WM-COMP-8 | Verified | Added xdg_surface buffer-commit readiness checks and rejected non-null buffer commits when the base xdg_surface has no toplevel/popup role object or the role has not acked a configure. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*xdg surface*"`, `./build/tests/lambda_tests --test-case="*xdg popup*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, and `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"` passed. |
| 2026-05-31 | WM-COMP-9 | In progress | Toplevel comparison found missing configured-surface checks for `show_window_menu`, `move`, and `resize`, missing resize-edge enum validation, and an omitted `window_menu` WM capability. Starting with this automated protocol-validation slice. |
| 2026-05-31 | WM-COMP-9 | Verified slice | Added the configured-surface gate for toplevel interactive requests, validated resize edges with the generated xdg-shell enum helper, and advertised the supported `window_menu` WM capability. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*xdg toplevel*"`, `./build/tests/lambda_tests --test-case="*xdg surface*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"`, and `git diff --check` passed. |
| 2026-05-31 | WM-COMP-9 | In progress | Size-hint comparison found Lambda already applies pending min/max hints on commit, but rejects invalid combinations during `set_min_size`/`set_max_size`; wlroots stores the requests and rejects invalid pending state on client commit. Implementing that timing change as the next automated slice. |
| 2026-05-31 | WM-COMP-9 | Verified slice | Moved xdg_toplevel min/max size-hint validation to `wl_surface.commit` while keeping request handlers as pending-state setters. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*size hints*"`, `./build/tests/lambda_tests --test-case="*xdg toplevel*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"`, and `git diff --check` passed. |
| 2026-05-31 | WM-COMP-9 | In progress | Title/app-id comparison found wlroots validates `set_title` as UTF-8 and posts a protocol error for invalid bytes, while `set_app_id` is copied without UTF-8 validation. Implementing title validation only. |
| 2026-05-31 | WM-COMP-9 | Verified slice | Added strict UTF-8 validation for xdg_toplevel titles and left app IDs as opaque strings to match wlroots behavior. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*title*"`, `./build/tests/lambda_tests --test-case="*xdg toplevel*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"`, and `git diff --check` passed. |
| 2026-05-31 | WM-COMP-9 | In progress | Parent comparison found wlroots stores a requested parent only if that parent is mapped, listens for parent unmap, and reparents children to the old parent's parent. Lambda stores the pointer directly and only reparents on role destruction. Implementing mapped parent retention and null-buffer unmap reparenting. |
| 2026-05-31 | WM-COMP-9 | Verified slice | Added explicit xdg_toplevel mapped state, retained requested parents only when mapped, marked toplevels mapped on successful buffer commits, and reset/reparented parent links on null-buffer unmap. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*xdg toplevel parent*"`, `./build/tests/lambda_tests --test-case="*xdg toplevel*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"`, and `git diff --check` passed. |
| 2026-05-31 | WM-COMP-9 | In progress | Configure reset comparison found wlroots clears xdg_surface configured state and pending configures on null-buffer unmap. Lambda keeps the old configured state, so remapped clients can commit against stale configure state. Implementing configure-state reset plus an immediate fresh toplevel configure for remap. |
| 2026-05-31 | WM-COMP-9 | Verified slice | Reset xdg_surface configured/configure-list/current-configure state on null-buffer unmap and sent a fresh toplevel configure so remapping clients have a new serial to ack. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*xdg surface configure*"`, `./build/tests/lambda_tests --test-case="*xdg surface*"`, `./build/tests/lambda_tests --test-case="*xdg toplevel*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"`, and `git diff --check` passed. |
| 2026-05-31 | WM-COMP-9 | In progress | Toplevel role reset comparison found wlroots clears parent, title, app_id, and requested state on unmap. Lambda now clears mapped/parent state but still keeps title and app_id. Implementing the client-owned title/app-id reset as an automated slice. |
| 2026-05-31 | WM-COMP-9 | Verified slice | Added a toplevel unmap reset helper that clears client-owned parent, mapped, title, and app_id state. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*xdg toplevel*"`, `./build/tests/lambda_tests --test-case="*unmap*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"`, and `git diff --check` passed. |
