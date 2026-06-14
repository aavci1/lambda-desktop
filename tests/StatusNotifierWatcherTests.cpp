#include <Lambda/System/StatusNotifierWatcher.hpp>

#include "DBusTestHelpers.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <utility>

namespace {

using lambda::testing::dbus::pollBus;
using lambda::testing::dbus::pumpUntil;
using lambda::testing::dbus::startPrivateBus;

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

  auto serviceBus = lambda::dbus::Bus::openAddress(privateBus->address);
  auto observer = lambda::dbus::Bus::openAddress(privateBus->address);
  serviceBus.requestName(lambda::system::StatusNotifierWatcherService::serviceName);

  lambda::system::StatusNotifierWatcherService watcher(serviceBus);
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
      lambda::dbus::SignalMatch{
          .sender = lambda::system::StatusNotifierWatcherService::serviceName,
          .path = lambda::system::StatusNotifierWatcherService::objectPath,
          .interface = lambda::system::StatusNotifierWatcherService::interfaceName,
          .member = "StatusNotifierHostRegistered",
      },
      [&](lambda::dbus::Message&) {
        ++hostRegisteredSignals;
      });
  auto itemRegisteredSlot = observer.matchSignal(
      lambda::dbus::SignalMatch{
          .sender = lambda::system::StatusNotifierWatcherService::serviceName,
          .path = lambda::system::StatusNotifierWatcherService::objectPath,
          .interface = lambda::system::StatusNotifierWatcherService::interfaceName,
          .member = "StatusNotifierItemRegistered",
      },
      [&](lambda::dbus::Message& message) {
        registeredItem = message.readString();
      });
  auto itemUnregisteredSlot = observer.matchSignal(
      lambda::dbus::SignalMatch{
          .sender = lambda::system::StatusNotifierWatcherService::serviceName,
          .path = lambda::system::StatusNotifierWatcherService::objectPath,
          .interface = lambda::system::StatusNotifierWatcherService::interfaceName,
          .member = "StatusNotifierItemUnregistered",
      },
      [&](lambda::dbus::Message& message) {
        unregisteredItem = message.readString();
      });

  CHECK(std::get<std::int32_t>(observer.getProperty(lambda::dbus::PropertyAddress{
                                .destination = lambda::system::StatusNotifierWatcherService::serviceName,
                                .path = lambda::system::StatusNotifierWatcherService::objectPath,
                                .interface = lambda::system::StatusNotifierWatcherService::interfaceName,
                                .name = "ProtocolVersion",
                            },
                            "i")) == lambda::system::StatusNotifierWatcherService::protocolVersion);
  CHECK(std::get<bool>(observer.getProperty(lambda::dbus::PropertyAddress{
                         .destination = lambda::system::StatusNotifierWatcherService::serviceName,
                         .path = lambda::system::StatusNotifierWatcherService::objectPath,
                         .interface = lambda::system::StatusNotifierWatcherService::interfaceName,
                         .name = "IsStatusNotifierHostRegistered",
                     },
                     "b")) == false);

  std::string const hostName = "org.freedesktop.StatusNotifierHost.lambda-test";
  auto hostBus = lambda::dbus::Bus::openAddress(privateBus->address);
  hostBus.requestName(hostName);
  auto hostReply = hostBus.call(lambda::dbus::MethodCall{
      .destination = lambda::system::StatusNotifierWatcherService::serviceName,
      .path = lambda::system::StatusNotifierWatcherService::objectPath,
      .interface = lambda::system::StatusNotifierWatcherService::interfaceName,
      .member = "RegisterStatusNotifierHost",
      .arguments = {hostName},
  });
  CHECK(hostReply.valid());
  serviceBus.flush();
  CHECK(pumpUntil(observer,
                  [&] { return hostRegisteredSignals == 1; },
                  std::chrono::milliseconds(500)));
  CHECK(std::get<bool>(observer.getProperty(lambda::dbus::PropertyAddress{
                         .destination = lambda::system::StatusNotifierWatcherService::serviceName,
                         .path = lambda::system::StatusNotifierWatcherService::objectPath,
                         .interface = lambda::system::StatusNotifierWatcherService::interfaceName,
                         .name = "IsStatusNotifierHostRegistered",
                     },
                     "b")) == true);

  std::string const itemName = "org.freedesktop.StatusNotifierItem.lambda-test-1";
  {
    auto itemBus = lambda::dbus::Bus::openAddress(privateBus->address);
    itemBus.requestName(itemName);
    auto itemReply = itemBus.call(lambda::dbus::MethodCall{
        .destination = lambda::system::StatusNotifierWatcherService::serviceName,
        .path = lambda::system::StatusNotifierWatcherService::objectPath,
        .interface = lambda::system::StatusNotifierWatcherService::interfaceName,
        .member = "RegisterStatusNotifierItem",
        .arguments = {itemName},
    });
    CHECK(itemReply.valid());
    serviceBus.flush();
    CHECK(pumpUntil(observer,
                    [&] { return registeredItem == itemName; },
                    std::chrono::milliseconds(500)));

    auto items = std::get<lambda::dbus::StringArray>(observer.getProperty(
        lambda::dbus::PropertyAddress{
            .destination = lambda::system::StatusNotifierWatcherService::serviceName,
            .path = lambda::system::StatusNotifierWatcherService::objectPath,
            .interface = lambda::system::StatusNotifierWatcherService::interfaceName,
            .name = "RegisteredStatusNotifierItems",
        },
        "as")).values;
    CHECK(contains(items, itemName));

    auto allReply = observer.call(lambda::dbus::MethodCall{
        .destination = lambda::system::StatusNotifierWatcherService::serviceName,
        .path = lambda::system::StatusNotifierWatcherService::objectPath,
        .interface = "org.freedesktop.DBus.Properties",
        .member = "GetAll",
        .arguments = {std::string(lambda::system::StatusNotifierWatcherService::interfaceName)},
    });
    auto all = allReply.readVariantDictionary();
    auto allItems = std::get<lambda::dbus::StringArray>(
        all.values.at("RegisteredStatusNotifierItems")).values;
    CHECK(contains(allItems, itemName));
    CHECK(std::get<bool>(all.values.at("IsStatusNotifierHostRegistered")) == true);
    CHECK(std::get<std::int32_t>(all.values.at("ProtocolVersion")) ==
          lambda::system::StatusNotifierWatcherService::protocolVersion);
  }

  CHECK(pumpUntil(observer,
                  [&] { return unregisteredItem == itemName; },
                  std::chrono::milliseconds(1000)));

  auto pathItemBus = lambda::dbus::Bus::openAddress(privateBus->address);
  std::string const pathItemName = pathItemBus.uniqueName();
  registeredItem.clear();
  auto pathReply = pathItemBus.call(lambda::dbus::MethodCall{
      .destination = lambda::system::StatusNotifierWatcherService::serviceName,
      .path = lambda::system::StatusNotifierWatcherService::objectPath,
      .interface = lambda::system::StatusNotifierWatcherService::interfaceName,
      .member = "RegisterStatusNotifierItem",
      .arguments = {std::string(lambda::system::StatusNotifierWatcherService::defaultItemObjectPath)},
  });
  CHECK(pathReply.valid());
  serviceBus.flush();
  CHECK(pumpUntil(observer,
                  [&] { return registeredItem == pathItemName; },
                  std::chrono::milliseconds(500)));
  REQUIRE(watcher.items().size() == 1);
  CHECK(watcher.items()[0].serviceName == pathItemName);
  CHECK(watcher.items()[0].objectPath ==
        lambda::system::StatusNotifierWatcherService::defaultItemObjectPath);
}

#endif
