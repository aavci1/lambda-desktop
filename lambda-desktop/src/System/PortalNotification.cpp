#include <Lambda/System/Notifications.hpp>
#include <Lambda/System/PortalNotification.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace lambda::system {

namespace {

std::string stringValue(dbus::VariantDictionary const& values,
                        std::string const& key,
                        std::string fallback = {}) {
  auto it = values.values.find(key);
  if (it == values.values.end()) {
    return fallback;
  }
  if (auto value = std::get_if<std::string>(&it->second)) {
    return *value;
  }
  return fallback;
}

dbus::StringArray buttonActions(dbus::VariantDictionary const& notification) {
  dbus::StringArray actions;
  auto buttons = notification.values.find("buttons");
  if (buttons == notification.values.end()) {
    return actions;
  }
  auto buttonArray = std::get_if<std::shared_ptr<dbus::ArrayValue>>(&buttons->second);
  if (!buttonArray || !*buttonArray) {
    return actions;
  }

  for (auto const& rawButton : (*buttonArray)->values) {
    auto button = std::get_if<std::shared_ptr<dbus::VariantDictionary>>(&rawButton);
    if (!button || !*button) {
      continue;
    }
    std::string action = stringValue(**button, "action");
    std::string label = stringValue(**button, "label");
    if (action.empty() || label.empty()) {
      continue;
    }
    actions.values.push_back(std::move(action));
    actions.values.push_back(std::move(label));
  }
  return actions;
}

std::shared_ptr<dbus::ArrayValue> emptyVariantArray() {
  return std::make_shared<dbus::ArrayValue>(
      dbus::ArrayValue{.elementSignature = "v", .values = {}});
}

} // namespace

PortalNotificationService::PortalNotificationService(dbus::Bus& bus) : bus_(&bus) {}

dbus::ObjectDefinition PortalNotificationService::objectDefinition() {
  return dbus::ObjectDefinition{
      .methods = {
          dbus::ExportedMethod{
              .interface = interfaceName,
              .member = "AddNotification",
              .handler = [this](dbus::Message& message) {
                return addNotification(message);
              },
          },
          dbus::ExportedMethod{
              .interface = interfaceName,
              .member = "RemoveNotification",
              .handler = [this](dbus::Message& message) {
                return removeNotification(message);
              },
          },
      },
      .properties = {
          dbus::ExportedProperty{
              .interface = interfaceName,
              .name = "SupportedOptions",
              .value = dbus::EmptyVariantDictionary{},
              .writable = false,
              .getter = [] {
                return dbus::BasicValue(dbus::EmptyVariantDictionary{});
              },
              .setter = nullptr,
          },
          dbus::ExportedProperty{
              .interface = interfaceName,
              .name = "version",
              .value = std::uint32_t(2),
              .writable = false,
              .getter = nullptr,
              .setter = nullptr,
          },
      },
  };
}

dbus::Slot PortalNotificationService::exportObject() {
  return bus_->exportObject(objectPath, objectDefinition());
}

dbus::Slot PortalNotificationService::watchNotificationActions() {
  return bus_->matchSignal(
      dbus::SignalMatch{
          .sender = NotificationsService::serviceName,
          .path = NotificationsService::objectPath,
          .interface = NotificationsService::interfaceName,
          .member = "ActionInvoked",
      },
      [this](dbus::Message& message) {
        std::uint32_t const daemonId = message.readUint32();
        std::string action = message.readString();
        emitActionInvoked(daemonId, std::move(action));
      });
}

dbus::MethodReply PortalNotificationService::addNotification(dbus::Message& message) {
  std::string appId = message.readString();
  std::string id = message.readString();
  auto notification = message.readVariantDictionary();

  std::string title = stringValue(notification, "title", appId);
  std::string body = stringValue(notification, "body");
  if (body.empty()) {
    body = stringValue(notification, "markup-body");
  }

  PortalNotificationKey key{.appId = appId, .id = id};
  std::uint32_t replacesId = 0;
  if (auto existing = daemonIds_.find(key); existing != daemonIds_.end()) {
    replacesId = existing->second;
  }

  auto reply = bus_->call(dbus::MethodCall{
      .destination = NotificationsService::serviceName,
      .path = NotificationsService::objectPath,
      .interface = NotificationsService::interfaceName,
      .member = "Notify",
      .arguments = {appId,
                    replacesId,
                    std::string(),
                    std::move(title),
                    std::move(body),
                    buttonActions(notification),
                    dbus::EmptyVariantDictionary{},
                    std::int32_t(-1)},
  });
  std::uint32_t const daemonId = reply.readUint32();
  daemonIds_[key] = daemonId;
  portalKeys_[daemonId] = std::move(key);
  return dbus::MethodReply{};
}

dbus::MethodReply PortalNotificationService::removeNotification(dbus::Message& message) {
  PortalNotificationKey key{.appId = message.readString(), .id = message.readString()};
  auto existing = daemonIds_.find(key);
  if (existing == daemonIds_.end()) {
    return dbus::MethodReply{};
  }

  std::uint32_t const daemonId = existing->second;
  try {
    (void)bus_->call(dbus::MethodCall{
        .destination = NotificationsService::serviceName,
        .path = NotificationsService::objectPath,
        .interface = NotificationsService::interfaceName,
        .member = "CloseNotification",
        .arguments = {daemonId},
    });
  } catch (dbus::Error const&) {
  }
  portalKeys_.erase(daemonId);
  daemonIds_.erase(existing);
  return dbus::MethodReply{};
}

void PortalNotificationService::emitActionInvoked(std::uint32_t daemonId, std::string action) {
  auto key = portalKeys_.find(daemonId);
  if (key == portalKeys_.end()) {
    return;
  }
  bus_->emitSignal(objectPath,
                   interfaceName,
                   "ActionInvoked",
                   {key->second.appId,
                    key->second.id,
                    std::move(action),
                    dbus::BasicValue(emptyVariantArray())});
}

} // namespace lambda::system
