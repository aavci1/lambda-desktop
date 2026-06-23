#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include <wayland-server-core.h>

#include <algorithm>

namespace lambdaui::compositor {
namespace {

constexpr std::size_t kMaxSeatSerialRecords = 64;

bool kindAllowed(SeatSerialKind kind, std::span<SeatSerialKind const> allowedKinds) {
  return std::ranges::find(allowedKinds, kind) != allowedKinds.end();
}

wl_client* surfaceClient(WaylandServer::Impl::Surface const* surface) {
  return surface && surface->resource ? wl_resource_get_client(surface->resource) : nullptr;
}

} // namespace

std::uint32_t issueSeatSerial(WaylandServer::Impl* server,
                              SeatSerialKind kind,
                              wl_client* client,
                              WaylandServer::Impl::Surface* surface) {
  if (!server) return 0;
  return issueSeatSerial(server->nextInputSerial_, server->seatSerials_, kind, client, surface);
}

std::uint32_t issueSeatSerial(std::uint32_t& nextSerial,
                              std::deque<WaylandServer::Impl::SeatSerialRecord>& records,
                              SeatSerialKind kind,
                              wl_client* client,
                              WaylandServer::Impl::Surface* surface) {
  std::uint32_t serial = nextSerial++;
  if (serial == 0) serial = nextSerial++;
  records.push_back(WaylandServer::Impl::SeatSerialRecord{
      .serial = serial,
      .kind = kind,
      .client = client,
      .surface = surface,
  });
  while (records.size() > kMaxSeatSerialRecords) {
    records.pop_front();
  }
  return serial;
}

std::uint32_t issueSeatSerialForSurface(WaylandServer::Impl* server,
                                        SeatSerialKind kind,
                                        WaylandServer::Impl::Surface* surface) {
  return issueSeatSerial(server, kind, surfaceClient(surface), surface);
}

bool seatSerialIsValid(WaylandServer::Impl const* server,
                       std::uint32_t serial,
                       wl_client* client,
                       WaylandServer::Impl::Surface const* surface,
                       std::span<SeatSerialKind const> allowedKinds) {
  if (!server || serial == 0 || allowedKinds.empty()) return false;
  return seatSerialIsValid(server->seatSerials_, serial, client, surface, allowedKinds);
}

bool seatSerialIsValid(std::deque<WaylandServer::Impl::SeatSerialRecord> const& records,
                       std::uint32_t serial,
                       wl_client* client,
                       WaylandServer::Impl::Surface const* surface,
                       std::span<SeatSerialKind const> allowedKinds) {
  if (serial == 0 || allowedKinds.empty()) return false;
  for (auto it = records.rbegin(); it != records.rend(); ++it) {
    if (it->serial != serial) continue;
    if (!kindAllowed(it->kind, allowedKinds)) return false;
    if (client && it->client != client) return false;
    if (surface && it->surface != surface) return false;
    return true;
  }
  return false;
}

void clearSeatSerialsForSurface(WaylandServer::Impl* server, WaylandServer::Impl::Surface const* surface) {
  if (!server || !surface) return;
  clearSeatSerialsForSurface(server->seatSerials_, surface);
}

void clearSeatSerialsForSurface(std::deque<WaylandServer::Impl::SeatSerialRecord>& records,
                                WaylandServer::Impl::Surface const* surface) {
  if (!surface) return;
  std::erase_if(records, [surface](WaylandServer::Impl::SeatSerialRecord const& record) {
    return record.surface == surface;
  });
}

} // namespace lambdaui::compositor
