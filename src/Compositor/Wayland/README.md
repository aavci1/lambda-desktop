# Compositor Wayland integration

This directory is the target home for the libwayland-server orchestration code.
`Server` should own the display/socket lifecycle and dispatch loop. Per-global
protocol implementations belong under `Globals/`.

