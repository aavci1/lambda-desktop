# Flux project status and roadmap

**Last updated:** 2026-05-26
**Purpose:** Single place for current project status, active work, and archived milestone notes.  
**Architecture reference:** [compositor.md](compositor.md) (compositor design and framework log). Current Window Manager readiness work is tracked in [lambda-window-manager-readiness-spec.md](lambda-window-manager-readiness-spec.md).
**How to run:** [compositor-user-guide.md](compositor-user-guide.md), [linux-development.md](linux-development.md).

---

## 1. Project snapshot

| Area | Status |
|------|--------|
| **Flux v5 UI runtime** | Shipped. Retained mount, reactive graph, `Bindable` modifiers, `For`/`Show`/`Switch`. |
| **App platforms** | macOS (Metal), Linux Wayland (Vulkan), Linux KMS (Vulkan). |
| **Examples** | 28+ demo targets; build with `-DFLUX_BUILD_EXAMPLES=ON`. |
| **Linux compositor** | `lambda-window-manager` — KMS + Wayland server + Vulkan paint. Roadmap P0–P3 largely implemented; hardware validation continues. |
| **Desktop shell** | `lambda-shell` — layer-shell UI, shared `Flux::Shell::ShellIpc`, compositor-backed background effects. |

Build the compositor with `-DLAMBDA_BUILD_WINDOW_MANAGER=ON` (Linux only). See [conventions.md](conventions.md) for CMake layout.

---

## 2. Flux v5 — complete

v5 is the current public API under `Flux/Reactive`, `Flux/UI`, and `Flux/SceneGraph`. Applications use declarative `body()` trees that mount once; state changes flow through signals and update retained scene nodes.

**Runtime characteristics**

- `MountRoot` / `MountContext` own the retained tree and environment bindings.
- `Signal`, `Computed`, `Effect`, and `Scope` form the reactive graph.
- `Bindable<T>` on view modifiers installs effects that patch mounted nodes.
- `Window::setTheme()` and compile-time `EnvironmentKey` supply reactive theme and chrome metrics.

**Validation (Stage 9)**

- Tests: `flux_reactive_tests` and full `ctest` pass on normal, ASAN, UBSAN, and TSAN builds.
- All example binaries passed launch-smoke checks.
- `scripts/check_stale_symbols.sh` passes.

**Performance (AmbientLoopLab, `animation-demo`, 60 fps steady state)**

| Bucket | Wall-clock share |
|--------|------------------|
| Signal set | ~0.01% |
| Effect runs (inclusive body) | ~0.17% |
| Poll / propagation / flush | &lt;0.01% each |
| **Inclusive reactive path** | **~0.19%** |

Measured with `FLUX_PROFILE_REACTIVE=ON` (see historical note in git history; detailed tables were archived when v5 perf docs were consolidated).

**Docs for app authors:** [reactive-graph.md](reactive-graph.md), [composites.md](composites.md), [migrating-to-v5.md](migrating-to-v5.md), [ui-view-body-style.md](ui-view-body-style.md).

---

## 3. Linux compositor — current status

Executable: `lambda-window-manager` (`src/Compositor/`). Consumes Flux as a **renderer library** (`VulkanContext`, `Canvas`, `Image::fromDmabuf`) — not `Application::run`.

| Phase | Summary | State |
|-------|---------|--------|
| 1 — First pixels | KMS + Vulkan scanout, atomic page flips | Largely done; some TTY kill-path checks still informal |
| 2 — Wayland server | SHM + dma-buf clients, xdg-shell | Done on hardware smoke |
| 3 — Input + WM | Focus, chrome, move, resize, snap, shortcuts | Done; popup grabs **implemented, config-gated** (`popup_grabs`, default off) |
| 4 — Protocols | layer-shell, viewporter, `ext_background_effect_v1`, clipboard, … | Done in tree; see [compositor-user-guide.md](compositor-user-guide.md) |
| 5 — Polish | Animations, config hot reload, cursor theme, async wallpapers | In progress; adaptive sync / DPMS / multi-output still open |

**Rendering path:** Immediate-mode `Canvas` over immutable `CommittedSurfaceSnapshot` lists — not the app `SceneGraph`.

**Structure:** Runtime split (`CompositorRenderFrame`, `CompositorConfigWatch`, `Presenter`); WM split (`FocusStack`, `PointerRouter`, `InteractiveMoveResize`, …). Protocol codegen via `cmake/FluxWaylandProtocols.cmake`.

**Remaining validation**

- Follow [lambda-window-manager-readiness-spec.md](lambda-window-manager-readiness-spec.md) for the current compositor/window-manager checklist.
- Hardware-validate real-app menus (GTK/Qt/browser), presentation timing under load, selected-output scale behavior, and the GBM/atomic-KMS path.
- Optional **tablet v2** (P3.5); legacy `LAMBDA_WINDOW_MANAGER_PRESENT=vulkan-display` debug presenter may be removed after smoke.

---

## 4. Lambda shell — current status

Process model: `lambda-window-manager` + `lambda-shell` (reconnect if WM restarts). Contract: [lambda-shell-spec.md](lambda-shell-spec.md).

| Surface namespace | Role |
|-------------------|------|
| `lambda.topbar` | Status bar (layer top) |
| `lambda.dock` | Dock (layer overlay) |
| `lambda.command-launcher` | Modal launcher |

Shell UI uses Flux v5 reactive views (`ShellModel` signals, no remount loop). Layer chrome uses `LayerShellChromeOptions` + `BackdropBlur` aligned with production (**P1.5** done).

---

## 5. Completed milestones (archive)

Short record of finished plans removed from `docs/` to reduce clutter.

### Compositor structural cleanup (2026-05-18)

- Split Wayland globals into `src/Compositor/Wayland/Globals/`.
- Pimpl `WaylandServer`; moved runtime to `CompositorRuntime.cpp`.
- Replaced hand-rolled TOML with tomlplusplus.
- Unified resize tracing under `flux::detail::resizeTrace` (`FLUX_RESIZE_TRACE`).
- Window/input logic in `Window/WindowManager.cpp`.

### Compositor bug sweep

- Popup-first hit testing; `SurfaceRole` tag; configure states on xdg-toplevel.
- xkbcommon text input for launcher; `execvp` spawn; config color formats.
- Xcursor parsing without libXcursor; config hot reload scale guard.

### Popup hit-test fix

- `surfaceAt` consults popups before toplevels; geometry covered by unit tests and popup demo.

### Flux v5 stage gates (0–8 complete, 9 validation green)

Stages 0–8 landed retained mounting and reactive control flow. Stage 9 hardening added relayout fixes, environment key migration, interaction signals, and profiling tooling.

### Compositor roadmap P0–P3 (2026-05)

Landed on `window-manager-refactor`: `LayerShellChrome`, `ext_background_effect_v1`, shared `ShellIpc`, WM/runtime splits, presenter abstraction, subsurface hit tests, expanded unit tests, install/user-guide updates, async wallpaper loading, brighter shell glass. Popup grabs behind config (default off).

---

## 6. Active roadmap (reference)

Detailed P0–P3 specs are kept below for history. **Current priorities are in §8.**

Principles and prioritized work items (**P0–P3**). Update the tracking table at the end as items land.
## Principles (non-negotiable)

1. **Enrich Flux first** when a capability is reusable (blur regions, layer-shell chrome, IPC helpers, capability flags on `Window`). Applications declare intent; the compositor interprets generic protocol state.
2. **Backend parity where it makes sense.** Graphics APIs (`Canvas`, `Image`) stay Metal/Vulkan aligned. Linux-only compositor paths (DMABUF, KMS) are explicitly Linux-only with no silent Mac no-ops.
3. **Honest protocols.** Do not advertise `wl_seat` capabilities, globals, or client APIs that are empty stubs.
4. **One source of truth for visuals.** Shell layer surfaces and compositor chrome should share the same framework chrome/blur path instead of parallel color constants in the shell repo and the WM.

---

## Summary roadmap

| Phase | Theme | Outcome |
|-------|--------|---------|
| **P0** | Correctness & honesty | Real menus, truthful seat caps, no dead protocol linkage |
| **P1** | Framework shell chrome | Remove `lambda.*` string policy from WM; unified glass/blur |
| **P2** | Structure & build hygiene | Smaller modules, single protocol build, doc truth |
| **P3** | Polish & session | Themes, tests, install docs, presentation simplification |

Work items below are numbered **`P0.x` … `P3.x`** for tracking. Dependencies are called out per item.

---

## P0 — Correctness and honest capabilities

### P0.1 — xdg-popup input grabs (deferred path)

| | |
|---|---|
| **Owner** | Compositor (`Wayland/Globals/XdgShell.cpp`, `Window/WindowManager.cpp`) |
| **Parity** | Linux compositor only; no Metal API |

**Problem.** Non-grabbing popups render, but real toolkit menus (GTK/Qt) expect `xdg_popup.grab` semantics. Grabs were deferred after hardware lockups.

**Implementation details.**

- Add a compositor config flag, e.g. `[input] popup_grabs = true` (default `false` until hardware-validated).
- Implement grab lifecycle on `WaylandServer::Impl::XdgPopup`:
  - On `xdg_popup.grab(seat, serial)`: mark popup as grabbing; route pointer/keyboard exclusively to popup subtree until dismiss.
  - On dismiss / destroy: release grab to parent popup or toplevel per xdg-shell rules.
  - Reject nested grabs that violate spec (post `xdg_popup.error` as today for other errors).
- Wire `WindowManager` dismissal (`Super+Q`, click-outside) to consult **popup-first** hit test, then grab owner.
- Validate popup grabs against real toolkit menus before enabling them by default.
- Land behind flag first; enable by default only after CachyOS smoke + one real app (e.g. foot context menu if available).

**Key points.**

- Do not reintroduce the old “grab everything on map” path; follow serial + seat binding from the client request.
- Keep non-grabbing popups working when the flag is off (current behavior).
- Log grab enter/exit when `FLUX_RESIZE_TRACE` or a dedicated `LAMBDA_WINDOW_MANAGER_POPUP_TRACE` is set.

**Acceptance.**

- Demo client: submenu stays interactive; outside click dismisses; parent regains focus per spec.
- With `popup_grabs = false`, behavior matches today.
- No WM freeze on grab/ungrab cycle (manual TTY checklist).

**Depends on.** None (highest priority correctness).

---

### P0.2 — Seat capabilities: touch

| | |
|---|---|
| **Owner** | Compositor (`Wayland/Globals/Seat.cpp`) |
| **Parity** | Linux compositor only |

**Problem.** `wl_seat` advertises pointer + keyboard; `get_touch` is an empty lambda. Clients may bind touch and hang or misbehave.

**Implementation details.**

- **Option A (v1 recommended):** Do not advertise `WL_SEAT_CAPABILITY_TOUCH` until touch is implemented; `get_touch` must not be callable.
- **Option B:** Implement minimal touch: `wl_touch` resource, down/up/motion/frame/cancel, map to surface under point, forward through `KmsInputBridge` if libinput touch events exist.
- Document chosen option in [compositor.md](compositor.md) §12.2.

**Key points.**

- Prefer honesty over a stub global.
- If implementing touch, share coordinate transform with pointer path (viewport, surface transform).

**Acceptance.**

- `wayland-info` / demo clients see seat caps consistent with behavior.
- No crash when a client calls `get_touch` on the supported path.

**Depends on.** None.

---

### P0.3 — Remove or implement tablet v2

| | |
|---|---|
| **Owner** | Build + compositor |
| **Parity** | N/A until implemented |

**Problem.** `tablet-v2-protocol.c` is linked into `flux` and `lambda-window-manager`, but no `zwp_tablet_manager_v2` global is registered. `wp_cursor_shape_manager_v1.get_tablet_tool_v2` is a no-op stub.

**Implementation details.**

- **Short term (required):** Remove `tablet-v2-protocol.c` from `flux` target sources; remove from WM unless a global is added. Keep XML in `src/Compositor/Protocols/` for future use.
- **Long term (optional P3):** Register `zwp_tablet_manager_v2`, forward tablet tool motion to cursor shape or pointer focus; only if hardware available.

**Key points.**

- Dead protocol objects confuse audits and increase link size.
- Cursor-shape tablet entry must either work or be absent from the interface version we bind.

**Acceptance.**

- `flux` and `lambda-window-manager` link without unused tablet object files.
- No reference to tablet interfaces in client code until implemented.

**Depends on.** None.

---

### P0.4 — WindowConfig backend capability honesty

| | |
|---|---|
| **Owner** | Framework (`include/Flux/UI/Window.hpp`, `WaylandWindow.cpp`, `MacMetalWindow.mm`, `KmsWindow.cpp`) |
| **Parity** | Mac / Linux Wayland / Linux KMS |

**Problem.** `LayerShellOptions::backgroundBlur`, `layerShell`, `outputName`, etc. are ignored on some backends with no feedback. Mac preview silently diverges from Linux shell.

**Implementation details.**

- Add `struct PlatformWindowCapabilities` (or methods on `platform::Window`):
  - `supportsLayerShell`, `supportsBackgroundBlur`, `supportsOutputSelection`, …
- Populate per backend in `Window` factory paths.
- On `Window` construction, if config requests an unsupported feature:
  - Debug build: `assert` or log once via `FLUX_WARN_UNSUPPORTED_WINDOW_CONFIG`.
  - Release: optional one-line `stderr` when env `FLUX_LOG_WINDOW_CONFIG=1`.
- Document matrix in [conventions.md](conventions.md) and `Window.hpp` comments.

**Key points.**

- Preview on Mac should not set `layerShell` expecting compositor behavior; shell preview uses normal `Window` + local chrome until P1 lands.
- Capabilities are framework API; compositor does not implement Mac layer-shell.

**Acceptance.**

- Table in docs matches runtime queries.
- `lambda-shell` does not rely on ignored fields without comment.

**Depends on.** None.

---

## P1 — Framework-first shell chrome and IPC

### P1.1 — `LayerShellChrome` (framework surface style)

| | |
|---|---|
| **Owner** | Framework API + compositor snapshot consumer |
| **Parity** | Blur: Metal + Vulkan `Canvas`; protocol: Linux Wayland server + client |

**Problem.** Compositor hardcodes `lambda.topbar` / `lambda.dock` for `shellGlassSurface`, square corners, and reserved zones. Shell spec defines visual tokens; preview duplicates fills in `ShellPreviewChrome.hpp`.

**Implementation details.**

- Add to `include/Flux/UI/Window.hpp` (names illustrative):

```cpp
enum class LayerShellChromeStyle : std::uint8_t {
  None,           // client buffer only
  BlurPanel,      // ext_background_effect blur + optional compositor tint
  BlurPanelBorder // blur + compositor-drawn border (dock/top bar)
};

struct LayerShellChromeOptions {
  LayerShellChromeStyle style = LayerShellChromeStyle::None;
  float blurRadius = 32.f;      // maps to effect + Canvas fallback
  Color tint = ...;             // default from theme
  Color borderColor = ...;
  float tintOpacity = 0.48f;
  bool squareBottomCorners = false; // was squareContentCorners for top bar
};
```

- Extend `LayerShellOptions` with `LayerShellChromeOptions chrome`.
- **Client (`WaylandWindow.cpp`):** On commit/configure, if `chrome.style != None`, use `ext_background_effect_surface_v1` for full-surface blur, tint, border, blur radius, and corner radii.
- **Compositor:** Replace `shellGlassSurface` bool in `CommittedSurfaceSnapshot` with background-effect state copied from surface role state (set from protocol, not string compare).
- **Painter (`CommittedSurfacePainter.cpp`):** Drive `drawBackdropBlur` + tint + border from snapshot background-effect state, not `if (shellGlassSurface)`.
- **Remove** all `nameSpace == "lambda.topbar"` branches from `Snapshots.cpp`, `LayerShell.cpp`, `Destroy.cpp` except optional **defaults** when shell sends generic chrome without protocol extension.

**Key points.**

- Default glass appearance must match current compositor + [lambda-shell-spec.md](lambda-shell-spec.md) tokens (blur 32, alphas).
- macOS preview uses `Canvas::drawBackdropBlur` via framework, not `shellGlassFill()` rectangles only.
- Compositor tint is an *enhancement* over client blur region, not a second parallel system.

**Acceptance.**

- `lambda-shell` top bar and dock render consistently across configured outputs and scales.
- Third-party layer-shell client can request blur/tint/border/corner radii without `lambda.*` namespace.
- No `shellGlassSurface` in `WaylandTypes.hpp` after migration (or deprecated one release).

**Depends on.** P0.4 (preview uses capabilities). Optional: P1.2 for clean metadata transport.

---

### P1.2 — Background effect metadata protocol

| | |
|---|---|
| **Owner** | `src/Compositor/Protocols/` + framework client bind |
| **Parity** | Server + Linux Wayland client; no Mac server |

**Problem.** Relying only on `namespace` strings for chrome style is fragile. Shell spec wants capability-based surfaces.

**Implementation details.**

- Extend `ext_background_effect_v1.xml`:
  - one object per `wl_surface`
  - `set_blur_region(region)`, `set_blur_radius(fixed)`, `set_tint(rgba)`, `set_border(rgba)`, `set_corner_radii(fixed, fixed, fixed, fixed)`
- Generate client code into `build/wayland-protocols/`; server code only in WM target.
- Bind from `lambda-shell` in `ShellController` when creating layer surfaces.
- Compositor stores background-effect state on `Impl::Surface`; snapshots copy into `SurfaceBackgroundEffectSnapshot`.

**Key points.**

- Keep v1 small; shell can hardcode style enums in controller.
- Namespace strings remain for debugging and reserved-zone *hints*, not for rendering decisions.

**Acceptance.**

- Shell uses protocol, not WM string table, for glass vs border.
- Demo client can set chrome without being `lambda.dock`.

**Depends on.** P1.1 (snapshot/painter shape).

---

### P1.3 — Exclusive zone and work area (generic layer-shell)

| | |
|---|---|
| **Owner** | Compositor `LayerShell.cpp` + framework client |
| **Parity** | Linux Wayland |

**Problem.** `refreshShellReservedZones()` only recognizes `lambda.topbar` and bottom-anchored `lambda.dock`.

**Implementation details.**

- Trust client `set_exclusive_zone` and `set_anchor` for **all** layer surfaces (already in protocol); remove namespace special cases.
- Shell continues to call `zwlr_layer_surface_v1_set_exclusive_zone` from dock/top bar logic (36px top bar per spec).
- WM publishes combined reserved rects to shell via existing IPC snapshot (`topBarExclusiveZone_`, `dockReservedZone_` → generalize to `reservedEdges` struct).
- `lambda.command-launcher` modal: use layer `keyboard_interactivity` + explicit modal flag from P1.2 or `namespace` suffix convention **documented in shell spec**, with WM treating `keyboard_interactivity == exclusive` as modal for hit-testing (not string-only).

**Key points.**

- Dock “does not reserve work area” in milestone 1 is a **shell policy** (exclusive zone 0), not WM hardcoding.
- Top bar 36px is shell’s `set_exclusive_zone(36)`, not WM magic number tied to namespace.

**Acceptance.**

- Renaming namespace to `com.example.bar` still reserves zone if client sets exclusive zone.
- [lambda-shell-spec.md](lambda-shell-spec.md) updated to say WM does not interpret `lambda.*` for geometry.

**Depends on.** P1.1 (chrome decoupled from names).

---

### P1.4 — Shared shell IPC module

| | |
|---|---|
| **Owner** | Framework or `src/ShellProtocol/` shared static lib linked by WM + shell |
| **Parity** | Linux-only transport; parsing code portable |

**Problem.** `ShellProtocol.cpp` (compositor) and `ShellJson.cpp` (shell) duplicate JSON escaping and field parsing.

**Implementation details.**

- Create `include/Flux/Shell/ShellIpc.hpp` + `src/Shell/ShellIpc.cpp` (or `lambda/shell_protocol.hpp` under `include/Flux/`):
  - Message types as `enum class` + `struct` (hello, refreshState, openCommandLauncher, focusApp, launchApp, …).
  - `std::optional<ShellMessage> parseLine(std::string_view line)`.
  - `std::string serialize(ShellMessage const&)`.
- Compositor `Shell/ShellProtocol.cpp` becomes thin: read fd → `parseLine` → dispatch.
- `lambda-shell` `ShellIpc` uses same parser; delete duplicate `escapeJson` / `jsonStringField` where redundant.
- Add `tests/ShellIpcTests.cpp` (roundtrip, escape, malformed lines).

**Key points.**

- No behavior change to wire format in v1 (still JSON lines).
- Future: schema version field in hello message.

**Acceptance.**

- Single implementation of escape/parse; tests green.
- Fuzz malformed lines without crash.

**Depends on.** None (can parallelize P1.1).

---

### P1.5 — Unify preview and production shell rendering

| | |
|---|---|
| **Owner** | Shell + framework |
| **Parity** | Metal + Vulkan blur |

**Problem.** `ShellPreviewChrome.hpp` draws fake glass with gradients; compositor draws blur + border from painter.

**Implementation details.**

- Replace `shell_preview::wrapTopBar` / `wrapDock` glass rectangles with:
  - `Window` or root `Element` using `BackdropBlur` view (existing `src/UI/Views/BackdropBlur.cpp`) sized to bar/dock bounds, **or**
  - Layer-shell chrome options on a normal window when preview simulates shell (if preview stays single-window).
- Map shell theme tokens from [lambda-shell-spec.md](lambda-shell-spec.md) to `LayerShellChromeOptions` / `Theme` keys.
- Keep `ShellPreviewChrome.hpp` wired to the shared layer-shell glass options so preview chrome and compositor chrome stay visually aligned.
- Keep `wrapTopBar` ZStack **stretch alignment** fix (layout); only replace fill/stroke source.

**Key points.**

- Preview is allowed to differ in *layout container* (one window vs three surfaces) but not in *chrome math*.
- Validate the shared shell chrome/material path through real shell surfaces.

**Acceptance.**

- Side-by-side screenshot or `FLUX_DEBUG_LAYOUT` bounds match between preview and compositor for bar/dock chrome regions.
- Single definition of default tint/border/blur radius in framework theme.

**Depends on.** P1.1.

---

## P2 — Structure, build hygiene, documentation truth

### P2.1 — Split `CompositorRuntime.cpp`

| | |
|---|---|
| **Owner** | Compositor |
| **Parity** | Linux only |

**Problem.** ~1460 lines: presentation loop, config reload, tracing, cursor, render orchestration.

**Implementation details.**

- Extract units (files under `src/Compositor/`):
  - `PresentationLoop.cpp` — KMS poll, frame pacing, present completion, `wp_presentation` feedback hooks.
  - `CompositorRenderFrame.cpp` — build snapshots, call `SurfaceRenderer`, chrome, cursor.
  - `CompositorConfigWatch.cpp` — hot reload, scale change, wallpaper.
- `CompositorRuntime.cpp` retains `runKmsCompositor()` orchestration only (~200–300 lines).
- No behavior change in first commit (move-only).

**Key points.**

- Eases testing presentation math without linking entire Wayland server.
- Follow naming from [roadmap.md §5 (archive)](roadmap.md §5 (archive)) intent.

**Acceptance.**

- Build passes; hardware smoke unchanged.
- Each new file &lt; 600 lines.

**Depends on.** None.

---

### P2.2 — Split `WindowManager.cpp`

| | |
|---|---|
| **Owner** | Compositor `Window/` |
| **Parity** | Linux only |

**Problem.** ~1715 lines mixing focus, shortcuts, drag, resize, snap, layer-shell hits, launcher modal.

**Implementation details.**

- Suggested split:
  - `FocusStack.cpp` — raise, activate, click-to-focus.
  - `PointerRouter.cpp` — hit test order (popup → chrome → toplevel → layer), motion, buttons.
  - `KeyboardShortcuts.cpp` — bindings from config, dispatch table.
  - `InteractiveMoveResize.cpp` — drag, resize, snap preview (uses `WindowGeometry.hpp`).
  - `LayerShellInput.cpp` — layer surface hits, modal exclusivity (post P1.3).
- `WindowManager.cpp` becomes façade calling into units.

**Key points.**

- Extract pure functions already in `WindowGeometry.hpp` first; add tests per §3 compositor gaps and P3.2 below.

**Acceptance.**

- Existing `CompositorWindowGeometryTests` green; add focus-stack tests if extracted logic is pure.

**Depends on.** P1.3 for layer input filedivorce.

---

### P2.3 — Wayland protocol CMake consolidation

| | |
|---|---|
| **Owner** | CMake + `src/Compositor/Protocols/` |
| **Parity** | Client headers for `flux`; server for WM only |

**Problem.** Duplicate generated sources: `build/wayland-protocols/` for clients vs checked-in `src/Compositor/Protocols/*-protocol.c` for server and erroneously for `flux`.

**Implementation details.**

- Single `flux_wayland_protocols` CMake function:
  - Input: XML path, client/server/both.
  - Output: generated `.c/.h` in build dir.
- `flux` links **client** objects only (xdg-shell, layer-shell, viewporter, ext-background-effect, cutouts, decoration, fractional-scale).
- `lambda-window-manager` links **server** objects only.
- Remove checked-in generated `.c` from git over time (generate at build) **or** pin one generated tree and stop double-generating—pick one policy and document in [conventions.md](conventions.md).
- Delete tablet from both targets until P0.3 long-term.

**Key points.**

- Prevents client/server struct drift.
- Shorter clean builds when XML unchanged.

**Acceptance.**

- No protocol `.c` compiled twice; WM and flux both build.
- Changing XML rebuilds only affected targets.

**Depends on.** P0.3.

---

### P2.4 — Presentation backend simplification

| | |
|---|---|
| **Owner** | Compositor `CompositorRuntime` / KMS |
| **Parity** | Linux KMS |

**Problem.** Two presenters: default GBM/atomic KMS vs `LAMBDA_WINDOW_MANAGER_PRESENT=vulkan-display`.

**Implementation details.**

- Introduce `Presenter` interface: `present(RenderTarget&, frameCallback)`.
- Default: `AtomicKmsPresenter` (current default path).
- Legacy: `VulkanDisplayPresenter` behind env flag, documented as debug-only.
- Unify `wp_presentation` timestamp source behind presenter.
- Goal state (optional): remove legacy path once atomic path passes video/game smoke in §12.3.

**Key points.**

- Reduces test matrix for timing bugs.
- Document in [compositor-user-guide.md](compositor-user-guide.md).

**Acceptance.**

- Default path unchanged on hardware.
- Legacy flag still works one release after refactor.

**Depends on.** P2.1.

---

### P2.5 — Documentation and spec alignment

| | |
|---|---|
| **Owner** | `docs/` |
| **Parity** | N/A |

**Problem.** `compositor.md` §1.4 claims SceneGraph usage; §1.9 layout outdated; §2.1 DMABUF status stale; framework log uses “local working tree”.

**Implementation details.**

- Update [compositor.md](compositor.md):
  - §1.4: compositor renders via immediate `Canvas` + snapshots (SceneGraph is app-side).
  - §1.9: reflect actual tree (`CompositorRuntime`, `Wayland/Globals`, no `Scene/`).
  - §2.1: mark DMABUF/SHM reuse done; IOSurface import status.
  - §12.1: require commit SHA for new entries.
- Done: docs index points here.
- When P1/P0 items land, update phase table and [roadmap.md §5 (archive)](roadmap.md §5 (archive)).

**Key points.**

- Living spec must match tree or mislead future contributors.

**Acceptance.**

- No section contradicts `src/Compositor/` layout.

**Depends on.** None (can start immediately).

---

### P2.6 — Subsurface hit testing and coordinate transform

| | |
|---|---|
| **Owner** | Compositor `Window/PointerRouter` (post split), `Core.cpp` |
| **Parity** | Linux only |

**Problem.** Open question in [compositor.md](compositor.md) §12.2: subsurfaces render but pointer routing may not walk subsurface order/transform.

**Implementation details.**

- Centralize `surfaceAt(x, y)` to walk toplevel tree including subsurfaces (z-order, position).
- Unit tests with fake surface trees (no Wayland): point in subsurface vs parent.
- Ensure viewport / surface transform applied consistently with render path in `Snapshots.cpp`.

**Key points.**

- Blocks correct embedded UI (video controls, CSD subsurfaces).

**Acceptance.**

- Demo or test: click on subsurface receives enter/leave on correct `wl_surface`.
- Document closed in §12.2.

**Depends on.** P2.2 (pointer router module).

---

## P3 — Polish, tests, session

### P3.1 — Shell theme bridge to compositor chrome config

| | |
|---|---|
| **Owner** | Shell + compositor config |
| **Parity** | Visual parity Metal/Vulkan |

**Implementation details.**

- Shell reads user theme (light/dark/system) and sends tokens over IPC (`shellTheme` message) or sets layer chrome protocol colors on map.
- Compositor maps tokens to `[chrome]` tint defaults when not overridden in TOML.
- Single table in [lambda-shell-spec.md](lambda-shell-spec.md) references framework theme keys.

**Acceptance.**

- Dark mode toggles shell glass tint on bar/dock without WM namespace hacks.

**Depends on.** P1.1, P1.4.

---

### P3.2 — Deterministic compositor tests (expanded)

| | |
|---|---|
| **Owner** | `tests/` |
| **Parity** | N/A |

**Implementation details.**

- Add tests per §3 compositor gaps and P3.2 below:
  - Config hot reload (scale change, wallpaper path).
  - Layer-shell exclusive zone aggregation (pure function extracted from `LayerShell.cpp`).
  - Snapshot builder: role flags, chrome snapshot, buffer null.
  - Shell IPC roundtrip (P1.4).
  - Presentation feedback bookkeeping (mock presenter).
- Where possible, avoid KMS (headless pure logic only).

**Acceptance.**

- CI runs new tests on Linux build; no GPU required for ≥80% of cases.

**Depends on.** P1.4, P2.2, P2.3.

---

### P3.3 — `Image::fromDmabuf` / IOSurface boundary

| | |
|---|---|
| **Owner** | Framework `Image.hpp`, Metal/Vulkan impl |
| **Parity** | Vulkan Linux; Metal IOSurface optional |

**Implementation details.**

- Document in `Image.hpp`: `fromDmabuf` is Linux-only; returns `nullptr` or `expected` error on Mac.
- Either implement `fromExternalMetal(IOSurface*)` for symmetry or add `Image::canImportDmabuf()` compile-time/platform query.
- Update [compositor.md](compositor.md) §2.1 and §12.1 with final decision.

**Acceptance.**

- Audit script / docs list DMABUF as Vulkan-only without “Metal deferred” ambiguity.

**Depends on.** None.

---

### P3.4 — Install and session documentation

| | |
|---|---|
| **Owner** | `docs/compositor-user-guide.md` |
| **Parity** | N/A |

**Implementation details.**

- Document: seat permissions, `lambda-window-manager` autostart from TTY, shell ordering, `XDG_RUNTIME_DIR` socket path, environment variables table (presenter, traces, validation layers).
- Explicit security note: trusted shell binary; layer namespace not a security boundary without authentication.

**Acceptance.**

- New user can follow guide on CachyOS TTY without reading source.

**Depends on.** None.

---

### P3.5 — Tablet v2 (optional, hardware-gated)

| | |
|---|---|
| **Owner** | Compositor + libinput |
| **Parity** | Linux |

**Implementation details.**

- Register global; map tablet tool to pointer when in proximity; integrate with `wp_cursor_shape` tablet request.
- Only if test hardware available; otherwise remain removed per P0.3.

**Depends on.** P0.3 short-term removal.

---

## Suggested execution order

```text
Wave 1 (correctness):     P0.3 → P0.2 → P0.4 → P0.1 (flagged)
Wave 2 (framework shell): P1.4 ∥ P1.1 → P1.5 → P1.3 → P1.2 (optional)
Wave 3 (structure):       P2.5 ∥ P2.3 → P2.1 → P2.2 → P2.6 → P2.4
Wave 4 (polish):          P3.2 → P3.1 → P3.3 → P3.4
```

**Parallelism.** P1.4 and P0.x are independent. P1.1 blocks P1.5 and P1.3. P2 splits should be move-only first to reduce risk.

---

## Framework changes log discipline

For every item that touches `include/Flux/` or `src/Graphics/` / `src/Platform/`:

1. Implement framework change with tests in `tests/` where applicable.
2. Add a row to [compositor.md](compositor.md) §12.1 with **real commit SHA**, description, and **Mac parity status**.
3. If no Metal parity is required, state why (Linux-only KMS/DMABUF/Wayland).


## 7. Work item tracking

| ID | Summary | Status |
|----|---------|--------|
| P0.1 | xdg-popup grabs | done (config-gated, default off) |
| P0.2 | Seat touch honesty | done |
| P0.3 | Tablet dead code removal | done |
| P0.4 | Window capability flags | done |
| P1.1 | LayerShellChrome in framework | done |
| P1.2 | background-effect material protocol | done |
| P1.3 | Generic exclusive zone | done |
| P1.4 | Shared shell IPC | done |
| P1.5 | Preview/production chrome unity | done |
| P2.1 | Split CompositorRuntime | done |
| P2.2 | Split WindowManager | done |
| P2.3 | Protocol CMake consolidation | done |
| P2.4 | Presenter abstraction | done |
| P2.5 | Doc/spec alignment | done |
| P2.6 | Subsurface hit testing | done |
| P3.1 | Shell theme bridge | done |
| P3.2 | Expanded tests | done |
| P3.3 | DMABUF/IOSurface docs | done |
| P3.4 | Install/session docs | done |
| P3.5 | Tablet v2 (optional) | pending |

---

## 8. Next to implement

Ordered by impact. These are the live backlog items after P0–P3 landed in tree.

### 1. Hardware-validate popup grabs (P0.1 follow-up)

- Set `[input] popup_grabs = true` in compositor config.
- Validate popup grabs against at least one real toolkit menu (GTK/Qt/browser).
- If stable on CachyOS hardware, flip default to `true` and document in [compositor-user-guide.md](compositor-user-guide.md).

### 2. Real-application validation

- Extend beyond `foot`: GTK, Qt, browser clients; confirm layer-shell shell, clipboard, and presentation feedback under normal use.
- See [compositor-user-guide.md](compositor-user-guide.md) “Remaining Work”.

### 3. Presentation and pacing hardening

- Hardware-smoke GBM/atomic-KMS page-flip completion with video or game workloads.
- Adaptive sync and triple-buffering (phase 5 polish).
- Consider removing the legacy `LAMBDA_WINDOW_MANAGER_PRESENT=vulkan-display` path once atomic presenter is proven.

### 4. Session and display polish

- Proper input/session brokering instead of manual `/dev/input/event*` ACLs.
- DPMS / panel power-off (software idle blanking exists; inhibitors supported).
- Multi-output desktop layout (still undecided for v1 vs post-v1).

### 5. Optional: tablet v2 (P3.5)

- Only if test hardware is available; register `zwp_tablet_manager_v2` and wire cursor-shape tablet tools.

### 6. Framework / shell follow-ups

- IOSurface import on Metal remains optional (`Image::fromDmabuf` is Linux-only by design).
- Continue logging framework changes in [compositor.md](compositor.md) §12.1 with real commit SHAs.

**Related docs:** [compositor.md](compositor.md) · [lambda-shell-spec.md](lambda-shell-spec.md) · [compositor-user-guide.md](compositor-user-guide.md)
