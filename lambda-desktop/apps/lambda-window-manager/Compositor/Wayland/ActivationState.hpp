#pragma once

#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace lambda::compositor {

inline constexpr std::uint32_t kActivationTokenTimeoutMs = 30'000;

[[nodiscard]] inline bool activationTokenMutable(WaylandServer::Impl::ActivationToken const* token) {
  return token && !token->committed;
}

[[nodiscard]] inline bool activationTokenExpired(WaylandServer::Impl::ActivationToken const* token,
                                                 std::uint32_t nowMs) {
  return token && token->expiresAtMs != 0 &&
         static_cast<std::int32_t>(nowMs - token->expiresAtMs) >= 0;
}

[[nodiscard]] inline bool activationTokenMatches(WaylandServer::Impl::ActivationToken const* token,
                                                 std::string_view tokenName) {
  return token && token->committed && token->token == tokenName;
}

[[nodiscard]] inline bool activationTokenMatches(WaylandServer::Impl::ActivationToken const* token,
                                                 std::string_view tokenName,
                                                 std::uint32_t nowMs) {
  return activationTokenMatches(token, tokenName) && !activationTokenExpired(token, nowMs);
}

[[nodiscard]] inline WaylandServer::Impl::ActivationToken* activationTokenForName(
    std::vector<std::unique_ptr<WaylandServer::Impl::ActivationToken>> const& tokens,
    std::string_view tokenName) {
  for (auto const& token : tokens) {
    if (activationTokenMatches(token.get(), tokenName)) return token.get();
  }
  return nullptr;
}

[[nodiscard]] inline WaylandServer::Impl::ActivationToken* activationTokenForName(
    std::vector<std::unique_ptr<WaylandServer::Impl::ActivationToken>> const& tokens,
    std::string_view tokenName,
    std::uint32_t nowMs) {
  for (auto const& token : tokens) {
    if (activationTokenMatches(token.get(), tokenName, nowMs)) return token.get();
  }
  return nullptr;
}

[[nodiscard]] inline bool activationTokenFocusedSurfaceValid(WaylandServer::Impl::Surface const* keyboardFocus,
                                                             WaylandServer::Impl::Surface const* pointerFocus,
                                                             WaylandServer::Impl::ActivationToken const* token) {
  if (!token || !token->surface) return true;
  return token->surface == keyboardFocus || token->surface == pointerFocus;
}

[[nodiscard]] inline bool activationTokenFocusedSurfaceValid(WaylandServer::Impl const* server,
                                                             WaylandServer::Impl::ActivationToken const* token) {
  return activationTokenFocusedSurfaceValid(server ? server->keyboardFocus_ : nullptr,
                                            server ? server->pointerFocus_ : nullptr,
                                            token);
}

} // namespace lambda::compositor
