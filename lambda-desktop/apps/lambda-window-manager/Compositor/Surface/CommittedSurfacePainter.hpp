#pragma once

#include "Compositor/Chrome/ChromeConfig.hpp"
#include "Compositor/Wayland/WaylandTypes.hpp"

#include <Lambda/Graphics/Canvas.hpp>
#include <Lambda/Graphics/Image.hpp>
#include <Lambda/Graphics/TextSystem.hpp>

#include <chrono>
#include <cstdint>

namespace lambda::compositor {

inline constexpr std::chrono::milliseconds kSurfaceOpenAnimationDuration{140};

struct SurfaceRenderSnapshot {
  std::uint64_t id = 0;
  std::uint64_t serial = 0;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::int32_t committedWidth = 0;
  std::int32_t committedHeight = 0;
  std::int32_t bufferWidth = 0;
  std::int32_t bufferHeight = 0;
  std::int32_t bufferTransform = 0;
  float sourceX = 0.f;
  float sourceY = 0.f;
  float sourceWidth = 0.f;
  float sourceHeight = 0.f;
  std::int32_t destinationWidth = 0;
  std::int32_t destinationHeight = 0;
  bool activeSizing = false;
  bool geometryAnimationGrowing = false;
};

struct SurfaceVisualState {
  std::chrono::steady_clock::time_point firstSeen{};
  SurfaceRenderSnapshot lastSnapshot{};
  bool hasLastSnapshot = false;
};

[[nodiscard]] SurfaceRenderSnapshot retainedSurfaceRenderSnapshot(CommittedSurfaceSnapshot const& surface);

[[nodiscard]] bool shouldTraceRenderSnapshot(CommittedSurfaceSnapshot const& current,
                                             SurfaceVisualState const& visual);

void drawCommittedSurfaceSnapshot(Canvas& canvas,
                                  TextSystem& textSystem,
                                  CommittedSurfaceSnapshot const& surface,
                                  SurfaceVisualState& visual,
                                  Image& clientImage,
                                  std::chrono::steady_clock::time_point frameTime,
                                  ChromeConfig const& chrome,
                                  bool animationsEnabled);

} // namespace lambda::compositor
