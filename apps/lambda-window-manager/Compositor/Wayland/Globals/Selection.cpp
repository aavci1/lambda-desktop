#include "Compositor/Wayland/Globals/Selection.hpp"

#include "Compositor/Diagnostics/CrashLog.hpp"
#include "Compositor/Wayland/ResourceTemplates.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "Compositor/Window/WindowManagerInternal.hpp"
#include "primary-selection-unstable-v1-server-protocol.h"

#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include <algorithm>
#include <memory>

namespace lambda::compositor {
namespace {

extern struct zwp_primary_selection_offer_v1_interface const primarySelectionOfferImpl;
extern struct wl_data_offer_interface const dataOfferImpl;

std::uint32_t dataDeviceVersion(WaylandServer::Impl::DataDevice const* device) {
  return device && device->resource
             ? std::min<std::uint32_t>(static_cast<std::uint32_t>(wl_resource_get_version(device->resource)), 3u)
             : 1u;
}

std::uint32_t resourceId(wl_resource* resource) {
  return resource ? wl_resource_get_id(resource) : 0u;
}

void primarySelectionManagerDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void primarySelectionSourceOffer(wl_client*, wl_resource* resource, char const* mimeType) {
  auto* source = resourceData<WaylandServer::Impl::PrimarySelectionSource>(resource);
  if (!source || !mimeType) return;
  if (std::find(source->mimeTypes.begin(), source->mimeTypes.end(), mimeType) == source->mimeTypes.end()) {
    source->mimeTypes.emplace_back(mimeType);
  }
}

void primarySelectionSourceDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

struct zwp_primary_selection_source_v1_interface const primarySelectionSourceImpl{
    .offer = primarySelectionSourceOffer,
    .destroy = primarySelectionSourceDestroy,
};

void primarySelectionDeviceDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void sendPrimarySelectionToDevice(WaylandServer::Impl::PrimarySelectionDevice* device) {
  if (!device || !device->resource) return;
  auto* server = device->server;
  wl_client* deviceClient = wl_resource_get_client(device->resource);
  if (!server->keyboardFocus_ || wl_resource_get_client(server->keyboardFocus_->resource) != deviceClient ||
      !server->primarySelectionSource_ || server->primarySelectionSource_->mimeTypes.empty()) {
    zwp_primary_selection_device_v1_send_selection(device->resource, nullptr);
    return;
  }

  auto offer = std::make_unique<WaylandServer::Impl::PrimarySelectionOffer>();
  offer->server = server;
  offer->source = server->primarySelectionSource_;
  wl_resource* offerResource =
      wl_resource_create(deviceClient, &zwp_primary_selection_offer_v1_interface, 1, 0);
  if (!offerResource) {
    wl_client_post_no_memory(deviceClient);
    return;
  }
  offer->resource = offerResource;
  auto* raw = offer.get();
  server->primarySelectionOffers_.push_back(std::move(offer));
  wl_resource_set_implementation(offerResource, &primarySelectionOfferImpl, raw, destroyResourceCallback<WaylandServer::Impl::PrimarySelectionOffer, WaylandServer::Impl, &WaylandServer::Impl::destroyPrimarySelectionOffer>);
  zwp_primary_selection_device_v1_send_data_offer(device->resource, offerResource);
  for (auto const& mimeType : raw->source->mimeTypes) {
    zwp_primary_selection_offer_v1_send_offer(offerResource, mimeType.c_str());
  }
  zwp_primary_selection_device_v1_send_selection(device->resource, offerResource);
}

void sendPrimarySelectionForFocusImpl(WaylandServer::Impl* server) {
  for (auto const& device : server->primarySelectionDevices_) {
    sendPrimarySelectionToDevice(device.get());
  }
}

void primarySelectionDeviceSetSelection(wl_client*, wl_resource* resource, wl_resource* sourceResource, std::uint32_t) {
  auto* device = resourceData<WaylandServer::Impl::PrimarySelectionDevice>(resource);
  if (!device) return;
  auto* server = device->server;
  auto* source = resourceData<WaylandServer::Impl::PrimarySelectionSource>(sourceResource);
  if (server->primarySelectionSource_ && server->primarySelectionSource_ != source &&
      server->primarySelectionSource_->resource) {
    zwp_primary_selection_source_v1_send_cancelled(server->primarySelectionSource_->resource);
  }
  server->primarySelectionSource_ = source;
  sendPrimarySelectionForFocusImpl(server);
}

struct zwp_primary_selection_device_v1_interface const primarySelectionDeviceImpl{
    .set_selection = primarySelectionDeviceSetSelection,
    .destroy = primarySelectionDeviceDestroy,
};

void primarySelectionOfferReceive(wl_client*, wl_resource* resource, char const* mimeType, int fd) {
  auto* offer = resourceData<WaylandServer::Impl::PrimarySelectionOffer>(resource);
  if (!offer || !offer->source || !offer->source->resource || !mimeType) {
    close(fd);
    return;
  }
  zwp_primary_selection_source_v1_send_send(offer->source->resource, mimeType, fd);
  close(fd);
}

void primarySelectionOfferDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

struct zwp_primary_selection_offer_v1_interface const primarySelectionOfferImpl{
    .receive = primarySelectionOfferReceive,
    .destroy = primarySelectionOfferDestroy,
};

void primarySelectionManagerCreateSource(wl_client* client, wl_resource* resource, std::uint32_t id) {
  auto* server = serverFrom(resource);
  auto source = std::make_unique<WaylandServer::Impl::PrimarySelectionSource>();
  source->server = server;
  wl_resource* sourceResource = wl_resource_create(client, &zwp_primary_selection_source_v1_interface, 1, id);
  if (!sourceResource) {
    wl_client_post_no_memory(client);
    return;
  }
  source->resource = sourceResource;
  auto* raw = source.get();
  server->primarySelectionSources_.push_back(std::move(source));
  wl_resource_set_implementation(sourceResource, &primarySelectionSourceImpl, raw, destroyResourceCallback<WaylandServer::Impl::PrimarySelectionSource, WaylandServer::Impl, &WaylandServer::Impl::destroyPrimarySelectionSource>);
}

void primarySelectionManagerGetDevice(wl_client* client,
                                      wl_resource* resource,
                                      std::uint32_t id,
                                      wl_resource* seatResource) {
  auto* server = serverFrom(resource);
  auto device = std::make_unique<WaylandServer::Impl::PrimarySelectionDevice>();
  device->server = server;
  device->seat = seatResource;
  wl_resource* deviceResource = wl_resource_create(client, &zwp_primary_selection_device_v1_interface, 1, id);
  if (!deviceResource) {
    wl_client_post_no_memory(client);
    return;
  }
  device->resource = deviceResource;
  auto* raw = device.get();
  server->primarySelectionDevices_.push_back(std::move(device));
  wl_resource_set_implementation(deviceResource, &primarySelectionDeviceImpl, raw, destroyResourceCallback<WaylandServer::Impl::PrimarySelectionDevice, WaylandServer::Impl, &WaylandServer::Impl::destroyPrimarySelectionDevice>);
  sendPrimarySelectionToDevice(raw);
}

struct zwp_primary_selection_device_manager_v1_interface const primarySelectionManagerImpl{
    .create_source = primarySelectionManagerCreateSource,
    .get_device = primarySelectionManagerGetDevice,
    .destroy = primarySelectionManagerDestroy,
};

void bindPrimarySelectionManagerImpl(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource =
      wl_resource_create(client, &zwp_primary_selection_device_manager_v1_interface, std::min(version, 1u), id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &primarySelectionManagerImpl, data, nullptr);
}

void dataDeviceManagerDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void dataSourceOffer(wl_client*, wl_resource* resource, char const* mimeType) {
  auto* source = resourceData<WaylandServer::Impl::DataSource>(resource);
  if (!source || !mimeType) return;
  if (std::find(source->mimeTypes.begin(), source->mimeTypes.end(), mimeType) == source->mimeTypes.end()) {
    source->mimeTypes.emplace_back(mimeType);
  }
}

void dataSourceDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

bool validDndActionMask(std::uint32_t actions) {
  constexpr std::uint32_t valid = WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY |
                                  WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE |
                                  WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK;
  return (actions & ~valid) == 0u;
}

bool validPreferredDndAction(std::uint32_t action) {
  return action == WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE ||
         action == WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY ||
         action == WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE ||
         action == WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK;
}

std::uint32_t chooseDndAction(std::uint32_t sourceActions,
                              std::uint32_t targetActions,
                              std::uint32_t preferredAction) {
  std::uint32_t const available = sourceActions & targetActions;
  if (preferredAction != WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE &&
      (available & preferredAction) != 0u) {
    return preferredAction;
  }
  if ((available & WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY) != 0u) return WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
  if ((available & WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE) != 0u) return WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE;
  if ((available & WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK) != 0u) return WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK;
  return WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;
}

void updateDndOfferAction(WaylandServer::Impl::DataOffer* offer) {
  if (!offer || !offer->dnd || !offer->resource || !offer->source) return;
  std::uint32_t const selected =
      chooseDndAction(offer->source->dndActions, offer->dndActions, offer->preferredAction);
  if (selected == offer->selectedAction) return;
  offer->selectedAction = selected;
  if (wl_resource_get_version(offer->resource) >= WL_DATA_OFFER_ACTION_SINCE_VERSION) {
    wl_data_offer_send_action(offer->resource, selected);
  }
  if (offer->source->resource &&
      wl_resource_get_version(offer->source->resource) >= WL_DATA_SOURCE_ACTION_SINCE_VERSION) {
    wl_data_source_send_action(offer->source->resource, selected);
  }
}

void dataSourceSetActions(wl_client*, wl_resource* resource, std::uint32_t actions) {
  auto* source = resourceData<WaylandServer::Impl::DataSource>(resource);
  if (!source) return;
  if (!validDndActionMask(actions)) {
    wl_resource_post_error(resource,
                           WL_DATA_SOURCE_ERROR_INVALID_ACTION_MASK,
                           "invalid data source DnD action mask");
    return;
  }
  source->dndActions = actions;
  if (source->server) {
    for (auto const& offer : source->server->dataOffers_) {
      if (offer->source == source) {
        if (offer->dnd && offer->resource &&
            wl_resource_get_version(offer->resource) >= WL_DATA_OFFER_SOURCE_ACTIONS_SINCE_VERSION) {
          wl_data_offer_send_source_actions(offer->resource, source->dndActions);
        }
        updateDndOfferAction(offer.get());
      }
    }
  }
}

struct wl_data_source_interface const dataSourceImpl{
    .offer = dataSourceOffer,
    .destroy = dataSourceDestroy,
    .set_actions = dataSourceSetActions,
};

void dataOfferAccept(wl_client*, wl_resource* resource, std::uint32_t, char const* mimeType) {
  auto* offer = resourceData<WaylandServer::Impl::DataOffer>(resource);
  if (!offer || !offer->source || !offer->source->resource) return;
  offer->acceptedMimeType = mimeType ? mimeType : "";
  wl_data_source_send_target(offer->source->resource, mimeType);
}

void dataOfferReceive(wl_client*, wl_resource* resource, char const* mimeType, int fd) {
  auto* offer = resourceData<WaylandServer::Impl::DataOffer>(resource);
  if (!offer || !offer->source || !offer->source->resource || !mimeType) {
    close(fd);
    return;
  }
  wl_data_source_send_send(offer->source->resource, mimeType, fd);
  close(fd);
}

void dataOfferDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void dataOfferFinish(wl_client*, wl_resource* resource) {
  auto* offer = resourceData<WaylandServer::Impl::DataOffer>(resource);
  if (!offer || !offer->source || !offer->source->resource) return;
  if (offer->dnd && offer->selectedAction == WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE) {
    if (wl_resource_get_version(offer->source->resource) >= WL_DATA_SOURCE_DND_FINISHED_SINCE_VERSION) {
      wl_data_source_send_cancelled(offer->source->resource);
    }
    return;
  }
  if (wl_resource_get_version(offer->source->resource) >= WL_DATA_SOURCE_DND_FINISHED_SINCE_VERSION) {
    wl_data_source_send_dnd_finished(offer->source->resource);
  }
}
void dataOfferSetActions(wl_client*, wl_resource* resource, std::uint32_t actions, std::uint32_t preferredAction) {
  auto* offer = resourceData<WaylandServer::Impl::DataOffer>(resource);
  if (!offer) return;
  if (!validDndActionMask(actions)) {
    wl_resource_post_error(resource,
                           WL_DATA_OFFER_ERROR_INVALID_ACTION_MASK,
                           "invalid data offer DnD action mask");
    return;
  }
  if (!validPreferredDndAction(preferredAction)) {
    wl_resource_post_error(resource,
                           WL_DATA_OFFER_ERROR_INVALID_ACTION,
                           "invalid preferred DnD action");
    return;
  }
  if (preferredAction != WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE && (actions & preferredAction) == 0u) {
    wl_resource_post_error(resource,
                           WL_DATA_OFFER_ERROR_INVALID_ACTION,
                           "preferred DnD action is not in target action mask");
    return;
  }
  offer->dndActions = actions;
  offer->preferredAction = preferredAction;
  updateDndOfferAction(offer);
}

struct wl_data_offer_interface const dataOfferImpl{
    .accept = dataOfferAccept,
    .receive = dataOfferReceive,
    .destroy = dataOfferDestroy,
    .finish = dataOfferFinish,
    .set_actions = dataOfferSetActions,
};

void sendSelectionToDataDevice(WaylandServer::Impl::DataDevice* device) {
  if (!device || !device->resource) return;
  auto* server = device->server;
  wl_client* deviceClient = wl_resource_get_client(device->resource);
  if (!server->keyboardFocus_ || wl_resource_get_client(server->keyboardFocus_->resource) != deviceClient ||
      !server->selectionSource_ || server->selectionSource_->mimeTypes.empty()) {
    wl_data_device_send_selection(device->resource, nullptr);
    return;
  }

  auto offer = std::make_unique<WaylandServer::Impl::DataOffer>();
  offer->server = server;
  offer->source = server->selectionSource_;
  wl_resource* offerResource = wl_resource_create(deviceClient, &wl_data_offer_interface, dataDeviceVersion(device), 0);
  if (!offerResource) {
    wl_client_post_no_memory(deviceClient);
    return;
  }
  offer->resource = offerResource;
  auto* raw = offer.get();
  server->dataOffers_.push_back(std::move(offer));
  wl_resource_set_implementation(offerResource, &dataOfferImpl, raw, destroyResourceCallback<WaylandServer::Impl::DataOffer, WaylandServer::Impl, &WaylandServer::Impl::destroyDataOffer>);
  wl_data_device_send_data_offer(device->resource, offerResource);
  for (auto const& mimeType : raw->source->mimeTypes) {
    wl_data_offer_send_offer(offerResource, mimeType.c_str());
  }
  wl_data_device_send_selection(device->resource, offerResource);
  diagnostics::crashLog("data-selection-send device=%u offer=%u source=%u mime_count=%zu",
                        resourceId(device->resource),
                        resourceId(offerResource),
                        resourceId(raw->source ? raw->source->resource : nullptr),
                        raw->source ? raw->source->mimeTypes.size() : 0u);
}

void sendSelectionForFocusImpl(WaylandServer::Impl* server) {
  for (auto const& device : server->dataDevices_) {
    sendSelectionToDataDevice(device.get());
  }
}

WaylandServer::Impl::DataDevice* dataDeviceForClientImpl(WaylandServer::Impl* server, wl_client* client) {
  for (auto const& device : server->dataDevices_) {
    if (device->resource && wl_resource_get_client(device->resource) == client) return device.get();
  }
  return nullptr;
}

void clearDndImpl(WaylandServer::Impl* server, bool destroyOffer = true) {
  if (server->dndTarget_) {
    if (auto* device = dataDeviceForClientImpl(server, wl_resource_get_client(server->dndTarget_->resource))) {
      wl_data_device_send_leave(device->resource);
    }
  }
  if (destroyOffer && server->dndOffer_ && server->dndOffer_->resource) {
    wl_resource_destroy(server->dndOffer_->resource);
  }
  server->dndSource_ = nullptr;
  server->dndOrigin_ = nullptr;
  server->dndTarget_ = nullptr;
  server->dndOffer_ = nullptr;
}

WaylandServer::Impl::DataOffer* createDndOffer(WaylandServer::Impl* server,
                                               wl_client* client,
                                               std::uint32_t version) {
  if (!server->dndSource_ || server->dndSource_->mimeTypes.empty()) return nullptr;
  auto offer = std::make_unique<WaylandServer::Impl::DataOffer>();
  offer->server = server;
  offer->source = server->dndSource_;
  offer->dnd = true;
  wl_resource* offerResource = wl_resource_create(client, &wl_data_offer_interface, version, 0);
  if (!offerResource) {
    wl_client_post_no_memory(client);
    return nullptr;
  }
  offer->resource = offerResource;
  auto* raw = offer.get();
  server->dataOffers_.push_back(std::move(offer));
  wl_resource_set_implementation(offerResource, &dataOfferImpl, raw, destroyResourceCallback<WaylandServer::Impl::DataOffer, WaylandServer::Impl, &WaylandServer::Impl::destroyDataOffer>);
  if (wl_resource_get_version(offerResource) >= WL_DATA_OFFER_SOURCE_ACTIONS_SINCE_VERSION) {
    wl_data_offer_send_source_actions(offerResource, server->dndSource_->dndActions);
  }
  updateDndOfferAction(raw);
  return raw;
}

void updateDndTargetImpl(WaylandServer::Impl* server, WaylandServer::Impl::Surface* target, std::uint32_t timeMs) {
  if (!server->dndSource_) return;
  if (target == server->dndOrigin_) target = nullptr;
  if (target != server->dndTarget_) {
    if (server->dndTarget_) {
      if (auto* previousDevice = dataDeviceForClientImpl(server, wl_resource_get_client(server->dndTarget_->resource))) {
        wl_data_device_send_leave(previousDevice->resource);
      }
    }
    if (server->dndOffer_ && server->dndOffer_->resource) {
      wl_resource_destroy(server->dndOffer_->resource);
    }
    server->dndTarget_ = target;
    server->dndOffer_ = nullptr;
    if (target) {
      wl_client* targetClient = wl_resource_get_client(target->resource);
      WaylandServer::Impl::DataDevice* targetDevice = dataDeviceForClientImpl(server, targetClient);
      if (targetDevice) {
        server->dndOffer_ = createDndOffer(server, targetClient, dataDeviceVersion(targetDevice));
        wl_resource* offerResource = server->dndOffer_ ? server->dndOffer_->resource : nullptr;
        if (offerResource) {
          wl_data_device_send_data_offer(targetDevice->resource, offerResource);
          for (auto const& mimeType : server->dndOffer_->source->mimeTypes) {
            wl_data_offer_send_offer(offerResource, mimeType.c_str());
          }
        }
        std::uint32_t const serial = issueSeatSerialForSurface(server, SeatSerialKind::DataDeviceEnter, target);
        wl_fixed_t const x = wl_fixed_from_double(wm::surfaceLocalX(target, server->pointerX_));
        wl_fixed_t const y = wl_fixed_from_double(wm::surfaceLocalY(target, server->pointerY_));
        wl_data_device_send_enter(targetDevice->resource, serial, target->resource, x, y, offerResource);
      }
    }
  }
  if (server->dndTarget_) {
    if (auto* device = dataDeviceForClientImpl(server, wl_resource_get_client(server->dndTarget_->resource))) {
      wl_fixed_t const x = wl_fixed_from_double(wm::surfaceLocalX(server->dndTarget_, server->pointerX_));
      wl_fixed_t const y = wl_fixed_from_double(wm::surfaceLocalY(server->dndTarget_, server->pointerY_));
      wl_data_device_send_motion(device->resource, timeMs, x, y);
      updateDndOfferAction(server->dndOffer_);
    }
  }
}

void dataDeviceStartDrag(wl_client* client,
                         wl_resource* resource,
                         wl_resource* sourceResource,
                         wl_resource* originResource,
                         wl_resource*,
                         std::uint32_t) {
  auto* device = resourceData<WaylandServer::Impl::DataDevice>(resource);
  if (!device) return;
  auto* server = device->server;
  auto* source = resourceData<WaylandServer::Impl::DataSource>(sourceResource);
  auto* origin = resourceData<WaylandServer::Impl::Surface>(originResource);
  if (!origin || wl_resource_get_client(origin->resource) != client) return;
  if (server->dndSource_) clearDndImpl(server);
  server->dndSource_ = source;
  server->dndOrigin_ = origin;
  if (source && source->resource &&
      wl_resource_get_version(source->resource) >= WL_DATA_SOURCE_ACTION_SINCE_VERSION) {
    wl_data_source_send_action(source->resource, WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE);
  }
  updateDndTargetImpl(server, surfaceAt(server, server->pointerX_, server->pointerY_), 0);
}

void dataDeviceSetSelection(wl_client*, wl_resource* resource, wl_resource* sourceResource, std::uint32_t) {
  auto* device = resourceData<WaylandServer::Impl::DataDevice>(resource);
  if (!device) return;
  auto* server = device->server;
  auto* source = resourceData<WaylandServer::Impl::DataSource>(sourceResource);
  diagnostics::crashLog("data-set-selection device=%u source=%u old_source=%u",
                        resourceId(resource),
                        resourceId(sourceResource),
                        resourceId(server->selectionSource_ ? server->selectionSource_->resource : nullptr));
  if (server->selectionSource_ && server->selectionSource_ != source && server->selectionSource_->resource) {
    wl_data_source_send_cancelled(server->selectionSource_->resource);
  }
  server->selectionSource_ = source;
  sendSelectionForFocusImpl(server);
}

void dataDeviceRelease(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

struct wl_data_device_interface const dataDeviceImpl{
    .start_drag = dataDeviceStartDrag,
    .set_selection = dataDeviceSetSelection,
    .release = dataDeviceRelease,
};

void dataDeviceManagerCreateDataSource(wl_client* client, wl_resource* resource, std::uint32_t id) {
  auto* server = serverFrom(resource);
  auto source = std::make_unique<WaylandServer::Impl::DataSource>();
  source->server = server;
  std::uint32_t const version =
      std::min<std::uint32_t>(static_cast<std::uint32_t>(wl_resource_get_version(resource)), 3u);
  wl_resource* sourceResource = wl_resource_create(client, &wl_data_source_interface, version, id);
  if (!sourceResource) {
    wl_client_post_no_memory(client);
    return;
  }
  source->resource = sourceResource;
  auto* raw = source.get();
  server->dataSources_.push_back(std::move(source));
  wl_resource_set_implementation(sourceResource, &dataSourceImpl, raw, destroyResourceCallback<WaylandServer::Impl::DataSource, WaylandServer::Impl, &WaylandServer::Impl::destroyDataSource>);
}

void dataDeviceManagerGetDataDevice(wl_client* client, wl_resource* resource, std::uint32_t id, wl_resource* seat) {
  auto* server = serverFrom(resource);
  auto device = std::make_unique<WaylandServer::Impl::DataDevice>();
  device->server = server;
  device->seat = seat;
  std::uint32_t const version =
      std::min<std::uint32_t>(static_cast<std::uint32_t>(wl_resource_get_version(resource)), 3u);
  wl_resource* deviceResource = wl_resource_create(client, &wl_data_device_interface, version, id);
  if (!deviceResource) {
    wl_client_post_no_memory(client);
    return;
  }
  device->resource = deviceResource;
  auto* raw = device.get();
  server->dataDevices_.push_back(std::move(device));
  wl_resource_set_implementation(deviceResource, &dataDeviceImpl, raw, destroyResourceCallback<WaylandServer::Impl::DataDevice, WaylandServer::Impl, &WaylandServer::Impl::destroyDataDevice>);
  sendSelectionToDataDevice(raw);
}

struct wl_data_device_manager_interface const dataDeviceManagerImpl{
    .create_data_source = dataDeviceManagerCreateDataSource,
    .get_data_device = dataDeviceManagerGetDataDevice,
    .release = dataDeviceManagerDestroy,
};

void bindDataDeviceManagerImpl(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource = wl_resource_create(client, &wl_data_device_manager_interface, std::min(version, 3u), id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &dataDeviceManagerImpl, data, nullptr);
}


} // namespace

void sendPrimarySelectionForFocus(WaylandServer::Impl* server) {
  sendPrimarySelectionForFocusImpl(server);
}

void sendSelectionForFocus(WaylandServer::Impl* server) {
  sendSelectionForFocusImpl(server);
}

WaylandServer::Impl::DataDevice* dataDeviceForClient(WaylandServer::Impl* server, wl_client* client) {
  return dataDeviceForClientImpl(server, client);
}

void clearDnd(WaylandServer::Impl* server, bool destroyOffer) {
  clearDndImpl(server, destroyOffer);
}

void updateDndTarget(WaylandServer::Impl* server, WaylandServer::Impl::Surface* target, std::uint32_t timeMs) {
  updateDndTargetImpl(server, target, timeMs);
}

void bindPrimarySelectionManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  bindPrimarySelectionManagerImpl(client, data, version, id);
}

void bindDataDeviceManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  bindDataDeviceManagerImpl(client, data, version, id);
}

} // namespace lambda::compositor
