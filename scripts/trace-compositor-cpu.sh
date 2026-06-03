#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${LWM_BUILD_DIR:-$ROOT/build}"
DEFAULT_BIN="$BUILD_DIR/apps/lambda-window-manager/lambda-window-manager"
FALLBACK_BIN="$BUILD_DIR/lambda-window-manager"
BIN="${LAMBDA_WINDOW_MANAGER_BIN:-$DEFAULT_BIN}"
LOG_DIR="${LWM_TRACE_DIR:-$ROOT/.debug-logs}"
TRACE_LOG="${LAMBDA_WINDOW_MANAGER_CPU_TRACE_LOG:-$LOG_DIR/lambda-window-manager-cpu.log}"
PACING_LOG="${LAMBDA_WINDOW_MANAGER_PACING_TRACE_LOG:-$LOG_DIR/lambda-window-manager-pacing.log}"
STDERR_LOG="${LAMBDA_WINDOW_MANAGER_STDERR_LOG:-$LOG_DIR/lambda-window-manager-compositor.log}"

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  cat <<EOF
Usage: scripts/trace-compositor-cpu.sh [lambda-window-manager args...]

Starts lambda-window-manager with CPU tracing and CPU-time sampling enabled.
Use this from the TTY where you normally start the compositor, reproduce the
problem, then inspect the printed log paths.

Default flow:
  1. Start this script.
  2. Let the compositor idle for about 5 seconds.
  3. Drag the terminal window around for about 10 seconds.
  4. Let it idle again for about 5 seconds.
  5. Quit/kill the compositor.

Environment overrides:
  LWM_BUILD_DIR                         Build directory. Default: $BUILD_DIR
  LWM_TRACE_DIR                         Log directory. Default: $LOG_DIR
  LAMBDA_WINDOW_MANAGER_BIN             Binary path. Default: $DEFAULT_BIN
  LAMBDA_WINDOW_MANAGER_CPU_TRACE_LOG   CPU trace log. Default: $TRACE_LOG
  LAMBDA_WINDOW_MANAGER_STDERR_LOG      Compositor stderr log. Default: $STDERR_LOG
  LAMBDA_WINDOW_MANAGER_SAMPLE_TRACE    1 enables sampled hot symbols. Default: 1
  LAMBDA_WINDOW_MANAGER_SAMPLE_USEC     CPU sample interval. Default: 1000
  LAMBDA_DEBUG_PERF                     0, 1, 2, or anomaly. Default: 0
  LAMBDA_WINDOW_MANAGER_PACING_TRACE    1 enables verbose pacing log. Default: 0
  LAMBDA_WINDOW_MANAGER_PACING_TRACE_LOG Pacing log. Default: $PACING_LOG
EOF
  exit 0
fi

mkdir -p "$LOG_DIR"

if [[ ! -x "$BIN" ]]; then
  if [[ "$BIN" == "$DEFAULT_BIN" && -x "$FALLBACK_BIN" ]]; then
    BIN="$FALLBACK_BIN"
  else
    echo "Compositor binary not found: $BIN" >&2
    echo "Build it first with: cmake --build $BUILD_DIR --target lambda-window-manager" >&2
    exit 1
  fi
fi

: >"$TRACE_LOG"
: >"$STDERR_LOG"
if [[ "${LAMBDA_WINDOW_MANAGER_PACING_TRACE:-0}" != "0" ]]; then
  : >"$PACING_LOG"
fi

echo "CPU trace log: $TRACE_LOG"
echo "Compositor log: $STDERR_LOG"
if [[ "${LAMBDA_WINDOW_MANAGER_PACING_TRACE:-0}" != "0" ]]; then
  echo "Pacing trace log: $PACING_LOG"
fi
echo
echo "Reproduce the CPU issue now: idle ~5s, drag a window ~10s, idle ~5s, then quit/kill the compositor."
echo "After it exits, useful commands are:"
echo "  tail -n 40 \"$TRACE_LOG\""
echo "  tail -n 80 \"$STDERR_LOG\""

export LAMBDA_WINDOW_MANAGER_CPU_TRACE=1
export LAMBDA_WINDOW_MANAGER_PACING_TRACE="${LAMBDA_WINDOW_MANAGER_PACING_TRACE:-0}"
export LAMBDA_WINDOW_MANAGER_PACING_TRACE_LOG="$PACING_LOG"
export LAMBDA_WINDOW_MANAGER_SAMPLE_TRACE="${LAMBDA_WINDOW_MANAGER_SAMPLE_TRACE:-1}"
export LAMBDA_WINDOW_MANAGER_SAMPLE_USEC="${LAMBDA_WINDOW_MANAGER_SAMPLE_USEC:-1000}"
export LAMBDA_WINDOW_MANAGER_CPU_TRACE_LOG="$TRACE_LOG"
export LAMBDA_DEBUG_PERF="${LAMBDA_DEBUG_PERF:-0}"

"$BIN" "$@" 2>&1 | tee "$STDERR_LOG"
