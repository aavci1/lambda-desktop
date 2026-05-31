#pragma once

#include "Compositor/Chrome/ChromeConfig.hpp"
#include "Compositor/SceneDamage.hpp"
#include "Compositor/Surface/CommittedSurfacePainter.hpp"
#include "Compositor/WaylandServer.hpp"
#include "Graphics/Vulkan/VulkanFrameRecorder.hpp"

#include <Lambda/Graphics/Canvas.hpp>
#include <Lambda/Graphics/Image.hpp>
#include <Lambda/Graphics/TextSystem.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lambda::compositor {

struct CachedClientImage {
  struct DmabufEntry {
    std::uint64_t bufferId = 0;
    std::shared_ptr<Image> image;
    bool imported = false;
  };

  std::uint64_t id = 0;
  std::uint64_t serial = 0;
  std::uint64_t dmabufBufferId = 0;
  std::int32_t shmBufferWidth = 0;
  std::int32_t shmBufferHeight = 0;
  std::shared_ptr<Image> image;
  bool logged = false;
  bool dmabufImported = false;
  std::vector<DmabufEntry> dmabufImages;
  std::unique_ptr<lambda::VulkanFrameRecorder> recordedOps;
  std::uint64_t recordedSignature = 0;
  std::int32_t recordedX = 0;
  std::int32_t recordedY = 0;
  std::uint64_t lastDrawSignature = 0;
  std::uint32_t stableDrawSignatureFrames = 0;
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
  std::unordered_map<std::uint64_t, std::uint64_t> rejectedOverlaySignaturesBySurface;
  SceneDamageState sceneDamage;
  std::uint64_t primaryReuseSignature = 0;
  std::uint64_t primaryReuseOverlaySurfaceId = 0;
  bool primaryReuseSignatureValid = false;
};

void drawSurfaceImage(Canvas &canvas, CommittedSurfaceSnapshot const &surface, Image &image, float opacity,
                      float scale);

void updateCachedImage(WaylandServer &wayland, Canvas &canvas, CommittedSurfaceSnapshot const &surface,
                       CachedClientImage &cached);

void drawCommittedSurface(WaylandServer &wayland, Canvas &canvas, TextSystem &textSystem,
                          CommittedSurfaceSnapshot const &surface, SurfaceVisualState &visual,
                          CachedClientImage &cached, std::chrono::steady_clock::time_point frameTime,
                          ChromeConfig const &chrome, bool animationsEnabled);

void captureClosingSurfaces(SurfaceRenderState &state, std::unordered_set<std::uint64_t> const &liveSurfaceIds,
                            std::chrono::steady_clock::time_point frameTime, bool animationsEnabled);

void drawClosingSurfaces(Canvas &canvas, SurfaceRenderState &state, std::chrono::steady_clock::time_point frameTime);

[[nodiscard]] bool hasActiveSurfaceAnimations(SurfaceRenderState const &state,
                                              std::chrono::steady_clock::time_point frameTime, bool animationsEnabled);

void pruneSurfaceRenderState(SurfaceRenderState &state, std::unordered_set<std::uint64_t> const &liveSurfaceIds);

} // namespace lambda::compositor
