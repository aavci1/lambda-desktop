#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PLAN="$ROOT/docs/compositor-wlroots-improvement-plan.md"
SMOKE="$ROOT/scripts/run-real-app-smoke.sh"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

require_in_file() {
  local needle="$1"
  local file="$2"
  grep -F -- "$needle" "$file" >/dev/null || fail "missing '$needle' in ${file#$ROOT/}"
}

for label in \
  "Settings" \
  "Files" \
  "Editor" \
  "Terminal" \
  "Shell" \
  "Firefox" \
  "GTK" \
  "Qt" \
  "foot" \
  "mpv"; do
  require_in_file "| $label |" "$PLAN"
done

for criterion in \
  "no flicker" \
  "no non-synced chrome/content frames" \
  "dock/topbar stable" \
  "menus/popovers do not crash Shell or compositor" \
  "fullscreen panels restore" \
  "cursor updates immediately"; do
  require_in_file "$criterion" "$PLAN"
done

list_output="$("$SMOKE" --list)"
for case_name in \
  "lambda-settings" \
  "lambda-files" \
  "lambda-editor" \
  "lambda-terminal" \
  "shell" \
  "terminal" \
  "browser" \
  "gtk" \
  "qt" \
  "mpv"; do
  grep -E "^${case_name}[[:space:]]" <<<"$list_output" >/dev/null ||
    fail "scripts/run-real-app-smoke.sh --list is missing case '$case_name'"
done

for runner_capability in \
  "--start-compositor" \
  "LAMBDA_WINDOW_MANAGER_CPU_TRACE=1" \
  "LAMBDA_WINDOW_MANAGER_PACING_TRACE=1" \
  "LAMBDA_KMS_PRESENT_TRACE=1" \
  "wayland-info" \
  "required_wayland_globals" \
  "wl_compositor" \
  "xdg_wm_base" \
  "zwlr_layer_shell_v1" \
  "wp_cursor_shape_manager_v1" \
  "Wayland registry summary:" \
  "Trace summary:" \
  "fatal/protocol/runtime log matches"; do
  require_in_file "$runner_capability" "$SMOKE"
done

echo "WM-COMP-27 real-app matrix is documented and matches the smoke runner."
