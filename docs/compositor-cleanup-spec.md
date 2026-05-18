# Compositor cleanup spec

**Status:** implemented. Resize tracing, pimpl, config/chrome/surface/input/window scaffolding, tomlplusplus config parsing, all current Wayland globals, window/input-management behavior, server lifecycle, snapshot production, dmabuf CPU fallback, frame scheduling, and destroy cleanup have been migrated into concern files.
**Scope:** structural and discipline cleanup of the compositor module in `src/Compositor/`, plus the resize-tracing debt that escaped during the most recent commits.
**Goal:** restore the spec discipline that held during the first wave of work, decompose the god-class and the kitchen-sink `main.cpp` into per-concern files, and reduce the duplication and ad-hoc tracing that accumulated under fast iteration.

This is not new functionality. It is reorganizing what exists.

---

## 1. The motivation

Two rounds of code review have surfaced consistent issues that the framework-changes log and the phase status didn't capture. The structural problems compound: every new protocol added to the existing god-class makes the eventual decomposition harder, and every protocol added without log entries makes the cross-backend discipline harder to enforce.

The compositor has reached real functional completeness — phases 1 through 5 are substantively implemented, real apps work, and the resize stabilization shows that subtle hardware-paced bugs can be debugged and fixed. But the *structure* of the code has not kept pace with its capability. This spec pays down that debt before phase 6 polish work begins, because phase 6 will surface real bugs and a well-structured codebase will absorb them gracefully where the current shape will not.

The seven items below were originally enumerated as cleanup recommendations after the first deep code review. The latest review surfaced three more. All ten are addressed in this spec, ordered by leverage: highest-impact structural changes first, smallest stylistic improvements last.

---

## 2. The ten problems

For reference, each problem is restated with its evidence in the current tree.

### 2.1 Original recommendations

1. **`WaylandServer.cpp` is a 4076-line god file.** Every Wayland protocol implementation lives in one file. 24 destroy-resource callbacks. 22 protocol resource-state types. 19 `wl_global*` members in the `WaylandServer` class. The spec called for one file per global (§1.9); what landed is one file for all of them.

2. **`WaylandServer.hpp` exposes 22 internal types as forward declarations** so that callbacks in the `.cpp` can reach them. The header is full of Wayland C internals (`wl_client`, `wl_resource`, `wl_global`) and 22 friend types. The compositor's main loop only needs ~20 methods; it shouldn't need to know about `XdgToplevel`, `DataOffer`, or `PointerConstraint` as types.

3. **`main.cpp` is 1094 lines mixing eight concerns:** signal handling, math helpers, string helpers, hex/color/gradient parsing, hand-rolled TOML parser, image-fill-mode parsing, keybinding lookup, file-modification detection, cursor drawing (software and hardware), and the actual `main` orchestration. The spec called for `Output/`, `Input/`, `Surface/`, `Scene/`, `Window/`, `Chrome/` subdirectories (§1.9); none exist.

4. **Hand-rolled TOML parser.** Several hundred lines of fragile parsing. tomlplusplus or toml11 are mature header-only libraries that handle the format correctly. Reinvention without justification.

5. **24 near-identical `destroyResource` callbacks** for the various Wayland resource types. Same shape, repeated:
   ```cpp
   void XYZDestroyResource(wl_resource* resource) {
       if (auto* obj = dataFrom<XYZ>(resource)) {
           obj->server->destroyXYZ(obj);
       }
   }
   ```
   A single template eliminates all of them.

6. **Zero unit tests in the compositor module.** Demo clients serve as smoke tests but there is no `tests/CompositorTests.cpp`. The TOML parser, keybinding lookup, snap-math, focus state machine, and surface state transitions are all deterministic, unit-testable, and currently untested.

7. **Hardware-locking deferrals.** xdg-popups and xdg-activation both locked the test laptop on their first implementation attempts. The safe reintroduction path now covers non-grabbing popup rendering and activation-token focus requests with purpose-built smoke demos. Popup input grabs and real-app menu dismissal behavior remain pending because they are the hardware-risky part.

### 2.2 New findings from the latest review

8. **Three separate copies of resize-trace logging.** The four resize-stabilization commits added ad-hoc tracing in three places:
   - `waylandResizeTrace` / `waylandResizeTraceLog` in `src/Graphics/Vulkan/VulkanGpuCanvas.cpp` (env: `FLUX_WAYLAND_RESIZE_TRACE`).
   - `resizeTrace` / `resizeTraceLog` in `src/Platform/Linux/WaylandWindow.cpp` (env: `FLUX_WAYLAND_RESIZE_TRACE`, identical to above).
   - `resizeTraceEnabled` / `traceResizeSurface` in `src/Compositor/WaylandServer.cpp` (env: `FLUX_COMPOSITOR_RESIZE_TRACE`, different env, different format).
   Same pattern, three implementations, two env vars. Pragmatic to ship the resize fix; needs unification now that the fix is verified.

9. **Framework changes log is no longer being maintained.** The most recent four commits added framework-level code (`VulkanGpuCanvas.cpp` swapchain reuse with headroom; `WaylandWindow.cpp` viewporter integration) without entries in `docs/compositor.md` §12.1. The framework changes log is the mechanism that operationalizes the cross-backend discipline rule; when it stops being maintained, the rule stops being enforceable.

10. **Phase status not updated as work lands.** The resize stabilization addresses "interactive move/resize smoothing" which was explicitly listed as pending in phase 5 status. The status still shows it as pending. The status table needs to reflect reality.

---

## 3. The cleanup, phased

This work is structured as nine commits, each independently buildable and testable. Each is small enough to review on its own and large enough to be worth committing separately.

### Commit 1: Unify resize tracing into one utility

The three resize-trace implementations get replaced by a single header-only utility in the framework. Single env var (`FLUX_RESIZE_TRACE`), single log file path, single format-string convention.

```cpp
// Flux: src/Detail/ResizeTrace.hpp (internal, Detail module — header-only utilities)
namespace flux::detail {

inline bool resizeTraceEnabled() {
    static bool const enabled = []() {
        char const* value = std::getenv("FLUX_RESIZE_TRACE");
        return value && *value && std::strcmp(value, "0") != 0;
    }();
    return enabled;
}

void resizeTrace(char const* prefix, char const* format, ...);  // varargs printf-style

}
```

The three existing sites are replaced with calls to `flux::detail::resizeTrace("vulkan-resize", ...)`, `flux::detail::resizeTrace("wayland-window", ...)`, `flux::detail::resizeTrace("compositor", ...)`. The prefix discriminates the source. Single output path. Single env var. One env var change toggles all tracing.

**Framework or compositor?** Framework — two of the three implementations are in framework code (`VulkanGpuCanvas.cpp`, `WaylandWindow.cpp`). The compositor consumes it like any other framework utility.

**Backend parity?** No Metal API surface. The tracing exists because of Vulkan-specific resize behavior; Metal manages drawable size natively. No parity needed.

**Acceptance:** all three trace sites resolve to `flux::detail::resizeTrace`. The old `FLUX_WAYLAND_RESIZE_TRACE` and `FLUX_COMPOSITOR_RESIZE_TRACE` env vars are removed.

**LOC delta:** −80 (consolidation), +50 (new utility), net −30.

### Commit 2: Update the framework changes log retroactively

The four resize commits (c2c3e59, cc2f125, 35fac09, f84b263) added framework-level work that wasn't logged. Add entries to `docs/compositor.md` §12.1:

```
| 2026-05-18 | c2c3e59..f84b263 | Stabilized live resize: Vulkan swapchain reuse with size headroom in VulkanGpuCanvas, wp_viewporter client integration in WaylandWindow, compositor-side resize state machine. | Mac unaffected: CAMetalLayer manages drawable size natively, no swapchain to recreate per resize. No Metal API required. |
| 2026-05-18 | (this spec) | Unified resize-tracing utility into Flux Detail module. | No Metal API surface; tracing exists for Vulkan-specific resize behavior. |
```

Also update phase 5 status to reflect that interactive move/resize smoothing is no longer pending.

**LOC delta:** ~+30 in `docs/compositor.md`, none elsewhere.

### Commit 3: Pimpl `WaylandServer`

Hide the 22 forward-declared internal types and all the protocol-resource vectors behind a single `Impl` struct. The header becomes the compositor's *interface* to the Wayland server, not its implementation.

Before (current):

```cpp
class WaylandServer {
public:
    // 22 forward-declared structs
    struct Surface;
    struct XdgSurface;
    // ... 20 more ...

    // 22 destroy methods
    void destroySurface(Surface*);
    // ... 21 more ...

    // 19 wl_global* members
    // 21 vector<unique_ptr<X>> members
    // Tons of state
};
```

After:

```cpp
// WaylandServer.hpp
class WaylandServer {
public:
    explicit WaylandServer(WaylandOutputInfo output);
    ~WaylandServer();
    WaylandServer(WaylandServer const&) = delete;
    WaylandServer& operator=(WaylandServer const&) = delete;

    [[nodiscard]] char const* socketName() const noexcept;
    [[nodiscard]] int eventFd() const noexcept;
    [[nodiscard]] std::size_t toplevelCount() const noexcept;
    [[nodiscard]] std::vector<CommittedSurfaceSnapshot> committedSurfaces() const;
    [[nodiscard]] std::optional<CommittedSurfaceSnapshot> cursorSurface() const;
    [[nodiscard]] std::optional<SnapPreviewSnapshot> snapPreview() const;
    [[nodiscard]] std::vector<int> duplicateDmabufFds(std::uint64_t surfaceId) const;
    [[nodiscard]] bool copyDmabufToRgba(std::uint64_t surfaceId, std::vector<std::uint8_t>& out) const;

    void dispatch();
    void flushClients();
    void setShortcutBindings(std::vector<ShortcutBinding> bindings);
    void updateAnimations(std::uint32_t timeMs, bool animationsEnabled);
    void sendFrameCallbacks(std::uint32_t timeMs);
    void handlePointerMotion(double dx, double dy, std::uint32_t timeMs);
    // ... rest of the main-loop API, ~20 methods total ...

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
```

`Impl` is defined in `WaylandServer.cpp` and holds everything that's currently in the header: the 22 internal types, the resource vectors, the cursor state, the drag state, the keyboard modifiers, etc. The header drops from 265 lines to ~80.

**This is purely the pimpl pattern Flux uses everywhere else** (`Canvas`, `Image`, `Window`). The compositor was inconsistent with the framework's own conventions.

**Acceptance:** the header is ~80 lines. No internal types or Wayland C internals beyond the bare minimum needed for the public API (the snapshot structs already use plain types). All existing functionality unchanged. Compile clean.

**LOC delta:** ~−180 in header, ~+30 in cpp (for the Impl forwarding), net −150.

### Commit 4: Extract per-protocol decomposition scaffolding

Before any per-protocol code is moved, set up the scaffolding that the moves will fit into. This is a no-op commit in terms of behavior; it establishes the structure.

Create `src/Compositor/Wayland/` with:

- `Server.hpp` and `Server.cpp` — what's left of `WaylandServer` after extraction (the orchestration, the wl_display lifecycle, the socket management, the dispatch loop). Roughly 200 LOC.
- `Globals/` — a directory for one file per Wayland global. Empty at this commit.
- `ResourceTemplates.hpp` — the template `destroyResource<T>` helper (Commit 8 below) and the `ResourceList<T>` registry pattern. Will be populated in subsequent commits but defined here.

Create `src/Compositor/Surface/` for surface state types (the `Surface`, `CommittedSurfaceSnapshot`, etc.).

Create `src/Compositor/Window/` for window-management state (drag, resize, focus, snap).

Create `src/Compositor/Input/` for input dispatch state.

Create `src/Compositor/Chrome/` for window decoration rendering.

These directories are empty at this commit beyond placeholder README files explaining what goes in each. The actual moves happen in subsequent commits.

**LOC delta:** ~+50 (READMEs and headers), 0 functional change.

### Commit 5: Migrate first-tier globals (`wl_compositor`, `wl_surface`, `wl_subcompositor`, `wl_output`, `wl_seat`, `wl_pointer`, `wl_keyboard`)

The core protocol globals that everything else depends on. These move first because they're the most fundamental and other protocols often need to reference them.

For each global, create:

- `src/Compositor/Wayland/Globals/<Name>.hpp` — the protocol class declaration.
- `src/Compositor/Wayland/Globals/<Name>.cpp` — implementation, including static C callbacks.

Each global's class:

```cpp
// Wayland/Globals/WlCompositor.hpp
namespace flux::compositor {

class WaylandServerImpl;  // forward, defined in Server.cpp

class WlCompositor {
public:
    WlCompositor(wl_display* display, WaylandServerImpl* server);
    ~WlCompositor();

    // Methods needed by other protocols or by the orchestrator.

private:
    static void bindCallback(wl_client*, void* data, std::uint32_t version, std::uint32_t id);
    static void createSurface(wl_client*, wl_resource*, std::uint32_t id);
    static void createRegion(wl_client*, wl_resource*, std::uint32_t id);

    wl_global* global_ = nullptr;
    WaylandServerImpl* server_ = nullptr;
    flux::compositor::ResourceList<Surface> surfaces_;
};

}
```

The C callbacks are static methods of the class. They reach the instance via `wl_resource_get_user_data` returning a pointer to a per-resource state struct that includes a back-pointer to the protocol class.

**Surface state and lifecycle move with `wl_compositor`** to `Surface/` (since `Surface` is the central state object).

**Pointer and keyboard state move with `wl_seat`** to `Input/`.

Test by building the compositor and running the SHM demo client. Same behavior, same output, smaller files.

**LOC delta:** ~0 net (code is moved, not added). One file becomes ~10 files.

### Commit 6: Migrate xdg-shell and decoration (`xdg_wm_base`, `xdg_surface`, `xdg_toplevel`, `xdg_decoration_manager`)

The window-protocol globals. These depend on `wl_compositor`/`wl_surface` (already migrated in Commit 5).

**Window-management state moves to `Window/`** during this migration: the drag-, resize-, snap-, focus-tracking state currently in `WaylandServerImpl` moves to a `WindowManager` class that the xdg-toplevel implementation talks to.

**Chrome rendering moves to `Chrome/`** during this migration: the title-bar drawing, close-button hit-testing, etc.

Test by building and running the SHM demo client and a Flux test app. Same behavior.

**LOC delta:** ~0 net.

### Commit 7: Migrate remaining globals

The remaining ~10 globals get migrated to `Wayland/Globals/`:

- `wl_shm` and `zwp_linux_dmabuf_v1` (buffer protocols) → `Wayland/Globals/`.
- `wp_viewporter`, `wp_fractional_scale_manager_v1` (scaling protocols) → `Wayland/Globals/`.
- `wp_cursor_shape_manager_v1` (cursor protocol) → `Wayland/Globals/`.
- `zwp_idle_inhibit_manager_v1` (idle protocol) → `Wayland/Globals/`.
- `zwlr_layer_shell_v1` (layer shell) → `Wayland/Globals/`.
- `wp_presentation` (presentation time) → `Wayland/Globals/`.
- `zwp_relative_pointer_manager_v1`, `zwp_pointer_constraints_v1` (input protocols) → `Wayland/Globals/`.
- `zwp_primary_selection_device_manager_v1`, `wl_data_device_manager` (selection/clipboard) → `Wayland/Globals/`.
- `zxdg_output_manager_v1` (output protocol) → `Wayland/Globals/`.

Each is small (~100-300 LOC). After this commit, `Wayland/Globals/` contains ~17 files; `Wayland/Server.cpp` is purely orchestration (open socket, create globals, dispatch event loop).

Test by running the full set of demo clients. Same behavior across all of them.

**LOC delta:** ~0 net.

### Commit 8: Template the destroy callbacks

Define in `Wayland/ResourceTemplates.hpp`:

```cpp
namespace flux::compositor {

template <typename T>
void destroyResourceCallback(wl_resource* resource) {
    if (T* obj = static_cast<T*>(wl_resource_get_user_data(resource))) {
        obj->onResourceDestroy();
    }
}

// Each resource type defines `void onResourceDestroy()` which routes to
// the owning protocol class's appropriate remove method.

}
```

Each of the 24 destroy callbacks gets replaced with `destroyResourceCallback<TheType>`. The `onResourceDestroy` method on each resource type is a small one-liner that calls back into its protocol class to remove itself from the protocol's ResourceList.

**LOC delta:** ~−250 (removal of 24 callbacks), ~+50 (template + onResourceDestroy implementations), net −200.

### Commit 9: Decompose `main.cpp`

Split into:

- `src/Compositor/main.cpp` (~150 LOC): just the orchestration. Signal handling, the main loop, wiring config to the WaylandServer.
- `src/Compositor/Config/Config.{hpp,cpp}` (~400 LOC): the configuration data structures, the loader, the file-modification detection.
- `src/Compositor/Config/Toml.{hpp,cpp}` (~80 LOC): a thin wrapper over the tomlplusplus library that exposes only the Config-relevant types. Replaces the hand-rolled parser.
- `src/Compositor/Chrome/Cursor.{hpp,cpp}` (~300 LOC): software cursor drawing and hardware cursor image generation.
- `src/Detail/MathHelpers.hpp` (~30 LOC, framework header-only): `clamp01`, `easeOutCubic`, etc. If these are useful to the framework (they are — Flux apps use similar helpers), they belong in Flux's Detail module.
- `src/Detail/StringHelpers.hpp` (~50 LOC, framework header-only): `trim`, `lowerAscii`, the hex/color parsing utilities. Same reasoning.

The math and string helpers being moved into Flux gives them framework-wide visibility and Mac parity automatically (header-only, no platform dependency).

**Acceptance:** `main.cpp` is ~150 lines. Each concern has its own home. tomlplusplus is the TOML parser. No regressions in the existing demo clients or in config hot-reload.

**LOC delta:** main.cpp drops by ~900. Total: ~+50 (organization), net −850 in compositor; ~+80 added to Flux Detail.

### Commit 10: Add the tomlplusplus dependency

This is technically part of Commit 9 but is called out separately because it's the only commit that adds an external dependency.

Add tomlplusplus as a header-only library in the compositor's CMakeLists. The hand-rolled parser code is removed in Commit 9.

Document the addition in `docs/compositor.md`: external dependency on tomlplusplus (header-only).

**LOC delta:** ~−400 (hand-rolled parser removal), +(0) (tomlplusplus is header-only and vendored or fetched).

---

## 4. Tests

After the structural work, add a `tests/CompositorTests.cpp` file with unit tests for the deterministic parts:

### 4.1 Config parsing

```cpp
TEST_CASE("CompositorConfig parses minimal TOML") {
    CompositorConfig config = parseConfigString("");
    CHECK(config.animations == AnimationsConfig::default_());
}

TEST_CASE("CompositorConfig parses background.color") {
    auto config = parseConfigString("[background]\ncolor = \"#ff0000\"\n");
    REQUIRE(config.background.color.has_value());
    CHECK(config.background.color->r == 1.0f);
}

// ... etc, covering each config field, malformed inputs, etc.
```

### 4.2 Keybinding parsing

```cpp
TEST_CASE("Keybinding string parses to ShortcutBinding") {
    auto binding = parseShortcut("super+q");
    REQUIRE(binding.has_value());
    CHECK(binding->meta);
    CHECK(binding->key == kKeyQ);
}

TEST_CASE("Keybinding rejects unknown modifiers") {
    CHECK_FALSE(parseShortcut("hyper+q").has_value());
}
```

### 4.3 Snap math

```cpp
TEST_CASE("Drag-near-left-edge produces left half snap") {
    SnapState state;
    auto result = computeSnapTarget(state, /*output*/ {1920, 1080}, /*pointer*/ {2.f, 540.f});
    REQUIRE(result.has_value());
    CHECK(result->x == 0);
    CHECK(result->width == 960);
}
```

### 4.4 Focus state machine

```cpp
TEST_CASE("Focus restores to previous window when current closes") {
    WindowFocusStack stack;
    stack.push(windowA);
    stack.push(windowB);
    stack.remove(windowB);
    CHECK(stack.current() == windowA);
}
```

### 4.5 Surface state transitions

The xdg_surface configure/ack-configure cycle has well-defined invariants. Test them.

### 4.6 LOC delta for tests

~300 LOC of tests, no production code change. Should pass on first run because the production code already works — the tests document and protect the existing behavior.

---

## 5. The hardware-locking bugs

xdg-popups and xdg-activation were deferred because their first implementations locked the test laptop. The safe first stage is now implemented and hardware-smoked: popups render and dismiss without input grabs, activation can raise/focus a target window without lockups, and `wl_subcompositor`/basic subsurfaces are present so clients such as `foot` can start real popup/menu testing. Popup input grabs and real-app menu behavior remain pending.

After the per-protocol decomposition (Commits 4-7), reintroducing xdg-popups is contained: a new file in `Wayland/Globals/XdgPopup.cpp`, with no risk to other protocols. The implementation can have aggressive sanity-checking that, if it goes wrong, fails to construct the popup rather than spinning the compositor.

### 5.1 Plan for safely reintroducing xdg-popups

1. **Implement the protocol stub without any input grab.** Just respond to `get_popup` by creating a popup object that's tracked but renders as a regular surface (no popup-specific behavior). This isolates the protocol implementation from the input-grab logic that likely caused the freeze. Implemented and smoked with `flux-compositor-popup-demo`.

2. **Render and dismiss the popup correctly without grab.** Popups have positioning constraints (anchor, gravity, slide-along-edge fallback). Implement these statically — given a parent and a positioner, compute the popup's screen position. Outside-click and Escape dismissal send `popup_done` and unmap the popup. No animations, no grab. Implemented and smoked with `flux-compositor-popup-demo`.

3. **Add the input grab last.** Wayland's grab semantics are subtle: the popup's input grab is bounded (it can be "interactive" or "non-interactive"), and a misbehaving grab can starve the rest of the input system. Implement with explicit timeouts on grab acquisition; if a popup has held an input grab for more than N seconds without user interaction, force-release and log.

4. **Test with a known-good client.** Don't write a new test popup. Use `foot`'s right-click menu or a simple GTK4 app's combobox dropdown. If these work, popups work.

### 5.2 Plan for safely reintroducing xdg-activation

Activation is simpler: a client requests focus, optionally with a token from a previous focus event. The lockup likely came from a focus loop (activation triggers a focus event which triggers another activation, etc.). A simple loop-detection denies repeated requests for the same surface within a short window. Implemented and smoked with `flux-compositor-activation-demo`.

### 5.3 Acceptance

- ✗ `foot` terminal right-click menu opens and dismisses correctly.
- ✗ A GTK4 dropdown menu (e.g., `gnome-text-editor` font picker) works.
- ✗ Closing a popup by clicking outside it works.
- ✗ Closing a popup by pressing Escape works (if the client supports it).
- ✗ Compositor remains responsive even when a buggy client holds a popup grab; force-release fires after N seconds.

**This is two commits separated from the structural cleanup:** Commit 11 (xdg-popups) and Commit 12 (xdg-activation). They happen after Commits 1-10 land so the per-protocol structure is in place.

---

## 6. The order of operations matters

Commits 1-2 are independent of the structural work and unblock the spec-discipline restoration. Do them first.

Commits 3-7 form a coherent structural refactor. Do them in order; each commit should build clean and pass the demo clients.

Commits 8-10 are quality-of-life improvements that build on the new structure.

Commits 11-12 (the hardware-locking bug reintroductions) come after the structure is in place.

Tests (§4) land alongside or after Commit 10. They protect against regressions during the popup/activation work.

---

## 7. The standing rule, restated

The framework changes log (in `docs/compositor.md` §12.1) is the operational artifact for the cross-backend discipline. **Every commit that touches framework code in service of compositor work logs an entry.** When the entry says "no Mac parity needed because X," X is the reason. When the entry says "deferred until consumer surfaces," it's deferred explicitly, not silently.

The phase status table (§12) reflects current reality. When a pending item is addressed, the status updates in the same commit (or the commit immediately after).

These are operational details. They look like overhead. They are not: they are the discipline that prevents "60-day-old changes that no one remembers" from accumulating. The first wave of work held this discipline; the most recent four commits did not. This spec restores the discipline.

---

## 8. LOC summary

| Commit | Description | LOC delta |
|--------|-------------|-----------|
| 1 | Unify resize tracing | ~−30 |
| 2 | Framework changes log update | ~+30 (docs) |
| 3 | Pimpl WaylandServer | ~−150 |
| 4 | Decomposition scaffolding | ~+50 |
| 5 | Migrate first-tier globals | ~0 net |
| 6 | Migrate xdg-shell and decoration | ~0 net |
| 7 | Migrate remaining globals | ~0 net |
| 8 | Template destroy callbacks | ~−200 |
| 9 | Decompose main.cpp | ~−850 in compositor; ~+80 in Flux |
| 10 | tomlplusplus dependency | ~−400 |
| Tests | Add CompositorTests.cpp | ~+300 |
| 11 | Reintroduce xdg-popups | ~+400 |
| 12 | Reintroduce xdg-activation | ~+150 |

**Net effect on the compositor module:** ~−1100 LOC removed, ~+850 reorganized into 25+ files of 100-300 lines each, ~+550 new functionality (popups, activation, tests).

The compositor goes from "two big files plus demo clients" to "thirty small files plus demo clients." The complexity isn't gone — it can't be, Wayland has 19+ protocols — but it's now distributed in a way that lets each protocol be reasoned about independently.

---

## 9. Acceptance for the whole cleanup

After all 12 commits land:

- ✓ The remaining server implementation is split by concern: `WaylandServer.cpp` is a 113-line pimpl forwarding layer, `Wayland/ServerLifecycle.cpp` owns socket/display/global lifecycle, `Wayland/Snapshots.cpp` owns snapshot production and dmabuf CPU fallback, `Wayland/FrameScheduler.cpp` owns geometry animations and frame/presentation callbacks, and `Wayland/Destroy.cpp` owns resource cleanup.
- ✓ `src/Compositor/Wayland/Globals/` has per-protocol files for activation, core `wl_compositor`/`wl_surface`/`wl_subcompositor`, cursor-shape, fractional-scale, idle-inhibit, layer-shell, linux-dmabuf, output, pointer extensions, presentation, seat, selection/clipboard/DnD, shm, viewporter, xdg-output, and xdg-shell/decoration.
- ✓ `src/Compositor/main.cpp` is a small signal-handling entry point; KMS compositor orchestration lives in `src/Compositor/CompositorRuntime.cpp`, with config/chrome/surface/input concerns extracted.
- ✓ `src/Compositor/Config/` holds configuration loading and TOML integration.
- ✓ `src/Compositor/Surface/`, `Window/`, `Input/`, `Chrome/` each hold their respective concerns, including `Window/WindowManager.cpp` for focus, drag, resize, snap, keyboard shortcuts, and pointer forwarding.
- ✓ `WaylandServer.hpp` exposes the public interface behind pimpl and is now 56 lines; public snapshot, cursor, output, and shortcut types live in `Wayland/WaylandTypes.hpp`.
- ✓ Zero hand-rolled TOML parser in the compositor config loader; tomlplusplus dependency added.
- ✓ Duplicate destroy-resource callbacks are reduced and migrated protocol resources use the shared template where they own compositor state.
- ✓ Single resize-tracing utility in Flux Detail module.
- ◐ Deterministic compositor tests exist for config parsing/keybindings and snap/resize geometry; focus and surface state tests remain future coverage work because they still require a Wayland resource harness.
- ◐ xdg-popups work in the popup smoke demo and `foot` launches against the compositor; broader real-app menu behavior remains part of validation.
- ✓ xdg-activation works without lockups in the purpose-built activation smoke demo.
- ✓ Framework changes log up to date through this work, including the four resize commits.
- ✓ Phase status reflects current reality for completed cleanup pieces.
- ✓ Module dependency audit passes.
- ✓ All existing demo clients build after this cleanup pass.
- ✓ Existing compositor demo clients and the `solitaire-app` KMS build target compile after the cleanup split; runtime smoke testing remains a hardware checkpoint.

---

## 10. Scope estimate

- Commits 1-2: half a day (mechanical).
- Commit 3 (pimpl): half a day (mechanical but careful).
- Commits 4-7 (structural decomposition): 2-3 days (the bulk of the work).
- Commit 8 (template destroy): half a day.
- Commits 9-10 (main.cpp decomposition + TOML library): half a day.
- Tests: half a day.
- Commit 11 (popups): 1-2 days (real new work with hardware risk).
- Commit 12 (activation): half a day.

Total: roughly 6-8 days of focused work. Larger than originally framed because the resize-tracing unification and discipline restoration were added.

**Worth doing now.** Phase 6 polish will surface bugs that the current structure cannot accommodate cleanly. Doing the structural work first makes phase 6 work productive instead of frustrating.

---

## 11. Bottom line

The compositor has reached functional completeness. It needs a structural pass to match. This spec pays down the debt accumulated during fast iteration, restores the spec discipline that lapsed in recent commits, and unblocks the deferred popup/activation work that real apps require.

After this lands, phase 6 polish work can proceed against a structure that supports it.
