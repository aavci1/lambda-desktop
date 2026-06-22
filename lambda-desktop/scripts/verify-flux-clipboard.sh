#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${LAMBDA_CLIPBOARD_BUILD_DIR:-$ROOT/build}"
LOG_ROOT="${LAMBDA_CLIPBOARD_LOG_DIR:-$ROOT/.debug-logs/flux-clipboard}"
RUN_ID="$(date +%Y%m%d-%H%M%S)"
LOG_DIR="$LOG_ROOT/$RUN_ID"
INPUT_MODE="${LAMBDA_CLIPBOARD_INPUT_MODE:-auto}"
TEXT="${LAMBDA_CLIPBOARD_TEST_TEXT:-Flux clipboard probe $(date +%s%N)}"
PROBE_TIMEOUT_MS="${LAMBDA_CLIPBOARD_PROBE_TIMEOUT_MS:-6000}"
TERMINAL_TIMEOUT_MS="${LAMBDA_CLIPBOARD_TERMINAL_TIMEOUT_MS:-6000}"

usage() {
  cat <<EOF
Usage: lambda-desktop/scripts/verify-flux-clipboard.sh

Runs a focused Flux clipboard verification under headless Weston GL:
  - builds lambda-clipboard-probe and lambda-terminal
  - starts a source Flux TextInput process and copies selected text
  - starts a second Flux TextInput process and verifies paste through the shared Wayland clipboard
  - starts lambda-terminal and verifies Terminal reads the same clipboard with Ctrl+Shift+V

Environment:
  LAMBDA_CLIPBOARD_BUILD_DIR        Build dir. Default: $BUILD_DIR
  LAMBDA_CLIPBOARD_LOG_DIR          Log root. Default: $LOG_ROOT
  LAMBDA_CLIPBOARD_INPUT_MODE       auto, wtype, or dispatch. Default: $INPUT_MODE
  LAMBDA_CLIPBOARD_TEST_TEXT        Payload to copy/paste. Default generated per run.
  LAMBDA_CLIPBOARD_PROBE_TIMEOUT_MS Probe timeout. Default: $PROBE_TIMEOUT_MS
  LAMBDA_CLIPBOARD_TERMINAL_TIMEOUT_MS Terminal timeout. Default: $TERMINAL_TIMEOUT_MS
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

case "$INPUT_MODE" in
  auto|wtype|dispatch) ;;
  *)
    echo "LAMBDA_CLIPBOARD_INPUT_MODE must be auto, wtype, or dispatch." >&2
    exit 2
    ;;
esac

command -v weston >/dev/null 2>&1 || {
  echo "weston is required for clipboard verification." >&2
  exit 2
}

if [[ "$INPUT_MODE" != "dispatch" ]]; then
  command -v wtype >/dev/null 2>&1 || {
    if [[ "$INPUT_MODE" == "wtype" ]]; then
      echo "wtype is required when LAMBDA_CLIPBOARD_INPUT_MODE=wtype." >&2
      exit 2
    fi
  }
fi

mkdir -p "$LOG_DIR"
cmake --build "$BUILD_DIR" --target lambda-clipboard-probe lambda-terminal -j"$(nproc)"

RUNTIME_BASE="${XDG_RUNTIME_DIR:-/tmp}"
RUNTIME_DIR="$(mktemp -d "$RUNTIME_BASE/lambda-clipboard-runtime.XXXXXX")"
chmod 700 "$RUNTIME_DIR"
SOCKET="lambda-clipboard-$RUN_ID"
WESTON_PID=""
SOURCE_PID=""
SINK_PID=""
TERMINAL_PID=""
EFFECTIVE_MODE="$INPUT_MODE"

cleanup() {
  set +e
  for pid in "$TERMINAL_PID" "$SINK_PID" "$SOURCE_PID"; do
    if [[ -n "$pid" ]]; then
      kill -TERM "$pid" 2>/dev/null || true
      wait "$pid" 2>/dev/null || true
    fi
  done
  if [[ -n "$WESTON_PID" ]]; then
    kill -TERM "$WESTON_PID" 2>/dev/null || true
    sleep 1
    kill -KILL "$WESTON_PID" 2>/dev/null || true
    wait "$WESTON_PID" 2>/dev/null || true
  fi
  rm -rf "$RUNTIME_DIR"
  set -e
}
trap cleanup EXIT

wait_for_log() {
  local pattern="$1"
  local file="$2"
  local timeout_s="$3"
  local elapsed=0
  while (( elapsed < timeout_s * 10 )); do
    if [[ -f "$file" ]] && rg -q "$pattern" "$file"; then
      return 0
    fi
    sleep 0.1
    elapsed=$((elapsed + 1))
  done
  return 1
}

wait_for_socket() {
  for _ in $(seq 1 100); do
    if [[ -S "$RUNTIME_DIR/$SOCKET" ]]; then
      return 0
    fi
    if ! kill -0 "$WESTON_PID" 2>/dev/null; then
      echo "Weston exited before creating $SOCKET" >&2
      tail -n 120 "$LOG_DIR/weston.log" >&2 || true
      exit 20
    fi
    sleep 0.1
  done
  echo "Weston socket $SOCKET was not created." >&2
  tail -n 120 "$LOG_DIR/weston.log" >&2 || true
  exit 20
}

run_wtype() {
  WAYLAND_DISPLAY="$SOCKET" XDG_RUNTIME_DIR="$RUNTIME_DIR" wtype "$@" \
    >>"$LOG_DIR/wtype.log" 2>&1
}

start_source() {
  local auto_copy="$1"
  SOURCE_PID=""
  env WAYLAND_DISPLAY="$SOCKET" \
    XDG_RUNTIME_DIR="$RUNTIME_DIR" \
    LAMBDA_CLIPBOARD_PROBE_ROLE=source \
    LAMBDA_CLIPBOARD_PROBE_TEXT="$TEXT" \
    LAMBDA_CLIPBOARD_PROBE_TIMEOUT_MS="$PROBE_TIMEOUT_MS" \
    LAMBDA_CLIPBOARD_PROBE_AUTO_COPY="$auto_copy" \
    "$BUILD_DIR/lambda-desktop/tools/lambda-clipboard-probe" >"$LOG_DIR/source.stdout" 2>"$LOG_DIR/source.stderr" &
  SOURCE_PID=$!
  if ! wait_for_log 'lambda-clipboard-probe: ready role=source' "$LOG_DIR/source.stderr" 8; then
    echo "Source clipboard probe did not become ready." >&2
    tail -n 120 "$LOG_DIR/source.stderr" >&2 || true
    exit 1
  fi
}

start_weston() {
  XDG_RUNTIME_DIR="$RUNTIME_DIR" weston --backend=headless \
    --renderer=gl \
    --width=1280 \
    --height=720 \
    --idle-time=0 \
    --fake-seat \
    --socket="$SOCKET" \
    --no-config \
    --log="$LOG_DIR/weston.log" >"$LOG_DIR/weston-stdout.log" 2>&1 &
  WESTON_PID=$!
  wait_for_socket
}

start_weston

if [[ "$INPUT_MODE" == "dispatch" ]]; then
  EFFECTIVE_MODE="dispatch"
  start_source 1
elif [[ "$INPUT_MODE" == "wtype" || "$INPUT_MODE" == "auto" ]]; then
  start_source 0
  sleep 0.4
  if run_wtype -M ctrl c -m ctrl; then
    EFFECTIVE_MODE="wtype"
  elif [[ "$INPUT_MODE" == "auto" ]]; then
    echo "wtype copy path unavailable; falling back to command-dispatch mode." >>"$LOG_DIR/driver.log"
    kill -TERM "$SOURCE_PID" 2>/dev/null || true
    wait "$SOURCE_PID" 2>/dev/null || true
    SOURCE_PID=""
    EFFECTIVE_MODE="dispatch"
    start_source 1
  else
    echo "wtype failed while sending Ctrl+C." >&2
    cat "$LOG_DIR/wtype.log" >&2 || true
    exit 1
  fi
fi

if ! wait_for_log 'lambda-clipboard-probe: copied role=source' "$LOG_DIR/source.stderr" 8; then
  echo "Source clipboard probe did not publish the expected clipboard text." >&2
  tail -n 120 "$LOG_DIR/source.stderr" >&2 || true
  cat "$LOG_DIR/wtype.log" >&2 2>/dev/null || true
  exit 1
fi

env WAYLAND_DISPLAY="$SOCKET" \
  XDG_RUNTIME_DIR="$RUNTIME_DIR" \
  LAMBDA_CLIPBOARD_PROBE_ROLE=sink \
  LAMBDA_CLIPBOARD_PROBE_EXPECT="$TEXT" \
  LAMBDA_CLIPBOARD_PROBE_TIMEOUT_MS="$PROBE_TIMEOUT_MS" \
  LAMBDA_CLIPBOARD_PROBE_AUTO_PASTE="$([[ "$EFFECTIVE_MODE" == "dispatch" ]] && echo 1 || echo 0)" \
  "$BUILD_DIR/lambda-desktop/tools/lambda-clipboard-probe" >"$LOG_DIR/sink.stdout" 2>"$LOG_DIR/sink.stderr" &
SINK_PID=$!

if ! wait_for_log 'lambda-clipboard-probe: ready role=sink' "$LOG_DIR/sink.stderr" 8; then
  echo "Sink clipboard probe did not become ready." >&2
  tail -n 120 "$LOG_DIR/sink.stderr" >&2 || true
  exit 1
fi

if [[ "$EFFECTIVE_MODE" == "wtype" ]]; then
  sleep 0.4
  run_wtype -M ctrl v -m ctrl || {
    echo "wtype failed while sending Ctrl+V." >&2
    cat "$LOG_DIR/wtype.log" >&2 || true
    exit 1
  }
fi

if ! wait_for_log 'lambda-clipboard-probe: pasted role=sink' "$LOG_DIR/sink.stderr" 8; then
  echo "Sink clipboard probe did not paste the expected text." >&2
  tail -n 120 "$LOG_DIR/sink.stderr" >&2 || true
  cat "$LOG_DIR/wtype.log" >&2 2>/dev/null || true
  exit 1
fi
wait "$SINK_PID"
SINK_PID=""

env WAYLAND_DISPLAY="$SOCKET" \
  XDG_RUNTIME_DIR="$RUNTIME_DIR" \
  SHELL=/bin/cat \
  LAMBDA_TERMINAL_AUTOTEST_EXPECT_TEXT="$TEXT" \
  LAMBDA_TERMINAL_AUTOTEST_TIMEOUT_MS="$TERMINAL_TIMEOUT_MS" \
  LAMBDA_TERMINAL_AUTOTEST_PASTE_CLIPBOARD="$([[ "$EFFECTIVE_MODE" == "dispatch" ]] && echo 1 || echo 0)" \
  "$BUILD_DIR/lambda-desktop/lambda-terminal/lambda-terminal" >"$LOG_DIR/terminal.stdout" 2>"$LOG_DIR/terminal.stderr" &
TERMINAL_PID=$!

if ! wait_for_log 'lambda-terminal-autotest: ready' "$LOG_DIR/terminal.stderr" 8; then
  echo "lambda-terminal did not become ready for clipboard verification." >&2
  tail -n 120 "$LOG_DIR/terminal.stderr" >&2 || true
  exit 1
fi

if [[ "$EFFECTIVE_MODE" == "wtype" ]]; then
  sleep 0.4
  run_wtype -M ctrl -M shift v -m shift -m ctrl || {
    echo "wtype failed while sending Ctrl+Shift+V." >&2
    cat "$LOG_DIR/wtype.log" >&2 || true
    exit 1
  }
fi

if ! wait_for_log 'lambda-terminal-autotest: observed-text' "$LOG_DIR/terminal.stderr" 8; then
  echo "lambda-terminal did not observe the pasted clipboard text." >&2
  tail -n 160 "$LOG_DIR/terminal.stderr" >&2 || true
  cat "$LOG_DIR/wtype.log" >&2 2>/dev/null || true
  exit 1
fi
wait "$TERMINAL_PID"
TERMINAL_PID=""

fatal_pattern='fatal|segmentation|assert|AddressSanitizer|VK_ERROR|present failed|surface lost|lambda-(clipboard-probe|terminal-autotest): timeout'
fatal_count=$( (rg -n -i "$fatal_pattern" \
  "$LOG_DIR" || true) | wc -l )
if [[ "$fatal_count" -ne 0 ]]; then
  echo "SUMMARY flux-clipboard fatal_matches=$fatal_count mode=$EFFECTIVE_MODE log_dir=$LOG_DIR"
  rg -n -i "$fatal_pattern" "$LOG_DIR" >&2 || true
  exit 1
fi

echo "SUMMARY flux-clipboard mode=$EFFECTIVE_MODE text_bytes=${#TEXT} log_dir=$LOG_DIR"
echo "Flux clipboard verification completed."
