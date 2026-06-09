# Vulkan Improvement Plan

**Status:** Active backlog for Vulkan/Linux graphics work. Tracked as TODO-018 in [TODO.md](../TODO.md).
**Scope:** `src/Graphics/Vulkan/`, the Vulkan touchpoints in `src/Platform/Linux/`, and the platform interface in `src/UI/Platform/Application.hpp`. All items require a Linux machine (Wayland and/or KMS) to build, run, and verify.

## Working Environment

Build either Linux backend with tests:

```sh
# Wayland
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLAMBDA_PLATFORM=LINUX_WAYLAND -DLAMBDA_BUILD_TESTS=ON
# KMS (run from a TTY; see docs/compositor-user-guide.md)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLAMBDA_PLATFORM=LINUX_KMS -DLAMBDA_BUILD_TESTS=ON

cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
./build/tests/lambda_tests --test-case="*Vulkan*"
```

For headless verification without a GPU, install Mesa's software Vulkan driver (lavapipe) and force it:

```sh
sudo apt install mesa-vulkan-drivers  # or distro equivalent
VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json ./build/tests/lambda_tests --test-case="*Vulkan*"
```

Use an ASan build for the resource-lifetime items (VK-1, VK-4):

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DLAMBDA_ENABLE_ASAN=ON -DLAMBDA_BUILD_TESTS=ON
```

Suggested order: VK-2 and VK-4 first (mechanical, low risk), then VK-1 (highest runtime impact), then VK-3, VK-5, VK-6, VK-7.

---

## VK-1: Replace runtime `vkDeviceWaitIdle` with fence-scoped retirement

**Severity: High (UI-thread stalls, masks sync bugs).**

`vkDeviceWaitIdle` blocks the calling thread until the *entire device* is idle. It is acceptable during final teardown but is currently also used on hot paths in `src/Graphics/Vulkan/VulkanGpuCanvas.cpp`:

| Line | Context | Verdict |
| --- | --- | --- |
| 174 | `~VulkanImage()` — every image destruction waits on the whole device | Hot path, must fix |
| 5193 | `flushFrameCapture()` — frame capture readback | Hot-ish path, fix |
| 5228 | Screenshot readback | Same as above |
| 904 | `releaseSharedVulkanCore()` — last-reference teardown | Acceptable, leave |
| 1258 | `~VulkanCanvas()` — canvas teardown | Acceptable, leave |

What to do:

- [ ] [Auto + Manual] `~VulkanImage()` (line ~170): stop waiting for device idle. The canvas already has per-frame deferred-destroy queues (`destroyDeferredTextures` / `destroyDeferredBuffers`, drained at lines 1271–1272, 1525–1526, 1816, 1844–1857 once the owning frame's fence has signaled). Route image/view/allocation destruction through the same mechanism: enqueue the `VkImage`/`VkImageView`/`VmaAllocation` with the current frame index and destroy after the frame fence for that slot signals. Images destroyed while no canvas/frame exists (e.g. app shutdown) can keep the idle-wait fallback.
- [ ] [Auto + Manual] `flushFrameCapture()` (line ~5193) and the screenshot path (line ~5228): wait on the *specific* frame fence that produced the capture (the fence for the frame-in-flight slot that recorded the copy) instead of `vkDeviceWaitIdle`.
- [ ] [Auto] Keep teardown waits (lines 904, 1258) but add a short comment marking them as intentional shutdown-only waits so they don't get cargo-culted back into hot paths.

Verification on Linux:

- [ ] Run an animated demo (`animation-demo`, `scroll-demo`) and an image-heavy app (`lambda-files` thumbnails, `lambda-preview`) under both Wayland and KMS; watch for stutters when images are created/destroyed rapidly (navigate folders quickly in Files).
- [ ] Run `./build-asan/tests/lambda_tests --test-case="*Vulkan*"` and the full suite under ASan — premature destruction shows up as validation errors or ASan faults.
- [ ] Enable Vulkan validation layers (`VK_LAYER_KHRONOS_validation`, `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`) and confirm no use-after-free/sync complaints while resizing windows and capturing frames.
- [ ] Frame capture still works: `lambda-window-manager` screenshots and any `captureFrame` test paths produce correct images.

## VK-2: One shared `vkCheck` / `vkResultName` helper

**Severity: Medium (error reports lose the failure code; three copies drift).**

Current state:

- `src/Platform/Linux/WaylandApplication.cpp:19` — `vkCheck` throws `"<op> failed"` with **no VkResult code**.
- `src/Platform/Linux/KmsApplication.cpp:140` — separate copy.
- `src/Platform/Linux/KmsOutput.cpp:227, 254` — has its own `vkResultName` with result codes.
- `src/Graphics/Vulkan/VulkanGpuCanvas.cpp:435, 473` — the richest `vkResultName` switch, plus its own `vkCheck`.

What to do:

- [ ] [Auto] Create a small shared header, e.g. `src/Graphics/Vulkan/VulkanCheck.hpp`, exporting `char const* vkResultName(VkResult)` and `void vkCheck(VkResult, char const* what)` that always includes the result name in the thrown message. (Header-only or a tiny TU — match `scripts/check_stale_symbols.sh` expectations.)
- [ ] [Auto] Replace all four local copies (files above) with the shared helper. `KmsOutput.cpp`'s message format (`"<what> failed: <name> (<int>)"`) is the best baseline.
- [ ] [Auto] Make sure the header is usable from `src/Platform/Linux/` without dragging in the whole canvas; it should include only `<vulkan/vulkan_core.h>`.

Verification on Linux:

- [ ] Builds clean on Wayland and KMS; `./scripts/check_module_dependencies.sh` and `./scripts/check_stale_symbols.sh` pass.
- [ ] Force a failure (e.g. request an absurd swapchain extent or run with a broken `VK_ICD_FILENAMES`) and confirm the error message now contains the `VkResult` name.

## VK-3: Remove Vulkan types from `platform::Application`

**Severity: Medium (abstraction leak; macOS platform code sees Vulkan declarations).**

Current state: the cross-platform interface `src/UI/Platform/Application.hpp` (lines 53–54) declares

```cpp
virtual std::span<char const* const> requiredVulkanInstanceExtensions() const { return {}; }
virtual VkSurfaceKHR createVulkanSurface(VkInstance, void*) { return nullptr; }
```

with hand-rolled `VkInstance`/`VkSurfaceKHR` forward declarations at the top of the header. macOS implements these as no-ops. Consumers:

- `src/Platform/Linux/WaylandWindow.cpp:910, 913, 1603, 1607`
- `src/Platform/Linux/KmsWindow.cpp:174, 176`
- `src/Platform/Linux/KmsOutput.cpp:3527–3529, 3724–3726`
- Implementations: `WaylandApplication.cpp:98, 106`, `KmsApplication.cpp:730, 748` (declared in `KmsPlatform.hpp:62–63`).

What to do:

- [ ] [Auto] Introduce an opaque internal interface, e.g. `platform::GpuSurfaceProvider` (in a Linux/Vulkan-only header), with `requiredInstanceExtensions()` and `createSurface(VkInstance, void* nativeHandle)`. Wayland and KMS applications implement it; the generic `platform::Application` exposes at most `virtual GpuSurfaceProvider* gpuSurfaceProvider() { return nullptr; }` (forward-declared, no Vulkan types) or the Linux windows downcast to their concrete application type.
- [ ] [Auto] Delete the two Vulkan virtuals and the `VkInstance_T`/`VkSurfaceKHR_T` forward declarations from `src/UI/Platform/Application.hpp`. macOS platform TUs should no longer see any Vulkan names.
- [ ] [Auto] Update the call sites listed above to use the provider.

Verification:

- [ ] Both Linux backends build and open windows with working rendering (Wayland window + KMS from TTY).
- [ ] macOS still builds (CI covers this; `Application.hpp` no longer mentions Vulkan — `grep -i vulkan src/UI/Platform/Application.hpp` returns nothing).

## VK-4: Isolate `VMA_IMPLEMENTATION` into its own translation unit

**Severity: Low (fragility; one-line landmine).**

`src/Graphics/Vulkan/VulkanGpuCanvas.cpp:33` defines `VMA_IMPLEMENTATION` before including `vma/vk_mem_alloc.h` inside the 5,804-line canvas TU. Any second definition elsewhere breaks the link, and the huge header recompiles with the canvas on every edit.

What to do:

- [ ] [Auto] Add `src/Graphics/Vulkan/VulkanAllocator.cpp` containing only the warning-suppression pragmas (copy lines 24–38 of `VulkanGpuCanvas.cpp`), `#define VMA_IMPLEMENTATION`, and the include. Add it to `src/CMakeLists.txt`.
- [ ] [Auto] Remove the define (keep the plain include) from `VulkanGpuCanvas.cpp`.

Verification: clean rebuild on Linux links with no duplicate-symbol errors; `*Vulkan*` tests pass.

## VK-5: Split `VulkanGpuCanvas.cpp` (5,804 lines) into focused modules

**Severity: Medium (maintainability; blocks parallel work on VK-1/VK-6).**

The single TU currently contains: instance/device selection, VMA setup, pipeline construction, pipeline-cache persistence, swapchain management, the canvas/draw-op implementation, glyph atlas, image upload/eviction, backdrop blur, and the frame recorder.

What to do (mechanical moves, no behavior change; suggested seams):

- [ ] [Auto] `VulkanCore.cpp` — shared instance/device/queue/allocator (`gVulkanCore`, `acquireSharedVulkanCore` / `releaseSharedVulkanCore`, device selection with its rejection-reason log around lines 724–883).
- [ ] [Auto] `VulkanPipelines.cpp` — pipeline + pipeline-cache persistence (cache keyed by device UUID, lines ~643–708).
- [ ] [Auto] `VulkanSwapchain.cpp` — swapchain create/destroy/recreate and `OUT_OF_DATE`/`SUBOPTIMAL` handling.
- [ ] [Auto] `VulkanImages.cpp` — `VulkanImage`, upload, eviction, deferred destroy (pairs with VK-1).
- [ ] [Auto] `VulkanGlyphAtlas.cpp` — glyph atlas.
- [ ] [Auto] `VulkanCanvas.cpp` — the `Canvas` implementation and frame loop; keep under ~1,500 lines.
- [ ] [Auto] Keep `lambda_vulkan_shaders` CMake wiring intact; update `src/CMakeLists.txt` source lists.

Verification: full Linux build both backends, `ctest` green, demos render identically (spot-check `gradient-demo`, `blend-demo`, `text-demo`, `image-demo`).

## VK-6: Metal/Vulkan canvas parity for clip/transform edge cases

**Severity: Medium (visual divergence between platforms).**

`src/Graphics/Metal/MetalCanvas.mm` has CPU-side geometry helpers that the Vulkan canvas lacks:

- `boundsOfTransformedRect` (`MetalCanvas.mm:152`, used at 964, 973, 1105, 1322, 1385, 1410, 1741)
- `cornerRadiiAfterAxisAlignedClip` (`MetalCanvas.mm:179`, used at 1028)

`grep` confirms neither exists in `VulkanGpuCanvas.cpp` — rotated/scaled clips and rounded-corner clipping can render differently on Linux. The SDF shader logic is also implemented twice (`CanvasShaders.metal` vs `src/Graphics/shaders/*.frag`) with parity maintained by hand.

What to do:

- [ ] [Auto] Move the two helpers (and any sibling pure-geometry helpers in `MetalCanvas.mm`'s anonymous namespace) into a shared header, e.g. `src/Graphics/CanvasGeometry.hpp`; use them from both canvases.
- [ ] [Auto + Manual] Audit the Vulkan clip stack against Metal's: transformed-rect clip bounds and corner-radius adjustment after axis-aligned clips should follow the same code path.
- [ ] [Auto] Add a parity regression test on Linux using the Vulkan frame-capture path (`flushFrameCapture`): render a fixed scene (rotated clip + rounded-corner clip + gradient) offscreen and assert captured pixels against known-good values, mirroring what `MetalCanvasTests.mm` covers on macOS. Run it under lavapipe so it works headless.

Verification: side-by-side screenshots of `blend-demo`, `gradient-demo`, and a rotated-clip scene on macOS vs Linux look identical; new capture test passes under lavapipe.

## VK-7: Build and validation hygiene

**Severity: Low.**

- [ ] [Auto] `lambda_vulkan_shaders_check` (`src/CMakeLists.txt:263`) regenerates shader headers but is not exercised by any test or CI step. Wire it into the Linux CI job (`.github/workflows/ci.yml`) after the build step so hand-edited `.spv`/headers fail fast.
- [ ] [Auto] Evaluate running `./build/tests/lambda_tests --test-case="*Vulkan*"` in CI under lavapipe (see Working Environment); if stable, add it to the Linux job so Vulkan code gets exercised, not just compiled.
- [ ] [Auto] While touching error paths in VK-2, give image loading a diagnostic: `lambda::loadImage` returns `nullptr` silently on failure (`src/Graphics/ImageLoader.cpp:340–354`) which makes missing-asset bugs on Linux hard to trace; log the path and reason at least in debug builds.
