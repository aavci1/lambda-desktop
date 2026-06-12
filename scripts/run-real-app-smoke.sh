#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${LWM_BUILD_DIR:-$ROOT/build}"
LOG_ROOT="${LWM_REAL_APP_LOG_DIR:-$ROOT/.debug-logs/real-app-smoke}"
RUN_ID="$(date +%Y%m%d-%H%M%S)"
LOG_DIR="$LOG_ROOT/$RUN_ID"
SECONDS_PER_APP="${LWM_REAL_APP_SECONDS:-45}"
COMPOSITOR_TIMEOUT_SECONDS="${LWM_REAL_APP_COMPOSITOR_TIMEOUT_SECONDS:-300}"
BUILD=1
INCLUDE_SHELL=0
PROBE_ONLY=0
START_COMPOSITOR=0
SELECTED_CASE=""
COMPOSITOR_ARGS=()

usage() {
  cat <<EOF
Usage: scripts/run-real-app-smoke.sh [options]

Launches the Lambda apps and available mature Wayland clients against an
already-running lambda-window-manager session, or starts a guarded compositor
session when --start-compositor is passed. It records one log per app and
prints the manual checks to perform while each app is open.

Options:
  --no-build       Do not build Lambda app targets first.
  --include-shell  Also launch lambda-shell. Normally the shell is already running.
  --start-compositor
                 Start lambda-window-manager with CPU/KMS/pacing traces, wait
                 for its Wayland display file, run the selected apps, then stop it.
  --compositor-timeout N
                 Seconds before the started compositor is terminated. Default: $COMPOSITOR_TIMEOUT_SECONDS.
  --compositor-arg ARG
                 Extra argument passed to lambda-window-manager. Repeatable.
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
  LWM_REAL_APP_COMPOSITOR_TIMEOUT_SECONDS
                         Timeout for --start-compositor. Default: $COMPOSITOR_TIMEOUT_SECONDS
  LAMBDA_WINDOW_MANAGER_BIN
                         Override compositor binary for --start-compositor.
EOF
}

required_wayland_globals() {
  cat <<'EOF'
wl_compositor
wl_subcompositor
wl_shm
wl_output
wl_seat
wl_data_device_manager
xdg_wm_base
zxdg_decoration_manager_v1
zxdg_output_manager_v1
wp_viewporter
wp_fractional_scale_manager_v1
wp_cursor_shape_manager_v1
zwp_idle_inhibit_manager_v1
zwlr_layer_shell_v1
wp_presentation
zwp_relative_pointer_manager_v1
zwp_pointer_constraints_v1
zwp_primary_selection_device_manager_v1
xdg_activation_v1
xx_cutouts_manager_v1
xx_background_effect_manager_v1
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
    --start-compositor)
      START_COMPOSITOR=1
      shift
      ;;
    --compositor-timeout)
      COMPOSITOR_TIMEOUT_SECONDS="${2:-}"
      if ! [[ "$COMPOSITOR_TIMEOUT_SECONDS" =~ ^[0-9]+$ ]] || [[ "$COMPOSITOR_TIMEOUT_SECONDS" -le 0 ]]; then
        echo "--compositor-timeout requires a positive integer" >&2
        exit 2
      fi
      shift 2
      ;;
    --compositor-arg)
      if [[ -z "${2:-}" ]]; then
        echo "--compositor-arg requires a value" >&2
        exit 2
      fi
      COMPOSITOR_ARGS+=("$2")
      shift 2
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

if [[ "$START_COMPOSITOR" -ne 1 && -z "${WAYLAND_DISPLAY:-}" ]]; then
  display_file="${XDG_RUNTIME_DIR:-}/lambda-window-manager-display"
  if [[ -r "$display_file" ]]; then
    export WAYLAND_DISPLAY="$(cat "$display_file")"
  fi
fi

if [[ "$PROBE_ONLY" -ne 1 && "$START_COMPOSITOR" -ne 1 && -z "${WAYLAND_DISPLAY:-}" ]]; then
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

compositor_exe() {
  local override="${LAMBDA_WINDOW_MANAGER_BIN:-}"
  local candidates=()
  if [[ -n "$override" ]]; then
    candidates+=("$override")
  fi
  candidates+=(
    "$BUILD_DIR/apps/lambda-window-manager/lambda-window-manager"
    "$BUILD_DIR/lambda-window-manager"
  )
  local path
  for path in "${candidates[@]}"; do
    if [[ -x "$path" ]]; then
      printf "%s" "$path"
      return
    fi
  done
  printf "%s/apps/lambda-window-manager/lambda-window-manager" "$BUILD_DIR"
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
  if [[ "$START_COMPOSITOR" -eq 1 && "$PROBE_ONLY" -ne 1 ]]; then
    build_targets+=("lambda-window-manager")
  fi
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

WM_PID=""
DISPLAY_FILE="${XDG_RUNTIME_DIR:-}/lambda-window-manager-display"

stop_started_compositor() {
  set +e
  if [[ -n "$WM_PID" ]]; then
    kill -TERM -"$WM_PID" 2>/dev/null || kill -TERM "$WM_PID" 2>/dev/null || true
    sleep 1
    kill -KILL -"$WM_PID" 2>/dev/null || kill -KILL "$WM_PID" 2>/dev/null || true
    wait "$WM_PID" 2>/dev/null || true
  fi
  set -e
}

start_compositor_if_requested() {
  if [[ "$START_COMPOSITOR" -ne 1 || "$PROBE_ONLY" -eq 1 ]]; then
    return
  fi
  if [[ -z "${XDG_RUNTIME_DIR:-}" ]]; then
    echo "XDG_RUNTIME_DIR is required for --start-compositor." >&2
    exit 2
  fi
  if [[ -r "$DISPLAY_FILE" ]]; then
    echo "Refusing --start-compositor because $DISPLAY_FILE already exists." >&2
    echo "Stop the existing compositor or remove a stale display file before rerunning." >&2
    exit 2
  fi
  local wm
  wm="$(compositor_exe)"
  if [[ ! -x "$wm" ]]; then
    echo "Compositor binary not found: $wm" >&2
    echo "Build it first with: cmake --build $BUILD_DIR --target lambda-window-manager" >&2
    exit 1
  fi
  mkdir -p "$LOG_DIR"
  echo "Starting lambda-window-manager for real-app smoke validation."
  echo "Compositor log: $LOG_DIR/compositor.log"
  setsid timeout --signal=TERM --kill-after=5s "${COMPOSITOR_TIMEOUT_SECONDS}s" env \
    LAMBDA_WINDOW_MANAGER_CPU_TRACE=1 \
    LAMBDA_WINDOW_MANAGER_CPU_TRACE_LOG="$LOG_DIR/cpu.log" \
    LAMBDA_WINDOW_MANAGER_SAMPLE_TRACE="${LAMBDA_WINDOW_MANAGER_SAMPLE_TRACE:-0}" \
    LAMBDA_KMS_PRESENT_TRACE=1 \
    LAMBDA_WINDOW_MANAGER_PACING_TRACE=1 \
    LAMBDA_WINDOW_MANAGER_PACING_TRACE_LOG="$LOG_DIR/pacing.log" \
    "$wm" "${COMPOSITOR_ARGS[@]}" >"$LOG_DIR/compositor.log" 2>&1 &
  WM_PID=$!
  trap stop_started_compositor EXIT

  for _ in $(seq 1 150); do
    if [[ -r "$DISPLAY_FILE" ]]; then
      export WAYLAND_DISPLAY="$(cat "$DISPLAY_FILE")"
      return
    fi
    if ! kill -0 "$WM_PID" 2>/dev/null; then
      echo "lambda-window-manager exited before creating a Wayland display." >&2
      tail -n 160 "$LOG_DIR/compositor.log" >&2 || true
      exit 20
    fi
    sleep 0.1
  done
  echo "lambda-window-manager display file was not created: $DISPLAY_FILE" >&2
  tail -n 160 "$LOG_DIR/compositor.log" >&2 || true
  exit 20
}

start_compositor_if_requested

echo "Real-app smoke matrix:"
echo "  WAYLAND_DISPLAY: ${WAYLAND_DISPLAY:-not set}"
echo "  build dir:       $BUILD_DIR"
echo "  log dir:         $LOG_DIR"
echo "  seconds/app:     $SECONDS_PER_APP"
if [[ "$START_COMPOSITOR" -eq 1 && "$PROBE_ONLY" -ne 1 ]]; then
  echo "  compositor:      started by this script, timeout ${COMPOSITOR_TIMEOUT_SECONDS}s"
fi
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

capture_wayland_registry() {
  if [[ -z "${WAYLAND_DISPLAY:-}" ]]; then
    echo "Skipping Wayland registry check: WAYLAND_DISPLAY is not set."
    return 0
  fi
  if ! available_command wayland-info; then
    echo "Skipping Wayland registry check: wayland-info is not installed."
    if [[ "$START_COMPOSITOR" -eq 1 ]]; then
      echo "wayland-info is required for --start-compositor protocol registry validation." >&2
      return 1
    fi
    return 0
  fi

  local registry_log="$LOG_DIR/wayland-info.log"
  local summary_log="$LOG_DIR/wayland-registry-summary.log"
  if ! timeout --signal=TERM --kill-after=2s 8s wayland-info >"$registry_log" 2>&1; then
    echo "Wayland registry check failed; see $registry_log" >&2
    return 1
  fi

  local missing=()
  local global
  while IFS= read -r global; do
    [[ -z "$global" ]] && continue
    if ! grep -F -- "interface: '$global'" "$registry_log" >/dev/null &&
       ! grep -F -- "interface: \"$global\"" "$registry_log" >/dev/null &&
       ! grep -F -- "$global" "$registry_log" >/dev/null; then
      missing+=("$global")
    fi
  done < <(required_wayland_globals)

  {
    printf 'WAYLAND_DISPLAY=%s\n' "$WAYLAND_DISPLAY"
    printf 'registry_log=%s\n' "$registry_log"
    printf 'required_count=%s\n' "$(required_wayland_globals | wc -l)"
    printf 'missing_count=%s\n' "${#missing[@]}"
    if [[ "${#missing[@]}" -gt 0 ]]; then
      printf 'missing=%s\n' "${missing[*]}"
    fi
  } >"$summary_log"
  printf 'Wayland registry summary: required=%s missing=%s log=%s\n' \
    "$(required_wayland_globals | wc -l)" "${#missing[@]}" "$registry_log"
  if [[ "${#missing[@]}" -gt 0 ]]; then
    echo "Missing required Wayland globals: ${missing[*]}" >&2
    return 1
  fi
}

if ! capture_wayland_registry; then
  failed=1
fi

fatal_count=0
if [[ -d "$LOG_DIR" ]]; then
  fatal_count=$( (rg -n -i 'fatal|segmentation|assert|AddressSanitizer|VK_ERROR|present failed|surface lost|render error|protocol error|terminate called|exception|aborted' "$LOG_DIR" || true) | wc -l )
fi
if [[ "$fatal_count" -ne 0 ]]; then
  echo "Detected $fatal_count fatal/protocol/runtime log matches:"
  rg -n -i 'fatal|segmentation|assert|AddressSanitizer|VK_ERROR|present failed|surface lost|render error|protocol error|terminate called|exception|aborted' "$LOG_DIR" || true
  failed=1
fi
if [[ "$START_COMPOSITOR" -eq 1 && "$PROBE_ONLY" -ne 1 ]]; then
  cpu_trace_count=$(rg -c '^cpu-trace:' "$LOG_DIR/cpu.log" 2>/dev/null || echo 0)
  pacing_trace_count=$(rg -c '^pacing-trace:' "$LOG_DIR/pacing.log" 2>/dev/null || echo 0)
  printf 'Trace summary: cpu_trace=%s pacing_trace=%s compositor_log=%s cpu_log=%s pacing_log=%s\n' \
    "$cpu_trace_count" "$pacing_trace_count" "$LOG_DIR/compositor.log" "$LOG_DIR/cpu.log" "$LOG_DIR/pacing.log"
  if [[ "$cpu_trace_count" -lt 1 ]]; then
    echo "No CPU trace entries were captured by the started compositor." >&2
    failed=1
  fi
fi
if [[ "$failed" -ne 0 ]]; then
  echo "One or more required apps were missing or failed to launch. Document intentional exclusions or fix protocol/runtime failures."
  exit 1
fi

echo "Launch checks completed. Confirm the visual behavior before marking the real-app matrix done."
