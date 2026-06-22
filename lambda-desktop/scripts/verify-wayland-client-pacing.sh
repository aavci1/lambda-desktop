#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${LWM_WAYLAND_BUILD_DIR:-$ROOT/build}"
LOG_ROOT="${LWM_WAYLAND_CLIENT_PACING_LOG_DIR:-$ROOT/.debug-logs/wayland-client-pacing}"
RUN_ID="$(date +%Y%m%d-%H%M%S)"
LOG_DIR="$LOG_ROOT/$RUN_ID"
TEST_SECONDS="${LAMBDA_WAYLAND_CLIENT_PACING_SECONDS:-8}"
MAX_FRAME_DONE_TO_PRESENT_MS="${LAMBDA_WAYLAND_CLIENT_FRAME_DONE_PRESENT_MAX_MS:-8}"
FORCE_FIFO="${LAMBDA_WAYLAND_CLIENT_PACING_FORCE_FIFO:-1}"

usage() {
  cat <<EOF
Usage: lambda-desktop/scripts/verify-wayland-client-pacing.sh

Runs a focused Linux Wayland-client pacing check under headless Weston GL:
  - builds lambda-terminal
  - starts Weston headless with a fake seat
  - runs the scripted lambda-terminal workload with LAMBDA_RESIZE_TRACE=1
  - asserts frameDone callbacks produce app-render presents in the same dispatch/flush path
  - fails on Vulkan surface loss, present failure, or missing present-detail samples

Environment:
  LWM_WAYLAND_BUILD_DIR                         Wayland build dir. Default: $BUILD_DIR
  LWM_WAYLAND_CLIENT_PACING_LOG_DIR             Log root. Default: $LOG_ROOT
  LAMBDA_WAYLAND_CLIENT_PACING_SECONDS          Workload duration. Default: $TEST_SECONDS
  LAMBDA_WAYLAND_CLIENT_FRAME_DONE_PRESENT_MAX_MS Max frameDone->present delta. Default: $MAX_FRAME_DONE_TO_PRESENT_MS
  LAMBDA_WAYLAND_CLIENT_PACING_FORCE_FIFO       Force FIFO present mode. Default: $FORCE_FIFO
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

command -v weston >/dev/null 2>&1 || {
  echo "weston is required for Wayland client pacing verification." >&2
  exit 2
}

mkdir -p "$LOG_DIR"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

cmake --build "$BUILD_DIR" --target lambda-terminal -j"$(nproc)"

SOCKET="lambda-weston-pacing-$$"
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

WAYLAND_DISPLAY="$SOCKET" \
  LWM_BUILD_DIR="$BUILD_DIR" \
  LWM_TRACE_DIR="$LOG_DIR" \
  LAMBDA_TERMINAL_TEST_SECONDS="$TEST_SECONDS" \
  LAMBDA_TERMINAL_TEST_MODE=grid \
  LAMBDA_DEBUG_PERF=2 \
  LAMBDA_RESIZE_TRACE=1 \
  LAMBDA_VULKAN_FORCE_FIFO_PRESENT_MODE="$FORCE_FIFO" \
  "$ROOT/lambda-desktop/scripts/trace-terminal-rendering.sh" >"$LOG_DIR/driver.log" 2>&1

RENDER_LOG="$LOG_DIR/lambda-terminal-render.log"
RESIZE_LOG="$LOG_DIR/lambda-terminal-resize.log"

fatal_count=$( (rg -n -i 'fatal|segmentation|assert|AddressSanitizer|VK_ERROR|present failed|surface lost' \
  "$LOG_DIR/driver.log" "$RENDER_LOG" "$RESIZE_LOG" "$LOG_DIR/weston.log" || true) | wc -l )
if [[ "$fatal_count" -ne 0 ]]; then
  echo "SUMMARY wayland-client-pacing fatal_matches=$fatal_count log_dir=$LOG_DIR"
  rg -n -i 'fatal|segmentation|assert|AddressSanitizer|VK_ERROR|present failed|surface lost' \
    "$LOG_DIR/driver.log" "$RENDER_LOG" "$RESIZE_LOG" "$LOG_DIR/weston.log" >&2 || true
  exit 1
fi

awk -v label="wayland-client-pacing" \
    -v max_delta="$MAX_FRAME_DONE_TO_PRESENT_MS" \
    -v force_fifo="$FORCE_FIFO" '
  /wayland-window: frame-done/ {
    t = $2
    sub(/ms$/, "", t)
    pending = t + 0.0
    frame_done += 1
  }
  /app-render: present/ && pending > 0.0 {
    t = $2
    sub(/ms$/, "", t)
    delta = (t + 0.0) - pending
    pairs += 1
    delta_sum += delta
    if (delta > delta_max) delta_max = delta
    pending = 0.0
  }
  /vulkan-present-detail:/ { present_detail += 1 }
  /wayland-window: flush-deferred/ && /immediate=1/ { immediate_flush += 1 }
  /wayland-window: request-frame/ && /mode=fifo/ { fifo_requests += 1 }
  /wayland-window: request-frame/ && /mode=mailbox/ { mailbox_requests += 1 }
  END {
    avg = pairs > 0 ? delta_sum / pairs : 0.0
    printf "SUMMARY %s frame_done=%d present_pairs=%d present_detail=%d immediate_flush=%d fifo_requests=%d mailbox_requests=%d force_fifo=%s frame_done_to_present_avg=%.3f frame_done_to_present_max=%.3f max_allowed=%.3f\n",
      label, frame_done, pairs, present_detail, immediate_flush, fifo_requests, mailbox_requests, force_fifo, avg, delta_max, max_delta
    if (frame_done < 10 || pairs < 10 || present_detail < 10 || immediate_flush < 10 || delta_max > max_delta ||
        (force_fifo != "0" && (fifo_requests < 10 || mailbox_requests != 0))) {
      exit 1
    }
  }
' "$RESIZE_LOG"

echo "CASE wayland-client-pacing renderer=gl log_dir=$LOG_DIR"
echo "Wayland client pacing verification completed."
