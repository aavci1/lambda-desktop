#pragma once

#include <cstdint>
#include <optional>

namespace flux::compositor {

constexpr std::int32_t kCompositorTitleBarHeight = 28;
constexpr std::int32_t kCompositorSnapEdgeThreshold = 32;
constexpr std::int32_t kCompositorMinWindowWidth = 160;
constexpr std::int32_t kCompositorMinWindowHeight = 120;

enum class ResizeEdge : std::uint8_t {
  None = 0,
  Top = 1u << 0u,
  Bottom = 1u << 1u,
  Left = 1u << 2u,
  Right = 1u << 3u,
};

[[nodiscard]] constexpr ResizeEdge operator|(ResizeEdge lhs, ResizeEdge rhs) {
  return static_cast<ResizeEdge>(static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

[[nodiscard]] constexpr bool hasResizeEdge(ResizeEdge edges, ResizeEdge edge) {
  return (static_cast<std::uint8_t>(edges) & static_cast<std::uint8_t>(edge)) != 0u;
}

struct OutputGeometry {
  std::int32_t width = 0;
  std::int32_t height = 0;
};

struct WindowGeometry {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
};

struct ResizeDragGeometry {
  float startPointerX = 0.f;
  float startPointerY = 0.f;
  float pointerX = 0.f;
  float pointerY = 0.f;
  WindowGeometry startWindow{};
  ResizeEdge edges = ResizeEdge::None;
  OutputGeometry output{};
};

struct RestoreDragGeometry {
  float pointerX = 0.f;
  float pointerY = 0.f;
  float dragOffsetY = 0.f;
  WindowGeometry snappedWindow{};
  WindowGeometry restoreWindow{};
  OutputGeometry output{};
};

[[nodiscard]] std::optional<WindowGeometry> snapPreviewGeometry(WindowGeometry const& window,
                                                               OutputGeometry output);
[[nodiscard]] WindowGeometry snappedWindowGeometry(OutputGeometry output, bool leftHalf);
[[nodiscard]] WindowGeometry restoredDragGeometry(RestoreDragGeometry const& geometry);
[[nodiscard]] WindowGeometry resizedWindowGeometry(ResizeDragGeometry const& geometry);

} // namespace flux::compositor
