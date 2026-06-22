#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${LWM_BUILD_DIR:-$ROOT/build}"
TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/wm-comp27-audit.XXXXXX")"

cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

usage() {
  cat <<EOF
Usage: lambda-desktop/scripts/audit-wm-comp27-validation-gates.sh

Reports readiness for the remaining WM-COMP-27 real-app validation gates.
This does not perform visual validation; it makes the current prerequisites
and manual target-hardware steps explicit.

Environment:
  LWM_BUILD_DIR      Build directory used for Lambda app probes. Default: $BUILD_DIR
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

pending=0

gate() {
  local state="$1"
  local name="$2"
  local detail="$3"
  printf 'GATE %-8s %-32s %s\n' "$state" "$name" "$detail"
  if [[ "$state" == "PENDING" ]]; then
    pending=1
  fi
}

tool_gate() {
  local gate_name="$1"
  shift
  local missing=()
  local tool
  for tool in "$@"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
      missing+=("$tool")
    fi
  done
  if [[ "${#missing[@]}" -eq 0 ]]; then
    gate "READY" "$gate_name" "$*"
  else
    gate "PENDING" "$gate_name" "missing: ${missing[*]}"
  fi
}

lambda_app_gate() {
  local missing=()
  local app
  for app in lambda-window-manager lambda-shell lambda-settings lambda-files lambda-editor lambda-terminal; do
    local path="$BUILD_DIR/lambda-desktop/$app/$app"
    if [[ "$app" == "lambda-window-manager" ]]; then
      path="$BUILD_DIR/lambda-desktop/lambda-window-manager/lambda-window-manager"
    fi
    if [[ ! -x "$path" && ! -x "$BUILD_DIR/$app" ]]; then
      missing+=("$app")
    fi
  done
  if [[ "${#missing[@]}" -eq 0 ]]; then
    gate "READY" "lambda-binaries" "required compositor/Shell/app binaries are present in $BUILD_DIR"
  else
    gate "PENDING" "lambda-binaries" "build missing targets: ${missing[*]}"
  fi
}

tool_gate "real-app-tools" wayland-info wtype ydotool evemu-event
lambda_app_gate

if "$ROOT/lambda-desktop/scripts/run-real-app-smoke.sh" --no-build --probe-only >"$TMP_DIR/probe.out" 2>"$TMP_DIR/probe.err"; then
  gate "READY" "real-app-probe" "all required smoke cases detected; optional Qt may still be unavailable"
else
  gate "PENDING" "real-app-probe" "$(tr '\n' ' ' <"$TMP_DIR/probe.err") $(tr '\n' ' ' <"$TMP_DIR/probe.out")"
fi

display_file="${XDG_RUNTIME_DIR:-}/lambda-window-manager-display"
display=""
if [[ -r "$display_file" ]]; then
  display="$(cat "$display_file")"
fi

if [[ -n "$display" ]]; then
  gate "READY" "live-wayland-session" "lambda-window-manager-display=$display"
  if command -v wayland-info >/dev/null 2>&1; then
    if WAYLAND_DISPLAY="$display" timeout --signal=TERM --kill-after=2s 8s wayland-info >"$TMP_DIR/wayland-info.out" 2>"$TMP_DIR/wayland-info.err"; then
      gate "READY" "live-registry-probe" "wayland-info connected to $display"
    else
      gate "PENDING" "live-registry-probe" "$(tr '\n' ' ' <"$TMP_DIR/wayland-info.err")"
    fi
  else
    gate "PENDING" "live-registry-probe" "wayland-info is not installed"
  fi
elif [[ -n "${WAYLAND_DISPLAY:-}" ]]; then
  gate "PENDING" "live-wayland-session" "WAYLAND_DISPLAY=${WAYLAND_DISPLAY} is set, but $display_file is missing; start lambda-window-manager or run lambda-desktop/scripts/run-real-app-smoke.sh --start-compositor from a real TTY"
  gate "PENDING" "live-registry-probe" "requires the Lambda compositor display file"
else
  gate "PENDING" "live-wayland-session" "start lambda-window-manager or run lambda-desktop/scripts/run-real-app-smoke.sh --start-compositor from a real TTY"
  gate "PENDING" "live-registry-probe" "requires a live lambda-window-manager session"
fi

tty_name="$(tty || true)"
if [[ -n "$tty_name" && "$tty_name" != "not a tty" ]] && command -v fgconsole >/dev/null 2>&1 && fgconsole >/dev/null 2>&1; then
  gate "READY" "target-tty" "current_tty=$tty_name"
else
  gate "PENDING" "target-tty" "requires a real TTY for owned KMS compositor validation; current_tty=${tty_name:-unknown}"
fi

if [[ -e /dev/uinput && -r /dev/uinput && -w /dev/uinput ]]; then
  gate "READY" "uinput-driving" "/dev/uinput is readable and writable"
else
  gate "PENDING" "uinput-driving" "$(ls -l /dev/uinput 2>/dev/null || echo '/dev/uinput missing')"
fi

if command -v sudo >/dev/null 2>&1 && sudo -n true >/dev/null 2>&1; then
  gate "READY" "sudo-noninteractive" "sudo -n true succeeded"
else
  gate "PENDING" "sudo-noninteractive" "sudo -n true did not succeed"
fi

if compgen -G '/dev/input/event*' >/dev/null; then
  gate "READY" "evdev-devices" "$(ls /dev/input/event* | tr '\n' ' ')"
else
  gate "PENDING" "evdev-devices" "/dev/input/event* is not visible"
fi

if [[ -x "$ROOT/lambda-desktop/scripts/run-real-app-smoke.sh" ]]; then
  gate "READY" "real-app-helper" "lambda-desktop/scripts/run-real-app-smoke.sh"
else
  gate "PENDING" "real-app-helper" "missing lambda-desktop/scripts/run-real-app-smoke.sh"
fi

if [[ -x "$ROOT/lambda-desktop/scripts/verify-wm-comp27-matrix.sh" ]]; then
  gate "READY" "matrix-helper" "lambda-desktop/scripts/verify-wm-comp27-matrix.sh"
else
  gate "PENDING" "matrix-helper" "missing lambda-desktop/scripts/verify-wm-comp27-matrix.sh"
fi

gate "PENDING" "manual-visual-checks" \
  "run the real-app matrix on target hardware and inspect cursor shape/responsiveness, resize smoothness, popups/menus, fullscreen restore, screenshots, video/mpv, and long idle behavior"

if [[ "$pending" -eq 0 ]]; then
  echo "SUMMARY wm-comp27-validation-gates ready=1"
else
  echo "SUMMARY wm-comp27-validation-gates ready=0"
fi

exit "$pending"
