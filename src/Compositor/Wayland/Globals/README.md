# Wayland globals

One file per exposed Wayland global belongs here. Each implementation should own
its protocol callbacks and resource list instead of adding more callbacks to a
monolithic server file.

