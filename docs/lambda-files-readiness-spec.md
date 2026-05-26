# Lambda Files readiness spec

**Date:** 2026-05-25
**Status:** Draft
**Milestone order:** Window Manager first, then Shell, Settings, Files, Terminal, and the remaining desktop pieces.
**Scope:** `lambda-files`, shared desktop libraries needed by it, and the file/app-opening services needed for safe daily file management.

## Summary

This milestone turns `lambda-files` from a polished file browser into a safe daily file manager. The target is not a full Finder/Nautilus replacement with every advanced feature. The target is that the user can browse normal folders, create and rename items, copy and move files, use the clipboard, send files to trash, recover from common errors, and open files through the desktop app registry without risking accidental data loss.

Files is a user data app, so the readiness bar is higher than for visual shell polish. Permanent deletion should not be the first delete implementation. The first delete path must be trash-first, undoable where practical, and clear about failures. Operations should report progress and errors rather than blocking the UI or silently failing.

Session startup/shutdown automation remains out of scope. New log collection and diagnostic infrastructure are also out of scope.

## Current baseline

Implemented today:

- `lambda-files` is a native Flux app under `examples/lambda-files`.
- It opens a resizable window with an integrated titlebar and glass background.
- It has a sidebar with Home, Desktop, Documents, and Downloads when present.
- It lists the current directory with folders first and case-insensitive sorting.
- It has a responsive grid view.
- It has visual file kinds for folders, generic files, PDFs, images, presentations, and Sketch files.
- It supports selection of one item.
- It supports breadcrumbs, back, forward, and up navigation.
- It supports a hidden-file toggle.
- It resolves file-open commands through MIME/default-app data and the shared app registry.
- It has basic keyboard shortcuts for back, forward, up, and activate selected.

Important limitations:

- No create folder or create file.
- No rename.
- No trash.
- No permanent delete confirmation path.
- No copy, cut, paste, duplicate, or move.
- No undo.
- No multi-select or range selection.
- No keyboard selection model beyond activating an already selected item.
- No context menus for files, background, sidebar, or breadcrumbs.
- No drag and drop.
- No clipboard integration.
- No list/detail view.
- No sort controls.
- No search.
- No path entry.
- No filesystem watcher.
- No operation progress UI.
- No robust error model.
- No mounted volumes/removable devices.
- No icon theme integration for file and MIME icons.
- No thumbnail cache.
- Open-with/default-app UI is not fully wired yet, but the model/service path no longer depends on direct `xdg-open` file launching.

## Additional Files work identified

These areas should be included in the Files milestone:

- Split the current app into a testable filesystem model, operation layer, and UI layer.
- Add safe file operations: create folder, create file, rename, duplicate, copy, cut, paste, move, and trash.
- Implement freedesktop trash behavior on Linux instead of permanent delete as the default.
- Add undo for recent safe operations where practical.
- Add multi-selection, range selection, keyboard navigation, and selection persistence across refreshes.
- Add context menus for selected entries, empty directory area, sidebar places, and breadcrumbs.
- Add clipboard support using file URI lists and internal cut/copy state.
- Add drag-and-drop support for reordering? No. For file transfer and opening? Yes, where the platform supports it.
- Add list/detail view, sort controls, search/filter, refresh, and direct path entry.
- Add filesystem watching so external changes appear without manual navigation.
- Add operation progress, cancellation where safe, and detailed error reporting.
- Replace `xdg-open` shell command usage with a shared open-with/default-app service based on the Shell app registry and MIME data.
- Use the icon theme provider from the Shell milestone for file, folder, MIME, and place icons.
- Add basic Files preferences through Settings later, but keep defaults usable now.
- Add deterministic tests for path handling, operations, trash, selection, and model behavior.

Status update 2026-05-26: `FilesStore` now has deterministic XDG user-directory parsing, home fallback coverage, path normalization and navigation-history coverage, breadcrumb generation for home/root/outside-home paths, validated directory navigation that keeps the previous directory on missing/non-folder paths, explicit stable sorting by name/kind/size/modified time, current-folder search/filtering, directory listing tests that cover hidden-file filtering plus modified-time capture, directory refresh diff helpers, a non-UI `FilesModel` for directory/visible-entry/error/refresh state, refresh selection preservation for paths that still exist, selection/range-selection helpers, collision-free folder/file creation, rename validation, copy/move/duplicate operations with symlink-preserving recursive copy behavior and staged final-path replacement, internal copy/cut clipboard operation state, URI-list clipboard parsing/serialization, trash metadata generation, trash collision handling, restore-from-trash collision handling, conflict decisions including cancel, deterministic operation progress/failure/cancel state, safe undo helpers for create/rename/move/trash/copy, MIME/default-app fixture parsing, open-with choice resolution and open-command planning, mimeapps-list loading/merging, icon-theme fallback lookup through the shared app registry helpers, preference parse/serialize helpers, atomic preference load/save with generated defaults, and persisted hidden-file preference wiring in the app. The default file-open path now uses the shared app registry plus MIME/default-app data instead of directly shelling out to `xdg-open`. Live UI wiring for undo/open-with/icons, real watcher integration, progress/cancel UX, and external clipboard/DnD interop remain open.

## Goals

1. Make `lambda-files` safe for everyday local file management.
2. Make trash-first delete the default destructive action.
3. Add the core operation set: create, rename, copy, cut, paste, move, duplicate, trash, and restore where practical.
4. Add a real selection model that supports keyboard, pointer, multi-select, and range-select.
5. Add context menus and keyboard shortcuts for common actions.
6. Add a real operation queue with progress, cancellation, errors, and undo.
7. Add a filesystem watcher so directory contents stay current.
8. Replace ad hoc open behavior with a desktop open-with/default-app service.
9. Use icon theme and MIME metadata instead of hard-coded visual kinds as the primary icon source.
10. Keep the UI calm, direct, and reliable under large folders and slow operations.

## Non-goals

- No session manager, login, lock, logout, suspend/reboot UI, or auto-start work.
- No new log collection, log viewer, trace viewer, or crash-log pipeline.
- No Window Manager, Shell, or Settings feature work except consuming shared app registry, icon theme, and config services already defined by prior specs.
- No full document viewer.
- No full text editor.
- No archive manager.
- No network file browser.
- No cloud sync.
- No git/version-control UI.
- No tagging or metadata database.
- No advanced search indexer.
- No bulk rename tool.
- No tabs or split panes in the first readiness target.
- No root/admin escalation UI.
- No full permissions editor beyond read-only display or simple error messages.
- No permanent delete as the primary delete path. Permanent delete may be a guarded advanced action later.

## Assumptions

- The Window Manager readiness milestone provides stable window behavior, clipboard/DnD protocol support, and real-app validation.
- The Shell readiness milestone provides a shared app registry and icon theme provider.
- The Settings readiness milestone will later expose Files preferences, but Files must have safe defaults before then.
- The first daily-driver target is local Linux files under the user's permissions.
- Pure Wayland remains the compatibility policy.
- macOS support can keep existing behavior where native APIs exist, but this milestone primarily targets the Lambda Linux desktop.

## Readiness definition

Files is ready for the Terminal milestone when all of these are true:

- It can browse Home, Desktop, Documents, Downloads, and arbitrary local directories.
- It can create folders and basic files.
- It can rename files and folders.
- It can copy, cut, paste, move, and duplicate files and folders.
- Delete sends items to trash by default.
- Recent safe operations can be undone where practical.
- Multi-select and range-select work with pointer and keyboard.
- Context menus expose common actions.
- Clipboard file copy/cut/paste works inside Files and with other Wayland clients that support file URI lists.
- Drag and drop works for basic file transfer within Files and to compatible clients where platform support exists.
- The current directory updates when files change externally.
- Long operations show progress and do not freeze the UI.
- Operation errors are visible, specific, and recoverable.
- Open/open-with uses desktop app metadata instead of blindly shelling out to `xdg-open`.
- File and place icons come from the configured icon theme with clear fallback behavior.
- Large directories remain usable.
- Tests cover the model and operation layer.

## Architecture decisions

### Separate model, operations, and UI

The current implementation mixes listing/navigation state in the view body. For daily file operations, move the stateful logic into testable layers:

```text
examples/lambda-files/
  FilesApp.hpp
  FilesModel.hpp
  FilesModel.cpp
  FileOperations.hpp
  FileOperations.cpp
  TrashService.hpp
  TrashService.cpp
  FileClipboard.hpp
  FileClipboard.cpp
  FileWatcher.hpp
  FileWatcher.cpp
  OpenWithService.hpp
  OpenWithService.cpp
```

The exact filenames can change. The important split:

- UI renders state and dispatches commands.
- Model owns current directory, entries, selection, view mode, sort, filters, history, and operation state.
- Operation layer performs filesystem changes.
- Watcher layer detects external changes.
- Open-with layer delegates to shared desktop app/MIME registry.

### All file operations go through commands

Files should not mutate the filesystem from random UI callbacks. Use commands with explicit result types:

```cpp
enum class FileCommandKind {
  CreateFolder,
  CreateFile,
  Rename,
  Duplicate,
  Copy,
  Cut,
  Paste,
  MoveTo,
  Trash,
  RestoreFromTrash,
  Open,
  OpenWith,
  Reveal,
};
```

Each command should return success, partial success, cancellation, or detailed failure. The UI should never have to infer whether an operation worked.

### Trash-first delete

The default delete action must move files to trash.

Linux target:

- Follow the freedesktop Trash specification for files on supported local filesystems.
- Use `$XDG_DATA_HOME/Trash` or `$HOME/.local/share/Trash` as the home trash.
- Create `files/` and `info/` directories as needed.
- Create unique destination names to avoid collisions.
- Write `.trashinfo` files with original path and deletion date.
- Handle cross-filesystem trash rules carefully.
- If trash is unavailable, show an error with options. Do not silently permanently delete.

Permanent delete can exist later behind an explicit confirmation, but it is not required for this readiness milestone.

### Open-with belongs to desktop services

The first implementation uses `xdg-open`, but the daily desktop should use the shared app registry and MIME/default-app logic from the Shell milestone.

Files should be able to request:

- open with default app
- open with selected app
- reveal/open containing folder
- set default app later, when Settings/default-app UI exists

If no handler exists, Files should show a useful error and optionally offer "Open With" choices from compatible apps.

## Workstreams

### FI-1: Files model and state ownership

Problem:

Listing, navigation, selection, and hidden-file state currently live inside the view body. That makes real operations and tests harder than they need to be.

Scope:

- Current directory.
- Navigation history.
- Breadcrumbs.
- Entries.
- Selected items.
- Active/focused item.
- Sort mode.
- View mode.
- Hidden-file preference.
- Search/filter query.
- Loading/error state.
- Operation state.
- Preferences.

Acceptance:

- Files has a `FilesModel` or equivalent non-UI owner for app state.
- The UI reads state from the model and dispatches commands.
- Directory listing can be tested without mounting a Flux view.
- Navigation can be tested without rendering.
- Selection can be tested without rendering.
- Refreshing a directory preserves selection where the same paths still exist.
- Errors are represented in the model, not only as local strings.

Implementation notes:

- Keep the first model explicit rather than over-generic.
- Avoid doing filesystem work directly during view construction.
- Use stable file identities where possible, but path identity is acceptable for the first target if documented.

### FI-2: Navigation, places, and path entry

Problem:

Navigation works, but the app needs arbitrary path entry, better sidebar places, and robust behavior around missing paths.

Scope:

- Home.
- Desktop.
- Documents.
- Downloads.
- Recent current directory.
- Arbitrary path entry.
- Breadcrumb navigation.
- Back/forward/up.
- Refresh.
- Missing directory behavior.
- Permission-denied behavior.
- Optional mounted volumes section.

Acceptance:

- Sidebar places are computed from XDG user directories where available.
- Missing standard folders are hidden or shown unavailable, not broken.
- Breadcrumbs handle home, root, and paths outside home correctly.
- Direct path entry can navigate to any readable local directory.
- Invalid path entry shows an error and keeps the previous directory.
- Permission-denied directories show a clear error.
- Refresh reloads the current directory.
- Back/forward history is deterministic after path entry, sidebar navigation, breadcrumb navigation, and up navigation.

Implementation notes:

- Use `xdg-user-dirs` config if available instead of only environment variables.
- Mounted volumes can be a later enhancement unless a simple `/run/media/$USER` and `/media/$USER` section is cheap and reliable.

### FI-3: Selection and keyboard behavior

Problem:

Single selection is not enough for real file management.

Scope:

- Single selection.
- Multi-selection.
- Range selection.
- Keyboard focus.
- Arrow navigation.
- Home/end.
- Page up/down.
- Select all.
- Clear selection.
- Rename shortcut.
- Delete/trash shortcut.
- Open shortcut.
- Context menu shortcut.

Acceptance:

- Click selects one item.
- Ctrl-click toggles an item.
- Shift-click selects a range.
- Arrow keys move the focused item.
- Shift-arrow extends selection.
- Ctrl+A selects all visible items.
- Escape clears transient UI and then selection when appropriate.
- Return opens selected item or enters selected folder.
- F2 begins rename.
- Delete sends selected items to trash.
- The focused item and selected items are visually distinct and not color-only.
- Selection remains stable across sort changes where paths still exist.

Implementation notes:

- Define behavior for directories and files consistently.
- Avoid making grid-only assumptions; list/detail view should use the same selection model.

### FI-4: Core file operations

Problem:

Files cannot yet modify the filesystem. Daily use requires safe creation, rename, copy, move, duplicate, paste, and trash.

Scope:

- Create folder.
- Create empty file.
- Rename.
- Duplicate.
- Copy.
- Cut.
- Paste.
- Move.
- Trash.
- Restore from trash where practical.
- Operation conflicts.
- Name collision resolution.
- Partial failure reporting.
- Undo for recent safe operations.

Acceptance:

- Create folder uses a conflict-free default name such as `Untitled Folder`.
- Create file uses a conflict-free default name such as `Untitled.txt` or `Untitled`.
- Rename validates empty names, path separators, existing names, and permission errors.
- Duplicate creates a conflict-free copy near the source.
- Copy/paste supports files and directories.
- Cut/paste moves files and directories.
- Move across filesystems falls back to copy-then-delete only after copy success.
- Trash handles files and directories.
- Operation errors identify the failed item and reason.
- Partial success reports what succeeded and what failed.
- Undo supports at least rename, move, trash, and create. Copy undo may remove copied items only when safe and unmodified.
- The UI refreshes affected directories after operations.

Implementation notes:

- Use temporary names for copy/move operations where needed to avoid exposing half-finished final files.
- Never recursively delete real user files as a fallback for trash failure.
- Do not follow symlinks recursively unless the operation semantics explicitly require it.
- Preserve metadata as much as standard APIs reasonably allow.

### FI-5: Trash service

Problem:

Trash is a separate concern from generic move/delete and needs specification-level care.

Scope:

- Home trash location.
- `.trashinfo` file writing.
- Collision-free trash names.
- Restore original path.
- Trash listing support.
- Empty trash deferred unless needed.
- Cross-filesystem behavior.
- Error handling.

Acceptance:

- Sending an item to trash creates a file under `Trash/files` and matching metadata under `Trash/info`.
- Original path and deletion date are recorded.
- Name collisions in trash are handled safely.
- Files can show a Trash place if trash exists or after first trash operation.
- Trash view can list trashed items with original path and deletion date, or Trash view is explicitly deferred if restore is not implemented yet.
- Restore returns an item to its original path when possible and handles conflicts safely.
- If a file cannot be trashed, Files shows a clear error and does not permanently delete it.

Implementation notes:

- Follow the freedesktop Trash specification for Linux.
- macOS can use native trash APIs later; do not block Linux readiness on macOS parity.

### FI-6: Clipboard and drag/drop

Problem:

File management depends on clipboard and drag/drop interoperability.

Scope:

- Internal copy/cut state.
- Wayland clipboard integration.
- `text/uri-list`.
- Paste into current directory.
- Drag files out of Files.
- Drop files onto a folder.
- Drop files into current directory.
- Drag from external clients into Files.
- Operation feedback.

Acceptance:

- Copy stores selected file URIs.
- Cut stores selected file URIs plus move intent.
- Paste copies or moves according to the current clipboard intent.
- Clipboard paste works after navigating to another folder.
- Clipboard state survives selection changes.
- Files can consume `text/uri-list` from compatible external clients.
- Files can provide `text/uri-list` to compatible external clients.
- Dragging selected items onto a folder moves them by default within the same filesystem and copies them when modifier policy says copy.
- Dropping external files into a folder imports/copies them where protocol support allows.
- Unsupported drops are rejected visibly.

Implementation notes:

- The first implementation can keep drag/drop simple and local if Wayland toolkit support needs more framework work.
- Clipboard operations must not require Shell involvement except for shared MIME/app metadata.

### FI-7: Context menus and commands

Problem:

Common file operations need discoverable actions.

Scope:

- File item context menu.
- Multi-selection context menu.
- Empty-area context menu.
- Sidebar place context menu.
- Breadcrumb context menu.
- Toolbar/menu actions.
- Disabled actions.
- Keyboard accelerators.

Acceptance:

- Right-click or equivalent opens a context menu for selected items.
- File menu includes Open, Open With, Rename, Duplicate, Copy, Cut, Move to Trash, Properties if available.
- Folder menu includes Open, Open in New Window later if supported, Rename, Copy, Cut, Move to Trash, Properties if available.
- Empty-area menu includes New Folder, New File, Paste, Sort, View, Show Hidden Files.
- Sidebar place menu includes Open, Reveal/Properties where applicable.
- Disabled actions explain why when possible.
- Keyboard shortcuts match menu actions.

Implementation notes:

- Keep menus direct. Do not add actions that have no backend.
- Permanent delete should be absent or explicitly advanced and guarded.

### FI-8: Views, sorting, search, and filtering

Problem:

The grid is usable for a prototype, but daily file management needs list/detail views and sorting.

Scope:

- Grid view.
- List/detail view.
- Sort by name.
- Sort by kind/type.
- Sort by size.
- Sort by modified time.
- Ascending/descending.
- Search/filter current folder.
- Empty states.
- Large directory behavior.

Acceptance:

- Users can switch between grid and list/detail view.
- List/detail view shows name, kind, size, and modified time.
- Sorting is stable and deterministic.
- Directories-first is configurable or at least consistent.
- Search filters current folder without changing directory.
- Empty folders and no-search-results states are distinct.
- Large directories remain responsive enough for daily use.
- View mode and sort preferences persist.

Implementation notes:

- Full recursive search/indexing is deferred.
- Current-folder filtering is enough for this milestone.

### FI-9: Filesystem watching and refresh

Problem:

Directories can change outside Files. The UI needs to stay current.

Scope:

- Current directory watcher.
- Sidebar place availability refresh.
- Operation-triggered refresh.
- External create/delete/rename detection.
- Debouncing.
- Error recovery.

Acceptance:

- Creating, deleting, or renaming files outside Files updates the current view.
- Watch events are debounced to avoid excessive refresh.
- Watcher failure falls back to manual refresh with an unavailable state.
- Selection is preserved across refresh when possible.
- Refresh does not reset scroll unnecessarily unless the current listing changes substantially.

Implementation notes:

- Linux target can use inotify.
- macOS parity can use FSEvents later.
- Avoid watching entire directory trees in this milestone.

### FI-10: Open-with, MIME, icons, and thumbnails

Problem:

Opening files and representing them visually should use desktop metadata rather than local extension guesses.

Scope:

- MIME detection.
- Default app lookup.
- Open with default app.
- Open with selected app.
- App compatibility list.
- File icon lookup.
- Folder/place icon lookup.
- Thumbnail cache for common image files if cheap.
- Fallback icons.

Acceptance:

- Files uses the shared app registry/default-app service for open operations.
- If no default app exists, Files offers an Open With flow or clear error.
- File icons come from MIME/icon theme metadata.
- Folder and place icons come from icon theme metadata.
- Missing icons use consistent fallback icons.
- Image thumbnails are shown only when reliable and cached; otherwise icons are acceptable.
- Thumbnail generation does not block scrolling.

Implementation notes:

- Full PDF/video/document preview is deferred.
- MIME/default-app backend can start simple but should not be a shell command string hidden in Files.

### FI-11: Errors, progress, cancellation, and undo

Problem:

Filesystem operations fail frequently. The app needs user-visible and recoverable operation state.

Scope:

- Operation queue.
- Progress UI.
- Cancellation.
- Error banners/dialogs.
- Partial success reporting.
- Undo stack.
- Conflict prompts.
- Retry/skip/replace decisions.

Acceptance:

- Long operations show progress.
- Copy/move operations can be cancelled before completion when safe.
- Errors are specific and tied to affected paths.
- Conflict prompts offer safe choices: keep both, replace, skip, cancel.
- Undo is available immediately after supported operations.
- Undo failure is reported clearly.
- The app never silently drops selected operations.

Implementation notes:

- Start with one active operation at a time if that keeps behavior simple.
- Use a deterministic operation result model for tests.

### FI-12: Preferences and Settings integration

Problem:

Files needs a few preferences, but Settings owns the GUI control surface later.

Scope:

- Show hidden files.
- Default view mode.
- Sort mode.
- Directories first.
- Icon/thumbnail size.
- Sidebar places.
- Confirm before permanent delete, if permanent delete exists later.

Acceptance:

- Files has a config file or uses the shared Settings backend for app preferences.
- Preferences persist across restarts.
- Settings can later edit these values without reverse engineering Files.
- The hidden-file toggle persists or clearly behaves as session-only if intentionally chosen.

Suggested config:

```toml
[view]
default_view = "grid"
sort_by = "name"
sort_direction = "ascending"
directories_first = true
show_hidden = false
icon_size = 96

[sidebar]
show_home = true
show_desktop = true
show_documents = true
show_downloads = true
show_trash = true
show_volumes = true
```

Implementation notes:

- Keep preferences small. Do not add a full settings system inside Files.

### FI-13: Accessibility and keyboard quality

Problem:

Files must be usable with keyboard and expose enough structure for later accessibility work.

Scope:

- Keyboard navigation.
- Focus rings.
- Accessible labels.
- Selection state.
- Menu accessibility.
- Error/progress announcements where framework support exists.
- Reduced motion.

Acceptance:

- All major operations can be performed by keyboard.
- Focus is visible in sidebar, toolbar, breadcrumbs, grid, list, and menus.
- File items expose name, kind, size, and selection state through the framework's accessibility model where available.
- Error and progress UI is readable and not color-only.
- Reduced motion setting is respected for hover/open animations.

Implementation notes:

- Full screen-reader integration may depend on broader Flux accessibility work, but structure should not block it.

### FI-14: Tests and validation

Problem:

File managers are high-risk because bugs can destroy user data.

Scope:

- Path normalization tests.
- Directory listing tests.
- Sorting tests.
- Selection tests.
- Operation tests with temporary directories.
- Trash tests with temporary XDG directories.
- Clipboard model tests.
- Conflict resolution tests.
- Undo tests.
- Watcher tests where deterministic.
- Open-with service tests with fixture app registry/MIME data.

Acceptance:

- Tests use temporary directories and never touch real user files.
- Tests cover create, rename, duplicate, copy, move, trash, restore, undo, and conflict cases.
- Tests cover symlinks and permissions where practical.
- Tests cover invalid filenames and existing-name collisions.
- Tests cover selection range behavior.
- Tests cover sorting and filtering.
- Manual validation covers real home-folder workflows.

Implementation notes:

- Keep destructive tests isolated with temp roots and explicit guards.
- Do not depend on host desktop files for deterministic tests; use fixtures.

## Implementation order

1. Extract model and operation layer.

   Move navigation, listing, selection, and command state out of the view body.

2. Add selection and keyboard model.

   Implement multi-select, range-select, focus, and keyboard navigation before large operation UI.

3. Add safe core operations.

   Implement create folder/file, rename, duplicate, copy, move, cut/copy/paste, and trash.

4. Add operation UI.

   Add progress, conflict prompts, errors, cancellation, and undo.

5. Add context menus and shortcuts.

   Wire common commands into item/background/sidebar menus and keyboard accelerators.

6. Add watcher and refresh behavior.

   Keep directory contents current and preserve state across refreshes.

7. Add views, sorting, and search.

   Add list/detail view, sorting, and current-folder filtering.

8. Integrate desktop services.

   Use shared icon theme, MIME, app registry, and open-with/default-app services.

9. Add preferences and Settings handoff.

   Persist Files preferences and document what Settings will edit.

10. Add tests and update docs.

   Cover operation safety, trash, selection, model behavior, and manual validation.

## Manual validation checklist

### Build and unit checks

```sh
cmake --build build --target flux_tests
./build/flux_tests --test-case="*files*"
cmake --build build
git diff --check
```

### Launch checks

```sh
./build/lambda-window-manager
./build/lambda-shell
./examples/lambda-files
```

Expected:

- Files opens with integrated titlebar and glass background.
- Sidebar places appear for existing standard folders.
- Navigation works through sidebar, breadcrumbs, back, forward, up, and path entry.
- Closing and reopening preserves relevant preferences.

### Browsing checks

Validate:

- Home.
- Desktop.
- Documents.
- Downloads.
- Root path.
- Folder outside home.
- Permission-denied folder.
- Empty folder.
- Folder with hidden files.
- Folder with many entries.
- Folder modified externally.

Expected:

- Listings are correct.
- Errors are specific.
- Hidden-file behavior is consistent.
- Large folders remain usable.
- External changes refresh.

### Selection checks

Validate:

- click select
- ctrl-click toggle
- shift-click range
- arrow navigation
- shift-arrow extend
- select all
- clear selection
- selection preserved after refresh
- selection preserved after sort

Expected:

- Focus and selection are visually clear.
- Actions apply to the intended selected items.

### Operation checks

Use a temporary directory with test files.

Validate:

- create folder
- create file
- rename file
- rename folder
- duplicate file
- copy file
- copy folder
- cut/paste file
- move folder
- name collision keep both
- name collision replace
- operation cancel
- permission failure
- undo create
- undo rename
- undo move
- undo trash

Expected:

- No operation corrupts unrelated files.
- Partial failures are reported.
- Undo behaves only where safe.

### Trash checks

Validate:

- trash file
- trash folder
- trash multiple items
- trash collision
- restore item
- restore collision
- trash unavailable path

Expected:

- Files are moved to freedesktop trash.
- `.trashinfo` metadata is correct.
- Permanent deletion is not used as silent fallback.

### Clipboard and DnD checks

Validate:

- copy/paste within same folder
- copy/paste to another folder
- cut/paste to another folder
- copy from external app if supported
- paste into external app if supported
- drag file onto folder
- drag external file into current folder if supported

Expected:

- Operations match clipboard/drop intent.
- Unsupported paths are rejected visibly.

### Open-with checks

Validate:

- open text file
- open image
- open PDF
- file with no default handler
- open with explicit app
- reveal containing folder

Expected:

- Default app comes from desktop app/MIME registry.
- Missing handler shows useful error.
- Files does not depend on hidden shell command behavior.

### View checks

Validate:

- grid view
- list/detail view
- sort by name
- sort by kind
- sort by size
- sort by modified time
- ascending/descending
- search current folder
- no search results

Expected:

- Sorting and search are deterministic.
- Text and icons do not overlap.
- Preferences persist.

## Test additions

Add focused automated tests where behavior is deterministic:

- `homeDirectory()` fallback behavior.
- XDG user directory parsing.
- Path normalization.
- Breadcrumb generation for home, root, and paths outside home.
- Directory listing with hidden files on/off.
- Sorting by name, kind, size, and modified time.
- Grid/list selection behavior.
- Range selection behavior.
- Create folder/file with collision-free names.
- Rename validation.
- Copy/move/duplicate operations with temp directories.
- Cross-filesystem move fallback if testable.
- Trash file/folder metadata and collision handling using temp XDG paths.
- Restore from trash with conflict handling.
- Undo stack behavior.
- Clipboard URI-list parsing and serialization.
- Conflict resolution decisions.
- Open-with lookup using fixture app registry and MIME data.
- File icon lookup fallback using fixture icon theme data.

## Done checklist

- [ ] Files has a testable model outside the view body.
- [ ] Directory browsing, history, breadcrumbs, and path entry are robust.
- [ ] Multi-select and keyboard selection work.
- [ ] Create folder/file works.
- [ ] Rename works.
- [ ] Copy/cut/paste/move works.
- [ ] Duplicate works.
- [ ] Trash-first delete works.
- [ ] Restore from trash works or Trash restore is explicitly deferred with delete still trash-first.
- [ ] Undo works for supported safe operations.
- [ ] Context menus expose real commands.
- [ ] Clipboard file operations work internally and with compatible clients.
- [ ] Basic drag/drop works or unsupported portions are clearly documented.
- [ ] Current folder refreshes after external changes.
- [ ] Operation progress/errors/cancel states exist.
- [ ] Grid and list/detail views exist.
- [ ] Sorting and current-folder search work.
- [ ] Open-with uses shared app/MIME registry.
- [ ] Icons use shared icon theme provider with fallback.
- [x] Files preferences persist.
- [ ] Tests cover model, operations, trash, selection, and open-with fixtures.
- [ ] User guide and app docs match actual behavior.

## Deferred to later milestones

- Full document viewer.
- Full text editor.
- Archive manager and archive browsing.
- Network shares and remote filesystems.
- Cloud sync.
- Tags, comments, ratings, and metadata database.
- Full recursive indexed search.
- Bulk rename.
- Tabs and split panes.
- Advanced file previews.
- Full PDF/video thumbnailing.
- Full permissions editor and root/admin escalation.
- Permanent delete as a normal action.
- Version-control integration.
- Full desktop portal/file chooser implementation.
