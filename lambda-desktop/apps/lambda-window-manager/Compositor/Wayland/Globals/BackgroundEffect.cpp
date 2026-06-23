#include "Compositor/Wayland/Globals/BackgroundEffect.hpp"

#include "Compositor/Wayland/ResourceTemplates.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "ext-background-effect-v1-server-protocol.h"

#include <algorithm>
#include <memory>
#include <vector>
#include <wayland-server-core.h>
#include <wayland-util.h>

namespace lambdaui::compositor {
namespace {

void backgroundEffectManagerDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void backgroundEffectSurfaceDestroy(wl_client*, wl_resource* resource) {
  auto* effect = resourceData<WaylandServer::Impl::BackgroundEffect>(resource);
  if (effect && effect->surface) {
    effect->surface->pendingBackgroundBlurRects.clear();
    effect->surface->backgroundBlurPending = true;
    effect->surface->pendingBackgroundEffectState = {};
    effect->surface->backgroundEffectStatePending = true;
  }
  wl_resource_destroy(resource);
}

std::vector<CommittedSurfaceSnapshot::RegionRect> copyRegionRects(wl_resource* regionResource) {
  if (!regionResource) return {};
  auto* region = resourceData<WaylandServer::Impl::Region>(regionResource);
  return region ? region->rects : std::vector<CommittedSurfaceSnapshot::RegionRect>{};
}

void backgroundEffectSurfaceSetBlurRegion(wl_client*, wl_resource* resource, wl_resource* regionResource) {
  auto* effect = resourceData<WaylandServer::Impl::BackgroundEffect>(resource);
  if (!effect || !effect->surface) {
    wl_resource_post_error(resource,
                           EXT_BACKGROUND_EFFECT_SURFACE_V1_ERROR_SURFACE_DESTROYED,
                           "associated wl_surface has been destroyed");
    return;
  }

  effect->surface->pendingBackgroundBlurRects = copyRegionRects(regionResource);
  effect->surface->backgroundBlurPending = true;
  effect->surface->pendingBackgroundEffectState = {};
  effect->surface->backgroundEffectStatePending = true;
}

Color colorFromRgba(std::uint32_t rgba) {
  auto channel = [&](int shift) {
    return static_cast<float>((rgba >> shift) & 0xffu) / 255.f;
  };
  return Color{channel(24), channel(16), channel(8), channel(0)};
}

void backgroundEffectSurfaceSetBlurRadius(wl_client*, wl_resource* resource, std::int32_t radius) {
  auto* effect = resourceData<WaylandServer::Impl::BackgroundEffect>(resource);
  if (!effect || !effect->surface) {
    wl_resource_post_error(resource,
                           EXT_BACKGROUND_EFFECT_SURFACE_V1_ERROR_SURFACE_DESTROYED,
                           "associated wl_surface has been destroyed");
    return;
  }

  if (!effect->surface->backgroundEffectStatePending) {
    effect->surface->pendingBackgroundEffectState = effect->surface->backgroundEffectState;
  }
  effect->surface->pendingBackgroundEffectState.blurRadius =
      std::max(0.f, static_cast<float>(wl_fixed_to_double(radius)));
  effect->surface->backgroundEffectStatePending = true;
}

void backgroundEffectSurfaceSetTint(wl_client*, wl_resource* resource, std::uint32_t tint) {
  auto* effect = resourceData<WaylandServer::Impl::BackgroundEffect>(resource);
  if (!effect || !effect->surface) {
    wl_resource_post_error(resource,
                           EXT_BACKGROUND_EFFECT_SURFACE_V1_ERROR_SURFACE_DESTROYED,
                           "associated wl_surface has been destroyed");
    return;
  }

  if (!effect->surface->backgroundEffectStatePending) {
    effect->surface->pendingBackgroundEffectState = effect->surface->backgroundEffectState;
  }
  effect->surface->pendingBackgroundEffectState.tint = colorFromRgba(tint);
  effect->surface->backgroundEffectStatePending = true;
}

void backgroundEffectSurfaceSetBaseColor(wl_client*, wl_resource* resource, std::uint32_t baseColor) {
  auto* effect = resourceData<WaylandServer::Impl::BackgroundEffect>(resource);
  if (!effect || !effect->surface) {
    wl_resource_post_error(resource,
                           EXT_BACKGROUND_EFFECT_SURFACE_V1_ERROR_SURFACE_DESTROYED,
                           "associated wl_surface has been destroyed");
    return;
  }

  if (!effect->surface->backgroundEffectStatePending) {
    effect->surface->pendingBackgroundEffectState = effect->surface->backgroundEffectState;
  }
  effect->surface->pendingBackgroundEffectState.baseColor = colorFromRgba(baseColor);
  effect->surface->backgroundEffectStatePending = true;
}

void backgroundEffectSurfaceSetBorder(wl_client*, wl_resource* resource, std::uint32_t border) {
  auto* effect = resourceData<WaylandServer::Impl::BackgroundEffect>(resource);
  if (!effect || !effect->surface) {
    wl_resource_post_error(resource,
                           EXT_BACKGROUND_EFFECT_SURFACE_V1_ERROR_SURFACE_DESTROYED,
                           "associated wl_surface has been destroyed");
    return;
  }

  if (!effect->surface->backgroundEffectStatePending) {
    effect->surface->pendingBackgroundEffectState = effect->surface->backgroundEffectState;
  }
  effect->surface->pendingBackgroundEffectState.borderColor = colorFromRgba(border);
  effect->surface->backgroundEffectStatePending = true;
}

void backgroundEffectSurfaceSetCornerRadii(wl_client*,
                                           wl_resource* resource,
                                           std::int32_t topLeft,
                                           std::int32_t topRight,
                                           std::int32_t bottomRight,
                                           std::int32_t bottomLeft) {
  auto* effect = resourceData<WaylandServer::Impl::BackgroundEffect>(resource);
  if (!effect || !effect->surface) {
    wl_resource_post_error(resource,
                           EXT_BACKGROUND_EFFECT_SURFACE_V1_ERROR_SURFACE_DESTROYED,
                           "associated wl_surface has been destroyed");
    return;
  }

  if (!effect->surface->backgroundEffectStatePending) {
    effect->surface->pendingBackgroundEffectState = effect->surface->backgroundEffectState;
  }
  effect->surface->pendingBackgroundEffectState.cornerRadiusSet = true;
  effect->surface->pendingBackgroundEffectState.cornerRadius = CornerRadius{
      std::max(0.f, static_cast<float>(wl_fixed_to_double(topLeft))),
      std::max(0.f, static_cast<float>(wl_fixed_to_double(topRight))),
      std::max(0.f, static_cast<float>(wl_fixed_to_double(bottomRight))),
      std::max(0.f, static_cast<float>(wl_fixed_to_double(bottomLeft))),
  };
  effect->surface->backgroundEffectStatePending = true;
}

void backgroundEffectSurfaceSetShape(wl_client*,
                                     wl_resource* resource,
                                     std::uint32_t shape,
                                     std::uint32_t placement,
                                     std::int32_t arrowWidth,
                                     std::int32_t arrowHeight) {
  auto* effect = resourceData<WaylandServer::Impl::BackgroundEffect>(resource);
  if (!effect || !effect->surface) {
    wl_resource_post_error(resource,
                           EXT_BACKGROUND_EFFECT_SURFACE_V1_ERROR_SURFACE_DESTROYED,
                           "associated wl_surface has been destroyed");
    return;
  }

  if (!effect->surface->backgroundEffectStatePending) {
    effect->surface->pendingBackgroundEffectState = effect->surface->backgroundEffectState;
  }
  auto& state = effect->surface->pendingBackgroundEffectState;
  state.shape = shape == EXT_BACKGROUND_EFFECT_SURFACE_V1_SHAPE_CALLOUT
                    ? BackgroundEffectShape::Callout
                    : BackgroundEffectShape::RoundedRect;
  switch (placement) {
  case EXT_BACKGROUND_EFFECT_SURFACE_V1_CALLOUT_PLACEMENT_ABOVE:
    state.calloutPlacement = BackgroundEffectCalloutPlacement::Above;
    break;
  case EXT_BACKGROUND_EFFECT_SURFACE_V1_CALLOUT_PLACEMENT_END:
    state.calloutPlacement = BackgroundEffectCalloutPlacement::End;
    break;
  case EXT_BACKGROUND_EFFECT_SURFACE_V1_CALLOUT_PLACEMENT_START:
    state.calloutPlacement = BackgroundEffectCalloutPlacement::Start;
    break;
  case EXT_BACKGROUND_EFFECT_SURFACE_V1_CALLOUT_PLACEMENT_BELOW:
  default:
    state.calloutPlacement = BackgroundEffectCalloutPlacement::Below;
    break;
  }
  state.arrowWidth = std::max(0.f, static_cast<float>(wl_fixed_to_double(arrowWidth)));
  state.arrowHeight = std::max(0.f, static_cast<float>(wl_fixed_to_double(arrowHeight)));
  effect->surface->backgroundEffectStatePending = true;
}

struct ext_background_effect_surface_v1_interface const backgroundEffectSurfaceImpl{
    .destroy = backgroundEffectSurfaceDestroy,
    .set_blur_region = backgroundEffectSurfaceSetBlurRegion,
    .set_blur_radius = backgroundEffectSurfaceSetBlurRadius,
    .set_tint = backgroundEffectSurfaceSetTint,
    .set_border = backgroundEffectSurfaceSetBorder,
    .set_corner_radii = backgroundEffectSurfaceSetCornerRadii,
    .set_base_color = backgroundEffectSurfaceSetBaseColor,
    .set_shape = backgroundEffectSurfaceSetShape,
};

void backgroundEffectManagerGetBackgroundEffect(wl_client* client,
                                                wl_resource* resource,
                                                std::uint32_t id,
                                                wl_resource* surfaceResource) {
  auto* server = serverFrom(resource);
  auto* surface = resourceData<WaylandServer::Impl::Surface>(surfaceResource);
  if (!server || !surface) return;
  if (surface->backgroundEffect) {
    wl_resource_post_error(resource,
                           EXT_BACKGROUND_EFFECT_MANAGER_V1_ERROR_BACKGROUND_EFFECT_EXISTS,
                           "wl_surface already has a background effect object");
    return;
  }

  auto effect = std::make_unique<WaylandServer::Impl::BackgroundEffect>();
  effect->server = server;
  effect->surface = surface;
  auto const version =
      backgroundEffectResourceVersion(static_cast<std::uint32_t>(wl_resource_get_version(resource)));
  wl_resource* effectResource =
      wl_resource_create(client, &ext_background_effect_surface_v1_interface, version, id);
  if (!effectResource) {
    wl_client_post_no_memory(client);
    return;
  }

  effect->resource = effectResource;
  auto* raw = effect.get();
  surface->backgroundEffect = raw;
  server->backgroundEffects_.push_back(std::move(effect));
  wl_resource_set_implementation(effectResource,
                                 &backgroundEffectSurfaceImpl,
                                 raw,
                                 destroyResourceCallback<WaylandServer::Impl::BackgroundEffect,
                                                         WaylandServer::Impl,
                                                         &WaylandServer::Impl::destroyBackgroundEffect>);
}

struct ext_background_effect_manager_v1_interface const backgroundEffectManagerImpl{
    .destroy = backgroundEffectManagerDestroy,
    .get_background_effect = backgroundEffectManagerGetBackgroundEffect,
};

} // namespace

void bindBackgroundEffectManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource = wl_resource_create(client,
                                             &ext_background_effect_manager_v1_interface,
                                             backgroundEffectResourceVersion(version),
                                             id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &backgroundEffectManagerImpl, data, nullptr);
  ext_background_effect_manager_v1_send_capabilities(
      resource,
      EXT_BACKGROUND_EFFECT_MANAGER_V1_CAPABILITY_BLUR);
}

} // namespace lambdaui::compositor
