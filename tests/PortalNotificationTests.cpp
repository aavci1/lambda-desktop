#include <Lambda/System/Notifications.hpp>
#include <Lambda/System/PortalNotification.hpp>

#include "DBusTestHelpers.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

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

std::shared_ptr<lambda::dbus::VariantDictionary> button(std::string label, std::string action) {
  auto value = std::make_shared<lambda::dbus::VariantDictionary>();
  value->values["label"] = std::move(label);
  value->values["action"] = std::move(action);
  return value;
}

std::shared_ptr<lambda::dbus::ArrayValue>
buttons(std::vector<lambda::dbus::BasicValue> values) {
  return std::make_shared<lambda::dbus::ArrayValue>(
      lambda::dbus::ArrayValue{.elementSignature = "a{sv}", .values = std::move(values)});
}

std::shared_ptr<lambda::dbus::VariantDictionary>
notification(std::string title, std::string body) {
  auto value = std::make_shared<lambda::dbus::VariantDictionary>();
  value->values["title"] = std::move(title);
  value->values["body"] = std::move(body);
  value->values["buttons"] = buttons({button("Open", "open")});
  return value;
}

} // namespace

TEST_CASE("Portal Notification support is compile-time declared") {
  CHECK((LAMBDA_HAS_DBUS == 0 || LAMBDA_HAS_DBUS == 1));
}

#if LAMBDA_HAS_DBUS

TEST_CASE("PortalNotificationService routes notifications through notification daemon") {
  auto privateBus = startPrivateBus();
  if (!privateBus) {
    MESSAGE("Skipping portal notification integration test because a private bus could not be started");
    return;
  }

  auto notificationBus = lambda::dbus::Bus::openAddress(privateBus->address);
  auto portalBus = lambda::dbus::Bus::openAddress(privateBus->address);
  auto client = lambda::dbus::Bus::openAddress(privateBus->address);
  notificationBus.requestName(lambda::system::NotificationsService::serviceName);
  portalBus.requestName(lambda::system::PortalNotificationService::serviceName);

  lambda::system::NotificationsService notifications(notificationBus, 4);
  auto notificationSlot = notifications.exportObject();
  lambda::system::PortalNotificationService portalNotifications(portalBus);
  auto portalSlot = portalNotifications.exportObject();
  auto actionWatchSlot = portalNotifications.watchNotificationActions();

  std::atomic<bool> notificationsRunning = true;
  std::thread notificationsThread([&] {
    while (notificationsRunning.load()) {
      pollBus(notificationBus, 25);
    }
  });
  ScopeExit stopNotifications([&] {
    notificationsRunning = false;
    if (notificationsThread.joinable()) {
      notificationsThread.join();
    }
  });

  std::atomic<bool> portalRunning = true;
  std::thread portalThread([&] {
    while (portalRunning.load()) {
      pollBus(portalBus, 25);
    }
  });
  ScopeExit stopPortal([&] {
    portalRunning = false;
    if (portalThread.joinable()) {
      portalThread.join();
    }
  });

  auto version = client.getProperty(lambda::dbus::PropertyAddress{
                                       .destination = lambda::system::PortalNotificationService::serviceName,
                                       .path = lambda::system::PortalNotificationService::objectPath,
                                       .interface = lambda::system::PortalNotificationService::interfaceName,
                                       .name = "version",
                                   },
                                   "u");
  CHECK(std::get<std::uint32_t>(version) == 2);
  auto supported = client.getProperty(lambda::dbus::PropertyAddress{
                                          .destination = lambda::system::PortalNotificationService::serviceName,
                                          .path = lambda::system::PortalNotificationService::objectPath,
                                          .interface = lambda::system::PortalNotificationService::interfaceName,
                                          .name = "SupportedOptions",
                                      },
                                      "a{sv}");
  bool const supportedOptionsShape =
      std::holds_alternative<std::shared_ptr<lambda::dbus::VariantDictionary>>(supported) ||
      std::holds_alternative<lambda::dbus::EmptyVariantDictionary>(supported);
  CHECK(supportedOptionsShape);

  auto addReply = client.call(lambda::dbus::MethodCall{
      .destination = lambda::system::PortalNotificationService::serviceName,
      .path = lambda::system::PortalNotificationService::objectPath,
      .interface = lambda::system::PortalNotificationService::interfaceName,
      .member = "AddNotification",
      .arguments = {std::string("org.lambda.TestApp"),
                    std::string("build"),
                    notification("Build complete", "Tests passed")},
  });
  CHECK(addReply.valid());
  REQUIRE(notifications.history().size() == 1);
  auto const daemonId = notifications.history().front().id;
  CHECK(notifications.history().front().appName == "org.lambda.TestApp");
  CHECK(notifications.history().front().summary == "Build complete");
  CHECK(notifications.history().front().body == "Tests passed");
  REQUIRE(notifications.history().front().actions.size() == 1);
  CHECK(notifications.history().front().actions[0].key == "open");
  CHECK(notifications.history().front().actions[0].label == "Open");

  auto replaceReply = client.call(lambda::dbus::MethodCall{
      .destination = lambda::system::PortalNotificationService::serviceName,
      .path = lambda::system::PortalNotificationService::objectPath,
      .interface = lambda::system::PortalNotificationService::interfaceName,
      .member = "AddNotification",
      .arguments = {std::string("org.lambda.TestApp"),
                    std::string("build"),
                    notification("Build failed", "One test failed")},
  });
  CHECK(replaceReply.valid());
  REQUIRE(notifications.history().size() == 1);
  CHECK(notifications.history().front().id == daemonId);
  CHECK(notifications.history().front().summary == "Build failed");

  std::string invokedAppId;
  std::string invokedPortalId;
  std::string invokedAction;
  auto actionSlot = client.matchSignal(
      lambda::dbus::SignalMatch{
          .sender = lambda::system::PortalNotificationService::serviceName,
          .path = lambda::system::PortalNotificationService::objectPath,
          .interface = lambda::system::PortalNotificationService::interfaceName,
          .member = "ActionInvoked",
      },
      [&](lambda::dbus::Message& message) {
        invokedAppId = message.readString();
        invokedPortalId = message.readString();
        invokedAction = message.readString();
        auto parameters = std::get<std::shared_ptr<lambda::dbus::ArrayValue>>(message.readBasic("av"));
        REQUIRE(parameters);
        CHECK(parameters->values.empty());
      });

  REQUIRE(notifications.invokeAction(daemonId, "open"));
  notificationBus.flush();
  CHECK(pumpUntil(client,
                  [&] {
                    return invokedAppId == "org.lambda.TestApp" &&
                           invokedPortalId == "build" &&
                           invokedAction == "open";
                  },
                  std::chrono::milliseconds(500)));

  auto removeReply = client.call(lambda::dbus::MethodCall{
      .destination = lambda::system::PortalNotificationService::serviceName,
      .path = lambda::system::PortalNotificationService::objectPath,
      .interface = lambda::system::PortalNotificationService::interfaceName,
      .member = "RemoveNotification",
      .arguments = {std::string("org.lambda.TestApp"), std::string("build")},
  });
  CHECK(removeReply.valid());
  auto closed = notifications.notification(daemonId);
  REQUIRE(closed);
  CHECK(closed->closed);
}

#endif
