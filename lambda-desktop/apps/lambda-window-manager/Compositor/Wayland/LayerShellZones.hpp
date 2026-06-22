#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace lambda::compositor {

inline constexpr std::uint32_t kLayerShellAnchorTop = 1u;
inline constexpr std::uint32_t kLayerShellAnchorBottom = 2u;
inline constexpr std::uint32_t kLayerShellAnchorLeft = 4u;
inline constexpr std::uint32_t kLayerShellAnchorRight = 8u;

struct LayerShellReservedZoneInput {
  char const* nameSpace = nullptr;
  std::int32_t exclusiveZone = 0;
  std::uint32_t anchor = 0;
  std::int32_t marginBottom = 0;
  std::int32_t extent = 0;
};

struct LayerShellReservedZones {
  std::int32_t dock = 0;
};

struct LayerShellConfigureSizeInput {
  std::uint32_t requestedWidth = 0;
  std::uint32_t requestedHeight = 0;
  std::uint32_t anchor = 0;
  std::int32_t marginTop = 0;
  std::int32_t marginRight = 0;
  std::int32_t marginBottom = 0;
  std::int32_t marginLeft = 0;
  std::int32_t outputWidth = 1;
  std::int32_t outputHeight = 1;
};

struct LayerShellConfigureSize {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

struct LayerShellPlacementInput {
  std::uint32_t anchor = 0;
  std::int32_t marginTop = 0;
  std::int32_t marginRight = 0;
  std::int32_t marginBottom = 0;
  std::int32_t marginLeft = 0;
  std::int32_t surfaceWidth = 0;
  std::int32_t surfaceHeight = 0;
  std::int32_t outputX = 0;
  std::int32_t outputY = 0;
  std::int32_t outputWidth = 1;
  std::int32_t outputHeight = 1;
};

struct LayerShellPlacement {
  std::int32_t x = 0;
  std::int32_t y = 0;
};

[[nodiscard]] LayerShellReservedZones aggregateLayerShellReservedZones(
    std::span<LayerShellReservedZoneInput const> layers);

[[nodiscard]] LayerShellConfigureSize resolveLayerShellConfigureSize(LayerShellConfigureSizeInput const& input);

[[nodiscard]] LayerShellPlacement resolveLayerShellPlacement(LayerShellPlacementInput const& input);

} // namespace lambda::compositor
