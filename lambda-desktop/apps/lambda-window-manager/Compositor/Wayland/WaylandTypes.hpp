#pragma once

#include <Lambda/Core/Color.hpp>
#include <Lambda/Graphics/Image.hpp>
#include <Lambda/UI/Window.hpp>

#include "Compositor/Screenshot.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lambda::compositor {

enum class BackgroundEffectShape : std::uint8_t {
  RoundedRect,
  Callout,
};

enum class BackgroundEffectCalloutPlacement : std::uint8_t {
  Below,
  Above,
  End,
  Start,
};

struct SurfaceBackgroundEffectSnapshot {
  float blurRadius = 0.f;
  Color baseColor{0.f, 0.f, 0.f, 0.f};
  Color tint{0.f, 0.f, 0.f, 0.f};
  Color borderColor{0.f, 0.f, 0.f, 0.f};
  bool cornerRadiusSet = false;
  CornerRadius cornerRadius{};
  BackgroundEffectShape shape = BackgroundEffectShape::RoundedRect;
  BackgroundEffectCalloutPlacement calloutPlacement = BackgroundEffectCalloutPlacement::Below;
  float arrowWidth = 16.f;
  float arrowHeight = 8.f;
};

enum class CursorShape : std::uint8_t {
  Arrow,
  IBeam,
  Hand,
  Crosshair,
  ResizeEW,
  ResizeNS,
  ResizeNESW,
  ResizeNWSE,
  ResizeAll,
  NotAllowed,
};

struct WaylandOutputInfo {
  std::string name;
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::int32_t refreshMilliHz = 60'000;
  std::int32_t physicalWidthMm = 0;
  std::int32_t physicalHeightMm = 0;
  std::uint64_t drmDevice = 0;
  int drmFd = -1;
};

struct DmabufFormatModifierPreference {
  std::uint32_t format = 0;
  std::uint64_t modifier = 0;
};

struct PresentationTiming {
  std::uint64_t monotonicNsec = 0;
  std::uint64_t sequence = 0;
  std::uint32_t backendPresentId = 0;
  std::uint32_t refreshNsec = 0;
  std::uint32_t flags = 0;
};

struct PresentationCompletion {
  std::uint32_t backendPresentId = 0;
  std::uint64_t monotonicNsec = 0;
  std::uint64_t sequence = 0;
  std::uint32_t flags = 0;
};

struct CommittedSurfaceSnapshot {
  struct DmabufPlane {
    std::uint32_t offset = 0;
    std::uint32_t stride = 0;
    std::uint64_t modifier = 0;
  };

  struct RegionRect {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
  };

  std::uint64_t id = 0;
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
  std::int32_t titleBarHeight = 0;
  std::string title;
  std::string appId;
  bool serverSideDecorated = false;
  bool cutoutsBound = false;
  bool cutoutsRejected = false;
  bool closeButtonHovered = false;
  bool closeButtonPressed = false;
  bool maximizeButtonHovered = false;
  bool maximizeButtonPressed = false;
  bool minimizeButtonHovered = false;
  bool minimizeButtonPressed = false;
  bool focused = false;
  bool fullscreen = false;
  bool activeSizing = false;
  bool pacingSizing = false;
  bool geometryAnimationGrowing = false;
  std::int32_t shadowClipTop = 0;
  std::int32_t shadowClipBottom = 0;
  std::int32_t windowClipTop = 0;
  std::int32_t windowClipBottom = 0;
  SurfaceBackgroundEffectSnapshot backgroundEffect{};
  std::uint64_t serial = 0;
  std::uint32_t lastConfigureSerial = 0;
  std::int32_t lastConfigureWidth = 0;
  std::int32_t lastConfigureHeight = 0;
  std::uint64_t lastResizeInputNsec = 0;
  std::uint64_t lastConfigureSentNsec = 0;
  std::uint64_t lastConfigureAckNsec = 0;
  std::uint64_t lastCommitNsec = 0;
  std::vector<RegionRect> backgroundBlurRects;
  std::vector<RegionRect> opaqueRegionRects;
  std::vector<RegionRect> bufferDamageRects;
  std::shared_ptr<std::vector<std::uint8_t> const> rgbaPixels;
  std::uint8_t const* shmPixels = nullptr;
  std::size_t shmPixelBytes = 0;
  Image::PixelFormat pixelFormat = Image::PixelFormat::Rgba8888;
  bool contentFullyOpaque = false;
  std::uint64_t dmabufBufferId = 0;
  std::uint32_t dmabufFormat = 0;
  std::vector<DmabufPlane> dmabufPlanes;
};

struct SnapPreviewSnapshot {
  std::uint64_t surfaceId = 0;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::int32_t targetX = 0;
  std::int32_t targetY = 0;
  std::int32_t targetWidth = 0;
  std::int32_t targetHeight = 0;
  std::int32_t cacheX = 0;
  std::int32_t cacheY = 0;
  std::int32_t cacheWidth = 0;
  std::int32_t cacheHeight = 0;
};

struct WindowCyclerOverlaySnapshot {
  std::vector<std::uint64_t> surfaceIds;
  std::size_t selectedIndex = 0;
};

enum class ShortcutAction : std::uint8_t {
  CloseFocused,
  CycleFocus,
  SnapLeft,
  SnapRight,
  Maximize,
  Restore,
  LaunchCommand,
  Screenshot,
  ScreenshotRegion,
  ScreenshotActiveWindow,
  Terminate,
};

struct ShortcutBinding {
  ShortcutAction action = ShortcutAction::CloseFocused;
  std::uint32_t key = 0;
  bool meta = false;
  bool ctrl = false;
  bool alt = false;
  bool shift = false;
};

} // namespace lambda::compositor
