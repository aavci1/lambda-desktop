#include <Lambda/System/Notifications.hpp>

#include "DBusTestHelpers.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <utility>
#include <vector>
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

  std::vector<lambda::system::NotificationPosted> postedSignals;
  auto postedSlot = client.matchSignal(
      lambda::dbus::SignalMatch{
          .sender = lambda::system::NotificationsService::serviceName,
          .path = lambda::system::NotificationsService::objectPath,
          .interface = lambda::system::NotificationsService::monitorInterfaceName,
          .member = lambda::system::NotificationsService::postedSignalName,
      },
      [&](lambda::dbus::Message& message) {
        std::uint32_t const id = message.readUint32();
        std::string appName = message.readString();
        std::string appIcon = message.readString();
        std::string summary = message.readString();
        std::string body = message.readString();
        std::int32_t const expireTimeoutMs = message.readInt32();
        std::vector<lambda::system::NotificationAction> actions;
        auto rawActions = message.readStringArray();
        for (std::size_t i = 0; i + 1u < rawActions.values.size(); i += 2u) {
          actions.push_back(lambda::system::NotificationAction{
              .key = rawActions.values[i],
              .label = rawActions.values[i + 1u],
          });
        }
        postedSignals.push_back(lambda::system::NotificationPosted{
            .id = id,
            .appName = std::move(appName),
            .appIcon = std::move(appIcon),
            .summary = std::move(summary),
            .body = std::move(body),
            .actions = std::move(actions),
            .expireTimeoutMs = expireTimeoutMs,
        });
      });

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
  serviceBus.flush();
  CHECK(pumpUntil(client,
                  [&] { return postedSignals.size() == 1 && postedSignals.back().id == id; },
                  std::chrono::milliseconds(500)));
  REQUIRE(postedSignals.size() == 1);
  CHECK(postedSignals.back().appName == "notify-send");
  CHECK(postedSignals.back().appIcon == "dialog-information");
  CHECK(postedSignals.back().summary == "Build complete");
  CHECK(postedSignals.back().body == "Tests passed");
  CHECK(postedSignals.back().expireTimeoutMs == 5000);
  CHECK(postedSignals.back().actions == std::vector<lambda::system::NotificationAction>{{.key = "default",
                                                                                         .label = "Open"}});

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
  serviceBus.flush();
  CHECK(pumpUntil(client,
                  [&] {
                    return postedSignals.size() == 2 &&
                           postedSignals.back().id == id &&
                           postedSignals.back().summary == "Build failed";
                  },
                  std::chrono::milliseconds(500)));

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

  invokedId = 0;
  invokedAction.clear();
  auto privateActionReply = client.call(lambda::dbus::MethodCall{
      .destination = lambda::system::NotificationsService::serviceName,
      .path = lambda::system::NotificationsService::objectPath,
      .interface = lambda::system::NotificationsService::monitorInterfaceName,
      .member = lambda::system::NotificationsService::invokeActionMethodName,
      .arguments = {actionId, std::string("default")},
  });
  CHECK(privateActionReply.valid());
  serviceBus.flush();
  CHECK(pumpUntil(client,
                  [&] { return invokedId == actionId && invokedAction == "default"; },
                  std::chrono::milliseconds(500)));

}

#endif
