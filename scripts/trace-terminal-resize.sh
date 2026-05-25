#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_DIR="${LWM_TRACE_DIR:-$ROOT/.debug-logs}"
TEST_SECONDS="${LAMBDA_TERMINAL_RESIZE_TEST_SECONDS:-60}"
RENDER_LOG="${LAMBDA_TERMINAL_RENDER_LOG:-$LOG_DIR/lambda-terminal-resize-render.log}"
RESIZE_LOG="${LAMBDA_TERMINAL_RESIZE_LOG:-$LOG_DIR/lambda-terminal-resize.log}"

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  cat <<EOF
Usage: scripts/trace-terminal-resize.sh

Runs the lambda-terminal synthetic workload with resize tracing enabled. While it
runs, repeatedly resize the terminal window. Logs are written under $LOG_DIR by default.

Environment overrides:
  LWM_BUILD_DIR                          Build directory. Default: $ROOT/build
  LWM_TRACE_DIR                          Log directory. Default: $LOG_DIR
  LAMBDA_TERMINAL_RESIZE_TEST_SECONDS    Run duration. Default: $TEST_SECONDS
  LAMBDA_TERMINAL_RENDER_LOG             Terminal render log. Default: $RENDER_LOG
  LAMBDA_TERMINAL_RESIZE_LOG             Resize trace log. Default: $RESIZE_LOG
  LAMBDA_TERMINAL_TEST_ROWS              Grid rows drawn by workload. Default: 48
  LAMBDA_TERMINAL_TEST_SEGMENTS          Color runs per row in grid mode. Default: 14
  LAMBDA_TERMINAL_FRAME_SLEEP            Workload frame sleep. Default: 0.016
EOF
  exit 0
fi

mkdir -p "$LOG_DIR"

echo "Resize trace test:"
echo "  terminal render log: $RENDER_LOG"
echo "  resize trace log:    $RESIZE_LOG"
echo "  duration:            ${TEST_SECONDS}s"
echo
echo "During the run, repeatedly resize the lambda-terminal window."
echo

export LAMBDA_TERMINAL_TEST_SECONDS="$TEST_SECONDS"
export LAMBDA_TERMINAL_TEST_MODE="${LAMBDA_TERMINAL_TEST_MODE:-grid}"
export LAMBDA_TERMINAL_RENDER_LOG="$RENDER_LOG"
export LAMBDA_TERMINAL_RESIZE_LOG="$RESIZE_LOG"
export FLUX_DEBUG_PERF="${FLUX_DEBUG_PERF:-2}"
export FLUX_RESIZE_TRACE=1

"$ROOT/scripts/trace-terminal-rendering.sh"

if [[ ! -s "$RESIZE_LOG" ]]; then
  echo "No resize trace entries were captured. Resize the window during the run and try again."
  exit 0
fi

echo
echo "Resize trace summary:"
awk '
  {
    prefix = $3
    sub(/:$/, "", prefix)
    count[prefix]++

    line = $0
    while (match(line, /(elapsed|record|submit|queuePresent|waitFrame|acquire|waitImage|presentFence|waitPresentFence|resetPresentFence)=([0-9.]+)ms/)) {
      field = substr(line, RSTART, RLENGTH)
      split(field, parts, "=")
      metric = parts[1]
      value = parts[2]
      sub(/ms$/, "", value)
      key = prefix " " metric
      if ((value + 0.0) > maxValue[key]) {
        maxValue[key] = value + 0.0
      }
      line = substr(line, RSTART + RLENGTH)
    }
  }
  END {
    for (prefix in count) {
      printf "  %-24s %6d events\n", prefix, count[prefix]
    }
    print ""
    print "  Slowest traced phase by prefix:"
    for (key in maxValue) {
      printf "  %-34s %8.3fms\n", key, maxValue[key]
    }
  }
' "$RESIZE_LOG"

echo
echo "Recent resize trace entries:"
tail -n 20 "$RESIZE_LOG"
