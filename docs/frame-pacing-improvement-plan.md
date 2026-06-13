# Frame Pacing Improvement Plan

**Status:** FP-1 … FP-16 code landed (`50d65831..HEAD`); post-implementation code review backlog fixed (2026-06-11). This document now tracks **open verification gaps** (manual, hardware input, macOS runtime). Tracked as TODO-019 in [TODO.md](../TODO.md).
**Scope:** `apps/lambda-window-manager/Compositor/`, `src/Platform/Linux/` (KMS + Wayland), `src/Graphics/Vulkan/`, `src/Graphics/Metal/`, `src/Platform/Mac/`, `src/SceneGraph/`, and `src/UI/Application.*`.
**Source:** Original frame-pacing audit at `ca4466cd`; implementation review of `50d65831..HEAD` with citation verification against current sources. Severity reflects expected impact on smoothness, pacing, and correctness.

Line numbers in the FP-* sections below describe the **original audit context** and are often stale after the implementation pass. Use the **Citation errata** table for current file:line references. Delete this document and TODO-019 when all verification gaps are closed.

## Verification Snapshot: 2026-06-11

- [x] Clean normal and KMS rebuilds completed with zero warnings and zero errors.
- [x] Focused normal tests passed: Vulkan image-recorder replay test (1 case, 12 assertions), compositor/Vulkan/presentation/damage/cursor/pointer slice (88 cases, 602 assertions), and reactive tests (22 cases, 126 assertions).
- [x] Focused KMS tests passed: Vulkan image-recorder replay test (1 case, 12 assertions), compositor/Vulkan/presentation/damage/cursor/pointer slice (89 cases, 606 assertions), and reactive tests (22 cases, 126 assertions).
- [x] KMS compositor runtime smoke completed with normal-build `lambda-shell`, scripted `lambda-terminal`, and `lambda-editor`; logs had zero fatal/error matches.
- [x] Runtime traces captured 20 CPU samples, 37 KMS timing windows, 10,712 pacing events, and 900 terminal `vulkan-present-detail` samples. Summary: compositor CPU avg/max 12.54%/15.60%, compositor surface avg/max 0.471/0.632 ms, present avg/max 0.395/0.582 ms, terminal atlas avg 0.004 ms, terminal `waitImage` avg 0.000 ms.
- [x] Added `scripts/verify-frame-pacing-linux.sh` and `lambda-presentation-feedback-check`. Latest run passed both presenters: atomic-KMS reported `CLOCK_MONOTONIC`, refresh 16,666,666 ns, nonzero sequence, and `VSYNC|HW_CLOCK|HW_COMPLETION`; Vulkan-display reported `CLOCK_MONOTONIC`, refresh 16,666,666 ns, and nonzero sequence. Atomic runtime summary: 20 CPU samples, 31 KMS timing windows, 9,914 pacing events, 826 terminal `vulkan-present-detail` samples, zero fatal matches, CPU avg/max 12.38%/16.50%, surface avg/max 0.359/0.552 ms, present avg/max 0.506/1.069 ms, terminal `waitImage` avg 0.000 ms, terminal atlas avg 0.008 ms.
- [x] Added synthetic KMS raw-input pointer-motion verification to `scripts/verify-frame-pacing-linux.sh`. The verifier now enforces this through the controlled static-surface and terminal-load pointer cases below; the original baseline run reported 180 synthetic pointer events, 180 hardware-cursor fast-path moves, zero fallback/unavailable moves, zero runtime failures, no pointer-triggered full redraw loops, and sampled CPU trace output without a new compositor coredump.
- [x] Added KMS pointer-under-terminal-load verification to `scripts/verify-frame-pacing-linux.sh`. Latest run passed on 2026-06-11 (`.debug-logs/frame-pacing-verify/20260611-205253/atomic-pointer-under-terminal-load`): 180 synthetic pointer events, 180 hardware-cursor fast-path moves, zero fallback/unavailable moves, zero runtime failures, zero pointer-triggered render loops, 884 terminal `vulkan-present-detail` samples, 1,652.398 ms of timestamp-proven overlap between pointer motion and terminal rendering, and 103 accepted cursor updates while a primary flip was pending with zero deferred cursor moves.
- [x] Added controlled static-surface pointer-sweep verification to `scripts/verify-frame-pacing-linux.sh`. Latest run passed on 2026-06-11 (`.debug-logs/frame-pacing-verify/20260611-163000/atomic-pointer-fast-path`): 80 synthetic pointer-motion events over a held static Wayland surface, 81 hardware-cursor fast-path moves including the initial diagnostic position, zero fallback/unavailable moves, zero runtime failures, zero pointer-triggered render loops after the sweep began, static surface flips observed, and surface avg/max 0.007/0.018 ms.
- [x] Added static decorated-SHM surface cache verification to `scripts/verify-frame-pacing-linux.sh`. Latest run: 500 surface draw-cache hits, 1 miss, zero transient-chrome blocks, 524 rendered frames, CPU avg/max 9.13%/11.10%, surface avg/max 0.016/0.017 ms, present avg/max 0.258/0.264 ms.
- [x] Added server-side chrome hover/press verification to `scripts/verify-frame-pacing-linux.sh`. Latest run: synthetic close-button move, press, move-away, release, and completion events all observed; surface draw-cache reported 4 hits, 1 miss, zero transient-chrome blocks, CPU avg/max 1.00%/2.20%, and surface avg/max 0.315/0.811 ms.
- [x] Added scripted resize-storm verification to `scripts/verify-frame-pacing-linux.sh`. Latest run: 18 resize/configure events, 522 sizing cache-block samples, 558 rendered frames, zero fatal matches, CPU avg/max 15.47%/16.70%, surface avg/max 1.117/1.299 ms, and present avg/max 0.433/0.446 ms.
- [x] Added ASan capture-heavy verification for Vulkan recorder/render-target tests. A fresh `build-asan` now builds `lambda_tests` with the generated Wayland server protocol headers ordered correctly, and the ASan slice passed 22 cases/170 assertions including recorded image replay after the recording canvas is destroyed.
- [x] Perf-wrapped full Linux verifier passed on 2026-06-11 (`.debug-logs/frame-pacing-verify/20260611-144159`): atomic, pointer-fast-path, surface-cache, chrome hover/press, resize-storm, and Vulkan-display cases all passed with zero fatal matches. Atomic app smoke ran `lambda-shell`, scripted `lambda-terminal`, and `lambda-editor`; summary: CPU avg/max 11.54%/16.30%, 869 terminal `vulkan-present-detail` samples, terminal `waitImage` avg 0.000 ms, terminal atlas avg 0.004 ms, surface avg/max 0.337/1.098 ms, present avg/max 0.403/0.496 ms. Pointer case reported 180/180 hardware-cursor fast-path moves and zero fallback moves. Resize storm reported 37 resize events, 568 sizing cache-block samples, CPU avg/max 16.62%/17.40%, surface avg/max 1.018/1.194 ms, and present avg/max 0.377/0.386 ms. `perf stat` captured 732,396.19 ms task-clock, 1,821,935 page faults, 2,622,262,507,174 cycles, and 1,834,944,457,566 instructions.
- [x] Standard external compositor smoke partially passed: Weston 15.0.1 headless with kiosk shell/fake seat ran `weston-smoke` until a controlled 8-second timeout with no Weston runtime errors. The Lambda presentation-feedback checker also connected far enough to report that Weston headless advertises `CLOCK_MONOTONIC_RAW`, so it remains unsuitable for validating Lambda's stricter `CLOCK_MONOTONIC` presentation contract.
- [x] Added `scripts/verify-wayland-client-pacing.sh` for FP-14 Linux client pacing under headless Weston GL. Latest run passed on 2026-06-11 (`.debug-logs/wayland-client-pacing/20260611-163744`): 47 `frame-done` callbacks, 46 matched `app-render: present` pairs, 47 `vulkan-present-detail` samples, 46 immediate flushes, zero fatal matches, frameDone-to-present avg/max 1.340/5.637 ms under the 8 ms gate.
- [x] Validation-layer focused Vulkan render-target run passed on 2026-06-11: `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation VK_LOADER_DEBUG=layer ./build/tests/lambda_tests --source-file="*VulkanRenderTargetTests.cpp" --no-skip` loaded `VK_LAYER_KHRONOS_validation` and passed 20 cases / 146 assertions after matching the backdrop-blur pipeline format to its R16 render targets and covering same-frame image draw/destroy.
- [x] KMS cursor/presentation review batch build and focused tests passed on 2026-06-11: `cmake --build build -j"$(nproc)" --target lambda_tests lambda-window-manager` and `./build/tests/lambda_tests --source-file="*CompositorPresentationFeedbackTests.cpp" --no-skip` (6 cases / 46 assertions).
- [x] Wayland/chrome review batch build and focused tests passed on 2026-06-11: `cmake --build build -j"$(nproc)" --target lambda_tests lambda-window-manager`, compositor scene/surface/chrome slice (47 cases / 300 assertions), and frame-scheduler/scene-damage/window-state source slice (41 cases / 253 assertions).
- [x] Vulkan recorder/replay review batch build and focused tests passed on 2026-06-11: normal and `LAMBDA_VULKAN_PREPARED_GEOMETRY=1` Vulkan render-target tests (21 cases / 157 assertions each), validation-layer Vulkan render-target tests (21 cases / 157 assertions), and a prepared/recorder scene slice (6 cases / 79 assertions).
- [x] Compositor damage review batch build and focused tests passed on 2026-06-11: `cmake --build build -j"$(nproc)" --target lambda_tests lambda-window-manager`, `./build/tests/lambda_tests --source-file="*CompositorSceneDamageTests.cpp" --no-skip` (10 cases / 30 assertions), `./build/tests/lambda_tests --source-file="*CompositorSurfaceUploadDamageTests.cpp" --no-skip` (3 cases / 7 assertions), and `./build/tests/lambda_tests --source-file="*CompositorPresentationFeedbackTests.cpp" --no-skip` (6 cases / 46 assertions).
- [x] Metal atlas review batch passed available Linux checks on 2026-06-11: `cmake --build build -j"$(nproc)" --target lambda_tests lambda-window-manager` and `./build/tests/lambda_tests --source-file="*MetalCanvasTests.mm" --no-skip` (0 cases on Linux, source excluded).
- [x] Metal backdrop/drawable review batch passed available Linux checks on 2026-06-11: `cmake --build build -j"$(nproc)" --target lambda_tests lambda-window-manager` and `./build/tests/lambda_tests --source-file="*MetalCanvasTests.mm" --no-skip` (0 cases on Linux, source excluded).
- [x] Final Linux test pass after review fixes: full `ctest --test-dir build --output-on-failure` passed under headless Weston (2/2 tests); `RuntimeInputTests.cpp` passed separately under headless Weston (33 cases / 198 assertions, with the existing `may_fail` case allowed); fresh Vulkan validation-layer render-target run passed (21 cases / 157 assertions); normal focused Vulkan/compositor slice passed (6 cases / 79 assertions); rebuilt KMS focused slice passed (6 cases / 79 assertions); ASan Vulkan recorder/render-target slice passed (4 cases / 65 assertions).
- [x] Linux verifier rerun after installing `evemu` and `wayland-utils` passed on 2026-06-11 (`.debug-logs/frame-pacing-verify/20260611-145444`): atomic, pointer-fast-path, surface-cache, chrome hover/press, resize-storm, and Vulkan-display cases all passed with zero fatal matches; tool availability now reports `ydotool`, `wtype`, and `evemu-event` available.
- [x] Real evdev hardware-input validation passed on 2026-06-11 (`.debug-logs/frame-pacing-verify/20260611-151835/evemu-hardware-input`): `evemu-event` injected 60 relative pointer events into `/dev/input/event6` (`GXTP7863:00 27C6:01E0 Mouse`), KMS/libinput logged 60 raw pointer motions, the hardware-cursor fast path logged 60 moves, presentation feedback reported `CLOCK_MONOTONIC` with `VSYNC|HW_CLOCK|HW_COMPLETION`, and there were zero fallback/unavailable moves or fatal matches.
- [x] Real evdev hardware-input validation is now repeatable through `scripts/verify-evemu-hardware-input.sh` with an explicit `LWM_EVEMU_DEVICE`. The helper now enables KMS input debug logging and fails unless it sees a pointer device, raw libinput motion, hardware-cursor fast-path moves, hardware presentation feedback, zero fallback/unavailable moves, and zero fatal matches. Latest strict run passed on 2026-06-12 (`.debug-logs/evemu-hardware-input/20260612-050841`) against `/dev/input/event6` (`GXTP7863:00 27C6:01E0 Mouse`): 12 `evemu-event` relative-motion commands, 2 pointer-device logs, 12 raw-pointer logs, 12 hardware-cursor fast-path moves, presentation feedback with `VSYNC|HW_CLOCK|HW_COMPLETION`, and zero fatal matches.
- [x] Runtime Vulkan validation-layer text-scroll and debug-screenshot pass completed on 2026-06-11 (`.debug-logs/frame-pacing-verify/20260611-153332/validation-scroll-screenshot`) after fixing validation errors from the first run. Fixes: enable the `VK_KHR_surface_maintenance1` instance-extension dependency chain and rotate swapchain geometry storage buffers/descriptors per in-flight frame. Rerun summary: 946 `vulkan-present-detail` samples, validation layer loaded, zero validation errors, zero fatal matches, valid `P6` debug screenshot (1,545,615 bytes), `waitImage` max 0.000 ms, screenshot phase max 22.480 ms, atlas max 4.710 ms, record max 1.463 ms, and `queuePresent` max 0.164 ms. Focused validation-layer `VulkanRenderTargetTests.cpp` also passed 21 cases / 157 assertions.
- [x] ASan + Vulkan validation-layer KMS resize-storm pass completed on 2026-06-11 (`.debug-logs/frame-pacing-verify/20260611-155247/asan-validation-resize-storm`) after fixing DRM modifier plane layouts, KMS external render-target frame-slot rotation, and per-buffer Vulkan render fences for KMS command-buffer/semaphore reuse. Rerun summary: validation layer loaded, zero validation errors, zero ASan errors, zero fatal matches, presentation feedback reported `CLOCK_MONOTONIC` with `VSYNC|HW_CLOCK|HW_COMPLETION`, CPU avg/max 42.27%/44.20%, surface avg/max 3.925/4.197 ms, and present avg/max 0.991/1.184 ms. Follow-up normal Linux verifier passed (`.debug-logs/frame-pacing-verify/20260611-155435`) across atomic, pointer-fast-path, surface-cache, chrome hover/press, resize-storm, and Vulkan-display cases with zero fatal matches.
- [x] Post-KMS-render-fence Linux regression checks passed on 2026-06-11: full `ctest --test-dir build --output-on-failure` under headless Weston passed 2/2 tests; focused `RuntimeInputTests.cpp` under headless Weston passed 33 cases / 198 assertions with the existing `may_fail` case allowed; focused `AnimationTests.cpp` passed 11 cases / 53 assertions; focused `CompositorSnapshotBuilderTests.cpp` passed 2 cases / 14 assertions; focused `TextInputScrollTests.cpp` passed 2 cases / 11 assertions.
- [x] Resize-storm snap capture and frame-profile artifact checks are now enforced by `scripts/verify-frame-pacing-linux.sh`. Latest run passed on 2026-06-11 (`.debug-logs/frame-pacing-verify/20260611-163000/resize-storm`): 12 saved PNG frames, 12 full-output metrics rows with nonzero alpha/pixels, 240 snap trace frame rows, 235 surface rows, 37 resize events, 555 sizing cache-block samples, zero fatal matches, CPU avg/max 22.64%/95.90%, surface avg/max 0.900/1.139 ms, and present avg/max 0.373/0.381 ms.
- [x] Multi-dirty partial-damage, glass-chrome, and captured moving-window artifact verification is now enforced by `scripts/verify-frame-pacing-linux.sh` and `lambda-frame-artifact-check`. Latest run passed on 2026-06-11 (`.debug-logs/frame-pacing-verify/20260611-205718/multi-dirty-partial`): 423 partial frames, 340 multi-rect partial frames, max logical rect count 2, 628 flips with at least two surfaces, 1,272 prepared blur ops / 1,271 runs / 1.572 ms stacked blur work, zero fatal matches, surface avg/max 0.055/0.064 ms, 48 captured PNG frames, 564 expected checker-gradient content matches, 588/594 newly exposed old-window samples returned to the solid background, and 70 titlebar/glass capture rows all alpha-valid with 61 changing rows and 16.841 luma range.
- [x] FP-8 offscreen scanout-copy validation is now enforced by `scripts/verify-frame-pacing-linux.sh`. Latest focused run passed on 2026-06-11 (`.debug-logs/frame-pacing-verify/20260611-203400/scanout-copy-partial`) with direct scanout rendering disabled: 421 partial frames, 407 multi-rect partial frames, 384 region scanout copies, 1,035 scanout copy rects, region scanout-copy pixels at 192,887,438 / 796,262,400 full-equivalent pixels (0.242 ratio), primary-preserve region-copy ratio 0.209, and present avg/max 0.508/0.531 ms.
- [x] Alternating partial/full-damage artifact validation is now enforced by the `partial-full-artifacts` verifier case. Latest focused run passed on 2026-06-11 (`.debug-logs/frame-pacing-verify/20260611-203400/partial-full-artifacts`): 207 partial frames, 340 forced-full frames, 48 captured PNG frames, 528 expected checker-gradient content matches, and 532/535 newly exposed old-window samples returned to the solid background with zero fatal matches.
- [x] FP-11 cached-surface/RADV validation refresh passed on 2026-06-11. The latest surface-cache verifier run (`.debug-logs/frame-pacing-verify/20260611-180528/surface-cache-static`) reported 521 surface draw-cache hits, 1 miss, zero transient-chrome blocks, and surface avg/max 0.012/0.013 ms. RADV validation logs in `.debug-logs/fp11-validation/20260611-181036` show `VK_LAYER_KHRONOS_validation` on `AMD Radeon Graphics (RADV RENOIR)` passing `VulkanRenderTargetTests.cpp` 21 cases / 157 assertions, plus normal and `LAMBDA_VULKAN_PREPARED_GEOMETRY=1` runs passing the same slice.
- [x] FP-9 active 10-window snapshot-load verification is now enforced by `scripts/verify-frame-pacing-linux.sh`. Latest full run passed on 2026-06-11 (`.debug-logs/frame-pacing-verify/20260611-183736/snapshot-load-10-windows`): 13 CPU samples, 11 stable samples with at least nine surfaces, 842 rendered frames, stable `snapshot_ms` avg/max 0.092/0.096, stable `surface_ms` avg/max 0.042/0.046, 7,326 surface-cache hits / 10 misses, and malloc-count preload output of 317,412 allocations / 842 frames = 377.0 allocations/frame under the 430 gate. Pre-FP-9 baseline at `5bcfab46` (`.debug-logs/fp9-baseline/20260611-183607/snapshot-load-10-windows`) was stable `snapshot_ms` avg/max 0.114/0.120, stable `surface_ms` avg/max 0.113/0.119, 0 hits / 6,963 misses, and 383,272 allocations / 775 frames = 494.5 allocations/frame.
- [x] FP-14 input/animation regression refresh passed on 2026-06-11. Linux `AnimationTests.cpp` passed 11 cases / 53 assertions directly, and Linux `RuntimeInputTests.cpp` passed 33 cases / 198 assertions under headless Weston (`.debug-logs/fp14-input-tests/20260611-184321`) with the known nested-autofocus `may_fail` case still allowed. macOS full `ctest --test-dir build --output-on-failure -j"$(sysctl -n hw.ncpu)"` had already passed 2/2 tests on 2026-06-11.
- [x] FP-3 Wayland resize stderr quietness is now enforced by `scripts/verify-wayland-resize-quiet.sh`. Latest run passed on 2026-06-11 (`.debug-logs/wayland-resize-quiet/20260611-190522`) under headless Weston GL: `lambda-terminal` completed 90 scripted self-resize steps with `LAMBDA_RESIZE_TRACE=0`, zero fatal matches, and zero `resize-trace`/`vulkan-recreate-swapchain`/swapchain-extent stderr noise.
- [x] FP-7 glass-terminal partial-damage measurement is now enforced by `scripts/verify-frame-pacing-linux.sh`. Latest run passed on 2026-06-11 (`.debug-logs/frame-pacing-verify/20260611-200703/glass-terminal-partial`): 651 partial frames, zero forced-full partial frames, one static decorated glass client presented in both runs, terminal perf traces observed in both runs, warm partial `background_ms`/`surface_ms`/`total_ms` averages 0.008/0.161/0.651 ms versus forced-full 0.009/0.177/0.676 ms.
- [x] FP-13 Wayland resize recreate verification is now enforced by `scripts/verify-wayland-resize-recreate.sh`. Latest run passed on 2026-06-11 (`.debug-logs/wayland-resize-recreate/20260611-201230`) under headless Weston GL: 90 scripted resize steps, 2 swapchain recreates, 3 dirty resize decisions, 88 clean no-recreate resize decisions, recreate elapsed max 0.831 ms, and retire-queue max 0.001 ms.
- [x] FP-14 FIFO-only Wayland client pacing is now enforced by `scripts/verify-wayland-client-pacing.sh`. Latest run passed on 2026-06-11 (`.debug-logs/wayland-client-pacing/20260611-201500`) under headless Weston GL with `LAMBDA_VULKAN_FORCE_FIFO_PRESENT_MODE=1`: 21 frame-done callbacks, 20 present pairs, 21 present-detail samples, 20 immediate flushes, 22 FIFO request frames, zero mailbox request frames, and frameDone-to-present avg/max 1.529/6.312 ms.
- [x] FP-5 close-button hover/press visuals are now captured by `scripts/verify-frame-pacing-linux.sh`. Latest focused run passed on 2026-06-11 (`.debug-logs/frame-pacing-verify/20260611-204025/chrome-hover-press-cache`): synthetic move, press, move-away, release, and completion events all observed; surface cache stayed hot with 3 hits / 1 miss / zero transient blocks; 11 snap PNGs were captured; close-button metrics reported 6 alpha-valid rows, 4 changing rows, max luma delta 81.964, and luma range 81.963.
- [x] FP-6 native Wayland popover pacing is now enforced by `scripts/verify-wayland-popover-pacing.sh`. Latest run passed on 2026-06-13 (`.debug-logs/wayland-popover-pacing/20260613-175456`) under headless Weston GL: one immediate initial configure render, eight committed redraw requests, eight frame requests, eight frame-done callbacks, eight committed renders from frame callbacks, zero immediate committed redraw renders, and native popover Escape dismissal through the guarded backend path because Weston headless lacks virtual-keyboard support.
- [x] Manual-gate helper refresh: `scripts/run-real-app-smoke.sh` now includes `lambda-editor` and prints the remaining cursor appearance/responsiveness, resize feel, stale-content, and text-after-scroll visual checks. `scripts/run-real-app-smoke.sh --probe-only` ran on 2026-06-11: Lambda apps, `foot`, and Firefox are available; GTK app is still missing, and Qt remains optional/missing on this machine.
- [x] Added guarded VT-switch verifier `scripts/verify-kms-vt-switch.sh`. It requires an explicit `LWM_VT_SWITCH_TARGET`, refuses to guess a VT, and checks pre/post presentation feedback plus compositor VT release/acquire logs. It was syntax-checked and its non-TTY guard was verified in this Codex session; the actual VT switch still requires a real TTY.
- [ ] Remaining local input/visual gap: `ydotool`, `wtype`, `evemu-event`, and `wayland-info` are installed, real evdev input is validated, and the evdev check is now repeatable via `scripts/verify-evemu-hardware-input.sh`. Outside the sandbox the user is now in `wheel` and `/dev/input/event*` is visible with ACLs, but `/dev/uinput` is still `0600 root:root` and `sudo -n true` still requires a password, so uinput-backed validation cannot run non-interactively here. Manual cursor visual validation still requires an interactive human session.
- [ ] Remaining environment gap: automated app smoke passed with `lambda-shell`, scripted `lambda-terminal`, and `lambda-editor`; captured drag/move stale-pixel checks now pass; and the manual real-app smoke helper now covers `lambda-editor` plus explicit cursor/resize/text-scroll checks. Manual visual checks are still needed for cursor appearance, resize feel, and representative app inspection on a live compositor session.
- [x] macOS compile verification after the latest Metal atlas/backdrop batches: fixed the ARC-only compile error in `MetalCanvas.mm` backdrop buffer pooling (`id<MTLBuffer>&` with no explicit ownership), then `cmake --build build -j"$(sysctl -n hw.ncpu)"` passed (2026-06-11).
- [x] macOS focused tests: `MetalCanvasTests.mm` passed 12 cases / 92 assertions including the deferred atlas grow and glyph-padding regressions, `SceneGraphTests.cpp` passed 20 cases / 115 assertions, and the explicit deferred atlas grow regression passed with `LAMBDA_DEBUG_PERF=2` (2026-06-11).
- [x] macOS full `ctest --test-dir build --output-on-failure -j"$(sysctl -n hw.ncpu)"` passed 2/2 tests (2026-06-11).
- [x] Added repeatable macOS Metal/editor runtime helper `scripts/verify-macos-metal-editor-perf.sh`. It builds `lambda-editor`, generates a large unicode-heavy UTF-8 paste payload, drives the normal `edit.paste` command through the opt-in `LAMBDA_EDITOR_AUTOTEST_PASTE_FILE` hook, parses `LAMBDA_DEBUG_PERF=2` detail rows for `CanvasDrawableWait` and frame-budget p99, and requires `atlasGrow` perf evidence by default. Linux validation here: script syntax passed, non-Darwin guard returned the expected "macOS is required" exit, Linux build of `lambda-editor`/`lambda_tests` passed, focused Editor/TextInput/EventQueue tests passed, and a headless-Weston smoke of the editor autotest pasted and exited cleanly.
- [x] Post-helper Linux regression refresh passed on 2026-06-11 after `c68fb58a`: full `ctest --test-dir build --output-on-failure -j"$(nproc)"` passed 2/2 under headless Weston, and `scripts/verify-wayland-client-pacing.sh` passed under headless Weston GL (`.debug-logs/wayland-client-pacing/20260611-215505`) with 21 frame-done callbacks, 20 present pairs, 21 `vulkan-present-detail` samples, 20 immediate flushes, FIFO-only requests, frameDone-to-present avg/max 1.582/6.187 ms under the 8 ms gate, and no mailbox requests. A source/test audit found no `vkQueueWaitIdle` references outside historical plan text.
- [x] Added `scripts/audit-todo019-validation-gates.sh` to make the remaining external gates reproducible. Latest outside-sandbox run reports input tools and the evdev/VT/macOS helpers present, but still marks KMS VT switch pending because this session is `not a tty`, uinput pending because `/dev/uinput` is `crw------- root root`, noninteractive sudo pending, live Wayland/manual smoke pending, and macOS runtime pending on this Linux host.
- [ ] Remaining macOS runtime/visual gap for the latest Metal changes: run `scripts/verify-macos-metal-editor-perf.sh` on macOS and compare backdrop-blur visuals by frame-capture/manual inspection.
- [x] Post-implementation review backlog (REV-*) fixed; remaining unchecked items are validation gaps.

## Working Environment

```sh
# Wayland client path
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLAMBDA_PLATFORM=LINUX_WAYLAND -DLAMBDA_BUILD_TESTS=ON
# Compositor / KMS path (run from a TTY; see docs/compositor-user-guide.md)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLAMBDA_PLATFORM=LINUX_KMS -DLAMBDA_BUILD_TESTS=ON

cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

Use an ASan build for the resource-lifetime items (FP-10, FP-11, FP-12, FP-13):

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DLAMBDA_ENABLE_ASAN=ON -DLAMBDA_BUILD_TESTS=ON
```

Measurement tooling that already exists — use it before and after every change:

| Tool | How | What it shows |
| --- | --- | --- |
| Vulkan present phases | `LAMBDA_RESIZE_TRACE=1`, `vulkan-present-detail` trace (`VulkanCanvasLifecycle.inc:633-660`) | Per-phase ms: frame fence, acquire, atlas, backdrop prep, upload, record, submit, present |
| Compositor frame profile CSV | `SnapAnimationTrace::recordFrame` in `CompositorRuntime.cpp:760-838` (header at `747-754`) | input→render, render-ahead, snapshot/surface/present phase ms |
| Flip interval error | `intervalErrorMs` trace at `CompositorRuntime.cpp:1883-1887` | Render-ahead lead accuracy vs real flips |
| Metal counters | `debug::perf` — `CanvasDrawableWait`, `CanvasPresent`, `DisplayLinkToPresent`, `atlasGrow` | Drawable stalls, atlas growth, and present cost on macOS |
| Validation layers | `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` | Sync/lifetime regressions from any of these changes |

**Open REV code items:** none. Original FP workstreams (FP-1 … FP-16) are marked complete above; remaining manual verification checkboxes under each FP section still apply where unchecked.

---

# Phase 1 — Verified hot-path stalls (quick wins)

## FP-1: Record glyph-atlas uploads into the frame command buffer

**Severity: Critical (full GPU queue drain mid-present whenever text changes).** Verified in source.

`uploadAtlasIfNeeded()` runs inside `presentImpl()`. Any frame with a dirty atlas (new glyph, eviction, rebuild) goes through:

- `VulkanGlyphAtlas.inc:122-132` — `uploadAtlasIfNeeded()` → `uploadTexture(res.atlas, …)`
- `VulkanImages.inc:248-265` — `uploadTexture()` uses `beginImmediate()` / `endImmediate()`
- `VulkanImages.inc:309-318` — `endImmediate()` calls `vkQueueSubmit2` then **`vkQueueWaitIdle`**

That is a synchronous submit plus a full queue drain on the hot path, before the frame's own command buffer is recorded.

What to do:

- [x] [Auto] Add an atlas-upload path that records the staging-buffer copy and layout transitions into the **frame** command buffer, following the existing `recordPendingTextureUploads` pattern (`VulkanImages.inc:186-205`): stage pixels into a `Buffer`, queue a `PendingTextureUpload`-style job for the atlas texture, retire the staging buffer through `pendingBufferDestroys_` with `kMaxFramesInFlight + 1`.
- [x] [Auto] The atlas is sampled by draws in the same frame — record the copy before the main render pass begins (same place `recordPendingTextureUploads` runs) with a transfer→shader-read barrier.
- [x] [Auto] Same treatment for imported-image layout transitions: `transitionImmediate` in `ensureImageTexture` (`VulkanImages.inc:17` and `:320-324`) stalls the queue on first draw of an external image. Record the transition into the frame command buffer via a pre-pass barrier batch instead.
- [x] [Auto] After this, `endImmediate()` should have no callers on the per-frame path. Keep it for genuine one-off initialization, and add a comment stating it must not be called from `beginFrame`/`present`.

Verification on Linux:

- [x] `LAMBDA_RESIZE_TRACE=1` — the `atlasMs` phase in `vulkan-present-detail` should drop to ~0 on frames that add new glyphs (type rapidly in `lambda-terminal` / `lambda-editor` with a cold atlas).
- [x] `./build/tests/lambda_tests --test-case="*Vulkan*,Compositor*"` (lavapipe OK) — glyph text test (`Vulkan RenderTarget renders glyph atlas text`) still passes.
- [x] Validation layers report no missing-barrier or layout errors while scrolling text-heavy content. Verified by the 2026-06-11 runtime text-scroll pass in `.debug-logs/frame-pacing-verify/20260611-153332/validation-scroll-screenshot` with 946 `vulkan-present-detail` samples and zero validation errors after the per-frame storage-buffer/descriptor fix.

## FP-2: Composite partial-damage frames once, not once per damage rect

**Severity: Critical (N damage rects ≈ N× full scene draw recording).** Verified in source.

`CompositorRenderFrame.cpp:1024-1033`: when `partialDamageFrame` is true, the code loops `sceneDamage.rects` and calls the full `drawFrameContent()` (wallpaper + all surfaces + cursor + overlays) inside each clip. GPU work is bounded by the clip; CPU draw recording and scene traversal are multiplied by the rect count.

What to do:

- [x] [Auto] Replace the per-rect loop with a single pass: push all damage rects as one clip region (add a multi-rect clip helper on `Canvas` if needed — Vulkan scissor-list or a stencil/union-clip; a bounding-union `clipRect` is an acceptable first step when rect count is small) and call `drawFrameContent()` once.
- [x] [Auto] Merge overlapping/adjacent damage rects before use: both damage systems cap rect count with a cliff to full-output damage (`SceneDamage.cpp:51-53` at >64, `CompositorSceneGraph.cpp:173-175` at >96). Add a union/merge step (merge rects whose union area is within ~20% of the sum) so busy scenes degrade to fewer larger rects instead of full-screen damage.
- [x] [Auto] Keep the KMS `damageRects` passed to the presenter unchanged — only the compositing pass collapses; scanout damage stays per-rect.

Verification on Linux (KMS TTY):

- [x] [Auto] Frame profile/CPU trace: `scripts/verify-frame-pacing-linux.sh` now keeps two decorated checker windows mapped, drives scripted multi-toplevel motion, and fails unless multi-rect partial frames are observed with low `surface_ms`. Latest run: 411 partial frames, 332 multi-rect partial frames, max logical rect count 2, 623 flips with at least two surfaces, and surface avg/max 0.059/0.086 ms.
- [x] [Auto] Captured visual artifact check: the `multi-dirty-partial` verifier drags two decorated checker-gradient windows under glass chrome, saves 48 compositor frames, validates current surface content against the expected SHM gradient, and verifies newly exposed old-window areas return to the solid background. Latest pass: 555 content matches and 573/579 exposed samples returned to background in `.debug-logs/frame-pacing-verify/20260611-203118/multi-dirty-partial`.
- [x] `tests/` damage suites still pass (`computeSceneDamage` unit tests).

## FP-3: Remove the remaining queue-drain fallback and noisy recreate log

**Severity: Medium (catastrophic when hit; trivial fixes).**

- `KmsOutput.cpp:883-886` — `markFrameRendered()` falls back to `vkQueueWaitIdle` when `buffer.renderFinished == VK_NULL_HANDLE`. The async-fence path normally creates the semaphore, so this is rare — but it fully drains the GPU queue when hit.
- `VulkanSwapchain.inc:53-70` — unconditional `fprintf` to stderr on every swapchain recreate (every resize step).

What to do:

- [x] [Auto] Guarantee `renderFinished` exists by the time `prepareFrame()` returns (create it eagerly with the buffer); replace the `vkQueueWaitIdle` fallback with a logged error + per-buffer fence wait so a misconfigured driver degrades per-buffer instead of device-wide.
- [x] [Auto] Gate the swapchain-extent `fprintf` behind `detail::resizeTraceEnabled()` like the other resize traces.

Verification:

- [x] [Auto] Grep: no `vkQueueWaitIdle` outside `endImmediate` teardown comments; no unconditional stderr writes in the resize path.
- [x] [Auto] Resize a Wayland window continuously with `LAMBDA_RESIZE_TRACE=0`; stderr stays quiet. `scripts/verify-wayland-resize-quiet.sh` runs `lambda-terminal` under headless Weston with a terminal-only scripted resize diagnostic and fails if resize tracing, `vulkan-recreate-swapchain`, or swapchain-extent logs leak to stderr. Latest pass: 90 resize steps, zero fatal matches, zero resize-noise matches.

---

# Phase 2 — Input smoothness

## FP-4: Cursor-only atomic commits (no recomposition, no queue dependency)

**Severity: High (cursor lag under load; wasted full frames per pointer event).**

Three related problems:

- `KmsOutput.cpp:3636-3643` — `moveCursor()` on the atomic path only stores `cursorX_/cursorY_`; the cursor plane is applied by `addAtomicCursorProperties()` during the **next full commit**.
- `CompositorRuntime.cpp:797-806` + `2131-2149` — cursor-only dirtiness (`inputHardwareCursorFrameRequired`) is not part of `renderNeeded`, so when the present queue is blocked (flip pending, acquire fence unsignaled) cursor motion can wait multiple frames.
- Pointer motion that *is* consumed still forces a full `renderAtomicFrame()` even though the hardware cursor lives on its own DRM plane (full snapshot, surface draw, offscreen→scanout copy, schedule).

What to do:

- [x] [Auto] Add a cursor-only commit path in `KmsOutput`: an atomic request containing only cursor-plane properties (`CRTC_X/CRTC_Y`, FB if the image changed), `DRM_MODE_ATOMIC_NONBLOCK`, no page-flip event, retry on `EBUSY`/`EAGAIN` like the existing cursor commit. It must be schedulable while a primary-plane flip is pending.
- [x] [Auto] In the compositor loop, when the only dirtiness is hardware-cursor motion, call the cursor-only commit and **skip** `renderAtomicFrame()` entirely (keep the existing full-frame path for software cursor).
- [x] [Auto] Handle hardware-cursor motion in input dispatch so cursor updates are not starved behind a blocked present queue.

Verification on Linux (KMS TTY):

- [x] Frame profile/pacing trace: pointer-motion-only periods produce no compositor frames. `scripts/verify-frame-pacing-linux.sh` reported 80 controlled synthetic pointer-motion events over a held static surface, 81 hardware-cursor fast-path moves including the initial diagnostic position, zero fallback/unavailable moves, and no `input=1 render=1` redraw loops after the pointer stream began.
- [x] [Auto] Hardware-cursor fast path remains active while a scripted terminal workload is presenting: latest run reported 180/180 hardware-cursor fast-path moves, 884 terminal present samples, zero pointer-triggered render loops, and 1,652.398 ms of verified timestamp overlap.
- [ ] [Manual] Cursor visual responsiveness under a truly GPU-saturated/heavy client still needs human confirmation.
- [x] [Auto] No cursor-plane starvation across primary-plane flips: the `atomic-pointer-under-terminal-load` verifier now fails unless at least one hardware-cursor fast-path move is accepted while a primary flip is pending. Latest pass accepted 103 pending-flip cursor moves with zero deferred cursor moves.

## FP-5: Stop chrome hover from busting the surface draw-op cache

**Severity: High (continuous re-recording of full surface draws during normal mouse use).**

- `Snapshots.cpp:152-158` recomputes `closeButtonHovered` from the pointer each snapshot.
- `SurfaceRenderer.cpp:125-132` — `hasTransientChromeState()` returns true for any hover/press state, which blocks `VulkanFrameRecorder` replay (`SurfaceRenderer.cpp:465-485`), so the whole surface (client content + chrome) is re-recorded every frame the pointer is near a titlebar.
- `CompositorRuntime.cpp:797-806` — pointer motion sets `inputRenderRequiredThisLoop` even when no visual state changed.

What to do:

- [x] [Auto] Split server-side decoration chrome into its own recorded layer (or own scene node) so the client-content op cache stays valid across hover changes; only the titlebar layer re-records on hover/press transitions.
- [x] [Auto] Emit damage only for the titlebar bounds when hover state changes; pointer motion that changes no hover state and moves only the hardware cursor must not set `inputRenderRequired` (pairs with FP-4).
- [x] [Auto] Add a diagnostics counter delta check: `recordSurfaceDrawCacheBlock(…TransientChrome)` should stop incrementing during plain pointer sweeps over a window.

Verification on Linux (KMS TTY):

- [x] Automated static decorated-SHM client check keeps the surface draw cache hot while the compositor drives window movement: 500 hits, 1 miss, zero transient-chrome cache blocks, and surface avg/max 0.011/0.012 ms.
- [x] Automated server-side chrome hover/press check drives KMS pointer position and button events over the close control without closing the window: move, press, move-away, release, and complete events observed; cache stayed hot with 4 hits, 1 miss, and zero transient-chrome cache blocks.
- [x] [Auto] Sweep the pointer across a window with static content: controlled verifier case keeps a static Wayland surface alive while synthetic pointer motion runs, reports zero pointer-triggered render loops, and keeps `surface_ms` near idle (latest surface avg/max 0.007/0.018 ms).
- [x] [Auto] Hover/press visuals on close buttons still update correctly: the `chrome-hover-press-cache` verifier captures titlebar/control regions during synthetic close-button move, press, move-away, and release events, and fails unless close-button luma changes are measured. Latest pass: 6 close-button rows, 4 changing rows, max luma delta 81.964, and luma range 81.963 in `.debug-logs/frame-pacing-verify/20260611-204025/chrome-hover-press-cache`.

## FP-6: Pace popover rendering through the frame scheduler

**Severity: Medium (unthrottled renders from pointer handlers).**

`WaylandWindow.cpp:1628-1636` — `renderPopover()` does `beginFrame`/`render`/`present` synchronously from pointer handlers (call sites at 1647, 1666, …) with no `wl_surface_frame` gate. Bursts of pointer events produce unpaced presents on the popover surface.

What to do:

- [x] [Auto] Route popover redraws through `requestAnimationFrame()` / the shared redraw coalescing used by the main surface (a per-popover `redrawRequested` flag drained by the same frame pump). Linux Wayland popovers now coalesce redraws through a per-popover `wl_surface_frame` callback; the first configured paint remains immediate.
- [x] [Auto] Verify popover first paint, committed-update pacing, and native Escape dismissal. `scripts/verify-wayland-popover-pacing.sh` runs `popover-demo` in headless autotest mode and requires the first configured paint to render immediately, committed reactive updates to render from `wl_surface_frame` callbacks, zero immediate committed redraw renders, and a native popover `onDismiss` after Escape. Latest run passed on 2026-06-13 (`.debug-logs/wayland-popover-pacing/20260613-175456`): escape_driver=backend, initial_render=1, committed_redraw=8, frame_request=8, frame_done=8, paced_render=8, immediate_committed_render=0, autotest_dismissed=1, backend_escape=1.

---

# Phase 3 — Damage & blur cost

## FP-7: Make partial damage coexist with backdrop blur

**Severity: High (glass-themed desktops never use the partial path).**

- `CompositorRenderFrame.cpp:922-928` — `partialDamageCandidate` requires `!sceneUsesBackdropSampling(…)`; `sceneUsesBackdropSampling` (`:486-497`) is true whenever titlebar glass blur or any client `background-blur` is active. One tiny client damage rect then forces full-output compositing.
- `CommittedSurfacePainter.cpp:326` — `drawBackdropBlur` runs per `backgroundBlurRects` entry inside whatever clip is active; blur itself is the expensive part even with correct damage.

What to do:

- [x] [Auto] Use the existing `inflateDamageForBackdropSampling` (`CompositorSceneGraph.cpp`) to *expand* damage by the blur sampling radius around affected blur regions instead of disabling the partial path. Only fall back to full output when an inflated rect actually covers most of the screen.
- [x] [Auto] Cache blurred backdrop results per region: key by (region rect, backdrop content signature) — the Vulkan canvas already has `backdropBlurCache_` keyed by signature; ensure compositor-side damage that does not intersect a blur region's *sample area* leaves its cache entry valid, so unrelated client updates don't re-blur every glass titlebar.
- [x] [Auto] Measure: with one glass titlebar and a terminal printing output, partial frames activate and `background_ms`/`surface_ms` in the CSV drop versus forced full-output frames. `scripts/verify-frame-pacing-linux.sh` now runs a glass-terminal partial/full comparison and fails if partial frames are absent, forced-full partial frames appear, static glass presentation is missing, terminal perf traces are missing, or warm partial `background_ms`/`surface_ms` do not drop. Latest pass: `.debug-logs/frame-pacing-verify/20260611-200703/glass-terminal-partial`, 651 partial frames, `background_ms` 0.008 vs 0.009, `surface_ms` 0.161 vs 0.177, `total_ms` 0.651 vs 0.676.

Verification on Linux (KMS TTY):

- [x] [Auto] No visual artifacts at blur-region edges when neighboring content updates: the KMS scene-damage regression covers a neighboring content update under glass chrome and asserts the inflated blur sampling halo is inside damage; the `multi-dirty-partial` runtime verifier captures titlebar/glass regions during moving checker content and fails on missing, transparent, or visually static titlebar samples. Latest pass: 11 KMS scene-damage cases / 34 assertions and 70 titlebar rows with 61 changing rows.
- [x] [Auto] Frame profile/CPU trace shows partial frames active with glass chrome enabled: the multi-dirty verifier forces a glass config and latest run observed 411 partial frames, 332 multi-rect partial frames, 1,577 prepared blur ops, 1,574 blur runs, 1.678 ms stacked blur work, and surface avg/max 0.059/0.086 ms.

## FP-8: Region-limited scanout copies in KmsOutput

**Severity: Medium (fixed full-screen copy bandwidth every frame; large at 4K).**

- `KmsOutput.cpp:2776-2805` — `finishRenderCommandBuffer` always `vkCmdCopyImage`s the full mode extent from offscreen to scanout on the non-direct-scanout path.
- `KmsOutput.cpp:807, 859-863` — partial frames first copy the **full** displayed primary into the render target (`copyDisplayedPrimaryToRenderTarget` uses the full extent), so partial damage saves fragment shading but no copy bandwidth.
- `KmsOutput.cpp:755-758` — `canPreparePartialFrame` rejects partial while `pendingBuffer_ >= 0`, i.e. for the entire flip window, even though only one flip is in flight.

What to do:

- [x] [Auto] When `buffer.damageRects` is non-empty, issue one `VkImageCopy` region per (merged) damage rect for both the displayed-primary preservation copy and the offscreen→scanout copy, instead of full-extent copies.
- [x] [Auto] Relax `canPreparePartialFrame` to gate on `pageFlipPending_` only, provided the partial source buffer is not the pending one.
- [x] [Auto] Prefer direct scanout (`directScanoutRender_`) when modifiers allow — audit why it is not chosen on the test hardware and log the reason once at startup.
- [x] [Auto] Low priority, same file: reuse the `drmModeAtomicAlloc` request across commits if the driver allows (allocation per schedule at `CompositorRuntime.cpp:1496-1534`).

Verification on Linux (KMS TTY, ideally 4K):

- [x] [Auto] GPU copy time/bandwidth drops for small-damage frames: the forced-offscreen `scanout-copy-partial` verifier records region-copy pixels separately from full-frame copy equivalents and fails if region copies do not reduce copied pixels. Latest full verifier pass: 128 region scanout copies at a 0.241 region/full pixel ratio, primary-preserve region copies at 0.209, and present avg/max 0.533/0.588 ms.
- [x] [Auto] No stale-pixel artifacts when alternating partial and full frames: `partial-full-artifacts` forces alternating full-output and partial frames, saves 48 compositor frames, and verifies current checker-gradient content plus newly exposed old-window background restoration. Latest pass: 207 partial frames, 340 forced-full frames, 528 content matches, and 532/535 exposed samples returned to background in `.debug-logs/frame-pacing-verify/20260611-203400/partial-full-artifacts`.
- [ ] [Manual] VT switch and mode changes still work. Helper added: `scripts/verify-kms-vt-switch.sh` should be run from a real TTY with an explicit `LWM_VT_SWITCH_TARGET`; this Codex session is not a TTY, so it can only verify the script guard.

## FP-9: Cut per-frame snapshot, string, and SHM copy overhead

**Severity: Medium (steady CPU tax scaling with window count).**

- `Snapshots.cpp:367-392` — `committedSurfaces()` builds the full snapshot vector by value (titles, appIds, vectors) and is called at least twice per loop (`CompositorRenderFrame.cpp:667`, empty-damage check at `CompositorRuntime.cpp:1851`).
- `CompositorSceneGraph.cpp:448-454, 834-835` — retained scene state copies full snapshots including strings every frame.
- `CommittedSurfacePainter.cpp:467-468` (also `SurfaceRenderer.cpp:457-458, 495-496`) — `visual.lastSnapshot = surface` copies the full snapshot per draw.
- `SurfaceRenderer.cpp:174-224` — SHM damage upload memcpys each region into a freshly sized `regionPixels` vector before `updatePixelsRegion`.
- `TextNode.cpp:32-34` — `canPrepareRenderOps()` returns false, so static text never uses the PreparedRenderOps cache.

What to do:

- [x] [Auto] Build `committedSurfaces()` once per loop iteration and pass it to both consumers; longer term, keep a persistent snapshot store updated on `contentSerial` change and expose `std::span<CommittedSurfaceSnapshot const>`.
- [x] [Auto] Replace retained scene-state snapshots and `visual.lastSnapshot` with a trimmed struct (id, serial, geometry, mapping, chrome hash) — no strings, no blur-rect vectors.
- [x] [Auto] Upload SHM damage directly from the mapped buffer with a row stride (extend `Image::updatePixelsRegion` to take stride) and reuse a persistent staging buffer per surface; coalesce adjacent rects first (shares the merge helper from FP-2).
- [x] [Auto] Allow `TextNode` to prepare render ops for static layouts (key the cache on layout pointer + dpiScale; invalidate via the existing glyph-atlas generation check).
- [x] [Auto] Add a one-shot log/metric when the dmabuf import fallback path (`SurfaceRenderer.cpp:399-425`, CPU `copyDmabufToRgba`) triggers, so unaccelerated clients are visible instead of silently slow.

Verification on Linux:

- [x] [Auto] CSV/CPU trace `snapshot_ms` drops with ~10 windows open, and malloc-count preload output shows per-frame allocation-count reduction. `scripts/verify-frame-pacing-linux.sh` now includes `snapshot-load-10-windows`; latest full current run reported stable `snapshot_ms` avg/max 0.092/0.096, stable `surface_ms` avg/max 0.042/0.046, and 377.0 allocations/frame. The matched pre-FP-9 `5bcfab46` baseline reported stable `snapshot_ms` avg/max 0.114/0.120, stable `surface_ms` avg/max 0.113/0.119, and 494.5 allocations/frame.
- [x] [Auto] `tests/` snapshot/damage suites still pass; text rendering and frame-capture coverage remain green. Latest focused run passed `CompositorSnapshotBuilderTests.cpp` (2 cases / 14 assertions), `CompositorSceneDamageTests.cpp` (10 / 30), `CompositorSurfaceUploadDamageTests.cpp` (3 / 7), `SceneGraphTests.cpp` (20 / 115), `TextInputScrollTests.cpp` (2 / 11), and `VulkanRenderTargetTests.cpp` (21 / 157).

---

# Phase 4 — Pipeline depth & latent correctness

## FP-10: Asynchronous frame-capture and screenshot readbacks

**Severity: High when capture is active (serializes the pipeline); prerequisite for cheap parity testing.**

`VulkanCanvasLifecycle.inc:537-538` — `flushFrameCapture()` / `flushScreenshot()` run after submit and **before** `vkQueuePresentKHR`, waiting on the frame fence (`VulkanImages.inc:388-391, 443-447`). The KMS render-target path has the same shape, and `VulkanCanvasLifecycle.inc:814-816` makes every render-target present fully synchronous when no external semaphore is provided.

What to do:

- [x] [Auto] Move readback completion off the present path: keep recording the copy into the frame command buffer (`captureFrameIfRequested`), but collect results on a *later* frame (or a worker thread) once the recorded fence has signaled — never between submit and present.
- [x] [Auto] `takeCapturedFrameForCanvas` and the screenshot API may then need a "pending" state; callers (window-manager screenshots, tests) poll or block explicitly rather than implicitly stalling every present.
- [x] [Auto] Document the render-target fence wait at `:814-816` as intentional for offscreen rasterization, or offer the exported-fence variant for callers that can overlap.

Verification on Linux:

- [x] With `LAMBDA_DEBUG_SCREENSHOT_PATH` active, `vulkan-present-detail` shows present no longer blocked by readback waits; captured images remain correct (existing frame-capture parity test still passes under lavapipe). Verified by `.debug-logs/frame-pacing-verify/20260611-153332/validation-scroll-screenshot`: valid `P6` debug screenshot, `waitImage` max 0.000 ms, `queuePresent` max 0.164 ms, and focused validation-layer `VulkanRenderTargetTests.cpp` passed 21 cases / 157 assertions.
- [x] KMS compositor frame capture remains asynchronous enough for resize-storm verification: `scripts/verify-frame-pacing-linux.sh` now runs the resize-storm case with `LWM_SNAP_CAPTURE_ALWAYS=1` / `LWM_SNAP_TRACE_ALWAYS=1` and fails if PNG frames, full-output metrics, or snap trace rows are missing. Latest pass: `.debug-logs/frame-pacing-verify/20260611-161022/resize-storm` saved 12 PNG frames and 240 snap trace frames with zero fatal matches.
- [x] ASan run of capture-heavy Vulkan recorder/render-target tests is clean: `build-asan` passed 22 cases/170 assertions with leak detection enabled.

## FP-11: Re-enable the frame-recorder prepared-geometry fast path safely

**Severity: High for the compositor's per-surface cache (double copy + upload every replayed frame today).**

- `VulkanCanvasDrawOps.inc:904-907` — `canUsePreparedGeometry` is hard-coded `false && …` citing a RADV crash; replay copies recorded CPU geometry into the live vectors and re-uploads, while `prepareRecorderBuffers()` still allocates VMA buffers + descriptors that are never bound.
- `VulkanCanvasDrawOps.inc:760-761` — `ensureRecorderBuffer` returns early when capacity suffices and **skips the content upload**: latent stale-data bug that becomes live the moment the fast path is re-enabled.

What to do:

- [x] [Auto] Fix the upload-skip first: always `uploadRecorderBuffer` when `data` is provided, or skip only when the recorded geometry signature is unchanged (signatures already exist — `prepareRecordedGeometrySignatures`).
- [x] [Auto] While the fast path stays off, stop allocating the unused prepared buffers/descriptors in `prepareRecorderBuffers()` (alloc only when the fast path will bind them).
- [x] [Auto + Manual] Scope the prepared-geometry workaround to the affected driver via `VkPhysicalDeviceDriverProperties::driverID`, keep `LAMBDA_VULKAN_PREPARED_GEOMETRY=1` as a RADV reproduction override, and flip `canUsePreparedGeometry` on for unaffected drivers (lavapipe, ANV, NVIDIA).
- [x] [Auto] Add a lavapipe regression test that replays a recorder twice with mutated geometry between replays (catches the stale-upload bug) — extend `tests/VulkanRenderTargetTests.cpp`.

Verification on Linux:

- [x] [Auto] Compositor CPU trace shows cached-window `surface_ms` is low, and validation layers are clean under RADV. Latest evidence: `.debug-logs/frame-pacing-verify/20260611-180528/surface-cache-static` reported 521 hits, 1 miss, zero transient-chrome blocks, and surface avg/max 0.012/0.013 ms; `.debug-logs/fp11-validation/20260611-181036/vulkan-validation-radv.log` used `VK_LAYER_KHRONOS_validation` with `AMD Radeon Graphics (RADV RENOIR)` and passed 21 cases / 157 assertions.

## FP-12: Tie deferred destruction and swapchain acquire to real frame completion

**Severity: Medium (latent use-after-free under resize storms; acquire leak on a defensive path).**

- `VulkanCanvasLifecycle.inc:432-434` — `destroyDeferred*(false)` decrements `framesRemaining` once per `presentImpl()` entry, **including** the OUT_OF_DATE early return (`:441-443`) and the missing-`imageRenderFinished` early return (`:455-458`) where nothing was submitted. The render-target cache-hit path (`:721-727`) also decrements without GPU work. Countdown is `kMaxFramesInFlight + 1` (`VulkanCanvasDrawOps.inc:471, 482`; `VulkanImages.inc:118, 192`).
- `VulkanCanvasLifecycle.inc:455-458` — returns after a **successful** `vkAcquireNextImageKHR` without presenting or releasing the image; repeated hits can exhaust acquirable images.

What to do:

- [x] [Auto] Move the deferred-destroy decrement to immediately after a successful `vkQueueSubmit2` (and after the render-target submit), so aborted presents and cache hits no longer age the queues.
- [x] [Auto] Validate `imageRenderFinished_` sizing *before* calling `vkAcquireNextImageKHR`; if the defensive condition can still trigger post-acquire, recreate the swapchain immediately (which releases acquired images) rather than returning.
- [x] [Auto] Add a debug assertion (validation builds) that a deferred entry's countdown only reaches zero after at least `kMaxFramesInFlight` successful submits.

Verification on Linux:

- [x] Automated resize-storm smoke drives compositor-side configure changes under CPU/KMS/pacing traces; latest run had 19 resize/configure events, 560 sizing cache-block samples, and zero fatal matches.
- [x] ASan + validation-layer run while resizing a window violently (configure storms) — no use-after-free, no `vkAcquireNextImageKHR` returning `VK_NOT_READY`/timeout from image exhaustion. Verified by `.debug-logs/frame-pacing-verify/20260611-155247/asan-validation-resize-storm`: zero validation errors, zero ASan errors, zero fatal matches, valid `CLOCK_MONOTONIC`/`VSYNC|HW_CLOCK|HW_COMPLETION` presentation feedback.

## FP-13: Cheaper swapchain recreation and present-fence policy

**Severity: Medium (full pipeline drain per resize step; extra sync per present).**

- `VulkanSwapchain.inc:10-13` — `recreateSwapchain()` waits **all** frame fences on the present thread before rebuilding (called from `present()` at `VulkanCanvasLifecycle.inc:386-388`).
- `VulkanCanvasLifecycle.inc:551-572` — with `swapchainMaintenance1`, every present first waits (up to 1 s) on the per-image present fence; fences are created signaled (`VulkanSwapchain.inc:105-106`). This adds a CPU sync per image reuse on top of acquire back-pressure.

What to do:

- [x] [Auto] Use the existing `oldSwapchain` handoff (`VulkanSwapchain.inc:89`) without draining all frame fences: keep the old swapchain alive in a retire list and destroy it when its images' present fences (maintenance1) or frame fences signal. Only the first-ever create needs no wait at all.
- [x] [Auto] Restrict the present-fence wait to swapchain retirement and resource destruction; steady-state presents should rely on acquire back-pressure alone. Keep `presentFenceRuntimeDisabled_` as the escape hatch.
- [x] [Auto] Interactive resize on Wayland: trace `vulkan-recreate-swapchain` and confirm recreate cost no longer scales with frames in flight; extent-headroom growth (only-grow logic at `VulkanCanvasLifecycle.inc:217-224`) still avoids most recreates. `scripts/verify-wayland-resize-recreate.sh` runs scripted terminal self-resize under headless Weston GL and fails on missing resize steps, excessive recreates, expensive recreate elapsed time, or retire-queue stalls. Latest pass: `.debug-logs/wayland-resize-recreate/20260611-201230`, 90 resize steps, 2 recreates, 88 clean no-recreate resize decisions, recreate elapsed max 0.831 ms, retire-queue max 0.001 ms.

## FP-14: Client-side pacing — flush on frameDone, avoid double throttling, align platforms

**Severity: Medium (added latency per frame on Wayland clients; platform asymmetry).**

- `WaylandWindow.cpp:1844-1861` — `frameDone` posts a `FrameEvent` and wakes the loop, but `presentRequestedWindows` already ran this exec iteration (`Application.cpp:838-839`), so the present happens one loop trip later. macOS flushes inline from the display-link tick (`Application.mm:737-739`, `MacMetalWindow.mm:2002-2010`).
- `WaylandWindow.cpp:1302-1321` + `VulkanSwapchain.inc:33-38` — rendering is gated on `wl_surface_frame` **and** blocks in acquire/present. Fine under MAILBOX; on FIFO-only drivers the throttles stack.
- `MacMetalWindow.mm:2002-2010` — legacy off-main-thread CVDisplayLink only posts + wakes (extra latency vs the modern path).
- `Application.mm:737-739` vs `Application.cpp:838-839` — macOS `flushRedraw` skips the frame-ready gate; Linux exec requires it.

What to do:

- [x] [Auto] Call `Application::flushRedraw()` from `frameDone` after dispatch (mirroring macOS), or re-run the present pass after Wayland dispatch within the same loop iteration.
- [x] [Auto] When the active present mode is FIFO (no MAILBOX), drop the `wl_frame` gate and let FIFO pace presentation; keep the gate under MAILBOX. Consider `VK_PRESENT_MODE_FIFO_RELAXED_KHR` where available.
- [x] [Auto] Legacy CVDisplayLink path: `CFRunLoopPerformBlock` to the main run loop with `flushRedraw`, matching the modern CADisplayLink path.
- [x] [Auto] Document (or unify) the `requireFrameReady` asymmetry between `Application.cpp` exec and `Application.mm flushRedraw` so future platform work doesn't diverge further.

Verification:

- [x] [Auto] Wayland: `LAMBDA_RESIZE_TRACE=1 app-render` traces show render starting in the same dispatch/flush path as `frameDone`. `scripts/verify-wayland-client-pacing.sh` runs `lambda-terminal` under headless Weston GL and asserts frameDone-to-present deltas; latest avg/max 1.340/5.637 ms.
- [x] [Auto] Interactive Wayland resize and animation demos hold refresh rate on a FIFO-only driver (e.g. lavapipe window). `scripts/verify-wayland-client-pacing.sh` now forces FIFO with `LAMBDA_VULKAN_FORCE_FIFO_PRESENT_MODE=1`, asserts `mode=fifo` request-frame traces, fails if any mailbox request appears, and keeps frameDone-to-present under the 8 ms gate. Latest pass: `.debug-logs/wayland-client-pacing/20260611-201500`, 22 FIFO request frames, zero mailbox frames, avg/max 1.529/6.312 ms.
- [x] [Auto] Existing input/animation tests pass on both platforms. Linux refresh: `RuntimeInputTests.cpp` under headless Weston passed 33 cases / 198 assertions with the existing `may_fail` autofocus case allowed, and `AnimationTests.cpp` passed 11 / 53. macOS evidence: full `ctest` passed 2/2 on 2026-06-11.

## FP-15: Presenter-level pacing correctness (KMS + Vulkan-display fallback)

**Severity: Medium (fallback presenter mispaces clients; feedback protocol inaccuracies).**

- `KmsOutput.cpp:655-665` + `CompositorRuntime.cpp:1302-1311` — KMS `IN_FENCE_FD` is opt-in via `LAMBDA_COMPOSITOR_USE_KMS_IN_FENCE`; without it the loop waits for the render sync-fd before committing instead of letting the kernel wait GPU-side.
- `CompositorRuntime.cpp:2268-2286` — Vulkan-display fallback blocks on `waitForVblank()` **and** then on swapchain fences inside `canvas.present()` (double pacing).
- `CompositorRenderFrame.cpp:1122-1127` — the fallback sends `wl_frame` callbacks immediately after `canvas.present()`, before scanout (atomic path correctly defers to flip: `CompositorRuntime.cpp:1591-1594`).
- `FrameScheduler.cpp:386-396` — presentation-feedback fallback (500 ms) reports `presented` with render-time estimates instead of `discarded`; `normalizePresentationTiming` (`:93-98`) substitutes "now" for zero timestamps; `wl_callback_send_done` uses compositor monotonic ms, not flip time (`:327-329`).
- `KmsOutput.cpp:3505-3530` — one `drmWaitVBlank` failure permanently degrades `waitForVblank()` to an unanchored `sleep_for(refresh)`.
- `KmsOutput.cpp:2965-2973` — blocking `waitForPageFlip()` remains in the synchronous `present()` API (compositor uses the async dispatch; keep apps off the sync path).

What to do:

- [x] [Auto] Probe and enable `IN_FENCE_FD` by default when the primary plane supports it; keep the env var as an opt-out; extend `allowRenderFence` to non-render-ahead frames.
- [x] [Auto] Vulkan-display fallback: pick one pacing source (drop the vblank wait and let swapchain pacing rule, or use `VK_GOOGLE_display_timing` when available); buffer `wl_frame` callbacks per presentId and dispatch them from the present-timing poll, mirroring the atomic path.
- [x] [Auto] Presentation feedback: send `discarded` on fallback expiry (shorten to ~2 refresh periods); never substitute render-time for zero timestamps on HW-completion feedback; pass flip/vblank ms into `sendFrameCallbacksOnly`.
- [x] [Auto] Anchor the vblank sleep fallback to the last flip timestamp (phase-locked) and retry `drmWaitVBlank` per-CRTC instead of disabling it forever.
- [x] [Auto] Add the schedule→flip→feedback integration test that `tests/CompositorPresentationFeedbackTests.cpp` currently lacks (assert `tv_sec/nsec`, `refresh`, `seq`, `HW_COMPLETION` flags through a mock presenter).

Verification on Linux (KMS TTY):

- [x] `weston-presentation-shm` (or equivalent client) reports sane presentation timestamps on both presenters.
- [x] With IN_FENCE enabled, CSV `present_ms` shows commit no longer waiting for render-fence readiness.

## FP-16: Metal path improvements (macOS — can be done on this machine)

**Severity: Medium-high in aggregate (atlas-grow hitches, backdrop blur cost, end-of-frame drawable stalls).**

Implementation note: the code changes are in place, but macOS compile/runtime verification is still pending
because the current implementation pass ran on Linux.

- `GlyphAtlas.mm:68-107` — `grow()` clears all entries, doubles dimensions, and synchronously re-rasterizes/re-uploads every glyph; `prepareForFrameBegin`/`afterPresent` (`:56-66`) trigger it. `uploadR8` (`:23-29`) does a synchronous `replaceRegion` per glyph.
- `MetalCanvas.mm:1992-2000` — backdrop frames allocate `finalUniformBuffer`/`finalClipBuffer` with `newBufferWithLength` every present; `encodeBackdropBlur` (`:1944-1956`) runs 3 iterations × 2 passes at full resolution (Vulkan downsamples; Metal does not); backdrop frames disable MSAA (`:637-649`).
- `MetalCanvas.mm:597-634` — `nextDrawable` is acquired after all CPU uploads; with `allowsNextDrawableTimeout = YES` a nil drawable abandons the fully recorded frame.
- `MacMetalWindow.mm:99-106` — `displaySyncEnabled` never set explicitly.
- `MetalCanvas.mm:2791-2795` — render-target mode without a shared event uses `waitUntilCompleted` (prefer `MTLSharedEvent`, already supported via `targetSpec_.signalValue`).

What to do:

- [x] [Auto + Manual] Copy-preserving atlas grow: blit the old atlas texture into the new one and keep entries; batch new-glyph uploads through a blit encoder; grow only in `afterPresent` with a headroom threshold.
- [x] [Auto] Pool the two backdrop buffers grow-only on `MetalCanvas` (like the existing triple-buffered arenas), and add a downsampled blur chain mirroring Vulkan's `backdropBlurDownsample()`.
- [x] [Auto] Acquire the drawable right after the in-flight semaphore wait in `beginFrame` (hold it through encode) and retry `nextDrawable` once before dropping a frame.
- [x] [Auto] Set `metalLayer.displaySyncEnabled = YES` explicitly with a comment documenting the intent.
- [x] [Auto] Prefer `MTLSharedEvent` for render-target consumers instead of `waitUntilCompleted`.

Verification on macOS:

- [x] Full `ctest` suite passes, including `MetalCanvasTests.mm` and the deferred atlas grow regression.
- [ ] `debug::perf` runtime check — run `scripts/verify-macos-metal-editor-perf.sh` on macOS; it drives a large unicode-heavy paste into `lambda-editor`, gates `CanvasDrawableWait` p95/frame-budget p99, and requires `atlasGrow` perf evidence by default.
- [ ] Backdrop blur visuals unchanged by frame-capture/manual comparison.

---

# Post-implementation code review backlog

No open REV code items remain from the 2026-06-11 review of commits `50d65831..HEAD`.

# Citation errata (original FP audit → current sources)

| Original claim | Current status | Correct anchor |
| --- | --- | --- |
| Atlas upload via `uploadTexture` + `vkQueueWaitIdle` on hot path | Fixed — uses `queueAtlasUploadIfNeeded` → `queueTextureUpload` | `VulkanGlyphAtlas.inc:127-132`, `VulkanImages.inc:151+`, `243-254` |
| `transitionImmediate` on imported images | Fixed — uses `queueTextureTransition` | `VulkanImages.inc:31-33` |
| Per-rect `drawFrameContent` loop on partial damage | Fixed — sparse multi-rect clip with bounding-union fallback | `CompositorRenderFrame.cpp:509-533`, `1130-1144` |
| `sceneUsesBackdropSampling` blocks partial damage | Removed — uses `inflateDamageForBackdropSampling` | `CompositorSceneGraph.cpp:216-243`, `CompositorRenderFrame.cpp:1005-1010` |
| `markFrameRendered` `vkQueueWaitIdle` fallback | Fixed — no `vkQueueWaitIdle` in `KmsOutput.cpp` | `KmsOutput.cpp:1087-1095` |
| `canUsePreparedGeometry = false &&` | Fixed — driver-gated `recorderPreparedGeometryFastPathEnabled()` | `VulkanCanvasDrawOps.inc:830-838`, `978-988` |
| `ensureRecorderBuffer` skips upload | Fixed — uploads on content-signature changes and skips unchanged buffers | `VulkanCanvasDrawOps.inc:795-832` |
| Deferred destroy before acquire / on early return | Fixed — `retireDeferredResourcesAfterSubmit` after submit only | `VulkanCanvasLifecycle.inc:558`, `VulkanImages.inc:265-269` |
| Present-fence 1 s blocking wait | Fixed — `vkGetFenceStatus` + replacement | `VulkanCanvasLifecycle.inc:575-597` |
| `recreateSwapchain` waits all frame fences | Fixed — retire list, no frame-fence drain | `VulkanSwapchain.inc:131-152` |
| Swapchain recreate unconditional stderr | Fixed — gated by `resizeTraceEnabled()` | `VulkanSwapchain.inc:184-203` |
| `flushFrameCapture` before present | Fixed — `pollReadbacks(false)` after submit | `VulkanCanvasLifecycle.inc:561`, `VulkanImages.inc:613-616` |
| Transient chrome blocks recorder replay | Fixed — `stableChromeSnapshot` + `drawTransientChromeControls` | `SurfaceRenderer.cpp:137-145`, `544-572` |
| SHM upload memcpy into `regionPixels` vector | Fixed — span subview with stride | `SurfaceRenderer.cpp:236-243` |
| `TextNode::canPrepareRenderOps` always false | Fixed — returns `layout_ != nullptr` | `TextNode.cpp:50-52` |
| `frameDone` present one loop late | Fixed — inline `flushRedraw()` | `WaylandWindow.cpp:1908-1927` |
| Metal `grow()` clears and re-rasterizes all glyphs | Fixed — copy-preserving blit grow | `GlyphAtlas.mm:111-172` |
| Metal backdrop full-res 3×2 blur, per-frame buffer alloc | Fixed — downsample + per-frame-slot pooled buffers | `MetalCanvas.mm:1933-1966`, `2031-2043`, `2090-2103` |
| `displaySyncEnabled` never set | Fixed — `YES` at create | `MacMetalWindow.mm:104` |
| Presentation feedback 500 ms fake-presented fallback | Fixed — ~2 refresh periods, `discarded` on expiry | `CompositorPresentation.hpp:96-97`, `FrameScheduler.cpp:137-149` |

---

## Exit criteria

- [x] All REV items above deleted as fixed and verified.
- [ ] All unchecked manual verification boxes under FP-1 … FP-16 closed.
- [ ] `vulkan-present-detail`, compositor frame CSV, and `debug::perf` counters within budget; no `vkQueueWaitIdle` on per-frame paths (teardown/rasterize exceptions documented in REV-V11/V12).
- [x] Validation-layer run clean on Linux (resize storm, text scroll, capture paths).
- [ ] Remove this document and TODO-019 from `TODO.md`, and drop the row from `docs/roadmap.md`.
