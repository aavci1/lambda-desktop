#include "Compositor/Wayland/Globals/Activation.hpp"

#include "Compositor/Wayland/ActivationState.hpp"
#include "Compositor/Wayland/ResourceTemplates.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include "xdg-activation-v1-server-protocol.h"

#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <wayland-server-core.h>

namespace lambdaui::compositor {

void focusSurface(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface, std::uint32_t timeMs);

namespace {

constexpr std::array<SeatSerialKind, 7> kActivationSerialKinds{
    SeatSerialKind::PointerEnter,
    SeatSerialKind::PointerButtonPress,
    SeatSerialKind::PointerButtonRelease,
    SeatSerialKind::KeyboardEnter,
    SeatSerialKind::KeyboardKey,
    SeatSerialKind::KeyboardModifiers,
    SeatSerialKind::DataDeviceEnter,
};

std::uint32_t monotonicMilliseconds() {
  using Clock = std::chrono::steady_clock;
  auto const now = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch());
  return static_cast<std::uint32_t>(now.count());
}

void activationTokenDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

bool postActivationTokenAlreadyUsedIfNeeded(wl_resource* resource,
                                            WaylandServer::Impl::ActivationToken const* token) {
  if (activationTokenMutable(token)) return false;
  wl_resource_post_error(resource,
                         XDG_ACTIVATION_TOKEN_V1_ERROR_ALREADY_USED,
                         "activation token already committed");
  return true;
}

void makeActivationTokenResourceInert(wl_resource* resource, WaylandServer::Impl::ActivationToken* token) {
  wl_resource_set_user_data(resource, nullptr);
  if (token) token->resource = nullptr;
}

bool activationTokenCommitRequestValid(wl_resource* resource, WaylandServer::Impl::ActivationToken const* token) {
  if (!token || !token->hasSerial) return true;
  wl_client* client = wl_resource_get_client(resource);
  if (!seatSerialIsValid(token->server, token->serial, client, nullptr, kActivationSerialKinds)) {
    return false;
  }
  return activationTokenFocusedSurfaceValid(token->server, token);
}

int activationTokenTimeout(void* data) {
  auto* token = static_cast<WaylandServer::Impl::ActivationToken*>(data);
  if (!token || !token->server) return 0;
  token->server->destroyActivationToken(token);
  return 0;
}

bool armActivationTokenTimeout(WaylandServer::Impl::ActivationToken* token) {
  if (!token || !token->server || kActivationTokenTimeoutMs == 0) return true;
  if (!token->server->display_) return true;
  wl_event_loop* loop = wl_display_get_event_loop(token->server->display_);
  if (!loop) return true;
  token->timeout = wl_event_loop_add_timer(loop, activationTokenTimeout, token);
  if (!token->timeout) return false;
  wl_event_source_timer_update(token->timeout, kActivationTokenTimeoutMs);
  return true;
}

void sendInvalidActivationTokenAndDestroy(wl_resource* resource, WaylandServer::Impl::ActivationToken* token) {
  if (!token || !token->server) return;
  std::string const invalidToken = "lambda-invalid-" + std::to_string(token->server->nextActivationTokenId_++);
  xdg_activation_token_v1_send_done(resource, invalidToken.c_str());
  auto* server = token->server;
  makeActivationTokenResourceInert(resource, token);
  server->destroyActivationToken(token);
}

void activationTokenSetSerial(wl_client*, wl_resource* resource, std::uint32_t serial, wl_resource*) {
  auto* token = resourceData<WaylandServer::Impl::ActivationToken>(resource);
  if (postActivationTokenAlreadyUsedIfNeeded(resource, token)) return;
  token->serial = serial;
  token->hasSerial = true;
}

void activationTokenSetAppId(wl_client*, wl_resource* resource, char const* appId) {
  auto* token = resourceData<WaylandServer::Impl::ActivationToken>(resource);
  if (postActivationTokenAlreadyUsedIfNeeded(resource, token)) return;
  token->appId = appId ? appId : "";
}

void activationTokenSetSurface(wl_client*, wl_resource* resource, wl_resource* surfaceResource) {
  auto* token = resourceData<WaylandServer::Impl::ActivationToken>(resource);
  if (postActivationTokenAlreadyUsedIfNeeded(resource, token)) return;
  token->surface = resourceData<WaylandServer::Impl::Surface>(surfaceResource);
}

void activationTokenCommit(wl_client*, wl_resource* resource) {
  auto* token = resourceData<WaylandServer::Impl::ActivationToken>(resource);
  if (postActivationTokenAlreadyUsedIfNeeded(resource, token)) return;
  if (!activationTokenCommitRequestValid(resource, token)) {
    sendInvalidActivationTokenAndDestroy(resource, token);
    return;
  }
  token->committed = true;
  token->expiresAtMs = monotonicMilliseconds() + kActivationTokenTimeoutMs;
  if (!armActivationTokenTimeout(token)) {
    wl_client_post_no_memory(wl_resource_get_client(resource));
    auto* server = token->server;
    makeActivationTokenResourceInert(resource, token);
    server->destroyActivationToken(token);
    return;
  }
  xdg_activation_token_v1_send_done(resource, token->token.c_str());
  makeActivationTokenResourceInert(resource, token);
}

struct xdg_activation_token_v1_interface const activationTokenImpl{
    .set_serial = activationTokenSetSerial,
    .set_app_id = activationTokenSetAppId,
    .set_surface = activationTokenSetSurface,
    .commit = activationTokenCommit,
    .destroy = activationTokenDestroy,
};

void activationDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void activationGetToken(wl_client* client, wl_resource* resource, std::uint32_t id) {
  auto* server = serverFrom(resource);
  auto token = std::make_unique<WaylandServer::Impl::ActivationToken>();
  token->server = server;
  token->token = "lambda-" + std::to_string(server->nextActivationTokenId_++);
  auto const version = activationResourceVersion(static_cast<std::uint32_t>(wl_resource_get_version(resource)));
  wl_resource* tokenResource =
      wl_resource_create(client, &xdg_activation_token_v1_interface, version, id);
  if (!tokenResource) {
    wl_client_post_no_memory(client);
    return;
  }
  token->resource = tokenResource;
  auto* raw = token.get();
  server->activationTokens_.push_back(std::move(token));
  wl_resource_set_implementation(tokenResource,
                                 &activationTokenImpl,
                                 raw,
                                 destroyResourceCallback<WaylandServer::Impl::ActivationToken,
                                                         WaylandServer::Impl,
                                                         &WaylandServer::Impl::destroyActivationToken>);
}

void activationActivate(wl_client*, wl_resource* resource, char const* tokenName, wl_resource* surfaceResource) {
  auto* server = serverFrom(resource);
  auto* surface = resourceData<WaylandServer::Impl::Surface>(surfaceResource);
  if (!server || !surfaceIsXdgToplevel(surface)) return;
  auto* token = activationTokenForName(server->activationTokens_, tokenName ? tokenName : "", monotonicMilliseconds());
  if (!token) return;

  std::uint32_t const now = monotonicMilliseconds();
  server->lastActivationSurface_ = surface;
  server->lastActivationTimeMs_ = now;
  focusSurface(server, surface, now);
  server->destroyActivationToken(token);
  server->flushClients();
}

struct xdg_activation_v1_interface const activationImpl{
    .destroy = activationDestroy,
    .get_activation_token = activationGetToken,
    .activate = activationActivate,
};

} // namespace

void bindActivation(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  wl_resource* resource = wl_resource_create(client, &xdg_activation_v1_interface, activationResourceVersion(version), id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &activationImpl, data, nullptr);
}

} // namespace lambdaui::compositor
