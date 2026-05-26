# Lambda Terminal readiness spec

**Date:** 2026-05-25
**Status:** Draft
**Milestone order:** Window Manager first, then Shell, Settings, Files, Terminal, and the remaining desktop pieces.
**Scope:** `lambda-terminal`, shared terminal/preferences libraries needed by it, and the desktop services it consumes for clipboard, settings, app identity, and window background.

## Summary

This milestone turns `lambda-terminal` from a promising pty/libvterm prototype into a terminal emulator that can be used as the daily command-line surface for the Lambda desktop. The target is not to clone every feature of mature terminal emulators. The target is that normal shell work, editing, long command output, copying and pasting, resizing, keyboard shortcuts, Unicode text, mouse-aware terminal apps, and preferences are reliable enough that the user does not need to keep another terminal open for routine work.

The current terminal already has the hard foundation: pty spawning, libvterm screen state, text input, resize propagation, row damage, row caching, fast ASCII layout, colors, bold text, cursor rendering, and a black glass window background. The readiness work should build on that foundation while splitting the implementation into testable pieces.

Session startup/shutdown automation remains out of scope. New log collection infrastructure is also out of scope. Existing trace scripts can continue to be used for validation.

## Current baseline

Implemented today:

- Native Flux app under `examples/lambda-terminal`.
- Uses `forkpty` to spawn `$SHELL`, falling back to `/bin/sh`.
- Sets `TERM=xterm-256color` and `COLORTERM=truecolor`.
- Uses libvterm for terminal parsing and screen state.
- Enables UTF-8 mode in libvterm.
- Enables alternate screen.
- Disables libvterm reflow because robust resize needs terminal-owned scrollback.
- Receives pty output through the Flux event loop and a wake thread.
- Sends text input and a small set of key sequences to the pty.
- Tracks libvterm row damage and refreshes only dirty rows.
- Caches rows and fast ASCII glyph layouts.
- Handles resize by updating libvterm size and `TIOCSWINSZ`.
- Renders foreground colors, background colors, bold text, cursor, and a black translucent/glass background.
- Updates the window title from terminal title escape sequences.
- Shows a status line when shell startup, exit, or pty read fails.
- Has trace scripts for terminal rendering and terminal resize scenarios.

Important limitations:

- No scrollback.
- No selection.
- No copy.
- No paste.
- No bracketed paste handling.
- No primary selection behavior.
- No OSC 52 clipboard handling.
- No mouse reporting.
- No URL detection or opening.
- No search.
- No tabs or splits.
- No preferences or profiles.
- No configurable font, font size, line height, color scheme, shell, working directory, cursor shape, or glass background.
- Key handling covers only a small subset of terminal keys.
- Modifier encoding is incomplete.
- Function keys, keypad keys, application cursor mode, and application keypad mode are incomplete.
- Unicode rendering is incomplete. The current renderer uses `VTermScreenCell::chars[0]`, which does not fully handle combining marks, wide characters, emoji sequences, or grapheme clusters.
- Scrollback, selection, and resize behavior are not represented in a testable model.
- The app id/name should be standardized with Shell app discovery as `lambda-terminal`.
- The implementation is mostly in one file.

## Additional Terminal work identified

These areas should be included in the Terminal milestone:

- Split pty/session, terminal model, renderer, input encoder, selection, clipboard, preferences, and UI into testable components.
- Add terminal-owned scrollback that works with normal screen, alternate screen, resize, search, and selection.
- Add selection across visible screen and scrollback.
- Add copy, paste, bracketed paste, and primary selection where platform support exists.
- Add complete key encoding for common terminal apps: modifiers, function keys, keypad, application cursor mode, application keypad mode, Alt/Meta, Ctrl combinations, and focus events if enabled.
- Add mouse reporting for common DEC modes, including SGR mouse.
- Add Unicode correctness for wide characters, combining marks, invalid UTF-8, emoji basics, and cell width handling.
- Add color/attribute coverage beyond foreground/background/bold: underline, italic, dim, reverse, blink policy, strikethrough, truecolor, 256-color, selection colors, cursor style, and hyperlink metadata where supported.
- Add search in scrollback/current screen.
- Add preferences and profiles: font, size, line height, theme, opacity/glass, shell, environment, working directory, scrollback limit, cursor style, copy/paste policy, and keybindings.
- Add desktop integration: app id, desktop entry/app registry, Settings handoff, clipboard services, URL opening, and window title behavior.
- Add performance targets and repeatable tests for high-output workloads and resize workloads.

Status update 2026-05-26: the first terminal core split is in place. `TerminalCore` now covers deterministic key sequence generation, application cursor/keypad mode encoding, focus-event reporting encoding, bracketed paste wrapping, copy/paste payload policy helpers, SGR mouse event encoding, mouse-to-cell coordinate mapping, resize row/column calculation, Unicode width handling, ANSI/256/truecolor conversion, basic attribute resolution for bold/dim/italic/underline/reverse/strikethrough, flat preference parsing/default fallback plus serialization, profile-based preference parsing/serialization, default config file creation, atomic preference save, active-profile fallback, configurable black glass/solid background selection, scrollback limit enforcement, viewport movement, normal/alternate screen separation, row resize behavior, selected-text reconstruction, plain-text search across visible rows and scrollback, HTTP/HTTPS URL detection, canonical `lambda-terminal` app identity, and shared app-registry browser command planning for URL opening. A controlled PTY smoke test verifies child output can be read and fed into the terminal text model without UI/pointer activity. `lambda-terminal` uses the shared key encoder, resize calculator, profile background config, and live renderer handling for italic/underline/strikethrough attributes. Live app integration for the new scrollback/selection/search/mouse/URL model and deeper renderer/session split work remain open.

## Goals

1. Make `lambda-terminal` usable for normal daily shell work.
2. Keep libvterm as the terminal parser/state authority instead of hand-rolling escape sequence parsing.
3. Add scrollback, selection, copy, paste, and search.
4. Make keyboard and mouse behavior compatible with common terminal applications.
5. Make Unicode rendering correct enough for real-world command output and editors.
6. Preserve the recent resize and rendering performance improvements.
7. Add preferences/profiles that Settings can edit later.
8. Keep the terminal visually integrated with Lambda glass, while allowing a black-tinted profile by default.
9. Add deterministic tests around terminal behavior instead of relying only on manual trace scripts.

## Non-goals

- No session manager, login, lock, logout, suspend/reboot UI, or auto-start work.
- No new log collection or trace infrastructure.
- No full terminal multiplexer. Tabs and splits can be deferred if single-window terminal behavior is not ready.
- No SSH client UI or connection manager.
- No serial terminal UI.
- No built-in shell.
- No built-in text editor.
- No GPU text-rendering rewrite unless the existing text path cannot meet the performance target.
- No sixel, iTerm inline images, Kitty graphics protocol, or rich media protocols in the first readiness target.
- No full ligature shaping requirement for monospace terminal text.
- No input method framework beyond not blocking future IME support.
- No macOS-specific terminal parity requirement beyond keeping the app buildable where practical.

## Assumptions

- The Window Manager readiness milestone provides stable resize, frame scheduling, keyboard delivery, clipboard protocols, and glass background rendering.
- The Shell readiness milestone provides app identity and launch/focus behavior for `lambda-terminal`.
- The Settings readiness milestone defines how terminal preferences will eventually be edited, but the terminal can ship its own config file first.
- The Files milestone provides open-with/default-app foundations that Terminal can use for URL opening later.
- `libvterm` remains available and is the core terminal emulation dependency.
- Pure Wayland remains the desktop compatibility policy.

## Readiness definition

Terminal is ready for the next milestone when all of these are true:

- It can run an interactive shell for long sessions without missing redraws.
- It supports scrollback with a configurable limit.
- It supports pointer and keyboard selection across visible rows and scrollback.
- It supports copy and paste through the desktop clipboard.
- It supports bracketed paste when the running application enables it.
- It supports enough key encodings for shells, `vim`, `less`, `htop`, `nano`, REPLs, and common CLI tools.
- It supports mouse reporting well enough for `vim`, `less -S`, `htop`, and similar terminal apps.
- It renders common Unicode text correctly, including wide characters and combining marks.
- It handles rapid output without pegging CPU when idle afterward.
- It handles rapid resize without severe artifacts or runaway CPU.
- It exposes preferences for font, font size, color scheme, background/glass, scrollback limit, shell, and cursor behavior.
- It has a clean shell-exit state with clear actions.
- It has tests for model behavior, input encoding, scrollback, selection, Unicode width, and config parsing.
- It has manual validation against real terminal applications.

## Architecture decisions

### Split the current single-file implementation

The current implementation is concentrated in `examples/lambda-terminal/main.cpp`. Readiness needs testable units.

Suggested module shape:

```text
examples/lambda-terminal/
  main.cpp
  TerminalSession.hpp
  TerminalSession.cpp
  TerminalModel.hpp
  TerminalModel.cpp
  TerminalRenderer.hpp
  TerminalRenderer.cpp
  TerminalInputEncoder.hpp
  TerminalInputEncoder.cpp
  TerminalSelection.hpp
  TerminalSelection.cpp
  TerminalClipboard.hpp
  TerminalClipboard.cpp
  TerminalPreferences.hpp
  TerminalPreferences.cpp
  TerminalConfig.hpp
  TerminalConfig.cpp
```

The exact filenames can change. The required separation is:

- pty/process lifecycle
- libvterm integration
- screen/scrollback model
- input encoding
- selection and clipboard
- rendering/layout cache
- preferences/config
- Flux view/window wiring

### Terminal owns scrollback

libvterm screen state is not enough for a daily terminal. The terminal should own scrollback so resize, selection, and search can be correct.

Rules:

- Normal screen output can push lines into scrollback.
- Alternate screen should not usually push application-screen contents into normal scrollback.
- Entering alternate screen should preserve normal screen and scrollback.
- Exiting alternate screen should restore normal screen.
- Selection should survive scrollback navigation when possible.
- Scrollback limit should be configurable by line count.
- Scrollback memory use should be bounded.

### Input encoding is its own component

Terminal key behavior should not be scattered through UI callbacks.

The input encoder should know:

- modifier state
- application cursor mode
- application keypad mode
- bracketed paste mode
- mouse reporting modes
- focus reporting mode
- terminal profile compatibility mode

This makes it possible to test key sequences without launching a pty.

### Rendering is a cache, not the source of truth

The source of truth should be terminal cells, attributes, scrollback, selection, and cursor state. Text layouts, glyph caches, and row caches should be derived and disposable.

This preserves performance work while making correctness easier to test.

## Workstreams

### TE-1: App identity, lifecycle, and structure

Problem:

The app works, but identity and lifecycle need to be clean before Shell/Settings integration.

Scope:

- Canonical app id.
- Desktop app registry entry.
- Window title behavior.
- Shell spawn errors.
- Shell exit behavior.
- Process cleanup.
- Module split.
- Testable session/model setup.

Acceptance:

- The canonical app id is `lambda-terminal`.
- Shell app registry discovers and launches `lambda-terminal`.
- Window title defaults to `Terminal` and updates from terminal title sequences.
- Shell startup failure shows a clear in-window state.
- Shell exit shows a clear state with actions: close window, restart shell, copy exit status if available.
- Closing the terminal sends SIGHUP/terminates the child process cleanly.
- No orphan pty child remains after window close in normal cases.
- The implementation is split into testable units.

Implementation notes:

- Keep `TERM=xterm-256color` initially, but document compatibility assumptions.
- Capture child exit status where possible.
- Do not depend on Shell for terminal lifecycle.

### TE-2: PTY and event-loop reliability

Problem:

Terminal output must wake the app event loop reliably and then return to idle.

Scope:

- pty read loop.
- nonblocking writes.
- event-loop wake path.
- backpressure.
- child process exit.
- SIGWINCH through `TIOCSWINSZ`.
- high-output workloads.
- idle behavior after output stops.

Acceptance:

- Terminal output triggers redraw without pointer movement.
- Rapid output is read and applied without starvation.
- The terminal returns to idle after output stops.
- Large writes from paste/input do not block the UI indefinitely.
- pty hangup is detected and shown.
- Resize sends correct rows/cols through `TIOCSWINSZ`.

Implementation notes:

- The existing wake thread solved a real redraw issue. Preserve the behavior while making it easier to reason about.
- Consider batching pty input per frame to avoid excessive layout churn.
- Keep tests independent of a real user shell where possible by using a controlled pty child.

### TE-3: Scrollback and viewport

Problem:

No scrollback means terminal output is lost immediately, which is not acceptable for daily use.

Scope:

- Scrollback buffer.
- Viewport offset.
- Scroll wheel.
- PageUp/PageDown scrollback when not captured by alternate-screen apps.
- Keyboard scroll shortcuts.
- Jump to bottom on new output policy.
- Normal screen vs alternate screen.
- Scrollback limit.
- Scrollback memory bounds.

Acceptance:

- Normal output that scrolls off the top is retained up to the configured limit.
- User can scroll through previous output.
- New output auto-scrolls when the viewport is at bottom.
- New output does not forcibly jump to bottom when the user is reviewing scrollback, unless configured.
- Alternate-screen apps such as `vim` and `less` do not pollute normal scrollback with their full-screen content.
- Exiting alternate screen restores previous normal screen and scrollback.
- Scrollback limit is configurable and enforced.

Implementation notes:

- Store terminal rows as cells/attributes, not only rendered strings.
- Selection and search must understand scrollback coordinates.

### TE-4: Selection, copy, paste, and clipboard

Problem:

Copy/paste is one of the minimum terminal requirements.

Scope:

- Pointer drag selection.
- Word selection.
- Line selection.
- Rectangular selection later if cheap.
- Selection across scrollback and visible screen.
- Copy.
- Paste.
- Bracketed paste.
- Primary selection.
- Clipboard ownership.
- Selection colors.
- Clear selection behavior.

Acceptance:

- Drag selects text.
- Double click selects word.
- Triple click selects line.
- Selection can span wrapped/continued rows where the model supports it.
- Copy writes selected text to clipboard.
- Paste sends clipboard text to the pty.
- If bracketed paste mode is enabled, paste is wrapped with bracketed paste sequences.
- Large paste does not freeze the UI.
- Primary selection works where the platform supports it, or is explicitly deferred.
- Selection text preserves line breaks in a predictable way.
- Copy from scrollback works.

Implementation notes:

- Do not implement paste by simulating per-character keypresses.
- Sanitize pasted text only according to explicit terminal policy. Do not silently strip content except for safety controls that are documented.
- Consider a paste confirmation for very large or multi-line paste as a preference.

### TE-5: Keyboard input coverage

Problem:

The current key support is too small for real terminal apps.

Scope:

- Printable text.
- Ctrl-letter combinations.
- Ctrl-space, Ctrl-[, Ctrl-], Ctrl-\, Ctrl-^, Ctrl-_.
- Alt/Meta combinations.
- Shift/Alt/Ctrl modifiers for arrows and navigation keys.
- Function keys F1-F24.
- Insert/Delete/Home/End/PageUp/PageDown variants.
- Keypad keys.
- Application cursor mode.
- Application keypad mode.
- Backspace/Delete policy.
- Focus in/out reporting if enabled.
- Configurable shortcuts that should be handled by Terminal instead of sent to pty.

Acceptance:

- `Ctrl+C`, `Ctrl+D`, `Ctrl+Z`, `Ctrl+L`, `Ctrl+R`, and common shell shortcuts work.
- `vim`, `nano`, `less`, `htop`, and REPLs receive expected navigation keys.
- Shift/Ctrl/Alt modified arrows produce xterm-compatible sequences.
- Function keys work with common terminal apps.
- Alt combinations are encoded as Escape-prefixed input unless profile says otherwise.
- Application cursor mode is respected.
- Application keypad mode is respected.
- Terminal shortcuts such as copy/paste can be configured and do not break normal shell input.

Implementation notes:

- Add unit tests for input sequence generation.
- Use xterm behavior as the default compatibility target.

### TE-6: Mouse reporting and pointer behavior

Problem:

Many terminal apps rely on mouse reporting. The Terminal also needs normal pointer selection when mouse reporting is not active.

Scope:

- Normal pointer selection.
- Scroll wheel.
- DEC mouse modes 1000, 1002, 1003.
- SGR mouse mode 1006.
- Button press/release.
- Motion reporting.
- Wheel reporting.
- Modifier encoding.
- Alternate-screen scroll behavior.
- Pointer cursor shape.

Acceptance:

- When mouse reporting is off, pointer drag selects text.
- When mouse reporting is on, terminal apps receive mouse events.
- SGR mouse mode works for modern apps.
- Scroll wheel scrolls terminal viewport normally.
- In alternate-screen apps that enable mouse reporting, wheel events are sent to the app.
- Pointer coordinates map correctly after resize and scrollback movement.

Implementation notes:

- Start with SGR mouse and basic button/wheel support if legacy encodings are too much for the first pass.
- Keep selection override behavior configurable later, for example Shift-drag selects even when mouse reporting is active.

### TE-7: Unicode, width, and text attributes

Problem:

The renderer currently emits only `cell.chars[0]`. Real terminal text requires cell-width and grapheme awareness.

Scope:

- UTF-8 decoding.
- libvterm cell character arrays.
- Combining marks.
- Wide characters.
- Ambiguous-width policy.
- Emoji basics.
- Invalid UTF-8 replacement.
- Zero-width cells.
- Double-width cells.
- Copy text reconstruction.
- Text attributes.

Acceptance:

- ASCII remains fast.
- Common Latin text renders correctly.
- Combining marks attach to their base character where supported by the text system.
- CJK wide characters occupy two cells and do not overlap following text.
- Emoji basics do not corrupt layout, even if color emoji rendering is limited.
- Invalid input is replaced safely.
- Copying selected Unicode text preserves the intended text.
- Underline, italic, dim, reverse, strikethrough, and selection attributes render or degrade predictably.

Implementation notes:

- Use libvterm's cell width and `chars` array correctly.
- Keep fast ASCII path, but route non-ASCII through a correctness path.
- Add fixture tests for known Unicode examples.

### TE-8: Color, theme, cursor, and visual rendering

Problem:

The terminal has a useful initial look, but daily use needs configurable colors and better attribute coverage.

Scope:

- 16-color palette.
- 256-color palette.
- Truecolor.
- Default foreground/background.
- Selection foreground/background.
- Cursor color.
- Cursor shape.
- Bold color policy.
- Dim/italic/underline/reverse attributes.
- Bell policy.
- Black glass background.
- Solid background fallback.

Acceptance:

- ANSI 16-color output maps correctly.
- 256-color output maps correctly.
- Truecolor output maps correctly.
- Reverse video works.
- Selection colors remain readable.
- Cursor shape supports block, bar, and underline if framework rendering allows it.
- Terminal can use black glass background by default.
- Terminal can use solid opaque background by profile.
- Visual changes are stored in preferences.

Implementation notes:

- Do not bake all colors into code. Use profiles/color schemes.
- Keep row/layout cache invalidation correct when theme changes.

### TE-9: Resize, reflow, and performance

Problem:

Resize performance improved significantly, but terminal readiness needs repeatable targets and correct behavior under scrollback.

Scope:

- Interactive resize.
- Terminal row/column recalculation.
- `TIOCSWINSZ`.
- Reflow policy.
- Scrollback with resize.
- High-output workloads.
- Row cache invalidation.
- Glyph/layout cache behavior.
- Idle after resize/output.

Acceptance:

- Rapid resize does not crash.
- Rapid resize does not leave stale blank regions after the Window Manager readiness fixes.
- Terminal rows/cols track window size accurately.
- Shell and full-screen terminal apps receive size changes.
- High-output workload remains interactive.
- After output stops, CPU returns to idle.
- The trace terminal rendering and resize scripts remain useful and documented.

Implementation notes:

- Reflow should be terminal-owned once scrollback exists.
- Do not regress row damage/caching while adding scrollback and Unicode.
- Add non-visual performance tests where possible, for example model/layout invalidation counts.

### TE-10: Search and navigation

Problem:

Long terminal output is hard to use without search.

Scope:

- Search query UI.
- Search in visible screen and scrollback.
- Next/previous match.
- Case sensitivity toggle.
- Match highlight.
- Search state with new output.
- Keyboard shortcuts.

Acceptance:

- User can open search with a shortcut.
- Search finds text in scrollback.
- Next/previous navigation works.
- Matches are highlighted.
- Closing search restores normal terminal focus.
- Search does not send typed query characters to the pty.

Implementation notes:

- Regex search is deferred unless cheap.
- Plain substring search is enough for readiness.

### TE-11: Preferences, profiles, and Settings handoff

Problem:

Terminal behavior must be configurable before it becomes a daily tool.

Scope:

- Config path.
- Default profile.
- Shell command.
- Initial working directory.
- Environment variables.
- Font family.
- Font size.
- Line height.
- Scrollback limit.
- Color scheme.
- Background kind: glass or solid.
- Glass blur/base/tint/border/opacity.
- Cursor shape.
- Copy/paste policy.
- Large paste confirmation.
- Bell policy.
- Keybindings.

Acceptance:

- Terminal creates a default config when missing.
- Terminal loads preferences at startup.
- Preferences persist across restarts.
- Invalid preferences fall back safely with visible errors where appropriate.
- Settings can later edit preferences without reverse engineering.
- Profile changes that can apply live do so; restart-required values are marked or documented.

Suggested config:

```toml
[profile.default]
shell = ""
working_directory = ""
scrollback_lines = 10000

[profile.default.font]
family = "monospace"
size = 14
line_height = 18

[profile.default.background]
kind = "glass"
blur_radius = 46
base_color = "#00000080"
tint_color = "#0000006B"
border_color = "#FFFFFF29"
opacity = 1.0

[profile.default.cursor]
shape = "block"
blink = true

[profile.default.copy_paste]
copy_on_select = false
paste_confirm_lines = 5
bracketed_paste = true
```

Implementation notes:

- App-specific preferences can live under `$XDG_CONFIG_HOME/lambda-terminal/config.toml`.
- Settings should later expose the common preferences, but Terminal should not wait for Settings to exist.

### TE-12: Desktop integration

Problem:

Terminal must fit into the Lambda desktop lifecycle.

Scope:

- App id and desktop entry metadata.
- Shell launcher integration.
- Settings integration.
- Clipboard integration.
- URL detection/opening.
- Current working directory handoff.
- Open Terminal Here later from Files.
- Notifications/bell later.

Acceptance:

- Shell app registry can discover and launch `lambda-terminal`.
- Running terminal windows are matched to the Terminal dock item.
- Terminal exposes enough metadata for Settings to edit preferences later.
- Terminal clipboard operations use desktop clipboard services.
- URL detection can identify common URL patterns.
- Opening a URL uses the shared open-with/default-app service where available.
- Files can later request "Open Terminal Here" through a clean command-line or IPC interface.

Implementation notes:

- A CLI argument such as `--working-directory PATH` is useful and should be included.
- A CLI argument such as `--command COMMAND...` can be deferred if shell/profile complexity grows.

### TE-13: Tests and validation

Problem:

Terminal correctness is easy to regress. Most behavior can be tested without rendering real pixels.

Scope:

- Input encoder tests.
- Scrollback tests.
- Selection tests.
- Clipboard text reconstruction tests.
- Unicode width tests.
- Color/attribute tests.
- Resize/model tests.
- Config tests.
- PTY smoke tests.
- Manual application matrix.

Acceptance:

- Unit tests cover key sequence generation.
- Unit tests cover application cursor/keypad mode sequence generation.
- Unit tests cover scrollback push, viewport movement, and limit enforcement.
- Unit tests cover normal vs alternate screen behavior.
- Unit tests cover selection and copied text.
- Unit tests cover bracketed paste output.
- Unit tests cover Unicode width and combining examples.
- Unit tests cover color palette conversion.
- Unit tests cover config defaults and invalid values.
- Smoke tests run a controlled pty child and verify output reaches the model without pointer movement.
- Manual validation covers shells and terminal applications.

Implementation notes:

- Prefer model-level tests over screenshot tests.
- Keep trace scripts for manual performance validation.

## Implementation order

1. Split the implementation into components.

   Extract session, model, renderer, input encoder, selection, clipboard, and preferences.

2. Standardize app identity and lifecycle.

   Set canonical app id, improve shell exit state, and clean child process handling.

3. Add scrollback and viewport.

   Implement normal/alternate screen behavior and scrollback limit.

4. Add selection and clipboard.

   Implement pointer selection, copy, paste, bracketed paste, and selection rendering.

5. Expand input encoding.

   Cover keys, modifiers, function keys, keypad, app cursor/keypad modes, and shortcuts.

6. Add Unicode and attribute correctness.

   Fix cell text construction and rendering for wide/combining text and additional attributes.

7. Add mouse reporting.

   Support common mouse modes and selection override behavior.

8. Add preferences/profiles.

   Make font, colors, background, shell, scrollback, cursor, and paste policy configurable.

9. Add search and desktop integration.

   Add scrollback search, URL opening, and Files/Settings handoff points.

10. Add tests and update docs.

   Cover model behavior, input, scrollback, selection, Unicode, config, and manual validation.

## Manual validation checklist

### Build and unit checks

```sh
cmake --build build --target flux_tests
./build/flux_tests --test-case="*terminal*"
cmake --build build --target lambda-terminal
git diff --check
```

### Launch checks

```sh
./build/lambda-window-manager
./build/lambda-shell
./build/examples/lambda-terminal
```

Expected:

- Terminal opens with black glass background.
- Shell prompt appears without moving the pointer.
- Typing appears immediately.
- `Ctrl+C` reaches the shell/app.
- Window title updates when shell/program sets it.
- Closing the window cleans up the child shell.

### Shell workflow checks

Validate:

- `echo hello`
- `printf` with many lines
- `cat`
- `less`
- `vim` or `nvim`
- `nano`
- `htop` or similar full-screen app
- Python/Node/other REPL if installed
- command failure and exit status visibility in shell

Expected:

- Output renders correctly.
- Full-screen apps enter and exit alternate screen correctly.
- Keyboard navigation works.
- Mouse behavior works where apps enable it.

### Scrollback checks

Validate:

- output more than one screen
- scroll wheel
- page up/down
- jump to bottom
- new output while scrolled up
- alternate-screen app does not pollute normal scrollback
- scrollback limit enforcement

Expected:

- Previous output is available.
- Viewport behavior is predictable.
- Memory use remains bounded.

### Selection and clipboard checks

Validate:

- drag selection
- word selection
- line selection
- selection across rows
- selection in scrollback
- copy
- paste
- multi-line paste
- bracketed paste in apps that enable it
- primary selection if supported

Expected:

- Copied text is correct.
- Paste reaches the pty once and in the correct form.
- Large paste policy is respected.

### Key input checks

Validate:

- Ctrl-letter shortcuts.
- Alt-letter shortcuts.
- Shift/Ctrl/Alt arrows.
- Home/End/PageUp/PageDown.
- Insert/Delete.
- F1-F12 at minimum.
- keypad keys if available.
- application cursor mode in `vim`.
- application keypad mode if testable.

Expected:

- Common shells and terminal apps interpret keys correctly.

### Unicode and color checks

Validate:

- ASCII.
- accented Latin text.
- combining marks.
- CJK wide characters.
- emoji basics.
- invalid UTF-8 fixture.
- ANSI 16 colors.
- 256 colors.
- truecolor.
- bold.
- dim.
- italic.
- underline.
- reverse.
- strikethrough if supported.

Expected:

- Text does not overlap or corrupt neighboring cells.
- Colors and attributes are readable and predictable.

### Resize and performance checks

Validate:

- rapid window resize while shell idle.
- rapid window resize while output is streaming.
- `scripts/trace-terminal-rendering.sh`.
- `scripts/trace-terminal-resize.sh`.
- large output such as `find /usr -maxdepth 4` or a controlled synthetic workload.

Expected:

- No severe artifacts.
- Rows/columns update accurately.
- CPU returns to idle after output/resize stops.
- The terminal remains responsive.

### Preference checks

Validate:

- font size.
- line height.
- scrollback limit.
- color scheme.
- glass background.
- solid background.
- cursor shape.
- default shell.
- working directory argument.
- invalid config values.

Expected:

- Valid preferences apply or are clearly marked restart-required.
- Invalid preferences fall back safely.
- Preferences persist across restart.

## Test additions

Add focused automated tests where behavior is deterministic:

- Input encoder for Ctrl, Alt, modified arrows, function keys, keypad, app cursor mode, and app keypad mode.
- Bracketed paste encoding.
- Scrollback push and limit enforcement.
- Normal screen and alternate screen switching.
- Viewport scroll behavior.
- Selection coordinate mapping.
- Copied text reconstruction.
- Unicode width for ASCII, combining marks, wide CJK, emoji basics, and invalid UTF-8.
- Color palette and truecolor conversion.
- Attribute model for bold, dim, italic, underline, reverse, and strikethrough.
- Resize rows/cols calculation.
- Config defaults, parsing, validation, and invalid fallback.
- PTY smoke test with a controlled child process.

## Done checklist

- [ ] Terminal implementation is split into testable components.
- [x] Canonical app id is `lambda-terminal`.
- [x] Shell lifecycle and child cleanup are robust.
- [x] Scrollback works with configurable limit.
- [x] Normal and alternate screen behavior is correct.
- [x] Selection works across visible screen and scrollback.
- [ ] Copy and paste work through desktop clipboard.
- [x] Bracketed paste works.
- [x] Key encoding covers common terminal apps.
- [x] Mouse reporting covers common terminal apps.
- [x] Unicode wide/combining text is handled correctly enough for daily use.
- [x] ANSI/256/truecolor and common attributes render correctly.
- [x] Resize remains responsive and accurate.
- [x] Search works across visible screen and scrollback.
- [x] Preferences/profiles exist and persist.
- [x] Black glass background is configurable, not hard-coded only.
- [x] Desktop integration with Shell app registry works.
- [x] Tests cover model, input, scrollback, selection, Unicode, color, resize, config, and pty smoke behavior.
- [ ] User guide and app docs match actual behavior.

## Deferred to later milestones

- Tabs.
- Split panes.
- Terminal multiplexing.
- SSH connection manager.
- Serial terminal UI.
- Sixel, Kitty graphics protocol, iTerm inline images, and rich terminal media.
- Full ligature support.
- Full IME support.
- Advanced profile import/export.
- Terminal bell notifications through a notification daemon.
- Deep shell integration.
- GPU text-rendering rewrite unless performance requires it.
