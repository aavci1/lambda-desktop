#include <Lambda/System/Notifications.hpp>

#include "DBusTestHelpers.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
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

std::shared_ptr<lambda::dbus::StructValue> notificationImageData() {
  return std::make_shared<lambda::dbus::StructValue>(
      lambda::dbus::StructValue{.signature = "iiibiiay",
                                .fields = {std::int32_t(1),
                                           std::int32_t(1),
                                           std::int32_t(4),
                                           true,
                                           std::int32_t(8),
                                           std::int32_t(4),
                                           lambda::dbus::ByteArray{{0x11, 0x22, 0x33, 0xff}}}});
}

std::shared_ptr<lambda::dbus::VariantDictionary> notificationHints() {
  auto hints = std::make_shared<lambda::dbus::VariantDictionary>();
  hints->values["urgency"] = std::uint8_t(2);
  hints->values["category"] = std::string("device.added");
  hints->values["desktop-entry"] = std::string("lambda-files");
  hints->values["image-path"] = std::string("file:///tmp/notification.png");
  hints->values["image-data"] = notificationImageData();
  hints->values["sound-name"] = std::string("message-new-instant");
  hints->values["sound-file"] = std::string("/tmp/sound.oga");
  hints->values["x"] = std::int32_t(24);
  hints->values["y"] = std::int32_t(48);
  hints->values["action-icons"] = true;
  hints->values["resident"] = true;
  hints->values["suppress-sound"] = true;
  hints->values["transient"] = true;
  return hints;
}

lambda::dbus::VariantDictionary policyHints(bool resident = false, bool transient = false) {
  lambda::dbus::VariantDictionary hints;
  if (resident) {
    hints.values["resident"] = true;
  }
  if (transient) {
    hints.values["transient"] = true;
  }
  return hints;
}

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
        lambda::system::NotificationHints hints;
        CHECK(message.signature(false).ends_with("a{sv}"));
        if (message.signature(false).ends_with("a{sv}")) {
          auto rawHints = message.readVariantDictionary();
          hints.urgency = static_cast<lambda::system::NotificationUrgency>(
              std::get<std::uint8_t>(rawHints.values.at("urgency")));
          if (auto it = rawHints.values.find("category"); it != rawHints.values.end()) {
            if (auto category = std::get_if<std::string>(&it->second)) {
              hints.category = *category;
            }
          }
          if (auto it = rawHints.values.find("desktop-entry"); it != rawHints.values.end()) {
            if (auto desktopEntry = std::get_if<std::string>(&it->second)) {
              hints.desktopEntry = *desktopEntry;
            }
          }
          if (auto it = rawHints.values.find("suppress-sound"); it != rawHints.values.end()) {
            if (auto suppressSound = std::get_if<bool>(&it->second)) {
              hints.suppressSound = *suppressSound;
            }
          }
          if (auto it = rawHints.values.find("transient"); it != rawHints.values.end()) {
            if (auto transient = std::get_if<bool>(&it->second)) {
              hints.transient = *transient;
            }
          }
        }
        postedSignals.push_back(lambda::system::NotificationPosted{
            .id = id,
            .appName = std::move(appName),
            .appIcon = std::move(appIcon),
            .summary = std::move(summary),
            .body = std::move(body),
            .actions = std::move(actions),
            .expireTimeoutMs = expireTimeoutMs,
            .hints = std::move(hints),
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
                    notificationHints(),
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
  CHECK(record->hints.urgency == lambda::system::NotificationUrgency::Critical);
  CHECK(record->hints.category == "device.added");
  CHECK(record->hints.desktopEntry == "lambda-files");
  CHECK(record->hints.imagePath == "file:///tmp/notification.png");
  REQUIRE(record->hints.imageData);
  CHECK(record->hints.imageData->width == 1);
  CHECK(record->hints.imageData->height == 1);
  CHECK(record->hints.imageData->rowStride == 4);
  CHECK(record->hints.imageData->hasAlpha);
  CHECK(record->hints.imageData->bitsPerSample == 8);
  CHECK(record->hints.imageData->channels == 4);
  CHECK(record->hints.imageData->pixels == std::vector<std::uint8_t>{0x11, 0x22, 0x33, 0xff});
  CHECK(record->hints.soundName == "message-new-instant");
  CHECK(record->hints.soundFile == "/tmp/sound.oga");
  REQUIRE(record->hints.x);
  CHECK(*record->hints.x == 24);
  REQUIRE(record->hints.y);
  CHECK(*record->hints.y == 48);
  CHECK(record->hints.actionIcons);
  CHECK(record->hints.resident);
  CHECK(record->hints.suppressSound);
  CHECK(record->hints.transient);
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
  CHECK(postedSignals.back().hints.urgency == lambda::system::NotificationUrgency::Critical);
  CHECK(postedSignals.back().hints.category == "device.added");
  CHECK(postedSignals.back().hints.desktopEntry == "lambda-files");
  CHECK(postedSignals.back().hints.suppressSound);
  CHECK(postedSignals.back().hints.transient);

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
  CHECK(record->hints.urgency == lambda::system::NotificationUrgency::Normal);
  CHECK(record->hints.category.empty());
  CHECK_FALSE(record->hints.imageData);
  CHECK_FALSE(record->hints.transient);
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

TEST_CASE("NotificationsService expires and clears daemon history by policy") {
  auto privateBus = startPrivateBus();
  if (!privateBus) {
    MESSAGE("Skipping notifications policy test because a private bus could not be started in this environment");
    return;
  }

  auto serviceBus = lambda::dbus::Bus::openAddress(privateBus->address);
  auto client = lambda::dbus::Bus::openAddress(privateBus->address);
  auto controlClient = lambda::dbus::Bus::openAddress(privateBus->address);
  serviceBus.requestName(lambda::system::NotificationsService::serviceName);

  lambda::system::NotificationsService service(serviceBus, 8);
  auto objectSlot = service.exportObject();

  std::vector<std::pair<std::uint32_t, lambda::system::NotificationCloseReason>> closedSignals;
  auto closedSlot = client.matchSignal(
      lambda::dbus::SignalMatch{
          .sender = lambda::system::NotificationsService::serviceName,
          .path = lambda::system::NotificationsService::objectPath,
          .interface = lambda::system::NotificationsService::interfaceName,
          .member = "NotificationClosed",
      },
      [&](lambda::dbus::Message& message) {
        closedSignals.push_back({message.readUint32(),
                                 static_cast<lambda::system::NotificationCloseReason>(message.readUint32())});
      });

  auto expiring = service.notify("app",
                                 0,
                                 "",
                                 "Expiring",
                                 "Body",
                                 lambda::dbus::StringArray{},
                                 policyHints(),
                                 10);
  auto resident = service.notify("app",
                                 0,
                                 "",
                                 "Resident",
                                 "Body",
                                 lambda::dbus::StringArray{},
                                 policyHints(true, false),
                                 10);
  auto sticky = service.notify("app",
                               0,
                               "",
                               "Sticky",
                               "Body",
                               lambda::dbus::StringArray{},
                               policyHints(),
                               0);
  auto transient = service.notify("app",
                                  0,
                                  "",
                                  "Transient",
                                  "Body",
                                  lambda::dbus::StringArray{},
                                  policyHints(false, true),
                                  10);

  auto const deadline = service.nextExpirationDeadline();
  REQUIRE(deadline);
  CHECK(service.expireDueNotifications(*deadline - std::chrono::milliseconds(1)) == 0);
  CHECK(service.expireDueNotifications(*deadline + std::chrono::milliseconds(1)) == 2);
  serviceBus.flush();
  CHECK(pumpUntil(client,
                  [&] { return closedSignals.size() == 2; },
                  std::chrono::milliseconds(500)));
  auto expiringRecord = service.notification(expiring);
  REQUIRE(expiringRecord);
  CHECK(expiringRecord->closed);
  CHECK(expiringRecord->closeReason == lambda::system::NotificationCloseReason::Expired);
  auto residentRecord = service.notification(resident);
  REQUIRE(residentRecord);
  CHECK_FALSE(residentRecord->closed);
  auto stickyRecord = service.notification(sticky);
  REQUIRE(stickyRecord);
  CHECK_FALSE(stickyRecord->closed);
  CHECK_FALSE(service.notification(transient));

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

  std::uint32_t const cleared = lambda::system::NotificationsClient(std::move(controlClient)).clearHistory();
  running = false;
  if (serviceThread.joinable()) {
    serviceThread.join();
  }
  CHECK(cleared == 2);
  CHECK(pumpUntil(client,
                  [&] { return closedSignals.size() == 4; },
                  std::chrono::milliseconds(500)));
  CHECK(service.history().empty());
}

#endif
