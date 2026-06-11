#pragma once

#include "wayland-server-protocol.h"

#include <cstdint>

namespace lambda::compositor {

inline constexpr std::uint32_t kValidDndActionMask =
    WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY |
    WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE |
    WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK;

inline bool validDndActionMask(std::uint32_t actions) {
  return (actions & ~kValidDndActionMask) == 0u;
}

inline bool validPreferredDndAction(std::uint32_t action) {
  return action == WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE ||
         action == WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY ||
         action == WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE ||
         action == WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK;
}

inline std::uint32_t chooseDndAction(std::uint32_t sourceActions,
                                     std::uint32_t targetActions,
                                     std::uint32_t preferredAction) {
  std::uint32_t const available = sourceActions & targetActions;
  if (preferredAction != WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE &&
      (available & preferredAction) != 0u) {
    return preferredAction;
  }
  if ((available & WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY) != 0u) {
    return WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
  }
  if ((available & WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE) != 0u) {
    return WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE;
  }
  if ((available & WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK) != 0u) {
    return WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK;
  }
  return WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;
}

inline bool dataOfferAcceptsDndActions(bool dnd) {
  return dnd;
}

inline bool dataOfferCanFinishDnd(bool dnd,
                                  bool hasAcceptedMimeType,
                                  std::uint32_t selectedAction) {
  return dnd &&
         hasAcceptedMimeType &&
         selectedAction != WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;
}

inline bool dataSourceCanSetDndActions(bool actionsAlreadySet, bool sourceAlreadyUsed) {
  return !actionsAlreadySet && !sourceAlreadyUsed;
}

inline bool dataSourceCanStartDrag(bool sourceAlreadyUsed) {
  return !sourceAlreadyUsed;
}

inline bool dataSourceCanSetSelection(bool actionsAlreadySet, bool sourceAlreadyUsed) {
  return !actionsAlreadySet && !sourceAlreadyUsed;
}

inline bool dataDeviceCanUseDragIconSurface(bool iconProvided, bool surfaceHasNoRole) {
  return !iconProvided || surfaceHasNoRole;
}

} // namespace lambda::compositor
