#include <Lambda/System/PortalAppChooser.hpp>

#include "DBusTestHelpers.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <variant>

namespace {

using lambda::testing::dbus::pollBus;
using lambda::testing::dbus::startPrivateBus;

template <typename Callback>
class ScopeExit {
public:
  explicit ScopeExit(Callback callback) : callback_(std::move(callback)) {}
  ~ScopeExit() { callback_(); }

private:
  Callback callback_;
};

std::shared_ptr<lambda::dbus::VariantDictionary> options(std::string lastChoice = {},
                                                         std::string activationToken = {}) {
  auto value = std::make_shared<lambda::dbus::VariantDictionary>();
  if (!lastChoice.empty()) {
    value->values["last_choice"] = std::move(lastChoice);
  }
  if (!activationToken.empty()) {
    value->values["activation_token"] = std::move(activationToken);
  }
  value->values["uri"] = std::string("https://lambda.invalid/");
  value->values["content_type"] = std::string("x-scheme-handler/https");
  return value;
}

lambda::dbus::MethodCall chooseCall(lambda::dbus::StringArray choices,
                                    std::shared_ptr<lambda::dbus::VariantDictionary> requestOptions) {
  return lambda::dbus::MethodCall{
      .destination = lambda::system::PortalAppChooserService::serviceName,
      .path = lambda::system::PortalAppChooserService::objectPath,
      .interface = lambda::system::PortalAppChooserService::interfaceName,
      .member = "ChooseApplication",
      .arguments = {lambda::dbus::ObjectPath{"/org/freedesktop/portal/desktop/request/1_1/lambda"},
                    std::string("org.lambda.TestApp"),
                    std::string("wayland:window"),
                    std::move(choices),
                    std::move(requestOptions)},
  };
}

} // namespace

TEST_CASE("Portal AppChooser support is compile-time declared") {
  CHECK((LAMBDA_HAS_DBUS == 0 || LAMBDA_HAS_DBUS == 1));
}

#if LAMBDA_HAS_DBUS

TEST_CASE("PortalAppChooserService chooses applications on a private bus") {
  auto privateBus = startPrivateBus();
  if (!privateBus) {
    MESSAGE("Skipping portal app chooser integration test because a private bus could not be started");
    return;
  }

  auto serviceBus = lambda::dbus::Bus::openAddress(privateBus->address);
  auto client = lambda::dbus::Bus::openAddress(privateBus->address);
  serviceBus.requestName(lambda::system::PortalAppChooserService::serviceName);

  lambda::system::PortalAppChooserService appChooser(serviceBus);
  auto slot = appChooser.exportObject();

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

  auto reply = client.call(chooseCall(
      lambda::dbus::StringArray{{"org.lambda.BrowserDev", "org.lambda.Browser"}},
      options("org.lambda.Browser", "activation-token")));
  CHECK(reply.readUint32() == 0);
  auto results = reply.readVariantDictionary();
  CHECK(std::get<std::string>(results.values["choice"]) == "org.lambda.Browser");
  CHECK(std::get<std::string>(results.values["activation_token"]) == "activation-token");
  REQUIRE(appChooser.lastRequest());
  CHECK(appChooser.lastRequest()->appId == "org.lambda.TestApp");
  CHECK(appChooser.lastRequest()->parentWindow == "wayland:window");
  CHECK(appChooser.lastRequest()->choices.values.size() == 2);

  auto fallbackReply = client.call(chooseCall(
      lambda::dbus::StringArray{{"org.lambda.First", "org.lambda.Second"}},
      options("org.lambda.Missing")));
  CHECK(fallbackReply.readUint32() == 0);
  auto fallbackResults = fallbackReply.readVariantDictionary();
  CHECK(std::get<std::string>(fallbackResults.values["choice"]) == "org.lambda.First");

  auto emptyReply = client.call(chooseCall(lambda::dbus::StringArray{}, options()));
  CHECK(emptyReply.readUint32() == 2);
  CHECK(emptyReply.readVariantDictionary().values.empty());

  auto updateReply = client.call(lambda::dbus::MethodCall{
      .destination = lambda::system::PortalAppChooserService::serviceName,
      .path = lambda::system::PortalAppChooserService::objectPath,
      .interface = lambda::system::PortalAppChooserService::interfaceName,
      .member = "UpdateChoices",
      .arguments = {lambda::dbus::ObjectPath{"/org/freedesktop/portal/desktop/request/1_1/lambda"},
                    lambda::dbus::StringArray{{"org.lambda.Updated"}}},
  });
  CHECK(updateReply.valid());
  auto updated =
      appChooser.updatedChoices("/org/freedesktop/portal/desktop/request/1_1/lambda");
  REQUIRE(updated);
  REQUIRE(updated->values.size() == 1);
  CHECK(updated->values.front() == "org.lambda.Updated");
}

#endif
