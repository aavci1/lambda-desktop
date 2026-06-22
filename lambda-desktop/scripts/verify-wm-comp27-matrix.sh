#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
PLAN="$ROOT/lambda-desktop/docs/compositor-wlroots-improvement-plan.md"
SMOKE="$ROOT/lambda-desktop/scripts/run-real-app-smoke.sh"
AUDIT="$ROOT/lambda-desktop/scripts/audit-wm-comp27-validation-gates.sh"
SERVER_LIFECYCLE="$ROOT/lambda-desktop/apps/lambda-window-manager/Compositor/Wayland/ServerLifecycle.cpp"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

require_in_file() {
  local needle="$1"
  local file="$2"
  grep -F -- "$needle" "$file" >/dev/null || fail "missing '$needle' in ${file#$ROOT/}"
}

[[ -x "$AUDIT" ]] || fail "missing executable lambda-desktop/scripts/audit-wm-comp27-validation-gates.sh"

required_globals="$(
  awk '
    /^required_wayland_globals\(\)/ { in_fn = 1; next }
    in_fn && /^EOF$/ { in_doc = 0; in_fn = 0; next }
    in_fn && /^  cat <<'\''EOF'\''/ { in_doc = 1; next }
    in_doc && NF > 0 { print }
  ' "$SMOKE"
)"
[[ -n "$required_globals" ]] || fail "could not extract required Wayland globals from lambda-desktop/scripts/run-real-app-smoke.sh"

advertised_globals="$(
  grep -oE '&[A-Za-z0-9_]+_interface' "$SERVER_LIFECYCLE" |
    sed -E 's/^&//; s/_interface$//' |
    sort -u
)"
[[ -n "$advertised_globals" ]] || fail "could not extract advertised Wayland globals from ServerLifecycle.cpp"

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
    fail "lambda-desktop/scripts/run-real-app-smoke.sh --list is missing case '$case_name'"
done

for runner_capability in \
  "--start-compositor" \
  "comma-separated" \
  "valid_case_name" \
  "Unknown case:" \
  "SELECTED_CASES" \
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

for audit_capability in \
  "real-app-probe" \
  "live-wayland-session" \
  "live-registry-probe" \
  "lambda-window-manager-display" \
  "target-tty" \
  "uinput-driving" \
  "manual-visual-checks" \
  "wm-comp27-validation-gates" \
  "run-real-app-smoke.sh"; do
  require_in_file "$audit_capability" "$AUDIT"
done

require_in_file "audit-wm-comp27-validation-gates.sh" "$PLAN"

while IFS= read -r global; do
  [[ -z "$global" ]] && continue
  grep -Fx -- "$global" <<<"$advertised_globals" >/dev/null ||
    fail "required Wayland global '$global' is not advertised by ServerLifecycle.cpp"
done <<<"$required_globals"

while IFS= read -r global; do
  [[ -z "$global" ]] && continue
  grep -Fx -- "$global" <<<"$required_globals" >/dev/null ||
    fail "advertised Wayland global '$global' is missing from run-real-app-smoke.sh registry validation"
done <<<"$advertised_globals"

echo "WM-COMP-27 real-app matrix is documented and matches the smoke runner."
