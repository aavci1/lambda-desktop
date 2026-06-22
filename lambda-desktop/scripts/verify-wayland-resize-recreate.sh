#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${LWM_WAYLAND_BUILD_DIR:-$ROOT/build}"
LOG_ROOT="${LWM_WAYLAND_RESIZE_RECREATE_LOG_DIR:-$ROOT/.debug-logs/wayland-resize-recreate}"
RUN_ID="$(date +%Y%m%d-%H%M%S)"
LOG_DIR="$LOG_ROOT/$RUN_ID"
RESIZE_COUNT="${LAMBDA_WAYLAND_RESIZE_RECREATE_COUNT:-90}"
TIMEOUT_SECONDS="${LAMBDA_WAYLAND_RESIZE_RECREATE_TIMEOUT_SECONDS:-12}"
MAX_RECREATES="${LAMBDA_WAYLAND_RESIZE_RECREATE_MAX_RECREATES:-3}"
MAX_RECREATE_MS="${LAMBDA_WAYLAND_RESIZE_RECREATE_MAX_MS:-12}"
MAX_RETIRE_QUEUE_MS="${LAMBDA_WAYLAND_RESIZE_RECREATE_MAX_RETIRE_QUEUE_MS:-1}"

usage() {
  cat <<EOF
Usage: lambda-desktop/scripts/verify-wayland-resize-recreate.sh

Runs a focused Wayland resize/swapchain recreation check under headless Weston:
  - builds lambda-terminal
  - starts Weston headless with a fake seat
  - runs lambda-terminal with scripted self-resize and LAMBDA_RESIZE_TRACE=1
  - fails if swapchain recreates are missing, too frequent, or too expensive

Environment:
  LWM_WAYLAND_BUILD_DIR                       Wayland build dir. Default: $BUILD_DIR
  LWM_WAYLAND_RESIZE_RECREATE_LOG_DIR         Log root. Default: $LOG_ROOT
  LAMBDA_WAYLAND_RESIZE_RECREATE_COUNT        Scripted resize steps. Default: $RESIZE_COUNT
  LAMBDA_WAYLAND_RESIZE_RECREATE_TIMEOUT_SECONDS Timeout. Default: $TIMEOUT_SECONDS
  LAMBDA_WAYLAND_RESIZE_RECREATE_MAX_RECREATES Max recreate rows. Default: $MAX_RECREATES
  LAMBDA_WAYLAND_RESIZE_RECREATE_MAX_MS       Max recreate elapsed ms. Default: $MAX_RECREATE_MS
  LAMBDA_WAYLAND_RESIZE_RECREATE_MAX_RETIRE_QUEUE_MS Max retireQueue ms. Default: $MAX_RETIRE_QUEUE_MS
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

command -v weston >/dev/null 2>&1 || {
  echo "weston is required for Wayland resize recreate verification." >&2
  exit 2
}

mkdir -p "$LOG_DIR"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

cmake --build "$BUILD_DIR" --target lambda-terminal -j"$(nproc)"

TERMINAL="$BUILD_DIR/lambda-desktop/lambda-terminal/lambda-terminal"
if [[ ! -x "$TERMINAL" ]]; then
  echo "Missing executable: $TERMINAL" >&2
  exit 2
fi

SOCKET="lambda-weston-resize-recreate-$$"
WESTON_PID=""
cleanup() {
  set +e
  if [[ -n "$WESTON_PID" ]]; then
    kill -TERM "$WESTON_PID" 2>/dev/null || true
    sleep 1
    kill -KILL "$WESTON_PID" 2>/dev/null || true
    wait "$WESTON_PID" 2>/dev/null || true
  fi
  rm -f "$XDG_RUNTIME_DIR/$SOCKET" "$XDG_RUNTIME_DIR/$SOCKET.lock"
  set -e
}
trap cleanup EXIT

weston --backend=headless \
  --renderer=gl \
  --width=1280 \
  --height=720 \
  --idle-time=0 \
  --fake-seat \
  --socket="$SOCKET" \
  --no-config \
  --log="$LOG_DIR/weston.log" >"$LOG_DIR/weston-stdout.log" 2>&1 &
WESTON_PID=$!

for _ in $(seq 1 100); do
  if [[ -S "$XDG_RUNTIME_DIR/$SOCKET" ]]; then
    break
  fi
  if ! kill -0 "$WESTON_PID" 2>/dev/null; then
    echo "Weston exited before creating $SOCKET" >&2
    tail -n 120 "$LOG_DIR/weston.log" >&2 || true
    exit 20
  fi
  sleep 0.1
done

if [[ ! -S "$XDG_RUNTIME_DIR/$SOCKET" ]]; then
  echo "Weston socket $SOCKET was not created." >&2
  tail -n 120 "$LOG_DIR/weston.log" >&2 || true
  exit 20
fi

set +e
timeout --foreground --signal=TERM --kill-after=2s "${TIMEOUT_SECONDS}s" env \
  WAYLAND_DISPLAY="$SOCKET" \
  LAMBDA_DEBUG_PERF=2 \
  LAMBDA_RESIZE_TRACE=1 \
  LAMBDA_RESIZE_TRACE_LOG="$LOG_DIR/resize.log" \
  LAMBDA_RESIZE_TRACE_STDERR=0 \
  LAMBDA_TERMINAL_SCRIPTED_RESIZE=1 \
  LAMBDA_TERMINAL_SCRIPTED_RESIZE_COUNT="$RESIZE_COUNT" \
  LAMBDA_TERMINAL_SCRIPTED_RESIZE_EXIT=1 \
  LAMBDA_TERMINAL_SCRIPTED_RESIZE_LOG="$LOG_DIR/scripted-resize.log" \
  "$TERMINAL" >"$LOG_DIR/lambda-terminal.log" 2>&1
terminal_status=$?
set -e

if [[ "$terminal_status" -ne 0 ]]; then
  echo "SUMMARY wayland-resize-recreate terminal_status=$terminal_status log_dir=$LOG_DIR"
  tail -n 120 "$LOG_DIR/lambda-terminal.log" >&2 || true
  exit "$terminal_status"
fi

resize_steps=$(rg -c '^scripted-resize step=' "$LOG_DIR/scripted-resize.log" 2>/dev/null || echo 0)
fatal_count=$( (rg -n -i 'fatal|segmentation|assert|AddressSanitizer|VK_ERROR|present failed|surface lost|exception|aborted' \
  "$LOG_DIR/lambda-terminal.log" "$LOG_DIR/weston.log" "$LOG_DIR/resize.log" || true) | wc -l )
recreate_count=$(rg -c 'vulkan-recreate-swapchain' "$LOG_DIR/resize.log" 2>/dev/null || echo 0)
dirty_resize_count=$(rg -c 'vulkan-resize: .*dirty=1' "$LOG_DIR/resize.log" 2>/dev/null || echo 0)
clean_resize_count=$(rg -c 'vulkan-resize: .*dirty=0' "$LOG_DIR/resize.log" 2>/dev/null || echo 0)
max_elapsed="$(awk '
  /vulkan-recreate-swapchain/ {
    if (match($0, /elapsed=([0-9.]+)ms/, m) && m[1] + 0 > max) max = m[1] + 0
  }
  END { printf "%.3f", max + 0 }
' "$LOG_DIR/resize.log")"
max_retire_queue="$(awk '
  /vulkan-recreate-swapchain/ {
    if (match($0, /retireQueue=([0-9.]+)ms/, m) && m[1] + 0 > max) max = m[1] + 0
  }
  END { printf "%.3f", max + 0 }
' "$LOG_DIR/resize.log")"

printf 'SUMMARY wayland-resize-recreate resize_steps=%s expected_steps=%s recreates=%s dirty_resizes=%s clean_resizes=%s max_elapsed_ms=%s max_retire_queue_ms=%s fatal_matches=%s max_recreates=%s max_elapsed_allowed=%s max_retire_allowed=%s log_dir=%s\n' \
  "$resize_steps" "$RESIZE_COUNT" "$recreate_count" "$dirty_resize_count" "$clean_resize_count" \
  "$max_elapsed" "$max_retire_queue" "$fatal_count" "$MAX_RECREATES" "$MAX_RECREATE_MS" \
  "$MAX_RETIRE_QUEUE_MS" "$LOG_DIR"

awk -v resize_steps="$resize_steps" \
    -v expected="$RESIZE_COUNT" \
    -v fatal_count="$fatal_count" \
    -v recreates="$recreate_count" \
    -v max_recreates="$MAX_RECREATES" \
    -v max_elapsed="$max_elapsed" \
    -v max_elapsed_allowed="$MAX_RECREATE_MS" \
    -v max_retire="$max_retire_queue" \
    -v max_retire_allowed="$MAX_RETIRE_QUEUE_MS" \
    'BEGIN {
      if (resize_steps < expected) exit 1;
      if (fatal_count != 0) exit 2;
      if (recreates < 1 || recreates > max_recreates) exit 3;
      if (max_elapsed > max_elapsed_allowed) exit 4;
      if (max_retire > max_retire_allowed) exit 5;
    }'

echo "CASE wayland-resize-recreate renderer=gl log_dir=$LOG_DIR"
echo "Wayland resize recreate verification completed."
