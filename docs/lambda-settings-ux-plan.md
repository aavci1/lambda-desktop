# Lambda Settings UX plan

**Last updated:** 2026-06-03
**Status:** Active design and organization plan for `lambda-settings`, integrated with the P2 Settings roadmap.

## Purpose

`lambda-settings` already has the important backend pieces: owner-config schemas, validation, TOML round trips, default generation, dirty/revert/reset, and save/error handling. The remaining product gap is that the app reads visually like a config editor or demo surface instead of a mature system settings app.

This plan keeps Settings inside its ownership boundary: Settings edits owner config files and shows truthful system/about status. Window Manager, Shell, Files, Terminal, and shared services remain the runtime authorities.

## Current state

- `lambda-settings` opens as a system-titlebar Lambda app with a sidebar, scrollable content, and bottom save bar.
- It reads and writes Window Manager and Shell owner config files directly.
- Schema metadata exists for the displayed settings and drives editor controls.
- Writes are atomic and validated; unknown keys are preserved where practical.
- Current sections expose Appearance, Display, Keyboard, Desktop, Dock & Panel, Notifications, General, and About.
- The app has a useful skeleton, but its content surface is mostly heading plus transparent divided rows.
- Several unused showcase-oriented helpers still exist in the view layer, including theme/accent/wallpaper preview code that is not wired through `contentForSection`.

## Design direction

Use the existing system-settings shell, but make the main content behave like a real settings product:

- Preserve the sidebar plus detail pane structure.
- Convert every settings group into a reusable `Card` surface.
- Put rows inside cards with stable padding, clear label hierarchy, quiet secondary text, and right-aligned controls.
- Prefer the `toggle-demo` and `card-demo` card language over bespoke glass wrappers.
- Use framework `ThemeKey` tokens for card background, labels, separators, spacing, and radii where practical.
- Keep only app-specific layout constants in `SettingsTheme`, such as sidebar width and content padding.
- Make page titles and group headings user-oriented, not config-oriented.
- Avoid fake previews or unavailable controls unless they map to real settings or explicitly report unavailable state.

## Proposed navigation

Recommended sidebar organization:

- General
- Appearance
- Display
- Input
- Windows
- Dock & Panel
- Notifications
- Launcher & Clipboard
- About

This separates long-form settings by user intent. Clipboard and Launcher should not be hidden under Notifications, and Keyboard plus shortcut/cursor behavior should be gathered into clearer Input/Windows surfaces.

## Proposed page groups

General:

- Config Files: Window Manager config path, Shell config path, write policy, unknown-key preservation.
- Apply Behavior: saved state, restart-required explanation, hot-reload caveats.

Appearance:

- Desktop Background: background color, wallpaper, wallpaper mode.
- Icons: icon theme, symbolic icon theme.
- Motion: reduced motion.

Display:

- Output: selected output.
- Scaling: scale.

Input:

- Keyboard: layout, repeat rate, repeat delay.
- Shortcuts: close window and future input-owned shortcut groups.

Windows:

- Behavior: animations, idle blank timeout.
- Cursor: hardware cursor, cursor theme, cursor size.
- Screenshots: full, region, and active-window screenshot shortcuts.

Dock & Panel:

- Position: dock position.
- Behavior: auto-hide, full-width, running unpinned apps, tooltips.
- Sizing: item size, bottom gap, corner radius.
- Material: blur radius, opacity, base color, tint color, border color.
- Content: pinned apps, clock format, quick settings modules.

Notifications:

- Banners: enabled, do not disturb, banner timeout, previews.
- History: history limit.

Launcher & Clipboard:

- Launcher: empty-query behavior, max results, categories.
- Clipboard History: enabled, persistence, max entries, max text bytes, primary-selection policy.

About:

- Product identity.
- System information rows.
- Build/version information when backed by real values.

## Implementation plan

1. Refactor the visual primitives first:
   - Replace `sectionBlock()` with a `settingsCard()` helper backed by `Card`.
   - Update `SettingsRowView` to match the demo-style settings row: internal padding, label/detail stack, stable right-side control region, and compact responsive stacking.
   - Make `rowsList()` a card-internal row stack, not a top-level visual surface.

2. Improve the page model:
   - Extend `SettingsGroup` with an optional caption.
   - Keep schema-driven `BoundSetting` row generation.
   - Add page-level captions sparingly where they clarify apply behavior or ownership.

3. Reorganize the navigation:
   - Rename Keyboard to Input.
   - Split Desktop into Windows where appropriate.
   - Split Launcher & Clipboard out from Notifications.
   - Expose Shell launcher settings that already exist in the schema.

4. Clean unused demo code:
   - Remove unused `appearancePage`, `ThemeCard`, `AccentDot`, and wallpaper preview helpers unless they are wired to real owner config.
   - If preview cards are kept, make them real setting editors with truthful current values.

5. Calm the footer:
   - Keep Save/Revert/Reset because config writes are staged.
   - Put status text in a stable footer position.
   - Keep reset visually secondary and avoid making the footer look like a glass demo strip.

6. Preserve validation and behavior:
   - Do not change the TOML schema or save semantics during the visual pass.
   - Re-run targeted Settings tests after each behavior-affecting change.
   - Manually validate launch, scrolling, compact layout, save/revert/reset, invalid values, restart-required rows, and all sidebar pages.

## Acceptance criteria

- The first screen looks like a real system settings app, not a component demo.
- Every visible control corresponds to a real setting or explicitly unavailable system state.
- Page groups are scannable at normal app width and remain usable in compact widths.
- Settings remains a GUI owner-config editor and does not become a second source of truth.
- Existing Settings backend tests still pass.
- Manual validation covers Appearance, Display, Input, Windows, Dock & Panel, Notifications, Launcher & Clipboard, About, save/revert/reset, and owner-config persistence.
