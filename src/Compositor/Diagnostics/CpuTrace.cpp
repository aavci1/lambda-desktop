#include "Compositor/Diagnostics/CpuTrace.hpp"

#include <Flux/Debug/DebugFlags.hpp>

#include <dlfcn.h>
#include <execinfo.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cxxabi.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>

namespace flux::compositor::diagnostics {
namespace {

constexpr int kMaxCpuSamples = 4096;
constexpr int kMaxCpuSampleFrames = 12;

void *gCpuSamples[kMaxCpuSamples]{};
volatile sig_atomic_t gCpuSampleWriteIndex = 0;
volatile sig_atomic_t gCpuSampleTotal = 0;
volatile sig_atomic_t gCpuSampleLastRead = 0;

struct CpuTraceState {
  CpuTraceClock::time_point windowStart = CpuTraceClock::now();
  double cpuStartMs = 0.0;
  std::FILE *file = nullptr;
  std::uint64_t frames = 0;
  std::uint64_t loops = 0;
  std::uint64_t idleSkips = 0;
  std::uint64_t polls = 0;
  std::uint64_t pollWakeups = 0;
  std::uint64_t zeroTimeoutPolls = 0;
  std::uint64_t inputOrSystemWakeups = 0;
  std::uint64_t waylandWakeups = 0;
  std::uint64_t pageFlipWakeups = 0;
  std::uint64_t renderReadyWakeups = 0;
  std::uint64_t unknownWakeups = 0;
  std::uint64_t dispatches = 0;
  std::uint64_t waylandDispatches = 0;
  std::uint64_t waylandDispatchesChanged = 0;
  std::uint64_t surfaces = 0;
  double pollMs = 0.0;
  double dispatchMs = 0.0;
  double backgroundMs = 0.0;
  double snapshotMs = 0.0;
  double surfaceMs = 0.0;
  double closingMs = 0.0;
  double launcherMs = 0.0;
  double cursorMs = 0.0;
  double presentMs = 0.0;
  double canvasPresentMs = 0.0;
  double kmsPresentMs = 0.0;
  double totalMs = 0.0;
  double maxTotalMs = 0.0;
  double maxSurfaceMs = 0.0;
  double maxPresentMs = 0.0;
  std::uint64_t shmCopies = 0;
  std::size_t shmBytes = 0;
  double shmCopyMs = 0.0;
  std::uint64_t imageCreates = 0;
  std::uint64_t imageUpdates = 0;
  std::size_t imageBytes = 0;
  double imageUploadMs = 0.0;
  std::uint64_t dmabufImports = 0;
  std::uint64_t dmabufImportFailures = 0;
  double dmabufImportMs = 0.0;
  std::uint64_t dmabufFallbackCopies = 0;
  std::uint64_t dmabufFallbackFailures = 0;
  std::size_t dmabufFallbackBytes = 0;
  double dmabufFallbackMs = 0.0;
  std::uint64_t surfaceCommits = 0;
  std::uint64_t surfaceStateCommits = 0;
  std::uint64_t surfaceShmCommits = 0;
  std::uint64_t surfaceDmabufCommits = 0;
  std::uint64_t surfaceEmptyCommits = 0;
  std::uint64_t surfaceOtherCommits = 0;
  std::uint64_t surfaceDrawCacheHits = 0;
  std::uint64_t surfaceDrawCacheMisses = 0;
  double surfaceDrawRecordMs = 0.0;
  std::uint64_t hottestSurfaceId = 0;
  std::uint64_t hottestSurfaceCommits = 0;
  std::int32_t hottestSurfaceWidth = 0;
  std::int32_t hottestSurfaceHeight = 0;
  std::uint64_t currentSurfaceId = 0;
  std::uint64_t currentSurfaceCommits = 0;
  std::int32_t currentSurfaceWidth = 0;
  std::int32_t currentSurfaceHeight = 0;
};

std::mutex &traceMutex() {
  static std::mutex mutex;
  return mutex;
}

bool cpuSamplerEnabled() {
  static bool const enabled = [] {
    char const *value = std::getenv("FLUX_COMPOSITOR_SAMPLE_TRACE");
    return debug::envNonZero(value);
  }();
  return enabled;
}

int cpuSamplerIntervalUsec() {
  static int const interval = [] {
    char const *value = std::getenv("FLUX_COMPOSITOR_SAMPLE_USEC");
    if (!value || !*value)
      return 2000;
    int const parsed = std::atoi(value);
    return parsed > 0 ? parsed : 2000;
  }();
  return interval;
}

void cpuSamplerSignalHandler(int) {
  void *frames[kMaxCpuSampleFrames]{};
  int const count = backtrace(frames, kMaxCpuSampleFrames);
  if (count <= 2)
    return;
  sig_atomic_t const writeIndex = gCpuSampleWriteIndex;
  gCpuSampleWriteIndex = writeIndex + 1;
  gCpuSamples[writeIndex % kMaxCpuSamples] = frames[2];
  gCpuSampleTotal = gCpuSampleTotal + 1;
}

std::string symbolName(void *pc) {
  Dl_info info{};
  if (dladdr(pc, &info) == 0 || !info.dli_sname) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%p", pc);
    return buffer;
  }
  int status = 0;
  char *demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
  std::string name = status == 0 && demangled ? demangled : info.dli_sname;
  std::free(demangled);
  return name;
}

void writeCpuSamples(std::FILE *file) {
  if (!cpuSamplerEnabled())
    return;
  sig_atomic_t const total = gCpuSampleTotal;
  sig_atomic_t const last = gCpuSampleLastRead;
  if (total <= last)
    return;
  sig_atomic_t available = total - last;
  if (available > kMaxCpuSamples)
    available = kMaxCpuSamples;

  struct Hit {
    void *pc = nullptr;
    int count = 0;
  };
  std::array<Hit, 32> hits{};
  int uniqueHits = 0;
  for (sig_atomic_t i = 0; i < available; ++i) {
    sig_atomic_t const sampleIndex = total - available + i;
    void *pc = gCpuSamples[sampleIndex % kMaxCpuSamples];
    if (!pc)
      continue;
    auto existing = std::find_if(hits.begin(), hits.begin() + uniqueHits, [&](Hit const &hit) {
      return hit.pc == pc;
    });
    if (existing != hits.begin() + uniqueHits) {
      ++existing->count;
      continue;
    }
    if (uniqueHits < static_cast<int>(hits.size())) {
      hits[uniqueHits++] = Hit{.pc = pc, .count = 1};
      continue;
    }
    auto smallest = std::min_element(hits.begin(), hits.end(), [](Hit const &a, Hit const &b) {
      return a.count < b.count;
    });
    if (smallest != hits.end() && smallest->count <= 1) {
      *smallest = Hit{.pc = pc, .count = 1};
    }
  }
  gCpuSampleLastRead = total;
  if (uniqueHits == 0)
    return;
  std::sort(hits.begin(), hits.begin() + uniqueHits, [](Hit const &a, Hit const &b) {
    return a.count > b.count;
  });
  std::fprintf(file, "cpu-samples: samples=%d", static_cast<int>(available));
  int const topCount = std::min(uniqueHits, 8);
  for (int i = 0; i < topCount; ++i) {
    std::string name = symbolName(hits[i].pc);
    std::fprintf(file, " top%d=%d:%s", i + 1, hits[i].count, name.c_str());
  }
  std::fprintf(file, "\n");
}

double processCpuMilliseconds() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0)
    return 0.0;
  auto const userMs =
      static_cast<double>(usage.ru_utime.tv_sec) * 1000.0 + static_cast<double>(usage.ru_utime.tv_usec) / 1000.0;
  auto const systemMs =
      static_cast<double>(usage.ru_stime.tv_sec) * 1000.0 + static_cast<double>(usage.ru_stime.tv_usec) / 1000.0;
  return userMs + systemMs;
}

CpuTraceState &state() {
  static CpuTraceState traceState = [] {
    CpuTraceState result;
    result.cpuStartMs = processCpuMilliseconds();
    return result;
  }();
  return traceState;
}

std::string defaultTracePath() {
  if (char const *configured = std::getenv("FLUX_COMPOSITOR_CPU_TRACE_LOG"); configured && *configured) {
    return configured;
  }
  if (char const *stateHome = std::getenv("XDG_STATE_HOME"); stateHome && *stateHome) {
    return std::string(stateHome) + "/flux-compositor/cpu.log";
  }
  if (char const *home = std::getenv("HOME"); home && *home) {
    return std::string(home) + "/.local/state/flux-compositor/cpu.log";
  }
  return "/tmp/flux-compositor-cpu.log";
}

char const *tracePath() {
  static std::string const path = defaultTracePath();
  return path.c_str();
}

char const *fallbackTracePath() {
  char const *path = std::getenv("FLUX_COMPOSITOR_CPU_TRACE_LOG");
  return path && *path ? path : "/tmp/flux-compositor-cpu.log";
}

std::FILE *traceFile(CpuTraceState &traceState) {
  if (traceState.file)
    return traceState.file;
  try {
    std::filesystem::path const path(tracePath());
    if (path.has_parent_path())
      std::filesystem::create_directories(path.parent_path());
  } catch (...) {
  }
  traceState.file = std::fopen(tracePath(), "a");
  if (!traceState.file && std::strcmp(tracePath(), fallbackTracePath()) != 0) {
    traceState.file = std::fopen(fallbackTracePath(), "a");
  }
  return traceState.file;
}

void resetCounters(CpuTraceState &traceState, CpuTraceClock::time_point now, double cpuNowMs) {
  traceState.windowStart = now;
  traceState.cpuStartMs = cpuNowMs;
  traceState.frames = 0;
  traceState.loops = 0;
  traceState.idleSkips = 0;
  traceState.polls = 0;
  traceState.pollWakeups = 0;
  traceState.zeroTimeoutPolls = 0;
  traceState.inputOrSystemWakeups = 0;
  traceState.waylandWakeups = 0;
  traceState.pageFlipWakeups = 0;
  traceState.renderReadyWakeups = 0;
  traceState.unknownWakeups = 0;
  traceState.dispatches = 0;
  traceState.waylandDispatches = 0;
  traceState.waylandDispatchesChanged = 0;
  traceState.surfaces = 0;
  traceState.pollMs = 0.0;
  traceState.dispatchMs = 0.0;
  traceState.backgroundMs = 0.0;
  traceState.snapshotMs = 0.0;
  traceState.surfaceMs = 0.0;
  traceState.closingMs = 0.0;
  traceState.launcherMs = 0.0;
  traceState.cursorMs = 0.0;
  traceState.presentMs = 0.0;
  traceState.canvasPresentMs = 0.0;
  traceState.kmsPresentMs = 0.0;
  traceState.totalMs = 0.0;
  traceState.maxTotalMs = 0.0;
  traceState.maxSurfaceMs = 0.0;
  traceState.maxPresentMs = 0.0;
  traceState.shmCopies = 0;
  traceState.shmBytes = 0;
  traceState.shmCopyMs = 0.0;
  traceState.imageCreates = 0;
  traceState.imageUpdates = 0;
  traceState.imageBytes = 0;
  traceState.imageUploadMs = 0.0;
  traceState.dmabufImports = 0;
  traceState.dmabufImportFailures = 0;
  traceState.dmabufImportMs = 0.0;
  traceState.dmabufFallbackCopies = 0;
  traceState.dmabufFallbackFailures = 0;
  traceState.dmabufFallbackBytes = 0;
  traceState.dmabufFallbackMs = 0.0;
  traceState.surfaceCommits = 0;
  traceState.surfaceStateCommits = 0;
  traceState.surfaceShmCommits = 0;
  traceState.surfaceDmabufCommits = 0;
  traceState.surfaceEmptyCommits = 0;
  traceState.surfaceOtherCommits = 0;
  traceState.surfaceDrawCacheHits = 0;
  traceState.surfaceDrawCacheMisses = 0;
  traceState.surfaceDrawRecordMs = 0.0;
  traceState.hottestSurfaceId = 0;
  traceState.hottestSurfaceCommits = 0;
  traceState.hottestSurfaceWidth = 0;
  traceState.hottestSurfaceHeight = 0;
  traceState.currentSurfaceId = 0;
  traceState.currentSurfaceCommits = 0;
  traceState.currentSurfaceWidth = 0;
  traceState.currentSurfaceHeight = 0;
}

void maybeLog(CpuTraceState &traceState) {
  auto const now = CpuTraceClock::now();
  double const elapsedMs = cpuTraceElapsedMilliseconds(traceState.windowStart);
  if (elapsedMs < 1000.0)
    return;

  double const cpuNowMs = processCpuMilliseconds();
  double const cpuPercent = elapsedMs > 0.0 ? (cpuNowMs - traceState.cpuStartMs) * 100.0 / elapsedMs : 0.0;
  double const seconds = elapsedMs / 1000.0;
  double const invFrames = traceState.frames > 0 ? 1.0 / static_cast<double>(traceState.frames) : 0.0;
  double const shmMb = static_cast<double>(traceState.shmBytes) / (1024.0 * 1024.0);
  double const imageMb = static_cast<double>(traceState.imageBytes) / (1024.0 * 1024.0);
  double const fallbackMb = static_cast<double>(traceState.dmabufFallbackBytes) / (1024.0 * 1024.0);

  if (std::FILE *file = traceFile(traceState)) {
    std::fprintf(
        file,
        "cpu-trace: window=%.2fs cpu=%.1f%% loops=%llu idle_skips=%llu frames=%llu "
        "fps=%.1f polls=%llu zero_polls=%llu wakeups=%llu "
        "wake_src input_system=%llu wayland=%llu pageflip=%llu render_ready=%llu unknown=%llu "
        "poll_ms=%.3f dispatches=%llu dispatch_ms=%.3f "
        "wayland_dispatches=%llu changed=%llu unchanged=%llu "
        "surfaces=%.2f/f "
        "phase_avg_ms total=%.3f bg=%.3f snapshot=%.3f surface=%.3f closing=%.3f "
        "launcher=%.3f cursor=%.3f present=%.3f canvas_present=%.3f kms_present=%.3f "
        "max_total=%.3f max_surface=%.3f "
        "max_present=%.3f shm copies=%llu mb=%.1f mbps=%.1f copy_ms=%.3f "
        "image creates=%llu updates=%llu mb=%.1f mbps=%.1f upload_ms=%.3f "
        "dmabuf imports=%llu failures=%llu import_ms=%.3f fallback_copies=%llu "
        "fallback_failures=%llu fallback_mb=%.1f fallback_ms=%.3f "
        "surface_draw_cache hits=%llu misses=%llu record_ms=%.3f "
        "commits total=%llu state=%llu shm=%llu dmabuf=%llu empty=%llu other=%llu "
        "hot_surface=%llu hot_commits=%llu hot_size=%dx%d\n",
        seconds, cpuPercent, static_cast<unsigned long long>(traceState.loops),
        static_cast<unsigned long long>(traceState.idleSkips), static_cast<unsigned long long>(traceState.frames),
        static_cast<double>(traceState.frames) / seconds, static_cast<unsigned long long>(traceState.polls),
        static_cast<unsigned long long>(traceState.zeroTimeoutPolls),
        static_cast<unsigned long long>(traceState.pollWakeups),
        static_cast<unsigned long long>(traceState.inputOrSystemWakeups),
        static_cast<unsigned long long>(traceState.waylandWakeups),
        static_cast<unsigned long long>(traceState.pageFlipWakeups),
        static_cast<unsigned long long>(traceState.renderReadyWakeups),
        static_cast<unsigned long long>(traceState.unknownWakeups), traceState.pollMs,
        static_cast<unsigned long long>(traceState.dispatches), traceState.dispatchMs,
        static_cast<unsigned long long>(traceState.waylandDispatches),
        static_cast<unsigned long long>(traceState.waylandDispatchesChanged),
        static_cast<unsigned long long>(traceState.waylandDispatches - traceState.waylandDispatchesChanged),
        static_cast<double>(traceState.surfaces) * invFrames, traceState.totalMs * invFrames,
        traceState.backgroundMs * invFrames, traceState.snapshotMs * invFrames, traceState.surfaceMs * invFrames,
        traceState.closingMs * invFrames, traceState.launcherMs * invFrames, traceState.cursorMs * invFrames,
        traceState.presentMs * invFrames, traceState.canvasPresentMs * invFrames, traceState.kmsPresentMs * invFrames,
        traceState.maxTotalMs, traceState.maxSurfaceMs, traceState.maxPresentMs,
        static_cast<unsigned long long>(traceState.shmCopies), shmMb, shmMb / seconds, traceState.shmCopyMs,
        static_cast<unsigned long long>(traceState.imageCreates),
        static_cast<unsigned long long>(traceState.imageUpdates), imageMb, imageMb / seconds, traceState.imageUploadMs,
        static_cast<unsigned long long>(traceState.dmabufImports),
        static_cast<unsigned long long>(traceState.dmabufImportFailures), traceState.dmabufImportMs,
        static_cast<unsigned long long>(traceState.dmabufFallbackCopies),
        static_cast<unsigned long long>(traceState.dmabufFallbackFailures), fallbackMb, traceState.dmabufFallbackMs,
        static_cast<unsigned long long>(traceState.surfaceDrawCacheHits),
        static_cast<unsigned long long>(traceState.surfaceDrawCacheMisses), traceState.surfaceDrawRecordMs,
        static_cast<unsigned long long>(traceState.surfaceCommits),
        static_cast<unsigned long long>(traceState.surfaceStateCommits),
        static_cast<unsigned long long>(traceState.surfaceShmCommits),
        static_cast<unsigned long long>(traceState.surfaceDmabufCommits),
        static_cast<unsigned long long>(traceState.surfaceEmptyCommits),
        static_cast<unsigned long long>(traceState.surfaceOtherCommits),
        static_cast<unsigned long long>(traceState.hottestSurfaceId),
        static_cast<unsigned long long>(traceState.hottestSurfaceCommits), traceState.hottestSurfaceWidth,
        traceState.hottestSurfaceHeight);
    writeCpuSamples(file);
    std::fflush(file);
    fsync(fileno(file));
  }

  resetCounters(traceState, now, cpuNowMs);
}

} // namespace

bool cpuTraceEnabled() noexcept {
  static bool const enabled = [] {
    char const *value = std::getenv("FLUX_COMPOSITOR_CPU_TRACE");
    return debug::envNonZero(value);
  }();
  return enabled;
}

char const *cpuTracePath() noexcept { return tracePath(); }

void initializeCpuSampler() noexcept {
  if (!cpuTraceEnabled() || !cpuSamplerEnabled())
    return;
  struct sigaction action {};
  action.sa_handler = cpuSamplerSignalHandler;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_RESTART;
  if (sigaction(SIGPROF, &action, nullptr) != 0)
    return;
  int const intervalUsec = cpuSamplerIntervalUsec();
  itimerval timer{};
  timer.it_interval.tv_sec = intervalUsec / 1'000'000;
  timer.it_interval.tv_usec = intervalUsec % 1'000'000;
  timer.it_value = timer.it_interval;
  if (setitimer(ITIMER_PROF, &timer, nullptr) == 0) {
    std::fprintf(stderr,
                 "flux-compositor: CPU sample trace enabled at %dus CPU interval "
                 "(set FLUX_COMPOSITOR_SAMPLE_TRACE=0 to disable)\n",
                 intervalUsec);
  }
}

CpuTraceClock::time_point cpuTraceNow() noexcept { return CpuTraceClock::now(); }

double cpuTraceElapsedMilliseconds(CpuTraceClock::time_point start) noexcept {
  return std::chrono::duration<double, std::milli>(CpuTraceClock::now() - start).count();
}

void recordCpuFrame(CpuFrameTrace const &frame) {
  if (!cpuTraceEnabled())
    return;
  std::scoped_lock lock(traceMutex());
  auto &traceState = state();
  ++traceState.frames;
  traceState.surfaces += frame.surfaces;
  traceState.backgroundMs += frame.backgroundMs;
  traceState.snapshotMs += frame.snapshotMs;
  traceState.surfaceMs += frame.surfaceMs;
  traceState.closingMs += frame.closingMs;
  traceState.launcherMs += frame.launcherMs;
  traceState.cursorMs += frame.cursorMs;
  traceState.presentMs += frame.presentMs;
  traceState.canvasPresentMs += frame.canvasPresentMs;
  traceState.kmsPresentMs += frame.kmsPresentMs;
  traceState.totalMs += frame.totalMs;
  traceState.maxTotalMs = std::max(traceState.maxTotalMs, frame.totalMs);
  traceState.maxSurfaceMs = std::max(traceState.maxSurfaceMs, frame.surfaceMs);
  traceState.maxPresentMs = std::max(traceState.maxPresentMs, frame.presentMs);
  maybeLog(traceState);
}

void recordCpuLoop() {
  if (!cpuTraceEnabled())
    return;
  std::scoped_lock lock(traceMutex());
  auto &traceState = state();
  ++traceState.loops;
  if ((traceState.loops & 63ull) == 0ull)
    maybeLog(traceState);
}

void recordCpuIdleSkip() {
  if (!cpuTraceEnabled())
    return;
  std::scoped_lock lock(traceMutex());
  ++state().idleSkips;
}

void recordCpuPoll(double milliseconds, bool woke, int timeoutMs) {
  if (!cpuTraceEnabled())
    return;
  std::scoped_lock lock(traceMutex());
  auto &traceState = state();
  ++traceState.polls;
  if (woke)
    ++traceState.pollWakeups;
  if (timeoutMs == 0)
    ++traceState.zeroTimeoutPolls;
  traceState.pollMs += milliseconds;
}

void recordCpuWakeSources(bool inputOrSystem, bool wayland, bool pageFlip, bool renderReady) {
  if (!cpuTraceEnabled())
    return;
  std::scoped_lock lock(traceMutex());
  auto &traceState = state();
  if (inputOrSystem)
    ++traceState.inputOrSystemWakeups;
  if (wayland)
    ++traceState.waylandWakeups;
  if (pageFlip)
    ++traceState.pageFlipWakeups;
  if (renderReady)
    ++traceState.renderReadyWakeups;
  if (!inputOrSystem && !wayland && !pageFlip && !renderReady)
    ++traceState.unknownWakeups;
}

void recordCpuDispatch(double milliseconds) {
  if (!cpuTraceEnabled())
    return;
  std::scoped_lock lock(traceMutex());
  auto &traceState = state();
  ++traceState.dispatches;
  traceState.dispatchMs += milliseconds;
}

void recordWaylandDispatch(bool contentChanged) {
  if (!cpuTraceEnabled())
    return;
  std::scoped_lock lock(traceMutex());
  auto &traceState = state();
  ++traceState.waylandDispatches;
  if (contentChanged)
    ++traceState.waylandDispatchesChanged;
}

void recordSurfaceDrawCache(bool hit, double recordMilliseconds) {
  if (!cpuTraceEnabled())
    return;
  std::scoped_lock lock(traceMutex());
  auto &traceState = state();
  if (hit) {
    ++traceState.surfaceDrawCacheHits;
  } else {
    ++traceState.surfaceDrawCacheMisses;
  }
  traceState.surfaceDrawRecordMs += recordMilliseconds;
}

void recordSurfaceCommit(std::uint64_t surfaceId, CpuSurfaceCommitKind kind, std::int32_t width, std::int32_t height) {
  if (!cpuTraceEnabled())
    return;
  std::scoped_lock lock(traceMutex());
  auto &traceState = state();
  ++traceState.surfaceCommits;
  switch (kind) {
  case CpuSurfaceCommitKind::State:
    ++traceState.surfaceStateCommits;
    break;
  case CpuSurfaceCommitKind::Shm:
    ++traceState.surfaceShmCommits;
    break;
  case CpuSurfaceCommitKind::Dmabuf:
    ++traceState.surfaceDmabufCommits;
    break;
  case CpuSurfaceCommitKind::Empty:
    ++traceState.surfaceEmptyCommits;
    break;
  case CpuSurfaceCommitKind::Other:
    ++traceState.surfaceOtherCommits;
    break;
  }
  if (surfaceId == traceState.currentSurfaceId) {
    ++traceState.currentSurfaceCommits;
    traceState.currentSurfaceWidth = width;
    traceState.currentSurfaceHeight = height;
  } else {
    traceState.currentSurfaceId = surfaceId;
    traceState.currentSurfaceCommits = 1;
    traceState.currentSurfaceWidth = width;
    traceState.currentSurfaceHeight = height;
  }
  if (traceState.currentSurfaceCommits > traceState.hottestSurfaceCommits) {
    traceState.hottestSurfaceId = traceState.currentSurfaceId;
    traceState.hottestSurfaceCommits = traceState.currentSurfaceCommits;
    traceState.hottestSurfaceWidth = traceState.currentSurfaceWidth;
    traceState.hottestSurfaceHeight = traceState.currentSurfaceHeight;
  }
}

void recordShmCopy(std::size_t bytes, double milliseconds) {
  if (!cpuTraceEnabled())
    return;
  std::scoped_lock lock(traceMutex());
  auto &traceState = state();
  ++traceState.shmCopies;
  traceState.shmBytes += bytes;
  traceState.shmCopyMs += milliseconds;
}

void recordSurfaceImageUpload(std::size_t bytes, double milliseconds, bool created) {
  if (!cpuTraceEnabled())
    return;
  std::scoped_lock lock(traceMutex());
  auto &traceState = state();
  if (created) {
    ++traceState.imageCreates;
  } else {
    ++traceState.imageUpdates;
  }
  traceState.imageBytes += bytes;
  traceState.imageUploadMs += milliseconds;
}

void recordDmabufImport(double milliseconds, bool imported) {
  if (!cpuTraceEnabled())
    return;
  std::scoped_lock lock(traceMutex());
  auto &traceState = state();
  if (imported) {
    ++traceState.dmabufImports;
  } else {
    ++traceState.dmabufImportFailures;
  }
  traceState.dmabufImportMs += milliseconds;
}

void recordDmabufFallbackCopy(std::size_t bytes, double milliseconds, bool success) {
  if (!cpuTraceEnabled())
    return;
  std::scoped_lock lock(traceMutex());
  auto &traceState = state();
  if (success) {
    ++traceState.dmabufFallbackCopies;
    traceState.dmabufFallbackBytes += bytes;
  } else {
    ++traceState.dmabufFallbackFailures;
  }
  traceState.dmabufFallbackMs += milliseconds;
}

} // namespace flux::compositor::diagnostics
