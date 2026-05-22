# Lambda Shell Specification

Date: 2026-05-22
Status: Draft

## Purpose

Lambda is the desktop environment. For this specification, the implementation scope is only `lambda-shell`: the trusted shell process that renders and operates the persistent desktop chrome:

- top bar
- dock
- command launcher

The existing compositor/window-management process remains responsible for Wayland/KMS composition, output ownership, surface stacking, window placement, focus, input routing, and application surfaces. That process should be renamed from `flux-compositor` to `lambda-window-manager`. The shell must not own window-manager internals and must not render widgets or application window contents for this phase.

## Mockup Summary

The supplied standalone HTML mockup is a legacy SADE desktop mockup. Treat it as a visual reference only; SADE is not a product or implementation name. All user-facing labels, process names, protocol names, config paths, and build targets for this work must use Lambda names.

The relevant shell elements in the mockup are:

- A fixed top bar at the top of the screen, 36px high, with brand/menu controls on the left and network/system/status/date controls on the right.
- A centered dock near the bottom edge, with launcher, pinned apps, separators, running indicators, hover lift, and tooltips.
- A full-screen launcher overlay with dimmed/blurred background, search input at the top, and searchable app tiles.

The mockup also includes wallpaper, application windows, Files/Settings/Terminal/Browser/Mail/Music/Calendar app content, weather/system widgets, quick toggles, and sample window-management behavior. Those pieces are out of scope for `lambda-shell` and should not be implemented from this spec.

## Goals

- Move the command launcher out of `lambda-window-manager` and into `lambda-shell`.
- Keep `lambda-window-manager` and `lambda-shell` independently buildable and restartable.
- Keep shell state derived from window-manager state where possible instead of duplicating window-management state.
- Make top bar, dock, and command launcher extensible without pulling application or widget concerns into the shell.
- Preserve the mockup's visual language: glass surfaces, restrained shadows, compact controls, and direct desktop utility.

## Non-Goals

- Widgets, desktop cards, weather/system panels, and other left-rail mockup components.
- Files, Settings, Terminal, Browser, Mail, Calendar, Music, Trash implementations.
- Application window chrome/content redesign.
- Full settings UI for shell customization.
- Replacing window-manager responsibilities such as composition, stacking, moving, resizing, hit testing, global focus, or surface damage tracking.

## Current Codebase Baseline

The current repository already contains the compositor/window manager under `src/Compositor`:

- `CMakeLists.txt` builds the process as `flux-compositor` behind `FLUX_BUILD_COMPOSITOR`.
- The runtime owns KMS output selection, Wayland server lifecycle, surface snapshots, rendering, frame pacing, cursor rendering, input routing, window chrome, popups, layer-shell, and application windows.
- The window manager already tracks xdg-toplevel title and app id, focus, geometry, minimize/maximize state, and layer surfaces. This is the right authority for shell window/app state snapshots.
- `zwlr_layer_shell_v1` exists and can be used as the first transport for shell surfaces, but exclusive zone, keyboard interactivity, and popup handling are currently no-ops or minimal. `lambda-shell` can start with the current layer-shell path, but top-bar work-area reservation and command-launcher modality require either hardening layer-shell behavior or adding a trusted Lambda shell protocol.
- The command launcher is still embedded in the compositor: `CommandLauncherSnapshot`, `commandLauncherVisible_`, `commandLauncherText_`, `commandLauncherMessage_`, `drawCommandLauncher`, `handleCommandLauncherKey`, `spawnCommand`, and launcher frame profiling remain in `src/Compositor`. These must be removed or converted to shell protocol dispatch during migration.

The rename from `flux-compositor` to `lambda-window-manager` should cover the build target, executable name, user-visible logs, default config/state paths, crash-log paths, documentation, and launch scripts. Internal C++ namespaces and source directory names may be renamed later if doing so would create unnecessary churn in the first shell milestone.

## Process Boundary

`lambda-shell` runs as an independent trusted application. It connects to `lambda-window-manager` through a stable shell protocol and renders one or more shell-owned surfaces.

`lambda-window-manager` owns:

- display discovery and scale factors
- output geometry and work areas
- surface stacking and occlusion
- pointer/keyboard routing
- application launch/focus/minimize/close requests
- authoritative window/application state
- global shortcut capture and dispatch to trusted clients

`lambda-shell` owns:

- top-bar layout and interactions
- dock layout and interactions
- command launcher UI, search, ranking, and action execution
- user-visible shell animations
- shell preferences that do not affect window-manager correctness

`lambda-shell` must tolerate `lambda-window-manager` restarts by reconnecting, rebuilding state from snapshots, and re-registering shell surfaces.

## Shell Surfaces

The shell should register three logical surfaces:

| Surface | Layer | Hit Test Behavior | Purpose |
| --- | --- | --- | --- |
| `lambda.topbar` | shell-top | interactive only inside top-bar bounds | Persistent status/menu bar |
| `lambda.dock` | shell-overlay | interactive only inside dock bounds | App launcher/focus affordance |
| `lambda.command-launcher` | shell-modal | exclusive when open | Command search and action execution |

`lambda-window-manager` may expose these as separate transparent surfaces or as one shell surface with internally routed regions. The protocol should treat them as separate capabilities even if implementation batches them.

## Work Area Rules

- The top bar reserves 36px at the top of each output.
- Dock does not reserve permanent work area in the first milestone; it floats above windows at the bottom center.
- When the command launcher is open, it is modal above normal app windows, top bar, and dock.
- Fullscreen application behavior is a window-manager decision. Recommended first behavior: fullscreen covers dock but keeps top bar visible unless `lambda-window-manager` has a separate exclusive/fullscreen mode.

## Visual Design Tokens

The shell should expose tokens equivalent to the mockup:

| Token | Default | Notes |
| --- | --- | --- |
| `accent` | `#2a7fff` | Primary interactive color |
| `accent2` | mixed lighter accent | Used in brand mark and gradients |
| `radius` | `14px` | Large shell surfaces |
| `radiusSm` | `10px` | Controls and smaller tiles |
| `glassAlphaLight` | `0.80` | Light mode shell glass |
| `glassAlphaDark` | `0.45` | Dark mode shell glass |
| `blur` | `32px` | Persistent shell glass blur |
| `lineLight` | `rgba(20,30,60,0.08)` | Hairline borders |
| `lineDark` | `rgba(255,255,255,0.08)` | Hairline borders |

Mode can be `light`, `dark`, or `system`. The shell should mirror window-manager/output color scheme where available.

## Top Bar

### Layout

The top bar is a full-width, fixed-height shell surface:

- position: top 0, left 0, right 0
- height: 36px
- horizontal padding: 14px
- display: left cluster and right tray
- background: glass surface with backdrop blur and bottom hairline border
- font: system UI, 12.5px, medium weight

### Left Cluster

The left cluster contains:

- Lambda brand mark
- `Lambda` text label
- top-level menu buttons: File, Edit, View, Window, Help

Initial implementation can render menus as inert buttons if menu plumbing is not ready, but hover/pressed states should exist. Future menu actions should be supplied by window-manager/application focus state.

Brand requirements:

- Use `Lambda` consistently in user-facing copy.
- Use lower-case `lambda-*` and `lambda.*` only for process names, protocol identifiers, app ids, config keys, and surface ids.
- The mark can be a compact lambda glyph, geometric lambda-inspired mark, or existing desktop logo rendered at 20x20.
- The brand element should be clickable and open the command launcher or system menu once that behavior is implemented. First milestone may open the command launcher.

### Right Tray

The right tray contains compact status items:

- network throughput summary
- Wi-Fi state
- Bluetooth state
- volume state
- battery percentage
- date and time

The mockup uses day, short month date, and 24-hour time. Default format:

```text
Fri 22 May, 14:35
```

The clock should update at least every 30 seconds. Tray item data may be stubbed in milestone 1, but the data API should distinguish `unknown`, `unavailable`, and actual values.

### Interactions

- Hovering a menu/tray item shows a subtle glass/alpha highlight.
- Clicking status items should dispatch shell intents; first milestone may log or no-op unavailable panels.
- The top bar must not steal drag/resize/focus gestures outside its bounds.
- Keyboard focus should enter top-bar controls only through normal tab navigation or explicit shortcuts.

## Dock

### Layout

The dock is a centered bottom shell surface:

- anchored horizontally at output center
- bottom offset: 12px
- content padding: 8px vertical, 10px horizontal
- gap: 6px
- background: glass surface with backdrop blur
- border radius: 18px
- icon cell size: 48x48
- icon visual size: 40-44px

The dock should compute its layout from a model, not hard-coded markup.

### Default Items

The mockup order should be translated to Lambda naming:

```text
launcher | separator | files | browser | terminal | settings | calendar | mail | music | separator | trash
```

For this shell phase, the model can include these example entries, but only shell behavior is in scope. Dock activation should send launch/focus intents to `lambda-window-manager`; it should not instantiate application windows itself.

### Item Model

Each dock item should include:

```ts
type DockItem = {
  id: string;
  kind: "launcher" | "app" | "separator" | "trash";
  label: string;
  icon: IconDescriptor;
  pinned: boolean;
  appId?: string;
  running?: boolean;
  focused?: boolean;
  attention?: boolean;
  disabled?: boolean;
};
```

`running`, `focused`, and `attention` are derived from window-manager state.

### Behavior

- Click launcher item: open command launcher.
- Click app item with running windows: focus most recent window for that app.
- Click app item without running windows: request app launch.
- Click focused running app: first milestone keeps focus; future behavior may expose app window cycling.
- Click trash: no-op until trash/file-management integration exists.
- Hover: icon lifts by roughly 6px and scales to about 1.06.
- Active press: icon returns closer to base position and scale.
- Tooltip: appears above item with the item label.
- Running indicator: 4px dot centered below icon cell.

The dock should remain usable when `lambda-window-manager` reports no applications.

## Command Launcher

### Responsibility

The command launcher is moving from `lambda-window-manager` to `lambda-shell`. After migration:

- `lambda-window-manager` captures the global shortcut and asks `lambda-shell` to open the launcher.
- `lambda-shell` renders the launcher, owns text input, searches providers, ranks results, and executes selected actions through protocol requests.
- `lambda-window-manager` only enforces modality, focus, launch/focus authority, and privileged operation checks.

### Visual Form

The mockup launcher becomes the first command launcher shell:

- full-screen overlay
- background dim: `rgba(8,14,30,0.25)`
- backdrop blur: 20px
- open opacity transition: 200ms
- search field at top center, 80px from top
- search field width: 420px on desktop, clamped on smaller outputs
- search field background: translucent white, 0.5px light border
- result area centered below the search field

The first implementation may keep the grid presentation for app results. The command launcher API should support list results because commands, files, and system actions need denser result rows.

### Open and Close

Open triggers:

- dock launcher item
- brand mark or configured top-bar action
- window-manager global shortcut, recommended `Super+Space`

Close triggers:

- `Escape`
- click outside launcher content
- successful action execution, unless action declares `keepOpen`
- window-manager cancellation event

Opening the launcher should:

- request exclusive keyboard focus
- clear the previous query unless opened in continuation mode
- focus the input
- request a fresh provider snapshot

### Input Behavior

- Empty query shows recommended apps/actions.
- Typing filters and ranks providers incrementally.
- `Enter` executes highlighted result.
- Arrow up/down changes highlighted result.
- `Tab` can move between result categories when categories are visible.
- `Cmd/Ctrl+Backspace` clears current query.
- IME composition must not execute commands prematurely.

### Result Model

```ts
type CommandResult = {
  id: string;
  providerId: string;
  title: string;
  subtitle?: string;
  icon?: IconDescriptor;
  keywords?: string[];
  score: number;
  action: CommandAction;
  category?: "apps" | "windows" | "files" | "settings" | "system" | "commands";
  disabled?: boolean;
  danger?: boolean;
};

type CommandAction =
  | { type: "launch-app"; appId: string }
  | { type: "focus-window"; windowId: string }
  | { type: "open-settings"; panel?: string }
  | { type: "system"; command: "lock" | "logout" | "sleep" | "restart" | "shutdown" }
  | { type: "shell-command"; commandId: string; payload?: unknown };
```

### Providers

Milestone 1 providers:

- Apps provider: launchable applications from the window-manager/app registry.
- Windows provider: currently running windows from window-manager state.
- Shell commands provider: settings, lock screen placeholder, reload shell, open diagnostics.

Future providers:

- Files
- Recent documents
- Calculator
- Web/search shortcuts
- Plugin-contributed commands

Providers should be cancellable and should return partial results if slow. The shell should debounce query changes around 50-100ms.

### Ranking

Ranking should combine:

- exact prefix match
- acronym match
- fuzzy subsequence match
- recency
- running/focused state
- provider priority

Disabled or dangerous actions should rank normally but require explicit confirmation before execution.

## Shell-Window-Manager Protocol

The protocol should be transport-neutral. The first implementation may use Wayland layer-shell plus a local socket, a Wayland-like trusted global, WebSocket, or an in-process bridge during early development, but message names should remain stable.

The existing `zwlr_layer_shell_v1` implementation is enough to start rendering top-bar and dock surfaces. It is not enough by itself for the full spec until `lambda-window-manager` implements top-bar exclusive zones/work-area updates, command-launcher keyboard exclusivity, and shell-surface authentication.

### Handshake

Shell to window manager:

```json
{
  "type": "lambda.shell.hello",
  "protocolVersion": 1,
  "shellVersion": "0.1.0",
  "capabilities": ["topbar", "dock", "command-launcher"]
}
```

Window manager to shell:

```json
{
  "type": "lambda.windowManager.welcome",
  "protocolVersion": 1,
  "sessionId": "opaque-session-id",
  "outputs": [],
  "theme": { "mode": "system", "accent": "#2a7fff" }
}
```

### State Snapshots

`lambda-window-manager` sends a full snapshot after connection and incremental updates afterward.

```ts
type DesktopSnapshot = {
  outputs: OutputState[];
  apps: AppRegistryEntry[];
  windows: WindowState[];
  activeWindowId?: string;
  focusedAppId?: string;
  system: SystemStatus;
};
```

Important window fields:

```ts
type WindowState = {
  id: string;
  appId: string;
  title: string;
  outputId: string;
  state: "normal" | "minimized" | "maximized" | "fullscreen" | "closed";
  focused: boolean;
  attention: boolean;
  lastFocusedAt?: number;
};
```

### Commands From Shell

The shell can request:

- `lambda.windowManager.launchApp`
- `lambda.windowManager.focusWindow`
- `lambda.windowManager.focusApp`
- `lambda.windowManager.minimizeWindow`
- `lambda.windowManager.closeWindow`
- `lambda.windowManager.showWindowSwitcher`
- `lambda.windowManager.setShellSurfaceGeometry`
- `lambda.windowManager.claimCommandLauncherModal`
- `lambda.windowManager.releaseCommandLauncherModal`

The window manager can reject requests with structured errors:

```ts
type WindowManagerError = {
  code: "not-found" | "not-allowed" | "invalid-state" | "unsupported" | "internal";
  message: string;
  requestId?: string;
};
```

### Commands To Shell

The window manager can request:

- `lambda.shell.openCommandLauncher`
- `lambda.shell.closeCommandLauncher`
- `lambda.shell.refreshState`
- `lambda.shell.setTheme`

`lambda.shell.openCommandLauncher` is the expected target for the global shortcut path. The shell should acknowledge the request after the launcher surface is visible and keyboard focus is acquired.

### Events To Shell

The window manager should emit:

- output added/removed/changed
- app registry changed
- window created/updated/destroyed
- active window changed
- global shortcut invoked
- system status updated
- theme preference changed

The shell should treat event order as authoritative within one connection, and request a fresh snapshot after reconnect or detected sequence gaps.

## State Management

Shell-owned state:

- command launcher open/closed state
- command query and highlighted result
- local dock hover/tooltip state
- top-bar menu popover state
- cached provider results
- shell preferences not required by the window manager

Window-manager-derived state:

- running apps
- focused app/window
- attention badges
- launchable app registry
- output geometry and scale
- system status values

The shell should never infer a window exists because it requested an app launch. It should wait for window-manager state updates.

## Accessibility

- All dock items and top-bar controls need keyboard-reachable buttons with accessible labels.
- Command launcher input must have a label such as `Command`.
- Results should expose `aria-activedescendant` or equivalent focus semantics.
- Running/focused/attention state should not depend only on color.
- Motion should respect reduced-motion preferences.
- Text should remain legible over wallpaper in both light and dark modes.

## Multi-Display Behavior

Milestone 1 may render shell chrome only on the primary output. The protocol and layout model must support multiple outputs:

- one top bar per output
- dock on primary output by default
- command launcher opens on output containing pointer or focused window
- per-output scale factor aware sizes
- work area computed per output

## Failure Behavior

- If `lambda-shell` crashes, `lambda-window-manager` keeps running applications and may show no shell chrome.
- If `lambda-window-manager` disconnects, `lambda-shell` shows a reconnecting state or exits cleanly.
- If a command provider fails, the launcher should show remaining provider results.
- If app launch/focus fails, the shell should keep the launcher open and show a concise error row or toast.

## Security

- Treat shell as trusted, but do not let arbitrary apps impersonate shell protocol messages.
- Privileged system actions must require window-manager confirmation or policy approval.
- Command launcher providers must mark destructive actions with `danger: true`.
- Application-provided commands must not execute without going through `lambda-window-manager` or a trusted broker.
- Do not expose raw shell IPC to untrusted application surfaces.

## Implementation Milestones

### Milestone 0: Window Manager Cleanup

- Rename the compositor executable/build target from `flux-compositor` to `lambda-window-manager`.
- Update user-visible runtime strings, default config/state paths, crash-log paths, docs, and launch scripts to the Lambda name.
- Remove the compositor-rendered command launcher UI and snapshot plumbing.
- Remove direct command spawning from compositor launcher handling.
- Convert the global launcher shortcut from opening compositor UI to dispatching `lambda.shell.openCommandLauncher`.
- Keep existing window management, window chrome, input routing, layer-shell, and Wayland protocol behavior intact.

### Milestone 1: Static Shell

- Build separate `lambda-shell` application.
- Register top bar, dock, and command launcher surfaces.
- Render `Lambda` top bar from mockup-derived layout.
- Render dock from a local model.
- Render command launcher with app search from local/static app registry.
- Dock launcher opens the launcher.
- Launcher can launch/focus through window-manager request stubs.

### Milestone 2: Window Manager State Integration

- Subscribe to window-manager snapshots and window/app updates.
- Drive dock running indicators from real state.
- Drive command launcher app/window providers from real state.
- Implement global shortcut path: window manager captures shortcut, shell opens launcher.
- Support window-manager rejection/error reporting.

### Milestone 3: Production Launcher Behavior

- Add provider cancellation and async partial results.
- Add keyboard navigation and result categories.
- Add settings/system shell commands.
- Add command history and recency ranking.
- Add confirmation flow for dangerous actions.

### Milestone 4: Multi-Output and Polish

- Render top bar per output.
- Place command launcher on active output.
- Add menu popovers and status panels.
- Add reduced-motion handling.
- Add robust reconnect/snapshot recovery.

## Acceptance Criteria

- The process is named `lambda-window-manager` in the build target, executable, logs, default config/state paths, and docs.
- `lambda-window-manager` no longer contains command launcher UI code or direct command-spawning launcher behavior.
- `lambda-shell` can be started, stopped, and restarted without killing application windows.
- The top bar always renders as `Lambda` and never uses SADE branding.
- Dock state reflects window-manager running/focused app state.
- Opening the command launcher gives the shell keyboard focus and does not leak keystrokes to applications.
- Executing a launcher app result sends a window-manager launch/focus request and waits for state confirmation.
- Closing the launcher returns focus according to window-manager policy.
- Widgets and app contents remain out of shell scope.
