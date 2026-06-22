#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${LWM_WAYLAND_BUILD_DIR:-$ROOT/build}"
LOG_ROOT="${LWM_WAYLAND_RESIZE_QUIET_LOG_DIR:-$ROOT/.debug-logs/wayland-resize-quiet}"
RUN_ID="$(date +%Y%m%d-%H%M%S)"
LOG_DIR="$LOG_ROOT/$RUN_ID"
RESIZE_COUNT="${LAMBDA_WAYLAND_RESIZE_QUIET_COUNT:-90}"
TIMEOUT_SECONDS="${LAMBDA_WAYLAND_RESIZE_QUIET_TIMEOUT_SECONDS:-12}"

usage() {
  cat <<EOF
Usage: lambda-desktop/scripts/verify-wayland-resize-quiet.sh

Runs a focused Wayland resize stderr check under headless Weston:
  - builds lambda-terminal
  - starts Weston headless with a fake seat
  - runs lambda-terminal with its scripted self-resize diagnostic
  - leaves LAMBDA_RESIZE_TRACE disabled
  - fails if resize-trace or swapchain-recreate diagnostics leak to stderr

Environment:
  LWM_WAYLAND_BUILD_DIR                    Wayland build dir. Default: $BUILD_DIR
  LWM_WAYLAND_RESIZE_QUIET_LOG_DIR         Log root. Default: $LOG_ROOT
  LAMBDA_WAYLAND_RESIZE_QUIET_COUNT        Scripted resize steps. Default: $RESIZE_COUNT
  LAMBDA_WAYLAND_RESIZE_QUIET_TIMEOUT_SECONDS Timeout. Default: $TIMEOUT_SECONDS
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

command -v weston >/dev/null 2>&1 || {
  echo "weston is required for Wayland resize-quiet verification." >&2
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

SOCKET="lambda-weston-resize-quiet-$$"
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
  LAMBDA_DEBUG_PERF=0 \
  LAMBDA_RESIZE_TRACE=0 \
  LAMBDA_TERMINAL_SCRIPTED_RESIZE=1 \
  LAMBDA_TERMINAL_SCRIPTED_RESIZE_COUNT="$RESIZE_COUNT" \
  LAMBDA_TERMINAL_SCRIPTED_RESIZE_EXIT=1 \
  LAMBDA_TERMINAL_SCRIPTED_RESIZE_LOG="$LOG_DIR/scripted-resize.log" \
  "$TERMINAL" >"$LOG_DIR/lambda-terminal.log" 2>&1
terminal_status=$?
set -e

if [[ "$terminal_status" -ne 0 ]]; then
  echo "SUMMARY wayland-resize-quiet terminal_status=$terminal_status log_dir=$LOG_DIR"
  tail -n 120 "$LOG_DIR/lambda-terminal.log" >&2 || true
  exit "$terminal_status"
fi

resize_steps=$(rg -c '^scripted-resize step=' "$LOG_DIR/scripted-resize.log" 2>/dev/null || echo 0)
fatal_count=$( (rg -n -i 'fatal|segmentation|assert|AddressSanitizer|VK_ERROR|present failed|surface lost|exception|aborted' \
  "$LOG_DIR/lambda-terminal.log" "$LOG_DIR/weston.log" || true) | wc -l )
resize_noise_count=$( (rg -n 'resize-trace:|vulkan-recreate-swapchain|swapchain extent window=|request-resize-redraw|apply-configure|toplevel-configure' \
  "$LOG_DIR/lambda-terminal.log" || true) | wc -l )

printf 'SUMMARY wayland-resize-quiet resize_steps=%s expected_steps=%s fatal_matches=%s resize_noise=%s log_dir=%s\n' \
  "$resize_steps" "$RESIZE_COUNT" "$fatal_count" "$resize_noise_count" "$LOG_DIR"

if [[ "$resize_steps" -lt "$RESIZE_COUNT" || "$fatal_count" -ne 0 || "$resize_noise_count" -ne 0 ]]; then
  if [[ "$fatal_count" -ne 0 ]]; then
    rg -n -i 'fatal|segmentation|assert|AddressSanitizer|VK_ERROR|present failed|surface lost|exception|aborted' \
      "$LOG_DIR/lambda-terminal.log" "$LOG_DIR/weston.log" >&2 || true
  fi
  if [[ "$resize_noise_count" -ne 0 ]]; then
    rg -n 'resize-trace:|vulkan-recreate-swapchain|swapchain extent window=|request-resize-redraw|apply-configure|toplevel-configure' \
      "$LOG_DIR/lambda-terminal.log" >&2 || true
  fi
  exit 1
fi

echo "CASE wayland-resize-quiet renderer=gl log_dir=$LOG_DIR"
echo "Wayland resize stderr verification completed."
