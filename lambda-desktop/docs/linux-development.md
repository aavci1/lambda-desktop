# Linux Development

This is the setup checklist for developing and testing Lambda on Linux, with the Vulkan and KMS paths enabled.

## Target

- Distro: Arch Linux.
- Kernel: use a current Arch kernel; KMS work assumes modern DRM APIs and working modesetting.
- GPU stack: Mesa for AMD/Intel, or the proprietary NVIDIA stack if that is the target hardware.
- Vulkan: Vulkan 1.3 is required. Lambda also requires `dynamicRendering` and `synchronization2`.

## Packages

Base build tools:

```sh
sudo pacman -S --needed base-devel cmake ninja pkgconf git clang
```

Graphics and text dependencies:

```sh
sudo pacman -S --needed \
  wayland wayland-protocols libxkbcommon \
  vulkan-headers vulkan-icd-loader vulkan-tools glslang \
  mesa libdrm libinput libseat systemd-libs xdg-desktop-portal libnotify \
  networkmanager bluez bluez-utils upower udisks2 libsecret \
  freetype2 fontconfig harfbuzz zlib
```

Driver packages:

```sh
# AMD
sudo pacman -S --needed vulkan-radeon mesa-utils

# Intel
sudo pacman -S --needed vulkan-intel mesa-utils

# NVIDIA
sudo pacman -S --needed nvidia nvidia-utils
```

Verify Vulkan before building Lambda:

```sh
vulkaninfo --summary
```

The selected physical device must report Vulkan 1.3. If Lambda rejects the device, the error should now say whether the problem is API version, `dynamicRendering`, `synchronization2`, a missing extension, or queue/present support.

## Configure

Default Linux build:

```sh
cmake -S . -B build -G Ninja \
  -DLAMBDAUI_BUILD_TESTS=ON \
  -DLAMBDAUI_BUILD_DEMOS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

On Linux, `LAMBDAUI_PLATFORM=AUTO` selects the Wayland framework backend and
`LAMBDA_BUILD_DESKTOP=ON` builds the Linux desktop suite by default.
`lambda-window-manager` is a Linux-only KMS target built alongside that Wayland
backend unless `LAMBDA_BUILD_WINDOW_MANAGER=OFF`, so day-to-day development
should not need a separate KMS build directory.

Dedicated KMS platform build:

```sh
cmake -S . -B build-linux-kms -G Ninja \
  -DLAMBDA_PLATFORM=LINUX_KMS \
  -DLAMBDAUI_BUILD_TESTS=ON \
  -DLAMBDAUI_BUILD_DEMOS=OFF
cmake --build build-linux-kms
```

## KMS Window Manager

KMS needs DRM master. `lambda-window-manager` now tries libseat/logind or seatd for DRM and input device access, with a direct-open fallback for development. Run it from a real TTY, not inside a desktop session that already owns DRM master. If needed, switch to a spare VT with `Ctrl+Alt+F3`, log in, then run:

```sh
./build-linux-kms/lambda-desktop/lambda-window-manager/lambda-window-manager
```

Expected result: the selected output starts the Lambda compositor. If this fails before clients launch, fix the Linux graphics stack before debugging higher-level rendering.

Useful checks:

```sh
ls -l /dev/dri
loginctl session-status
vulkaninfo --summary
```

For KMS debugging:

```sh
LAMBDA_DEBUG_KMS=1 ./build-linux-kms/lambda-desktop/lambda-window-manager/lambda-window-manager
```

## Portal Backend

The Linux build includes `lambda-portal`, a session-bus backend for the first Lambda portal slice. It currently exports the xdg-desktop-portal Settings backend (`org.freedesktop.impl.portal.Settings`) with appearance color-scheme, accent-color, contrast, and reduced-motion values, the Account backend (`org.freedesktop.impl.portal.Account`) with local user-info replies, the AppChooser backend (`org.freedesktop.impl.portal.AppChooser`) used by OpenURI application selection, the FileChooser backend (`org.freedesktop.impl.portal.FileChooser`) with deterministic `file://` URI results for Open/Save requests, the Inhibit backend (`org.freedesktop.impl.portal.Inhibit`) with in-memory request handles, the Notification backend (`org.freedesktop.impl.portal.Notification`) with basic add/remove/action routing through `lambda-notifications`, and the ScreenCast backend (`org.freedesktop.impl.portal.ScreenCast`) with request/session plumbing plus deterministic development PipeWire stream metadata when explicitly configured.

Development smoke without installing:

```sh
mkdir -p "${XDG_CONFIG_HOME:-$HOME/.config}/lambda-shell"
cat > "${XDG_CONFIG_HOME:-$HOME/.config}/lambda-shell/config.toml" <<'EOF'
[appearance]
color_scheme = "prefer-dark"
accent_color = "#4080bf"
high_contrast = false
reduced_motion = false
EOF
./build/lambda-desktop/lambda-portal/lambda-portal
```

For notification routing, also run `lambda-notifications` on the same session bus, then call the backend directly:

```sh
./build/lambda-desktop/lambda-notifications/lambda-notifications
gdbus call --session \
  --dest org.freedesktop.impl.portal.desktop.lambda \
  --object-path /org/freedesktop/portal/desktop \
  --method org.freedesktop.impl.portal.Notification.AddNotification \
  org.lambda.Smoke smoke-id "{'title': <'Portal smoke'>, 'body': <'Notification body'>}"
```

For the Account backend path, the backend returns local user information:

```sh
busctl --user call org.freedesktop.impl.portal.desktop.lambda \
  /org/freedesktop/portal/desktop \
  org.freedesktop.impl.portal.Account GetUserInformation \
  "ossa{sv}" \
  /org/freedesktop/portal/desktop/request/1_1/account_smoke \
  org.lambda.Smoke "" \
  1 reason s "Smoke reason"
```

For the OpenURI/AppChooser backend path, the frontend supplies candidate app ids and the backend returns the selected id:

```sh
busctl --user call org.freedesktop.impl.portal.desktop.lambda \
  /org/freedesktop/portal/desktop \
  org.freedesktop.impl.portal.AppChooser ChooseApplication \
  "ossasa{sv}" \
  /org/freedesktop/portal/desktop/request/1_1/lambda \
  org.lambda.Smoke "" \
  1 org.lambda.Browser \
  1 uri s https://lambda.invalid/
```

For the FileChooser backend path, the basic backend returns deterministic file URIs from request options or development environment fallbacks; the interactive Lambda chooser UI is still pending:

```sh
LAMBDA_PORTAL_FILECHOOSER_SAVE_URI=/tmp/lambda-filechooser-smoke.txt \
  ./build/lambda-desktop/lambda-portal/lambda-portal
busctl --user call org.freedesktop.impl.portal.desktop.lambda \
  /org/freedesktop/portal/desktop \
  org.freedesktop.impl.portal.FileChooser SaveFile \
  "osssa{sv}" \
  /org/freedesktop/portal/desktop/request/1_1/file_smoke \
  org.lambda.Smoke "" "Save smoke" 0
```

For the ScreenCast backend path, the basic backend returns no-selection unless a development stream node id is explicitly provided; real WM capture and PipeWire publishing are still pending:

```sh
LAMBDA_PORTAL_SCREENCAST_NODE_ID=42 \
LAMBDA_PORTAL_SCREENCAST_PIPEWIRE_SERIAL=987654321 \
  ./build/lambda-desktop/lambda-portal/lambda-portal
busctl --user call org.freedesktop.impl.portal.desktop.lambda \
  /org/freedesktop/portal/desktop \
  org.freedesktop.impl.portal.ScreenCast CreateSession \
  "oosa{sv}" \
  /org/freedesktop/portal/desktop/request/1_1/cast_create \
  /org/freedesktop/portal/desktop/session/1_1/cast \
  org.lambda.Smoke 0
busctl --user call org.freedesktop.impl.portal.desktop.lambda \
  /org/freedesktop/portal/desktop \
  org.freedesktop.impl.portal.ScreenCast Start \
  "oossa{sv}" \
  /org/freedesktop/portal/desktop/request/1_1/cast_start \
  /org/freedesktop/portal/desktop/session/1_1/cast \
  org.lambda.Smoke "" 0
```

For the Inhibit backend path, the backend records the request and exports a request object that can be closed:

```sh
request=/org/freedesktop/portal/desktop/request/1_1/inhibit_smoke
busctl --user call org.freedesktop.impl.portal.desktop.lambda \
  /org/freedesktop/portal/desktop \
  org.freedesktop.impl.portal.Inhibit Inhibit \
  "ossua{sv}" "$request" org.lambda.Smoke "" \
  4 1 reason s "Smoke reason"
busctl --user call org.freedesktop.impl.portal.desktop.lambda \
  "$request" org.freedesktop.impl.portal.Request Close
```

For installed portal selection, run a session with `XDG_CURRENT_DESKTOP=Lambda` and install the generated D-Bus service plus `lambda.portal` metadata alongside `xdg-desktop-portal`. `lambda-portal` reads the Shell appearance config path (`LAMBDA_SHELL_CONFIG`, then `$XDG_CONFIG_HOME/lambda-shell/config.toml`, then `$HOME/.config/lambda-shell/config.toml`) for `appearance.color_scheme`, `appearance.accent_color`, `appearance.high_contrast`, and `appearance.reduced_motion`; `LAMBDA_PORTAL_COLOR_SCHEME`, `LAMBDA_PORTAL_ACCENT_COLOR`, `LAMBDA_PORTAL_HIGH_CONTRAST`, and `LAMBDA_PORTAL_REDUCED_MOTION` remain development fallbacks when no config value is present.

## Notifications Daemon

The Linux build includes `lambda-notifications`, a basic `org.freedesktop.Notifications` session-bus daemon. It currently accepts notifications, stores in-memory history, supports replacement/close/action signals, backs the Shell live banner path, and receives the portal Notification backend route.

Development smoke without installing:

```sh
./build/lambda-desktop/lambda-notifications/lambda-notifications
gdbus call --session \
  --dest org.freedesktop.Notifications \
  --object-path /org/freedesktop/Notifications \
  --method org.freedesktop.Notifications.Notify \
  lambda-smoke "uint32 0" dialog-information "Smoke" "Notification body" "@as []" "@a{sv} {}" 5000
```

## StatusNotifierWatcher

The Linux build includes `lambda-status-notifier-watcher`, a basic `org.kde.StatusNotifierWatcher` session-bus daemon. It accepts StatusNotifierItem and StatusNotifierHost registrations, exposes watcher properties, emits registration signals, and lets Shell register a host, read item metadata through bounded `Properties.GetAll` calls, refresh on item property changes, and render live tray labels/glyphs from category, title/id, status, icon names, tooltip text, pixmap availability, and menu path metadata. Theme-image rendering, overlay/attention image presentation, DBusMenu menus, activation actions, and real slow-client validation are still pending.

Development smoke without installing:

```sh
./build/lambda-desktop/lambda-status-notifier-watcher/lambda-status-notifier-watcher
gdbus call --session \
  --dest org.kde.StatusNotifierWatcher \
  --object-path /StatusNotifierWatcher \
  --method org.freedesktop.DBus.Properties.Get \
  org.kde.StatusNotifierWatcher ProtocolVersion
```

## Polkit Authentication Agent

The Linux build includes `lambda-polkit-agent`, a basic system-bus authentication-agent process for polkit. It exports `org.freedesktop.PolicyKit1.AuthenticationAgent`, registers that object with `org.freedesktop.PolicyKit1.Authority` for the current session when `XDG_SESSION_ID` is available, and falls back to a documented current-process subject otherwise.

This slice proves registration and request routing only. `BeginAuthentication` currently parses the request and returns `org.freedesktop.PolicyKit1.Error.Cancelled` so privileged callers fail quickly instead of hanging; the Lambda dialog and `polkit-agent-helper-1`/PAM authentication path still need to be implemented before `pkexec` can succeed.

Development smoke:

```sh
XDG_SESSION_ID="${XDG_SESSION_ID:-$(loginctl show-user "$USER" -p Display --value 2>/dev/null || true)}" \
  ./build/lambda-desktop/lambda-polkit-agent/lambda-polkit-agent
```

In a complete Lambda session this process should be started automatically before privileged Shell or Settings actions are exposed.

## Secret Service

The Linux build includes `lambda-secrets`, a basic in-memory `org.freedesktop.secrets` session-bus daemon. It supports the plain Secret Service session handshake, a default collection and alias, item create/search/get/replace/delete flows, and item label/attribute properties so libsecret-style development flows can run inside a Lambda session.

This is not a production keyring yet. Secrets are not encrypted or persisted, lock/unlock is currently a no-op, prompt objects are not implemented, and login unlock still needs to be designed.

Development smoke without installing:

```sh
./build/lambda-desktop/lambda-secrets/lambda-secrets
gdbus call --session \
  --dest org.freedesktop.secrets \
  --object-path /org/freedesktop/secrets \
  --method org.freedesktop.Secret.Service.ReadAlias \
  default
```

## Network Status

Shell network and Wi-Fi status prefer NetworkManager when reading the live system and fall back to `/sys/class/net` when NetworkManager is unavailable. For development, confirm the system service is visible with:

```sh
nmcli general status
gdbus call --system \
  --dest org.freedesktop.NetworkManager \
  --object-path /org/freedesktop/NetworkManager \
  --method org.freedesktop.NetworkManager.GetDevices
```

## Bluetooth Status

Shell Bluetooth status prefers BlueZ when reading the live system and falls back to `/sys/class/rfkill` plus `/sys/class/bluetooth` when BlueZ is unavailable. For development, confirm the system service is visible with:

```sh
bluetoothctl show
gdbus call --system \
  --dest org.bluez \
  --object-path / \
  --method org.freedesktop.DBus.ObjectManager.GetManagedObjects
```

## Media Status

MPRIS media-player state is discovered from session-bus names with the `org.mpris.MediaPlayer2.` prefix. Shell reads this on the live system path for its media docklet; fixture-backed status tests keep media unavailable so they do not depend on the user's session bus. For development, confirm a player is visible with:

```sh
gdbus call --session \
  --dest org.freedesktop.DBus \
  --object-path /org/freedesktop/DBus \
  --method org.freedesktop.DBus.ListNames
```

If a player name is present, inspect the player interface with:

```sh
gdbus call --session \
  --dest org.mpris.MediaPlayer2.<player> \
  --object-path /org/mpris/MediaPlayer2 \
  --method org.freedesktop.DBus.Properties.Get \
  org.mpris.MediaPlayer2.Player PlaybackStatus
```

## Power Status

Shell battery status prefers UPower when reading the live system and falls back to `/sys/class/power_supply` when UPower is unavailable. For development, confirm the system service is visible with:

```sh
upower -d
gdbus call --system \
  --dest org.freedesktop.UPower \
  --object-path /org/freedesktop/UPower \
  --method org.freedesktop.UPower.GetDisplayDevice
```

## Removable Volumes

UDisks2 removable-volume state is read from the system bus. For development, confirm the service is visible with:

```sh
udisksctl status
gdbus call --system \
  --dest org.freedesktop.UDisks2 \
  --object-path /org/freedesktop/UDisks2 \
  --method org.freedesktop.DBus.ObjectManager.GetManagedObjects
```

## Common Failures

- `Missing required Vulkan instance extension: VK_KHR_display`: the ICD or driver does not expose direct display support.
- `Vulkan 1.3 required`: update Mesa/driver or select a different GPU.
- `missing Vulkan 1.3 feature(s): dynamicRendering, synchronization2`: the device or driver cannot run the Lambda Vulkan backend.
- `no graphics queue family can present to this surface`: the selected Vulkan device cannot present to the KMS/Wayland surface.
- `DRM master unavailable` or `drmModeGetResources failed`: run from a real TTY, check the reported errno, confirm no other compositor owns the card, and use `fuser -v /dev/dri/card*` to find card holders.
