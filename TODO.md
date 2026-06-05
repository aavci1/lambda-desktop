# TODO

When starting work, read all TODO items in this document, pick one item to complete, and ask questions until the bug or TODO item is perfectly clear before starting implementation. Handle any directly related work that can be fixed or completed together. Always try to automate testing or verification of the implementation when possible; if automatic verification is not practical, tell the user exactly how to verify it manually. Then update this document by deleting the completed item or revising its description based on the outcome, commit the changes, and push.

## TODO Summary

| ID | Type | Item | Severity | Priority |
| --- | --- | --- | --- | --- |
| TODO-002 | Bug | Copy/paste is not working across applications | High | P1 |
| TODO-004 | Feature | Complete basic Editor functionality and toolbar actions | N/A | P1 |
| TODO-006 | Bug | Window close animation is inconsistent across window types | Medium | P2 |
| TODO-007 | Bug | Minimized apps do not move to the dock with previews | Medium | P2 |
| TODO-008 | Bug | Window content can stretch while resizing | Medium | P1 |
| TODO-009 | Bug | Files opens supported images in Firefox instead of Preview | Medium | P1 |
| TODO-010 | Bug | Files loses window events after launching another app | High | P0 |
| TODO-011 | Feature | Fix and enhance Super+Tab window cycler | N/A | P1 |
| TODO-013 | Feature | Add Editor file watcher with reload prompt | N/A | P1 |

## TODO-002: Copy/paste is not working across applications

- [ ] Fix copy/paste as one cross-application clipboard feature instead of handling per-app bugs separately.
- [ ] Plain text copy and paste should work consistently anywhere text can be selected or edited across Flux apps.
- [ ] Regular editable text surfaces should use Ctrl+C for copy and Ctrl+V for paste.
- [ ] Terminal should use terminal-standard Ctrl+Shift+C for copy and Ctrl+Shift+V for paste, so Ctrl+C remains available for terminal interrupt behavior.
- [ ] Do not add or rely on Super+C or Super+V clipboard behavior.
- [ ] The text editor selection bug is part of this item: pressing Ctrl+C currently replaces the selected text with `c`, and there is no way to bring the text back because Ctrl+Z is not working.
- [ ] Verify copy and paste across multiple apps or text surfaces, including the Editor app, Terminal, and at least one other application.

## TODO-004: Complete basic Editor functionality and toolbar actions

- [ ] Current state: New, Open, Save, and Save As are implemented in `apps/lambda-editor/main.cpp`.
- [ ] Replace the remaining placeholder toolbar actions with real behavior, or remove any action that is intentionally out of scope instead of leaving a clickable "not implemented" button.
- [ ] Add undo/redo for normal text editing operations: typing, delete/backspace, newline insertion, cut, and paste.
- [ ] Use Ctrl+Z for undo and Ctrl+Shift+Z for redo.
- [ ] Wire toolbar buttons, keyboard shortcuts, menu/action registry entries, and any focused text-edit command handlers to the same underlying editor commands.
- [ ] Implement editing toolbar actions: Cut, Copy, Paste, Delete, and Select All. Clipboard behavior should stay consistent with TODO-002.
- [ ] Implement navigation/search toolbar actions: Find, Replace, and Go To Line.
- [ ] Implement view controls: Word Wrap, Zoom Out, Zoom In, and Font selection or font size controls.
- [ ] Resolve the Print toolbar action by implementing it if platform support exists, or removing it from the primary toolbar until printing is supported.
- [ ] Ensure toolbar buttons and shortcuts have correct enabled/disabled states based on selection, clipboard availability, undo/redo history, and document state.
- [ ] Automate verification for document edit history and editor commands where possible, including undo/redo over typing, delete/backspace, newline, cut, and paste.

## TODO-006: Window close animation is inconsistent across window types

- [ ] When a window is closed, the chrome currently disappears before the window contents in some cases. The full window should fade out as one unit.
- [ ] Use a single snapshot of the whole window for the close animation, including chrome, content, shadow, and transparent regions.
- [ ] Stop input to the closing window immediately when the close starts.
- [ ] Implement the close animation through one standard compositor path instead of app-specific behavior.
- [ ] Ensure close behavior is consistent for apps with system titlebars, apps with integrated/custom titlebars, transparent content, undecorated windows, and normal opaque content.
- [ ] Verify by closing representative windows from multiple app types and confirming chrome, shadow, and content fade together with no separate disappearance.

## TODO-007: Minimized apps do not move to the dock with previews

- [ ] Minimized windows should disappear from the desktop and should not appear in the Super+Tab window list while minimized.
- [ ] When minimizing, the dock should animate open space for the minimized window item while the window simultaneously scales down and moves into that dock space.
- [ ] The dock item should show a window preview, and the preview should stay up to date during the minimized period.
- [ ] Clicking the minimized window preview in the dock should restore the window with the reverse animation.
- [ ] Restoring should return the window to the size, position, stacking behavior, and focus state it had before minimization.
- [ ] Support multiple minimized windows as distinct restorable window previews rather than losing per-window identity.
- [ ] Verify with multiple app windows that minimized windows leave the desktop, are excluded from Super+Tab, keep their dock previews current, and restore with the reverse animation.

## TODO-008: Window content can stretch while resizing

- [ ] Resizing the terminal app sometimes causes the content to stretch, even when the terminal is minimal.
- [ ] During live resize, existing window content/framebuffer should remain unscaled and aligned to the top-left.
- [ ] Newly exposed space should be filled immediately with the correct window/app background during live resize.
- [ ] The terminal is the known reproduction case, but verify other app windows too and fix any shared compositor or rendering path that can stretch content during resize.
- [ ] Verify by resizing Terminal and representative non-terminal apps while watching for stretched content, stale scaled framebuffer regions, or delayed background fill.

## TODO-009: Files opens supported images in Firefox instead of Preview

- [ ] Clicking a supported image in Files currently opens it in Firefox, likely because app/MIME association falls through to the browser.
- [ ] Supported image files should open in `lambda-preview` without requiring Preview to be installed into the host system.
- [ ] Prefer solving this through Flux's local app registry/open-with path: give the local development `lambda-preview` app entry the MIME types Preview can actually open, then have Files choose it for those types.
- [ ] Preview-supported MIME types should include the formats handled by the current image loader and Files MIME detector, including `image/png`, `image/jpeg`, `image/gif`, `image/webp`, and `image/svg+xml`; add other formats only if `lambda::loadImage` can actually decode them.
- [ ] Keep system `mimeapps.list` support for installed apps, but local Flux app associations should work in a build-tree/development run without writing desktop files to the user's system.
- [ ] Add or update tests around local `lambda-preview` registration and Files default open-with resolution so supported images choose Preview instead of Firefox/browser fallback.
- [ ] Verify manually by running from the development build and opening PNG, JPEG, and SVG files from Files.

## TODO-010: Files loses window events after launching another app

- [ ] When Files launches another app for a file, the launch should always be non-modal.
- [ ] Files must remain movable, closable, focusable, and able to receive normal window events after launching another app.
- [ ] The launched app may take focus, but it must not capture or block input/events for the Files window.
- [ ] Known repro: opening an image in Firefox from Files leaves Files unable to move or close and not receiving events.
- [ ] The same event handling issue happens when opening a video.
- [ ] Verify by opening image and video files from Files, then moving, focusing, and closing the Files window while the launched app remains open.

## TODO-011: Fix and enhance Super+Tab window cycler

- [ ] The window manager can only cycle through two apps with Super+Tab.
- [ ] When Super+Tab starts, build a stable ordered list of eligible windows and cycle through that list until the switcher interaction ends.
- [ ] Order the cycle list by most-recently-used window order.
- [ ] Exclude minimized windows from the cycle list.
- [ ] Super+Tab should cycle forward; Shift+Super+Tab should cycle backward.
- [ ] Add a window switcher overlay that appears while the user is cycling windows with Super+Tab or Shift+Super+Tab.
- [ ] Use the same stable window list built when Super+Tab starts, so the visual order does not change while the user cycles.
- [ ] Render each window as an item with a live or recently captured window preview at the top, then the app icon and readable window/app title underneath.
- [ ] Show a clear current-selection indicator that moves as the user presses Tab, without activating the selected window until the switcher is confirmed.
- [ ] Keep the overlay non-focusable and non-destructive: it should not steal app input, change window order during cycling, or interfere with pointer events outside the switcher flow.
- [ ] On Super release, activate the selected window. If cancellation is supported, return focus to the original window without changing the active window.
- [ ] For many open windows, keep the selected item visible and avoid shrinking previews below a useful size; prefer a horizontally scrollable or centered-neighbor layout over showing every window at unreadable size.
- [ ] Verify with at least three open non-minimized windows that repeated Super+Tab visits every window in MRU order, wraps correctly, and Shift+Super+Tab cycles backward.

## TODO-013: Add Editor file watcher with reload prompt

- [ ] In the Editor app, watch the currently opened file for external changes.
- [ ] When the watched file changes outside the Editor app, show a non-blocking, non-intrusive bottom-right banner/toast asking whether to reload the file instead of reloading automatically.
- [ ] The reload banner/toast should be easy to ignore and should not block editing, scrolling, toolbar actions, window movement, or window close.
- [ ] Provide clear Reload and Dismiss actions in the banner/toast.
- [ ] Do not show the reload banner/toast for saves initiated by the same Editor instance.
- [ ] If the user reloads, preserve the current scroll position and caret position when possible; if the file changed enough that the exact position no longer exists, clamp to the nearest valid position.
- [ ] If the buffer has unsaved local changes, the banner/toast should clearly communicate that reloading will discard those edits, and the user should be able to ignore local changes and reload in one action.
- [ ] If the watched file is deleted or renamed externally, show a similar non-blocking bottom-right banner/toast explaining that the file disappeared, with Dismiss and Save As actions.
- [ ] Update the watcher when the user opens a different file, creates a new file, or closes the document, and avoid leaving stale watchers running.
- [ ] Coalesce duplicate filesystem events so one external save produces one reload prompt.
- [ ] Automate verification if possible by opening a file in Editor, changing it externally, asserting the reload banner/toast appears, accepting reload, and checking that scroll and caret position are preserved.
