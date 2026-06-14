# Lambda and Lambda roadmap

**Last updated:** 2026-06-14 (started SVC-1 D-Bus capability with exported-object introspection and `PropertiesChanged` helpers, SVC-2 libseat KMS device access, SVC-3 logind client basics with active-session discovery and Shell launcher power actions, SVC-4 portal Settings backend basics, SVC-6 notification service basics, SVC-7 StatusNotifierWatcher basics, SVC-9 NetworkManager status/toggle basics with Shell manager/device/AP event refresh, SVC-10 BlueZ status/toggle basics with Shell property/ObjectManager event refresh, SVC-11 UPower basics with Shell display-device/manager event refresh, SVC-12 MPRIS basics with Shell docklet/control integration and event refresh, and SVC-14 UDisks2 basics with Files mounted-volume sidebar/event refresh; integrated daily-driver gap analysis: new System Services workstream SVC-*, compositor protocol/power gates WM-11…WM-21, missing-app specs, reprioritized gate)
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
| [frame-pacing-improvement-plan.md](frame-pacing-improvement-plan.md) | FP-1…FP-16 implementation log, post-implementation review backlog (REV-*), citation errata, and verification checklist (TODO-019) |
| [compositor-user-guide.md](compositor-user-guide.md) | Build, configure, and run `lambda-window-manager` from a TTY |
| [linux-development.md](linux-development.md) | Linux package setup and KMS/Vulkan development notes |
| [conventions.md](conventions.md) | Repository layout, CMake, namespaces, platforms, examples |
| [reactive-graph.md](reactive-graph.md), [composites.md](composites.md), [migrating-to-v5.md](migrating-to-v5.md), [ui-view-body-style.md](ui-view-body-style.md), [event_queue.md](event_queue.md) | Lambda v5 framework reference |

Folded into this roadmap (2026-06-14):

- `lambda-daily-driver-gap-analysis.md` — findings integrated into Product Decisions, the Daily-Driver Gate, the new System Services section, the Window Manager protocol/power gates, the Shell provider work, and the missing-app specs in P6.

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

### Recommended revisions to product decisions (from 2026-06-14 gap analysis)

These revise the decisions above. They are recommendations for the owner to accept or reject; the gate below is written assuming acceptance. Each carries a one-line rationale.

- **Adopt a System Services layer as a first-class workstream (new, accept).** D-Bus integration, seat/session management, and portals are prerequisites for most Shell and app gates. They are currently scattered across Shell provider notes and a single "portal direction" backlog line. Promote them to the SVC-* workstream and schedule them early. Rationale: the Shell status providers, file dialogs in third-party apps, screen sharing, suspend, and tray apps all depend on this layer; without it those gates cannot close.
- **Pull screen lock, logout, and suspend/power into the gate (revises "intentionally outside the gate").** Rationale: a machine you cannot lock, suspend, or cleanly log out of is not a daily driver. These are small once logind + session-lock + PAM exist (SVC-3, WM-11, the lock app). Greeter/login and crash-restart policy can stay deferred.
- **Pull XWayland into the gate as a late item (revises "later explicit product decision").** Rationale: keep pure-Wayland as the bring-up target, but a daily driver must run the long tail (some games, proprietary tools, older Electron, and screen-share fallbacks). Schedule it after the Wayland-native path is solid (WM-20), not at the end of everything.
- **Workspaces / virtual desktops: explicit decision required (still open).** Not currently planned. Many users consider them table-stakes. Recommendation: commit to `ext-workspace-v1` + a Shell switcher as a post-gate fast-follow, or explicitly state Lambda ships without workspaces in v1.
- **Touch and IME: explicit decision required (still open).** Touch and input methods remain out of scope. Recommendation: keep deferred unless touchscreen/2-in-1 hardware or non-Latin input is a target audience; if so, schedule `wl_touch` and `text-input-v3`/`input-method-v2` as a separate workstream.
- **Toolkit theming bridge (new, accept).** Without the portal Settings interface (+ XSettings/gsettings fallback), external GTK/Qt apps will not follow the desktop theme, dark-mode, cursor, or font. Tracked as SVC-15. Rationale: visual coherence of the whole desktop, not just Lambda apps.

## Ownership Boundaries

- Window Manager owns KMS/Wayland composition, output/scale, window state/focus, input routing, layer-shell placement, compositor chrome, screenshots, compositor-drawn screenshot UI, protocol validation, **and the compositor-side implementation of session-lock, client screen-capture, idle-notify, DPMS/output power, foreign-toplevel, output-management, data-control, and gamma/color protocols (WM-11…WM-21).**
- Shell owns persistent desktop chrome: dock, command launcher, app presentation, status docklets/quick settings, notifications, clipboard-history UI/policy, Shell preferences, user-facing desktop navigation, **the system-tray (StatusNotifierItem) host UI, the power/session menu UI, media-key/OSD behavior, and the lock-screen front-end** (authentication and the lock protocol client live with the lock app/SVC).
- Settings owns GUI editing for owner config files and truthful system/about status. The owning process remains the runtime authority.
- Files owns safe local file management, trash-first delete, selection, clipboard file operations, open-with UI, file icons, current-folder refresh/watch behavior, **and removable-volume presentation/mount/unmount via the udisks2 service (SVC-14).**
- Terminal owns pty/libvterm behavior, scrollback, keyboard/input encoding, selection/copy/paste, terminal rendering, terminal preferences, and terminal desktop integration.
- **System Services (new) owns the cross-cutting desktop backends that have no single app home: the D-Bus capability, seat/session management (libseat/logind), the xdg-desktop-portal backend, the notifications daemon, the StatusNotifierWatcher, the polkit authentication agent, the NetworkManager/BlueZ/UPower/MPRIS/Secret Service/udisks2 clients, and the toolkit theming bridge. These are libraries and small service processes consumed by Shell, Settings, Files, and the lock app. They are not a second source of truth for component config.**
- Shared desktop services are still needed for app registry, icon theme, MIME/default-app/open-with, status providers, notifications, clipboard history, portals, secrets/keyring, and future file chooser/screenshot/screencast APIs.

## Project Snapshot

| Area | Current status |
| --- | --- |
| Lambda v5 UI runtime | Shipped. Retained mount, reactive graph, `Bindable` modifiers, `For`/`Show`/`Switch`. |
| App platforms | macOS Metal, Linux Wayland Vulkan, Linux KMS Vulkan. |
| Examples | Demo and Lambda app targets build through CMake when examples are enabled. |
| Window Manager | Core compositor is usable for dogfooding. Open: titlebar/content frame coherence, wlroots backlog, real-app validation, polish, **and the daily-driver protocol/power gates (session-lock, screen-capture, idle-notify, DPMS, foreign-toplevel, output-management) — not yet started.** |
| System Services | **Started. Basic `lambda::dbus` capability exists on Linux via `sd-bus` with bus connections, sync calls, signal matches, Unix-fd passing, object-path replies, object-path arrays, byte arrays/scalars/arrays, managed-object dictionaries, nested `a{sv}` metadata values, `Properties.GetAll`, `PropertiesChanged` emit/read helpers, simple property get/set, object export with basic introspection XML, portal/notification/SNI/NetworkManager/BlueZ/UPower/MPRIS/UDisks2 value shapes, and event-loop fd pumping. KMS now attempts libseat-managed DRM/input opens with direct-open fallback and dispatches libseat enable/disable callbacks into the existing VT release/acquire path. A basic `lambda::system::LogindClient` can issue logind power calls, hold fd-based inhibitors, discover the current session path, and subscribe to sleep/session lock signals against a tested D-Bus fixture; Shell exposes search-driven launcher actions for suspend, hibernate, restart, and power off through that client. A basic `lambda-portal` service exports `org.freedesktop.impl.portal.Settings` with appearance color-scheme/accent/contrast/reduced-motion. A basic `lambda-notifications` service exports `org.freedesktop.Notifications` with Notify/Close/GetCapabilities/GetServerInformation, in-memory history, and standard close/action signals. A basic `lambda-status-notifier-watcher` service exports `org.kde.StatusNotifierWatcher` with item/host registration, properties, signals, and owner-loss pruning. A basic `lambda::system::NetworkManagerClient` reads manager/device/Wi-Fi/AP status, formats Shell network/Wi-Fi labels, can toggle Wi-Fi enabled state, and can watch manager/device/AP property and device inventory changes; Shell network/Wi-Fi status now prefers NetworkManager with sysfs fallback, the network docklet can toggle Wi-Fi, and production Shell refreshes on those NetworkManager events through the D-Bus event pump. A basic `lambda::system::BlueZClient` reads adapter/device power and connection status, formats Shell Bluetooth labels, can toggle adapter power, and can watch adapter/device property and ObjectManager add/remove changes; Shell Bluetooth status now prefers BlueZ with rfkill/sysfs fallback, the Bluetooth docklet can toggle adapters, and production Shell refreshes on those BlueZ events through the D-Bus event pump. A basic `lambda::system::UPowerClient` reads display-device battery state and manager AC state, can watch display-device, manager, and device inventory changes, Shell battery status now prefers UPower with sysfs fallback, and production Shell refreshes on those UPower events through the D-Bus event pump. A basic `lambda::system::MPRISClient` discovers session media players, reads now-playing metadata/capabilities, can send transport/volume controls, can watch player property/seek/name changes, and Shell now exposes a media docklet with play/pause/skip actions that refreshes from the D-Bus event pump. A basic `lambda::system::UDisks2Client` reads drives/visible filesystem volumes, can mount/unmount/eject, can watch ObjectManager/property changes, and Files now shows mounted visible volumes in its sidebar with event-driven refresh. No FileChooser/Screencast portal, Shell tray host UI, polkit agent, or secrets client yet. Largest remaining greenfield gap.** |
| Shell | App registry, dock, IPC, config, icons, and status *fallback* exist; live notifications/clipboard/tray/network and most provider control remain, gated on System Services. |
| Settings | Real owner-config editor exists; live apply depends on Window Manager/Shell hot-reload; network/Bluetooth/display-output pages depend on System Services + output-management. |
| Files | Live file browser/manager exists; model layer is ahead of live UI for several commands; removable-media mounting depends on udisks2 (SVC-14). |
| Terminal | Usable for basic shell work; search UI, mouse reporting, and complex Unicode remain open. |
| Apps | Editor substantial but mid-gate; image viewer (`lambda-preview`) is a 232-line stub; screenshot tool, PDF viewer, archive manager, calculator, system monitor, media player not built. |

## Daily-Driver Gate

Lambda is not daily-driver complete until these gates are closed. The 2026-06-14 gap analysis inserts a System Services layer (P0.5) ahead of most Shell/app polish, because those gates depend on it, and pulls lock/logout/suspend and XWayland into the gate (see Product Decisions revisions).

| Priority | Area | Gate | Status |
| --- | --- | --- | --- |
| P0 | Window Manager | Visual stability and real-app compositor validation | Open: narrowed to validation/polish |
| P0.5 | System Services and Session | D-Bus capability; libseat/logind seat+power; portal backend (file chooser, settings, screencast); notifications daemon; tray host; idle-notify+DPMS; screen lock; polkit; network/audio/bluetooth/power/media/secrets/mounts | **Open: SVC-1 basic D-Bus capability started; SVC-2 basic libseat device path started; SVC-3 basic logind client and Shell launcher power actions started; SVC-4 basic portal Settings backend started; SVC-6 basic notification service started; SVC-7 basic watcher started; SVC-9 basic NetworkManager status client started; SVC-10 basic BlueZ status client started; SVC-11 basic UPower client started; SVC-12 basic MPRIS client started; SVC-14 basic UDisks2 client started; remaining services not started** |
| P1 | Shell | Live launcher providers, notifications, clipboard history, quick-settings providers, tray, power/session menu, media keys/OSD | Open (most items gated on P0.5) |
| P2 | Settings | Owner config editing plus live-apply clarity; network/Bluetooth/display-output pages | Mostly done for owner config; system pages gated on P0.5/WM-16 |
| P3 | Files | Safe file-management live UI beyond the model layer; removable-media mounting | Open |
| P4 | Terminal | Daily terminal UI completeness | Open |
| P5 | Compositor protocol/power | Session-lock, client screen-capture, idle-notify, DPMS, foreign-toplevel, output-management, data-control, gamma; XWayland | **Open: not started (WM-11…WM-21)** |
| P6 | Shared services and missing core apps | Shared desktop backends, real image viewer, screenshot tool, PDF viewer, archive manager, calculator, system monitor, media player | Editor backlog integrated; image viewer is a stub; others not built |

Recommended sequencing (phases, not strict serialization — many SVC items parallelize once the D-Bus capability exists):

- **Phase A — make it run as a session:** SVC-2 (libseat/logind seat), SVC-1 (D-Bus capability). Basic SVC-1 and SVC-2 slices are now in tree; target-hardware validation and finer session/device lifecycle work remain before this phase is closed.
- **Phase B — make the session safe and modern:** SVC-3 (logind power/inhibit) + WM-13 (idle-notify) + WM-14 (DPMS); WM-11 (session-lock) + lock app + PAM; WM-12 (screen capture) + SVC-5 (screencast portal) + SVC-4 (portal file chooser/settings); SVC-6 (notifications daemon). Basic SVC-3 client plumbing with current-session discovery and search-driven Shell launcher power actions, SVC-4 Settings backend plumbing, and SVC-6 notification service plumbing are now in tree; full Shell power/session menu, Shell/lock/idle integration, real logind validation, Shell notification UI, and FileChooser/Screencast portal work remain.
- **Phase C — connectivity and polish:** SVC-9 (NetworkManager) + Shell Wi-Fi; SVC-7 (tray host); SVC-8 (polkit); SVC-10/11/12/13/14 (BlueZ/UPower/MPRIS/secrets/udisks); SVC-15 (theming bridge); WM-15/16/17/18 (foreign-toplevel/output-management/data-control/gamma). The basic SVC-7 watcher/registry daemon, SVC-9 status client with Shell manager/device/AP event refresh, SVC-10 status client with Shell property/ObjectManager event refresh, SVC-11 display-device battery client with Shell display-device/manager event refresh, SVC-12 media client and Shell docklet/control path with event refresh, and SVC-14 removable-volume client with Files mounted-volume sidebar/event refresh are now in tree; Shell tray rendering, item property/menu hosting, AP/connect flows, Bluetooth pairing/connect flows, richer Shell now-playing/media keys, and full Files volume mount/eject UI remain.
- **Phase D — finish apps and the long tail:** Files (P3), Terminal (P4), Editor gate; real image viewer (IMG-*) and screenshot tool (SHOT-*); WM-20 (XWayland); then optional apps (PDF, archive, calculator, system monitor, media) and the workspaces/touch/IME decisions.

## Detailed Workstream Index

The old readiness specs used component-specific workstream IDs. This section keeps those workstreams in the roadmap so no active spec work is lost. SVC-* and WM-11…WM-21 and the new app IDs are added from the 2026-06-14 gap analysis.

System Services and Session (new):

- SVC-1: D-Bus capability in the framework (session + system bus, calls, signals, properties, object export, event-loop integration).
- SVC-2: Seat/session management via libseat (logind/seatd), VT switching, unprivileged device access.
- SVC-3: logind client — suspend/hibernate/poweroff/reboot, lid/power-key/idle inhibitors, sleep signals, lock/unlock session.
- SVC-4: xdg-desktop-portal backend — FileChooser, Settings (color-scheme/accent), OpenURI, Inhibit, Account, Notification routing.
- SVC-5: Screencast/RemoteDesktop portal + PipeWire stream (depends on WM-12 capture + dmabuf export).
- SVC-6: Notifications daemon (`org.freedesktop.Notifications`) backing Shell SH-8.
- SVC-7: StatusNotifierWatcher + tray host + DBusMenu rendering (Shell SH bar).
- SVC-8: polkit authentication agent.
- SVC-9: NetworkManager client (Wi-Fi/ethernet/VPN) for Shell quick settings + Settings network page; basic read-only Shell status slice started.
- SVC-10: BlueZ client (adapters, discovery, pairing agent, connect); basic read-only Shell status slice started.
- SVC-11: UPower client (rich battery/AC events; augments sysfs).
- SVC-12: MPRIS controller + media-key bindings + Shell now-playing; basic client/control slice started.
- SVC-13: Secret Service / keyring (bundle or implement; libsecret-compatible).
- SVC-14: udisks2 client — removable volumes, mount/unmount/eject, Files integration; basic client/control slice started.
- SVC-15: Toolkit theming bridge (portal Settings + XSettings/gsettings dark-mode/cursor/font).

Window Manager (existing WM-1…WM-10 plus new protocol/power gates):

- WM-1…WM-10: as previously defined (idle/frame scheduling, resize/snap stability, geometry/state invariants, chrome/decorations/glass/shadows, output selection/scale, input/keyboard/cursor, screenshot readiness, protocol validation, config contract, real-app validation).
- WM-11: `ext-session-lock-v1` (compositor side; secure lock surfaces, input isolation, crash-stays-locked).
- WM-12: Client screen capture — `wlr-screencopy-v1` (tooling) and `ext-image-copy-capture-v1` + capture sources (modern), with dmabuf export for screencast.
- WM-13: `ext-idle-notify-v1` (idle timers driving auto-lock/dim/blank).
- WM-14: DPMS / real output power-off on KMS, tied to idle-notify and logind idle-action.
- WM-15: `wlr-foreign-toplevel-management-v1` (or `ext-foreign-toplevel-list-v1`) for docks/switchers.
- WM-16: Output management protocol (`wlr-output-management-v1` / `ext-output-management`) for GUI display config (Settings ST-5, kanshi).
- WM-17: `wlr-data-control-v1` / `ext-data-control-v1` for external clipboard managers.
- WM-18: `wlr-gamma-control-v1` and/or `wp-color-management-v1` (night light / color temperature / calibration).
- WM-19: Touch input (`wl_seat` touch capability + dispatch) — gated on the touch product decision.
- WM-20: XWayland (rootless X11 server integration, window mapping, clipboard bridge, DnD bridge, scaling).
- WM-21: Misc protocol fills — `keyboard-shortcuts-inhibit-v1`, `security-context-v1`, `xdg-foreign`, `tearing-control`, `content-type`, `single-pixel-buffer`, `alpha-modifier` as consumers appear.

Shell: SH-1…SH-13 (as previously defined; SH-8 notifications, SH-9 clipboard history, SH-6/SH-7 status/quick settings now consume SVC backends; new SH items for tray host UI, power/session menu, media-key/OSD, lock front-end are tracked under SH-7/SH-11 and SVC-6/7/12 and WM-11).

Settings: ST-1…ST-10 (as previously defined; ST-5 display gains WM-16 dependency; new network/Bluetooth/sound pages gain SVC-9/10/audio dependencies).

Files: FI-1…FI-14 (as previously defined; add removable-media mounting via SVC-14 under FI-10/new FI item).

Terminal: TE-1…TE-13 (as previously defined).

Editor: ED-1…ED-8 (as previously defined).

Missing/expanded apps (new):

- IMG-*: Image viewer (replace `lambda-preview` stub with a real viewer).
- SHOT-*: Screenshot tool front-end (on the compositor capture + portal Screenshot).
- PDF-*: PDF/document viewer.
- ARC-*: Archive manager.
- CALC-*: Calculator.
- MON-*: System monitor.
- MED-*: Media player.

---

## P0 Window Manager

Current implementation:

- `lambda-window-manager` owns a selected KMS output, runs a Wayland server, renders through Vulkan/Canvas, and hosts Lambda plus normal Wayland apps.
- Core idle behavior, Lambda app disconnect handling, shell focus restoration, output selection/scale, cursor config, keyboard config, screenshot modes, in-tree protocol demos, config defaults, compositor CPU/pacing traces, real-app smoke tooling with optional owned-compositor trace collection, Wayland registry validation, source/list drift checking for advertised globals, and the WM-COMP-27 real-app validation matrix exist.
- Screenshot full-output, active-window, and region capture are implemented with compositor-owned region UI. (Note: this is the compositor's own capture; client-facing capture for external tools and screencast is WM-12.)
- Protocol work includes layer-shell, xdg-shell, xdg-output, viewporter, cursor-shape, fractional-scale, activation, presentation-time, relative pointer, pointer constraints, primary selection, clipboard/data-device, idle inhibit, and background-effect paths.
- The active wlroots comparison plan has verified core surface-state slices through WM-COMP-27 (see [compositor-wlroots-improvement-plan.md](compositor-wlroots-improvement-plan.md)).
- Firefox dogfooding blockers addressed in the tested paths; fullscreen shell-panel behavior exists for the single-output desktop; KMS presentation has a frame queue, improved pacing, direct scanout/overlay for video, and hardware-cursor motion that does not force scene redraws.

Open gate (visual stability and validation — unchanged):

- Resolve the deferred system-titlebar/content frame-coherence issue on DP-1 HiDPI resize.
- Finish the remaining wlroots comparison backlog: broader non-popup seat/grab workflow parity and a visual regression/real-app harness.
- Continue resize/snap/maximize/restore validation across GTK/Qt/terminal apps.
- Complete `TODO.md` WM items TODO-006 (close animation snapshot), TODO-007 (minimized state + dock-preview handoff), TODO-008 (live resize frame coherence).
- Complete live real-app validation (Lambda apps, browser, GTK, Qt, `foot`, clipboard, menus/popups, maximize/restore/snap/minimize, screenshots, fullscreen video, mpv, long idle).
- Keep popup grabs config-gated until hardware validation; keep unsupported touch/tablet non-advertised until WM-19.

Open gate (daily-driver protocol and power gates — new, WM-11…WM-21):

These are the compositor-side prerequisites for a usable daily desktop. They are not started. Detailed implementation:

- **WM-11 — `ext-session-lock-v1`.** Implement the lock manager global. On `lock`: stop presenting normal client surfaces, present only the per-output lock surfaces the lock client commits, route all input exclusively to lock surfaces, and refuse to unlock until the client calls `unlock_and_destroy`. Crash safety is mandatory: if the lock client dies while locked, the compositor must keep the outputs secured (solid/blanked, no client content) rather than revealing the desktop. Provide one lock surface per output and reconfigure on output hotplug. Acceptance: `loginctl lock-session` (via SVC-3) or the Shell lock action shows only the lock UI; killing the lock client keeps the screen secure; unlock restores the prior scene.
- **WM-12 — client screen capture.** Implement `wlr-screencopy-v1` for the existing tooling ecosystem (grim/slurp/wf-recorder) and `ext-image-copy-capture-v1` with output and foreign-toplevel capture sources for the modern path. Support SHM and dmabuf capture; reuse the existing internal screenshot pipeline for pixel readback. Expose dmabuf-exported frames so SVC-5 can feed PipeWire without an extra copy. Acceptance: `grim` saves a correct screenshot; `slurp | grim -g -` captures a region; a dmabuf frame can be imported by the screencast path.
- **WM-13 — `ext-idle-notify-v1`.** Implement the idle-notifier global. Track input activity per seat; fire `idled` after each client's requested timeout and `resumed` on activity. This is the signal source for auto-lock, auto-dim, and DPMS-off. Honor active idle-inhibitors (already implemented) by suppressing idle while inhibited. Acceptance: a client idle timer fires after the configured interval and resumes on input; an active inhibitor prevents firing.
- **WM-14 — DPMS / output power-off.** On KMS, actually power off the panel at idle (atomic CRTC disable or the DPMS connector property), not just paint black. Drive from WM-13 idle and from logind idle-action (SVC-3). Restore on input. Replace the current software-only blanking noted in `compositor.md`. Acceptance: after idle, the backlight turns off and the panel powers down; input wakes it; no scene corruption on restore.
- **WM-15 — foreign-toplevel management.** Implement `wlr-foreign-toplevel-management-v1` (and/or `ext-foreign-toplevel-list-v1`) so a separate client (dock, alt-tab switcher, taskbar) can enumerate toplevels, observe title/app-id/state, and request activate/minimize/maximize/close. This also cleans up the Shell/compositor split (the dock can stop relying on private state). Acceptance: an external taskbar lists windows and can focus/close them; the Shell dock can be reworked onto it.
- **WM-16 — output management protocol.** Implement `wlr-output-management-v1` (broad tool support) and/or `ext-output-management`. Expose heads/modes/position/scale/transform and apply configurations transactionally with a test/apply/revert cycle. This backs Settings ST-5 (display page) and tools like kanshi/wdisplays. Acceptance: `wlr-randr` lists and changes modes/scale/position; Settings display page drives real changes; a bad config reverts.
- **WM-17 — data-control.** Implement `wlr-data-control-v1` / `ext-data-control-v1` so clipboard-history and `wl-clipboard` tools can observe and set selections out-of-band. Complements the Shell's internal clipboard-history model (SH-9). Acceptance: `wl-paste --watch` sees clipboard changes; `wl-copy` sets them.
- **WM-18 — gamma / color.** Implement `wlr-gamma-control-v1` (and consider `wp-color-management-v1`) for night-light/color-temperature and basic calibration via gammastep/wlsunset. Acceptance: gammastep warms the display on a schedule; resetting restores.
- **WM-19 — touch input (gated on product decision).** Advertise `WL_SEAT_CAPABILITY_TOUCH`, dispatch `wl_touch` down/up/motion/frame/cancel, and route to surface hit-testing. Only schedule if touch hardware is a target. Acceptance: a touchscreen drives taps/drags to clients.
- **WM-20 — XWayland (gated on product decision; scheduled late).** Integrate a rootless XWayland server: launch and supervise Xwayland, map X11 windows into the scene as toplevels, bridge clipboard and primary selection both ways, bridge DnD, handle X11 override-redirect/popups, and apply per-window scaling. Acceptance: an X11-only app (e.g., an older game or tool) runs, is focusable/movable, copy-paste works to/from Wayland apps, and DnD works.
- **WM-21 — protocol fills.** Add `keyboard-shortcuts-inhibit-v1` (VMs/remote-desktop/games), `security-context-v1` (Flatpak/portal policy), `xdg-foreign` (cross-client dialog parenting), and perf protocols (`tearing-control`, `content-type`, `single-pixel-buffer`, `alpha-modifier`) as real consumers appear. Acceptance: per-protocol conformance demo + a real client that needs it.

Validation (additions for WM-11…WM-21):

- Deterministic tests for: session-lock state machine (locked/unlocked, surface-per-output, crash-stays-locked), idle-notify timer/inhibitor logic, output-management transaction test/apply/revert, capture frame plumbing (SHM and dmabuf), foreign-toplevel state transitions, and data-control selection events.
- Manual hardware: lock/unlock with PAM, DPMS power-off/restore on the target panel, `grim`/`slurp`/`wf-recorder`/OBS capture, gammastep, `wlr-randr`/kanshi output config, and (if scheduled) XWayland real-app matrix.

Deferred (revised):

- Multi-output desktop *layout* management beyond the single-output foundations (basic multi-output presentation should be validated as part of WM-16).
- Workspaces / virtual desktops (`ext-workspace-v1`) — pending the workspaces product decision.
- Greeter/login and crash-restart policy (session lock/logout/suspend are now in-gate via WM-11 + SVC-3 + the lock app).

---

## P0.5 System Services and Session Integration  *(new — top new priority)*

This layer is mostly greenfield. A basic Linux `lambda::dbus` capability is now in tree, the KMS backend now tries libseat-managed DRM/input opens before falling back to direct opens, `lambda-portal` covers the portal Settings backend, `lambda-notifications` covers the first Freedesktop notification service slice, `lambda-status-notifier-watcher` covers the first tray watcher/registry slice, `lambda::system::NetworkManagerClient` covers the first network-status client slice, `lambda::system::BlueZClient` covers the first Bluetooth-status client slice, `lambda::system::UPowerClient` covers the first power-status client slice, `lambda::system::MPRISClient` plus Shell docklet wiring covers the first media-status/control slice, and `lambda::system::UDisks2Client` covers the first removable-volume client slice. There is still no FileChooser/Screencast portal backend, Shell tray host UI, polkit agent, or secrets integration. The Shell's existing provider notes already name these D-Bus services as consumers — this section provides the shared capability and service implementations they depend on.

Process/library shape:

- Add a framework-level `lambda::dbus` capability (recommended backend: `sd-bus` from systemd/elogind — present on the target distro, ergonomic, integrates with an external event loop via `sd_bus_get_fd`/`sd_bus_process`/`sd_bus_get_events`; alternative: `sdbus-c++`). It must support: connecting to the session and system buses, async + sync method calls, signal subscription, property get/set/watch, and exporting objects/interfaces (needed for being a service: notifications, StatusNotifierWatcher, portal backend, polkit agent).
- First-party service processes where a daemon boundary is cleaner than in-process: a `lambda-portal` backend binary, optionally a `lambda-polkit-agent`, and the lock app `lambda-lock`. Bus clients (logind, NetworkManager, BlueZ, UPower, MPRIS, udisks, secrets) can be in-process libraries consumed by Shell/Settings/Files.

Workstreams:

- **SVC-1 — D-Bus capability.** Basic slice started: `lambda::dbus` opens session/system/custom-address buses through `sd-bus`, exposes sync method calls with basic-value arguments including Unix fds, object paths, object-path arrays, string arrays, byte arrays/scalars/arrays, RGB tuples, empty `a{sv}` hints dictionaries, variant replies, `a{sv}` property dictionaries, namespaced portal Settings dictionaries, managed-object dictionaries for ObjectManager, nested `a{sv}` values for metadata, signal matching, simple property get/set/`GetAll`, `PropertiesChanged` emit/read helpers, object export with method/property handlers and basic `Introspectable` XML, signal emission, unique-name lookup, fd/events/process/flush helpers, and `BusEventPump` integration with `Application::registerEventPollSource`. Focused integration tests cover `Peer.Ping`, exported method calls, exported-object introspection, Unix-fd round trips, object-path replies, exported property get/set/GetAll, `PropertiesChanged` helpers, signal delivery, logind `GetSessionByPID`, portal Settings D-Bus shapes, notification D-Bus shapes, StatusNotifierWatcher D-Bus shapes, NetworkManager device/AP D-Bus shapes, BlueZ ObjectManager shapes, MPRIS metadata shapes, UDisks2 block/filesystem shapes, and UPower display-device D-Bus shapes against a private real bus when `dbus-daemon` can run. Remaining finer details: async calls, broader generic array/dictionary/variant support beyond the currently needed service shapes, detailed method argument introspection metadata, additional service-client fixtures, and compositor-runtime fd pumping.
- **SVC-2 — Seat/session via libseat.** Basic slice started: `lambda-window-manager` links `libseat`, opens a seat at KMS startup when available, opens DRM and libinput devices through `libseat_open_device` with direct-open fallback, tracks seat-managed device IDs for close, routes the libseat fd through the KMS poll loop, and maps `enable_seat`/`disable_seat` callbacks onto the existing DRM-master/libinput suspend/resume VT paths. Remaining finer details: close/reopen all seat-managed devices on enable as libseat recommends, implement `libseat_switch_session`, remove/limit direct-open fallback once hardware validation passes, and validate unprivileged launch + VT switch on target hardware. Acceptance: `lambda-window-manager` launches as an unprivileged user inside a logind session with no manual device permissions; Ctrl-Alt-F<n> switches VTs and returns cleanly; the GPU is released/reacquired without corruption.
- **SVC-3 — logind client.** Basic slice started: `lambda::system::LogindClient` connects through `lambda::dbus`, calls `Suspend`/`Hibernate`/`PowerOff`/`Reboot`, takes fd-based `Inhibit` locks, resolves session object paths through `GetSessionByPID`, subscribes to manager `PrepareForSleep`, and watches explicit or current-session `Lock`/`Unlock` signals. Shell command-launcher search now exposes suspend, hibernate, restart, and power-off actions through the client. `LogindTests.cpp` covers the power calls, inhibitor fd plumbing, current-session discovery, sleep signal, and session lock/unlock signals against a fake `org.freedesktop.login1` on a private bus, and `ShellModelTests.cpp` covers launcher action discovery. Remaining finer details: expand the full Shell power/session menu with confirmations and error states, wire the client into WM idle/DPMS behavior and the lock app, hold the real delay inhibitors for `handle-power-key`, `handle-lid-switch`, and `handle-suspend-key`, honor `IdleAction` together with WM-13, and validate against a real system bus. Acceptance: closing the lid suspends (and locks first); the power key triggers the Shell power menu; `loginctl lock-session` locks; resume-from-suspend shows the lock screen.
- **SVC-4 — xdg-desktop-portal backend.** Basic slice started: `lambda-portal` is a Linux session-bus service with installed D-Bus service metadata and `lambda.portal` selector metadata; it exports `org.freedesktop.impl.portal.Settings` at `/org/freedesktop/portal/desktop`, supports `Read`, `ReadAll`, `SettingChanged`, and `version`, and exposes `org.freedesktop.appearance` `color-scheme`, `accent-color`, `contrast`, and `reduced-motion`. Defaults are controlled by environment variables until Settings/Shell preferences grow the owning config. `PortalSettingsTests.cpp` covers the backend against a private bus, and a process-level private-bus smoke verifies the built `lambda-portal` service name and `Read` call. Remaining finer details: wire Settings/Shell-owned appearance preferences into the service, validate through the real `xdg-desktop-portal` frontend with GTK/Qt apps, implement FileChooser (Open/Save → a Lambda file-chooser surface, reusing `lambda-files` components), then OpenURI, Inhibit, Account, and Notification routing to SVC-6. Acceptance: Firefox/Flatpak "Open File" shows Lambda's chooser; a Flatpak app launches and can pick files; dark-mode preference propagates to a GTK app.
- **SVC-5 — Screencast/RemoteDesktop portal + PipeWire.** Implement `org.freedesktop.impl.portal.ScreenCast` (and `RemoteDesktop`) backed by WM-12 dmabuf capture, publishing frames through a `libpipewire` stream so browsers and conferencing apps can share the screen; provide a source-picker UI (whole output / window / region). Acceptance: Firefox/Chromium and OBS (PipeWire source) can share a screen/window via the portal.
- **SVC-6 — Notifications daemon.** Basic slice started: `lambda-notifications` is a Linux session-bus service with installed D-Bus activation metadata; it exports `org.freedesktop.Notifications` at `/org/freedesktop/Notifications`, implements `Notify`, `CloseNotification`, `GetCapabilities`, and `GetServerInformation`, stores an in-memory history with replacement support, parses actions, and emits `NotificationClosed` and `ActionInvoked`. `NotificationsTests.cpp` covers the spec methods/signals against a private bus, and a process-level private-bus smoke verifies the built daemon accepts a real `Notify` call through `gdbus`. Remaining finer details: wire the service into Shell's banner/notification-center UI and config, action click handling, DND suppression policy, timeout expiry, persistence/history grouping, icon/image hints, and portal Notification routing from SVC-4. Acceptance: `notify-send "x"` shows a banner; actions invoke; DND suppresses; history shows past notifications.
- **SVC-7 — StatusNotifierWatcher + tray host.** Basic slice started: `lambda-status-notifier-watcher` is a Linux session-bus service with installed D-Bus activation metadata; it exports `org.kde.StatusNotifierWatcher` at `/StatusNotifierWatcher`, implements `RegisterStatusNotifierItem`, `RegisterStatusNotifierHost`, `RegisteredStatusNotifierItems`, `IsStatusNotifierHostRegistered`, `ProtocolVersion`, `StatusNotifierItemRegistered`, `StatusNotifierItemUnregistered`, and `StatusNotifierHostRegistered`, supports path-only item registrations by using the sender unique name, and prunes registered services on `NameOwnerChanged`. `StatusNotifierWatcherTests.cpp` covers properties, `GetAll`, host/item registration signals, path-only registration, and owner-loss removal against a private bus, and a process-level private-bus smoke verifies the built daemon answers a real `ProtocolVersion` property query through `gdbus`. Remaining finer details: Shell tray host UI, reading `org.kde.StatusNotifierItem`/`org.freedesktop.StatusNotifierItem` properties, icon pixmap/name updates, `com.canonical.dbusmenu` menus, item activation/context/scroll actions, and real tray-app validation. Acceptance: Steam/Discord/Telegram/nm-applet-style tray icons appear and their menus work.
- **SVC-8 — polkit agent.** Register an `org.freedesktop.PolicyKit1.AuthenticationAgent` for the session with the polkit authority; present an authentication dialog; authenticate via `polkit-agent-helper-1`/PAM. Acceptance: a `pkexec`/privileged action prompts for a password and succeeds/fails correctly.
- **SVC-9 — NetworkManager client.** Basic slice started: `lambda::system::NetworkManagerClient` connects through `lambda::dbus`, calls `GetDevices`, reads manager `State`, `NetworkingEnabled`, `WirelessEnabled`, and `WirelessHardwareEnabled`, reads device `Interface`, `DeviceType`, and `State`, reads wireless `ActiveAccessPoint`, reads AP `Ssid`/`Strength`, formats connected/connecting/off/unavailable Shell labels, can set `WirelessEnabled`, and can watch manager/device/AP property changes plus device add/remove signals. Shell network/Wi-Fi status now prefers NetworkManager on the real `/sys` path, falls back to the existing sysfs scan when NetworkManager is unavailable, routes the network docklet primary action to Wi-Fi enabled toggling when hardware is present, and production Shell subscribes to those NetworkManager events through `BusEventPump` so global network/Wi-Fi enablement, active interface, SSID, and signal changes can refresh without waiting for the polling timer. `NetworkManagerTests.cpp` covers manager/device/AP reads, SSID/signal formatting, connecting/off mapping, Wi-Fi property writes, manager/device/AP change signals, and device add/remove signals against a fake `org.freedesktop.NetworkManager` on a private bus. Remaining finer details: access-point and saved-connection enumeration, active-connection/connectivity/VPN state, access-point connect/disconnect with a secrets path, Settings network page, and real system-bus validation. Acceptance: connect to a WPA2 network from the desktop; state updates live in the docklet.
- **SVC-10 — BlueZ client.** Basic slice started: `lambda::system::BlueZClient` connects through `lambda::dbus`, calls ObjectManager `GetManagedObjects`, reads adapter `Address`, `Alias`, `Powered`, and `Discovering`, reads device `Address`, `Alias`, `Name`, `Adapter`, `Paired`, and `Connected`, formats unavailable/off/on/connected-device Shell labels, can set adapter `Powered`, and can watch adapter/device property changes plus ObjectManager add/remove signals. Shell Bluetooth status now prefers BlueZ on the real `/sys` path, falls back to the existing rfkill/sysfs scan when BlueZ is unavailable, routes the Bluetooth docklet primary action to toggling known adapters powered on/off, and production Shell subscribes to those BlueZ events through `BusEventPump` so power/connection and adapter/device arrival/removal changes can refresh without waiting for the polling timer. `BlueZTests.cpp` covers adapter/device reads, status formatting, adapter power writes, unsupported ObjectManager property skipping, adapter change signals, and ObjectManager add/remove signals against a fake `org.bluez` on a private bus. Remaining finer details: discovery, pairing agent, pair/unpair, trust/block, connect/disconnect, Settings Bluetooth page, and real system-bus validation. Acceptance: pair and connect a Bluetooth device.
- **SVC-11 — UPower client.** Basic slice started: `lambda::system::UPowerClient` connects through `lambda::dbus`, calls `GetDisplayDevice`, reads display-device `IsPresent`, `Percentage`, `State`, `TimeToEmpty`, `TimeToFull`, and `IconName`, reads manager `OnBattery`, and can watch display-device `PropertiesChanged`, manager `PropertiesChanged`, `DeviceAdded`, and `DeviceRemoved`; Shell battery status now prefers UPower on the real `/sys` path, falls back to the existing sysfs scan when UPower is unavailable, and production Shell subscribes to those UPower events through `BusEventPump` so battery and AC/device changes can refresh without waiting for the polling timer. `UPowerTests.cpp` covers display-device reads, status formatting, display-device/manager property changes, and device add/remove signals against a fake `org.freedesktop.UPower` on a private bus, and Shell system-status tests continue to cover sysfs fallback. Remaining finer details: AC/lid/warning-level exposure, multiple-device summaries, power-mode policy, real system-bus validation, and richer quick-settings UI. Acceptance: the battery docklet updates on AC plug/unplug and percentage changes without polling.
- **SVC-12 — MPRIS + media keys.** Basic slice started: `lambda::system::MPRISClient` connects through `lambda::dbus`, discovers `org.mpris.MediaPlayer2.*` names through the session bus, reads root `Identity`/`DesktopEntry`, reads player `PlaybackStatus`, `Metadata`, `Volume`, `Position`, and capability properties, parses common metadata fields, formats a now-playing label, can send `PlayPause`, `Play`, `Pause`, `Stop`, `Next`, `Previous`, and `Volume` writes, and can watch player `PropertiesChanged`, `Seeked`, and MPRIS `NameOwnerChanged` events. Shell system status reads live MPRIS on the live system path, renders a media docklet, routes docklet primary/secondary/scroll actions to play/pause, next, and previous on the active controllable player, and production Shell subscribes to player changes through `BusEventPump` so media changes can refresh without waiting for the polling timer. `MPRISTests.cpp` covers player discovery, metadata parsing, active-player selection, status formatting, transport calls, volume writes, and property/seek/name change signals against a fake MPRIS player on a private bus. Remaining finer details: richer active-player selection, full Shell now-playing UI, XF86 media-key routing, art caching, progress updates, TrackList support, and real player validation. Acceptance: media keys control the active player; the bar shows the current track and can pause/skip.
- **SVC-13 — Secret Service / keyring.** Provide `org.freedesktop.secrets` (collections/items, encrypted store, unlock tied to login). Implementing the full spec is large; recommended first step is to bundle/run `gnome-keyring-daemon` (or a minimal compatible store) so libsecret-based apps work, then evaluate a native implementation. Acceptance: an app storing/reading a secret via libsecret succeeds; the store unlocks at login.
- **SVC-14 — udisks2 client + Files integration.** Basic slice started: `lambda::system::UDisks2Client` connects through `lambda::dbus`, calls ObjectManager `GetManagedObjects`, reads drives and visible filesystem block devices, decodes UDisks byte-array paths and `MountPoints`, filters hidden/system volumes, formats volume names, can call `Filesystem.Mount`, `Filesystem.Unmount`, and `Drive.Eject`, and can watch ObjectManager/property changes. Files now appends mounted visible volumes to the sidebar and refreshes those places from UDisks2 events through `BusEventPump`. `UDisks2Tests.cpp` covers visible-volume filtering, drive/volume metadata, mount point decoding, mount/unmount calls, eject calls, and ObjectManager/property change signals against a fake `org.freedesktop.UDisks2` on a private bus; `FilesStoreTests.cpp` covers mounted-volume sidebar placement. Remaining finer details: unmounted volume presentation, mount/unmount/eject UI and errors, encrypted volumes, job progress, optional auto-mount policy, and real removable-media validation. Acceptance: insert a USB stick → it appears in Files → open → unmount/eject safely.
- **SVC-15 — Toolkit theming bridge.** Drive external GTK/Qt appearance: expose color-scheme/accent/cursor/font via the portal Settings interface (SVC-4) and provide an XSettings/gsettings fallback where needed, so toolkit apps match the desktop and follow dark-mode. Acceptance: switching the desktop to dark mode flips a GTK app and a Qt app to dark; cursor theme/size and default font propagate.

Validation:

- Deterministic: D-Bus call/signal/property/export round trips against a test bus; logind inhibitor/sleep-signal logic; notifications daemon spec messages; SNI watcher registration/host; portal request/response shapes; NetworkManager/BlueZ/UPower/udisks state mapping from fixtures.
- Manual hardware: libseat unprivileged launch + VT switch; lid/power-key/suspend; `notify-send` and real app notifications; tray-using apps; `pkexec` prompt; Wi-Fi connect; Bluetooth pair; battery events; media keys; portal file dialog + screen share in a browser; USB mount/eject; dark-mode propagation.

Deferred:

- Full Secret Service native implementation if bundling gnome-keyring initially.
- GeoClue/location, online-accounts, color-calibration management, and printing-service integration (CUPS) beyond what the Editor/PDF print abstraction needs.

---

## P1 Shell

Current implementation:

- `lambda-shell` creates layer-shell dock, command launcher, and dock menu surfaces.
- Shell IPC uses request IDs and structured parse/serialization helpers; Shell exits cleanly when the Window Manager IPC is unavailable.
- Shared app registry discovers local Lambda executables and installed `.desktop` entries, with app-id aliases and launch resolution shared by Shell and Window Manager.
- Dock pins load from Shell config; running unpinned apps appear; dock click launches/focuses/restores; dock context menu supports new window, pin/unpin, and quit where wired.
- Icon theme lookup is used by dock and launcher with Material glyph fallback.
- Docklet status rendering shows real values when present and unavailable/unknown honestly; network/Wi-Fi, Bluetooth, and battery now prefer basic system-service clients with sysfs fallback.
- Quick-status popup exists, but provider controls are disabled/unavailable.
- Notification and clipboard-history models/config/policies exist and are tested, but live UI is not implemented.
- Advanced launcher provider models exist; production launcher still renders/activates dock/app results.
- Shell config is generated, parsed, hot-reloaded, and covers appearance, dock, quick settings, notifications, clipboard history, and launcher policy.

Open gate:

- Wire the production launcher to the richer provider model and activation path (apps, windows, Settings panels, Shell actions, empty/error states); add ranking to the live launcher.
- Implement live notifications **on top of SVC-6**: banners, center/history, grouping, dismissal, clear-all, actions, DND.
- Implement clipboard history capture/picker/paste/clear with memory-only-by-default persistence; back out-of-band capture with **WM-17 (data-control)** where external sources matter.
- Add/complete real Shell-owned status providers **on top of SVC backends**: network/Wi-Fi (basic SVC-9 status exists; live events/controls remain), Bluetooth (basic SVC-10 status exists; live events/controls remain), audio/volume (PipeWire/`wpctl`, already partial), battery/power (SVC-11 + sysfs), brightness (backlight via logind/udev policy), do-not-disturb.
- Add quick-settings controls for providers that can actually be controlled; add per-row Settings links.
- **New (gap analysis):** add the system-tray host UI consuming SVC-7; finish the power/session menu (lock/logout/suspend/restart/shutdown) consuming SVC-3 + WM-11 beyond the basic search-driven launcher power actions; media-key handling + now-playing via SVC-12; a volume/brightness OSD; and the lock-screen front-end (the lock UI rendered by `lambda-lock` using WM-11 + PAM).
- Confirm/add a wallpaper surface (layer-shell background) and an alt-tab/window switcher (consuming WM-15).
- Complete manual validation with Lambda apps and selected external Wayland apps.

Deferred:

- Full freedesktop notification spec parity beyond SVC-6's covered set.
- Multi-output Shell layout beyond preserving clean models.
- Files/recent-documents launcher providers.
- Workspaces switcher — pending the workspaces product decision.

### Shell Status And Quick Settings Detail

Status and quick settings are Shell-owned. The Window Manager may include snapshot fields for compatibility, but it must not become the audio, network, Bluetooth, battery, brightness, notification, clipboard, or quick-settings backend. The backends themselves live in System Services (SVC-*); the Shell consumes them.

Provider model requirements:

- Providers expose typed snapshots with availability: unavailable, read-only, or writable.
- Visible state must be real or explicitly unavailable; never show fake connected, charged, unmuted, or active values.
- Provider failures must not freeze Shell; slow providers update independently.
- Shell config controls quick-settings order through `quick_settings.modules`.

Provider backends (now mapped to SVC-*):

- Clock: honor `dock.clock_format`, update conservatively, avoid redraws when text is unchanged.
- Battery/power: SVC-11 (UPower) for events; `/sys/class/power_supply` as the fallback already implemented. Read-only until a power-mode backend exists.
- Brightness: `/sys/class/backlight`; enable the slider only with a real write path (logind/udev policy or a small privileged helper).
- Audio: PipeWire/WirePlumber (the `wpctl`/`pactl` subprocess path is the current fallback); add volume slider + mute when control is real; mic mute/output picker later.
- Network/Wi-Fi: SVC-9 (NetworkManager). Basic Shell status prefers NetworkManager with `/sys/class/net` fallback, and the network docklet can toggle Wi-Fi enabled; access-point connect/disconnect and full management remain for Settings/quick settings.
- Bluetooth: SVC-10 (BlueZ). Basic Shell status prefers BlueZ with rfkill/sysfs fallback, and the Bluetooth docklet can toggle adapter power; pairing and device management remain for Settings/quick settings.
- Media: SVC-12 (MPRIS). Basic client/status/control plumbing exists, and the dock can show now-playing with play/pause/skip actions; richer now-playing UI, event-driven updates, and media-key routing remain.
- Notifications: SVC-6 + Shell UI.
- Clipboard history: Shell model + optional WM-17 for external sources.

Docklet, quick-settings, Shell-actions, and testing requirements: as previously specified (network/Wi-Fi, Bluetooth, volume, battery, notifications, clipboard, clock docklets; quick-settings header + controls + per-row Settings links; `open-settings:*` and `toggle-dnd` actions; snapshot/icon/ordering/availability/clock/sysfs/quick-settings/DND/clipboard tests; manual checks across battery/no-battery, PipeWire, internal/external brightness, NetworkManager and BlueZ states, notification center, and clipboard privacy). Add manual checks for the tray host (real tray apps), the power/session menu (lock/suspend/logout), media keys, and the lock front-end.

---

## P2 Settings

Current implementation:

- `lambda-settings` opens as a system-titlebar Lambda app; reads/writes Window Manager and Shell owner config files directly; generates owner configs with defaults; schema metadata exists; writes are atomic and validated; unknown keys preserved where practical.
- Appearance edits real background/wallpaper/glass and Shell icon/reduced-motion config; Display edits selected output and scale; Input edits keyboard layout/repeat and close-window shortcuts; Windows edits animations, hardware cursor, cursor theme/size, idle blank timeout, screenshot shortcuts; Files edits hidden files/default view/sorting/grid icon size/Trash visibility; Dock & Panel edits Shell dock/quick-settings; Notifications edits Shell notification config; Launcher & Clipboard edits Shell launcher/clipboard config; About/System shows real or explicitly unavailable values.
- Save/revert/reset/error UX exists; card-backed groups, responsive rows, framework theme tokens; detailed UX plan in [lambda-settings-ux-plan.md](lambda-settings-ux-plan.md).

Open gate:

- Ensure hot-reloadable changes apply live where the owning process supports them; keep apply-mode labels accurate; avoid claiming unavailable system providers are live.
- Add Settings UI for Terminal preferences, MIME/default-app editing, and shared-service settings after those backends exist.
- **New (gap analysis):** once SVC-9/SVC-10 land, add full Network (saved networks, VPN, password entry) and Bluetooth (pairing/device management) pages. Once WM-16 lands, make the Display page drive real output configuration (modes/scale/position/transform, multi-output arrangement) via the output-management protocol. Add a Lock/Power/Idle page (idle-to-lock/dim/sleep timeouts wired to WM-13/WM-14/SVC-3) and a Default Applications page (MIME/open-with) once the shared MIME service exists.

Validation: as previously specified (schema, validation, dirty/revert/reset, WM/Shell/Files round trips, unknown-key preservation, atomic-write failure, shortcut conflicts, wallpaper normalization, theme discovery, system-info fixtures; manual coverage of all pages). Add: display-output apply/revert via WM-16; network/Bluetooth pages against SVC-9/10; lock/power/idle timeout wiring.

Deferred:

- Users/accounts, package/update manager, privacy/portal permission editor (until the portal backend exposes a permission store), printing configuration.

---

## P3 Files

Current implementation:

- `lambda-files` opens as an integrated-titlebar Lambda app with glass background.
- Live UI supports sidebar places, breadcrumbs, back/forward/up, grid/list view, hidden-file toggle, multi-select/range/select-all/clear, activation, create folder/file, copy/cut/paste, duplicate, trash-first delete, reveal, default open, context menus, and periodic refresh.
- Preferences persist for hidden files and view mode; clipboard uses internal copy/cut state plus `text/uri-list`; default open uses the shared app registry + MIME/default-app data; icon theme lookup with fallback.
- Model layer has path normalization, navigation history, breadcrumbs, stable sort, search/filtering, refresh diffs, selection preservation, rename validation, copy/move/duplicate, trash metadata, restore helpers, conflict decisions, progress/failure/cancel state, safe undo helpers, MIME/default-app fixtures, icon lookup, and preference load/save.

Open gate:

- Add direct text path entry; rename UI; Trash view/restore UI (or explicitly defer restore while keeping delete trash-first); undo UI; operation progress/cancel UX + partial-failure reporting; conflict prompts (keep both/replace/skip/cancel); current-folder search UI; visible sort controls; explicit open-with chooser UI; native watcher integration; Lambda-level file drag source/target before advertising DnD.
- **New (gap analysis):** removable-media support via SVC-14 — show mounted/removable volumes in places, mount on open, and unmount/eject safely; auto-mount on insert is optional and policy-gated.

Validation: as previously specified (model, operations, trash, selection, preferences, open-with fixtures, MIME parsing, URI-list clipboard, icon fallback, grid layout). Add: removable-volume enumerate/mount/unmount against SVC-14 fixtures; manual USB insert/open/eject.

Deferred:

- Advanced default-app editing, thumbnails, tabs, split panes, indexed search, network shares, archive browsing (see ARC-* for a standalone archive manager), full document viewer (see PDF-*), and full text editor.

---

## P4 Terminal

Current implementation:

- `lambda-terminal` uses `forkpty`, libvterm, UTF-8 mode, alternate screen, nonblocking pty I/O, event-loop wake thread, row damage, row/layout caching, resize via `TIOCSWINSZ`, title updates, and child cleanup.
- Live UI supports text input, key encoding, resize, glass/solid background, colors, bold/italic/underline/reverse/strikethrough, cursor, scrollback viewport (wheel + `Shift+PageUp/Down`), scrollback-aware selection/copy, clipboard copy/paste, bracketed paste, and profile/preferences persistence.
- `TerminalCore` covers input encoding, application cursor/keypad, focus encoding, SGR mouse helper, mouse-to-cell mapping, resize calc, Unicode width helpers, color/attribute resolution, preferences/profiles, scrollback model, selection reconstruction, search helper, URL detection, app identity, and browser command planning.

Open gate:

- Add live search UI (query, match highlight, next/prev, close, scrollback search); wire mouse reporting (SGR/basic/motion/wheel) while preserving selection when off; improve Unicode/grapheme handling (combining marks, complex emoji); add primary selection support or explicitly defer it in the UI; OSC 52 policy; URL opening UI/policy; continue splitting pty/session/model/renderer/UI; cursor shape/color-scheme preferences if rendering supports them.

Validation: as previously specified (core model, input, scrollback, selection, Unicode helpers, color, resize, config, preferences, pty smoke).

Deferred:

- Tabs, split panes, multiplexing, SSH connection manager, serial terminal UI.

---

## P6 Shared Services And Missing Core Apps

Shared desktop services are now specified under **P0.5 System Services (SVC-1…SVC-15)**. The shared-service backlog items below map to SVC-* and to the shared MIME/default-app/open-with and icon/thumbnail services:

- App registry service boundaries and permission model — exists (shared registry); formalize boundaries.
- Shared icon-theme provider and cache — exists; extend with thumbnailing.
- Shared MIME/default-app/open-with service — needed (consumed by Files, Editor, image/PDF viewers); define a single service so all apps resolve defaults consistently.
- Status provider service model — Shell SH-6/SH-7 on SVC-9/10/11/12 + audio/brightness.
- Notification backend — SVC-6. Clipboard-history backend — Shell model + WM-17.
- Portal direction (file chooser/screenshot/screencast/untrusted clients) — SVC-4/SVC-5 + WM-12.
- Secrets/keyring — SVC-13.
- Thumbnailing service (new) — generate/cache thumbnails for Files and the image viewer (freedesktop thumbnail spec).

### Editor (`lambda-editor`)

Editor current implementation, open gate, tabs/recovery, printing/page-setup, spellcheck/Notepad parity, and deferred items are as previously specified (ED-1…ED-8): a compact single-document app with toolbar (New/Open/Save/Save As/Undo/Redo/Cut/Copy/Paste/Find/Replace/Go To Line/Word Wrap/Zoom), Lambda open/save dialog path, dirty tracking, and command-line open. The open gate covers data-loss prompts on new/open/close/quit/tab-close; explicit handling of missing/permission/large/invalid/encoding/newline cases; BOM/UTF-16/CRLF/LF/final-newline preservation; fuller status bar; persisted zoom; Time/Date/Print/Page Setup actions; richer find/replace; editor context menu; document navigation keys; large-file guardrails; a full menu bar (File/Edit/Format/View/Help); Files open-with wiring; and splitting `main.cpp`. Tabs/session-restore, print/page-setup abstractions, and spellcheck (Hunspell or system) follow. Rich text/Markdown/syntax-highlighting/AI remain deferred.

- **New (gap analysis):** Editor print/page-setup should share a desktop-wide print abstraction with the PDF viewer (PDF-*); spellcheck should expose a shared service if other apps want it later.

### Missing core apps (specs)

Detailed below in rough daily-driver priority. Each is a Lambda app built on the v5 UI runtime, integrated with the shared app registry, MIME/open-with, icon/thumbnail, and (where relevant) portal/print services.

- **IMG-* — Image viewer (replace the `lambda-preview` stub).** Current `lambda-preview` is ~232 lines: open-by-typed-path with basic zoom. Build a real viewer:
  - Open from Files/open-with and CLI argument; **folder navigation** (next/previous within the directory, in the Files sort order).
  - Formats: stb_image (PNG/JPG/GIF/BMP/etc.) plus SVG via the framework `flux::Svg`; later libheif/libavif/libraw for HEIC/AVIF/RAW.
  - View: fit / fill / 100% / free zoom + pan; **honor EXIF orientation**; rotate/flip; fullscreen and a simple slideshow.
  - Actions: copy-to-clipboard, set-as-wallpaper (via Settings/Shell), delete-to-trash via the Files/trash service.
  - Acceptance: double-clicking an image in Files opens it; arrow keys move through the folder; rotated photos display upright; common formats load; fit/zoom/pan feel right.
- **SHOT-* — Screenshot tool front-end.** A UI on top of the compositor's existing capture (and WM-12 / the portal Screenshot once present):
  - Modes: full output, active window, region (with the compositor region overlay); optional delay.
  - Post-capture: annotate (arrow/box/text/highlight/blur), copy-to-clipboard, and save to XDG Pictures/Screenshots with sane filenames.
  - Integrate with the portal Screenshot interface so app-initiated screenshots route here.
  - Acceptance: a keybind captures a region; the result can be annotated, copied, and saved; an app's portal screenshot request shows this UI.
- **PDF-* — PDF/document viewer.** Render via a library (poppler-cpp or mupdf): page navigation, zoom/fit modes, search with match navigation, thumbnail/outline sidebar, open-with from Files, and printing via the shared print abstraction (with Editor). Acceptance: opening a PDF from Files renders it; search/zoom/fit/thumbnails work; print produces correct output.
- **ARC-* — Archive manager.** List/extract/create via libarchive (zip, tar.gz/xz/zst; optional 7z): browse archive contents, extract-here/extract-to, compress selection, with Files context-menu integration ("Extract here", "Compress…"). Acceptance: extract and create a zip and a tar.zst from Files; browse an archive's contents.
- **CALC-* — Calculator.** Basic + scientific modes, keyboard input, and a short history. Small effort; expected on any desktop. Acceptance: arithmetic and scientific operations with keyboard and history.
- **MON-* — System monitor.** Process list (from `/proc`), CPU/memory/network/disk usage, sort/filter, and kill/renice (signals; privileged kill via SVC-8 polkit). Acceptance: see and sort processes, view resource graphs, and end a process (with auth where required).
- **MED-* — Media player.** Audio/video via a backend (libmpv or GStreamer): playlist, transport, subtitle/track selection, hardware decode (VA-API), and **MPRIS publish** so the Shell/media keys control it (SVC-12). Acceptance: plays common audio/video; transport works; the Shell shows now-playing and media keys control it.

### Browser, mail, calendar (external, plumbing-gated)

These remain mature external Wayland apps unless Lambda explicitly decides to own them. The real requirement is that they **run and integrate**:

- Firefox/Chromium and Thunderbird run Wayland-native today; the gate is the portal **FileChooser** (SVC-4) for file dialogs, the **ScreenCast** portal (SVC-5 + WM-12) for screen sharing, **Settings** portal (SVC-15) for dark-mode/theme, **notifications** (SVC-6), and — for the long tail of apps without Wayland builds — **XWayland** (WM-20).
- Acceptance: a browser can open/save files via the Lambda chooser, share its screen in a video call, follow dark-mode, and notify; an X11-only app runs under XWayland with working clipboard/DnD.

### Workspaces / Touch / IME (product decisions)

- **Workspaces / virtual desktops:** not planned. If accepted, implement `ext-workspace-v1` (compositor) + a Shell switcher + keybindings; otherwise document that Lambda v1 ships without workspaces.
- **Touch:** WM-19 if touch hardware is a target.
- **IME / international input:** `text-input-v3` + `input-method-v2` + `virtual-keyboard` (compositor) + an input-method app, if non-Latin input or an on-screen keyboard is a target. Currently out of scope.

---

## Validation Model

- Unit/model tests for deterministic behavior.
- Manual hardware/app smoke tests for compositor, Shell, System Services, and real Wayland clients.
- Real-app validation with Lambda apps plus mature Wayland clients such as Firefox, GTK apps, Qt apps, and `foot`.
- Visual/glass/animation behavior requires target-machine manual validation until screenshot/render regression coverage exists.
- Do not mark a gate complete solely because model helpers exist; live UI and user workflow must be wired where the gate is user-facing.
- **System Services and protocol gates require real-bus and real-protocol validation:** test against the actual session/system bus and real clients (`notify-send`, a tray app, `pkexec`, NetworkManager, BlueZ, `grim`/`slurp`/OBS, a browser's portal flows, `wlr-randr`/kanshi, `loginctl lock-session`, suspend/resume), not just model fixtures.

Manual validation coverage inherited from the old readiness specs (Window Manager, Shell, Settings, Files, Terminal, Editor) is unchanged; see prior sections. Additions:

- System Services: libseat unprivileged launch + VT switching; logind suspend/lid/power-key + lock-on-sleep; notifications daemon with real apps; tray host with real tray apps; polkit prompt; Wi-Fi connect; Bluetooth pair; battery events; media keys + now-playing; portal file dialog and screen share in a browser; USB mount/eject; dark-mode propagation to GTK/Qt apps.
- Compositor protocol/power: session-lock (lock/unlock/crash-stays-locked); DPMS power-off/restore; `grim`/`slurp`/`wf-recorder`/OBS capture; gammastep; `wlr-randr`/kanshi output config; idle-notify-driven auto-lock/dim/blank; (if scheduled) XWayland real-app matrix.

Useful targeted checks:

```sh
./build/lambda_tests --test-case="Shell*"
./build/lambda_tests --test-case="Settings*"
./build/lambda_tests --test-case="*Files*"
./build/lambda_tests --test-case="Terminal*"
./build/lambda_tests --test-case="screenshot*"
./build/lambda_tests --test-case="surface*"
# new (as workstreams land):
./build/lambda_tests --test-case="*DBus*"
./build/lambda_tests --test-case="*Logind*"
./build/lambda_tests --test-case="*PortalSettings*"
./build/lambda_tests --test-case="*Notifications*"
./build/lambda_tests --test-case="*SessionLock*"
./build/lambda_tests --test-case="*Portal*"
./build/lambda_tests --test-case="*OutputManagement*"
```

`ctest --test-dir build --output-on-failure` may require a live Wayland display and Vulkan device depending on the environment.

## Archived Completed Work

Lambda v5:

- Retained mounting and fine-grained reactivity are the current public API.
- `MountRoot`, `MountContext`, `Signal`, `Computed`, `Effect`, `Scope`, `Bindable`, `For`, `Show`, and `Switch` are implemented.
- Framework docs live in the focused Lambda v5 reference docs.

Framework review (2026-06-13) findings addressed:

- Subtree-visual-bounds caching, Vulkan prepared-recorder deferred destruction, `setBounds` position-dirty, `PointerMove` handler-copy safety, hit-test inverse fast-path + cached-bounds early reject, and an effect-flush iteration guard landed with regression tests; `-Wall -Wextra` enabled by default. See `docs/framework-review-20260613.md`.

Compositor structural cleanup:

- Wayland globals split into `apps/lambda-window-manager/Compositor/Wayland/Globals/`; `WaylandServer` uses a pimpl; runtime split into `CompositorRenderFrame`, `CompositorConfigWatch`, `Presenter`, and related modules; window/input logic split into `FocusStack`, `PointerRouter`, `InteractiveMoveResize`, `KeyboardShortcuts`, and `LayerShellInput`; resize tracing unified; protocol codegen consolidated through `cmake/LambdaWaylandProtocols.cmake`.

Compositor and Shell framework work:

- Layer-shell chrome and compositor-backed background effects; shared Shell IPC module; shared preview/production rendering; deterministic tests for subsurface hit testing, popup geometry, layer-shell zones, output selector, screenshot logic, and compositor state helpers; Firefox-oriented xdg-popup/inert-resource lifecycle fixes; fullscreen restore preserving prior window state with panel leave/return; KMS queued presentation with overlay/scanout decisions; hardware-cursor motion decoupled from full redraws; explicit-sync disabled until justified; SHM buffer-damage partial updates.

Documentation cleanup:

- Old Lambda readiness specs and Shell mockup/status plans consolidated into this roadmap on 2026-05-26.
- The 2026-06-14 daily-driver gap analysis was folded into Product Decisions, the Daily-Driver Gate, P0.5 System Services, WM-11…WM-21, and the P6 missing-app specs.
- `compositor.md` remains an architecture/history reference, not the active backlog.

## Update Rules

- Update this roadmap when a Lambda gate changes status.
- When implementation lands, update the relevant `Current implementation`, `Open gate`, and `Validation` bullets in the same change.
- Keep user/run instructions in [compositor-user-guide.md](compositor-user-guide.md), not here.
- Keep architecture/history notes in [compositor.md](compositor.md), not here, unless they affect active roadmap decisions.
- When a System Services or compositor-protocol workstream (SVC-*, WM-11…WM-21) lands, record the real-client validation that proved it (the daemon/protocol is not "done" until a real third-party client exercises it).
