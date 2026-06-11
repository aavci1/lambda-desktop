#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WAYLAND_BUILD_DIR="${LWM_WAYLAND_BUILD_DIR:-$ROOT/build}"
KMS_BUILD_DIR="${LWM_KMS_BUILD_DIR:-$ROOT/build-kms}"
LOG_ROOT="${LWM_VT_SWITCH_LOG_DIR:-$ROOT/.debug-logs/kms-vt-switch}"
RUN_ID="$(date +%Y%m%d-%H%M%S)"
LOG_DIR="$LOG_ROOT/$RUN_ID"
TARGET_VT="${LWM_VT_SWITCH_TARGET:-}"
HOLD_SECONDS="${LWM_VT_SWITCH_HOLD_SECONDS:-2}"
CHECK_TIMEOUT_MS="${LAMBDA_PRESENTATION_FEEDBACK_TIMEOUT_MS:-8000}"

usage() {
  cat <<EOF
Usage: LWM_VT_SWITCH_TARGET=N scripts/verify-kms-vt-switch.sh

Runs a focused KMS VT release/acquire check from a real Linux TTY:
  - builds lambda-window-manager and lambda-presentation-feedback-check
  - starts lambda-window-manager with CPU, KMS, and pacing traces
  - verifies presentation feedback before the VT switch
  - switches to the explicit target VT, then switches back to the original VT
  - verifies presentation feedback after reacquire
  - asserts the compositor logged VT release/acquire activity and no fatal errors

This script changes the active VT. It refuses to guess a target VT and refuses
to run when it cannot determine the current foreground VT.

Environment:
  LWM_VT_SWITCH_TARGET       Required target VT number, different from current VT
  LWM_VT_SWITCH_HOLD_SECONDS Seconds to stay on the target VT. Default: $HOLD_SECONDS
  LWM_VT_SWITCH_LOG_DIR      Log root. Default: $LOG_ROOT
  LWM_WAYLAND_BUILD_DIR      Wayland build dir. Default: $WAYLAND_BUILD_DIR
  LWM_KMS_BUILD_DIR          KMS build dir. Default: $KMS_BUILD_DIR
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

if [[ -z "$TARGET_VT" ]]; then
  usage >&2
  echo "LWM_VT_SWITCH_TARGET is required; refusing to guess a VT." >&2
  exit 2
fi
if ! [[ "$TARGET_VT" =~ ^[0-9]+$ ]] || [[ "$TARGET_VT" -le 0 ]]; then
  echo "LWM_VT_SWITCH_TARGET must be a positive VT number." >&2
  exit 2
fi

command -v chvt >/dev/null 2>&1 || {
  echo "chvt is required for VT-switch verification." >&2
  exit 2
}
command -v fgconsole >/dev/null 2>&1 || {
  echo "fgconsole is required for VT-switch verification." >&2
  exit 2
}
command -v rg >/dev/null 2>&1 || {
  echo "ripgrep is required for log validation." >&2
  exit 2
}

current_tty="$(tty || true)"
if [[ "$current_tty" == "not a tty" || -z "$current_tty" ]]; then
  echo "This script must run from a real TTY or privileged session that can switch VTs." >&2
  exit 2
fi

CURRENT_VT="$(fgconsole 2>/dev/null || true)"
if ! [[ "$CURRENT_VT" =~ ^[0-9]+$ ]] || [[ "$CURRENT_VT" -le 0 ]]; then
  echo "Could not determine the current foreground VT with fgconsole." >&2
  exit 2
fi
if [[ "$CURRENT_VT" == "$TARGET_VT" ]]; then
  echo "LWM_VT_SWITCH_TARGET must differ from the current VT ($CURRENT_VT)." >&2
  exit 2
fi

mkdir -p "$LOG_DIR"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

cmake --build "$KMS_BUILD_DIR" --target lambda-window-manager -j"$(nproc)"
cmake --build "$WAYLAND_BUILD_DIR" --target lambda-presentation-feedback-check -j"$(nproc)"

WM="$KMS_BUILD_DIR/apps/lambda-window-manager/lambda-window-manager"
CHECKER="$WAYLAND_BUILD_DIR/tools/lambda-presentation-feedback-check"
for binary in "$WM" "$CHECKER"; do
  if [[ ! -x "$binary" ]]; then
    echo "Missing executable: $binary" >&2
    exit 2
  fi
done

display_file="$XDG_RUNTIME_DIR/lambda-window-manager-display"
rm -f "$display_file"

wm_pid=""
cleanup() {
  set +e
  if [[ -n "${CURRENT_VT:-}" ]]; then
    chvt "$CURRENT_VT" 2>/dev/null || true
  fi
  if [[ -n "$wm_pid" ]]; then
    kill -TERM -"$wm_pid" 2>/dev/null || kill -TERM "$wm_pid" 2>/dev/null || true
  fi
  sleep 1
  if [[ -n "$wm_pid" ]]; then
    kill -KILL -"$wm_pid" 2>/dev/null || kill -KILL "$wm_pid" 2>/dev/null || true
  fi
  rm -f "$display_file"
  set -e
}
trap cleanup EXIT

setsid timeout --signal=TERM --kill-after=5s 35s env \
  LAMBDA_WINDOW_MANAGER_CPU_TRACE=1 \
  LAMBDA_WINDOW_MANAGER_CPU_TRACE_LOG="$LOG_DIR/cpu.log" \
  LAMBDA_WINDOW_MANAGER_SAMPLE_TRACE=0 \
  LAMBDA_KMS_PRESENT_TRACE=1 \
  LAMBDA_WINDOW_MANAGER_PACING_TRACE=1 \
  LAMBDA_WINDOW_MANAGER_PACING_TRACE_LOG="$LOG_DIR/pacing.log" \
  "$WM" >"$LOG_DIR/compositor.log" 2>&1 &
wm_pid=$!

display=""
for _ in $(seq 1 150); do
  if [[ -r "$display_file" ]]; then
    display="$(cat "$display_file")"
    break
  fi
  if ! kill -0 "$wm_pid" 2>/dev/null; then
    echo "lambda-window-manager exited before creating a Wayland display." >&2
    tail -n 120 "$LOG_DIR/compositor.log" >&2 || true
    exit 20
  fi
  sleep 0.1
done
if [[ -z "$display" ]]; then
  echo "lambda-window-manager display file was not created." >&2
  tail -n 120 "$LOG_DIR/compositor.log" >&2 || true
  exit 20
fi

env WAYLAND_DISPLAY="$display" \
  LAMBDA_PRESENTATION_FEEDBACK_TIMEOUT_MS="$CHECK_TIMEOUT_MS" \
  LAMBDA_PRESENTATION_FEEDBACK_REQUIRE_HARDWARE_FLAGS=1 \
  LAMBDA_PRESENTATION_FEEDBACK_HOLD_MS=0 \
  "$CHECKER" >"$LOG_DIR/presentation-before.log" 2>&1

printf 'switch current_vt=%s target_vt=%s hold_seconds=%s\n' \
  "$CURRENT_VT" "$TARGET_VT" "$HOLD_SECONDS" >"$LOG_DIR/vt-switch.log"
chvt "$TARGET_VT"
sleep "$HOLD_SECONDS"
chvt "$CURRENT_VT"
sleep "$HOLD_SECONDS"

env WAYLAND_DISPLAY="$display" \
  LAMBDA_PRESENTATION_FEEDBACK_TIMEOUT_MS="$CHECK_TIMEOUT_MS" \
  LAMBDA_PRESENTATION_FEEDBACK_REQUIRE_HARDWARE_FLAGS=1 \
  LAMBDA_PRESENTATION_FEEDBACK_HOLD_MS=0 \
  "$CHECKER" >"$LOG_DIR/presentation-after.log" 2>&1

fatal_count=$( (rg -n -i 'fatal|segmentation|assert|terminate called|AddressSanitizer|exception|aborted|drmSetMaster attempt 10|VT_RELDISP .* failed|KDSETMODE\\(KD_GRAPHICS\\) after VT acquire failed' "$LOG_DIR" || true) | wc -l )
release_count=$(rg -c 'releasing DRM master for VT switch|vt-state foreground=0|suspended libinput for inactive VT' "$LOG_DIR/compositor.log" 2>/dev/null || echo 0)
acquire_count=$(rg -c 'reacquiring DRM master after VT switch|vt-state foreground=1|vt-resume|resumed libinput for active VT' "$LOG_DIR/compositor.log" 2>/dev/null || echo 0)
before_present=$(rg -c 'presented clock=.*VSYNC.*HW_CLOCK.*HW_COMPLETION' "$LOG_DIR/presentation-before.log" 2>/dev/null || echo 0)
after_present=$(rg -c 'presented clock=.*VSYNC.*HW_CLOCK.*HW_COMPLETION' "$LOG_DIR/presentation-after.log" 2>/dev/null || echo 0)

printf 'SUMMARY kms-vt-switch current_vt=%s target_vt=%s release_events=%s acquire_events=%s before_present=%s after_present=%s fatal_matches=%s log_dir=%s\n' \
  "$CURRENT_VT" "$TARGET_VT" "$release_count" "$acquire_count" "$before_present" "$after_present" "$fatal_count" "$LOG_DIR"

if [[ "$fatal_count" -ne 0 || "$release_count" -lt 1 || "$acquire_count" -lt 1 ||
      "$before_present" -lt 1 || "$after_present" -lt 1 ]]; then
  echo "KMS VT-switch verification failed." >&2
  tail -n 160 "$LOG_DIR/compositor.log" >&2 || true
  exit 1
fi

echo "CASE kms-vt-switch display=$display log_dir=$LOG_DIR"
echo "KMS VT-switch verification completed."
