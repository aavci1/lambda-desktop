#pragma once

#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include <string_view>

namespace lambda::compositor {

[[nodiscard]] inline bool xdgToplevelSurfaceConfigured(WaylandServer::Impl::XdgToplevel const* toplevel) {
  return toplevel &&
         toplevel->xdgSurface &&
         toplevel->xdgSurface->surface &&
         surfaceIsXdgToplevel(toplevel->xdgSurface->surface) &&
         toplevel->xdgSurface->configured;
}

[[nodiscard]] inline bool xdgToplevelMapped(WaylandServer::Impl::XdgToplevel const* toplevel) {
  return toplevel &&
         toplevel->mapped &&
         toplevel->xdgSurface &&
         toplevel->xdgSurface->surface &&
         surfaceIsXdgToplevel(toplevel->xdgSurface->surface);
}

[[nodiscard]] inline WaylandServer::Impl::XdgToplevel* xdgToplevelRetainedParent(
    WaylandServer::Impl::XdgToplevel* parent) {
  return xdgToplevelMapped(parent) ? parent : nullptr;
}

struct XdgResizeConfigureGate {
  bool sendConfigure = true;
  bool rememberPending = false;
};

[[nodiscard]] inline XdgResizeConfigureGate xdgResizeConfigureGate(bool inFlight,
                                                                   bool acked,
                                                                   bool sameAsInFlight) {
  if (!inFlight) return {};
  if (sameAsInFlight) {
    return XdgResizeConfigureGate{.sendConfigure = false, .rememberPending = false};
  }
  if (!acked) {
    return XdgResizeConfigureGate{.sendConfigure = false, .rememberPending = true};
  }
  return XdgResizeConfigureGate{.sendConfigure = true, .rememberPending = false};
}

inline bool resetXdgToplevelClientStateForUnmap(WaylandServer::Impl::XdgToplevel* toplevel) {
  if (!toplevel) return false;
  bool const changed = toplevel->mapped ||
                       toplevel->parent != nullptr ||
                       !toplevel->title.empty() ||
                       !toplevel->appId.empty();
  toplevel->parent = nullptr;
  toplevel->mapped = false;
  toplevel->title.clear();
  toplevel->appId.clear();
  return changed;
}

[[nodiscard]] inline bool xdgToplevelTitleUtf8Valid(std::string_view text) {
  auto inRange = [](unsigned char value, unsigned char low, unsigned char high) {
    return low <= value && value <= high;
  };
  auto continuation = [&](std::size_t index) {
    return index < text.size() && inRange(static_cast<unsigned char>(text[index]), 0x80, 0xBF);
  };

  for (std::size_t index = 0; index < text.size();) {
    unsigned char const first = static_cast<unsigned char>(text[index]);
    if (first <= 0x7F) {
      ++index;
      continue;
    }
    if (inRange(first, 0xC2, 0xDF)) {
      if (!continuation(index + 1)) return false;
      index += 2;
      continue;
    }
    if (first == 0xE0) {
      if (index + 2 >= text.size()) return false;
      unsigned char const second = static_cast<unsigned char>(text[index + 1]);
      if (!inRange(second, 0xA0, 0xBF) || !continuation(index + 2)) return false;
      index += 3;
      continue;
    }
    if (inRange(first, 0xE1, 0xEC) || inRange(first, 0xEE, 0xEF)) {
      if (!continuation(index + 1) || !continuation(index + 2)) return false;
      index += 3;
      continue;
    }
    if (first == 0xED) {
      if (index + 2 >= text.size()) return false;
      unsigned char const second = static_cast<unsigned char>(text[index + 1]);
      if (!inRange(second, 0x80, 0x9F) || !continuation(index + 2)) return false;
      index += 3;
      continue;
    }
    if (first == 0xF0) {
      if (index + 3 >= text.size()) return false;
      unsigned char const second = static_cast<unsigned char>(text[index + 1]);
      if (!inRange(second, 0x90, 0xBF) || !continuation(index + 2) || !continuation(index + 3)) return false;
      index += 4;
      continue;
    }
    if (inRange(first, 0xF1, 0xF3)) {
      if (!continuation(index + 1) || !continuation(index + 2) || !continuation(index + 3)) return false;
      index += 4;
      continue;
    }
    if (first == 0xF4) {
      if (index + 3 >= text.size()) return false;
      unsigned char const second = static_cast<unsigned char>(text[index + 1]);
      if (!inRange(second, 0x80, 0x8F) || !continuation(index + 2) || !continuation(index + 3)) return false;
      index += 4;
      continue;
    }
    return false;
  }
  return true;
}

} // namespace lambda::compositor
