#include <Lambda/System/PortalInhibit.hpp>

#include "DBusTestHelpers.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <utility>

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

TEST_CASE("Portal Inhibit support is compile-time declared") {
  CHECK((LAMBDA_HAS_DBUS == 0 || LAMBDA_HAS_DBUS == 1));
}

#if LAMBDA_HAS_DBUS

TEST_CASE("PortalInhibitService tracks inhibit requests and monitor acknowledgements") {
  auto privateBus = startPrivateBus();
  if (!privateBus) {
    MESSAGE("Skipping portal inhibit integration test because a private bus could not be started");
    return;
  }

  auto serviceBus = lambdaui::dbus::Bus::openAddress(privateBus->address);
  auto client = lambdaui::dbus::Bus::openAddress(privateBus->address);
  serviceBus.requestName(lambdaui::system::PortalInhibitService::serviceName);

  lambdaui::system::PortalInhibitService inhibit(serviceBus);
  auto slot = inhibit.exportObject();

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

  std::string const requestPath = "/org/freedesktop/portal/desktop/request/1_1/inhibit";
  auto inhibitReply = client.call(lambdaui::dbus::MethodCall{
      .destination = lambdaui::system::PortalInhibitService::serviceName,
      .path = lambdaui::system::PortalInhibitService::objectPath,
      .interface = lambdaui::system::PortalInhibitService::interfaceName,
      .member = "Inhibit",
      .arguments = {lambdaui::dbus::ObjectPath{requestPath},
                    std::string("org.lambda.TestApp"),
                    std::string("wayland:window"),
                    std::uint32_t(5),
                    options("Rendering a video")},
  });
  CHECK(inhibitReply.valid());
  auto request = inhibit.request(requestPath);
  REQUIRE(request);
  CHECK(request->appId == "org.lambda.TestApp");
  CHECK(request->window == "wayland:window");
  CHECK(request->flags == 5);
  CHECK(request->reason == "Rendering a video");
  CHECK(!request->closed);

  auto closeReply = client.call(lambdaui::dbus::MethodCall{
      .destination = lambdaui::system::PortalInhibitService::serviceName,
      .path = requestPath,
      .interface = lambdaui::system::PortalInhibitService::requestInterfaceName,
      .member = "Close",
  });
  CHECK(closeReply.valid());
  request = inhibit.request(requestPath);
  REQUIRE(request);
  CHECK(request->closed);

  std::string const monitorRequestPath = "/org/freedesktop/portal/desktop/request/1_1/monitor";
  std::string const sessionPath = "/org/freedesktop/portal/desktop/session/1_1/session";
  auto monitorReply = client.call(lambdaui::dbus::MethodCall{
      .destination = lambdaui::system::PortalInhibitService::serviceName,
      .path = lambdaui::system::PortalInhibitService::objectPath,
      .interface = lambdaui::system::PortalInhibitService::interfaceName,
      .member = "CreateMonitor",
      .arguments = {lambdaui::dbus::ObjectPath{monitorRequestPath},
                    lambdaui::dbus::ObjectPath{sessionPath},
                    std::string("org.lambda.TestApp"),
                    std::string("wayland:window")},
  });
  CHECK(monitorReply.readUint32() == 0);
  auto monitor = inhibit.monitor(sessionPath);
  REQUIRE(monitor);
  CHECK(monitor->handle.value == monitorRequestPath);
  CHECK(monitor->appId == "org.lambda.TestApp");
  CHECK(!monitor->queryEndAcknowledged);

  auto queryReply = client.call(lambdaui::dbus::MethodCall{
      .destination = lambdaui::system::PortalInhibitService::serviceName,
      .path = lambdaui::system::PortalInhibitService::objectPath,
      .interface = lambdaui::system::PortalInhibitService::interfaceName,
      .member = "QueryEndResponse",
      .arguments = {lambdaui::dbus::ObjectPath{sessionPath}},
  });
  CHECK(queryReply.valid());
  monitor = inhibit.monitor(sessionPath);
  REQUIRE(monitor);
  CHECK(monitor->queryEndAcknowledged);
}

#endif
