# Compositor Window Chrome

Flux compositor chrome has three toplevel modes:

- Foreign CSD clients do not bind `zxdg_decoration_v1`; the compositor does not draw title-bar chrome for them.
- SSD clients bind `zxdg_decoration_v1` but not `xx_cutouts_v1`; the compositor draws the default SADE-style title bar above the client buffer.
- SSD cutout clients bind both protocols; the client buffer includes the 42 px title-bar area and the compositor overlays only the close/minimize controls.

The compositor vendors `xx-cutouts-v1.xml` from upstream wayland-protocols. For Flux/SADE toplevels, the topmost generic `cutout` box in surface-local coordinates is the compositor controls reservation. The current default reservation is:

```text
x = surface_width - 90
y = 0
width = 90
height = 42
type = cutout
id = 1
```

Cutout events are sent before the matching `xdg_surface.configure` whenever the reservation geometry changes. If a client calls `set_unhandled` with id `1`, the compositor demotes that toplevel to the default SSD path and stops sending cutout boxes.

In cutout mode, pointer events inside the controls reservation are compositor-owned. Pointer events elsewhere on the surface are delivered to the client, including the rest of the title-bar area. Clients that want draggable title-bar space should call `xdg_toplevel.move`; the compositor also supports Alt+left-drag anywhere on a toplevel as a fallback move gesture.

Chrome defaults live under `[chrome]` in the compositor config and match the SADE values. `[chrome.dark]` can override those values for a future dark theme path.
