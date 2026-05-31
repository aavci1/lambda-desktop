#include "Compositor/Wayland/LayerShellZones.hpp"

#include <algorithm>
#include <string_view>

namespace lambda::compositor {

namespace {

bool hasAnchor(std::uint32_t anchor, std::uint32_t edge) {
  return (anchor & edge) != 0;
}

std::uint32_t positiveExtent(std::int32_t extent) {
  return static_cast<std::uint32_t>(std::max(1, extent));
}

} // namespace

LayerShellReservedZones aggregateLayerShellReservedZones(
    std::span<LayerShellReservedZoneInput const> layers) {
  LayerShellReservedZones zones;
  for (auto const& layer : layers) {
    if (!layer.nameSpace) continue;
    if (std::string_view(layer.nameSpace) == "lambda.dock" &&
        hasAnchor(layer.anchor, kLayerShellAnchorBottom)) {
      zones.dock = std::max(zones.dock, layer.extent + std::max(0, layer.marginBottom));
    }
  }
  return zones;
}

LayerShellConfigureSize resolveLayerShellConfigureSize(LayerShellConfigureSizeInput const& input) {
  LayerShellConfigureSize size{
      .width = input.requestedWidth,
      .height = input.requestedHeight,
  };

  if (input.requestedWidth == 0 &&
      hasAnchor(input.anchor, kLayerShellAnchorLeft) &&
      hasAnchor(input.anchor, kLayerShellAnchorRight)) {
    size.width = positiveExtent(input.outputWidth - input.marginLeft - input.marginRight);
  }

  if (input.requestedHeight == 0 &&
      hasAnchor(input.anchor, kLayerShellAnchorTop) &&
      hasAnchor(input.anchor, kLayerShellAnchorBottom)) {
    size.height = positiveExtent(input.outputHeight - input.marginTop - input.marginBottom);
  }

  return size;
}

} // namespace lambda::compositor
