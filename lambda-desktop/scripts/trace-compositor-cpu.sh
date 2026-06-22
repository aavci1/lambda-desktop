#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${LWM_BUILD_DIR:-$ROOT/build}"
DEFAULT_BIN="$BUILD_DIR/lambda-desktop/lambda-window-manager/lambda-window-manager"
FALLBACK_BIN="$BUILD_DIR/lambda-window-manager"
BIN="${LAMBDA_WINDOW_MANAGER_BIN:-$DEFAULT_BIN}"
LOG_DIR="${LWM_TRACE_DIR:-$ROOT/.debug-logs}"
TRACE_LOG="${LAMBDA_WINDOW_MANAGER_CPU_TRACE_LOG:-$LOG_DIR/lambda-window-manager-cpu.log}"
PACING_LOG="${LAMBDA_WINDOW_MANAGER_PACING_TRACE_LOG:-$LOG_DIR/lambda-window-manager-pacing.log}"
STDERR_LOG="${LAMBDA_WINDOW_MANAGER_STDERR_LOG:-$LOG_DIR/lambda-window-manager-compositor.log}"

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  cat <<EOF
Usage: lambda-desktop/scripts/trace-compositor-cpu.sh [lambda-window-manager args...]

Starts lambda-window-manager with CPU tracing and CPU-time sampling enabled.
Use this from the TTY where you normally start the compositor, reproduce the
problem, then inspect the printed log paths.

Default flow:
  1. Start this script.
  2. Let the compositor idle for about 5 seconds.
  3. Drag the terminal window around for about 10 seconds.
  4. Let it idle again for about 5 seconds.
  5. Quit/kill the compositor.

Environment overrides:
  LWM_BUILD_DIR                         Build directory. Default: $BUILD_DIR
  LWM_TRACE_DIR                         Log directory. Default: $LOG_DIR
  LAMBDA_WINDOW_MANAGER_BIN             Binary path. Default: $DEFAULT_BIN
  LAMBDA_WINDOW_MANAGER_CPU_TRACE_LOG   CPU trace log. Default: $TRACE_LOG
  LAMBDA_WINDOW_MANAGER_STDERR_LOG      Compositor stderr log. Default: $STDERR_LOG
  LAMBDA_WINDOW_MANAGER_SAMPLE_TRACE    1 enables sampled hot symbols. Default: 1
  LAMBDA_WINDOW_MANAGER_SAMPLE_USEC     CPU sample interval. Default: 1000
  LAMBDA_DEBUG_PERF                     0, 1, 2, or anomaly. Default: 0
  LAMBDA_KMS_PRESENT_TRACE              1 enables atomic-KMS presenter timing. Default: 0
  LAMBDA_WINDOW_MANAGER_PACING_TRACE    1 enables verbose pacing log. Default: 0
  LAMBDA_WINDOW_MANAGER_PACING_TRACE_LOG Pacing log. Default: $PACING_LOG
EOF
  exit 0
fi

mkdir -p "$LOG_DIR"

if [[ ! -x "$BIN" ]]; then
  if [[ "$BIN" == "$DEFAULT_BIN" && -x "$FALLBACK_BIN" ]]; then
    BIN="$FALLBACK_BIN"
  else
    echo "Compositor binary not found: $BIN" >&2
    echo "Build it first with: cmake --build $BUILD_DIR --target lambda-window-manager" >&2
    exit 1
  fi
fi

summarize_cpu_trace() {
  if [[ ! -s "$TRACE_LOG" ]]; then
    echo "No CPU trace entries were captured: $TRACE_LOG"
    return
  fi

  echo
  echo "CPU trace summary:"
  awk '
    function token_value(pattern, suffix,    raw) {
      if (!match(line, pattern)) return 0.0
      raw = substr(line, RSTART, RLENGTH)
      sub(/^.*=/, "", raw)
      if (suffix != "") sub(suffix "$", "", raw)
      return raw + 0.0
    }
    function phrase_value(pattern, key,    raw) {
      if (!match(line, pattern)) return 0.0
      raw = substr(line, RSTART, RLENGTH)
      sub("^.*" key "=", "", raw)
      return raw + 0.0
    }
    /^cpu-trace:/ {
      line = $0
      n += 1
      cpu = token_value("cpu=[0-9.]+%", "%")
      fps = token_value("fps=[0-9.]+", "")
      frames = token_value("frames=[0-9]+", "")
      commits = phrase_value("commits total=[0-9]+", "total")
      surface = phrase_value("phase_avg_ms total=[0-9.]+ bg=[0-9.]+ snapshot=[0-9.]+ surface=[0-9.]+", "surface")
      copy = token_value("copy_mp_f=[0-9.]+", "")
      blur = token_value("blur_mp_f=[0-9.]+", "")
      misses = token_value("cache_misses=[0-9]+", "")
      surface_cache_hits = phrase_value("surface_draw_cache hits=[0-9]+", "hits")
      surface_cache_misses = phrase_value("surface_draw_cache hits=[0-9]+ misses=[0-9]+", "misses")
      surface_block_material = phrase_value("callout=[0-9]+ material=[0-9]+", "material")
      surface_block_sizing = phrase_value("material=[0-9]+ sizing=[0-9]+", "sizing")
      surface_block_transient = phrase_value("sizing=[0-9]+ transient=[0-9]+", "transient")
      surface_block_opening = phrase_value("transient=[0-9]+ opening=[0-9]+", "opening")
      runs = token_value("runs=[0-9]+", "")
      vk_record = phrase_value("vulkan_ms record=[0-9.]+", "record")
      vk_draw = phrase_value("vulkan_ms record=[0-9.]+ draw_ops=[0-9.]+", "draw_ops")
      vk_stacked = phrase_value("stacked_blur=[0-9.]+", "stacked_blur")
      vk_path_tess = phrase_value("path_tess=[0-9.]+", "path_tess")
      vk_visited = phrase_value("vulkan_ops calls=[0-9.]+/f visited=[0-9.]+/f", "visited")
      vk_submitted = phrase_value("visited=[0-9.]+/f submitted=[0-9.]+/f", "submitted")
      vk_scissors = phrase_value("submitted=[0-9.]+/f scissors=[0-9.]+/f", "scissors")

      cpu_sum += cpu
      fps_sum += fps
      frames_sum += frames
      commits_sum += commits
      surface_sum += surface
      copy_sum += copy
      blur_sum += blur
      misses_sum += misses
      surface_cache_hits_sum += surface_cache_hits
      surface_cache_misses_sum += surface_cache_misses
      surface_block_material_sum += surface_block_material
      surface_block_sizing_sum += surface_block_sizing
      surface_block_transient_sum += surface_block_transient
      surface_block_opening_sum += surface_block_opening
      runs_sum += runs
      vk_record_sum += vk_record
      vk_draw_sum += vk_draw
      vk_stacked_sum += vk_stacked
      vk_path_tess_sum += vk_path_tess
      vk_visited_sum += vk_visited
      vk_submitted_sum += vk_submitted
      vk_scissors_sum += vk_scissors

      if (cpu > cpu_max) cpu_max = cpu
      if (fps > fps_max) fps_max = fps
      if (surface > surface_max) surface_max = surface
      if (copy > copy_max) copy_max = copy
      if (blur > blur_max) blur_max = blur
      if (misses > misses_max) misses_max = misses
      if (vk_record > vk_record_max) vk_record_max = vk_record
      if (vk_draw > vk_draw_max) vk_draw_max = vk_draw
      if (vk_stacked > vk_stacked_max) vk_stacked_max = vk_stacked
    }
    END {
      if (n == 0) {
        print "  no cpu-trace lines"
        exit
      }
      printf "  samples:              %d\n", n
      printf "  cpu avg/max:          %.1f%% / %.1f%%\n", cpu_sum / n, cpu_max
      printf "  fps avg/max:          %.1f / %.1f\n", fps_sum / n, fps_max
      printf "  frames total:         %.0f\n", frames_sum
      printf "  commits total:        %.0f\n", commits_sum
      printf "  surface avg/max:      %.3fms / %.3fms\n", surface_sum / n, surface_max
      printf "  copy_mp_f avg/max:    %.2f / %.2f\n", copy_sum / n, copy_max
      printf "  blur_mp_f avg/max:    %.2f / %.2f\n", blur_sum / n, blur_max
      printf "  cache_misses sum/max: %.0f / %.0f\n", misses_sum, misses_max
      printf "  surface draw cache:   hits=%.0f misses=%.0f\n", surface_cache_hits_sum, surface_cache_misses_sum
      printf "  surface cache blocks: material=%.0f sizing=%.0f transient=%.0f opening=%.0f\n",
             surface_block_material_sum, surface_block_sizing_sum, surface_block_transient_sum, surface_block_opening_sum
      printf "  runs total:           %.0f\n", runs_sum
      printf "  vulkan record avg/max: %.3fms / %.3fms\n", vk_record_sum / n, vk_record_max
      printf "  vulkan draw avg/max:   %.3fms / %.3fms\n", vk_draw_sum / n, vk_draw_max
      printf "  stacked ms avg/max:    %.3fms / %.3fms\n", vk_stacked_sum / n, vk_stacked_max
      printf "  path_tess avg:         %.3fms\n", vk_path_tess_sum / n
      printf "  vulkan ops avg:        visited=%.1f submitted=%.1f scissors=%.1f\n",
             vk_visited_sum / n, vk_submitted_sum / n, vk_scissors_sum / n
    }
  ' "$TRACE_LOG"

  echo
  echo "Steady-state summary:"
  awk '
    function token_value(pattern, suffix,    raw) {
      if (!match(line, pattern)) return 0.0
      raw = substr(line, RSTART, RLENGTH)
      sub(/^.*=/, "", raw)
      if (suffix != "") sub(suffix "$", "", raw)
      return raw + 0.0
    }
    function phrase_value(pattern, key,    raw) {
      if (!match(line, pattern)) return 0.0
      raw = substr(line, RSTART, RLENGTH)
      sub("^.*" key "=", "", raw)
      return raw + 0.0
    }
    /^cpu-trace:/ {
      line = $0
      surfaces = token_value("surfaces=[0-9.]+/f", "/f")
      commits = phrase_value("commits total=[0-9]+", "total")
      if (surfaces < 2.95 || commits != 0) next
      n += 1
      cpu = token_value("cpu=[0-9.]+%", "%")
      fps = token_value("fps=[0-9.]+", "")
      surface = phrase_value("phase_avg_ms total=[0-9.]+ bg=[0-9.]+ snapshot=[0-9.]+ surface=[0-9.]+", "surface")
      present = phrase_value("cursor=[0-9.]+ present=[0-9.]+", "present")
      canvas_present = phrase_value("present=[0-9.]+ canvas_present=[0-9.]+", "canvas_present")
      kms_present = phrase_value("canvas_present=[0-9.]+ kms_present=[0-9.]+", "kms_present")
      copy = token_value("copy_mp_f=[0-9.]+", "")
      blur_misses = token_value("cache_misses=[0-9]+", "")
      surface_cache_hits = phrase_value("surface_draw_cache hits=[0-9]+", "hits")
      surface_cache_misses = phrase_value("surface_draw_cache hits=[0-9]+ misses=[0-9]+", "misses")
      vk_record = phrase_value("vulkan_ms record=[0-9.]+", "record")
      vk_stacked = phrase_value("stacked_blur=[0-9.]+", "stacked_blur")
      vk_path_tess = phrase_value("path_tess=[0-9.]+", "path_tess")
      cpu_sum += cpu
      fps_sum += fps
      surface_sum += surface
      present_sum += present
      canvas_present_sum += canvas_present
      kms_present_sum += kms_present
      copy_sum += copy
      blur_misses_sum += blur_misses
      surface_cache_hits_sum += surface_cache_hits
      surface_cache_misses_sum += surface_cache_misses
      vk_record_sum += vk_record
      vk_stacked_sum += vk_stacked
      vk_path_tess_sum += vk_path_tess
      if (fps < 59.5) missed_budget += 1
    }
    END {
      if (n == 0) {
        print "  no steady-state samples"
        exit
      }
      printf "  samples:              %d\n", n
      printf "  missed budget samples: %.0f\n", missed_budget
      printf "  cpu avg:              %.1f%%\n", cpu_sum / n
      printf "  fps avg:              %.1f\n", fps_sum / n
      printf "  surface avg:          %.3fms\n", surface_sum / n
      printf "  present avg:          %.3fms canvas=%.3f kms=%.3f\n",
             present_sum / n, canvas_present_sum / n, kms_present_sum / n
      printf "  copy_mp_f avg:        %.2f\n", copy_sum / n
      printf "  blur cache misses:    %.0f\n", blur_misses_sum
      printf "  surface draw cache:   hits=%.0f misses=%.0f\n", surface_cache_hits_sum, surface_cache_misses_sum
      printf "  vulkan record avg:    %.3fms\n", vk_record_sum / n
      printf "  stacked blur avg:     %.3fms\n", vk_stacked_sum / n
      printf "  path_tess avg:        %.3fms\n", vk_path_tess_sum / n
    }
  ' "$TRACE_LOG"

  echo
  echo "Recent KMS presenter trace:"
  if grep -q '^kms-trace:' "$STDERR_LOG"; then
    grep '^kms-trace:' "$STDERR_LOG" | tail -n 12
  else
    echo "  no kms-trace entries; set LAMBDA_KMS_PRESENT_TRACE=1 to enable"
  fi
}

: >"$TRACE_LOG"
: >"$STDERR_LOG"
if [[ "${LAMBDA_WINDOW_MANAGER_PACING_TRACE:-0}" != "0" ]]; then
  : >"$PACING_LOG"
fi

echo "CPU trace log: $TRACE_LOG"
echo "Compositor log: $STDERR_LOG"
if [[ "${LAMBDA_WINDOW_MANAGER_PACING_TRACE:-0}" != "0" ]]; then
  echo "Pacing trace log: $PACING_LOG"
fi
echo
echo "Reproduce the CPU issue now: idle ~5s, drag a window ~10s, idle ~5s, then quit/kill the compositor."
echo "After it exits, useful commands are:"
echo "  tail -n 40 \"$TRACE_LOG\""
echo "  tail -n 80 \"$STDERR_LOG\""

export LAMBDA_WINDOW_MANAGER_CPU_TRACE=1
export LAMBDA_WINDOW_MANAGER_PACING_TRACE="${LAMBDA_WINDOW_MANAGER_PACING_TRACE:-0}"
export LAMBDA_WINDOW_MANAGER_PACING_TRACE_LOG="$PACING_LOG"
export LAMBDA_WINDOW_MANAGER_SAMPLE_TRACE="${LAMBDA_WINDOW_MANAGER_SAMPLE_TRACE:-1}"
export LAMBDA_WINDOW_MANAGER_SAMPLE_USEC="${LAMBDA_WINDOW_MANAGER_SAMPLE_USEC:-1000}"
export LAMBDA_WINDOW_MANAGER_CPU_TRACE_LOG="$TRACE_LOG"
export LAMBDA_DEBUG_PERF="${LAMBDA_DEBUG_PERF:-0}"
export LAMBDA_KMS_PRESENT_TRACE="${LAMBDA_KMS_PRESENT_TRACE:-0}"

"$BIN" "$@" 2>&1 | tee "$STDERR_LOG"
summarize_cpu_trace
