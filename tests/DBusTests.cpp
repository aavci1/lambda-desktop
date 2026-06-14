#include <Lambda/System/DBus.hpp>

#include "DBusTestHelpers.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

using lambda::testing::dbus::pollBus;
using lambda::testing::dbus::pumpUntil;
using lambda::testing::dbus::startPrivateBus;

struct TestPipe {
  int readFd = -1;
  int writeFd = -1;

  ~TestPipe() {
    if (readFd >= 0) {
      close(readFd);
    }
    if (writeFd >= 0) {
      close(writeFd);
    }
  }

  [[nodiscard]] bool open() {
    int fds[2] = {-1, -1};
    if (pipe(fds) != 0) {
      return false;
    }
    readFd = fds[0];
    writeFd = fds[1];
    return true;
  }
};

} // namespace

TEST_CASE("DBus support is compile-time declared") {
  CHECK((LAMBDA_HAS_DBUS == 0 || LAMBDA_HAS_DBUS == 1));
}

#if LAMBDA_HAS_DBUS

TEST_CASE("DBus supports calls signals properties and exported objects") {
  auto privateBus = startPrivateBus();
  if (!privateBus) {
    MESSAGE("Skipping D-Bus integration test because a private bus could not be started in this environment");
    return;
  }

  auto service = lambda::dbus::Bus::openAddress(privateBus->address);
  auto client = lambda::dbus::Bus::openAddress(privateBus->address);
  std::string const serviceName =
      "org.lambda.DBusTest" + std::to_string(static_cast<unsigned long long>(getpid()));
  service.requestName(serviceName);

  std::string storedMode = "ready";
  TestPipe fdPipe;
  REQUIRE(fdPipe.open());
  auto objectSlot = service.exportObject(
      "/org/lambda/DBusTest",
      lambda::dbus::ObjectDefinition{
          .methods = {
              lambda::dbus::ExportedMethod{
                  .interface = "org.lambda.DBusTest",
                  .member = "Echo",
                  .handler = [](lambda::dbus::Message& message) {
                    return lambda::dbus::MethodReply{.values = {message.readString()}};
                  },
              },
              lambda::dbus::ExportedMethod{
                  .interface = "org.lambda.DBusTest",
                  .member = "GetWriteFd",
                  .handler = [&fdPipe](lambda::dbus::Message&) {
                    return lambda::dbus::MethodReply{
                        .values = {lambda::dbus::UnixFd::borrow(fdPipe.writeFd)},
                    };
                  },
              },
          },
          .properties = {
              lambda::dbus::ExportedProperty{
                  .interface = "org.lambda.DBusTest",
                  .name = "Mode",
                  .value = std::string("ready"),
                  .writable = true,
                  .getter = [&storedMode] { return lambda::dbus::BasicValue(storedMode); },
                  .setter = [&storedMode](lambda::dbus::BasicValue const& value) {
                    storedMode = std::get<std::string>(value);
                  },
              },
          },
      });

  std::atomic<bool> running = true;
  std::thread serviceThread([&] {
    while (running.load()) {
      pollBus(service, 25);
    }
  });

  auto peerReply = client.call(lambda::dbus::MethodCall{
      .destination = serviceName,
      .path = "/org/lambda/DBusTest",
      .interface = "org.freedesktop.DBus.Peer",
      .member = "Ping",
  });
  CHECK(peerReply.valid());

  auto introspectReply = client.call(lambda::dbus::MethodCall{
      .destination = serviceName,
      .path = "/org/lambda/DBusTest",
      .interface = "org.freedesktop.DBus.Introspectable",
      .member = "Introspect",
  });
  std::string const xml = introspectReply.readString();
  CHECK(xml.find("<interface name=\"org.freedesktop.DBus.Introspectable\">") != std::string::npos);
  CHECK(xml.find("<interface name=\"org.freedesktop.DBus.Properties\">") != std::string::npos);
  CHECK(xml.find("<interface name=\"org.lambda.DBusTest\">") != std::string::npos);
  CHECK(xml.find("<method name=\"Echo\"/>") != std::string::npos);
  CHECK(xml.find("<method name=\"GetWriteFd\"/>") != std::string::npos);
  CHECK(xml.find("<property name=\"Mode\" type=\"s\" access=\"readwrite\"/>") != std::string::npos);

  auto echoReply = client.call(lambda::dbus::MethodCall{
      .destination = serviceName,
      .path = "/org/lambda/DBusTest",
      .interface = "org.lambda.DBusTest",
      .member = "Echo",
      .arguments = {std::string("hello")},
  });
  CHECK(echoReply.readString() == "hello");

  auto fdReply = client.call(lambda::dbus::MethodCall{
      .destination = serviceName,
      .path = "/org/lambda/DBusTest",
      .interface = "org.lambda.DBusTest",
      .member = "GetWriteFd",
  });
  auto writeFd = fdReply.readUnixFd();
  CHECK(writeFd.valid());
  std::string const fdPayload = "fd-ok";
  if (writeFd.valid()) {
    CHECK(write(writeFd.get(), fdPayload.data(), fdPayload.size()) == static_cast<ssize_t>(fdPayload.size()));
    char fdBuffer[16] = {};
    CHECK(read(fdPipe.readFd, fdBuffer, fdPayload.size()) == static_cast<ssize_t>(fdPayload.size()));
    CHECK(std::string(fdBuffer, fdPayload.size()) == fdPayload);
  }

  auto mode = client.getProperty(lambda::dbus::PropertyAddress{
                                     .destination = serviceName,
                                     .path = "/org/lambda/DBusTest",
                                     .interface = "org.lambda.DBusTest",
                                     .name = "Mode",
                                 },
                                 "s");
  CHECK(std::get<std::string>(mode) == "ready");

  client.setProperty(lambda::dbus::PropertyAddress{
                         .destination = serviceName,
                         .path = "/org/lambda/DBusTest",
                         .interface = "org.lambda.DBusTest",
                         .name = "Mode",
                     },
                     std::string("updated"));
  CHECK(storedMode == "updated");

  std::string signalPayload;
  auto signalSlot = client.matchSignal(lambda::dbus::SignalMatch{
                                           .sender = serviceName,
                                           .path = "/org/lambda/DBusTest",
                                           .interface = "org.lambda.DBusTest",
                                           .member = "Changed",
                                       },
                                       [&signalPayload](lambda::dbus::Message& message) {
                                         signalPayload = message.readString();
                                       });

  service.emitSignal("/org/lambda/DBusTest", "org.lambda.DBusTest", "Changed", {std::string("done")});
  service.flush();
  CHECK(pumpUntil(client, [&] { return signalPayload == "done"; }, std::chrono::milliseconds(500)));

  running = false;
  serviceThread.join();
}

#endif
