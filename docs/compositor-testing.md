# Flux Compositor Testing Checklist

Use this checklist when validating compositor changes on the TTY machine. Each checkpoint says what to run and what to expect visually.

## Before Launch

1. Build the KMS compositor.

```sh
cmake --build build
```

2. Ensure input ACLs exist after reboot.

```sh
sudo setfacl -m "u:$USER:rw" /dev/input/event*
```

3. Launch from a TTY and capture logs.

```sh
./build/lambda-window-manager 2>&1 | tee compositor.log
```

To see connected connector names and indexes:

```sh
./build/lambda-window-manager --list-outputs
```

To run on a secondary monitor without full multi-output:

```sh
./build/lambda-window-manager --output secondary 2>&1 | tee compositor.log
```

To test a specific config file, use:

```sh
./build/lambda-window-manager --config /path/to/config.toml 2>&1 | tee compositor.log
```

Expected result: the selected output logs as connected, the configured background appears on that output, the system cursor is visible, moving the mouse moves the cursor, and `Ctrl+Alt+Backspace` exits the compositor.

## Core Demo Tests

Run these from another TTY or SSH shell while the compositor is running.

### SHM

```sh
./build/lambda-window-manager-shm-demo
```

Expected result: a decorated window appears with SHM-rendered content. The client prints a submitted-buffer message.

### DMABUF

```sh
./build/lambda-window-manager-dmabuf-demo
```

Expected result: a decorated window appears with the DMABUF demo content. The compositor should not hang. The client should report a committed DMABUF buffer.

### Viewporter

```sh
./build/lambda-window-manager-viewport-demo
```

Expected result: the window content is non-black and shows the viewported pattern. Resizing should keep content within the surface bounds.

### Fractional Scale

```sh
./build/lambda-window-manager-fractional-scale-demo
```

Expected result: the demo reports the preferred scale. At integer scales, Flux app content should be sharp. At fractional scales, clients that support `wp_fractional_scale_v1` should render at the requested fractional scale.

### Cursor Shape

```sh
./build/lambda-window-manager-cursor-shape-demo
```

Expected result: moving over the demo changes the active cursor shape. The primary visible cursor should be the system theme cursor, not an old built-in cursor.

### Layer Shell

```sh
./build/lambda-window-manager-layer-shell-demo
```

Expected result: a top layer-surface bar appears and renders non-black content.

### Presentation Time

```sh
./build/lambda-window-manager-presentation-time-demo
```

Expected result: the client receives presentation feedback. It should print `sync_output` before `presented`. On the default GBM/atomic-KMS presenter, feedback is sent from the DRM page-flip completion event and includes refresh intervals, sequence counters, and `VSYNC`/`HW_CLOCK`/`HW_COMPLETION` flags.

The legacy Vulkan-display presenter can still use DRM vblank timing, and if the Vulkan driver supports `VK_GOOGLE_display_timing`, the compositor logs `Vulkan display timing available` after the first present. After the first past-presentation record arrives, `wp_presentation_time` feedback is delayed until the matching Vulkan completion record is available; if a record does not arrive promptly, the compositor falls back to DRM-vblank timing for that feedback.

Set `LAMBDA_WINDOW_MANAGER_PRESENT=vulkan-display` before launching the compositor to force the legacy presenter for comparison while the atomic-KMS path is being hardware-smoked.

### Idle Blanking

Set `idle_blank_timeout_seconds = 5` in `~/.config/lambda-window-manager/config.toml`, wait five seconds without input, then move the pointer or press a key.

Expected result: the compositor renders a black frame after the timeout and redraws the desktop on input. Running `lambda-window-manager-idle-inhibit-demo` should keep the desktop visible while its inhibitor is active.

### Pointer Extensions

```sh
./build/lambda-window-manager-relative-pointer-demo
./build/lambda-window-manager-pointer-constraints-demo
```

Expected result: relative pointer events and pointer constraints operate inside the demo window without losing the visible cursor permanently.

### Clipboard And Primary Selection

```sh
./build/lambda-window-manager-clipboard-demo
./build/lambda-window-manager-primary-selection-demo
```

Expected result: clicking or selecting inside the demo transfers the advertised text payload and the client logs the received payload.

### Drag And Drop

```sh
./build/lambda-window-manager-dnd-demo
```

Expected result: two windows appear. Drag the orange source rectangle to the blue target window. The target highlights during drag and shows the dropped red rectangle after drop.

### Popup

```sh
./build/lambda-window-manager-popup-demo
```

Expected result: the popup appears next to its parent surface. Moving the pointer over popup rows changes the highlighted row, clicking a row turns it green and logs the click, clicking outside dismisses it, and the compositor does not freeze.

### Popup Grab (submenu)

Requires compositor config `[input] popup_grabs = true`.

```sh
./build/lambda-window-manager-popup-grab-demo
```

Optional trace logging:

```sh
LAMBDA_WINDOW_MANAGER_POPUP_TRACE=1 ./build/lambda-window-manager
```

Expected result: clicking the parent opens a menu popup that grabs input. Hovering **More >** opens a nested submenu that takes the grab; both stay interactive, clicking outside dismisses the top popup, and the compositor does not freeze.

### Activation

```sh
./build/lambda-window-manager-activation-demo
```

Expected result: activation requests focus the intended demo window and do not make the hardware cursor disappear.

## Flux App Tests

Run a few regular Flux examples from the KMS build. Good smoke cases:

- `toggle-demo`
- `animation-demo`
- any simple text input demo

Expected result: windows are decorated, focus works, keyboard input goes to the focused client, client content remains sharp at integer scale, and resizing does not stretch the last client frame.

### Command Launcher

Press `Super+Space`, type a command such as:

```sh
./build/lambda-window-manager-shm-demo
```

Press Enter.

Expected result: a centered command launcher appears, the typed command is visible, Enter launches the app against the running compositor, and Escape dismisses the launcher without launching anything.

## Real App Tests

Run real apps after the in-tree demos. These checks catch protocol gaps that the demos do not exercise, especially xdg-popup menu behavior, clipboard ownership, keyboard focus, and resize behavior under mature toolkit clients.

Set the compositor display once in the shell where the apps will be launched:

```sh
export WAYLAND_DISPLAY="$(cat "$XDG_RUNTIME_DIR/lambda-window-manager-display")"
```

For every real app below, first validate the common window-manager actions:

- focus by clicking the window and by using the shell/dock when available
- move by dragging the server-side titlebar or app-provided titlebar
- resize from each edge and at least one corner
- snap left/right, maximize, restore, minimize, and restore from the shell/dock
- type into a text field or terminal prompt
- close the window and confirm the previous focused window is sensible
- leave the app idle for a minute and confirm it does not make the compositor redraw continuously

### foot

```sh
foot
```

Expected result:

- The terminal opens and accepts text input.
- `Ctrl+C` kills a process inside `foot`; it should not kill the compositor.
- Multiple windows can be opened and focused.
- Resizing should reflow terminal content rather than stretch it.
- Plain right-click may not open a menu with the default local `foot` config; the default binding is selection extension. Use a configured popup/menu action if testing `foot` popups specifically.

### Browser

Use Firefox when available:

```sh
MOZ_ENABLE_WAYLAND=1 firefox
```

If Firefox is unavailable, use another pure-Wayland browser installed on the target machine.

Expected result:

- A normal browser window opens without XWayland.
- Address-bar typing works.
- Page scrolling works with wheel/trackpad input.
- Browser menus, context menus, and permission/popover UI appear at the correct position and dismiss correctly.
- Fullscreen enter/exit does not leave the window, shell panels, or focus state stuck.
- Clipboard copy/paste works between the browser and another Wayland client.

### GTK App

Use any installed GTK app with text input and menus. Good candidates:

```sh
gedit
nautilus
gnome-text-editor
```

Expected result:

- The app launches as Wayland, not XWayland.
- Headerbar or server-side decoration mode is usable.
- Menus, popovers, file/path entries, and context menus are positioned correctly.
- Text input and clipboard copy/paste work.
- Resizing and snapping do not leave blank newly exposed regions.

### Qt App

Run this only if a Qt Wayland app is installed. Good candidates:

```sh
QT_QPA_PLATFORM=wayland kate
QT_QPA_PLATFORM=wayland dolphin
QT_QPA_PLATFORM=wayland qterminal
```

Expected result:

- The app launches through the Wayland platform plugin.
- Menus and context menus appear above the app and dismiss correctly.
- Text input and clipboard copy/paste work.
- Resize, maximize, restore, minimize, and shell focus restore work.

### Cross-App Clipboard

Open at least two mature clients, for example `foot` and Firefox or a GTK editor.

Expected result:

- Copy from one app and paste into the other.
- Copy in the reverse direction.
- If the source app exits after copying, pasting should either continue through the source protocol handoff or fail cleanly without freezing the compositor.

## Regression Checks

After a compositor implementation change, run:

```sh
cmake --build build --target flux_tests
./build/flux_tests --test-case="*compositor*"
cmake --build build
git diff --check
```

## Useful Logs

- `compositor.log`: stderr from the compositor session.
- `~/.local/state/lambda-window-manager/crash.log`: durable compositor crash breadcrumbs when enabled with `LAMBDA_WINDOW_MANAGER_CRASH_LOG=1` or `LAMBDA_WINDOW_MANAGER_CRASH_LOG=/path/to/log`. The compositor appends low-volume lifecycle/surface/DMABUF/frame-stall events and fsyncs each entry so the last events should survive a compositor crash or hard reset.

### Instrumentation Environment

Boolean-style variables are enabled by any non-empty value except `0`. Unset instrumentation variables should not emit logs, create trace files, take timing samples, or do per-frame trace work.

Compositor traces:

- `LAMBDA_WINDOW_MANAGER_CPU_TRACE=1`: enables one summary line per second in `~/.local/state/lambda-window-manager/cpu.log`, including process CPU percent, frame phases, wake sources, SHM copy time, image upload time, DMABUF import/fallback counts, and surface commit attribution.
- `LAMBDA_WINDOW_MANAGER_CPU_TRACE_LOG=/path/to/log`: overrides the CPU trace log path.
- `LAMBDA_WINDOW_MANAGER_SAMPLE_TRACE=1`: enables the SIGPROF sampled CPU profiler. This only runs when `LAMBDA_WINDOW_MANAGER_CPU_TRACE=1` is also enabled.
- `LAMBDA_WINDOW_MANAGER_SAMPLE_USEC=2000`: sets the sampled profiler CPU-time interval in microseconds.
- `LAMBDA_WINDOW_MANAGER_PROFILE=1`: prints frame-profile summaries to stderr.
- `LAMBDA_WINDOW_MANAGER_IDLE_PROFILE=1`: prints loop, poll, idle, vblank, and render summaries to stderr.
- `LAMBDA_WINDOW_MANAGER_TIMING=1`: prints coarse compositor timing lines to stderr.
- `LAMBDA_WINDOW_MANAGER_PACING_TRACE=1`: logs atomic page-flip scheduling and completion cadence to stderr and `/tmp/lambda-window-manager-pacing.log`.
- `LAMBDA_WINDOW_MANAGER_PACING_TRACE_LOG=/path/to/log`: overrides the pacing trace log path.
- `LAMBDA_WINDOW_MANAGER_CRASH_LOG=1`: enables durable crash breadcrumbs at the default path.
- `LAMBDA_WINDOW_MANAGER_CRASH_LOG=/path/to/log`: enables durable crash breadcrumbs at a custom path.

Resize traces:

- `FLUX_RESIZE_TRACE=1`: enables resize tracing across compositor and framework resize paths.
- `FLUX_RESIZE_TRACE_LOG=/path/to/log`: overrides the resize trace path. Default is `/tmp/flux-resize-trace.log`.

Input, KMS, and Wayland debug:

- `FLUX_DEBUG_COMPOSITOR_INPUT=1`: logs compositor-dispatched input events.
- `FLUX_DEBUG_KMS=1`: logs KMS, libinput, and platform details.
- `FLUX_DEBUG_WAYLAND_DECORATIONS=1`: logs Wayland client-side decoration behavior.

Framework debug:

- `FLUX_DEBUG_INPUT=1`: enables framework input debug logs.
- `FLUX_DEBUG_INPUT_VERBOSE=1`: enables verbose framework input debug logs.
- `FLUX_DEBUG_LAYOUT=1`: enables framework layout debug logs.
- `FLUX_DEBUG_PERF=1`: enables framework performance logs.
- `FLUX_DEBUG_PERF=2` or `FLUX_DEBUG_PERF=verbose`: enables verbose framework performance logs.
- `FLUX_DEBUG_PERF=anomaly` or `FLUX_DEBUG_PERF=quiet`: only logs framework performance anomalies.

Graphics debug and performance toggles:

- `FLUX_DEBUG_SCREENSHOT_PATH=/path/to/image`: writes one Vulkan debug screenshot.
- `FLUX_RENDER_TARGET_DISABLE_FRAME_CACHE=1`: disables render target frame caching.

For resize/vblank investigation, launch the compositor like this:

```sh
rm -f /tmp/flux-resize-trace.log /tmp/lambda-window-manager-pacing.log
FLUX_RESIZE_TRACE=1 LAMBDA_WINDOW_MANAGER_PACING_TRACE=1 ./build/lambda-window-manager 2>&1 | tee compositor.log
```

Expected trace shape while resizing: `configure` lines should be followed by client `commit-match-*` lines within one or two refresh intervals, and `flip-complete` intervals should stay close to the output refresh period with low error.

For compositor CPU attribution, launch with:

```sh
rm -f ~/.local/state/lambda-window-manager/cpu.log
LAMBDA_WINDOW_MANAGER_CPU_TRACE=1 ./build/lambda-window-manager 2>&1 | tee compositor.log
```

Expected trace shape under load: the `cpu=` value should track the compositor CPU seen in `top`, while `phase_avg_ms`, `shm`, `image`, and `dmabuf` fields identify whether the work is in frame rendering/presentation, SHM copies, texture uploads, or DMABUF import/fallback.

## Known Bad Signs

- Cursor is visible but cannot move: input ACLs are missing or the compositor could not open input devices.
- A demo reports missing `wl_compositor`, `wl_shm`, `wl_seat`, or `xdg_wm_base`: the client connected to the wrong Wayland display.
- A window is fully black: inspect compositor logs for buffer import, SHM mmap, DMABUF, or descriptor allocation failures.
- The TTY echoes typed characters while the compositor is active: the input device or terminal mode ownership path regressed.
