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
| P0 | WM-COMP-1 Surface commit state core | Planned | Start with pending/current state inventory and a testable migration skeleton | Existing compositor tests plus new state-transition tests | Settings app resize on DP-1 HiDPI; system titlebar and content stay in sync |
| P1 | WM-COMP-2 Layer shell configure and state correctness | Planned | Starts after WM-COMP-1 reaches its automated or manual gate | Layer-shell protocol and geometry tests | Dock/topbar visual behavior if geometry changes affect shell chrome |
| P2 | WM-COMP-3 Subsurface state, order, and synchronized commits | Planned | Starts after WM-COMP-2 reaches its gate | Subsurface commit/order/hit-test tests | Real apps with popovers or embedded subsurfaces if automated coverage is incomplete |
| P3 | WM-COMP-4 Scene and output damage architecture | Planned | Starts after WM-COMP-3 reaches its gate | Snapshot/damage tests plus render scheduler tests | DP-1 resize trace, real-app flicker check, video/browser pacing |
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
- `apps/lambda-window-manager/Compositor/Wayland/Snapshots.cpp`
- `tests/`

**Implementation steps:**

1. Inventory existing pending/current surface fields and update this plan with the exact migration order.
2. Add a small state object or transaction helper for core `wl_surface` commit data without changing behavior.
3. Move buffer, scale, transform, offset, damage, opaque region, input region, viewport source, and viewport destination into the explicit pending/current path.
4. Move role-synchronized xdg state reads so snapshots and compositor chrome consume one committed view of a surface.
5. Add tests for state-only commits, buffer plus viewport atomicity, frame callback delivery, and configure-ack commit behavior.
6. Run targeted compositor tests and the feasible full test suite.
7. If tests fully cover the change, commit and push. If visual timing still needs target hardware, mark this workstream waiting for validation.

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
- `apps/lambda-window-manager/Compositor/LayerShellZones.cpp`
- `tests/`

**Implementation steps:**

1. Add layer configure serial tracking and `ack_configure` validation.
2. Move `set_size`, `set_anchor`, `set_margin`, `set_exclusive_zone`, `set_keyboard_interactivity`, and `set_layer` into pending layer state.
3. Apply pending layer state only on the surface commit that commits the acknowledged configure state.
4. Recompute exclusive zones and layer placement from committed state.
5. Add tests for invalid serials, delayed acks, pending property changes, and dock/topbar geometry.

**Acceptance criteria:**

- Invalid layer-shell ack serials are rejected.
- Layer placement and exclusive zones update from committed state, not request-time state.
- Dock and topbar layout tests pass without relying on timing side effects.

## WM-COMP-3 Subsurface State, Order, and Synchronized Commits

**Why this matters:** wlroots' subsurface implementation keeps parent and child state coherent through synchronized commit rules. Missing or partial subsurface semantics can cause popover, embedded view, and hit-test bugs in real clients.

**Goal:** implement wl_subsurface pending/current position, stacking order, and sync/desync commit behavior.

**Expected code areas:**

- `apps/lambda-window-manager/Compositor/Wayland/Globals/Core.cpp`
- `apps/lambda-window-manager/Compositor/Wayland/Snapshots.cpp`
- `apps/lambda-window-manager/Compositor/Window/PointerRouter.cpp`
- `tests/`

**Implementation steps:**

1. Inventory current subsurface support and missing protocol requests.
2. Add pending/current position and sibling order state.
3. Implement `set_sync`, `set_desync`, `place_above`, and `place_below`.
4. Cache synchronized subsurface commits until the parent commits.
5. Update snapshots and hit testing to use committed subsurface order.
6. Add tests for synchronized commit release, desynchronized immediate commit, ordering, and hit testing.

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

