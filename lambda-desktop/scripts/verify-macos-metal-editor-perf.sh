#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${LWM_MACOS_BUILD_DIR:-$ROOT/build}"
LOG_ROOT="${LWM_MACOS_EDITOR_PERF_LOG_DIR:-$ROOT/.debug-logs/macos-metal-editor-perf}"
RUN_ID="$(date +%Y%m%d-%H%M%S)"
LOG_DIR="$LOG_ROOT/$RUN_ID"
RUN_SECONDS="${LAMBDA_MACOS_EDITOR_PERF_SECONDS:-8}"
MAX_DRAWABLE_WAIT_P95_MS="${LAMBDA_MACOS_EDITOR_PERF_MAX_DRAWABLE_WAIT_P95_MS:-4}"
MAX_BUDGET_P99_MS="${LAMBDA_MACOS_EDITOR_PERF_MAX_BUDGET_P99_MS:-25}"
MIN_DETAIL_LINES="${LAMBDA_MACOS_EDITOR_PERF_MIN_DETAIL_LINES:-3}"
REQUIRE_ATLAS_GROW="${LAMBDA_MACOS_EDITOR_PERF_REQUIRE_ATLAS_GROW:-1}"

usage() {
  cat <<EOF
Usage: lambda-desktop/scripts/verify-macos-metal-editor-perf.sh

Runs the macOS Metal/editor runtime check for TODO-019:
  - builds lambda-editor
  - generates a large unicode-heavy UTF-8 paste payload
  - drives lambda-editor through its normal edit.paste command path
  - captures LAMBDA_DEBUG_PERF=2 detail rows
  - fails if drawable wait, frame budget p99, atlas growth, or autotest markers are missing/out of budget

Environment:
  LWM_MACOS_BUILD_DIR                           Build directory. Default: $BUILD_DIR
  LWM_MACOS_EDITOR_PERF_LOG_DIR                 Log root. Default: $LOG_ROOT
  LAMBDA_MACOS_EDITOR_PERF_SECONDS              Seconds to render after paste. Default: $RUN_SECONDS
  LAMBDA_MACOS_EDITOR_PERF_MAX_DRAWABLE_WAIT_P95_MS  Default: $MAX_DRAWABLE_WAIT_P95_MS
  LAMBDA_MACOS_EDITOR_PERF_MAX_BUDGET_P99_MS    Default: $MAX_BUDGET_P99_MS
  LAMBDA_MACOS_EDITOR_PERF_MIN_DETAIL_LINES     Default: $MIN_DETAIL_LINES
  LAMBDA_MACOS_EDITOR_PERF_REQUIRE_ATLAS_GROW   Require atlasGrow perf evidence. Default: $REQUIRE_ATLAS_GROW
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "macOS is required for Metal editor perf verification." >&2
  exit 2
fi

if ! [[ "$RUN_SECONDS" =~ ^[0-9]+$ ]] || [[ "$RUN_SECONDS" -le 0 ]]; then
  echo "LAMBDA_MACOS_EDITOR_PERF_SECONDS must be a positive integer." >&2
  exit 2
fi

command -v cmake >/dev/null 2>&1 || {
  echo "cmake is required." >&2
  exit 2
}
command -v awk >/dev/null 2>&1 || {
  echo "awk is required." >&2
  exit 2
}
command -v sort >/dev/null 2>&1 || {
  echo "sort is required." >&2
  exit 2
}

mkdir -p "$LOG_DIR"

TEXT_FILE="$LOG_DIR/unicode-heavy-paste.txt"
APP_LOG="$LOG_DIR/lambda-editor.log"
DRAWABLE_VALUES="$LOG_DIR/drawable-wait-ms.txt"
BUDGET_VALUES="$LOG_DIR/budget-p99-ms.txt"
ATLAS_VALUES="$LOG_DIR/atlas-grow-count.txt"

if command -v python3 >/dev/null 2>&1; then
  python3 - "$TEXT_FILE" <<'PY'
import sys

path = sys.argv[1]
samples = [
    [0x03BB, 0x03C0, 0x03A9, 0x0394, 0x03B2, 0x03B3],
    [0x05D0, 0x05D1, 0x05D2, 0x05E9, 0x05EA],
    [0x0627, 0x0644, 0x0633, 0x0644, 0x0627, 0x0645],
    [0x0905, 0x0915, 0x0937, 0x0930, 0x092E],
    [0x3042, 0x30A2, 0x30AB, 0x65E5, 0x672C],
    [0xAC00, 0xB098, 0xB2E4, 0xD55C, 0xAE00],
]
with open(path, "w", encoding="utf-8") as f:
    for i in range(1400):
        cjk = "".join(chr(0x4E00 + ((i * 13 + j * 29) % 2400)) for j in range(8))
        symbols = "".join(chr(cp) for cp in samples[i % len(samples)])
        emoji = chr(0x1F600 + (i % 80))
        f.write(f"{i:05d} Lambda editor Metal paste perf {symbols} {cjk} {emoji} frame pacing atlas drawable wait\\n")
PY
elif command -v perl >/dev/null 2>&1; then
  perl -CS -e '
    my $path = shift;
    open(my $fh, ">:encoding(UTF-8)", $path) or die "open $path: $!";
    my @samples = (
      [0x03BB,0x03C0,0x03A9,0x0394,0x03B2,0x03B3],
      [0x05D0,0x05D1,0x05D2,0x05E9,0x05EA],
      [0x0627,0x0644,0x0633,0x0644,0x0627,0x0645],
      [0x0905,0x0915,0x0937,0x0930,0x092E],
      [0x3042,0x30A2,0x30AB,0x65E5,0x672C],
      [0xAC00,0xB098,0xB2E4,0xD55C,0xAE00],
    );
    for my $i (0..1399) {
      my $cjk = join("", map { chr(0x4E00 + (($i * 13 + $_ * 29) % 2400)) } 0..7);
      my $symbols = join("", map { chr($_) } @{$samples[$i % @samples]});
      my $emoji = chr(0x1F600 + ($i % 80));
      printf $fh "%05d Lambda editor Metal paste perf %s %s %s frame pacing atlas drawable wait\n",
        $i, $symbols, $cjk, $emoji;
    }
  ' "$TEXT_FILE"
else
  echo "python3 or perl is required to generate the UTF-8 paste payload." >&2
  exit 2
fi

cmake --build "$BUILD_DIR" --target lambda-editor -j"$(sysctl -n hw.ncpu)"

EDITOR="$BUILD_DIR/lambda-desktop/lambda-editor/lambda-editor"
if [[ ! -x "$EDITOR" ]]; then
  echo "Missing executable: $EDITOR" >&2
  exit 2
fi

set +e
env \
  LAMBDA_DEBUG_PERF=2 \
  LAMBDA_EDITOR_AUTOTEST_PASTE_FILE="$TEXT_FILE" \
  LAMBDA_EDITOR_AUTOTEST_EXIT_AFTER_PASTE_SECONDS="$RUN_SECONDS" \
  "$EDITOR" >"$APP_LOG" 2>&1 &
editor_pid=$!

max_wait=$((RUN_SECONDS + 20))
for _ in $(seq 1 "$max_wait"); do
  if ! kill -0 "$editor_pid" 2>/dev/null; then
    break
  fi
  sleep 1
done

if kill -0 "$editor_pid" 2>/dev/null; then
  kill -TERM "$editor_pid" 2>/dev/null || true
  sleep 1
  kill -KILL "$editor_pid" 2>/dev/null || true
  wait "$editor_pid" 2>/dev/null
  editor_status=124
else
  wait "$editor_pid"
  editor_status=$?
fi
set -e

if [[ "$editor_status" -ne 0 ]]; then
  echo "SUMMARY macos-metal-editor-perf editor_status=$editor_status log_dir=$LOG_DIR"
  tail -n 120 "$APP_LOG" >&2 || true
  exit "$editor_status"
fi

sed -n 's/.*drawableWait=\([0-9.][0-9.]*\)ms.*/\1/p' "$APP_LOG" >"$DRAWABLE_VALUES"
sed -n 's/.*budget\[p50\/p99\]=[0-9.][0-9.]*\/\([0-9.][0-9.]*\)ms.*/\1/p' "$APP_LOG" >"$BUDGET_VALUES"
sed -n 's/.*atlasGrow=\([0-9][0-9]*\).*/\1/p' "$APP_LOG" >"$ATLAS_VALUES"

percentile_from_file() {
  local file="$1"
  local percentile="$2"
  local count
  count="$(awk 'NF { n += 1 } END { print n + 0 }' "$file")"
  if [[ "$count" -le 0 ]]; then
    printf "0.000"
    return
  fi
  local target
  target=$(( (count * percentile + 99) / 100 ))
  sort -n "$file" | awk -v target="$target" 'NR == target { printf "%.3f", $1; found = 1 } END { if (!found) printf "0.000" }'
}

max_from_file() {
  local file="$1"
  awk 'NF && $1 + 0 > max { max = $1 + 0 } END { printf "%.3f", max + 0 }' "$file"
}

count_nonempty() {
  awk 'NF { n += 1 } END { print n + 0 }' "$1"
}

float_gt() {
  awk -v a="$1" -v b="$2" 'BEGIN { exit !(a > b) }'
}

detail_lines="$(grep -c '\[lambda:perf:detail\]' "$APP_LOG" || true)"
paste_count="$(grep -c 'lambda-editor-autotest: pasted' "$APP_LOG" || true)"
complete_count="$(grep -c 'lambda-editor-autotest: complete' "$APP_LOG" || true)"
drawable_samples="$(count_nonempty "$DRAWABLE_VALUES")"
budget_samples="$(count_nonempty "$BUDGET_VALUES")"
atlas_grow_total="$(awk 'NF { total += $1 + 0 } END { print total + 0 }' "$ATLAS_VALUES")"
drawable_p95="$(percentile_from_file "$DRAWABLE_VALUES" 95)"
drawable_max="$(max_from_file "$DRAWABLE_VALUES")"
budget_p99_max="$(max_from_file "$BUDGET_VALUES")"

printf 'SUMMARY macos-metal-editor-perf detail_lines=%s paste=%s complete=%s drawable_samples=%s drawable_wait_p95_ms=%s drawable_wait_max_ms=%s budget_samples=%s budget_p99_max_ms=%s atlas_grow_total=%s require_atlas_grow=%s max_drawable_wait_p95_ms=%s max_budget_p99_ms=%s log_dir=%s\n' \
  "$detail_lines" "$paste_count" "$complete_count" "$drawable_samples" "$drawable_p95" "$drawable_max" \
  "$budget_samples" "$budget_p99_max" "$atlas_grow_total" "$REQUIRE_ATLAS_GROW" \
  "$MAX_DRAWABLE_WAIT_P95_MS" "$MAX_BUDGET_P99_MS" "$LOG_DIR"

failed=0
if [[ "$paste_count" -lt 1 || "$complete_count" -lt 1 ]]; then
  failed=1
fi
if [[ "$detail_lines" -lt "$MIN_DETAIL_LINES" || "$drawable_samples" -lt "$MIN_DETAIL_LINES" ||
      "$budget_samples" -lt "$MIN_DETAIL_LINES" ]]; then
  failed=1
fi
if [[ "$REQUIRE_ATLAS_GROW" != "0" && "$atlas_grow_total" -lt 1 ]]; then
  failed=1
fi
if float_gt "$drawable_p95" "$MAX_DRAWABLE_WAIT_P95_MS"; then
  failed=1
fi
if float_gt "$budget_p99_max" "$MAX_BUDGET_P99_MS"; then
  failed=1
fi

if [[ "$failed" -ne 0 ]]; then
  echo "macOS Metal editor perf verification failed." >&2
  tail -n 160 "$APP_LOG" >&2 || true
  exit 1
fi

echo "CASE macos-metal-editor-perf log_dir=$LOG_DIR"
echo "macOS Metal editor perf verification completed."
