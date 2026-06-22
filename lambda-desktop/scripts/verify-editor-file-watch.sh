#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${LAMBDA_EDITOR_FILE_WATCH_BUILD_DIR:-$ROOT/build}"
LOG_ROOT="${LAMBDA_EDITOR_FILE_WATCH_LOG_DIR:-$ROOT/.debug-logs/editor-file-watch}"
RUN_ID="$(date +%Y%m%d-%H%M%S)"
LOG_DIR="$LOG_ROOT/$RUN_ID"
TIMEOUT_SECONDS="${LAMBDA_EDITOR_FILE_WATCH_TIMEOUT_SECONDS:-18}"

usage() {
  cat <<EOF
Usage: lambda-desktop/scripts/verify-editor-file-watch.sh

Runs the TODO-013 editor file-watch verification under headless Weston GL:
  - builds lambda-editor
  - opens a real file in lambda-editor
  - mutates the file outside the editor process
  - asserts the external-change prompt appears
  - accepts reload through the same command path as the toast action
  - verifies text reload plus caret and scroll preservation
  - removes a second watched file and verifies the missing-file prompt/dismiss path

Environment:
  LAMBDA_EDITOR_FILE_WATCH_BUILD_DIR       Build dir. Default: $BUILD_DIR
  LAMBDA_EDITOR_FILE_WATCH_LOG_DIR         Log root. Default: $LOG_ROOT
  LAMBDA_EDITOR_FILE_WATCH_TIMEOUT_SECONDS App timeout. Default: $TIMEOUT_SECONDS
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

command -v weston >/dev/null 2>&1 || {
  echo "weston is required for editor file-watch verification." >&2
  exit 2
}

command -v timeout >/dev/null 2>&1 || {
  echo "timeout is required for editor file-watch verification." >&2
  exit 2
}

mkdir -p "$LOG_DIR"
cmake --build "$BUILD_DIR" --target lambda-editor -j"$(nproc)"

EDITOR="$BUILD_DIR/lambda-desktop/lambda-editor/lambda-editor"
if [[ ! -x "$EDITOR" ]]; then
  echo "Missing executable: $EDITOR" >&2
  exit 2
fi

RUNTIME_BASE="${LAMBDA_EDITOR_FILE_WATCH_RUNTIME_BASE:-${XDG_RUNTIME_DIR:-/tmp}}"
if ! RUNTIME_DIR="$(mktemp -d "$RUNTIME_BASE/lambda-editor-file-watch-runtime.XXXXXX" 2>/dev/null)"; then
  RUNTIME_BASE="/tmp"
  RUNTIME_DIR="$(mktemp -d "$RUNTIME_BASE/lambda-editor-file-watch-runtime.XXXXXX")"
fi
chmod 700 "$RUNTIME_DIR"
SOCKET="lambda-editor-file-watch-$RUN_ID"
WESTON_PID=""

cleanup() {
  set +e
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

wait_for_socket() {
  for _ in $(seq 1 100); do
    if [[ -S "$RUNTIME_DIR/$SOCKET" ]]; then
      return 0
    fi
    if ! kill -0 "$WESTON_PID" 2>/dev/null; then
      echo "Weston exited before creating $SOCKET" >&2
      tail -n 120 "$LOG_DIR/weston.log" >&2 || true
      return 1
    fi
    sleep 0.1
  done
  echo "Weston socket $SOCKET was not created." >&2
  tail -n 120 "$LOG_DIR/weston.log" >&2 || true
  return 1
}

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

write_lines() {
  local path="$1"
  local count="$2"
  local prefix="$3"
  for i in $(seq 1 "$count"); do
    printf '%s line %03d editor file-watch validation payload\n' "$prefix" "$i"
  done >"$path"
}

run_editor_case() {
  local mode="$1"
  local input_file="$LOG_DIR/watch-$mode-input.txt"
  local replacement_file="$LOG_DIR/watch-$mode-replacement.txt"
  local app_log="$LOG_DIR/lambda-editor-$mode.log"

  write_lines "$input_file" 140 "initial $mode"
  write_lines "$replacement_file" 150 "replacement $mode"

  set +e
  WAYLAND_DISPLAY="$SOCKET" \
    XDG_RUNTIME_DIR="$RUNTIME_DIR" \
    LAMBDA_EDITOR_AUTOTEST_FILE_WATCH=1 \
    LAMBDA_EDITOR_AUTOTEST_FILE_WATCH_MODE="$mode" \
    LAMBDA_EDITOR_AUTOTEST_FILE_WATCH_TEXT_FILE="$replacement_file" \
    timeout --signal=TERM --kill-after=2s "${TIMEOUT_SECONDS}s" \
    "$EDITOR" "$input_file" >"$app_log" 2>&1
  local editor_status=$?
  set -e

  if [[ "$editor_status" -ne 0 ]]; then
    echo "SUMMARY editor-file-watch mode=$mode editor_status=$editor_status log_dir=$LOG_DIR"
    tail -n 160 "$app_log" >&2 || true
    exit 1
  fi
}

run_editor_case modified
run_editor_case missing

fatal_count=$( (rg -n -i 'fatal|segmentation|assert|AddressSanitizer|VK_ERROR|present failed|surface lost|render error' \
  "$LOG_DIR"/lambda-editor-*.log "$LOG_DIR/weston.log" || true) | wc -l )
prompt_count="$(grep -h -c 'lambda-editor-watch-autotest: prompt-observed' "$LOG_DIR"/lambda-editor-*.log 2>/dev/null | awk '{ total += $1 } END { print total + 0 }')"
reload_count="$(grep -h -c 'lambda-editor-watch: reloaded' "$LOG_DIR"/lambda-editor-*.log 2>/dev/null | awk '{ total += $1 } END { print total + 0 }')"
verified_count="$(grep -h -c 'lambda-editor-watch-autotest: verified' "$LOG_DIR"/lambda-editor-*.log 2>/dev/null | awk '{ total += $1 } END { print total + 0 }')"
missing_prompt_count="$(grep -h -c 'lambda-editor-watch-autotest: missing-prompt-observed' "$LOG_DIR"/lambda-editor-*.log 2>/dev/null | awk '{ total += $1 } END { print total + 0 }')"
missing_verified_count="$(grep -h -c 'lambda-editor-watch-autotest: missing-verified' "$LOG_DIR"/lambda-editor-*.log 2>/dev/null | awk '{ total += $1 } END { print total + 0 }')"

printf 'SUMMARY editor-file-watch prompt=%s reload=%s verified=%s missing_prompt=%s missing_verified=%s fatal_matches=%s log_dir=%s\n' \
  "$prompt_count" "$reload_count" "$verified_count" "$missing_prompt_count" "$missing_verified_count" \
  "$fatal_count" "$LOG_DIR"

if [[ "$fatal_count" -ne 0 || "$prompt_count" -lt 1 || "$reload_count" -lt 1 ||
      "$verified_count" -lt 1 || "$missing_prompt_count" -lt 1 || "$missing_verified_count" -lt 1 ]]; then
  rg -n -i 'lambda-editor-watch|fatal|segmentation|assert|AddressSanitizer|VK_ERROR|present failed|surface lost|render error' \
    "$LOG_DIR"/lambda-editor-*.log "$LOG_DIR/weston.log" >&2 || true
  exit 1
fi

echo "Editor file-watch verification completed."
