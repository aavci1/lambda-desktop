#include <Lambda/System/PortalAccount.hpp>

#include "DBusTestHelpers.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <variant>

namespace {

using lambdaui::testing::dbus::pollBus;
using lambdaui::testing::dbus::startPrivateBus;

template <typename Callback>
class ScopeExit {
public:
  explicit ScopeExit(Callback callback) : callback_(std::move(callback)) {}
  ~ScopeExit() { callback_(); }

private:
  Callback callback_;
};

std::shared_ptr<lambdaui::dbus::VariantDictionary> options(std::string reason) {
  auto value = std::make_shared<lambdaui::dbus::VariantDictionary>();
  value->values["reason"] = std::move(reason);
  return value;
}

} // namespace

TEST_CASE("Portal Account support is compile-time declared") {
  CHECK((LAMBDA_HAS_DBUS == 0 || LAMBDA_HAS_DBUS == 1));
}

#if LAMBDA_HAS_DBUS

TEST_CASE("PortalAccountService returns account information on a private bus") {
  auto privateBus = startPrivateBus();
  if (!privateBus) {
    MESSAGE("Skipping portal account integration test because a private bus could not be started");
    return;
  }

  auto serviceBus = lambdaui::dbus::Bus::openAddress(privateBus->address);
  auto client = lambdaui::dbus::Bus::openAddress(privateBus->address);
  serviceBus.requestName(lambdaui::system::PortalAccountService::serviceName);

  lambdaui::system::PortalAccountService account(
      serviceBus,
      lambdaui::system::PortalAccountState{
          .id = "lambda-user",
          .name = "Lambda User",
          .imageUri = "file:///home/lambda/.face",
      });
  auto slot = account.exportObject();

  std::atomic<bool> serviceRunning = true;
  std::thread serviceThread([&] {
    while (serviceRunning.load()) {
      pollBus(serviceBus, 25);
    }
  });
  ScopeExit stopService([&] {
    serviceRunning = false;
    if (serviceThread.joinable()) {
      serviceThread.join();
    }
  });

  auto reply = client.call(lambdaui::dbus::MethodCall{
      .destination = lambdaui::system::PortalAccountService::serviceName,
      .path = lambdaui::system::PortalAccountService::objectPath,
      .interface = lambdaui::system::PortalAccountService::interfaceName,
      .member = "GetUserInformation",
      .arguments = {lambdaui::dbus::ObjectPath{"/org/freedesktop/portal/desktop/request/1_1/account"},
                    std::string("org.lambda.TestApp"),
                    std::string("wayland:window"),
                    options("Share your name with the test app.")},
  });

  CHECK(reply.readUint32() == 0);
  auto results = reply.readVariantDictionary();
  CHECK(std::get<std::string>(results.values["id"]) == "lambda-user");
  CHECK(std::get<std::string>(results.values["name"]) == "Lambda User");
  CHECK(std::get<std::string>(results.values["image"]) == "file:///home/lambda/.face");

  REQUIRE(account.lastRequest());
  CHECK(account.lastRequest()->handle.value == "/org/freedesktop/portal/desktop/request/1_1/account");
  CHECK(account.lastRequest()->appId == "org.lambda.TestApp");
  CHECK(account.lastRequest()->window == "wayland:window");
  CHECK(account.lastRequest()->reason == "Share your name with the test app.");
}

#endif
