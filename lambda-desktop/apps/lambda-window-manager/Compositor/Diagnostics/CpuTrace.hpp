#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace lambdaui::compositor::diagnostics {

using CpuTraceClock = std::chrono::steady_clock;

struct CpuFrameTrace {
  std::size_t surfaces = 0;
  double backgroundMs = 0.0;
  double snapshotMs = 0.0;
  double surfaceMs = 0.0;
  double closingMs = 0.0;
  double cursorMs = 0.0;
  double presentMs = 0.0;
  double canvasPresentMs = 0.0;
  double kmsPresentMs = 0.0;
  double totalMs = 0.0;
};

struct CpuLoopDecisionTrace {
  bool pollTimeoutZero = false;
  bool forceRender = false;
  bool animationFrameNeeded = false;
  bool snapPreviewFrameNeeded = false;
  bool renderAheadNeeded = false;
  bool atomicFrameDirty = false;
  bool atomicFrameBlocked = false;
  bool atomicReadyFrame = false;
  bool atomicPageFlipPending = false;
  bool renderNeeded = false;
  bool genericRenderWake = false;
  bool hadInputActivity = false;
  bool configReloaded = false;
  bool pollInputOrSystem = false;
  bool waylandWoke = false;
};

struct CpuAtomicLoopTrace {
  std::uint64_t updateReadyCalls = 0;
  std::uint64_t updateReadyFrames = 0;
  double updateReadyMs = 0.0;
  std::uint64_t scheduleAttempts = 0;
  std::uint64_t scheduleSuccess = 0;
  std::uint64_t schedulePresent = 0;
  std::uint64_t scheduleDirect = 0;
  std::uint64_t scheduleDirectRepeat = 0;
  std::uint64_t scheduleOverlay = 0;
  double scheduleMs = 0.0;
  std::uint64_t dispatchFlipCalls = 0;
  std::uint64_t dispatchFlipCompletions = 0;
  double dispatchFlipMs = 0.0;
  std::uint64_t renderCalls = 0;
  std::uint64_t renderAheadCalls = 0;
  double renderCallMs = 0.0;
  std::uint64_t emptyDamageChecks = 0;
  std::uint64_t emptyDamageSkips = 0;
  double emptyDamageMs = 0.0;
};

enum class CpuSurfaceCommitKind : std::uint8_t {
  State,
  Shm,
  Dmabuf,
  Empty,
  Other,
};

enum class CpuSurfaceDrawCacheBlockReason : std::uint8_t {
  Backend,
  Clip,
  Callout,
  Material,
  Sizing,
  TransientChrome,
  OpeningAnimation,
};

bool cpuTraceEnabled() noexcept;
char const *cpuTracePath() noexcept;
void initializeCpuSampler() noexcept;
CpuTraceClock::time_point cpuTraceNow() noexcept;
double cpuTraceElapsedMilliseconds(CpuTraceClock::time_point start) noexcept;

void recordCpuFrame(CpuFrameTrace const &frame);
void recordCpuLoop();
void recordCpuIdleSkip();
void recordCpuPoll(double milliseconds, bool woke, int timeoutMs);
void recordCpuWakeSources(bool inputOrSystem, bool wayland, bool pageFlip, bool renderReady);
void recordCpuDispatch(double milliseconds);
void recordCpuLoopDecision(CpuLoopDecisionTrace const &decision);
void recordCpuAtomicLoop(CpuAtomicLoopTrace const &trace);
void recordWaylandDispatch(bool contentChanged);
void recordSurfaceDrawCache(bool hit, double recordMilliseconds);
void recordSurfaceDrawCacheBlock(CpuSurfaceDrawCacheBlockReason reason);
void recordSurfaceCommit(std::uint64_t surfaceId, CpuSurfaceCommitKind kind, std::int32_t width, std::int32_t height);
void recordShmCopy(std::size_t bytes, double milliseconds);
void recordSurfaceImageUpload(std::size_t bytes, double milliseconds, bool created);
void recordDmabufImport(double milliseconds, bool imported);
void recordDmabufFallbackCopy(std::size_t bytes, double milliseconds, bool success);

} // namespace lambdaui::compositor::diagnostics
