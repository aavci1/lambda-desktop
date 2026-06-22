# Lambda Compositor User Guide

This guide covers the current Linux KMS compositor as it exists in this repository. It is meant for daily testing from a TTY, not for installation as a display manager.

## Build

Configure one Linux build. On Linux, `LAMBDA_PLATFORM=AUTO` selects the Wayland
framework backend and `LAMBDA_BUILD_DESKTOP=ON` builds the desktop suite,
including the KMS-only `lambda-window-manager` target by default.

```sh
cmake -S . -B build -G Ninja \
  -DLAMBDA_BUILD_TESTS=ON \
  -DLAMBDA_BUILD_DEMOS=ON
cmake --build build
```

Run the unit tests that cover deterministic compositor logic:

```sh
cmake --build build --target lambda_tests
./build/lambda-desktop/tests/lambda_desktop_tests --test-case="*compositor*"
```

## Launch

Run from a real TTY. The compositor owns the selected KMS output until it exits.

```sh
./build/lambda-desktop/lambda-window-manager/lambda-window-manager 2>&1 | tee compositor.log
```

List connected KMS outputs:

```sh
./build/lambda-desktop/lambda-window-manager/lambda-window-manager --list-outputs
```

Run on a specific output by connector name, 0-based index, `primary`, or `secondary`:

```sh
./build/lambda-desktop/lambda-window-manager/lambda-window-manager --output secondary 2>&1 | tee compositor.log
```

Use a specific config file without changing the environment:

```sh
./build/lambda-desktop/lambda-window-manager/lambda-window-manager --config /path/to/config.toml 2>&1 | tee compositor.log
```

Expected startup behavior:

- The output switches to the Lambda window manager.
- The compositor logs all connected KMS outputs and the selected output.
- A background is drawn from the compositor config.
- The cursor uses the system Xcursor theme or a client-provided cursor surface.
- The compositor writes the selected physical size, logical size, and scale to stderr and `compositor-sizes.log`.

Start the desktop shell from another TTY after the window manager is running:

```sh
./build/lambda-desktop/lambda-shell/lambda-shell
```

Expected shell behavior:

- A glass dock appears at the bottom.
- `Super+Space` or the dock launcher icon opens the shell command launcher.
- The dock reflects running/focused application state reported by `lambda-window-manager`.

Clients can connect without setting `WAYLAND_DISPLAY` if they use the Lambda demo helper. For other Wayland clients, use the display name written to:

```sh
$XDG_RUNTIME_DIR/lambda-window-manager-display
```

## Exit

Use the compositor terminate shortcut:

```text
Ctrl+Alt+Backspace
```

`Ctrl+C` is intentionally delivered to the focused Wayland client, which is required for terminal apps such as `foot`. To terminate from another shell, send SIGTERM:

```sh
pkill -TERM lambda-window-manager
```

## Input Permissions

The KMS platform tries to open DRM and input devices through libseat/logind or seatd first. If no seat manager is available, the development direct-open fallback may still need local ACLs before mouse and keyboard input work:

```sh
sudo setfacl -m "u:$USER:rw" /dev/input/event*
```

If the cursor is visible but does not move, or no shortcuts work, check whether libseat opened the device path; if it fell back to direct opens, check ACLs.

## Session and environment

| Variable | Purpose |
|----------|---------|
| `XDG_RUNTIME_DIR` | Required for Wayland socket and shell IPC under `$XDG_RUNTIME_DIR/lambda-window-manager-display` and `lambda-window-manager-shell.sock`. |
| `LAMBDA_WINDOW_MANAGER_CONFIG` | Override compositor TOML path (see `--config`). |
| `LAMBDA_WINDOW_MANAGER_OUTPUT` | Override `[output]` connector selector (`HDMI-A-1`, `0`, `primary`, `secondary`). |
| `LAMBDA_WINDOW_MANAGER_PRESENT` | `vulkan-display` selects legacy Vulkan-display presenter; default is GBM/atomic-KMS. |
| `LAMBDA_WINDOW_MANAGER_TIMING` | Log per-phase timing to stderr when non-zero. |
| `LAMBDA_WINDOW_MANAGER_PACING_TRACE` | Verbose presentation pacing trace. |
| `LAMBDA_WINDOW_MANAGER_PROFILE` | Aggregate frame-profile stats every ~2s. |
| `LAMBDA_WINDOW_MANAGER_IDLE_PROFILE` | Poll/idle loop stats every ~2s. |
| `LAMBDA_RESIZE_TRACE` | Log resize/configure traces (compositor + clients). |
| `LAMBDA_WINDOW_MANAGER_POPUP_TRACE` | Log popup hit testing, grab, focus, and pointer-button routing traces. |
| `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` | Enable Vulkan validation layers during development. |

Typical TTY session:

1. Log in on TTY1; ensure `XDG_RUNTIME_DIR` is set (usually `/run/user/$UID` on systemd).
2. Run `./build/lambda-desktop/lambda-window-manager/lambda-window-manager` (optionally `--output`, `--config`). The compositor uses libseat/logind or seatd for DRM/input device access when available and falls back to direct device opens for development.
3. From TTY2 or SSH, run `./build/lambda-desktop/lambda-shell/lambda-shell` after the compositor logs its socket path.
4. Wayland clients use `$XDG_RUNTIME_DIR/lambda-window-manager-display` unless they read the compositor-published env.

Security note: the shell connects over a Unix socket with mode `0600` under `XDG_RUNTIME_DIR`. Layer-shell namespace strings are not a security boundary; only trusted shell binaries should connect to the compositor IPC socket.

## Config

The compositor creates a default config file when none exists. The path is selected in this order:

1. `--config PATH`
2. `LAMBDA_WINDOW_MANAGER_CONFIG`
3. `$XDG_CONFIG_HOME/lambda-window-manager/config.toml`
4. `$HOME/.config/lambda-window-manager/config.toml`

The file is reloaded at runtime when it changes.

Current keys:

```toml
background = "#3380f2"
# background_gradient = "#203040 #405060"
# wallpaper = "/path/to/wallpaper.jpg"
# wallpaper_mode = "cover" # cover, contain, stretch, center, tile
# cursor_theme = "Adwaita" # unset uses XCURSOR_THEME or system default
# cursor_size = 24 # unset uses XCURSOR_SIZE or 24
# output = "HDMI-A-1" # connector name, 0-based index, primary, or secondary

scale = 2.0 # fallback scale for outputs without an override

# Per-output scale overrides use connector names:
# [outputs."eDP-1"]
# scale = 1.25
#
# [outputs."DP-1"]
# scale = 2.0

animations = true
hardware_cursor = true
idle_blank_timeout_seconds = 0 # 0 disables compositor-side idle blanking

[rendering.backdrop_blur]
# Effective downsample is round(base_downsample * output scale), applied to width and height.
base_downsample = 2

[input]
popup_grabs = true

[input.keyboard]
# Empty values use xkb/system defaults for that field.
layout = ""
variant = ""
model = ""
options = ""
repeat_rate = 25
repeat_delay_ms = 600

[chrome]
title_bar_height = 28
controls_width = 84
controls_inset_right = 8
controls_inset_top = 6
button_size = 16
button_radius = 5
close_glyph_color = "#5b6781"
close_glyph_hover_color = "#ffffff"
close_hover_background = "#e25555"
minimize_glyph_color = "#5b6781"
minimize_glyph_hover_color = "#16203a"
minimize_hover_background = "#00000012"
title_text_color = "#16203a"
title_text_font_size = 11.5
title_text_font_weight = 600
window_corner_radius = 14
# window_corner_radius can also be configured per corner:
# [chrome.window_corner_radius]
# all = 14
# top_left = 14
# top_right = 14
# bottom_right = 14
# bottom_left = 14
resize_grip_size = 4
window_border_color = "#ffffff9e"
window_border_width = 1
border_line_color = "#ffffff9e"

[chrome.glass]
blur_radius = 46
base_color = "#ffffff80"
tint_color = "#dbf5ff8f"
border_color = "#ffffff9e"
opacity = 1.0
contrast_color = "#000000"
focused_contrast_opacity = 0.18
unfocused_contrast_opacity = 0.13

[keybindings]
close = "super+q"
cycle_focus = "super+tab"
snap_left = "super+left"
snap_right = "super+right"
maximize = "super+up"
restore = "super+down"
launch_command = "super+space"
screenshot = ["super+shift+3", "printscreen", "sysrq"]
screenshot_region = "super+shift+4"
screenshot_active_window = ["super+shift+5", "alt+printscreen", "alt+sysrq"]
terminate = "ctrl+alt+backspace"
```

`scale` is compositor-level output scale. The compositor advertises a logical output size to clients and sends fractional-scale protocol updates when clients support them. Integer scales such as `1.0` and `2.0` are the safest baselines; `1.25` and `1.5` exercise fractional scaling. Use `[outputs."CONNECTOR"]` entries to override scale per KMS connector while keeping a fallback `scale` for every other output.

`output` selects which connected KMS connector the single-output compositor owns. Use `--list-outputs` to see connector names and indexes. Changing this key while the compositor is running is logged, but moving to another output requires restarting the compositor.

`wallpaper_mode` accepts `cover`, `contain`, `stretch`, `center`, and `tile`.
Wallpaper paths may be absolute, `~/...`, or relative to the config file directory. On Linux,
the framework image loader supports stb_image formats, including JPEG and PNG.

Normal-window glass is client-requested. Lambda apps that use `WindowBackground::glassEffect()` send a complete
glass material descriptor to the compositor, including blur radius, base color, tint, border color, and opacity.
The compositor does not synthesize default glass for windows that did not request it.
`[chrome.glass]` configures compositor chrome previews and layer-shell chrome styling, not normal-window glass.
The contrast color and opacity values add a single frame-shaped readability layer behind white title text and controls,
so transparent title bars stay legible on light backgrounds without drawing a separate titlebar strip.
`window_border_color` / `window_border_width` control the subtle rounded outline around the window frame.
Clients that use `ext-background-effect-v1` request their own explicit blur regions and material values.

`[rendering.backdrop_blur].base_downsample` controls the compositor backdrop blur quality/performance tradeoff.
The effective blur texture downsample is `round(base_downsample * scale)` and applies to both width and height.
The default `2` keeps 1x output behavior unchanged while reducing blur texture resolution naturally as output
scale increases.

Keybinding values can be a single shortcut string or an array of shortcut strings. Shortcut tokens use `+`
separators, for example `super+shift+3`, `alt+printscreen`, or `ctrl+alt+backspace`.

Config application policy:

| Key or section | Policy |
|----------------|--------|
| `background`, `background_gradient` | Hot reload |
| `wallpaper`, `wallpaper_mode` | Hot reload; image decoding is asynchronous |
| `cursor_theme`, `cursor_size` | Hot reload for compositor-owned cursors |
| `scale`, `[outputs.*].scale` | Hot reload for the selected output |
| `output` | Restart required to move the compositor to another KMS output |
| `animations`, `hardware_cursor`, `idle_blank_timeout_seconds` | Hot reload |
| `[rendering.backdrop_blur]` | Hot reload |
| `[input] popup_grabs` | Hot reload |
| `[input.keyboard]` | Hot reload; existing keyboard resources receive updated keymap/repeat info |
| `[chrome]`, `[chrome.glass]`, `[chrome.dark]` | Hot reload |
| `[keybindings]` | Hot reload |

`[input] popup_grabs` enables xdg-popup grab handling for application menus and context menus. It is on by default so popup input uses popup-first hit testing, same-client owner events, wlroots-style parent/child popup grab stacking, and implicit pointer-button delivery while transient menu surfaces are being recreated. Disable it only when isolating a client-side popup issue.

## Window Management

Current shortcuts:

| Shortcut | Behavior |
|----------|----------|
| `Super+Q` | Close the focused window |
| `Super+Tab` | Cycle focus |
| `Super+Left` | Snap focused window to the left half |
| `Super+Right` | Snap focused window to the right half |
| `Super+Up` | Maximize focused window |
| `Super+Down` | Restore a snapped or maximized window |
| `Super+Space` | Open the command launcher |
| `PrintScreen`, `SysRq`, `Super+Shift+3` | Capture the selected output |
| `Super+Shift+4` | Select a screenshot region |
| `Alt+PrintScreen`, `Alt+SysRq`, `Super+Shift+5` | Capture the active window |
| `Ctrl+Alt+F1` ... `Ctrl+Alt+F12` | Switch to another VT/session |
| `Alt+Left`, `Alt+Right` | Switch to the previous or next VT/session |
| `Ctrl+Alt+Backspace` | Terminate the compositor |

Mouse behavior:

- Click the left title bar close button to close a window.
- Drag title bar to move a window.
- Drag a snapped or maximized title bar to restore it once dragging starts.
- Drag to within a few pixels of the left or right output edge, then hold briefly to preview half snap.
- Drag to within a few pixels of an output corner, then hold briefly to preview quarter snap.
- Drag to within a few pixels of the top output edge, then hold briefly to preview maximize.
- Drag window edges and corners to resize.
- Click a window to focus it and raise it.

Command launcher:

- Press `Super+Space`.
- Type to filter the shell app registry.
- Press Enter or click a result to launch/focus it; Escape or an outside click cancels.

The launched process receives `WAYLAND_DISPLAY` for the running compositor automatically.

## Cursor Theme

The compositor has no built-in cursor artwork. It uses Xcursor theme images for compositor-owned cursor shapes and accepts client cursor surfaces when clients provide them.

Config keys:

```toml
cursor_theme = "Adwaita"
cursor_size = 24
```

If those keys are unset, the compositor falls back to environment variables and then to the system default.

Useful environment variables:

```sh
XCURSOR_THEME=Adwaita
XCURSOR_SIZE=24
```

Set them before launching the compositor if you need to force a theme or size.

## Current Limitations

- Single active output only. The selected output is configurable, but multi-monitor desktop layout is not implemented.
- No display-manager, login, lock screen, workspaces, or XWayland.
- Input device permissions are still manual unless your session grants ACLs.
- Popup support works for the test demos and uses popup-first pointer hit testing with xdg-popup grabs enabled by default. Popup grabs track parent/child grab stacks and reject invalid grab-after-commit requests. The popup demo has visible hover/click validation, and Firefox application/context menus have been manually validated for menu actions and click-open submenus. Broader GTK/Qt menu coverage is still useful.
- Presentation-time feedback uses DRM vblank pacing timestamps, refresh intervals, and sequence counters when available. If the driver rejects vblank waits, the compositor falls back to compositor-clock timing.
- Software idle blanking is available with `idle_blank_timeout_seconds`; `0` disables it. Active idle inhibitors prevent the compositor from blanking. DPMS/panel power-off is not implemented yet.

## Remaining Work

- Real-app validation beyond `foot`, especially GTK, Qt, and browser clients.
- Broader popup/menu validation with GTK, Qt, and unusual nested/grabbed popup workflows.
- Hardware validation of the new GBM/atomic-KMS page-flip completion path, then broader video/game validation of presentation-time feedback.
- Adaptive sync and triple-buffering.
- Target-hardware validation of libseat/logind input/session brokering without manual `/dev/input/event*` ACLs.
- Full multi-output desktop layout if it lands in v1.
- Install/session-manager packaging documentation.

Set `LAMBDA_WINDOW_MANAGER_PRESENT=vulkan-display` only for debugging if you need to compare against the previous Vulkan-display presenter.
