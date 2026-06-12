#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WAYLAND_BUILD_DIR="${LWM_WAYLAND_BUILD_DIR:-$ROOT/build}"
KMS_BUILD_DIR="${LWM_KMS_BUILD_DIR:-$ROOT/build-kms}"
LOG_ROOT="${LWM_EVEMU_HARDWARE_INPUT_LOG_DIR:-$ROOT/.debug-logs/evemu-hardware-input}"
RUN_ID="$(date +%Y%m%d-%H%M%S)"
LOG_DIR="$LOG_ROOT/$RUN_ID"
DEVICE="${LWM_EVEMU_DEVICE:-}"
EVENT_COUNT="${LWM_EVEMU_EVENT_COUNT:-60}"
EVENT_INTERVAL_SECONDS="${LWM_EVEMU_EVENT_INTERVAL_SECONDS:-0.008}"
WARMUP_SECONDS="${LWM_EVEMU_WARMUP_SECONDS:-1.5}"
CHECK_TIMEOUT_MS="${LAMBDA_PRESENTATION_FEEDBACK_TIMEOUT_MS:-8000}"
CHECK_HOLD_MS="${LAMBDA_PRESENTATION_FEEDBACK_HOLD_MS:-4500}"

usage() {
  cat <<EOF
Usage: LWM_EVEMU_DEVICE=/dev/input/eventX scripts/verify-evemu-hardware-input.sh

Runs a focused hardware-input check against the KMS compositor:
  - builds lambda-window-manager and lambda-presentation-feedback-check
  - starts lambda-window-manager with CPU, KMS, and pacing traces
  - holds a presentation-feedback client surface
  - injects small relative pointer motion into the explicitly selected evdev device
  - asserts libinput raw pointer motion and the hardware-cursor fast path were observed

Environment:
  LWM_EVEMU_DEVICE                  Required evdev pointer device, e.g. /dev/input/event6
  LWM_EVEMU_EVENT_COUNT             Relative events to inject. Default: $EVENT_COUNT
  LWM_EVEMU_EVENT_INTERVAL_SECONDS  Sleep between events. Default: $EVENT_INTERVAL_SECONDS
  LWM_EVEMU_WARMUP_SECONDS          Delay before injection. Default: $WARMUP_SECONDS
  LWM_EVEMU_HARDWARE_INPUT_LOG_DIR  Log root. Default: $LOG_ROOT
  LWM_WAYLAND_BUILD_DIR             Wayland build dir. Default: $WAYLAND_BUILD_DIR
  LWM_KMS_BUILD_DIR                 KMS build dir. Default: $KMS_BUILD_DIR
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

if [[ -z "$DEVICE" ]]; then
  usage >&2
  echo "LWM_EVEMU_DEVICE is required; refusing to guess a hardware input device." >&2
  exit 2
fi
if [[ ! -e "$DEVICE" || ! -c "$DEVICE" ]]; then
  echo "Input device does not exist or is not a character device: $DEVICE" >&2
  exit 2
fi
if [[ ! -w "$DEVICE" ]]; then
  echo "Input device is not writable by this process: $DEVICE" >&2
  exit 2
fi
if ! [[ "$EVENT_COUNT" =~ ^[0-9]+$ ]] || [[ "$EVENT_COUNT" -lt 4 ]]; then
  echo "LWM_EVEMU_EVENT_COUNT must be an integer >= 4." >&2
  exit 2
fi

command -v evemu-event >/dev/null 2>&1 || {
  echo "evemu-event is required for hardware-input verification." >&2
  exit 2
}
command -v rg >/dev/null 2>&1 || {
  echo "ripgrep is required for log validation." >&2
  exit 2
}

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

device_event="${DEVICE##*/}"
device_name="$(cat "/sys/class/input/$device_event/device/name" 2>/dev/null || echo unknown)"
printf 'device=%s name=%s\n' "$DEVICE" "$device_name" >"$LOG_DIR/evemu.log"

display_file="$XDG_RUNTIME_DIR/lambda-window-manager-display"
rm -f "$display_file"

wm_pid=""
checker_pid=""
cleanup() {
  set +e
  if [[ -n "$checker_pid" ]]; then
    kill -TERM -"$checker_pid" 2>/dev/null || kill -TERM "$checker_pid" 2>/dev/null || true
  fi
  if [[ -n "$wm_pid" ]]; then
    kill -TERM -"$wm_pid" 2>/dev/null || kill -TERM "$wm_pid" 2>/dev/null || true
  fi
  sleep 1
  if [[ -n "$checker_pid" ]]; then
    kill -KILL -"$checker_pid" 2>/dev/null || kill -KILL "$checker_pid" 2>/dev/null || true
  fi
  if [[ -n "$wm_pid" ]]; then
    kill -KILL -"$wm_pid" 2>/dev/null || kill -KILL "$wm_pid" 2>/dev/null || true
  fi
  rm -f "$display_file"
  set -e
}
trap cleanup EXIT

setsid timeout --signal=TERM --kill-after=5s 25s env \
  LAMBDA_WINDOW_MANAGER_CPU_TRACE=1 \
  LAMBDA_WINDOW_MANAGER_CPU_TRACE_LOG="$LOG_DIR/cpu.log" \
  LAMBDA_WINDOW_MANAGER_SAMPLE_TRACE=0 \
  LAMBDA_KMS_PRESENT_TRACE=1 \
  LAMBDA_WINDOW_MANAGER_PACING_TRACE=1 \
  LAMBDA_WINDOW_MANAGER_PACING_TRACE_LOG="$LOG_DIR/pacing.log" \
  LAMBDA_COMPOSITOR_ENABLE_HARDWARE_CURSOR_MOTION_FAST_PATH=1 \
  LAMBDA_DEBUG_KMS=1 \
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

setsid timeout --signal=TERM --kill-after=2s 12s env \
  WAYLAND_DISPLAY="$display" \
  LAMBDA_PRESENTATION_FEEDBACK_TIMEOUT_MS="$CHECK_TIMEOUT_MS" \
  LAMBDA_PRESENTATION_FEEDBACK_REQUIRE_HARDWARE_FLAGS=1 \
  LAMBDA_PRESENTATION_FEEDBACK_HOLD_MS="$CHECK_HOLD_MS" \
  "$CHECKER" >"$LOG_DIR/presentation-feedback.log" 2>&1 &
checker_pid=$!

sleep "$WARMUP_SECONDS"

for index in $(seq 1 "$EVENT_COUNT"); do
  code="REL_X"
  value=4
  if (( index % 2 == 0 )); then
    code="REL_Y"
    value=3
  fi
  if (( (index / 12) % 2 == 1 )); then
    value=$((-value))
  fi
  printf 'evemu command index=%d code=%s value=%d\n' "$index" "$code" "$value" >>"$LOG_DIR/evemu.log"
  evemu-event --sync "$DEVICE" --type EV_REL --code "$code" --value "$value" >>"$LOG_DIR/evemu.log" 2>&1
  sleep "$EVENT_INTERVAL_SECONDS"
done

checker_status=0
if ! wait "$checker_pid"; then
  checker_status=$?
fi
checker_pid=""
sleep 1

fatal_count=$( (rg -n -i 'fatal|segmentation|assert|terminate called|AddressSanitizer|exception|aborted|input event handling failed|hardware cursor motion fast path failed' "$LOG_DIR" || true) | wc -l )
evemu_commands=$(rg -c '^evemu command' "$LOG_DIR/evemu.log" 2>/dev/null || echo 0)
raw_pointer=$(rg -c '\[lambda:kms:input\] raw pointer motion' "$LOG_DIR/compositor.log" 2>/dev/null || echo 0)
pointer_devices=$(rg -c '\[lambda:kms:input\] device added: .*pointer=1' "$LOG_DIR/compositor.log" 2>/dev/null || echo 0)
fast_path=$(rg -c 'hardware-cursor-motion-fast-path moved=1' "$LOG_DIR/pacing.log" 2>/dev/null || echo 0)
fallback=$(rg -c 'hardware-cursor-motion-fast-path moved=0|hardware-cursor-motion-fast-path unavailable' "$LOG_DIR/pacing.log" 2>/dev/null || echo 0)
presentation=$(rg -c 'presented clock=.*VSYNC.*HW_CLOCK.*HW_COMPLETION' "$LOG_DIR/presentation-feedback.log" 2>/dev/null || echo 0)

printf 'SUMMARY evemu-hardware-input device=%s name="%s" evemu_commands=%s pointer_devices=%s raw_pointer_logs=%s hardware_cursor_fast_path=%s fallback_or_unavailable=%s presentation_hw=%s checker_status=%s fatal_matches=%s log_dir=%s\n' \
  "$DEVICE" "$device_name" "$evemu_commands" "$pointer_devices" "$raw_pointer" "$fast_path" "$fallback" "$presentation" "$checker_status" "$fatal_count" "$LOG_DIR"

minimum_moves=$((EVENT_COUNT / 2))
if [[ "$fatal_count" -ne 0 || "$checker_status" -ne 0 || "$evemu_commands" -ne "$EVENT_COUNT" ||
      "$pointer_devices" -lt 1 || "$raw_pointer" -lt "$minimum_moves" ||
      "$fast_path" -lt "$minimum_moves" ||
      "$fallback" -ne 0 || "$presentation" -lt 1 ]]; then
  echo "Hardware evdev input verification failed." >&2
  tail -n 120 "$LOG_DIR/compositor.log" >&2 || true
  exit 1
fi

echo "CASE evemu-hardware-input display=$display log_dir=$LOG_DIR"
echo "Hardware evdev input verification completed."
