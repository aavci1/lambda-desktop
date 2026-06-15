#include <Lambda/System/PolkitAgent.hpp>

#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace lambda::system {

namespace {

std::uint64_t processStartTime(pid_t pid) {
  std::ifstream stat("/proc/" + std::to_string(static_cast<long long>(pid)) + "/stat");
  std::string line;
  if (!std::getline(stat, line)) {
    return 0;
  }

  auto const commandEnd = line.rfind(") ");
  if (commandEnd == std::string::npos) {
    return 0;
  }

  std::istringstream fields(line.substr(commandEnd + 2));
  std::vector<std::string> values;
  std::string value;
  while (fields >> value) {
    values.push_back(value);
  }
  if (values.size() <= 19) {
    return 0;
  }

  try {
    return static_cast<std::uint64_t>(std::stoull(values[19]));
  } catch (...) {
    return 0;
  }
}

std::string nonEmptyEnvironment(char const* name) {
  char const* value = std::getenv(name);
  return value && *value ? std::string(value) : std::string();
}

std::shared_ptr<dbus::VariantDictionary> detailsFor(PolkitSubject const& subject) {
  return std::make_shared<dbus::VariantDictionary>(subject.details);
}

std::map<std::string, std::string> readStringDictionary(dbus::Message& message) {
  std::map<std::string, std::string> result;
  auto value = message.readBasic("a{ss}");
  auto const dictionary = std::get<std::shared_ptr<dbus::DictionaryValue>>(value);
  if (!dictionary) {
    return result;
  }
  for (auto const& entry : dictionary->entries) {
    result[std::get<std::string>(entry.key)] = std::get<std::string>(entry.value);
  }
  return result;
}

std::size_t readIdentityCount(dbus::Message& message) {
  auto value = message.readBasic("a(sa{sv})");
  auto const identities = std::get<std::shared_ptr<dbus::ArrayValue>>(value);
  return identities ? identities->values.size() : 0;
}

} // namespace

PolkitSubject polkitUnixSessionSubject(std::string sessionId) {
  dbus::VariantDictionary details;
  details.values["session-id"] = std::move(sessionId);
  return PolkitSubject{
      .kind = "unix-session",
      .details = std::move(details),
  };
}

PolkitSubject polkitUnixProcessSubject(std::uint32_t pid,
                                       std::int32_t uid,
                                       std::uint64_t startTime) {
  dbus::VariantDictionary details;
  details.values["pid"] = pid;
  details.values["uid"] = uid;
  details.values["start-time"] = startTime;
  return PolkitSubject{
      .kind = "unix-process",
      .details = std::move(details),
  };
}

PolkitSubject currentPolkitSubject() {
  std::string const sessionId = nonEmptyEnvironment("XDG_SESSION_ID");
  if (!sessionId.empty()) {
    return polkitUnixSessionSubject(sessionId);
  }
  auto const pid = static_cast<std::uint32_t>(getpid());
  auto const uid = static_cast<std::int32_t>(geteuid());
  return polkitUnixProcessSubject(pid, uid, processStartTime(static_cast<pid_t>(pid)));
}

dbus::BasicValue polkitSubjectValue(PolkitSubject const& subject) {
  return std::make_shared<dbus::StructValue>(
      dbus::StructValue{.signature = "sa{sv}",
                        .fields = {subject.kind, detailsFor(subject)}});
}

std::string polkitLocaleFromEnvironment() {
  for (char const* name : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
    std::string value = nonEmptyEnvironment(name);
    if (!value.empty()) {
      return value;
    }
  }
  return "C";
}

PolkitAuthenticationAgentService::PolkitAuthenticationAgentService(dbus::Bus& bus)
    : PolkitAuthenticationAgentService(bus, currentPolkitSubject()) {}

PolkitAuthenticationAgentService::PolkitAuthenticationAgentService(dbus::Bus& bus,
                                                                   PolkitSubject subject,
                                                                   std::string locale,
                                                                   std::string objectPath)
    : bus_(&bus),
      subject_(std::move(subject)),
      locale_(std::move(locale)),
      objectPath_(std::move(objectPath)) {}

dbus::Slot PolkitAuthenticationAgentService::exportObject() {
  return bus_->exportObject(
      objectPath_,
      dbus::ObjectDefinition{
          .methods = {
              dbus::ExportedMethod{
                  .interface = agentInterfaceName,
                  .member = "BeginAuthentication",
                  .handler = [this](dbus::Message& message) {
                    return beginAuthentication(message);
                  },
              },
              dbus::ExportedMethod{
                  .interface = agentInterfaceName,
                  .member = "CancelAuthentication",
                  .handler = [this](dbus::Message& message) {
                    return cancelAuthentication(message);
                  },
              },
          },
          .properties = {},
      });
}

void PolkitAuthenticationAgentService::registerAgent() {
  (void)bus_->call(dbus::MethodCall{
      .destination = authorityServiceName,
      .path = authorityObjectPath,
      .interface = authorityInterfaceName,
      .member = "RegisterAuthenticationAgent",
      .arguments = {polkitSubjectValue(subject_), locale_, objectPath_},
  });
}

void PolkitAuthenticationAgentService::unregisterAgent() {
  (void)bus_->call(dbus::MethodCall{
      .destination = authorityServiceName,
      .path = authorityObjectPath,
      .interface = authorityInterfaceName,
      .member = "UnregisterAuthenticationAgent",
      .arguments = {polkitSubjectValue(subject_), objectPath_},
  });
}

dbus::MethodReply PolkitAuthenticationAgentService::beginAuthentication(dbus::Message& message) {
  PolkitAuthenticationRequest request;
  request.actionId = message.readString();
  request.message = message.readString();
  request.iconName = message.readString();
  request.details = readStringDictionary(message);
  request.cookie = message.readString();
  request.identityCount = readIdentityCount(message);
  lastAuthentication_ = std::move(request);

  return dbus::MethodReply::error("org.freedesktop.PolicyKit1.Error.Cancelled",
                                  "Lambda polkit authentication UI is not implemented yet");
}

dbus::MethodReply PolkitAuthenticationAgentService::cancelAuthentication(dbus::Message& message) {
  cancelledCookies_.push_back(message.readString());
  return dbus::MethodReply{};
}

} // namespace lambda::system
