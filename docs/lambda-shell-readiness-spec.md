# Lambda Shell readiness spec

**Date:** 2026-05-25
**Status:** Draft
**Milestone order:** Window Manager first, then Shell, Settings, Files, Terminal, and the remaining desktop pieces.
**Scope:** `lambda-shell`, shared desktop libraries needed by the shell, and the shell-facing IPC contract with `lambda-window-manager`.

## Summary

This milestone turns `lambda-shell` from a working visual shell into a dependable daily workspace layer. The target is not a complete desktop suite. The target is that the user can manually start the Window Manager and Shell, then use the top bar, dock, and command launcher to start, focus, restore, and inspect normal applications for long dogfooding sessions.

The Window Manager remains responsible for KMS, Wayland, composition, window management, focus authority, layer-shell placement, screenshot capture/UI, and client surfaces. The Shell owns persistent desktop chrome and user-facing desktop navigation: top bar, dock, command launcher, app discovery UI, icon presentation, notifications, quick settings, status controls, clipboard history UI, status presentation, and shell preferences.

Session startup/shutdown automation remains out of scope. The user will keep starting and ending sessions manually for now. New log collection and diagnostic infrastructure are also out of scope. Existing diagnostics can be used while developing, but this milestone should not depend on adding new logging systems.

## Current baseline

Implemented today:

- `lambda-shell` is a separate Flux application built from `src/Shell/LambdaShell.cpp`.
- It creates three layer-shell windows: top bar, dock, and command launcher.
- It connects to the Window Manager shell socket and sends `lambda.shell.hello`.
- It receives Window Manager snapshots and updates dock running/focused state.
- The top bar reserves work area through layer-shell exclusive zone.
- The dock can open the command launcher and send launch/focus requests.
- `Super+Space` is captured by the Window Manager and routed to `lambda.shell.openCommandLauncher`.
- The command launcher renders a simple app grid and handles basic typing, backspace, arrows, escape, and enter.
- Shell panels use compositor-backed glass/background-effect rendering.

Important limitations in the current implementation:

- The app list is static in the Shell and the launch command table is hard-coded in the Window Manager.
- Dock item matching uses local alias rules and string searching in the raw snapshot JSON.
- There is no freedesktop `.desktop` parser, no app categories, no keywords, no recency, and no user pin model.
- Dock and launcher icons are hard-coded Material glyphs with hard-coded colors.
- There is no icon theme provider or fallback icon theme contract.
- The top bar status icons are mostly visual. Network, Bluetooth, volume, and battery are not real Shell-owned providers yet.
- There is no quick settings surface for network, Bluetooth, audio, battery, power mode, brightness, do-not-disturb, or related status controls.
- There is no notification daemon, notification banner UI, notification center, or do-not-disturb state.
- There is no clipboard history UI, persistence policy, privacy filtering, or clear-history behavior.
- The command launcher only searches static dock items. It does not search windows, settings panels, shell actions, recent apps, or files.
- The launcher uses manual text-event handling, not a proper text editing model.
- IPC parsing is intentionally small and ad hoc; it should become structured before the protocol grows.
- `lambda-shell` exits if IPC disconnects and throws if it cannot connect at startup.
- Multi-output Shell behavior remains mostly architectural. The current daily target is still the selected single output.

## Additional Shell work identified

These areas should be included in the Shell milestone:

- Move app discovery out of hard-coded Shell and Window Manager tables into a shared app registry.
- Add freedesktop `.desktop` discovery, local Lambda example app discovery, and explicit app aliases.
- Keep Window Manager launch/focus authority, but let Shell own app presentation, search, pins, and icons.
- Add a real dock model: pinned apps, running unpinned apps, disabled entries, attention state, and multi-window behavior.
- Add an icon theme provider for Shell and later apps, including freedesktop theme lookup and bundled fallback behavior.
- Replace snapshot string searching with structured snapshot parsing.
- Add request IDs and structured errors for launch/focus/modal IPC paths.
- Add launcher providers for apps, windows, shell actions, and Settings panels.
- Add launcher ranking, keyboard navigation, empty states, error states, and action confirmation rules.
- Add real top-bar status providers for clock, network, audio, Bluetooth, and battery where available.
- Add quick settings/status controls for the providers Shell owns: network, Bluetooth, audio, battery/power, brightness where available, and do-not-disturb.
- Add Shell-owned notification presentation: banners, notification center/history, app grouping, do-not-disturb, and basic actions.
- Add clipboard history as a Shell-owned user feature backed by a small trusted service/model if needed.
- Add Shell config for dock pins, icon theme, clock format, top-bar modules, launcher behavior, and reduced motion.
- Add accessibility and keyboard navigation pass for top bar, dock, and launcher.
- Add focused Shell tests for model behavior, app discovery, ranking, IPC parsing, and config parsing.

Status update 2026-05-26: the first Shell app-registry and model helpers are in place. They cover deterministic `.desktop` parsing for common fields, installed desktop-file discovery from fixture XDG app dirs, visibility filtering for hidden/no-display/TryExec, Exec token and field-code handling, Lambda app-id alias matching, local `./examples/APP_NAME` executable precedence, installed/local registry merging, icon theme lookup fallback across app/MIME/place contexts, structured Window Manager snapshot parsing with reordered/escaped fields, dock state from pinned apps plus Window Manager snapshots, live Shell model snapshot application, dock click decisions for stopped/running/minimized apps, launcher text/highlight keyboard state, launcher ranking for prefix/acronym/fuzzy/running/recent matches, merged launcher result models for app/window/Settings-panel/Shell-action providers, launcher empty/error states, notification grouping/dismissal/clear-all/do-not-disturb/history limits, clipboard history dedupe/clear/disabled/limit behavior, clipboard max-text-size, primary-selection, and memory-only-by-default persistence policy, Shell IPC request-id parse/serialize round trips, quick-settings available/unknown/unavailable provider ordering, top-bar status module availability classification, Shell config defaults/invalid-value fallback using the spec's `appearance`, `dock`, `top_bar`, `quick_settings`, `notifications`, `clipboard_history`, and `launcher` sections, and generated Shell config serialization. Live service providers and full UI integration remain open.

## Goals

1. Make `lambda-shell` usable as the primary launcher/focus surface for daily dogfooding.
2. Remove hard-coded app launch tables from the Window Manager path in favor of a shared registry contract.
3. Keep app execution authority in the Window Manager or a trusted shared desktop service, not in random app surfaces.
4. Make the dock accurately represent pinned, running, focused, minimized, and attention states.
5. Make the command launcher useful for apps, open windows, Settings panels, and shell actions.
6. Make icon lookup real enough that Shell and future apps can share the same icon theme provider.
7. Make top-bar status values real or explicitly unavailable, never fake.
8. Make quick settings/status controls real for the first useful set of desktop controls.
9. Make notifications usable enough for app banners, history, and do-not-disturb.
10. Make clipboard history usable, private by default, and controllable.
11. Make Shell startup, shutdown, and IPC failure behavior clean without adding session management.
12. Keep the implementation compatible with the selected single-output Window Manager target while not blocking later multi-output work.

## Non-goals

- No session manager, display manager, login, lock screen, logout flow, suspend/reboot UI, or auto-restart supervisor.
- No new log collection, log viewer, trace viewer, or crash-log pipeline.
- No Window Manager geometry, composition, focus, protocol, or rendering work except Shell-facing IPC/schema changes.
- No Settings backend implementation. Shell may define links to Settings panels, but Settings becomes real in the next milestone.
- No Files, Terminal, browser, mail, calendar, music, or trash feature work.
- No full freedesktop notification spec completeness requirement beyond the notification behavior defined here.
- No full system tray/status-notifier implementation in this milestone, unless it is needed for the status/notification controls defined here.
- No multi-output Shell layout beyond preserving a clean model and avoiding hard-coded assumptions that make it impossible later.
- No XWayland policy changes.
- No input method framework. Shell text handling should avoid making IME support harder later, but full IME is outside this milestone.
- No remote search, cloud search, or plugin system.

## Assumptions

- `lambda-window-manager` has completed the Window Manager readiness milestone or at least exposes stable focus, restore, launch, output, and layer-shell behavior.
- The user manually starts `lambda-window-manager`.
- The user manually starts `lambda-shell`.
- The selected output model remains the active desktop target for now.
- Settings may still be edited manually while Shell is being completed.
- Real external Wayland apps such as Firefox, GTK apps, Qt apps, and `foot` can be used for validation.
- Pure Wayland remains the compatibility policy.

## Readiness definition

The Shell is ready for the next milestone when all of these are true:

- The top bar, dock, and command launcher can run for long sessions without visual glitches or unexpected redraw loops.
- The dock shows correct pinned, running, focused, minimized/restorable, disabled, and attention states.
- Clicking a dock app launches it, focuses it, or restores it according to deterministic rules.
- The command launcher opens reliably from `Super+Space`, the dock launcher item, and the top bar brand action.
- The launcher can search and execute app, open-window, Settings-panel, and Shell-action results.
- App discovery includes local Lambda example executables, installed `.desktop` entries, and explicit aliases.
- Shell and Window Manager agree on canonical app ids and app-id matching.
- Icons come from the configured icon theme when possible and from a bundled fallback otherwise.
- The top bar shows real clock/status data or clearly unavailable states.
- Quick settings can show and control the first useful network/audio/Bluetooth/power/brightness/do-not-disturb states where providers are available.
- Notifications can be received, displayed as banners, stored in a notification center, dismissed, cleared, and suppressed by do-not-disturb.
- Clipboard history can record supported text entries, expose a picker, paste a chosen entry, and clear sensitive/history data according to policy.
- IPC messages have structured parsing, request IDs for commands, and structured error feedback.
- Shell failure behavior is clean: missing Window Manager connection and disconnected IPC do not produce confusing crashes.
- Shell config exists, has generated defaults, and is simple enough for Settings to edit later.
- Manual validation covers Lambda apps, external Wayland apps, app launch/focus/restore, launcher keyboard use, quick settings, notifications, clipboard history, and icon/status fallback behavior.

## Architecture decisions

### Shell owns presentation; Window Manager owns authority

The Shell should own:

- app list presentation
- dock pins and ordering
- launcher query, ranking, and providers
- icon lookup and display
- top-bar status modules
- quick settings/status controls
- notification banners and notification center
- do-not-disturb state
- clipboard history UI and policy
- shell preferences
- local UI state

The Window Manager should own:

- actual toplevel/window focus
- app launch permission and execution
- restore/minimize/maximize/close requests
- global shortcut capture
- command launcher modality
- layer-shell placement and work area
- surface snapshots

The shared boundary should be a typed app/window/action protocol. The Shell should not guess that a launch succeeded because it sent a request; it should wait for a Window Manager snapshot or a structured result.

### App registry should be shared

App discovery should become a shared desktop library used by both Shell and Window Manager:

- Shell uses it for dock entries, launcher results, labels, icons, categories, aliases, and search terms.
- Window Manager uses it to validate and execute launch requests.

This avoids one hard-coded table in the Shell and another hard-coded table in the Window Manager. It also avoids trusting the Shell to send arbitrary shell commands as the normal launch path.

Suggested module shape:

```text
src/Desktop/AppRegistry/
  AppEntry.hpp
  AppRegistry.hpp
  AppRegistry.cpp
  DesktopEntryParser.cpp
  ExecParser.cpp
  IconTheme.cpp
```

The exact directory can change, but the concept should be a shared library, not duplicated Shell/Window Manager code.

## Workstreams

### SH-1: Process, IPC, and failure behavior

Problem:

The Shell currently has just enough IPC to work in the happy path. Readiness needs the Shell to fail cleanly, recover state deterministically, and expose enough protocol shape for app launch/focus/restore without ad hoc parsing.

Scope:

- Shell startup when Window Manager socket exists.
- Shell startup when Window Manager socket is missing.
- Shell behavior when IPC disconnects.
- `lambda.shell.hello` and Window Manager welcome handshake.
- Snapshot parsing.
- App launch/focus/restore requests.
- Launcher modal claim/release.
- Structured errors.
- Request IDs.
- Protocol versioning.

Acceptance:

- Starting `lambda-shell` while the Window Manager is available creates all Shell surfaces and receives an initial snapshot.
- Starting `lambda-shell` while the Window Manager is unavailable exits with a clear user-facing error or shows a reconnecting Shell state. It must not throw an uncaught exception with no useful context.
- If IPC disconnects, Shell releases local modal state and either exits cleanly or enters a minimal reconnecting state. No session manager or auto-restart behavior is required.
- Every Shell command request has a `requestId`.
- Window Manager errors include `requestId`, `code`, and `message`.
- Snapshot parsing is structured enough to survive field order changes and escaped strings.
- Unknown optional fields are ignored.
- Missing required fields produce a rejected snapshot and a requested refresh.
- Launcher modal claim/release cannot leave the launcher surface permanently keyboard-interactive after close or disconnect.

Preferred IPC additions:

```json
{
  "type": "lambda.windowManager.launchApp",
  "requestId": "shell-42",
  "appId": "lambda-terminal"
}
```

```json
{
  "type": "lambda.windowManager.error",
  "requestId": "shell-42",
  "code": "not-found",
  "message": "No launchable app registered for lambda-terminal"
}
```

Implementation notes:

- Do not keep growing manual string parsing for complex snapshots. Use a structured parser or a narrow JSON utility with tests.
- No backwards compatibility is required for old internal messages.
- Keep the protocol newline-delimited if that is still convenient.
- Do not add a general-purpose untrusted app IPC surface.

### SH-2: Shared app registry

Problem:

The current app model is split between static Shell dock items and a hard-coded Window Manager command table. That cannot support a daily desktop.

Scope:

- Local Lambda example executable discovery.
- Installed `.desktop` discovery.
- Explicit built-in fallback entries.
- App id canonicalization.
- App aliases.
- App labels and generic names.
- App keywords and categories.
- Launch command resolution.
- Icon name/path resolution metadata.
- Hidden/NoDisplay/TryExec handling.
- Registry refresh.

Acceptance:

- `lambda-files`, `lambda-settings`, and `lambda-terminal` are discovered as first-class apps.
- Local developer executables are preferred before installed versions. For current repo workflows, check `./examples/APP_NAME` first when it exists and is executable.
- Installed `.desktop` apps are discovered from standard XDG application directories.
- App ids are stable across Shell and Window Manager.
- `StartupWMClass`, desktop-entry id, executable name, and Wayland `app_id` aliases can map running windows back to registry entries.
- `NoDisplay=true` entries are hidden from normal app browsing unless explicitly pinned or searched by exact id.
- `Hidden=true` entries are ignored.
- `TryExec` is respected.
- Desktop-entry `Exec` fields are tokenized according to desktop-entry rules, not by blindly passing arbitrary strings to `/bin/sh`.
- Registry refresh can be requested without restarting Shell, even if first implementation uses a manual refresh action.

Suggested app entry:

```cpp
struct AppEntry {
  std::string id;
  std::string name;
  std::string genericName;
  std::string comment;
  std::string icon;
  std::vector<std::string> categories;
  std::vector<std::string> keywords;
  std::vector<std::string> aliases;
  std::vector<std::string> mimeTypes;
  std::vector<std::string> exec;
  std::optional<std::string> workingDirectory;
  bool terminal = false;
  bool noDisplay = false;
  bool hidden = false;
  bool launchable = true;
  bool lambdaBuiltin = false;
};
```

Discovery precedence:

1. Explicit local Lambda development apps:
   - `./examples/lambda-files`
   - `./examples/lambda-settings`
   - `./examples/lambda-terminal`
2. Explicit user Shell config entries.
3. XDG desktop entries from `$XDG_DATA_HOME/applications`.
4. XDG desktop entries from `$XDG_DATA_DIRS/applications`.
5. Conservative built-in fallbacks for browser and terminal only if no real entry exists.

Implementation notes:

- The local example paths should be relative to the current working directory first, matching the current development workflow.
- The old Window Manager `commandForAppId` table should be removed once the shared registry is used.
- Aliases should be data-driven, not hard-coded in multiple places.
- Launch should set `WAYLAND_DISPLAY` to the Window Manager display.

### SH-3: Icon theme provider

Problem:

The Shell currently uses hard-coded glyphs and colors. A daily desktop needs real app icons, file icons later, symbolic icons, and a predictable fallback path.

Scope:

- Freedesktop icon theme lookup.
- Configured icon theme name.
- Theme inheritance.
- Search paths.
- Symbolic and regular icons.
- App icon lookup by desktop entry icon field.
- Fallback app icons.
- Dock/launcher icon rendering.
- Cache invalidation on config/theme change.

Acceptance:

- `lambda-shell` can render app icons from the configured icon theme.
- Absolute icon paths from `.desktop` entries work.
- Named icons from `.desktop` entries resolve through freedesktop icon theme lookup.
- Missing icons render a consistent fallback icon, not an empty square.
- Symbolic icons can be tinted for top-bar/status use.
- Dock and launcher use the same icon provider.
- Settings can later use the same provider without duplicating lookup code.

Preferred config shape:

```toml
[appearance]
icon_theme = "Adwaita"
symbolic_icon_theme = ""
icon_size = 48
```

Implementation notes:

- Cursor theme remains Window Manager config; icon theme belongs to Shell/shared desktop config.
- A bundled fallback icon set is acceptable and probably necessary for fresh installs.
- Hard-coded Material glyph mappings should become fallback-only.

### SH-4: Dock model and behavior

Problem:

The dock looks useful, but it is still mostly static. It needs to become the user's reliable app switcher and launcher.

Scope:

- Pinned item model.
- Running unpinned app items.
- Focused/running/minimized/attention states.
- Disabled launchers.
- Multi-window apps.
- App launch and focus requests.
- Restore minimized windows through Window Manager primitives.
- Tooltip and hover behavior.
- Right-click or long-press context menu model, even if first UI is minimal.
- Dock config persistence.

Acceptance:

- Dock pins load from Shell config.
- Default pins include Files, Terminal, Settings, and a browser when available.
- Browser, mail, calendar, music, and trash entries are not shown as fake launchable apps unless they resolve to actual app entries or explicit disabled placeholders.
- Running apps not pinned appear after pinned apps in a stable section.
- Focused app indicator matches the Window Manager focused window.
- Minimized app windows remain represented and can be restored by clicking the dock item.
- Clicking an app with no running windows sends `launchApp`.
- Clicking an app with one running window sends `focusApp` or `focusWindow`.
- Clicking an app with multiple windows focuses the most recently used window; a later context menu can expose the full window list.
- Launch errors leave the Shell responsive and show a launcher/dock error state.
- Hover, press, running indicator, disabled state, and attention state are visually distinct and not color-only.

Suggested config:

```toml
[dock]
position = "bottom"
auto_hide = false
show_running_unpinned = true
show_tooltips = true
pinned = ["lambda-files", "lambda-terminal", "lambda-settings", "firefox"]
```

Implementation notes:

- The dock does not need to reserve work area in this milestone.
- Keep dock dimensions stable so appearing running apps do not create distracting jumps beyond expected width changes.
- Avoid fake entries for apps that are not installed. Disabled placeholders are acceptable only if clearly disabled.
- Trash can stay deferred until Files implements trash behavior.

### SH-5: Command launcher

Problem:

The launcher is the main keyboard path into the desktop. It currently searches static dock entries only.

Scope:

- Query text model.
- Keyboard navigation.
- App provider.
- Open windows provider.
- Settings panels provider.
- Shell actions provider.
- Ranking.
- Empty states.
- Error states.
- Result categories.
- Action execution.
- Confirmation for dangerous actions.
- Reduced-motion handling.

Acceptance:

- `Super+Space` opens the launcher and focuses the query.
- Dock launcher and top-bar brand action open the launcher.
- `Escape` closes the launcher and releases modal focus.
- Typing, backspace, delete, arrow up/down, enter, home/end, and paste work through a real text model or equivalent tested handling.
- Empty query shows recommended apps and recent actions.
- App results come from the shared app registry.
- Window results come from Window Manager snapshots and include current window title/app.
- Settings panel results exist as actions such as "Appearance", "Keyboard", "Dock", and "Display", even if Settings backends are implemented in the next milestone.
- Shell action results include at least "Reload app registry", "Open Settings", and "Open Terminal".
- Destructive/session actions are absent or disabled in this milestone.
- Ranking supports exact prefix, word prefix, acronym, fuzzy subsequence, running boost, recent boost, and provider priority.
- Result activation sends a typed action request and waits for success/error feedback where applicable.
- Launch/focus errors are visible in the launcher without crashing or silently closing it.

Suggested result model:

```cpp
struct LauncherResult {
  std::string id;
  std::string providerId;
  std::string title;
  std::string subtitle;
  std::string icon;
  float score = 0.f;
  LauncherAction action;
  std::string category;
  bool disabled = false;
  bool danger = false;
};
```

Implementation notes:

- The UI can remain visually close to the current grid, but the model should support dense rows because window and settings results need subtitles.
- Provider execution can be synchronous at first if it is fast; the interface should allow async/cancellable providers.
- Do not execute raw shell commands from query text in this milestone.

### SH-6: Top bar and status modules

Problem:

The top bar is currently mostly static. For daily use, it should show truthful system state and provide obvious entry points to Shell, Settings, quick settings, notifications, and clipboard history.

Scope:

- Clock.
- Active app/window title.
- Network status.
- Bluetooth status.
- Volume/audio status.
- Battery/power status.
- Status unavailable states.
- Top-bar module ordering.
- Basic click actions.
- Entry points for quick settings, notification center, and clipboard history.
- Simple status popovers where useful.

Acceptance:

- Clock updates without forcing unnecessary redraws.
- Clock format is configurable.
- Active title reflects the focused Window Manager toplevel, with sensible fallback when none is focused.
- Network status is real when NetworkManager or system interfaces are available, otherwise `unavailable`.
- Volume status is real when PipeWire/PulseAudio tooling or API is available, otherwise `unavailable`.
- Battery status is real on battery-backed systems through sysfs or UPower, otherwise `unavailable`.
- Bluetooth status is real when available, otherwise `unavailable`.
- The top bar never displays fake "connected", fake battery percentage, or fake volume values.
- Clicking the brand opens the command launcher.
- Clicking a status cluster opens the matching quick-settings/status control surface.
- Clicking the notification affordance opens notification center.
- Clicking the clipboard affordance opens clipboard history when enabled.
- Clicking Settings-related status affordances can open Settings panel actions, even if detailed Settings backends land later.

Suggested config:

```toml
[top_bar]
clock_format = "%a %d %b, %H:%M"
show_active_title = true
modules = ["network", "bluetooth", "volume", "battery", "notifications", "clipboard", "clock"]
```

Implementation notes:

- Shell should own system status providers. The Window Manager should not become an audio/network/battery backend.
- Providers should be cheap and event-driven where practical. Polling is acceptable initially if intervals are conservative.
- Top-bar status providers should be the same providers used by quick settings, not separate duplicate probes.

### SH-7: Quick settings and status controls

Problem:

Status indicators are not enough for a daily desktop. The Shell should provide the first useful status control surface for common repeated actions, while Settings remains the deeper configuration app.

Scope:

- Quick settings popover.
- Network state and basic connect/disconnect affordance where a backend exists.
- Bluetooth state and basic enable/disable affordance where a backend exists.
- Audio volume and mute.
- Microphone mute if available.
- Battery/power state.
- Power mode if available.
- Brightness if available.
- Do-not-disturb toggle.
- Open relevant Settings panel actions.
- Unavailable/provider-missing states.

Acceptance:

- Quick settings opens from the top bar.
- Providers report `available`, `unavailable`, `unknown`, and concrete values distinctly.
- Volume can be viewed and adjusted when PipeWire/PulseAudio support is available.
- Mute can be toggled when an audio backend is available.
- Network status is truthful and can link to Settings even if full network management is deferred.
- Bluetooth status is truthful and can toggle adapter power only if a real backend exists.
- Battery/power status is truthful and never fake.
- Brightness control appears only when a real backend exists.
- Do-not-disturb state affects notification banner presentation immediately.
- Unavailable controls are visibly disabled and do not pretend to work.
- Quick settings stays responsive if one provider is slow or unavailable.

Suggested config:

```toml
[quick_settings]
modules = ["network", "bluetooth", "audio", "battery", "brightness", "do_not_disturb"]
```

Implementation notes:

- Prefer small provider interfaces that Settings can also query later.
- Do not put long-form configuration in quick settings.
- Avoid privileged operations unless the backend and permission path are explicit.

### SH-8: Notifications

Problem:

Applications need a desktop-owned way to show transient information, and the user needs control over interruption and history. Shell owns the visible notification experience.

Scope:

- Notification receive API or local service boundary.
- Notification banners.
- Notification center/history.
- App identity and icon lookup.
- App grouping.
- Dismiss.
- Clear all.
- Do-not-disturb.
- Basic actions.
- Expiration timeout.
- Urgency.
- Persistence policy.
- Privacy policy.

Acceptance:

- Shell can receive notifications from trusted desktop components and, when protocol support exists, normal Wayland apps.
- Banners appear above normal windows without stealing focus.
- Banners use app icon/name from the app registry where possible.
- Notifications are stored in a notification center until dismissed, expired by policy, or cleared.
- User can dismiss one notification.
- User can clear all notifications.
- Do-not-disturb suppresses banners while preserving history according to policy.
- Urgent notifications can bypass do-not-disturb only if the policy explicitly allows it.
- Notification actions can be displayed and invoked for supported notifications.
- Sensitive notification content can be hidden by policy.
- Missing app metadata falls back cleanly.

Suggested config:

```toml
[notifications]
enabled = true
do_not_disturb = false
banner_timeout_seconds = 6
history_limit = 100
show_previews = true
```

Implementation notes:

- Full freedesktop notification spec parity can come later, but the Shell ownership and user model should be correct now.
- Notification center UI belongs to Shell, not Settings.
- Settings will later edit notification preferences.

### SH-9: Clipboard history

Problem:

Clipboard history is a repeated desktop workflow and should be owned by Shell at the user-facing level. It also needs privacy rules from the start.

Scope:

- Text clipboard history.
- Optional primary selection history policy.
- Clipboard picker UI.
- Paste selected history item.
- Pin/favorite later if cheap.
- Clear item.
- Clear all.
- Persistence policy.
- Sensitive app/type exclusions.
- Maximum entries.
- Maximum entry size.
- Private mode/do-not-record toggle.

Acceptance:

- Shell records text clipboard entries when clipboard history is enabled.
- Duplicate consecutive entries are coalesced.
- Very large entries are skipped or truncated according to documented policy.
- Clipboard history picker opens from a shortcut and/or top-bar affordance.
- Choosing an item makes it the current clipboard and can request paste where supported.
- User can delete one entry.
- User can clear all entries.
- User can disable clipboard history.
- History is memory-only by default unless persistence is explicitly enabled.
- Password/sensitive exclusions are supported where the source advertises sensitivity; otherwise the policy is documented honestly.
- Non-text formats are ignored or represented as unsupported in this milestone.

Suggested config:

```toml
[clipboard_history]
enabled = true
persist = false
max_entries = 100
max_text_bytes = 1048576
record_primary_selection = false
```

Implementation notes:

- The Shell owns the UI and policy. A small trusted clipboard-history service/model can back it internally if that keeps clipboard protocol handling cleaner.
- Do not make clipboard history a Window Manager feature unless low-level protocol mechanics force a helper there.
- Never silently persist clipboard history without an explicit preference.

### SH-10: Shell config and preferences

Problem:

Settings will edit Shell preferences later. The Shell needs a stable, documented config file first.

Scope:

- Config path.
- Default generation.
- Dock pins.
- Icon theme.
- Clock format.
- Top-bar modules.
- Quick settings modules.
- Notification preferences.
- Clipboard history preferences.
- Launcher behavior.
- Reduced motion.
- App registry extra entries and aliases.
- Hot reload boundaries.

Acceptance:

- Shell creates a default config when none exists.
- Config path follows this order:
  - `--config PATH`
  - `LAMBDA_SHELL_CONFIG`
  - `$XDG_CONFIG_HOME/lambda-shell/config.toml`
  - `$HOME/.config/lambda-shell/config.toml`
- Invalid values fall back safely.
- Hot-reloadable keys are documented.
- Restart-required keys are documented.
- Settings can later write the config without reverse engineering the Shell.

Suggested default config:

```toml
[appearance]
icon_theme = ""
symbolic_icon_theme = ""
icon_size = 48
reduced_motion = false

[dock]
position = "bottom"
auto_hide = false
show_running_unpinned = true
show_tooltips = true
pinned = ["lambda-files", "lambda-terminal", "lambda-settings", "firefox"]

[top_bar]
clock_format = "%a %d %b, %H:%M"
show_active_title = true
modules = ["network", "bluetooth", "volume", "battery", "notifications", "clipboard", "clock"]

[quick_settings]
modules = ["network", "bluetooth", "audio", "battery", "brightness", "do_not_disturb"]

[notifications]
enabled = true
do_not_disturb = false
banner_timeout_seconds = 6
history_limit = 100
show_previews = true

[clipboard_history]
enabled = true
persist = false
max_entries = 100
max_text_bytes = 1048576
record_primary_selection = false

[launcher]
empty_query = "recommended"
max_results = 12
show_categories = true
```

Implementation notes:

- Keep config values platform-neutral where possible.
- Do not put Window Manager rendering values in Shell config.
- Do not make Settings the source of truth yet; Settings will become an editor later.

### SH-11: Visual quality and layout

Problem:

The Shell should feel integrated with the Window Manager glass/chrome work while remaining practical and readable.

Scope:

- Top-bar layout.
- Dock layout.
- Launcher layout.
- Quick settings layout.
- Notification banner and center layout.
- Clipboard history picker layout.
- Glass material usage.
- Icon rendering.
- Text contrast.
- Small-output behavior.
- Fractional scale behavior.
- Reduced motion.

Acceptance:

- Top bar, dock, and launcher use the same background-effect path as other Shell panels.
- Quick settings, notification center, and clipboard history use the same Shell glass/material language.
- Text remains readable over light and dark wallpapers.
- Dock icons and labels never overlap.
- Launcher results fit on small laptop outputs without clipped text.
- Quick settings controls fit on small laptop outputs without overlapping.
- Notification banners do not cover the top bar or command launcher in incoherent ways.
- Clipboard history entries clip/wrap predictably.
- Fractional scale does not produce blurry text or misaligned hit targets beyond existing framework limits.
- Reduced motion disables or shortens Shell hover/open animations.
- Top bar and dock do not redraw continuously while idle.

Implementation notes:

- Do not add decorative UI that competes with app content.
- Keep the Shell quiet and utilitarian. The main screen is a workspace, not a landing page.
- Stable dimensions are preferred for dock cells, top-bar modules, and launcher rows.

### SH-12: Accessibility and keyboard behavior

Problem:

The Shell is a primary navigation surface. It must be usable without precise pointer interactions.

Scope:

- Keyboard focus.
- Launcher text input.
- Launcher result navigation.
- Dock keyboard navigation.
- Top-bar keyboard navigation.
- Accessible names.
- Focus visibility.
- Color-independent state.
- Reduced motion.

Acceptance:

- Launcher can be fully operated by keyboard.
- Dock can be reached and operated by keyboard through a documented shortcut or focus path.
- Top-bar interactive controls have accessible labels in the Flux accessibility model where available.
- Focus rings are visible on top-bar controls, dock items, and launcher results.
- Running/focused/attention states are not conveyed only by color.
- Reduced motion is respected.

Implementation notes:

- Full screen-reader integration may depend on broader Flux accessibility work and can be deferred if needed.
- The Shell should still structure controls so real accessibility hooks can be added without rewriting the UI.

### SH-13: Tests and validation

Problem:

Shell behavior is easy to regress because it combines async IPC, app discovery, UI state, and external desktop files.

Scope:

- App registry tests.
- Desktop entry parser tests.
- Exec parser tests.
- Icon theme lookup tests.
- Dock model tests.
- Launcher ranking tests.
- IPC parser/serializer tests.
- Config parser tests.
- Quick settings provider tests.
- Notification model tests.
- Clipboard history policy/model tests.
- Manual smoke checklist.

Acceptance:

- Unit tests cover app id matching and alias resolution.
- Unit tests cover local Lambda app discovery precedence.
- Unit tests cover `.desktop` parsing for common fields and escaped Exec values.
- Unit tests cover hidden/no-display/try-exec behavior.
- Unit tests cover launcher ranking for prefix, acronym, fuzzy, running, and recent boosts.
- Unit tests cover dock state from Window Manager snapshots.
- Unit tests cover malformed IPC and config fallback.
- Unit tests cover quick settings unavailable/available provider states.
- Unit tests cover notification grouping, dismissal, clear-all, do-not-disturb, and history limits.
- Unit tests cover clipboard history dedupe, clear, size limit, persistence policy, and disabled state.
- Manual checks validate real apps and real icon themes on the target machine.

Implementation notes:

- Avoid screenshot-based Shell tests for this milestone unless existing render fixtures make it cheap.
- Prefer deterministic model tests for most Shell behavior.

## Implementation order

1. Stabilize IPC and structured snapshot parsing.

   Add request IDs, structured errors, and robust snapshot parsing before expanding behavior.

2. Build shared app registry and migrate launch tables.

   Discover local Lambda examples, parse installed `.desktop` entries, and remove duplicated hard-coded launch tables.

3. Add icon theme provider.

   Resolve app icons and replace hard-coded dock/launcher glyphs with theme-backed icons plus fallback.

4. Rebuild dock model.

   Load pins from Shell config, append running unpinned apps, handle minimized/restore behavior, and expose launch/focus errors.

5. Expand command launcher.

   Add providers, ranking, keyboard behavior, settings actions, empty states, and error states.

6. Add top-bar status providers.

   Make clock/title/status truthful and configurable.

7. Add quick settings/status controls.

   Reuse top-bar providers for the first useful controls and unavailable states.

8. Add notifications.

   Add banners, notification center/history, do-not-disturb, grouping, dismissal, and clear-all.

9. Add clipboard history.

   Add text history, picker, paste, clear, limits, and privacy policy.

10. Add Shell config generation and hot reload.

   Document keys and restart requirements.

11. Add tests and update docs.

   Cover registry, icons, dock, launcher, quick settings, notifications, clipboard history, IPC, config, and manual validation.

## Manual validation checklist

### Build and unit checks

```sh
cmake --build build --target flux_tests
./build/flux_tests --test-case="*shell*"
./build/flux_tests --test-case="*desktop*"
cmake --build build
git diff --check
```

### Launch checks

```sh
./build/lambda-window-manager
./build/lambda-shell
```

Expected:

- Top bar appears and reserves work area.
- Dock appears above windows.
- `Super+Space` opens the launcher.
- Dock launcher opens the launcher.
- Top-bar brand opens the launcher.
- Closing the launcher releases keyboard focus.
- Exiting Shell does not kill app windows.

### App registry checks

Validate discovery for:

- `lambda-files`
- `lambda-settings`
- `lambda-terminal`
- Firefox or installed browser
- `foot` or installed terminal
- one GTK app
- one Qt app if installed

Expected:

- Local Lambda example executables are preferred when present.
- Installed apps show correct names.
- Missing apps do not appear as fake enabled dock entries.
- App icons resolve or use fallback.
- Launch requests start the expected app.

### Dock checks

Validate:

- pinned app launch
- focus existing app
- restore minimized app
- focused indicator
- running indicator
- unpinned running app display
- disabled placeholder behavior
- multi-window app focus policy
- launch error state

### Launcher checks

Validate:

- empty query recommended results
- app search
- window search
- settings panel search
- shell action search
- prefix/acronym/fuzzy matching
- arrow navigation
- enter activation
- escape close
- backspace/delete editing
- paste if supported by Flux text input path
- launch/focus error feedback

### Top-bar checks

Validate:

- clock updates.
- active title follows focused window.
- network state is real or unavailable.
- Bluetooth state is real or unavailable.
- volume state is real or unavailable.
- battery state is real or unavailable.
- clicking brand opens launcher.
- clicking quick settings/status cluster opens quick settings.
- clicking notification affordance opens notification center.
- clicking clipboard affordance opens clipboard history when enabled.
- clicking Settings-related status affordance opens or requests the matching Settings panel when available.

### Quick settings checks

Validate:

- quick settings open/close
- network state available/unavailable
- Bluetooth state available/unavailable
- volume display
- mute toggle if available
- battery/power state
- brightness control if available
- do-not-disturb toggle
- Settings panel links

Expected:

- Available controls work.
- Unavailable controls are disabled and truthful.
- Do-not-disturb immediately affects notification banners.

### Notification checks

Validate:

- receive notification
- banner display
- dismiss banner
- notification center history
- grouped app notifications
- clear one notification
- clear all notifications
- do-not-disturb suppresses banners
- notification action if available
- sensitive preview policy

Expected:

- Notifications appear without stealing focus.
- History and dismissal behavior is deterministic.
- Missing app metadata uses fallback icon/name.

### Clipboard history checks

Validate:

- copy text entry
- duplicate copy coalescing
- large entry policy
- picker open/close
- choose entry
- delete one entry
- clear all entries
- disable history
- memory-only default
- persistence enabled if supported

Expected:

- Clipboard history follows privacy policy.
- Disabled history records nothing.
- Choosing an entry makes it available for paste.

### Config checks

Use at least these config variants:

- default generated config
- custom pinned apps
- empty pinned apps
- valid icon theme
- invalid icon theme
- custom clock format
- quick settings module order
- notifications enabled/disabled
- do-not-disturb default state
- clipboard history enabled/disabled
- clipboard history persistence disabled by default
- reduced motion enabled
- `show_running_unpinned = false`
- reordered top-bar modules
- invalid config values

Expected:

- Valid values apply.
- Invalid values fail safely.
- Missing icon/theme values use fallback.
- Restart requirements are clear.

## Test additions

Add focused automated tests where behavior is deterministic:

- Shell IPC serialize/parse with request IDs.
- Snapshot parser with escaped strings and reordered fields.
- App registry local example precedence.
- Desktop entry parser for `Name`, `GenericName`, `Comment`, `Icon`, `Exec`, `TryExec`, `NoDisplay`, `Hidden`, `Categories`, `Keywords`, `MimeType`, and `StartupWMClass`.
- Exec parser field-code handling.
- App alias matching from running Wayland `app_id`.
- Dock model from pinned apps plus running windows.
- Dock click behavior for stopped, running, minimized, and multi-window apps.
- Launcher provider merge and ranking.
- Launcher empty/error states.
- Quick settings provider states.
- Notification model grouping, dismissal, do-not-disturb, and history limits.
- Clipboard history dedupe, clear, disabled state, size limit, and persistence policy.
- Icon theme lookup fallback.
- Shell config defaults and invalid-value fallback.

## Done checklist

- [ ] Shell IPC uses request IDs and structured errors.
- [ ] Shell snapshot parsing is structured and tested.
- [ ] Shared app registry exists and is used by Shell and Window Manager launch path.
- [ ] Local Lambda example apps are discovered with `./examples/APP_NAME` precedence.
- [ ] Installed `.desktop` apps are discovered.
- [ ] Dock pins load from Shell config.
- [ ] Running unpinned apps appear in the dock.
- [ ] Dock click launches, focuses, or restores correctly.
- [ ] Icon theme provider is used by dock and launcher.
- [ ] Missing icons have a consistent fallback.
- [ ] Command launcher supports app, window, Settings-panel, and Shell-action providers.
- [ ] Launcher ranking and keyboard navigation are tested.
- [ ] Top-bar status values are real or explicitly unavailable.
- [ ] Quick settings/status controls are real or explicitly unavailable.
- [ ] Notifications support banners, notification center/history, dismissal, clear-all, and do-not-disturb.
- [ ] Clipboard history supports text entries, picker, paste, clear, limits, and privacy policy.
- [ ] Shell config is generated and documented.
- [ ] Shell handles missing/disconnected IPC cleanly without adding session management.
- [ ] Manual validation passes with Lambda apps and selected external apps.
- [ ] User guide and Shell spec are updated to match actual behavior.

## Deferred to later milestones

- Session wrapper, Shell auto-start, Shell auto-restart, login, lock, logout, suspend/reboot UI.
- New log collection or trace infrastructure.
- Full Settings backend and Settings UI integration.
- Files app operations and trash implementation.
- Terminal scrollback, clipboard, and terminal preferences.
- Full freedesktop notification spec parity.
- Full system tray/status notifier beyond the Shell-owned status controls.
- Full multi-output Shell layout.
- Files/recent documents launcher provider.
- Calculator and plugin launcher providers.
- Input method support.
- XWayland compatibility work.
