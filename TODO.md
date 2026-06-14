# TODO

When starting work, read all TODO items in this document, pick one item to complete, and ask questions until the bug or TODO item is perfectly clear before starting implementation. Handle any directly related work that can be fixed or completed together. Always build with `-j$(nproc)`. Always try to automate testing or verification of the implementation when possible; if automatic verification is not practical, tell the user exactly how to verify it manually. Then update this document by deleting the completed item or revising its description based on the outcome, commit the changes, and push.

Verification labels: `[Auto]` means the item can be automatically tested or verified. `[Manual]` means the item requires manual verification. `[Auto + Manual]` means automated coverage should be added, but manual verification is also required.

## TODO Summary

| ID | Type | Item | Severity | Priority |
| --- | --- | --- | --- | --- |
| TODO-006 | Bug | Window close animation is inconsistent across window types | Medium | P2 |
| TODO-007 | Bug | Minimized windows and dock previews are not wired across Window Manager and Shell | Medium | P2 |
| TODO-008 | Bug | Live resize can stretch stale window content | Medium | P1 |
| TODO-009 | Bug | Files opens supported images in Firefox instead of Preview | Medium | P1 |
| TODO-014 | Bug | Tooltips are not showing | Medium | P1 |
| TODO-015 | Feature | Add a cross-window command registry and command palette | N/A | P2 |
| TODO-016 | Bug | useAutoFocus cannot focus targets inside nested child components | Medium | P2 |
| TODO-019 | Performance | Work through the frame-pacing improvement plan | High | P1 |
| TODO-020 | Feature | Complete SVC-1 D-Bus capability details | N/A | P0.5 |
| TODO-021 | Feature | Complete SVC-2 libseat seat/session integration | N/A | P0.5 |
| TODO-022 | Feature | Complete SVC-3 logind power/session integration | N/A | P0.5 |
| TODO-023 | Feature | Complete SVC-4 xdg-desktop-portal backend | N/A | P0.5 |
| TODO-024 | Feature | Complete SVC-6 notifications daemon and Shell UI | N/A | P0.5 |
| TODO-025 | Feature | Complete SVC-7 StatusNotifierWatcher and tray host | N/A | P0.5 |
| TODO-026 | Feature | Complete SVC-11 UPower power-status integration | N/A | P0.5 |
| TODO-027 | Feature | Complete SVC-9 NetworkManager connectivity integration | N/A | P0.5 |
| TODO-028 | Feature | Complete SVC-10 BlueZ Bluetooth integration | N/A | P0.5 |
| TODO-029 | Feature | Complete SVC-12 MPRIS media integration | N/A | P0.5 |

## TODO-006: Window close animation is inconsistent across window types

- [ ] [Manual] When a window is closed, the chrome currently disappears before the window contents in some cases. The full window should fade out as one unit.
- [ ] [Auto + Manual] Use a single snapshot of the whole window for the close animation, including chrome, content, shadow, and transparent regions.
- [ ] [Auto] Stop input to the closing window immediately when the close starts.
- [ ] [Auto + Manual] Implement the close animation through one standard compositor path instead of app-specific behavior.
- [ ] [Auto + Manual] Ensure close behavior is consistent for apps with system titlebars, apps with integrated/custom titlebars, transparent content, undecorated windows, and normal opaque content.
- [ ] [Manual] Verify by closing representative windows from multiple app types and confirming chrome, shadow, and content fade together with no separate disappearance.

## TODO-007: Minimized windows and dock previews are not wired across Window Manager and Shell

- [ ] [Auto] Keep the ownership boundary explicit: Window Manager owns minimized window state, focus/stacking exclusion, Super+Tab eligibility, preview/snapshot source, and restore requests; Shell owns dock item presentation, opening space in the dock, click targets, and dock-side animation.
- [ ] [Auto + Manual] Define or reuse a Window Manager/Shell IPC contract for minimized window identity, preview updates, restore requests, and animation timing instead of letting Shell infer minimized state from the normal running-app list alone.
- [ ] [Auto + Manual] Minimized windows should disappear from the desktop and should not appear in the Super+Tab window list while minimized.
- [ ] [Manual] When minimizing, Shell should animate open space for the minimized window item while Window Manager simultaneously scales the window down and moves it into that dock space.
- [ ] [Auto + Manual] The dock item should show a window preview, and the preview should stay up to date during the minimized period.
- [ ] [Manual] Clicking the minimized window preview in the dock should restore the window with the reverse animation.
- [ ] [Auto + Manual] Restoring should return the window to the size, position, stacking behavior, and focus state it had before minimization.
- [ ] [Auto] Support multiple minimized windows as distinct restorable window previews rather than losing per-window identity.
- [ ] [Manual] Verify with multiple app windows that minimized windows leave the desktop, are excluded from Super+Tab, keep their dock previews current, and restore with the reverse animation.

## TODO-008: Live resize can stretch stale window content

- [ ] [Manual] Resizing the terminal app sometimes causes the content to stretch, even when the terminal is minimal.
- [ ] [Auto + Manual] Keep this tied to the compositor frame-coherence work in `docs/compositor-wlroots-improvement-plan.md` WM-COMP-1: the fix should build one coherent frame model for chrome, borders, background, and client content from a single committed geometry snapshot.
- [ ] [Auto + Manual] During live resize, existing window content/framebuffer should remain unscaled and aligned to the top-left.
- [ ] [Auto + Manual] Newly exposed space should be filled immediately with the correct window/app background during live resize.
- [ ] [Auto + Manual] The terminal is the known reproduction case, but verify other app windows too and fix any shared compositor or rendering path that can stretch content during resize.
- [ ] [Manual] Verify by resizing Terminal and representative non-terminal apps while watching for stretched content, stale scaled framebuffer regions, or delayed background fill.

## TODO-009: Files opens supported images in Firefox instead of Preview

- [ ] [Manual] Clicking a supported image in Files currently opens it in Firefox, likely because app/MIME association falls through to the browser.
- [ ] [Auto + Manual] Supported image files should open in `lambda-preview` without requiring Preview to be installed into the host system.
- [ ] [Auto] Prefer solving this through Flux's local app registry/open-with path: give the local development `lambda-preview` app entry the MIME types Preview can actually open, then have Files choose it for those types.
- [ ] [Auto] Preview-supported MIME types should include the formats handled by the current image loader and Files MIME detector, including `image/png`, `image/jpeg`, `image/gif`, `image/webp`, and `image/svg+xml`; add other formats only if `lambda::loadImage` can actually decode them.
- [ ] [Auto] Keep system `mimeapps.list` support for installed apps, but local Flux app associations should work in a build-tree/development run without writing desktop files to the user's system.
- [ ] [Auto] Add or update tests around local `lambda-preview` registration and Files default open-with resolution so supported images choose Preview instead of Firefox/browser fallback.
- [ ] [Manual] Verify manually by running from the development build and opening PNG, JPEG, and SVG files from Files.

## TODO-014: Tooltips are not showing

- [ ] [Manual] Tooltips are not showing at all when running the tooltip demo, even though they previously worked at least on macOS.
- [ ] [Auto + Manual] Fix the shared `Tooltip`/`useTooltip` implementation rather than adding local tooltip popover behavior per app or demo.
- [ ] [Auto + Manual] Verify the tooltip demo shows tooltips after hover delay for buttons, icons, toggles, and placement examples.
- [ ] [Auto + Manual] Verify Editor toolbar buttons use the same real tooltip implementation and show their labels on hover.
- [ ] [Auto] Add automated coverage where practical for tooltip state/lifecycle behavior, including hover enter, hover exit, timer cancellation, and avoiding stale tooltip popovers after the target unmounts.

## TODO-015: Add a cross-window command registry and command palette

- [ ] [Auto + Manual] Use `lambda-editor` as the first pilot app/window for the command registry and command palette.
- [ ] [Auto + Manual] Use `Ctrl+Shift+P` as the v1 command palette shortcut for the focused editor window. Do not show the command palette command itself inside the command palette.
- [ ] [Auto] Keep v1 window-scoped only; do not include app-level, Shell-level, compositor-level, or cross-window/global command scopes in the first implementation.
- [ ] [Auto] Give each command a stable public ID so future user keybinding configuration can refer to commands without depending on display titles.
- [ ] [Auto] Use namespaced command IDs for standard and app commands, such as `edit.copy`, `edit.cut`, `edit.paste`, `edit.selectAll`, `file.open`, and `editor.find`, instead of plain unscoped names.
- [ ] [Auto + Manual] Disabled commands should remain visible in the command palette, but be visually clear as disabled and should not invoke.
- [ ] [Auto] Commands should not take invocation arguments in v1. Treat a command as the non-visual representation of a toolbar/menu/keybinding action: the command handler decides what to do from current window/view state.
- [ ] [Auto + Manual] Dispatch command shortcuts through a focus-first bubbling model, similar to other input events: the focused view gets first chance to handle a matching command/shortcut; if it handles the command, propagation stops, and only unhandled commands bubble through containing view/window command scopes. For example, text input can handle `Ctrl+X`, while unrelated commands such as a future `Super+Q` can bubble to a higher scope.
- [ ] [Auto + Manual] Separate physical keybindings from semantic commands: framework/window-level keybinding resolution should translate shortcuts into command IDs, and views should handle commands such as `copy`, `cut`, or `selectAll` without needing to know whether the shortcut was `Ctrl+C`, `Ctrl+Shift+C`, or a user-configured binding.
- [ ] [Auto + Manual] Deliberate and define the first standard command list before implementation. Treat common editing actions as likely standard commands, including copy, cut, paste, select all, undo, redo, find, replace, delete, delete word, select word, move by word, and related text navigation/editing commands.
- [ ] [Auto + Manual] Standard commands should allow different surfaces to bind the same semantic command to different physical shortcuts when necessary. For example, normal text inputs can map `Ctrl+C` to `copy`, while Terminal can map `Ctrl+Shift+C` to `copy` and keep `Ctrl+C` available for terminal interrupt behavior.
- [ ] [Auto + Manual] Text input and editor views should expose command handlers for semantic editing commands instead of embedding all keybinding policy locally, so global/user keybinding changes can happen without rewriting each view.
- [ ] [Auto + Manual] Use a responder-chain style command handling model for v1 unless implementation reveals a better local pattern: focused view handlers get first chance to handle semantic commands; handled commands are consumed and stop propagation, while unhandled commands continue to containing views/window-level handlers. Keep room for document/window state to register palette-visible commands and metadata.
- [ ] [Auto] If multiple commands register the same shortcut in the same effective scope, the last registered command wins for dispatch.
- [ ] [Auto + Manual] Add conflict diagnostics for duplicate shortcuts where practical: use compile-time/static checks for static command definitions and first-run/runtime diagnostics for dynamic registrations.
- [ ] [Auto] Model a command as a centrally registered action with at least a stable ID, title, optional description, optional icon, optional shortcut, optional group/category, enabled/disabled state, visibility state, and an invocation handler.
- [ ] [Auto + Manual] Keep the first implementation window-scoped: each active window can register and unregister its commands as its focused view/document state changes.
- [ ] [Auto + Manual] Build one shared command registry path that can be used by shortcuts, toolbar buttons, context-specific action surfaces, and a searchable command palette instead of each UI surface owning separate action logic.
- [ ] [Auto + Manual] Add a searchable command palette for the focused window, inspired by VS Code's Command Palette: commands should be searchable by title/category/description, grouped for discoverability, and invokable without requiring toolbar placement.
- [ ] [Auto] Use clear command names and group/category labels so users can discover related actions without memorizing shortcuts.
- [ ] [Auto] Shortcuts should dispatch through the same command registry used by the palette and toolbar actions; do not keep independent shortcut-only action paths for commands that are registered.
- [ ] [Auto + Manual] Toolbar buttons should be able to bind to command IDs so enabled state, tooltip/title, icon, shortcut display, and action execution come from the command metadata.
- [ ] [Auto + Manual] The registry should make rich app functionality discoverable without requiring traditional menus or permanently visible toolbar buttons.
- [ ] [Auto] Design the API so it can later grow from per-window commands into app-level, Shell-level, compositor-level, and individual-view command scopes without rewriting the initial command metadata model.
- [ ] [Auto + Manual] Add focused tests for command registration/unregistration, command lookup, command invocation, enabled/visibility filtering, shortcut dispatch, and toolbar/palette binding behavior.
- [ ] [Manual] Verify manually with a representative app that commands can be invoked from the palette, toolbar buttons, and shortcuts while staying synchronized as app state changes.

## TODO-016: useAutoFocus cannot focus targets inside nested child components

- [ ] [Auto] `useAutoFocus` requests focus with `ComponentKey::fromScope(hookScope)` and `Runtime::requestFocus` matches targets via `stableTargetKey_.hasPrefix(key)`, but stable target keys only carry the nearest body scope id: `HookInteractionSignalScope` builds its key fresh from `fromScope(owner)` instead of extending the parent scope key on the stack.
- [ ] [Auto] As a result, focusable targets mounted inside nested child components (their own `body()` scopes) can never match the hook's scope key. Only focusables mounted directly in the same body scope work (the current `lambda-editor` usage).
- [ ] [Auto] The test "auto focus requests first focusable target inside hook scope" in `tests/RuntimeInputTests.cpp` documents the expected nested behavior and is currently marked `doctest::may_fail`; remove the decorator when fixing.
- [ ] [Auto] A fix likely needs hierarchical scope keys (extend the parent key in `HookInteractionSignalScope`) plus stable accumulation across `For`/`Show` remounts (capture the parent key at view creation and re-push it during reconcile), since `stableTargetKey_` equality is also used for focus restore, hover tracking, command registry prefix walks, and overlay anchors. Scope this carefully before implementing.
- [ ] [Auto] This was masked until the FileDialogTests segfault (fixed) aborted the suite before `RuntimeInputTests.cpp` ran.

## TODO-019: Work through the frame-pacing improvement plan

- [x] [Auto] Implement the prioritized workstreams in [docs/frame-pacing-improvement-plan.md](docs/frame-pacing-improvement-plan.md) (FP-1 through FP-16), produced by the 2026-06 compositor and graphics-stack frame-pacing review.
- [x] [Auto] Verify the Linux code paths with clean normal/KMS builds, focused compositor/Vulkan/reactive tests, and a KMS compositor run with shell, terminal workload, editor, CPU tracing, KMS timing traces, and `vulkan-present-detail` logs.
- [x] [Auto] Fix the compile warnings found by clean normal/KMS rebuilds.
- [x] [Auto] Add and run the repeatable Linux verifier script, including the repo-local `wp_presentation` timestamp client for both atomic-KMS and Vulkan-display presenters.
- [x] [Auto] Add and run the static decorated-surface cache verifier; latest run showed 500 surface draw-cache hits, 1 miss, zero transient-chrome blocks, and surface avg/max 0.011/0.012 ms.
- [x] [Auto] Add and run the KMS synthetic chrome hover/press verifier; latest run drove close-button move, press, move-away, and release events, with 4 surface draw-cache hits, 1 miss, and zero transient-chrome blocks.
- [x] [Auto] Harden sampled CPU tracing so pointer motion no longer crashes inside the sampler signal handler; latest KMS pointer verifier survived 180 synthetic pointer events with sampled trace output and no new compositor coredump.
- [x] [Auto] Add and run the scripted resize-storm verifier; latest run exercised 18 resize/configure events, 522 sizing cache-block samples, zero fatal matches, and surface avg/max 1.117/1.299 ms.
- [x] [Auto] Add and run ASan coverage for capture-heavy Vulkan recorder/render-target tests; latest run passed 22 cases/170 assertions and covered replay after the recording canvas is destroyed.
- [x] [Auto] Rerun the full Linux verifier under `perf stat`; latest run passed atomic, pointer-fast-path, surface-cache, chrome hover/press, resize-storm, and Vulkan-display cases with zero fatal matches, 180/180 pointer fast-path moves, standard app smoke, and `perf` counters captured in `.debug-logs/perf/frame-pacing-20260611-144159.stat`.
- [x] [Auto] Final Linux validation after the review fixes: full `ctest` passed under headless Weston, Vulkan validation layers passed (21 cases/157 assertions), rebuilt KMS focused slice passed (6 cases/79 assertions), normal focused slice passed (6 cases/79 assertions), and ASan Vulkan recorder/render-target slice passed (4 cases/65 assertions).
- [x] [Auto + Manual] Work through the post-implementation review backlog (REV-V1 … REV-V12, REV-K1 … REV-K8, REV-M1 … REV-M6, REV-W1 … REV-W4, REV-F2/F4/F5/F13) in [docs/frame-pacing-improvement-plan.md](docs/frame-pacing-improvement-plan.md); all REV sections are deleted as fixed.
- [x] [Auto] Rerun the Linux verifier after installing `evemu` and `wayland-utils`; latest run passed atomic, pointer-fast-path, surface-cache, chrome hover/press, resize-storm, and Vulkan-display cases with zero fatal matches, and reported `ydotool`, `wtype`, and `evemu-event` available.
- [x] [Auto] Run real evdev hardware-input validation after installing `evemu`: latest run injected 60 relative pointer events into `/dev/input/event6`, KMS/libinput logged 60 raw pointer motions, the hardware-cursor fast path logged 60 moves, presentation feedback reported `CLOCK_MONOTONIC` with `VSYNC|HW_CLOCK|HW_COMPLETION`, and there were zero fallback/unavailable moves or fatal matches.
- [x] [Auto] Add and run repeatable real-evdev hardware-input validation: `scripts/verify-evemu-hardware-input.sh` requires an explicit `LWM_EVEMU_DEVICE`, enables KMS input debug logging, and now fails unless it sees a pointer device, raw libinput motion, hardware-cursor fast-path moves, hardware presentation feedback, zero fallback/unavailable moves, and zero fatal matches. Latest strict run passed against `/dev/input/event6` with 12 `evemu-event` relative-motion commands, 2 pointer-device logs, 12 raw-pointer logs, 12 hardware-cursor fast-path moves, hardware presentation feedback, and zero fatal matches.
- [x] [Auto] Fix and rerun the runtime Vulkan validation-layer text-scroll/debug-screenshot path: latest run produced 946 `vulkan-present-detail` samples, loaded `VK_LAYER_KHRONOS_validation`, reported zero validation errors and zero fatal matches, wrote a valid debug screenshot, and kept `waitImage` max at 0.000 ms.
- [x] [Auto] Fix and rerun the ASan + Vulkan validation-layer KMS resize-storm path: latest run loaded `VK_LAYER_KHRONOS_validation`, reported zero validation errors, zero ASan errors, and zero fatal matches, and presentation feedback reported `CLOCK_MONOTONIC` with `VSYNC|HW_CLOCK|HW_COMPLETION`. Follow-up normal Linux verifier passed atomic, pointer-fast-path, surface-cache, chrome hover/press, resize-storm, and Vulkan-display cases with zero fatal matches.
- [x] [Auto] Add and run multi-dirty partial-damage and glass-chrome verification: latest run kept two decorated checker windows mapped while scripted multi-toplevel motion ran under a forced glass config, observed 423 partial frames, 340 multi-rect partial frames, 628 flips with at least two surfaces, 1,272 prepared blur ops / 1,271 blur runs, kept surface avg/max at 0.055/0.064 ms under the verifier's 4.000 ms gate, and validated 70 titlebar/glass capture rows with 61 changing rows for blur-edge coverage.
- [x] [Auto] Strengthen KMS cursor/primary-flip validation: the pointer-under-terminal-load verifier now requires accepted hardware-cursor fast-path moves while a primary flip is pending. Latest run accepted 103 pending-flip cursor moves, deferred zero cursor moves, kept 180/180 fast-path moves, and had zero pointer-triggered render loops.
- [x] [Auto] Refresh FP-11 cached-surface/RADV validation: latest surface-cache verifier run reported 521 surface draw-cache hits, 1 miss, zero transient-chrome blocks, and surface avg/max 0.012/0.013 ms; RADV validation logs in `.debug-logs/fp11-validation/20260611-181036` passed normal, validation-layer, and forced prepared-geometry Vulkan render-target slices at 21 cases / 157 assertions.
- [x] [Auto] Add and run FP-9 active 10-window snapshot-load verification: latest full verifier run reported stable `snapshot_ms` avg/max 0.092/0.096, stable `surface_ms` avg/max 0.042/0.046, 7,326 surface-cache hits / 10 misses, and 377.0 allocations/frame under the malloc-count gate; matched pre-FP-9 baseline was 0.114/0.120, 0.113/0.119, 0 hits / 6,963 misses, and 494.5 allocations/frame.
- [x] [Auto] Refresh FP-14 input/animation tests: Linux `RuntimeInputTests.cpp` passed 33 cases / 198 assertions under headless Weston with the known nested-autofocus `may_fail` case allowed, Linux `AnimationTests.cpp` passed 11 / 53, and macOS full `ctest` already passed 2/2 on 2026-06-11.
- [x] [Auto] Add and run Wayland popover pacing verification: latest `scripts/verify-wayland-popover-pacing.sh` passed under headless Weston GL, proving one immediate initial configure render, eight committed redraw requests, eight frame requests/dones, eight committed renders from frame callbacks, zero immediate committed redraw renders, and native popover Escape dismissal through the guarded backend path because Weston headless lacks virtual-keyboard support.
- [x] [Auto] Refresh manual validation helpers: `scripts/run-real-app-smoke.sh` now includes `lambda-editor` and prints the remaining cursor appearance/responsiveness, resize feel, stale-content, and text-after-scroll visual checks. Added guarded `scripts/verify-kms-vt-switch.sh` for real-TTY VT release/acquire validation with an explicit `LWM_VT_SWITCH_TARGET`; it was syntax-checked and its non-TTY guard was verified here.
- [ ] [Manual] Complete the remaining visual checks from the plan: manual cursor appearance, resize feel, representative app inspection, and real VT-switch validation requiring interactive input or a real TTY. Captured drag/move stale-pixel checks and repeatable real-evdev hardware input now pass automatically. Outside the sandbox the user is now in `wheel` and `/dev/input/event*` is visible with ACLs, but `/dev/uinput` is still `0600 root:root`, `sudo -n true` still requires a password, and this Codex process is not a TTY, so live uinput-backed/manual and VT-switch validation cannot run non-interactively here.
- [x] [Auto] Re-run macOS compile/focused tests for the latest Metal review fixes. Fixed the macOS-only ARC compile error in `MetalCanvas.mm` backdrop buffer pooling and the Metal glyph-atlas padding clear regression, then `cmake --build build -j"$(sysctl -n hw.ncpu)"`, focused Metal tests, focused SceneGraph tests, full `ctest`, the deferred atlas grow regression, and the glyph-padding regression all passed on 2026-06-11.
- [x] [Auto] Add repeatable macOS Metal/editor runtime validation: `scripts/verify-macos-metal-editor-perf.sh` builds `lambda-editor`, drives the normal `edit.paste` command with a generated unicode-heavy payload through `LAMBDA_EDITOR_AUTOTEST_PASTE_FILE`, parses `LAMBDA_DEBUG_PERF=2` detail rows for `CanvasDrawableWait`/frame-budget p99, and requires `atlasGrow` perf evidence by default. Linux-side verification passed: script syntax, non-Darwin guard, Linux `lambda-editor`/`lambda_tests` build, focused Editor/TextInput/EventQueue tests, and a headless-Weston smoke of the editor autotest paste path.
- [x] [Auto] Refresh Linux regression evidence after the macOS editor perf helper: full `ctest --test-dir build --output-on-failure -j"$(nproc)"` passed 2/2 under headless Weston, `scripts/verify-wayland-client-pacing.sh` passed with frameDone-to-present avg/max 1.582/6.187 ms under the 8 ms gate, and source/test search found no `vkQueueWaitIdle` references.
- [x] [Auto] Add and run remaining-gate audit helper: `scripts/audit-todo019-validation-gates.sh` reports input tools plus evdev/VT/macOS helpers are present, but KMS VT switching still needs a real TTY, `/dev/uinput` is still `crw------- root root`, noninteractive sudo still fails, live manual Wayland smoke is not currently attached to a compositor session, and macOS runtime validation still requires macOS.
- [ ] [Manual] Complete remaining macOS runtime/visual verification for FP-14/FP-16 and latest Metal fixes: run `scripts/verify-macos-metal-editor-perf.sh` on macOS and compare backdrop-blur visuals.
- [ ] [Auto] When all REV items, manual verification gaps, and macOS runtime checks are done, delete the plan document and this TODO item.

## TODO-020: Complete SVC-1 D-Bus capability details

- [x] [Auto] Add a Linux `lambda::dbus` backend using `sd-bus` with session/system/custom-address connections, sync method calls, signal subscription, simple property get/set, object export, signal emission, and event-loop fd pumping hooks.
- [x] [Auto] Add focused integration coverage for `Peer.Ping`, exported method calls, exported property get/set, and signal delivery against a private real bus when the local environment allows `dbus-daemon` to bind a socket.
- [x] [Auto] Add Unix-fd basic value support and focused fd round-trip coverage for fd-returning service APIs such as logind inhibitors.
- [x] [Auto] Cover object-path reply plumbing through logind `GetSessionByPID` fixture tests.
- [x] [Auto] Add the first richer D-Bus shapes needed by SVC-4 Settings: string arrays, RGB tuples, variant replies/signals, and the `a{sa{sv}}` namespaced variant dictionary.
- [x] [Auto] Add notification-service D-Bus helpers for empty `a{sv}` hints dictionaries and explicit skipping of currently ignored dictionaries.
- [x] [Auto] Add `Properties.GetAll`, `a{sv}` reply reading, and unique-name lookup for StatusNotifierWatcher initialization and path-only item registration.
- [x] [Auto] Add NetworkManager D-Bus value shapes: object-path arrays, byte arrays, byte scalars, and fixture coverage through the first NetworkManager client.
- [x] [Auto] Add BlueZ ObjectManager D-Bus shape support for managed-object dictionaries (`a{oa{sa{sv}}}`) and fixture coverage through the first BlueZ client.
- [x] [Auto] Add nested `a{sv}` variant-property support for MPRIS metadata and fixture coverage through the first MPRIS client.
- [ ] [Auto] Add async method calls and pending-call cancellation.
- [ ] [Auto] Add broader generic type support needed by remaining portals and services: arbitrary arrays beyond the service-specific shapes already in tree, arbitrary dictionaries, richer nested variants, and request option maps.
- [ ] [Auto] Add object introspection and first-class `PropertiesChanged` helpers.
- [x] [Auto] Add deterministic fixture tests for the first UPower client that consumes `lambda::dbus`.
- [x] [Auto] Add deterministic fixture tests for the first NetworkManager client that consumes `lambda::dbus`.
- [x] [Auto] Add deterministic fixture tests for the first BlueZ client that consumes `lambda::dbus`.
- [x] [Auto] Add deterministic fixture tests for the first MPRIS client that consumes `lambda::dbus`.
- [ ] [Auto] Add deterministic fixture tests for the remaining first service clients that consume `lambda::dbus` (udisks2).
- [ ] [Auto + Manual] Wire D-Bus fd pumping into the compositor/service runtime paths that will host SVC-2/SVC-3/SVC-6+ work, and validate against the real session and system bus outside sandbox restrictions.

## TODO-021: Complete SVC-2 libseat seat/session integration

- [x] [Auto] Add `libseat` build wiring for the KMS platform and `lambda-window-manager`.
- [x] [Auto] Open a libseat seat at KMS startup when available, poll/dispatch the libseat fd, and keep direct-open fallback for environments where libseat is unavailable or cannot open a specific device.
- [x] [Auto] Open DRM cards and libinput evdev devices through `libseat_open_device`, track returned device IDs, and close them through `libseat_close_device`.
- [x] [Auto] Route `enable_seat`/`disable_seat` callbacks through the existing DRM-master release/reacquire and libinput suspend/resume path.
- [ ] [Auto + Manual] Reopen all seat-managed DRM/input devices on `enable_seat` instead of relying on still-valid fds.
- [ ] [Auto] Implement `libseat_switch_session` support for explicit VT/session switching.
- [ ] [Manual] Validate `lambda-window-manager` starts as an unprivileged user inside a logind session with no manual `/dev/dri` or `/dev/input` permissions.
- [ ] [Manual] Validate Ctrl-Alt-F<n> VT switching away and back releases/reacquires the GPU and resumes input without corruption.
- [ ] [Auto + Manual] Decide when to remove or hard-disable direct device-open fallback after target-hardware libseat validation passes.

## TODO-022: Complete SVC-3 logind power/session integration

- [x] [Auto] Add a basic `lambda::system::LogindClient` on top of `lambda::dbus` for `Suspend`, `Hibernate`, `PowerOff`, `Reboot`, fd-based `Inhibit`, `PrepareForSleep`, and session `Lock`/`Unlock` signals.
- [x] [Auto] Add deterministic fake-bus coverage for logind power calls, inhibitor fd plumbing, sleep signal delivery, and session lock/unlock signal delivery.
- [x] [Auto] Discover the active logind session path through `GetSessionByPID` instead of requiring callers to provide it manually, and add current-session lock/unlock watcher helpers.
- [ ] [Auto + Manual] Wire logind session `Lock`/`Unlock` and `PrepareForSleep` into the lock app / Shell lock flow, including lock-before-sleep behavior.
- [ ] [Auto + Manual] Wire `Suspend`, `Hibernate`, `PowerOff`, and `Reboot` into the Shell power/session menu.
- [ ] [Auto + Manual] Hold delay inhibitors for `handle-power-key`, `handle-lid-switch`, and `handle-suspend-key`, then release them at the correct point in the lock/suspend flow.
- [ ] [Auto + Manual] Honor logind `IdleAction` together with WM-13 idle-notify and WM-14 DPMS behavior.
- [ ] [Manual] Validate against the real system bus: `loginctl lock-session` locks, suspend/resume returns to the lock screen, lid close suspends after locking, and the power key opens the Shell power menu.

## TODO-023: Complete SVC-4 xdg-desktop-portal backend

- [x] [Auto] Add a Linux `lambda-portal` session-bus service target with D-Bus service metadata and `lambda.portal` selector metadata for `org.freedesktop.impl.portal.desktop.lambda`.
- [x] [Auto] Export the `org.freedesktop.impl.portal.Settings` backend interface with `Read`, `ReadAll`, `SettingChanged`, and `version`.
- [x] [Auto] Expose `org.freedesktop.appearance` values for `color-scheme`, `accent-color`, `contrast`, and `reduced-motion`; for now these default from `LAMBDA_PORTAL_COLOR_SCHEME`, `LAMBDA_PORTAL_ACCENT_COLOR`, `LAMBDA_PORTAL_HIGH_CONTRAST`, and `LAMBDA_PORTAL_REDUCED_MOTION`.
- [x] [Auto] Add deterministic private-bus coverage for portal Settings methods, property, namespaced dictionary response, RGB tuple variant, and `SettingChanged` signal.
- [x] [Auto] Smoke the built `lambda-portal` process on a private session bus and verify a Settings `Read` call through `gdbus`.
- [ ] [Auto + Manual] Wire Settings/Shell-owned appearance preferences into `lambda-portal` instead of relying on environment variables.
- [ ] [Manual] Install or stage `lambda-portal` with `xdg-desktop-portal`, set `XDG_CURRENT_DESKTOP=Lambda`, and verify a real GTK/Qt app reads the color-scheme/accent through the frontend portal.
- [ ] [Auto + Manual] Implement the FileChooser portal backend for Open/Save using a Lambda file-chooser surface backed by `lambda-files` components.
- [ ] [Auto + Manual] Implement OpenURI, Inhibit, Account, and Notification portal backends; route portal Notification requests through the SVC-6 daemon.
- [ ] [Auto + Manual] Add real frontend request/response tests for xdg-desktop-portal request objects, handles, cancellation, and response codes.

## TODO-024: Complete SVC-6 notifications daemon and Shell UI

- [x] [Auto] Add a Linux `lambda-notifications` session-bus service target with D-Bus activation metadata for `org.freedesktop.Notifications`.
- [x] [Auto] Export the Freedesktop notifications interface with `Notify`, `CloseNotification`, `GetCapabilities`, `GetServerInformation`, `NotificationClosed`, and `ActionInvoked`.
- [x] [Auto] Keep basic in-memory notification history with replacement support, action parsing, DND state plumbing, and close/action signal helpers.
- [x] [Auto] Add deterministic private-bus coverage for capabilities, server information, notification creation/replacement, close signals, and action signals.
- [x] [Auto] Smoke the built `lambda-notifications` process on a private session bus and verify a real `Notify` call through `gdbus`.
- [ ] [Auto + Manual] Wire `lambda-notifications` into Shell's live notification banner and notification-center UI.
- [ ] [Auto + Manual] Route action button clicks from Shell UI back to `ActionInvoked`, close/dismiss events to `NotificationClosed`, and close all visible UI when notifications are closed by D-Bus.
- [ ] [Auto + Manual] Enforce Shell notification config, including enabled/disabled, DND, banner timeout, history limit, and preview visibility.
- [ ] [Auto + Manual] Implement timeout expiry, grouping, persistence policy, and clear-all behavior against the service history.
- [ ] [Auto] Parse the common notification hints that affect presentation, including urgency, category, desktop-entry, image/icon data, transient, and sound suppression.
- [ ] [Auto + Manual] Route the SVC-4 portal Notification backend through this service.
- [ ] [Manual] Validate `notify-send "x"` shows a Shell banner, actions invoke, DND suppresses banners, and history shows past notifications.

## TODO-025: Complete SVC-7 StatusNotifierWatcher and tray host

- [x] [Auto] Add a Linux `lambda-status-notifier-watcher` session-bus service target with D-Bus activation metadata for `org.kde.StatusNotifierWatcher`.
- [x] [Auto] Export the watcher interface with `RegisterStatusNotifierItem`, `RegisterStatusNotifierHost`, `RegisteredStatusNotifierItems`, `IsStatusNotifierHostRegistered`, `ProtocolVersion`, `StatusNotifierItemRegistered`, `StatusNotifierItemUnregistered`, and `StatusNotifierHostRegistered`.
- [x] [Auto] Support path-only item registration by using the caller's unique bus name, and remove registered items/hosts when their bus name loses ownership.
- [x] [Auto] Add deterministic private-bus coverage for host/item registration, properties, `Properties.GetAll`, registration signals, owner-loss removal, and path-only item registration.
- [x] [Auto] Smoke the built `lambda-status-notifier-watcher` process on a private session bus and verify a `ProtocolVersion` property query through `gdbus`.
- [ ] [Auto + Manual] Wire Shell's tray host UI to the watcher and render registered status item icons in the bar.
- [ ] [Auto] Read StatusNotifierItem properties for category, title, status, icon names, icon pixmaps, overlay/attention icons, tooltip, menu object path, and item updates.
- [ ] [Auto + Manual] Implement `com.canonical.dbusmenu` menu hosting and item `Activate`, `ContextMenu`, `SecondaryActivate`, and `Scroll` actions.
- [ ] [Auto + Manual] Handle item property/signal changes live without freezing Shell when a tray item misbehaves or disappears.
- [ ] [Manual] Validate Steam/Discord/Telegram/nm-applet-style tray icons appear, update, activate, and show menus.

## TODO-026: Complete SVC-11 UPower power-status integration

- [x] [Auto] Add a basic `lambda::system::UPowerClient` on top of `lambda::dbus` for `GetDisplayDevice`, display-device battery properties, manager `OnBattery`, and display-device `PropertiesChanged` signals.
- [x] [Auto] Add deterministic fake-bus coverage for display-device reads, battery status formatting, and change-signal delivery.
- [x] [Auto] Make Shell battery status prefer UPower on the real `/sys` path and preserve the existing sysfs fallback when UPower is unavailable or tests use a fixture sysroot.
- [ ] [Auto + Manual] Wire UPower `PropertiesChanged`, `DeviceAdded`, and `DeviceRemoved` into Shell so battery/AC status updates without waiting for the polling timer.
- [ ] [Auto] Expose richer UPower state in Shell status models: charging/discharging/full/empty, on-AC/on-battery, warning level, time-to-empty/full, and icon-name hints.
- [ ] [Auto + Manual] Add quick-settings/power UI affordances once policy controls exist, while keeping read-only status truthful until then.
- [ ] [Manual] Validate against the real system bus: AC plug/unplug and battery percentage changes update the Shell docklet, and systems without batteries still report unavailable truthfully.

## TODO-027: Complete SVC-9 NetworkManager connectivity integration

- [x] [Auto] Add a basic `lambda::system::NetworkManagerClient` on top of `lambda::dbus` for `GetDevices`, manager `State`/`NetworkingEnabled`/`WirelessEnabled`/`WirelessHardwareEnabled`, device `Interface`/`DeviceType`/`State`, wireless `ActiveAccessPoint`, access-point `Ssid`/`Strength`, and a `WirelessEnabled` setter.
- [x] [Auto] Add deterministic fake-bus coverage for device enumeration, connected Wi-Fi SSID/signal formatting, connecting/off mapping, and Wi-Fi toggle property writes.
- [x] [Auto] Make Shell network/Wi-Fi status prefer NetworkManager on the real `/sys` path and preserve the existing sysfs fallback when NetworkManager is unavailable or tests use a fixture sysroot.
- [ ] [Auto + Manual] Wire NetworkManager state, device, and access-point signals into Shell so network/Wi-Fi status updates without waiting for the polling timer.
- [ ] [Auto] Enumerate visible access points, saved connections, active connections, metered state, connectivity state, and VPN state for Shell quick settings and Settings.
- [ ] [Auto + Manual] Implement Wi-Fi connect/disconnect flows, including a secrets path for password-protected networks.
- [ ] [Auto + Manual] Build the Settings network page for saved networks, VPNs, and detailed adapter/IP state.
- [ ] [Manual] Validate against the real system bus: Ethernet, Wi-Fi connect/disconnect, Wi-Fi enable/disable, captive/limited connectivity, and no-NetworkManager fallback all report truthfully in the Shell docklet.

## TODO-028: Complete SVC-10 BlueZ Bluetooth integration

- [x] [Auto] Add a basic `lambda::system::BlueZClient` on top of `lambda::dbus` for ObjectManager `GetManagedObjects`, adapter `Address`/`Alias`/`Powered`/`Discovering`, device `Address`/`Alias`/`Name`/`Adapter`/`Paired`/`Connected`, and an adapter `Powered` setter.
- [x] [Auto] Add deterministic fake-bus coverage for adapter/device enumeration, connected-device status formatting, off/on/unavailable mapping, adapter power writes, and unsupported ObjectManager property skipping.
- [x] [Auto] Make Shell Bluetooth status prefer BlueZ on the real `/sys` path and preserve the existing rfkill/sysfs fallback when BlueZ is unavailable or tests use a fixture sysroot.
- [ ] [Auto + Manual] Wire BlueZ ObjectManager, adapter, and device signals into Shell so Bluetooth status updates without waiting for the polling timer.
- [ ] [Auto] Enumerate adapters and devices with richer state: discoverable/discovering, trusted/blocked, paired/unpaired, battery where exposed, icon/class/category, and connection errors.
- [ ] [Auto + Manual] Implement adapter power toggle, discovery, pairing-agent, pair/unpair, trust/untrust, connect/disconnect, and forget-device flows.
- [ ] [Auto + Manual] Build the Settings Bluetooth page for pairing, device management, and adapter details.
- [ ] [Manual] Validate against the real system bus: adapter power on/off, device pair/connect/disconnect, no-adapter fallback, and Bluetooth controller removal all report truthfully in the Shell docklet.

## TODO-029: Complete SVC-12 MPRIS media integration

- [x] [Auto] Add a basic `lambda::system::MPRISClient` on top of `lambda::dbus` for session-bus player discovery, root `Identity`/`DesktopEntry`, player `PlaybackStatus`/`Metadata`/`Volume`/`Position`/capability properties, transport methods, and volume writes.
- [x] [Auto] Add deterministic fake-bus coverage for player discovery, metadata parsing, status formatting, `PlayPause`, `Next`, and volume property writes.
- [ ] [Auto + Manual] Wire MPRIS `PropertiesChanged`, `Seeked`, and name-owner changes into Shell so now-playing status updates live.
- [ ] [Auto + Manual] Add Shell now-playing UI and media-key routing for play/pause, next, previous, stop, seek, and volume where supported.
- [ ] [Auto] Implement player selection policy for multiple players, including active/playing precedence, stale-player pruning, and per-player capability gating.
- [ ] [Auto] Add richer metadata handling: art URL/image cache, track length/position progress, album/artist lists, desktop-entry icon lookup, and TrackList support where present.
- [ ] [Manual] Validate against real MPRIS players such as browser media, VLC, Spotify, or mpv: player appears, metadata updates, media keys control the expected player, and unsupported controls stay disabled.
