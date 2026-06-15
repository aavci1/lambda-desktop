#include <Lambda/System/Notifications.hpp>

#include <algorithm>
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
                    message.skip("a{sv}");
                    std::int32_t expireTimeoutMs = message.readInt32();

                    dbus::MethodReply reply;
                    reply.values = {dbus::BasicValue(notify(std::move(appName),
                                                            replacesId,
                                                            std::move(appIcon),
                                                            std::move(summary),
                                                            std::move(body),
                                                            std::move(actions),
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
  if (replacesId != 0) {
    if (auto* existing = find(replacesId)) {
      existing->appName = std::move(appName);
      existing->appIcon = std::move(appIcon);
      existing->summary = std::move(summary);
      existing->body = std::move(body);
      existing->actions = parseActions(actions);
      existing->expireTimeoutMs = expireTimeoutMs;
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
                            .actions = parseActions(actions),
                            .expireTimeoutMs = expireTimeoutMs,
                        });
  trimHistory();
  emitPosted(notifications_.front());
  return id;
}

bool NotificationsService::closeNotification(std::uint32_t id, NotificationCloseReason reason) {
  auto* record = find(id);
  if (!record || record->closed) {
    return false;
  }
  record->closed = true;
  record->closeReason = reason;
  emitClosed(id, reason);
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

std::vector<std::string> NotificationsService::capabilities() const {
  return {"actions", "body", "body-markup", "persistence"};
}

std::optional<NotificationRecord> NotificationsService::notification(std::uint32_t id) const {
  if (auto const* record = find(id)) {
    return *record;
  }
  return std::nullopt;
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
                    record.expireTimeoutMs});
}

void NotificationsService::emitActionInvoked(std::uint32_t id, std::string const& actionKey) const {
  bus_->emitSignal(objectPath, interfaceName, "ActionInvoked", {id, actionKey});
}

void NotificationsService::trimHistory() {
  if (notifications_.size() > historyLimit_) {
    notifications_.resize(historyLimit_);
  }
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

} // namespace lambda::system
