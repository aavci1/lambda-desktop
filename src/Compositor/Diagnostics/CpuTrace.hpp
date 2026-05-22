#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace flux::compositor::diagnostics {

using CpuTraceClock = std::chrono::steady_clock;

struct CpuFrameTrace {
  std::size_t surfaces = 0;
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
};

enum class CpuSurfaceCommitKind : std::uint8_t {
  State,
  Shm,
  Dmabuf,
  Empty,
  Other,
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
void recordWaylandDispatch(bool contentChanged);
void recordSurfaceDrawCache(bool hit, double recordMilliseconds);
void recordSurfaceCommit(std::uint64_t surfaceId, CpuSurfaceCommitKind kind, std::int32_t width, std::int32_t height);
void recordShmCopy(std::size_t bytes, double milliseconds);
void recordSurfaceImageUpload(std::size_t bytes, double milliseconds, bool created);
void recordDmabufImport(double milliseconds, bool imported);
void recordDmabufFallbackCopy(std::size_t bytes, double milliseconds, bool success);

} // namespace flux::compositor::diagnostics
