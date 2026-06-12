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
      std::int32_t const bottomGap = std::max(0, layer.marginBottom);
      zones.dock = std::max(zones.dock, layer.extent + bottomGap * 2);
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

LayerShellPlacement resolveLayerShellPlacement(LayerShellPlacementInput const& input) {
  LayerShellPlacement placement;

  if (hasAnchor(input.anchor, kLayerShellAnchorLeft)) {
    placement.x = input.marginLeft;
  } else if (hasAnchor(input.anchor, kLayerShellAnchorRight)) {
    placement.x = input.outputWidth - input.surfaceWidth - input.marginRight;
  } else {
    placement.x = (input.outputWidth - input.surfaceWidth) / 2;
  }

  if (hasAnchor(input.anchor, kLayerShellAnchorTop)) {
    placement.y = input.marginTop;
  } else if (hasAnchor(input.anchor, kLayerShellAnchorBottom)) {
    placement.y = input.outputHeight - input.surfaceHeight - input.marginBottom;
  } else {
    placement.y = (input.outputHeight - input.surfaceHeight) / 2;
  }

  return placement;
}

} // namespace lambda::compositor
