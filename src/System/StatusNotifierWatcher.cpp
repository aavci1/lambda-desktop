#include <Lambda/System/StatusNotifierWatcher.hpp>

#include <algorithm>
#include <memory>
#include <utility>

namespace lambda::system {

namespace {

constexpr char kDBusService[] = "org.freedesktop.DBus";
constexpr char kDBusPath[] = "/org/freedesktop/DBus";
constexpr char kDBusInterface[] = "org.freedesktop.DBus";

bool startsWithObjectPath(std::string const& value) {
  return !value.empty() && value.front() == '/';
}

} // namespace

StatusNotifierWatcherService::StatusNotifierWatcherService(dbus::Bus& bus) : bus_(&bus) {}

dbus::Slot StatusNotifierWatcherService::exportObject() {
  return bus_->exportObject(
      objectPath,
      dbus::ObjectDefinition{
          .methods = {
              dbus::ExportedMethod{
                  .interface = interfaceName,
                  .member = "RegisterStatusNotifierItem",
                  .handler = [this](dbus::Message& message) {
                    auto serviceOrPath = message.readString();
                    if (!registerStatusNotifierItem(std::move(serviceOrPath), message.sender())) {
                      return dbus::MethodReply::error("org.freedesktop.DBus.Error.InvalidArgs",
                                                      "StatusNotifierItem service name is empty");
                    }
                    return dbus::MethodReply{};
                  },
              },
              dbus::ExportedMethod{
                  .interface = interfaceName,
                  .member = "RegisterStatusNotifierHost",
                  .handler = [this](dbus::Message& message) {
                    auto serviceOrPath = message.readString();
                    if (!registerStatusNotifierHost(std::move(serviceOrPath), message.sender())) {
                      return dbus::MethodReply::error("org.freedesktop.DBus.Error.InvalidArgs",
                                                      "StatusNotifierHost service name is empty");
                    }
                    return dbus::MethodReply{};
                  },
              },
          },
          .properties = {
              dbus::ExportedProperty{
                  .interface = interfaceName,
                  .name = "RegisteredStatusNotifierItems",
                  .value = dbus::StringArray{},
                  .writable = false,
                  .getter = [this] {
                    return dbus::BasicValue(dbus::StringArray{registeredItemServices()});
                  },
                  .setter = nullptr,
              },
              dbus::ExportedProperty{
                  .interface = interfaceName,
                  .name = "IsStatusNotifierHostRegistered",
                  .value = false,
                  .writable = false,
                  .getter = [this] {
                    return dbus::BasicValue(isHostRegistered());
                  },
                  .setter = nullptr,
              },
              dbus::ExportedProperty{
                  .interface = interfaceName,
                  .name = "ProtocolVersion",
                  .value = protocolVersion,
                  .writable = false,
                  .getter = nullptr,
                  .setter = nullptr,
              },
          },
      });
}

dbus::Slot StatusNotifierWatcherService::watchNameOwners() {
  return bus_->matchSignal(
      dbus::SignalMatch{
          .sender = kDBusService,
          .path = kDBusPath,
          .interface = kDBusInterface,
          .member = "NameOwnerChanged",
      },
      [this](dbus::Message& message) {
        std::string name = message.readString();
        (void)message.readString();
        std::string newOwner = message.readString();
        if (newOwner.empty()) {
          (void)removeName(name);
        }
      });
}

bool StatusNotifierWatcherService::registerStatusNotifierItem(std::string serviceOrPath,
                                                              std::string sender) {
  auto normalized = normalizeItem(std::move(serviceOrPath), std::move(sender));
  if (!normalized) {
    return false;
  }

  auto existing = std::find_if(items_.begin(), items_.end(), [&](auto const& item) {
    return item.serviceName == normalized->serviceName;
  });
  if (existing != items_.end()) {
    existing->objectPath = std::move(normalized->objectPath);
    existing->ownerName = std::move(normalized->ownerName);
    return true;
  }

  std::string service = normalized->serviceName;
  items_.push_back(StatusNotifierItemRegistration{
      .serviceName = std::move(normalized->serviceName),
      .objectPath = std::move(normalized->objectPath),
      .ownerName = std::move(normalized->ownerName),
  });
  emitItemRegistered(service);
  return true;
}

bool StatusNotifierWatcherService::registerStatusNotifierHost(std::string serviceOrPath,
                                                              std::string sender) {
  auto service = normalizeService(std::move(serviceOrPath), std::move(sender));
  if (!service) {
    return false;
  }
  if (std::find(hosts_.begin(), hosts_.end(), *service) != hosts_.end()) {
    return true;
  }
  hosts_.push_back(*service);
  emitHostRegistered();
  return true;
}

bool StatusNotifierWatcherService::removeName(std::string const& name) {
  bool removed = false;
  for (auto it = items_.begin(); it != items_.end();) {
    if (it->serviceName == name || it->ownerName == name) {
      std::string service = it->serviceName;
      it = items_.erase(it);
      emitItemUnregistered(service);
      removed = true;
    } else {
      ++it;
    }
  }

  auto const oldHostCount = hosts_.size();
  hosts_.erase(std::remove(hosts_.begin(), hosts_.end(), name), hosts_.end());
  return removed || hosts_.size() != oldHostCount;
}

std::vector<std::string> StatusNotifierWatcherService::registeredItemServices() const {
  std::vector<std::string> services;
  services.reserve(items_.size());
  for (auto const& item : items_) {
    services.push_back(item.serviceName);
  }
  return services;
}

std::optional<StatusNotifierWatcherService::NormalizedRegistration>
StatusNotifierWatcherService::normalizeItem(std::string serviceOrPath, std::string sender) const {
  if (serviceOrPath.empty()) {
    return std::nullopt;
  }
  if (startsWithObjectPath(serviceOrPath)) {
    if (sender.empty()) {
      return std::nullopt;
    }
    return NormalizedRegistration{
        .serviceName = sender,
        .objectPath = std::move(serviceOrPath),
        .ownerName = std::move(sender),
    };
  }
  std::string owner = serviceOrPath;
  auto const slash = serviceOrPath.find('/');
  if (slash != std::string::npos) {
    owner = serviceOrPath.substr(0, slash);
  }
  return NormalizedRegistration{
      .serviceName = std::move(serviceOrPath),
      .objectPath = defaultItemObjectPath,
      .ownerName = std::move(owner),
  };
}

std::optional<std::string> StatusNotifierWatcherService::normalizeService(std::string serviceOrPath,
                                                                          std::string sender) const {
  if (serviceOrPath.empty()) {
    return std::nullopt;
  }
  if (startsWithObjectPath(serviceOrPath)) {
    if (sender.empty()) {
      return std::nullopt;
    }
    return sender;
  }
  return serviceOrPath;
}

void StatusNotifierWatcherService::emitItemRegistered(std::string const& service) const {
  bus_->emitSignal(objectPath, interfaceName, "StatusNotifierItemRegistered", {service});
}

void StatusNotifierWatcherService::emitItemUnregistered(std::string const& service) const {
  bus_->emitSignal(objectPath, interfaceName, "StatusNotifierItemUnregistered", {service});
}

void StatusNotifierWatcherService::emitHostRegistered() const {
  bus_->emitSignal(objectPath, interfaceName, "StatusNotifierHostRegistered");
}

StatusNotifierWatcherClient::StatusNotifierWatcherClient(dbus::Bus bus) : bus_(std::move(bus)) {}

StatusNotifierWatcherClient StatusNotifierWatcherClient::connectSession() {
  return StatusNotifierWatcherClient(dbus::Bus::open(dbus::BusType::Session));
}

void StatusNotifierWatcherClient::registerHost(std::string serviceName) {
  (void)bus_.call(dbus::MethodCall{
      .destination = StatusNotifierWatcherService::serviceName,
      .path = StatusNotifierWatcherService::objectPath,
      .interface = StatusNotifierWatcherService::interfaceName,
      .member = "RegisterStatusNotifierHost",
      .arguments = {std::move(serviceName)},
  });
}

std::vector<std::string> StatusNotifierWatcherClient::registeredItems() {
  return std::get<dbus::StringArray>(bus_.getProperty(dbus::PropertyAddress{
                                                          .destination = StatusNotifierWatcherService::serviceName,
                                                          .path = StatusNotifierWatcherService::objectPath,
                                                          .interface = StatusNotifierWatcherService::interfaceName,
                                                          .name = "RegisteredStatusNotifierItems",
                                                      },
                                                      "as"))
      .values;
}

StatusNotifierItemsWatch StatusNotifierWatcherClient::watchItems(std::function<void()> handler) {
  auto sharedHandler = std::make_shared<std::function<void()>>(std::move(handler));
  auto notify = [sharedHandler](dbus::Message&) {
    if (sharedHandler && *sharedHandler) {
      (*sharedHandler)();
    }
  };
  return StatusNotifierItemsWatch{
      .itemRegistered =
          bus_.matchSignal(
              dbus::SignalMatch{
                  .sender = StatusNotifierWatcherService::serviceName,
                  .path = StatusNotifierWatcherService::objectPath,
                  .interface = StatusNotifierWatcherService::interfaceName,
                  .member = "StatusNotifierItemRegistered",
              },
              notify),
      .itemUnregistered =
          bus_.matchSignal(
              dbus::SignalMatch{
                  .sender = StatusNotifierWatcherService::serviceName,
                  .path = StatusNotifierWatcherService::objectPath,
                  .interface = StatusNotifierWatcherService::interfaceName,
                  .member = "StatusNotifierItemUnregistered",
              },
              notify),
  };
}

} // namespace lambda::system
