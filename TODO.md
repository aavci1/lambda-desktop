# TODO

When starting work, read all TODO items in this document, pick one item to complete, and ask questions until the bug or TODO item is perfectly clear before starting implementation. Handle any directly related work that can be fixed or completed together. Always build with `-j$(nproc)`. Always try to automate testing or verification of the implementation when possible; if automatic verification is not practical, tell the user exactly how to verify it manually. Then update this document by deleting the completed item or revising its description based on the outcome, commit the changes, and push.

Verification labels: `[Auto]` means the item can be automatically tested or verified. `[Manual]` means the item requires manual verification. `[Auto + Manual]` means automated coverage should be added, but manual verification is also required.

## TODO Summary

| ID | Type | Item | Severity | Priority |
| --- | --- | --- | --- | --- |
| TODO-002 | Bug | Flux app clipboard shortcuts and shared text clipboard need cross-app validation | High | P1 |
| TODO-006 | Bug | Window close animation is inconsistent across window types | Medium | P2 |
| TODO-007 | Bug | Minimized windows and dock previews are not wired across Window Manager and Shell | Medium | P2 |
| TODO-008 | Bug | Live resize can stretch stale window content | Medium | P1 |
| TODO-009 | Bug | Files opens supported images in Firefox instead of Preview | Medium | P1 |
| TODO-013 | Feature | Add Editor file watcher with reload prompt | N/A | P1 |
| TODO-014 | Bug | Tooltips are not showing | Medium | P1 |

## TODO-002: Flux app clipboard shortcuts and shared text clipboard need cross-app validation

- [ ] [Auto] Keep this item scoped to Flux app text editing shortcuts, Terminal-specific clipboard shortcuts, and shared plain-text copy/paste behavior; do not treat Shell clipboard history as the same feature.
- [ ] [Auto] Treat the existing Wayland clipboard/data-device protocol path as supporting infrastructure that still needs app-level shortcut and cross-application workflow validation.
- [ ] [Auto + Manual] Fix copy/paste as one cross-application clipboard feature instead of handling per-app bugs separately.
- [ ] [Auto + Manual] Plain text copy and paste should work consistently anywhere text can be selected or edited across Flux apps.
- [ ] [Auto] Regular editable text surfaces should use Ctrl+C for copy and Ctrl+V for paste.
- [ ] [Auto] Terminal should use terminal-standard Ctrl+Shift+C for copy and Ctrl+Shift+V for paste, so Ctrl+C remains available for terminal interrupt behavior.
- [ ] [Auto] Do not add or rely on Super+C or Super+V clipboard behavior.
- [ ] [Auto + Manual] Verify copy and paste across multiple apps or text surfaces, including Terminal and at least two regular editable text surfaces. Editor-local Ctrl+A/C/X/V and Ctrl+Z/Shift+Ctrl+Z behavior has focused automated coverage, but the shared cross-application clipboard path still needs validation.

## TODO-006: Window close animation is inconsistent across window types

- [ ] [Manual] When a window is closed, the chrome currently disappears before the window contents in some cases. The full window should fade out as one unit.
- [ ] [Auto + Manual] Use a single snapshot of the whole window for the close animation, including chrome, content, shadow, and transparent regions.
- [ ] [Auto] Stop input to the closing window immediately when the close starts.
- [ ] [Auto + Manual] Implement the close animation through one standard compositor path instead of app-specific behavior.
- [ ] [Auto + Manual] Ensure close behavior is consistent for apps with system titlebars, apps with integrated/custom titlebars, transparent content, undecorated windows, and normal opaque content.
- [ ] [Manual] Verify by closing representative windows from multiple app types and confirming chrome, shadow, and content fade together with no separate disappearance.

## TODO-007: Minimized windows and dock previews are not wired across Window Manager and Shell

- [ ] [Auto] Keep the ownership boundary explicit: Window Manager owns minimized window state, focus/stacking exclusion, Super+Tab eligibility, preview/snapshot source, and restore requests; Shell owns dock item presentation, opening space in the dock, click targets, and dock-side animation.
- [ ] [Auto + Manual] Define or reuse a Window Manager/Shell IPC contract for minimized window identity, preview updates, restore requests, and animation timing instead of letting Shell infer minimized state from the normal running-app list alone.
- [ ] [Auto + Manual] Minimized windows should disappear from the desktop and should not appear in the Super+Tab window list while minimized.
- [ ] [Manual] When minimizing, Shell should animate open space for the minimized window item while Window Manager simultaneously scales the window down and moves it into that dock space.
- [ ] [Auto + Manual] The dock item should show a window preview, and the preview should stay up to date during the minimized period.
- [ ] [Manual] Clicking the minimized window preview in the dock should restore the window with the reverse animation.
- [ ] [Auto + Manual] Restoring should return the window to the size, position, stacking behavior, and focus state it had before minimization.
- [ ] [Auto] Support multiple minimized windows as distinct restorable window previews rather than losing per-window identity.
- [ ] [Manual] Verify with multiple app windows that minimized windows leave the desktop, are excluded from Super+Tab, keep their dock previews current, and restore with the reverse animation.

## TODO-008: Live resize can stretch stale window content

- [ ] [Manual] Resizing the terminal app sometimes causes the content to stretch, even when the terminal is minimal.
- [ ] [Auto + Manual] Keep this tied to the compositor frame-coherence work in `docs/compositor-wlroots-improvement-plan.md` WM-COMP-1: the fix should build one coherent frame model for chrome, borders, background, and client content from a single committed geometry snapshot.
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

## TODO-014: Tooltips are not showing

- [ ] [Manual] Tooltips are not showing at all when running the tooltip demo, even though they previously worked at least on macOS.
- [ ] [Auto + Manual] Fix the shared `Tooltip`/`useTooltip` implementation rather than adding local tooltip popover behavior per app or demo.
- [ ] [Auto + Manual] Verify the tooltip demo shows tooltips after hover delay for buttons, icons, toggles, and placement examples.
- [ ] [Auto + Manual] Verify Editor toolbar buttons use the same real tooltip implementation and show their labels on hover.
- [ ] [Auto] Add automated coverage where practical for tooltip state/lifecycle behavior, including hover enter, hover exit, timer cancellation, and avoiding stale tooltip popovers after the target unmounts.
