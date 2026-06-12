#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${LWM_BUILD_DIR:-$ROOT/build}"
LOG_ROOT="${LWM_REAL_APP_LOG_DIR:-$ROOT/.debug-logs/real-app-smoke}"
RUN_ID="$(date +%Y%m%d-%H%M%S)"
LOG_DIR="$LOG_ROOT/$RUN_ID"
SECONDS_PER_APP="${LWM_REAL_APP_SECONDS:-45}"
BUILD=1
INCLUDE_SHELL=0
PROBE_ONLY=0
SELECTED_CASE=""

usage() {
  cat <<EOF
Usage: scripts/run-real-app-smoke.sh [options]

Launches the Lambda apps and available mature Wayland clients against an
already-running lambda-window-manager session. It records one log per app and
prints the manual checks to perform while each app is open.

Options:
  --no-build       Do not build Lambda app targets first.
  --include-shell  Also launch lambda-shell. Normally the shell is already running.
  --case NAME      Run one case: lambda-settings, lambda-files, lambda-editor,
                   lambda-terminal, shell, terminal, browser, gtk, qt, mpv.
  --seconds N      Seconds to leave each app open. Default: $SECONDS_PER_APP.
  --probe-only     Print detected apps and do not launch anything.
  --list           List candidate cases and exit.
  -h, --help       Show this help.

Environment:
  LWM_BUILD_DIR          Build directory. Default: $BUILD_DIR
  LWM_REAL_APP_LOG_DIR   Smoke log root. Default: $LOG_ROOT
  LWM_REAL_APP_SECONDS   Seconds per app. Default: $SECONDS_PER_APP
EOF
}

case_catalog() {
  cat <<'EOF'
lambda-settings|required|Lambda Settings app with system titlebar glass.
lambda-files|required|Lambda Files app with integrated titlebar glass.
lambda-editor|required|Lambda Editor app with text rendering, scrolling, and toolbar chrome.
lambda-terminal|required|Lambda Terminal app with black glass terminal background.
shell|required|Lambda Shell dock, launcher, status docklets, and focus integration.
terminal|required|Mature Wayland terminal, preferably foot.
browser|required|Pure Wayland browser, preferably Firefox with MOZ_ENABLE_WAYLAND=1.
gtk|required|GTK app with text input and menus/popovers.
qt|optional|Qt Wayland app with menus/popovers, if installed.
mpv|required|mpv Wayland video/player surface.
EOF
}

print_catalog() {
  case_catalog | while IFS='|' read -r name required description; do
    printf "%-16s %-8s %s\n" "$name" "$required" "$description"
  done
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-build)
      BUILD=0
      shift
      ;;
    --include-shell)
      INCLUDE_SHELL=1
      shift
      ;;
    --case)
      SELECTED_CASE="${2:-}"
      if [[ -z "$SELECTED_CASE" ]]; then
        echo "--case requires a name" >&2
        exit 2
      fi
      shift 2
      ;;
    --seconds)
      SECONDS_PER_APP="${2:-}"
      if ! [[ "$SECONDS_PER_APP" =~ ^[0-9]+$ ]] || [[ "$SECONDS_PER_APP" -le 0 ]]; then
        echo "--seconds requires a positive integer" >&2
        exit 2
      fi
      shift 2
      ;;
    --probe-only)
      PROBE_ONLY=1
      BUILD=0
      shift
      ;;
    --list)
      print_catalog
      exit 0
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

if [[ -z "${WAYLAND_DISPLAY:-}" ]]; then
  display_file="${XDG_RUNTIME_DIR:-}/lambda-window-manager-display"
  if [[ -r "$display_file" ]]; then
    export WAYLAND_DISPLAY="$(cat "$display_file")"
  fi
fi

if [[ "$PROBE_ONLY" -ne 1 && -z "${WAYLAND_DISPLAY:-}" ]]; then
  echo "WAYLAND_DISPLAY is not set and lambda-window-manager-display was not found." >&2
  echo "Start lambda-window-manager first, then rerun this script from another TTY or SSH shell." >&2
  exit 1
fi

case_selected() {
  [[ -z "$SELECTED_CASE" || "$SELECTED_CASE" == "$1" ]]
}

quote_path() {
  printf "%q" "$1"
}

available_command() {
  command -v "$1" >/dev/null 2>&1
}

add_case() {
  rows+=("$1|$2|$3|$4|$5|")
}

add_missing() {
  rows+=("$1|$2|missing||$3|$4")
}

lambda_exe() {
  local name="$1"
  local candidates=(
    "$BUILD_DIR/apps/$name/$name"
    "$BUILD_DIR/$name"
    "$BUILD_DIR/demos/$name"
  )
  local path
  for path in "${candidates[@]}"; do
    if [[ -x "$path" ]]; then
      printf "%s" "$path"
      return
    fi
  done
  printf "%s/apps/%s/%s" "$BUILD_DIR" "$name" "$name"
}

lambda_cmd() {
  quote_path "$(lambda_exe "$1")"
}

rows=()

if case_selected "lambda-settings"; then
  if [[ -x "$(lambda_exe lambda-settings)" || "$BUILD" -eq 1 ]]; then
    add_case "lambda-settings" "required" "lambda-settings" "$(lambda_cmd lambda-settings)" \
      "Settings should open, accept input, move/resize/snap/maximize/restore/minimize, and close cleanly."
  else
    add_missing "lambda-settings" "required" "$(lambda_exe lambda-settings)" \
      "Build the lambda-settings target or rerun without --no-build."
  fi
fi

if case_selected "lambda-files"; then
  if [[ -x "$(lambda_exe lambda-files)" || "$BUILD" -eq 1 ]]; then
    add_case "lambda-files" "required" "lambda-files" "$(lambda_cmd lambda-files)" \
      "Files should browse directories, use the integrated titlebar, handle menus, and survive move/resize/snap."
  else
    add_missing "lambda-files" "required" "$(lambda_exe lambda-files)" \
      "Build the lambda-files target or rerun without --no-build."
  fi
fi

if case_selected "lambda-editor"; then
  if [[ -x "$(lambda_exe lambda-editor)" || "$BUILD" -eq 1 ]]; then
    add_case "lambda-editor" "required" "lambda-editor" "$(lambda_cmd lambda-editor) $(quote_path "$ROOT/TODO.md")" \
      "Editor should render all visible text while scrolling, keep toolbar/chrome responsive, and resize without stretched or stale content."
  else
    add_missing "lambda-editor" "required" "$(lambda_exe lambda-editor)" \
      "Build the lambda-editor target or rerun without --no-build."
  fi
fi

if case_selected "lambda-terminal"; then
  if [[ -x "$(lambda_exe lambda-terminal)" || "$BUILD" -eq 1 ]]; then
    add_case "lambda-terminal" "required" "lambda-terminal" "$(lambda_cmd lambda-terminal)" \
      "Terminal should accept typing, Ctrl+C should affect the shell process, and resize should feel smooth without stretched or stale content."
  else
    add_missing "lambda-terminal" "required" "$(lambda_exe lambda-terminal)" \
      "Build the lambda-terminal target or rerun without --no-build."
  fi
fi

if case_selected "shell"; then
  if [[ "$INCLUDE_SHELL" -eq 1 || "$SELECTED_CASE" == "shell" ]]; then
    if [[ -x "$(lambda_exe lambda-shell)" || "$BUILD" -eq 1 ]]; then
      add_case "shell" "required" "lambda-shell" "$(lambda_cmd lambda-shell)" \
        "Shell should show dock/status docklets, launch/focus/restore apps, and remain stable while other apps are tested."
    else
      add_missing "shell" "required" "$(lambda_exe lambda-shell)" \
        "Build lambda-shell or rerun without --no-build."
    fi
  else
    add_case "shell" "required" "already-running" ":" \
      "Validate the already-running shell manually: dock, launcher, status docklets, focus/restore, and no idle redraw loop."
  fi
fi

if case_selected "terminal"; then
  if available_command foot; then
    add_case "terminal" "required" "foot" "foot" \
      "foot should open, accept text, reflow on resize, and keep Ctrl+C inside the terminal."
  elif available_command qterminal; then
    add_case "terminal" "required" "qterminal" "QT_QPA_PLATFORM=wayland qterminal" \
      "qterminal should open through Wayland, accept text, and reflow on resize."
  else
    add_missing "terminal" "required" "foot or qterminal" \
      "Install foot or another mature Wayland terminal, or document the intentional exclusion."
  fi
fi

if case_selected "browser"; then
  if available_command firefox; then
    add_case "browser" "required" "firefox" "MOZ_ENABLE_WAYLAND=1 firefox" \
      "Browser should open natively on Wayland, type in the address bar, scroll, show menus/popovers, and copy/paste."
  elif available_command chromium; then
    add_case "browser" "required" "chromium" "chromium --ozone-platform=wayland" \
      "Chromium should open on Wayland, type, scroll, show menus/popovers, and copy/paste."
  elif available_command google-chrome-stable; then
    add_case "browser" "required" "google-chrome-stable" "google-chrome-stable --ozone-platform=wayland" \
      "Chrome should open on Wayland, type, scroll, show menus/popovers, and copy/paste."
  else
    add_missing "browser" "required" "firefox/chromium/google-chrome-stable" \
      "Install a pure Wayland browser, or document the intentional exclusion."
  fi
fi

if case_selected "gtk"; then
  if available_command gnome-text-editor; then
    add_case "gtk" "required" "gnome-text-editor" "gnome-text-editor" \
      "GTK editor should open on Wayland; validate text input, menus/popovers, clipboard, and resize/snap."
  elif available_command gedit; then
    add_case "gtk" "required" "gedit" "gedit" \
      "gedit should open on Wayland; validate text input, menus/popovers, clipboard, and resize/snap."
  elif available_command nautilus; then
    add_case "gtk" "required" "nautilus" "nautilus" \
      "Nautilus should open on Wayland; validate headerbar, menus/popovers, clipboard, and resize/snap."
  else
    add_missing "gtk" "required" "gnome-text-editor/gedit/nautilus" \
      "Install a GTK Wayland app, or document the intentional exclusion."
  fi
fi

if case_selected "qt"; then
  if available_command kate; then
    add_case "qt" "optional" "kate" "QT_QPA_PLATFORM=wayland kate" \
      "Kate should open through Qt Wayland; validate menus, text input, clipboard, and resize/snap."
  elif available_command dolphin; then
    add_case "qt" "optional" "dolphin" "QT_QPA_PLATFORM=wayland dolphin" \
      "Dolphin should open through Qt Wayland; validate menus, clipboard, and resize/snap."
  elif available_command qterminal; then
    add_case "qt" "optional" "qterminal" "QT_QPA_PLATFORM=wayland qterminal" \
      "qterminal should open through Qt Wayland; validate text input, clipboard, and resize/snap."
  else
    add_missing "qt" "optional" "kate/dolphin/qterminal" \
      "No Qt Wayland app found. This can be documented as unavailable on this machine."
  fi
fi

if case_selected "mpv"; then
  if available_command mpv; then
    add_case "mpv" "required" "mpv" "mpv --no-config --idle=yes --force-window=yes" \
      "mpv should open a Wayland window, keep video/player chrome stable, and close without compositor errors."
  else
    add_missing "mpv" "required" "mpv" \
      "Install mpv, or document the intentional exclusion for video/player validation."
  fi
fi

if [[ "${#rows[@]}" -eq 0 ]]; then
  echo "No cases selected. Use --list to see valid names." >&2
  exit 2
fi

if [[ "$BUILD" -eq 1 ]]; then
  build_targets=()
  for row in "${rows[@]}"; do
    IFS='|' read -r name required target command expected detail <<<"$row"
    case "$target" in
      lambda-settings|lambda-files|lambda-editor|lambda-terminal|lambda-shell)
        build_targets+=("$target")
        ;;
    esac
  done
  if [[ "${#build_targets[@]}" -gt 0 ]]; then
    cmake --build "$BUILD_DIR" --target "${build_targets[@]}" -j"$(nproc)"
  fi
fi

echo "Real-app smoke matrix:"
echo "  WAYLAND_DISPLAY: ${WAYLAND_DISPLAY:-not set}"
echo "  build dir:       $BUILD_DIR"
echo "  log dir:         $LOG_DIR"
echo "  seconds/app:     $SECONDS_PER_APP"
echo

missing_required=0
for row in "${rows[@]}"; do
  IFS='|' read -r name required target command expected detail <<<"$row"
  if [[ "$target" == "missing" ]]; then
    echo "MISSING $name ($required): $expected"
    echo "        $detail"
    [[ "$required" == "required" ]] && missing_required=1
    continue
  fi
  printf "%-16s %-10s %-24s %s\n" "$name" "$required" "$target" "$expected"
done

if [[ "$PROBE_ONLY" -eq 1 ]]; then
  exit "$missing_required"
fi

mkdir -p "$LOG_DIR"
failed=0
[[ "$missing_required" -ne 0 ]] && failed=1

for row in "${rows[@]}"; do
  IFS='|' read -r name required target command expected detail <<<"$row"
  if [[ "$target" == "missing" ]]; then
    continue
  fi
  if [[ "$target" == "already-running" ]]; then
    echo
    echo "== $name =="
    echo "$expected"
    continue
  fi

  log="$LOG_DIR/$name.log"
  echo
  echo "== $name: $target =="
  echo "$expected"
  echo "Common checks: focus, move, resize edges/corner, snap, maximize, restore, minimize/restore, text input, close."
  echo "Visual checks: cursor appearance, cursor motion during load, resize feel, no stretched/stale content, no empty text after scroll."
  echo "Command: $command"

  set +e
  timeout --signal=TERM --kill-after=2s "${SECONDS_PER_APP}s" bash -lc "exec $command" >"$log" 2>&1
  status=$?
  set -e

  if [[ "$status" -ne 0 && "$status" -ne 124 && "$status" -ne 143 ]]; then
    echo "FAIL $name: exited with status $status; see $log"
    failed=1
  else
    echo "LOG  $name: $log"
  fi
done

echo
echo "Cross-app clipboard check:"
echo "  Copy text from one mature client and paste into another, then reverse direction."
echo "  If the source exits before paste, paste should either continue or fail cleanly without freezing the WM."
echo
echo "Frame-pacing visual checks:"
echo "  Cursor should keep the expected theme/shape and remain responsive while Terminal, Editor, or browser content is busy."
echo "  Live resize should look continuous: exposed areas fill immediately and existing content stays aligned instead of stretching."
echo "  Scroll representative Lambda apps, especially Editor, and confirm text keeps rendering beyond the initial window height."
echo
echo "Logs written to: $LOG_DIR"
if [[ "$failed" -ne 0 ]]; then
  echo "One or more required apps were missing or failed to launch. Document intentional exclusions or fix protocol/runtime failures."
  exit 1
fi

echo "Launch checks completed. Confirm the visual behavior before marking the real-app matrix done."
