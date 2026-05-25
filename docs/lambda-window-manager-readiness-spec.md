# Lambda Window Manager readiness spec

**Date:** 2026-05-25
**Status:** Draft
**Milestone order:** Window Manager first, then Shell, Settings, Files, Terminal, and the remaining desktop pieces.
**Scope:** `lambda-window-manager` and the Flux platform/compositor code it directly depends on.

## Summary

This milestone hardens `lambda-window-manager` until it is a reliable base for the rest of the Lambda desktop work. The target is not "feature complete desktop environment"; the target is "the Window Manager can be manually launched from a TTY, can host the shell and normal Wayland apps for long dogfooding sessions, and its core rendering/window-management behavior is boring."

Session startup/shutdown automation is intentionally excluded. For now, sessions are started and ended manually. New log collection, log rotation, crash-log work, and trace infrastructure are also excluded. Existing diagnostic tools may still be used during development, but this milestone does not require building new logging systems.

## Additional Window Manager work identified

Beyond the short assessment item, these Window Manager areas still need explicit readiness work:

- Idle and interactive CPU sanity for real glass/transparent workloads.
- Output and scale correctness for the current single-selected-output model.
- Stable window geometry invariants for move, resize, maximize, restore, snap, minimize, and shell focus requests.
- Shell-facing focus/raise/restore primitives, especially for minimized windows.
- Cursor theme behavior and fallback behavior.
- Keyboard layout, repeat, keymap, and shortcut delivery behavior. The current Linux keyboard path mostly relies on xkb defaults, which is not good enough as an unstated daily-driver assumption.
- Rendering stability for glass backgrounds, system titlebars, integrated titlebars, shadows, borders, snap previews, and shell panels under windows.
- Resize/snap visual stability, including exposed newly-sized regions during animations.
- Screenshot capture, options, and UI, because screenshots are a core compositor/window-manager feature.
- Clean Wayland disconnect behavior for Flux clients when the Window Manager exits or crashes.
- Real-app Wayland validation beyond the in-tree demos.
- Config contract and hot-reload/restart boundaries, because Settings will edit these values later.
- Protocol honesty and regression cleanup where current behavior is partially implemented or config-gated.

These are included below. Session lifecycle and new logging are not.

## Goals

1. Make manually launched `lambda-window-manager` dependable for daily dogfooding.
2. Keep the current single-output architecture but make output selection, scale, and external-display selection trustworthy.
3. Make window geometry transitions visually stable.
4. Make server-side, integrated, and client-side titlebar modes behave predictably.
5. Make shell panels and normal windows coexist without flicker, stale damage, or sampling artifacts.
6. Make cursor themes configurable and reliable enough before Settings exposes them.
7. Make keyboard layout and repeat behavior explicit instead of depending on hidden host defaults.
8. Make screenshots, screenshot options, and screenshot UI reliable enough for daily use and later visual regression tests.
9. Make Flux Wayland clients fail closed when the Window Manager exits or crashes instead of spinning in the background.
10. Establish a clear manual smoke checklist for this phase.

## Non-goals

- No session manager, display manager, greeter, PAM, login, lock screen, logout UI, reboot UI, or auto-restart loop.
- No new log pipeline, log viewer, crash-log storage, trace viewer, or log rotation.
- No full multi-output desktop layout. Selecting a single output is in scope; spanning multiple outputs at once is not.
- No XWayland.
- No workspaces, overview, tiling mode, virtual desktops, or window grouping.
- No touch, tablet, gesture shell behavior, or input method framework.
- No portal layer for screencast, file chooser, or permissions. Screenshot capture and trusted screenshot UI are in scope, but untrusted app screenshot portals are not.
- No Shell UI redesign. Shell behavior is in scope only where the Window Manager needs correct primitives.
- No Settings UI. The Window Manager config contract must be ready, but the Settings app consumes it later.

## Assumptions

- The developer manually starts `lambda-window-manager` from a real TTY.
- The developer manually starts `lambda-shell` from another TTY or SSH shell.
- `/dev/input/event*` ACL setup remains manual for this milestone.
- `XDG_RUNTIME_DIR` is available from the TTY environment.
- The first target machine remains the current Linux/KMS development machine.
- Mature external Wayland apps can be used for validation.
- Pure Wayland remains the compatibility policy.

## Readiness definition

The Window Manager is ready for the next milestone when all of these are true:

- It can run idle with shell, Files, Settings, Terminal, and at least one external app open without continuous unexpected rendering.
- Moving, resizing, snapping, maximizing, restoring, and minimizing windows does not expose blank regions, stale buffers, or flickering chrome.
- Glass windows, shell panels, and server-side titlebars use consistent material behavior.
- Shell panels do not flicker when windows animate near or under them.
- Cursor theme selection works from config and falls back predictably.
- Keyboard layout, variant, options, and repeat behavior are either configurable through the Window Manager config or documented as intentionally fixed defaults. The preferred outcome for this milestone is configurability.
- `--list-outputs`, `--output`, `LAMBDA_WINDOW_MANAGER_OUTPUT`, config `output`, and per-output scale overrides are trustworthy for the single selected output.
- Screenshots save correctly and match the visible frame.
- Screenshot options and compositor-drawn screenshot UI support full output, active window, and region capture without relying on Shell.
- Normal clients receive frame callbacks and presentation feedback at the right time under idle, animation, resize, and real app workloads.
- Flux Wayland clients such as `lambda-terminal`, `lambda-settings`, and `lambda-files` exit cleanly when the Window Manager dies, matching `lambda-shell`'s current behavior through IPC disconnect.
- The Window Manager exposes enough stable focus/restore behavior for the Shell milestone to build on.
- The manual validation checklist is short, repeatable, and documented.

## Baseline audit findings

**Date:** 2026-05-25
**Method:** Static audit of the current source tree against this spec. This pass did not run a live TTY hardware session.

These are the concrete findings to resolve or validate before broad refactors.

1. Flux Wayland clients do not fail closed when the Window Manager dies.

   `src/Platform/Linux/WaylandWindow.cpp` polls the Wayland display fd for `POLLIN` only, ignores fatal `POLLHUP`/`POLLERR`, does not check `wl_display_flush`, `wl_display_read_events`, or `wl_display_dispatch_pending` failures, and does not use `wl_display_get_error()` to terminate the app. `src/UI/Application.cpp` then keeps polling each unique platform event fd. This matches the observed behavior where `lambda-terminal`, `lambda-settings`, and `lambda-files` spin after compositor death.

   Status: implemented and manually verified on 2026-05-25.

2. Minimize does not actually mark a toplevel minimized.

   `minimizeToplevel()` in `src/Compositor/Window/FocusStack.cpp` lowers the surface and changes focus, but never sets `surface->minimized = true`. Snapshot generation already skips minimized surfaces, and `focusSurface()` already clears `minimized`, so the intended state model exists but the minimize command is incomplete.

3. Screenshot support is still full-output save only.

   `requestScreenshot()` stores a boolean, and `CompositorRuntime` captures the next full rendered frame and writes a PNG through `Screenshot.cpp`. There is no active-window mode, region mode, compositor-drawn selection UI, cancel path, clipboard path, cursor inclusion policy, or documented shadow/border policy for active-window capture.

4. Keyboard configuration is absent.

   `CompositorConfig` has no keyboard layout/model/variant/options/repeat fields. The compositor server creates an xkb keymap from empty `xkb_rule_names`, KMS input uses `XkbState::createDefaultKeymap()`, and `wl_keyboard.repeat_info` is hard-coded to `25, 600`.

5. Core surface protocol state is advertised but partly ignored.

   `wl_surface.damage`, `damage_buffer`, `set_opaque_region`, `set_input_region`, `set_buffer_transform`, and `offset` are no-ops. This may be acceptable for a narrow client set only if explicitly documented, but protocol readiness requires either implementation or a clear limitation for each ignored request.

6. Several xdg-shell toplevel requests are no-ops.

   `set_window_geometry`, `set_parent`, `show_window_menu`, `set_min_size`, `set_max_size`, `set_fullscreen`, and `unset_fullscreen` are currently ignored. Real GTK/Qt/browser validation should decide which of these must be implemented for daily-driver use and which remain documented limitations.

7. Output and scale support exists, but selector behavior needs test coverage.

   Output listing, selector parsing, per-output scale, `wl_output`, xdg-output, and fractional-scale paths are present. The missing piece is deterministic tests for selector parsing and restart/hot-reload boundaries, especially invalid selectors and `secondary`.

8. Idle and frame scheduling have the right structure but still need live validation.

   The runtime has `contentSerial`, `atomicFrameDirty`, frame callback dispatch, idle skips, render-ahead, and CPU trace instrumentation. Static review did not show a single obvious redraw loop, but the acceptance criteria require a live idle run with shell, Files, Settings, Terminal, and at least one external app.

9. Geometry tests cover pure geometry, not the full Window Manager state machine.

   `CompositorWindowGeometryTests` covers snap, popup, restore-drag, and resize geometry helpers. There are no focused tests for `minimizeToplevel`, Shell IPC focus/restore, focus order after close/minimize, or minimized-window snapshot behavior.

10. Real-app validation docs are incomplete.

    `docs/compositor-testing.md` covers compositor demos and `foot`, but the readiness spec also requires Firefox or another browser, one GTK app, and one Qt app where available. The smoke doc should be expanded when those checks are run.

11. Config contract is not ready for Settings.

    Existing config parsing covers background, wallpaper, output, scale, cursor, animations, hardware cursor, idle blanking, chrome, keybindings, and popup grabs. It does not yet cover keyboard config, screenshot options, or a canonical hot-reload/applies-next-window/restart-required matrix for every key.

12. Existing compositor docs contain stale sections, but no whole document is clearly obsolete.

    `docs/roadmap.md` already flags stale sections in `docs/compositor.md`. Treat that as a documentation alignment task: update or retire stale sections in place when this milestone lands. Do not delete architecture docs unless the readiness index fully replaces their current purpose.

## Workstreams

### WM-1: Baseline idle and frame scheduling

Problem:

Recent work reduced idle CPU, but readiness needs a stable baseline. Idle should not keep rendering merely because glass, shell panels, transparent clients, or cached shadows exist.

Scope:

- Audit all paths that wake the render loop.
- Ensure content serials change only when visible output changes.
- Ensure shell panels do not force redraws while unchanged.
- Ensure transparent/glass windows do not defeat frame caching while idle.
- Ensure cursor movement is the only expected high-frequency work while the pointer moves.
- Ensure frame callbacks are not sent in a loop when clients are idle.
- Ensure Flux Wayland clients treat compositor disconnect as fatal and request app shutdown exactly once.

Acceptance:

- With `lambda-shell`, `lambda-files`, `lambda-settings`, and `lambda-terminal` open and idle, the Window Manager stays near idle on the target machine.
- Leaving the system alone for several minutes does not produce visible changes, flicker, or periodic redraw artifacts.
- Opening and closing one extra app does not leave the compositor in a busy state.
- Client frame callbacks stop when clients stop requesting frames.
- Killing or crashing `lambda-window-manager` does not leave `lambda-terminal`, `lambda-settings`, or `lambda-files` spinning at high CPU.

Implementation notes:

- Check `contentSerial_` increments in geometry, shell zones, config reload, animation, pointer, and surface commit paths.
- Check `atomicFrameDirty` and render-ahead behavior in the KMS presenter path.
- Check whether layer-shell surfaces with background effects invalidate blur caches while static.
- In the Flux Linux Wayland backend, handle fatal `POLLHUP`/`POLLERR`, failed `wl_display_flush`, failed `wl_display_read_events`, and `wl_display_get_error()` by marking the shared display connection dead and calling `Application::quit()`.
- Do not solve this per app. `lambda-shell` already exits through Window Manager IPC disconnect, but normal Flux apps need shared Wayland-client disconnect handling.
- Keep any measurement tooling local to validation; do not add new logging features for this milestone.

### WM-2: Resize and snap visual stability

Problem:

Resize and snap are core daily interactions. The previous fixes improved artifacts, but readiness requires that the user can rapidly resize and snap windows without blank exposed regions, delayed chrome, panel blinking, or stale content.

Scope:

- Interactive pointer resize.
- Keyboard snap left/right/maximize/restore.
- Drag-to-edge half snap.
- Drag-to-corner quarter snap.
- Top-edge maximize preview.
- Maximize and unmaximize geometry animation.
- Snap preview rendering.
- Window growth under top panel and over dock area.

Acceptance:

- During resize, newly exposed window regions are filled by the correct current surface/background path.
- During snap/maximize animations, position and size animate together.
- Unmaximize does not scale first and move later.
- Window chrome does not lag behind the window content.
- Shell topbar and dock do not blink when a window grows under or near them.
- The snap preview does not sample shadows as part of the blur.
- Animations disabled in config produces immediate final geometry with no intermediate artifacts.

Implementation notes:

- Keep configure pacing coherent with client commits.
- Do not stretch stale client buffers as a substitute for real resized content unless explicitly choosing a temporary fallback for a single frame.
- Preserve the existing desire for intermediate resizes; do not collapse resize animation into final-only resize.
- Make sure `windowClipTop`, `windowClipBottom`, `shadowClipTop`, and `shadowClipBottom` are applied consistently to animated and non-animated windows.

### WM-3: Window geometry and state invariants

Problem:

The Window Manager needs strict geometry rules before Shell and Settings build more behavior on top. Shell should later be able to request focus, restore, and launch without working around Window Manager edge cases.

Scope:

- Initial placement.
- Focus and raise.
- Focus cycling.
- Close/minimize/maximize/restore shortcuts.
- Pointer move.
- Pointer resize from edges and corners.
- Double-click titlebar maximize/restore.
- Restore from snapped/maximized state when dragging titlebar.
- Minimized window restoration via Window Manager API.
- Bounds clamping against the selected logical output and reserved topbar area.

Acceptance:

- Every managed toplevel has positive frame width/height.
- No managed toplevel can be permanently moved outside the selected output.
- Snapped and maximized windows respect topbar reserved area.
- Restore returns to a sensible previous geometry.
- Minimized windows are absent from normal hit testing and presentation but remain focusable/restorable through Shell IPC.
- `focusShellApp` and `focusShellWindow` restore minimized windows before focusing them.
- Closing the focused window activates a sensible previous toplevel.
- Focus state in shell snapshots matches the actual keyboard-focused toplevel.

Implementation notes:

- Add deterministic tests around focus/restore/minimize invariants if not already covered.
- Treat shell IPC focus requests as Window Manager commands, not as shell-side behavior.
- Keep titlebar modes independent of focus rules.

### WM-4: Chrome, decorations, glass, and shadows

Problem:

Visual consistency has been one of the highest-friction areas. This milestone should freeze the Window Manager-side material model enough that Settings can later expose it without changing semantics.

Scope:

- Server-side titlebars.
- Integrated titlebars with cutouts.
- Client-side/no titlebar cases.
- Glass window backgrounds.
- Solid/opaque window backgrounds.
- Shell panels using background effect.
- Window border and corner radius.
- Focused/unfocused shadows.
- Snap preview material.

Acceptance:

- System titlebar and content area use the same glass material when a full-window glass background is active.
- Integrated-titlebar windows and system-titlebar windows render equivalent default glass.
- Solid/opaque windows do not pay the full glass blur cost.
- Shadows respect configured corner radius.
- Shadows do not participate in blur sampling.
- Borders wrap the whole window frame, including system titlebar area.
- The separator between system titlebar and content is stable and does not blink frame-to-frame.
- Window background choices remain client-owned through `Window::setBackground()`, with compositor default glass only applying according to documented policy.
- `chrome.glass` names and shapes remain aligned with `GlassEffectOptions`.

Implementation notes:

- Avoid new visual constants outside the shared chrome config/material path.
- Preserve default `WindowBackground::glassEffect()` as compositor/default material.
- Ensure titlebar-only material and full-window material do not diverge accidentally.
- Validate with `lambda-settings` for system titlebar and `lambda-files` for integrated titlebar.

### WM-5: Output selection and scale

Problem:

Full multi-output layout is not in scope, but selecting a single output must be reliable. External display testing depends on this.

Scope:

- `--list-outputs`.
- `--output NAME`.
- `--output INDEX`.
- `--output primary`.
- `--output secondary`.
- `LAMBDA_WINDOW_MANAGER_OUTPUT`.
- Config `output`.
- Fallback `scale`.
- Per-output `[outputs."CONNECTOR"].scale`.
- `wl_output`, `xdg-output`, and fractional-scale announcements.

Acceptance:

- Output listing shows every connected KMS connector with stable names, sizes, and refresh rates.
- Explicit output selection picks the requested connector or fails clearly.
- Selecting `secondary` works when at least two outputs are connected.
- Invalid output selector does not silently choose the wrong output.
- Logical output size equals physical size divided by selected scale.
- Fractional-scale clients receive the configured scale.
- Integer-scale clients receive a sane `wl_output` scale fallback.
- Shell topbar work area uses the selected logical output size.
- Window initial placement, snap, maximize, and pointer clamp use logical output coordinates.

Implementation notes:

- Output hotplug and multi-output layout are deferred.
- Changing `output` while running may continue requiring restart.
- Scale hot reload should update clients only if the existing architecture already supports it safely; otherwise document restart requirement.

### WM-6: Input, keyboard, and cursor readiness

Problem:

Input must be predictable before daily dogfooding. Cursor themes already work when a theme exists, but the config behavior and fallbacks should be explicit and reliable. Keyboard behavior also needs to stop depending on implicit xkb defaults.

Scope:

- Manual input ACL expectations.
- Pointer motion and absolute position.
- Pointer buttons.
- Scroll axes.
- Keyboard focus.
- Global shortcuts.
- Client keyboard delivery.
- xkb keymap creation.
- Keyboard layout, model, variant, and options.
- Keyboard repeat rate and delay.
- Cursor theme lookup.
- Cursor size.
- Client-provided cursor surfaces.
- Cursor-shape protocol.
- Pointer constraints and relative pointer smoke.

Acceptance:

- If input devices are readable, pointer and keyboard work immediately after launch.
- If input devices are not readable, failure is visible enough for the user to diagnose without a session manager.
- `Ctrl+C` goes to focused terminal clients, not to the Window Manager.
- `Ctrl+Alt+Backspace` terminates the Window Manager.
- Super shortcuts are captured only for configured Window Manager actions.
- Text input reaches focused Flux apps and mature Wayland apps.
- Configured keyboard layout, variant, and options are reflected in text input and shortcut resolution.
- Keyboard repeat works for client text input and does not repeat one-shot Window Manager shortcuts unless explicitly intended.
- Cursor theme config selects the requested Xcursor theme when installed.
- Cursor size config changes compositor-owned cursor size.
- Missing cursor theme falls back to environment/system/default cursor without invisible cursor.
- Client cursor surfaces still work.
- Cursor shape protocol changes compositor-owned cursor shape.

Implementation notes:

- Current keymap creation uses xkb defaults. Add explicit config support unless there is a strong reason to defer it after this milestone.
- Preferred config shape:

  ```toml
  [input.keyboard]
  layout = "us"
  variant = ""
  model = ""
  options = ""
  repeat_rate = 25
  repeat_delay_ms = 600
  ```

- Empty keyboard fields should mean xkb/system default for that field.
- Invalid keymap configuration should fall back to a safe default and report a clear startup/config error without crashing.
- Repeat configuration should be clamped to sensible ranges.
- Input methods remain out of scope.
- Touch remains out of scope and should not be advertised as supported until implemented.

### WM-7: Screenshot readiness

Problem:

Screenshot capture is now built into the Window Manager. The full screenshot feature should grow into a complete trusted screenshot workflow owned by the compositor/window manager. The UI can be drawn directly by the compositor, the same way snap indicators, selection affordances, and server-side chrome are already compositor-drawn.

Scope:

- `PrintScreen`.
- `SysRq`.
- `Super+Shift+3`.
- `Super+Shift+4` or equivalent for region capture.
- Active-window capture shortcut.
- Full selected-output capture.
- Active window capture.
- Region capture.
- Compositor-drawn region selection overlay.
- Compositor-drawn mode/options overlay.
- Output selection for the current selected-output model and future multi-output model.
- Cursor inclusion policy.
- Shadow/window-border inclusion policy for window capture.
- Copy-to-clipboard option.
- Save-to-file option.
- PNG write path.
- Unique filename generation.
- Screenshot content correctness.
- Error and cancellation behavior.

Acceptance:

- Screenshot shortcut saves one PNG under `~/Pictures/Screenshots`.
- Screenshot UI is owned by `lambda-window-manager` and does not require `lambda-shell`.
- The compositor can draw a region-selection overlay above all normal windows and shell layers.
- Region selection can be cancelled without saving a file or disturbing focus.
- Full-output capture captures the selected logical output.
- Active-window capture captures the focused toplevel, including server-side chrome, border, shadow policy, and glass result according to documented rules.
- Region capture captures the selected logical region exactly in output coordinates.
- The saved PNG matches the visible selected output at the time of capture.
- The screenshot includes shell panels, normal windows, cursor policy as documented, glass effects, shadows, and wallpaper/background as expected.
- Cursor inclusion is configurable or at least explicitly documented for each mode.
- User can choose save-to-file versus copy-to-clipboard for at least the common full/region flows, if clipboard support is available.
- Multiple screenshots do not overwrite each other.
- Screenshot failure does not crash the Window Manager.
- Screenshot request does not leave a stale frame or force future redraw loops.
- Screenshot overlays do not participate in blur sampling for the captured content unless deliberately capturing the overlay.
- Screenshot UI has keyboard-only paths: confirm, cancel, and mode selection.

Implementation notes:

- If cursor inclusion is currently undefined, define and document it.
- Keep screenshot UI compositor-owned because the Window Manager owns pixels, output geometry, surface stacking, and trusted capture timing.
- The overlay should reuse existing compositor drawing primitives used for snap previews/window chrome where possible.
- Region selection should operate in logical coordinates and convert to framebuffer coordinates at capture time.
- Clipboard integration can use the existing data-device path if available; if not, copy-to-clipboard should be marked unavailable rather than silently ignored.
- Use screenshots as a future visual-regression base, but do not require building the full regression harness in this milestone.

### WM-8: Wayland protocol validation

Problem:

The compositor exposes many protocols. Readiness requires confidence that advertised globals are not misleading and that common clients can use them.

Scope:

- Core surfaces.
- SHM.
- linux-dmabuf.
- xdg-shell.
- xdg-decoration.
- xdg-output.
- fractional-scale.
- viewporter.
- layer-shell.
- presentation-time.
- cursor-shape.
- relative-pointer.
- pointer-constraints.
- idle-inhibit.
- primary-selection.
- data-device clipboard.
- drag and drop.
- xdg-activation.
- popups.
- background-effect.
- cutouts.

Acceptance:

- Every advertised global has at least one smoke path or explicit documented limitation.
- In-tree protocol demos still pass the visual checks in `docs/compositor-testing.md`.
- Real apps can open menus/popups without freezing the Window Manager.
- Clipboard copy/paste works between at least two mature Wayland clients.
- Primary selection works where clients support it.
- Drag and drop demo still works.
- Presentation-time feedback demo reports presented feedback.
- Layer-shell shell surfaces reserve and overlay correctly.
- Background effect protocol remains the only path for client-requested blur/glass.
- Cutouts continue to work for integrated titlebar controls.

Implementation notes:

- Popup grabs are currently config-gated. This milestone should decide whether they are ready to enable by default or remain explicitly deferred.
- Touch/tablet protocols should remain absent or clearly non-advertised if not implemented.
- Do not add compatibility protocols only as stubs.

### WM-9: Config contract

Problem:

Settings will later edit the Window Manager config. Before that, the config shape needs to be stable and documented.

Scope:

- Background color/gradient.
- Wallpaper path and mode.
- Output selector.
- Scale and per-output scale.
- Cursor theme and size.
- Animations.
- Hardware cursor.
- Keyboard layout, model, variant, options, repeat rate, and repeat delay.
- Idle blank timeout.
- Window glass policy.
- Chrome metrics.
- Glass material.
- Border, shadow, and corner radius.
- Keybindings.
- Popup grab flag.

Acceptance:

- Default config generated on first run includes the supported keys.
- User guide matches generated default config.
- Invalid values are ignored safely.
- Hot reload works for keys that are documented as hot-reloadable.
- Keys that require restart are documented as requiring restart.
- Setting `window_glass = false` disables compositor default full-window glass without breaking explicit client glass.
- Setting `animations = false` disables Window Manager geometry animations.
- Cursor theme and size config are documented and validated manually.
- Keyboard layout and repeat config are documented and validated manually.

Implementation notes:

- Keep the config format simple enough for Settings to write later.
- Avoid adding legacy aliases unless they are still used internally.
- Prefer one canonical key name per concept.
- For every supported key, mark it as one of: hot-reloadable, applies on next client/window, or restart-required.
- The compositor should not silently accept unknown keys that look like misspelled supported keys if the existing config parser can reasonably surface that.
- Generated defaults should avoid machine-specific values except selected output examples.

### WM-10: Real-app validation

Problem:

In-tree demos are necessary but not sufficient. The Window Manager must tolerate common real Wayland clients before app work builds on top.

Required clients:

- `lambda-shell`
- `lambda-settings`
- `lambda-files`
- `lambda-terminal`
- `foot` or another mature terminal
- Firefox or another mature browser
- One GTK app with menus/popovers
- One Qt app with menus/popovers, if available on the target system

Acceptance:

- Apps launch against the running Window Manager.
- Apps can be focused, raised, moved, resized, snapped, maximized, restored, minimized, and closed.
- Text input works in text fields.
- Menus/popovers render in the right place and dismiss predictably.
- Clipboard works between mature clients.
- Browser scrolling and basic typing work.
- No client can make the Window Manager spin, freeze, or permanently lose pointer/keyboard focus in ordinary use.

Implementation notes:

- If a real app fails because it needs an intentionally unsupported feature such as XWayland, document that as outside scope.
- If a real app fails because an advertised protocol is incomplete, treat that as a Window Manager bug.

## Implementation order

1. Baseline audit.

   Check current code against this spec and list concrete bugs. Do not start with refactors.

2. Geometry and focus primitives.

   Fix focus/raise/restore/minimize behavior, especially shell-facing focus requests.

3. Resize/snap visual stability.

   Re-test interactive resize, snap, maximize, restore, shell panel overlap, and system/integrated titlebar cases.

4. Glass/chrome/shadow consistency.

   Lock down material, border, shadow, and corner-radius behavior.

5. Output/scale/cursor config.

   Validate selected output, scale, cursor theme, and cursor size.

6. Screenshot workflow.

   Validate full-output, active-window, region capture, compositor-drawn UI, options, file output, clipboard output, and cancellation.

7. Protocol and real-app smoke.

   Run the in-tree demos and a small real-app matrix.

8. Documentation update.

   Update user guide/testing docs to match actual behavior.

## Manual validation checklist

### Build and unit checks

```sh
cmake --build build --target flux_tests
./build/flux_tests --test-case="*compositor*"
cmake --build build
git diff --check
```

### Launch checks

```sh
./build/lambda-window-manager --list-outputs
./build/lambda-window-manager --output primary
./build/lambda-window-manager --output secondary
./build/lambda-window-manager --config /path/to/test-config.toml
```

Expected:

- Correct selected output.
- Visible background/wallpaper.
- Pointer moves.
- Keyboard shortcuts work.
- `Ctrl+Alt+Backspace` exits.

### Core demos

Run the existing compositor demos:

- SHM
- DMABUF
- Viewporter
- Fractional scale
- Cursor shape
- Layer shell
- Presentation time
- Idle inhibit
- Relative pointer
- Pointer constraints
- Clipboard
- Primary selection
- Drag and drop
- Popup
- Popup grab if enabled
- Activation

Expected:

- Each demo behaves as described in `docs/compositor-testing.md`.
- No demo leaves focus, pointer, presentation, or rendering in a broken state after exit.

### Lambda app checks

Run:

- `lambda-shell`
- `lambda-settings`
- `lambda-files`
- `lambda-terminal`

Validate:

- Shell topbar and dock appear with correct glass material.
- Settings system titlebar and content material match.
- Files integrated titlebar material matches Window Manager defaults.
- Terminal black glass renders correctly.
- Windows can be moved, resized, snapped, maximized, restored, minimized, and closed.
- Rapid resize does not leave blank regions.
- Snap/unmaximize animation keeps chrome and content together.

### Real-app checks

Run:

- `foot`
- Firefox
- One GTK app
- One Qt app if available

Validate:

- Menus/popups.
- Text input.
- Clipboard.
- Resize.
- Focus.
- Close.
- Scroll.

### Screenshot checks

Validate:

- `PrintScreen` full-output capture.
- active-window capture shortcut.
- region capture shortcut.
- compositor-drawn region selection overlay.
- keyboard cancel.
- pointer cancel if supported.
- save-to-file.
- copy-to-clipboard if available.
- cursor included/excluded according to policy.
- active-window capture with server-side chrome.
- active-window capture with integrated titlebar.
- capture over shell panels.
- capture over glass windows.

Expected:

- Captured PNGs match the visible target.
- Region coordinates are exact in logical output space.
- Screenshot UI does not disturb focus after cancel or capture.
- Screenshot overlay is not accidentally included in the capture.
- Failures are visible and do not crash the Window Manager.

### Config checks

Use at least these config variants:

- `window_glass = true`
- `window_glass = false`
- `animations = true`
- `animations = false`
- `scale = 1.0`
- `scale = 1.25` or `1.5`
- `scale = 2.0`
- default keyboard layout
- non-default keyboard layout, if available on the target system
- keyboard repeat disabled or very slow, if supported
- valid `cursor_theme`
- invalid `cursor_theme`
- valid `cursor_size`
- wallpaper cover/contain/stretch/center/tile
- selected output by name
- selected output by index

Expected:

- Valid values apply.
- Invalid values fail safely.
- Restart requirements are clear.

## Test additions

Add focused automated tests where behavior is deterministic:

- Output selector parsing.
- Per-output scale selection.
- Window snap geometry.
- Maximize/restore geometry.
- Minimized focus/restore behavior.
- Shell focus request behavior.
- Chrome corner radius and border configuration parsing.
- Keyboard config parsing and invalid-keymap fallback.
- Keyboard repeat config clamping.
- Config invalid-value fallback.
- Screenshot path generation if not already covered.
- Screenshot mode/option state transitions.
- Screenshot region coordinate conversion.
- Screenshot filename generation and collision handling.

Do not require GPU screenshot comparison in this milestone unless it is straightforward to add using existing render fixtures.

## Done checklist

- [ ] Idle behavior is acceptable with shell and core Lambda apps open.
- [ ] Flux Wayland apps exit cleanly when the Window Manager exits or crashes.
- [ ] Resize/snap/maximize/restore visual artifacts are resolved on the target machine.
- [ ] System-titlebar and integrated-titlebar glass are visually consistent.
- [ ] Shell panels do not flicker during nearby window animations.
- [ ] Shadow, border, and corner radius behavior is consistent.
- [ ] Shell focus requests restore minimized windows.
- [ ] Single-output selection and scale behavior are validated.
- [ ] Cursor theme and size configuration are validated.
- [ ] Keyboard layout and repeat configuration are validated.
- [ ] Screenshot capture is reliable.
- [ ] Screenshot full-output, active-window, and region modes are specified and validated.
- [ ] Compositor-drawn screenshot UI is cancelable and does not disturb focus.
- [ ] In-tree protocol demos pass.
- [ ] Real-app smoke matrix passes or has documented intentional exclusions.
- [ ] User guide and testing docs match actual behavior.

## Deferred to later milestones

- Session wrapper, auto-start, auto-restart, login, lock, logout, suspend/reboot UI.
- New log collection or trace infrastructure.
- Multi-output desktop layout.
- Shell app registry UI and dock polish.
- Settings app editor for Window Manager config.
- Files, Terminal, and other app features.
- Icon theme provider beyond cursor theme behavior.
- Notification, audio, network, power, and portal services.
