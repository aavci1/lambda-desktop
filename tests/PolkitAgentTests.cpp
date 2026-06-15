#include <Lambda/System/PolkitAgent.hpp>

#include "DBusTestHelpers.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <exception>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace {

using lambda::testing::dbus::pollBus;
using lambda::testing::dbus::startPrivateBus;

struct BlockingCallResult {
  bool completed = false;
  std::optional<std::string> errorName;
  std::optional<std::string> errorMessage;
  std::exception_ptr unexpected;
};

template <typename Pump, typename Call>
BlockingCallResult runBlockingCall(Pump pump, Call call) {
  std::atomic<bool> done = false;
  BlockingCallResult result;
  std::thread caller([&] {
    try {
      call();
      result.completed = true;
    } catch (lambda::dbus::Error const& error) {
      result.errorName = error.name();
      result.errorMessage = error.what();
    } catch (...) {
      result.unexpected = std::current_exception();
    }
    done = true;
  });

  while (!done.load()) {
    pump();
  }
  caller.join();
  if (result.unexpected) {
    std::rethrow_exception(result.unexpected);
  }
  return result;
}

std::shared_ptr<lambda::dbus::DictionaryValue>
stringDictionary(std::vector<std::pair<std::string, std::string>> entries) {
  std::vector<lambda::dbus::DictionaryEntry> values;
  values.reserve(entries.size());
  for (auto& [key, value] : entries) {
    values.push_back(lambda::dbus::DictionaryEntry{.key = std::move(key),
                                                   .value = std::move(value)});
  }
  return std::make_shared<lambda::dbus::DictionaryValue>(
      lambda::dbus::DictionaryValue{.keySignature = "s",
                                    .valueSignature = "s",
                                    .entries = std::move(values)});
}

std::shared_ptr<lambda::dbus::ArrayValue>
identityArray(std::vector<lambda::dbus::BasicValue> identities) {
  return std::make_shared<lambda::dbus::ArrayValue>(
      lambda::dbus::ArrayValue{.elementSignature = "(sa{sv})",
                               .values = std::move(identities)});
}

std::optional<lambda::system::PolkitSubject> readSubject(lambda::dbus::Message& message) {
  auto value = message.readBasic("(sa{sv})");
  auto const subject = std::get<std::shared_ptr<lambda::dbus::StructValue>>(value);
  if (!subject || subject->fields.size() != 2) {
    return std::nullopt;
  }
  auto const details =
      std::get<std::shared_ptr<lambda::dbus::VariantDictionary>>(subject->fields[1]);
  if (!details) {
    return std::nullopt;
  }
  return lambda::system::PolkitSubject{
      .kind = std::get<std::string>(subject->fields[0]),
      .details = *details,
  };
}

} // namespace

TEST_CASE("Polkit agent support is compile-time declared") {
  CHECK((LAMBDA_HAS_DBUS == 0 || LAMBDA_HAS_DBUS == 1));
}

#if LAMBDA_HAS_DBUS

TEST_CASE("Polkit subjects use documented D-Bus shapes") {
  auto session = lambda::system::polkitUnixSessionSubject("session-7");
  auto sessionValue = lambda::system::polkitSubjectValue(session);
  CHECK(lambda::dbus::signatureFor(sessionValue) == "(sa{sv})");
  auto sessionStruct = std::get<std::shared_ptr<lambda::dbus::StructValue>>(sessionValue);
  REQUIRE(sessionStruct);
  REQUIRE(sessionStruct->fields.size() == 2);
  CHECK(std::get<std::string>(sessionStruct->fields[0]) == "unix-session");
  auto sessionDetails =
      std::get<std::shared_ptr<lambda::dbus::VariantDictionary>>(sessionStruct->fields[1]);
  REQUIRE(sessionDetails);
  CHECK(std::get<std::string>(sessionDetails->values.at("session-id")) == "session-7");

  auto process = lambda::system::polkitUnixProcessSubject(123, 1000, 456);
  auto processStruct =
      std::get<std::shared_ptr<lambda::dbus::StructValue>>(lambda::system::polkitSubjectValue(process));
  REQUIRE(processStruct);
  auto processDetails =
      std::get<std::shared_ptr<lambda::dbus::VariantDictionary>>(processStruct->fields[1]);
  REQUIRE(processDetails);
  CHECK(std::get<std::uint32_t>(processDetails->values.at("pid")) == 123);
  CHECK(std::get<std::int32_t>(processDetails->values.at("uid")) == 1000);
  CHECK(std::get<std::uint64_t>(processDetails->values.at("start-time")) == 456);
}

TEST_CASE("PolkitAuthenticationAgentService registers and exports authentication methods") {
  auto privateBus = startPrivateBus();
  if (!privateBus) {
    MESSAGE("Skipping polkit agent integration test because a private bus could not be started");
    return;
  }

  auto authorityBus = lambda::dbus::Bus::openAddress(privateBus->address);
  auto agentBus = lambda::dbus::Bus::openAddress(privateBus->address);
  auto client = lambda::dbus::Bus::openAddress(privateBus->address);
  authorityBus.requestName(lambda::system::PolkitAuthenticationAgentService::authorityServiceName);

  std::optional<lambda::system::PolkitSubject> registeredSubject;
  std::string registeredLocale;
  std::string registeredPath;
  std::optional<lambda::system::PolkitSubject> unregisteredSubject;
  std::string unregisteredPath;

  auto authoritySlot = authorityBus.exportObject(
      lambda::system::PolkitAuthenticationAgentService::authorityObjectPath,
      lambda::dbus::ObjectDefinition{
          .methods = {
              lambda::dbus::ExportedMethod{
                  .interface = lambda::system::PolkitAuthenticationAgentService::authorityInterfaceName,
                  .member = "RegisterAuthenticationAgent",
                  .handler = [&](lambda::dbus::Message& message) {
                    registeredSubject = readSubject(message);
                    registeredLocale = message.readString();
                    registeredPath = message.readString();
                    return lambda::dbus::MethodReply{};
                  },
              },
              lambda::dbus::ExportedMethod{
                  .interface = lambda::system::PolkitAuthenticationAgentService::authorityInterfaceName,
                  .member = "UnregisterAuthenticationAgent",
                  .handler = [&](lambda::dbus::Message& message) {
                    unregisteredSubject = readSubject(message);
                    unregisteredPath = message.readString();
                    return lambda::dbus::MethodReply{};
                  },
              },
          },
          .properties = {},
      });

  auto subject = lambda::system::polkitUnixSessionSubject("lambda-session");
  lambda::system::PolkitAuthenticationAgentService agent(
      agentBus,
      subject,
      "C",
      lambda::system::PolkitAuthenticationAgentService::defaultObjectPath);
  auto agentSlot = agent.exportObject();

  auto registerResult = runBlockingCall([&] { pollBus(authorityBus, 10); },
                                        [&] { agent.registerAgent(); });
  CHECK(registerResult.completed);
  CHECK_FALSE(registerResult.errorName);
  REQUIRE(registeredSubject);
  CHECK(registeredSubject->kind == "unix-session");
  CHECK(std::get<std::string>(registeredSubject->details.values.at("session-id")) ==
        "lambda-session");
  CHECK(registeredLocale == "C");
  CHECK(registeredPath == lambda::system::PolkitAuthenticationAgentService::defaultObjectPath);

  auto details = stringDictionary({{"polkit.caller-pid", "321"},
                                   {"polkit.subject-pid", "654"}});
  auto identities =
      identityArray({lambda::system::polkitSubjectValue(
          lambda::system::polkitUnixProcessSubject(321, 1000, 456))});
  auto beginResult = runBlockingCall(
      [&] { pollBus(agentBus, 10); },
      [&] {
        (void)client.call(lambda::dbus::MethodCall{
            .destination = agentBus.uniqueName(),
            .path = lambda::system::PolkitAuthenticationAgentService::defaultObjectPath,
            .interface = lambda::system::PolkitAuthenticationAgentService::agentInterfaceName,
            .member = "BeginAuthentication",
            .arguments = {std::string("org.lambda.test.action"),
                          std::string("Authenticate to run a test action"),
                          std::string("dialog-password"),
                          details,
                          std::string("cookie-1"),
                          identities},
        });
      });
  CHECK_FALSE(beginResult.completed);
  CHECK(beginResult.errorName == "org.freedesktop.PolicyKit1.Error.Cancelled");
  REQUIRE(agent.lastAuthentication());
  CHECK(agent.lastAuthentication()->actionId == "org.lambda.test.action");
  CHECK(agent.lastAuthentication()->message == "Authenticate to run a test action");
  CHECK(agent.lastAuthentication()->iconName == "dialog-password");
  CHECK(agent.lastAuthentication()->details.at("polkit.caller-pid") == "321");
  CHECK(agent.lastAuthentication()->cookie == "cookie-1");
  CHECK(agent.lastAuthentication()->identityCount == 1);

  auto cancelResult = runBlockingCall(
      [&] { pollBus(agentBus, 10); },
      [&] {
        auto reply = client.call(lambda::dbus::MethodCall{
            .destination = agentBus.uniqueName(),
            .path = lambda::system::PolkitAuthenticationAgentService::defaultObjectPath,
            .interface = lambda::system::PolkitAuthenticationAgentService::agentInterfaceName,
            .member = "CancelAuthentication",
            .arguments = {std::string("cookie-1")},
        });
        CHECK(reply.valid());
      });
  CHECK(cancelResult.completed);
  CHECK_FALSE(cancelResult.errorName);
  REQUIRE(agent.cancelledCookies().size() == 1);
  CHECK(agent.cancelledCookies()[0] == "cookie-1");

  auto unregisterResult = runBlockingCall([&] { pollBus(authorityBus, 10); },
                                          [&] { agent.unregisterAgent(); });
  CHECK(unregisterResult.completed);
  CHECK_FALSE(unregisterResult.errorName);
  REQUIRE(unregisteredSubject);
  CHECK(unregisteredSubject->kind == "unix-session");
  CHECK(unregisteredPath == lambda::system::PolkitAuthenticationAgentService::defaultObjectPath);
}

#endif
