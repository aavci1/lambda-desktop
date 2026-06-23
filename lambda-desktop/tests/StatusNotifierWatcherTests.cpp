#include <Lambda/System/StatusNotifierWatcher.hpp>

#include "DBusTestHelpers.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using lambdaui::testing::dbus::pollBus;
using lambdaui::testing::dbus::pumpUntil;
using lambdaui::testing::dbus::startPrivateBus;

template <typename Callback>
class ScopeExit {
public:
  explicit ScopeExit(Callback callback) : callback_(std::move(callback)) {}
  ~ScopeExit() { callback_(); }

private:
  Callback callback_;
};

bool contains(std::vector<std::string> const& values, std::string const& needle) {
  return std::find(values.begin(), values.end(), needle) != values.end();
}

lambdaui::dbus::ByteArray bytes(std::vector<std::uint8_t> values) {
  return lambdaui::dbus::ByteArray{.values = std::move(values)};
}

std::shared_ptr<lambdaui::dbus::StructValue>
pixmapStruct(std::int32_t width, std::int32_t height, std::vector<std::uint8_t> data) {
  return std::make_shared<lambdaui::dbus::StructValue>(
      lambdaui::dbus::StructValue{.signature = "iiay",
                                .fields = {width, height, bytes(std::move(data))}});
}

std::shared_ptr<lambdaui::dbus::ArrayValue>
pixmapArray(std::vector<lambdaui::dbus::BasicValue> values) {
  return std::make_shared<lambdaui::dbus::ArrayValue>(
      lambdaui::dbus::ArrayValue{.elementSignature = "(iiay)", .values = std::move(values)});
}

std::shared_ptr<lambdaui::dbus::StructValue> tooltipValue() {
  return std::make_shared<lambdaui::dbus::StructValue>(
      lambdaui::dbus::StructValue{
          .signature = "sa(iiay)ss",
          .fields = {std::string("software-update"),
                     pixmapArray({pixmapStruct(1, 1, {0xff, 0x00, 0x00, 0xff})}),
                     std::string("Updater tooltip"),
                     std::string("Updates are available")},
      });
}

lambdaui::dbus::ObjectDefinition fakeStatusNotifierItem(std::string title = "Updater",
                                                       std::string iconName = "software-update",
                                                       std::string status = "Active") {
  return lambdaui::dbus::ObjectDefinition{
      .methods = {},
      .properties = {
          lambdaui::dbus::ExportedProperty{
              .interface = "org.kde.StatusNotifierItem",
              .name = "Category",
              .value = std::string("ApplicationStatus"),
              .writable = false,
              .getter = nullptr,
              .setter = nullptr,
          },
          lambdaui::dbus::ExportedProperty{
              .interface = "org.kde.StatusNotifierItem",
              .name = "Id",
              .value = std::string("lambda-updater"),
              .writable = false,
              .getter = nullptr,
              .setter = nullptr,
          },
          lambdaui::dbus::ExportedProperty{
              .interface = "org.kde.StatusNotifierItem",
              .name = "Title",
              .value = std::move(title),
              .writable = false,
              .getter = nullptr,
              .setter = nullptr,
          },
          lambdaui::dbus::ExportedProperty{
              .interface = "org.kde.StatusNotifierItem",
              .name = "Status",
              .value = std::move(status),
              .writable = false,
              .getter = nullptr,
              .setter = nullptr,
          },
          lambdaui::dbus::ExportedProperty{
              .interface = "org.kde.StatusNotifierItem",
              .name = "IconName",
              .value = std::move(iconName),
              .writable = false,
              .getter = nullptr,
              .setter = nullptr,
          },
          lambdaui::dbus::ExportedProperty{
              .interface = "org.kde.StatusNotifierItem",
              .name = "IconPixmap",
              .value = pixmapArray({pixmapStruct(2, 1, {0x10, 0x20, 0x30, 0x40,
                                                        0x50, 0x60, 0x70, 0x80})}),
              .writable = false,
              .getter = nullptr,
              .setter = nullptr,
          },
          lambdaui::dbus::ExportedProperty{
              .interface = "org.kde.StatusNotifierItem",
              .name = "OverlayIconName",
              .value = std::string(""),
              .writable = false,
              .getter = nullptr,
              .setter = nullptr,
          },
          lambdaui::dbus::ExportedProperty{
              .interface = "org.kde.StatusNotifierItem",
              .name = "OverlayIconPixmap",
              .value = pixmapArray({pixmapStruct(1, 1, {0x01, 0x02, 0x03, 0x04})}),
              .writable = false,
              .getter = nullptr,
              .setter = nullptr,
          },
          lambdaui::dbus::ExportedProperty{
              .interface = "org.kde.StatusNotifierItem",
              .name = "AttentionIconName",
              .value = std::string("dialog-warning"),
              .writable = false,
              .getter = nullptr,
              .setter = nullptr,
          },
          lambdaui::dbus::ExportedProperty{
              .interface = "org.kde.StatusNotifierItem",
              .name = "AttentionIconPixmap",
              .value = pixmapArray({pixmapStruct(1, 1, {0xaa, 0xbb, 0xcc, 0xdd})}),
              .writable = false,
              .getter = nullptr,
              .setter = nullptr,
          },
          lambdaui::dbus::ExportedProperty{
              .interface = "org.kde.StatusNotifierItem",
              .name = "ToolTip",
              .value = tooltipValue(),
              .writable = false,
              .getter = nullptr,
              .setter = nullptr,
          },
          lambdaui::dbus::ExportedProperty{
              .interface = "org.kde.StatusNotifierItem",
              .name = "Menu",
              .value = lambdaui::dbus::ObjectPath{"/Menu"},
              .writable = false,
              .getter = nullptr,
              .setter = nullptr,
          },
          lambdaui::dbus::ExportedProperty{
              .interface = "org.kde.StatusNotifierItem",
              .name = "ItemIsMenu",
              .value = false,
              .writable = false,
              .getter = nullptr,
              .setter = nullptr,
          },
      },
  };
}

} // namespace

TEST_CASE("StatusNotifierWatcher support is compile-time declared") {
  CHECK((LAMBDA_HAS_DBUS == 0 || LAMBDA_HAS_DBUS == 1));
}

#if LAMBDA_HAS_DBUS

TEST_CASE("StatusNotifierWatcherService registers hosts and items") {
  auto privateBus = startPrivateBus();
  if (!privateBus) {
    MESSAGE("Skipping StatusNotifierWatcher integration test because a private bus could not be started");
    return;
  }

  auto serviceBus = lambdaui::dbus::Bus::openAddress(privateBus->address);
  auto observer = lambdaui::dbus::Bus::openAddress(privateBus->address);
  serviceBus.requestName(lambdaui::system::StatusNotifierWatcherService::serviceName);

  lambdaui::system::StatusNotifierWatcherService watcher(serviceBus);
  auto objectSlot = watcher.exportObject();
  auto ownerSlot = watcher.watchNameOwners();

  std::atomic<bool> running = true;
  std::thread serviceThread([&] {
    while (running.load()) {
      pollBus(serviceBus, 25);
    }
  });
  ScopeExit stopServiceThread([&] {
    running = false;
    if (serviceThread.joinable()) {
      serviceThread.join();
    }
  });

  int hostRegisteredSignals = 0;
  std::string registeredItem;
  std::string unregisteredItem;
  auto hostSignalSlot = observer.matchSignal(
      lambdaui::dbus::SignalMatch{
          .sender = lambdaui::system::StatusNotifierWatcherService::serviceName,
          .path = lambdaui::system::StatusNotifierWatcherService::objectPath,
          .interface = lambdaui::system::StatusNotifierWatcherService::interfaceName,
          .member = "StatusNotifierHostRegistered",
      },
      [&](lambdaui::dbus::Message&) {
        ++hostRegisteredSignals;
      });
  auto itemRegisteredSlot = observer.matchSignal(
      lambdaui::dbus::SignalMatch{
          .sender = lambdaui::system::StatusNotifierWatcherService::serviceName,
          .path = lambdaui::system::StatusNotifierWatcherService::objectPath,
          .interface = lambdaui::system::StatusNotifierWatcherService::interfaceName,
          .member = "StatusNotifierItemRegistered",
      },
      [&](lambdaui::dbus::Message& message) {
        registeredItem = message.readString();
      });
  auto itemUnregisteredSlot = observer.matchSignal(
      lambdaui::dbus::SignalMatch{
          .sender = lambdaui::system::StatusNotifierWatcherService::serviceName,
          .path = lambdaui::system::StatusNotifierWatcherService::objectPath,
          .interface = lambdaui::system::StatusNotifierWatcherService::interfaceName,
          .member = "StatusNotifierItemUnregistered",
      },
      [&](lambdaui::dbus::Message& message) {
        unregisteredItem = message.readString();
      });

  CHECK(std::get<std::int32_t>(observer.getProperty(lambdaui::dbus::PropertyAddress{
                                .destination = lambdaui::system::StatusNotifierWatcherService::serviceName,
                                .path = lambdaui::system::StatusNotifierWatcherService::objectPath,
                                .interface = lambdaui::system::StatusNotifierWatcherService::interfaceName,
                                .name = "ProtocolVersion",
                            },
                            "i")) == lambdaui::system::StatusNotifierWatcherService::protocolVersion);
  CHECK(std::get<bool>(observer.getProperty(lambdaui::dbus::PropertyAddress{
                         .destination = lambdaui::system::StatusNotifierWatcherService::serviceName,
                         .path = lambdaui::system::StatusNotifierWatcherService::objectPath,
                         .interface = lambdaui::system::StatusNotifierWatcherService::interfaceName,
                         .name = "IsStatusNotifierHostRegistered",
                     },
                     "b")) == false);

  std::string const hostName = "org.freedesktop.StatusNotifierHost.lambda-test";
  auto hostBus = lambdaui::dbus::Bus::openAddress(privateBus->address);
  hostBus.requestName(hostName);
  auto hostReply = hostBus.call(lambdaui::dbus::MethodCall{
      .destination = lambdaui::system::StatusNotifierWatcherService::serviceName,
      .path = lambdaui::system::StatusNotifierWatcherService::objectPath,
      .interface = lambdaui::system::StatusNotifierWatcherService::interfaceName,
      .member = "RegisterStatusNotifierHost",
      .arguments = {hostName},
  });
  CHECK(hostReply.valid());
  serviceBus.flush();
  CHECK(pumpUntil(observer,
                  [&] { return hostRegisteredSignals == 1; },
                  std::chrono::milliseconds(500)));
  CHECK(std::get<bool>(observer.getProperty(lambdaui::dbus::PropertyAddress{
                         .destination = lambdaui::system::StatusNotifierWatcherService::serviceName,
                         .path = lambdaui::system::StatusNotifierWatcherService::objectPath,
                         .interface = lambdaui::system::StatusNotifierWatcherService::interfaceName,
                         .name = "IsStatusNotifierHostRegistered",
                     },
                     "b")) == true);

  std::string const itemName = "org.freedesktop.StatusNotifierItem.lambda-test-1";
  {
    auto itemBus = lambdaui::dbus::Bus::openAddress(privateBus->address);
    itemBus.requestName(itemName);
    auto itemReply = itemBus.call(lambdaui::dbus::MethodCall{
        .destination = lambdaui::system::StatusNotifierWatcherService::serviceName,
        .path = lambdaui::system::StatusNotifierWatcherService::objectPath,
        .interface = lambdaui::system::StatusNotifierWatcherService::interfaceName,
        .member = "RegisterStatusNotifierItem",
        .arguments = {itemName},
    });
    CHECK(itemReply.valid());
    serviceBus.flush();
    CHECK(pumpUntil(observer,
                    [&] { return registeredItem == itemName; },
                    std::chrono::milliseconds(500)));

    auto items = std::get<lambdaui::dbus::StringArray>(observer.getProperty(
        lambdaui::dbus::PropertyAddress{
            .destination = lambdaui::system::StatusNotifierWatcherService::serviceName,
            .path = lambdaui::system::StatusNotifierWatcherService::objectPath,
            .interface = lambdaui::system::StatusNotifierWatcherService::interfaceName,
            .name = "RegisteredStatusNotifierItems",
        },
        "as")).values;
    CHECK(contains(items, itemName));

    auto allReply = observer.call(lambdaui::dbus::MethodCall{
        .destination = lambdaui::system::StatusNotifierWatcherService::serviceName,
        .path = lambdaui::system::StatusNotifierWatcherService::objectPath,
        .interface = "org.freedesktop.DBus.Properties",
        .member = "GetAll",
        .arguments = {std::string(lambdaui::system::StatusNotifierWatcherService::interfaceName)},
    });
    auto all = allReply.readVariantDictionary();
    auto allItems = std::get<lambdaui::dbus::StringArray>(
        all.values.at("RegisteredStatusNotifierItems")).values;
    CHECK(contains(allItems, itemName));
    CHECK(std::get<bool>(all.values.at("IsStatusNotifierHostRegistered")) == true);
    CHECK(std::get<std::int32_t>(all.values.at("ProtocolVersion")) ==
          lambdaui::system::StatusNotifierWatcherService::protocolVersion);
  }

  CHECK(pumpUntil(observer,
                  [&] { return unregisteredItem == itemName; },
                  std::chrono::milliseconds(1000)));

  auto pathItemBus = lambdaui::dbus::Bus::openAddress(privateBus->address);
  std::string const pathItemName = pathItemBus.uniqueName();
  registeredItem.clear();
  auto pathReply = pathItemBus.call(lambdaui::dbus::MethodCall{
      .destination = lambdaui::system::StatusNotifierWatcherService::serviceName,
      .path = lambdaui::system::StatusNotifierWatcherService::objectPath,
      .interface = lambdaui::system::StatusNotifierWatcherService::interfaceName,
      .member = "RegisterStatusNotifierItem",
      .arguments = {std::string(lambdaui::system::StatusNotifierWatcherService::defaultItemObjectPath)},
  });
  CHECK(pathReply.valid());
  serviceBus.flush();
  CHECK(pumpUntil(observer,
                  [&] { return registeredItem == pathItemName; },
                  std::chrono::milliseconds(500)));
  REQUIRE(watcher.items().size() == 1);
  CHECK(watcher.items()[0].serviceName == pathItemName);
  CHECK(watcher.items()[0].objectPath ==
        lambdaui::system::StatusNotifierWatcherService::defaultItemObjectPath);
}

TEST_CASE("StatusNotifierWatcherClient reads and watches registered items") {
  auto privateBus = startPrivateBus();
  if (!privateBus) {
    MESSAGE("Skipping StatusNotifierWatcher client test because a private bus could not be started");
    return;
  }

  auto serviceBus = lambdaui::dbus::Bus::openAddress(privateBus->address);
  serviceBus.requestName(lambdaui::system::StatusNotifierWatcherService::serviceName);

  lambdaui::system::StatusNotifierWatcherService watcher(serviceBus);
  auto objectSlot = watcher.exportObject();
  auto ownerSlot = watcher.watchNameOwners();

  std::atomic<bool> running = true;
  std::thread serviceThread([&] {
    while (running.load()) {
      pollBus(serviceBus, 25);
    }
  });
  ScopeExit stopServiceThread([&] {
    running = false;
    if (serviceThread.joinable()) {
      serviceThread.join();
    }
  });

  auto client = lambdaui::system::StatusNotifierWatcherClient(
      lambdaui::dbus::Bus::openAddress(privateBus->address));
  client.registerHost("org.freedesktop.StatusNotifierHost.lambda-test-client");
  CHECK(watcher.isHostRegistered());

  int refreshes = 0;
  auto watch = client.watchItems([&] {
    ++refreshes;
  });

  std::string const itemName = "org.freedesktop.StatusNotifierItem.lambda-test-client";
  auto itemBus = lambdaui::dbus::Bus::openAddress(privateBus->address);
  itemBus.requestName(itemName);
  auto itemReply = itemBus.call(lambdaui::dbus::MethodCall{
      .destination = lambdaui::system::StatusNotifierWatcherService::serviceName,
      .path = lambdaui::system::StatusNotifierWatcherService::objectPath,
      .interface = lambdaui::system::StatusNotifierWatcherService::interfaceName,
      .member = "RegisterStatusNotifierItem",
      .arguments = {itemName},
  });
  CHECK(itemReply.valid());
  serviceBus.flush();
  CHECK(pumpUntil(client.bus(), [&] { return refreshes > 0; }, std::chrono::milliseconds(500)));

  auto items = client.registeredItems();
  CHECK(contains(items, itemName));

  auto missingProperties = client.registeredItemProperties();
  REQUIRE(missingProperties.size() == 1);
  CHECK(missingProperties.front().address.id == itemName);
  CHECK_FALSE(missingProperties.front().propertiesAvailable);
}

TEST_CASE("StatusNotifierWatcherClient reads StatusNotifierItem properties") {
  auto privateBus = startPrivateBus();
  if (!privateBus) {
    MESSAGE("Skipping StatusNotifierItem properties test because a private bus could not be started");
    return;
  }

  auto serviceBus = lambdaui::dbus::Bus::openAddress(privateBus->address);
  serviceBus.requestName(lambdaui::system::StatusNotifierWatcherService::serviceName);

  lambdaui::system::StatusNotifierWatcherService watcher(serviceBus);
  auto watcherSlot = watcher.exportObject();
  auto ownerSlot = watcher.watchNameOwners();

  std::atomic<bool> serviceRunning = true;
  std::thread serviceThread([&] {
    while (serviceRunning.load()) {
      pollBus(serviceBus, 25);
    }
  });
  ScopeExit stopServiceThread([&] {
    serviceRunning = false;
    if (serviceThread.joinable()) {
      serviceThread.join();
    }
  });

  std::string const itemName = "org.freedesktop.StatusNotifierItem.lambda-properties";
  auto itemBus = lambdaui::dbus::Bus::openAddress(privateBus->address);
  itemBus.requestName(itemName);
  auto itemSlot = itemBus.exportObject("/StatusNotifierItem", fakeStatusNotifierItem());

  auto registerReply = itemBus.call(lambdaui::dbus::MethodCall{
      .destination = lambdaui::system::StatusNotifierWatcherService::serviceName,
      .path = lambdaui::system::StatusNotifierWatcherService::objectPath,
      .interface = lambdaui::system::StatusNotifierWatcherService::interfaceName,
      .member = "RegisterStatusNotifierItem",
      .arguments = {itemName},
  });
  CHECK(registerReply.valid());

  std::atomic<bool> itemRunning = true;
  std::thread itemThread([&] {
    while (itemRunning.load()) {
      pollBus(itemBus, 25);
    }
  });
  ScopeExit stopItemThread([&] {
    itemRunning = false;
    if (itemThread.joinable()) {
      itemThread.join();
    }
  });

  auto client = lambdaui::system::StatusNotifierWatcherClient(
      lambdaui::dbus::Bus::openAddress(privateBus->address));
  auto addresses = client.registeredItemAddresses();
  REQUIRE(addresses.size() == 1);
  CHECK(addresses.front().id == itemName);
  CHECK(addresses.front().serviceName == itemName);
  CHECK(addresses.front().objectPath ==
        lambdaui::system::StatusNotifierWatcherService::defaultItemObjectPath);

  auto properties = client.readItemProperties(addresses.front());
  CHECK(properties.propertiesAvailable);
  CHECK(properties.category == "ApplicationStatus");
  CHECK(properties.itemId == "lambda-updater");
  CHECK(properties.title == "Updater");
  CHECK(properties.status == "Active");
  CHECK(properties.iconName == "software-update");
  REQUIRE(properties.iconPixmaps.size() == 1);
  CHECK(properties.iconPixmaps.front().width == 2);
  CHECK(properties.iconPixmaps.front().height == 1);
  CHECK(properties.iconPixmaps.front().data == std::vector<std::uint8_t>{0x10, 0x20, 0x30, 0x40,
                                                                          0x50, 0x60, 0x70, 0x80});
  REQUIRE(properties.overlayIconPixmaps.size() == 1);
  CHECK(properties.overlayIconPixmaps.front().width == 1);
  CHECK(properties.attentionIconName == "dialog-warning");
  REQUIRE(properties.attentionIconPixmaps.size() == 1);
  CHECK(properties.attentionIconPixmaps.front().data == std::vector<std::uint8_t>{0xaa, 0xbb, 0xcc, 0xdd});
  CHECK(properties.tooltipAvailable);
  CHECK(properties.tooltip.iconName == "software-update");
  REQUIRE(properties.tooltip.iconPixmaps.size() == 1);
  CHECK(properties.tooltip.iconPixmaps.front().height == 1);
  CHECK(properties.tooltip.title == "Updater tooltip");
  CHECK(properties.tooltip.description == "Updates are available");
  CHECK(properties.menu.value == "/Menu");
  CHECK(!properties.itemIsMenu);

  int propertyRefreshes = 0;
  auto propertyWatch = client.watchItemProperties(addresses.front(), [&] {
    ++propertyRefreshes;
  });
  itemBus.emitPropertiesChanged(
      "/StatusNotifierItem",
      "org.kde.StatusNotifierItem",
      lambdaui::dbus::VariantDictionary{.values = {{"Title", std::string("Updater ready")}}},
      {});
  itemBus.flush();
  CHECK(pumpUntil(client.bus(), [&] { return propertyRefreshes == 1; }, std::chrono::milliseconds(500)));

  auto encodedAddress =
      lambdaui::system::parseStatusNotifierItemAddress("org.example.Tray/CustomItem");
  CHECK(encodedAddress.serviceName == "org.example.Tray");
  CHECK(encodedAddress.objectPath == "/CustomItem");
}

#endif
