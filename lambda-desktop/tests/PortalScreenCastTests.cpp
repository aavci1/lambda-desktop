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

std::shared_ptr<lambda::dbus::VariantDictionary> options() {
  return std::make_shared<lambda::dbus::VariantDictionary>();
}

std::shared_ptr<lambda::dbus::VariantDictionary> selectOptions() {
  auto value = std::make_shared<lambda::dbus::VariantDictionary>();
  value->values["types"] = std::uint32_t(1);
  value->values["multiple"] = false;
  value->values["cursor_mode"] = std::uint32_t(1);
  value->values["persist_mode"] = std::uint32_t(2);
  return value;
}

lambda::dbus::MethodCall screenCastCall(std::string member,
                                        std::vector<lambda::dbus::BasicValue> arguments) {
  return lambda::dbus::MethodCall{
      .destination = lambda::system::PortalScreenCastService::serviceName,
      .path = lambda::system::PortalScreenCastService::objectPath,
      .interface = lambda::system::PortalScreenCastService::interfaceName,
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

  auto serviceBus = lambda::dbus::Bus::openAddress(privateBus->address);
  auto client = lambda::dbus::Bus::openAddress(privateBus->address);
  serviceBus.requestName(lambda::system::PortalScreenCastService::serviceName);

  lambda::system::PortalScreenCastService screenCast(serviceBus);
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
      lambda::dbus::PropertyAddress{
          .destination = lambda::system::PortalScreenCastService::serviceName,
          .path = lambda::system::PortalScreenCastService::objectPath,
          .interface = lambda::system::PortalScreenCastService::interfaceName,
          .name = "AvailableSourceTypes",
      },
      "u");
  CHECK(std::get<std::uint32_t>(availableSources) == 1);

  std::string const createHandle = "/org/freedesktop/portal/desktop/request/1_1/create";
  std::string const sessionHandle = "/org/freedesktop/portal/desktop/session/1_1/cast";
  auto createReply = client.call(screenCastCall(
      "CreateSession",
      {lambda::dbus::ObjectPath{createHandle},
       lambda::dbus::ObjectPath{sessionHandle},
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
      {lambda::dbus::ObjectPath{selectHandle},
       lambda::dbus::ObjectPath{sessionHandle},
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
      {lambda::dbus::ObjectPath{startHandle},
       lambda::dbus::ObjectPath{sessionHandle},
       std::string("org.lambda.TestApp"),
       std::string("wayland:window"),
       options()}));
  CHECK(startReply.readUint32() == 0);
  auto startResults = startReply.readVariantDictionary();
  CHECK(std::get<std::uint32_t>(startResults.values["persist_mode"]) == 2);
  auto streams =
      std::get<std::shared_ptr<lambda::dbus::ArrayValue>>(startResults.values["streams"]);
  REQUIRE(streams);
  CHECK(streams->elementSignature == "(ua{sv})");
  REQUIRE(streams->values.size() == 1);
  auto stream = std::get<std::shared_ptr<lambda::dbus::StructValue>>(streams->values.front());
  REQUIRE(stream);
  REQUIRE(stream->fields.size() == 2);
  CHECK(std::get<std::uint32_t>(stream->fields[0]) == 42);
  auto streamProperties =
      std::get<std::shared_ptr<lambda::dbus::VariantDictionary>>(stream->fields[1]);
  REQUIRE(streamProperties);
  CHECK(std::get<std::uint32_t>(streamProperties->values["source_type"]) == 1);
  CHECK(std::get<std::uint64_t>(streamProperties->values["pipewire-serial"]) == 987654321);
  CHECK(std::get<std::string>(streamProperties->values["mapping_id"]) == "monitor-1");
  auto size =
      std::get<std::shared_ptr<lambda::dbus::StructValue>>(streamProperties->values["size"]);
  REQUIRE(size);
  CHECK(std::get<std::int32_t>(size->fields[0]) == 1920);
  CHECK(std::get<std::int32_t>(size->fields[1]) == 1080);
  auto position =
      std::get<std::shared_ptr<lambda::dbus::StructValue>>(streamProperties->values["position"]);
  REQUIRE(position);
  CHECK(std::get<std::int32_t>(position->fields[0]) == 10);
  CHECK(std::get<std::int32_t>(position->fields[1]) == 20);
  session = screenCast.session(sessionHandle);
  REQUIRE(session);
  CHECK(session->started);
  REQUIRE(session->streams.size() == 1);
  CHECK(session->streams.front().nodeId == 42);

  auto closeRequestReply = client.call(lambda::dbus::MethodCall{
      .destination = lambda::system::PortalScreenCastService::serviceName,
      .path = startHandle,
      .interface = lambda::system::PortalScreenCastService::requestInterfaceName,
      .member = "Close",
  });
  CHECK(closeRequestReply.valid());
  auto request = screenCast.request(startHandle);
  REQUIRE(request);
  CHECK(request->closed);

  auto closeSessionReply = client.call(lambda::dbus::MethodCall{
      .destination = lambda::system::PortalScreenCastService::serviceName,
      .path = sessionHandle,
      .interface = lambda::system::PortalScreenCastService::sessionInterfaceName,
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

  auto serviceBus = lambda::dbus::Bus::openAddress(privateBus->address);
  auto client = lambda::dbus::Bus::openAddress(privateBus->address);
  serviceBus.requestName(lambda::system::PortalScreenCastService::serviceName);

  lambda::system::PortalScreenCastService screenCast(serviceBus);
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
      {lambda::dbus::ObjectPath{createHandle},
       lambda::dbus::ObjectPath{sessionHandle},
       std::string("org.lambda.TestApp"),
       options()}));
  CHECK(createReply.readUint32() == 0);
  (void)createReply.readVariantDictionary();

  auto startReply = client.call(screenCastCall(
      "Start",
      {lambda::dbus::ObjectPath{"/org/freedesktop/portal/desktop/request/1_1/start_empty"},
       lambda::dbus::ObjectPath{sessionHandle},
       std::string("org.lambda.TestApp"),
       std::string(),
       options()}));
  CHECK(startReply.readUint32() == 2);
  CHECK(startReply.readVariantDictionary().values.empty());
}

#endif
