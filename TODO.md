# TODO

When starting work, read all TODO items in this document, pick one item to complete, and ask questions until the bug or TODO item is perfectly clear before starting implementation. Handle any directly related work that can be fixed or completed together. Always build with `-j$(nproc)`. Always try to automate testing or verification of the implementation when possible; if automatic verification is not practical, tell the user exactly how to verify it manually. Then update this document by deleting the completed item or revising its description based on the outcome, commit the changes, and push.

Verification labels: `[Auto]` means the item can be automatically tested or verified. `[Manual]` means the item requires manual verification. `[Auto + Manual]` means automated coverage should be added, but manual verification is also required.

## TODO Summary

| ID | Type | Item | Severity | Priority |
| --- | --- | --- | --- | --- |
| TODO-002 | Bug | Copy/paste is not working across applications | High | P1 |
| TODO-004 | Feature | Complete basic Editor functionality and toolbar actions | N/A | P1 |
| TODO-006 | Bug | Window close animation is inconsistent across window types | Medium | P2 |
| TODO-007 | Bug | Minimized apps do not move to the dock with previews | Medium | P2 |
| TODO-008 | Bug | Window content can stretch while resizing | Medium | P1 |
| TODO-009 | Bug | Files opens supported images in Firefox instead of Preview | Medium | P1 |
| TODO-010 | Bug | Verify Files remains interactive after launching another app | High | P0 |
| TODO-011 | Feature | Fix and enhance Super+Tab window cycler | N/A | P1 |
| TODO-013 | Feature | Add Editor file watcher with reload prompt | N/A | P1 |
| TODO-014 | Bug | File dialog view stops using full width after folder navigation | Medium | P2 |
| TODO-015 | Feature | Simplify Open and Save file dialog navigation UI | N/A | P2 |

## TODO-002: Copy/paste is not working across applications

- [ ] [Auto + Manual] Fix copy/paste as one cross-application clipboard feature instead of handling per-app bugs separately.
- [ ] [Auto + Manual] Plain text copy and paste should work consistently anywhere text can be selected or edited across Flux apps.
- [ ] [Auto] Regular editable text surfaces should use Ctrl+C for copy and Ctrl+V for paste.
- [ ] [Auto] Terminal should use terminal-standard Ctrl+Shift+C for copy and Ctrl+Shift+V for paste, so Ctrl+C remains available for terminal interrupt behavior.
- [ ] [Auto] Do not add or rely on Super+C or Super+V clipboard behavior.
- [ ] [Auto] The text editor selection bug is part of this item: pressing Ctrl+C currently replaces the selected text with `c`, and there is no way to bring the text back because Ctrl+Z is not working.
- [ ] [Auto + Manual] Verify copy and paste across multiple apps or text surfaces, including the Editor app, Terminal, and at least one other application.

## TODO-004: Complete basic Editor functionality and toolbar actions

- [ ] [Manual] Current state: New, Open, Save, and Save As are implemented in `apps/lambda-editor/main.cpp`.
- [ ] [Auto + Manual] Replace the remaining placeholder toolbar actions with real behavior, or remove any action that is intentionally out of scope instead of leaving a clickable "not implemented" button.
- [ ] [Auto] Add undo/redo for normal text editing operations: typing, delete/backspace, newline insertion, cut, and paste.
- [ ] [Auto] Use Ctrl+Z for undo and Ctrl+Shift+Z for redo.
- [ ] [Auto + Manual] Wire toolbar buttons, keyboard shortcuts, menu/action registry entries, and any focused text-edit command handlers to the same underlying editor commands.
- [ ] [Auto + Manual] Implement editing toolbar actions: Cut, Copy, Paste, Delete, and Select All. Clipboard behavior should stay consistent with TODO-002.
- [ ] [Auto + Manual] Implement navigation/search toolbar actions: Find, Replace, and Go To Line.
- [ ] [Auto + Manual] Implement view controls: Word Wrap, Zoom Out, Zoom In, and Font selection or font size controls.
- [ ] [Auto + Manual] Resolve the Print toolbar action by implementing it if platform support exists, or removing it from the primary toolbar until printing is supported.
- [ ] [Auto + Manual] Ensure toolbar buttons and shortcuts have correct enabled/disabled states based on selection, clipboard availability, undo/redo history, and document state.
- [ ] [Auto] Automate verification for document edit history and editor commands where possible, including undo/redo over typing, delete/backspace, newline, cut, and paste.

## TODO-006: Window close animation is inconsistent across window types

- [ ] [Manual] When a window is closed, the chrome currently disappears before the window contents in some cases. The full window should fade out as one unit.
- [ ] [Auto + Manual] Use a single snapshot of the whole window for the close animation, including chrome, content, shadow, and transparent regions.
- [ ] [Auto] Stop input to the closing window immediately when the close starts.
- [ ] [Auto + Manual] Implement the close animation through one standard compositor path instead of app-specific behavior.
- [ ] [Auto + Manual] Ensure close behavior is consistent for apps with system titlebars, apps with integrated/custom titlebars, transparent content, undecorated windows, and normal opaque content.
- [ ] [Manual] Verify by closing representative windows from multiple app types and confirming chrome, shadow, and content fade together with no separate disappearance.

## TODO-007: Minimized apps do not move to the dock with previews

- [ ] [Auto + Manual] Minimized windows should disappear from the desktop and should not appear in the Super+Tab window list while minimized.
- [ ] [Manual] When minimizing, the dock should animate open space for the minimized window item while the window simultaneously scales down and moves into that dock space.
- [ ] [Auto + Manual] The dock item should show a window preview, and the preview should stay up to date during the minimized period.
- [ ] [Manual] Clicking the minimized window preview in the dock should restore the window with the reverse animation.
- [ ] [Auto + Manual] Restoring should return the window to the size, position, stacking behavior, and focus state it had before minimization.
- [ ] [Auto] Support multiple minimized windows as distinct restorable window previews rather than losing per-window identity.
- [ ] [Manual] Verify with multiple app windows that minimized windows leave the desktop, are excluded from Super+Tab, keep their dock previews current, and restore with the reverse animation.

## TODO-008: Window content can stretch while resizing

- [ ] [Manual] Resizing the terminal app sometimes causes the content to stretch, even when the terminal is minimal.
- [ ] [Auto + Manual] During live resize, existing window content/framebuffer should remain unscaled and aligned to the top-left.
- [ ] [Auto + Manual] Newly exposed space should be filled immediately with the correct window/app background during live resize.
- [ ] [Auto + Manual] The terminal is the known reproduction case, but verify other app windows too and fix any shared compositor or rendering path that can stretch content during resize.
- [ ] [Manual] Verify by resizing Terminal and representative non-terminal apps while watching for stretched content, stale scaled framebuffer regions, or delayed background fill.

## TODO-009: Files opens supported images in Firefox instead of Preview

- [ ] [Manual] Clicking a supported image in Files currently opens it in Firefox, likely because app/MIME association falls through to the browser.
- [ ] [Auto + Manual] Supported image files should open in `lambda-preview` without requiring Preview to be installed into the host system.
- [ ] [Auto] Prefer solving this through Flux's local app registry/open-with path: give the local development `lambda-preview` app entry the MIME types Preview can actually open, then have Files choose it for those types.
- [ ] [Auto] Preview-supported MIME types should include the formats handled by the current image loader and Files MIME detector, including `image/png`, `image/jpeg`, `image/gif`, `image/webp`, and `image/svg+xml`; add other formats only if `lambda::loadImage` can actually decode them.
- [ ] [Auto] Keep system `mimeapps.list` support for installed apps, but local Flux app associations should work in a build-tree/development run without writing desktop files to the user's system.
- [ ] [Auto] Add or update tests around local `lambda-preview` registration and Files default open-with resolution so supported images choose Preview instead of Firefox/browser fallback.
- [ ] [Manual] Verify manually by running from the development build and opening PNG, JPEG, and SVG files from Files.

## TODO-010: Verify Files remains interactive after launching another app

- [ ] [Manual] Manually verify the async launch fix by opening image and video files from Files, then moving, focusing, and closing the Files window while the launched app remains open.
- [ ] [Manual] If Files still loses events after the async launch fix, investigate compositor focus/input routing next; the file launch path should already be non-blocking and should not leave the launched app holding Files' inherited non-stdio file descriptors.

## TODO-011: Fix and enhance Super+Tab window cycler

- [ ] [Manual] The window manager can only cycle through two apps with Super+Tab.
- [ ] [Auto] When Super+Tab starts, build a stable ordered list of eligible windows and cycle through that list until the switcher interaction ends.
- [ ] [Auto] Order the cycle list by most-recently-used window order.
- [ ] [Auto] Exclude minimized windows from the cycle list.
- [ ] [Auto + Manual] Super+Tab should cycle forward; Shift+Super+Tab should cycle backward.
- [ ] [Manual] Add a window switcher overlay that appears while the user is cycling windows with Super+Tab or Shift+Super+Tab.
- [ ] [Auto] Use the same stable window list built when Super+Tab starts, so the visual order does not change while the user cycles.
- [ ] [Manual] Render each window as an item with a live or recently captured window preview at the top, then the app icon and readable window/app title underneath.
- [ ] [Manual] Show a clear current-selection indicator that moves as the user presses Tab, without activating the selected window until the switcher is confirmed.
- [ ] [Auto + Manual] Keep the overlay non-focusable and non-destructive: it should not steal app input, change window order during cycling, or interfere with pointer events outside the switcher flow.
- [ ] [Auto + Manual] On Super release, activate the selected window. If cancellation is supported, return focus to the original window without changing the active window.
- [ ] [Manual] For many open windows, keep the selected item visible and avoid shrinking previews below a useful size; prefer a horizontally scrollable or centered-neighbor layout over showing every window at unreadable size.
- [ ] [Auto + Manual] Verify with at least three open non-minimized windows that repeated Super+Tab visits every window in MRU order, wraps correctly, and Shift+Super+Tab cycles backward.

## TODO-013: Add Editor file watcher with reload prompt

- [ ] [Auto] In the Editor app, watch the currently opened file for external changes.
- [ ] [Auto + Manual] When the watched file changes outside the Editor app, show a non-blocking, non-intrusive bottom-right banner/toast asking whether to reload the file instead of reloading automatically.
- [ ] [Auto + Manual] The reload banner/toast should be easy to ignore and should not block editing, scrolling, toolbar actions, window movement, or window close.
- [ ] [Auto + Manual] Provide clear Reload and Dismiss actions in the banner/toast.
- [ ] [Auto] Do not show the reload banner/toast for saves initiated by the same Editor instance.
- [ ] [Auto] If the user reloads, preserve the current scroll position and caret position when possible; if the file changed enough that the exact position no longer exists, clamp to the nearest valid position.
- [ ] [Auto + Manual] If the buffer has unsaved local changes, the banner/toast should clearly communicate that reloading will discard those edits, and the user should be able to ignore local changes and reload in one action.
- [ ] [Auto + Manual] If the watched file is deleted or renamed externally, show a similar non-blocking bottom-right banner/toast explaining that the file disappeared, with Dismiss and Save As actions.
- [ ] [Auto] Update the watcher when the user opens a different file, creates a new file, or closes the document, and avoid leaving stale watchers running.
- [ ] [Auto] Coalesce duplicate filesystem events so one external save produces one reload prompt.
- [ ] [Auto] Automate verification if possible by opening a file in Editor, changing it externally, asserting the reload banner/toast appears, accepting reload, and checking that scroll and caret position are preserved.

## TODO-014: File dialog view stops using full width after folder navigation

- [ ] [Manual] In the file open dialog, the file/folder view area does not use the full available width after clicking into another folder.
- [ ] [Auto + Manual] Navigating into a folder should preserve or recompute the file/folder view layout so it fills the available content width.
- [ ] [Auto + Manual] Verify the issue in the Open File dialog and any shared FileDialog view used by Save dialogs.
- [ ] [Auto] Add layout coverage if possible for the FileDialog content area after directory navigation, ensuring the file/folder list or grid receives the expected width constraints.
- [ ] [Manual] Manually verify by opening the file dialog, navigating into a subfolder, and confirming the file/folder view area still spans the dialog width.

## TODO-015: Simplify Open and Save file dialog navigation UI

- [ ] [Auto + Manual] Apply these FileDialog UI changes to both Open and Save dialogs.
- [ ] [Auto + Manual] Replace the URL/path bar with a breadcrumb control similar to the Files app.
- [ ] [Auto + Manual] Replace the single Up button with a Back / Up / Forward button group.
- [ ] [Auto + Manual] Remove the Go button and any dialog functionality that only exists to submit the removed URL/path bar.
- [ ] [Auto + Manual] Remove the Show Hidden control.
- [ ] [Auto + Manual] Remove the `Name: Select a file` input/control from both Open and Save dialogs.
- [ ] [Manual] Remove the file/folder count text.
- [ ] [Manual] Remove the extra `Open File` or `Save File` label from inside the dialog if it duplicates the window title or primary action.
- [ ] [Auto + Manual] Remove the Refresh and New Folder buttons.
- [ ] [Auto] Remove or disable the Refresh and New Folder functionality so the removed buttons do not leave hidden shortcuts or unreachable actions behind.
- [ ] [Auto + Manual] Verify keyboard and pointer navigation still allow entering folders, going back, going up, going forward, selecting a file, confirming Open or Save, and canceling the dialog.
- [ ] [Manual] Manually verify the simplified Open and Save dialogs against the Files app breadcrumb behavior and confirm the removed controls no longer appear in either mode.
