#pragma once

#include "Compositor/Chrome/ChromeConfig.hpp"
#include "Compositor/WaylandServer.hpp"

#include <Flux/Graphics/Canvas.hpp>
#include <Flux/Graphics/Image.hpp>
#include <Flux/Graphics/TextSystem.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>

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

struct SurfaceRenderState {
  std::unordered_map<std::uint64_t, CachedClientImage> clientImages;
  std::unordered_map<std::uint64_t, SurfaceVisualState> surfaceVisuals;
  std::unordered_map<std::uint64_t, ClosingSurfaceVisual> closingSurfaces;
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
                          ChromeConfig const& chrome,
                          bool animationsEnabled);

void captureClosingSurfaces(SurfaceRenderState& state,
                            std::unordered_set<std::uint64_t> const& liveSurfaceIds,
                            std::chrono::steady_clock::time_point frameTime,
                            bool animationsEnabled);

void drawClosingSurfaces(Canvas& canvas,
                         SurfaceRenderState& state,
                         std::chrono::steady_clock::time_point frameTime);

void pruneSurfaceRenderState(SurfaceRenderState& state,
                             std::unordered_set<std::uint64_t> const& liveSurfaceIds);

} // namespace flux::compositor
