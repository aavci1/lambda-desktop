#include <Lambda/System/PortalAppChooser.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lambdaui::system {

namespace {

constexpr std::uint32_t responseSuccess = 0;
constexpr std::uint32_t responseOther = 2;

dbus::MethodReply methodReply(std::vector<dbus::ReplyValue> values) {
  dbus::MethodReply reply;
  reply.values = std::move(values);
  return reply;
}

std::optional<std::string> stringValue(dbus::VariantDictionary const& values,
                                       std::string const& key) {
  auto it = values.values.find(key);
  if (it == values.values.end()) {
    return std::nullopt;
  }
  if (auto value = std::get_if<std::string>(&it->second)) {
    return *value;
  }
  return std::nullopt;
}

bool contains(dbus::StringArray const& choices, std::string const& value) {
  return std::find(choices.values.begin(), choices.values.end(), value) != choices.values.end();
}

} // namespace

PortalAppChooserService::PortalAppChooserService(dbus::Bus& bus) : bus_(&bus) {}

dbus::ObjectDefinition PortalAppChooserService::objectDefinition() {
  return dbus::ObjectDefinition{
      .methods = {
          dbus::ExportedMethod{
              .interface = interfaceName,
              .member = "ChooseApplication",
              .handler = [this](dbus::Message& message) {
                return chooseApplication(message);
              },
          },
          dbus::ExportedMethod{
              .interface = interfaceName,
              .member = "UpdateChoices",
              .handler = [this](dbus::Message& message) {
                return updateChoices(message);
              },
          },
      },
      .properties = {},
  };
}

dbus::Slot PortalAppChooserService::exportObject() {
  return bus_->exportObject(objectPath, objectDefinition());
}

std::optional<dbus::StringArray> PortalAppChooserService::updatedChoices(std::string const& handle) const {
  if (auto it = updatedChoices_.find(handle); it != updatedChoices_.end()) {
    return it->second;
  }
  return std::nullopt;
}

dbus::MethodReply PortalAppChooserService::chooseApplication(dbus::Message& message) {
  PortalAppChooserRequest request{
      .handle = message.readObjectPath(),
      .appId = message.readString(),
      .parentWindow = message.readString(),
      .choices = message.readStringArray(),
      .options = message.readVariantDictionary(),
  };

  auto selected = choose(request.choices, request.options);
  lastRequest_ = request;
  if (!selected) {
    return methodReply({responseOther, std::make_shared<dbus::VariantDictionary>()});
  }

  auto results = std::make_shared<dbus::VariantDictionary>();
  results->values["choice"] = *selected;
  if (auto activationToken = stringValue(request.options, "activation_token")) {
    results->values["activation_token"] = *activationToken;
  }
  return methodReply({responseSuccess, results});
}

dbus::MethodReply PortalAppChooserService::updateChoices(dbus::Message& message) {
  auto handle = message.readObjectPath();
  auto choices = message.readStringArray();
  updatedChoices_[handle.value] = std::move(choices);
  return dbus::MethodReply{};
}

std::optional<std::string> PortalAppChooserService::choose(dbus::StringArray const& choices,
                                                           dbus::VariantDictionary const& options) const {
  if (choices.values.empty()) {
    return std::nullopt;
  }
  if (auto lastChoice = stringValue(options, "last_choice")) {
    if (contains(choices, *lastChoice)) {
      return lastChoice;
    }
  }
  return choices.values.front();
}

} // namespace lambdaui::system
