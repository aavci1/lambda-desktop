#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
WAYLAND_BUILD_DIR="${LWM_WAYLAND_BUILD_DIR:-$ROOT/build}"
KMS_BUILD_DIR="${LWM_KMS_BUILD_DIR:-$ROOT/build-kms}"
LOG_ROOT="${LWM_FRAME_PACING_LOG_DIR:-$ROOT/.debug-logs/frame-pacing-verify}"
RUN_ID="$(date +%Y%m%d-%H%M%S)"
LOG_DIR="$LOG_ROOT/$RUN_ID"
TERMINAL_SECONDS="${LAMBDA_TERMINAL_TEST_SECONDS:-18}"
EDITOR_SECONDS="${LAMBDA_EDITOR_SMOKE_SECONDS:-10}"
CHECK_TIMEOUT_MS="${LAMBDA_PRESENTATION_FEEDBACK_TIMEOUT_MS:-8000}"

usage() {
  cat <<EOF
Usage: lambda-desktop/scripts/verify-frame-pacing-linux.sh

Runs the TODO-019 Linux frame-pacing verification smoke:
  - builds the KMS compositor and Wayland verifier/client targets
  - starts lambda-window-manager with CPU, KMS, and pacing traces
  - verifies wp_presentation feedback on atomic-KMS and Vulkan-display presenters
  - runs lambda-shell, a scripted lambda-terminal workload, and lambda-editor
  - drives synthetic pointer motion through the KMS raw-input path with the hardware-cursor fast path enabled
  - overlaps synthetic hardware-cursor motion with the scripted terminal workload
  - exercises a static SHM client surface and asserts compositor surface draw-cache reuse
  - holds ten decorated static SHM client windows and asserts snapshot/surface phases stay cheap
  - moves two decorated checker windows and asserts multi-rect partial damage stays cheap
  - compares a glass-titlebar terminal workload against a forced full-output baseline
  - forces the offscreen scanout-copy path and asserts partial damage copies fewer pixels than full-frame copies
  - alternates partial and forced-full damage while checking captured frames for stale exposed pixels
  - drives server-side chrome hover/press transitions and asserts client surface-cache reuse
  - drives scripted resize configures and asserts the compositor sizing path stays healthy
  - captures compositor resize-storm frames and snap CSV metrics for artifact inspection
  - summarizes CPU/pacing/present-detail logs and fails on fatal runtime errors

Environment:
  LWM_WAYLAND_BUILD_DIR                 Wayland build dir. Default: $WAYLAND_BUILD_DIR
  LWM_KMS_BUILD_DIR                     KMS build dir. Default: $KMS_BUILD_DIR
  LWM_FRAME_PACING_LOG_DIR              Log root. Default: $LOG_ROOT
  LWM_FRAME_PACING_CASES                Comma-separated case names, or "all". Default: all
  LAMBDA_TERMINAL_TEST_SECONDS          Terminal workload duration. Default: $TERMINAL_SECONDS
  LAMBDA_EDITOR_SMOKE_SECONDS           Editor smoke duration. Default: $EDITOR_SECONDS
  LAMBDA_PRESENTATION_FEEDBACK_TIMEOUT_MS Presentation checker timeout. Default: $CHECK_TIMEOUT_MS
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

mkdir -p "$LOG_DIR"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"

build_target() {
  local dir="$1"
  shift
  cmake --build "$dir" --target "$@" -j"$(nproc)"
}

build_target "$KMS_BUILD_DIR" lambda-window-manager
build_target "$WAYLAND_BUILD_DIR" lambda-presentation-feedback-check lambda-frame-artifact-check lambda-shell lambda-terminal lambda-editor

WM="$KMS_BUILD_DIR/lambda-desktop/lambda-window-manager/lambda-window-manager"
CHECKER="$WAYLAND_BUILD_DIR/lambda-desktop/tools/lambda-presentation-feedback-check"
ARTIFACT_CHECK="$WAYLAND_BUILD_DIR/lambda-desktop/tools/lambda-frame-artifact-check"
SHELL_APP="$WAYLAND_BUILD_DIR/lambda-desktop/lambda-shell/lambda-shell"
EDITOR_APP="$WAYLAND_BUILD_DIR/lambda-desktop/lambda-editor/lambda-editor"

for binary in "$WM" "$CHECKER" "$ARTIFACT_CHECK" "$SHELL_APP" "$EDITOR_APP"; do
  if [[ ! -x "$binary" ]]; then
    echo "Missing executable: $binary" >&2
    exit 2
  fi
done

build_malloc_counter() {
  local output="$1"
  "${CC:-cc}" -shared -fPIC "$ROOT/lambda-desktop/tools/malloc-count-preload.c" -ldl -o "$output"
}

summarize_runtime_logs() {
  local dir="$1"
  local label="$2"
  local fatal_count
  fatal_count=$( (rg -n -i 'fatal|segmentation|assert|terminate called|AddressSanitizer|exception|aborted|input event handling failed|hardware cursor motion fast path failed' "$dir" || true) | wc -l )
  local cpu_lines
  cpu_lines=$(rg -c '^cpu-trace:' "$dir/cpu.log" 2>/dev/null || echo 0)
  local kms_lines
  kms_lines=$(rg -c '^kms-trace:' "$dir/compositor.log" 2>/dev/null || echo 0)
  local pacing_lines
  pacing_lines=$(rg -c '^pacing-trace:' "$dir/compositor.log" 2>/dev/null || echo 0)
  local terminal_present_lines
  terminal_present_lines=$(rg -c 'vulkan-present-detail:' "$dir/lambda-terminal-render.log" 2>/dev/null || echo 0)
  printf 'SUMMARY %s fatal_matches=%s cpu_trace_lines=%s kms_trace_lines=%s pacing_lines=%s terminal_present_lines=%s\n' \
    "$label" "$fatal_count" "$cpu_lines" "$kms_lines" "$pacing_lines" "$terminal_present_lines"
  if [[ "$fatal_count" -ne 0 ]]; then
    return 1
  fi
}

summarize_pointer_fast_path() {
  local dir="$1"
  local label="$2"
  local synthetic_events
  synthetic_events=$(rg -c 'synthetic-pointer-motion emitted=' "$dir/pacing.log" 2>/dev/null || echo 0)
  local fast_path_moves
  fast_path_moves=$(rg -c 'hardware-cursor-motion-fast-path moved=1' "$dir/pacing.log" 2>/dev/null || echo 0)
  local fallback_moves
  fallback_moves=$(rg -c 'hardware-cursor-motion-fast-path moved=0|hardware-cursor-motion-fast-path unavailable' "$dir/pacing.log" 2>/dev/null || echo 0)
  local runtime_failures
  runtime_failures=$( (rg -n -i 'input event handling failed|hardware cursor motion fast path failed' "$dir/compositor.log" || true) | wc -l )
  local input_render_loops
  input_render_loops=$(awk '
    /synthetic-pointer-motion emitted=/ { seen_pointer = 1 }
    seen_pointer && /^pacing-trace:/ && / loop / && /input=1/ && /render=1/ { loops += 1 }
    END { print loops + 0 }
  ' "$dir/pacing.log" 2>/dev/null || echo 0)
  local static_surface_flips
  static_surface_flips=$(rg -c 'flip-scheduled .*surfaces=[1-9][0-9]*' "$dir/pacing.log" 2>/dev/null || echo 0)
  printf 'SUMMARY %s synthetic_pointer_events=%s hardware_cursor_fast_path_moves=%s fallback_or_unavailable_moves=%s runtime_failures=%s input_render_loops_after_pointer=%s static_surface_flips=%s\n' \
    "$label" "$synthetic_events" "$fast_path_moves" "$fallback_moves" "$runtime_failures" "$input_render_loops" "$static_surface_flips"
  if [[ "$synthetic_events" -le 0 || "$fast_path_moves" -lt "$synthetic_events" ||
        "$fallback_moves" -ne 0 || "$runtime_failures" -ne 0 || "$input_render_loops" -ne 0 ||
        "$static_surface_flips" -le 0 ]]; then
    return 1
  fi
}

summarize_pointer_terminal_overlap() {
  local dir="$1"
  local label="$2"
  local timing
  timing="$(awk '
    FILENAME ~ /pacing\.log$/ && /synthetic-pointer-motion emitted=/ {
      t = $2
      sub(/ms$/, "", t)
      if (pointer_first == 0 || t + 0.0 < pointer_first) pointer_first = t + 0.0
      if (t + 0.0 > pointer_last) pointer_last = t + 0.0
      pointer_events += 1
    }
    FILENAME ~ /lambda-terminal-render\.log$/ && /vulkan-present-detail:/ {
      t = $2
      sub(/ms$/, "", t)
      if (terminal_first == 0 || t + 0.0 < terminal_first) terminal_first = t + 0.0
      if (t + 0.0 > terminal_last) terminal_last = t + 0.0
      terminal_frames += 1
    }
    END {
      overlap = pointer_events > 0 && terminal_frames > 0 && pointer_first <= terminal_last && pointer_last >= terminal_first
      overlap_ms = 0.0
      if (overlap) {
        start = pointer_first > terminal_first ? pointer_first : terminal_first
        end = pointer_last < terminal_last ? pointer_last : terminal_last
        overlap_ms = end - start
      }
      printf "pointer_events=%d pointer_first=%.3f pointer_last=%.3f terminal_frames=%d terminal_first=%.3f terminal_last=%.3f overlap_ms=%.3f",
        pointer_events, pointer_first, pointer_last, terminal_frames, terminal_first, terminal_last, overlap_ms
      if (!overlap || overlap_ms <= 0.0) exit 1
    }
  ' "$dir/pacing.log" "$dir/lambda-terminal-render.log")" || {
    printf 'SUMMARY %s pointer_terminal_overlap_failed\n' "$label"
    return 1
  }
  printf 'SUMMARY %s pointer_terminal_overlap %s\n' "$label" "$timing"
}

summarize_cursor_primary_interleave() {
  local dir="$1"
  local label="$2"
  local accepted_interleaved
  accepted_interleaved=$(rg -c 'hardware-cursor-motion-fast-path moved=1 pendingFlip=1 deferred=0' "$dir/pacing.log" 2>/dev/null || echo 0)
  local deferred_interleaved
  deferred_interleaved=$(rg -c 'hardware-cursor-motion-fast-path moved=1 pendingFlip=1 deferred=1' "$dir/pacing.log" 2>/dev/null || echo 0)
  local pending_pointer_moves
  pending_pointer_moves=$(rg -c 'hardware-cursor-motion-fast-path moved=1 pendingFlip=1' "$dir/pacing.log" 2>/dev/null || echo 0)
  printf 'SUMMARY %s cursor_primary_interleave accepted=%s deferred=%s pending_pointer_moves=%s\n' \
    "$label" "$accepted_interleaved" "$deferred_interleaved" "$pending_pointer_moves"
  if [[ "$accepted_interleaved" -le 0 ]]; then
    return 1
  fi
}

summarize_chrome_interaction() {
  local dir="$1"
  local label="$2"
  local move_close
  local press_close
  local move_away
  local release_away
  local complete
  move_close=$(rg -c 'synthetic-chrome-interaction stage=move-close' "$dir/pacing.log" 2>/dev/null || echo 0)
  press_close=$(rg -c 'synthetic-chrome-interaction stage=press-close' "$dir/pacing.log" 2>/dev/null || echo 0)
  move_away=$(rg -c 'synthetic-chrome-interaction stage=move-away' "$dir/pacing.log" 2>/dev/null || echo 0)
  release_away=$(rg -c 'synthetic-chrome-interaction stage=release-away' "$dir/pacing.log" 2>/dev/null || echo 0)
  complete=$(rg -c 'synthetic-chrome-interaction complete' "$dir/pacing.log" 2>/dev/null || echo 0)
  printf 'SUMMARY %s chrome_interaction move_close=%s press_close=%s move_away=%s release_away=%s complete=%s\n' \
    "$label" "$move_close" "$press_close" "$move_away" "$release_away" "$complete"
  if [[ "$move_close" -lt 1 || "$press_close" -lt 1 || "$move_away" -lt 1 || "$release_away" -lt 1 || "$complete" -lt 1 ]]; then
    return 1
  fi
}

summarize_chrome_visual_artifacts() {
  local dir="$1"
  local label="$2"
  local capture_root="$dir/snap-capture"
  local metrics_file
  metrics_file="$(find "$capture_root" -name metrics.csv -type f -print -quit 2>/dev/null || true)"
  if [[ -z "$metrics_file" ]]; then
    printf 'SUMMARY %s chrome_visual_metrics_missing\n' "$label"
    return 1
  fi
  local min_delta="${LWM_CHROME_VISUAL_MIN_DELTA:-1.0}"
  awk -F, -v label="$label" -v min_delta="$min_delta" '
    $2 ~ /^close-button:/ {
      rows += 1
      luma = $7 + 0.0
      delta = $9 + 0.0
      if (rows == 1 || luma < min_luma) min_luma = luma
      if (rows == 1 || luma > max_luma) max_luma = luma
      abs_delta = delta < 0.0 ? -delta : delta
      if (abs_delta > max_abs_delta) max_abs_delta = abs_delta
      if (abs_delta >= min_delta) delta_rows += 1
      if (($8 + 0.0) > 1.0) alpha_rows += 1
    }
    END {
      range = max_luma - min_luma
      printf "SUMMARY %s chrome_visual close_rows=%d alpha_rows=%d delta_rows=%d min_delta=%.3f max_abs_delta=%.3f luma_range=%.3f metrics=%s\n",
        label, rows, alpha_rows, delta_rows, min_delta, max_abs_delta, range, FILENAME
      if (rows < 3 || alpha_rows != rows || delta_rows < 1 || max_abs_delta < min_delta || range < min_delta) exit 1
    }
  ' "$metrics_file"
}

summarize_resize_storm() {
  local dir="$1"
  local label="$2"
  local resize_events
  resize_events=$(rg -c 'resize-configure|ack-configure .* configure=[1-9][0-9]*x[1-9][0-9]*' "$dir/compositor.log" 2>/dev/null || echo 0)
  local resize_defers
  resize_defers=$(rg -c 'resize-configure-defer' "$dir/compositor.log" 2>/dev/null || echo 0)
  local resize_replacements
  resize_replacements=$(rg -c 'resize-configure-replace-acked' "$dir/compositor.log" 2>/dev/null || echo 0)
  local resize_acks
  resize_acks=$(rg -c 'ack-configure .* configure=[1-9][0-9]*x[1-9][0-9]*' "$dir/compositor.log" 2>/dev/null || echo 0)
  local resize_commits
  resize_commits=$(rg -c 'commit-match-(shm|dmabuf).* configureSerial=.* configure=[1-9][0-9]*x[1-9][0-9]*' "$dir/compositor.log" 2>/dev/null || echo 0)
  local sizing_blocks
  sizing_blocks=$(awk '
    function phrase(pattern, key,    raw) {
      if (!match(line, pattern)) return 0.0
      raw = substr(line, RSTART, RLENGTH)
      sub("^.*" key "=", "", raw)
      return raw + 0.0
    }
    /^cpu-trace:/ {
      line = $0
      sizing += phrase("surface_draw_block backend=[0-9]+ clip=[0-9]+ callout=[0-9]+ material=[0-9]+ sizing=[0-9]+", "sizing")
    }
    END { printf "%.0f\n", sizing }
  ' "$dir/cpu.log" 2>/dev/null || echo 0)
  printf 'SUMMARY %s resize_events=%s resize_defers=%s resize_replacements=%s resize_acks=%s resize_commits=%s sizing_cache_blocks=%s\n' \
    "$label" "$resize_events" "$resize_defers" "$resize_replacements" "$resize_acks" "$resize_commits" "$sizing_blocks"
  if [[ "$resize_events" -le 0 || "$sizing_blocks" -le 0 ]]; then
    return 1
  fi
}

summarize_snap_artifacts() {
  local dir="$1"
  local label="$2"
  local capture_root="$dir/snap-capture"
  local trace_root="$dir/snap-trace"
  local metrics_file
  local trace_file
  metrics_file="$(find "$capture_root" -name metrics.csv -type f -print -quit 2>/dev/null || true)"
  trace_file="$(find "$trace_root" -name snap.csv -type f -print -quit 2>/dev/null || true)"
  local png_count
  png_count="$(find "$capture_root" -name 'frame-*.png' -type f 2>/dev/null | wc -l | tr -d ' ')"

  if [[ -z "$metrics_file" || -z "$trace_file" || "$png_count" -le 0 ]]; then
    printf 'SUMMARY %s snap_artifacts_missing pngs=%s metrics=%s trace=%s\n' \
      "$label" "$png_count" "${metrics_file:-missing}" "${trace_file:-missing}"
    return 1
  fi

  local metrics_summary
  metrics_summary="$(awk -F, '
    NR > 1 {
      rows += 1
      if ($2 == "full-output") {
        full += 1
        if (($8 + 0.0) > 1.0) full_alpha += 1
        if (($10 + 0.0) > 0.0) full_pixels += $10 + 0.0
      }
    }
    END {
      printf "metrics_rows=%d full_output_rows=%d full_output_alpha_rows=%d full_output_pixels=%.0f",
        rows, full, full_alpha, full_pixels
      if (rows <= 0 || full <= 0 || full_alpha <= 0 || full_pixels <= 0) exit 1
    }
  ' "$metrics_file")" || {
    printf 'SUMMARY %s snap_capture_invalid pngs=%s metrics=%s\n' "$label" "$png_count" "$metrics_file"
    return 1
  }

  local trace_summary
  trace_summary="$(awk -F, '
    NR > 1 {
      rows += 1
      if ($2 == "frame") frames += 1
      if ($2 == "surface") surfaces += 1
    }
    END {
      printf "trace_rows=%d trace_frames=%d trace_surfaces=%d", rows, frames, surfaces
      if (rows <= 0 || frames <= 0 || surfaces <= 0) exit 1
    }
  ' "$trace_file")" || {
    printf 'SUMMARY %s snap_trace_invalid trace=%s\n' "$label" "$trace_file"
    return 1
  }

  printf 'SUMMARY %s snap_artifacts pngs=%s %s %s metrics=%s trace=%s\n' \
    "$label" "$png_count" "$metrics_summary" "$trace_summary" "$metrics_file" "$trace_file"
}

summarize_frame_artifacts() {
  local dir="$1"
  local label="$2"
  local background_rgb="${3:-32,48,64}"
  local min_content_matches="${LWM_FRAME_ARTIFACT_MIN_CONTENT_MATCHES:-24}"
  local min_exposed_samples="${LWM_FRAME_ARTIFACT_MIN_EXPOSED_SAMPLES:-12}"
  local output
  output="$("$ARTIFACT_CHECK" \
    --capture-root "$dir/snap-capture" \
    --trace-root "$dir/snap-trace" \
    --background-rgb "$background_rgb" \
    --min-content-matches "$min_content_matches" \
    --min-exposed-samples "$min_exposed_samples")" || {
    printf 'SUMMARY %s frame_artifact_check_failed\n' "$label"
    return 1
  }
  printf 'SUMMARY %s frame_artifacts %s\n' "$label" "$output"
}

summarize_blur_edge_artifacts() {
  local dir="$1"
  local label="$2"
  local metrics_file
  metrics_file=$(find "$dir/snap-capture" -name metrics.csv -print -quit 2>/dev/null || true)
  if [[ -z "$metrics_file" || ! -s "$metrics_file" ]]; then
    printf 'SUMMARY %s blur_edge_metrics_missing=1\n' "$label"
    return 1
  fi
  local min_luma_range="${LWM_BLUR_EDGE_MIN_LUMA_RANGE:-0.5}"
  awk -F, -v label="$label" -v min_luma_range="$min_luma_range" '
    NR == 1 { next }
    $2 ~ /^titlebar:/ {
      rows += 1
      luma = $7 + 0.0
      alpha = $8 + 0.0
      delta = $9 + 0.0
      if (rows == 1 || luma < min_luma) min_luma = luma
      if (rows == 1 || luma > max_luma) max_luma = luma
      if (alpha >= 220.0) alpha_rows += 1
      if (delta < 0) delta = -delta
      if (delta >= 0.05) changing_rows += 1
    }
    END {
      luma_range = rows > 0 ? max_luma - min_luma : 0.0
      printf "SUMMARY %s blur_edge_titlebar rows=%d alpha_rows=%d changing_rows=%d luma_range=%.3f min_luma_range=%.3f\n",
        label, rows, alpha_rows, changing_rows, luma_range, min_luma_range
      if (rows <= 0 || alpha_rows <= 0 || changing_rows <= 0 || luma_range < min_luma_range) exit 1
    }
  ' "$metrics_file"
}

summarize_partial_full_damage_alternation() {
  local dir="$1"
  local label="$2"
  local partial_frames
  local forced_full_frames
  partial_frames=$(rg -c 'scene-damage-render partial=1' "$dir/pacing.log" 2>/dev/null || echo 0)
  forced_full_frames=$(rg -c 'scene-damage-render partial=0 .*forcedFull=1' "$dir/pacing.log" 2>/dev/null || echo 0)
  printf 'SUMMARY %s damage_alternation partial_frames=%s forced_full_frames=%s\n' \
    "$label" "$partial_frames" "$forced_full_frames"
  if [[ "$partial_frames" -le 0 || "$forced_full_frames" -le 0 ]]; then
    return 1
  fi
}

summarize_surface_cache() {
  local dir="$1"
  local label="$2"
  [[ -s "$dir/cpu.log" ]] || {
    echo "SUMMARY $label surface_cache_missing_cpu_log=1"
    return 1
  }
  awk -v label="$label" '
    function phrase(pattern, key,    raw) {
      if (!match(line, pattern)) return 0.0
      raw = substr(line, RSTART, RLENGTH)
      sub("^.*" key "=", "", raw)
      return raw + 0.0
    }
    /^cpu-trace:/ {
      line = $0
      hits = phrase("surface_draw_cache hits=[0-9]+ misses=[0-9]+", "hits")
      misses = phrase("surface_draw_cache hits=[0-9]+ misses=[0-9]+", "misses")
      transient = phrase("surface_draw_block backend=[0-9]+ clip=[0-9]+ callout=[0-9]+ material=[0-9]+ sizing=[0-9]+ transient=[0-9]+", "transient")
      surface = phrase("phase_avg_ms total=[0-9.]+ bg=[0-9.]+ snapshot=[0-9.]+ surface=[0-9.]+", "surface")
      frames = phrase("frames=[0-9]+", "frames")
      hits_sum += hits
      misses_sum += misses
      transient_sum += transient
      surface_sum += surface
      if (surface > surface_max) surface_max = surface
      if (frames > 0) frame_samples += 1
      n += 1
    }
    END {
      if (n == 0) {
        printf "SUMMARY %s surface_cache_samples=0\n", label
        exit 1
      }
      printf "SUMMARY %s surface_cache_samples=%d frame_samples=%d hits=%.0f misses=%.0f transient_blocks=%.0f surface_avg=%.3f surface_max=%.3f\n",
        label, n, frame_samples, hits_sum, misses_sum, transient_sum, surface_sum / n, surface_max
      if (hits_sum <= 0 || hits_sum <= misses_sum || transient_sum != 0 || frame_samples <= 0) exit 1
    }
  ' "$dir/cpu.log"
}

summarize_snapshot_load() {
  local dir="$1"
  local label="$2"
  [[ -s "$dir/cpu.log" ]] || {
    echo "SUMMARY $label snapshot_load_missing_cpu_log=1"
    return 1
  }
  local min_surfaces="${LWM_SNAPSHOT_LOAD_MIN_SURFACES:-9.0}"
  local snapshot_budget_ms="${LWM_SNAPSHOT_LOAD_SNAPSHOT_MAX_MS:-0.250}"
  local surface_budget_ms="${LWM_SNAPSHOT_LOAD_SURFACE_MAX_MS:-1.500}"
  awk -v label="$label" \
      -v min_surfaces="$min_surfaces" \
      -v snapshot_budget_ms="$snapshot_budget_ms" \
      -v surface_budget_ms="$surface_budget_ms" '
    function token(pattern, suffix,    raw) {
      if (!match(line, pattern)) return 0.0
      raw = substr(line, RSTART, RLENGTH)
      sub(/^.*=/, "", raw)
      if (suffix != "") sub(suffix "$", "", raw)
      return raw + 0.0
    }
    function phrase(pattern, key,    raw) {
      if (!match(line, pattern)) return 0.0
      raw = substr(line, RSTART, RLENGTH)
      sub("^.*" key "=", "", raw)
      return raw + 0.0
    }
    /^cpu-trace:/ {
      line = $0
      frames = token("frames=[0-9]+", "")
      if (frames <= 0) next
      surfaces = token("surfaces=[0-9.]+/f", "/f")
      snapshot = phrase("phase_avg_ms total=[0-9.]+ bg=[0-9.]+ snapshot=[0-9.]+", "snapshot")
      surface = phrase("phase_avg_ms total=[0-9.]+ bg=[0-9.]+ snapshot=[0-9.]+ surface=[0-9.]+", "surface")
      samples += 1
      frames_sum += frames
      surfaces_sum += surfaces
      snapshot_sum += snapshot
      surface_sum += surface
      if (surfaces > surfaces_max) surfaces_max = surfaces
      if (snapshot > snapshot_max) snapshot_max = snapshot
      if (surface > surface_max) surface_max = surface
      if (surfaces >= min_surfaces) {
        stable_samples += 1
        stable_snapshot_sum += snapshot
        stable_surface_sum += surface
        if (snapshot > stable_snapshot_max) stable_snapshot_max = snapshot
        if (surface > stable_surface_max) stable_surface_max = surface
      }
    }
    END {
      if (samples == 0) {
        printf "SUMMARY %s snapshot_load samples=0\n", label
        exit 1
      }
      surfaces_avg = surfaces_sum / samples
      snapshot_avg = snapshot_sum / samples
      surface_avg = surface_sum / samples
      stable_snapshot_avg = stable_samples > 0 ? stable_snapshot_sum / stable_samples : 0.0
      stable_surface_avg = stable_samples > 0 ? stable_surface_sum / stable_samples : 0.0
      printf "SUMMARY %s snapshot_load samples=%d stable_samples=%d frames=%.0f surfaces_avg=%.2f surfaces_max=%.2f min_surfaces=%.2f snapshot_avg=%.3f snapshot_max=%.3f stable_snapshot_avg=%.3f stable_snapshot_max=%.3f snapshot_budget=%.3f surface_avg=%.3f surface_max=%.3f stable_surface_avg=%.3f stable_surface_max=%.3f surface_budget=%.3f\n",
        label, samples, stable_samples, frames_sum, surfaces_avg, surfaces_max, min_surfaces,
        snapshot_avg, snapshot_max, stable_snapshot_avg, stable_snapshot_max, snapshot_budget_ms,
        surface_avg, surface_max, stable_surface_avg, stable_surface_max, surface_budget_ms
      if (stable_samples == 0 || stable_snapshot_max > snapshot_budget_ms || stable_surface_max > surface_budget_ms) exit 1
    }
  ' "$dir/cpu.log"
}

summarize_malloc_count() {
  local dir="$1"
  local label="$2"
  local log="$dir/wm-malloc.log"
  [[ -s "$log" ]] || {
    echo "SUMMARY $label malloc_count_missing=1"
    return 1
  }
  local max_allocs="${LWM_SNAPSHOT_LOAD_MALLOC_MAX_ALLOCS:-0}"
  local max_allocs_per_frame="${LWM_SNAPSHOT_LOAD_MALLOC_MAX_ALLOCS_PER_FRAME:-430}"
  awk -v label="$label" \
      -v cpu_log="$dir/cpu.log" \
      -v max_allocs="$max_allocs" \
      -v max_allocs_per_frame="$max_allocs_per_frame" '
    function value(name,    pattern, raw) {
      pattern = name "=[0-9]+"
      if (!match($0, pattern)) return 0
      raw = substr($0, RSTART, RLENGTH)
      sub(/^.*=/, "", raw)
      return raw + 0
    }
    FILENAME == cpu_log && /^cpu-trace:/ {
      frames += value("frames")
    }
    FILENAME != cpu_log && /lambda-malloc-count:/ {
      samples += 1
      total += value("total_allocs")
      bytes += value("requested_bytes")
      mallocs += value("malloc")
      callocs += value("calloc")
      reallocs += value("realloc")
      aligned += value("aligned_alloc")
      posix += value("posix_memalign")
      frees += value("free")
    }
    END {
      allocs_per_frame = frames > 0 ? total / frames : 0.0
      printf "SUMMARY %s malloc_count samples=%d frames=%.0f total_allocs=%.0f allocs_per_frame=%.1f requested_bytes=%.0f malloc=%.0f calloc=%.0f realloc=%.0f aligned_alloc=%.0f posix_memalign=%.0f free=%.0f max_allocs=%.0f max_allocs_per_frame=%.1f\n",
        label, samples, frames, total, allocs_per_frame, bytes, mallocs, callocs, reallocs, aligned, posix, frees,
        max_allocs, max_allocs_per_frame
      if (samples == 0 || frames <= 0 ||
          (max_allocs > 0 && total > max_allocs) ||
          (max_allocs_per_frame > 0 && allocs_per_frame > max_allocs_per_frame)) exit 1
    }
  ' "$dir/cpu.log" "$log"
}

summarize_multi_dirty_partial() {
  local dir="$1"
  local label="$2"
  [[ -s "$dir/pacing.log" ]] || {
    echo "SUMMARY $label multi_dirty_missing_pacing_log=1"
    return 1
  }
  [[ -s "$dir/cpu.log" ]] || {
    echo "SUMMARY $label multi_dirty_missing_cpu_log=1"
    return 1
  }

  local partial_frames
  partial_frames=$(rg -c 'scene-damage-render partial=1' "$dir/pacing.log" 2>/dev/null || echo 0)
  local multi_rect_partial_frames
  multi_rect_partial_frames=$(rg -c 'scene-damage-render partial=1 logicalRects=([2-9]|[1-9][0-9]+)' "$dir/pacing.log" 2>/dev/null || echo 0)
  local multi_surface_flips
  multi_surface_flips=$(rg -c 'flip-scheduled .*surfaces=([2-9]|[1-9][0-9]+)' "$dir/pacing.log" 2>/dev/null || echo 0)
  local max_logical_rects
  max_logical_rects=$(awk '
    /scene-damage-render partial=1/ {
      for (i = 1; i <= NF; ++i) {
        if ($i ~ /^logicalRects=/) {
          raw = $i
          sub(/^logicalRects=/, "", raw)
          if (raw + 0 > max) max = raw + 0
        }
      }
    }
    END { print max + 0 }
  ' "$dir/pacing.log")
  printf 'SUMMARY %s multi_dirty_partial partial_frames=%s multi_rect_partial_frames=%s max_logical_rects=%s multi_surface_flips=%s\n' \
    "$label" "$partial_frames" "$multi_rect_partial_frames" "$max_logical_rects" "$multi_surface_flips"
  if [[ "$partial_frames" -le 0 || "$multi_rect_partial_frames" -le 0 || "$multi_surface_flips" -le 0 ]]; then
    return 1
  fi

  local surface_budget_ms="${LWM_MULTI_DIRTY_SURFACE_MAX_MS:-4.0}"
  awk -v label="$label" -v surface_budget_ms="$surface_budget_ms" '
    function phrase(pattern, key,    raw) {
      if (!match(line, pattern)) return 0.0
      raw = substr(line, RSTART, RLENGTH)
      sub("^.*" key "=", "", raw)
      return raw + 0.0
    }
    /^cpu-trace:/ {
      line = $0
      frames = phrase("frames=[0-9]+", "frames")
      surface = phrase("phase_avg_ms total=[0-9.]+ bg=[0-9.]+ snapshot=[0-9.]+ surface=[0-9.]+", "surface")
      if (frames <= 0) next
      samples += 1
      surface_sum += surface
      if (surface > surface_max) surface_max = surface
    }
    END {
      if (samples == 0) {
        printf "SUMMARY %s multi_dirty_surface_samples=0\n", label
        exit 1
      }
      surface_avg = surface_sum / samples
      printf "SUMMARY %s multi_dirty_surface samples=%d surface_avg=%.3f surface_max=%.3f budget=%.3f\n",
        label, samples, surface_avg, surface_max, surface_budget_ms
      if (surface_max > surface_budget_ms) exit 1
    }
  ' "$dir/cpu.log"

  awk -v label="$label" '
    function phrase(pattern, key,    raw) {
      if (!match(line, pattern)) return 0.0
      raw = substr(line, RSTART, RLENGTH)
      sub("^.*" key "=", "", raw)
      return raw + 0.0
    }
    /^cpu-trace:/ {
      line = $0
      prepared += phrase("blur prepared=[0-9]+ ops=[0-9]+ quads=[0-9]+", "prepared")
      runs += phrase("cache_hits=[0-9]+ cache_misses=[0-9]+ runs=[0-9]+", "runs")
      stacked_blur += phrase("vulkan_ms record=[0-9.]+ draw_ops=[0-9.]+ stacked_blur=[0-9.]+", "stacked_blur")
      samples += 1
    }
    END {
      printf "SUMMARY %s glass_blur samples=%d prepared=%.0f runs=%.0f stacked_blur_ms=%.3f\n",
        label, samples, prepared, runs, stacked_blur
      if (samples == 0 || prepared <= 0 || runs <= 0 || stacked_blur <= 0.0) exit 1
    }
  ' "$dir/cpu.log"
}

summarize_scanout_copy_partial() {
  local dir="$1"
  local label="$2"
  [[ -s "$dir/compositor.log" ]] || {
    echo "SUMMARY $label scanout_copy_missing_compositor_log=1"
    return 1
  }
  [[ -s "$dir/cpu.log" ]] || {
    echo "SUMMARY $label scanout_copy_missing_cpu_log=1"
    return 1
  }
  if ! rg -q 'uses optimal offscreen render targets with copy to scanout' "$dir/compositor.log"; then
    echo "SUMMARY $label scanout_copy_offscreen_path=0"
    return 1
  fi

  local max_ratio="${LWM_SCANOUT_COPY_MAX_PIXEL_RATIO:-0.85}"
  awk -v label="$label" -v max_ratio="$max_ratio" '
    function value(name,    pattern, raw) {
      pattern = name "=[0-9]+"
      if (!match(line, pattern)) return 0
      raw = substr(line, RSTART, RLENGTH)
      sub(/^.*=/, "", raw)
      return raw + 0
    }
    /^kms-trace:/ {
      line = $0
      scanout_full += value("scanout_copy_full")
      scanout_region += value("scanout_copy_region")
      scanout_rects += value("scanout_copy_rects")
      scanout_pixels += value("scanout_copy_pixels")
      scanout_full_equiv += value("scanout_copy_full_equiv_pixels")
      scanout_full_pixels += value("scanout_copy_full_pixels")
      scanout_region_pixels += value("scanout_copy_region_pixels")
      scanout_region_full_equiv += value("scanout_copy_region_full_equiv_pixels")
      preserve_full += value("primary_preserve_copy_full")
      preserve_region += value("primary_preserve_copy_region")
      preserve_rects += value("primary_preserve_copy_rects")
      preserve_pixels += value("primary_preserve_copy_pixels")
      preserve_full_equiv += value("primary_preserve_copy_full_equiv_pixels")
      preserve_region_pixels += value("primary_preserve_copy_region_pixels")
      preserve_region_full_equiv += value("primary_preserve_copy_region_full_equiv_pixels")
      samples += 1
    }
    END {
      ratio = scanout_full_equiv > 0 ? scanout_pixels / scanout_full_equiv : 1.0
      region_ratio = scanout_region_full_equiv > 0 ? scanout_region_pixels / scanout_region_full_equiv : 1.0
      preserve_ratio = preserve_region_full_equiv > 0 ? preserve_region_pixels / preserve_region_full_equiv : 0.0
      printf "SUMMARY %s scanout_copy samples=%d full=%d region=%d rects=%d pixels=%d full_equiv_pixels=%d pixel_ratio=%.3f region_pixels=%d region_full_equiv_pixels=%d region_pixel_ratio=%.3f max_ratio=%.3f preserve_full=%d preserve_region=%d preserve_rects=%d preserve_region_pixels=%d preserve_region_full_equiv_pixels=%d preserve_region_pixel_ratio=%.3f\n",
        label, samples, scanout_full, scanout_region, scanout_rects, scanout_pixels, scanout_full_equiv,
        ratio, scanout_region_pixels, scanout_region_full_equiv, region_ratio, max_ratio,
        preserve_full, preserve_region, preserve_rects, preserve_region_pixels,
        preserve_region_full_equiv, preserve_ratio
      if (samples == 0 || scanout_region <= 0 || scanout_rects <= 0 ||
          scanout_region_pixels <= 0 || scanout_region_full_equiv <= 0 || region_ratio >= max_ratio) exit 1
    }
  ' "$dir/compositor.log"

  local present_budget_ms="${LWM_SCANOUT_COPY_PRESENT_MAX_MS:-3.0}"
  awk -v label="$label" -v present_budget_ms="$present_budget_ms" '
    function phrase(pattern, key,    raw) {
      if (!match(line, pattern)) return 0.0
      raw = substr(line, RSTART, RLENGTH)
      sub("^.*" key "=", "", raw)
      return raw + 0.0
    }
    /^cpu-trace:/ {
      line = $0
      frames = phrase("frames=[0-9]+", "frames")
      present = phrase("cursor=[0-9.]+ present=[0-9.]+", "present")
      if (frames <= 0) next
      samples += 1
      present_sum += present
      if (present > present_max) present_max = present
    }
    END {
      if (samples == 0) {
        printf "SUMMARY %s scanout_copy_present samples=0\n", label
        exit 1
      }
      printf "SUMMARY %s scanout_copy_present samples=%d present_avg=%.3f present_max=%.3f budget=%.3f\n",
        label, samples, present_sum / samples, present_max, present_budget_ms
      if (present_max > present_budget_ms) exit 1
    }
  ' "$dir/cpu.log"
}

summarize_glass_terminal_partial() {
  local dir="$1"
  local label="$2"
  local partial_dir="$dir/partial"
  local full_dir="$dir/full"
  [[ -s "$partial_dir/cpu.log" && -s "$full_dir/cpu.log" ]] || {
    echo "SUMMARY $label glass_terminal_missing_cpu_log=1"
    return 1
  }
  [[ -s "$partial_dir/pacing.log" && -s "$full_dir/pacing.log" ]] || {
    echo "SUMMARY $label glass_terminal_missing_pacing_log=1"
    return 1
  }

  local partial_frames
  partial_frames=$(rg -c 'scene-damage-render partial=1' "$partial_dir/pacing.log" 2>/dev/null || echo 0)
  local forced_partial_frames
  forced_partial_frames=$(rg -c 'scene-damage-render partial=1' "$full_dir/pacing.log" 2>/dev/null || echo 0)
  local terminal_perf_partial
  terminal_perf_partial=$(rg -c 'vulkan-present-detail:|\[lambda:perf:detail\]' "$partial_dir/lambda-terminal-render.log" 2>/dev/null || echo 0)
  local terminal_perf_full
  terminal_perf_full=$(rg -c 'vulkan-present-detail:|\[lambda:perf:detail\]' "$full_dir/lambda-terminal-render.log" 2>/dev/null || echo 0)
  local static_presented_partial
  static_presented_partial=$( (rg --no-filename 'lambda-presentation-feedback-check: presented' "$partial_dir"/presentation-feedback-static*.log 2>/dev/null || true) | wc -l | tr -d ' ' )
  local static_presented_full
  static_presented_full=$( (rg --no-filename 'lambda-presentation-feedback-check: presented' "$full_dir"/presentation-feedback-static*.log 2>/dev/null || true) | wc -l | tr -d ' ' )

  local warmup_skip="${LWM_GLASS_TERMINAL_CPU_WARMUP_SAMPLES:-2}"
  local metrics
  metrics="$(awk -v partial_log="$partial_dir/cpu.log" -v full_log="$full_dir/cpu.log" -v warmup_skip="$warmup_skip" '
    function phrase(pattern, key,    raw) {
      if (!match(line, pattern)) return 0.0
      raw = substr(line, RSTART, RLENGTH)
      sub("^.*" key "=", "", raw)
      return raw + 0.0
    }
    function record(kind) {
      frames = phrase("frames=[0-9]+", "frames")
      surfaces = phrase("surfaces=[0-9.]+/f", "surfaces")
      if (frames <= 0 || surfaces < 1.0) return
      seen[kind] += 1
      if (seen[kind] <= warmup_skip) return
      bg = phrase("phase_avg_ms total=[0-9.]+ bg=[0-9.]+", "bg")
      surface = phrase("phase_avg_ms total=[0-9.]+ bg=[0-9.]+ snapshot=[0-9.]+ surface=[0-9.]+", "surface")
      total = phrase("phase_avg_ms total=[0-9.]+", "total")
      samples[kind] += 1
      frames_sum[kind] += frames
      surfaces_sum[kind] += surfaces
      bg_sum[kind] += bg
      surface_sum[kind] += surface
      total_sum[kind] += total
      if (surfaces > surfaces_max[kind]) surfaces_max[kind] = surfaces
      if (bg > bg_max[kind]) bg_max[kind] = bg
      if (surface > surface_max[kind]) surface_max[kind] = surface
      if (total > total_max[kind]) total_max[kind] = total
    }
    /^cpu-trace:/ {
      line = $0
      if (FILENAME == partial_log) {
        record("partial")
      } else if (FILENAME == full_log) {
        record("full")
      }
    }
    END {
      p_bg = samples["partial"] > 0 ? bg_sum["partial"] / samples["partial"] : 0.0
      f_bg = samples["full"] > 0 ? bg_sum["full"] / samples["full"] : 0.0
      p_surface = samples["partial"] > 0 ? surface_sum["partial"] / samples["partial"] : 0.0
      f_surface = samples["full"] > 0 ? surface_sum["full"] / samples["full"] : 0.0
      p_total = samples["partial"] > 0 ? total_sum["partial"] / samples["partial"] : 0.0
      f_total = samples["full"] > 0 ? total_sum["full"] / samples["full"] : 0.0
      p_surfaces = samples["partial"] > 0 ? surfaces_sum["partial"] / samples["partial"] : 0.0
      f_surfaces = samples["full"] > 0 ? surfaces_sum["full"] / samples["full"] : 0.0
      printf "warmup_skip=%d partial_samples=%d full_samples=%d partial_frames=%.0f full_frames=%.0f partial_surfaces_avg=%.2f full_surfaces_avg=%.2f partial_surfaces_max=%.2f full_surfaces_max=%.2f partial_bg_avg=%.3f full_bg_avg=%.3f partial_bg_max=%.3f full_bg_max=%.3f partial_surface_avg=%.3f full_surface_avg=%.3f partial_surface_max=%.3f full_surface_max=%.3f partial_total_avg=%.3f full_total_avg=%.3f partial_total_max=%.3f full_total_max=%.3f",
        warmup_skip,
        samples["partial"], samples["full"], frames_sum["partial"], frames_sum["full"],
        p_surfaces, f_surfaces, surfaces_max["partial"], surfaces_max["full"],
        p_bg, f_bg, bg_max["partial"], bg_max["full"],
        p_surface, f_surface, surface_max["partial"], surface_max["full"],
        p_total, f_total, total_max["partial"], total_max["full"]
      if (samples["partial"] == 0 || samples["full"] == 0) exit 1
    }
  ' "$partial_dir/cpu.log" "$full_dir/cpu.log")" || {
    printf 'SUMMARY %s glass_terminal_metrics_failed\n' "$label"
    return 1
  }

  local max_ratio="${LWM_GLASS_TERMINAL_PARTIAL_MAX_RATIO:-0.92}"
  printf 'SUMMARY %s glass_terminal partial_damage_frames=%s forced_full_partial_frames=%s terminal_perf_partial=%s terminal_perf_full=%s static_presented_partial=%s static_presented_full=%s %s max_ratio=%s\n' \
    "$label" "$partial_frames" "$forced_partial_frames" "$terminal_perf_partial" "$terminal_perf_full" \
    "$static_presented_partial" "$static_presented_full" "$metrics" "$max_ratio"

  awk -v partial_frames="$partial_frames" \
      -v forced_partial_frames="$forced_partial_frames" \
      -v terminal_perf_partial="$terminal_perf_partial" \
      -v terminal_perf_full="$terminal_perf_full" \
      -v static_presented_partial="$static_presented_partial" \
      -v static_presented_full="$static_presented_full" \
      -v metrics="$metrics" \
      -v max_ratio="$max_ratio" '
    function metric(name,    pattern, raw) {
      pattern = name "=[0-9.]+"
      if (!match(metrics, pattern)) return 0.0
      raw = substr(metrics, RSTART, RLENGTH)
      sub(/^.*=/, "", raw)
      return raw + 0.0
    }
    END {
      partial_surface = metric("partial_surface_avg")
      full_surface = metric("full_surface_avg")
      partial_bg = metric("partial_bg_avg")
      full_bg = metric("full_bg_avg")
      partial_surfaces = metric("partial_surfaces_avg")
      full_surfaces = metric("full_surfaces_avg")
      surface_ok = full_surface > 0.0 && partial_surface < full_surface * max_ratio
      bg_ok = full_bg > 0.0 && partial_bg < full_bg * max_ratio
      if (partial_frames <= 0 || forced_partial_frames != 0 ||
          static_presented_partial <= 0 || static_presented_full <= 0 ||
          partial_surfaces < 1.5 || full_surfaces < 1.5 ||
          terminal_perf_partial < 5 || terminal_perf_full < 5 ||
          !surface_ok || !bg_ok) {
        exit 1
      }
    }
  '
}

summarize_cpu_trace() {
  local log="$1"
  [[ -s "$log" ]] || return 0
  awk '
    function token(pattern, suffix,    raw) {
      if (!match(line, pattern)) return 0.0
      raw = substr(line, RSTART, RLENGTH)
      sub(/^.*=/, "", raw)
      if (suffix != "") sub(suffix "$", "", raw)
      return raw + 0.0
    }
    function phrase(pattern, key,    raw) {
      if (!match(line, pattern)) return 0.0
      raw = substr(line, RSTART, RLENGTH)
      sub("^.*" key "=", "", raw)
      return raw + 0.0
    }
    /^cpu-trace:/ {
      line = $0
      n += 1
      cpu = token("cpu=[0-9.]+%", "%")
      fps = token("fps=[0-9.]+", "")
      frames = token("frames=[0-9]+", "")
      surface = phrase("phase_avg_ms total=[0-9.]+ bg=[0-9.]+ snapshot=[0-9.]+ surface=[0-9.]+", "surface")
      present = phrase("cursor=[0-9.]+ present=[0-9.]+", "present")
      cpu_sum += cpu
      fps_sum += fps
      frames_sum += frames
      surface_sum += surface
      present_sum += present
      if (cpu > cpu_max) cpu_max = cpu
      if (surface > surface_max) surface_max = surface
      if (present > present_max) present_max = present
    }
    END {
      if (n == 0) exit
      printf "CPU samples=%d cpu_avg=%.2f cpu_max=%.2f fps_avg=%.2f frames=%.0f surface_avg=%.3f surface_max=%.3f present_avg=%.3f present_max=%.3f\n",
        n, cpu_sum / n, cpu_max, fps_sum / n, frames_sum, surface_sum / n, surface_max, present_sum / n, present_max
    }
  ' "$log"
}

summarize_terminal_present() {
  local log="$1"
  [[ -s "$log" ]] || return 0
  awk '
    function val(name,    raw) {
      if (!match($0, name "=[0-9.]+")) return 0.0
      raw = substr($0, RSTART, RLENGTH)
      sub(/^.*=/, "", raw)
      return raw + 0.0
    }
    /vulkan-present-detail:/ {
      n += 1
      waitImage += val("waitImage")
      atlas += val("atlas")
      record += val("record")
      queuePresent += val("queuePresent")
      if (val("waitImage") > 0.01) waitImageOver += 1
      if (val("atlas") > 0.01) atlasOver += 1
      if (val("record") > recordMax) recordMax = val("record")
    }
    END {
      if (n == 0) exit
      printf "Terminal present samples=%d waitImage_avg=%.3f waitImage_over_0.01ms=%d atlas_avg=%.3f atlas_over_0.01ms=%d record_avg=%.3f record_max=%.3f queuePresent_avg=%.3f\n",
        n, waitImage / n, waitImageOver, atlas / n, atlasOver, record / n, recordMax, queuePresent / n
    }
  ' "$log"
}

run_multi_dirty_partial_case() {
  local label="multi-dirty-partial"
  local dir="$LOG_DIR/$label"
  mkdir -p "$dir"

  local display_file="$XDG_RUNTIME_DIR/lambda-window-manager-display"
  rm -f "$display_file"

  local wm_pid=""
  local checker_a_pid=""
  local checker_b_pid=""
  local config_file="$dir/compositor-config.toml"
  cat >"$config_file" <<'EOF'
background = "#203040"
animations = false

[rendering.backdrop_blur]
base_downsample = 2

[chrome]
title_bar_height = 28
controls_width = 84
controls_inset_right = 8
controls_inset_top = 6
button_size = 16
button_radius = 5
button_gap = 4
title_text_color = "#ffffff"
window_corner_radius = 14
content_inset_width = 4
window_border_color = "#ffffff66"
window_border_width = 1
border_line_color = "#ffffff66"
focused_shadow_radius = 0
unfocused_shadow_radius = 0

[chrome.glass]
blur_radius = 64
base_color = "#ddddff"
tint_color = "#ddffff"
border_color = "#ffffff66"
opacity = 0.05
contrast_color = "#000000"
focused_contrast_opacity = 0.18
unfocused_contrast_opacity = 0.13
EOF
  cleanup() {
    set +e
    if [[ -n "$checker_a_pid" ]]; then
      kill -TERM -"$checker_a_pid" 2>/dev/null || kill -TERM "$checker_a_pid" 2>/dev/null || true
    fi
    if [[ -n "$checker_b_pid" ]]; then
      kill -TERM -"$checker_b_pid" 2>/dev/null || kill -TERM "$checker_b_pid" 2>/dev/null || true
    fi
    if [[ -n "$wm_pid" ]]; then
      kill -TERM -"$wm_pid" 2>/dev/null || kill -TERM "$wm_pid" 2>/dev/null || true
    fi
    sleep 1
    if [[ -n "$checker_a_pid" ]]; then
      kill -KILL -"$checker_a_pid" 2>/dev/null || kill -KILL "$checker_a_pid" 2>/dev/null || true
    fi
    if [[ -n "$checker_b_pid" ]]; then
      kill -KILL -"$checker_b_pid" 2>/dev/null || kill -KILL "$checker_b_pid" 2>/dev/null || true
    fi
    if [[ -n "$wm_pid" ]]; then
      kill -KILL -"$wm_pid" 2>/dev/null || kill -KILL "$wm_pid" 2>/dev/null || true
    fi
    set -e
  }
  trap cleanup RETURN

  setsid timeout --signal=TERM --kill-after=5s 35s env \
    LAMBDA_WINDOW_MANAGER_CPU_TRACE=1 \
    LAMBDA_WINDOW_MANAGER_CPU_TRACE_LOG="$dir/cpu.log" \
    LAMBDA_WINDOW_MANAGER_SAMPLE_TRACE=0 \
    LAMBDA_KMS_PRESENT_TRACE=1 \
    LAMBDA_WINDOW_MANAGER_PACING_TRACE=1 \
    LAMBDA_WINDOW_MANAGER_PACING_TRACE_LOG="$dir/pacing.log" \
    LAMBDA_WINDOW_MANAGER_CONFIG="$config_file" \
    LWM_SNAP_CAPTURE_FRAMES=48 \
    LWM_SNAP_CAPTURE_ALWAYS=1 \
    LWM_SNAP_CAPTURE_CHROME_REGIONS=1 \
    LWM_SNAP_CAPTURE_DIR="$dir/snap-capture" \
    LWM_SNAP_TRACE=1 \
    LWM_SNAP_TRACE_ALWAYS=1 \
    LWM_SNAP_TRACE_FRAMES=480 \
    LWM_SNAP_TRACE_DIR="$dir/snap-trace" \
    LWM_DIAGNOSTIC_SCRIPTED_EXERCISE=1 \
    LWM_DIAGNOSTIC_SCRIPTED_MULTI_TOPLEVELS=1 \
    "$WM" >"$dir/compositor.log" 2>&1 &
  wm_pid=$!

  local display=""
  for _ in $(seq 1 150); do
    if [[ -r "$display_file" ]]; then
      display="$(cat "$display_file")"
      break
    fi
    if ! kill -0 "$wm_pid" 2>/dev/null; then
      break
    fi
    sleep 0.1
  done
  if [[ -z "$display" ]]; then
    echo "CASE $label display-not-ready log_dir=$dir" >&2
    tail -n 120 "$dir/compositor.log" >&2 || true
    return 20
  fi

  setsid timeout --signal=TERM --kill-after=2s 18s env \
    WAYLAND_DISPLAY="$display" \
    LAMBDA_PRESENTATION_FEEDBACK_TIMEOUT_MS="$CHECK_TIMEOUT_MS" \
    LAMBDA_PRESENTATION_FEEDBACK_REQUIRE_HARDWARE_FLAGS=1 \
    LAMBDA_PRESENTATION_FEEDBACK_HOLD_MS=11000 \
    LAMBDA_PRESENTATION_FEEDBACK_REQUEST_SERVER_DECORATION=1 \
    "$CHECKER" >"$dir/presentation-feedback-a.log" 2>&1 &
  checker_a_pid=$!
  sleep 0.4
  setsid timeout --signal=TERM --kill-after=2s 18s env \
    WAYLAND_DISPLAY="$display" \
    LAMBDA_PRESENTATION_FEEDBACK_TIMEOUT_MS="$CHECK_TIMEOUT_MS" \
    LAMBDA_PRESENTATION_FEEDBACK_REQUIRE_HARDWARE_FLAGS=1 \
    LAMBDA_PRESENTATION_FEEDBACK_HOLD_MS=11000 \
    LAMBDA_PRESENTATION_FEEDBACK_REQUEST_SERVER_DECORATION=1 \
    "$CHECKER" >"$dir/presentation-feedback-b.log" 2>&1 &
  checker_b_pid=$!

  local checker_a_status=0
  wait "$checker_a_pid" || checker_a_status=$?
  checker_a_pid=""
  local checker_b_status=0
  wait "$checker_b_pid" || checker_b_status=$?
  checker_b_pid=""
  if [[ "$checker_a_status" -ne 0 || "$checker_b_status" -ne 0 ]]; then
    echo "CASE $label checker_a_status=$checker_a_status checker_b_status=$checker_b_status" >&2
    tail -n 80 "$dir/presentation-feedback-a.log" >&2 || true
    tail -n 80 "$dir/presentation-feedback-b.log" >&2 || true
    return 21
  fi

  sleep 1
  cleanup
  trap - RETURN
  wait "$wm_pid" 2>/dev/null || true

  local checker_a_line
  local checker_b_line
  checker_a_line="$(tr '\n' ' ' < "$dir/presentation-feedback-a.log")"
  checker_b_line="$(tr '\n' ' ' < "$dir/presentation-feedback-b.log")"
  local presenter_line
  presenter_line="$(rg -o 'using (GBM/atomic-KMS|Vulkan display) presenter' "$dir/compositor.log" | tail -1 || true)"
  printf 'CASE %s display=%s %s checker_a=%s checker_b=%s log_dir=%s\n' \
    "$label" "$display" "$presenter_line" "$checker_a_line" "$checker_b_line" "$dir"
  summarize_runtime_logs "$dir" "$label"
  summarize_multi_dirty_partial "$dir" "$label"
  summarize_snap_artifacts "$dir" "$label"
  summarize_frame_artifacts "$dir" "$label" "32,48,64"
  summarize_blur_edge_artifacts "$dir" "$label"
  summarize_cpu_trace "$dir/cpu.log"
}

run_glass_terminal_partial_case() {
  local label="glass-terminal-partial"
  local dir="$LOG_DIR/$label"
  mkdir -p "$dir"

  local config_file="$dir/compositor-config.toml"
  cat >"$config_file" <<'EOF'
background = "#203040"
animations = false

[rendering.backdrop_blur]
base_downsample = 2

[chrome]
title_bar_height = 28
controls_width = 84
controls_inset_right = 8
controls_inset_top = 6
button_size = 16
button_radius = 5
button_gap = 4
title_text_color = "#ffffff"
window_corner_radius = 14
content_inset_width = 4
window_border_color = "#ffffff66"
window_border_width = 1
border_line_color = "#ffffff66"
focused_shadow_radius = 0
unfocused_shadow_radius = 0

[chrome.glass]
blur_radius = 64
base_color = "#ddddff"
tint_color = "#ddffff"
border_color = "#ffffff66"
opacity = 0.05
contrast_color = "#000000"
focused_contrast_opacity = 0.18
unfocused_contrast_opacity = 0.13
EOF

  run_one_glass_terminal() {
    local mode="$1"
    local force_full="$2"
    local run_dir="$dir/$mode"
    mkdir -p "$run_dir"
    local display_file="$XDG_RUNTIME_DIR/lambda-window-manager-display"
    rm -f "$display_file"

    local wm_pid=""
    local -a static_pids=()
    cleanup_one() {
      set +e
      for pid in "${static_pids[@]}"; do
        if [[ -n "$pid" ]]; then
          kill -TERM -"$pid" 2>/dev/null || kill -TERM "$pid" 2>/dev/null || true
        fi
      done
      if [[ -n "$wm_pid" ]]; then
        kill -TERM -"$wm_pid" 2>/dev/null || kill -TERM "$wm_pid" 2>/dev/null || true
      fi
      sleep 1
      for pid in "${static_pids[@]}"; do
        if [[ -n "$pid" ]]; then
          kill -KILL -"$pid" 2>/dev/null || kill -KILL "$pid" 2>/dev/null || true
        fi
      done
      if [[ -n "$wm_pid" ]]; then
        kill -KILL -"$wm_pid" 2>/dev/null || kill -KILL "$wm_pid" 2>/dev/null || true
      fi
      set -e
    }
    trap cleanup_one RETURN

    local -a wm_env=(
      LAMBDA_WINDOW_MANAGER_CPU_TRACE=1
      LAMBDA_WINDOW_MANAGER_CPU_TRACE_LOG="$run_dir/cpu.log"
      LAMBDA_WINDOW_MANAGER_SAMPLE_TRACE=0
      LAMBDA_KMS_PRESENT_TRACE=1
      LAMBDA_WINDOW_MANAGER_PACING_TRACE=1
      LAMBDA_WINDOW_MANAGER_PACING_TRACE_LOG="$run_dir/pacing.log"
      LAMBDA_WINDOW_MANAGER_CONFIG="$config_file"
      LAMBDA_COMPOSITOR_DISABLE_KMS_OVERLAYS=1
      LAMBDA_COMPOSITOR_DISABLE_FULLSCREEN_DIRECT_SCANOUT=1
      LWM_DIAGNOSTIC_DISABLE_RENDER_AHEAD=1
      LWM_DIAGNOSTIC_SPREAD_INITIAL_TOPLEVELS=1
    )
    if [[ "$force_full" == "1" ]]; then
      wm_env+=(LWM_DIAGNOSTIC_FORCE_FULL_DAMAGE=1)
    fi

    setsid timeout --signal=TERM --kill-after=5s 35s env "${wm_env[@]}" \
      "$WM" >"$run_dir/compositor.log" 2>&1 &
    wm_pid=$!

    local display=""
    for _ in $(seq 1 150); do
      if [[ -r "$display_file" ]]; then
        display="$(cat "$display_file")"
        break
      fi
      if ! kill -0 "$wm_pid" 2>/dev/null; then
        break
      fi
      sleep 0.1
    done
    if [[ -z "$display" ]]; then
      echo "CASE $label/$mode display-not-ready log_dir=$run_dir" >&2
      tail -n 120 "$run_dir/compositor.log" >&2 || true
      return 20
    fi

    local static_count="${LWM_GLASS_TERMINAL_STATIC_WINDOWS:-1}"
    for index in $(seq 1 "$static_count"); do
      local static_log="$run_dir/presentation-feedback-static-$index.log"
      setsid timeout --signal=TERM --kill-after=2s 22s env \
        WAYLAND_DISPLAY="$display" \
        LAMBDA_PRESENTATION_FEEDBACK_TIMEOUT_MS="$CHECK_TIMEOUT_MS" \
        LAMBDA_PRESENTATION_FEEDBACK_REQUIRE_HARDWARE_FLAGS=1 \
        LAMBDA_PRESENTATION_FEEDBACK_HOLD_MS=17000 \
        LAMBDA_PRESENTATION_FEEDBACK_REQUEST_SERVER_DECORATION=1 \
        LAMBDA_PRESENTATION_FEEDBACK_WIDTH="${LWM_GLASS_TERMINAL_STATIC_WIDTH:-300}" \
        LAMBDA_PRESENTATION_FEEDBACK_HEIGHT="${LWM_GLASS_TERMINAL_STATIC_HEIGHT:-220}" \
        "$CHECKER" >"$static_log" 2>&1 &
      local static_pid="$!"
      static_pids+=("$static_pid")
      for _ in $(seq 1 80); do
        if rg -q 'lambda-presentation-feedback-check: presented' "$static_log" 2>/dev/null; then
          break
        fi
        if ! kill -0 "$static_pid" 2>/dev/null; then
          break
        fi
        sleep 0.1
      done
      if ! rg -q 'lambda-presentation-feedback-check: presented' "$static_log" 2>/dev/null; then
        echo "CASE $label/$mode static-checker-not-ready index=$index log_dir=$run_dir" >&2
        tail -n 80 "$static_log" >&2 || true
        return 21
      fi
      sleep 0.1
    done

    WAYLAND_DISPLAY="$display" \
      LWM_BUILD_DIR="$WAYLAND_BUILD_DIR" \
      LWM_TRACE_DIR="$run_dir" \
      LAMBDA_TERMINAL_TEST_SECONDS=12 \
      LAMBDA_TERMINAL_TEST_MODE=scroll \
      LAMBDA_TERMINAL_FRAME_SLEEP=0.016 \
      LAMBDA_TERMINAL_WINDOW_WIDTH="${LWM_GLASS_TERMINAL_WINDOW_WIDTH:-300}" \
      LAMBDA_TERMINAL_WINDOW_HEIGHT="${LWM_GLASS_TERMINAL_WINDOW_HEIGHT:-220}" \
      LAMBDA_DEBUG_PERF=2 \
      LAMBDA_RESIZE_TRACE=0 \
      "$ROOT/lambda-desktop/scripts/trace-terminal-rendering.sh" >"$run_dir/terminal-driver.log" 2>&1

    sleep 1
    cleanup_one
    trap - RETURN
    wait "$wm_pid" 2>/dev/null || true

    local presenter_line
    presenter_line="$(rg -o 'using (GBM/atomic-KMS|Vulkan display) presenter' "$run_dir/compositor.log" | tail -1 || true)"
    printf 'CASE %s/%s display=%s force_full=%s %s log_dir=%s\n' \
      "$label" "$mode" "$display" "$force_full" "$presenter_line" "$run_dir"
    summarize_runtime_logs "$run_dir" "$label/$mode"
    summarize_cpu_trace "$run_dir/cpu.log"
    summarize_terminal_present "$run_dir/lambda-terminal-render.log"
  }

  run_one_glass_terminal partial 0
  run_one_glass_terminal full 1
  summarize_glass_terminal_partial "$dir" "$label"
}

run_scanout_copy_partial_case() {
  local label="scanout-copy-partial"
  local dir="$LOG_DIR/$label"
  mkdir -p "$dir"

  local display_file="$XDG_RUNTIME_DIR/lambda-window-manager-display"
  rm -f "$display_file"

  local wm_pid=""
  local checker_a_pid=""
  local checker_b_pid=""
  cleanup() {
    set +e
    if [[ -n "$checker_a_pid" ]]; then
      kill -TERM -"$checker_a_pid" 2>/dev/null || kill -TERM "$checker_a_pid" 2>/dev/null || true
    fi
    if [[ -n "$checker_b_pid" ]]; then
      kill -TERM -"$checker_b_pid" 2>/dev/null || kill -TERM "$checker_b_pid" 2>/dev/null || true
    fi
    if [[ -n "$wm_pid" ]]; then
      kill -TERM -"$wm_pid" 2>/dev/null || kill -TERM "$wm_pid" 2>/dev/null || true
    fi
    sleep 1
    if [[ -n "$checker_a_pid" ]]; then
      kill -KILL -"$checker_a_pid" 2>/dev/null || kill -KILL "$checker_a_pid" 2>/dev/null || true
    fi
    if [[ -n "$checker_b_pid" ]]; then
      kill -KILL -"$checker_b_pid" 2>/dev/null || kill -KILL "$checker_b_pid" 2>/dev/null || true
    fi
    if [[ -n "$wm_pid" ]]; then
      kill -KILL -"$wm_pid" 2>/dev/null || kill -KILL "$wm_pid" 2>/dev/null || true
    fi
    set -e
  }
  trap cleanup RETURN

  setsid timeout --signal=TERM --kill-after=5s 35s env \
    LAMBDA_WINDOW_MANAGER_CPU_TRACE=1 \
    LAMBDA_WINDOW_MANAGER_CPU_TRACE_LOG="$dir/cpu.log" \
    LAMBDA_WINDOW_MANAGER_SAMPLE_TRACE=0 \
    LAMBDA_KMS_PRESENT_TRACE=1 \
    LAMBDA_WINDOW_MANAGER_PACING_TRACE=1 \
    LAMBDA_WINDOW_MANAGER_PACING_TRACE_LOG="$dir/pacing.log" \
    LAMBDA_COMPOSITOR_DISABLE_DIRECT_SCANOUT_RENDER=1 \
    LAMBDA_COMPOSITOR_DISABLE_KMS_OVERLAYS=1 \
    LWM_DIAGNOSTIC_SCRIPTED_EXERCISE=1 \
    LWM_DIAGNOSTIC_SCRIPTED_MULTI_TOPLEVELS=1 \
    "$WM" >"$dir/compositor.log" 2>&1 &
  wm_pid=$!

  local display=""
  for _ in $(seq 1 150); do
    if [[ -r "$display_file" ]]; then
      display="$(cat "$display_file")"
      break
    fi
    if ! kill -0 "$wm_pid" 2>/dev/null; then
      break
    fi
    sleep 0.1
  done
  if [[ -z "$display" ]]; then
    echo "CASE $label display-not-ready log_dir=$dir" >&2
    tail -n 120 "$dir/compositor.log" >&2 || true
    return 20
  fi

  setsid timeout --signal=TERM --kill-after=2s 18s env \
    WAYLAND_DISPLAY="$display" \
    LAMBDA_PRESENTATION_FEEDBACK_TIMEOUT_MS="$CHECK_TIMEOUT_MS" \
    LAMBDA_PRESENTATION_FEEDBACK_REQUIRE_HARDWARE_FLAGS=1 \
    LAMBDA_PRESENTATION_FEEDBACK_HOLD_MS=11000 \
    LAMBDA_PRESENTATION_FEEDBACK_REQUEST_SERVER_DECORATION=1 \
    "$CHECKER" >"$dir/presentation-feedback-a.log" 2>&1 &
  checker_a_pid=$!
  sleep 0.4
  setsid timeout --signal=TERM --kill-after=2s 18s env \
    WAYLAND_DISPLAY="$display" \
    LAMBDA_PRESENTATION_FEEDBACK_TIMEOUT_MS="$CHECK_TIMEOUT_MS" \
    LAMBDA_PRESENTATION_FEEDBACK_REQUIRE_HARDWARE_FLAGS=1 \
    LAMBDA_PRESENTATION_FEEDBACK_HOLD_MS=11000 \
    LAMBDA_PRESENTATION_FEEDBACK_REQUEST_SERVER_DECORATION=1 \
    "$CHECKER" >"$dir/presentation-feedback-b.log" 2>&1 &
  checker_b_pid=$!

  local checker_a_status=0
  wait "$checker_a_pid" || checker_a_status=$?
  checker_a_pid=""
  local checker_b_status=0
  wait "$checker_b_pid" || checker_b_status=$?
  checker_b_pid=""
  if [[ "$checker_a_status" -ne 0 || "$checker_b_status" -ne 0 ]]; then
    echo "CASE $label checker_a_status=$checker_a_status checker_b_status=$checker_b_status" >&2
    tail -n 80 "$dir/presentation-feedback-a.log" >&2 || true
    tail -n 80 "$dir/presentation-feedback-b.log" >&2 || true
    return 21
  fi

  sleep 1
  cleanup
  trap - RETURN
  wait "$wm_pid" 2>/dev/null || true

  local checker_a_line
  local checker_b_line
  checker_a_line="$(tr '\n' ' ' < "$dir/presentation-feedback-a.log")"
  checker_b_line="$(tr '\n' ' ' < "$dir/presentation-feedback-b.log")"
  local presenter_line
  presenter_line="$(rg -o 'using (GBM/atomic-KMS|Vulkan display) presenter' "$dir/compositor.log" | tail -1 || true)"
  printf 'CASE %s display=%s %s checker_a=%s checker_b=%s log_dir=%s\n' \
    "$label" "$display" "$presenter_line" "$checker_a_line" "$checker_b_line" "$dir"
  summarize_runtime_logs "$dir" "$label"
  summarize_multi_dirty_partial "$dir" "$label"
  summarize_scanout_copy_partial "$dir" "$label"
  summarize_cpu_trace "$dir/cpu.log"
}

run_partial_full_artifacts_case() {
  local label="partial-full-artifacts"
  local dir="$LOG_DIR/$label"
  mkdir -p "$dir"

  local display_file="$XDG_RUNTIME_DIR/lambda-window-manager-display"
  rm -f "$display_file"

  local wm_pid=""
  local checker_a_pid=""
  local checker_b_pid=""
  local config_file="$dir/compositor-config.toml"
  cat >"$config_file" <<'EOF'
background = "#203040"
animations = false

[chrome]
focused_shadow_radius = 0
unfocused_shadow_radius = 0
EOF
  cleanup() {
    set +e
    if [[ -n "$checker_a_pid" ]]; then
      kill -TERM -"$checker_a_pid" 2>/dev/null || kill -TERM "$checker_a_pid" 2>/dev/null || true
    fi
    if [[ -n "$checker_b_pid" ]]; then
      kill -TERM -"$checker_b_pid" 2>/dev/null || kill -TERM "$checker_b_pid" 2>/dev/null || true
    fi
    if [[ -n "$wm_pid" ]]; then
      kill -TERM -"$wm_pid" 2>/dev/null || kill -TERM "$wm_pid" 2>/dev/null || true
    fi
    sleep 1
    if [[ -n "$checker_a_pid" ]]; then
      kill -KILL -"$checker_a_pid" 2>/dev/null || kill -KILL "$checker_a_pid" 2>/dev/null || true
    fi
    if [[ -n "$checker_b_pid" ]]; then
      kill -KILL -"$checker_b_pid" 2>/dev/null || kill -KILL "$checker_b_pid" 2>/dev/null || true
    fi
    if [[ -n "$wm_pid" ]]; then
      kill -KILL -"$wm_pid" 2>/dev/null || kill -KILL "$wm_pid" 2>/dev/null || true
    fi
    set -e
  }
  trap cleanup RETURN

  setsid timeout --signal=TERM --kill-after=5s 35s env \
    LAMBDA_WINDOW_MANAGER_CPU_TRACE=1 \
    LAMBDA_WINDOW_MANAGER_CPU_TRACE_LOG="$dir/cpu.log" \
    LAMBDA_WINDOW_MANAGER_SAMPLE_TRACE=0 \
    LAMBDA_KMS_PRESENT_TRACE=1 \
    LAMBDA_WINDOW_MANAGER_PACING_TRACE=1 \
    LAMBDA_WINDOW_MANAGER_PACING_TRACE_LOG="$dir/pacing.log" \
    LAMBDA_WINDOW_MANAGER_CONFIG="$config_file" \
    LAMBDA_COMPOSITOR_DISABLE_DIRECT_SCANOUT_RENDER=1 \
    LAMBDA_COMPOSITOR_DISABLE_KMS_OVERLAYS=1 \
    LWM_SNAP_CAPTURE_FRAMES=48 \
    LWM_SNAP_CAPTURE_ALWAYS=1 \
    LWM_SNAP_CAPTURE_DIR="$dir/snap-capture" \
    LWM_SNAP_TRACE=1 \
    LWM_SNAP_TRACE_ALWAYS=1 \
    LWM_SNAP_TRACE_FRAMES=480 \
    LWM_SNAP_TRACE_DIR="$dir/snap-trace" \
    LWM_DIAGNOSTIC_SCRIPTED_EXERCISE=1 \
    LWM_DIAGNOSTIC_SCRIPTED_MULTI_TOPLEVELS=1 \
    LWM_DIAGNOSTIC_ALTERNATE_FULL_DAMAGE=1 \
    "$WM" >"$dir/compositor.log" 2>&1 &
  wm_pid=$!

  local display=""
  for _ in $(seq 1 150); do
    if [[ -r "$display_file" ]]; then
      display="$(cat "$display_file")"
      break
    fi
    if ! kill -0 "$wm_pid" 2>/dev/null; then
      break
    fi
    sleep 0.1
  done
  if [[ -z "$display" ]]; then
    echo "CASE $label display-not-ready log_dir=$dir" >&2
    tail -n 120 "$dir/compositor.log" >&2 || true
    return 20
  fi

  setsid timeout --signal=TERM --kill-after=2s 18s env \
    WAYLAND_DISPLAY="$display" \
    LAMBDA_PRESENTATION_FEEDBACK_TIMEOUT_MS="$CHECK_TIMEOUT_MS" \
    LAMBDA_PRESENTATION_FEEDBACK_REQUIRE_HARDWARE_FLAGS=1 \
    LAMBDA_PRESENTATION_FEEDBACK_HOLD_MS=11000 \
    LAMBDA_PRESENTATION_FEEDBACK_REQUEST_SERVER_DECORATION=1 \
    "$CHECKER" >"$dir/presentation-feedback-a.log" 2>&1 &
  checker_a_pid=$!
  sleep 0.4
  setsid timeout --signal=TERM --kill-after=2s 18s env \
    WAYLAND_DISPLAY="$display" \
    LAMBDA_PRESENTATION_FEEDBACK_TIMEOUT_MS="$CHECK_TIMEOUT_MS" \
    LAMBDA_PRESENTATION_FEEDBACK_REQUIRE_HARDWARE_FLAGS=1 \
    LAMBDA_PRESENTATION_FEEDBACK_HOLD_MS=11000 \
    LAMBDA_PRESENTATION_FEEDBACK_REQUEST_SERVER_DECORATION=1 \
    "$CHECKER" >"$dir/presentation-feedback-b.log" 2>&1 &
  checker_b_pid=$!

  local checker_a_status=0
  wait "$checker_a_pid" || checker_a_status=$?
  checker_a_pid=""
  local checker_b_status=0
  wait "$checker_b_pid" || checker_b_status=$?
  checker_b_pid=""
  if [[ "$checker_a_status" -ne 0 || "$checker_b_status" -ne 0 ]]; then
    echo "CASE $label checker_a_status=$checker_a_status checker_b_status=$checker_b_status" >&2
    tail -n 80 "$dir/presentation-feedback-a.log" >&2 || true
    tail -n 80 "$dir/presentation-feedback-b.log" >&2 || true
    return 21
  fi

  sleep 1
  cleanup
  trap - RETURN
  wait "$wm_pid" 2>/dev/null || true

  local checker_a_line
  local checker_b_line
  checker_a_line="$(tr '\n' ' ' < "$dir/presentation-feedback-a.log")"
  checker_b_line="$(tr '\n' ' ' < "$dir/presentation-feedback-b.log")"
  local presenter_line
  presenter_line="$(rg -o 'using (GBM/atomic-KMS|Vulkan display) presenter' "$dir/compositor.log" | tail -1 || true)"
  printf 'CASE %s display=%s %s checker_a=%s checker_b=%s log_dir=%s\n' \
    "$label" "$display" "$presenter_line" "$checker_a_line" "$checker_b_line" "$dir"
  summarize_runtime_logs "$dir" "$label"
  summarize_multi_dirty_partial "$dir" "$label"
  summarize_partial_full_damage_alternation "$dir" "$label"
  summarize_snap_artifacts "$dir" "$label"
  summarize_frame_artifacts "$dir" "$label" "32,48,64"
  summarize_cpu_trace "$dir/cpu.log"
}

run_resize_storm_case() {
  local label="resize-storm"
  local dir="$LOG_DIR/$label"
  mkdir -p "$dir"

  local display_file="$XDG_RUNTIME_DIR/lambda-window-manager-display"
  rm -f "$display_file"

  local wm_pid=""
  local checker_pid=""
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
    set -e
  }
  trap cleanup RETURN

  setsid timeout --signal=TERM --kill-after=5s 30s env \
    LAMBDA_WINDOW_MANAGER_CPU_TRACE=1 \
    LAMBDA_WINDOW_MANAGER_CPU_TRACE_LOG="$dir/cpu.log" \
    LAMBDA_WINDOW_MANAGER_SAMPLE_TRACE=0 \
    LAMBDA_KMS_PRESENT_TRACE=1 \
    LAMBDA_WINDOW_MANAGER_PACING_TRACE=1 \
    LAMBDA_WINDOW_MANAGER_PACING_TRACE_LOG="$dir/pacing.log" \
    LAMBDA_RESIZE_TRACE=1 \
    LWM_SNAP_CAPTURE_FRAMES=12 \
    LWM_SNAP_CAPTURE_ALWAYS=1 \
    LWM_SNAP_CAPTURE_DIR="$dir/snap-capture" \
    LWM_SNAP_TRACE=1 \
    LWM_SNAP_TRACE_ALWAYS=1 \
    LWM_SNAP_TRACE_FRAMES=240 \
    LWM_SNAP_TRACE_DIR="$dir/snap-trace" \
    LWM_DIAGNOSTIC_SCRIPTED_EXERCISE=1 \
    LWM_DIAGNOSTIC_SCRIPTED_RESIZE=1 \
    "$WM" >"$dir/compositor.log" 2>&1 &
  wm_pid=$!

  local display=""
  for _ in $(seq 1 150); do
    if [[ -r "$display_file" ]]; then
      display="$(cat "$display_file")"
      break
    fi
    if ! kill -0 "$wm_pid" 2>/dev/null; then
      break
    fi
    sleep 0.1
  done
  if [[ -z "$display" ]]; then
    echo "CASE $label display-not-ready log_dir=$dir" >&2
    tail -n 120 "$dir/compositor.log" >&2 || true
    return 20
  fi

  setsid timeout --signal=TERM --kill-after=2s 15s env \
    WAYLAND_DISPLAY="$display" \
    LAMBDA_PRESENTATION_FEEDBACK_TIMEOUT_MS="$CHECK_TIMEOUT_MS" \
    LAMBDA_PRESENTATION_FEEDBACK_REQUIRE_HARDWARE_FLAGS=1 \
    LAMBDA_PRESENTATION_FEEDBACK_HOLD_MS=10000 \
    LAMBDA_PRESENTATION_FEEDBACK_REQUEST_SERVER_DECORATION=1 \
    "$CHECKER" >"$dir/presentation-feedback.log" 2>&1 &
  checker_pid=$!

  local checker_status=0
  wait "$checker_pid" || checker_status=$?
  checker_pid=""
  if [[ "$checker_status" -ne 0 ]]; then
    echo "CASE $label checker_status=$checker_status" >&2
    tail -n 120 "$dir/presentation-feedback.log" >&2 || true
    return "$checker_status"
  fi

  sleep 1
  cleanup
  trap - RETURN
  wait "$wm_pid" 2>/dev/null || true

  local checker_line
  checker_line="$(tr '\n' ' ' < "$dir/presentation-feedback.log")"
  local presenter_line
  presenter_line="$(rg -o 'using (GBM/atomic-KMS|Vulkan display) presenter' "$dir/compositor.log" | tail -1 || true)"
  printf 'CASE %s display=%s %s checker=%s log_dir=%s\n' "$label" "$display" "$presenter_line" "$checker_line" "$dir"
  summarize_runtime_logs "$dir" "$label"
  summarize_resize_storm "$dir" "$label"
  summarize_snap_artifacts "$dir" "$label"
  summarize_cpu_trace "$dir/cpu.log"
}

run_chrome_interaction_case() {
  local label="chrome-hover-press-cache"
  local dir="$LOG_DIR/$label"
  mkdir -p "$dir"

  local display_file="$XDG_RUNTIME_DIR/lambda-window-manager-display"
  rm -f "$display_file"

  local wm_pid=""
  local checker_pid=""
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
    set -e
  }
  trap cleanup RETURN

  setsid timeout --signal=TERM --kill-after=5s 30s env \
    LAMBDA_WINDOW_MANAGER_CPU_TRACE=1 \
    LAMBDA_WINDOW_MANAGER_CPU_TRACE_LOG="$dir/cpu.log" \
    LAMBDA_WINDOW_MANAGER_SAMPLE_TRACE=0 \
    LAMBDA_KMS_PRESENT_TRACE=1 \
    LAMBDA_WINDOW_MANAGER_PACING_TRACE=1 \
    LAMBDA_WINDOW_MANAGER_PACING_TRACE_LOG="$dir/pacing.log" \
    LWM_SNAP_CAPTURE_FRAMES=32 \
    LWM_SNAP_CAPTURE_ALWAYS=1 \
    LWM_SNAP_CAPTURE_CHROME_REGIONS=1 \
    LWM_SNAP_CAPTURE_DIR="$dir/snap-capture" \
    LWM_SNAP_TRACE=1 \
    LWM_SNAP_TRACE_ALWAYS=1 \
    LWM_SNAP_TRACE_FRAMES=160 \
    LWM_SNAP_TRACE_DIR="$dir/snap-trace" \
    LAMBDA_WINDOW_MANAGER_SYNTHETIC_CHROME_INTERACTION=1 \
    LAMBDA_WINDOW_MANAGER_SYNTHETIC_CHROME_INTERACTION_WARMUP_MS=2500 \
    LAMBDA_WINDOW_MANAGER_SYNTHETIC_CHROME_INTERACTION_INTERVAL_MS=350 \
    "$WM" >"$dir/compositor.log" 2>&1 &
  wm_pid=$!

  local display=""
  for _ in $(seq 1 150); do
    if [[ -r "$display_file" ]]; then
      display="$(cat "$display_file")"
      break
    fi
    if ! kill -0 "$wm_pid" 2>/dev/null; then
      break
    fi
    sleep 0.1
  done
  if [[ -z "$display" ]]; then
    echo "CASE $label display-not-ready log_dir=$dir" >&2
    tail -n 120 "$dir/compositor.log" >&2 || true
    return 20
  fi

  setsid timeout --signal=TERM --kill-after=2s 15s env \
    WAYLAND_DISPLAY="$display" \
    LAMBDA_PRESENTATION_FEEDBACK_TIMEOUT_MS="$CHECK_TIMEOUT_MS" \
    LAMBDA_PRESENTATION_FEEDBACK_REQUIRE_HARDWARE_FLAGS=1 \
    LAMBDA_PRESENTATION_FEEDBACK_HOLD_MS=7000 \
    LAMBDA_PRESENTATION_FEEDBACK_REQUEST_SERVER_DECORATION=1 \
    "$CHECKER" >"$dir/presentation-feedback.log" 2>&1 &
  checker_pid=$!

  local checker_status=0
  wait "$checker_pid" || checker_status=$?
  checker_pid=""
  if [[ "$checker_status" -ne 0 ]]; then
    echo "CASE $label checker_status=$checker_status" >&2
    tail -n 120 "$dir/presentation-feedback.log" >&2 || true
    return "$checker_status"
  fi

  sleep 1
  cleanup
  trap - RETURN
  wait "$wm_pid" 2>/dev/null || true

  local checker_line
  checker_line="$(tr '\n' ' ' < "$dir/presentation-feedback.log")"
  local presenter_line
  presenter_line="$(rg -o 'using (GBM/atomic-KMS|Vulkan display) presenter' "$dir/compositor.log" | tail -1 || true)"
  printf 'CASE %s display=%s %s checker=%s log_dir=%s\n' "$label" "$display" "$presenter_line" "$checker_line" "$dir"
  summarize_runtime_logs "$dir" "$label"
  summarize_chrome_interaction "$dir" "$label"
  summarize_surface_cache "$dir" "$label"
  summarize_snap_artifacts "$dir" "$label"
  summarize_chrome_visual_artifacts "$dir" "$label"
  summarize_cpu_trace "$dir/cpu.log"
}

run_surface_cache_case() {
  local label="surface-cache-static"
  local dir="$LOG_DIR/$label"
  mkdir -p "$dir"

  local display_file="$XDG_RUNTIME_DIR/lambda-window-manager-display"
  rm -f "$display_file"

  local wm_pid=""
  local checker_pid=""
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
    set -e
  }
  trap cleanup RETURN

  setsid timeout --signal=TERM --kill-after=5s 30s env \
    LAMBDA_WINDOW_MANAGER_CPU_TRACE=1 \
    LAMBDA_WINDOW_MANAGER_CPU_TRACE_LOG="$dir/cpu.log" \
    LAMBDA_WINDOW_MANAGER_SAMPLE_TRACE=0 \
    LAMBDA_KMS_PRESENT_TRACE=1 \
    LAMBDA_WINDOW_MANAGER_PACING_TRACE=1 \
    LAMBDA_WINDOW_MANAGER_PACING_TRACE_LOG="$dir/pacing.log" \
    LWM_DIAGNOSTIC_FLOOR_RENDERING=1 \
    LWM_DIAGNOSTIC_SCRIPTED_EXERCISE=1 \
    "$WM" >"$dir/compositor.log" 2>&1 &
  wm_pid=$!

  local display=""
  for _ in $(seq 1 150); do
    if [[ -r "$display_file" ]]; then
      display="$(cat "$display_file")"
      break
    fi
    if ! kill -0 "$wm_pid" 2>/dev/null; then
      break
    fi
    sleep 0.1
  done
  if [[ -z "$display" ]]; then
    echo "CASE $label display-not-ready log_dir=$dir" >&2
    tail -n 120 "$dir/compositor.log" >&2 || true
    return 20
  fi

  setsid timeout --signal=TERM --kill-after=2s 15s env \
    WAYLAND_DISPLAY="$display" \
    LAMBDA_PRESENTATION_FEEDBACK_TIMEOUT_MS="$CHECK_TIMEOUT_MS" \
    LAMBDA_PRESENTATION_FEEDBACK_REQUIRE_HARDWARE_FLAGS=1 \
    LAMBDA_PRESENTATION_FEEDBACK_HOLD_MS=9000 \
    LAMBDA_PRESENTATION_FEEDBACK_REQUEST_SERVER_DECORATION=1 \
    "$CHECKER" >"$dir/presentation-feedback.log" 2>&1 &
  checker_pid=$!

  local checker_status=0
  wait "$checker_pid" || checker_status=$?
  checker_pid=""
  if [[ "$checker_status" -ne 0 ]]; then
    echo "CASE $label checker_status=$checker_status" >&2
    tail -n 120 "$dir/presentation-feedback.log" >&2 || true
    return "$checker_status"
  fi

  sleep 1
  cleanup
  trap - RETURN
  wait "$wm_pid" 2>/dev/null || true

  local checker_line
  checker_line="$(tr '\n' ' ' < "$dir/presentation-feedback.log")"
  local presenter_line
  presenter_line="$(rg -o 'using (GBM/atomic-KMS|Vulkan display) presenter' "$dir/compositor.log" | tail -1 || true)"
  printf 'CASE %s display=%s %s checker=%s log_dir=%s\n' "$label" "$display" "$presenter_line" "$checker_line" "$dir"
  summarize_runtime_logs "$dir" "$label"
  summarize_surface_cache "$dir" "$label"
  summarize_cpu_trace "$dir/cpu.log"
}

run_snapshot_load_case() {
  local label="snapshot-load-10-windows"
  local dir="$LOG_DIR/$label"
  mkdir -p "$dir"

  local display_file="$XDG_RUNTIME_DIR/lambda-window-manager-display"
  rm -f "$display_file"

  local wm_pid=""
  local -a checker_pids=()
  local malloc_counter_so="$dir/malloc-count-preload.so"
  build_malloc_counter "$malloc_counter_so"
  cleanup() {
    set +e
    for pid in "${checker_pids[@]}"; do
      if [[ -n "$pid" ]]; then
        kill -TERM -"$pid" 2>/dev/null || kill -TERM "$pid" 2>/dev/null || true
      fi
    done
    if [[ -n "$wm_pid" ]]; then
      kill -TERM -"$wm_pid" 2>/dev/null || kill -TERM "$wm_pid" 2>/dev/null || true
    fi
    sleep 1
    for pid in "${checker_pids[@]}"; do
      if [[ -n "$pid" ]]; then
        kill -KILL -"$pid" 2>/dev/null || kill -KILL "$pid" 2>/dev/null || true
      fi
    done
    if [[ -n "$wm_pid" ]]; then
      kill -KILL -"$wm_pid" 2>/dev/null || kill -KILL "$wm_pid" 2>/dev/null || true
    fi
    set -e
  }
  trap cleanup RETURN

  setsid timeout --signal=TERM --kill-after=5s 40s env \
    LAMBDA_WINDOW_MANAGER_CPU_TRACE=1 \
    LAMBDA_WINDOW_MANAGER_CPU_TRACE_LOG="$dir/cpu.log" \
    LAMBDA_WINDOW_MANAGER_SAMPLE_TRACE=0 \
    LAMBDA_KMS_PRESENT_TRACE=1 \
    LAMBDA_WINDOW_MANAGER_PACING_TRACE=1 \
    LAMBDA_WINDOW_MANAGER_PACING_TRACE_LOG="$dir/pacing.log" \
    LD_PRELOAD="$malloc_counter_so" \
    LAMBDA_MALLOC_COUNT_LOG="$dir/wm-malloc.log" \
    LWM_DIAGNOSTIC_FLOOR_RENDERING=1 \
    LWM_DIAGNOSTIC_SCRIPTED_EXERCISE=1 \
    "$WM" >"$dir/compositor.log" 2>&1 &
  wm_pid=$!

  local display=""
  for _ in $(seq 1 150); do
    if [[ -r "$display_file" ]]; then
      display="$(cat "$display_file")"
      break
    fi
    if ! kill -0 "$wm_pid" 2>/dev/null; then
      break
    fi
    sleep 0.1
  done
  if [[ -z "$display" ]]; then
    echo "CASE $label display-not-ready log_dir=$dir" >&2
    tail -n 120 "$dir/compositor.log" >&2 || true
    return 20
  fi

  for index in $(seq 1 10); do
    setsid timeout --signal=TERM --kill-after=2s 20s env \
      WAYLAND_DISPLAY="$display" \
      LAMBDA_PRESENTATION_FEEDBACK_TIMEOUT_MS="$CHECK_TIMEOUT_MS" \
      LAMBDA_PRESENTATION_FEEDBACK_REQUIRE_HARDWARE_FLAGS=1 \
      LAMBDA_PRESENTATION_FEEDBACK_HOLD_MS=12000 \
      LAMBDA_PRESENTATION_FEEDBACK_REQUEST_SERVER_DECORATION=1 \
      "$CHECKER" >"$dir/presentation-feedback-$index.log" 2>&1 &
    checker_pids+=("$!")
    sleep 0.1
  done

  local checker_status=0
  for index in "${!checker_pids[@]}"; do
    local pid="${checker_pids[$index]}"
    if ! wait "$pid"; then
      checker_status=1
      echo "CASE $label checker_$((index + 1))_failed" >&2
      tail -n 80 "$dir/presentation-feedback-$((index + 1)).log" >&2 || true
    fi
    checker_pids[$index]=""
  done
  if [[ "$checker_status" -ne 0 ]]; then
    return 21
  fi

  sleep 1
  cleanup
  trap - RETURN
  wait "$wm_pid" 2>/dev/null || true

  local presenter_line
  presenter_line="$(rg -o 'using (GBM/atomic-KMS|Vulkan display) presenter' "$dir/compositor.log" | tail -1 || true)"
  printf 'CASE %s display=%s %s checker_windows=10 log_dir=%s\n' "$label" "$display" "$presenter_line" "$dir"
  summarize_runtime_logs "$dir" "$label"
  if [[ "${LWM_SNAPSHOT_LOAD_REQUIRE_SURFACE_CACHE:-1}" == "0" ]]; then
    summarize_surface_cache "$dir" "$label" || true
  else
    summarize_surface_cache "$dir" "$label"
  fi
  summarize_snapshot_load "$dir" "$label"
  summarize_malloc_count "$dir" "$label"
  summarize_cpu_trace "$dir/cpu.log"
}

run_compositor_case() {
  local label="$1"
  local presenter="$2"
  local require_hardware_flags="$3"
  local run_apps="$4"
  local synthetic_pointer="${5:-0}"
  local synthetic_pointer_warmup_ms="${6:-1500}"
  local checker_hold_ms="${7:-0}"
  local synthetic_pointer_count="${8:-180}"
  local synthetic_pointer_dx="${9:-5}"
  local synthetic_pointer_dy="${10:-2}"
  local synthetic_pointer_start_x="${11:-}"
  local synthetic_pointer_start_y="${12:-}"
  local dir="$LOG_DIR/$label"
  mkdir -p "$dir"

  local display_file="$XDG_RUNTIME_DIR/lambda-window-manager-display"
  rm -f "$display_file"

  local wm_pid=""
  local shell_pid=""
  cleanup() {
    set +e
    if [[ -n "$shell_pid" ]]; then
      kill -TERM -"$shell_pid" 2>/dev/null || kill -TERM "$shell_pid" 2>/dev/null || true
    fi
    if [[ -n "$wm_pid" ]]; then
      kill -TERM -"$wm_pid" 2>/dev/null || kill -TERM "$wm_pid" 2>/dev/null || true
    fi
    sleep 1
    if [[ -n "$shell_pid" ]]; then
      kill -KILL -"$shell_pid" 2>/dev/null || kill -KILL "$shell_pid" 2>/dev/null || true
    fi
    if [[ -n "$wm_pid" ]]; then
      kill -KILL -"$wm_pid" 2>/dev/null || kill -KILL "$wm_pid" 2>/dev/null || true
    fi
    set -e
  }
  trap cleanup RETURN

  local -a env_args=(
    LAMBDA_WINDOW_MANAGER_CPU_TRACE=1
    LAMBDA_WINDOW_MANAGER_CPU_TRACE_LOG="$dir/cpu.log"
    LAMBDA_WINDOW_MANAGER_SAMPLE_TRACE=1
    LAMBDA_WINDOW_MANAGER_SAMPLE_USEC=1000
    LAMBDA_KMS_PRESENT_TRACE=1
    LAMBDA_WINDOW_MANAGER_PACING_TRACE=1
    LAMBDA_WINDOW_MANAGER_PACING_TRACE_LOG="$dir/pacing.log"
  )
  if [[ -n "$presenter" ]]; then
    env_args+=(LAMBDA_WINDOW_MANAGER_PRESENT="$presenter")
  fi
  if [[ "$synthetic_pointer" == "1" ]]; then
    env_args+=(
      LAMBDA_COMPOSITOR_ENABLE_HARDWARE_CURSOR_MOTION_FAST_PATH=1
      LAMBDA_WINDOW_MANAGER_SYNTHETIC_POINTER_MOTION=1
      LAMBDA_WINDOW_MANAGER_SYNTHETIC_POINTER_MOTION_COUNT="$synthetic_pointer_count"
      LAMBDA_WINDOW_MANAGER_SYNTHETIC_POINTER_MOTION_INTERVAL_MS=8
      LAMBDA_WINDOW_MANAGER_SYNTHETIC_POINTER_MOTION_WARMUP_MS="$synthetic_pointer_warmup_ms"
      LAMBDA_WINDOW_MANAGER_SYNTHETIC_POINTER_MOTION_DX="$synthetic_pointer_dx"
      LAMBDA_WINDOW_MANAGER_SYNTHETIC_POINTER_MOTION_DY="$synthetic_pointer_dy"
    )
    if [[ -n "$synthetic_pointer_start_x" && -n "$synthetic_pointer_start_y" ]]; then
      env_args+=(
        LAMBDA_WINDOW_MANAGER_SYNTHETIC_POINTER_MOTION_START_X="$synthetic_pointer_start_x"
        LAMBDA_WINDOW_MANAGER_SYNTHETIC_POINTER_MOTION_START_Y="$synthetic_pointer_start_y"
      )
    fi
  fi

  setsid timeout --signal=TERM --kill-after=5s 60s env "${env_args[@]}" "$WM" >"$dir/compositor.log" 2>&1 &
  wm_pid=$!

  local display=""
  for _ in $(seq 1 150); do
    if [[ -r "$display_file" ]]; then
      display="$(cat "$display_file")"
      break
    fi
    if ! kill -0 "$wm_pid" 2>/dev/null; then
      break
    fi
    sleep 0.1
  done
  if [[ -z "$display" ]]; then
    echo "CASE $label display-not-ready log_dir=$dir" >&2
    tail -n 120 "$dir/compositor.log" >&2 || true
    return 20
  fi

  env WAYLAND_DISPLAY="$display" \
    LAMBDA_PRESENTATION_FEEDBACK_TIMEOUT_MS="$CHECK_TIMEOUT_MS" \
    LAMBDA_PRESENTATION_FEEDBACK_REQUIRE_HARDWARE_FLAGS="$require_hardware_flags" \
    LAMBDA_PRESENTATION_FEEDBACK_HOLD_MS="$checker_hold_ms" \
    "$CHECKER" >"$dir/presentation-feedback.log" 2>&1

  if [[ "$run_apps" == "1" ]]; then
    setsid timeout --signal=TERM --kill-after=2s 45s env WAYLAND_DISPLAY="$display" "$SHELL_APP" >"$dir/shell.log" 2>&1 &
    shell_pid=$!
    sleep 2

    env WAYLAND_DISPLAY="$display" \
      LWM_BUILD_DIR="$WAYLAND_BUILD_DIR" \
      LWM_TRACE_DIR="$dir" \
      LAMBDA_TERMINAL_TEST_SECONDS="$TERMINAL_SECONDS" \
      LAMBDA_TERMINAL_TEST_MODE=grid \
      LAMBDA_DEBUG_PERF=2 \
      LAMBDA_RESIZE_TRACE=1 \
      "$ROOT/lambda-desktop/scripts/trace-terminal-rendering.sh" >"$dir/terminal-driver.log" 2>&1

    set +e
    timeout --signal=TERM --kill-after=2s "${EDITOR_SECONDS}s" env WAYLAND_DISPLAY="$display" "$EDITOR_APP" "$ROOT/lambda-desktop/docs/TODO.md" >"$dir/editor.log" 2>&1
    local editor_status=$?
    set -e
    if [[ "$editor_status" -ne 0 && "$editor_status" -ne 124 ]]; then
      echo "CASE $label editor_status=$editor_status" >&2
      return "$editor_status"
    fi
  fi

  if [[ "$synthetic_pointer" == "1" ]]; then
    sleep 5
  fi

  sleep 2
  cleanup
  trap - RETURN
  wait "$wm_pid" 2>/dev/null || true
  if [[ -n "$shell_pid" ]]; then
    wait "$shell_pid" 2>/dev/null || true
  fi

  local checker_line
  checker_line="$(tr '\n' ' ' < "$dir/presentation-feedback.log")"
  local presenter_line
  presenter_line="$(rg -o 'using (GBM/atomic-KMS|Vulkan display) presenter' "$dir/compositor.log" | tail -1 || true)"
  printf 'CASE %s display=%s %s checker=%s log_dir=%s\n' "$label" "$display" "$presenter_line" "$checker_line" "$dir"
  summarize_runtime_logs "$dir" "$label"
  if [[ "$synthetic_pointer" == "1" ]]; then
    summarize_pointer_fast_path "$dir" "$label"
    if [[ "$run_apps" == "1" ]]; then
      summarize_pointer_terminal_overlap "$dir" "$label"
      summarize_cursor_primary_interleave "$dir" "$label"
    fi
  fi
  summarize_cpu_trace "$dir/cpu.log"
  summarize_terminal_present "$dir/lambda-terminal-render.log"
}

echo "Frame pacing verification logs: $LOG_DIR"
CASE_FILTER="${LWM_FRAME_PACING_CASES:-all}"

case_selected() {
  local name="$1"
  if [[ "$CASE_FILTER" == "all" || -z "$CASE_FILTER" ]]; then
    return 0
  fi
  local item
  local -a selected_cases=()
  IFS=',' read -r -a selected_cases <<<"$CASE_FILTER"
  for item in "${selected_cases[@]}"; do
    if [[ "$item" == "$name" ]]; then
      return 0
    fi
  done
  return 1
}

run_selected_case() {
  local name="$1"
  shift
  if case_selected "$name"; then
    "$@"
  else
    echo "Skipping case $name"
  fi
}

run_selected_case atomic run_compositor_case atomic "" 1 1
run_selected_case atomic-pointer-fast-path run_compositor_case atomic-pointer-fast-path "" 1 0 1 1500 7000 80 1 0 64 72
run_selected_case atomic-pointer-under-terminal-load run_compositor_case atomic-pointer-under-terminal-load "" 1 1 1 7000
run_selected_case surface-cache-static run_surface_cache_case
run_selected_case snapshot-load-10-windows run_snapshot_load_case
run_selected_case multi-dirty-partial run_multi_dirty_partial_case
run_selected_case glass-terminal-partial run_glass_terminal_partial_case
run_selected_case scanout-copy-partial run_scanout_copy_partial_case
run_selected_case partial-full-artifacts run_partial_full_artifacts_case
run_selected_case chrome-hover-press-cache run_chrome_interaction_case
run_selected_case resize-storm run_resize_storm_case
run_selected_case vulkan-display run_compositor_case vulkan-display vulkan-display 0 0

echo "Pointer-driving tool availability:"
for tool in ydotool wtype evemu-event; do
  if command -v "$tool" >/dev/null 2>&1; then
    echo "  $tool: available"
  else
    echo "  $tool: missing"
  fi
done

if pgrep -af 'lambda-window-manager|lambda-shell|lambda-terminal|lambda-editor|lambda-presentation-feedback-check' >/dev/null; then
  echo "Warning: Lambda processes still running after verification:" >&2
  pgrep -af 'lambda-window-manager|lambda-shell|lambda-terminal|lambda-editor|lambda-presentation-feedback-check' >&2 || true
fi

echo "Frame pacing verification completed."
