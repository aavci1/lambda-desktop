# Compositor Window Chrome

Flux compositor chrome has three toplevel modes:

- Foreign CSD clients do not bind `zxdg_decoration_v1`; the compositor does not draw title-bar chrome for them.
- SSD clients bind `zxdg_decoration_v1` but not `xx_cutouts_v1`; the compositor draws the default SADE-style title bar above the client buffer.
- SSD cutout clients bind both protocols; the client buffer includes the 28 px title-bar area and the compositor overlays only the close/minimize controls.

The compositor vendors `xx-cutouts-v1.xml` from upstream wayland-protocols. For Flux/SADE toplevels, the topmost generic `cutout` box in surface-local coordinates is the compositor controls reservation. The current default reservation is:

```text
x = surface_width - 58
y = 0
width = 58
height = 28
type = cutout
id = 1
```

Cutout events are sent before the matching `xdg_surface.configure` whenever the reservation geometry changes. If a client calls `set_unhandled` with id `1`, the compositor demotes that toplevel to the default SSD path and stops sending cutout boxes.

In cutout mode, pointer events inside the controls reservation are compositor-owned. Pointer events elsewhere on the surface are delivered to the client, including the rest of the title-bar area. Clients that want draggable title-bar space should call `xdg_toplevel.move`; the compositor also supports Alt+left-drag anywhere on a toplevel as a fallback move gesture.

## Render Fixture

The window painter can be exercised without running a compositor. On macOS the fixture uses Metal; on Linux it uses the same Vulkan render-target path as the compositor:

```sh
cmake --build build --target flux-compositor-render-fixture
./build/flux-compositor-render-fixture build/compositor-window-chrome-fixture.png
```

The fixture renders synthetic Tier 1, Tier 2, and Tier 3 `CommittedSurfaceSnapshot`s through the same `drawCommittedSurfaceSnapshot` path used by the KMS compositor, reads back the offscreen render target, and writes a PNG for visual inspection or pixel checks.

Chrome defaults live under `[chrome]` in the compositor config and match the SADE values. `[chrome.dark]` can override those values for a future dark theme path. The compositor also enables `window_glass` by default, which synthesizes a full-window blur region and applies `window_glass_opacity` to the glass material behind client buffers; set `window_glass = false` to disable that default while still allowing clients to request explicit `ext-background-effect-v1` blur regions.

The title bar and frame metrics are configurable from `[chrome]`. The commonly tuned values are:

```toml
[chrome]
title_bar_height = 28
controls_width = 84
controls_inset_right = 8
controls_inset_top = 6
button_size = 16
resize_grip_size = 4
window_corner_radius = 14
window_border_color = "#d8dee899"
window_border_width = 1
```

`window_corner_radius` can also be configured per corner:

```toml
[chrome.window_corner_radius]
all = 14
top_left = 14
top_right = 14
bottom_right = 14
bottom_left = 14
```

The resize grip is a thin hit-test ring around the visible rounded frame, not a full square strip. With the default 4 px grip, most of the 28 px title bar remains available for dragging while the rounded corners still expose diagonal resize handles.

Window controls are laid out from the active `title_bar_height`. `controls_width` is split into contiguous minimize, maximize, and close segments using the full title-bar height for hover, press, and click handling. `button_size`, `controls_width`, `controls_inset_top`, and `button_radius` are treated as the 28 px title-bar baseline and scale proportionally when the title bar gets taller or shorter. Glyphs are centered inside their segments after scaling.
