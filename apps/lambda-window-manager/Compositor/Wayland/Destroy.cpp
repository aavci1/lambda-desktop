#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "Compositor/Wayland/DataDeviceDndState.hpp"
#include "Compositor/Wayland/DecorationState.hpp"
#include "Compositor/Wayland/ResourceTemplates.hpp"
#include "Compositor/Wayland/SeatFocusState.hpp"
#include "Compositor/Wayland/XdgPopupState.hpp"
#include "Compositor/Diagnostics/CrashLog.hpp"
#include "Compositor/Window/WindowManagerInternal.hpp"
#include "pointer-constraints-unstable-v1-server-protocol.h"
#include "presentation-time-server-protocol.h"

#include <sys/mman.h>
#include <unistd.h>
#include <wayland-server-core.h>

#include <algorithm>
#include <vector>

namespace lambda::compositor {

void releaseOrDeferBufferRelease(WaylandServer::Impl* server, wl_resource* buffer) {
  if (!server || !buffer) return;
  if (server->bufferReleaseIsRetained(buffer)) {
    server->queueOrphanedBufferRelease(buffer);
    return;
  }
  wl_buffer_send_release(buffer);
}

void detachPointerButtonGrabSurface(WaylandServer::Impl* server, WaylandServer::Impl::Surface const* surface) {
  if (!server || !surface || server->pointerButtonGrabSurface_ != surface) return;
  wm::popupTrace("lambda-window-manager: pointer button grab surface detached surface=%llu client=%p count=%u\n",
                 static_cast<unsigned long long>(surface->id),
                 static_cast<void*>(server->pointerButtonGrabClient_),
                 server->pointerButtonCount_);
  server->pointerButtonGrabSurface_ = nullptr;
  server->pointerButtonGrabClient_ = nullptr;
  server->pointerButtonCount_ = 0;
}

bool resetXdgPopupRole(WaylandServer::Impl* server,
                       WaylandServer::Impl::XdgPopup* popup,
                       bool sendPopupDone) {
  if (!server || !popup) return false;

  WaylandServer::Impl::Surface* surface = popup->xdgSurface ? popup->xdgSurface->surface : nullptr;
  while (surface) {
    auto child = std::find_if(server->popups_.begin(),
                              server->popups_.end(),
                              [popup, surface](auto const& candidate) {
                                return candidate && candidate.get() != popup &&
                                       candidate->parentSurface == surface;
                              });
    if (child == server->popups_.end()) break;
    resetXdgPopupRole(server, child->get(), true);
  }

  diagnostics::crashLog("xdg-popup-reset resource=%u surface=%llu send_done=%u",
                        popup->resource ? wl_resource_get_id(popup->resource) : 0u,
                        static_cast<unsigned long long>(surface ? surface->id : 0),
                        sendPopupDone ? 1u : 0u);

  if (xdgPopupGrabContains(server->popupGrab_, popup)) releasePopupGrab(server, popup, 0);
  if (server->grabPopup_ == popup) xdgPopupGrabSyncTop(server->popupGrab_, server->grabPopup_);
  if (surface) {
    detachPointerButtonGrabSurface(server, surface);
    SurfaceSeatCleanupResult const seatCleanup = clearUnmappedSurfaceSeatState(server, surface);
    if (seatCleanup.pointerFocusChanged) updatePointerConstraintsForFocus(server);
    if (surface->xdgPopup == popup) surface->xdgPopup = nullptr;
    if (surfaceIsXdgPopup(surface)) surface->role = SurfaceRole::None;
  }
  for (auto& child : server->popups_) {
    if (child && child->parentSurface == surface) child->parentSurface = nullptr;
  }
  if (popup->resource) {
    if (sendPopupDone) xdg_popup_send_popup_done(popup->resource);
    wl_resource_set_user_data(popup->resource, nullptr);
  }
  eraseResource(server->popups_, popup);
  return true;
}

bool resetXdgPopupsParentedBySurface(WaylandServer::Impl* server,
                                     WaylandServer::Impl::Surface* parentSurface,
                                     bool sendPopupDone) {
  if (!server || !parentSurface) return false;

  bool changed = false;
  while (true) {
    auto child = std::find_if(server->popups_.begin(),
                              server->popups_.end(),
                              [parentSurface](auto const& popup) {
                                return xdgPopupReferencesParentSurface(popup.get(), parentSurface);
                              });
    if (child == server->popups_.end()) break;
    changed = resetXdgPopupRole(server, child->get(), sendPopupDone) || changed;
  }
  return changed;
}

void resetSubsurfaceRole(WaylandServer::Impl* server, WaylandServer::Impl::Subsurface* subsurface) {
  if (!server || !subsurface) return;
  if (subsurface->surface && subsurface->surface->subsurfaceRole == subsurface) {
    subsurface->surface->subsurfaceRole = nullptr;
    if (surfaceIsSubsurface(subsurface->surface)) subsurface->surface->role = SurfaceRole::None;
  }
  if (subsurface->resource) wl_resource_set_user_data(subsurface->resource, nullptr);
  eraseResource(server->subsurfaces_, subsurface);
}

void resetViewportRole(WaylandServer::Impl* server, WaylandServer::Impl::Viewport* viewport) {
  if (!server || !viewport) return;
  if (viewport->surface && viewport->surface->viewport == viewport) {
    viewport->surface->viewport = nullptr;
    viewport->surface->pendingViewportState.sourceSet = false;
    viewport->surface->pendingViewportState.sourceX = 0.f;
    viewport->surface->pendingViewportState.sourceY = 0.f;
    viewport->surface->pendingViewportState.sourceWidth = 0.f;
    viewport->surface->pendingViewportState.sourceHeight = 0.f;
    viewport->surface->pendingViewportState.destinationSet = false;
    viewport->surface->pendingViewportState.destinationWidth = 0;
    viewport->surface->pendingViewportState.destinationHeight = 0;
  }
  if (viewport->resource) wl_resource_set_user_data(viewport->resource, nullptr);
  eraseResource(server->viewports_, viewport);
}

void resetFractionalScaleRole(WaylandServer::Impl* server, WaylandServer::Impl::FractionalScale* fractionalScale) {
  if (!server || !fractionalScale) return;
  if (fractionalScale->surface && fractionalScale->surface->fractionalScale == fractionalScale) {
    fractionalScale->surface->fractionalScale = nullptr;
  }
  if (fractionalScale->resource) wl_resource_set_user_data(fractionalScale->resource, nullptr);
  eraseResource(server->fractionalScales_, fractionalScale);
}

void resetLayerSurfaceRole(WaylandServer::Impl* server, WaylandServer::Impl::LayerSurface* layerSurface) {
  if (!server || !layerSurface) return;
  refreshShellReservedZones(server);
  SurfaceSeatCleanupResult const seatCleanup =
      clearUnmappedSurfaceSeatState(server, layerSurface->surface);
  if (seatCleanup.pointerFocusChanged) updatePointerConstraintsForFocus(server);
  if (layerSurface->surface && layerSurface->surface->layerSurface == layerSurface) {
    layerSurface->surface->layerSurface = nullptr;
    if (surfaceIsLayerSurface(layerSurface->surface)) layerSurface->surface->role = SurfaceRole::None;
  }
  if (layerSurface->resource) wl_resource_set_user_data(layerSurface->resource, nullptr);
  eraseResource(server->layerSurfaces_, layerSurface);
}

void resetToplevelDecoration(WaylandServer::Impl* server, WaylandServer::Impl::ToplevelDecoration* decoration) {
  if (!server || !decoration) return;
  if (decoration->resource) wl_resource_set_user_data(decoration->resource, nullptr);
  eraseResource(server->toplevelDecorations_, decoration);
}

void resetXxCutouts(WaylandServer::Impl* server, WaylandServer::Impl::XxCutouts* cutouts) {
  if (!server || !cutouts) return;
  if (cutouts->toplevel && cutouts->toplevel->cutouts == cutouts) {
    cutouts->toplevel->cutouts = nullptr;
    cutouts->toplevel->cutoutsRejected = false;
  }
  if (cutouts->resource) wl_resource_set_user_data(cutouts->resource, nullptr);
  eraseResource(server->cutouts_, cutouts);
}

void WaylandServer::Impl::destroySurface(Surface* surface) {
  if (surface) {
    diagnostics::crashLog("surface-destroy surface=%llu resource=%u role=%u commits=%llu buffer=%dx%d frame=%dx%d",
                          static_cast<unsigned long long>(surface->id),
                          surface->resource ? wl_resource_get_id(surface->resource) : 0u,
                          static_cast<unsigned int>(surface->role),
                          static_cast<unsigned long long>(surface->commitCount),
                          surface->width,
                          surface->height,
                          surface->frameWidth,
                          surface->frameHeight);
  }
  bool const hadToplevel = surfaceIsXdgToplevel(surface);
  bool const activatePrevious = keyboardFocus_ == surface && hadToplevel;
  wm::clearFocusCycle(this);
  removeSurfaceFromFocusOrder(this, surface);
  if (pointerFocus_ == surface) pointerFocus_ = nullptr;
  detachPointerButtonGrabSurface(this, surface);
  if (keyboardFocus_ == surface) keyboardFocus_ = nullptr;
  if (dragSurface_ == surface) {
    dragSurface_ = nullptr;
    dragSnapTarget_.reset();
    dragSnapTargetStartedAtMs_ = 0;
  }
  if (snapPreviewSurfaceId_ == surface->id) {
    snapPreviewVisible_ = false;
    snapPreviewDropPending_ = false;
    snapPreviewSurfaceId_ = 0;
    snapPreviewStartedAtMs_ = 0;
    snapPreviewStartWindow_ = {};
    snapPreviewTargetWindow_ = {};
  }
  if (resizeSurface_ == surface) resizeSurface_ = nullptr;
  if (closePressSurface_ == surface) closePressSurface_ = nullptr;
  if (maximizePressSurface_ == surface) maximizePressSurface_ = nullptr;
  if (minimizePressSurface_ == surface) minimizePressSurface_ = nullptr;
  if (lastTitleClickSurface_ == surface) lastTitleClickSurface_ = nullptr;
  if (cursorSurface_ == surface) cursorSurface_ = nullptr;
  if (lastActivationSurface_ == surface) lastActivationSurface_ = nullptr;
  for (auto& token : activationTokens_) {
    if (token->surface == surface) token->surface = nullptr;
  }
  if (dndIcon_ == surface) dndIcon_ = nullptr;

  resetXdgPopupsParentedBySurface(this, surface, true);
  while (true) {
    auto ownPopup = std::find_if(popups_.begin(), popups_.end(), [surface](auto const& popup) {
      return popup && popup->xdgSurface && popup->xdgSurface->surface == surface;
    });
    if (ownPopup == popups_.end()) break;
    resetXdgPopupRole(this, ownPopup->get(), false);
  }
  for (auto& xdgSurface : xdgSurfaces_) {
    if (xdgSurface && xdgSurface->surface == surface) xdgSurface->surface = nullptr;
  }
  for (auto it = subsurfaces_.begin(); it != subsurfaces_.end();) {
    if ((*it)->surface == surface || (*it)->parent == surface) {
      resetSubsurfaceRole(this, it->get());
      it = subsurfaces_.begin();
    } else {
      ++it;
    }
  }
  if (dndOrigin_ == surface || dndTarget_ == surface) clearDnd(this);
  if (surface->viewport) resetViewportRole(this, surface->viewport);
  if (surface->fractionalScale) resetFractionalScaleRole(this, surface->fractionalScale);
  if (surface->layerSurface) resetLayerSurfaceRole(this, surface->layerSurface);
  surface->xdgPopup = nullptr;
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
  if (surface->cachedSubsurfaceCommit) {
    std::vector<PresentationFeedback*> cachedFeedbacks =
        std::move(surface->cachedSubsurfaceCommit->pendingPresentationFeedbacks);
    for (auto* feedback : cachedFeedbacks) {
      if (!feedback || !feedback->resource) continue;
      wp_presentation_feedback_send_discarded(feedback->resource);
      wl_resource_destroy(feedback->resource);
    }
    for (wl_resource* callback : surface->cachedSubsurfaceCommit->pendingFrameCallbacks) {
      wl_resource_destroy(callback);
    }
    surface->cachedSubsurfaceCommit.reset();
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
  for (wl_resource* callback : surface->pendingFrameCallbacks) {
    wl_resource_destroy(callback);
  }
  surface->pendingFrameCallbacks.clear();
  for (wl_resource* buffer : surface->pendingBufferReleases) {
    releaseOrDeferBufferRelease(this, buffer);
  }
  surface->pendingBufferReleases.clear();
  if (surface->bufferState.buffer && surface->dmabufBuffer) {
    releaseOrDeferBufferRelease(this, surface->bufferState.buffer);
  }
  clearSeatSerialsForSurface(this, surface);
  eraseResource(surfaces_, surface);
  if (activatePrevious) activateMostRecentToplevel(this, 0);
  if (hadToplevel) notifyShellStateChanged();
}

void WaylandServer::Impl::destroySubsurface(Subsurface* subsurface) {
  resetSubsurfaceRole(this, subsurface);
}

void WaylandServer::Impl::destroyXdgSurface(XdgSurface* surface) {
  if (surface) {
    diagnostics::crashLog("xdg-surface-destroy resource=%u surface=%llu",
                          surface->resource ? wl_resource_get_id(surface->resource) : 0u,
                          static_cast<unsigned long long>(surface->surface ? surface->surface->id : 0));
    while (true) {
      auto popup = std::find_if(popups_.begin(), popups_.end(), [surface](auto const& candidate) {
        return candidate && candidate->xdgSurface == surface;
      });
      if (popup == popups_.end()) break;
      resetXdgPopupRole(this, popup->get(), false);
    }
    for (auto& toplevel : toplevels_) {
      if (toplevel && toplevel->xdgSurface == surface) toplevel->xdgSurface = nullptr;
    }
    for (auto& popup : popups_) {
      if (popup && popup->xdgSurface == surface) popup->xdgSurface = nullptr;
    }
    if (surfaceIsXdgSurfaceBase(surface->surface)) {
      surface->surface->role = SurfaceRole::None;
    }
  }
  eraseResource(xdgSurfaces_, surface);
}

void WaylandServer::Impl::destroyXdgPositioner(XdgPositioner* positioner) {
  eraseResource(xdgPositioners_, positioner);
}

void WaylandServer::Impl::destroyXdgToplevel(XdgToplevel* toplevel) {
  Surface* surface = toplevel && toplevel->xdgSurface ? toplevel->xdgSurface->surface : nullptr;
  bool const hadToplevel = surfaceIsXdgToplevel(surface);
  bool const activatePrevious = keyboardFocus_ == surface && surfaceIsXdgToplevel(surface);
  resetXdgPopupsParentedBySurface(this, surface, true);
  if (surfaceIsXdgToplevel(surface)) {
    wm::clearFocusCycle(this);
    detachPointerButtonGrabSurface(this, surface);
    SurfaceSeatCleanupResult const seatCleanup = clearUnmappedSurfaceSeatState(this, surface);
    if (seatCleanup.pointerFocusChanged) updatePointerConstraintsForFocus(this);
    surface->role = SurfaceRole::None;
  }
  while (auto* decoration = decorationFor(this, toplevel)) {
    resetToplevelDecoration(this, decoration);
  }
  while (auto* cutouts = cutoutsFor(this, toplevel)) {
    if (shouldReportDefunctCutoutsOnToplevelDestroy(cutouts->resource != nullptr)) {
      wl_resource_post_error(cutouts->resource,
                             XX_CUTOUTS_MANAGER_V1_ERROR_DEFUNCT_CUTOUTS_OBJECT,
                             "xx_cutouts_v1 must be destroyed before its xdg_toplevel");
    }
    resetXxCutouts(this, cutouts);
  }
  for (auto& child : toplevels_) {
    if (child && child.get() != toplevel && child->parent == toplevel) {
      child->parent = toplevel->parent;
    }
  }
  eraseResource(toplevels_, toplevel);
  if (hadToplevel) {
    notifyShellStateChanged();
  }
  if (activatePrevious) activateMostRecentToplevel(this, 0);
}

void WaylandServer::Impl::destroyXdgPopup(XdgPopup* popup) {
  resetXdgPopupRole(this, popup, false);
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
  if (buffer && buffer->resource) {
    for (auto const& surface : surfaces_) {
      if (!surface) continue;
      if (surface->pendingBufferState.buffer == buffer->resource) {
        surface->pendingBufferState.buffer = nullptr;
        surface->pendingBufferState.bufferAttached = false;
      }
      if (surface->cachedSubsurfaceCommit &&
          surface->cachedSubsurfaceCommit->bufferState.buffer == buffer->resource) {
        surface->cachedSubsurfaceCommit->bufferState.buffer = nullptr;
        surface->cachedSubsurfaceCommit->bufferState.bufferAttached = false;
      }
      if (surface->bufferState.buffer == buffer->resource) {
        surface->bufferState.buffer = nullptr;
        surface->shmPixels = nullptr;
        surface->shmPixelBytes = 0;
        surface->rgbaPixels.reset();
        surface->rgbaFullyOpaque = false;
      }
      auto& releases = surface->pendingBufferReleases;
      releases.erase(std::remove(releases.begin(), releases.end(), buffer->resource), releases.end());
    }
  }
  orphanedBufferReleases_.erase(std::remove(orphanedBufferReleases_.begin(),
                                            orphanedBufferReleases_.end(),
                                            buffer->resource),
                                orphanedBufferReleases_.end());
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
  if (buffer) {
    diagnostics::crashLog("dmabuf-destroy id=%llu size=%dx%d format=0x%08x",
                          static_cast<unsigned long long>(buffer->id),
                          buffer->width,
                          buffer->height,
                          buffer->format);
  }
  wl_resource* const bufferResource = buffer ? buffer->resource : nullptr;
  for (auto const& surface : surfaces_) {
    if (!surface) continue;
    if (bufferResource) {
      if (surface->pendingBufferState.buffer == bufferResource) {
        surface->pendingBufferState.buffer = nullptr;
        surface->pendingBufferState.bufferAttached = false;
      }
      if (surface->cachedSubsurfaceCommit &&
          surface->cachedSubsurfaceCommit->bufferState.buffer == bufferResource) {
        surface->cachedSubsurfaceCommit->bufferState.buffer = nullptr;
        surface->cachedSubsurfaceCommit->bufferState.bufferAttached = false;
      }
      if (surface->bufferState.buffer == bufferResource) {
        surface->bufferState.buffer = nullptr;
      }
      auto& releases = surface->pendingBufferReleases;
      releases.erase(std::remove(releases.begin(), releases.end(), bufferResource), releases.end());
    }
    if (surface->dmabufBuffer == buffer) {
      surface->dmabufBuffer = nullptr;
      surface->width = 0;
      surface->height = 0;
      surface->rgbaPixels.reset();
      surface->shmPixels = nullptr;
      surface->shmPixelBytes = 0;
      surface->rgbaFullyOpaque = false;
      ++surface->serial;
    }
  }
  for (auto& plane : buffer->planes) {
    if (plane.fd >= 0) close(plane.fd);
    plane.fd = -1;
  }
  if (bufferResource) {
    orphanedBufferReleases_.erase(std::remove(orphanedBufferReleases_.begin(),
                                              orphanedBufferReleases_.end(),
                                              bufferResource),
                                  orphanedBufferReleases_.end());
  }
  eraseResource(dmabufBuffers_, buffer);
}

void WaylandServer::Impl::destroyToplevelDecoration(ToplevelDecoration* decoration) {
  resetToplevelDecoration(this, decoration);
}

void WaylandServer::Impl::destroyXxCutouts(XxCutouts* cutouts) {
  resetXxCutouts(this, cutouts);
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
  resetViewportRole(this, viewport);
}

void WaylandServer::Impl::destroyFractionalScale(FractionalScale* fractionalScale) {
  resetFractionalScaleRole(this, fractionalScale);
}

void WaylandServer::Impl::destroyCursorShapeDevice(CursorShapeDevice* device) {
  eraseResource(cursorShapeDevices_, device);
}

void WaylandServer::Impl::destroyIdleInhibitor(IdleInhibitor* inhibitor) {
  eraseResource(idleInhibitors_, inhibitor);
}

void WaylandServer::Impl::destroyXdgOutput(XdgOutput* output) {
  eraseResource(xdgOutputs_, output);
}

void WaylandServer::Impl::destroyLayerSurface(LayerSurface* layerSurface) {
  resetLayerSurfaceRole(this, layerSurface);
}

void WaylandServer::Impl::destroyPresentationFeedback(PresentationFeedback* feedback) {
  if (feedback && feedback->surface) {
    auto eraseFeedback = [feedback](std::vector<PresentationFeedback*>& feedbacks) {
      feedbacks.erase(std::remove(feedbacks.begin(), feedbacks.end(), feedback), feedbacks.end());
    };
    eraseFeedback(feedback->surface->pendingPresentationFeedbacks);
    eraseFeedback(feedback->surface->presentationFeedbacks);
    if (feedback->surface->cachedSubsurfaceCommit) {
      eraseFeedback(feedback->surface->cachedSubsurfaceCommit->pendingPresentationFeedbacks);
    }
  }
  if (feedback) {
    for (auto& batch : pendingPresentationBatches_) {
      batch.feedbacks.erase(std::remove(batch.feedbacks.begin(), batch.feedbacks.end(), feedback),
                            batch.feedbacks.end());
    }
    pendingPresentationBatches_.erase(
        std::remove_if(pendingPresentationBatches_.begin(),
                       pendingPresentationBatches_.end(),
                       [](PendingPresentationBatch const& batch) { return batch.feedbacks.empty(); }),
        pendingPresentationBatches_.end());
  }
  eraseResource(presentationFeedbacks_, feedback);
}

void WaylandServer::Impl::destroyRelativePointer(RelativePointer* relativePointer) {
  eraseResource(relativePointers_, relativePointer);
}

void WaylandServer::Impl::destroyPointerConstraint(PointerConstraint* constraint) {
  if (!constraint) return;
  auto removeConstraintState = [constraint](std::vector<PointerConstraintCommitState>& states) {
    states.erase(std::remove_if(states.begin(),
                                states.end(),
                                [constraint](PointerConstraintCommitState const& state) {
                                  return state.constraint == constraint;
                                }),
                 states.end());
  };
  for (auto& surface : surfaces_) {
    removeConstraintState(surface->pendingPointerConstraintStates);
    if (surface->cachedSubsurfaceCommit) {
      removeConstraintState(surface->cachedSubsurfaceCommit->pointerConstraintStates);
    }
  }
  if (constraint->active && constraint->resource) {
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
  if (dndSource_ == source) clearDnd(this, true, true, false);
  for (auto& offer : dataOffers_) {
    if (offer->source == source) offer->source = nullptr;
  }
  eraseResource(dataSources_, source);
}

void WaylandServer::Impl::destroyDataOffer(DataOffer* offer) {
  if (offer &&
      dataOfferShouldCancelSourceOnDestroy(offer->dnd, offer->dropPerformed, offer->finished) &&
      offer->source &&
      offer->source->resource) {
    wl_data_source_send_cancelled(offer->source->resource);
  }
  if (dndOffer_ == offer) dndOffer_ = nullptr;
  eraseResource(dataOffers_, offer);
}

void WaylandServer::Impl::destroyActivationToken(ActivationToken* token) {
  if (!token) return;
  if (token->resource) {
    wl_resource_set_user_data(token->resource, nullptr);
    token->resource = nullptr;
  }
  if (token->timeout) {
    wl_event_source_remove(token->timeout);
    token->timeout = nullptr;
  }
  eraseResource(activationTokens_, token);
}

} // namespace lambda::compositor
