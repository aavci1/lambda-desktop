#pragma once

#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include <cstddef>
#include <memory>
#include <span>

namespace lambdaui::compositor {

struct SelectionDeviceSeatCleanup {
  std::size_t dataDevicesCleared = 0;
  std::size_t primarySelectionDevicesCleared = 0;

  [[nodiscard]] bool changed() const {
    return dataDevicesCleared > 0 || primarySelectionDevicesCleared > 0;
  }
};

[[nodiscard]] inline bool dataDeviceReferencesSeatResource(WaylandServer::Impl::DataDevice const* device,
                                                           wl_resource* seatResource) {
  return seatResource && device && device->seat == seatResource;
}

[[nodiscard]] inline bool primarySelectionDeviceReferencesSeatResource(
    WaylandServer::Impl::PrimarySelectionDevice const* device,
    wl_resource* seatResource) {
  return seatResource && device && device->seat == seatResource;
}

inline bool clearDataDeviceSeatResource(WaylandServer::Impl::DataDevice* device,
                                        wl_resource* seatResource) {
  if (!dataDeviceReferencesSeatResource(device, seatResource)) return false;
  device->seat = nullptr;
  return true;
}

inline bool clearPrimarySelectionDeviceSeatResource(WaylandServer::Impl::PrimarySelectionDevice* device,
                                                   wl_resource* seatResource) {
  if (!primarySelectionDeviceReferencesSeatResource(device, seatResource)) return false;
  device->seat = nullptr;
  return true;
}

inline SelectionDeviceSeatCleanup clearSelectionDeviceSeatResources(
    std::span<std::unique_ptr<WaylandServer::Impl::DataDevice>> dataDevices,
    std::span<std::unique_ptr<WaylandServer::Impl::PrimarySelectionDevice>> primarySelectionDevices,
    wl_resource* seatResource) {
  SelectionDeviceSeatCleanup result;
  if (!seatResource) return result;
  for (auto& device : dataDevices) {
    if (clearDataDeviceSeatResource(device.get(), seatResource)) ++result.dataDevicesCleared;
  }
  for (auto& device : primarySelectionDevices) {
    if (clearPrimarySelectionDeviceSeatResource(device.get(), seatResource)) {
      ++result.primarySelectionDevicesCleared;
    }
  }
  return result;
}

inline SelectionDeviceSeatCleanup clearSelectionDeviceSeatResources(WaylandServer::Impl* server,
                                                                    wl_resource* seatResource) {
  if (!server) return {};
  return clearSelectionDeviceSeatResources(
      std::span<std::unique_ptr<WaylandServer::Impl::DataDevice>>(server->dataDevices_.data(),
                                                                  server->dataDevices_.size()),
      std::span<std::unique_ptr<WaylandServer::Impl::PrimarySelectionDevice>>(
          server->primarySelectionDevices_.data(),
          server->primarySelectionDevices_.size()),
      seatResource);
}

} // namespace lambdaui::compositor
