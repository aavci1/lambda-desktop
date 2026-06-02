# Lambda and Lambda roadmap

**Last updated:** 2026-06-03
**Status:** Source of truth for current project status, Lambda desktop readiness, active backlog, and archived roadmap notes.

## Purpose

This document replaces the older roadmap, Lambda desktop readiness index, component readiness specs, Shell mockup spec, and Shell status-bar plan. Keep current priorities here; keep implementation details in code and stable usage/reference material in the focused docs listed below.

## Document Map

| Document | Role |
| --- | --- |
| [roadmap.md](roadmap.md) | Current project status, Lambda desktop backlog, ownership boundaries, validation gates, and archived milestone notes |
| [lambda-settings-ux-plan.md](lambda-settings-ux-plan.md) | Active UX and organization plan for `lambda-settings` |
| [compositor.md](compositor.md) | Architecture/history reference for the compositor and framework/compositor boundary |
| [compositor-wlroots-improvement-plan.md](compositor-wlroots-improvement-plan.md) | Active wlroots comparison plan for compositor protocol, state, rendering, and validation improvements |
| [compositor-user-guide.md](compositor-user-guide.md) | Build, configure, and run `lambda-window-manager` from a TTY |
| [linux-development.md](linux-development.md) | Linux package setup and KMS/Vulkan development notes |
| [conventions.md](conventions.md) | Repository layout, CMake, namespaces, platforms, examples |
| [reactive-graph.md](reactive-graph.md), [composites.md](composites.md), [migrating-to-v5.md](migrating-to-v5.md), [ui-view-body-style.md](ui-view-body-style.md), [event_queue.md](event_queue.md) | Lambda v5 framework reference |

Deleted/superseded docs:

- `lambda-desktop-assessment.md`
- `lambda-window-manager-readiness-spec.md`
- `lambda-shell-readiness-spec.md`
- `lambda-shell-spec.md`
- `lambda-shell-status-bar-plan.md`
- `lambda-settings-readiness-spec.md`
- `lambda-files-readiness-spec.md`
- `lambda-terminal-readiness-spec.md`

## Product Decisions

- The daily-driver implementation order is Window Manager, Shell, Settings, Files, Terminal, then shared services and remaining apps.
- The first daily-driver target is pure Wayland. XWayland remains a later explicit product decision.
- Sessions are started and ended manually for now. Session automation, greeter/login, logout, lock, suspend/reboot UI, and crash-restart policy are intentionally outside the current daily-driver gate.
- New log collection and log viewer work are outside the current daily-driver gate.
- Browser, mail, calendar, and media can be mature external Wayland apps unless Lambda explicitly decides to own them.
- Settings edits component-owned config. It must not become a second source of truth for Window Manager, Shell, Files, or Terminal preferences.

## Ownership Boundaries

- Window Manager owns KMS/Wayland composition, output/scale, window state/focus, input routing, layer-shell placement, compositor chrome, screenshots, compositor-drawn screenshot UI, and protocol validation.
- Shell owns persistent desktop chrome: dock, command launcher, app presentation, status docklets/quick settings, notifications, clipboard-history UI/policy, Shell preferences, and user-facing desktop navigation.
- Settings owns GUI editing for owner config files and truthful system/about status. The owning process remains the runtime authority.
- Files owns safe local file management, trash-first delete, selection, clipboard file operations, open-with UI, file icons, and current-folder refresh/watch behavior.
- Terminal owns pty/libvterm behavior, scrollback, keyboard/input encoding, selection/copy/paste, terminal rendering, terminal preferences, and terminal desktop integration.
- Shared desktop services are still needed for app registry, icon theme, MIME/default-app/open-with, status providers, notifications, clipboard history, portals, secrets/keyring, and future file chooser/screenshot/screencast APIs.

## Project Snapshot

| Area | Current status |
| --- | --- |
| Lambda v5 UI runtime | Shipped. Retained mount, reactive graph, `Bindable` modifiers, `For`/`Show`/`Switch`. |
| App platforms | macOS Metal, Linux Wayland Vulkan, Linux KMS Vulkan. |
| Examples | Demo and Lambda app targets build through CMake when examples are enabled. |
| Window Manager | Core compositor is usable for dogfooding on the target hardware; remaining gate is deferred titlebar/content frame coherence, the rest of the wlroots comparison backlog, broader real-app validation, and visual polish. |
| Shell | App registry, dock, IPC, config, icons, and status fallback exist; live notification/clipboard/provider work remains. |
| Settings | Real owner-config editor exists; live apply depends on Window Manager/Shell hot-reload support. |
| Files | Live file browser/manager exists; model layer is ahead of live UI for several commands. |
| Terminal | Usable for basic shell work; search UI, mouse reporting, and complex Unicode remain open. |

## Daily-Driver Gate

Lambda is not daily-driver complete until these gates are closed.

| Priority | Area | Gate | Status |
| --- | --- | --- | --- |
| P0 | Window Manager | Visual stability and real-app compositor validation | Open: narrowed to validation/polish |
| P1 | Shell | Live launcher providers, notifications, clipboard history, quick settings providers | Open |
| P2 | Settings | Owner config editing plus live-apply clarity | Mostly done |
| P3 | Files | Safe file-management live UI beyond the model layer | Open |
| P4 | Terminal | Daily terminal UI completeness | Open |
| P5 | Shared services and missing core apps | Shared desktop backends, editor, document viewer | Unspecced/integrated backlog |

## Detailed Workstream Index

The old readiness specs used component-specific workstream IDs. This section keeps those workstreams in the roadmap so no active spec work is lost.

Window Manager:

- WM-1: Baseline idle and frame scheduling.
- WM-2: Resize and snap visual stability.
- WM-3: Window geometry and state invariants.
- WM-4: Chrome, decorations, glass, and shadows.
- WM-5: Output selection and scale.
- WM-6: Input, keyboard, and cursor readiness.
- WM-7: Screenshot readiness.
- WM-8: Wayland protocol validation.
- WM-9: Config contract.
- WM-10: Real-app validation.

Shell:

- SH-1: Process, IPC, and failure behavior.
- SH-2: Shared app registry.
- SH-3: Icon theme provider.
- SH-4: Dock model and behavior.
- SH-5: Command launcher.
- SH-6: Top bar and status modules.
- SH-7: Quick settings and status controls.
- SH-8: Notifications.
- SH-9: Clipboard history.
- SH-10: Shell config and preferences.
- SH-11: Visual quality and layout.
- SH-12: Accessibility and keyboard behavior.
- SH-13: Tests and validation.

Settings:

- ST-1: Settings model, schema, and persistence.
- ST-2: Window Manager settings integration.
- ST-3: Shell settings integration.
- ST-4: Appearance, theme, and personalization.
- ST-5: Display and output settings.
- ST-6: Keyboard, shortcuts, cursor, and input.
- ST-7: Desktop, screenshots, and app defaults.
- ST-8: System status and About.
- ST-9: Page structure and UX cleanup.
- ST-10: Tests and validation.

Files:

- FI-1: Files model and state ownership.
- FI-2: Navigation, places, and path entry.
- FI-3: Selection and keyboard behavior.
- FI-4: Core file operations.
- FI-5: Trash service.
- FI-6: Clipboard and drag/drop.
- FI-7: Context menus and commands.
- FI-8: Views, sorting, search, and filtering.
- FI-9: Filesystem watching and refresh.
- FI-10: Open-with, MIME, icons, and thumbnails.
- FI-11: Errors, progress, cancellation, and undo.
- FI-12: Preferences and Settings integration.
- FI-13: Accessibility and keyboard quality.
- FI-14: Tests and validation.

Terminal:

- TE-1: App identity, lifecycle, and structure.
- TE-2: PTY and event-loop reliability.
- TE-3: Scrollback and viewport.
- TE-4: Selection, copy, paste, and clipboard.
- TE-5: Keyboard input coverage.
- TE-6: Mouse reporting and pointer behavior.
- TE-7: Unicode, width, and text attributes.
- TE-8: Color, theme, cursor, and visual rendering.
- TE-9: Resize, reflow, and performance.
- TE-10: Search and navigation.
- TE-11: Preferences, profiles, and Settings handoff.
- TE-12: Desktop integration.
- TE-13: Tests and validation.

## P0 Window Manager

Current implementation:

- `lambda-window-manager` owns a selected KMS output, runs a Wayland server, renders through Vulkan/Canvas, and hosts Lambda plus normal Wayland apps.
- Core idle behavior, Lambda app disconnect handling, shell focus restoration, output selection/scale, cursor config, keyboard config, screenshot modes, in-tree protocol demos, config defaults, compositor CPU/pacing traces, and real-app smoke tooling exist.
- Screenshot full-output, active-window, and region capture are implemented with compositor-owned region UI.
- Protocol work includes layer-shell, xdg-shell, xdg-output, viewporter, cursor-shape, fractional-scale, activation, presentation-time, relative pointer, pointer constraints, primary selection, clipboard/data-device, idle inhibit, and background-effect paths.
- The active wlroots comparison plan has verified core surface-state slices, layer-shell state, subsurface sync, scene damage, seat serials, dmabuf lifetime, popup/xdg lifecycle, activation, pointer constraints, presentation-time, fractional-scale, idle-inhibit, output/xdg-output updates, pointer-extension cleanup, cursor-shape cleanup, and viewporter resource hygiene through WM-COMP-20.
- Firefox dogfooding blockers addressed in the tested paths: xdg-popup lifecycle cleanup follows the wlroots-style inert-resource pattern, Firefox crash/recovery windows no longer grow on focus changes, fullscreen video can restore, and fullscreen preserves pre-fullscreen maximized/snapped/normal state.
- Fullscreen shell-panel behavior exists for the current single-output desktop: panels leave the fullscreen area and restore afterward.
- KMS presentation has a compositor frame queue, improved frame pacing, direct scanout/overlay paths for video, hardware-cursor motion that does not force scene redraws, and video overlay tracing for skipped-frame analysis.
- Explicit sync is removed for now; it did not fix the observed rendering artifacts and created client-crash risk when syncobj state was advertised without a matching attached buffer.
- Wayland buffer damage is tracked through snapshots and used for partial cached-image updates on the SHM image path. Current high-value clients in daily testing mostly use dma-buf, so this is plumbing rather than a proven daily-driver win.

Open gate:

- Resolve the deferred system-titlebar/content frame-coherence issue: Settings resize on DP-1 HiDPI can still show momentary non-synced titlebar/content width even though borders stay synced and flicker is gone.
- Finish the remaining wlroots comparison backlog in [compositor-wlroots-improvement-plan.md](compositor-wlroots-improvement-plan.md): global resource hygiene, xdg toplevel configure-state parity, seat focus/grab workflow parity, data-device DnD lifecycle parity, layer-shell dynamic behavior, output-layout foundation, and a visual regression/real-app harness.
- Continue resize/snap/maximize/restore validation across more apps; Firefox restore/fullscreen paths are fixed in the tested scenarios, but GTK/Qt/terminal coverage still needs a pass.
- Re-test Shell panel behavior around fullscreen, window animations, launcher/dock popovers, dock context menus, and long-running browser/video sessions.
- Complete live real-app validation with Lambda apps plus browser, GTK, Qt, `foot`, clipboard, menus/popups, maximize/restore/snap/minimize, screenshots, fullscreen video, mpv playback, and long idle sessions.
- Keep popup grabs honest: config-gated until hardware validation says they can be enabled by default.
- Keep unsupported touch/tablet behavior absent or clearly non-advertised.

Validation:

- Deterministic tests cover output selector, scale config, keybindings, config fallback, screenshot policy/regions/paths/PNG writing, frame geometry, snap/resize geometry, minimized focus restoration, fullscreen restore state, popup geometry, layer-shell zones, and surface input regions.
- Manual target-hardware traces now cover Firefox stability, fullscreen video restore, mpv/video pacing, compositor CPU, hardware overlay/scanout decisions, DP-1 terminal rendering, DP-1 HiDPI resize/flicker checks, dock context menus, clipboard/primary selection, and text drag/drop in the tested paths.
- Full visual acceptance still requires target hardware; GPU/Wayland tests may not run in headless CI. SHM buffer-damage optimization still needs a real SHM partial-damage client before it can be counted as performance-validated.

Deferred:

- Session wrapper, auto-start, auto-restart, login, lock, logout, suspend/reboot UI.
- Multi-output desktop layout. The wlroots comparison may add output-layout foundations, but enabling a real multi-output desktop remains deferred.
- DPMS/panel power-off and richer power management.
- XWayland.

## P1 Shell

Current implementation:

- `lambda-shell` creates layer-shell dock, command launcher, and dock menu surfaces.
- Shell IPC uses request IDs and structured parse/serialization helpers.
- Shell exits cleanly when the Window Manager IPC is unavailable or disconnects.
- Shared app registry discovers local Lambda executables and installed `.desktop` entries, with app-id aliases and launch resolution shared by Shell and Window Manager.
- Dock pins load from Shell config; running unpinned apps appear; dock click launches, focuses, or restores; dock context menu supports new window, pin/unpin, and quit where wired.
- Icon theme lookup is used by dock and launcher with Material glyph fallback.
- Docklet status rendering shows real values when present and unavailable/unknown values honestly. The current Window Manager snapshot still reports system providers as unknown.
- Quick-status popup exists, but provider controls are disabled/unavailable.
- Notification and clipboard-history models/config/policies exist and are tested, but live UI is not implemented.
- Advanced launcher provider models for apps, windows, Settings panels, Shell actions, empty states, error states, and ranking exist; production launcher still renders and activates dock/app `DockItem` results.
- Shell config is generated, parsed, hot-reloaded, and covers appearance, dock, quick settings, notifications, clipboard history, and launcher policy.

Open gate:

- Wire the production launcher to the richer provider model and activation path for apps, windows, Settings panels, Shell actions, empty states, and errors.
- Add launcher ranking to the live launcher path.
- Implement live notification receive API/service boundary, banners, notification center/history, grouping, dismissal, clear-all, actions where supported, and do-not-disturb behavior.
- Implement clipboard-history capture/service boundary, picker UI, selected-entry paste, clear, size/history limits, primary-selection policy, and memory-only-by-default persistence policy.
- Add real Shell-owned status providers where available: network/Wi-Fi, Bluetooth, audio/volume, battery/power, brightness, do-not-disturb.
- Add quick settings controls for providers that can actually be controlled.
- Add status provider links into Settings for deeper configuration.
- Complete manual validation with Lambda apps and selected external Wayland apps.

Deferred:

- Full freedesktop notification spec parity.
- Full system tray/status-notifier support.
- Multi-output Shell layout beyond preserving clean models.
- Files/recent-documents launcher providers.

### Shell Status And Quick Settings Detail

Status and quick settings are Shell-owned. The Window Manager may include snapshot fields for compatibility, but it must not become the audio, network, Bluetooth, battery, brightness, notification, clipboard, or quick-settings backend.

Provider model requirements:

- Providers expose typed snapshots with availability: unavailable, read-only, or writable.
- Visible state must be real or explicitly unavailable; never show fake connected, charged, unmuted, or active values.
- Provider failures must not freeze Shell.
- Slow providers should update independently so one missing backend does not block docklets or quick settings.
- Shell config controls quick-settings order through `quick_settings.modules`.

Provider backends:

- Clock: honor `dock.clock_format`, update conservatively, and avoid redraws when formatted text is unchanged.
- Battery/power: read `/sys/class/power_supply` first; optionally use UPower later for richer state. Show absent battery as unavailable. Show percentage and charging/discharging/full only from real state. Battery is initially read-only; power mode waits for a real backend.
- Brightness: read `/sys/class/backlight`; show unavailable when absent; show read-only when writable access or privileged backend is missing. Enable a slider only with a real write path such as logind/udev policy or a small privileged helper.
- Audio: use PipeWire/WirePlumber APIs, with `wpctl` as an optional first fallback. Read default sink volume and mute state. Add volume slider and mute toggle only when control is real. Microphone mute and output picker can come later.
- Network/Wi-Fi: use NetworkManager D-Bus. Show unavailable when NetworkManager or usable interfaces are absent. Distinguish wired, Wi-Fi, disconnected, connecting, connected, and SSID. First useful control is Wi-Fi enable/disable; connection selection/password entry belongs in Settings.
- Bluetooth: use BlueZ D-Bus. Show unavailable when BlueZ/adapters are absent. Show powered state and connected device count/name. Toggle adapter power only when supported; pairing/device management belongs in Settings.
- Notifications: provide docklet affordance/count/DND state, banners, notification center/history, dismissal, clear-all, timeout/history/previews policy, and later freedesktop notification receive path.
- Clipboard history: provide docklet affordance, picker, selected-entry paste or write-back-to-clipboard, clear history, max entries, max text bytes, persistence policy, and primary-selection policy. Live clipboard observation is still missing.

Docklet UI requirements:

- Support at least network/Wi-Fi, Bluetooth, volume, battery, notifications, clipboard, and clock.
- Each status item should be individually clickable where it has a detail surface.
- Icons should be driven by typed provider state.
- Unavailable items must be disabled or hidden by policy, not shown as active.

Quick settings UI requirements:

- Include a header with title and Settings button.
- Include Wi-Fi, Bluetooth, volume/mute, brightness, battery/power, and do-not-disturb controls when configured.
- Keep read-only and unavailable controls visibly disabled.
- Add per-row Settings links for deeper pages: network, sound, Bluetooth, display, notifications, clipboard.
- Keep long-form setup out of quick settings.

Shell actions:

- `open-settings:network`
- `open-settings:sound`
- `open-settings:bluetooth`
- `open-settings:display`
- `open-settings:notifications`
- `open-settings:clipboard`
- `toggle-dnd`

Status testing:

- Provider snapshot normalization.
- Icon selection for Wi-Fi, Bluetooth, volume, battery, notifications, and clipboard.
- Module ordering from Shell config.
- Unavailable/read-only/writable control state.
- Clock formatting.
- Sysfs battery and brightness fixture parsing.
- Quick settings rows from provider snapshots.
- Notification DND behavior.
- Clipboard policy behavior.
- Manual checks on battery-backed and no-battery machines, PipeWire/WirePlumber volume/mute, internal-panel brightness and external-only monitor, NetworkManager states, BlueZ absent/present/off/on/connected states, notification center interactions, and clipboard privacy/clear behavior.

## P2 Settings

Current implementation:

- `lambda-settings` opens as a system-titlebar Lambda app.
- It reads and writes Window Manager and Shell owner config files directly.
- It generates owner configs with defaults when missing.
- Schema metadata exists for displayed settings.
- Writes are atomic and validated; unknown keys are preserved where practical.
- Appearance edits real Window Manager background/wallpaper/glass and Shell icon/reduced-motion config.
- Display edits selected output and scale config.
- Keyboard edits keyboard layout/repeat and screenshot/close shortcuts.
- Desktop edits animations, hardware cursor, cursor theme/size, idle blank timeout, and screenshot shortcuts.
- Dock & Panel edits Shell dock and quick-settings values.
- Notifications edits Shell notification and clipboard-history config, while Launcher schema/config exists in the backend but is not yet exposed as its own page.
- About/System shows real or explicitly unavailable values.
- Save/revert/reset/error UX exists; restart-required rows are visible.
- The app has the right Settings skeleton, but the current content design reads more like a config editor: section headings plus transparent divided rows, bespoke glass colors, and some unused demo-style appearance helpers.
- The detailed UX plan lives in [lambda-settings-ux-plan.md](lambda-settings-ux-plan.md).

Open gate:

- Ensure hot-reloadable changes apply live where the owning process supports them.
- Keep apply-mode labels accurate: `Applies after Save` versus `Restart required`.
- Avoid claiming unavailable system providers are live.
- Convert settings groups into real `Card` surfaces using the framework `Card` component and the visual language already proven in `toggle-demo`/`card-demo`.
- Update settings rows to use stable internal padding, label/detail hierarchy, quiet secondary text, and right-aligned controls rather than transparent row lists.
- Reorganize Settings navigation around user intent: General, Appearance, Display, Input, Windows, Dock & Panel, Notifications, Launcher & Clipboard, and About.
- Split Launcher & Clipboard out from Notifications, and expose the Shell launcher settings that already exist in the schema.
- Rename/reframe Keyboard/Desktop content into clearer Input and Windows pages where appropriate.
- Extend `SettingsGroup` with optional captions so cards can explain ownership/apply behavior without ad hoc page text.
- Calm the save footer: keep staged Save/Revert/Reset semantics, but make status placement and reset styling feel like a system footer rather than a demo strip.
- Replace bespoke glass surface tokens with `ThemeKey`/framework tokens where practical, leaving only app-specific layout constants in `SettingsTheme`.
- Remove or wire the unused showcase-oriented helpers such as theme/accent/wallpaper preview code; no visible or retained UI should imply fake personalization settings.
- Add Settings UI later for Files preferences, Terminal preferences, MIME/default-app editing, and shared-service settings after those backends exist.

Validation:

- Targeted Settings tests pass for schema, validation, dirty/revert/reset, Window Manager and Shell config round trips, unknown key preservation, atomic write failure, owner config file helpers, shortcut conflict detection, wallpaper path normalization, theme discovery, and system info fixtures.
- Manual Settings validation must cover launch, scrolling, compact layout, Appearance, Display, Input, Windows, Dock & Panel, Notifications, Launcher & Clipboard, About/System, save/revert/reset, invalid values, restart-required rows, and owner-config persistence.

Deferred:

- Full network manager, Bluetooth pairing, audio mixer, notification daemon/center, privacy/portal permissions, users/accounts, package/update manager, multi-output layout editor, and Files open-with/default-app UI.

## P3 Files

Current implementation:

- `lambda-files` opens as an integrated-titlebar Lambda app with glass background.
- Live UI supports sidebar places, breadcrumbs, back/forward/up, grid/list view, hidden-file toggle, pointer/keyboard multi-select, range select, select all, clear selection, activation, create folder/file, copy/cut/paste, duplicate, trash-first delete, reveal, default open, context menus, and periodic refresh.
- Preferences persist for hidden files and view mode.
- Clipboard uses internal copy/cut state plus `text/uri-list` text for compatible clients.
- Default file open uses shared app registry plus MIME/default-app data rather than direct `xdg-open` file launching.
- Icon theme lookup is used for file/folder/MIME icons with fallback.
- Model layer has path normalization, navigation history, breadcrumb generation, stable sort by name/kind/size/modified time, current-folder search/filtering, directory refresh diffs, selection preservation, rename validation, copy/move/duplicate operations, trash metadata, restore helpers, conflict decisions, operation progress/failure/cancel state, safe undo helpers, MIME/default-app fixtures, icon lookup, and preference load/save.

Open gate:

- Add direct text path entry.
- Add rename UI.
- Add Trash view/restore UI or explicitly defer restore while keeping delete trash-first.
- Add undo UI for supported safe operations.
- Add operation progress/cancel UX and partial-failure reporting beyond simple error text.
- Add conflict prompts for keep both, replace, skip, and cancel.
- Add current-folder search UI.
- Add visible sort controls for name/kind/size/modified time and direction.
- Add explicit open-with chooser UI.
- Replace polling refresh with native watcher integration where practical.
- Add Lambda-level file drag source/target widgets before advertising drag/drop.

Validation:

- Targeted Files tests pass for model, operations, trash, selection, preferences, open-with fixtures, MIME/default-app parsing, URI-list clipboard, icon fallback, and grid layout.

Deferred:

- Advanced default-app editing, thumbnails, mounted volumes/removable devices, tabs, split panes, indexed search, network shares, archive browsing, full document viewer, and full text editor.

## P4 Terminal

Current implementation:

- `lambda-terminal` uses `forkpty`, libvterm, UTF-8 mode, alternate screen, nonblocking pty reads/writes, event-loop wake thread, row damage, row/layout caching, resize through `TIOCSWINSZ`, terminal title updates, and child cleanup.
- Live UI supports text input, key encoding helpers, resize, black glass/solid background config, foreground/background colors, bold/italic/underline/reverse/strikethrough rendering, cursor, live scrollback viewport with wheel and `Shift+PageUp`/`Shift+PageDown`, scrollback-aware selection/copy, desktop clipboard copy/paste, bracketed paste policy, and profile/preferences persistence.
- `TerminalCore` covers deterministic input encoding, application cursor/keypad, focus-event encoding, SGR mouse encoding helper, mouse-to-cell mapping, resize calculation, Unicode width helpers, color/attribute resolution, preferences/profiles, scrollback model, selection reconstruction, search helper, URL detection, app identity, and browser command planning.
- `main.cpp` only creates the app/window/profile background and installs the terminal view; `TerminalSession.cpp` still owns live pty/session/rendering/UI behavior.

Open gate:

- Add live search UI with query entry, match highlight, next/previous, close behavior, and scrollback search.
- Wire terminal-app mouse reporting in `TerminalSession` for SGR/basic button/motion/wheel modes while preserving normal selection when reporting is off.
- Improve live Unicode/grapheme handling beyond `VTermScreenCell::chars[0]`, especially combining marks and complex emoji sequences.
- Add primary selection support or explicitly defer it in the user-facing UI.
- Add OSC 52 policy/support if desired.
- Add URL opening UI and policy.
- Continue splitting live pty/session/model/renderer/UI into testable components.
- Add cursor shape/color scheme preferences if framework rendering supports them.

Validation:

- Targeted Terminal tests pass for core model, input, scrollback, selection, Unicode helpers, color, resize, config, preferences, and pty smoke behavior.

Deferred:

- Tabs, split panes, terminal multiplexing, SSH connection manager, and serial terminal UI.

## P5 Shared Services And Missing Core Apps

These remain backlog items, not separate specs.

Shared desktop services:

- App registry service boundaries and permission model.
- Shared icon-theme provider and cache.
- Shared MIME/default-app/open-with service.
- Status provider service model for Shell and Settings.
- Notification backend/service boundary.
- Clipboard-history backend/service boundary.
- Portal direction for screenshot/screencast/file chooser and untrusted clients.
- Secrets/keyring.

Preliminary text editor:

- Plain-text open/edit/save/save-as.
- New file flow.
- Encoding/newline handling.
- Dirty state.
- Search.
- Clipboard.
- Files/open-with integration.

Document/PDF viewer:

- PDF rendering.
- Page navigation.
- Zoom and fit modes.
- Search.
- Thumbnail/sidebar navigation.
- Files/open-with integration.

Optional later apps:

- Archive manager.
- Image viewer.
- Media player.
- System monitor.
- Browser.
- Mail.
- Calendar.
- Calculator.

## Validation Model

- Unit/model tests for deterministic behavior.
- Manual hardware/app smoke tests for compositor, Shell, and real Wayland clients.
- Real-app validation with Lambda apps plus mature Wayland clients such as Firefox, GTK apps, Qt apps, and `foot`.
- Visual/glass/animation behavior requires target-machine manual validation until screenshot/render regression coverage exists.
- Do not mark a gate complete solely because model helpers exist; live UI and user workflow must be wired where the gate is user-facing.

Manual validation coverage inherited from the old readiness specs:

- Window Manager: build/unit checks, TTY launch, Shell launch, protocol smoke, Lambda apps, browser/GTK/Qt/foot real-app matrix, screenshots, fullscreen/restore, video/mpv pacing, hardware overlay/scanout traces, config reload/restart matrix, idle CPU, and compositor crash/disconnect behavior.
- Shell: build/unit checks, launch/failure behavior, app registry discovery, dock launch/focus/restore, launcher keyboard behavior, docklet status, quick settings, notification workflows, clipboard-history workflows, config reload, and Lambda/external app validation.
- Settings: build/unit checks, launch, Appearance, Display, Input, Windows, Dock & Panel, Notifications, Launcher & Clipboard, About/System, save/revert/reset, invalid values, restart-required rows, compact layout, and owner-config persistence.
- Files: build/unit checks, launch, browsing places/root/outside-home/permission-denied/large/hidden/external-change cases, selection, safe operations, trash, clipboard/DnD behavior, open-with behavior, grid/list/sort/search behavior, and preference persistence.
- Terminal: build/unit checks, launch, shell workflows, full-screen apps, scrollback, selection/clipboard, key input, Unicode/color/attributes, resize/performance traces, and preferences.

Useful targeted checks:

```sh
./build/lambda_tests --test-case="Shell*"
./build/lambda_tests --test-case="Settings*"
./build/lambda_tests --test-case="*Files*"
./build/lambda_tests --test-case="Terminal*"
./build/lambda_tests --test-case="screenshot*"
./build/lambda_tests --test-case="surface*"
```

`ctest --test-dir build --output-on-failure` may require a live Wayland display and Vulkan device depending on the environment.

## Archived Completed Work

Lambda v5:

- Retained mounting and fine-grained reactivity are the current public API.
- `MountRoot`, `MountContext`, `Signal`, `Computed`, `Effect`, `Scope`, `Bindable`, `For`, `Show`, and `Switch` are implemented.
- Framework docs live in the focused Lambda v5 reference docs.

Compositor structural cleanup:

- Wayland globals split into `apps/lambda-window-manager/Compositor/Wayland/Globals/`.
- `WaylandServer` uses a pimpl.
- Runtime split into `CompositorRenderFrame`, `CompositorConfigWatch`, `Presenter`, and related modules.
- Window/input logic split into `FocusStack`, `PointerRouter`, `InteractiveMoveResize`, `KeyboardShortcuts`, and `LayerShellInput`.
- Resize tracing unified under `lambda::detail::resizeTrace`.
- Protocol codegen consolidated through `cmake/LambdaWaylandProtocols.cmake`.

Compositor and Shell framework work:

- Layer-shell chrome and compositor-backed background effects exist.
- Shared Shell IPC module exists.
- Shell preview and production rendering use shared paths where practical.
- Subsurface hit testing, popup geometry, layer-shell zones, output selector, screenshot logic, and compositor state helpers have deterministic tests.
- Firefox-oriented xdg-popup and inert-resource lifecycle fixes landed after real crash traces.
- Fullscreen restore now preserves prior maximized/snapped/normal window state, and Shell panels leave/return around fullscreen.
- KMS frame pacing now uses a queued compositor presentation path with video overlay/scanout decisions traced for skipped-frame analysis.
- Hardware cursor motion is decoupled from full scene redraws.
- Explicit sync advertisement is disabled/removed until there is a demonstrated benefit and a correct client-buffer lifecycle.
- Wayland buffer damage is propagated into committed snapshots and used for partial cached-image updates on the SHM path.

Documentation cleanup:

- Old Lambda readiness specs and Shell mockup/status plans were consolidated into this roadmap on 2026-05-26.
- `compositor.md` remains an architecture/history reference, not the active backlog.

## Update Rules

- Update this roadmap when a Lambda gate changes status.
- When implementation lands, update the relevant `Current implementation`, `Open gate`, and `Validation` bullets in the same change.
- Keep user/run instructions in [compositor-user-guide.md](compositor-user-guide.md), not here.
- Keep architecture/history notes in [compositor.md](compositor.md), not here, unless they affect active roadmap decisions.
