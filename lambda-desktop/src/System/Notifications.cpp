#include <Lambda/System/Notifications.hpp>

#include <algorithm>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace lambda::system {

namespace {

std::vector<NotificationAction> parseActions(dbus::StringArray const& rawActions) {
  std::vector<NotificationAction> actions;
  for (std::size_t i = 0; i + 1u < rawActions.values.size(); i += 2u) {
    if (rawActions.values[i].empty()) {
      continue;
    }
    actions.push_back(NotificationAction{
        .key = rawActions.values[i],
        .label = rawActions.values[i + 1u],
    });
  }
  return actions;
}

template <typename T>
std::optional<T> valueAs(dbus::BasicValue const& value) {
  if (auto const* typed = std::get_if<T>(&value)) {
    return *typed;
  }
  return std::nullopt;
}

std::optional<dbus::BasicValue> hintValue(dbus::VariantDictionary const& hints,
                                          std::initializer_list<char const*> keys) {
  for (char const* key : keys) {
    auto it = hints.values.find(key);
    if (it != hints.values.end()) {
      return it->second;
    }
  }
  return std::nullopt;
}

std::string stringHint(dbus::VariantDictionary const& hints,
                       std::initializer_list<char const*> keys) {
  if (auto value = hintValue(hints, keys)) {
    if (auto typed = valueAs<std::string>(*value)) {
      return *typed;
    }
  }
  return {};
}

std::optional<std::int32_t> int32Hint(dbus::VariantDictionary const& hints,
                                      std::initializer_list<char const*> keys) {
  if (auto value = hintValue(hints, keys)) {
    return valueAs<std::int32_t>(*value);
  }
  return std::nullopt;
}

bool boolHint(dbus::VariantDictionary const& hints, std::initializer_list<char const*> keys) {
  if (auto value = hintValue(hints, keys)) {
    if (auto typed = valueAs<bool>(*value)) {
      return *typed;
    }
  }
  return false;
}

NotificationUrgency urgencyFromByte(std::uint8_t raw) noexcept {
  switch (raw) {
  case 0:
    return NotificationUrgency::Low;
  case 2:
    return NotificationUrgency::Critical;
  case 1:
  default:
    return NotificationUrgency::Normal;
  }
}

std::optional<NotificationImageData> imageDataFromValue(dbus::BasicValue const& value) {
  auto const* structure = std::get_if<std::shared_ptr<dbus::StructValue>>(&value);
  if (!structure || !*structure || (*structure)->fields.size() != 7u) {
    return std::nullopt;
  }

  auto const& fields = (*structure)->fields;
  auto width = valueAs<std::int32_t>(fields[0]);
  auto height = valueAs<std::int32_t>(fields[1]);
  auto rowStride = valueAs<std::int32_t>(fields[2]);
  auto hasAlpha = valueAs<bool>(fields[3]);
  auto bitsPerSample = valueAs<std::int32_t>(fields[4]);
  auto channels = valueAs<std::int32_t>(fields[5]);
  auto pixels = valueAs<dbus::ByteArray>(fields[6]);
  if (!width || !height || !rowStride || !hasAlpha || !bitsPerSample || !channels || !pixels ||
      *width <= 0 || *height <= 0 || *rowStride <= 0 || *bitsPerSample <= 0 || *channels <= 0 ||
      pixels->values.empty()) {
    return std::nullopt;
  }

  return NotificationImageData{
      .width = *width,
      .height = *height,
      .rowStride = *rowStride,
      .hasAlpha = *hasAlpha,
      .bitsPerSample = *bitsPerSample,
      .channels = *channels,
      .pixels = pixels->values,
  };
}

std::optional<NotificationImageData> imageDataHint(dbus::VariantDictionary const& hints) {
  if (auto value = hintValue(hints, {"image-data", "image_data", "icon_data"})) {
    return imageDataFromValue(*value);
  }
  return std::nullopt;
}

NotificationHints parseHints(dbus::VariantDictionary const& hints) {
  NotificationHints parsed;
  if (auto value = hintValue(hints, {"urgency"})) {
    if (auto raw = valueAs<std::uint8_t>(*value)) {
      parsed.urgency = urgencyFromByte(*raw);
    }
  }
  parsed.category = stringHint(hints, {"category"});
  parsed.desktopEntry = stringHint(hints, {"desktop-entry"});
  parsed.imagePath = stringHint(hints, {"image-path", "image_path"});
  parsed.imageData = imageDataHint(hints);
  parsed.soundName = stringHint(hints, {"sound-name"});
  parsed.soundFile = stringHint(hints, {"sound-file"});
  parsed.x = int32Hint(hints, {"x"});
  parsed.y = int32Hint(hints, {"y"});
  parsed.actionIcons = boolHint(hints, {"action-icons"});
  parsed.resident = boolHint(hints, {"resident"});
  parsed.suppressSound = boolHint(hints, {"suppress-sound"});
  parsed.transient = boolHint(hints, {"transient"});
  return parsed;
}

dbus::StringArray encodeActions(std::vector<NotificationAction> const& actions) {
  dbus::StringArray encoded;
  encoded.values.reserve(actions.size() * 2u);
  for (auto const& action : actions) {
    if (action.key.empty()) {
      continue;
    }
    encoded.values.push_back(action.key);
    encoded.values.push_back(action.label);
  }
  return encoded;
}

std::shared_ptr<dbus::StructValue> encodeImageData(NotificationImageData const& image) {
  return std::make_shared<dbus::StructValue>(
      dbus::StructValue{.signature = "iiibiiay",
                        .fields = {image.width,
                                   image.height,
                                   image.rowStride,
                                   image.hasAlpha,
                                   image.bitsPerSample,
                                   image.channels,
                                   dbus::ByteArray{image.pixels}}});
}

dbus::VariantDictionary encodeHints(NotificationHints const& hints) {
  dbus::VariantDictionary encoded;
  encoded.values["urgency"] = static_cast<std::uint8_t>(hints.urgency);
  if (!hints.category.empty()) {
    encoded.values["category"] = hints.category;
  }
  if (!hints.desktopEntry.empty()) {
    encoded.values["desktop-entry"] = hints.desktopEntry;
  }
  if (!hints.imagePath.empty()) {
    encoded.values["image-path"] = hints.imagePath;
  }
  if (hints.imageData) {
    encoded.values["image-data"] = encodeImageData(*hints.imageData);
  }
  if (!hints.soundName.empty()) {
    encoded.values["sound-name"] = hints.soundName;
  }
  if (!hints.soundFile.empty()) {
    encoded.values["sound-file"] = hints.soundFile;
  }
  if (hints.x) {
    encoded.values["x"] = *hints.x;
  }
  if (hints.y) {
    encoded.values["y"] = *hints.y;
  }
  if (hints.actionIcons) {
    encoded.values["action-icons"] = hints.actionIcons;
  }
  if (hints.resident) {
    encoded.values["resident"] = hints.resident;
  }
  if (hints.suppressSound) {
    encoded.values["suppress-sound"] = hints.suppressSound;
  }
  if (hints.transient) {
    encoded.values["transient"] = hints.transient;
  }
  return encoded;
}

} // namespace

NotificationsService::NotificationsService(dbus::Bus& bus, std::size_t historyLimit)
    : bus_(&bus),
      historyLimit_(std::max<std::size_t>(1u, historyLimit)) {}

dbus::Slot NotificationsService::exportObject() {
  return bus_->exportObject(
      objectPath,
      dbus::ObjectDefinition{
          .methods = {
              dbus::ExportedMethod{
                  .interface = interfaceName,
                  .member = "GetCapabilities",
                  .handler = [this](dbus::Message&) {
                    dbus::MethodReply reply;
                    reply.values = {dbus::BasicValue(dbus::StringArray{capabilities()})};
                    return reply;
                  },
              },
              dbus::ExportedMethod{
                  .interface = interfaceName,
                  .member = "GetServerInformation",
                  .handler = [](dbus::Message&) {
                    dbus::MethodReply reply;
                    reply.values = {
                        dbus::BasicValue(std::string("Lambda Notifications")),
                        dbus::BasicValue(std::string("Lambda")),
                        dbus::BasicValue(std::string("1.0")),
                        dbus::BasicValue(std::string("1.2")),
                    };
                    return reply;
                  },
              },
              dbus::ExportedMethod{
                  .interface = interfaceName,
                  .member = "Notify",
                  .handler = [this](dbus::Message& message) {
                    std::string appName = message.readString();
                    std::uint32_t replacesId = message.readUint32();
                    std::string appIcon = message.readString();
                    std::string summary = message.readString();
                    std::string body = message.readString();
                    dbus::StringArray actions = message.readStringArray();
                    dbus::VariantDictionary hints = message.readVariantDictionary();
                    std::int32_t expireTimeoutMs = message.readInt32();

                    dbus::MethodReply reply;
                    reply.values = {dbus::BasicValue(notify(std::move(appName),
                                                            replacesId,
                                                            std::move(appIcon),
                                                            std::move(summary),
                                                            std::move(body),
                                                            std::move(actions),
                                                            std::move(hints),
                                                            expireTimeoutMs))};
                    return reply;
                  },
              },
              dbus::ExportedMethod{
                  .interface = interfaceName,
                  .member = "CloseNotification",
                  .handler = [this](dbus::Message& message) {
                    std::uint32_t const id = message.readUint32();
                    if (!closeNotification(id)) {
                      return dbus::MethodReply::error("org.freedesktop.Notifications.Error.NotFound",
                                                      "Unknown notification id");
                    }
                    return dbus::MethodReply{};
                  },
              },
              dbus::ExportedMethod{
                  .interface = monitorInterfaceName,
                  .member = "ClearHistory",
                  .handler = [this](dbus::Message&) {
                    dbus::MethodReply reply;
                    reply.values = {dbus::BasicValue(static_cast<std::uint32_t>(
                        std::min<std::size_t>(clearHistory(), std::numeric_limits<std::uint32_t>::max())))};
                    return reply;
                  },
              },
              dbus::ExportedMethod{
                  .interface = monitorInterfaceName,
                  .member = invokeActionMethodName,
                  .handler = [this](dbus::Message& message) {
                    std::uint32_t const id = message.readUint32();
                    std::string actionKey = message.readString();
                    if (!invokeAction(id, std::move(actionKey))) {
                      return dbus::MethodReply::error("org.lambda.Notifications.Error.ActionUnavailable",
                                                      "Unknown notification action");
                    }
                    return dbus::MethodReply{};
                  },
              },
          },
          .properties = {},
      });
}

std::uint32_t NotificationsService::notify(std::string appName,
                                           std::uint32_t replacesId,
                                           std::string appIcon,
                                           std::string summary,
                                           std::string body,
                                           dbus::StringArray actions,
                                           std::int32_t expireTimeoutMs) {
  return notify(std::move(appName),
                replacesId,
                std::move(appIcon),
                std::move(summary),
                std::move(body),
                std::move(actions),
                dbus::VariantDictionary{},
                expireTimeoutMs);
}

std::uint32_t NotificationsService::notify(std::string appName,
                                           std::uint32_t replacesId,
                                           std::string appIcon,
                                           std::string summary,
                                           std::string body,
                                           dbus::StringArray actions,
                                           dbus::VariantDictionary hints,
                                           std::int32_t expireTimeoutMs) {
  NotificationHints parsedHints = parseHints(hints);
  auto const now = std::chrono::steady_clock::now();
  if (replacesId != 0) {
    if (auto* existing = find(replacesId)) {
      existing->appName = std::move(appName);
      existing->appIcon = std::move(appIcon);
      existing->summary = std::move(summary);
      existing->body = std::move(body);
      existing->actions = parseActions(actions);
      existing->hints = std::move(parsedHints);
      existing->expireTimeoutMs = expireTimeoutMs;
      existing->postedAt = now;
      existing->closed = false;
      existing->closeReason = NotificationCloseReason::Undefined;
      emitPosted(*existing);
      return existing->id;
    }
  }

  std::uint32_t const id = nextId_++;
  notifications_.insert(notifications_.begin(),
                        NotificationRecord{
                            .id = id,
                            .appName = std::move(appName),
                            .appIcon = std::move(appIcon),
                            .summary = std::move(summary),
                            .body = std::move(body),
                            .expireTimeoutMs = expireTimeoutMs,
                            .actions = parseActions(actions),
                            .hints = std::move(parsedHints),
                            .postedAt = now,
                        });
  trimHistory();
  emitPosted(notifications_.front());
  return id;
}

bool NotificationsService::closeNotification(std::uint32_t id, NotificationCloseReason reason) {
  auto it = std::find_if(notifications_.begin(), notifications_.end(), [&](auto const& notification) {
    return notification.id == id;
  });
  if (it == notifications_.end() || it->closed) {
    return false;
  }
  it->closed = true;
  it->closeReason = reason;
  bool const transient = it->hints.transient;
  emitClosed(id, reason);
  if (transient) {
    notifications_.erase(it);
  }
  return true;
}

bool NotificationsService::invokeAction(std::uint32_t id, std::string actionKey) {
  auto const* record = find(id);
  if (!record || record->closed) {
    return false;
  }
  bool const known = std::any_of(record->actions.begin(), record->actions.end(), [&](auto const& action) {
    return action.key == actionKey;
  });
  if (!known) {
    return false;
  }
  emitActionInvoked(id, actionKey);
  return true;
}

std::size_t NotificationsService::clearHistory(NotificationCloseReason reason) {
  std::vector<std::uint32_t> closedIds;
  closedIds.reserve(notifications_.size());
  for (auto& notification : notifications_) {
    if (!notification.closed) {
      notification.closed = true;
      notification.closeReason = reason;
      closedIds.push_back(notification.id);
    }
  }
  notifications_.clear();
  for (std::uint32_t id : closedIds) {
    emitClosed(id, reason);
  }
  return closedIds.size();
}

std::size_t NotificationsService::expireDueNotifications(std::chrono::steady_clock::time_point now) {
  std::size_t expired = 0;
  std::vector<std::uint32_t> ids;
  ids.reserve(notifications_.size());
  for (auto const& notification : notifications_) {
    if (!notification.closed &&
        !notification.hints.resident &&
        notification.expireTimeoutMs > 0 &&
        notification.postedAt + std::chrono::milliseconds(notification.expireTimeoutMs) <= now) {
      ids.push_back(notification.id);
    }
  }
  for (std::uint32_t id : ids) {
    if (closeNotification(id, NotificationCloseReason::Expired)) {
      ++expired;
    }
  }
  return expired;
}

std::vector<std::string> NotificationsService::capabilities() const {
  return {"actions", "body", "body-markup", "persistence"};
}

std::optional<NotificationRecord> NotificationsService::notification(std::uint32_t id) const {
  if (auto const* record = find(id)) {
    return *record;
  }
  return std::nullopt;
}

std::optional<std::chrono::steady_clock::time_point> NotificationsService::nextExpirationDeadline() const {
  std::optional<std::chrono::steady_clock::time_point> deadline;
  for (auto const& notification : notifications_) {
    if (notification.closed || notification.hints.resident || notification.expireTimeoutMs <= 0) {
      continue;
    }
    auto const candidate = notification.postedAt + std::chrono::milliseconds(notification.expireTimeoutMs);
    if (!deadline || candidate < *deadline) {
      deadline = candidate;
    }
  }
  return deadline;
}

NotificationRecord* NotificationsService::find(std::uint32_t id) {
  auto it = std::find_if(notifications_.begin(), notifications_.end(), [&](auto const& notification) {
    return notification.id == id;
  });
  return it == notifications_.end() ? nullptr : &*it;
}

NotificationRecord const* NotificationsService::find(std::uint32_t id) const {
  auto it = std::find_if(notifications_.begin(), notifications_.end(), [&](auto const& notification) {
    return notification.id == id;
  });
  return it == notifications_.end() ? nullptr : &*it;
}

void NotificationsService::emitClosed(std::uint32_t id, NotificationCloseReason reason) const {
  bus_->emitSignal(objectPath,
                   interfaceName,
                   "NotificationClosed",
                   {id, static_cast<std::uint32_t>(reason)});
}

void NotificationsService::emitPosted(NotificationRecord const& record) const {
  bus_->emitSignal(objectPath,
                   monitorInterfaceName,
                   postedSignalName,
                   {record.id,
                    record.appName,
                    record.appIcon,
                    record.summary,
                    record.body,
                    record.expireTimeoutMs,
                    encodeActions(record.actions),
                    encodeHints(record.hints)});
}

void NotificationsService::emitActionInvoked(std::uint32_t id, std::string const& actionKey) const {
  bus_->emitSignal(objectPath, interfaceName, "ActionInvoked", {id, actionKey});
}

void NotificationsService::trimHistory() {
  if (notifications_.size() > historyLimit_) {
    std::vector<std::uint32_t> closedIds;
    closedIds.reserve(notifications_.size() - historyLimit_);
    for (std::size_t index = historyLimit_; index < notifications_.size(); ++index) {
      if (!notifications_[index].closed) {
        notifications_[index].closed = true;
        notifications_[index].closeReason = NotificationCloseReason::Undefined;
        closedIds.push_back(notifications_[index].id);
      }
    }
    notifications_.resize(historyLimit_);
    for (std::uint32_t id : closedIds) {
      emitClosed(id, NotificationCloseReason::Undefined);
    }
  }
  pruneClosedTransient();
}

void NotificationsService::pruneClosedTransient() {
  notifications_.erase(std::remove_if(notifications_.begin(),
                                      notifications_.end(),
                                      [](NotificationRecord const& notification) {
                                        return notification.closed && notification.hints.transient;
                                      }),
                       notifications_.end());
}

NotificationsClient::NotificationsClient(dbus::Bus bus) : bus_(std::move(bus)) {}

NotificationsClient NotificationsClient::connectSession() {
  return NotificationsClient(dbus::Bus::open(dbus::BusType::Session));
}

dbus::Slot NotificationsClient::watchPosted(std::function<void(NotificationPosted)> handler) {
  return bus_.matchSignal(
      dbus::SignalMatch{
          .sender = NotificationsService::serviceName,
          .path = NotificationsService::objectPath,
          .interface = NotificationsService::monitorInterfaceName,
          .member = NotificationsService::postedSignalName,
      },
      [handler = std::move(handler)](dbus::Message& message) mutable {
        NotificationPosted posted;
        posted.id = message.readUint32();
        posted.appName = message.readString();
        posted.appIcon = message.readString();
        posted.summary = message.readString();
        posted.body = message.readString();
        posted.expireTimeoutMs = message.readInt32();
        posted.actions = parseActions(message.readStringArray());
        if (message.signature(false).ends_with("a{sv}")) {
          posted.hints = parseHints(message.readVariantDictionary());
        }
        if (handler) {
          handler(std::move(posted));
        }
      });
}

dbus::Slot NotificationsClient::watchClosed(std::function<void(std::uint32_t, NotificationCloseReason)> handler) {
  return bus_.matchSignal(
      dbus::SignalMatch{
          .sender = NotificationsService::serviceName,
          .path = NotificationsService::objectPath,
          .interface = NotificationsService::interfaceName,
          .member = "NotificationClosed",
      },
      [handler = std::move(handler)](dbus::Message& message) mutable {
        std::uint32_t const id = message.readUint32();
        auto const reason = static_cast<NotificationCloseReason>(message.readUint32());
        if (handler) {
          handler(id, reason);
        }
      });
}

void NotificationsClient::closeNotification(std::uint32_t id) {
  (void)bus_.call(dbus::MethodCall{
      .destination = NotificationsService::serviceName,
      .path = NotificationsService::objectPath,
      .interface = NotificationsService::interfaceName,
      .member = "CloseNotification",
      .arguments = {id},
  });
}

std::uint32_t NotificationsClient::clearHistory() {
  auto reply = bus_.call(dbus::MethodCall{
      .destination = NotificationsService::serviceName,
      .path = NotificationsService::objectPath,
      .interface = NotificationsService::monitorInterfaceName,
      .member = "ClearHistory",
      .arguments = {},
  });
  return reply.readUint32();
}

void NotificationsClient::invokeAction(std::uint32_t id, std::string actionKey) {
  (void)bus_.call(dbus::MethodCall{
      .destination = NotificationsService::serviceName,
      .path = NotificationsService::objectPath,
      .interface = NotificationsService::monitorInterfaceName,
      .member = NotificationsService::invokeActionMethodName,
      .arguments = {id, std::move(actionKey)},
  });
}

} // namespace lambda::system
