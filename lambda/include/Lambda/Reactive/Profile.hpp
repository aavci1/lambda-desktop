#pragma once

#include <cstdint>

#ifndef LAMBDAUI_PROFILE_REACTIVE
#define LAMBDAUI_PROFILE_REACTIVE 0
#endif

#if LAMBDAUI_PROFILE_REACTIVE
#include <cstdio>
#include <mach/mach_time.h>
#endif

namespace lambdaui::Reactive::detail::profile {

enum class Bucket : std::uint8_t {
  SignalSet,
  EffectRun,
  PollSourcesChanged,
  PropagatePending,
  FlushEffects,
  Count,
};

#if LAMBDAUI_PROFILE_REACTIVE

namespace detail {

inline constexpr std::uint64_t kDumpIntervalNanos = 5'000'000'000ull;

struct Counters {
  std::uint64_t buckets[static_cast<std::uint8_t>(Bucket::Count)]{};
  std::uint64_t frames = 0;
  std::uint64_t frameIntervalNanos = 0;
};

struct ActiveTimer {
  Bucket bucket = Bucket::SignalSet;
  std::uint64_t startTick = 0;
  std::uint64_t childTicks = 0;
  ActiveTimer* parent = nullptr;
};

inline thread_local Counters counters{};
inline thread_local ActiveTimer* activeTimer = nullptr;
inline thread_local std::uint64_t startTick = 0;
inline thread_local std::uint64_t lastDumpTick = 0;
inline thread_local std::uint64_t lastFrameTick = 0;

inline double nanosPerTick() {
  static double scale = [] {
    mach_timebase_info_data_t info{};
    mach_timebase_info(&info);
    return static_cast<double>(info.numer) / static_cast<double>(info.denom);
  }();
  return scale;
}

inline std::uint64_t nowTicks() {
  return mach_absolute_time();
}

inline std::uint64_t ticksToNanos(std::uint64_t ticks) {
  return static_cast<std::uint64_t>(static_cast<long double>(ticks) * nanosPerTick());
}

inline double percent(std::uint64_t nanos, std::uint64_t elapsedNanos) {
  if (elapsedNanos == 0) {
    return 0.0;
  }
  return (static_cast<double>(nanos) * 100.0) / static_cast<double>(elapsedNanos);
}

inline std::uint64_t bucketNanos(Bucket bucket) {
  return counters.buckets[static_cast<std::uint8_t>(bucket)];
}

inline std::uint64_t reactiveTotalNanos() {
  std::uint64_t total = 0;
  for (std::uint8_t index = 0; index < static_cast<std::uint8_t>(Bucket::Count); ++index) {
    total += counters.buckets[index];
  }
  return total;
}

inline void resetCounters() {
  counters = Counters{};
}

inline void dumpAndReset(std::uint64_t now) {
  std::uint64_t const elapsedNanos = ticksToNanos(now - lastDumpTick);
  std::uint64_t const totalElapsedNanos = ticksToNanos(now - startTick);
  double const elapsedSeconds = static_cast<double>(totalElapsedNanos) / 1'000'000'000.0;

  std::fprintf(
      stderr,
      "[lambda:reactive-profile] %.0fs frames=%llu reactive_total=%.2f%% "
      "signal_set=%.2f%% effect_runs=%.2f%% poll=%.2f%% propagation=%.2f%% "
      "flush=%.2f%% frame_interval=%.1f%%\n",
      elapsedSeconds,
      static_cast<unsigned long long>(counters.frames),
      percent(reactiveTotalNanos(), elapsedNanos),
      percent(bucketNanos(Bucket::SignalSet), elapsedNanos),
      percent(bucketNanos(Bucket::EffectRun), elapsedNanos),
      percent(bucketNanos(Bucket::PollSourcesChanged), elapsedNanos),
      percent(bucketNanos(Bucket::PropagatePending), elapsedNanos),
      percent(bucketNanos(Bucket::FlushEffects), elapsedNanos),
      percent(counters.frameIntervalNanos, elapsedNanos));
  std::fflush(stderr);

  resetCounters();
  lastDumpTick = now;
}

} // namespace detail

class ScopedTimer {
public:
  explicit ScopedTimer(Bucket bucket)
      : timer_{.bucket = bucket,
               .startTick = detail::nowTicks(),
               .childTicks = 0,
               .parent = detail::activeTimer}
      , active_(true) {
    detail::activeTimer = &timer_;
  }

  ScopedTimer(ScopedTimer const&) = delete;
  ScopedTimer& operator=(ScopedTimer const&) = delete;

  ~ScopedTimer() {
    if (!active_) {
      return;
    }
    std::uint64_t const elapsedTicks = detail::nowTicks() - timer_.startTick;
    std::uint64_t const selfTicks =
        elapsedTicks > timer_.childTicks ? elapsedTicks - timer_.childTicks : 0;
    detail::counters.buckets[static_cast<std::uint8_t>(timer_.bucket)] +=
        detail::ticksToNanos(selfTicks);
    detail::activeTimer = timer_.parent;
    if (timer_.parent) {
      timer_.parent->childTicks += elapsedTicks;
    }
  }

private:
  detail::ActiveTimer timer_;
  bool active_ = false;
};

inline void frameBoundary() {
  std::uint64_t const now = detail::nowTicks();
  if (detail::startTick == 0) {
    detail::startTick = now;
    detail::lastDumpTick = now;
    detail::lastFrameTick = now;
    detail::counters.frames = 1;
    return;
  }

  detail::counters.frameIntervalNanos += detail::ticksToNanos(now - detail::lastFrameTick);
  detail::lastFrameTick = now;
  ++detail::counters.frames;

  if (detail::ticksToNanos(now - detail::lastDumpTick) >= detail::kDumpIntervalNanos) {
    detail::dumpAndReset(now);
  }
}

#else

class ScopedTimer {
public:
  explicit constexpr ScopedTimer(Bucket) noexcept {}
};

inline void frameBoundary() {}

#endif

} // namespace lambdaui::Reactive::detail::profile
