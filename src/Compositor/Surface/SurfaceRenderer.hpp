#pragma once

#include "Compositor/WaylandServer.hpp"

#include <Flux/Graphics/Canvas.hpp>
#include <Flux/Graphics/Image.hpp>
#include <Flux/Graphics/TextSystem.hpp>

#include <chrono>
#include <cstdint>
#include <memory>

namespace flux::compositor {

struct CachedClientImage {
  std::uint64_t id = 0;
  std::uint64_t serial = 0;
  std::shared_ptr<Image> image;
  bool logged = false;
};

struct SurfaceVisualState {
  std::chrono::steady_clock::time_point firstSeen{};
  CommittedSurfaceSnapshot lastSnapshot{};
  bool hasLastSnapshot = false;
};

struct ClosingSurfaceVisual {
  CommittedSurfaceSnapshot snapshot{};
  std::shared_ptr<Image> image;
  std::chrono::steady_clock::time_point closedAt{};
};

[[nodiscard]] bool shouldTraceRenderSnapshot(CommittedSurfaceSnapshot const& current,
                                             SurfaceVisualState const& visual);

void drawSurfaceImage(Canvas& canvas,
                      CommittedSurfaceSnapshot const& surface,
                      Image& image,
                      float opacity,
                      float scale);

void updateCachedImage(WaylandServer& wayland,
                       Canvas& canvas,
                       CommittedSurfaceSnapshot const& surface,
                       CachedClientImage& cached);

void drawCommittedSurface(WaylandServer& wayland,
                          Canvas& canvas,
                          TextSystem& textSystem,
                          CommittedSurfaceSnapshot const& surface,
                          SurfaceVisualState& visual,
                          CachedClientImage& cached,
                          std::chrono::steady_clock::time_point frameTime,
                          bool animationsEnabled);

} // namespace flux::compositor
