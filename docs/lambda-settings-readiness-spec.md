# Lambda Settings readiness spec

**Date:** 2026-05-25
**Status:** Draft
**Milestone order:** Window Manager first, then Shell, Settings, Files, Terminal, and the remaining desktop pieces.
**Scope:** `lambda-settings`, shared settings/config libraries needed by it, and the supported configuration surfaces for `lambda-window-manager`, `lambda-shell`, and common Lambda desktop preferences.

## Summary

This milestone turns `lambda-settings` from a polished visual placeholder into the real control panel for the desktop pieces that already exist. The target is not a complete system administration app. The target is that the user can change the current Window Manager and Shell settings from a GUI instead of editing config files by hand, see truthful system information, and understand which changes apply immediately versus after restart.

The Settings app should not become a second source of truth. It should edit the same config contracts used by the Window Manager and Shell readiness specs, validate values before writing them, write files atomically, and request live reload only for settings that the owning process supports. If a value belongs to the Window Manager, the Window Manager remains the authority. If a value belongs to the Shell, the Shell remains the authority. Settings is the editor and status surface.

Session startup/shutdown automation remains out of scope. The user will keep manually starting and ending sessions for now. New log collection and diagnostic infrastructure are also out of scope.

## Current baseline

Implemented today:

- `lambda-settings` is a native Flux app under `examples/lambda-settings`.
- It opens a resizable window with a system titlebar and glass background.
- It has a polished sidebar and content layout.
- It has sections for General, Appearance, Desktop, Dock & Panel, Workspaces, Privacy, Notifications, Power, and About.
- Appearance has local controls for theme mode, accent color, wallpaper choice, transparency, radius, reduced motion, and high contrast.
- The UI handles local selection state reactively.
- The About page and most other sections display plausible rows.

Important limitations:

- Most values are mock values.
- Controls do not read or write Window Manager config.
- Controls do not read or write Shell config.
- Controls do not persist across app restarts.
- There is no shared settings backend, config parser/writer, or schema metadata.
- There is no display/output backend despite Window Manager support for selected output and scale.
- There is no keyboard/input settings backend.
- There is no cursor theme or icon theme editor.
- There is no real wallpaper picker.
- There is no keybinding editor.
- There is no live apply/restart-required distinction.
- There is no real system information provider for About.
- There are no error states for failed writes, invalid config, unavailable services, or permission problems.

## Additional Settings work identified

These areas should be included in the Settings milestone:

- Build a settings backend that can load, validate, edit, and atomically write Window Manager and Shell config files.
- Add schema metadata so the UI can know labels, value types, ranges, defaults, validation errors, and whether a setting is hot-reloadable.
- Add a real Appearance section for theme mode, accent, wallpaper/background, glass material, borders, shadows, radius, cursor theme, icon theme, font, reduced motion, and high contrast.
- Add a real Display section for selected output, scale, output metadata, and restart-required output changes.
- Add a real Keyboard section for keyboard layout, variant, options, repeat rate, repeat delay, and shortcuts.
- Add a real Dock & Panel section for Shell dock pins, running-unpinned behavior, icon size, clock format, and top-bar modules.
- Add a real Desktop section for wallpaper mode, background color/gradient, animations, idle blanking, and default screenshot behavior.
- Add a truthful About section backed by system and build metadata.
- Add read-only or limited-control system pages for Power, Network, Audio, Bluetooth, Notifications, and Privacy, with unavailable states instead of fake values.
- Add apply/revert/error UX that is explicit enough to prevent silent config corruption.
- Add tests for config read/write, validation, schema metadata, and settings model behavior.

Status update 2026-05-26: a first `SettingsBackend` is in place with deterministic schema descriptors, apply-mode metadata, default extraction, validation for color/number/enum/path/shortcut values, dirty/revert/reset/save model state, Window Manager TOML load/write while preserving unknown keys, Shell TOML load/write for the current Shell config contract while preserving unknown keys, atomic file writes including write-failure preservation tests, shortcut conflict detection, wallpaper path normalization, theme discovery plus missing-theme fallback status, fixture-based system-info parsing/formatting, and About-page system rows that show real kernel/architecture/memory data plus explicit unavailable states instead of fake CPU/storage/display values. Full UI binding and richer provider discovery remain open.

## Goals

1. Make Settings the GUI editor for current Window Manager and Shell configuration.
2. Remove mock data from the primary pages that users will rely on daily.
3. Persist user choices through the same config files the owning processes read.
4. Clearly distinguish immediate, next-window, next-client, and restart-required changes.
5. Provide safe validation and atomic writes.
6. Give users a truthful view of display, input, theme, dock, panel, and system basics.
7. Keep the Settings UI responsive and usable even when the Window Manager, Shell, or system providers are unavailable.
8. Make the settings model testable without launching a GUI.

## Non-goals

- No session manager, login, lock screen, logout UI, suspend/reboot UI, or auto-start management.
- No new log collection, log viewer, trace viewer, or crash-log pipeline.
- No Window Manager implementation work except consuming the config/schema contracts already defined by the Window Manager milestone.
- No Shell implementation work except consuming the config/schema contracts already defined by the Shell milestone.
- No Files app operations, trash behavior, open-with UI, or MIME management beyond settings entries needed later.
- No Terminal preferences beyond exposing placeholder navigation for a later Terminal milestone.
- No full network manager UI.
- No full Bluetooth pairing UI.
- No full audio mixer UI.
- No notification daemon or notification center implementation.
- No users/accounts administration.
- No package/update manager.
- No portal permissions framework.
- No full accessibility framework. Settings should expose current accessibility-related toggles only when there is a backend.
- No multi-output layout editor unless the Window Manager milestone has added full multi-output support. Selected-output and scale are in scope.

## Assumptions

- The Window Manager readiness milestone defines stable config keys for output, scale, cursor, keyboard, chrome, glass, wallpaper, animations, keybindings, idle blanking, and screenshots.
- The Shell readiness milestone defines stable config keys for dock pins, icon theme, top-bar modules, launcher behavior, reduced motion, and app registry preferences.
- The user manually starts `lambda-window-manager`, `lambda-shell`, and `lambda-settings`.
- The first useful Settings milestone can write user config files without requiring a privileged daemon.
- Some changes require process restart. Settings should say so plainly instead of pretending they applied live.
- Pure Wayland remains the compatibility policy.

## Readiness definition

Settings is ready for the Files milestone when all of these are true:

- It loads current Window Manager and Shell config values on startup.
- It creates missing default config files through the same defaults as the owning components.
- It writes valid changed values atomically.
- It preserves unrelated and unknown config values where practical.
- It shows validation errors before writing invalid values.
- It clearly marks restart-required changes.
- It can request live reload for hot-reloadable Window Manager and Shell settings where the owning process supports it.
- Appearance, Display, Keyboard, Dock & Panel, Desktop, and About pages contain real data and real controls.
- Power, Network, Audio, Bluetooth, Notifications, Privacy, and Workspaces either contain real limited data or are explicitly unavailable/deferred.
- It never displays fake system values as if they were real.
- It remains usable when the Shell is not running, when the Window Manager socket is unavailable, or when system providers are unavailable.
- It has tests for settings schemas, config read/write, validation, and model state.

## Architecture decisions

### Settings edits owner config

Settings should not own Window Manager or Shell settings. It should edit owner config files:

- Window Manager config:
  - `--config PATH` when launched with an explicit target.
  - `LAMBDA_WINDOW_MANAGER_CONFIG` when set.
  - `$XDG_CONFIG_HOME/lambda-window-manager/config.toml`.
  - `$HOME/.config/lambda-window-manager/config.toml`.
- Shell config:
  - `--shell-config PATH` when launched with an explicit target.
  - `LAMBDA_SHELL_CONFIG` when set.
  - `$XDG_CONFIG_HOME/lambda-shell/config.toml`.
  - `$HOME/.config/lambda-shell/config.toml`.
- Settings app config:
  - `--config PATH` for Settings app preferences if needed.
  - `LAMBDA_SETTINGS_CONFIG` when set.
  - `$XDG_CONFIG_HOME/lambda-settings/config.toml`.
  - `$HOME/.config/lambda-settings/config.toml`.

The app may also use an internal state file for non-authoritative UI preferences such as last selected section.

### Shared config/schema library

The config parser/writer should be shared with the owning components where reasonable. At minimum, the schemas should be shared so Settings cannot drift from the actual runtime config.

Suggested module shape:

```text
src/Desktop/Settings/
  SettingsSchema.hpp
  SettingsSchema.cpp
  SettingsStore.hpp
  SettingsStore.cpp
  TomlSettingsDocument.hpp
  TomlSettingsDocument.cpp
  SystemInfoProvider.hpp
  SystemInfoProvider.cpp
```

The exact directory can change. The important point is that schema and persistence are not buried in `examples/lambda-settings/SettingsApp.hpp`.

### Apply modes

Each setting should declare an apply mode:

```cpp
enum class ApplyMode {
  Immediate,
  NextWindow,
  NextClient,
  RestartRequired,
  ReadOnly,
  Unsupported,
};
```

Examples:

- Wallpaper/background color: immediate if Window Manager hot reload supports it.
- Window glass material: immediate if Window Manager hot reload supports it.
- Output selector: restart-required unless Window Manager later supports live output switch.
- Scale: immediate only if Window Manager and clients can safely update; otherwise restart-required.
- Dock pins: immediate if Shell hot reload supports it.
- Icon theme: immediate if Shell can reload icons; otherwise Shell restart-required.
- Keyboard layout: immediate only if Window Manager can rebuild and broadcast keymap safely.
- About hardware info: read-only.

### Writes must be safe

Settings should never partially corrupt config files.

Acceptance for writes:

- Parse current file.
- Apply edits to the parsed document.
- Validate resulting document.
- Write to a temporary file in the same directory.
- Flush and rename atomically.
- Preserve file permissions where practical.
- Surface write errors in the UI.
- Avoid deleting unknown keys unless the user explicitly resets a whole section.

## Workstreams

### ST-1: Settings model, schema, and persistence

Problem:

The current app stores local UI state only. It needs a real model that can load, validate, modify, persist, and apply settings owned by other components.

Scope:

- Settings schema metadata.
- TOML load/write.
- Default config creation.
- Unknown key preservation.
- Value validation.
- Dirty state.
- Apply modes.
- Save/revert/reset behavior.
- Error model.
- Backend tests.

Acceptance:

- Settings can load Window Manager, Shell, and Settings app config documents.
- Missing config files are created through owner defaults or a shared default generator.
- Every displayed setting has a schema entry with key path, owner, type, default, validation, and apply mode.
- Invalid config values are shown with warnings and fallback values.
- UI changes mark the model dirty.
- Saving writes all valid changes atomically.
- Revert restores the last loaded values.
- Reset-to-default works for an individual setting and for a page.
- Unknown config keys are preserved.
- Tests cover read, write, validation, unknown preservation, default creation, and atomic write failure.

Suggested schema entry:

```cpp
struct SettingDescriptor {
  std::string id;
  std::string owner;
  std::vector<std::string> keyPath;
  SettingType type;
  SettingValue defaultValue;
  SettingValue minValue;
  SettingValue maxValue;
  ApplyMode applyMode;
  std::string label;
  std::string help;
};
```

Implementation notes:

- Start with explicit hand-written descriptors. Do not overbuild a generic settings engine.
- Keep the UI model separate from the visual components.
- Prefer immediate apply for clearly hot-reloadable values, but never silently claim immediate apply for uncertain values.

### ST-2: Window Manager settings integration

Problem:

The Window Manager already has the most important daily-driver controls, but the user still has to edit TOML by hand.

Scope:

- Output selector.
- Scale and per-output scale.
- Wallpaper path.
- Wallpaper mode.
- Background color.
- Background gradient.
- Window glass toggle.
- Chrome glass material.
- Border color/width.
- Shadow color/radius/offset.
- Window corner radii per corner.
- Animations.
- Hardware cursor.
- Cursor theme.
- Cursor size.
- Idle blank timeout.
- Keyboard layout/variant/model/options.
- Keyboard repeat.
- Keybindings.
- Screenshot folder or screenshot behavior if/when configurable.
- Popup grab flag if still exposed.

Acceptance:

- Settings reads the current Window Manager config.
- Appearance page edits wallpaper/background, glass material, border, shadow, radius, theme-adjacent values, cursor theme, and cursor size.
- Display page lists selected output metadata and edits selected output and scale according to the current single-output model.
- Keyboard page edits layout, variant, model, options, repeat rate, repeat delay, and Window Manager shortcuts.
- Desktop page edits animations and idle blanking.
- Invalid output/scale/cursor/keyboard values are blocked before save.
- Restart-required values are clearly marked.
- If the Window Manager is running and supports hot reload for a setting, Settings can request or trigger apply without manual restart.
- If the Window Manager is not running, Settings still edits the config and marks changes as applying next launch.

Implementation notes:

- Do not duplicate Window Manager default constants in the UI. Import or generate from shared schema.
- Output listing can initially come from `lambda-window-manager --list-outputs` only if it is robust and non-disruptive. A cleaner IPC/status path is preferred later.
- Keybinding editing can start with a focused subset: launcher, screenshots, snap/maximize/restore, close, minimize, focus cycle, and terminate.

### ST-3: Shell settings integration

Problem:

After the Shell milestone, Shell config should be user-editable. Settings is the right control surface for dock, panel, launcher, icon theme, and Shell behavior.

Scope:

- Dock pins.
- Dock position if supported.
- Dock auto-hide if supported.
- Show running unpinned apps.
- Dock icon size.
- Dock tooltips.
- Icon theme.
- Symbolic icon theme.
- Top-bar clock format.
- Top-bar modules.
- Active title display.
- Launcher max results.
- Launcher empty-query behavior.
- Launcher categories.
- Reduced motion.
- App registry refresh.

Acceptance:

- Dock & Panel page reads and writes Shell config.
- Dock pins can be reordered and changed using app registry entries.
- Invalid pinned app ids are shown as missing, not silently dropped.
- Icon theme selector lists installed themes and exposes fallback behavior.
- Top-bar module order can be configured.
- Clock format preview updates immediately in the Settings UI.
- Launcher settings are saved and validated.
- Shell running state is detected where possible.
- Hot-reloadable Shell settings apply live; restart-required settings are marked.

Implementation notes:

- The icon theme provider from the Shell milestone should be reused.
- App picker for dock pins should use the shared app registry.
- Do not implement app discovery in Settings separately.

### ST-4: Appearance, theme, and personalization

Problem:

Appearance is the most visible Settings page and currently only changes local state.

Scope:

- Light/dark/system theme preference.
- Accent color.
- Wallpaper selection.
- Wallpaper mode.
- Solid background.
- Gradient background.
- Glass material preview.
- Window border preview.
- Shadow preview.
- Corner radius per corner.
- Font family/size preference if supported.
- Cursor theme/size.
- Icon theme/size.
- Reduced motion.
- High contrast.

Acceptance:

- Appearance controls read real values from config.
- Changing controls updates preview state.
- Saving writes to the correct owner config.
- Wallpaper picker can choose an existing image path.
- Wallpaper mode supports cover, contain, stretch, center, and tile if supported by Window Manager.
- Glass material editor exposes base color, tint color, border color, opacity, and blur radius using the same names as the Window Manager config.
- Corner radius editor supports all corners, plus per-corner override.
- Cursor theme and icon theme selectors show installed themes and fallback state.
- Reduced motion writes to both Window Manager and Shell where both have a relevant setting.
- High contrast is only enabled if there is an actual theme path for it; otherwise it is shown as deferred/unavailable.

Implementation notes:

- Keep previews local and cheap. Do not require live compositor changes while dragging sliders unless hot reload is reliable.
- Avoid fake wallpaper presets unless they correspond to real bundled assets or real selectable files.

### ST-5: Display and output settings

Problem:

The Window Manager supports selected output and scale, but there is no GUI to inspect or edit them.

Scope:

- Connected output listing.
- Selected output.
- Output name/connector.
- Physical mode summary.
- Logical size.
- Scale.
- Per-output scale overrides.
- Refresh rate display.
- Restart-required output switch messaging.

Acceptance:

- Display page shows the currently selected output and scale.
- Output list is real or explicitly unavailable.
- Scale selector supports common values: 1.0, 1.25, 1.5, 1.75, 2.0.
- Custom scale values are clamped to the Window Manager-supported range.
- Per-output scale writes to `[outputs."CONNECTOR"].scale`.
- Changing selected output is allowed but marked restart-required unless live switching exists.
- Invalid output selectors are rejected.
- Settings does not pretend to configure multi-output layout before the Window Manager supports it.

Implementation notes:

- Full display arrangement, rotation, mirroring, and hotplug UI are deferred.
- The page should be clear that this milestone is single-selected-output.

### ST-6: Keyboard, shortcuts, cursor, and input

Problem:

Keyboard and cursor configuration are daily-driver basics. Cursor config already exists in Window Manager config, and keyboard config is part of the Window Manager readiness target.

Scope:

- Keyboard layout.
- Keyboard variant.
- Keyboard model.
- Keyboard options.
- Repeat rate.
- Repeat delay.
- Window Manager shortcut editor.
- Cursor theme.
- Cursor size.
- Hardware cursor toggle.
- Pointer speed/touchpad settings only if backend exists.

Acceptance:

- Keyboard page reads and writes Window Manager keyboard config.
- Layout selector can show common layouts even if full xkeyboard listing is deferred.
- Repeat controls validate ranges.
- Shortcut editor can record a shortcut and detect conflicts.
- Shortcut editor supports reset-to-default for each action.
- Cursor page or Appearance subsection reads and writes cursor theme and size.
- Missing cursor theme is shown as fallback, not as success.
- Pointer/touchpad controls are hidden or marked unavailable until there is a real backend.

Implementation notes:

- Shortcut editing should use the same keybinding parser as the Window Manager.
- Do not add touch/touchpad UI that writes nowhere.

### ST-7: Desktop, screenshots, and app defaults

Problem:

Some desktop-level choices do not belong purely to Appearance, Shell, or Window Manager chrome. Settings should expose the subset that already has a backend.

Scope:

- Animations.
- Idle blanking.
- Screenshot behavior if configurable.
- Default file manager/terminal/browser if the app registry supports it.
- App registry refresh.
- Open-with/default apps only as deferred placeholders unless the backend exists.

Acceptance:

- Desktop page edits animations and idle blanking.
- Screenshot section displays the current save location and supported shortcuts.
- If screenshot folder becomes configurable, Settings edits it.
- Default terminal/browser/file manager is shown only if backed by the shared app registry.
- Open-with/default-app UI is deferred unless there is a real MIME/default-app backend.

Implementation notes:

- Files app behavior and MIME handling belong mostly to later Files/default-app work.
- Do not expose desktop icons or widgets unless the Shell actually implements them.

### ST-8: System status and About

Problem:

The current About and Power pages show fake data. This must be replaced with truthful read-only status before daily use.

Scope:

- OS name.
- Kernel version.
- Hostname.
- CPU summary.
- Memory total.
- Storage summary.
- GPU/display summary when available.
- Build version.
- Git commit or build id when available.
- Battery presence and percentage.
- Power source.
- Network availability.
- Audio availability.
- Bluetooth availability.

Acceptance:

- About page shows real system/build data or `Unavailable`.
- Power page shows real battery/power state or `Unavailable`.
- Network page or rows show real network availability if a lightweight provider exists.
- Audio rows show real volume/mute state if a lightweight provider exists.
- Bluetooth rows show real adapter state if a lightweight provider exists.
- No fake percentages, fake update cadence, fake kernel names, or fake hardware values remain in readiness pages.

Implementation notes:

- Prefer simple providers first: `/proc`, `/sys`, `uname`, and existing libraries available in the build.
- Avoid requiring privileged services for read-only data.
- Deep network/audio/Bluetooth control UIs are later milestones.

### ST-9: Page structure and UX cleanup

Problem:

The current sections are visually credible but not aligned to the first real milestone. Some pages are placeholders that should either become real or be marked unavailable.

Scope:

- Sidebar page set.
- Search within Settings if cheap.
- Dirty state presentation.
- Apply/restart messaging.
- Empty/unavailable states.
- Error banners.
- Reset/revert controls.
- Responsive layout.
- Keyboard navigation.

Recommended first real pages:

- General
- Appearance
- Display
- Keyboard
- Desktop
- Dock & Panel
- Applications
- Power
- Network
- Audio
- Bluetooth
- Notifications
- Privacy
- About

Acceptance:

- Primary pages have real controls or explicit unavailable states.
- Placeholder-only pages are removed, hidden, or clearly marked as deferred.
- Unsaved changes are visible.
- Save/apply/revert behavior is consistent.
- Restart-required changes are grouped and visible.
- Write/apply errors stay visible until dismissed or fixed.
- Sidebar and pages are keyboard-navigable.
- Text fits in narrow windows without overlapping controls.
- The app remains usable at its current default size.

Implementation notes:

- Settings can use a system titlebar and should keep the glass background.
- Do not add marketing copy or explanatory tutorial text inside the app.
- Keep operational pages dense and direct.

### ST-10: Tests and validation

Problem:

Settings will become the UI that edits critical config. Silent regressions can break the desktop.

Scope:

- Settings schema tests.
- Window Manager config load/write tests.
- Shell config load/write tests.
- Atomic write tests.
- Validation tests.
- Apply mode tests.
- Dirty/revert/reset model tests.
- System info provider tests with fixture data.
- Shortcut conflict tests.
- Theme/icon/cursor discovery tests.

Acceptance:

- Unit tests cover every settings schema descriptor.
- Unit tests cover round-tripping Window Manager config while preserving unknown keys.
- Unit tests cover round-tripping Shell config while preserving unknown keys.
- Unit tests cover invalid values and defaults.
- Unit tests cover write failure behavior where practical.
- Unit tests cover shortcut conflict detection.
- Manual validation confirms real edits affect Window Manager and Shell after live reload or restart.

Implementation notes:

- Most Settings logic should be testable without launching a Flux window.
- Prefer fixture TOML files for config tests.
- Avoid GPU/screenshot tests for this milestone unless existing render fixtures make it cheap.

## Implementation order

1. Extract a Settings model and backend.

   Move real state out of `SettingsApp.hpp` local hooks and into a testable model/store.

2. Add schema and TOML persistence.

   Load/write Window Manager and Shell configs with validation and atomic writes.

3. Wire the Appearance page to real config.

   Start with wallpaper/background, glass/chrome, cursor, icon theme, reduced motion, and high contrast state.

4. Add Display and Keyboard pages.

   Expose selected output, scale, keyboard layout/repeat, shortcuts, cursor, and hardware cursor settings.

5. Add Dock & Panel page.

   Edit Shell dock pins, icon theme, top-bar modules, and launcher preferences.

6. Replace fake system data.

   Implement read-only About/Power/system providers and mark unavailable values honestly.

7. Add save/apply/revert/restart-required UX.

   Make all writes and apply boundaries explicit.

8. Add tests and update docs.

   Cover schema, config, validation, model behavior, and manual validation.

## Manual validation checklist

### Build and unit checks

```sh
cmake --build build --target flux_tests
./build/flux_tests --test-case="*settings*"
./build/flux_tests --test-case="*config*"
cmake --build build
git diff --check
```

### Launch checks

```sh
./build/lambda-window-manager
./build/lambda-shell
./examples/lambda-settings
```

Expected:

- Settings opens with a system titlebar and glass background.
- Settings loads Window Manager and Shell config values.
- Missing config files are created if needed.
- Pages remain responsive while values load.
- If Window Manager or Shell is unavailable, Settings still opens and shows unavailable/apply-next-launch states.

### Appearance checks

Validate:

- wallpaper path selection
- wallpaper mode
- background color
- background gradient
- glass base color
- glass tint
- glass opacity
- glass blur radius
- border color/width
- shadow color/radius/offset
- corner radius all corners
- corner radius per corner
- cursor theme/size
- icon theme/size
- reduced motion

Expected:

- Valid changes save.
- Hot-reloadable changes apply live when supported.
- Restart-required changes are marked.
- Invalid values are blocked.

### Display checks

Validate:

- selected output display
- selected output change
- scale 1.0
- scale 1.25 or 1.5
- scale 2.0
- invalid scale
- per-output scale

Expected:

- Current values match Window Manager config.
- Restart-required output changes are clear.
- Invalid values do not write.

### Keyboard and shortcuts checks

Validate:

- keyboard layout
- keyboard repeat rate
- keyboard repeat delay
- launcher shortcut
- screenshot shortcut
- snap shortcuts
- close/minimize/maximize shortcuts
- duplicate shortcut conflict
- reset shortcut to default

Expected:

- Valid shortcuts write in Window Manager config syntax.
- Conflicts are visible before save.
- Invalid shortcut edits do not corrupt config.

### Dock and panel checks

Validate:

- dock pinned apps reorder
- add pinned app from app registry
- remove pinned app
- missing pinned app display
- show running unpinned toggle
- icon theme
- top-bar clock format
- top-bar module order
- launcher max results

Expected:

- Valid changes write to Shell config.
- Shell hot reload applies supported values or Settings marks restart-required.

### System info checks

Validate:

- About OS/kernel/hostname/build data
- CPU/memory/storage data
- battery present system
- battery absent system
- network unavailable case
- audio unavailable case
- Bluetooth unavailable case

Expected:

- Values are real or `Unavailable`.
- No fake rows remain in readiness pages.

### Persistence checks

Validate:

- edit and save
- close and reopen Settings
- restart Window Manager
- restart Shell
- revert unsaved changes
- reset single setting
- reset page
- invalid manual config edit
- read-only config file

Expected:

- Saved values persist.
- Unknown keys are preserved.
- Invalid config is reported without crashing.
- Write errors are shown and do not lose user changes.

Current status: Settings has no separate app-specific preferences that require their own generated config file. It reads and writes the Window Manager and Shell owner configs directly; both owner config files are generated with defaults by the Settings backend when missing.

## Test additions

Add focused automated tests where behavior is deterministic:

- Settings schema descriptors are complete and unique.
- Window Manager config defaults load into Settings model.
- Shell config defaults load into Settings model.
- Window Manager config round-trip preserves unknown keys.
- Shell config round-trip preserves unknown keys.
- Invalid color, number, enum, path, and shortcut values are rejected.
- Atomic write failure leaves original file intact.
- Dirty/revert/reset behavior works.
- Apply mode mapping is correct for each setting.
- Shortcut conflict detection works.
- Wallpaper path normalization works.
- Icon theme and cursor theme discovery handle missing themes.
- System info providers parse fixture `/proc`, `/sys`, and `uname` data.

## Done checklist

- [x] Settings backend loads Window Manager config.
- [x] Settings backend loads Shell config.
- [x] Settings app has generated/default config support if app-specific preferences are needed.
- [x] Schema metadata exists for every displayed real setting.
- [x] Config writes are atomic and validated.
- [x] Unknown config keys are preserved where practical.
- [ ] Appearance page edits real config.
- [ ] Display page edits real selected-output/scale config.
- [ ] Keyboard page edits real keyboard and shortcut config.
- [ ] Dock & Panel page edits real Shell config.
- [ ] Desktop page edits real animations/idle/screenshot-supported config.
- [x] About/System pages show real or unavailable values, not fake values.
- [ ] Save/revert/reset/error UX is implemented.
- [ ] Restart-required changes are visible.
- [ ] Hot-reloadable changes apply live where supported.
- [x] Tests cover schema, config, validation, model state, and system provider fixtures.
- [ ] User guide and Settings docs are updated to match actual behavior.

## Deferred to later milestones

- Session startup/shutdown, login, lock, logout, suspend/reboot UI.
- New log collection or trace infrastructure.
- Full network manager UI.
- Full Bluetooth pairing UI.
- Full audio mixer UI.
- Full notification daemon and notification center.
- Full privacy/portal permissions UI.
- Full users/accounts administration.
- Package/update manager.
- Full multi-output layout editor.
- Files open-with/default-app/MIME UI.
- Terminal profiles and terminal-specific preferences.
- Accessibility framework beyond settings toggles backed by real behavior.
- XWayland compatibility controls.
