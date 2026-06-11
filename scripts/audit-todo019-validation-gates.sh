#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
  cat <<'EOF'
Usage: scripts/audit-todo019-validation-gates.sh

Reports readiness for the remaining TODO-019 validation gates. This does not
perform visual/manual validation; it makes the remaining prerequisites explicit:
real TTY VT switching, uinput-backed input driving, live visual inspection, and
macOS Metal runtime/visual checks.
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

tool_state() {
  local missing=()
  local tool
  for tool in "$@"; do
    if ! command -v "$tool" >/dev/null 2>&1; then
      missing+=("$tool")
    fi
  done
  if [[ "${#missing[@]}" -eq 0 ]]; then
    gate "READY" "linux-input-tools" "$*"
  else
    gate "PENDING" "linux-input-tools" "missing: ${missing[*]}"
  fi
}

tty_name="$(tty || true)"
if [[ -n "$tty_name" && "$tty_name" != "not a tty" ]] && fgconsole >/dev/null 2>&1; then
  gate "READY" "kms-vt-switch" "current_tty=$tty_name; run LWM_VT_SWITCH_TARGET=N scripts/verify-kms-vt-switch.sh"
else
  gate "PENDING" "kms-vt-switch" "requires a real TTY with fgconsole/chvt access; current_tty=${tty_name:-unknown}"
fi

tool_state ydotool wtype evemu-event wayland-info chvt fgconsole

if [[ -e /dev/uinput && -r /dev/uinput && -w /dev/uinput ]]; then
  gate "READY" "uinput" "/dev/uinput is readable and writable"
else
  detail="$(ls -l /dev/uinput 2>/dev/null || echo '/dev/uinput missing')"
  gate "PENDING" "uinput" "$detail"
fi

if command -v sudo >/dev/null 2>&1 && sudo -n true >/dev/null 2>&1; then
  gate "READY" "sudo-noninteractive" "sudo -n true succeeded"
else
  gate "PENDING" "sudo-noninteractive" "sudo -n true did not succeed"
fi

display_file="${XDG_RUNTIME_DIR:-}/lambda-window-manager-display"
if [[ -n "${WAYLAND_DISPLAY:-}" || -r "$display_file" ]]; then
  gate "READY" "live-wayland-session" "run scripts/run-real-app-smoke.sh for manual visual checks"
else
  gate "PENDING" "live-wayland-session" "start lambda-window-manager, then run scripts/run-real-app-smoke.sh"
fi

if [[ "$(uname -s)" == "Darwin" ]]; then
  gate "READY" "macos-metal-runtime" "run scripts/verify-macos-metal-editor-perf.sh and compare backdrop blur visuals"
else
  gate "PENDING" "macos-metal-runtime" "requires macOS; current=$(uname -s)"
fi

if [[ -x "$ROOT/scripts/verify-evemu-hardware-input.sh" ]]; then
  gate "READY" "real-evdev-helper" "run with LWM_EVEMU_DEVICE=/dev/input/eventN"
else
  gate "PENDING" "real-evdev-helper" "missing scripts/verify-evemu-hardware-input.sh"
fi

if [[ -x "$ROOT/scripts/verify-kms-vt-switch.sh" ]]; then
  gate "READY" "vt-helper" "scripts/verify-kms-vt-switch.sh"
else
  gate "PENDING" "vt-helper" "missing scripts/verify-kms-vt-switch.sh"
fi

if [[ -x "$ROOT/scripts/verify-macos-metal-editor-perf.sh" ]]; then
  gate "READY" "macos-perf-helper" "scripts/verify-macos-metal-editor-perf.sh"
else
  gate "PENDING" "macos-perf-helper" "missing scripts/verify-macos-metal-editor-perf.sh"
fi

if [[ "$pending" -eq 0 ]]; then
  echo "SUMMARY todo019-validation-gates ready=1"
else
  echo "SUMMARY todo019-validation-gates ready=0"
fi

exit "$pending"
