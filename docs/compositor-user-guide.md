# Flux Compositor User Guide

This guide covers the current Linux KMS compositor as it exists in this repository. It is meant for daily testing from a TTY, not for installation as a display manager.

## Build

Configure a KMS build:

```sh
cmake -S . -B build-kms-compositor -G Ninja \
  -DFLUX_PLATFORM=LINUX_KMS \
  -DFLUX_BUILD_COMPOSITOR=ON \
  -DFLUX_BUILD_TESTS=ON \
  -DFLUX_BUILD_EXAMPLES=ON
cmake --build build-kms-compositor
```

Run the unit tests that cover deterministic compositor logic:

```sh
cmake --build build --target flux_tests
./build/flux_tests --test-case="*compositor*"
```

## Launch

Run from a real TTY. The compositor owns the selected KMS output until it exits.

```sh
./build-kms-compositor/flux-compositor 2>&1 | tee compositor.log
```

Use a specific config file without changing the environment:

```sh
./build-kms-compositor/flux-compositor --config /path/to/config.toml 2>&1 | tee compositor.log
```

Expected startup behavior:

- The output switches to the Flux compositor.
- A background is drawn from the compositor config.
- The cursor uses the system Xcursor theme or a client-provided cursor surface.
- The compositor writes the selected physical size, logical size, and scale to stderr and `compositor-sizes.log`.

Clients can connect without setting `WAYLAND_DISPLAY` if they use the Flux demo helper. For other Wayland clients, use the display name written to:

```sh
$XDG_RUNTIME_DIR/flux-compositor-display
```

## Exit

Use the compositor terminate shortcut:

```text
Ctrl+Alt+Backspace
```

`Ctrl+C` is intentionally delivered to the focused Wayland client, which is required for terminal apps such as `foot`. To terminate from another shell, send SIGTERM:

```sh
pkill -TERM flux-compositor
```

## Input Permissions

The compositor reads `/dev/input/event*` directly through the KMS platform layer. After reboot, local ACLs may need to be restored before mouse and keyboard input work:

```sh
sudo setfacl -m "u:$USER:rw" /dev/input/event*
```

If the cursor is visible but does not move, or no shortcuts work, check ACLs first.

## Config

The compositor creates a default config file when none exists. The path is selected in this order:

1. `--config PATH`
2. `FLUX_COMPOSITOR_CONFIG`
3. `$XDG_CONFIG_HOME/flux-compositor/config.toml`
4. `$HOME/.config/flux-compositor/config.toml`

The file is reloaded at runtime when it changes.

Current keys:

```toml
background = "#3380f2"
# background_gradient = "#203040 #405060"
# wallpaper = "/path/to/wallpaper.png"
# wallpaper_mode = "cover" # cover, contain, stretch, center, tile
# cursor_theme = "Adwaita" # unset uses XCURSOR_THEME or system default
# cursor_size = 24 # unset uses XCURSOR_SIZE or 24

scale = 2.0
animations = true
hardware_cursor = true

[keybindings]
close = "super+q"
cycle_focus = "super+tab"
snap_left = "super+left"
snap_right = "super+right"
maximize = "super+up"
restore = "super+down"
terminate = "ctrl+alt+backspace"
```

`scale` is compositor-level output scale. The compositor advertises a logical output size to clients and sends fractional-scale protocol updates when clients support them. Integer scales such as `1.0` and `2.0` are the safest baselines; `1.25` and `1.5` exercise fractional scaling.

`wallpaper_mode` accepts `cover`, `contain`, `stretch`, `center`, and `tile`.

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
| `Ctrl+Alt+Backspace` | Terminate the compositor |

Mouse behavior:

- Click title bar close button to close a window.
- Drag title bar to move a window.
- Drag a snapped or maximized title bar to restore it once dragging starts.
- Drag left, right, or top edge of the output to preview snap or maximize.
- Drag window edges and corners to resize.
- Click a window to focus it and raise it.

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

- Single output only.
- No display-manager, login, lock screen, workspaces, or XWayland.
- Input device permissions are still manual unless your session grants ACLs.
- Popup support works for the test demos and uses popup-first pointer hit testing, but broader `foot`/GTK/Qt/browser menu behavior still needs real-app validation.
- Presentation-time support exists, but timestamp precision still needs hardware-derived presentation data.
- Idle-inhibit protocol state is tracked; actual idle blanking and inhibition policy are not implemented yet.

## Remaining Work

- Real-app validation beyond `foot`, especially GTK, Qt, and browser clients.
- Popup menu validation with real applications; full xdg-popup input-grab semantics remain deferred.
- Hardware-derived presentation-time feedback and refresh counters.
- Adaptive sync and triple-buffering.
- Proper input/session brokering instead of manual `/dev/input/event*` ACLs.
- Multi-output decision and implementation if it lands in v1.
- Install/session-manager packaging documentation.
