#include <Lambda/System/PortalScreenCast.hpp>

#include "DBusTestHelpers.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

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

class EnvironmentVariableScope {
public:
  EnvironmentVariableScope(char const* name, char const* value) : name_(name) {
    if (char const* existing = std::getenv(name)) {
      previous_ = existing;
    }
    if (value) {
      setenv(name, value, 1);
    } else {
      unsetenv(name);
    }
  }

  ~EnvironmentVariableScope() {
    if (previous_) {
      setenv(name_.c_str(), previous_->c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

private:
  std::string name_;
  std::optional<std::string> previous_;
};

std::shared_ptr<lambdaui::dbus::VariantDictionary> options() {
  return std::make_shared<lambdaui::dbus::VariantDictionary>();
}

std::shared_ptr<lambdaui::dbus::VariantDictionary> selectOptions() {
  auto value = std::make_shared<lambdaui::dbus::VariantDictionary>();
  value->values["types"] = std::uint32_t(1);
  value->values["multiple"] = false;
  value->values["cursor_mode"] = std::uint32_t(1);
  value->values["persist_mode"] = std::uint32_t(2);
  return value;
}

lambdaui::dbus::MethodCall screenCastCall(std::string member,
                                        std::vector<lambdaui::dbus::BasicValue> arguments) {
  return lambdaui::dbus::MethodCall{
      .destination = lambdaui::system::PortalScreenCastService::serviceName,
      .path = lambdaui::system::PortalScreenCastService::objectPath,
      .interface = lambdaui::system::PortalScreenCastService::interfaceName,
      .member = std::move(member),
      .arguments = std::move(arguments),
  };
}

} // namespace

TEST_CASE("Portal ScreenCast support is compile-time declared") {
  CHECK((LAMBDA_HAS_DBUS == 0 || LAMBDA_HAS_DBUS == 1));
}

#if LAMBDA_HAS_DBUS

TEST_CASE("PortalScreenCastService records sessions and returns configured streams") {
  EnvironmentVariableScope nodeId("LAMBDA_PORTAL_SCREENCAST_NODE_ID", "42");
  EnvironmentVariableScope serial("LAMBDA_PORTAL_SCREENCAST_PIPEWIRE_SERIAL", "987654321");
  EnvironmentVariableScope width("LAMBDA_PORTAL_SCREENCAST_WIDTH", "1920");
  EnvironmentVariableScope height("LAMBDA_PORTAL_SCREENCAST_HEIGHT", "1080");
  EnvironmentVariableScope x("LAMBDA_PORTAL_SCREENCAST_X", "10");
  EnvironmentVariableScope y("LAMBDA_PORTAL_SCREENCAST_Y", "20");
  EnvironmentVariableScope mapping("LAMBDA_PORTAL_SCREENCAST_MAPPING_ID", "monitor-1");

  auto privateBus = startPrivateBus();
  if (!privateBus) {
    MESSAGE("Skipping portal screencast integration test because a private bus could not be started");
    return;
  }

  auto serviceBus = lambdaui::dbus::Bus::openAddress(privateBus->address);
  auto client = lambdaui::dbus::Bus::openAddress(privateBus->address);
  serviceBus.requestName(lambdaui::system::PortalScreenCastService::serviceName);

  lambdaui::system::PortalScreenCastService screenCast(serviceBus);
  auto slot = screenCast.exportObject();

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

  auto availableSources = client.getProperty(
      lambdaui::dbus::PropertyAddress{
          .destination = lambdaui::system::PortalScreenCastService::serviceName,
          .path = lambdaui::system::PortalScreenCastService::objectPath,
          .interface = lambdaui::system::PortalScreenCastService::interfaceName,
          .name = "AvailableSourceTypes",
      },
      "u");
  CHECK(std::get<std::uint32_t>(availableSources) == 1);

  std::string const createHandle = "/org/freedesktop/portal/desktop/request/1_1/create";
  std::string const sessionHandle = "/org/freedesktop/portal/desktop/session/1_1/cast";
  auto createReply = client.call(screenCastCall(
      "CreateSession",
      {lambdaui::dbus::ObjectPath{createHandle},
       lambdaui::dbus::ObjectPath{sessionHandle},
       std::string("org.lambda.TestApp"),
       options()}));
  CHECK(createReply.readUint32() == 0);
  auto createResults = createReply.readVariantDictionary();
  CHECK(std::get<std::string>(createResults.values["session_id"]) == "cast");
  auto session = screenCast.session(sessionHandle);
  REQUIRE(session);
  CHECK(session->appId == "org.lambda.TestApp");
  CHECK(!session->closed);

  std::string const selectHandle = "/org/freedesktop/portal/desktop/request/1_1/select";
  auto selectReply = client.call(screenCastCall(
      "SelectSources",
      {lambdaui::dbus::ObjectPath{selectHandle},
       lambdaui::dbus::ObjectPath{sessionHandle},
       std::string("org.lambda.TestApp"),
       selectOptions()}));
  CHECK(selectReply.readUint32() == 0);
  CHECK(selectReply.readVariantDictionary().values.empty());
  session = screenCast.session(sessionHandle);
  REQUIRE(session);
  CHECK(session->sourcesSelected);

  std::string const startHandle = "/org/freedesktop/portal/desktop/request/1_1/start";
  auto startReply = client.call(screenCastCall(
      "Start",
      {lambdaui::dbus::ObjectPath{startHandle},
       lambdaui::dbus::ObjectPath{sessionHandle},
       std::string("org.lambda.TestApp"),
       std::string("wayland:window"),
       options()}));
  CHECK(startReply.readUint32() == 0);
  auto startResults = startReply.readVariantDictionary();
  CHECK(std::get<std::uint32_t>(startResults.values["persist_mode"]) == 2);
  auto streams =
      std::get<std::shared_ptr<lambdaui::dbus::ArrayValue>>(startResults.values["streams"]);
  REQUIRE(streams);
  CHECK(streams->elementSignature == "(ua{sv})");
  REQUIRE(streams->values.size() == 1);
  auto stream = std::get<std::shared_ptr<lambdaui::dbus::StructValue>>(streams->values.front());
  REQUIRE(stream);
  REQUIRE(stream->fields.size() == 2);
  CHECK(std::get<std::uint32_t>(stream->fields[0]) == 42);
  auto streamProperties =
      std::get<std::shared_ptr<lambdaui::dbus::VariantDictionary>>(stream->fields[1]);
  REQUIRE(streamProperties);
  CHECK(std::get<std::uint32_t>(streamProperties->values["source_type"]) == 1);
  CHECK(std::get<std::uint64_t>(streamProperties->values["pipewire-serial"]) == 987654321);
  CHECK(std::get<std::string>(streamProperties->values["mapping_id"]) == "monitor-1");
  auto size =
      std::get<std::shared_ptr<lambdaui::dbus::StructValue>>(streamProperties->values["size"]);
  REQUIRE(size);
  CHECK(std::get<std::int32_t>(size->fields[0]) == 1920);
  CHECK(std::get<std::int32_t>(size->fields[1]) == 1080);
  auto position =
      std::get<std::shared_ptr<lambdaui::dbus::StructValue>>(streamProperties->values["position"]);
  REQUIRE(position);
  CHECK(std::get<std::int32_t>(position->fields[0]) == 10);
  CHECK(std::get<std::int32_t>(position->fields[1]) == 20);
  session = screenCast.session(sessionHandle);
  REQUIRE(session);
  CHECK(session->started);
  REQUIRE(session->streams.size() == 1);
  CHECK(session->streams.front().nodeId == 42);

  auto closeRequestReply = client.call(lambdaui::dbus::MethodCall{
      .destination = lambdaui::system::PortalScreenCastService::serviceName,
      .path = startHandle,
      .interface = lambdaui::system::PortalScreenCastService::requestInterfaceName,
      .member = "Close",
  });
  CHECK(closeRequestReply.valid());
  auto request = screenCast.request(startHandle);
  REQUIRE(request);
  CHECK(request->closed);

  auto closeSessionReply = client.call(lambdaui::dbus::MethodCall{
      .destination = lambdaui::system::PortalScreenCastService::serviceName,
      .path = sessionHandle,
      .interface = lambdaui::system::PortalScreenCastService::sessionInterfaceName,
      .member = "Close",
  });
  CHECK(closeSessionReply.valid());
  session = screenCast.session(sessionHandle);
  REQUIRE(session);
  CHECK(session->closed);
}

TEST_CASE("PortalScreenCastService reports no selection without a configured stream") {
  EnvironmentVariableScope nodeId("LAMBDA_PORTAL_SCREENCAST_NODE_ID", nullptr);

  auto privateBus = startPrivateBus();
  if (!privateBus) {
    MESSAGE("Skipping portal screencast no-stream test because a private bus could not be started");
    return;
  }

  auto serviceBus = lambdaui::dbus::Bus::openAddress(privateBus->address);
  auto client = lambdaui::dbus::Bus::openAddress(privateBus->address);
  serviceBus.requestName(lambdaui::system::PortalScreenCastService::serviceName);

  lambdaui::system::PortalScreenCastService screenCast(serviceBus);
  auto slot = screenCast.exportObject();

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

  std::string const createHandle = "/org/freedesktop/portal/desktop/request/1_1/create_empty";
  std::string const sessionHandle = "/org/freedesktop/portal/desktop/session/1_1/empty";
  auto createReply = client.call(screenCastCall(
      "CreateSession",
      {lambdaui::dbus::ObjectPath{createHandle},
       lambdaui::dbus::ObjectPath{sessionHandle},
       std::string("org.lambda.TestApp"),
       options()}));
  CHECK(createReply.readUint32() == 0);
  (void)createReply.readVariantDictionary();

  auto startReply = client.call(screenCastCall(
      "Start",
      {lambdaui::dbus::ObjectPath{"/org/freedesktop/portal/desktop/request/1_1/start_empty"},
       lambdaui::dbus::ObjectPath{sessionHandle},
       std::string("org.lambda.TestApp"),
       std::string(),
       options()}));
  CHECK(startReply.readUint32() == 2);
  CHECK(startReply.readVariantDictionary().values.empty());
}

#endif
