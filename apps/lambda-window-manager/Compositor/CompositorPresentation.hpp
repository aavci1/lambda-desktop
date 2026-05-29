#pragma once

#include <Lambda/Debug/DebugFlags.hpp>

#include "Compositor/Diagnostics/CpuTrace.hpp"

#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace lambda::compositor::presentation {

using SteadyClock = std::chrono::steady_clock;

[[nodiscard]] inline std::uint32_t monotonicMilliseconds() {
  using Clock = std::chrono::steady_clock;
  auto const now = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch());
  return static_cast<std::uint32_t>(now.count());
}

[[nodiscard]] inline std::uint64_t monotonicNanoseconds() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<std::uint64_t>(now.tv_sec) * 1'000'000'000ull + static_cast<std::uint64_t>(now.tv_nsec);
}

[[nodiscard]] inline double elapsedMilliseconds(SteadyClock::time_point start) {
  return std::chrono::duration<double, std::milli>(SteadyClock::now() - start).count();
}

[[nodiscard]] inline bool timingTraceEnabled() {
  static bool const enabled = debug::envNonZero(std::getenv("LAMBDA_WINDOW_MANAGER_TIMING"));
  return enabled;
}

[[nodiscard]] inline bool pacingTraceEnabled() {
  static bool const enabled = debug::envNonZero(std::getenv("LAMBDA_WINDOW_MANAGER_PACING_TRACE"));
  return enabled;
}

inline void traceTiming(char const* label, SteadyClock::time_point start) {
  if (!timingTraceEnabled()) return;
  std::fprintf(stderr, "lambda-window-manager: timing %s %.3fms\n", label, elapsedMilliseconds(start));
}

inline void tracePacing(char const* format, ...) {
  if (!pacingTraceEnabled()) return;
  std::uint64_t const now = monotonicNanoseconds();

  auto write = [&](FILE* file, va_list args) {
    std::fprintf(file, "pacing-trace: %.3fms ", static_cast<double>(now) / 1'000'000.0);
    std::vfprintf(file, format, args);
  };

  va_list args;
  va_start(args, format);
  va_list stderrArgs;
  va_copy(stderrArgs, args);
  write(stderr, stderrArgs);
  va_end(stderrArgs);

  char const* path = std::getenv("LAMBDA_WINDOW_MANAGER_PACING_TRACE_LOG");
  if (!path || !*path) path = "/tmp/lambda-window-manager-pacing.log";
  if (FILE* file = std::fopen(path, "a")) {
    write(file, args);
    std::fclose(file);
  }
  va_end(args);
}

[[nodiscard]] inline std::uint32_t refreshNsec(std::uint32_t refreshMilliHz) {
  return refreshMilliHz > 0
             ? static_cast<std::uint32_t>(1'000'000'000'000ull / static_cast<std::uint64_t>(refreshMilliHz))
             : 0u;
}

[[nodiscard]] inline std::uint64_t clampRenderAheadLeadNsec(std::uint64_t lead, std::uint32_t refreshMilliHz) {
  std::uint64_t const refresh = refreshNsec(refreshMilliHz);
  if (refresh > 2'000'000ull) {
    lead = std::min<std::uint64_t>(lead, refresh - 1'000'000ull);
  }
  return std::max<std::uint64_t>(lead, 1'000'000ull);
}

[[nodiscard]] inline std::uint64_t nominalRenderAheadLeadNsec(std::uint32_t refreshMilliHz) {
  std::uint64_t const refresh = refreshNsec(refreshMilliHz);
  return refresh > 0 ? (refresh * 3ull) / 8ull : 6'000'000ull;
}

[[nodiscard]] inline std::uint64_t clampInteractiveRenderAheadLeadNsec(std::uint64_t lead,
                                                                       std::uint32_t refreshMilliHz) {
  std::uint64_t const refresh = refreshNsec(refreshMilliHz);
  if (refresh == 0) return clampRenderAheadLeadNsec(lead, refreshMilliHz);
  std::uint64_t const minimumLead = std::max<std::uint64_t>(refresh / 4ull, 1'000'000ull);
  std::uint64_t const maximumLead =
      refresh > 2'000'000ull ? std::max<std::uint64_t>(minimumLead, refresh - 1'000'000ull) : minimumLead;
  return std::clamp(lead, minimumLead, maximumLead);
}

[[nodiscard]] inline std::uint64_t initialRenderAheadLeadNsec(std::uint32_t refreshMilliHz) {
  static double const configuredMs = [] {
    char const* value = std::getenv("LAMBDA_WINDOW_MANAGER_RENDER_AHEAD_LEAD_MS");
    if (!value || !*value) return 0.0;
    char* end = nullptr;
    double parsed = std::strtod(value, &end);
    return end != value && parsed > 0.0 ? parsed : 0.0;
  }();
  std::uint64_t const configuredLead = static_cast<std::uint64_t>(configuredMs * 1'000'000.0);
  std::uint64_t const defaultLead = nominalRenderAheadLeadNsec(refreshMilliHz);
  if (configuredLead > 0) return clampRenderAheadLeadNsec(configuredLead, refreshMilliHz);
  return clampInteractiveRenderAheadLeadNsec(defaultLead, refreshMilliHz);
}

struct LoopInstrumentation {
  using Clock = std::chrono::steady_clock;

  bool enabled = debug::envNonZero(std::getenv("LAMBDA_WINDOW_MANAGER_IDLE_PROFILE"));
  Clock::time_point windowStart = Clock::now();
  std::uint64_t loops = 0;
  std::uint64_t frames = 0;
  std::uint64_t idleSkips = 0;
  std::uint64_t polls = 0;
  std::uint64_t pollWakeups = 0;
  std::uint64_t dispatches = 0;
  std::uint64_t vtSleeps = 0;
  std::uint64_t configChecks = 0;
  std::uint64_t lastSurfaceCount = 0;
  double pollMs = 0.0;
  double dispatchMs = 0.0;
  double vblankMs = 0.0;
  double renderMs = 0.0;

  static double milliseconds(Clock::time_point begin, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
  }

  void recordPoll(Clock::time_point begin, bool woke, int timeoutMs) {
    ++polls;
    if (woke) ++pollWakeups;
    if (!enabled && !diagnostics::cpuTraceEnabled()) return;
    double const elapsed = milliseconds(begin, Clock::now());
    pollMs += elapsed;
    diagnostics::recordCpuPoll(elapsed, woke, timeoutMs);
  }

  void recordDispatch(Clock::time_point begin) {
    ++dispatches;
    if (!enabled && !diagnostics::cpuTraceEnabled()) return;
    double const elapsed = milliseconds(begin, Clock::now());
    dispatchMs += elapsed;
    diagnostics::recordCpuDispatch(elapsed);
  }

  void recordVblank(Clock::time_point begin) {
    if (!enabled) return;
    vblankMs += milliseconds(begin, Clock::now());
  }

  void recordRender(Clock::time_point begin) {
    ++frames;
    if (!enabled) return;
    renderMs += milliseconds(begin, Clock::now());
  }

  void maybeLog() {
    if (!enabled) return;
    auto const now = Clock::now();
    double const elapsedMs = milliseconds(windowStart, now);
    if (elapsedMs < 2000.0) return;

    double const seconds = elapsedMs / 1000.0;
    std::fprintf(stderr,
                 "lambda-window-manager: idle-profile %.1fs loops=%llu frames=%llu fps=%.1f polls=%llu poll_wakeups=%llu "
                 "idle_skips=%llu dispatches=%llu vt_sleeps=%llu config_checks=%llu surfaces=%llu poll_ms=%.2f "
                 "dispatch_ms=%.2f vblank_ms=%.2f render_ms=%.2f\n",
                 seconds,
                 static_cast<unsigned long long>(loops),
                 static_cast<unsigned long long>(frames),
                 frames / seconds,
                 static_cast<unsigned long long>(polls),
                 static_cast<unsigned long long>(pollWakeups),
                 static_cast<unsigned long long>(idleSkips),
                 static_cast<unsigned long long>(dispatches),
                 static_cast<unsigned long long>(vtSleeps),
                 static_cast<unsigned long long>(configChecks),
                 static_cast<unsigned long long>(lastSurfaceCount),
                 pollMs,
                 dispatchMs,
                 vblankMs,
                 renderMs);

    windowStart = now;
    loops = 0;
    frames = 0;
    idleSkips = 0;
    polls = 0;
    pollWakeups = 0;
    dispatches = 0;
    vtSleeps = 0;
    configChecks = 0;
    pollMs = 0.0;
    dispatchMs = 0.0;
    vblankMs = 0.0;
    renderMs = 0.0;
  }
};

struct CompositorFrameProfile {
  using Clock = std::chrono::steady_clock;

  bool enabled = debug::envNonZero(std::getenv("LAMBDA_WINDOW_MANAGER_PROFILE"));
  Clock::time_point windowStart = Clock::now();
  std::uint64_t frames = 0;
  std::uint64_t surfaces = 0;
  double backgroundMs = 0.0;
  double snapshotMs = 0.0;
  double surfaceMs = 0.0;
  double closingMs = 0.0;
  double cursorMs = 0.0;
  double presentMs = 0.0;
  double totalMs = 0.0;

  static double milliseconds(Clock::time_point begin, Clock::time_point end = Clock::now()) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
  }

  void maybeLog() {
    if (!enabled) return;
    auto const now = Clock::now();
    double const elapsedMs = milliseconds(windowStart, now);
    if (elapsedMs < 2000.0 || frames == 0) return;

    double const invFrames = 1.0 / static_cast<double>(frames);
    std::fprintf(stderr,
                 "lambda-window-manager: frame-profile %.1fs frames=%llu fps=%.1f surfaces=%.2f/f "
                 "total=%.3fms bg=%.3fms snapshots=%.3fms surfaces=%.3fms closing=%.3fms "
                 "cursor=%.3fms present=%.3fms\n",
                 elapsedMs / 1000.0,
                 static_cast<unsigned long long>(frames),
                 frames / (elapsedMs / 1000.0),
                 static_cast<double>(surfaces) * invFrames,
                 totalMs * invFrames,
                 backgroundMs * invFrames,
                 snapshotMs * invFrames,
                 surfaceMs * invFrames,
                 closingMs * invFrames,
                 cursorMs * invFrames,
                 presentMs * invFrames);

    windowStart = now;
    frames = 0;
    surfaces = 0;
    backgroundMs = 0.0;
    snapshotMs = 0.0;
    surfaceMs = 0.0;
    closingMs = 0.0;
    cursorMs = 0.0;
    presentMs = 0.0;
    totalMs = 0.0;
  }
};

struct AtomicFrameProfile {
  double backgroundMs = 0.0;
  double snapshotMs = 0.0;
  double surfaceMs = 0.0;
  double closingMs = 0.0;
  double cursorMs = 0.0;
  double presentMs = 0.0;
  double canvasPresentMs = 0.0;
  double kmsPresentMs = 0.0;
  double totalMs = 0.0;
  std::size_t activeSizingSurfaces = 0;
  std::int32_t maxBufferWidth = 0;
  std::int32_t maxBufferHeight = 0;
  std::int32_t maxFrameWidth = 0;
  std::int32_t maxFrameHeight = 0;
  std::uint32_t maxDmabufFormat = 0;
  std::uint64_t maxDmabufModifier = 0;
  std::uint64_t maxAgeSurfaceId = 0;
  double maxInputToRenderMs = 0.0;
  double maxConfigureToRenderMs = 0.0;
  double maxAckToRenderMs = 0.0;
  double maxCommitToRenderMs = 0.0;
  double maxConfigureToCommitMs = 0.0;
};

} // namespace lambda::compositor::presentation

#define LAMBDA_WINDOW_MANAGER_TRACE_TIMING(...)                                      \
  do {                                                                              \
    if (::lambda::compositor::presentation::timingTraceEnabled()) {                 \
      ::lambda::compositor::presentation::traceTiming(__VA_ARGS__);                 \
    }                                                                               \
  } while (false)

#define LAMBDA_WINDOW_MANAGER_TRACE_PACING(...)                                     \
  do {                                                                              \
    if (::lambda::compositor::presentation::pacingTraceEnabled()) {                 \
      ::lambda::compositor::presentation::tracePacing(__VA_ARGS__);                 \
    }                                                                               \
  } while (false)
