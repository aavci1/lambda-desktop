#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include "Compositor/Wayland/DecorationState.hpp"
#include "Compositor/Wayland/ResourceTemplates.hpp"
#include "pointer-constraints-unstable-v1-server-protocol.h"
#include "presentation-time-server-protocol.h"

#include <sys/mman.h>
#include <unistd.h>
#include <wayland-server-core.h>

#include <algorithm>
#include <cstdio>
#include <vector>

namespace flux::compositor {

void WaylandServer::Impl::destroySurface(Surface* surface) {
  bool const activatePrevious = keyboardFocus_ == surface && surfaceIsXdgToplevel(surface);
  removeSurfaceFromFocusOrder(this, surface);
  if (pointerFocus_ == surface) pointerFocus_ = nullptr;
  if (keyboardFocus_ == surface) keyboardFocus_ = nullptr;
  if (primarySelectionSource_ &&
      wl_resource_get_client(primarySelectionSource_->resource) == wl_resource_get_client(surface->resource)) {
    primarySelectionSource_ = nullptr;
    sendPrimarySelectionForFocus(this);
  }
  if (selectionSource_ && wl_resource_get_client(selectionSource_->resource) == wl_resource_get_client(surface->resource)) {
    selectionSource_ = nullptr;
    sendSelectionForFocus(this);
  }
  if (dragSurface_ == surface) dragSurface_ = nullptr;
  if (resizeSurface_ == surface) resizeSurface_ = nullptr;
  if (closePressSurface_ == surface) closePressSurface_ = nullptr;
  if (minimizePressSurface_ == surface) minimizePressSurface_ = nullptr;
  if (lastTitleClickSurface_ == surface) lastTitleClickSurface_ = nullptr;
  if (lastPointerButtonSurface_ == surface) lastPointerButtonSurface_ = nullptr;
  if (cursorSurface_ == surface) cursorSurface_ = nullptr;
  if (lastActivationSurface_ == surface) lastActivationSurface_ = nullptr;
  for (auto& token : activationTokens_) {
    if (token->surface == surface) token->surface = nullptr;
  }
  for (auto& popup : popups_) {
    if (popup->parentSurface == surface) popup->parentSurface = nullptr;
  }
  for (auto it = subsurfaces_.begin(); it != subsurfaces_.end();) {
    if ((*it)->surface == surface || (*it)->parent == surface) {
      wl_resource_destroy((*it)->resource);
      it = subsurfaces_.begin();
    } else {
      ++it;
    }
  }
  if (dndOrigin_ == surface || dndTarget_ == surface) clearDnd(this);
  for (auto& device : cursorShapeDevices_) {
    if (device->pointer && wl_resource_get_client(device->pointer) == wl_resource_get_client(surface->resource)) {
      device->pointer = nullptr;
    }
  }
  if (surface->viewport) wl_resource_destroy(surface->viewport->resource);
  if (surface->fractionalScale) wl_resource_destroy(surface->fractionalScale->resource);
  if (surface->layerSurface) wl_resource_destroy(surface->layerSurface->resource);
  if (surface->xdgPopup) wl_resource_destroy(surface->xdgPopup->resource);
  if (surface->backgroundEffect && surface->backgroundEffect->surface == surface) {
    surface->backgroundEffect->surface = nullptr;
    surface->backgroundEffect = nullptr;
  }
  for (auto it = pointerConstraints_.begin(); it != pointerConstraints_.end();) {
    if ((*it)->surface == surface) {
      wl_resource_destroy((*it)->resource);
      it = pointerConstraints_.begin();
    } else {
      ++it;
    }
  }
  std::vector<PresentationFeedback*> pendingFeedbacks = std::move(surface->pendingPresentationFeedbacks);
  surface->pendingPresentationFeedbacks.clear();
  for (auto* feedback : pendingFeedbacks) {
    if (!feedback || !feedback->resource) continue;
    wp_presentation_feedback_send_discarded(feedback->resource);
    wl_resource_destroy(feedback->resource);
  }
  std::vector<PresentationFeedback*> committedFeedbacks = std::move(surface->presentationFeedbacks);
  surface->presentationFeedbacks.clear();
  for (auto* feedback : committedFeedbacks) {
    if (!feedback || !feedback->resource) continue;
    wp_presentation_feedback_send_discarded(feedback->resource);
    wl_resource_destroy(feedback->resource);
  }
  for (auto it = idleInhibitors_.begin(); it != idleInhibitors_.end();) {
    if ((*it)->surface == surface) {
      wl_resource_destroy((*it)->resource);
      it = idleInhibitors_.begin();
    } else {
      ++it;
    }
  }
  for (wl_resource* callback : surface->frameCallbacks) {
    wl_resource_destroy(callback);
  }
  surface->frameCallbacks.clear();
  eraseResource(surfaces_, surface);
  if (activatePrevious) activateMostRecentToplevel(this, 0);
}

void WaylandServer::Impl::destroySubsurface(Subsurface* subsurface) {
  if (subsurface && subsurface->surface && subsurface->surface->subsurfaceRole == subsurface) {
    subsurface->surface->subsurfaceRole = nullptr;
    if (surfaceIsSubsurface(subsurface->surface)) subsurface->surface->role = SurfaceRole::None;
  }
  eraseResource(subsurfaces_, subsurface);
}

void WaylandServer::Impl::destroyXdgSurface(XdgSurface* surface) {
  eraseResource(xdgSurfaces_, surface);
}

void WaylandServer::Impl::destroyXdgPositioner(XdgPositioner* positioner) {
  eraseResource(xdgPositioners_, positioner);
}

void WaylandServer::Impl::destroyXdgToplevel(XdgToplevel* toplevel) {
  Surface* surface = toplevel && toplevel->xdgSurface ? toplevel->xdgSurface->surface : nullptr;
  bool const activatePrevious = keyboardFocus_ == surface && surfaceIsXdgToplevel(surface);
  if (surfaceIsXdgToplevel(surface)) {
    removeSurfaceFromFocusOrder(this, surface);
    if (pointerFocus_ == surface) pointerFocus_ = nullptr;
    if (keyboardFocus_ == surface) keyboardFocus_ = nullptr;
    surface->role = SurfaceRole::None;
  }
  while (auto* decoration = decorationFor(this, toplevel)) {
    wl_resource_destroy(decoration->resource);
  }
  while (auto* cutouts = cutoutsFor(this, toplevel)) {
    if (shouldReportDefunctCutoutsOnToplevelDestroy(cutouts->resource != nullptr)) {
      wl_resource_post_error(cutouts->resource,
                             XX_CUTOUTS_MANAGER_V1_ERROR_DEFUNCT_CUTOUTS_OBJECT,
                             "xx_cutouts_v1 must be destroyed before its xdg_toplevel");
    }
    wl_resource_destroy(cutouts->resource);
  }
  eraseResource(toplevels_, toplevel);
  if (activatePrevious) activateMostRecentToplevel(this, 0);
}

void WaylandServer::Impl::destroyXdgPopup(XdgPopup* popup) {
  if (popup && popup->xdgSurface && popup->xdgSurface->surface && popup->xdgSurface->surface->xdgPopup == popup) {
    popup->xdgSurface->surface->xdgPopup = nullptr;
    if (surfaceIsXdgPopup(popup->xdgSurface->surface)) popup->xdgSurface->surface->role = SurfaceRole::None;
  }
  eraseResource(popups_, popup);
}

void WaylandServer::Impl::destroyShmPool(ShmPool* pool) {
  for (auto& buffer : shmBuffers_) {
    if (buffer->pool == pool) buffer->pool = nullptr;
  }
  if (pool->data) munmap(pool->data, static_cast<std::size_t>(pool->size));
  if (pool->fd >= 0) close(pool->fd);
  eraseResource(shmPools_, pool);
}

void WaylandServer::Impl::destroyShmBuffer(ShmBuffer* buffer) {
  if (buffer->data) munmap(buffer->data, static_cast<std::size_t>(buffer->size));
  if (buffer->fd >= 0) close(buffer->fd);
  eraseResource(shmBuffers_, buffer);
}

void WaylandServer::Impl::destroyDmabufParams(DmabufParams* params) {
  for (auto& plane : params->planes) {
    if (plane.fd >= 0) close(plane.fd);
    plane.fd = -1;
  }
  eraseResource(dmabufParams_, params);
}

void WaylandServer::Impl::destroyDmabufBuffer(DmabufBuffer* buffer) {
  for (auto const& surface : surfaces_) {
    if (surface->dmabufBuffer == buffer) {
      surface->dmabufBuffer = nullptr;
      surface->width = 0;
      surface->height = 0;
      surface->rgbaPixels.reset();
      surface->rgbaFullyOpaque = false;
      ++surface->serial;
    }
  }
  for (auto& plane : buffer->planes) {
    if (plane.fd >= 0) close(plane.fd);
    plane.fd = -1;
  }
  eraseResource(dmabufBuffers_, buffer);
}

void WaylandServer::Impl::destroyToplevelDecoration(ToplevelDecoration* decoration) {
  eraseResource(toplevelDecorations_, decoration);
}

void WaylandServer::Impl::destroyXxCutouts(XxCutouts* cutouts) {
  if (cutouts && cutouts->toplevel && cutouts->toplevel->cutouts == cutouts) {
    cutouts->toplevel->cutouts = nullptr;
    cutouts->toplevel->cutoutsRejected = false;
  }
  eraseResource(cutouts_, cutouts);
}

void WaylandServer::Impl::destroyRegion(Region* region) {
  eraseResource(regions_, region);
}

void WaylandServer::Impl::destroyBackgroundEffect(BackgroundEffect* effect) {
  if (effect && effect->surface && effect->surface->backgroundEffect == effect) {
    effect->surface->backgroundEffect = nullptr;
  }
  eraseResource(backgroundEffects_, effect);
}

void WaylandServer::Impl::destroyViewport(Viewport* viewport) {
  if (viewport->surface && viewport->surface->viewport == viewport) {
    viewport->surface->viewport = nullptr;
    viewport->surface->pendingSourceSet = false;
    viewport->surface->pendingSourceX = 0.f;
    viewport->surface->pendingSourceY = 0.f;
    viewport->surface->pendingSourceWidth = 0.f;
    viewport->surface->pendingSourceHeight = 0.f;
    viewport->surface->pendingDestinationSet = false;
    viewport->surface->pendingDestinationWidth = 0;
    viewport->surface->pendingDestinationHeight = 0;
  }
  eraseResource(viewports_, viewport);
}

void WaylandServer::Impl::destroyFractionalScale(FractionalScale* fractionalScale) {
  if (fractionalScale->surface && fractionalScale->surface->fractionalScale == fractionalScale) {
    fractionalScale->surface->fractionalScale = nullptr;
  }
  eraseResource(fractionalScales_, fractionalScale);
}

void WaylandServer::Impl::destroyCursorShapeDevice(CursorShapeDevice* device) {
  eraseResource(cursorShapeDevices_, device);
}

void WaylandServer::Impl::destroyIdleInhibitor(IdleInhibitor* inhibitor) {
  eraseResource(idleInhibitors_, inhibitor);
  std::fprintf(stderr, "flux-compositor: idle inhibitors active=%zu\n", idleInhibitors_.size());
}

void WaylandServer::Impl::destroyLayerSurface(LayerSurface* layerSurface) {
  if (layerSurface && layerSurface->surface && layerSurface->surface->layerSurface == layerSurface) {
    layerSurface->surface->layerSurface = nullptr;
    if (surfaceIsLayerSurface(layerSurface->surface)) layerSurface->surface->role = SurfaceRole::None;
  }
  eraseResource(layerSurfaces_, layerSurface);
}

void WaylandServer::Impl::destroyPresentationFeedback(PresentationFeedback* feedback) {
  if (feedback && feedback->surface) {
    auto eraseFeedback = [feedback](std::vector<PresentationFeedback*>& feedbacks) {
      feedbacks.erase(std::remove(feedbacks.begin(), feedbacks.end(), feedback), feedbacks.end());
    };
    eraseFeedback(feedback->surface->pendingPresentationFeedbacks);
    eraseFeedback(feedback->surface->presentationFeedbacks);
  }
  eraseResource(presentationFeedbacks_, feedback);
}

void WaylandServer::Impl::destroyRelativePointer(RelativePointer* relativePointer) {
  eraseResource(relativePointers_, relativePointer);
}

void WaylandServer::Impl::destroyPointerConstraint(PointerConstraint* constraint) {
  if (constraint && constraint->active && constraint->resource) {
    if (constraint->kind == PointerConstraint::Kind::Lock) {
      zwp_locked_pointer_v1_send_unlocked(constraint->resource);
    } else {
      zwp_confined_pointer_v1_send_unconfined(constraint->resource);
    }
  }
  eraseResource(pointerConstraints_, constraint);
}

void WaylandServer::Impl::destroyPrimarySelectionDevice(PrimarySelectionDevice* device) {
  eraseResource(primarySelectionDevices_, device);
}

void WaylandServer::Impl::destroyPrimarySelectionSource(PrimarySelectionSource* source) {
  if (primarySelectionSource_ == source) {
    primarySelectionSource_ = nullptr;
    sendPrimarySelectionForFocus(this);
  }
  for (auto& offer : primarySelectionOffers_) {
    if (offer->source == source) offer->source = nullptr;
  }
  eraseResource(primarySelectionSources_, source);
}

void WaylandServer::Impl::destroyPrimarySelectionOffer(PrimarySelectionOffer* offer) {
  eraseResource(primarySelectionOffers_, offer);
}

void WaylandServer::Impl::destroyDataDevice(DataDevice* device) {
  if (dndTarget_ && device->resource &&
      wl_resource_get_client(device->resource) == wl_resource_get_client(dndTarget_->resource)) {
    clearDnd(this);
  }
  eraseResource(dataDevices_, device);
}

void WaylandServer::Impl::destroyDataSource(DataSource* source) {
  if (selectionSource_ == source) {
    selectionSource_ = nullptr;
    sendSelectionForFocus(this);
  }
  if (dndSource_ == source) clearDnd(this);
  for (auto& offer : dataOffers_) {
    if (offer->source == source) offer->source = nullptr;
  }
  eraseResource(dataSources_, source);
}

void WaylandServer::Impl::destroyDataOffer(DataOffer* offer) {
  if (dndOffer_ == offer) dndOffer_ = nullptr;
  eraseResource(dataOffers_, offer);
}

void WaylandServer::Impl::destroyActivationToken(ActivationToken* token) {
  eraseResource(activationTokens_, token);
}

} // namespace flux::compositor
