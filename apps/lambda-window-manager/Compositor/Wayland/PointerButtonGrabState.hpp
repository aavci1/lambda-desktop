#pragma once

#include "Compositor/Wayland/WaylandServerImpl.hpp"

#include <cstdint>

namespace lambda::compositor {

struct PointerButtonGrabRefs {
  WaylandServer::Impl::Surface** grabSurface = nullptr;
  wl_client** grabClient = nullptr;
  std::uint32_t* buttonCount = nullptr;
};

struct PointerButtonGrabTransition {
  WaylandServer::Impl::Surface* previousSurface = nullptr;
  WaylandServer::Impl::Surface* nextSurface = nullptr;
  wl_client* previousClient = nullptr;
  wl_client* nextClient = nullptr;
  std::uint32_t previousCount = 0;
  std::uint32_t nextCount = 0;
};

[[nodiscard]] inline bool pointerButtonGrabClearStale(PointerButtonGrabRefs refs) {
  if (!refs.buttonCount || *refs.buttonCount == 0) return false;
  if (refs.grabSurface && *refs.grabSurface) return false;
  if (refs.grabClient) *refs.grabClient = nullptr;
  *refs.buttonCount = 0;
  return true;
}

[[nodiscard]] inline WaylandServer::Impl::Surface* pointerButtonGrabDeliverySurface(
    PointerButtonGrabRefs refs,
    WaylandServer::Impl::Surface* pointerFocus) {
  (void)pointerButtonGrabClearStale(refs);
  if (refs.buttonCount && *refs.buttonCount > 0 && refs.grabSurface) return *refs.grabSurface;
  return pointerFocus;
}

[[nodiscard]] inline wl_client* pointerButtonGrabDeliveryClient(PointerButtonGrabRefs refs,
                                                                wl_client* fallbackClient) {
  (void)pointerButtonGrabClearStale(refs);
  if (refs.buttonCount && *refs.buttonCount > 0 && refs.grabClient && *refs.grabClient) {
    return *refs.grabClient;
  }
  return fallbackClient;
}

inline PointerButtonGrabTransition pointerButtonGrabUpdateForButton(
    PointerButtonGrabRefs refs,
    WaylandServer::Impl::Surface* deliverySurface,
    wl_client* deliveryClient,
    bool pressed) {
  PointerButtonGrabTransition transition{
      .previousSurface = refs.grabSurface ? *refs.grabSurface : nullptr,
      .nextSurface = refs.grabSurface ? *refs.grabSurface : nullptr,
      .previousClient = refs.grabClient ? *refs.grabClient : nullptr,
      .nextClient = refs.grabClient ? *refs.grabClient : nullptr,
      .previousCount = refs.buttonCount ? *refs.buttonCount : 0,
      .nextCount = refs.buttonCount ? *refs.buttonCount : 0,
  };
  if (!refs.buttonCount) return transition;

  if (pressed) {
    if (*refs.buttonCount == 0) {
      if (refs.grabSurface) *refs.grabSurface = deliverySurface;
      if (refs.grabClient) *refs.grabClient = deliveryClient;
    }
    ++*refs.buttonCount;
  } else if (*refs.buttonCount > 0) {
    --*refs.buttonCount;
    if (*refs.buttonCount == 0) {
      if (refs.grabSurface) *refs.grabSurface = nullptr;
      if (refs.grabClient) *refs.grabClient = nullptr;
    }
  }

  transition.nextSurface = refs.grabSurface ? *refs.grabSurface : nullptr;
  transition.nextClient = refs.grabClient ? *refs.grabClient : nullptr;
  transition.nextCount = *refs.buttonCount;
  return transition;
}

} // namespace lambda::compositor
