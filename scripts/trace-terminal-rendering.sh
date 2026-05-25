#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${LWM_BUILD_DIR:-$ROOT/build}"
LOG_DIR="${LWM_TRACE_DIR:-$ROOT/.debug-logs}"
LOG_PATH="${LAMBDA_TERMINAL_RENDER_LOG:-$LOG_DIR/lambda-terminal-render.log}"
RESIZE_LOG_PATH="${LAMBDA_TERMINAL_RESIZE_LOG:-$LOG_DIR/lambda-terminal-resize.log}"
TEST_SECONDS="${LAMBDA_TERMINAL_TEST_SECONDS:-45}"
TEST_MODE="${LAMBDA_TERMINAL_TEST_MODE:-grid}"
PERF_LEVEL="${FLUX_DEBUG_PERF:-2}"
RESIZE_TRACE="${FLUX_RESIZE_TRACE:-0}"
WORKLOAD_PATH="$LOG_DIR/lambda-terminal-workload.sh"

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  cat <<EOF
Usage: scripts/trace-terminal-rendering.sh

Builds and runs lambda-terminal with a synthetic terminal workload and Flux
rendering perf logs enabled.

Environment overrides:
  LWM_BUILD_DIR                  Build directory. Default: $BUILD_DIR
  LWM_TRACE_DIR                  Directory for logs. Default: $LOG_DIR
  LAMBDA_TERMINAL_RENDER_LOG     Terminal perf log. Default: $LOG_PATH
  LAMBDA_TERMINAL_RESIZE_LOG     Resize trace log. Default: $RESIZE_LOG_PATH
  LAMBDA_TERMINAL_TEST_SECONDS   Run duration. Default: $TEST_SECONDS
  LAMBDA_TERMINAL_TEST_MODE      grid or scroll. Default: $TEST_MODE
  LAMBDA_TERMINAL_TEST_ROWS      Grid rows drawn by workload. Default: 48
  LAMBDA_TERMINAL_TEST_SEGMENTS  Color runs per row in grid mode. Default: 14
  LAMBDA_TERMINAL_FRAME_SLEEP    Workload frame sleep. Default: 0.016
  FLUX_DEBUG_PERF                1, 2, anomaly, or 0. Default: $PERF_LEVEL
  FLUX_RESIZE_TRACE              Enable resize logs. Default: $RESIZE_TRACE
EOF
  exit 0
fi

mkdir -p "$LOG_DIR"
rm -f "$LOG_PATH"
if [[ "$RESIZE_TRACE" != "0" ]]; then
  rm -f "$RESIZE_LOG_PATH"
fi

cat >"$WORKLOAD_PATH" <<'WORKLOAD'
#!/usr/bin/env bash
set -euo pipefail

mode="${LAMBDA_TERMINAL_TEST_MODE:-grid}"
duration="${LAMBDA_TERMINAL_WORKLOAD_SECONDS:-300}"
sleep_s="${LAMBDA_TERMINAL_FRAME_SLEEP:-0.016}"
rows="${LAMBDA_TERMINAL_TEST_ROWS:-48}"
segments="${LAMBDA_TERMINAL_TEST_SEGMENTS:-14}"

cleanup() {
  printf '\033[0m\033[?25h\r\n'
}
trap cleanup EXIT

printf '\033[?25l\033[2J'
SECONDS=0
frame=0

if [[ "$mode" == "scroll" ]]; then
  while (( SECONDS < duration )); do
    for ((line = 0; line < 10; ++line)); do
      fg=$((16 + (frame * 7 + line * 11) % 216))
      bg=$((16 + (frame * 5 + line * 13) % 216))
      printf '\033[38;5;%dm\033[48;5;%dmframe %06d scroll line %02d ' "$fg" "$bg" "$frame" "$line"
      printf 'abcdefghijklmnopqrstuvwxyz 0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ'
      printf '\033[0m\r\n'
    done
    frame=$((frame + 1))
    sleep "$sleep_s"
  done
else
  while (( SECONDS < duration )); do
    printf '\033[H'
    for ((row = 0; row < rows; ++row)); do
      for ((segment = 0; segment < segments; ++segment)); do
        fg=$((16 + (frame + row * 3 + segment * 5) % 216))
        bg=$((16 + (frame * 2 + row * 7 + segment * 11) % 216))
        printf '\033[38;5;%dm\033[48;5;%dm%05d:%02d:%02d ' "$fg" "$bg" "$frame" "$row" "$segment"
      done
      printf '\033[0m\033[K\r\n'
    done
    printf '\033[0mterminal-render-test frame=%06d mode=%s rows=%s segments=%s\033[K\r\n' \
      "$frame" "$mode" "$rows" "$segments"
    frame=$((frame + 1))
    sleep "$sleep_s"
  done
fi
WORKLOAD
chmod +x "$WORKLOAD_PATH"

cmake --build "$BUILD_DIR" --target lambda-terminal -j"$(nproc)"

echo "Writing terminal render log to: $LOG_PATH"
if [[ "$RESIZE_TRACE" != "0" ]]; then
  echo "Writing resize trace to: $RESIZE_LOG_PATH"
fi
echo "Run this while lambda-window-manager is active; resize the terminal during the test if needed."

export LAMBDA_TERMINAL_TEST_MODE="$TEST_MODE"
export LAMBDA_TERMINAL_WORKLOAD_SECONDS="$((TEST_SECONDS + 5))"

terminal_binary="$BUILD_DIR/examples/lambda-terminal"
if [[ ! -x "$terminal_binary" ]]; then
  terminal_binary="$BUILD_DIR/lambda-terminal"
fi
if [[ ! -x "$terminal_binary" ]]; then
  echo "lambda-terminal not found in $BUILD_DIR/examples or $BUILD_DIR" >&2
  exit 1
fi

command=("$terminal_binary")
if command -v timeout >/dev/null 2>&1; then
  command=(timeout --foreground "${TEST_SECONDS}s" "${command[@]}")
else
  echo "timeout(1) not found; close lambda-terminal manually when done."
fi

set +e
(
  cd "$ROOT"
  SHELL="$WORKLOAD_PATH" \
  FLUX_DEBUG_PERF="$PERF_LEVEL" \
  FLUX_RESIZE_TRACE="$RESIZE_TRACE" \
  FLUX_RESIZE_TRACE_LOG="$RESIZE_LOG_PATH" \
  "${command[@]}"
) 2>&1 | tee "$LOG_PATH"
status=${PIPESTATUS[0]}
set -e

if [[ "$status" -eq 124 ]]; then
  echo "Terminal render test completed after ${TEST_SECONDS}s."
  exit 0
fi

exit "$status"
