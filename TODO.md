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
| TODO-015 | Feature | Add a cross-window command registry and command palette | N/A | P2 |
| TODO-016 | Bug | useAutoFocus cannot focus targets inside nested child components | Medium | P2 |
| TODO-017 | Bug | Overlay rebuild test trips stack-use-after-scope under ASan | Medium | P2 |
| TODO-019 | Performance | Work through the frame-pacing improvement plan | High | P1 |

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

## TODO-015: Add a cross-window command registry and command palette

- [ ] [Auto + Manual] Use `lambda-editor` as the first pilot app/window for the command registry and command palette.
- [ ] [Auto + Manual] Use `Ctrl+Shift+P` as the v1 command palette shortcut for the focused editor window. Do not show the command palette command itself inside the command palette.
- [ ] [Auto] Keep v1 window-scoped only; do not include app-level, Shell-level, compositor-level, or cross-window/global command scopes in the first implementation.
- [ ] [Auto] Give each command a stable public ID so future user keybinding configuration can refer to commands without depending on display titles.
- [ ] [Auto] Use namespaced command IDs for standard and app commands, such as `edit.copy`, `edit.cut`, `edit.paste`, `edit.selectAll`, `file.open`, and `editor.find`, instead of plain unscoped names.
- [ ] [Auto + Manual] Disabled commands should remain visible in the command palette, but be visually clear as disabled and should not invoke.
- [ ] [Auto] Commands should not take invocation arguments in v1. Treat a command as the non-visual representation of a toolbar/menu/keybinding action: the command handler decides what to do from current window/view state.
- [ ] [Auto + Manual] Dispatch command shortcuts through a focus-first bubbling model, similar to other input events: the focused view gets first chance to handle a matching command/shortcut; if it handles the command, propagation stops, and only unhandled commands bubble through containing view/window command scopes. For example, text input can handle `Ctrl+X`, while unrelated commands such as a future `Super+Q` can bubble to a higher scope.
- [ ] [Auto + Manual] Separate physical keybindings from semantic commands: framework/window-level keybinding resolution should translate shortcuts into command IDs, and views should handle commands such as `copy`, `cut`, or `selectAll` without needing to know whether the shortcut was `Ctrl+C`, `Ctrl+Shift+C`, or a user-configured binding.
- [ ] [Auto + Manual] Deliberate and define the first standard command list before implementation. Treat common editing actions as likely standard commands, including copy, cut, paste, select all, undo, redo, find, replace, delete, delete word, select word, move by word, and related text navigation/editing commands.
- [ ] [Auto + Manual] Standard commands should allow different surfaces to bind the same semantic command to different physical shortcuts when necessary. For example, normal text inputs can map `Ctrl+C` to `copy`, while Terminal can map `Ctrl+Shift+C` to `copy` and keep `Ctrl+C` available for terminal interrupt behavior.
- [ ] [Auto + Manual] Text input and editor views should expose command handlers for semantic editing commands instead of embedding all keybinding policy locally, so global/user keybinding changes can happen without rewriting each view.
- [ ] [Auto + Manual] Use a responder-chain style command handling model for v1 unless implementation reveals a better local pattern: focused view handlers get first chance to handle semantic commands; handled commands are consumed and stop propagation, while unhandled commands continue to containing views/window-level handlers. Keep room for document/window state to register palette-visible commands and metadata.
- [ ] [Auto] If multiple commands register the same shortcut in the same effective scope, the last registered command wins for dispatch.
- [ ] [Auto + Manual] Add conflict diagnostics for duplicate shortcuts where practical: use compile-time/static checks for static command definitions and first-run/runtime diagnostics for dynamic registrations.
- [ ] [Auto] Model a command as a centrally registered action with at least a stable ID, title, optional description, optional icon, optional shortcut, optional group/category, enabled/disabled state, visibility state, and an invocation handler.
- [ ] [Auto + Manual] Keep the first implementation window-scoped: each active window can register and unregister its commands as its focused view/document state changes.
- [ ] [Auto + Manual] Build one shared command registry path that can be used by shortcuts, toolbar buttons, context-specific action surfaces, and a searchable command palette instead of each UI surface owning separate action logic.
- [ ] [Auto + Manual] Add a searchable command palette for the focused window, inspired by VS Code's Command Palette: commands should be searchable by title/category/description, grouped for discoverability, and invokable without requiring toolbar placement.
- [ ] [Auto] Use clear command names and group/category labels so users can discover related actions without memorizing shortcuts.
- [ ] [Auto] Shortcuts should dispatch through the same command registry used by the palette and toolbar actions; do not keep independent shortcut-only action paths for commands that are registered.
- [ ] [Auto + Manual] Toolbar buttons should be able to bind to command IDs so enabled state, tooltip/title, icon, shortcut display, and action execution come from the command metadata.
- [ ] [Auto + Manual] The registry should make rich app functionality discoverable without requiring traditional menus or permanently visible toolbar buttons.
- [ ] [Auto] Design the API so it can later grow from per-window commands into app-level, Shell-level, compositor-level, and individual-view command scopes without rewriting the initial command metadata model.
- [ ] [Auto + Manual] Add focused tests for command registration/unregistration, command lookup, command invocation, enabled/visibility filtering, shortcut dispatch, and toolbar/palette binding behavior.
- [ ] [Manual] Verify manually with a representative app that commands can be invoked from the palette, toolbar buttons, and shortcuts while staying synchronized as app state changes.

## TODO-016: useAutoFocus cannot focus targets inside nested child components

- [ ] [Auto] `useAutoFocus` requests focus with `ComponentKey::fromScope(hookScope)` and `Runtime::requestFocus` matches targets via `stableTargetKey_.hasPrefix(key)`, but stable target keys only carry the nearest body scope id: `HookInteractionSignalScope` builds its key fresh from `fromScope(owner)` instead of extending the parent scope key on the stack.
- [ ] [Auto] As a result, focusable targets mounted inside nested child components (their own `body()` scopes) can never match the hook's scope key. Only focusables mounted directly in the same body scope work (the current `lambda-editor` usage).
- [ ] [Auto] The test "auto focus requests first focusable target inside hook scope" in `tests/RuntimeInputTests.cpp` documents the expected nested behavior and is currently marked `doctest::may_fail`; remove the decorator when fixing.
- [ ] [Auto] A fix likely needs hierarchical scope keys (extend the parent key in `HookInteractionSignalScope`) plus stable accumulation across `For`/`Show` remounts (capture the parent key at view creation and re-push it during reconcile), since `stableTargetKey_` equality is also used for focus restore, hover tracking, command registry prefix walks, and overlay anchors. Scope this carefully before implementing.
- [ ] [Auto] This was masked until the FileDialogTests segfault (fixed) aborted the suite before `RuntimeInputTests.cpp` ran.

## TODO-017: Overlay rebuild test trips stack-use-after-scope under ASan

- [ ] [Auto] `tests/RuntimeInputTests.cpp` "overlay rebuild relayouts mounted content without remounting state" aborts with AddressSanitizer stack-use-after-scope in `StatefulOverlayProbe::body()`'s `onCleanup` lambda (`++*cleanups` reading a dead stack int) when a scope is disposed after the probe's stack captures are gone.
- [ ] [Auto] Reproduce with `-DCMAKE_BUILD_TYPE=Debug -DLAMBDA_ENABLE_ASAN=ON` and `--test-case="overlay rebuild relayouts mounted content without remounting state"`. The test passes in Release because the read goes unnoticed.
- [ ] [Auto] Determine whether the late cleanup is a framework scope-disposal ordering bug (cleanup running after unmount should not outlive the owning mount) or a test lifetime bug, and fix accordingly.

## TODO-019: Work through the frame-pacing improvement plan

- [x] [Auto] Implement the prioritized workstreams in [docs/frame-pacing-improvement-plan.md](docs/frame-pacing-improvement-plan.md) (FP-1 through FP-16), produced by the 2026-06 compositor and graphics-stack frame-pacing review.
- [x] [Auto] Verify the Linux code paths with clean normal/KMS builds, focused compositor/Vulkan/reactive tests, and a KMS compositor run with shell, terminal workload, editor, CPU tracing, KMS timing traces, and `vulkan-present-detail` logs.
- [x] [Auto] Fix the compile warnings found by clean normal/KMS rebuilds.
- [x] [Auto] Add and run the repeatable Linux verifier script, including the repo-local `wp_presentation` timestamp client for both atomic-KMS and Vulkan-display presenters.
- [x] [Auto] Add and run the static decorated-surface cache verifier; latest run showed 500 surface draw-cache hits, 1 miss, zero transient-chrome blocks, and surface avg/max 0.012/0.013 ms.
- [ ] [Manual] Complete the remaining hardware/visual checks from the plan: validation layers, manual cursor/hardware input-driver checks, resize/drag visual checks, and representative app smoke checks requiring interactive input.
- [ ] [Auto + Manual] Complete macOS compile/runtime verification for the Metal portions of FP-14/FP-16, including `debug::perf`, full `ctest`, and backdrop blur visual comparison.
- [ ] [Auto] When the remaining manual/macOS verification is done, delete the plan document and this TODO item.
