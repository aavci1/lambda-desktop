#include <Lambda/System/DBus.hpp>
#include <Lambda/UI/Application.hpp>

#include "DBusTestHelpers.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <unistd.h>

namespace {

using lambdaui::testing::dbus::pollBus;
using lambdaui::testing::dbus::pumpUntil;
using lambdaui::testing::dbus::startPrivateBus;

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

std::shared_ptr<lambdaui::dbus::ArrayValue> arrayValue(std::string elementSignature,
                                                     std::vector<lambdaui::dbus::BasicValue> values) {
  return std::make_shared<lambdaui::dbus::ArrayValue>(
      lambdaui::dbus::ArrayValue{.elementSignature = std::move(elementSignature), .values = std::move(values)});
}

std::shared_ptr<lambdaui::dbus::StructValue> structValue(std::string signature,
                                                       std::vector<lambdaui::dbus::BasicValue> fields) {
  return std::make_shared<lambdaui::dbus::StructValue>(
      lambdaui::dbus::StructValue{.signature = std::move(signature), .fields = std::move(fields)});
}

std::shared_ptr<lambdaui::dbus::DictionaryValue>
dictionaryValue(std::string keySignature,
                std::string valueSignature,
                std::vector<lambdaui::dbus::DictionaryEntry> entries) {
  return std::make_shared<lambdaui::dbus::DictionaryValue>(
      lambdaui::dbus::DictionaryValue{.keySignature = std::move(keySignature),
                                    .valueSignature = std::move(valueSignature),
                                    .entries = std::move(entries)});
}

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

  auto service = lambdaui::dbus::Bus::openAddress(privateBus->address);
  auto client = lambdaui::dbus::Bus::openAddress(privateBus->address);
  std::string const serviceName =
      "org.lambda.DBusTest" + std::to_string(static_cast<unsigned long long>(getpid()));
  service.requestName(serviceName);

  std::string storedMode = "ready";
  std::optional<lambdaui::dbus::VariantDictionary> observedOptions;
  TestPipe fdPipe;
  REQUIRE(fdPipe.open());
  auto objectSlot = service.exportObject(
      "/org/lambda/DBusTest",
      lambdaui::dbus::ObjectDefinition{
          .methods = {
              lambdaui::dbus::ExportedMethod{
                  .interface = "org.lambda.DBusTest",
                  .member = "Echo",
                  .handler = [](lambdaui::dbus::Message& message) {
                    return lambdaui::dbus::MethodReply{.values = {message.readString()}};
                  },
              },
              lambdaui::dbus::ExportedMethod{
                  .interface = "org.lambda.DBusTest",
                  .member = "Fail",
                  .handler = [](lambdaui::dbus::Message&) {
                    return lambdaui::dbus::MethodReply::error("org.lambda.DBusTest.Failed", "expected failure");
                  },
              },
              lambdaui::dbus::ExportedMethod{
                  .interface = "org.lambda.DBusTest",
                  .member = "RoundTripOptions",
                  .handler = [&observedOptions](lambdaui::dbus::Message& message) {
                    auto options = message.readVariantDictionary();
                    observedOptions = options;
                    return lambdaui::dbus::MethodReply{
                        .values = {lambdaui::dbus::BasicValue(std::make_shared<lambdaui::dbus::VariantDictionary>(
                            std::move(options)))},
                    };
                  },
              },
              lambdaui::dbus::ExportedMethod{
                  .interface = "org.lambda.DBusTest",
                  .member = "GetWriteFd",
                  .handler = [&fdPipe](lambdaui::dbus::Message&) {
                    return lambdaui::dbus::MethodReply{
                        .values = {lambdaui::dbus::UnixFd::borrow(fdPipe.writeFd)},
                    };
                  },
              },
          },
          .properties = {
              lambdaui::dbus::ExportedProperty{
                  .interface = "org.lambda.DBusTest",
                  .name = "Mode",
                  .value = std::string("ready"),
                  .writable = true,
                  .getter = [&storedMode] { return lambdaui::dbus::BasicValue(storedMode); },
                  .setter = [&storedMode](lambdaui::dbus::BasicValue const& value) {
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

  auto peerReply = client.call(lambdaui::dbus::MethodCall{
      .destination = serviceName,
      .path = "/org/lambda/DBusTest",
      .interface = "org.freedesktop.DBus.Peer",
      .member = "Ping",
  });
  CHECK(peerReply.valid());

  auto introspectReply = client.call(lambdaui::dbus::MethodCall{
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
  CHECK(xml.find("<method name=\"Fail\"/>") != std::string::npos);
  CHECK(xml.find("<method name=\"RoundTripOptions\"/>") != std::string::npos);
  CHECK(xml.find("<method name=\"GetWriteFd\"/>") != std::string::npos);
  CHECK(xml.find("<property name=\"Mode\" type=\"s\" access=\"readwrite\"/>") != std::string::npos);
  CHECK(xml.find("<signal name=\"PropertiesChanged\">") != std::string::npos);

  auto echoReply = client.call(lambdaui::dbus::MethodCall{
      .destination = serviceName,
      .path = "/org/lambda/DBusTest",
      .interface = "org.lambda.DBusTest",
      .member = "Echo",
      .arguments = {std::string("hello")},
  });
  CHECK(echoReply.readString() == "hello");

  std::string asyncPayload;
  bool asyncDone = false;
  auto pendingEcho = client.callAsync(lambdaui::dbus::MethodCall{
                                         .destination = serviceName,
                                         .path = "/org/lambda/DBusTest",
                                         .interface = "org.lambda.DBusTest",
                                         .member = "Echo",
                                         .arguments = {std::string("async")},
                                     },
                                     [&](lambdaui::dbus::AsyncMethodReply reply) {
                                       CHECK(reply.ok());
                                       asyncPayload = reply.message.readString();
                                       asyncDone = true;
                                     });
  CHECK(pendingEcho.valid());
  CHECK(pumpUntil(client, [&] { return asyncDone; }, std::chrono::milliseconds(500)));
  CHECK(asyncPayload == "async");

  bool asyncErrorDone = false;
  std::string asyncErrorName;
  std::string asyncErrorMessage;
  auto pendingError = client.callAsync(lambdaui::dbus::MethodCall{
                                          .destination = serviceName,
                                          .path = "/org/lambda/DBusTest",
                                          .interface = "org.lambda.DBusTest",
                                          .member = "Fail",
                                      },
                                      [&](lambdaui::dbus::AsyncMethodReply reply) {
                                        CHECK_FALSE(reply.ok());
                                        CHECK(reply.message.valid());
                                        asyncErrorName = reply.errorName.value_or("");
                                        asyncErrorMessage = reply.errorMessage.value_or("");
                                        asyncErrorDone = true;
                                      });
  CHECK(pendingError.valid());
  CHECK(pumpUntil(client, [&] { return asyncErrorDone; }, std::chrono::milliseconds(500)));
  CHECK(asyncErrorName == "org.lambda.DBusTest.Failed");
  CHECK(asyncErrorMessage == "expected failure");

  bool canceledCallbackCalled = false;
  auto canceled = client.callAsync(lambdaui::dbus::MethodCall{
                                      .destination = serviceName,
                                      .path = "/org/lambda/DBusTest",
                                      .interface = "org.lambda.DBusTest",
                                      .member = "Echo",
                                      .arguments = {std::string("cancel")},
                                  },
                                  [&](lambdaui::dbus::AsyncMethodReply) {
                                    canceledCallbackCalled = true;
                                  });
  CHECK(canceled.valid());
  canceled.cancel();
  CHECK_FALSE(canceled.valid());
  auto const cancelDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
  while (std::chrono::steady_clock::now() < cancelDeadline) {
    pollBus(client, 10);
  }
  CHECK_FALSE(canceledCallbackCalled);

  lambdaui::dbus::VariantDictionary nestedOptions;
  nestedOptions.values["token"] = std::string("nested-token");
  lambdaui::dbus::VariantDictionary requestOptions;
  requestOptions.values["modal"] = true;
  requestOptions.values["metadata"] = std::make_shared<lambdaui::dbus::VariantDictionary>(nestedOptions);
  requestOptions.values["labels"] = dictionaryValue(
      "s",
      "s",
      {
          lambdaui::dbus::DictionaryEntry{.key = std::string("open"), .value = std::string("Open")},
          lambdaui::dbus::DictionaryEntry{.key = std::string("cancel"), .value = std::string("Cancel")},
      });
  auto filterRules = arrayValue("(us)",
                                {structValue("us",
                                             {std::uint32_t(0),
                                              std::string("*.txt")})});
  requestOptions.values["current_filter"] =
      structValue("sa(us)", {std::string("Text files"), filterRules});
  auto choiceItems = arrayValue("(ss)",
                                {structValue("ss", {std::string("utf8"), std::string("UTF-8")}),
                                 structValue("ss", {std::string("latin1"), std::string("Latin-1")})});
  requestOptions.values["choices"] =
      arrayValue("(ssa(ss)s)",
                 {structValue("ssa(ss)s",
                              {std::string("encoding"),
                               std::string("Encoding"),
                               choiceItems,
                               std::string("utf8")})});
  auto optionReply = client.call(lambdaui::dbus::MethodCall{
      .destination = serviceName,
      .path = "/org/lambda/DBusTest",
      .interface = "org.lambda.DBusTest",
      .member = "RoundTripOptions",
      .arguments = {std::make_shared<lambdaui::dbus::VariantDictionary>(requestOptions)},
  });
  auto roundTrippedOptions = optionReply.readVariantDictionary();
  REQUIRE(observedOptions);
  CHECK(std::get<bool>(observedOptions->values["modal"]));
  auto const observedNested =
      std::get<std::shared_ptr<lambdaui::dbus::VariantDictionary>>(observedOptions->values["metadata"]);
  REQUIRE(observedNested);
  CHECK(std::get<std::string>(observedNested->values["token"]) == "nested-token");
  auto const roundTrippedLabels =
      std::get<std::shared_ptr<lambdaui::dbus::DictionaryValue>>(roundTrippedOptions.values["labels"]);
  REQUIRE(roundTrippedLabels);
  CHECK(lambdaui::dbus::signatureFor(lambdaui::dbus::BasicValue(roundTrippedLabels)) == "a{ss}");
  CHECK(roundTrippedLabels->entries.size() == 2);
  CHECK(std::get<std::string>(roundTrippedLabels->entries[0].key) == "open");
  CHECK(std::get<std::string>(roundTrippedLabels->entries[0].value) == "Open");
  auto const roundTrippedFilter =
      std::get<std::shared_ptr<lambdaui::dbus::StructValue>>(roundTrippedOptions.values["current_filter"]);
  REQUIRE(roundTrippedFilter);
  CHECK(lambdaui::dbus::signatureFor(lambdaui::dbus::BasicValue(roundTrippedFilter)) == "(sa(us))");
  REQUIRE(roundTrippedFilter->fields.size() == 2);
  CHECK(std::get<std::string>(roundTrippedFilter->fields[0]) == "Text files");
  auto const roundTrippedRules =
      std::get<std::shared_ptr<lambdaui::dbus::ArrayValue>>(roundTrippedFilter->fields[1]);
  REQUIRE(roundTrippedRules);
  CHECK(roundTrippedRules->elementSignature == "(us)");
  REQUIRE(roundTrippedRules->values.size() == 1);
  auto const roundTrippedChoices =
      std::get<std::shared_ptr<lambdaui::dbus::ArrayValue>>(roundTrippedOptions.values["choices"]);
  REQUIRE(roundTrippedChoices);
  CHECK(roundTrippedChoices->elementSignature == "(ssa(ss)s)");
  REQUIRE(roundTrippedChoices->values.size() == 1);
  auto const roundTrippedChoice =
      std::get<std::shared_ptr<lambdaui::dbus::StructValue>>(roundTrippedChoices->values[0]);
  REQUIRE(roundTrippedChoice);
  REQUIRE(roundTrippedChoice->fields.size() == 4);
  CHECK(std::get<std::string>(roundTrippedChoice->fields[0]) == "encoding");
  CHECK(std::get<std::string>(roundTrippedChoice->fields[3]) == "utf8");

  auto fdReply = client.call(lambdaui::dbus::MethodCall{
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

  auto mode = client.getProperty(lambdaui::dbus::PropertyAddress{
                                     .destination = serviceName,
                                     .path = "/org/lambda/DBusTest",
                                     .interface = "org.lambda.DBusTest",
                                     .name = "Mode",
                                 },
                                 "s");
  CHECK(std::get<std::string>(mode) == "ready");

  client.setProperty(lambdaui::dbus::PropertyAddress{
                         .destination = serviceName,
                         .path = "/org/lambda/DBusTest",
                         .interface = "org.lambda.DBusTest",
                         .name = "Mode",
                     },
                     std::string("updated"));
  CHECK(storedMode == "updated");

  std::string signalPayload;
  auto signalSlot = client.matchSignal(lambdaui::dbus::SignalMatch{
                                           .sender = serviceName,
                                           .path = "/org/lambda/DBusTest",
                                           .interface = "org.lambda.DBusTest",
                                           .member = "Changed",
                                       },
                                       [&signalPayload](lambdaui::dbus::Message& message) {
                                         signalPayload = message.readString();
                                       });

  service.emitSignal("/org/lambda/DBusTest", "org.lambda.DBusTest", "Changed", {std::string("done")});
  service.flush();
  CHECK(pumpUntil(client, [&] { return signalPayload == "done"; }, std::chrono::milliseconds(500)));

  lambdaui::dbus::PropertiesChanged propertiesChanged;
  auto propertiesChangedSlot = client.matchSignal(
      lambdaui::dbus::SignalMatch{
          .sender = serviceName,
          .path = "/org/lambda/DBusTest",
          .interface = "org.freedesktop.DBus.Properties",
          .member = "PropertiesChanged",
      },
      [&propertiesChanged](lambdaui::dbus::Message& message) {
        propertiesChanged = lambdaui::dbus::readPropertiesChanged(message);
      });
  lambdaui::dbus::VariantDictionary changedProperties;
  changedProperties.values["Mode"] = std::string("changed");
  service.emitPropertiesChanged("/org/lambda/DBusTest",
                                "org.lambda.DBusTest",
                                std::move(changedProperties),
                                lambdaui::dbus::StringArray{{"LegacyMode"}});
  service.flush();
  CHECK(pumpUntil(client,
                  [&] { return propertiesChanged.interface == "org.lambda.DBusTest"; },
                  std::chrono::milliseconds(500)));
  CHECK(std::get<std::string>(propertiesChanged.changed.values["Mode"]) == "changed");
  CHECK(propertiesChanged.invalidated.values == std::vector<std::string>{"LegacyMode"});

  running = false;
  serviceThread.join();
}

TEST_CASE("DBus event pump services calls through Application poll sources") {
  auto privateBus = startPrivateBus();
  if (!privateBus) {
    MESSAGE("Skipping D-Bus event-pump test because a private bus could not be started in this environment");
    return;
  }

  lambdaui::Application app;
  auto service = lambdaui::dbus::Bus::openAddress(privateBus->address);
  auto client = lambdaui::dbus::Bus::openAddress(privateBus->address);
  std::string const serviceName =
      "org.lambda.DBusPumpTest" + std::to_string(static_cast<unsigned long long>(getpid()));
  service.requestName(serviceName);
  auto objectSlot = service.exportObject(
      "/org/lambda/DBusPumpTest",
      lambdaui::dbus::ObjectDefinition{
          .methods = {
              lambdaui::dbus::ExportedMethod{
                  .interface = "org.lambda.DBusPumpTest",
                  .member = "Echo",
                  .handler = [](lambdaui::dbus::Message& message) {
                    return lambdaui::dbus::MethodReply{.values = {message.readString()}};
                  },
              },
          },
      });
  lambdaui::dbus::BusEventPump pump(app, service);

  std::atomic<bool> callDone = false;
  std::atomic<bool> callSucceeded = false;
  std::thread clientThread([&] {
    try {
      auto reply = client.call(lambdaui::dbus::MethodCall{
          .destination = serviceName,
          .path = "/org/lambda/DBusPumpTest",
          .interface = "org.lambda.DBusPumpTest",
          .member = "Echo",
          .arguments = {std::string("pumped")},
          .timeoutUsec = 1'000'000,
      });
      callSucceeded.store(reply.readString() == "pumped");
    } catch (...) {
      callSucceeded.store(false);
    }
    callDone.store(true);
    app.quit();
  });

  int const exitCode = app.exec();
  clientThread.join();

  CHECK(exitCode == 0);
  CHECK(callDone.load());
  CHECK(callSucceeded.load());
}

#endif
