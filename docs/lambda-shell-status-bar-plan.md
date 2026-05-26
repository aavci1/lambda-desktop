# Lambda Shell status bar plan

**Date:** 2026-05-26
**Status:** Investigation and implementation plan
**Scope:** `lambda-shell` top-bar status modules, quick settings, status provider backends, and related Shell-owned UI.

## Summary

The current top-bar status icons are placeholder-level. They render a fixed set of hard-coded symbolic icons from string values that are hard-coded as `"unknown"` in the Window Manager snapshot. The quick-status popup exists, but all entries are disabled and it explicitly says provider controls are unavailable.

The correct direction is to move status ownership into `lambda-shell`: Shell should own system status providers, top-bar module presentation, quick settings, notification state, clipboard history UI, and Settings entry points. The Window Manager should not become the audio, network, battery, brightness, Bluetooth, notification, or clipboard backend.

The top bar should never lie. If a provider is missing, show `unavailable`. If a provider can read but not write, show real status and disable controls. If a provider can write, enable controls and update state only after the backend confirms or a refresh succeeds.

## Current implementation

### Snapshot source

`lambda-window-manager` currently sends a `system` object in `lambda.windowManager.snapshot`, but every value is hard-coded to `"unknown"` in `src/Compositor/Shell/ShellProtocol.cpp`:

```json
"system":{
  "network":"unknown",
  "wifi":"unknown",
  "bluetooth":"unknown",
  "volume":"unknown",
  "battery":"unknown"
}
```

This means the Window Manager is not currently providing real status data.

### Shell model

`lambda-shell` parses those fields into:

- `ShellSystemStatusSnapshot` in `src/Shell/ShellModels.hpp`
- `SystemStatus` in `src/Shell/UI/LambdaShellTypes.hpp`
- `ShellModel::systemStatus_` in `src/Shell/ShellModel.hpp`

This model is string-based. It distinguishes actual string values from `"unknown"` and `"unavailable"`, but it does not have typed state such as percentage, muted, charging, connected device count, provider availability, or control capability.

### Top-bar rendering

`src/Shell/UI/LambdaShellTypes.cpp` maps `SystemStatus` into four `TopBarStatusItem`s:

- network or Wi-Fi
- Bluetooth
- volume
- battery

`src/Shell/UI/LambdaTopBar.cpp` renders those items as icons and optional text labels. The popup is a `PopupMenu`, not a rich control popover, and all menu items are disabled.

### Existing config and models

The Shell config already includes useful shape:

- `top_bar.modules = ["network", "bluetooth", "volume", "battery", "notifications", "clipboard", "clock"]`
- `quick_settings.modules = ["network", "bluetooth", "audio", "battery", "brightness", "do_not_disturb"]`
- notification preferences
- clipboard history preferences

`NotificationCenterModel` and `ClipboardHistoryModel` already exist and have tests, but they are not wired into live top-bar UI, notification banners, notification center, or clipboard picker behavior.

## Main design correction

Stop treating the Window Manager snapshot as the source of system status. The Shell should own status providers.

The Window Manager should continue to own:

- toplevel/window focus and launch authority
- global shortcut capture
- layer-shell placement
- compositor rendering
- screenshots and compositor UI

The Shell should own:

- status providers
- top-bar status module ordering and rendering
- quick settings and status controls
- notification banners and notification center
- do-not-disturb state
- clipboard history UI and policy
- Settings entry points for deeper configuration

The `system` object in Window Manager snapshots can be deprecated or ignored by the Shell. Keeping it temporarily for compatibility is acceptable internally, but no new real provider work should go into the Window Manager.

## Provider model

Add a typed Shell provider layer. Suggested starting shape:

```cpp
enum class ProviderAvailability {
  Unavailable,
  Unknown,
  Available,
};

struct StatusValue {
  std::string id;
  std::string label;
  ProviderAvailability availability = ProviderAvailability::Unavailable;
  bool enabled = false;
  std::string summary;
  std::optional<float> percent;
  bool muted = false;
  bool charging = false;
  bool writable = false;
  flux::IconName icon = flux::IconName::Circle;
};

class StatusProvider {
public:
  virtual ~StatusProvider() = default;
  virtual StatusValue snapshot() const = 0;
  virtual void refresh() = 0;
};
```

Add optional control interfaces for providers that can mutate state:

```cpp
class ToggleProvider {
public:
  virtual ~ToggleProvider() = default;
  virtual bool setEnabled(bool enabled) = 0;
};

class LevelProvider {
public:
  virtual ~LevelProvider() = default;
  virtual bool setLevel(float normalized) = 0;
};

class ActionProvider {
public:
  virtual ~ActionProvider() = default;
  virtual bool invoke(std::string_view action) = 0;
};
```

The top bar and quick settings should consume the same provider snapshots. Avoid separate duplicated status paths for top-bar icons and quick-settings controls.

## Provider backends

### Clock

Shell-owned only.

Requirements:

- Honor `top_bar.clock_format`.
- Update at a conservative interval, ideally on minute boundaries for minute-level formats.
- Avoid unnecessary redraws when formatted text has not changed.

Current gap:

- `ShellModel::formatTimeText()` is hard-coded and does not use Shell config.

### Battery and power

Use sysfs first:

- `/sys/class/power_supply/*/type`
- `/sys/class/power_supply/*/capacity`
- `/sys/class/power_supply/*/status`

Optional later:

- UPower over D-Bus for richer state and multiple batteries.

Requirements:

- Show unavailable when no battery exists.
- Show percentage and charging/discharging/full when available.
- Choose icons from actual percentage and charging state.
- Never display fake battery percentages.

Controls:

- Battery is initially read-only.
- Power mode can be added later if a real backend exists.

### Brightness

Use sysfs first:

- `/sys/class/backlight/*/brightness`
- `/sys/class/backlight/*/max_brightness`

Requirements:

- Show unavailable if no backlight exists.
- Show read-only state if current brightness can be read but not written.
- Enable slider only when write permission or a real privileged backend exists.

Control backend policy:

- Initial implementation may be read-only if permissions are not available.
- Later write path should use an explicit backend such as logind/udev policy or a small privileged helper.
- Do not silently fail or pretend brightness changed.

### Audio

Preferred final direction:

- PipeWire/WirePlumber API.

Pragmatic first milestone:

- Use `wpctl` if present.

Requirements:

- Read default sink volume.
- Read mute state.
- Show unavailable if no backend exists.
- Show muted/low/medium/high icons based on real state.

Controls:

- Volume slider.
- Mute toggle.
- Later: microphone mute and output device picker.

### Network and Wi-Fi

Preferred backend:

- NetworkManager D-Bus.

Requirements:

- Show unavailable if NetworkManager or usable network interfaces are not available.
- Show disconnected, connecting, connected, and SSID when available.
- Distinguish wired network and Wi-Fi.
- Never show fake connected state.

Controls:

- First useful control: Wi-Fi enabled/disabled if NetworkManager exposes it.
- Open Settings network panel for deeper connection selection.
- Full password entry and network selection can be deferred to Settings.

### Bluetooth

Preferred backend:

- BlueZ D-Bus.

Requirements:

- Show unavailable if BlueZ or adapters are absent.
- Show powered on/off.
- Show connected device count or primary connected device name when available.

Controls:

- Toggle adapter power when supported.
- Open Settings Bluetooth panel for pairing and device management.

### Notifications

Use existing `NotificationCenterModel` as the model foundation.

Requirements:

- Top-bar notification affordance.
- Notification count/unread state.
- Do-not-disturb state.
- Notification center UI.
- Banner UI.
- Dismiss one notification.
- Clear all notifications.
- Respect `notifications.enabled`, `notifications.do_not_disturb`, timeout, history limit, and preview settings.

Future backend:

- Add a real freedesktop notification receive path for normal apps.

### Clipboard history

Use existing `ClipboardHistoryModel` as the model foundation.

Requirements:

- Top-bar clipboard affordance.
- Clipboard picker UI.
- Paste or write selected entry back to clipboard.
- Clear history.
- Respect enabled, persistence, max entries, max text bytes, and primary-selection policy.

Current gap:

- Linux `Application::clipboard()` is currently memory-backed in generic UI code, not a full Wayland clipboard integration.
- Live clipboard observation is missing.

## Top-bar UI changes

Replace fixed four-icon rendering from `SystemStatus` with module-driven rendering from provider state.

Requirements:

- Respect `top_bar.modules`.
- Support at least:
  - `network`
  - `wifi`
  - `bluetooth`
  - `volume` or `audio`
  - `battery` or `power`
  - `notifications`
  - `clipboard`
  - `clock`
- Each status item should be individually clickable.
- Status icons should use typed state:
  - Wi-Fi strength/off/unknown
  - Bluetooth off/on/connected
  - volume muted/low/medium/high
  - battery charging/low/full
  - notifications unread/DND/off
  - clipboard available/empty/disabled
- Compact status should prioritize truth and scanability.
- Unavailable items should be visibly disabled or hidden based on module policy, but never shown as fake active state.

The top bar should remain lightweight. Detailed controls belong in quick settings or dedicated popovers.

## Quick settings UI

Replace the disabled `PopupMenu` with a Flux `Popover`, because quick settings needs sliders, toggles, disabled rows, and Settings links.

Suggested layout:

- Header with title and Settings button.
- Wi-Fi tile: current network, power toggle, Settings link.
- Bluetooth tile: adapter/device state, power toggle, Settings link.
- Volume row: icon, slider, mute button.
- Brightness row: icon, slider, disabled/read-only state when needed.
- Battery/power row: percentage, charging status, power-mode placeholder if unavailable.
- Do-not-disturb tile: toggle.
- Footer or per-row actions to open relevant Settings panels.

Requirements:

- Respect `quick_settings.modules`.
- Keep unavailable controls visibly disabled.
- Keep the popover responsive if one provider is slow or unavailable.
- Do not put long-form configuration in quick settings.

## IPC and actions

Add Shell actions for status UI that need app/window authority:

- `open-settings:network`
- `open-settings:sound`
- `open-settings:bluetooth`
- `open-settings:display`
- `open-settings:notifications`
- `open-settings:clipboard`
- `toggle-dnd`

These can initially route through existing launcher/action model or direct Window Manager launch/focus requests for `lambda-settings` with a target panel once Settings supports panel routing.

Avoid adding a general-purpose untrusted status IPC surface.

## Implementation order

1. Add typed provider state and replace `SystemStatus`-based top-bar rendering with provider snapshots.
2. Thread `ShellConfig` into the live Shell model/controller for:
   - `top_bar.modules`
   - `quick_settings.modules`
   - `top_bar.clock_format`
   - notification and clipboard policies
3. Implement clock provider with configurable formatting.
4. Implement battery provider from sysfs.
5. Implement brightness provider from sysfs as read-only or writable depending on permissions.
6. Replace disabled quick-status `PopupMenu` with quick-settings `Popover`.
7. Implement audio provider, starting with `wpctl` if available.
8. Wire do-not-disturb to existing notification model and expose it in quick settings/top bar.
9. Add notification affordance, notification center, and banner UI.
10. Implement NetworkManager provider for truthful network/Wi-Fi state and Wi-Fi power toggle.
11. Implement BlueZ provider for Bluetooth state and adapter power toggle.
12. Add clipboard picker UI after Linux clipboard integration is real.
13. Add Settings links for every deeper or unavailable control path.
14. Remove or ignore Window Manager `system` snapshot fields once Shell-owned providers cover status.

## Testing strategy

Add deterministic unit tests for:

- Provider snapshot normalization.
- Icon selection for Wi-Fi, Bluetooth, volume, battery, notifications, and clipboard.
- Module ordering from Shell config.
- Unavailable/read-only/writable control state.
- Clock formatting.
- Sysfs battery parsing from fixture directories.
- Sysfs brightness parsing from fixture directories.
- Quick settings model rows from provider snapshots.
- Notification DND behavior.
- Clipboard policy behavior.

Add manual verification for:

- Real battery-backed machine and desktop without a battery.
- Audio volume and mute with PipeWire/WirePlumber.
- Brightness read/write on internal laptop panel and absent external-only monitor case.
- NetworkManager connected/disconnected/Wi-Fi disabled states.
- Bluetooth absent, adapter present off, adapter present on, connected device.
- Notification banner and notification center interactions.
- Clipboard picker privacy and clear-history behavior.

## Readiness criteria

The status bar can be considered daily-driver ready when:

- Every visible status item is backed by real state or clearly marked unavailable.
- Quick settings can control volume, mute, DND, and brightness where supported.
- Network, Wi-Fi, Bluetooth, battery, audio, brightness, notifications, clipboard, and clock respect Shell config.
- Provider failures do not freeze or crash Shell.
- Disabled controls explain their unavailable/read-only state.
- Notifications and clipboard history are Shell-owned and user-controllable.
- The Window Manager no longer carries fake system status as an authoritative path.
