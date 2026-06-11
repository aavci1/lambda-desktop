#include "Compositor/Diagnostics/CpuTrace.hpp"

#include <Lambda/Debug/DebugFlags.hpp>
#include <Lambda/Debug/PerfCounters.hpp>

#include <atomic>
#include <dlfcn.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <ucontext.h>
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

namespace lambda::compositor::diagnostics {
namespace {

constexpr int kMaxCpuSamples = 4096;

static_assert(std::atomic<std::uintptr_t>::is_always_lock_free);
static_assert(std::atomic<unsigned int>::is_always_lock_free);

std::array<std::atomic<std::uintptr_t>, kMaxCpuSamples> gCpuSamples{};
std::atomic<unsigned int> gCpuSampleWriteIndex{0};
std::atomic<unsigned int> gCpuSampleTotal{0};
std::atomic<unsigned int> gCpuSampleLastRead{0};

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
  std::uint64_t loopDecisions = 0;
  std::uint64_t loopRenderNeeded = 0;
  std::uint64_t loopRenderForce = 0;
  std::uint64_t loopRenderAnimation = 0;
  std::uint64_t loopRenderSnapPreview = 0;
  std::uint64_t loopRenderAhead = 0;
  std::uint64_t loopRenderAtomicDirty = 0;
  std::uint64_t loopRenderGenericWake = 0;
  std::uint64_t loopRenderInput = 0;
  std::uint64_t loopRenderConfig = 0;
  std::uint64_t loopRenderWaylandWake = 0;
  std::uint64_t loopPollZeroForce = 0;
  std::uint64_t loopPollZeroAnimation = 0;
  std::uint64_t loopPollZeroSnapPreview = 0;
  std::uint64_t loopPollZeroRenderAhead = 0;
  std::uint64_t loopPollZeroAtomicDirty = 0;
  std::uint64_t loopPollZeroBlocked = 0;
  std::uint64_t loopAtomicBlocked = 0;
  std::uint64_t loopAtomicReady = 0;
  std::uint64_t loopAtomicPendingFlip = 0;
  std::uint64_t atomicUpdateReadyCalls = 0;
  std::uint64_t atomicUpdateReadyFrames = 0;
  double atomicUpdateReadyMs = 0.0;
  std::uint64_t atomicScheduleAttempts = 0;
  std::uint64_t atomicScheduleSuccess = 0;
  std::uint64_t atomicSchedulePresent = 0;
  std::uint64_t atomicScheduleDirect = 0;
  std::uint64_t atomicScheduleDirectRepeat = 0;
  std::uint64_t atomicScheduleOverlay = 0;
  double atomicScheduleMs = 0.0;
  std::uint64_t atomicDispatchFlipCalls = 0;
  std::uint64_t atomicDispatchFlipCompletions = 0;
  double atomicDispatchFlipMs = 0.0;
  std::uint64_t atomicRenderCalls = 0;
  std::uint64_t atomicRenderAheadCalls = 0;
  double atomicRenderCallMs = 0.0;
  std::uint64_t atomicEmptyDamageChecks = 0;
  std::uint64_t atomicEmptyDamageSkips = 0;
  double atomicEmptyDamageMs = 0.0;
  std::uint64_t surfaces = 0;
  double pollMs = 0.0;
  double dispatchMs = 0.0;
  double backgroundMs = 0.0;
  double snapshotMs = 0.0;
  double surfaceMs = 0.0;
  double closingMs = 0.0;
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
  std::uint64_t surfaceDrawCacheBlockBackend = 0;
  std::uint64_t surfaceDrawCacheBlockClip = 0;
  std::uint64_t surfaceDrawCacheBlockCallout = 0;
  std::uint64_t surfaceDrawCacheBlockMaterial = 0;
  std::uint64_t surfaceDrawCacheBlockSizing = 0;
  std::uint64_t surfaceDrawCacheBlockTransientChrome = 0;
  std::uint64_t surfaceDrawCacheBlockOpeningAnimation = 0;
  std::uint64_t hottestSurfaceId = 0;
  std::uint64_t hottestSurfaceCommits = 0;
  std::int32_t hottestSurfaceWidth = 0;
  std::int32_t hottestSurfaceHeight = 0;
  std::uint64_t currentSurfaceId = 0;
  std::uint64_t currentSurfaceCommits = 0;
  std::int32_t currentSurfaceWidth = 0;
  std::int32_t currentSurfaceHeight = 0;
  debug::perf::RenderCounters renderCounterStart{};
};

std::mutex &traceMutex() {
  static std::mutex mutex;
  return mutex;
}

bool cpuSamplerEnabled() {
  static bool const enabled = [] {
    char const *value = std::getenv("LAMBDA_WINDOW_MANAGER_SAMPLE_TRACE");
    return debug::envNonZero(value);
  }();
  return enabled;
}

int cpuSamplerIntervalUsec() {
  static int const interval = [] {
    char const *value = std::getenv("LAMBDA_WINDOW_MANAGER_SAMPLE_USEC");
    if (!value || !*value)
      return 2000;
    int const parsed = std::atoi(value);
    return parsed > 0 ? parsed : 2000;
  }();
  return interval;
}

void *programCounterFromSignalContext(void *context) noexcept {
  if (!context)
    return nullptr;
  auto *ucontext = static_cast<ucontext_t *>(context);
#if defined(__linux__) && defined(__x86_64__) && defined(REG_RIP)
  return reinterpret_cast<void *>(ucontext->uc_mcontext.gregs[REG_RIP]);
#elif defined(__linux__) && defined(__i386__) && defined(REG_EIP)
  return reinterpret_cast<void *>(ucontext->uc_mcontext.gregs[REG_EIP]);
#elif defined(__linux__) && defined(__aarch64__)
  return reinterpret_cast<void *>(ucontext->uc_mcontext.pc);
#elif defined(__linux__) && defined(__arm__)
  return reinterpret_cast<void *>(ucontext->uc_mcontext.arm_pc);
#else
  (void)ucontext;
  return nullptr;
#endif
}

void cpuSamplerSignalHandler(int, siginfo_t *, void *context) {
  void *pc = programCounterFromSignalContext(context);
  if (!pc)
    return;
  auto const writeIndex = gCpuSampleWriteIndex.fetch_add(1, std::memory_order_relaxed);
  gCpuSamples[writeIndex % kMaxCpuSamples].store(reinterpret_cast<std::uintptr_t>(pc), std::memory_order_relaxed);
  gCpuSampleTotal.fetch_add(1, std::memory_order_relaxed);
}

std::string symbolName(void *pc) {
  Dl_info info{};
  if (dladdr(pc, &info) == 0 || !info.dli_sname) {
    if (info.dli_fname && info.dli_fbase) {
      std::filesystem::path const modulePath(info.dli_fname);
      auto const moduleBase = reinterpret_cast<std::uintptr_t>(info.dli_fbase);
      auto const address = reinterpret_cast<std::uintptr_t>(pc);
      char buffer[96]{};
      std::snprintf(buffer,
                    sizeof(buffer),
                    "%s+0x%zx",
                    modulePath.filename().string().c_str(),
                    static_cast<std::size_t>(address - moduleBase));
      return buffer;
    }
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
  unsigned int const total = gCpuSampleTotal.load(std::memory_order_relaxed);
  unsigned int const last = gCpuSampleLastRead.load(std::memory_order_relaxed);
  if (total == last)
    return;
  unsigned int available = total - last;
  if (available > kMaxCpuSamples)
    available = kMaxCpuSamples;

  struct Hit {
    void *pc = nullptr;
    int count = 0;
  };
  std::array<Hit, 32> hits{};
  int uniqueHits = 0;
  for (unsigned int i = 0; i < available; ++i) {
    unsigned int const sampleIndex = total - available + i;
    void *pc =
        reinterpret_cast<void *>(gCpuSamples[sampleIndex % kMaxCpuSamples].load(std::memory_order_relaxed));
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
  gCpuSampleLastRead.store(total, std::memory_order_relaxed);
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
    result.renderCounterStart = debug::perf::renderCountersSnapshot();
    return result;
  }();
  return traceState;
}

std::uint64_t counterDelta(std::uint64_t current, std::uint64_t previous) {
  return current >= previous ? current - previous : 0;
}

debug::perf::RenderCounters renderCounterDelta(debug::perf::RenderCounters const& current,
                                               debug::perf::RenderCounters const& previous) {
  debug::perf::RenderCounters delta{};
  for (std::size_t i = 0; i < current.ops.size(); ++i) {
    delta.ops[i] = counterDelta(current.ops[i], previous.ops[i]);
    delta.drawCalls[i] = counterDelta(current.drawCalls[i], previous.drawCalls[i]);
    delta.uploadBytes[i] = counterDelta(current.uploadBytes[i], previous.uploadBytes[i]);
  }
  delta.opOrderEntries = counterDelta(current.opOrderEntries, previous.opOrderEntries);
  delta.pathVertices = counterDelta(current.pathVertices, previous.pathVertices);
  delta.glyphVertices = counterDelta(current.glyphVertices, previous.glyphVertices);
  delta.backdropBlurRuns = counterDelta(current.backdropBlurRuns, previous.backdropBlurRuns);
  delta.backdropBlurPreparedRuns = counterDelta(current.backdropBlurPreparedRuns, previous.backdropBlurPreparedRuns);
  delta.backdropBlurOps = counterDelta(current.backdropBlurOps, previous.backdropBlurOps);
  delta.backdropBlurQuads = counterDelta(current.backdropBlurQuads, previous.backdropBlurQuads);
  delta.backdropBlurCacheHits = counterDelta(current.backdropBlurCacheHits, previous.backdropBlurCacheHits);
  delta.backdropBlurCacheMisses = counterDelta(current.backdropBlurCacheMisses, previous.backdropBlurCacheMisses);
  delta.backdropBlurPasses = counterDelta(current.backdropBlurPasses, previous.backdropBlurPasses);
  delta.backdropBlurPixels = counterDelta(current.backdropBlurPixels, previous.backdropBlurPixels);
  delta.backdropCopyPixels = counterDelta(current.backdropCopyPixels, previous.backdropCopyPixels);
  delta.backdropMaxCopyPixels = current.backdropMaxCopyPixels;
  delta.backdropMaxBlurPixels = current.backdropMaxBlurPixels;
  delta.recorderCapacityGrowths = counterDelta(current.recorderCapacityGrowths, previous.recorderCapacityGrowths);
  delta.recorderCapacityGrowthBytes =
      counterDelta(current.recorderCapacityGrowthBytes, previous.recorderCapacityGrowthBytes);
  delta.vulkanRecordNs = counterDelta(current.vulkanRecordNs, previous.vulkanRecordNs);
  delta.vulkanDrawOpsNs = counterDelta(current.vulkanDrawOpsNs, previous.vulkanDrawOpsNs);
  delta.vulkanStackedBlurNs = counterDelta(current.vulkanStackedBlurNs, previous.vulkanStackedBlurNs);
  delta.vulkanPathTessellateNs =
      counterDelta(current.vulkanPathTessellateNs, previous.vulkanPathTessellateNs);
  delta.vulkanDrawOpsCalls = counterDelta(current.vulkanDrawOpsCalls, previous.vulkanDrawOpsCalls);
  delta.vulkanDrawOpsVisited = counterDelta(current.vulkanDrawOpsVisited, previous.vulkanDrawOpsVisited);
  delta.vulkanDrawOpsSubmitted = counterDelta(current.vulkanDrawOpsSubmitted, previous.vulkanDrawOpsSubmitted);
  delta.vulkanScissorChanges = counterDelta(current.vulkanScissorChanges, previous.vulkanScissorChanges);
  delta.vulkanStackedOps = counterDelta(current.vulkanStackedOps, previous.vulkanStackedOps);
  delta.vulkanPathTessellations =
      counterDelta(current.vulkanPathTessellations, previous.vulkanPathTessellations);
  return delta;
}

std::string defaultTracePath() {
  if (char const *configured = std::getenv("LAMBDA_WINDOW_MANAGER_CPU_TRACE_LOG"); configured && *configured) {
    return configured;
  }
  if (char const *stateHome = std::getenv("XDG_STATE_HOME"); stateHome && *stateHome) {
    return std::string(stateHome) + "/lambda-window-manager/cpu.log";
  }
  if (char const *home = std::getenv("HOME"); home && *home) {
    return std::string(home) + "/.local/state/lambda-window-manager/cpu.log";
  }
  return "/tmp/lambda-window-manager-cpu.log";
}

char const *tracePath() {
  static std::string const path = defaultTracePath();
  return path.c_str();
}

char const *fallbackTracePath() {
  char const *path = std::getenv("LAMBDA_WINDOW_MANAGER_CPU_TRACE_LOG");
  return path && *path ? path : "/tmp/lambda-window-manager-cpu.log";
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
  traceState.loopDecisions = 0;
  traceState.loopRenderNeeded = 0;
  traceState.loopRenderForce = 0;
  traceState.loopRenderAnimation = 0;
  traceState.loopRenderSnapPreview = 0;
  traceState.loopRenderAhead = 0;
  traceState.loopRenderAtomicDirty = 0;
  traceState.loopRenderGenericWake = 0;
  traceState.loopRenderInput = 0;
  traceState.loopRenderConfig = 0;
  traceState.loopRenderWaylandWake = 0;
  traceState.loopPollZeroForce = 0;
  traceState.loopPollZeroAnimation = 0;
  traceState.loopPollZeroSnapPreview = 0;
  traceState.loopPollZeroRenderAhead = 0;
  traceState.loopPollZeroAtomicDirty = 0;
  traceState.loopPollZeroBlocked = 0;
  traceState.loopAtomicBlocked = 0;
  traceState.loopAtomicReady = 0;
  traceState.loopAtomicPendingFlip = 0;
  traceState.atomicUpdateReadyCalls = 0;
  traceState.atomicUpdateReadyFrames = 0;
  traceState.atomicUpdateReadyMs = 0.0;
  traceState.atomicScheduleAttempts = 0;
  traceState.atomicScheduleSuccess = 0;
  traceState.atomicSchedulePresent = 0;
  traceState.atomicScheduleDirect = 0;
  traceState.atomicScheduleDirectRepeat = 0;
  traceState.atomicScheduleOverlay = 0;
  traceState.atomicScheduleMs = 0.0;
  traceState.atomicDispatchFlipCalls = 0;
  traceState.atomicDispatchFlipCompletions = 0;
  traceState.atomicDispatchFlipMs = 0.0;
  traceState.atomicRenderCalls = 0;
  traceState.atomicRenderAheadCalls = 0;
  traceState.atomicRenderCallMs = 0.0;
  traceState.atomicEmptyDamageChecks = 0;
  traceState.atomicEmptyDamageSkips = 0;
  traceState.atomicEmptyDamageMs = 0.0;
  traceState.surfaces = 0;
  traceState.pollMs = 0.0;
  traceState.dispatchMs = 0.0;
  traceState.backgroundMs = 0.0;
  traceState.snapshotMs = 0.0;
  traceState.surfaceMs = 0.0;
  traceState.closingMs = 0.0;
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
  traceState.surfaceDrawCacheBlockBackend = 0;
  traceState.surfaceDrawCacheBlockClip = 0;
  traceState.surfaceDrawCacheBlockCallout = 0;
  traceState.surfaceDrawCacheBlockMaterial = 0;
  traceState.surfaceDrawCacheBlockSizing = 0;
  traceState.surfaceDrawCacheBlockTransientChrome = 0;
  traceState.surfaceDrawCacheBlockOpeningAnimation = 0;
  traceState.hottestSurfaceId = 0;
  traceState.hottestSurfaceCommits = 0;
  traceState.hottestSurfaceWidth = 0;
  traceState.hottestSurfaceHeight = 0;
  traceState.currentSurfaceId = 0;
  traceState.currentSurfaceCommits = 0;
  traceState.currentSurfaceWidth = 0;
  traceState.currentSurfaceHeight = 0;
  traceState.renderCounterStart = debug::perf::renderCountersSnapshot();
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
  debug::perf::RenderCounters const renderCounters =
      renderCounterDelta(debug::perf::renderCountersSnapshot(), traceState.renderCounterStart);
  double const blurCopyMpPerFrame =
      static_cast<double>(renderCounters.backdropCopyPixels) * invFrames / 1'000'000.0;
  double const blurMpPerFrame =
      static_cast<double>(renderCounters.backdropBlurPixels) * invFrames / 1'000'000.0;
  double const blurMaxCopyMp = static_cast<double>(renderCounters.backdropMaxCopyPixels) / 1'000'000.0;
  double const blurMaxMp = static_cast<double>(renderCounters.backdropMaxBlurPixels) / 1'000'000.0;
  auto const nsPerFrameMs = [&](std::uint64_t ns) {
    return static_cast<double>(ns) * invFrames / 1'000'000.0;
  };
  double const vulkanDrawOpsCallsPerFrame =
      static_cast<double>(renderCounters.vulkanDrawOpsCalls) * invFrames;
  double const vulkanVisitedOpsPerFrame =
      static_cast<double>(renderCounters.vulkanDrawOpsVisited) * invFrames;
  double const vulkanSubmittedOpsPerFrame =
      static_cast<double>(renderCounters.vulkanDrawOpsSubmitted) * invFrames;
  double const vulkanScissorsPerFrame =
      static_cast<double>(renderCounters.vulkanScissorChanges) * invFrames;

  if (std::FILE *file = traceFile(traceState)) {
    std::fprintf(
        file,
        "cpu-trace: window=%.2fs cpu=%.1f%% loops=%llu idle_skips=%llu frames=%llu "
        "fps=%.1f polls=%llu zero_polls=%llu wakeups=%llu "
        "wake_src input_system=%llu wayland=%llu pageflip=%llu render_ready=%llu unknown=%llu "
        "poll_ms=%.3f dispatches=%llu dispatch_ms=%.3f "
        "wayland_dispatches=%llu changed=%llu unchanged=%llu "
        "decisions=%llu render_needed=%llu "
        "render_reason force=%llu anim=%llu snap=%llu ahead=%llu dirty=%llu generic=%llu "
        "input=%llu config=%llu wayland=%llu "
        "zero_reason force=%llu anim=%llu snap=%llu ahead=%llu dirty=%llu blocked=%llu "
        "atomic_state blocked=%llu ready=%llu pending_flip=%llu "
        "atomic_ops update_ready=%llu update_frames=%llu update_ms=%.3f "
        "schedule=%llu scheduled=%llu schedule_ms=%.3f "
        "schedule_kind present=%llu direct=%llu direct_repeat=%llu overlay=%llu "
        "dispatch_flip=%llu flips=%llu dispatch_ms=%.3f "
        "render=%llu render_ahead=%llu render_ms=%.3f "
        "empty_damage=%llu empty_skips=%llu empty_ms=%.3f "
        "surfaces=%.2f/f "
        "phase_avg_ms total=%.3f bg=%.3f snapshot=%.3f surface=%.3f closing=%.3f "
        "cursor=%.3f present=%.3f canvas_present=%.3f kms_present=%.3f "
        "max_total=%.3f max_surface=%.3f "
        "max_present=%.3f shm copies=%llu mb=%.1f mbps=%.1f copy_ms=%.3f "
        "image creates=%llu updates=%llu mb=%.1f mbps=%.1f upload_ms=%.3f "
        "dmabuf imports=%llu failures=%llu import_ms=%.3f fallback_copies=%llu "
        "fallback_failures=%llu fallback_mb=%.1f fallback_ms=%.3f "
        "blur prepared=%llu ops=%llu quads=%llu cache_hits=%llu cache_misses=%llu "
        "runs=%llu passes=%llu copy_mp_f=%.2f blur_mp_f=%.2f "
        "proc_max_copy_mp=%.2f proc_max_blur_mp=%.2f "
        "vulkan_ms record=%.3f draw_ops=%.3f stacked_blur=%.3f path_tess=%.3f "
        "vulkan_ops calls=%.2f/f visited=%.1f/f submitted=%.1f/f scissors=%.1f/f "
        "stacked_ops=%llu path_tess_count=%llu "
        "surface_draw_cache hits=%llu misses=%llu record_ms=%.3f "
        "surface_draw_block backend=%llu clip=%llu callout=%llu material=%llu sizing=%llu transient=%llu opening=%llu "
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
        static_cast<unsigned long long>(traceState.loopDecisions),
        static_cast<unsigned long long>(traceState.loopRenderNeeded),
        static_cast<unsigned long long>(traceState.loopRenderForce),
        static_cast<unsigned long long>(traceState.loopRenderAnimation),
        static_cast<unsigned long long>(traceState.loopRenderSnapPreview),
        static_cast<unsigned long long>(traceState.loopRenderAhead),
        static_cast<unsigned long long>(traceState.loopRenderAtomicDirty),
        static_cast<unsigned long long>(traceState.loopRenderGenericWake),
        static_cast<unsigned long long>(traceState.loopRenderInput),
        static_cast<unsigned long long>(traceState.loopRenderConfig),
        static_cast<unsigned long long>(traceState.loopRenderWaylandWake),
        static_cast<unsigned long long>(traceState.loopPollZeroForce),
        static_cast<unsigned long long>(traceState.loopPollZeroAnimation),
        static_cast<unsigned long long>(traceState.loopPollZeroSnapPreview),
        static_cast<unsigned long long>(traceState.loopPollZeroRenderAhead),
        static_cast<unsigned long long>(traceState.loopPollZeroAtomicDirty),
        static_cast<unsigned long long>(traceState.loopPollZeroBlocked),
        static_cast<unsigned long long>(traceState.loopAtomicBlocked),
        static_cast<unsigned long long>(traceState.loopAtomicReady),
        static_cast<unsigned long long>(traceState.loopAtomicPendingFlip),
        static_cast<unsigned long long>(traceState.atomicUpdateReadyCalls),
        static_cast<unsigned long long>(traceState.atomicUpdateReadyFrames),
        traceState.atomicUpdateReadyMs,
        static_cast<unsigned long long>(traceState.atomicScheduleAttempts),
        static_cast<unsigned long long>(traceState.atomicScheduleSuccess),
        traceState.atomicScheduleMs,
        static_cast<unsigned long long>(traceState.atomicSchedulePresent),
        static_cast<unsigned long long>(traceState.atomicScheduleDirect),
        static_cast<unsigned long long>(traceState.atomicScheduleDirectRepeat),
        static_cast<unsigned long long>(traceState.atomicScheduleOverlay),
        static_cast<unsigned long long>(traceState.atomicDispatchFlipCalls),
        static_cast<unsigned long long>(traceState.atomicDispatchFlipCompletions),
        traceState.atomicDispatchFlipMs,
        static_cast<unsigned long long>(traceState.atomicRenderCalls),
        static_cast<unsigned long long>(traceState.atomicRenderAheadCalls),
        traceState.atomicRenderCallMs,
        static_cast<unsigned long long>(traceState.atomicEmptyDamageChecks),
        static_cast<unsigned long long>(traceState.atomicEmptyDamageSkips),
        traceState.atomicEmptyDamageMs,
        static_cast<double>(traceState.surfaces) * invFrames, traceState.totalMs * invFrames,
        traceState.backgroundMs * invFrames, traceState.snapshotMs * invFrames, traceState.surfaceMs * invFrames,
        traceState.closingMs * invFrames, traceState.cursorMs * invFrames,
        traceState.presentMs * invFrames, traceState.canvasPresentMs * invFrames, traceState.kmsPresentMs * invFrames,
        traceState.maxTotalMs, traceState.maxSurfaceMs, traceState.maxPresentMs,
        static_cast<unsigned long long>(traceState.shmCopies), shmMb, shmMb / seconds, traceState.shmCopyMs,
        static_cast<unsigned long long>(traceState.imageCreates),
        static_cast<unsigned long long>(traceState.imageUpdates), imageMb, imageMb / seconds, traceState.imageUploadMs,
        static_cast<unsigned long long>(traceState.dmabufImports),
        static_cast<unsigned long long>(traceState.dmabufImportFailures), traceState.dmabufImportMs,
        static_cast<unsigned long long>(traceState.dmabufFallbackCopies),
        static_cast<unsigned long long>(traceState.dmabufFallbackFailures), fallbackMb, traceState.dmabufFallbackMs,
        static_cast<unsigned long long>(renderCounters.backdropBlurPreparedRuns),
        static_cast<unsigned long long>(renderCounters.backdropBlurOps),
        static_cast<unsigned long long>(renderCounters.backdropBlurQuads),
        static_cast<unsigned long long>(renderCounters.backdropBlurCacheHits),
        static_cast<unsigned long long>(renderCounters.backdropBlurCacheMisses),
        static_cast<unsigned long long>(renderCounters.backdropBlurRuns),
        static_cast<unsigned long long>(renderCounters.backdropBlurPasses),
        blurCopyMpPerFrame, blurMpPerFrame, blurMaxCopyMp, blurMaxMp,
        nsPerFrameMs(renderCounters.vulkanRecordNs),
        nsPerFrameMs(renderCounters.vulkanDrawOpsNs),
        nsPerFrameMs(renderCounters.vulkanStackedBlurNs),
        nsPerFrameMs(renderCounters.vulkanPathTessellateNs),
        vulkanDrawOpsCallsPerFrame, vulkanVisitedOpsPerFrame,
        vulkanSubmittedOpsPerFrame, vulkanScissorsPerFrame,
        static_cast<unsigned long long>(renderCounters.vulkanStackedOps),
        static_cast<unsigned long long>(renderCounters.vulkanPathTessellations),
        static_cast<unsigned long long>(traceState.surfaceDrawCacheHits),
        static_cast<unsigned long long>(traceState.surfaceDrawCacheMisses), traceState.surfaceDrawRecordMs,
        static_cast<unsigned long long>(traceState.surfaceDrawCacheBlockBackend),
        static_cast<unsigned long long>(traceState.surfaceDrawCacheBlockClip),
        static_cast<unsigned long long>(traceState.surfaceDrawCacheBlockCallout),
        static_cast<unsigned long long>(traceState.surfaceDrawCacheBlockMaterial),
        static_cast<unsigned long long>(traceState.surfaceDrawCacheBlockSizing),
        static_cast<unsigned long long>(traceState.surfaceDrawCacheBlockTransientChrome),
        static_cast<unsigned long long>(traceState.surfaceDrawCacheBlockOpeningAnimation),
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
    char const *value = std::getenv("LAMBDA_WINDOW_MANAGER_CPU_TRACE");
    return debug::envNonZero(value);
  }();
  return enabled;
}

char const *cpuTracePath() noexcept { return tracePath(); }

void initializeCpuSampler() noexcept {
  if (!cpuTraceEnabled() || !cpuSamplerEnabled())
    return;
  struct sigaction action {};
  action.sa_sigaction = cpuSamplerSignalHandler;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_RESTART | SA_SIGINFO;
  if (sigaction(SIGPROF, &action, nullptr) != 0)
    return;
  int const intervalUsec = cpuSamplerIntervalUsec();
  itimerval timer{};
  timer.it_interval.tv_sec = intervalUsec / 1'000'000;
  timer.it_interval.tv_usec = intervalUsec % 1'000'000;
  timer.it_value = timer.it_interval;
  if (setitimer(ITIMER_PROF, &timer, nullptr) == 0) {
    std::fprintf(stderr,
                 "lambda-window-manager: CPU sample trace enabled at %dus CPU interval "
                 "(set LAMBDA_WINDOW_MANAGER_SAMPLE_TRACE=0 to disable)\n",
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

void recordCpuLoopDecision(CpuLoopDecisionTrace const &decision) {
  if (!cpuTraceEnabled())
    return;
  std::scoped_lock lock(traceMutex());
  auto &traceState = state();
  ++traceState.loopDecisions;
  if (decision.renderNeeded) {
    ++traceState.loopRenderNeeded;
    if (decision.forceRender)
      ++traceState.loopRenderForce;
    if (decision.animationFrameNeeded)
      ++traceState.loopRenderAnimation;
    if (decision.snapPreviewFrameNeeded)
      ++traceState.loopRenderSnapPreview;
    if (decision.renderAheadNeeded)
      ++traceState.loopRenderAhead;
    if (decision.atomicFrameDirty)
      ++traceState.loopRenderAtomicDirty;
    if (decision.genericRenderWake)
      ++traceState.loopRenderGenericWake;
    if (decision.hadInputActivity || decision.pollInputOrSystem)
      ++traceState.loopRenderInput;
    if (decision.configReloaded)
      ++traceState.loopRenderConfig;
    if (decision.waylandWoke)
      ++traceState.loopRenderWaylandWake;
  }
  if (decision.pollTimeoutZero) {
    if (decision.forceRender)
      ++traceState.loopPollZeroForce;
    if (decision.animationFrameNeeded)
      ++traceState.loopPollZeroAnimation;
    if (decision.snapPreviewFrameNeeded)
      ++traceState.loopPollZeroSnapPreview;
    if (decision.renderAheadNeeded)
      ++traceState.loopPollZeroRenderAhead;
    if (decision.atomicFrameDirty)
      ++traceState.loopPollZeroAtomicDirty;
    if (decision.atomicFrameBlocked)
      ++traceState.loopPollZeroBlocked;
  }
  if (decision.atomicFrameBlocked)
    ++traceState.loopAtomicBlocked;
  if (decision.atomicReadyFrame)
    ++traceState.loopAtomicReady;
  if (decision.atomicPageFlipPending)
    ++traceState.loopAtomicPendingFlip;
}

void recordCpuAtomicLoop(CpuAtomicLoopTrace const &trace) {
  if (!cpuTraceEnabled())
    return;
  std::scoped_lock lock(traceMutex());
  auto &traceState = state();
  traceState.atomicUpdateReadyCalls += trace.updateReadyCalls;
  traceState.atomicUpdateReadyFrames += trace.updateReadyFrames;
  traceState.atomicUpdateReadyMs += trace.updateReadyMs;
  traceState.atomicScheduleAttempts += trace.scheduleAttempts;
  traceState.atomicScheduleSuccess += trace.scheduleSuccess;
  traceState.atomicSchedulePresent += trace.schedulePresent;
  traceState.atomicScheduleDirect += trace.scheduleDirect;
  traceState.atomicScheduleDirectRepeat += trace.scheduleDirectRepeat;
  traceState.atomicScheduleOverlay += trace.scheduleOverlay;
  traceState.atomicScheduleMs += trace.scheduleMs;
  traceState.atomicDispatchFlipCalls += trace.dispatchFlipCalls;
  traceState.atomicDispatchFlipCompletions += trace.dispatchFlipCompletions;
  traceState.atomicDispatchFlipMs += trace.dispatchFlipMs;
  traceState.atomicRenderCalls += trace.renderCalls;
  traceState.atomicRenderAheadCalls += trace.renderAheadCalls;
  traceState.atomicRenderCallMs += trace.renderCallMs;
  traceState.atomicEmptyDamageChecks += trace.emptyDamageChecks;
  traceState.atomicEmptyDamageSkips += trace.emptyDamageSkips;
  traceState.atomicEmptyDamageMs += trace.emptyDamageMs;
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

void recordSurfaceDrawCacheBlock(CpuSurfaceDrawCacheBlockReason reason) {
  if (!cpuTraceEnabled())
    return;
  std::scoped_lock lock(traceMutex());
  auto &traceState = state();
  switch (reason) {
  case CpuSurfaceDrawCacheBlockReason::Backend:
    ++traceState.surfaceDrawCacheBlockBackend;
    break;
  case CpuSurfaceDrawCacheBlockReason::Clip:
    ++traceState.surfaceDrawCacheBlockClip;
    break;
  case CpuSurfaceDrawCacheBlockReason::Callout:
    ++traceState.surfaceDrawCacheBlockCallout;
    break;
  case CpuSurfaceDrawCacheBlockReason::Material:
    ++traceState.surfaceDrawCacheBlockMaterial;
    break;
  case CpuSurfaceDrawCacheBlockReason::Sizing:
    ++traceState.surfaceDrawCacheBlockSizing;
    break;
  case CpuSurfaceDrawCacheBlockReason::TransientChrome:
    ++traceState.surfaceDrawCacheBlockTransientChrome;
    break;
  case CpuSurfaceDrawCacheBlockReason::OpeningAnimation:
    ++traceState.surfaceDrawCacheBlockOpeningAnimation;
    break;
  }
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

} // namespace lambda::compositor::diagnostics
