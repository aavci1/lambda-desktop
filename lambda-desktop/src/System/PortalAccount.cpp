#include <Lambda/System/PortalAccount.hpp>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <pwd.h>
#include <string>
#include <system_error>
#include <sys/types.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace lambdaui::system {

namespace {

constexpr std::uint32_t responseSuccess = 0;

dbus::MethodReply methodReply(std::vector<dbus::ReplyValue> values) {
  dbus::MethodReply reply;
  reply.values = std::move(values);
  return reply;
}

std::string environmentValue(char const* name) {
  if (auto const* value = std::getenv(name)) {
    return value;
  }
  return {};
}

std::string stringValue(dbus::VariantDictionary const& values,
                        std::string const& key,
                        std::string fallback = {}) {
  auto it = values.values.find(key);
  if (it == values.values.end()) {
    return fallback;
  }
  if (auto value = std::get_if<std::string>(&it->second)) {
    return *value;
  }
  return fallback;
}

std::string displayNameFromGecos(std::string gecos, std::string const& id) {
  if (auto comma = gecos.find(','); comma != std::string::npos) {
    gecos.resize(comma);
  }
  if (!gecos.empty() && gecos.front() == '&') {
    std::string expanded = id;
    if (!expanded.empty()) {
      expanded.front() =
          static_cast<char>(std::toupper(static_cast<unsigned char>(expanded.front())));
    }
    gecos.replace(0, 1, expanded);
  }
  return gecos;
}

std::string fileUriIfExists(std::filesystem::path const& path) {
  std::error_code error;
  if (path.empty() || !std::filesystem::is_regular_file(path, error)) {
    return {};
  }
  return "file://" + path.string();
}

std::string accountImageUri() {
  std::filesystem::path const home = environmentValue("HOME");
  if (home.empty()) {
    return {};
  }
  if (auto face = fileUriIfExists(home / ".face"); !face.empty()) {
    return face;
  }
  return fileUriIfExists(home / ".face.icon");
}

} // namespace

PortalAccountService::PortalAccountService(dbus::Bus& bus, PortalAccountState state)
    : bus_(&bus),
      state_(std::move(state)) {
  if (state_.id.empty()) {
    state_.id = "lambda";
  }
  if (state_.name.empty()) {
    state_.name = state_.id;
  }
}

PortalAccountState PortalAccountService::stateFromSystem() {
  PortalAccountState state;
  passwd const* entry = getpwuid(getuid());
  if (entry && entry->pw_name) {
    state.id = entry->pw_name;
  }
  if (state.id.empty()) {
    state.id = environmentValue("USER");
  }
  if (state.id.empty()) {
    state.id = environmentValue("LOGNAME");
  }
  if (entry && entry->pw_gecos) {
    state.name = displayNameFromGecos(entry->pw_gecos, state.id);
  }
  if (state.name.empty()) {
    state.name = state.id;
  }
  state.imageUri = accountImageUri();
  return state;
}

dbus::ObjectDefinition PortalAccountService::objectDefinition() {
  return dbus::ObjectDefinition{
      .methods = {
          dbus::ExportedMethod{
              .interface = interfaceName,
              .member = "GetUserInformation",
              .handler = [this](dbus::Message& message) {
                return getUserInformation(message);
              },
          },
      },
      .properties = {},
  };
}

dbus::Slot PortalAccountService::exportObject() {
  return bus_->exportObject(objectPath, objectDefinition());
}

dbus::MethodReply PortalAccountService::getUserInformation(dbus::Message& message) {
  PortalAccountRequest request{
      .handle = message.readObjectPath(),
      .appId = message.readString(),
      .window = message.readString(),
      .reason = stringValue(message.readVariantDictionary(), "reason"),
  };
  lastRequest_ = std::move(request);

  auto results = std::make_shared<dbus::VariantDictionary>();
  results->values["id"] = state_.id;
  results->values["name"] = state_.name;
  results->values["image"] = state_.imageUri;
  return methodReply({responseSuccess, results});
}

} // namespace lambdaui::system
