#pragma once

#include <Lambda/Debug/DebugFlags.hpp>

#include <array>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>

namespace lambda::debug::perf {

enum class TimedMetric : std::uint8_t {
  ProcessReactiveUpdates = 0,
  SceneRender,
  CanvasPresent,
  CanvasDrawableWait,
  DisplayLinkToPresent,
  Count,
};

enum class RenderCounterKind : std::uint8_t {
  Rect = 0,
  Image,
  Path,
  Glyph,
  Count,
};

struct ComponentKeyCounters {
  std::uint64_t copies = 0;
  std::uint64_t copiedIds = 0;
  std::uint64_t appends = 0;
  std::uint64_t appendedIds = 0;
  std::uint64_t hashCalls = 0;
  std::uint64_t hashedIds = 0;
  std::uint64_t equalityCalls = 0;
  std::uint64_t equalityIds = 0;
  std::uint64_t prefixCalls = 0;
  std::uint64_t prefixIds = 0;
  std::uint64_t heapGrowths = 0;
  std::uint64_t heapCapacity = 0;
};

struct RenderCounters {
  std::array<std::uint64_t, static_cast<std::size_t>(RenderCounterKind::Count)> ops{};
  std::array<std::uint64_t, static_cast<std::size_t>(RenderCounterKind::Count)> drawCalls{};
  std::array<std::uint64_t, static_cast<std::size_t>(RenderCounterKind::Count)> uploadBytes{};
  std::uint64_t opOrderEntries = 0;
  std::uint64_t pathVertices = 0;
  std::uint64_t glyphVertices = 0;
  std::uint64_t backdropBlurRuns = 0;
  std::uint64_t backdropBlurPreparedRuns = 0;
  std::uint64_t backdropBlurOps = 0;
  std::uint64_t backdropBlurQuads = 0;
  std::uint64_t backdropBlurCacheHits = 0;
  std::uint64_t backdropBlurCacheMisses = 0;
  std::uint64_t backdropBlurPasses = 0;
  std::uint64_t backdropBlurPixels = 0;
  std::uint64_t backdropCopyPixels = 0;
  std::uint64_t backdropMaxCopyPixels = 0;
  std::uint64_t backdropMaxBlurPixels = 0;
  std::uint64_t recorderCapacityGrowths = 0;
  std::uint64_t recorderCapacityGrowthBytes = 0;
  std::uint64_t glyphAtlasGrowths = 0;
  std::uint64_t glyphAtlasGrowPixels = 0;
  std::uint64_t vulkanRecordNs = 0;
  std::uint64_t vulkanDrawOpsNs = 0;
  std::uint64_t vulkanStackedBlurNs = 0;
  std::uint64_t vulkanPathTessellateNs = 0;
  std::uint64_t vulkanDrawOpsCalls = 0;
  std::uint64_t vulkanDrawOpsVisited = 0;
  std::uint64_t vulkanDrawOpsSubmitted = 0;
  std::uint64_t vulkanScissorChanges = 0;
  std::uint64_t vulkanStackedOps = 0;
  std::uint64_t vulkanPathTessellations = 0;
};

struct SceneCounters {
  std::uint64_t renderPasses = 0;
  std::uint64_t nodesVisited = 0;
  std::uint64_t groupsVisited = 0;
  std::uint64_t leavesVisited = 0;
  std::uint64_t quickRejects = 0;
  std::uint64_t liveLeafRenders = 0;
  std::uint64_t preparedReplaySuccesses = 0;
  std::uint64_t preparedReplayFailures = 0;
};

struct TextCounters {
  std::uint64_t layoutCalls = 0;
  std::uint64_t layoutCacheHits = 0;
  std::uint64_t layoutCacheMisses = 0;
  std::uint64_t paragraphVariantHits = 0;
  std::uint64_t paragraphVariantMisses = 0;
};

namespace detail {

struct IntervalCounters {
  static constexpr std::size_t kMaxFrameBudgetSamples = 512;

  std::chrono::steady_clock::time_point startedAt = std::chrono::steady_clock::now();
  std::uint64_t frames = 0;
  ComponentKeyCounters componentKeys{};
  RenderCounters render{};
  SceneCounters scene{};
  TextCounters text{};
  std::uint64_t preparedPrepareCalls = 0;
  std::uint64_t preparedReplayCalls = 0;
  std::array<std::uint64_t, static_cast<std::size_t>(TimedMetric::Count)> durationsNs{};
  std::array<std::uint64_t, kMaxFrameBudgetSamples> frameBudgetSamplesNs{};
  std::size_t frameBudgetSampleCount = 0;

  void reset(std::chrono::steady_clock::time_point now) {
    startedAt = now;
    frames = 0;
    componentKeys = {};
    render = {};
    scene = {};
    text = {};
    preparedPrepareCalls = 0;
    preparedReplayCalls = 0;
    durationsNs.fill(0);
    frameBudgetSamplesNs.fill(0);
    frameBudgetSampleCount = 0;
  }
};

inline IntervalCounters& counters() {
  static IntervalCounters value{};
  return value;
}

inline double perFrame(std::uint64_t total, std::uint64_t frames) {
  if (frames == 0) {
    return 0.0;
  }
  return static_cast<double>(total) / static_cast<double>(frames);
}

inline double nanosToMillis(std::uint64_t totalNs) {
  return static_cast<double>(totalNs) / 1'000'000.0;
}

inline double perFrameMillis(std::uint64_t totalNs, std::uint64_t frames) {
  if (frames == 0) {
    return 0.0;
  }
  return nanosToMillis(totalNs) / static_cast<double>(frames);
}

inline double frameBudgetPercentileMillis(IntervalCounters const& interval, double percentile) {
  if (interval.frameBudgetSampleCount == 0) {
    return perFrameMillis(interval.durationsNs[static_cast<std::size_t>(TimedMetric::DisplayLinkToPresent)],
                          interval.frames);
  }
  std::array<std::uint64_t, IntervalCounters::kMaxFrameBudgetSamples> samples = interval.frameBudgetSamplesNs;
  std::size_t const count = interval.frameBudgetSampleCount;
  std::sort(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(count));
  double const clamped = std::clamp(percentile, 0.0, 1.0);
  std::size_t const index = std::min(count - 1, static_cast<std::size_t>(std::round(clamped * (count - 1))));
  return nanosToMillis(samples[index]);
}

inline double uploadKilobytesPerFrame(RenderCounters const& render, std::uint64_t frames) {
  std::uint64_t total = 0;
  for (std::uint64_t bytes : render.uploadBytes) {
    total += bytes;
  }
  return perFrame(total, frames) / 1024.0;
}

inline void printCompactCount(FILE* out, double value) {
  double const rounded = std::round(value);
  if (std::abs(value - rounded) < 0.05) {
    std::fprintf(out, "%.0f", rounded);
  } else {
    std::fprintf(out, "%.1f", value);
  }
}

inline void printDrawSummary(IntervalCounters const& interval) {
  std::fprintf(stderr, "draw");
  auto const printKind = [&](char label, RenderCounterKind kind) {
    double const value = perFrame(interval.render.drawCalls[static_cast<std::size_t>(kind)], interval.frames);
    if (value <= 0.0) {
      return;
    }
    std::fprintf(stderr, " %c", label);
    printCompactCount(stderr, value);
  };
  printKind('r', RenderCounterKind::Rect);
  printKind('i', RenderCounterKind::Image);
  printKind('p', RenderCounterKind::Path);
  printKind('g', RenderCounterKind::Glyph);
}

inline bool hasComponentKeyWork(ComponentKeyCounters const& ck) {
  return ck.copies || ck.copiedIds || ck.appends || ck.appendedIds || ck.hashCalls || ck.hashedIds ||
         ck.equalityCalls || ck.equalityIds || ck.prefixCalls || ck.prefixIds || ck.heapGrowths;
}

inline void printSummaryLine(IntervalCounters const& interval, double seconds) {
  double const fps = seconds > 0.0 ? static_cast<double>(interval.frames) / seconds : 0.0;
  std::fprintf(stderr, "[lambda:perf] %.0ffps  budget %.2fms  render %.2fms  ",
               fps,
               frameBudgetPercentileMillis(interval, 0.50),
               perFrameMillis(interval.durationsNs[static_cast<std::size_t>(TimedMetric::SceneRender)],
                              interval.frames));
  printDrawSummary(interval);
  std::fprintf(stderr, "  cache ");
  printCompactCount(stderr, perFrame(interval.scene.preparedReplaySuccesses, interval.frames));
  std::fprintf(stderr, "/");
  printCompactCount(stderr, perFrame(interval.scene.preparedReplayFailures, interval.frames));
  std::fprintf(stderr, "  blur %.1f/f %.1fMP/f  upload %.1fKB  text %llu/%llu\n",
               perFrame(interval.render.backdropBlurRuns, interval.frames),
               perFrame(interval.render.backdropBlurPixels, interval.frames) / 1'000'000.0,
               uploadKilobytesPerFrame(interval.render, interval.frames),
               static_cast<unsigned long long>(interval.text.layoutCacheHits),
               static_cast<unsigned long long>(interval.text.layoutCacheMisses));
}

inline void printVerboseLine(IntervalCounters const& interval, double seconds) {
  std::fprintf(
      stderr,
      "[lambda:perf:detail] %.2fs frames=%llu "
      "timing reactive=%.2fms render=%.2fms present=%.2fms drawableWait=%.2fms budget[p50/p99]=%.2f/%.2fms "
      "prepare=%llu(%.2f/f) replay=%llu(%.2f/f) "
      "scene passes=%llu(%.2f/f) nodes=%llu(%.2f/f) reject=%llu cache=%llu/%llu "
      "draw r%llu(%.2f/f) i%llu(%.2f/f)",
      seconds,
      static_cast<unsigned long long>(interval.frames),
      perFrameMillis(interval.durationsNs[static_cast<std::size_t>(TimedMetric::ProcessReactiveUpdates)],
                     interval.frames),
      perFrameMillis(interval.durationsNs[static_cast<std::size_t>(TimedMetric::SceneRender)], interval.frames),
      perFrameMillis(interval.durationsNs[static_cast<std::size_t>(TimedMetric::CanvasPresent)], interval.frames),
      perFrameMillis(interval.durationsNs[static_cast<std::size_t>(TimedMetric::CanvasDrawableWait)],
                     interval.frames),
      frameBudgetPercentileMillis(interval, 0.50),
      frameBudgetPercentileMillis(interval, 0.99),
      static_cast<unsigned long long>(interval.preparedPrepareCalls),
      perFrame(interval.preparedPrepareCalls, interval.frames),
      static_cast<unsigned long long>(interval.preparedReplayCalls),
      perFrame(interval.preparedReplayCalls, interval.frames),
      static_cast<unsigned long long>(interval.scene.renderPasses),
      perFrame(interval.scene.renderPasses, interval.frames),
      static_cast<unsigned long long>(interval.scene.nodesVisited),
      perFrame(interval.scene.nodesVisited, interval.frames),
      static_cast<unsigned long long>(interval.scene.quickRejects),
      static_cast<unsigned long long>(interval.scene.preparedReplaySuccesses),
      static_cast<unsigned long long>(interval.scene.preparedReplayFailures),
      static_cast<unsigned long long>(interval.render.drawCalls[static_cast<std::size_t>(RenderCounterKind::Rect)]),
      perFrame(interval.render.drawCalls[static_cast<std::size_t>(RenderCounterKind::Rect)], interval.frames),
      static_cast<unsigned long long>(interval.render.drawCalls[static_cast<std::size_t>(RenderCounterKind::Image)]),
      perFrame(interval.render.drawCalls[static_cast<std::size_t>(RenderCounterKind::Image)], interval.frames));

  if (interval.render.drawCalls[static_cast<std::size_t>(RenderCounterKind::Path)] > 0) {
    std::fprintf(stderr, " p%llu(%.2f/f)",
                 static_cast<unsigned long long>(
                     interval.render.drawCalls[static_cast<std::size_t>(RenderCounterKind::Path)]),
                 perFrame(interval.render.drawCalls[static_cast<std::size_t>(RenderCounterKind::Path)],
                          interval.frames));
  }
  if (interval.render.drawCalls[static_cast<std::size_t>(RenderCounterKind::Glyph)] > 0) {
    std::fprintf(stderr, " g%llu(%.2f/f)",
                 static_cast<unsigned long long>(
                     interval.render.drawCalls[static_cast<std::size_t>(RenderCounterKind::Glyph)]),
                 perFrame(interval.render.drawCalls[static_cast<std::size_t>(RenderCounterKind::Glyph)],
                          interval.frames));
  }

  std::fprintf(stderr,
               " blur runs=%llu(%.2f/f) passes=%llu px=%.1fMP/f copy=%.1fMP/f"
               " uploadKB %.1f text layout=%llu hit=%llu miss=%llu",
               static_cast<unsigned long long>(interval.render.backdropBlurRuns),
               perFrame(interval.render.backdropBlurRuns, interval.frames),
               static_cast<unsigned long long>(interval.render.backdropBlurPasses),
               perFrame(interval.render.backdropBlurPixels, interval.frames) / 1'000'000.0,
               perFrame(interval.render.backdropCopyPixels, interval.frames) / 1'000'000.0,
               uploadKilobytesPerFrame(interval.render, 1),
               static_cast<unsigned long long>(interval.text.layoutCalls),
               static_cast<unsigned long long>(interval.text.layoutCacheHits),
               static_cast<unsigned long long>(interval.text.layoutCacheMisses));

  if (interval.render.recorderCapacityGrowths > 0) {
    std::fprintf(stderr, " recorderGrow=%llu growKB=%.1f",
                 static_cast<unsigned long long>(interval.render.recorderCapacityGrowths),
                 static_cast<double>(interval.render.recorderCapacityGrowthBytes) / 1024.0);
  }
  if (interval.render.glyphAtlasGrowths > 0) {
    std::fprintf(stderr, " atlasGrow=%llu growMP=%.1f",
                 static_cast<unsigned long long>(interval.render.glyphAtlasGrowths),
                 static_cast<double>(interval.render.glyphAtlasGrowPixels) / 1'000'000.0);
  }

  if (hasComponentKeyWork(interval.componentKeys)) {
    ComponentKeyCounters const& ck = interval.componentKeys;
    std::fprintf(stderr,
                 " ck copy=%llu/%lluid append=%llu/%lluid hash=%llu/%lluid eq=%llu/%lluid "
                 "prefix=%llu/%lluid grow=%llu",
                 static_cast<unsigned long long>(ck.copies),
                 static_cast<unsigned long long>(ck.copiedIds),
                 static_cast<unsigned long long>(ck.appends),
                 static_cast<unsigned long long>(ck.appendedIds),
                 static_cast<unsigned long long>(ck.hashCalls),
                 static_cast<unsigned long long>(ck.hashedIds),
                 static_cast<unsigned long long>(ck.equalityCalls),
                 static_cast<unsigned long long>(ck.equalityIds),
                 static_cast<unsigned long long>(ck.prefixCalls),
                 static_cast<unsigned long long>(ck.prefixIds),
                 static_cast<unsigned long long>(ck.heapGrowths));
  }

  std::fprintf(stderr, "\n");
}

inline bool shouldPrintAnomalyLine(IntervalCounters const& interval, std::uint64_t completedWindows) {
  constexpr std::uint64_t kWarmupWindows = 3;
  constexpr std::uint64_t kTextMissThreshold = 8;
  constexpr double kFrameBudgetP99TargetMs = 16.67;
  return completedWindows < kWarmupWindows ||
         interval.scene.preparedReplayFailures > 0 ||
         interval.render.recorderCapacityGrowths > 0 ||
         interval.text.layoutCacheMisses > kTextMissThreshold ||
         frameBudgetPercentileMillis(interval, 0.99) > kFrameBudgetP99TargetMs;
}

inline void logIfReady() {
  IntervalCounters& interval = counters();
  static std::uint64_t completedWindows = 0;
  auto const now = std::chrono::steady_clock::now();
  auto const elapsed = now - interval.startedAt;
  if (elapsed < std::chrono::seconds(1)) {
    return;
  }

  double const seconds = std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count();
  bool const anomalyOnly = perfAnomalyOnly();
  if (!anomalyOnly || shouldPrintAnomalyLine(interval, completedWindows)) {
    printSummaryLine(interval, seconds);
    if (perfVerbose()) {
      printVerboseLine(interval, seconds);
    }
  }

  ++completedWindows;
  interval.reset(now);
}

} // namespace detail

inline bool enabled() {
  return perfEnabled();
}

inline bool backdropBlurDiagnosticsEnabled() {
  static bool const enabled = [] {
    char const* cpuTrace = std::getenv("LAMBDA_WINDOW_MANAGER_CPU_TRACE");
    return perfEnabled() || debug::envNonZero(cpuTrace);
  }();
  return enabled;
}

inline RenderCounters renderCountersSnapshot() {
  return detail::counters().render;
}

inline void recordPreparedPrepareCall() {
  if (!enabled()) {
    return;
  }
  ++detail::counters().preparedPrepareCalls;
}

inline void recordPreparedReplayCall() {
  if (!enabled()) {
    return;
  }
  ++detail::counters().preparedReplayCalls;
}

inline void recordPreparedReplayResult(bool success) {
  if (!enabled()) {
    return;
  }
  if (success) {
    ++detail::counters().scene.preparedReplaySuccesses;
  } else {
    ++detail::counters().scene.preparedReplayFailures;
  }
}

inline void recordSceneRenderPass() {
  if (!enabled()) {
    return;
  }
  ++detail::counters().scene.renderPasses;
}

inline void recordSceneNodeVisit(bool group) {
  if (!enabled()) {
    return;
  }
  auto& scene = detail::counters().scene;
  ++scene.nodesVisited;
  if (group) {
    ++scene.groupsVisited;
  } else {
    ++scene.leavesVisited;
  }
}

inline void recordSceneQuickReject() {
  if (!enabled()) {
    return;
  }
  ++detail::counters().scene.quickRejects;
}

inline void recordLiveLeafRender() {
  if (!enabled()) {
    return;
  }
  ++detail::counters().scene.liveLeafRenders;
}

inline void recordFrameOps(std::uint64_t rectOps, std::uint64_t imageOps, std::uint64_t pathOps,
                           std::uint64_t glyphOps, std::uint64_t orderEntries,
                           std::uint64_t pathVertices, std::uint64_t glyphVertices) {
  if (!enabled()) {
    return;
  }
  auto& render = detail::counters().render;
  render.ops[static_cast<std::size_t>(RenderCounterKind::Rect)] += rectOps;
  render.ops[static_cast<std::size_t>(RenderCounterKind::Image)] += imageOps;
  render.ops[static_cast<std::size_t>(RenderCounterKind::Path)] += pathOps;
  render.ops[static_cast<std::size_t>(RenderCounterKind::Glyph)] += glyphOps;
  render.opOrderEntries += orderEntries;
  render.pathVertices += pathVertices;
  render.glyphVertices += glyphVertices;
}

inline void recordDrawCall(RenderCounterKind kind) {
  if (!enabled()) {
    return;
  }
  ++detail::counters().render.drawCalls[static_cast<std::size_t>(kind)];
}

inline void recordUploadBytes(RenderCounterKind kind, std::uint64_t bytes) {
  if (!enabled()) {
    return;
  }
  detail::counters().render.uploadBytes[static_cast<std::size_t>(kind)] += bytes;
}

inline void recordRecorderCapacityGrowth(std::uint64_t bytes) {
  if (!enabled()) {
    return;
  }
  auto& render = detail::counters().render;
  ++render.recorderCapacityGrowths;
  render.recorderCapacityGrowthBytes += bytes;
}

inline void recordGlyphAtlasGrowth(std::uint64_t oldPixels, std::uint64_t newPixels) {
  if (!enabled()) {
    return;
  }
  auto& render = detail::counters().render;
  ++render.glyphAtlasGrowths;
  render.glyphAtlasGrowPixels += newPixels > oldPixels ? newPixels - oldPixels : newPixels;
}

inline void recordVulkanRecordDuration(std::chrono::nanoseconds elapsed) {
  if (!backdropBlurDiagnosticsEnabled()) {
    return;
  }
  detail::counters().render.vulkanRecordNs += static_cast<std::uint64_t>(elapsed.count());
}

inline void recordVulkanDrawOps(std::chrono::nanoseconds elapsed, std::uint64_t visited,
                                std::uint64_t submitted, std::uint64_t scissorChanges) {
  if (!backdropBlurDiagnosticsEnabled()) {
    return;
  }
  auto& render = detail::counters().render;
  render.vulkanDrawOpsNs += static_cast<std::uint64_t>(elapsed.count());
  ++render.vulkanDrawOpsCalls;
  render.vulkanDrawOpsVisited += visited;
  render.vulkanDrawOpsSubmitted += submitted;
  render.vulkanScissorChanges += scissorChanges;
}

inline void recordVulkanStackedBlur(std::chrono::nanoseconds elapsed, std::uint64_t ops) {
  if (!backdropBlurDiagnosticsEnabled()) {
    return;
  }
  auto& render = detail::counters().render;
  render.vulkanStackedBlurNs += static_cast<std::uint64_t>(elapsed.count());
  render.vulkanStackedOps += ops;
}

inline void recordVulkanPathTessellation(std::chrono::nanoseconds elapsed) {
  if (!backdropBlurDiagnosticsEnabled()) {
    return;
  }
  auto& render = detail::counters().render;
  render.vulkanPathTessellateNs += static_cast<std::uint64_t>(elapsed.count());
  ++render.vulkanPathTessellations;
}

inline void recordBackdropBlurPreparedRun(std::uint64_t ops, std::uint64_t quads) {
  if (!backdropBlurDiagnosticsEnabled()) {
    return;
  }
  auto& render = detail::counters().render;
  ++render.backdropBlurPreparedRuns;
  render.backdropBlurOps += ops;
  render.backdropBlurQuads += quads;
}

inline void recordBackdropBlurCacheLookup(bool hit) {
  if (!backdropBlurDiagnosticsEnabled()) {
    return;
  }
  auto& render = detail::counters().render;
  if (hit) {
    ++render.backdropBlurCacheHits;
  } else {
    ++render.backdropBlurCacheMisses;
  }
}

inline void recordBackdropBlurRun(std::uint64_t copyPixels, std::uint64_t blurPixels, std::uint64_t passes) {
  if (!backdropBlurDiagnosticsEnabled()) {
    return;
  }
  auto& render = detail::counters().render;
  ++render.backdropBlurRuns;
  render.backdropCopyPixels += copyPixels;
  render.backdropBlurPixels += blurPixels;
  render.backdropBlurPasses += passes;
  render.backdropMaxCopyPixels = std::max(render.backdropMaxCopyPixels, copyPixels);
  render.backdropMaxBlurPixels = std::max(render.backdropMaxBlurPixels, blurPixels);
}

inline void recordTextLayoutCall() {
  if (!enabled()) {
    return;
  }
  ++detail::counters().text.layoutCalls;
}

inline void recordTextLayoutCacheHit() {
  if (!enabled()) {
    return;
  }
  ++detail::counters().text.layoutCacheHits;
}

inline void recordTextLayoutCacheMiss() {
  if (!enabled()) {
    return;
  }
  ++detail::counters().text.layoutCacheMisses;
}

inline void recordTextParagraphVariantHit() {
  if (!enabled()) {
    return;
  }
  ++detail::counters().text.paragraphVariantHits;
}

inline void recordTextParagraphVariantMiss() {
  if (!enabled()) {
    return;
  }
  ++detail::counters().text.paragraphVariantMisses;
}

inline void recordDuration(TimedMetric metric, std::chrono::nanoseconds elapsed) {
  if (!enabled()) {
    return;
  }
  auto& interval = detail::counters();
  std::uint64_t const nanos = static_cast<std::uint64_t>(elapsed.count());
  interval.durationsNs[static_cast<std::size_t>(metric)] += nanos;
  if (metric == TimedMetric::DisplayLinkToPresent &&
      interval.frameBudgetSampleCount < detail::IntervalCounters::kMaxFrameBudgetSamples) {
    interval.frameBudgetSamplesNs[interval.frameBudgetSampleCount++] = nanos;
  }
}

inline void recordComponentKeyCopy(std::uint64_t copiedIds) {
  if (!enabled()) {
    return;
  }
  auto& counters = detail::counters().componentKeys;
  ++counters.copies;
  counters.copiedIds += copiedIds;
}

inline void recordComponentKeyAppend(std::uint64_t resultingIds) {
  if (!enabled()) {
    return;
  }
  auto& counters = detail::counters().componentKeys;
  ++counters.appends;
  counters.appendedIds += resultingIds;
}

inline void recordComponentKeyHash(std::uint64_t hashedIds) {
  if (!enabled()) {
    return;
  }
  auto& counters = detail::counters().componentKeys;
  ++counters.hashCalls;
  counters.hashedIds += hashedIds;
}

inline void recordComponentKeyEquality(std::uint64_t comparedIds) {
  if (!enabled()) {
    return;
  }
  auto& counters = detail::counters().componentKeys;
  ++counters.equalityCalls;
  counters.equalityIds += comparedIds;
}

inline void recordComponentKeyPrefixCompare(std::uint64_t comparedIds) {
  if (!enabled()) {
    return;
  }
  auto& counters = detail::counters().componentKeys;
  ++counters.prefixCalls;
  counters.prefixIds += comparedIds;
}

inline void recordComponentKeyHeapGrowth(std::uint64_t capacity) {
  if (!enabled()) {
    return;
  }
  auto& counters = detail::counters().componentKeys;
  ++counters.heapGrowths;
  counters.heapCapacity += capacity;
}

inline void recordPresentedFrame() {
  if (!enabled()) {
    return;
  }
  ++detail::counters().frames;
  detail::logIfReady();
}

class ScopedTimer {
public:
  explicit ScopedTimer(TimedMetric metric)
      : metric_(metric), enabled_(perf::enabled()),
        startedAt_(enabled_ ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{}) {}

  ~ScopedTimer() {
    if (!enabled_) {
      return;
    }
    recordDuration(metric_, std::chrono::steady_clock::now() - startedAt_);
  }

  ScopedTimer(ScopedTimer const&) = delete;
  ScopedTimer& operator=(ScopedTimer const&) = delete;

private:
  TimedMetric metric_;
  bool enabled_ = false;
  std::chrono::steady_clock::time_point startedAt_{};
};

} // namespace lambda::debug::perf
