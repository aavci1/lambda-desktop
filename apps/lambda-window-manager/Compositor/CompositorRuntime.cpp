#include "Compositor/CompositorRuntime.hpp"

#include <Lambda/Graphics/VulkanContext.hpp>
#include <Lambda/Debug/DebugFlags.hpp>
#include <Lambda/Platform/Linux/KmsOutput.hpp>

#include "Compositor/Chrome/CursorRenderer.hpp"
#include "Compositor/Chrome/WindowFrameGeometry.hpp"
#include "Compositor/CompositorConfigWatch.hpp"
#include "Compositor/CompositorPresentation.hpp"
#include "Compositor/CompositorRenderFrame.hpp"
#include "Compositor/Config/AppliedCompositorConfig.hpp"
#include "Compositor/Config/WallpaperCache.hpp"
#include "Compositor/Config/WallpaperLoader.hpp"
#include "Compositor/Config/CompositorConfig.hpp"
#include "Compositor/Diagnostics/CrashLog.hpp"
#include "Compositor/Diagnostics/CpuTrace.hpp"
#include "Compositor/Input/KmsInputBridge.hpp"
#include "Compositor/Presenter.hpp"
#include "Compositor/PresentationLoop.hpp"
#include "Compositor/Surface/SurfaceRenderer.hpp"
#include "Compositor/Screenshot.hpp"
#include "Compositor/WaylandServer.hpp"
#include "Graphics/Linux/FreeTypeTextSystem.hpp"
#include "Graphics/Vulkan/VulkanCanvas.hpp"
#include "presentation-time-server-protocol.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include <sys/stat.h>
#include <sys/types.h>
#include <poll.h>
#include <vulkan/vulkan.h>

namespace lambda::compositor {
using namespace presentation;

namespace {

int positiveEnvInt(char const* name, int fallback, int maxValue) {
  char const* raw = std::getenv(name);
  if (!raw || !*raw) return fallback;
  char* end = nullptr;
  long const value = std::strtol(raw, &end, 10);
  if (end == raw || value <= 0) return fallback;
  return static_cast<int>(std::min<long>(value, maxValue));
}

bool envEnabled(char const* name) {
  char const* raw = std::getenv(name);
  if (!raw || !*raw) return false;
  return std::strcmp(raw, "0") != 0 && std::strcmp(raw, "false") != 0 && std::strcmp(raw, "FALSE") != 0;
}

int millisecondsUntilCeil(SteadyClock::time_point deadline,
                          SteadyClock::time_point now,
                          int maxDelayMs) {
  if (now >= deadline) return 0;
  auto const remaining = deadline - now;
  auto const remainingUs = std::chrono::duration_cast<std::chrono::microseconds>(remaining).count();
  if (remainingUs <= 0) return 1;
  long long const remainingMs = (remainingUs + 999ll) / 1000ll;
  return static_cast<int>(std::clamp<long long>(remainingMs, 1, maxDelayMs));
}

void applyDiagnosticFloorChrome(WaylandServer& wayland, AppliedCompositorConfig& appliedConfig) {
  if (!envEnabled("LWM_DIAGNOSTIC_FLOOR_RENDERING")) return;
  ChromeConfig chrome = appliedConfig.config.chrome;
  chrome.titleBarHeight = 0;
  chrome.contentInsetWidth = 0;
  chrome.windowBorderWidth = 0.f;
  chrome.focusedShadowRadius = 0.f;
  chrome.unfocusedShadowRadius = 0.f;
  chrome.focusedShadowColor = Colors::transparent;
  chrome.unfocusedShadowColor = Colors::transparent;
  chrome.windowBorderColor = Colors::transparent;
  chrome.borderLineColor = Colors::transparent;
  chrome.insetHighlightColor = Colors::transparent;
  chrome.glass.blurRadius = 0.f;
  chrome.glass.opacity = 0.f;
  chrome.glass.baseColor = Colors::transparent;
  chrome.glass.tintColor = Colors::transparent;
  chrome.glass.borderColor = Colors::transparent;
  appliedConfig.config.chrome = chrome;
  wayland.setChromeConfig(chrome);
}

std::uint64_t drmDeviceForFd(int fd) {
  if (fd < 0) return 0;
  struct stat statbuf {};
  if (fstat(fd, &statbuf) != 0) return 0;
  return static_cast<std::uint64_t>(statbuf.st_rdev);
}

std::filesystem::path snapCaptureRoot() {
  if (char const* raw = std::getenv("LWM_SNAP_CAPTURE_DIR"); raw && *raw) {
    return raw;
  }
  return ".debug-logs/snap-capture";
}

std::filesystem::path snapTraceRoot() {
  if (char const* raw = std::getenv("LWM_SNAP_TRACE_DIR"); raw && *raw) {
    return raw;
  }
  return ".debug-logs/snap-trace";
}

std::string captureTimestamp() {
  auto const now = std::chrono::system_clock::now();
  std::time_t const time = std::chrono::system_clock::to_time_t(now);
  std::tm local{};
  localtime_r(&time, &local);
  char buffer[64]{};
  if (std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &local) == 0) {
    return "capture";
  }
  return buffer;
}

struct CaptureRegion {
  std::string name;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
};

struct RegionStats {
  double meanLuma = 0.0;
  double meanAlpha = 0.0;
  std::uint64_t pixels = 0;
};

struct LoopSurfaceSnapshotCache {
  std::vector<CommittedSurfaceSnapshot> surfaces;
  std::optional<CommittedSurfaceSnapshot> softwareCursor;
  bool surfacesValid = false;
  bool softwareCursorValid = false;
  bool softwareCursorRequested = false;

  void reset() {
    surfacesValid = false;
    softwareCursorValid = false;
    softwareCursorRequested = false;
    surfaces.clear();
    softwareCursor.reset();
  }

  std::vector<CommittedSurfaceSnapshot> const& committedSurfaces(WaylandServer& wayland) {
    if (!surfacesValid) {
      surfaces = wayland.committedSurfaces();
      surfacesValid = true;
    }
    return surfaces;
  }

  std::optional<CommittedSurfaceSnapshot> const& cursorSurface(WaylandServer& wayland, bool requested) {
    if (!softwareCursorValid || softwareCursorRequested != requested) {
      softwareCursor = requested ? wayland.cursorSurface() : std::optional<CommittedSurfaceSnapshot>{};
      softwareCursorValid = true;
      softwareCursorRequested = requested;
    }
    return softwareCursor;
  }
};

struct ActiveWindowScreenshotTarget {
  ScreenshotRegion region;
  CornerRadius cornerRadius{};
};

std::optional<RegionStats> captureRegionStats(std::vector<std::uint8_t> const& bgra,
                                              std::uint32_t width,
                                              std::uint32_t height,
                                              CaptureRegion const& region,
                                              std::int32_t logicalWidth,
                                              std::int32_t logicalHeight) {
  if (width == 0 || height == 0 || logicalWidth <= 0 || logicalHeight <= 0) return std::nullopt;
  double const scaleX = static_cast<double>(width) / static_cast<double>(logicalWidth);
  double const scaleY = static_cast<double>(height) / static_cast<double>(logicalHeight);
  std::int32_t const x0 = std::clamp(static_cast<std::int32_t>(std::floor(region.x * scaleX)),
                                     0,
                                     static_cast<std::int32_t>(width));
  std::int32_t const y0 = std::clamp(static_cast<std::int32_t>(std::floor(region.y * scaleY)),
                                     0,
                                     static_cast<std::int32_t>(height));
  std::int32_t const x1 = std::clamp(static_cast<std::int32_t>(std::ceil((region.x + region.width) * scaleX)),
                                     0,
                                     static_cast<std::int32_t>(width));
  std::int32_t const y1 = std::clamp(static_cast<std::int32_t>(std::ceil((region.y + region.height) * scaleY)),
                                     0,
                                     static_cast<std::int32_t>(height));
  if (x1 <= x0 || y1 <= y0) return std::nullopt;

  double luma = 0.0;
  double alpha = 0.0;
  std::uint64_t count = 0;
  for (std::int32_t y = y0; y < y1; ++y) {
    for (std::int32_t x = x0; x < x1; ++x) {
      std::size_t const offset = (static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)) * 4u;
      if (offset + 3u >= bgra.size()) continue;
      double const b = bgra[offset + 0u];
      double const g = bgra[offset + 1u];
      double const r = bgra[offset + 2u];
      luma += 0.2126 * r + 0.7152 * g + 0.0722 * b;
      alpha += bgra[offset + 3u];
      ++count;
    }
  }
  if (count == 0) return std::nullopt;
  return RegionStats{
      .meanLuma = luma / static_cast<double>(count),
      .meanAlpha = alpha / static_cast<double>(count),
      .pixels = count,
  };
}

std::optional<ActiveWindowScreenshotTarget> focusedWindowScreenshotTarget(WaylandServer const& wayland,
                                                                         ChromeConfig const& chrome) {
  for (auto const& surface : wayland.committedSurfaces()) {
    if (!surface.focused || surface.width <= 0 || surface.height <= 0) continue;
    Rect const frame = windowFrameRect(surface, chrome.contentInsetWidth);
    ScreenshotRegion const region{
        .x = static_cast<std::int32_t>(std::lround(frame.x)),
        .y = static_cast<std::int32_t>(std::lround(frame.y)),
        .width = static_cast<std::int32_t>(std::lround(frame.width)),
        .height = static_cast<std::int32_t>(std::lround(frame.height)),
    };
    auto normalized = normalizeScreenshotRegion(region, wayland.logicalOutputWidth(), wayland.logicalOutputHeight());
    if (!normalized) return std::nullopt;
    return ActiveWindowScreenshotTarget{
        .region = *normalized,
        .cornerRadius = surface.backgroundEffect.cornerRadiusSet ? surface.backgroundEffect.cornerRadius
                                                                 : chrome.windowCornerRadius,
    };
  }
  return std::nullopt;
}

bool logScreenshotSaveResult(ScreenshotSaveResult const& saved) {
  if (saved.error.empty()) {
    std::fprintf(stderr, "lambda-window-manager: saved screenshot to %s\n", saved.path.string().c_str());
    return true;
  } else {
    std::fprintf(stderr,
                 "lambda-window-manager: failed to save screenshot to %s: %s\n",
                 saved.path.string().c_str(),
                 saved.error.c_str());
    return false;
  }
}

void disarmOverlayCandidateFds(platform::KmsAtomicPresenter::OverlayCandidate& candidate) noexcept {
  candidate.acquireFenceFd = -1;
  for (auto& plane : candidate.planes) {
    plane.fd = -1;
  }
}

bool fdReadableNow(int fd) noexcept {
  if (fd < 0) return true;
  pollfd pfd{
      .fd = fd,
      .events = POLLIN,
      .revents = 0,
  };
  return poll(&pfd, 1, 0) > 0 && (pfd.revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) != 0;
}

CornerRadius scaleCornerRadius(CornerRadius corners, double scale) {
  float const factor = static_cast<float>(scale);
  return CornerRadius{
      corners.topLeft * factor,
      corners.topRight * factor,
      corners.bottomRight * factor,
      corners.bottomLeft * factor,
  };
}

bool saveScreenshotRequest(ScreenshotRequest const& request,
                           std::vector<std::uint8_t> const& pixels,
                           std::uint32_t width,
                           std::uint32_t height,
                           WaylandServer const& wayland,
                           ChromeConfig const& chrome) {
  if (request.mode == ScreenshotMode::FullOutput) {
    return logScreenshotSaveResult(saveScreenshotPng(pixels, width, height));
  }

  std::optional<ScreenshotRegion> logicalRegion = request.region;
  CornerRadius activeWindowCorners{};
  if (request.mode == ScreenshotMode::ActiveWindow) {
    auto target = focusedWindowScreenshotTarget(wayland, chrome);
    if (!target) {
      std::fprintf(stderr, "lambda-window-manager: active-window screenshot skipped; no focused window\n");
      return false;
    }
    logicalRegion = target->region;
    activeWindowCorners = target->cornerRadius;
  }
  if (!logicalRegion) {
    std::fprintf(stderr, "lambda-window-manager: screenshot skipped; empty region\n");
    return false;
  }

  auto framebufferRegion = logicalRegionToFramebuffer(*logicalRegion,
                                                      wayland.logicalOutputWidth(),
                                                      wayland.logicalOutputHeight(),
                                                      width,
                                                      height);
  if (!framebufferRegion) {
    std::fprintf(stderr, "lambda-window-manager: screenshot skipped; region is outside the output\n");
    return false;
  }
  auto cropped = cropBgra(pixels, width, height, *framebufferRegion);
  if (!cropped) {
    std::fprintf(stderr, "lambda-window-manager: screenshot skipped; failed to crop region\n");
    return false;
  }
  if (request.mode == ScreenshotMode::ActiveWindow) {
    double const scaleX = static_cast<double>(framebufferRegion->width) / static_cast<double>(logicalRegion->width);
    double const scaleY = static_cast<double>(framebufferRegion->height) / static_cast<double>(logicalRegion->height);
    maskBgraToRoundedRect(cropped->pixels,
                          cropped->width,
                          cropped->height,
                          scaleCornerRadius(activeWindowCorners, std::min(scaleX, scaleY)));
  }
  return logScreenshotSaveResult(saveScreenshotPng(cropped->pixels, cropped->width, cropped->height));
}

std::vector<CaptureRegion> snapCaptureRegions(WaylandServer const& wayland) {
  std::vector<CaptureRegion> regions;
  bool dockAdded = false;
  bool topBandAdded = false;
  for (auto const& surface : wayland.committedSurfaces()) {
    if (surface.pacingSizing && surface.titleBarHeight > 0) {
      regions.push_back(CaptureRegion{
          .name = "titlebar:" + std::to_string(surface.id),
          .x = surface.x,
          .y = surface.y - surface.titleBarHeight,
          .width = surface.width,
          .height = surface.titleBarHeight,
      });
    }
    if (!topBandAdded && surface.windowClipTop > 0) {
      regions.push_back(CaptureRegion{
          .name = "top-reserved-band",
          .x = 0,
          .y = 0,
          .width = wayland.logicalOutputWidth(),
          .height = surface.windowClipTop,
      });
      topBandAdded = true;
    }
    if (!dockAdded && surface.windowClipBottom > 0) {
      regions.push_back(CaptureRegion{
          .name = "dock-reserved-band",
          .x = 0,
          .y = surface.windowClipBottom,
          .width = wayland.logicalOutputWidth(),
          .height = std::max(0, wayland.logicalOutputHeight() - surface.windowClipBottom),
      });
      dockAdded = true;
    }
  }
  return regions;
}

struct SnapFrameCapture {
  bool enabled = false;
  int maxFrames = 0;
  int tailFrames = 8;
  int frames = 0;
  int remainingTail = 0;
  bool started = false;
  bool loggedStart = false;
  std::filesystem::path directory;
  std::ofstream csv;
  std::unordered_map<std::string, double> previousLuma;

  static SnapFrameCapture fromEnvironment() {
    int const requestedFrames = positiveEnvInt("LWM_SNAP_CAPTURE_FRAMES", 0, 600);
    if (requestedFrames <= 0) return {};
    SnapFrameCapture capture;
    capture.enabled = true;
    capture.maxFrames = requestedFrames;
    capture.tailFrames = positiveEnvInt("LWM_SNAP_CAPTURE_TAIL_FRAMES", 8, 120);
    capture.directory = snapCaptureRoot() / captureTimestamp();
    return capture;
  }

  [[nodiscard]] bool wantsFrame(WaylandServer const& wayland) {
    if (!enabled || frames >= maxFrames) return false;
    bool const active = wayland.hasActiveAnimations() ||
                        wayland.snapPreviewWakeDelayMs().has_value() ||
                        wayland.windowCyclerWakeDelayMs().has_value();
    if (active) {
      started = true;
      remainingTail = tailFrames;
    } else if (started && remainingTail > 0) {
      --remainingTail;
    } else {
      return false;
    }
    ensureOpen();
    return csv.is_open();
  }

  void ensureOpen() {
    if (loggedStart) return;
    loggedStart = true;
    std::filesystem::create_directories(directory);
    csv.open(directory / "metrics.csv", std::ios::out | std::ios::trunc);
    if (!csv) {
      std::fprintf(stderr,
                   "lambda-window-manager: failed to open snap capture metrics at %s\n",
                   (directory / "metrics.csv").string().c_str());
      enabled = false;
      return;
    }
    csv << "frame,region,x,y,width,height,mean_luma,mean_alpha,delta_luma,pixels\n";
    std::fprintf(stderr,
                 "lambda-window-manager: snap frame capture writing to %s\n",
                 directory.string().c_str());
  }

  void recordFrame(std::vector<std::uint8_t> const& bgra,
                   std::uint32_t width,
                   std::uint32_t height,
                   WaylandServer const& wayland) {
    if (!enabled || !csv || frames >= maxFrames) return;
    std::filesystem::path const path = directory / ("frame-" + std::to_string(frames) + ".png");
    auto saved = saveScreenshotPng(path, bgra, width, height);
    if (!saved.error.empty()) {
      std::fprintf(stderr,
                   "lambda-window-manager: failed to save snap capture frame to %s: %s\n",
                   saved.path.string().c_str(),
                   saved.error.c_str());
    }
    for (auto const& region : snapCaptureRegions(wayland)) {
      auto stats = captureRegionStats(bgra,
                                      width,
                                      height,
                                      region,
                                      wayland.logicalOutputWidth(),
                                      wayland.logicalOutputHeight());
      if (!stats) continue;
      auto const previous = previousLuma.find(region.name);
      double const delta = previous == previousLuma.end() ? 0.0 : stats->meanLuma - previous->second;
      previousLuma[region.name] = stats->meanLuma;
      csv << frames << ','
          << region.name << ','
          << region.x << ','
          << region.y << ','
          << region.width << ','
          << region.height << ','
          << std::fixed << std::setprecision(3)
          << stats->meanLuma << ','
          << stats->meanAlpha << ','
          << delta << ','
          << stats->pixels << '\n';
    }
    csv.flush();
    ++frames;
    if (frames >= maxFrames) {
      std::fprintf(stderr,
                   "lambda-window-manager: snap frame capture complete: %s (%d frames)\n",
                   directory.string().c_str(),
                   frames);
    }
  }
};

double ageMilliseconds(std::uint64_t nowNsec, std::uint64_t thenNsec) {
  return thenNsec > 0 && nowNsec >= thenNsec ? static_cast<double>(nowNsec - thenNsec) / 1'000'000.0 : 0.0;
}

struct SnapAnimationTrace {
  bool enabled = false;
  int maxFrames = 0;
  int tailFrames = 12;
  int frames = 0;
  int remainingTail = 0;
  bool started = false;
  bool loggedStart = false;
  std::filesystem::path directory;
  std::ofstream csv;

  static SnapAnimationTrace fromEnvironment() {
    if (!envEnabled("LWM_SNAP_TRACE")) return {};
    SnapAnimationTrace trace;
    trace.enabled = true;
    trace.maxFrames = positiveEnvInt("LWM_SNAP_TRACE_FRAMES", 600, 10'000);
    trace.tailFrames = positiveEnvInt("LWM_SNAP_TRACE_TAIL_FRAMES", 12, 240);
    trace.directory = snapTraceRoot() / captureTimestamp();
    return trace;
  }

  [[nodiscard]] bool wantsFrame(WaylandServer const& wayland) {
    if (!enabled || frames >= maxFrames) return false;
    bool const active = wayland.hasActiveAnimations() ||
                        wayland.snapPreviewWakeDelayMs().has_value() ||
                        wayland.windowCyclerWakeDelayMs().has_value();
    if (active) {
      started = true;
      remainingTail = tailFrames;
    } else if (started && remainingTail > 0) {
      --remainingTail;
    } else {
      return false;
    }
    ensureOpen();
    return csv.is_open();
  }

  void ensureOpen() {
    if (loggedStart) return;
    loggedStart = true;
    try {
      std::filesystem::create_directories(directory);
    } catch (std::exception const& error) {
      std::fprintf(stderr,
                   "lambda-window-manager: failed to create snap trace directory %s: %s\n",
                   directory.string().c_str(),
                   error.what());
      enabled = false;
      return;
    }
    csv.open(directory / "snap.csv", std::ios::out | std::ios::trunc);
    if (!csv) {
      std::fprintf(stderr,
                   "lambda-window-manager: failed to open snap trace at %s\n",
                   (directory / "snap.csv").string().c_str());
      enabled = false;
      return;
    }
    csv << "frame,event,monotonic_ms,content_serial,output_width,output_height,surface_count,"
           "snap_preview,active_animations,surface_id,x,y,width,height,buffer_width,buffer_height,"
           "committed_width,committed_height,titlebar_height,server_side,focused,active_sizing,pacing_sizing,"
           "geometry_animation_growing,"
           "window_clip_top,window_clip_bottom,shadow_clip_top,shadow_clip_bottom,"
           "last_configure_serial,last_configure_width,last_configure_height,"
           "input_to_render_ms,configure_to_render_ms,ack_to_render_ms,commit_to_render_ms,"
           "configure_to_commit_ms,render_ahead,total_ms,background_ms,snapshot_ms,surface_ms,present_ms\n";
    std::fprintf(stderr,
                 "lambda-window-manager: snap trace writing to %s\n",
                 directory.string().c_str());
  }

  void recordFrame(WaylandServer const& wayland,
                   std::vector<CommittedSurfaceSnapshot> const* surfacesOverride,
                   AtomicReadyFrame const* readyFrame,
                   bool renderAheadFrame) {
    if (!enabled || !csv || frames >= maxFrames) return;
    std::uint64_t const nowNsec = monotonicNanoseconds();
    double const nowMs = static_cast<double>(nowNsec) / 1'000'000.0;
    std::vector<CommittedSurfaceSnapshot> localSurfaces;
    std::vector<CommittedSurfaceSnapshot> const* surfaces = surfacesOverride;
    if (!surfaces) {
      localSurfaces = wayland.committedSurfaces();
      surfaces = &localSurfaces;
    }
    bool const snapPreview = wayland.snapPreview().has_value();
    bool const activeAnimations = wayland.hasActiveAnimations();
    auto const writeCommon = [&](char const* event, std::uint64_t surfaceId) {
      csv << frames << ','
          << event << ','
          << std::fixed << std::setprecision(3)
          << nowMs << ','
          << wayland.contentSerial() << ','
          << wayland.logicalOutputWidth() << ','
          << wayland.logicalOutputHeight() << ','
          << surfaces->size() << ','
          << (snapPreview ? 1 : 0) << ','
          << (activeAnimations ? 1 : 0) << ','
          << surfaceId << ',';
    };

    presentation::AtomicFrameProfile const profile = readyFrame ? readyFrame->profile : presentation::AtomicFrameProfile{};
    writeCommon("frame", 0);
    for (int i = 0; i < 26; ++i) {
      csv << "0,";
    }
    csv << (readyFrame ? (readyFrame->renderedAhead ? 1 : 0) : (renderAheadFrame ? 1 : 0)) << ','
        << profile.totalMs << ','
        << profile.backgroundMs << ','
        << profile.snapshotMs << ','
        << profile.surfaceMs << ','
        << profile.presentMs << '\n';

    for (auto const& surface : *surfaces) {
      double const configureToCommitMs =
          surface.lastConfigureSentNsec > 0 && surface.lastCommitNsec >= surface.lastConfigureSentNsec
              ? static_cast<double>(surface.lastCommitNsec - surface.lastConfigureSentNsec) / 1'000'000.0
              : 0.0;
      writeCommon("surface", surface.id);
      csv << surface.x << ','
          << surface.y << ','
          << surface.width << ','
          << surface.height << ','
          << surface.bufferWidth << ','
          << surface.bufferHeight << ','
          << surface.committedWidth << ','
          << surface.committedHeight << ','
          << surface.titleBarHeight << ','
          << (surface.serverSideDecorated ? 1 : 0) << ','
          << (surface.focused ? 1 : 0) << ','
          << (surface.activeSizing ? 1 : 0) << ','
          << (surface.pacingSizing ? 1 : 0) << ','
          << (surface.geometryAnimationGrowing ? 1 : 0) << ','
          << surface.windowClipTop << ','
          << surface.windowClipBottom << ','
          << surface.shadowClipTop << ','
          << surface.shadowClipBottom << ','
          << surface.lastConfigureSerial << ','
          << surface.lastConfigureWidth << ','
          << surface.lastConfigureHeight << ','
          << ageMilliseconds(nowNsec, surface.lastResizeInputNsec) << ','
          << ageMilliseconds(nowNsec, surface.lastConfigureSentNsec) << ','
          << ageMilliseconds(nowNsec, surface.lastConfigureAckNsec) << ','
          << ageMilliseconds(nowNsec, surface.lastCommitNsec) << ','
          << configureToCommitMs << ','
          << (readyFrame ? (readyFrame->renderedAhead ? 1 : 0) : (renderAheadFrame ? 1 : 0)) << ','
          << profile.totalMs << ','
          << profile.backgroundMs << ','
          << profile.snapshotMs << ','
          << profile.surfaceMs << ','
          << profile.presentMs << '\n';
    }
    if ((frames & 31) == 0) csv.flush();
    ++frames;
    if (frames >= maxFrames) {
      csv.flush();
      std::fprintf(stderr,
                   "lambda-window-manager: snap trace complete: %s (%d frames)\n",
                   directory.string().c_str(),
                   frames);
    }
  }
};

} // namespace

int runKmsCompositor(std::atomic<bool>& running, KmsCompositorOptions options) {
  try {
    diagnostics::crashLog("runtime-start listOutputs=%d", options.listOutputs ? 1 : 0);
    if (diagnostics::cpuTraceEnabled()) {
      std::fprintf(stderr,
                   "lambda-window-manager: CPU trace logging to %s (set LAMBDA_WINDOW_MANAGER_CPU_TRACE=0 to disable)\n",
                   diagnostics::cpuTracePath());
    }
    auto device = lambda::platform::KmsDevice::open();
    auto outputs = device->outputs();
    if (outputs.empty()) {
      std::fprintf(stderr, "lambda-window-manager: no connected KMS outputs\n");
      diagnostics::crashLog("runtime-abort no-connected-outputs");
      return 1;
    }
    presentation::printOutputs(outputs);
    if (options.listOutputs) return 0;

    LoadedCompositorConfig loadedConfig = loadConfigWithMetadata();
    auto outputIndex = presentation::selectOutputIndex(outputs, loadedConfig.config.outputSelector);
    if (!outputIndex) {
      std::fprintf(stderr,
                   "lambda-window-manager: output selector \"%s\" did not match any connected output\n",
                   loadedConfig.config.outputSelector ? loadedConfig.config.outputSelector->c_str() : "");
      diagnostics::crashLog("runtime-abort output-selector-miss selector=%s",
                            loadedConfig.config.outputSelector ? loadedConfig.config.outputSelector->c_str() : "");
      presentation::printOutputs(outputs);
      return 1;
    }

    lambda::platform::KmsOutput const& output = outputs[*outputIndex];
    diagnostics::crashLog("runtime-output name=%s physical=%ux%u refresh_millihz=%u scale=%.3f",
                          output.name().c_str(),
                          output.width(),
                          output.height(),
                          output.refreshRateMilliHz(),
                          scaleForOutput(loadedConfig.config, output.name()));
    WaylandServer wayland({
        .name = output.name(),
        .width = static_cast<std::int32_t>(output.width()),
        .height = static_cast<std::int32_t>(output.height()),
        .refreshMilliHz = static_cast<std::int32_t>(output.refreshRateMilliHz()),
        .drmDevice = drmDeviceForFd(device->fd()),
        .drmFd = device->fd(),
    });
    auto lastInputActivity = SteadyClock::now();
    bool inputActivityThisLoop = false;
    bool inputRenderRequiredThisLoop = false;
    bool idleBlanked = false;
    bool displayTimingSupportLogged = false;
    bool useVulkanPresentationCompletion = false;

    lambda::configureVulkanCanvasRuntime(device->requiredVulkanInstanceExtensions(), device->cacheDir());
    auto& vulkan = lambda::VulkanContext::instance();
    vulkan.addRequiredDeviceExtension(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
    vulkan.addRequiredDeviceExtension(VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
    vulkan.addRequiredDeviceExtension(VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME);
    vulkan.addRequiredDeviceExtension(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
    auto timingStart = SteadyClock::now();
    VkInstance instance = lambda::ensureSharedVulkanInstance();
    LAMBDA_WINDOW_MANAGER_TRACE_TIMING("ensure-vulkan-instance", timingStart);

    static lambda::FreeTypeTextSystem textSystem;

    auto effectiveConfig = [&] {
      CompositorConfig config = loadedConfig.config;
      config.scale = scaleForOutput(config, output.name());
      return config;
    };
    wayland.setPreferredScale(effectiveConfig().scale);

    auto const canvasStart = SteadyClock::now();
    auto presenter = createPresenter(PresenterContext{
        .output = output,
        .textSystem = textSystem,
        .vulkanInstance = instance,
        .dpiScale = wayland.preferredScale(),
        .logicalWidth = wayland.logicalOutputWidth(),
        .logicalHeight = wayland.logicalOutputHeight(),
    });
    diagnostics::crashLog(presenter->kind() == PresenterKind::AtomicKms ? "presenter atomic-kms"
                                                                          : "presenter vulkan-display");
    if (auto* atomicPresenter = presenter->atomicPresenter()) {
      std::vector<DmabufFormatModifierPreference> preferences;
      for (lambda::platform::KmsDmabufFormatModifier const& pair :
           atomicPresenter->overlayDmabufFormatModifierPreferences()) {
        preferences.push_back(DmabufFormatModifierPreference{
            .format = pair.format,
            .modifier = pair.modifier,
        });
      }
      wayland.setDmabufFormatModifierPreferences(std::move(preferences));
    }
    lambda::Canvas& canvasRef = presenter->canvas();
    lambda::Canvas* canvas = &canvasRef;
    LAMBDA_WINDOW_MANAGER_TRACE_TIMING("create-presenter", canvasStart);

    std::fprintf(stderr,
                 "lambda-window-manager: presenting %ux%u on %s\n",
                 output.width(),
                 output.height(),
                 output.name().c_str());
    diagnostics::crashLog("runtime-presenting output=%s socket=%s logical=%dx%d scale=%.3f",
                          output.name().c_str(),
                          wayland.socketName(),
                          wayland.logicalOutputWidth(),
                          wayland.logicalOutputHeight(),
                          wayland.preferredScale());

    AppliedCompositorConfig appliedConfig{};
    auto applyOutputScale = [&](bool force) {
      float const previousScale = wayland.preferredScale();
      wayland.setPreferredScale(appliedConfig.config.scale);
      if (force || std::abs(previousScale - wayland.preferredScale()) > 0.001f) {
        presenter->updateOutputGeometry(wayland.preferredScale(), wayland.logicalOutputWidth(), wayland.logicalOutputHeight());
        std::fprintf(stderr,
                     "lambda-window-manager: output physical=%ux%u logical=%dx%d scale=%.2f\n",
                     output.width(),
                     output.height(),
                     wayland.logicalOutputWidth(),
                     wayland.logicalOutputHeight(),
                     wayland.preferredScale());
        diagnostics::crashLog("output-scale physical=%ux%u logical=%dx%d scale=%.3f force=%d",
                              output.width(),
                              output.height(),
                              wayland.logicalOutputWidth(),
                              wayland.logicalOutputHeight(),
                              wayland.preferredScale(),
                              force ? 1 : 0);
      }
    };

    CompositorConfigWatchContext configWatch{
        .loadedConfig = loadedConfig,
        .appliedConfig = appliedConfig,
        .wayland = wayland,
        .presenter = *presenter,
        .canvas = *canvas,
        .effectiveConfig = effectiveConfig,
        .applyOutputScale = applyOutputScale,
    };
    AsyncWallpaperLoader wallpaperLoader;
    configWatch.wallpaperLoader = &wallpaperLoader;
    configWatch.wallpaperMaxLongEdge = std::max(output.width(), output.height());
    configWatch.wallpaperCacheDir = wallpaperCacheDirectory(device->cacheDir());
    applyCompositorRuntimeConfig(configWatch, true);
    applyDiagnosticFloorChrome(wayland, appliedConfig);

    SurfaceRenderState surfaceRenderState;
    CursorRenderState cursorState;
    LoopInstrumentation loopStats;
    CompositorFrameProfile frameProfile;
    bool const detailedFrameProfile =
        diagnostics::cpuTraceEnabled() || frameProfile.enabled || pacingTraceEnabled() || envEnabled("LWM_SNAP_TRACE");
    std::uint32_t const hardwareCursorWidth = output.cursorWidth();
    std::uint32_t const hardwareCursorHeight = output.cursorHeight();
    bool const hardwareCursorAvailable = hardwareCursorWidth > 0 && hardwareCursorHeight > 0;
    if (!hardwareCursorAvailable && appliedConfig.config.hardwareCursorEnabled) {
      std::fprintf(stderr, "lambda-window-manager: hardware cursor unavailable; using software cursor\n");
    }

    device->setInputHandler([&](lambda::platform::KmsInputEvent const& event) {
      inputActivityThisLoop = true;
      if (idleBlanked) {
        inputRenderRequiredThisLoop = true;
        return;
      }
      std::uint64_t const beforeSerial = wayland.contentSerial();
      dispatchKmsInputEvent(wayland, event);
      bool const contentChanged = wayland.contentSerial() != beforeSerial;
      if (contentChanged) {
        inputRenderRequiredThisLoop = true;
        return;
      }
      bool const hardwareCursorMoved =
          moveCurrentHardwareCursor(wayland,
                                    output,
                                    cursorState,
                                    appliedConfig.config.hardwareCursorEnabled && hardwareCursorAvailable);
      if (hardwareCursorMoved) {
        return;
      } else {
        inputRenderRequiredThisLoop = true;
      }
    });

    bool forceRender = true;
    std::optional<ScreenshotRequest> screenshotPending;
    std::optional<ScreenshotRequest> screenshotCaptureInFlight;
    bool snapCaptureInFlight = false;
    int frameCapturePollAttempts = 0;
    SnapFrameCapture snapFrameCapture = SnapFrameCapture::fromEnvironment();
    SnapAnimationTrace snapAnimationTrace = SnapAnimationTrace::fromEnvironment();
    if (snapAnimationTrace.enabled) snapAnimationTrace.ensureOpen();
    bool skipNextVblank = true;
    constexpr auto kScreenshotFlashDuration = std::chrono::milliseconds(170);
    std::optional<std::chrono::steady_clock::time_point> screenshotFlashStartedAt;
    auto screenshotFlashOpacityAt = [&](std::chrono::steady_clock::time_point now) {
      if (!screenshotFlashStartedAt) return 0.f;
      auto const elapsed = now - *screenshotFlashStartedAt;
      if (elapsed >= kScreenshotFlashDuration) return 0.f;
      float const t = std::clamp(
          static_cast<float>(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()) /
              static_cast<float>(std::chrono::duration_cast<std::chrono::microseconds>(kScreenshotFlashDuration).count()),
          0.f,
          1.f);
      float const remaining = 1.f - t;
      return 0.42f * remaining * remaining;
    };
    auto startScreenshotFlash = [&] {
      screenshotFlashStartedAt = std::chrono::steady_clock::now();
      skipNextVblank = true;
      if (idleBlanked) idleBlanked = false;
    };
    auto processCompletedFrameCapture = [&] {
      if (!screenshotCaptureInFlight && !snapCaptureInFlight) {
        return false;
      }
      std::vector<std::uint8_t> pixels;
      std::uint32_t width = 0;
      std::uint32_t height = 0;
      if (!lambda::takeCapturedFrameForCanvas(canvas, pixels, width, height)) {
        ++frameCapturePollAttempts;
        if (frameCapturePollAttempts > 120) {
          std::fprintf(stderr, "lambda-window-manager: screenshot capture did not produce a frame\n");
          screenshotCaptureInFlight.reset();
          snapCaptureInFlight = false;
          frameCapturePollAttempts = 0;
          return false;
        }
        forceRender = true;
        skipNextVblank = true;
        return false;
      }
      frameCapturePollAttempts = 0;
      if (screenshotCaptureInFlight) {
        if (saveScreenshotRequest(*screenshotCaptureInFlight,
                                  pixels,
                                  width,
                                  height,
                                  wayland,
                                  appliedConfig.config.chrome)) {
          startScreenshotFlash();
        }
        screenshotCaptureInFlight.reset();
      }
      if (snapCaptureInFlight) {
        snapFrameCapture.recordFrame(pixels, width, height, wayland);
        snapCaptureInFlight = false;
      }
      return true;
    };
    bool wasVtForeground = device->isVtForeground();
    bool vtAcquireFramePending = false;
    std::uint64_t softwarePresentationSequence = 0;
    std::uint64_t lastAtomicFlipNsec = 0;
    std::uint64_t lastAtomicScheduledNsec = 0;
    double lastAtomicScheduledRenderMs = 0.0;
    auto nextConfigCheckAt = SteadyClock::now();
    std::uint32_t diagnosticExerciseStep = 0;
    auto nextDiagnosticExerciseAt = SteadyClock::now() + std::chrono::milliseconds(17);
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
    AtomicReadyFrame atomicRenderedFrame{};
    std::deque<AtomicReadyFrame> atomicReadyFrames;
    bool atomicFrameDirty = true;
    std::uint64_t lastKnownContentSerial = wayland.contentSerial();
    presentation::AtomicFrameProfile lastAtomicScheduledProfile{};
    std::vector<std::uint64_t> lastAtomicScheduledFrameSurfaceIds;
    std::uint64_t atomicRenderAheadLeadEstimateNsec = initialRenderAheadLeadNsec(output.refreshRateMilliHz());
    std::uint64_t lastAcquireWaitFrameCallbackNsec = 0;
    auto lastCrashHeartbeat = SteadyClock::now();
    auto maybeCrashHeartbeat = [&](char const* reason) {
      if (!diagnostics::crashLogEnabled()) return;
      auto const now = SteadyClock::now();
      if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCrashHeartbeat).count() < 2000) return;
      lastCrashHeartbeat = now;
      diagnostics::crashLog("heartbeat reason=%s loops=%llu surfaces=%llu toplevels=%zu serial=%llu "
                            "dirty=%d ready=%d pendingFlip=%d vt=%d idleBlanked=%d forceRender=%d",
                            reason ? reason : "loop",
                            static_cast<unsigned long long>(loopStats.loops),
                            static_cast<unsigned long long>(loopStats.lastSurfaceCount),
                            wayland.toplevelCount(),
                            static_cast<unsigned long long>(wayland.contentSerial()),
                            atomicFrameDirty ? 1 : 0,
                            atomicReadyFrames.empty() ? 0 : 1,
                            presenter->atomicPresenter() && presenter->atomicPresenter()->hasPendingPageFlip() ? 1 : 0,
                            device->isVtForeground() ? 1 : 0,
                            idleBlanked ? 1 : 0,
                            forceRender ? 1 : 0);
    };
    constexpr int kIdlePollMs = 250;
    auto diagnosticExerciseInterval = [&] {
      std::uint64_t const refresh = refreshNsec(output.refreshRateMilliHz());
      if (refresh == 0) return std::chrono::milliseconds(17);
      std::uint64_t const ms = std::max<std::uint64_t>(1, (refresh + 999'999ull) / 1'000'000ull);
      return std::chrono::milliseconds(ms);
    };
    auto diagnosticExerciseDelayMs = [&] {
      if (!envEnabled("LWM_DIAGNOSTIC_SCRIPTED_EXERCISE")) return kIdlePollMs;
      auto const now = SteadyClock::now();
      return millisecondsUntilCeil(nextDiagnosticExerciseAt, now, kIdlePollMs);
    };
    auto runDiagnosticExercise = [&] {
      if (!envEnabled("LWM_DIAGNOSTIC_SCRIPTED_EXERCISE")) return false;
      auto const now = SteadyClock::now();
      if (now < nextDiagnosticExerciseAt) return false;
      nextDiagnosticExerciseAt = now + diagnosticExerciseInterval();
      bool const resize = envEnabled("LWM_DIAGNOSTIC_SCRIPTED_RESIZE");
      return wayland.diagnosticExerciseTopToplevel(diagnosticExerciseStep++, resize);
    };
    auto atomicCanPrepareFrame = [&]() {
      return presenter->atomicPresenter() && presenter->atomicPresenter()->canPrepareFrame();
    };
    auto diagnosticRenderAheadAllowed = [&]() {
      return !envEnabled("LWM_DIAGNOSTIC_DISABLE_RENDER_AHEAD");
    };
    auto atomicRenderAheadDeadlineNsec = [&]() -> std::uint64_t {
      if (!presenter->atomicPresenter() || !presenter->atomicPresenter()->hasPendingPageFlip()) return 0;
      std::uint64_t const refresh = refreshNsec(output.refreshRateMilliHz());
      if (lastAtomicFlipNsec == 0 || refresh == 0) return 0;
      std::uint64_t const expectedFlipNsec = lastAtomicFlipNsec + refresh;
      std::uint64_t const lead = clampRenderAheadLeadNsec(atomicRenderAheadLeadEstimateNsec,
                                                          output.refreshRateMilliHz());
      if (expectedFlipNsec <= lead) return 0;
      return expectedFlipNsec - lead;
    };
    auto atomicRenderAheadDelayMs = [&]() -> int {
      if (!presenter->atomicPresenter() || !atomicFrameDirty ||
          !presenter->atomicPresenter()->hasPendingPageFlip() || !atomicCanPrepareFrame()) {
        return kIdlePollMs;
      }
      if (wayland.hasActiveResizePacing()) return 0;
      std::uint64_t const deadline = atomicRenderAheadDeadlineNsec();
      if (deadline == 0) return 0;
      std::uint64_t const now = monotonicNanoseconds();
      if (now >= deadline) return 0;
      std::uint64_t const remainingNsec = deadline - now;
      std::uint64_t const remainingMs = (remainingNsec + 999'999ull) / 1'000'000ull;
      return static_cast<int>(std::min<std::uint64_t>(remainingMs, kIdlePollMs));
    };
    auto atomicRenderAheadDue = [&]() {
      return presenter->atomicPresenter() && atomicFrameDirty && atomicCanPrepareFrame() &&
             presenter->atomicPresenter()->hasPendingPageFlip() && atomicRenderAheadDelayMs() == 0;
    };
    auto queuedScanoutAcquireFenceFd = [&]() -> int {
      if (!presenter->atomicPresenter() || atomicReadyFrames.empty()) return -1;
      AtomicReadyFrame const& frame = atomicReadyFrames.back();
      if (!frame.directScanout && !frame.overlayOnly) return -1;
      if (frame.scanoutCandidate) return frame.scanoutCandidate->acquireFenceFd;
      if (frame.directScanout) return presenter->atomicPresenter()->preparedDirectScanoutAcquireFenceFd();
      return presenter->atomicPresenter()->preparedOverlayAcquireFenceFd();
    };
    auto queuedScanoutWaitingForAcquire = [&]() {
      int const fd = queuedScanoutAcquireFenceFd();
      return fd >= 0 && !fdReadableNow(fd);
    };
    auto acquireWaitFrameCallbackDelayMs = [&]() -> int {
      if (!presenter->atomicPresenter() || presenter->atomicPresenter()->hasPendingPageFlip() ||
          !queuedScanoutWaitingForAcquire()) {
        return kIdlePollMs;
      }
      if (presenter->atomicPresenter()->canScheduleDirectScanoutRepeat()) return 0;
      std::uint64_t const refresh = refreshNsec(output.refreshRateMilliHz());
      if (refresh == 0) return 0;
      std::uint64_t const anchor = std::max(lastAtomicFlipNsec, lastAcquireWaitFrameCallbackNsec);
      if (anchor == 0) return 0;
      std::uint64_t const deadline = anchor + refresh;
      std::uint64_t const now = monotonicNanoseconds();
      if (now >= deadline) return 0;
      std::uint64_t const remainingMs = (deadline - now + 999'999ull) / 1'000'000ull;
      return static_cast<int>(std::min<std::uint64_t>(remainingMs, kIdlePollMs));
    };
    auto maybeSendAcquireWaitFrameCallback = [&]() {
      if (!device->isVtForeground() || !presenter->atomicPresenter() ||
          presenter->atomicPresenter()->hasPendingPageFlip() || !queuedScanoutWaitingForAcquire() ||
          acquireWaitFrameCallbackDelayMs() != 0) {
        return false;
      }
      if (presenter->atomicPresenter()->canScheduleDirectScanoutRepeat()) {
        auto const traceStart = diagnostics::cpuTraceNow();
        std::uint32_t const presentId = presenter->atomicPresenter()->scheduleDirectScanoutRepeat();
        if (presentId == 0) {
          diagnostics::recordCpuAtomicLoop({
              .scheduleAttempts = 1,
              .scheduleSuccess = 0,
              .scheduleDirectRepeat = 1,
              .scheduleMs = diagnostics::cpuTraceElapsedMilliseconds(traceStart),
          });
          return false;
        }
        diagnostics::recordCpuAtomicLoop({
            .scheduleAttempts = 1,
            .scheduleSuccess = 1,
            .scheduleDirectRepeat = 1,
            .scheduleMs = diagnostics::cpuTraceElapsedMilliseconds(traceStart),
        });
        std::uint64_t const scheduledNsec = monotonicNanoseconds();
        double const sinceLastFlipMs =
            lastAtomicFlipNsec > 0 && scheduledNsec >= lastAtomicFlipNsec
                ? static_cast<double>(scheduledNsec - lastAtomicFlipNsec) / 1'000'000.0
                : 0.0;
        LAMBDA_WINDOW_MANAGER_TRACE_PACING("flip-scheduled id=%u surfaces=0 activeSizing=0 maxBuffer=0x0 "
                    "maxFrame=0x0 maxDmabuf=0x00000000 maxDmabufModifier=0x0000000000000000 "
                    "snapshotFrameTime=0.000ms renderAhead=0 directRepeat=1 "
                    "contentSerial=%llu sinceLastFlip=%.3fms gpuWait=0.000ms "
                    "ageSurface=0 inputToRender=0.000ms configureToRender=0.000ms ackToRender=0.000ms "
                    "commitToRender=0.000ms configureToCommit=0.000ms "
                    "phaseBg=0.000ms phaseSnapshot=0.000ms phaseSurface=0.000ms "
                    "phaseClosing=0.000ms phaseCursor=0.000ms "
                    "phasePresent=0.000ms phaseTotal=0.000ms\n",
                    presentId,
                    static_cast<unsigned long long>(wayland.contentSerial()),
                    sinceLastFlipMs);
        lastAtomicScheduledNsec = scheduledNsec;
        lastAtomicScheduledRenderMs = 0.0;
        lastAtomicScheduledProfile = {};
        lastAtomicScheduledFrameSurfaceIds.clear();
      } else {
        return false;
      }
      lastAcquireWaitFrameCallbackNsec = monotonicNanoseconds();
      LAMBDA_WINDOW_MANAGER_TRACE_PACING("acquire-wait-repeat contentSerial=%llu queuedSerial=%llu acquireFd=%d\n",
                  static_cast<unsigned long long>(wayland.contentSerial()),
                  static_cast<unsigned long long>(atomicReadyFrames.empty() ? 0ull : atomicReadyFrames.back().contentSerial),
                  queuedScanoutAcquireFenceFd());
      return true;
    };
    auto updateAtomicRenderAheadLead = [&](std::uint64_t renderToReadyNsec, bool usedRenderFence) {
      std::uint64_t const refresh = refreshNsec(output.refreshRateMilliHz());
      std::uint64_t const guard = refresh > 0 ? refresh / 8ull : 2'000'000ull;
      std::uint64_t const nominalLead = nominalRenderAheadLeadNsec(output.refreshRateMilliHz());
      std::uint64_t target = nominalLead;
      if (renderToReadyNsec > 0) {
        target = renderToReadyNsec + guard;
      } else if (!usedRenderFence) {
        target = guard * 2ull;
      }
      target = clampInteractiveRenderAheadLeadNsec(target, output.refreshRateMilliHz());
      if (target >= atomicRenderAheadLeadEstimateNsec) {
        atomicRenderAheadLeadEstimateNsec = (atomicRenderAheadLeadEstimateNsec * 3ull + target) / 4ull;
      } else {
        atomicRenderAheadLeadEstimateNsec = (atomicRenderAheadLeadEstimateNsec + target * 3ull) / 4ull;
      }
    };
    auto pollFds = [&](std::array<int, 8>& storage) {
      std::size_t count = 0;
      int const waylandEventFd = wayland.eventFd();
      if (waylandEventFd >= 0) storage[count++] = waylandEventFd;
      int const shellIpcFd = wayland.shellIpcFd();
      if (shellIpcFd >= 0) storage[count++] = shellIpcFd;
      if (presenter->atomicPresenter() && presenter->atomicPresenter()->hasPendingPageFlip()) {
        int const flipFd = presenter->atomicPresenter()->eventFd();
        if (flipFd >= 0) storage[count++] = flipFd;
      }
	      if (presenter->atomicPresenter()) {
	        for (auto frame = atomicReadyFrames.rbegin(); frame != atomicReadyFrames.rend(); ++frame) {
	          if (frame->overlayOnly || frame->directScanout) continue;
	          int const readyFd = presenter->atomicPresenter()->renderReadyFd(frame->presentToken);
	          if (readyFd >= 0) {
	            storage[count++] = readyFd;
	            break;
	          }
	        }
	        for (auto frame = atomicReadyFrames.rbegin(); frame != atomicReadyFrames.rend(); ++frame) {
	          int acquireFenceFd = -1;
	          if (frame->scanoutCandidate) {
	            acquireFenceFd = frame->scanoutCandidate->acquireFenceFd;
	          } else if (frame->directScanout) {
	            acquireFenceFd = presenter->atomicPresenter()->preparedDirectScanoutAcquireFenceFd();
	          } else if (frame->overlayOnly) {
	            acquireFenceFd = presenter->atomicPresenter()->preparedOverlayAcquireFenceFd();
	          }
	          if (acquireFenceFd < 0) continue;
	          if (!fdReadableNow(acquireFenceFd)) {
	            storage[count++] = acquireFenceFd;
	            break;
	          }
	        }
	      }
      return std::span<int const>(storage.data(), count);
    };
    auto pollMaskHas = [](std::uint64_t mask, std::size_t index) {
      return index < 64 && (mask & (std::uint64_t{1} << index)) != 0;
    };
    auto handleVtResume = [&] {
      auto const resumeStart = SteadyClock::now();
      diagnostics::crashLog("vt-resume begin");
      applyOutputScale(true);
      if (auto* atomicPresenter = presenter->atomicPresenter()) {
        atomicPresenter->syncModeStateFromKernel();
      }
      LAMBDA_WINDOW_MANAGER_TRACE_TIMING("vt-resume-total", resumeStart);
      vtAcquireFramePending = true;
      forceRender = true;
      skipNextVblank = true;
      atomicFrameDirty = true;
      diagnostics::crashLog("vt-resume end logical=%dx%d serial=%llu",
                            wayland.logicalOutputWidth(),
                            wayland.logicalOutputHeight(),
                            static_cast<unsigned long long>(wayland.contentSerial()));
    };
    auto commitScheduledSceneState = [&](AtomicReadyFrame& frame) {
      if (!frame.sceneGraphStateValid) return;
      surfaceRenderState.sceneGraph = std::move(frame.sceneGraphState);
      frame.sceneGraphStateValid = false;
    };
    auto scheduleAtomicFrame = [&](AtomicReadyFrame& frame) {
      if (!presenter->atomicPresenter() || !frame.ready) return false;
      if (frame.directScanout) {
        std::uint64_t const scheduleStartNsec = monotonicNanoseconds();
        std::uint64_t candidateBufferId = 0;
        int candidateAcquireFenceFd = -1;
        if (frame.scanoutCandidate) {
          candidateBufferId = frame.scanoutCandidate->bufferId;
          candidateAcquireFenceFd = frame.scanoutCandidate->acquireFenceFd;
          bool const acquireReady = fdReadableNow(candidateAcquireFenceFd);
          LAMBDA_WINDOW_MANAGER_TRACE_PACING("scanout-schedule-begin mode=direct contentSerial=%llu buffer=%llu "
                      "acquireFd=%d acquireReady=%d\n",
                      static_cast<unsigned long long>(frame.contentSerial),
                      static_cast<unsigned long long>(candidateBufferId),
                      candidateAcquireFenceFd,
                      acquireReady ? 1 : 0);
          auto candidate = std::move(*frame.scanoutCandidate);
          disarmOverlayCandidateFds(*frame.scanoutCandidate);
          frame.scanoutCandidate.reset();
          std::uint64_t const prepareStartNsec = monotonicNanoseconds();
          if (!presenter->atomicPresenter()->prepareDirectScanoutCandidate(std::move(candidate))) {
            LAMBDA_WINDOW_MANAGER_TRACE_PACING("scanout-prepare-failed mode=direct contentSerial=%llu buffer=%llu elapsed=%.3fms\n",
                        static_cast<unsigned long long>(frame.contentSerial),
                        static_cast<unsigned long long>(candidateBufferId),
                        static_cast<double>(monotonicNanoseconds() - prepareStartNsec) / 1'000'000.0);
            frame = AtomicReadyFrame{};
            return false;
          }
          LAMBDA_WINDOW_MANAGER_TRACE_PACING("scanout-prepare-done mode=direct contentSerial=%llu buffer=%llu elapsed=%.3fms\n",
                      static_cast<unsigned long long>(frame.contentSerial),
                      static_cast<unsigned long long>(candidateBufferId),
                      static_cast<double>(monotonicNanoseconds() - prepareStartNsec) / 1'000'000.0);
        }
        if (!fdReadableNow(presenter->atomicPresenter()->preparedDirectScanoutAcquireFenceFd())) return false;
        if (!presenter->atomicPresenter()->canScheduleDirectScanout()) return false;
        std::uint64_t const frameContentSerial = frame.contentSerial;
        std::uint32_t const presentId = presenter->atomicPresenter()->scheduleDirectScanout();
        if (presentId == 0) return false;
        std::uint64_t const scheduledNsec = monotonicNanoseconds();
        LAMBDA_WINDOW_MANAGER_TRACE_PACING("scanout-schedule-done mode=direct id=%u contentSerial=%llu buffer=%llu "
                    "elapsed=%.3fms commitCall=%.3fms\n",
                    presentId,
                    static_cast<unsigned long long>(frame.contentSerial),
                    static_cast<unsigned long long>(candidateBufferId),
                    static_cast<double>(scheduledNsec - scheduleStartNsec) / 1'000'000.0,
                    candidateAcquireFenceFd >= 0
                        ? static_cast<double>(scheduledNsec - scheduleStartNsec) / 1'000'000.0
                        : 0.0);
        double const sinceLastFlipMs =
            lastAtomicFlipNsec > 0 && scheduledNsec >= lastAtomicFlipNsec
                ? static_cast<double>(scheduledNsec - lastAtomicFlipNsec) / 1'000'000.0
                : 0.0;
        LAMBDA_WINDOW_MANAGER_TRACE_PACING("flip-scheduled id=%u surfaces=%zu activeSizing=%zu maxBuffer=%dx%d "
                    "maxFrame=%dx%d maxDmabuf=0x%08x maxDmabufModifier=0x%016llx "
                    "snapshotFrameTime=%.3fms renderAhead=%d directScanout=1 "
                    "contentSerial=%llu sinceLastFlip=%.3fms gpuWait=0.000ms "
                    "ageSurface=%llu inputToRender=%.3fms configureToRender=%.3fms ackToRender=%.3fms "
                    "commitToRender=%.3fms configureToCommit=%.3fms "
                    "phaseBg=%.3fms phaseSnapshot=%.3fms phaseSurface=%.3fms "
                    "phaseClosing=%.3fms phaseCursor=%.3fms "
                    "phasePresent=%.3fms phaseTotal=%.3fms\n",
                    presentId,
                    frame.surfaceCount,
                    frame.profile.activeSizingSurfaces,
                    frame.profile.maxBufferWidth,
                    frame.profile.maxBufferHeight,
                    frame.profile.maxFrameWidth,
                    frame.profile.maxFrameHeight,
                    frame.profile.maxDmabufFormat,
                    static_cast<unsigned long long>(frame.profile.maxDmabufModifier),
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame.frameTime)
                        .count(),
                    frame.renderedAhead ? 1 : 0,
                    static_cast<unsigned long long>(frame.contentSerial),
                    sinceLastFlipMs,
                    static_cast<unsigned long long>(frame.profile.maxAgeSurfaceId),
                    frame.profile.maxInputToRenderMs,
                    frame.profile.maxConfigureToRenderMs,
                    frame.profile.maxAckToRenderMs,
                    frame.profile.maxCommitToRenderMs,
                    frame.profile.maxConfigureToCommitMs,
                    frame.profile.backgroundMs,
                    frame.profile.snapshotMs,
                    frame.profile.surfaceMs,
                    frame.profile.closingMs,
                    frame.profile.cursorMs,
                    frame.profile.presentMs,
                    frame.profile.totalMs);
        lastAtomicScheduledNsec = scheduledNsec;
        lastAtomicScheduledRenderMs = frame.renderMs;
        lastAtomicScheduledProfile = frame.profile;
        frame.timing.backendPresentId = presentId;
        frame.timing.flags |= static_cast<std::uint32_t>(WP_PRESENTATION_FEEDBACK_KIND_VSYNC |
                                                         WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK);
        lastAtomicScheduledFrameSurfaceIds = frame.frameCallbackSurfaceIds;
        wayland.sendPresentationFeedbacks(monotonicMilliseconds(),
                                          frame.timing,
                                          lastAtomicScheduledFrameSurfaceIds);
        commitScheduledSceneState(frame);
        frame = AtomicReadyFrame{};
        if (wayland.contentSerial() != frameContentSerial) atomicFrameDirty = true;
        forceRender = false;
        return true;
      }
      if (frame.overlayOnly) {
        std::uint64_t const scheduleStartNsec = monotonicNanoseconds();
        std::uint64_t candidateBufferId = 0;
        int candidateAcquireFenceFd = -1;
        if (frame.scanoutCandidate) {
          candidateBufferId = frame.scanoutCandidate->bufferId;
          candidateAcquireFenceFd = frame.scanoutCandidate->acquireFenceFd;
          bool const acquireReady = fdReadableNow(candidateAcquireFenceFd);
          LAMBDA_WINDOW_MANAGER_TRACE_PACING("scanout-schedule-begin mode=overlay contentSerial=%llu buffer=%llu "
                      "acquireFd=%d acquireReady=%d\n",
                      static_cast<unsigned long long>(frame.contentSerial),
                      static_cast<unsigned long long>(candidateBufferId),
                      candidateAcquireFenceFd,
                      acquireReady ? 1 : 0);
          auto candidate = std::move(*frame.scanoutCandidate);
          disarmOverlayCandidateFds(*frame.scanoutCandidate);
          frame.scanoutCandidate.reset();
          std::uint64_t const prepareStartNsec = monotonicNanoseconds();
          if (!presenter->atomicPresenter()->prepareOverlayCandidateForDisplayedFrame(std::move(candidate))) {
            LAMBDA_WINDOW_MANAGER_TRACE_PACING("scanout-prepare-failed mode=overlay contentSerial=%llu buffer=%llu elapsed=%.3fms\n",
                        static_cast<unsigned long long>(frame.contentSerial),
                        static_cast<unsigned long long>(candidateBufferId),
                        static_cast<double>(monotonicNanoseconds() - prepareStartNsec) / 1'000'000.0);
            frame = AtomicReadyFrame{};
            return false;
          }
          LAMBDA_WINDOW_MANAGER_TRACE_PACING("scanout-prepare-done mode=overlay contentSerial=%llu buffer=%llu elapsed=%.3fms\n",
                      static_cast<unsigned long long>(frame.contentSerial),
                      static_cast<unsigned long long>(candidateBufferId),
                      static_cast<double>(monotonicNanoseconds() - prepareStartNsec) / 1'000'000.0);
        }
        if (!fdReadableNow(presenter->atomicPresenter()->preparedOverlayAcquireFenceFd())) return false;
        if (!presenter->atomicPresenter()->canScheduleOverlayOnly()) return false;
        std::uint64_t const frameContentSerial = frame.contentSerial;
        std::uint32_t const presentId = presenter->atomicPresenter()->scheduleOverlayOnly();
        if (presentId == 0) return false;
        std::uint64_t const scheduledNsec = monotonicNanoseconds();
        LAMBDA_WINDOW_MANAGER_TRACE_PACING("scanout-schedule-done mode=overlay id=%u contentSerial=%llu buffer=%llu elapsed=%.3fms\n",
                    presentId,
                    static_cast<unsigned long long>(frame.contentSerial),
                    static_cast<unsigned long long>(candidateBufferId),
                    static_cast<double>(scheduledNsec - scheduleStartNsec) / 1'000'000.0);
        double const sinceLastFlipMs =
            lastAtomicFlipNsec > 0 && scheduledNsec >= lastAtomicFlipNsec
                ? static_cast<double>(scheduledNsec - lastAtomicFlipNsec) / 1'000'000.0
                : 0.0;
        LAMBDA_WINDOW_MANAGER_TRACE_PACING("flip-scheduled id=%u surfaces=%zu activeSizing=%zu maxBuffer=%dx%d "
                    "maxFrame=%dx%d maxDmabuf=0x%08x maxDmabufModifier=0x%016llx "
                    "snapshotFrameTime=%.3fms renderAhead=%d overlayOnly=1 "
                    "contentSerial=%llu sinceLastFlip=%.3fms gpuWait=0.000ms "
                    "ageSurface=%llu inputToRender=%.3fms configureToRender=%.3fms ackToRender=%.3fms "
                    "commitToRender=%.3fms configureToCommit=%.3fms "
                    "phaseBg=%.3fms phaseSnapshot=%.3fms phaseSurface=%.3fms "
                    "phaseClosing=%.3fms phaseCursor=%.3fms "
                    "phasePresent=%.3fms phaseTotal=%.3fms\n",
                    presentId,
                    frame.surfaceCount,
                    frame.profile.activeSizingSurfaces,
                    frame.profile.maxBufferWidth,
                    frame.profile.maxBufferHeight,
                    frame.profile.maxFrameWidth,
                    frame.profile.maxFrameHeight,
                    frame.profile.maxDmabufFormat,
                    static_cast<unsigned long long>(frame.profile.maxDmabufModifier),
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame.frameTime)
                        .count(),
                    frame.renderedAhead ? 1 : 0,
                    static_cast<unsigned long long>(frame.contentSerial),
                    sinceLastFlipMs,
                    static_cast<unsigned long long>(frame.profile.maxAgeSurfaceId),
                    frame.profile.maxInputToRenderMs,
                    frame.profile.maxConfigureToRenderMs,
                    frame.profile.maxAckToRenderMs,
                    frame.profile.maxCommitToRenderMs,
                    frame.profile.maxConfigureToCommitMs,
                    frame.profile.backgroundMs,
                    frame.profile.snapshotMs,
                    frame.profile.surfaceMs,
                    frame.profile.closingMs,
                    frame.profile.cursorMs,
                    frame.profile.presentMs,
                    frame.profile.totalMs);
        lastAtomicScheduledNsec = scheduledNsec;
        lastAtomicScheduledRenderMs = frame.renderMs;
        lastAtomicScheduledProfile = frame.profile;
        frame.timing.backendPresentId = presentId;
        frame.timing.flags |= static_cast<std::uint32_t>(WP_PRESENTATION_FEEDBACK_KIND_VSYNC |
                                                         WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK);
        lastAtomicScheduledFrameSurfaceIds = frame.frameCallbackSurfaceIds;
        wayland.sendPresentationFeedbacks(monotonicMilliseconds(),
                                          frame.timing,
                                          lastAtomicScheduledFrameSurfaceIds);
        commitScheduledSceneState(frame);
        frame = AtomicReadyFrame{};
        if (wayland.contentSerial() != frameContentSerial) atomicFrameDirty = true;
        forceRender = false;
        return true;
      }
      bool const allowRenderFence = frame.renderedAhead && frame.profile.activeSizingSurfaces == 0;
      if (!allowRenderFence && presenter->atomicPresenter()->renderReadyFd(frame.presentToken) >= 0 &&
          !presenter->atomicPresenter()->updateRenderReady(frame.presentToken)) {
        LAMBDA_WINDOW_MANAGER_TRACE_PACING("schedule-wait-render-ready token=%u surfaces=%zu contentSerial=%llu\n",
                    frame.presentToken,
                    frame.surfaceCount,
                    static_cast<unsigned long long>(frame.contentSerial));
        return false;
      }
      if (!presenter->atomicPresenter()->canSchedulePresent(frame.presentToken)) return false;
      std::uint64_t const frameContentSerial = frame.contentSerial;
      std::uint32_t const presentId = presenter->atomicPresenter()->schedulePresent(frame.presentToken);
      if (presentId == 0) return false;
      std::uint64_t const scheduledNsec = monotonicNanoseconds();
      double const sinceLastFlipMs =
          lastAtomicFlipNsec > 0 && scheduledNsec >= lastAtomicFlipNsec
              ? static_cast<double>(scheduledNsec - lastAtomicFlipNsec) / 1'000'000.0
              : 0.0;
      double const gpuWaitMs =
          frame.timing.monotonicNsec > 0 && scheduledNsec >= frame.timing.monotonicNsec
              ? static_cast<double>(scheduledNsec - frame.timing.monotonicNsec) / 1'000'000.0
              : 0.0;
      LAMBDA_WINDOW_MANAGER_TRACE_PACING("flip-scheduled id=%u surfaces=%zu activeSizing=%zu maxBuffer=%dx%d "
                  "maxFrame=%dx%d maxDmabuf=0x%08x maxDmabufModifier=0x%016llx "
                  "snapshotFrameTime=%.3fms renderAhead=%d "
                  "contentSerial=%llu sinceLastFlip=%.3fms gpuWait=%.3fms "
                  "ageSurface=%llu inputToRender=%.3fms configureToRender=%.3fms ackToRender=%.3fms "
                  "commitToRender=%.3fms configureToCommit=%.3fms "
                  "phaseBg=%.3fms phaseSnapshot=%.3fms phaseSurface=%.3fms "
                  "phaseClosing=%.3fms phaseCursor=%.3fms "
                  "phasePresent=%.3fms phaseTotal=%.3fms\n",
                  presentId,
                  frame.surfaceCount,
                  frame.profile.activeSizingSurfaces,
                  frame.profile.maxBufferWidth,
                  frame.profile.maxBufferHeight,
                  frame.profile.maxFrameWidth,
                  frame.profile.maxFrameHeight,
                  frame.profile.maxDmabufFormat,
                  static_cast<unsigned long long>(frame.profile.maxDmabufModifier),
                  std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frame.frameTime)
                      .count(),
                  frame.renderedAhead ? 1 : 0,
                  static_cast<unsigned long long>(frame.contentSerial),
                  sinceLastFlipMs,
                  gpuWaitMs,
                  static_cast<unsigned long long>(frame.profile.maxAgeSurfaceId),
                  frame.profile.maxInputToRenderMs,
                  frame.profile.maxConfigureToRenderMs,
                  frame.profile.maxAckToRenderMs,
                  frame.profile.maxCommitToRenderMs,
                  frame.profile.maxConfigureToCommitMs,
                  frame.profile.backgroundMs,
                  frame.profile.snapshotMs,
                  frame.profile.surfaceMs,
                  frame.profile.closingMs,
                  frame.profile.cursorMs,
                  frame.profile.presentMs,
                  frame.profile.totalMs);
      if (frame.profile.totalMs > 100.0 || frame.profile.presentMs > 50.0) {
        diagnostics::crashLog("slow-frame-scheduled presentId=%u surfaces=%zu total=%.3fms present=%.3fms "
                              "surface=%.3fms maxBuffer=%dx%d maxFrame=%dx%d",
                              presentId,
                              frame.surfaceCount,
                              frame.profile.totalMs,
                              frame.profile.presentMs,
                              frame.profile.surfaceMs,
                              frame.profile.maxBufferWidth,
                              frame.profile.maxBufferHeight,
                              frame.profile.maxFrameWidth,
                              frame.profile.maxFrameHeight);
      }
      lastAtomicScheduledNsec = scheduledNsec;
      lastAtomicScheduledRenderMs = frame.renderMs;
      lastAtomicScheduledProfile = frame.profile;
      frame.timing.backendPresentId = presentId;
      frame.timing.flags |= static_cast<std::uint32_t>(WP_PRESENTATION_FEEDBACK_KIND_VSYNC |
                                                       WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK);
      lastAtomicScheduledFrameSurfaceIds = frame.frameCallbackSurfaceIds;
      wayland.sendPresentationFeedbacks(monotonicMilliseconds(),
                                        frame.timing,
                                        lastAtomicScheduledFrameSurfaceIds);
      commitScheduledSceneState(frame);
      frame = AtomicReadyFrame{};
      if (wayland.contentSerial() != frameContentSerial) atomicFrameDirty = true;
      forceRender = false;
      return true;
    };
    auto updateQueuedAtomicFrames = [&] {
      if (!presenter->atomicPresenter()) return;
      auto const traceStart = diagnostics::cpuTraceNow();
      std::uint64_t checkedFrames = 0;
      for (auto& frame : atomicReadyFrames) {
        if (frame.ready && !frame.overlayOnly && !frame.directScanout) {
          ++checkedFrames;
          (void)presenter->atomicPresenter()->updateRenderReady(frame.presentToken);
        }
      }
      diagnostics::recordCpuAtomicLoop({
          .updateReadyCalls = 1,
          .updateReadyFrames = checkedFrames,
          .updateReadyMs = diagnostics::cpuTraceElapsedMilliseconds(traceStart),
      });
    };
    auto discardQueuedAtomicFrame = [&](std::size_t index) {
      if (!presenter->atomicPresenter() || index >= atomicReadyFrames.size()) return;
      if (atomicReadyFrames[index].directScanout) {
        if (!atomicReadyFrames[index].scanoutCandidate) {
          presenter->atomicPresenter()->clearPreparedDirectScanout();
        }
      } else if (atomicReadyFrames[index].overlayOnly) {
        if (!atomicReadyFrames[index].scanoutCandidate) {
          presenter->atomicPresenter()->clearPreparedOverlayCandidate();
        }
      } else {
        presenter->atomicPresenter()->discardPreparedFrame(atomicReadyFrames[index].presentToken);
      }
      LAMBDA_WINDOW_MANAGER_TRACE_PACING("mailbox-discard token=%u surfaces=%zu contentSerial=%llu\n",
                  atomicReadyFrames[index].presentToken,
                  atomicReadyFrames[index].surfaceCount,
                  static_cast<unsigned long long>(atomicReadyFrames[index].contentSerial));
      atomicReadyFrames.erase(atomicReadyFrames.begin() + static_cast<std::ptrdiff_t>(index));
    };
    auto queuedAtomicFrameSchedulable = [&](std::size_t index) {
      if (!presenter->atomicPresenter() || index >= atomicReadyFrames.size()) return false;
      AtomicReadyFrame const& frame = atomicReadyFrames[index];
      if (!frame.ready) return false;
      bool const newest = index + 1 == atomicReadyFrames.size();
      if (frame.directScanout) {
        if (frame.scanoutCandidate) return fdReadableNow(frame.scanoutCandidate->acquireFenceFd);
        if (!newest) return false;
        if (!fdReadableNow(presenter->atomicPresenter()->preparedDirectScanoutAcquireFenceFd())) return false;
        return presenter->atomicPresenter()->canScheduleDirectScanout();
      }
      if (frame.overlayOnly) {
        if (frame.scanoutCandidate) return fdReadableNow(frame.scanoutCandidate->acquireFenceFd);
        if (!newest) return false;
        if (!fdReadableNow(presenter->atomicPresenter()->preparedOverlayAcquireFenceFd())) return false;
        return presenter->atomicPresenter()->canScheduleOverlayOnly();
      }
      bool const allowRenderFence = frame.renderedAhead && frame.profile.activeSizingSurfaces == 0;
      if (!allowRenderFence && presenter->atomicPresenter()->renderReadyFd(frame.presentToken) >= 0) {
        return false;
      }
      return presenter->atomicPresenter()->canSchedulePresent(frame.presentToken);
    };
    auto queuedAtomicScheduleCandidate = [&]() -> std::optional<std::size_t> {
      if (!presenter->atomicPresenter() || presenter->atomicPresenter()->hasPendingPageFlip() ||
          atomicReadyFrames.empty()) {
        return std::nullopt;
      }
      for (std::size_t index = atomicReadyFrames.size(); index > 0; --index) {
        std::size_t const candidate = index - 1;
        if (!queuedAtomicFrameSchedulable(candidate)) continue;
        return candidate;
      }
      return std::nullopt;
    };
    auto queuedAtomicScheduleDelayMs = [&]() -> int {
      if (!presenter->atomicPresenter() || presenter->atomicPresenter()->hasPendingPageFlip() ||
          atomicReadyFrames.empty()) {
        return kIdlePollMs;
      }
      for (std::size_t index = atomicReadyFrames.size(); index > 0; --index) {
        if (!queuedAtomicFrameSchedulable(index - 1)) continue;
        return 0;
      }
      return kIdlePollMs;
    };
    auto queuedActiveSizingCanRenderReplacement = [&]() {
      return presenter->atomicPresenter() && !presenter->atomicPresenter()->hasPendingPageFlip() &&
             !atomicReadyFrames.empty() && atomicReadyFrames.back().profile.activeSizingSurfaces > 0 &&
             queuedAtomicScheduleDelayMs() > 0;
    };
    auto scheduleQueuedAtomicFrame = [&] {
      auto const traceStart = diagnostics::cpuTraceNow();
      bool scheduled = false;
      if (!presenter->atomicPresenter() || presenter->atomicPresenter()->hasPendingPageFlip() ||
          atomicReadyFrames.empty()) {
        diagnostics::recordCpuAtomicLoop({
            .scheduleAttempts = 1,
            .scheduleSuccess = 0,
            .scheduleMs = diagnostics::cpuTraceElapsedMilliseconds(traceStart),
        });
        return false;
      }
      updateQueuedAtomicFrames();
      std::optional<std::size_t> candidate = queuedAtomicScheduleCandidate();
      if (!candidate) {
        diagnostics::recordCpuAtomicLoop({
            .scheduleAttempts = 1,
            .scheduleSuccess = 0,
            .scheduleMs = diagnostics::cpuTraceElapsedMilliseconds(traceStart),
        });
        return false;
      }
      std::size_t candidateIndex = *candidate;
      while (candidateIndex > 0 && !atomicReadyFrames.empty()) {
        discardQueuedAtomicFrame(0);
        --candidateIndex;
      }
      bool const candidateDirect = !atomicReadyFrames.empty() && atomicReadyFrames.front().directScanout;
      bool const candidateOverlay = !atomicReadyFrames.empty() && atomicReadyFrames.front().overlayOnly;
      scheduled = scheduleAtomicFrame(atomicReadyFrames.front());
      if (scheduled && !atomicReadyFrames.empty() && !atomicReadyFrames.front().ready) {
        atomicReadyFrames.pop_front();
      }
      diagnostics::recordCpuAtomicLoop({
          .scheduleAttempts = 1,
          .scheduleSuccess = scheduled ? 1ull : 0ull,
          .schedulePresent = scheduled && !candidateDirect && !candidateOverlay ? 1ull : 0ull,
          .scheduleDirect = scheduled && candidateDirect ? 1ull : 0ull,
          .scheduleOverlay = scheduled && candidateOverlay ? 1ull : 0ull,
          .scheduleMs = diagnostics::cpuTraceElapsedMilliseconds(traceStart),
      });
      return scheduled;
    };
    auto queuedRenderReadyFd = [&]() -> int {
      if (!presenter->atomicPresenter()) return -1;
      for (auto frame = atomicReadyFrames.rbegin(); frame != atomicReadyFrames.rend(); ++frame) {
        if (frame->overlayOnly || frame->directScanout) continue;
        int const fd = presenter->atomicPresenter()->renderReadyFd(frame->presentToken);
        if (fd >= 0) return fd;
      }
      return -1;
    };
    auto queuedActiveSizingSurfaces = [&]() -> std::size_t {
      return atomicReadyFrames.empty() ? 0u : atomicReadyFrames.back().profile.activeSizingSurfaces;
    };
    auto queuedContentSerial = [&]() -> std::uint64_t {
      return atomicReadyFrames.empty() ? 0ull : atomicReadyFrames.back().contentSerial;
    };
    auto discardAllQueuedAtomicFrames = [&] {
      if (presenter->atomicPresenter()) {
        for (auto const& frame : atomicReadyFrames) {
          if (frame.directScanout) {
            if (!frame.scanoutCandidate) {
              presenter->atomicPresenter()->clearPreparedDirectScanout();
            }
          } else if (frame.overlayOnly) {
            if (!frame.scanoutCandidate) {
              presenter->atomicPresenter()->clearPreparedOverlayCandidate();
            }
          } else {
            presenter->atomicPresenter()->discardPreparedFrame(frame.presentToken);
          }
        }
      }
      atomicReadyFrames.clear();
	      atomicRenderedFrame = AtomicReadyFrame{};
    };
    auto dispatchAtomicPageFlip = [&] {
      auto const traceStart = diagnostics::cpuTraceNow();
      if (!presenter->atomicPresenter()) return false;
      auto flip = presenter->atomicPresenter()->dispatchPageFlipEvents();
      if (!flip) {
        diagnostics::recordCpuAtomicLoop({
            .dispatchFlipCalls = 1,
            .dispatchFlipCompletions = 0,
            .dispatchFlipMs = diagnostics::cpuTraceElapsedMilliseconds(traceStart),
        });
        return false;
      }
      std::uint64_t const completionNsec = flip->monotonicNsec != 0 ? flip->monotonicNsec : monotonicNanoseconds();
      std::uint64_t const intervalNsec = lastAtomicFlipNsec > 0 && completionNsec >= lastAtomicFlipNsec
                                             ? completionNsec - lastAtomicFlipNsec
                                             : 0;
      std::uint64_t const queueNsec =
          flip->scheduledMonotonicNsec > 0 && completionNsec >= flip->scheduledMonotonicNsec
              ? completionNsec - flip->scheduledMonotonicNsec
              : 0;
      std::uint64_t const expectedNsec = refreshNsec(output.refreshRateMilliHz());
      double const intervalErrorMs = intervalNsec > 0 && expectedNsec > 0
                                         ? (static_cast<double>(intervalNsec) -
                                            static_cast<double>(expectedNsec)) /
                                               1'000'000.0
                                         : 0.0;
      double const completedRenderMs = lastAtomicScheduledRenderMs;
      presentation::AtomicFrameProfile const completedProfile = lastAtomicScheduledProfile;
      std::uint64_t const completedScheduledNsec = lastAtomicScheduledNsec;
      lastAtomicFlipNsec = completionNsec;
      PresentationCompletion completion{
          .backendPresentId = flip->presentId,
          .monotonicNsec = completionNsec,
          .sequence = flip->sequence,
          .flags = flip->hardware
	                       ? static_cast<std::uint32_t>(WP_PRESENTATION_FEEDBACK_KIND_VSYNC |
	                                                    WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK)
	                       : 0u,
	      };
	      wayland.setRetainedDmabufBufferIds(presenter->atomicPresenter()->overlayBufferIdsInUse());
	      wayland.completePresentationFeedbacks({completion}, monotonicMilliseconds());
	      wayland.sendFrameCallbacksOnly(monotonicMilliseconds(), lastAtomicScheduledFrameSurfaceIds);
	      lastAtomicScheduledFrameSurfaceIds.clear();
      double const renderSubmitToFlipMs =
          flip->renderSubmittedMonotonicNsec > 0 && completionNsec >= flip->renderSubmittedMonotonicNsec
              ? static_cast<double>(completionNsec - flip->renderSubmittedMonotonicNsec) / 1'000'000.0
              : 0.0;
      double const commitToFlipMs =
          completedProfile.maxAgeSurfaceId != 0 ? completedProfile.maxCommitToRenderMs + renderSubmitToFlipMs : 0.0;
      double const inputToFlipMs =
          completedProfile.maxAgeSurfaceId != 0 ? completedProfile.maxInputToRenderMs + renderSubmitToFlipMs : 0.0;
      double const configureToFlipMs =
          completedProfile.maxAgeSurfaceId != 0 ? completedProfile.maxConfigureToRenderMs + renderSubmitToFlipMs
                                                : 0.0;
      double const scheduledToCommitStartMs =
          flip->scheduledMonotonicNsec > 0 && flip->commitStartMonotonicNsec >= flip->scheduledMonotonicNsec
              ? static_cast<double>(flip->commitStartMonotonicNsec - flip->scheduledMonotonicNsec) / 1'000'000.0
              : 0.0;
      double const commitReturnToFlipMs =
          flip->commitReturnMonotonicNsec > 0 && completionNsec >= flip->commitReturnMonotonicNsec
              ? static_cast<double>(completionNsec - flip->commitReturnMonotonicNsec) / 1'000'000.0
              : 0.0;
      double const flipEventDispatchDelayMs =
          flip->eventDispatchStartMonotonicNsec > 0 && flip->eventDispatchStartMonotonicNsec >= completionNsec
              ? static_cast<double>(flip->eventDispatchStartMonotonicNsec - completionNsec) / 1'000'000.0
              : 0.0;
      double const flipEventHandleMs =
          flip->eventDispatchEndMonotonicNsec > 0 &&
                  flip->eventDispatchEndMonotonicNsec >= flip->eventDispatchStartMonotonicNsec
              ? static_cast<double>(flip->eventDispatchEndMonotonicNsec - flip->eventDispatchStartMonotonicNsec) /
                    1'000'000.0
              : 0.0;
      std::uint64_t const renderToReadyNsec =
          flip->renderSubmittedMonotonicNsec > 0 &&
                  flip->renderReadyMonotonicNsec >= flip->renderSubmittedMonotonicNsec
              ? flip->renderReadyMonotonicNsec - flip->renderSubmittedMonotonicNsec
              : 0;
      std::uint64_t renderAheadWorkNsec = renderToReadyNsec;
      if (renderAheadWorkNsec == 0 && completedProfile.totalMs > 0.0) {
        renderAheadWorkNsec = static_cast<std::uint64_t>(completedProfile.totalMs * 1'000'000.0);
      }
      updateAtomicRenderAheadLead(renderAheadWorkNsec, flip->usedRenderFence);
      LAMBDA_WINDOW_MANAGER_TRACE_PACING("flip-complete id=%u hw=%d seq=%llu interval=%.3fms expected=%.3fms error=%+.3fms "
                  "queue=%.3fms render=%.3fms renderToReady=%.3fms readyToCommit=%.3fms "
                  "commit=%.3fms scheduledToCommit=%.3fms commitReturnToFlip=%.3fms "
                  "eventDelay=%.3fms eventHandle=%.3fms scheduledDelta=%.3fms "
                  "renderFence=%d modeset=%d ageSurface=%llu commitToFlip=%.3fms "
                  "inputToFlip=%.3fms configureToFlip=%.3fms\n",
                  flip->presentId,
                  flip->hardware ? 1 : 0,
                  static_cast<unsigned long long>(flip->sequence),
                  static_cast<double>(intervalNsec) / 1'000'000.0,
                  static_cast<double>(expectedNsec) / 1'000'000.0,
                  intervalErrorMs,
                  static_cast<double>(queueNsec) / 1'000'000.0,
                  completedRenderMs,
                  static_cast<double>(renderToReadyNsec) / 1'000'000.0,
                  flip->renderReadyMonotonicNsec > 0 &&
                          flip->scheduledMonotonicNsec >= flip->renderReadyMonotonicNsec
                      ? static_cast<double>(flip->scheduledMonotonicNsec - flip->renderReadyMonotonicNsec) /
                            1'000'000.0
                      : 0.0,
                  static_cast<double>(flip->commitDurationNsec) / 1'000'000.0,
                  scheduledToCommitStartMs,
                  commitReturnToFlipMs,
                  flipEventDispatchDelayMs,
                  flipEventHandleMs,
                  completedScheduledNsec > 0 && flip->scheduledMonotonicNsec >= completedScheduledNsec
                      ? static_cast<double>(flip->scheduledMonotonicNsec - completedScheduledNsec) / 1'000'000.0
                      : 0.0,
                  flip->usedRenderFence ? 1 : 0,
                  flip->usedModeset ? 1 : 0,
                  static_cast<unsigned long long>(completedProfile.maxAgeSurfaceId),
                  commitToFlipMs,
                  inputToFlipMs,
                  configureToFlipMs);
      if (flip->usedModeset && (queueNsec > 100'000'000ull || flip->commitDurationNsec > 50'000'000ull)) {
        std::fprintf(stderr,
                     "lambda-window-manager: slow KMS modeset id=%u queue=%.3fms commit=%.3fms\n",
                     flip->presentId,
                     static_cast<double>(queueNsec) / 1'000'000.0,
                     static_cast<double>(flip->commitDurationNsec) / 1'000'000.0);
      }
      if (queueNsec > 100'000'000ull || flip->commitDurationNsec > 50'000'000ull ||
          flipEventDispatchDelayMs > 100.0) {
        diagnostics::crashLog("slow-flip-complete id=%u seq=%llu queue=%.3fms commit=%.3fms "
                              "eventDelay=%.3fms interval=%.3fms modeset=%d",
                              flip->presentId,
                              static_cast<unsigned long long>(flip->sequence),
                              static_cast<double>(queueNsec) / 1'000'000.0,
                              static_cast<double>(flip->commitDurationNsec) / 1'000'000.0,
                              flipEventDispatchDelayMs,
                              static_cast<double>(intervalNsec) / 1'000'000.0,
                              flip->usedModeset ? 1 : 0);
      }
      diagnostics::recordCpuAtomicLoop({
          .dispatchFlipCalls = 1,
          .dispatchFlipCompletions = 1,
          .dispatchFlipMs = diagnostics::cpuTraceElapsedMilliseconds(traceStart),
      });
      return true;
    };
    LoopSurfaceSnapshotCache loopSnapshots;
    CompositorRenderFrameContext renderFrameCtx{
        .wayland = wayland,
        .output = output,
        .presenter = *presenter,
        .canvas = *canvas,
        .textSystem = textSystem,
        .appliedConfig = appliedConfig,
        .surfaceRenderState = surfaceRenderState,
        .cursorState = cursorState,
        .frameProfile = frameProfile,
        .loopStats = loopStats,
        .idleBlanked = idleBlanked,
        .hardwareCursorAvailable = hardwareCursorAvailable,
        .detailedFrameProfile = detailedFrameProfile,
        .atomicReadyFrame = &atomicRenderedFrame,
        .atomicFrameDirty = &atomicFrameDirty,
        .lastKnownContentSerial = &lastKnownContentSerial,
        .vulkanDisplayTimingSupportLogged = displayTimingSupportLogged,
        .useVulkanPresentationCompletion = useVulkanPresentationCompletion,
    };
    auto renderCompositorFrame = [&](std::chrono::steady_clock::time_point frameTime,
                                     LoopInstrumentation::Clock::time_point renderStart,
                                     PresentationTiming presentationTiming,
                                     bool renderAheadFrame) {
      processCompletedFrameCapture();
      bool const frameCaptureInFlight = screenshotCaptureInFlight.has_value() || snapCaptureInFlight;
      bool snapTraceThisFrame = snapAnimationTrace.wantsFrame(wayland);
      bool snapCaptureThisFrame = !frameCaptureInFlight && snapFrameCapture.wantsFrame(wayland);
      bool const screenshotCaptureThisFrame = !frameCaptureInFlight && screenshotPending.has_value();
      bool frameCapturePending = screenshotCaptureThisFrame || snapCaptureThisFrame;
      if (frameCapturePending && !lambda::requestNextFrameCaptureForCanvas(canvas)) {
        if (screenshotCaptureThisFrame) {
          std::fprintf(stderr, "lambda-window-manager: screenshots are not supported by this presenter\n");
        }
        if (snapCaptureThisFrame) {
          std::fprintf(stderr, "lambda-window-manager: snap frame capture is not supported by this presenter\n");
          snapFrameCapture.enabled = false;
        }
        screenshotPending.reset();
        snapCaptureThisFrame = false;
        frameCapturePending = false;
      }
      renderFrameCtx.idleBlanked = idleBlanked;
      renderFrameCtx.hardwareCursorAvailable =
          hardwareCursorAvailable && !(screenshotPending && screenshotPending->includeCursor);
      renderFrameCtx.screenshotFlashOpacity = screenshotFlashOpacityAt(frameTime);
      renderFrameCtx.vulkanDisplayTimingSupportLogged = displayTimingSupportLogged;
      renderFrameCtx.useVulkanPresentationCompletion = useVulkanPresentationCompletion;
      std::vector<CommittedSurfaceSnapshot> const* cachedCommittedSurfaces = nullptr;
      std::optional<CommittedSurfaceSnapshot> const* cachedSoftwareCursor = nullptr;
      if (!renderFrameCtx.idleBlanked) {
        cachedCommittedSurfaces = &loopSnapshots.committedSurfaces(wayland);
        bool const softwareCursorRequested =
            !renderFrameCtx.appliedConfig.config.hardwareCursorEnabled || !renderFrameCtx.hardwareCursorAvailable;
        cachedSoftwareCursor = &loopSnapshots.cursorSurface(wayland, softwareCursorRequested);
      }
      renderFrameCtx.committedSurfaces = cachedCommittedSurfaces;
      renderFrameCtx.softwareCursorSnapshot = cachedSoftwareCursor;
      lambda::compositor::renderCompositorFrame(renderFrameCtx, frameTime, renderStart, presentationTiming, renderAheadFrame);
      if (atomicRenderedFrame.ready) {
        if ((atomicRenderedFrame.overlayOnly || atomicRenderedFrame.directScanout) && presenter->atomicPresenter()) {
          if (atomicRenderedFrame.directScanout) {
            presenter->atomicPresenter()->clearPreparedOverlayCandidate();
          } else if (atomicRenderedFrame.overlayOnly) {
            presenter->atomicPresenter()->clearPreparedDirectScanout();
          }
          for (auto const& frame : atomicReadyFrames) {
            if (!frame.overlayOnly && !frame.directScanout) {
              presenter->atomicPresenter()->discardPreparedFrame(frame.presentToken);
            }
          }
          atomicReadyFrames.clear();
        }
        atomicReadyFrames.push_back(std::move(atomicRenderedFrame));
        atomicRenderedFrame = AtomicReadyFrame{};
      }
      displayTimingSupportLogged = renderFrameCtx.vulkanDisplayTimingSupportLogged;
      useVulkanPresentationCompletion = renderFrameCtx.useVulkanPresentationCompletion;
      if (snapTraceThisFrame) {
        snapAnimationTrace.recordFrame(wayland,
                                       cachedCommittedSurfaces,
                                       atomicReadyFrames.empty() ? nullptr : &atomicReadyFrames.back(),
                                       renderAheadFrame);
      }
      if (frameCapturePending) {
        if (screenshotCaptureThisFrame) {
          screenshotCaptureInFlight = screenshotPending;
          screenshotPending.reset();
        }
        if (snapCaptureThisFrame) {
          snapCaptureInFlight = true;
        }
        frameCapturePollAttempts = 0;
        processCompletedFrameCapture();
      }
      if (screenshotFlashStartedAt && frameTime - *screenshotFlashStartedAt >= kScreenshotFlashDuration) {
        screenshotFlashStartedAt.reset();
      }
    };
    auto queueScreenshotIfRequested = [&] {
      auto request = wayland.consumeScreenshotRequest();
      if (!request) {
        return;
      }
      screenshotPending = *request;
      forceRender = true;
      skipNextVblank = true;
      if (idleBlanked) {
        idleBlanked = false;
      }
    };
    auto noteContentSerialChange = [&] {
      std::uint64_t const contentSerial = wayland.contentSerial();
      if (contentSerial == lastKnownContentSerial) return false;
      loopSnapshots.reset();
      atomicFrameDirty = true;
      lastKnownContentSerial = contentSerial;
      return true;
    };
    auto acknowledgeVtAcquireAfterFrame = [&] {
      if (!vtAcquireFramePending) return;
      vtAcquireFramePending = false;
      device->acknowledgeVtAcquire();
    };
    auto renderAtomicFrame = [&](bool renderAheadFrame) {
      auto const traceStart = diagnostics::cpuTraceNow();
      PresentationTiming presentationTiming{
          .monotonicNsec = monotonicNanoseconds(),
          .sequence = softwarePresentationSequence,
          .refreshNsec = refreshNsec(output.refreshRateMilliHz()),
          .flags = 0u,
      };
      auto const frameTime = std::chrono::steady_clock::now();
      wayland.updateAnimations(monotonicMilliseconds(), appliedConfig.config.animationsEnabled);
      auto timingStart = LoopInstrumentation::Clock::now();
      renderCompositorFrame(frameTime, timingStart, presentationTiming, renderAheadFrame);
      acknowledgeVtAcquireAfterFrame();
      forceRender = false;
      diagnostics::recordCpuAtomicLoop({
          .renderCalls = 1,
          .renderAheadCalls = renderAheadFrame ? 1ull : 0ull,
          .renderCallMs = diagnostics::cpuTraceElapsedMilliseconds(traceStart),
      });
    };
    auto tryClearAtomicDirtyForEmptyDamage = [&](bool animationFrameNeededNow,
                                                 bool screenshotFlashFrameNeededNow,
                                                 bool snapPreviewFrameNeededNow,
                                                 bool windowCyclerFrameNeededNow,
                                                 bool inputRenderRequiredNow,
                                                 bool configReloadedNow) {
      if (!presenter->atomicPresenter() || !atomicFrameDirty) return false;
      if (presenter->atomicPresenter()->hasPendingPageFlip() || !atomicReadyFrames.empty()) return false;
      if (forceRender || animationFrameNeededNow || screenshotFlashFrameNeededNow ||
          snapPreviewFrameNeededNow || windowCyclerFrameNeededNow ||
          inputRenderRequiredNow || configReloadedNow || wayland.hasPendingFrameCallbacks()) {
        return false;
      }

      auto const traceStart = diagnostics::cpuTraceNow();
      bool const softwareCursorRequested = !appliedConfig.config.hardwareCursorEnabled || !hardwareCursorAvailable;
      auto const& softwareCursorSnapshot = loopSnapshots.cursorSurface(wayland, softwareCursorRequested);
      auto const& committedSurfaces = loopSnapshots.committedSurfaces(wayland);
      CompositorSceneFramePlan scenePlan =
          buildCompositorSceneFrame(surfaceRenderState.sceneGraph,
                                    CompositorSceneFrameInput{
                                        .output = &output,
                                        .atomicPresenter = presenter->atomicPresenter(),
                                        .duplicateDmabufFds = {},
                                        .chrome = appliedConfig.config.chrome,
                                        .surfaceVisuals = surfaceRenderState.surfaceVisuals,
                                        .surfaces = committedSurfaces,
                                        .softwareCursor = softwareCursorSnapshot,
                                        .frameTime = SteadyClock::now(),
                                        .logicalOutputWidth = wayland.logicalOutputWidth(),
                                        .logicalOutputHeight = wayland.logicalOutputHeight(),
                                        .dpiScale = canvas ? canvas->dpiScale() : 1.f,
                                        .animationsEnabled = appliedConfig.config.animationsEnabled,
                                        .forceFullDamage = false,
                                        .selectScanout = false,
                                    });
      if (!scenePlan.damage.empty()) {
        diagnostics::recordCpuAtomicLoop({
            .emptyDamageChecks = 1,
            .emptyDamageSkips = 0,
            .emptyDamageMs = diagnostics::cpuTraceElapsedMilliseconds(traceStart),
        });
        return false;
      }

      surfaceRenderState.sceneGraph = std::move(scenePlan.nextState);
      atomicFrameDirty = false;
      lastKnownContentSerial = wayland.contentSerial();
      LAMBDA_WINDOW_MANAGER_TRACE_PACING("scene-damage-skip empty=1 surfaces=%zu contentSerial=%llu\n",
                    committedSurfaces.size(),
                    static_cast<unsigned long long>(lastKnownContentSerial));
      diagnostics::recordCpuAtomicLoop({
          .emptyDamageChecks = 1,
          .emptyDamageSkips = 1,
          .emptyDamageMs = diagnostics::cpuTraceElapsedMilliseconds(traceStart),
      });
      return true;
    };

    while (running.load(std::memory_order_relaxed) && !device->shouldTerminate()) {
      loopSnapshots.reset();
      ++loopStats.loops;
      diagnostics::recordCpuLoop();
      wayland.dispatchShellIpc();
      loopSnapshots.reset();
      auto const animationCheckTime = std::chrono::steady_clock::now();
      bool const animationFrameNeeded =
          wayland.hasActiveAnimations() ||
          hasActiveSurfaceAnimations(surfaceRenderState,
                                     animationCheckTime,
                                     appliedConfig.config.animationsEnabled);
      bool const screenshotFlashFrameNeededBeforePoll = screenshotFlashStartedAt.has_value();
      if (presenter->atomicPresenter() && !atomicReadyFrames.empty()) {
        updateQueuedAtomicFrames();
      }
      std::array<int, 8> eventFdStorage{};
      std::span<int const> const eventFds = pollFds(eventFdStorage);
      bool const atomicPageFlipPendingBeforePoll =
          presenter->atomicPresenter() && presenter->atomicPresenter()->hasPendingPageFlip();
      bool const queuedAtomicCanScheduleBeforePoll = queuedAtomicScheduleCandidate().has_value();
      bool const atomicFrameBlockedBeforePoll =
          presenter->atomicPresenter() && (atomicPageFlipPendingBeforePoll ||
                                           (!atomicReadyFrames.empty() && !queuedAtomicCanScheduleBeforePoll));
      bool const renderAheadNeededBeforePoll =
          diagnosticRenderAheadAllowed() &&
          presenter->atomicPresenter() && atomicFrameDirty && atomicPageFlipPendingBeforePoll &&
          atomicRenderAheadDue();
      bool const atomicDirtyCanRenderBeforePoll =
          presenter->atomicPresenter() && atomicFrameDirty &&
          (atomicReadyFrames.empty() || queuedActiveSizingCanRenderReplacement()) &&
          !atomicPageFlipPendingBeforePoll && atomicCanPrepareFrame();
      std::optional<int> const snapPreviewDelayBeforePoll = wayland.snapPreviewWakeDelayMs();
      bool const snapPreviewFrameNeededBeforePoll =
          snapPreviewDelayBeforePoll && *snapPreviewDelayBeforePoll <= 0;
      bool const snapPreviewCanRenderBeforePoll =
          snapPreviewFrameNeededBeforePoll && !atomicFrameBlockedBeforePoll;
      std::optional<int> const windowCyclerDelayBeforePoll = wayland.windowCyclerWakeDelayMs();
      bool const windowCyclerFrameNeededBeforePoll =
          windowCyclerDelayBeforePoll && *windowCyclerDelayBeforePoll <= 0;
      bool const windowCyclerCanRenderBeforePoll =
          windowCyclerFrameNeededBeforePoll && !atomicFrameBlockedBeforePoll;
      bool const animationCanRenderBeforePoll =
          (animationFrameNeeded || screenshotFlashFrameNeededBeforePoll || snapPreviewCanRenderBeforePoll ||
           windowCyclerCanRenderBeforePoll || atomicDirtyCanRenderBeforePoll) &&
          !atomicFrameBlockedBeforePoll;
      int const renderAheadDelayBeforePoll =
          diagnosticRenderAheadAllowed() ? atomicRenderAheadDelayMs() : kIdlePollMs;
      int const queuedScheduleDelayBeforePoll = queuedAtomicScheduleDelayMs();
      int const acquireWaitCallbackDelayBeforePoll = acquireWaitFrameCallbackDelayMs();
      int pollTimeoutMs = forceRender || animationCanRenderBeforePoll || renderAheadNeededBeforePoll
                              ? 0
                              : std::min(kIdlePollMs, renderAheadDelayBeforePoll);
      pollTimeoutMs = std::min(pollTimeoutMs, queuedScheduleDelayBeforePoll);
      pollTimeoutMs = std::min(pollTimeoutMs, acquireWaitCallbackDelayBeforePoll);
      pollTimeoutMs = std::min(pollTimeoutMs, diagnosticExerciseDelayMs());
      if (snapPreviewDelayBeforePoll) {
        int const snapPreviewDelayMs = std::max(0, *snapPreviewDelayBeforePoll);
        if (snapPreviewDelayMs > 0 || snapPreviewCanRenderBeforePoll) {
          pollTimeoutMs = std::min(pollTimeoutMs, snapPreviewDelayMs);
        }
      }
      if (windowCyclerDelayBeforePoll) {
        int const windowCyclerDelayMs = std::max(0, *windowCyclerDelayBeforePoll);
        if (windowCyclerDelayMs > 0 || windowCyclerCanRenderBeforePoll) {
          pollTimeoutMs = std::min(pollTimeoutMs, windowCyclerDelayMs);
        }
      }
      auto timingStart = LoopInstrumentation::Clock::now();
      auto const pollResult = device->pollEventDetails(pollTimeoutMs, eventFds);
      bool const pollWoke = pollResult.woke;
      loopStats.recordPoll(timingStart, pollWoke, pollTimeoutMs);
      bool const waylandFdPolled = wayland.eventFd() >= 0;
      bool const waylandWoke = waylandFdPolled && pollMaskHas(pollResult.extraReadableMask, 0);
      std::size_t const shellFdIndex = waylandFdPolled ? 1u : 0u;
      bool const shellFdPolled = wayland.shellIpcFd() >= 0;
      bool const shellWoke = shellFdPolled && pollMaskHas(pollResult.extraReadableMask, shellFdIndex);
      std::size_t const pageFlipFdIndex = shellFdIndex + (shellFdPolled ? 1u : 0u);
      bool const pageFlipFdPolled = presenter->atomicPresenter() && presenter->atomicPresenter()->hasPendingPageFlip();
      bool const pageFlipWoke = pageFlipFdPolled && pollMaskHas(pollResult.extraReadableMask, pageFlipFdIndex);
      std::size_t const renderReadyFdIndex = pageFlipFdIndex + (pageFlipFdPolled ? 1u : 0u);
      bool const renderReadyFdPolled =
          presenter->atomicPresenter() && queuedRenderReadyFd() >= 0;
      bool const renderReadyWoke =
          renderReadyFdPolled && pollMaskHas(pollResult.extraReadableMask, renderReadyFdIndex);
      bool renderReadyUpdated =
          renderReadyWoke && presenter->atomicPresenter();
      if (renderReadyUpdated) updateQueuedAtomicFrames();
      bool const pageFlipCompleted = dispatchAtomicPageFlip();
      if (pageFlipCompleted && presenter->atomicPresenter() && !atomicReadyFrames.empty() &&
          !presenter->atomicPresenter()->hasPendingPageFlip() && device->isVtForeground()) {
        updateQueuedAtomicFrames();
        bool const scheduledAfterFlip = queuedAtomicScheduleCandidate().has_value() && scheduleQueuedAtomicFrame();
        LAMBDA_WINDOW_MANAGER_TRACE_PACING("post-flip-schedule scheduled=%d ready=%d pendingFlip=%d dirty=%d "
                    "contentSerial=%llu queuedSerial=%llu\n",
                    scheduledAfterFlip ? 1 : 0,
                    atomicReadyFrames.empty() ? 0 : 1,
                    presenter->atomicPresenter()->hasPendingPageFlip() ? 1 : 0,
                    atomicFrameDirty ? 1 : 0,
                    static_cast<unsigned long long>(wayland.contentSerial()),
                    static_cast<unsigned long long>(queuedContentSerial()));
        if (scheduledAfterFlip && atomicFrameDirty && presenter->atomicPresenter()->hasPendingPageFlip() &&
            diagnosticRenderAheadAllowed() && atomicRenderAheadDue()) {
          renderAtomicFrame(true);
        }
      }
      if (pollWoke) {
        diagnostics::recordCpuWakeSources(pollResult.inputOrSystem,
                                          waylandWoke || shellWoke,
                                          pageFlipWoke,
                                          renderReadyWoke);
      }
      bool const acquireWaitFrameCallbackSent = maybeSendAcquireWaitFrameCallback();
      if (shellWoke) {
        wayland.dispatchShellIpc();
        loopSnapshots.reset();
      }
      if (waylandWoke) {
        timingStart = LoopInstrumentation::Clock::now();
        wayland.dispatch();
        loopSnapshots.reset();
        loopStats.recordDispatch(timingStart);
        bool const contentChanged = noteContentSerialChange();
        diagnostics::recordWaylandDispatch(contentChanged);
      }
      wayland.dispatchShellIpc();
      loopSnapshots.reset();
      queueScreenshotIfRequested();
      maybeCrashHeartbeat("main-loop");
      bool const hadInputActivity = inputActivityThisLoop;
      bool const inputRenderRequired = inputRenderRequiredThisLoop;
      if (hadInputActivity) {
        lastInputActivity = SteadyClock::now();
        inputActivityThisLoop = false;
        inputRenderRequiredThisLoop = false;
      }
      ++loopStats.configChecks;
      bool configReloaded = false;
      auto const nowForConfig = SteadyClock::now();
      bool const shouldCheckConfig = nowForConfig >= nextConfigCheckAt;
      if (shouldCheckConfig) {
        nextConfigCheckAt = nowForConfig + std::chrono::milliseconds(500);
      }
      if (shouldCheckConfig && maybeReloadCompositorConfig(configWatch)) {
        configReloaded = true;
        applyDiagnosticFloorChrome(wayland, appliedConfig);
      }
      bool const diagnosticExerciseChanged = runDiagnosticExercise();
      if (pollWallpaperPreview(configWatch)) {
        forceRender = true;
      }
      if (pollWallpaperLoad(configWatch)) {
        configReloaded = true;
        forceRender = true;
      } else if (configWatch.appliedConfig.wallpaperPreviewRevealStart ||
                 configWatch.appliedConfig.wallpaperRevealStart) {
        forceRender = true;
      }
      bool const vtForeground = device->isVtForeground();
      bool const wasForegroundBeforeCheck = wasVtForeground;
      if (vtForeground && !wasForegroundBeforeCheck) {
        diagnostics::crashLog("vt-state foreground=1");
        discardAllQueuedAtomicFrames();
        atomicFrameDirty = true;
        handleVtResume();
      }
      wasVtForeground = vtForeground;
      if (!vtForeground) {
        if (wasForegroundBeforeCheck) diagnostics::crashLog("vt-state foreground=0");
        ++loopStats.vtSleeps;
        std::array<int, 8> vtEventFdStorage{};
        std::span<int const> const vtEventFds = pollFds(vtEventFdStorage);
        timingStart = LoopInstrumentation::Clock::now();
        bool const vtPollWoke = device->pollEvents(kIdlePollMs, vtEventFds);
        loopStats.recordPoll(timingStart, vtPollWoke, kIdlePollMs);
        dispatchAtomicPageFlip();
        timingStart = LoopInstrumentation::Clock::now();
        wayland.dispatch();
        wayland.dispatchShellIpc();
        loopSnapshots.reset();
        loopStats.recordDispatch(timingStart);
        bool const contentChanged = noteContentSerialChange();
        diagnostics::recordWaylandDispatch(contentChanged);
        if (inputActivityThisLoop) {
          lastInputActivity = SteadyClock::now();
          inputActivityThisLoop = false;
          inputRenderRequiredThisLoop = false;
        }
        if (!device->isVtForeground()) {
          loopStats.maybeLog();
          continue;
        }
        discardAllQueuedAtomicFrames();
        handleVtResume();
        wasVtForeground = true;
      }

      bool const shouldIdleBlank =
          appliedConfig.config.idleBlankTimeoutSeconds > 0 && !wayland.hasIdleInhibitors() &&
          std::chrono::duration_cast<std::chrono::seconds>(SteadyClock::now() - lastInputActivity).count() >=
              appliedConfig.config.idleBlankTimeoutSeconds;
      if (shouldIdleBlank != idleBlanked) {
        idleBlanked = shouldIdleBlank;
        forceRender = true;
      }

      if (!renderReadyUpdated && presenter->atomicPresenter() && !atomicReadyFrames.empty()) {
        updateQueuedAtomicFrames();
        renderReadyUpdated = true;
      }

      bool const genericRenderWake =
          !presenter->atomicPresenter() &&
          (pollResult.inputOrSystem || waylandWoke || (pollWoke && (!pageFlipWoke || !pageFlipCompleted)));
      std::optional<int> const snapPreviewDelay = wayland.snapPreviewWakeDelayMs();
      bool const snapPreviewFrameNeeded = snapPreviewDelay && *snapPreviewDelay <= 0;
      std::optional<int> const windowCyclerDelay = wayland.windowCyclerWakeDelayMs();
      bool const windowCyclerFrameNeeded = windowCyclerDelay && *windowCyclerDelay <= 0;
      bool const screenshotFlashFrameNeeded = screenshotFlashStartedAt.has_value();
      if (forceRender || pollResult.inputOrSystem || waylandWoke || inputRenderRequired || configReloaded ||
          animationFrameNeeded || screenshotFlashFrameNeeded || snapPreviewFrameNeeded || windowCyclerFrameNeeded ||
          diagnosticExerciseChanged ||
          acquireWaitFrameCallbackSent) {
        if (!presenter->atomicPresenter() || forceRender || waylandWoke || inputRenderRequired ||
            configReloaded ||
            animationFrameNeeded || screenshotFlashFrameNeeded || snapPreviewFrameNeeded || windowCyclerFrameNeeded ||
            diagnosticExerciseChanged) {
          atomicFrameDirty = true;
        }
      }
      bool const emptyDamageSkipped =
          tryClearAtomicDirtyForEmptyDamage(animationFrameNeeded,
	                                            screenshotFlashFrameNeeded,
	                                            snapPreviewFrameNeeded,
	                                            windowCyclerFrameNeeded,
	                                            inputRenderRequired,
	                                            configReloaded);
      bool const atomicPageFlipPending = presenter->atomicPresenter() && presenter->atomicPresenter()->hasPendingPageFlip();
      bool const queuedAtomicCanSchedule = queuedAtomicScheduleCandidate().has_value();
      bool const atomicFrameBlocked =
          presenter->atomicPresenter() && (atomicPageFlipPending ||
                                           (!atomicReadyFrames.empty() && !queuedAtomicCanSchedule));
      bool const renderAheadNeeded =
          diagnosticRenderAheadAllowed() &&
          presenter->atomicPresenter() && atomicFrameDirty && atomicPageFlipPending &&
          atomicRenderAheadDue();
      bool const atomicDirtyCanRender =
          presenter->atomicPresenter() && atomicFrameDirty &&
          (atomicReadyFrames.empty() || queuedActiveSizingCanRenderReplacement()) &&
          !atomicPageFlipPending && atomicCanPrepareFrame();
      bool const animationCanRenderNow = animationFrameNeeded && !atomicFrameBlocked;
      bool const screenshotFlashCanRenderNow = screenshotFlashFrameNeeded && !atomicFrameBlocked;
      bool const snapPreviewCanRenderNow = snapPreviewFrameNeeded && !atomicFrameBlocked;
      bool const windowCyclerCanRenderNow = windowCyclerFrameNeeded && !atomicFrameBlocked;
      bool const renderNeeded =
          forceRender || animationCanRenderNow || screenshotFlashCanRenderNow || snapPreviewCanRenderNow ||
          windowCyclerCanRenderNow || renderAheadNeeded || genericRenderWake || inputRenderRequired || configReloaded ||
          atomicDirtyCanRender;
      if (pollWoke || renderNeeded) {
        LAMBDA_WINDOW_MANAGER_TRACE_PACING("loop woke=%d system=%d extra=0x%llx waylandWake=%d pageFlipWake=%d "
                    "pageFlipDone=%d input=%d nonFlipWake=%d force=%d anim=%d config=%d render=%d "
                    "ready=%d pendingFlip=%d renderReadyFd=%d renderReadyWake=%d renderAheadNeed=%d "
                    "renderAheadDelay=%d queuedDelay=%d renderAheadLead=%.3fms dirty=%d activeSizing=%zu "
                    "contentSerial=%llu readySerial=%llu renderReadyNow=%d\n",
                    pollWoke ? 1 : 0,
                    pollResult.inputOrSystem ? 1 : 0,
                    static_cast<unsigned long long>(pollResult.extraReadableMask),
                    waylandWoke ? 1 : 0,
                    pageFlipWoke ? 1 : 0,
                    pageFlipCompleted ? 1 : 0,
                    inputRenderRequired ? 1 : 0,
                    genericRenderWake ? 1 : 0,
                    forceRender ? 1 : 0,
                    animationFrameNeeded ? 1 : 0,
                    configReloaded ? 1 : 0,
                    renderNeeded ? 1 : 0,
                    atomicReadyFrames.empty() ? 0 : 1,
                    presenter->atomicPresenter() && presenter->atomicPresenter()->hasPendingPageFlip() ? 1 : 0,
                    renderReadyFdPolled ? 1 : 0,
                    renderReadyWoke ? 1 : 0,
                    renderAheadNeeded ? 1 : 0,
                    atomicRenderAheadDelayMs(),
                    queuedAtomicScheduleDelayMs(),
                    static_cast<double>(atomicRenderAheadLeadEstimateNsec) / 1'000'000.0,
                    atomicFrameDirty ? 1 : 0,
                    queuedActiveSizingSurfaces(),
                    static_cast<unsigned long long>(wayland.contentSerial()),
                    static_cast<unsigned long long>(queuedContentSerial()),
                    renderReadyUpdated ? 1 : 0);
        if (emptyDamageSkipped) {
          LAMBDA_WINDOW_MANAGER_TRACE_PACING("loop-empty-damage-skip contentSerial=%llu queuedSerial=%llu\n",
                      static_cast<unsigned long long>(wayland.contentSerial()),
                      static_cast<unsigned long long>(queuedContentSerial()));
        }
      }
      diagnostics::recordCpuLoopDecision({
          .pollTimeoutZero = pollTimeoutMs == 0,
          .forceRender = forceRender,
          .animationFrameNeeded = animationFrameNeeded,
          .snapPreviewFrameNeeded = snapPreviewFrameNeeded,
          .renderAheadNeeded = renderAheadNeeded,
          .atomicFrameDirty = atomicFrameDirty,
          .atomicFrameBlocked = atomicFrameBlocked,
          .atomicReadyFrame = !atomicReadyFrames.empty(),
          .atomicPageFlipPending = atomicPageFlipPending,
          .renderNeeded = renderNeeded,
          .genericRenderWake = genericRenderWake,
          .hadInputActivity = inputRenderRequired,
          .configReloaded = configReloaded,
          .pollInputOrSystem = pollResult.inputOrSystem,
          .waylandWoke = waylandWoke,
      });
      if (presenter->atomicPresenter()) {
        if (!atomicReadyFrames.empty() && !presenter->atomicPresenter()->hasPendingPageFlip()) {
          bool const canScheduleQueuedFrame = queuedAtomicScheduleCandidate().has_value();
          if (device->isVtForeground() && canScheduleQueuedFrame && scheduleQueuedAtomicFrame()) {
            if (atomicFrameDirty && presenter->atomicPresenter()->hasPendingPageFlip() &&
                diagnosticRenderAheadAllowed() && atomicRenderAheadDue()) {
              renderAtomicFrame(true);
            }
          } else if (!canScheduleQueuedFrame && atomicFrameDirty && queuedActiveSizingCanRenderReplacement() &&
                     device->isVtForeground() && atomicCanPrepareFrame()) {
            renderAtomicFrame(false);
          }
          loopStats.maybeLog();
          continue;
        }

        if (presenter->atomicPresenter()->hasPendingPageFlip()) {
          if (atomicFrameDirty && device->isVtForeground() &&
              diagnosticRenderAheadAllowed() && atomicRenderAheadDue()) {
            renderAtomicFrame(true);
          }
          loopStats.maybeLog();
          continue;
        }

        if (atomicFrameDirty && device->isVtForeground()) {
          renderAtomicFrame(false);
          if (!atomicReadyFrames.empty() && !presenter->atomicPresenter()->hasPendingPageFlip()) {
            scheduleQueuedAtomicFrame();
          }
          loopStats.maybeLog();
          continue;
        }

        ++loopStats.idleSkips;
        diagnostics::recordCpuIdleSkip();
        loopStats.maybeLog();
        continue;
      }

      if (!renderNeeded) {
        ++loopStats.idleSkips;
        diagnostics::recordCpuIdleSkip();
        loopStats.maybeLog();
        continue;
      }

      timingStart = LoopInstrumentation::Clock::now();
      PresentationTiming presentationTiming{};
      if (presenter->atomicPresenter()) {
        presentationTiming = PresentationTiming{
            .monotonicNsec = monotonicNanoseconds(),
            .sequence = softwarePresentationSequence,
            .refreshNsec = refreshNsec(output.refreshRateMilliHz()),
            .flags = 0u,
        };
      } else if (skipNextVblank) {
        skipNextVblank = false;
        presentationTiming = PresentationTiming{
            .monotonicNsec = monotonicNanoseconds(),
            .sequence = ++softwarePresentationSequence,
            .refreshNsec = refreshNsec(output.refreshRateMilliHz()),
            .flags = 0u,
        };
      } else {
        auto const vblank = output.waitForVblank();
        if (!vblank.hardware) ++softwarePresentationSequence;
        presentationTiming =
            presentation::presentationTimingFromVblank(vblank, output.refreshRateMilliHz(), softwarePresentationSequence);
        loopStats.recordVblank(timingStart);
      }
      if (!device->isVtForeground()) continue;
      auto const frameTime = std::chrono::steady_clock::now();
      wayland.updateAnimations(monotonicMilliseconds(), appliedConfig.config.animationsEnabled);
      timingStart = LoopInstrumentation::Clock::now();
      wayland.dispatch();
      wayland.dispatchShellIpc();
      loopSnapshots.reset();
      loopStats.recordDispatch(timingStart);
      bool const contentChanged = noteContentSerialChange();
      diagnostics::recordWaylandDispatch(contentChanged);

      timingStart = LoopInstrumentation::Clock::now();
      renderCompositorFrame(frameTime, timingStart, presentationTiming, false);
      if (presenter->atomicPresenter() && !atomicReadyFrames.empty() &&
          !presenter->atomicPresenter()->hasPendingPageFlip()) {
        timingStart = LoopInstrumentation::Clock::now();
        wayland.dispatch();
        wayland.dispatchShellIpc();
        loopStats.recordDispatch(timingStart);
        scheduleQueuedAtomicFrame();
      }
      if (vtAcquireFramePending) {
        vtAcquireFramePending = false;
        device->acknowledgeVtAcquire();
      }
      forceRender = false;
      loopStats.maybeLog();
    }

    output.hideCursor();
    diagnostics::crashLog("runtime-stop shouldTerminate=%d running=%d",
                          device->shouldTerminate() ? 1 : 0,
                          running.load(std::memory_order_relaxed) ? 1 : 0);
    return 0;
  } catch (std::exception const& e) {
    std::fprintf(stderr, "lambda-window-manager: %s\n", e.what());
    diagnostics::crashLog("runtime-exception what=%s", e.what());
    return 1;
  } catch (...) {
    diagnostics::crashLog("runtime-exception unknown");
    return 1;
  }
}

} // namespace lambda::compositor
