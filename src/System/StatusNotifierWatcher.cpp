#include <Lambda/System/StatusNotifierWatcher.hpp>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <utility>
#include <variant>

namespace lambda::system {

namespace {

constexpr char kDBusService[] = "org.freedesktop.DBus";
constexpr char kDBusPath[] = "/org/freedesktop/DBus";
constexpr char kDBusInterface[] = "org.freedesktop.DBus";
constexpr char kStatusNotifierItemInterface[] = "org.kde.StatusNotifierItem";
constexpr char kFreedesktopStatusNotifierItemInterface[] = "org.freedesktop.StatusNotifierItem";

bool startsWithObjectPath(std::string const& value) {
  return !value.empty() && value.front() == '/';
}

std::string registeredItemId(StatusNotifierItemRegistration const& item) {
  if (item.objectPath.empty() ||
      item.objectPath == StatusNotifierWatcherService::defaultItemObjectPath) {
    return item.serviceName;
  }
  return item.serviceName + item.objectPath;
}

std::optional<StatusNotifierItemPixmap>
pixmapFromValue(dbus::BasicValue const& value) {
  auto const* structure = std::get_if<std::shared_ptr<dbus::StructValue>>(&value);
  if (!structure || !*structure || (*structure)->fields.size() != 3) {
    return std::nullopt;
  }
  auto const* width = std::get_if<std::int32_t>(&(*structure)->fields[0]);
  auto const* height = std::get_if<std::int32_t>(&(*structure)->fields[1]);
  auto const* data = std::get_if<dbus::ByteArray>(&(*structure)->fields[2]);
  if (!width || !height || !data || *width <= 0 || *height <= 0) {
    return std::nullopt;
  }
  return StatusNotifierItemPixmap{
      .width = *width,
      .height = *height,
      .data = data->values,
  };
}

std::vector<StatusNotifierItemPixmap>
pixmapsFromValue(std::shared_ptr<dbus::ArrayValue> const& value) {
  std::vector<StatusNotifierItemPixmap> pixmaps;
  if (!value) {
    return pixmaps;
  }
  pixmaps.reserve(value->values.size());
  for (auto const& entry : value->values) {
    if (auto pixmap = pixmapFromValue(entry)) {
      pixmaps.push_back(std::move(*pixmap));
    }
  }
  return pixmaps;
}

std::optional<StatusNotifierItemTooltip>
tooltipFromValue(std::shared_ptr<dbus::StructValue> const& value) {
  if (!value || value->fields.size() != 4) {
    return std::nullopt;
  }
  auto const* iconName = std::get_if<std::string>(&value->fields[0]);
  auto const* iconPixmaps = std::get_if<std::shared_ptr<dbus::ArrayValue>>(&value->fields[1]);
  auto const* title = std::get_if<std::string>(&value->fields[2]);
  auto const* description = std::get_if<std::string>(&value->fields[3]);
  if (!iconName || !iconPixmaps || !title || !description) {
    return std::nullopt;
  }
  return StatusNotifierItemTooltip{
      .iconName = *iconName,
      .iconPixmaps = pixmapsFromValue(*iconPixmaps),
      .title = *title,
      .description = *description,
  };
}

template <typename T>
std::optional<T> readItemProperty(dbus::Bus& bus,
                                  StatusNotifierItemAddress const& address,
                                  std::string const& property,
                                  std::string_view signature) {
  for (char const* interface :
       std::array{kStatusNotifierItemInterface, kFreedesktopStatusNotifierItemInterface}) {
    try {
      return std::get<T>(bus.getProperty(dbus::PropertyAddress{
                                             .destination = address.serviceName,
                                             .path = address.objectPath,
                                             .interface = interface,
                                             .name = property,
                                         },
                                         signature));
    } catch (dbus::Error const&) {
    } catch (std::bad_variant_access const&) {
    }
  }
  return std::nullopt;
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
    services.push_back(registeredItemId(item));
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
  auto const slash = serviceOrPath.find('/');
  if (slash != std::string::npos) {
    std::string owner = serviceOrPath.substr(0, slash);
    std::string path = serviceOrPath.substr(slash);
    if (owner.empty() || path.empty()) {
      return std::nullopt;
    }
    return NormalizedRegistration{
        .serviceName = owner,
        .objectPath = std::move(path),
        .ownerName = std::move(owner),
    };
  }
  std::string owner = serviceOrPath;
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

StatusNotifierItemAddress parseStatusNotifierItemAddress(std::string id) {
  StatusNotifierItemAddress address;
  address.id = id;
  auto const slash = id.find('/');
  if (slash == std::string::npos) {
    address.serviceName = std::move(id);
    address.objectPath = StatusNotifierWatcherService::defaultItemObjectPath;
    return address;
  }
  address.serviceName = id.substr(0, slash);
  address.objectPath = id.substr(slash);
  return address;
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

std::vector<StatusNotifierItemAddress> StatusNotifierWatcherClient::registeredItemAddresses() {
  auto items = registeredItems();
  std::vector<StatusNotifierItemAddress> addresses;
  addresses.reserve(items.size());
  for (auto& item : items) {
    addresses.push_back(parseStatusNotifierItemAddress(std::move(item)));
  }
  return addresses;
}

StatusNotifierItemProperties
StatusNotifierWatcherClient::readItemProperties(StatusNotifierItemAddress const& address) {
  StatusNotifierItemProperties properties;
  properties.address = address;

  if (auto value = readItemProperty<std::string>(bus_, address, "Category", "s")) {
    properties.category = *value;
    properties.propertiesAvailable = true;
  }
  if (auto value = readItemProperty<std::string>(bus_, address, "Id", "s")) {
    properties.itemId = *value;
    properties.propertiesAvailable = true;
  }
  if (auto value = readItemProperty<std::string>(bus_, address, "Title", "s")) {
    properties.title = *value;
    properties.propertiesAvailable = true;
  }
  if (auto value = readItemProperty<std::string>(bus_, address, "Status", "s")) {
    properties.status = *value;
    properties.propertiesAvailable = true;
  }
  if (auto value = readItemProperty<std::string>(bus_, address, "IconName", "s")) {
    properties.iconName = *value;
    properties.propertiesAvailable = true;
  }
  if (auto value = readItemProperty<std::shared_ptr<dbus::ArrayValue>>(
          bus_, address, "IconPixmap", "a(iiay)")) {
    properties.iconPixmaps = pixmapsFromValue(*value);
    properties.propertiesAvailable = true;
  }
  if (auto value = readItemProperty<std::string>(bus_, address, "OverlayIconName", "s")) {
    properties.overlayIconName = *value;
    properties.propertiesAvailable = true;
  }
  if (auto value = readItemProperty<std::shared_ptr<dbus::ArrayValue>>(
          bus_, address, "OverlayIconPixmap", "a(iiay)")) {
    properties.overlayIconPixmaps = pixmapsFromValue(*value);
    properties.propertiesAvailable = true;
  }
  if (auto value = readItemProperty<std::string>(bus_, address, "AttentionIconName", "s")) {
    properties.attentionIconName = *value;
    properties.propertiesAvailable = true;
  }
  if (auto value = readItemProperty<std::shared_ptr<dbus::ArrayValue>>(
          bus_, address, "AttentionIconPixmap", "a(iiay)")) {
    properties.attentionIconPixmaps = pixmapsFromValue(*value);
    properties.propertiesAvailable = true;
  }
  if (auto value = readItemProperty<std::shared_ptr<dbus::StructValue>>(
          bus_, address, "ToolTip", "(sa(iiay)ss)")) {
    if (auto tooltip = tooltipFromValue(*value)) {
      properties.tooltip = std::move(*tooltip);
      properties.tooltipAvailable = true;
    }
    properties.propertiesAvailable = true;
  }
  if (auto value = readItemProperty<dbus::ObjectPath>(bus_, address, "Menu", "o")) {
    properties.menu = *value;
    properties.propertiesAvailable = true;
  }
  if (auto value = readItemProperty<bool>(bus_, address, "ItemIsMenu", "b")) {
    properties.itemIsMenu = *value;
    properties.propertiesAvailable = true;
  }
  return properties;
}

std::vector<StatusNotifierItemProperties> StatusNotifierWatcherClient::registeredItemProperties() {
  auto addresses = registeredItemAddresses();
  std::vector<StatusNotifierItemProperties> items;
  items.reserve(addresses.size());
  for (auto const& address : addresses) {
    items.push_back(readItemProperties(address));
  }
  return items;
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

dbus::Slot StatusNotifierWatcherClient::watchItemProperties(StatusNotifierItemAddress const& address,
                                                            std::function<void()> handler) {
  auto sharedHandler = std::make_shared<std::function<void()>>(std::move(handler));
  return bus_.matchSignal(
      dbus::SignalMatch{
          .sender = address.serviceName,
          .path = address.objectPath,
          .interface = "org.freedesktop.DBus.Properties",
          .member = "PropertiesChanged",
      },
      [sharedHandler](dbus::Message& message) {
        auto const changed = dbus::readPropertiesChanged(message);
        if (changed.interface == kStatusNotifierItemInterface ||
            changed.interface == kFreedesktopStatusNotifierItemInterface) {
          if (sharedHandler && *sharedHandler) {
            (*sharedHandler)();
          }
        }
      });
}

} // namespace lambda::system
