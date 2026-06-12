# Lambda Compositor

**Status:** phases 1–5 are implemented for Lambda demos and initial real-app testing. This document is now the architecture/history reference; active daily-driver readiness work is tracked in [roadmap.md](roadmap.md).
**Repository:** `lambda-window-manager` is currently built from this repository as `lambda-window-manager` while the Lambda-side KMS API settles.
**Scope:** a Linux Wayland compositor built on Lambda. Launched from a TTY, owns the display, hosts Wayland clients, manages windows, exits on signal.

**Out of scope, deliberately:** display-manager functionality (greeter, PAM, login). Session lifecycle (the compositor *is* a session, it doesn't manage sessions). Lock screen. Logout. Full multi-monitor desktop layout. Tab grouping. Window gluing. Accessibility. Input methods. Touch-specific shell behaviors. Form factors beyond desktop. These are all real concerns and all explicitly outside this spec.

**Next work:** see [roadmap.md](roadmap.md) for the current Window Manager readiness backlog and broader Lambda desktop project status. The detailed direct-implementation parity backlog is tracked in [compositor-wlroots-improvement-plan.md](compositor-wlroots-improvement-plan.md).

---

## How to read this document

This is a single living document covering the entire compositor effort. It is intended to be **edited in place as implementation progresses**. The structure:

- §1 fixes the architectural decisions that hold across all phases.
- §2 defines the framework-vs-compositor boundary, with the rule that anything generally useful goes into Lambda (and gains Metal/Vulkan parity).
- §3 covers the cross-cutting development workflow.
- §4 through §8 are the five phases. Each phase has: goal, scope, deliverables, framework changes required, acceptance criteria, and a **status line** that gets updated as work progresses.
- §9 collects deferred work and the reasoning for deferral.
- §10 lists known risks and what de-risks them.

When a phase completes, update its status. When a decision is revised in light of implementation experience, edit the corresponding section in place rather than appending. Keep the spec true.

---

## 1. Architectural commitments

These hold across all phases. Implementation may surface reasons to revise; if so, edit this section to reflect what was actually decided.

### 1.1 Process model

One process. The compositor is a single executable, `lambda-window-manager`, launched from a TTY. It owns the display via KMS for the duration of its run. It exits on SIGINT/SIGTERM. There is no system service, no privileged-client model, no state machine for session phases. It is a user-session compositor, the same way Sway is.

### 1.2 Single selected output (v1)

The compositor handles exactly one active output. Full multi-output layout is not in v1. The KMS code paths in Lambda enumerate connected outputs, and the compositor can select one by connector name, 0-based index, `primary`, or `secondary` through `--output`, `LAMBDA_WINDOW_MANAGER_OUTPUT`, or `output = "..."` in the config.

### 1.3 Vulkan-only

Linux only. The compositor doesn't run on Mac. Code that exists in the compositor repo is Vulkan-only by definition; framework changes that benefit the compositor must still maintain Metal parity per §2.

### 1.4 Lambda as renderer-component

The compositor consumes Lambda as a library, not as a framework. It does not call `Application::run` or use Lambda's window/event model. It uses:

- `VulkanContext` for shared Vulkan device state.
- `RenderTarget` for rendering into KMS framebuffers it owns.
- `Image::fromExternalVulkan` and `Image::fromDmabuf` for importing Wayland client buffers.
- `Canvas` for immediate-mode compositor rendering (background, client surfaces, chrome, cursor).
- `CommittedSurfaceSnapshot` + `SurfaceRenderer` for Wayland client buffer presentation (not Lambda `SceneGraph`).

It does *not* use Lambda's `Window`, `Application`, `Element`/`View`, or `SceneGraph`. The compositor has its own main loop, input dispatch, and snapshot-driven render path. Lambda apps build UI from declarative views; the compositor builds frames from Wayland surface state each tick.

### 1.5 No wlroots

Wayland protocols implemented directly against `libwayland-server`. DRM via `libdrm`. Input via `libinput`. Keyboard layouts via `libxkbcommon`. DMABUF import via standard Vulkan extensions.

This is a real cost — wlroots solves many edge cases we'll re-encounter — but it's the path that lets the compositor stay coherent with Lambda's design rather than adopting wlroots' opinions about scene graphs, output management, and surface state.

The direct implementation must still be held against wlroots behavior. As of 2026-06-12, the verified comparison pass covers surface commit-state slices, layer-shell state, subsurface sync, scene damage, seat serials, dmabuf lifetime, popup/xdg lifecycle, activation, pointer constraints, presentation-time, fractional-scale, idle-inhibit, output/xdg-output runtime updates, pointer-extension cleanup, cursor-shape cleanup, viewporter resource hygiene, remaining global resource hygiene, XDG configure/frame-size parity, DnD lifecycle parity, WM-COMP-23 popup/cursor/seat-cleanup/pointer-button-grab/keyboard-focus/seat-resource slices, layer-shell dynamic behavior helper/state slices, and output-layout foundation including output-enter, layer-placement, and window-geometry helpers. Remaining spec work:

- Resume the deferred frame-coherence issue when ready: system-titlebar and client content must render from one committed geometry model so Settings resize on DP-1 HiDPI cannot show momentary non-synced titlebar/content widths.
- Compare larger wlroots workflows that are not fully covered by narrow resource tests: remaining seat focus/grabs.
- Continue the repeatable visual/real-app validation harness for resize, dock/topbar, menus, fullscreen, browser/video, GTK/Qt, `foot`, and Lambda apps; the current runner can launch the matrix and optionally own a traced compositor session, while target-hardware visual acceptance remains open.

The ordered implementation plan and validation gates live in [compositor-wlroots-improvement-plan.md](compositor-wlroots-improvement-plan.md). The daily-driver gate lives in [roadmap.md](roadmap.md).

### 1.6 No X11 / no XWayland

Pure Wayland. Apps that don't support Wayland natively don't run. This is a deliberate scope cut.

### 1.7 Stacking window management

Windows stack. Click-to-focus. Drag title bar to move. Drag corner to resize. Snap to halves or quarters by moving within a few pixels of an output edge/corner and pausing briefly. No tiling, no tags, no workspaces in v1. Tiling can be a future addition; workspaces too.

### 1.8 Hybrid compositor chrome

The compositor supports three window-chrome tiers. Clients that do not bind `xdg_decoration_v1` keep client-side decoration and receive no compositor title bar. Clients that accept server-side decoration get compositor chrome above the client buffer. Clients that accept server-side decoration and bind `xx_cutouts_v1` render their own title-bar content inside the surface while the compositor overlays only the reserved close/minimize controls cutout.

Chrome cutouts, input routing, and chrome config keys are now covered by the roadmap and the compositor user guide.

### 1.9 Repository layout

Compositor sources live under `apps/lambda-window-manager/Compositor/` in this repository (target `lambda-window-manager`):

```
lambda-desktop/
├── CMakeLists.txt
├── cmake/
│   └── LambdaWaylandProtocols.cmake   # client/server protocol generation helper
├── docs/
│   ├── compositor.md
│   └── roadmap.md
├── apps/
│   ├── lambda-window-manager/
│   │   ├── CMakeLists.txt
│   │   └── Compositor/
│   │   ├── main.cpp
│   │   ├── CompositorRuntime.cpp      # KMS loop orchestration
│   │   ├── PresentationLoop.cpp       # output selection, pacing helpers
│   │   ├── CompositorRenderFrame.cpp  # snapshot → Canvas render path
│   │   ├── CompositorConfigWatch.cpp  # hot reload
│   │   ├── Presenter.cpp              # atomic-KMS vs Vulkan-display
│   │   ├── WaylandServer.{hpp,cpp}
│   │   ├── Wayland/Globals/           # one file per Wayland global
│   │   ├── Wayland/Snapshots.cpp      # committed surface snapshots
│   │   ├── Window/                    # WM façade + FocusStack, PointerRouter, …
│   │   ├── Surface/                   # client surface draw path
│   │   ├── Chrome/                    # title bar / glass rendering
│   │   ├── Config/                    # TOML config + apply
│   │   └── Protocols/                 # protocol XML (+ generated server .c)
│   └── Platform/Linux/                # KMS device/output/input
└── tests/                             # deterministic compositor unit tests
```
---

## 2. The framework-vs-compositor boundary

A standing rule for this work: **anything generally useful goes into Lambda. Anything Lambda-side gains Metal/Vulkan parity per the established pattern.**

The compositor will surface needs the framework doesn't currently meet. When that happens:

1. **Identify whether the need is general.** "Render into a caller-supplied image" is general (`RenderTarget`). "Import an external GPU resource" is general (`Image::fromExternal*`). "Track which Wayland surface owns this scene node" is compositor-specific.

2. **If general, the framework gets the feature.** Add it to Lambda, with Metal and Vulkan parity. Tests on both backends. Audit script continues to pass. This is the established discipline: see `lambda-window-manager-api-spec.md` for the model; see the recent `VulkanFrameRecorder` work for the pattern of "Vulkan needs it; Mac already has something analogous; bring both into alignment."

3. **If compositor-specific, it stays in the compositor repo.** Window decoration rendering is compositor-specific. Surface-state tracking is compositor-specific. Wayland protocol handling is compositor-specific.

4. **When the framework gains a feature for this work, it's tested via Lambda's own test suite, not via the compositor.** The compositor uses the feature; tests live next to where the feature lives. This means the compositor doesn't gate framework improvements — they land in Lambda first, are tested in Lambda, then are used by the compositor.

The pattern from `RenderTarget`: I claimed the API was "Vulkan only because the compositor is Linux." You pushed back: "I want consistency between backends." We added Metal parity. Result: Mac gains headless rendering for tests, and the framework's mental model is uniform across backends. That outcome is the template for every subsequent framework change driven by compositor work.

### 2.1 Known framework changes already required

These are the framework changes the compositor needed; most have landed:

- **DMABUF import on Linux (done).** `Image::fromDmabuf(...)` imports client dma-buf buffers on Linux/Vulkan (`LAMBDA_VULKAN`). It is not available on macOS/Metal builds. `Image::fromExternalVulkan` remains for caller-owned Vulkan images. IOSurface import on Metal is optional and not required for the compositor.

- **Output management beyond Window (done).** `KmsDevice` / `KmsOutput` / `KmsAtomicPresenter` let the compositor own KMS outputs without Lambda `Window`.

- **Frame pacing decoupled from Window (done).** The compositor main loop drives presentation; Lambda `Application::run` is unused.

- **Shell IPC, layer chrome, window capabilities (done).** See [roadmap.md](roadmap.md) archived completed work and §12.1 below for commit-level history.

These are listed here for visibility. Each phase's framework-changes section identifies which of these (or new ones) land with that phase.

---

## 3. Development workflow

### 3.1 Hardware

Target: AMD Ryzen 5 4600H, Radeon Vega (integrated, AMDGPU/Mesa), 16 GB RAM, CachyOS.

The CachyOS box runs no DE, no display manager, no Xorg. It boots to TTY. SSH is available from the development Mac. The compositor runs from TTY1; emergency access via TTY2 or SSH.

### 3.2 Edit-on-Mac, build-and-run-on-Linux

For everything beyond trivial. Git remotes on both machines; push from Mac, pull on the box, build, run. For tight iteration, `rsync` the working tree between commits.

### 3.3 Safety procedures for running the compositor

Before launching the compositor for the first time after any non-trivial change:

1. Ensure an emergency TTY is logged in. From the physical console: Ctrl+Alt+F2 reaches TTY2; log in there.
2. Ensure SSH from Mac works (last-resort kill path).
3. Run the compositor from TTY1.
4. If it hangs, Ctrl+Alt+F2 to TTY2, `sudo pkill -9 lambda-window-manager`.
5. If TTY switching breaks too, SSH from Mac and kill from there.
6. If SSH is also unresponsive, power-cycle.

The compositor as a normal process is killable. The kernel releases DRM master on process death; the TTY returns. Phase 1 verifies this works end-to-end.

### 3.4 Vulkan validation layers during development

Build the compositor with validation layers enabled by default during development. Set an environment variable to disable them for benchmarking. Validation catches misuse early; the cost is ~10-30% framerate hit which doesn't matter while developing.

### 3.5 Iteration speed expectations

Phase 1: build is fast (small binary, mostly linking Lambda). Iteration cycle is dominated by "switch to TTY, run, observe, kill, switch back, edit."

Phase 2+ : iteration cycle starts to matter. A Wayland client test app needs to be run alongside the compositor (the compositor must be running before the client connects). Set up a launcher script that starts the compositor, waits a beat, starts the client, watches both processes' output.

### 3.6 Logging

The compositor writes logs to stderr. Run as `lambda-window-manager 2>&1 | tee compositor.log` to capture sessions. Additional runtime instrumentation is controlled by environment variables such as `LAMBDA_WINDOW_MANAGER_CPU_TRACE`, `LAMBDA_WINDOW_MANAGER_PACING_TRACE`, `LAMBDA_RESIZE_TRACE`, and `LAMBDA_WINDOW_MANAGER_POPUP_TRACE`.

Avoid log spam in hot paths unless it is guarded by one of the instrumentation variables.

### 3.7 Coding conventions

Mirror Lambda's: C++23, RAII, no exceptions across module boundaries (use Result-style returns or assertions), namespace per module, header files end with `.hpp`, source files with `.cpp`, Wayland protocol implementations in their own files named after the global. Match Lambda's clang-format if available.

---

## 4. Phase 1: First pixels

**Status:** basic TTY smoke passed. Kernel-log, CPU-idle, and kill-path validation still pending.

### 4.1 Goal

A `lambda-window-manager` binary that owns the display, presents a solid color via KMS+Vulkan, exits cleanly on signal. No Wayland yet, no input handling beyond signal handling, no anything else.

This phase is mostly setup and confidence-building. The real compositor work starts in phase 2.

### 4.2 Scope

- The compositor repository is created with the layout from §1.9.
- `main.cpp` initializes via Lambda's KMS+Vulkan infrastructure.
- An output is selected (the first connected one).
- A render target is created over GBM scanout buffers, and those buffers are presented with atomic KMS commits.
- A render loop drives page flips and receives page-flip completion events from DRM.
- Each frame, the target is cleared to a solid color.
- SIGINT / SIGTERM cause clean shutdown: stop the render loop, release the RenderTarget, release the KMS framebuffers, release DRM master, exit.

### 4.3 Out of scope for phase 1

- Wayland server (phase 2).
- Input handling beyond signal handling (phase 3).
- Multiple outputs.
- Cursor.
- Anything client-related.

### 4.4 Framework changes required

**Lambda gains a public API for headless output management without `Window`.** Today Lambda's KMS code is tied to `Window`; the compositor needs to enumerate outputs, allocate framebuffers, and drive page flips without creating a `Window`. The proposed addition:

```cpp
// Lambda: include/Lambda/Platform/Linux/KmsOutput.hpp (new, Linux-only)
namespace lambda::platform {

class KmsDevice {
public:
    static std::unique_ptr<KmsDevice> open(char const* devicePath = nullptr);
    // Enumerates outputs, returns the connected ones.
    std::vector<std::unique_ptr<KmsOutput>> outputs();
    int fd() const noexcept;
};

class KmsOutput {
public:
    std::uint32_t width() const noexcept;
    std::uint32_t height() const noexcept;
    std::uint32_t refreshRateMilliHz() const noexcept;
    // Allocates a set of framebuffers (typically 2-3) backed by Vulkan images.
    // The caller wraps each in a RenderTarget.
    struct Framebuffer {
        VkImage image;
        VkImageView view;
        VkFormat format;
        std::uint32_t width;
        std::uint32_t height;
        VkSemaphore renderCompleteSemaphore;
        // ... handle, etc. for the page-flip
    };
    std::vector<Framebuffer> allocateFramebuffers(int count = 2);
    // Page-flip to the given framebuffer. Returns when the flip is scheduled.
    void scheduleFlip(Framebuffer const& fb);
    // Wait for the next vblank notification.
    void waitForVblank();
};

}
```

This generalizes the existing `Kms*` code in Lambda's platform layer into reusable types. `KmsWindow` (the existing class) becomes a thin user of these — it creates a `KmsDevice`, picks the first output, allocates framebuffers, wraps them in Canvas-bearing surfaces, and exposes the result as a `Window`.

**Metal parity:** the compositor doesn't run on Mac, but the framework-side change must preserve the existing Mac behavior. `CAMetalLayer` is Apple's framework-owned analog of KMS outputs; apps don't enumerate displays on Mac. No new public Metal API is required from this change. The Linux-side KmsDevice / KmsOutput types are gated `#if LAMBDA_PLATFORM_LINUX`; Mac builds don't see them. The existing `Window` continues to work identically on both backends.

### 4.5 Implementation

```cpp
// src/main.cpp
#include <Lambda/Graphics/VulkanContext.hpp>
#include <Lambda/Graphics/RenderTarget.hpp>
#include <Lambda/Platform/Linux/KmsOutput.hpp>
#include <Lambda/Core/Color.hpp>

#include <atomic>
#include <csignal>

namespace {
std::atomic<bool> g_running{true};
void onSignal(int) { g_running = false; }
}

int main(int /*argc*/, char** /*argv*/) {
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    auto& vk = lambda::VulkanContext::instance();
    vk.ensureInitialized();

    auto device = lambda::platform::KmsDevice::open();
    auto outputs = device->outputs();
    if (outputs.empty()) {
        std::fprintf(stderr, "No connected outputs\n");
        return 1;
    }
    auto& output = *outputs.front();

    auto framebuffers = output.allocateFramebuffers(2);
    int fbIndex = 0;

    lambda::Color clearColor{0.20f, 0.50f, 0.95f, 1.0f};

    while (g_running) {
        output.waitForVblank();

        auto const& fb = framebuffers[fbIndex];
        lambda::RenderTarget target{lambda::VulkanRenderTargetSpec{
            .image = fb.image,
            .view = fb.view,
            .format = fb.format,
            .width = fb.width,
            .height = fb.height,
            .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .signalSemaphore = fb.renderCompleteSemaphore,
        }};

        target.beginFrame();
        target.canvas().clear(clearColor);
        target.endFrame();

        output.scheduleFlip(fb);
        fbIndex = (fbIndex + 1) % framebuffers.size();
    }

    return 0;
}
```

Around 50 LOC including the framework change usage. Most of phase 1's work is in the framework change (KmsDevice/KmsOutput); the compositor itself is trivial at this stage.

### 4.6 Acceptance criteria

- ✗ `lambda-window-manager` binary builds clean on CachyOS.
- ✓ Running it from TTY1 turns the screen blue.
- ✓ The compositor exits cleanly through SIGTERM or its configured terminate shortcut; Ctrl+C is reserved for focused Wayland clients.
- ✗ CPU usage is low when content is static.
- ✗ No kernel errors in `journalctl -k` during a typical run.
- ✗ `kill -9` from TTY2 doesn't require a reboot — DRM master is released, TTY returns.
- ✗ Repository structure is in place for phase 2 to build on.
- ✗ Lambda's `KmsDevice` / `KmsOutput` API exists, is tested, and is used by the existing `KmsWindow` path without regression on Linux Lambda apps.

### 4.7 LOC estimate

- Lambda framework changes: ~400 LOC (new `KmsOutput` types, refactor of existing `KmsApplication` to use them).
- Compositor: ~50 LOC.
- Tests: ~150 LOC for the framework side.

### 4.8 Notes for the implementer

- The `lambda-window-manager` target is now the KMS smoke path. Use it from a real TTY to validate DRM/KMS and Vulkan presentation on hardware.
- Don't add a render thread in phase 1. Single-threaded is correct here. Threading can come later if needed.
- Validation layers will catch sync issues at this stage. Run with them on.

---

## 5. Phase 2: Wayland server, one client

**Status:** SHM and dma-buf smoke paths passed on hardware. The compositor opens a Wayland display, exposes the phase-2 core globals plus xdg-decoration, accepts SHM-backed client buffers, draws committed SHM surface pixels, renders basic subsurfaces, and displays GBM-backed dma-buf demo buffers. The dma-buf path now validates supported single-plane RGB buffers more strictly and renders the known-good readable linear CPU copy path; direct Vulkan sampling and explicit synchronization remain hardening work.

### 5.1 Goal

The compositor accepts a Wayland client connection and displays exactly one window. The client is a simple Lambda test app drawing "Hello compositor" text. The window appears at a fixed position with no chrome.

### 5.2 Scope

- libwayland-server initialized; the compositor listens on `$XDG_RUNTIME_DIR/wayland-0` (or `wayland-1` if 0 is taken).
- Core globals: `wl_compositor`, `wl_subcompositor`, `wl_shm`, `wl_output`, `wl_seat` (stub — no input dispatch yet, but advertises capabilities for client compatibility).
- `xdg_wm_base` / `xdg_surface` / `xdg_toplevel` for the basic window protocol.
- `zwp_linux_dmabuf_v1` for DMABUF-based buffer submission (the modern path; SHM is a fallback most clients don't actually use for performance work).
- The compositor's main loop: poll the Wayland fd alongside the KMS vblank, integrate Wayland event dispatch with the render loop.
- A scene-graph builder that takes the current Wayland surface state and produces a Lambda `SceneGraph` for the renderer.
- One window is composited per frame. Fixed position. No decoration.

### 5.3 Out of scope for phase 2

- Input dispatch (phase 3).
- Multiple windows (phase 3).
- Window chrome (phase 3).
- Layer-shell (phase 4).
- Cursor rendering (phase 3).
- xdg-shell features beyond basic toplevel (configure/ack-configure handled minimally for v1).

### 5.4 Framework changes required

**1. `Image::fromDmabuf(...)`** — a new factory on the framework's `Image` class that imports a DMABUF FD as a Vulkan-backed Image. Signature roughly:

```cpp
#if LAMBDA_VULKAN
struct DmabufPlane {
    int fd;
    std::uint32_t offset;
    std::uint32_t stride;
};
static std::shared_ptr<Image> fromDmabuf(
    std::uint32_t fourcc,                // DRM fourcc format
    std::uint64_t modifier,              // DRM modifier
    std::uint32_t width,
    std::uint32_t height,
    std::span<DmabufPlane const> planes
);
#endif
```

The implementation handles fourcc-to-VkFormat translation, allocates a VkImage with the right usage flags, imports the FDs as external memory, creates the image view.

**Metal parity:** `Image::fromIOSurface(IOSurfaceRef)` — Apple's equivalent of DMABUF for cross-process GPU resource sharing. Used by e.g. `AVPlayer` for video frame interop. Same shape as `fromDmabuf` but for the Apple cross-process texture mechanism. Not required for the compositor; required for Mac feature parity.

If implementing the Metal side immediately is meaningful work without a real consumer, the spec accepts deferring it with a TODO until a consumer surfaces. The principle from §2 is "feature parity when applicable"; if there's genuinely no Mac use case yet, "applicable" is false. Document the asymmetry, revisit when a Mac caller appears.

**2. Frame-driver decoupling.** The compositor needs Lambda's animation clock to fire at the right times even though the compositor doesn't have a `Window`. The existing `setFrameDriver(requestFrame, requestRedraw)` callback infrastructure (added in commit eac97c0) is the entry point. The compositor installs its own callbacks at startup; Lambda's clock calls them; the compositor schedules its render loop accordingly.

If the existing callback signature is insufficient (e.g., needs to indicate "this frame needs to happen by this deadline"), extend it. Don't introduce a parallel system.

No Mac-side change required; this is generalizing a path that already exists.

**3. SceneGraph utilities for "this node corresponds to this external GPU image."** Already covered by `Image::fromExternalVulkan` + `ImageNode`. No new framework API required, just standard composition.

### 5.5 Implementation overview

The Wayland server runs on the same thread as the render loop. Event dispatch happens between vblanks. Concretely:

```cpp
// Pseudocode for the main loop:
while (running) {
    wlServer.dispatchPending();              // Service Wayland clients
    output.waitForVblank();                  // Pace to display refresh
    sceneGraph = buildSceneFromSurfaces();   // Materialize Wayland state
    auto& fb = nextFramebuffer();
    lambda::RenderTarget target{...};
    target.beginFrame();
    target.renderScene(sceneGraph);
    target.endFrame();
    output.scheduleFlip(fb);
    wlServer.flushClients();                 // Send pending events back
}
```

Wayland event dispatch is non-blocking (`wl_event_loop_dispatch_idle` and friends). Vblank is the rhythm. If client events accumulate faster than vblanks, they queue and get serviced in batches.

This single-threaded model holds through phase 3. Phase 4 may surface a need for a Wayland event-loop thread separate from the render thread; deferred until evidence demands it.

### 5.6 Acceptance criteria

- ✓ `lambda-window-manager` accepts Wayland client connections through the scaffolded server.
- ✓ `wl_subcompositor` is exposed and basic `wl_subsurface` children render relative to their parent surface.
- ✓ Lambda test apps configured to use Wayland can connect and create toplevels through the implemented xdg-shell path.
- ✓ SHM-backed window content is copied into Lambda images and drawn on screen.
- ✓ `xdg-decoration` is exposed and server-side decoration mode is accepted/configured for clients that request it.
- ✓ `wl_surface.frame` callbacks are completed after compositor presentation rather than immediately at request time.
- ✓ DMABUF buffer submission works through the readable linear-buffer path.
- ✓ Resizing clients no longer crashes the compositor in the tested paths.
- ✓ Closing clients removes surfaces from the draw list and prunes cached client images.
- ✓ DMABUF-based buffer submission works in the purpose-built demo.
- ✓ The compositor exits cleanly through SIGTERM or its configured terminate shortcut; Ctrl+C is reserved for focused Wayland clients.

### 5.7 LOC estimate

- Lambda framework changes: ~600 LOC (DMABUF import + tests).
- Compositor:
  - Wayland server initialization & globals: ~400 LOC.
  - Surface state management: ~300 LOC.
  - Scene graph construction from surfaces: ~200 LOC.
  - Main-loop integration: ~150 LOC.
  - Generated protocol headers (XML → C): mechanical, doesn't count.
- Tests: ~200 LOC.

Total new code this phase: ~1850 LOC.

### 5.8 Notes for the implementer

- The Wayland protocol XMLs live in `protocols/`. Use `wayland-scanner` to generate client/server headers at build time. Standard CMake integration patterns exist; copy from a wlroots-based project's CMake for reference.
- `xdg-shell` is a stable protocol; use the released version from `wayland-protocols`.
- Don't try to be a "good Wayland citizen" yet. Edge cases (configure-without-ack, sub-surfaces, viewporter, etc.) wait for later phases. The goal is "one well-behaved client works"; weird clients don't have to work.
- Read greetd's bindings to libwayland-server if helpful for the C-API patterns. Read Sway's `desktop/output.c` for how mature compositors structure the render loop.

---

## 6. Phase 3: Input and window management

**Status:** input, chrome, and stacking window management are in progress and hardware-smoked. The compositor exposes real pointer and keyboard seat capabilities, forwards pointer/key events to the focused Wayland surface, draws client-provided cursor surfaces, raises windows on click, supports titlebar drag, corner resize, half-screen snapping with drag-unsnap, double-click-titlebar maximize/restore, compositor shortcuts, title text, macOS-inspired rounded/shadowed server-side chrome, a click-on-release close button, and a compositor-owned command launcher. Compositor-owned cursors use the system Xcursor theme. xdg-popup support now includes popup-first hit testing, default-on config-gated popup grabs, same-client owner-event routing, wlroots-style parent/child popup grab stacking, invalid grab-after-commit rejection, outside-click/Escape dismissal, and implicit pointer-button delivery for transient menu surfaces; Firefox application/context menus have been manually validated.

### 6.1 Goal

The compositor is usable as a minimal stacking compositor with multiple windows, input, and chrome. Substantively the hardest phase.

### 6.2 Scope

- libinput integration: pointer, keyboard, touch (touch may be stubbed if no test hardware).
- `wl_pointer`, `wl_keyboard`, `wl_touch` implementations with focus tracking and event dispatch. Pointer and keyboard are started; touch is still stubbed.
- Current manual test setup needs read access to `/dev/input/event*`, for example `sudo setfacl -m u:$(id -un):rw /dev/input/event*`. This is a development checkpoint; proper seat/session input-device brokering is still pending.
- libxkbcommon for keyboard layout handling (default: US layout; configuration deferred).
- Cursor rendering: the compositor draws client-provided cursor surfaces and system-theme cursor-shape cursors at the pointer position. Hardware cursor planes are used when the cursor image fits the KMS cursor plane.
- Multiple xdg_toplevel windows in z-order.
- Click-to-focus.
- Drag title bar to move window.
- Drag corner to resize window (xdg-shell's resize protocol).
- Snap to halves or quarters on edge/corner drag after a short dwell (compositor-driven, not a protocol feature).
- Double-click titlebar to maximize or restore.
- xdg_decoration_v1 for server-side decoration negotiation.
- Window chrome drawn via Lambda Canvas: title bar with app name, left-side close button, rounded corners, and soft shadow.
- Compositor shortcuts: Super+Q to close focused window; Super+Tab to cycle focus; Super+Left/Super+Right to snap; Super+Up to maximize; Super+Down to restore snapped/maximized windows; Ctrl+Alt+Backspace to terminate the compositor.
- xdg_popup support (right-click menus, dropdown menus in apps need this). Popup creation, positioning, configure, rendering, reposition, popup-first input hit testing, default-on config-gated popup grabs, same-client owner-event routing, parent/child popup grab stacking, invalid grab-after-commit rejection, outside-click dismissal, Escape dismissal, and implicit pointer-button delivery across transient menu surface teardown are implemented. `wl_subcompositor` is also exposed because real clients such as `foot` require it before popup/menu testing can proceed.

### 6.3 Out of scope for phase 3

- Layer shell (phase 4).
- Touch shell behaviors beyond basic touch-as-pointer.
- Window animations (phase 5).
- Hardware cursor planes (phase 5).

### 6.4 Framework changes required

**1. SceneNode "ownership tagging" for input routing.** The compositor's scene graph mixes its own UI (chrome, cursor, snap previews) with client-supplied surfaces. Input hit-testing needs to know whether a hit landed on a client surface (route input to that client via Wayland) or on chrome (handle locally, e.g., title-bar drag).

Today, `SceneNode::interaction()` returns a `scenegraph::Interaction*` whose concrete type is determined by the consumer. In Lambda apps, the concrete type is `lambda::InteractionData` (UI-shaped, with cursor preference and event callbacks).

For the compositor, the concrete type could be different — call it `lambda_compositor::SurfaceInteraction` — and would carry a Wayland surface reference plus chrome-vs-content classification. The hit-test returns the abstract `Interaction*`; the compositor's input dispatch casts down to its own type.

**No framework change is required for this**; the abstraction is already in place. This is a check that the existing design composes for the compositor's needs.

**2. Cursor preference protocol.** Lambda's existing `Cursor` enum (in `Lambda/UI/Cursor.hpp`) maps to compositor-side `CursorShape` values. Clients can either send `wl_pointer.set_cursor` with a cursor surface or use `wp_cursor_shape_v1`. The compositor snapshots SHM cursor surfaces through the same committed-surface path as normal SHM buffers and uses Xcursor theme images for compositor-owned cursor shapes. No new Lambda image API was required.

Metal parity: none required. This is Linux compositor protocol handling and KMS cursor-plane upload, not a cross-platform Lambda UI feature.

**3. Frame callbacks.** Wayland clients need `wl_surface.frame` callbacks to pace their rendering. The compositor sends these after each successful presentation. This is a Wayland protocol concern, not a framework concern — but it touches the integration with Lambda's animation clock. The compositor receives "I presented this frame, you can fire callbacks for surfaces that were in it" from somewhere; that signal needs to come from the existing render path. Probably just calling a callback after `RenderTarget::endFrame` returns.

If the existing `RenderTarget` doesn't expose a "frame presented" signal, add one. Both backends gain it.

### 6.5 Implementation notes

This is the phase where compositor-specific complexity dominates. Reference reading recommended before implementation:

- wlroots' `wlr_seat` for focus management semantics.
- Sway's `input/seat.c` and `input/pointer.c` for how mature compositors handle this in practice.
- xdg-shell spec sections on input focus and popup grabs.

Window management state is held in the compositor, not in Wayland. Wayland tells the compositor "this surface exists, here's its content"; the compositor decides where it goes, how it's stacked, and what input it receives. Phase 3 builds that decision layer.

### 6.6 Acceptance criteria

- ✓ Two Lambda apps run simultaneously, each in their own window.
- ✓ Windows can be moved by dragging their title bars.
- ✓ Windows can be resized by dragging their corners.
- ✓ Clicking a window brings it to the top and gives it focus.
- ✓ Keyboard input is routed to the focused window, including modifier updates and text-editing keys verified by Lambda demos.
- ✓ Cursor renders correctly and follows the pointer.
- ✓ Cursor changes when the client supplies a cursor surface through `wl_pointer.set_cursor`.
- ✓ Snap-to-half works from Super+Left/Super+Right; titlebar edge/corner drag supports half/quarter snap previews; dragging from a snapped/maximized titlebar restores the previous size without losing the cursor/titlebar grab.
- ✓ Double-clicking the titlebar maximizes/restores the window.
- ✓ Super+Q closes the focused window; the compositor close button sends xdg_toplevel close on click release.
- ✓ xdg-popup-based menus appear, dismiss, and route hover/click input correctly. The smoke demo creates, renders, repositions, outside-click-dismisses, and Escape-dismisses popups; popup grabs keep parent grabs active while child popups are active and reject grab-after-commit requests; Firefox application/context menus have been manually validated for menu actions and click-open submenus. Broader GTK/Qt menu validation remains useful.
- ◐ A non-trivial third-party client works: `foot` launches and basic terminal interaction works, including `Ctrl+C` routing to the terminal. Right-click menu behavior still needs broader validation.

### 6.7 LOC estimate

- Lambda framework changes: ~300 LOC (cursor SHM path if needed; frame-presented signal; misc.).
- Compositor:
  - Input: ~700 LOC.
  - Seat/focus management: ~500 LOC.
  - Window management state: ~400 LOC.
  - Chrome rendering: ~400 LOC.
  - Snap implementation: ~200 LOC.
  - Cursor rendering: ~250 LOC.
  - Shortcuts: ~150 LOC.
- Tests: ~400 LOC.

Total new code this phase: ~3300 LOC.

### 6.8 Notes for the implementer

- Phase 3 is when the compositor stops being a toy and starts being a real thing. Expect to learn a lot of Wayland edge cases here. Plan to revise this spec as the implementation surfaces realities.
- Focus management is the subtle hard part. Read the wlroots seat code multiple times.
- Don't try to handle every weird xdg-shell scenario. Get the common path solid, then patch edge cases as they appear with real clients.

---

## 7. Phase 4: Protocols for ecosystem compatibility

**Status:** first compatibility protocols in progress. `xdg_output_v1`, `wp_viewporter`, `wp_cursor_shape_v1`, `zwp_idle_inhibit_manager_v1`, `zwlr_layer_shell_v1`, an initial `wp_presentation_time` path, `zwp_relative_pointer_v1`, initial `zwp_pointer_constraints_v1` support, `zwp_primary_selection_v1`, initial `wl_data_device_manager` clipboard selection and drag-and-drop, `wp_fractional_scale_v1`, and `xdg_activation_v1` are implemented.

### 7.1 Goal

Real-world Wayland apps work. Implements the protocols that typical Linux apps (terminal emulators, video players, web browsers, editors) expect to find.

### 7.2 Scope

Protocols to implement, in rough priority order:

- **`zwlr_layer_shell_v1`**: required for panels, status bars, on-screen displays, notification daemons. Even if the compositor doesn't have a panel yet, this protocol's existence is what makes future panel work possible. Layer-shell clients render at fixed Z-order positions (background, bottom, top, overlay). Implemented with basic size/configure/anchor/margin handling.
- **`wp_viewporter`**: lets clients specify a source-region and destination-size for their surface, used heavily by video players and apps doing pixel-level scaling. Implemented.
- **`xdg_output_v1`**: gives clients logical output information (position, scale). Needed by apps that care about screen geometry. Implemented for the current single-output layout.
- **`wp_presentation_time`**: gives clients vblank-timing information for frame pacing. Used by video players and games for smooth playback. The compositor exposes the global, announces `CLOCK_MONOTONIC`, and sends one-shot presented/discarded feedback after compositor presentation. Feedback includes `sync_output` for the single active output. The default GBM/atomic-KMS presentation path uses DRM page-flip completion events with refresh intervals, sequence counters, and `VSYNC`/`HW_CLOCK`/`HW_COMPLETION` flags. The legacy Vulkan-display path can still use DRM vblank timing and optional `VK_GOOGLE_display_timing` completion records.
- **`zwp_relative_pointer_v1`** + **`zwp_pointer_constraints_v1`**: required for games and 3D apps that need raw mouse deltas with pointer locked to a window. Relative pointer motion is implemented; pointer constraints are implemented with focus-driven lock/confine activation.
- **`wp_cursor_shape_v1`**: lets clients request a system cursor by name rather than supplying a buffer. Newer protocol; modern toolkits use it. Implemented for pointer devices with compositor-drawn Xcursor theme images.
- **`zwp_primary_selection_v1`** + clipboard (`wl_data_device_manager`): clipboard, drag-and-drop, and middle-click-paste support. Primary selection is implemented for focused clients; regular clipboard selection is implemented for focused clients; drag-and-drop is implemented for UTF-8 text payloads with source/target action negotiation.
- **`zwp_idle_inhibit_manager_v1`**: lets video players prevent the screen from blanking. Implemented with protocol/state tracking and compositor-side software idle blanking; active inhibitors suppress blanking. DPMS/panel power-off is not implemented yet.
- **`xdg_activation_v1`**: lets apps focus a specific window programmatically (used by browser "open link in existing tab" etc.). Implemented with simple token generation and a repeated-activation guard to avoid focus loops.
- **`wp_fractional_scale_v1`**: HiDPI scaling for displays that aren't integer-multiple scales. Implemented as protocol negotiation from the compositor config scale, including fractional values, with `wl_output` integer scale fallback for clients that do not bind the fractional-scale protocol.

### 7.3 Out of scope for phase 4

- Screencast / screen sharing (`zwlr_screencopy_v1`, PipeWire-based capture): deferred. Useful when integrating with desktop apps that capture; not v1.
- Virtual keyboard / IME (`zwp_text_input_v3`): deferred. Required for non-Latin input; not v1.
- Tablet protocols (`zwp_tablet_v2`): deferred. Hardware-dependent; not v1.
- Workspace protocols (`ext_workspace_v1`): deferred. Workspaces aren't in v1.

### 7.4 Framework changes required

Mostly none. Layer-shell rendering uses the same scene-graph + chrome path as xdg-toplevel. Cursor-shape protocol uses the existing Lambda cursor enum directly. Clipboard might need framework support for the actual data exchange (MIME types, file descriptor passing) but the framework already supports clipboards via `Lambda/UI/Clipboard.hpp` — the compositor wires Wayland's data-device to that.

Potential exception: `wp_presentation_time` requires precise vblank timestamps from the render path. If the existing `RenderTarget::endFrame` doesn't expose actual present timestamps, that's a framework addition. **Metal parity:** Mac has `CAMetalLayer` presentation timing via `MTLDrawable.presentedTime`, so a similar API can exist on both backends.

### 7.5 Acceptance criteria

- ◐ `foot` (terminal emulator) launches and basic interaction works; clipboard/menu behavior still needs broader real-app validation.
- ✗ `mpv` (video player) plays a video smoothly with smooth frame pacing.
- ✗ A GTK4 app (e.g., `gnome-text-editor`) works correctly with the implemented protocols (decoration, cursor, clipboard, focus).
- ✗ A Qt6 app works correctly.
- ✗ A Firefox or Chromium build configured for Wayland runs and is usable.
- ◐ The protocols are exposed via the compositor's `wl_registry` globals and clients can negotiate them. `xdg_output_v1`, `wp_viewporter`, `wp_cursor_shape_v1`, `zwp_idle_inhibit_manager_v1`, `zwlr_layer_shell_v1`, `wp_presentation_time`, `zwp_relative_pointer_v1`, `zwp_pointer_constraints_v1`, `zwp_primary_selection_v1`, `wl_data_device_manager`, `wp_fractional_scale_v1`, and `xdg_activation_v1` are exposed.
- ✓ A purpose-built test layer-shell client renders at the top layer.
- ◐ A purpose-built presentation-time client receives `clock_id`, `sync_output`, and presented feedback after its committed frame is presented; the default GBM/atomic-KMS path is wired to DRM page-flip completion events and still needs hardware smoke validation.
- ✓ A purpose-built relative-pointer client receives relative motion deltas while its window has pointer focus.
- ✓ A purpose-built popup client can create, render, reposition, and dismiss positioned popups; app popup grabs are supported by the shared seat serial/grab path with wlroots-style grab stacking and Firefox menus have been manually validated.
- ◐ A purpose-built activation client can request an activation token and ask the compositor to raise/focus another window.
- ✓ A purpose-built pointer-constraints client receives pointer-lock activation and relative motion while the virtual pointer stays locked.
- ✓ A purpose-built primary-selection client can set a UTF-8 text primary selection and receive it back through the compositor.
- ✓ A purpose-built clipboard client can set a UTF-8 text clipboard selection and receive it back through the compositor.
- ✓ A purpose-built drag-and-drop client can drag a UTF-8 text payload from one toplevel to another with copy-action negotiation.
- ✓ A purpose-built fractional-scale client receives the configured preferred scale; Lambda Wayland clients consume fractional-scale events and render sharply at non-integer compositor scales.

### 7.6 LOC estimate

- Compositor protocol implementations: ~3000 LOC (one file per protocol, average ~250 LOC each).
- Compositor adjustments for protocol interactions: ~500 LOC.
- Framework changes (if presentation-time exposure needed): ~150 LOC across both backends.
- Tests: ~400 LOC.

Total new code this phase: ~4000 LOC.

### 7.7 Notes for the implementer

- Each protocol's XML in `wayland-protocols` has clear semantics. Read the protocol description carefully before implementing each.
- Many protocols have optional features; v1 doesn't need to implement all options. Prefer "implements the minimum required for typical clients to be happy" over "implements every protocol option exhaustively."
- Test with real apps, not synthetic tests, for protocol implementations. The clients drive the requirements.

---

## 8. Phase 5: Animation, polish, daily-driveability

**Status:** initial polish work in progress. New toplevels now get a short compositor-side fade/scale-in animation, closed surfaces fade out briefly, snap/maximize geometry changes are animated, titlebar drag-to-edge/corner shows an animated snap preview after a short dwell, startup config can set the background color or disable animations, and live window resize avoids stale-content scaling while reducing Vulkan swapchain churn.

### 8.1 Goal

The compositor feels good to use. It can be a daily driver for desktop work. The visual experience is coherent and the rough edges are smoothed.

### 8.2 Scope

- **Window animations:** open, close, move, resize, snap. All driven by Lambda's animation infrastructure (which is mature). Open animation: fade-in or scale-up. Close: fade-out. Move/resize: rubber-band physics or simple smoothing. Snap: preview overlay during drag, animated commit. Initial open fade/scale-in, close fade-out, server-driven snap/maximize geometry animation, and snap preview overlay are implemented. Titlebar drag to the top edge maximizes; dragging to left/right edges snaps halves; dragging to corners snaps quarters; Super+Up maximizes and Super+Down restores maximized/snapped windows.
- **Configuration file:** `~/.config/lambda-window-manager/config.toml`. The compositor creates this file with defaults on first launch when it does not exist; `LAMBDA_WINDOW_MANAGER_CONFIG=/path/to/config.toml` can override the path for testing. Keybindings, window-management preferences, animation toggles, output selection, and output scaling live here. Current parsing and hot reload support `background = "#RRGGBB"`, `background_gradient = "#RRGGBB,#RRGGBB"`, `wallpaper = "/path/to/file.jpg"`, `wallpaper_mode = "cover"` (`cover`, `contain`, `stretch`, `center`, or `tile`), `output = "HDMI-A-1"` or `output = "secondary"`, fallback `scale = 2.0`, per-connector `[outputs."DP-1"] scale = 2.0` overrides, `animations = false`, `hardware_cursor = false`, `idle_blank_timeout_seconds = 300`, and a `[keybindings]` section for compositor shortcuts including `launch_command = "super+space"`. Changing output selection requires restarting the compositor.
  Supported keybinding actions are `close`, `cycle_focus`, `snap_left`, `snap_right`, `maximize`, `restore`, and `terminate`; bindings use strings such as `"Super+Q"` or `"Ctrl+Alt+Backspace"`.
- **Hardware cursor:** use KMS cursor planes when supported. The compositor loads the system Xcursor theme for its default/cursor-shape cursors and uploads themed or client-provided cursor pixels to the KMS cursor plane when they fit without compositor-side scaling. If the cursor plane cannot be used, the same themed cursor image is drawn in software; there is no built-in fallback cursor artwork.
- **Frame timing improvements:** adaptive sync if the hardware supports it (FreeSync). Triple-buffering when beneficial. Initial KMS vblank pacing uses `drmWaitVBlank` when available and falls back to timer pacing if the driver rejects it.
- **Background / wallpaper:** the compositor draws a default backdrop. Configurable to a solid color, a gradient, or an image file. (The wallpaper is part of the compositor in v1; a separate `xdg-desktop-portal`-style daemon for wallpaper could come later.)
- **Multi-output:** TBD whether this lands in phase 5 or post-v1. If it lands, it's "support a second monitor side-by-side." If not, it's a v1.1 feature.
- **Documentation:** README, basic user docs, contribution guide.
- **Polish bug fixes:** issues surfaced during daily use that don't fit in any specific protocol or feature.

### 8.3 Framework changes required

**1. Compositor-style window animations may surface requirements on Lambda's animation system.** The existing `Animation<T>` clip-on-timeline mechanism is general; it should support these uses. If specific patterns are needed (e.g., a fixed-duration timeline that overrides reactive driving), they go into Lambda's animation module.

**2. Hardware cursor planes** are a KMS feature. If exposed cleanly via the Linux platform layer, the compositor uses it. Metal has its own cursor mechanism (NSCursor) which is unrelated; no parity required.

**3. Adaptive sync** (FreeSync / VRR) is KMS-side, no framework change required. The compositor controls page-flip timing directly.

### 8.4 Acceptance criteria

- ✗ The compositor runs reliably for a full day of dogfooding without crashes.
- ◐ Window animations feel smooth (60+ FPS during animations, no jank). Initial open animation, close fade-out, server-driven snap/maximize geometry animation, snap preview overlay, and live resize stabilization are implemented; broader animation polish remains pending.
- ◐ Configuration via the config file works for keybindings and basic preferences. Background color/gradient/wallpaper, cursor theme/size, animation-toggle parsing, compositor keybinding overrides, and hot reload are implemented.
- ◐ Hardware cursor works on the test hardware (AMD Vega supports cursor planes). Themed compositor cursors and compatible client-provided cursor surfaces use the KMS cursor plane; cursor surfaces that require compositor scaling still use the software path. Built-in compositor cursor artwork has been removed.
- ◐ User documentation explains how to build, configure, use, and test the compositor. Install/session-manager packaging docs remain pending.
- ✗ The compositor can be used as the primary desktop on the CachyOS development box.

### 8.5 LOC estimate

- Compositor:
  - Animation system: ~600 LOC.
  - Configuration: ~400 LOC.
  - Hardware cursor: ~200 LOC.
  - Wallpaper / background: ~200 LOC.
  - Multi-output (if included): ~700 LOC.
  - Polish / bug fixes: ~500 LOC.
- Framework changes: ~100 LOC if any.
- Tests: ~300 LOC.
- Documentation: substantial; doesn't count against LOC.

Total new code this phase: ~2700 LOC (more if multi-output lands here).

### 8.6 Notes for the implementer

- Phase 5 is partly "do the work" and partly "decide what good enough means." Avoid scope creep; the goal is "daily-driveable for a single-user desktop," not "matches GNOME feature parity."
- Animations should be tasteful. Default to subtle and short. Configurable for users who want them off or longer.
- Bug fixes from real use are the biggest part of this phase. The exact LOC is unpredictable.

---

## 9. Deferred work, explicit

These are explicitly out of v1:

- **Display manager / greeter / lock.** Per the architectural decision; user can use greetd or just run from TTY.
- **Multi-output beyond two monitors.** Maybe two in phase 5; certainly not three+.
- **Workspaces / virtual desktops.**
- **Tab grouping / window gluing.**
- **Accessibility (AT-SPI).**
- **Input methods (IME).**
- **Virtual keyboard.**
- **Touch-specific shell behaviors** beyond touch-as-pointer.
- **Form factors beyond desktop** (tablet shell, phone shell, etc.).
- **Screencast / screen sharing.**
- **Session save/restore (freeze/thaw).** This is the "device as computer" vision piece — explicitly deferred until v1 ships.
- **Window snapping custom positions** beyond the implemented halves and quarters.
- **Tiling layouts.**
- **HDR.**
- **Variable refresh rate beyond basic FreeSync.**
- **XWayland.** Pure Wayland for v1.

Most of these are real features users may want eventually. None are required for "a compositor that works as a daily-driver desktop." The discipline is to ship v1 first, then add.

---

## 10. Risks

**Phase 3 is the riskiest.** Input handling and window management have many edge cases. The estimate may be optimistic.

**The "no wlroots" choice has ongoing cost.** Every protocol the compositor implements directly is a chance to get a detail wrong that wlroots already got right. Plan for ongoing protocol-bug fixes in phase 6 (continuous improvement after v1 ships).

**The Vega GPU is mature but not the most-tested for compositor workloads.** Most Wayland compositor development happens on Intel + AMD Polaris/Navi. Renoir/Vega should work, but if there's a driver edge case, this configuration may hit it first. Mitigation: validation layers, `journalctl -k` monitoring.

**CachyOS's custom kernel may behave slightly differently from upstream.** Unlikely at the DRM ioctl layer (stable interface), but possible. Comparison kernel: `linux-lts`.

**The compositor's main thread doing both Wayland event dispatch and rendering may become a bottleneck.** If so, split rendering to a separate thread in phase 4 or 5. Don't preempt; wait for evidence.

**No XWayland is a real ecosystem cut.** Some apps (notably some games, older proprietary software, some specialized tools) don't have Wayland-native versions. The compositor won't run them in v1. Mitigation: accept the cut for v1, add XWayland post-v1 if there's demand.

---

## 11. Notes on the framework discipline

This document encodes the rule: **anything compositor work surfaces that would generally benefit Lambda apps goes into Lambda first, with backend parity.**

The pattern is:

1. Implementation hits a need (e.g., "render into KMS framebuffer," "import a DMABUF").
2. The need is generalized into a framework API (`RenderTarget`, `Image::fromDmabuf`).
3. The framework gains the API on Vulkan and Metal both, even when Metal doesn't have an immediate consumer.
4. The framework's tests cover the API on both backends.
5. The compositor consumes the API like any other library user.

When this rule is hard to satisfy (e.g., a Linux-only feature like DMABUF has no Mac analog), document the asymmetry rather than ignoring it. `VulkanContext` is honest about being Linux-only; future Linux-only additions follow that template.

When this rule is genuinely violated by expedience (e.g., "we'll add the Mac side later"), file the TODO in the framework, not in the compositor. The compositor should not accumulate "must remember to also add this to Mac" debt; that's a Lambda concern.

---

## 12. Status tracking

This section is updated as work progresses. Entries record completion of each phase's acceptance criteria.

| Phase | Status | Started | Completed | Notes |
|-------|--------|---------|-----------|-------|
| Phase 1: First pixels | Atomic KMS backend compiling | 2026-05-16 | - | Blue background, VT switching, and explicit compositor termination were verified on the earlier Vulkan-display path. The compositor now defaults to GBM scanout buffers plus atomic KMS page flips; hardware smoke validation is pending. Ctrl+C is ignored by the compositor so terminal clients can use it, while kernel-log, CPU-idle, and kill-path checks remain pending. |
| Phase 2: Wayland server, one client | SHM + dma-buf smoke passed | 2026-05-16 | - | Wayland display, `wl_compositor`, `wl_subcompositor`, `wl_shm`, `wl_output`, stub `wl_seat`, `xdg_wm_base`, `xdg-decoration`, linux-dmabuf protocol handling, SHM surface drawing, basic subsurface drawing, dma-buf demo drawing, and Lambda app smoke are verified on hardware; direct Vulkan sampling hardening remains. |
| Phase 3: Input + window management | Stacking WM active | 2026-05-16 | - | Focus, chrome, move/resize, snap, shortcuts, popup-first hit testing, and config-gated wlroots-style xdg-popup grabs (`popup_grabs`, default on) are in tree. Firefox app/context menu grabs have been manually validated; broader GTK/Qt coverage remains useful. |
| Phase 4: Protocol ecosystem | Compatibility protocols in progress | 2026-05-17 | - | `zxdg_output_manager_v1`, `wp_viewporter`, `wp_cursor_shape_v1`, `zwp_idle_inhibit_manager_v1`, `zwlr_layer_shell_v1`, `wp_presentation_time`, `zwp_relative_pointer_v1`, `zwp_pointer_constraints_v1`, `zwp_primary_selection_v1`, `wl_data_device_manager` clipboard/DnD, `wp_fractional_scale_v1`, and `xdg_activation_v1` are exposed. Current Wayland globals, including core surfaces and xdg-shell/decoration, live in `apps/lambda-window-manager/Compositor/Wayland/Globals/`; window/input-management lives in `apps/lambda-window-manager/Compositor/Window/WindowManager.cpp`; server lifecycle, snapshots, frame scheduling, and destroy cleanup live in dedicated `apps/lambda-window-manager/Compositor/Wayland/` files. |
| Phase 5: Animation + polish | Initial polish in progress | 2026-05-17 | - | New toplevels fade/scale in, closed surfaces fade out, snap/maximize geometry changes animate through intermediate client configures, titlebar drag-to-edge/corner shows an animated snap preview after a short dwell, live resize avoids stale-content scaling and reduces Vulkan swapchain churn, hot-reloaded config can set the background color/gradient/image, cursor theme/size, disable animations/hardware cursor, or override compositor shortcuts, compositor default/cursor-shape cursors use the system Xcursor theme with no built-in cursor artwork, compatible cursor images use a KMS cursor plane, the compositor default presentation path uses GBM/atomic KMS with page-flip completion events, and user/testing docs exist; adaptive sync/triple buffering and install/session docs remain pending. |

### 12.1 Framework changes log

Updated each time a Lambda change lands in service of compositor work:

| Date | Lambda commit | Description | Mac parity status |
|------|-------------|-------------|-------------------|
| 2026-05-21 | local working tree | Added explicit `Image::PixelFormat` upload/update APIs and changed Wayland SHM snapshots to preserve BGRA/XRGB byte order for direct Vulkan/Metal texture uploads, avoiding per-pixel CPU channel swizzles for browser-sized SHM surfaces. | Implemented for Vulkan and Metal; existing RGBA image APIs remain as compatibility wrappers. |
| 2026-05-21 | local working tree | Hardened the Linux atomic-KMS presenter by rotating exportable render-completion semaphores per rendered buffer and reporting concrete Vulkan result codes on failures. | Linux/KMS-only presentation path; no Metal equivalent required. |
| 2026-05-21 | local working tree | Added `Image::updateRgbaPixels(...)` so same-size RGBA images can update in place; the compositor uses this to reuse SHM client textures instead of creating a new GPU image for every commit. | Implemented for Vulkan texture-upload reuse and Metal `replaceRegion`; immutable external/imported images return false. |
| 2026-05-16 | local working tree | Added Linux KMS `KmsDevice` / `KmsOutput` API for compositor-owned display selection and Vulkan display-surface creation. | No Metal API required; Linux-only KMS surface. |
| 2026-05-16 | local working tree | Added initial compositor-side Wayland server scaffold and checked-in xdg-shell server bindings. | Linux compositor-only protocol integration; no Metal API involved. |
| 2026-05-16 | local working tree | Added checked-in linux-dmabuf server bindings and a compositor-side buffer-parameter lifetime scaffold. | Linux compositor-only protocol integration; no Metal API involved. |
| 2026-05-16 | local working tree | Added `Image::fromRgbaPixels(...)` and used it to draw SHM-backed Wayland surface snapshots. | Implemented for Vulkan and Metal. |
| 2026-05-16 | local working tree | Added checked-in xdg-decoration bindings and a server-side-decoration negotiation scaffold. | Linux compositor-only protocol integration; no Metal API involved. |
| 2026-05-16 | local working tree | Moved Wayland frame callbacks to the compositor present loop. | Linux compositor-only event-loop behavior; no Metal API involved. |
| 2026-05-16 | local working tree | Added a first `Image::fromDmabuf(...)` Vulkan path and compositor-side dmabuf surface import wiring. | Vulkan/Linux-only path; Metal parity still deferred to IOSurface import when needed. |
| 2026-05-16 | local working tree | Added compositor-owned server-side title bars and left-button move-drag handling. | Linux compositor-only window-management behavior; no Metal API involved. |
| 2026-05-16 | local working tree | Added `wl_pointer.set_cursor` handling so client cursor surfaces draw as the pointer image instead of as separate windows. | Linux compositor-only pointer behavior; no Metal API involved. |
| 2026-05-17 | local working tree | Added compositor window resizing, half-screen snap/drag-unsnap behavior, keyboard shortcuts, keyboard modifier forwarding, titlebar titles, and close-on-click-release handling. | Linux compositor-only window-management behavior; no Metal API involved. |
| 2026-05-17 | local working tree | Added `xdg-output-unstable-v1` protocol bindings and exposed `zxdg_output_manager_v1` for single-output logical geometry. | Linux compositor-only protocol integration; no Metal API involved. |
| 2026-05-17 | local working tree | Added stable `wp_viewporter` protocol bindings and compositor-side viewport state. | Linux compositor-only protocol integration; no Metal API involved. |
| 2026-05-17 | local working tree | Added `wp_cursor_shape_v1` protocol bindings, pointer cursor-shape handling, and compositor-side cursor rendering. | Linux compositor-only protocol integration; no Metal API involved. |
| 2026-05-17 | local working tree | Added `zwp_idle_inhibit_manager_v1` protocol bindings and compositor-side inhibitor tracking. | Linux compositor-only protocol integration; no Metal API involved. |
| 2026-05-18 | c2c3e59..f84b263 | Stabilized live resize: compositor resize geometry state, stale-content clipping, Vulkan swapchain reuse with size headroom, `wp_viewporter` client integration in `WaylandWindow`, and compositor-side viewport commit handling. | Mac unaffected: `CAMetalLayer` manages drawable size natively and has no Vulkan-style swapchain recreation per resize. No Metal API required. |
| 2026-05-18 | compositor-cleanup-spec commit 1 | Unified resize tracing into `lambda::detail::resizeTrace`, using `LAMBDA_RESIZE_TRACE` and `LAMBDA_RESIZE_TRACE_LOG` across Vulkan, Wayland-window, and compositor resize paths. | No Metal API surface; tracing exists for Vulkan/Linux resize behavior and is a framework-internal utility. |
| 2026-05-18 | compositor-cleanup-spec implementation | Added header-only tomlplusplus as the compositor config parser and removed the hand-rolled line scanner from `CompositorConfig.cpp`. | Header-only dependency is platform-neutral; no backend API surface. |
| 2026-05-18 | compositor-cleanup-spec implementation | Migrated current Wayland global implementations, including core surfaces, xdg-shell/decoration, viewporter, fractional-scale, cursor-shape, idle-inhibit, layer-shell, presentation-time, relative-pointer/pointer-constraints, primary-selection, clipboard, and DnD into `apps/lambda-window-manager/Compositor/Wayland/Globals/`. | Linux compositor-only protocol structure; no Metal API involved. |
| 2026-05-18 | compositor-cleanup-spec implementation | Moved compositor window/input-management behavior (focus, raise, titlebar drag, resize, snap/maximize, keyboard shortcuts, pointer/key forwarding, and popup dismissal) into `apps/lambda-window-manager/Compositor/Window/WindowManager.cpp`. | Linux compositor-only shell behavior; no Metal API involved. |
| 2026-05-18 | compositor-cleanup-spec implementation | Split the remaining Wayland server monolith into a pimpl forwarding layer plus lifecycle, snapshot/dmabuf fallback, frame scheduling, and destroy-cleanup translation units; public Wayland snapshot/shortcut types moved to `Wayland/WaylandTypes.hpp`. | Linux compositor-only structure; no Metal API involved. |
| 2026-05-18 | compositor-cleanup-spec implementation | Moved KMS compositor runtime orchestration out of `main.cpp` into `CompositorRuntime.cpp`; `main.cpp` now only handles process signals and calls the runtime. | Linux compositor-only executable structure; no Metal API involved. |
| 2026-05-18 | e5cc4ec | Added compositor-side Xcursor theme loading for default/cursor-shape cursors, removed built-in compositor cursor artwork, and generalized KMS cursor-plane upload to themed cursors plus compatible client cursor surfaces. | Linux compositor-only cursor integration; no Metal API involved. |
| 2026-05-18 | 77123e8 | Added compositor user/testing documentation, broadened deterministic config/geometry tests, and fixed Linux key-code parsing for configurable single-letter shortcuts. | Linux compositor docs/tests and config parsing; no backend API surface. |
| 2026-05-18 | 4826a6c | Moved non-grabbing xdg-popup placement into unit-tested window geometry helpers. | Linux compositor-only popup positioning hardening; no Metal API involved. |
| 2026-05-18 | 7c74c1a | Updated compositor status to reflect docs and popup geometry test coverage. | Documentation-only status update. |
| 2026-05-18 | 141ebd7 | Added config-file support for compositor cursor theme and size with Xcursor environment fallback. | Linux compositor-only cursor configuration; no Metal API involved. |
| 2026-05-18 | 7fec81f | Added `lambda-window-manager --config PATH` and `--help` so test configs can be selected without editing environment variables. | Linux compositor-only executable usability; no Metal API involved. |
| 2026-05-23 | window-manager-refactor branch | Split compositor runtime into `PresentationLoop`, `CompositorRenderFrame`, `CompositorConfigWatch`, and `Presenter`; added `lambda_wayland_protocols()` CMake helper; documented `Image::fromDmabuf` as Linux-only. | Linux/KMS compositor structure; no Metal API involved. |

### 12.2 Open questions

Tracked as work proceeds. Removed when answered.

- **Touch input:** `wl_seat` advertises pointer and keyboard only. Touch dispatch is not implemented and `WL_SEAT_CAPABILITY_TOUCH` is not advertised; clients that still request `wl_touch` receive an inert resource they can release cleanly.
- Phase 2: Does Wayland event dispatch share the main thread with rendering, or get its own thread? Current implementation shares the thread; revisit only if profiling or responsiveness issues show this is inadequate.
- Phase 3: Subsurface hit testing walks the subsurface tree in `WindowManager.cpp::surfaceAt` (deepest subsurface wins). Coordinate translation matches snapshot placement (`parent origin + subsurface position`).
- Phase 4: `wp_presentation_time` now sends `sync_output`. The default GBM/atomic-KMS path reports DRM page-flip completion timestamps; the legacy Vulkan-display path uses DRM vblank timing and can delay feedback on optional `VK_GOOGLE_display_timing` records.
- Presentation backend escape hatch: `LAMBDA_WINDOW_MANAGER_PRESENT=vulkan-display` forces the legacy Vulkan-display presenter for debugging while the atomic-KMS path is being hardware-smoked.
- Phase 5: Does multi-output land in v1 or post-v1?

### 12.3 Remaining implementation work

Tracked in [roadmap.md](roadmap.md). Update that document as work lands; keep §12.1 framework log entries here with commit SHAs.
