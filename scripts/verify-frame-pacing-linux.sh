#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
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
Usage: scripts/verify-frame-pacing-linux.sh

Runs the TODO-019 Linux frame-pacing verification smoke:
  - builds the KMS compositor and Wayland verifier/client targets
  - starts lambda-window-manager with CPU, KMS, and pacing traces
  - verifies wp_presentation feedback on atomic-KMS and Vulkan-display presenters
  - runs lambda-shell, a scripted lambda-terminal workload, and lambda-editor
  - drives synthetic pointer motion through the KMS raw-input path with the hardware-cursor fast path enabled
  - overlaps synthetic hardware-cursor motion with the scripted terminal workload
  - exercises a static SHM client surface and asserts compositor surface draw-cache reuse
  - moves two decorated checker windows and asserts multi-rect partial damage stays cheap
  - drives server-side chrome hover/press transitions and asserts client surface-cache reuse
  - drives scripted resize configures and asserts the compositor sizing path stays healthy
  - captures compositor resize-storm frames and snap CSV metrics for artifact inspection
  - summarizes CPU/pacing/present-detail logs and fails on fatal runtime errors

Environment:
  LWM_WAYLAND_BUILD_DIR                 Wayland build dir. Default: $WAYLAND_BUILD_DIR
  LWM_KMS_BUILD_DIR                     KMS build dir. Default: $KMS_BUILD_DIR
  LWM_FRAME_PACING_LOG_DIR              Log root. Default: $LOG_ROOT
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
build_target "$WAYLAND_BUILD_DIR" lambda-presentation-feedback-check lambda-shell lambda-terminal lambda-editor

WM="$KMS_BUILD_DIR/apps/lambda-window-manager/lambda-window-manager"
CHECKER="$WAYLAND_BUILD_DIR/tools/lambda-presentation-feedback-check"
SHELL_APP="$WAYLAND_BUILD_DIR/apps/lambda-shell/lambda-shell"
EDITOR_APP="$WAYLAND_BUILD_DIR/apps/lambda-editor/lambda-editor"

for binary in "$WM" "$CHECKER" "$SHELL_APP" "$EDITOR_APP"; do
  if [[ ! -x "$binary" ]]; then
    echo "Missing executable: $binary" >&2
    exit 2
  fi
done

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
      "$ROOT/scripts/trace-terminal-rendering.sh" >"$dir/terminal-driver.log" 2>&1

    set +e
    timeout --signal=TERM --kill-after=2s "${EDITOR_SECONDS}s" env WAYLAND_DISPLAY="$display" "$EDITOR_APP" "$ROOT/TODO.md" >"$dir/editor.log" 2>&1
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
    fi
  fi
  summarize_cpu_trace "$dir/cpu.log"
  summarize_terminal_present "$dir/lambda-terminal-render.log"
}

echo "Frame pacing verification logs: $LOG_DIR"
run_compositor_case atomic "" 1 1
run_compositor_case atomic-pointer-fast-path "" 1 0 1 1500 7000 80 1 0 64 72
run_compositor_case atomic-pointer-under-terminal-load "" 1 1 1 7000
run_surface_cache_case
run_multi_dirty_partial_case
run_chrome_interaction_case
run_resize_storm_case
run_compositor_case vulkan-display vulkan-display 0 0

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
