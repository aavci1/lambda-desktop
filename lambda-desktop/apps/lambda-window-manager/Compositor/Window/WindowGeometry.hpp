#pragma once

#include <cstdint>
#include <optional>
#include <span>

namespace lambda::compositor {

constexpr std::int32_t kCompositorTitleBarHeight = 28;
constexpr std::int32_t kCompositorSnapEdgeThreshold = 8;
constexpr std::int32_t kCompositorCenterSnapThreshold = 18;
constexpr std::int32_t kCompositorMinWindowWidth = 160;
constexpr std::int32_t kCompositorMinWindowHeight = 120;

enum class ResizeEdge : std::uint8_t {
  None = 0,
  Top = 1u << 0u,
  Bottom = 1u << 1u,
  Left = 1u << 2u,
  Right = 1u << 3u,
};

enum class PopupAnchor : std::uint8_t {
  None,
  Top,
  Bottom,
  Left,
  Right,
  TopLeft,
  BottomLeft,
  TopRight,
  BottomRight,
};

enum class PopupGravity : std::uint8_t {
  None,
  Top,
  Bottom,
  Left,
  Right,
  TopLeft,
  BottomLeft,
  TopRight,
  BottomRight,
};

enum class PopupConstraintAdjustment : std::uint32_t {
  None = 0,
  SlideX = 1u << 0u,
  SlideY = 1u << 1u,
  FlipX = 1u << 2u,
  FlipY = 1u << 3u,
  ResizeX = 1u << 4u,
  ResizeY = 1u << 5u,
};

[[nodiscard]] constexpr ResizeEdge operator|(ResizeEdge lhs, ResizeEdge rhs) {
  return static_cast<ResizeEdge>(static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

[[nodiscard]] constexpr PopupConstraintAdjustment operator|(PopupConstraintAdjustment lhs,
                                                           PopupConstraintAdjustment rhs) {
  return static_cast<PopupConstraintAdjustment>(static_cast<std::uint32_t>(lhs) |
                                                static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr bool hasResizeEdge(ResizeEdge edges, ResizeEdge edge) {
  return (static_cast<std::uint8_t>(edges) & static_cast<std::uint8_t>(edge)) != 0u;
}

[[nodiscard]] constexpr bool hasPopupConstraintAdjustment(PopupConstraintAdjustment adjustment,
                                                          PopupConstraintAdjustment flag) {
  return (static_cast<std::uint32_t>(adjustment) & static_cast<std::uint32_t>(flag)) != 0u;
}

struct OutputGeometry {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
};

struct WindowGeometry {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
};

enum class SnapTarget : std::uint8_t {
  LeftHalf,
  RightHalf,
  TopLeftQuarter,
  TopRightQuarter,
  BottomLeftQuarter,
  BottomRightQuarter,
  Maximized,
};

struct ResizeDragGeometry {
  float startPointerX = 0.f;
  float startPointerY = 0.f;
  float pointerX = 0.f;
  float pointerY = 0.f;
  WindowGeometry startWindow{};
  ResizeEdge edges = ResizeEdge::None;
  OutputGeometry output{};
  std::int32_t topInset = kCompositorTitleBarHeight;
};

struct RestoreDragGeometry {
  float pointerX = 0.f;
  float pointerY = 0.f;
  float dragOffsetY = 0.f;
  WindowGeometry snappedWindow{};
  WindowGeometry restoreWindow{};
  OutputGeometry output{};
  std::int32_t topInset = kCompositorTitleBarHeight;
  std::int32_t frameOutset = 0;
};

struct PopupPositionerGeometry {
  std::optional<WindowGeometry> parent;
  OutputGeometry output{};
  std::int32_t anchorRectX = 0;
  std::int32_t anchorRectY = 0;
  std::int32_t anchorRectWidth = 0;
  std::int32_t anchorRectHeight = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::int32_t offsetX = 0;
  std::int32_t offsetY = 0;
  PopupAnchor anchor = PopupAnchor::None;
  PopupGravity gravity = PopupGravity::None;
  PopupConstraintAdjustment constraintAdjustment = PopupConstraintAdjustment::None;
};

struct PopupGeometry {
  WindowGeometry window{};
  std::int32_t configureX = 0;
  std::int32_t configureY = 0;
  std::int32_t configureWidth = 0;
  std::int32_t configureHeight = 0;
};

[[nodiscard]] std::optional<WindowGeometry> snapPreviewGeometry(WindowGeometry const& window,
                                                               OutputGeometry output,
                                                               std::int32_t topInset = kCompositorTitleBarHeight,
                                                               std::int32_t frameOutset = 0);
[[nodiscard]] std::optional<SnapTarget> snapTargetForWindow(WindowGeometry const& window,
                                                            OutputGeometry output,
                                                            std::int32_t topInset = kCompositorTitleBarHeight,
                                                            std::int32_t frameOutset = 0);
[[nodiscard]] WindowGeometry snapTargetGeometry(OutputGeometry output,
                                                SnapTarget target,
                                                std::int32_t topInset = kCompositorTitleBarHeight,
                                                std::int32_t frameOutset = 0);
[[nodiscard]] WindowGeometry snappedWindowGeometry(OutputGeometry output,
                                                  bool leftHalf,
                                                  std::int32_t topInset = kCompositorTitleBarHeight,
                                                  std::int32_t frameOutset = 0);
[[nodiscard]] WindowGeometry maximizedWindowGeometry(OutputGeometry output,
                                                    std::int32_t topInset = kCompositorTitleBarHeight,
                                                    std::int32_t frameOutset = 0);
[[nodiscard]] WindowGeometry centerSnappedWindowGeometry(WindowGeometry window,
                                                        OutputGeometry output,
                                                        std::int32_t topInset = kCompositorTitleBarHeight,
                                                        std::int32_t threshold = kCompositorCenterSnapThreshold,
                                                        std::int32_t frameOutset = 0);
[[nodiscard]] WindowGeometry windowFrameGeometryForContent(WindowGeometry const& content,
                                                          std::int32_t topInset = kCompositorTitleBarHeight,
                                                          std::int32_t frameOutset = 0);
[[nodiscard]] WindowGeometry restoredDragGeometry(RestoreDragGeometry const& geometry);
[[nodiscard]] WindowGeometry resizedWindowGeometry(ResizeDragGeometry const& geometry);
[[nodiscard]] PopupGeometry positionedPopupGeometry(PopupPositionerGeometry const& geometry);
[[nodiscard]] std::optional<WindowGeometry> popupScreenGeometry(std::span<WindowGeometry const> parentToChildChain);

} // namespace lambda::compositor
