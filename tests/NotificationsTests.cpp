#include <Lambda/System/Notifications.hpp>

#include "DBusTestHelpers.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <utility>
#include <unistd.h>

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

} // namespace

TEST_CASE("Notifications support is compile-time declared") {
  CHECK((LAMBDA_HAS_DBUS == 0 || LAMBDA_HAS_DBUS == 1));
}

#if LAMBDA_HAS_DBUS

TEST_CASE("NotificationsService implements Freedesktop notification methods and signals") {
  auto privateBus = startPrivateBus();
  if (!privateBus) {
    MESSAGE("Skipping notifications integration test because a private bus could not be started in this environment");
    return;
  }

  auto serviceBus = lambda::dbus::Bus::openAddress(privateBus->address);
  auto client = lambda::dbus::Bus::openAddress(privateBus->address);
  serviceBus.requestName(lambda::system::NotificationsService::serviceName);

  lambda::system::NotificationsService service(serviceBus, 4);
  auto objectSlot = service.exportObject();

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

  auto capabilitiesReply = client.call(lambda::dbus::MethodCall{
      .destination = lambda::system::NotificationsService::serviceName,
      .path = lambda::system::NotificationsService::objectPath,
      .interface = lambda::system::NotificationsService::interfaceName,
      .member = "GetCapabilities",
  });
  auto capabilities = capabilitiesReply.readStringArray().values;
  CHECK(std::find(capabilities.begin(), capabilities.end(), "body") != capabilities.end());
  CHECK(std::find(capabilities.begin(), capabilities.end(), "actions") != capabilities.end());

  auto serverReply = client.call(lambda::dbus::MethodCall{
      .destination = lambda::system::NotificationsService::serviceName,
      .path = lambda::system::NotificationsService::objectPath,
      .interface = lambda::system::NotificationsService::interfaceName,
      .member = "GetServerInformation",
  });
  CHECK(serverReply.readString() == "Lambda Notifications");
  CHECK(serverReply.readString() == "Lambda");
  CHECK(serverReply.readString() == "1.0");
  CHECK(serverReply.readString() == "1.2");

  auto notifyReply = client.call(lambda::dbus::MethodCall{
      .destination = lambda::system::NotificationsService::serviceName,
      .path = lambda::system::NotificationsService::objectPath,
      .interface = lambda::system::NotificationsService::interfaceName,
      .member = "Notify",
      .arguments = {std::string("notify-send"),
                    std::uint32_t(0),
                    std::string("dialog-information"),
                    std::string("Build complete"),
                    std::string("Tests passed"),
                    lambda::dbus::StringArray{{"default", "Open"}},
                    lambda::dbus::EmptyVariantDictionary{},
                    std::int32_t(5000)},
  });
  std::uint32_t const id = notifyReply.readUint32();
  CHECK(id == 1);
  auto record = service.notification(id);
  REQUIRE(record);
  CHECK(record->summary == "Build complete");
  CHECK(record->body == "Tests passed");
  REQUIRE(record->actions.size() == 1);
  CHECK(record->actions[0].key == "default");
  CHECK(record->actions[0].label == "Open");

  auto replaceReply = client.call(lambda::dbus::MethodCall{
      .destination = lambda::system::NotificationsService::serviceName,
      .path = lambda::system::NotificationsService::objectPath,
      .interface = lambda::system::NotificationsService::interfaceName,
      .member = "Notify",
      .arguments = {std::string("notify-send"),
                    id,
                    std::string("dialog-information"),
                    std::string("Build failed"),
                    std::string("One test failed"),
                    lambda::dbus::StringArray{},
                    lambda::dbus::EmptyVariantDictionary{},
                    std::int32_t(-1)},
  });
  CHECK(replaceReply.readUint32() == id);
  record = service.notification(id);
  REQUIRE(record);
  CHECK(record->summary == "Build failed");
  CHECK(record->body == "One test failed");

  std::uint32_t closedId = 0;
  std::uint32_t closeReason = 0;
  auto closedSlot = client.matchSignal(
      lambda::dbus::SignalMatch{
          .sender = lambda::system::NotificationsService::serviceName,
          .path = lambda::system::NotificationsService::objectPath,
          .interface = lambda::system::NotificationsService::interfaceName,
          .member = "NotificationClosed",
      },
      [&](lambda::dbus::Message& message) {
        closedId = message.readUint32();
        closeReason = message.readUint32();
      });

  auto closeReply = client.call(lambda::dbus::MethodCall{
      .destination = lambda::system::NotificationsService::serviceName,
      .path = lambda::system::NotificationsService::objectPath,
      .interface = lambda::system::NotificationsService::interfaceName,
      .member = "CloseNotification",
      .arguments = {id},
  });
  CHECK(closeReply.valid());
  serviceBus.flush();
  CHECK(pumpUntil(client,
                  [&] { return closedId == id && closeReason == 3; },
                  std::chrono::milliseconds(500)));

  auto actionId = service.notify("app",
                                 0,
                                 "",
                                 "Action",
                                 "Body",
                                 lambda::dbus::StringArray{{"default", "Open"}},
                                 -1);
  std::uint32_t invokedId = 0;
  std::string invokedAction;
  auto actionSlot = client.matchSignal(
      lambda::dbus::SignalMatch{
          .sender = lambda::system::NotificationsService::serviceName,
          .path = lambda::system::NotificationsService::objectPath,
          .interface = lambda::system::NotificationsService::interfaceName,
          .member = "ActionInvoked",
      },
      [&](lambda::dbus::Message& message) {
        invokedId = message.readUint32();
        invokedAction = message.readString();
      });

  CHECK(service.invokeAction(actionId, "default"));
  serviceBus.flush();
  CHECK(pumpUntil(client,
                  [&] { return invokedId == actionId && invokedAction == "default"; },
                  std::chrono::milliseconds(500)));

}

#endif
