# Compositor wlroots improvement plan

**Last updated:** 2026-06-12
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
| P9 | WM-COMP-10 XDG activation token lifecycle | Verified | Activation tokens are single-use, validate serial/focus constraints, and expire after the wlroots-style 30 second lifetime | XDG activation, seat serial, compositor, and broader feasible suites pass | No manual gate needed for this protocol-lifecycle workstream |
| P10 | WM-COMP-11 Pointer constraints synced state | Verified | Pointer constraints now use committed region/cursor-hint state and wlroots-style oneshot deactivation cleanup | Pointer constraint, subsurface, compositor, and broader feasible suites pass | No manual gate needed for this protocol-state workstream |
| P11 | WM-COMP-12 Presentation-time resource versioning | Verified | Presentation feedback resources now use the client-bound manager version, capped by the implemented protocol version | Presentation helper, compositor, and broader feasible suites pass | No manual gate needed for this protocol-resource slice |
| P12 | WM-COMP-13 Fractional-scale consistency | Verified | Fractional-scale preferred-scale conversion is shared by initial and runtime events, including sub-1.0 scales | Fractional-scale helper, compositor, and broader feasible suites pass | No manual gate needed for this protocol-resource slice |
| P13 | WM-COMP-14 Idle-inhibit lifecycle/resource hygiene | Verified | Idle-inhibit resource versioning, bind allocation handling, logging, and active-surface checks now follow wlroots-style resource behavior | Idle-inhibit helper, compositor, and broader feasible suites pass | No manual gate needed for this protocol-resource slice |
| P14 | WM-COMP-15 Output scale/resource hygiene | Verified | Legacy `wl_output.scale` rounding, resource-version checks, and bind no-memory handling now follow wlroots | Output helper, compositor, and broader feasible suites pass | No manual gate needed for this protocol-resource slice |
| P15 | WM-COMP-16 XDG output runtime updates | Verified | XDG-output resources are tracked and receive logical-size updates when output scale changes, with wlroots-style done-event selection | XDG-output helper, compositor, and broader feasible suites pass | No manual gate needed for this protocol-resource slice |
| P16 | WM-COMP-17 Output done-event batching | Verified | Runtime `wl_output.scale` and xdg-output logical-size updates are batched behind one `wl_output.done`, matching wlroots scheduling semantics | XDG-output/output helper, compositor, and broader feasible suites pass | No manual gate needed for this protocol-resource slice |
| P17 | WM-COMP-18 Pointer extension dependent-resource cleanup | Verified | Server-side relative-pointer and pointer-constraint objects are removed when their wl_pointer resource is destroyed, leaving protocol resources inert | Pointer-extension helper, compositor, and broader feasible suites pass | No manual gate needed for this protocol-lifecycle slice |
| P18 | WM-COMP-19 Cursor-shape dependent-resource cleanup | Verified | Cursor-shape device state is removed when the underlying wl_pointer resource is destroyed, leaving protocol resources inert | Cursor-shape helper, compositor, and broader feasible suites pass | No manual gate needed for this protocol-lifecycle slice |
| P19 | WM-COMP-20 Viewporter resource hygiene | Verified | Viewporter resource versioning and bind no-memory handling now align with wlroots | Viewporter helper, compositor, and broader feasible suites pass | No manual gate needed for this protocol-resource slice |
| P20 | WM-COMP-21 Remaining global resource hygiene | Verified | Core, shm, layer-shell, cutouts, and xdg-decoration manager paths now have explicit version caps or no-memory bind/object guards; existing dependent-resource cleanup was audited | `CompositorGlobalResourceHygieneTests.cpp`, compositor source-file suite, and build pass | No manual gate needed for this protocol-resource slice |
| P21 | WM-COMP-22 XDG toplevel configure-state parity | Verified | `ack_configure` serial consumption, pending-to-current configure commit, pending-configure frame-size selection, window-manager transition geometry, XDG state configure sizing, popup parent geometry, cutout rejection sizing, client resize-grab starts, top-level pointer hit bounds, diagnostic exercise geometry, and pointer-constraint default regions now use explicit tested helpers; final low-level consumer audit found no remaining pending-configure-unsafe XDG toplevel geometry consumers | XDG surface/window-state tests, pointer-constraint tests, compositor suite, and broader feasible suite pass | No manual gate needed for this configure-state/helper workstream |
| P22 | WM-COMP-23 Seat focus and grab workflow parity | Verified slices | xdg-popup grabs now follow the wlroots popup-grab model for the implemented pointer/keyboard seat paths; unmapped XDG toplevels plus destroyed XDG popup/toplevel and layer-shell roles now clear stale seat focus/order/grab/serial/activation state while deactivating pointer constraints; cursor requests now validate against implicit pointer-button grabs during focus changes. Broader non-popup seat-grab parity remains planned. | Seat serial, popup, layer-shell, window-state, pointer-constraint, cursor-shape, config, compositor, and broader feasible tests pass | Firefox app/context menu actions and click-open submenus passed; no manual gate needed for these cleanup slices |
| P23 | WM-COMP-24 Data-device and drag/drop lifecycle parity | In progress | DnD action negotiation, offer/source validation, single-use source lifecycle, drag-icon role validation, post-finish offer request rejection, completed-drop cleanup, and post-drop offer-abort cancellation are factored into tested helpers; remaining destruction/cancel cleanup comparison is planned | Data-device/DnD/selection tests plus compositor suite | Manual DnD/file-transfer validation if payload behavior changes |
| P24 | WM-COMP-25 Layer-shell dynamic behavior parity | Planned | Compare output changes, exclusive-zone recomputation, keyboard interactivity, mapping/unmapping, and popup/menu interactions | Layer-shell tests plus compositor suite | Shell dock/topbar/fullscreen validation if placement/focus behavior changes |
| P25 | WM-COMP-26 Output layout and multi-output foundation | Planned | Design wlroots-style output-layout abstractions while v1 remains single-output, including enter/leave and xdg-output positions | Output/xdg-output/snapshot tests plus compositor suite | Target hardware multi-output gate before enabling multiple active outputs |
| P26 | WM-COMP-27 Visual regression and real-app harness | Planned | Add repeatable resize/menu/fullscreen validation scripts and traces so manual regressions become easier to catch | Scripted smoke checks and deterministic helpers | Target hardware visual gate remains |

## Remaining Work After 2026-06-01

Default next implementation order is the remaining non-popup WM-COMP-23 seat/grab parity, then WM-COMP-24 through WM-COMP-27, unless the deferred WM-COMP-1 titlebar/content sync issue is explicitly resumed first.

Deferred P0 work:

- Resume WM-COMP-1 only when we are ready to change the rendering/frame architecture again. The remaining symptom is momentary non-synced system-titlebar/content width during Settings resize on DP-1 HiDPI. Borders are now in sync and flicker is gone. A previous buffer-retention experiment did not solve it and was dropped.
- The next attempt should not stretch stale content. It should make the compositor build one frame model for chrome, borders, background, and client content from a single committed geometry snapshot, then render that model as one unit.
- This needs target-hardware visual validation because current automated tests cover state coherence but not the exact visible frame race.

Next comparison work:

- Compare larger behavior workflows against wlroots: xdg toplevel configure state, seat focus/grabs, data-device DnD lifecycle, dynamic layer-shell behavior, and output-layout foundations.
- Turn repeated manual regressions into scripts or trace-driven checks where possible, while keeping target-hardware visual validation for resize, popups, fullscreen, and real-app rendering.

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

## WM-COMP-10 XDG Activation Token Lifecycle

**Why this matters:** wlroots treats xdg-activation tokens as explicit single-use objects. Token requests become inert after commit, activation only works for a known committed token string, and activation consumes the token. Lambda currently generates token strings but activates any requested toplevel regardless of the supplied token.

**Goal:** make xdg-activation token lifecycle and activation matching explicit before tightening serial/focus validation.

**Expected code areas:**

- `apps/lambda-window-manager/Compositor/Wayland/Globals/Activation.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/ActivationState.hpp`
- `apps/lambda-window-manager/Compositor/Wayland/WaylandServerImpl.hpp`
- `tests/`

**Implementation steps:**

1. Done: make committed token resources immutable, require `activate` to reference a known committed token string, and consume that token after activation.
2. Done: validate token serials and focused-surface constraints against the seat serial ledger.
3. Done: add token expiration and cleanup so unused committed tokens are not retained indefinitely.

**Step 1 inventory:**

- wlroots makes the token resource inert on commit. Follow-up `set_serial`, `set_app_id`, `set_surface`, or `commit` requests on that resource receive `XDG_ACTIVATION_TOKEN_V1_ERROR_ALREADY_USED`.
- wlroots stores committed tokens in the activation manager, ignores unknown activation token strings, emits an activation request for known tokens, and destroys the token after use.
- Lambda sends a token string on commit but leaves the token object mutable, ignores the token string in `activate`, and rate-limits repeated focus by surface instead of consuming tokens.

**Acceptance criteria:**

- A committed activation token cannot be mutated or committed again.
- `xdg_activation.activate` only focuses a surface when the supplied token is known and committed.
- A successful activation consumes the token so it cannot be reused.
- Unused committed tokens expire and are destroyed after the wlroots-style 30 second lifetime.
- Automated tests cover token matching and lifecycle helpers.

## WM-COMP-11 Pointer Constraints Synced State

**Why this matters:** wlroots treats pointer constraint region and cursor-position hint updates as surface-synchronized state that becomes effective on the constrained surface's next commit. Lambda currently accepts `set_region` but ignores it, applies cursor hints immediately, and confines only to the full surface bounds.

**Goal:** make pointer constraints observe committed region/cursor-hint state and use the committed effective region for confinement.

**Expected code areas:**

- `apps/lambda-window-manager/Compositor/Wayland/Globals/PointerExtensions.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/PointerConstraintState.hpp`
- `apps/lambda-window-manager/Compositor/Wayland/WaylandServerImpl.hpp`
- `apps/lambda-window-manager/Compositor/Wayland/Globals/Core.cpp`
- `apps/lambda-window-manager/Compositor/Window/WindowManager.cpp`
- `tests/`

**Implementation steps:**

1. Done: add committed/pending pointer constraint region and cursor-hint state, apply it from the surface commit path, and use the effective committed region during confinement.
2. Done: review oneshot constraint deactivation/resource-inert behavior against wlroots after the region-state slice.

**Step 1 inventory:**

- wlroots stores pointer constraint `set_region` and locked-pointer `set_cursor_position_hint` requests in pending synchronized state and applies them on the associated surface commit.
- wlroots computes the effective region as the committed constraint region intersected with the surface input region, or the committed input region alone when no constraint region is set.
- Lambda currently ignores `set_region`, writes cursor hints immediately, and clamps confined pointer motion to the full committed surface bounds.

**Acceptance criteria:**

- Pointer constraint region and cursor hint requests do not affect compositor behavior before the constrained surface commits.
- Confined pointer motion clamps to the committed effective constraint region rather than always using the full surface bounds.
- Input-region changes on the constrained surface update the effective pointer constraint region on commit.
- Automated helper and compositor tests cover pending/current pointer constraint state and existing pointer/compositor behavior continues to pass.

## WM-COMP-12 Presentation-Time Resource Versioning

**Why this matters:** wlroots creates `wp_presentation_feedback` resources using the version of the bound `wp_presentation` manager. Lambda currently hardcodes feedback resources to version 2 even when the client bound an older manager version.

**Goal:** keep presentation-time resource versions and no-memory handling aligned with the client-bound protocol object.

**Expected code areas:**

- `apps/lambda-window-manager/Compositor/Wayland/Globals/Presentation.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/PresentationState.hpp`
- `tests/`

**Implementation steps:**

1. Done: create feedback resources at the bound presentation version and centralize the implemented version cap.

**Step 1 inventory:**

- wlroots uses `wl_resource_get_version(presentation_resource)` when creating `wp_presentation_feedback`.
- Lambda creates every feedback resource at version 2 regardless of the manager resource version. Its manager bind already posts no-memory on allocation failure, so that path only needs to use the same version helper.

**Acceptance criteria:**

- Feedback resources use the client-bound presentation manager version, capped by the implemented protocol version.
- Presentation manager binding continues to post no-memory on allocation failure.
- Automated helper tests cover version selection and existing presentation/compositor tests continue to pass.

## WM-COMP-13 Fractional-Scale Consistency

**Why this matters:** wlroots converts fractional-scale recommendations with `round(scale * 120)` for both existing and newly-created scale objects. Lambda had two helpers: runtime scale updates preserved sub-1.0 scales, while newly-created fractional-scale objects clamped the initial recommendation to at least 1.0.

**Goal:** make initial and runtime `wp_fractional_scale_v1.preferred_scale` events use the same conversion and resource-version rules.

**Expected code areas:**

- `apps/lambda-window-manager/Compositor/Wayland/Globals/FractionalScale.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/FractionalScaleState.hpp`
- `apps/lambda-window-manager/Compositor/Wayland/ServerLifecycle.cpp`
- `tests/`

**Implementation steps:**

1. Done: centralize the fractional-scale version/conversion helpers and use them in both object creation and runtime output-scale updates.

**Step 1 inventory:**

- wlroots uses one conversion path equivalent to `round(scale * 120)` when notifying fractional-scale clients.
- Lambda's `setPreferredScale` path clamps to the supported output range and sends 0.5 as 60, but `get_fractional_scale` used a local helper that sent 0.5 and 0.75 as 120.
- Lambda's fractional-scale manager bind path also lacked a no-memory guard after `wl_resource_create`.

**Acceptance criteria:**

- New fractional-scale objects and existing fractional-scale resources receive matching preferred-scale values.
- Sub-1.0 preferred scales are preserved down to Lambda's supported 0.5 minimum.
- Manager bind allocation failure posts no-memory instead of dereferencing a null resource.
- Automated helper tests cover the conversion and implemented protocol-version cap.

## WM-COMP-14 Idle-Inhibit Lifecycle/Resource Hygiene

**Why this matters:** wlroots keeps idle-inhibit as a small resource-lifecycle protocol: manager and inhibitor resources use the client-bound protocol version, allocation failures return no-memory, and compositor idle policy observes only live mapped surfaces. Lambda already has the core protocol, but the active-surface predicate is embedded in frame scheduling and the bind path lacks a no-memory guard.

**Goal:** centralize idle-inhibit resource versioning and active-surface rules, remove noisy per-inhibitor stderr logging, and add automated coverage for the policy helper.

**Expected code areas:**

- `apps/lambda-window-manager/Compositor/Wayland/Globals/IdleInhibit.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/IdleInhibitState.hpp`
- `apps/lambda-window-manager/Compositor/Wayland/FrameScheduler.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/Destroy.cpp`
- `tests/`

**Implementation steps:**

1. Done: compared wlroots resource creation and cleanup rules, then centralized Lambda's implemented version cap and active-surface predicate.
2. Done: used the helper from both binding and idle-policy checks, added bind allocation failure handling, and removed ad hoc stderr logs.
3. Done: ran targeted idle-inhibit/compositor tests and the broader feasible suite.

**Step 1 inventory:**

- wlroots creates idle-inhibit manager resources at the bound version and creates inhibitor resources using the manager resource's version.
- wlroots posts no-memory when manager binding or inhibitor creation allocation fails.
- Lambda already destroys surface-owned idle inhibitors from `destroySurface`, but it hardcodes inhibitor resources to version 1, dereferences a failed manager bind allocation, and logs every inhibitor create/destroy directly to stderr.
- Lambda's active idle-inhibitor predicate is currently correct but local to `FrameScheduler.cpp`; moving it to a helper makes the lifecycle policy testable and keeps future idle-policy changes out of scheduling code.

**Acceptance criteria:**

- Manager and inhibitor resources use Lambda's implemented idle-inhibit version cap.
- Manager bind allocation failure posts no-memory instead of dereferencing a null resource.
- Idle inhibition is active only for surfaces with a committed buffer, non-zero dimensions, and no minimized state.
- Automated helper tests cover version selection and active-surface policy.

## WM-COMP-15 Output Scale/Resource Hygiene

**Why this matters:** wlroots sends legacy `wl_output.scale` as `ceil(output->scale)`, while Lambda currently reports `1` for every non-integral scale. Fractional-scale clients already get accurate preferred-scale events, but legacy clients without fractional-scale support can render at the wrong scale on HiDPI fractional outputs.

**Goal:** make initial and runtime `wl_output.scale` events use a shared wlroots-style integer-scale helper, and make output binding use explicit resource-version/no-memory handling.

**Expected code areas:**

- `apps/lambda-window-manager/Compositor/Wayland/Globals/Output.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/OutputState.hpp`
- `apps/lambda-window-manager/Compositor/Wayland/ServerLifecycle.cpp`
- `tests/`

**Implementation steps:**

1. Done: compared wlroots output resource binding and scale notification behavior, then centralized Lambda's output version and integer-scale helper.
2. Done: used the helper for both initial output binding and runtime scale updates.
3. Done: ran targeted output/compositor tests and the broader feasible suite.

**Step 1 inventory:**

- wlroots binds `wl_output` resources at the advertised output version, posts no-memory on allocation failure, and gates scale/name/description/done events on the resource's bound version.
- wlroots sends `wl_output.scale` as `ceil(output->scale)`, so fractional values above an integer are rounded up for legacy clients.
- Lambda currently has duplicate integer-scale helpers in output binding and runtime scale updates; both return `1` for non-integral output scales.
- Lambda's output bind path dereferences a failed `wl_resource_create` allocation.

**Acceptance criteria:**

- Initial output bind and runtime output-scale updates use the same `ceil`-based integer scale helper.
- Output bind allocation failure posts no-memory instead of dereferencing a null resource.
- Output events are gated by the bound resource version.
- Automated helper tests cover version selection and integer-scale rounding.

## WM-COMP-16 XDG Output Runtime Updates

**Why this matters:** xdg-output logical size is how clients such as Xwayland discover the compositor-space size of a scaled output. wlroots updates xdg-output resources when output layout or effective resolution changes. Lambda currently sends logical size only when the xdg-output object is created, so runtime output-scale changes can leave clients with stale logical output geometry.

**Goal:** track xdg-output resources, update their logical size when Lambda's output scale changes the effective logical output size, and select the correct done event based on xdg-output and wl_output resource versions.

**Expected code areas:**

- `apps/lambda-window-manager/Compositor/Wayland/Globals/XdgOutput.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/Globals/XdgOutput.hpp`
- `apps/lambda-window-manager/Compositor/Wayland/XdgOutputState.hpp`
- `apps/lambda-window-manager/Compositor/Wayland/Globals/Output.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/ServerLifecycle.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/WaylandServerImpl.hpp`
- `tests/`

**Implementation steps:**

1. Done: compared wlroots xdg-output resource creation, done-event selection, and runtime update behavior.
2. Done: stored xdg-output resources with their paired `wl_output` resource, cleared stale output-resource links, and updated logical size from runtime scale changes.
3. Done: ran targeted xdg-output/compositor tests and the broader feasible suite.

**Step 1 inventory:**

- wlroots stores xdg-output resources per output and emits logical position/size details when output layout or effective resolution changes.
- For xdg-output resources below version 3, wlroots sends `zxdg_output_v1.done`; for version 3 and newer, it uses `wl_output.done` only when the paired wl_output resource supports done events.
- wlroots posts no-memory on xdg-output manager bind and object allocation failure.
- Lambda currently does not retain xdg-output resources after creation, cannot update their logical size on output-scale changes, calls `wl_output.done` for xdg-output v3 without checking the paired wl_output resource version, and lacks a manager bind no-memory guard.

**Acceptance criteria:**

- Runtime output-scale changes that alter logical output size send xdg-output logical-size updates.
- Done-event selection follows xdg-output and wl_output resource versions.
- Destroyed wl_output resources are not used for later xdg-output done events.
- Automated helper tests cover version selection, done-event selection, and logical-size update detection.

## WM-COMP-17 Output Done-Event Batching

**Why this matters:** wlroots schedules output done events so `wl_output.scale` and xdg-output logical-size updates caused by the same output state change are observed atomically. Lambda's runtime scale path can send `wl_output.done` once after scale and then again after xdg-output updates, which splits one logical output change into two client-visible batches.

**Goal:** send runtime output scale changes, xdg-output logical-size updates, and the final `wl_output.done` in one ordered batch.

**Expected code areas:**

- `apps/lambda-window-manager/Compositor/Wayland/Globals/XdgOutput.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/Globals/XdgOutput.hpp`
- `apps/lambda-window-manager/Compositor/Wayland/XdgOutputState.hpp`
- `apps/lambda-window-manager/Compositor/Wayland/ServerLifecycle.cpp`
- `tests/`

**Implementation steps:**

1. Done: suppressed runtime xdg-output `wl_output.done` while still allowing pre-v3 `zxdg_output_v1.done`.
2. Done: reordered `setPreferredScale` so output scale, xdg-output details, and final `wl_output.done` are emitted in one batch.
3. Done: ran targeted xdg-output/output/compositor tests and the broader feasible suite.

**Step 1 inventory:**

- wlroots sends xdg-output details and calls `wlr_output_schedule_done`; output scale changes also schedule done, so the event loop coalesces them.
- Lambda sends output scale and `wl_output.done` immediately from `setPreferredScale`.
- After WM-COMP-16, Lambda can also send `wl_output.done` from the xdg-output update helper for xdg-output v3 resources.

**Acceptance criteria:**

- Runtime scale updates send `wl_output.scale` before xdg-output logical details and one `wl_output.done` after both.
- Pre-v3 xdg-output resources still receive `zxdg_output_v1.done` for xdg-output details.
- Automated helper tests cover suppressing only wl_output done events during a batched update.

## WM-COMP-18 Pointer Extension Dependent-Resource Cleanup

**Why this matters:** wlroots removes server-side relative-pointer and pointer-constraint state when the underlying `wl_pointer` resource is destroyed, while leaving the extension protocol resource inert. Lambda currently only nulls the stored pointer, which keeps dead objects in the compositor lists until the extension resource is destroyed.

**Goal:** make pointer-extension objects follow wlroots' dependent-resource cleanup model and centralize their implemented protocol-version caps.

**Expected code areas:**

- `apps/lambda-window-manager/Compositor/Wayland/Globals/PointerExtensions.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/Globals/PointerExtensions.hpp`
- `apps/lambda-window-manager/Compositor/Wayland/Globals/Seat.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/PointerExtensionState.hpp`
- `tests/`

**Implementation steps:**

1. Done: added version helpers and cleanup entry points for relative-pointer and pointer-constraint resources.
2. Done: called those cleanup entry points from `wl_pointer` resource destruction.
3. Done: ran targeted pointer-extension/compositor tests and the broader feasible suite.

**Step 1 inventory:**

- wlroots creates relative-pointer and pointer-constraint resources at the bound manager version.
- When the underlying pointer resource is destroyed, wlroots removes the server-side relative-pointer or pointer-constraint object and sets the extension resource user data to null so future destructor callbacks are inert.
- Lambda already nulls `relativePointer->pointer` and `constraint->pointer` on pointer destruction, but leaves the server-side objects resident.

**Acceptance criteria:**

- Destroying a `wl_pointer` removes associated relative-pointer and pointer-constraint server objects.
- Extension protocol resources left alive after dependent pointer destruction are inert.
- Resource version selection is centralized and covered by automated helper tests.

## WM-COMP-19 Cursor-Shape Dependent-Resource Cleanup

**Why this matters:** wlroots makes cursor-shape device resources inert when their dependent seat/pointer object goes away. Lambda currently keeps cursor-shape devices with a raw `wl_pointer` pointer, so a later `set_shape` request can inspect a destroyed pointer resource.

**Goal:** remove server-side cursor-shape device state when the underlying `wl_pointer` resource is destroyed, and centralize cursor-shape version selection.

**Expected code areas:**

- `apps/lambda-window-manager/Compositor/Wayland/Globals/CursorShape.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/Globals/CursorShape.hpp`
- `apps/lambda-window-manager/Compositor/Wayland/Globals/Seat.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/CursorShapeState.hpp`
- `tests/`

**Implementation steps:**

1. Done: added cursor-shape version and dependent-pointer helpers.
2. Done: used the helpers for cursor-shape resource creation and `wl_pointer` destruction cleanup.
3. Done: ran targeted cursor-shape/compositor tests and the broader feasible suite.

**Step 1 inventory:**

- wlroots creates cursor-shape devices at the bound manager resource version and posts no-memory on bind/object allocation failure.
- wlroots sets the cursor-shape device resource user data to null when dependent seat-client/pointer state is destroyed, leaving later resource destruction inert.
- Lambda creates cursor-shape devices at the manager version, but lacks a manager bind no-memory guard and does not remove devices when `wl_pointer` is destroyed.

**Acceptance criteria:**

- Destroying a `wl_pointer` removes associated cursor-shape device server objects.
- Cursor-shape protocol resources left alive after dependent pointer destruction are inert.
- Manager bind allocation failure posts no-memory.
- Resource version selection and dependent-pointer matching are covered by automated helper tests.

## WM-COMP-20 Viewporter Resource Hygiene

**Why this matters:** wlroots creates `wp_viewport` resources at the bound viewporter manager version and posts no-memory on manager bind allocation failure. Lambda already has the important viewport pending/current commit validation, but its resource creation path still has the same small allocation/version gaps seen in other globals.

**Goal:** centralize the implemented viewporter version cap and use it for both manager and viewport object resources, with bind no-memory handling.

**Expected code areas:**

- `apps/lambda-window-manager/Compositor/Wayland/Globals/Viewporter.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/ViewporterState.hpp`
- `tests/`

**Implementation steps:**

1. Done: added a viewporter resource-version helper.
2. Done: used the helper in manager binding and viewport object creation, with no-memory handling.
3. Done: ran targeted viewporter/compositor tests and the broader feasible suite.

**Step 1 inventory:**

- wlroots advertises viewporter version 1 and creates viewport objects at the bound manager resource version.
- wlroots posts no-memory if manager binding or viewport object creation fails.
- Lambda already posts no-memory for viewport object creation and has commit-time validation for source bounds and integer source sizes without destination.
- Lambda hardcodes viewport object resources to version 1 and lacks a manager bind no-memory guard.

**Acceptance criteria:**

- Viewporter manager and viewport object resources use the implemented version cap.
- Manager bind allocation failure posts no-memory.
- Automated helper tests cover version selection.

## WM-COMP-21 Remaining Global Resource Hygiene

**Why this matters:** the recent resource-hygiene slices found the same small wlroots differences repeatedly: manager bind allocation failure, object creation failure, resource-version caps, and cleanup when dependent resources disappear. The remaining globals should be audited in one focused pass before moving to larger behavior changes.

**Goal:** make the remaining global bind/object paths follow the same explicit version, no-memory, and inert-resource patterns already applied to presentation-time, fractional-scale, idle-inhibit, output, xdg-output, pointer extensions, cursor-shape, and viewporter.

**Expected code areas:**

- `apps/lambda-window-manager/Compositor/Wayland/Globals/Core.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/Globals/Shm.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/Globals/XdgShell.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/Globals/LayerShell.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/Globals/Cutouts.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/Globals/BackgroundEffect.cpp`
- `tests/`

**Implementation steps:**

1. Done: inventoried the remaining global bind/object paths.
2. Done: added explicit version helpers for core compositor/subcompositor, shm, layer-shell, and cutouts resource caps.
3. Done: guarded remaining manager bind and object allocation failures with `wl_client_post_no_memory`.
4. Done: audited dependent-resource destruction; existing shm pool/buffer, xdg/background-effect, and pointer-dependent cleanup already leave dependents inert or cleared for this remaining-global slice.
5. Done: added targeted helper tests and ran the compositor suite subset.

**Step 1 inventory:**

- Core compositor: `wl_compositor` binding and `wl_surface` object creation lacked failed-allocation guards and used hardcoded version caps.
- SHM: `wl_shm` binding, pool creation, and buffer creation lacked failed-allocation guards; pool creation also needed to close the transferred fd if the pool resource allocation fails.
- XDG shell: xdg object creation paths already guard allocation failures; `zxdg_decoration_manager_v1` binding lacked a failed-allocation guard.
- Layer shell: layer-surface object creation already guarded allocation failure; manager binding needed a no-memory guard and both paths now use the explicit implemented version cap.
- Cutouts: cutouts object creation already guarded allocation failure; manager binding needed a no-memory guard and both paths now use the explicit implemented version cap.
- Background effect: manager and surface object creation already guard allocation failures; no code change was needed in this slice.

**Acceptance criteria:**

- Done: remaining manager bind paths do not dereference failed resource allocations.
- Done: new object resources are created at the capped bound-manager version where the protocol permits it.
- Done: destroyed dependent resources cannot leave live server objects with stale raw resource pointers in this audited remaining-global set.
- Done: version-cap changes are covered by automated helper tests; allocation-guard paths were build- and audit-verified because `wl_resource_create` failure injection is not available in the current harness.

## WM-COMP-22 XDG Toplevel Configure-State Parity

**Why this matters:** wlroots keeps xdg toplevel requested, scheduled, pending, and current state distinct. Lambda now handles many request and unmap-reset rules correctly, but the deferred titlebar/content issue suggests there may still be one visible geometry split between configure state, compositor chrome, and committed content.

**Goal:** compare the remaining xdg toplevel configure pipeline against wlroots and either prove Lambda's current model is coherent or migrate the missing state into an explicit synced-state path.

**Expected code areas:**

- `apps/lambda-window-manager/Compositor/Wayland/Globals/XdgShell.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/WaylandServerImpl.hpp`
- `apps/lambda-window-manager/Compositor/Wayland/Snapshots.cpp`
- `apps/lambda-window-manager/Compositor/Chrome/`
- `apps/lambda-window-manager/Compositor/Window/`
- `tests/`

**Implementation steps:**

1. Done: compared Lambda's `configureList`, `pendingConfigure`, and `currentConfigure` fields and extracted the ack/commit transition helper slice.
2. Done: identified and fixed a concrete snapshot/chrome hit-testing split where snapshots froze pending-configure frame size to committed dimensions while chrome hit testing still used live display dimensions.
3. Done: moved that frame-size decision into a shared helper used by snapshots, top chrome hit testing, cutout controls hit testing, `windowGeometryFor`, window-manager transition start-size bookkeeping, XDG state configure sizing, popup parent geometry, initial/cutout rejection sizing, client resize-grab starts, top-level pointer hit bounds, diagnostic exercise geometry, and pointer-constraint default regions.
4. Done: final low-level consumer audit found the remaining direct `displayWidth`/`displayHeight`/`frameWidth`/`frameHeight` reads are layer-shell placement/reservations, subsurface hit testing, cursor snapshots, snapshot-local viewport fallback fields, core frame-size assignment/logging, and cutout configure-send trigger guards rather than XDG toplevel pending-configure geometry consumers.
5. Done: added automated tests for `ack_configure` serial consumption, stale configure pruning, resize-configure detection, pending-to-current configure commit, pending-configure interactive frame-size selection, committed-size availability, window geometry helper coherence, usable window geometry rejection, and pointer-constraint default-region coherence.
6. Not needed for this helper workstream: no visible frame timing behavior changed.

**Acceptance criteria:**

- Configure acknowledgements and surface commits expose one coherent toplevel state to snapshots, chrome, hit testing, and window management.
- System-titlebar and content geometry cannot diverge because they read different toplevel state groups.
- Automated tests cover state transitions; target-hardware validation covers visible resize if rendering behavior changes.

## WM-COMP-23 Seat Focus and Grab Workflow Parity

**Why this matters:** WM-COMP-5 added a shared serial ledger and fixed several concrete grab/selection paths, but wlroots also has a broader seat model for focus changes, pointer grabs, keyboard grabs, popup grabs, cursor requests, and cancellation when surfaces disappear.

**Goal:** audit the full seat workflow against wlroots so input focus and grabs stay valid across popup/menu operations, destroyed surfaces, keyboard navigation, and mixed client/compositor interactions.

**Expected code areas:**

- `apps/lambda-window-manager/Compositor/Wayland/Globals/Seat.cpp`
- `apps/lambda-window-manager/Compositor/Window/PointerRouter.cpp`
- `apps/lambda-window-manager/Compositor/Window/FocusStack.cpp`
- `apps/lambda-window-manager/Compositor/Window/LayerShellInput.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/Globals/XdgShell.cpp`
- `tests/`

**Implementation steps:**

1. Verified popup-grab parity: compare wlroots popup grab behavior with Lambda's xdg-popup path.
2. Verified popup-grab parity: define popup cancellation/routing rules for same-client owner events, destroyed popup surfaces, transient pointer-button grabs, parent/child popup grab stacks, and invalid grab-after-commit requests.
3. In progress: compare the remaining wlroots seat focus and grab lifecycle with Lambda's non-popup pointer, keyboard, cursor, and layer-shell input paths.
4. Verified slice: cursor requests from `wl_pointer.set_cursor` and `wp_cursor_shape_device_v1.set_shape` now share a tested serial-validation helper that honors the active implicit pointer-button grab client/surface during focus changes while still accepting same-client focused-surface serials.
5. Verified slice: unmapped XDG toplevels now drop stale keyboard focus, pointer focus, pointer-button grab state, focus order, focus-cycle state, seat serials, and activation-token surface references; pointer constraints are updated when pointer focus is removed.
6. Verified slice: XDG popup and toplevel role destruction now use the same stale seat cleanup helper, so role teardown also drops serial records and activation-token surface references and refreshes pointer constraints when pointer focus is removed.
7. Verified slice: layer-shell role destruction now uses the same stale seat cleanup helper, covering focused Shell surfaces such as launcher/dock/quick-status role teardown without keeping stale pointer/keyboard focus, serials, or activation-token surface references.
8. Planned: define any remaining cancellation rules for destroyed focus surfaces, popup parents, and non-popup pointer/keyboard grabs outside the Firefox menu path.
9. Planned: add broader automated tests for grab start/end/cancel, destroyed-surface cleanup, popup/menu focus, and keyboard focus restoration.
10. Planned: request manual app menu/dock menu validation if runtime grab behavior changes again.

**Acceptance criteria:**

- Focus and grab state cannot keep stale surface/resource pointers after destroy or unmap.
- Popup and menu interactions follow a single seat/grab model instead of ad hoc focus checks.
- Cursor, keyboard, and pointer behavior remain correct in Firefox, Lambda apps, dock menus, and launcher popups.

## WM-COMP-24 Data-Device and Drag/Drop Lifecycle Parity

**Why this matters:** the current data-device path is usable for clipboard, primary selection, and text drag/drop, but wlroots has many lifecycle details around data sources, offers, action negotiation, leave/drop/cancel, and destroyed clients. These paths are easy to get mostly right and still fail with real applications.

**Goal:** compare Lambda's data-device implementation to wlroots and close lifecycle gaps without expanding product scope beyond the payloads Lambda can currently support.

**Expected code areas:**

- `apps/lambda-window-manager/Compositor/Wayland/Globals/Selection.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/Globals/Seat.cpp`
- `apps/lambda-window-manager/Compositor/Window/PointerRouter.cpp`
- `tests/`

**Implementation steps:**

1. Planned: inventory data-source, data-offer, data-device, drag icon, target enter/motion/leave/drop, and action negotiation behavior.
2. Planned: compare cancellation and cleanup behavior for source/target/client/surface destruction.
3. Planned: verify selection and DnD serial checks remain aligned with WM-COMP-5.
4. Verified slice: DnD action-mask validation, preferred-action validation, and source/target selected-action negotiation are covered by a shared helper and unit tests.
5. Verified slice: destination-side `wl_data_offer.finish` and `set_actions` validation now follows core Wayland protocol preconditions. `set_actions` is rejected for non-DnD offers, and `finish` is rejected for non-DnD offers, missing accepted MIME type, or missing selected action.
6. Verified slice: source-side `wl_data_source.set_actions` is now accepted only once before source use, non-null sources become single-use after successful `start_drag` or `set_selection`, reused sources raise `wl_data_device.used_source`, and sources that declared DnD actions are rejected from the normal selection path.
7. Verified slice: `wl_data_device.start_drag` now validates the optional drag-icon surface role, assigns a compositor-side `DragIcon` role for accepted icons, and clears that role when the DnD session ends.
8. Verified slice: `wl_data_offer.finish` now marks the offer finished, repeated `finish` is rejected, and post-finish `accept`, `receive`, and `set_actions` requests raise `invalid_offer` while `destroy` remains valid.
9. Verified slice: completed drops now skip the cancellation-style `wl_data_device.leave` and offer destruction path so destinations can still perform `receive` and `finish`; `wl_data_source.dnd_drop_performed` is emitted only after an actual `drop`.
10. Verified slice: if the destination destroys a post-drop DnD offer before `finish`, the source now receives `wl_data_source.cancelled`; pre-drop target switches and post-finish offer destruction do not emit cancellation.
11. Planned: request manual validation if file payloads, external app DnD, or visible drag icons are changed.

**Acceptance criteria:**

- Data offers and sources become inert or are destroyed at the same lifecycle points as wlroots for supported paths.
- Drag/drop action negotiation produces deterministic results and cleans up on cancel.
- Clipboard, primary selection, text DnD, and compatible real-app transfers keep working.

## WM-COMP-25 Layer-Shell Dynamic Behavior Parity

**Why this matters:** WM-COMP-2 fixed the core configure/commit model and the dock-menu crashes, but wlroots' layer-shell behavior also covers dynamic output changes, exclusive-zone recomputation, keyboard interactivity, popup parenting, and map/unmap transitions. Shell topbar and dock stability depend on these details.

**Goal:** audit dynamic layer-shell behavior and make dock/topbar/menu placement and focus robust across fullscreen, output-scale changes, shell restarts, and transient popup surfaces.

**Expected code areas:**

- `apps/lambda-window-manager/Compositor/Wayland/Globals/LayerShell.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/LayerShellState.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/LayerShellZones.cpp`
- `apps/lambda-window-manager/Compositor/Window/LayerShellInput.cpp`
- `apps/lambda-window-manager/Compositor/Window/FullscreenShellPanels.cpp`
- `tests/`

**Implementation steps:**

1. Planned: compare wlroots layer output assignment, configure scheduling, map/unmap, and exclusive-zone invalidation paths.
2. Planned: audit keyboard interactivity behavior for none, exclusive, and on-demand modes.
3. Planned: verify popup/menu interactions with layer surfaces and shell-owned transient surfaces.
4. Planned: add tests for dynamic size/anchor/margin/layer changes, fullscreen panel reservations, focus, and output-scale updates.
5. Planned: request manual Shell dock/topbar/fullscreen/menu validation if placement or focus behavior changes.

**Acceptance criteria:**

- Layer surfaces are placed and focused from committed state after dynamic changes.
- Exclusive zones are recomputed only from mapped, committed layer surfaces.
- Dock, topbar, launcher, quick-status, and dock menus remain stable during fullscreen and resize-heavy sessions.

## WM-COMP-26 Output Layout and Multi-Output Foundation

**Why this matters:** v1 intentionally runs one active output, but wlroots models outputs through an output layout and sends enter/leave plus xdg-output positions from that layout. Lambda's current single-output assumptions should be isolated before they become harder to unwind.

**Goal:** introduce or document the internal seams needed for a future multi-output desktop without enabling multiple active outputs in the daily-driver gate.

**Expected code areas:**

- `apps/lambda-window-manager/Compositor/Wayland/Globals/Output.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/Globals/XdgOutput.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/Snapshots.cpp`
- `apps/lambda-window-manager/Compositor/Window/`
- `apps/lambda-window-manager/Compositor/PresentationLoop.cpp`
- `src/Platform/Linux/KmsOutput.cpp`
- `tests/`

**Implementation steps:**

1. Planned: compare wlroots output-layout responsibilities with Lambda's selected-output runtime.
2. Planned: separate single-output policy from data structures that should already carry logical output position, scale, and transform.
3. Planned: add tests for output enter/leave selection, xdg-output logical position/size calculation, and layer/window placement on a logical layout.
4. Planned: keep multiple active outputs disabled until there is an explicit product decision and target-hardware validation plan.

**Acceptance criteria:**

- Single-output behavior remains unchanged.
- Output and xdg-output protocol paths can describe logical output position and effective size without hardcoded origin assumptions.
- The roadmap still treats full multi-output desktop layout as deferred until manually validated.

## WM-COMP-27 Visual Regression and Real-App Harness

**Why this matters:** the most expensive regressions today are visible timing or app-specific behavior: resize sync, dock/menu stability, fullscreen panel behavior, and external app popups. Automated unit tests catch protocol state, but target-hardware visual workflows still need repeatable scripts and traces.

**Goal:** make real-app validation less ad hoc by recording the exact manual workflows, adding scripted launch/trace helpers where possible, and keeping the pass/fail gates tied to visible behavior.

**Expected code areas:**

- `docs/`
- `tools/` or existing compositor smoke scripts if present
- `apps/lambda-window-manager/Compositor/` trace hooks only where needed
- `tests/`

**Implementation steps:**

1. Planned: document the current real-app validation matrix for Settings, Files, Terminal, Shell, Firefox, GTK, Qt, `foot`, and mpv.
2. Planned: add repeatable scripts for starting the compositor, launching clients, enabling relevant trace variables, and collecting logs.
3. Planned: add deterministic tests for any trace parsing or validation helpers.
4. Planned: keep visual pass/fail criteria explicit: no flicker, no non-synced chrome/content frames after the deferred fix, dock/topbar stable, menus do not crash Shell or compositor, fullscreen panels restore, cursor updates immediately.

**Acceptance criteria:**

- A future compositor change has a clear automated and manual validation checklist.
- Trace collection is repeatable enough to compare before/after behavior on DP-1 HiDPI.
- Manual validation requirements are documented before work that cannot be fully tested in automation is committed.

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
| 2026-05-31 | WM-COMP-10 | In progress | Activation comparison found Lambda ignores token strings during `activate` and leaves committed token resources mutable. Implementing single-use token matching and consumption before serial/focus validation. |
| 2026-05-31 | WM-COMP-10 | Verified slice | Added activation token lifecycle helpers, made committed token resources inert, required `activate` to name a known committed token, and consumed tokens on successful activation. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*activation*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"`, and `git diff --check` passed. |
| 2026-05-31 | WM-COMP-10 | Verified slice | Added activation token commit validation for supplied serials and focused-surface constraints against Lambda's seat serial ledger. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*activation*"`, `./build/tests/lambda_tests --test-case="*seat serial*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"`, and `git diff --check` passed. |
| 2026-05-31 | WM-COMP-10 | Verified | Added wlroots-style 30 second activation token expiry, event-loop timer cleanup, and expired-token lookup rejection. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*activation*"`, `./build/tests/lambda_tests --test-case="*seat serial*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"`, and `git diff --check` passed. |
| 2026-05-31 | WM-COMP-11 | In progress | Pointer-constraints comparison found wlroots commits constraint regions and cursor hints through surface-synchronized state and computes confinement from the committed constraint region intersected with the surface input region. Implementing the synced-state slice. |
| 2026-05-31 | WM-COMP-11 | Verified slice | Added pointer constraint pending/current region and cursor-hint state, cached it with synchronized subsurface commits, dropped cached state on constraint destruction, rebuilt effective regions from committed surface input state, and confined pointer movement to the committed effective region. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*pointer constraint*"`, `./build/tests/lambda_tests --test-case="*subsurface*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"`, and `git diff --check` passed. |
| 2026-05-31 | WM-COMP-11 | In progress | Lifecycle comparison found wlroots destroys the server-side oneshot pointer constraint after sending deactivation and leaves the protocol resource inert. Lambda only marked the object defunct. Implementing inert-resource removal for deactivated oneshot constraints. |
| 2026-05-31 | WM-COMP-11 | Verified | Deactivated oneshot pointer constraints now make the protocol resource inert and remove the server-side constraint object after sending the unlocked/unconfined event. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*pointer constraint*"`, `./build/tests/lambda_tests --test-case="*subsurface*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"`, and `git diff --check` passed. |
| 2026-05-31 | WM-COMP-12 | In progress | Presentation-time comparison found wlroots creates feedback resources using the bound manager version. Lambda already checks manager-bind allocation failure, so implementing the resource-version slice and centralizing the implemented version cap. |
| 2026-05-31 | WM-COMP-12 | Verified | Added a presentation version helper and used it for both manager binding and feedback resource creation so feedback resources inherit the client-bound version. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*presentation*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"`, and `git diff --check` passed. |
| 2026-05-31 | WM-COMP-13 | In progress | Fractional-scale comparison found wlroots uses the same `scale * 120` conversion for all notifications, while Lambda clamped the initial object-created event to at least 1.0. Implementing a shared helper and bind no-memory guard. |
| 2026-05-31 | WM-COMP-13 | Verified | Added a shared fractional-scale version/conversion helper, preserved sub-1.0 preferred scales on newly-created fractional-scale resources, reused the helper for runtime scale updates, and added bind no-memory handling. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*fractional scale*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"`, and `git diff --check` passed. |
| 2026-05-31 | WM-COMP-14 | In progress | Idle-inhibit comparison found wlroots uses the bound manager resource version for inhibitor resources, guards manager bind allocation failure, and keeps idle policy separate from protocol logging. Implementing a shared version/policy helper and removing ad hoc stderr logs. |
| 2026-05-31 | WM-COMP-14 | Verified | Added an idle-inhibit version/policy helper, used the bound manager version for inhibitor resources, added manager bind no-memory handling, removed per-inhibitor stderr logs, and tested the active-surface predicate. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*idle inhibit*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"`, and `git diff --check` passed. |
| 2026-05-31 | WM-COMP-15 | In progress | Output comparison found wlroots gates output events by bound resource version, guards bind allocation failure, and sends legacy `wl_output.scale` as `ceil(output->scale)`. Implementing a shared output version/scale helper and applying it to initial bind plus runtime scale updates. |
| 2026-05-31 | WM-COMP-15 | Verified | Added an output version/scale helper, changed legacy `wl_output.scale` to round fractional scales up with `ceil`, reused it for runtime scale updates, gated output events by bound resource version, and added bind no-memory handling. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*output*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"`, and `git diff --check` passed. |
| 2026-05-31 | WM-COMP-16 | In progress | XDG-output comparison found wlroots keeps xdg-output resources attached to outputs and sends logical-size updates when effective resolution changes. Lambda sends xdg-output details only at creation and does not guard manager bind allocation failure. Implementing resource tracking, done-event selection, and runtime logical-size updates. |
| 2026-05-31 | WM-COMP-16 | Verified | Added tracked xdg-output resources, cleared stale paired `wl_output` links on output-resource destruction, updated xdg-output logical size when runtime output scale changes, selected `zxdg_output_v1.done` versus `wl_output.done` from resource versions, and guarded manager bind allocation failure. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*xdg output*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"`, and `git diff --check` passed. |
| 2026-05-31 | WM-COMP-17 | In progress | Output done comparison found wlroots coalesces output scale and xdg-output logical-size notifications behind scheduled `wl_output.done`. Lambda can now emit a scale done and a separate xdg-output done in the same runtime scale update. Implementing batched done emission. |
| 2026-05-31 | WM-COMP-17 | Verified | Batched runtime scale notifications by sending `wl_output.scale`, then xdg-output logical details with only wl_output done suppressed, then one final `wl_output.done` per output resource. Pre-v3 xdg-output resources still receive `zxdg_output_v1.done`. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*xdg output*,*output*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"`, and `git diff --check` passed. |
| 2026-05-31 | WM-COMP-18 | In progress | Pointer-extension comparison found wlroots removes server-side relative-pointer and pointer-constraint state when the underlying `wl_pointer` resource is destroyed, while leaving the extension protocol resource inert. Implementing pointer-resource cleanup helpers and shared version caps. |
| 2026-05-31 | WM-COMP-18 | Verified | Added shared pointer-extension version helpers, matched extension objects by dependent `wl_pointer`, and changed `wl_pointer` destruction to inert and remove associated relative-pointer and pointer-constraint server objects instead of retaining null-pointer entries. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*pointer extension*,*pointer constraint*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"`, and `git diff --check` passed. |
| 2026-05-31 | WM-COMP-19 | In progress | Cursor-shape comparison found wlroots makes cursor-shape device resources inert when their dependent seat/pointer object is destroyed. Lambda retains cursor-shape devices with raw `wl_pointer` pointers. Implementing dependent-pointer cleanup and bind no-memory handling. |
| 2026-05-31 | WM-COMP-19 | Verified | Added cursor-shape version and dependent-pointer helpers, guarded manager bind allocation failure, created devices at the capped manager version, and changed `wl_pointer` destruction to inert and remove associated cursor-shape devices. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*cursor shape*,*pointer extension*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"`, and `git diff --check` passed. |
| 2026-05-31 | WM-COMP-20 | In progress | Viewporter comparison found Lambda already has commit-time viewport validation, but still hardcodes viewport object resources to version 1 and lacks manager bind no-memory handling. Implementing a shared version helper and bind guard. |
| 2026-05-31 | WM-COMP-20 | Verified | Added a shared viewporter version helper, created `wp_viewport` objects at the capped manager-bound version, and guarded manager bind allocation failure. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case="*viewport*"`, `./build/tests/lambda_tests --test-case="*Compositor*"`, `./build/tests/lambda_tests --source-file-exclude="*RuntimeInputTests.cpp"`, and `git diff --check` passed. |
| 2026-05-31 | WM-COMP-21+ | Planned | End-of-day wrap-up captured the remaining wlroots comparison workstreams: remaining global resource hygiene, xdg toplevel configure-state parity, full seat/grab workflow parity, data-device DnD lifecycle parity, dynamic layer-shell behavior, output-layout foundation, and visual regression/real-app harness work. |
| 2026-06-01 | WM-COMP-23 | Verified slice | Audited the Firefox xdg-popup menu path against wlroots-style seat/grab behavior and fixed popup input routing: popup-first hit testing now honors the grab client, same-client owner-event routing works during popup grabs, implicit pointer-button delivery survives transient popup surface teardown, and popup clicks avoid toplevel raise/keyboard focus changes. Firefox menu actions and click-open submenus passed manual validation. Build passed for `lambda-window-manager` and `lambda_tests`; `./build/tests/lambda_tests --test-case="*popup*"`, `./build/tests/lambda_tests --test-case="*config*"`, `./build/tests/lambda_tests --test-case="*seat serial*"`, `./build/tests/lambda_tests --test-case="*Compositor*" --source-file-exclude="*VulkanRenderTargetTests.cpp"`, and `git diff --check` passed. The unfiltered `*Compositor*` subset still requires a physical Vulkan device for three render-target tests in this environment. |
| 2026-06-01 | WM-COMP-23 | Verified popup-grab parity | Completed the remaining xdg-popup grab parity against wlroots for the implemented pointer/keyboard seat paths: server popup grabs now carry a grab client, seat resource, and popup stack; parent popup grabs remain active under child grabs; popup grabs are rejected after popup commit/map and when already grabbed; outside clicks end the whole active popup grab; popup null-buffer unmap releases its grab; keyboard focus is pinned to the active grabbed popup while the grab is active. Build passed for `lambda-window-manager` and `lambda_tests`; `./build/tests/lambda_tests --test-case="*popup*"`, `./build/tests/lambda_tests --test-case="*seat serial*"`, and `git diff --check` passed. |
| 2026-06-12 | WM-COMP-21 | Verified | Completed the remaining global resource-hygiene pass: core compositor/subcompositor, shm, layer-shell, cutouts, and xdg-decoration manager bind/object paths now guard failed resource allocation, close transferred shm pool fds on pool-resource failure, and use explicit implemented version caps where applicable. The audit found background-effect and xdg object allocation paths already guarded, and existing dependent-resource cleanup already clears the relevant stale pointers. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --source-file='*CompositorGlobalResourceHygieneTests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file='*Compositor*Tests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file-exclude='*RuntimeInputTests.cpp' --no-skip`, and `git diff --check` passed. The unfiltered suite was blocked by `RuntimeInputTests.cpp` requiring a Wayland display in this shell. |
| 2026-06-12 | WM-COMP-22 | Verified slice | Extracted XDG configure acknowledgement and pending-to-current commit into explicit helpers. `ack_configure` now consumes configures through the acknowledged serial, reports whether an in-flight resize configure was included in the consumed range, and keeps the acknowledged configure pending until the next `wl_surface.commit`. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case='*xdg surface*' --no-skip`, `./build/tests/lambda_tests --source-file='*CompositorWindowStateTests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file='*Compositor*Tests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file-exclude='*RuntimeInputTests.cpp' --no-skip`, and `git diff --check` passed. |
| 2026-06-12 | WM-COMP-22 | Verified slice | Shared the pending-configure frame-size decision between snapshots and chrome hit testing. While an XDG toplevel has an uncommitted configure, snapshots, top chrome hit testing, and cutout control hit testing now use the same committed frame dimensions instead of letting hit testing observe a speculative live frame size. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case='*interactive frame size*' --no-skip`, `./build/tests/lambda_tests --source-file='*CompositorWindowStateTests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file='*Compositor*Tests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file-exclude='*RuntimeInputTests.cpp' --no-skip`, and `git diff --check` passed. |
| 2026-06-12 | WM-COMP-22 | Verified slice | Moved `windowGeometryFor` and window-manager transition start-size bookkeeping onto the same pending-configure-safe frame-size helper used by snapshots and chrome hit testing. Snap/drag/maximize/fullscreen bookkeeping no longer starts from a speculative live frame size while an XDG toplevel configure is uncommitted. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case='*interactive frame size*,*window geometry helper*' --no-skip`, `./build/tests/lambda_tests --source-file='*CompositorWindowStateTests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file='*Compositor*Tests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file-exclude='*RuntimeInputTests.cpp' --no-skip`, and `git diff --check` passed. |
| 2026-06-12 | WM-COMP-22 | Verified slice | Extended the pending-configure-safe frame-size helper across remaining high-risk XDG toplevel geometry consumers: state configure sizing, initial/cutout rejection sizing, popup parent geometry, client-initiated resize-grab starts, and top-level pointer hit bounds now agree with snapshots/chrome while a configure is uncommitted. The helper now avoids inventing a fake committed `1x1` size for empty pending toplevels. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case='*interactive frame size*' --no-skip`, `./build/tests/lambda_tests --source-file='*CompositorWindowStateTests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file='*Compositor*Tests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file-exclude='*RuntimeInputTests.cpp' --no-skip`, and `git diff --check` passed. |
| 2026-06-12 | WM-COMP-22 | Verified | Completed the final XDG configure-state geometry consumer audit. The pending-configure-safe display-size decision now lives in a shared Wayland helper used by window-manager frame helpers and pointer-constraint default regions; duplicate resize configure checks, popup screen bounds, diagnostic compositor exercise geometry, pointer hit bounds, and pointer constraints no longer read speculative live XDG toplevel dimensions while a configure is uncommitted. The remaining direct live-size reads are layer-shell, subsurface, cursor snapshot, snapshot-local fallback, core assignment/logging, and cutout trigger-guard paths outside the XDG toplevel configure-state split. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case='*pointer constraint*' --no-skip`, `./build/tests/lambda_tests --test-case='*window geometry helper*,*usable window geometry helper*,*interactive frame size*' --no-skip`, `./build/tests/lambda_tests --source-file='*CompositorWindowStateTests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file='*Compositor*Tests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file-exclude='*RuntimeInputTests.cpp' --no-skip`, and `git diff --check` passed. |
| 2026-06-12 | WM-COMP-23 | Verified slice | Added a shared unmapped-surface seat cleanup helper and wired XDG toplevel null-buffer unmap through it. Unmapped toplevels now clear keyboard focus, pointer focus, pointer-button grabs, focus order, focus-cycle state, seat serial records, and activation-token surface references; pointer constraints are refreshed when pointer focus is removed. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case='*unmapped toplevel cleanup*,*seat serial*' --no-skip`, `./build/tests/lambda_tests --source-file='*CompositorWindowStateTests.cpp' --no-skip`, `./build/tests/lambda_tests --test-case='*pointer constraint*' --no-skip`, `./build/tests/lambda_tests --source-file='*Compositor*Tests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file-exclude='*RuntimeInputTests.cpp' --no-skip`, and `git diff --check` passed. |
| 2026-06-12 | WM-COMP-23 | Verified slice | Routed XDG popup and toplevel role destruction through the shared stale seat cleanup helper after preserving the existing pointer-button grab trace path. Destroyed popup/toplevel roles now clear stale focus/order/cycle state, seat serial records, and activation-token surface references consistently with XDG null-buffer unmap, and pointer constraints are refreshed when pointer focus is removed. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case='*unmapped toplevel cleanup*,*seat serial*' --no-skip`, `./build/tests/lambda_tests --test-case='*pointer constraint*' --no-skip`, `./build/tests/lambda_tests --source-file='*CompositorWindowStateTests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file='*Compositor*Tests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file-exclude='*RuntimeInputTests.cpp' --no-skip`, and `git diff --check` passed. |
| 2026-06-12 | WM-COMP-23 | Verified slice | Added a shared cursor-request serial validation helper used by both `wl_pointer.set_cursor` and cursor-shape `set_shape`. Cursor requests now remain valid for the implicit pointer-button grab client/surface while pointer focus changes, reject non-grab clients during the grab, and still accept same-client focused-surface serials. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --source-file='*CompositorSeatSerialTests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file='*CompositorCursorShapeTests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file='*Compositor*Tests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file-exclude='*RuntimeInputTests.cpp' --no-skip`, and `git diff --check` passed. |
| 2026-06-12 | WM-COMP-23 | Verified slice | Routed layer-shell role destruction through the shared stale seat cleanup helper. Focused Shell layer surfaces that lose their layer role now clear pointer focus, keyboard focus, pointer-button grabs, focus order/cycle state, seat serial records, and activation-token surface references, and pointer constraints are refreshed when pointer focus is removed. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --test-case='*layer*' --no-skip`, `./build/tests/lambda_tests --test-case='*unmapped toplevel cleanup*,*seat serial*' --no-skip`, `./build/tests/lambda_tests --test-case='*pointer constraint*' --no-skip`, `./build/tests/lambda_tests --source-file='*Compositor*Tests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file-exclude='*RuntimeInputTests.cpp' --no-skip`, and `git diff --check` passed. |
| 2026-06-12 | WM-COMP-24 | Verified slice | Factored data-device DnD action-mask validation, preferred-action validation, and source/target selected-action negotiation into `DataDeviceDndState.hpp` and added focused tests for invalid masks, invalid preferred actions, preferred-action selection, fallback order, and no-intersection behavior. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --source-file='*CompositorDataDeviceTests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file='*CompositorSeatSerialTests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file='*Compositor*Tests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file-exclude='*RuntimeInputTests.cpp' --no-skip`, and `git diff --check` passed. |
| 2026-06-12 | WM-COMP-24 | Verified slice | Added destination-side `wl_data_offer` request validation helpers and used them to reject `set_actions` on non-DnD offers and reject `finish` unless the offer is a DnD offer with an accepted MIME type and selected action. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --source-file='*CompositorDataDeviceTests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file='*CompositorSeatSerialTests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file='*Compositor*Tests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file-exclude='*RuntimeInputTests.cpp' --no-skip`, and `git diff --check` passed. |
| 2026-06-12 | WM-COMP-24 | Verified slice | Added source-side data-source lifecycle state and helpers so `wl_data_source.set_actions` is accepted only once before source use, successful non-null `start_drag`/`set_selection` consume the source, reused sources raise `wl_data_device.used_source`, and DnD-action sources cannot be used for normal selection. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --source-file='*CompositorDataDeviceTests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file='*CompositorSeatSerialTests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file='*Compositor*Tests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file-exclude='*RuntimeInputTests.cpp' --no-skip`, and `git diff --check` passed. |
| 2026-06-12 | WM-COMP-24 | Verified slice | Added drag-icon role validation for `wl_data_device.start_drag`: optional icon surfaces must have no existing role, accepted icons receive a compositor-side `DragIcon` role, icon destruction clears the pointer, and ending DnD clears the icon role. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --source-file='*CompositorDataDeviceTests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file='*CompositorSeatSerialTests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file='*Compositor*Tests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file-exclude='*RuntimeInputTests.cpp' --no-skip`, and `git diff --check` passed. |
| 2026-06-12 | WM-COMP-24 | Verified slice | Added post-finish `wl_data_offer` request validation: valid `finish` marks the offer finished, repeated `finish` raises `invalid_finish`, and post-finish `accept`, `receive`, and `set_actions` raise `invalid_offer` while closing receive fds. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --source-file='*CompositorDataDeviceTests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file='*CompositorSeatSerialTests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file='*Compositor*Tests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file-exclude='*RuntimeInputTests.cpp' --no-skip`, and `git diff --check` passed. |
| 2026-06-12 | WM-COMP-24 | Verified slice | Added completed-drop cleanup planning so successful `wl_data_device.drop` no longer immediately sends cancellation-style `leave` or destroys the offer, preserving the destination's ability to `receive` and `finish`; `wl_data_source.dnd_drop_performed` is now gated on an actual sent drop. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --source-file='*CompositorDataDeviceTests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file='*CompositorSeatSerialTests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file='*Compositor*Tests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file-exclude='*RuntimeInputTests.cpp' --no-skip`, and `git diff --check` passed. |
| 2026-06-12 | WM-COMP-24 | Verified slice | Added post-drop offer-abort cancellation: completed DnD offers record that `drop` was performed, and destroying such an unfinished offer sends `wl_data_source.cancelled` while pre-drop target changes and post-finish destroys stay quiet. Build passed for `lambda_tests` and `lambda-window-manager`; `./build/tests/lambda_tests --source-file='*CompositorDataDeviceTests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file='*CompositorSeatSerialTests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file='*Compositor*Tests.cpp' --no-skip`, `./build/tests/lambda_tests --source-file-exclude='*RuntimeInputTests.cpp' --no-skip`, and `git diff --check` passed. |
