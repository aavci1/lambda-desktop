#include <Lambda/System/PortalFileChooser.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
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

std::shared_ptr<dbus::VariantDictionary> emptyResults() {
  return std::make_shared<dbus::VariantDictionary>();
}

std::string byteArrayString(dbus::ByteArray const& bytes) {
  std::string value;
  value.reserve(bytes.values.size());
  for (auto byte : bytes.values) {
    if (byte == 0) {
      break;
    }
    value.push_back(static_cast<char>(byte));
  }
  return value;
}

std::optional<std::string> stringOption(dbus::VariantDictionary const& options,
                                        std::string const& key) {
  auto it = options.values.find(key);
  if (it == options.values.end()) {
    return std::nullopt;
  }
  if (auto value = std::get_if<std::string>(&it->second)) {
    return *value;
  }
  return std::nullopt;
}

std::optional<std::string> byteArrayOption(dbus::VariantDictionary const& options,
                                           std::string const& key) {
  auto it = options.values.find(key);
  if (it == options.values.end()) {
    return std::nullopt;
  }
  if (auto value = std::get_if<dbus::ByteArray>(&it->second)) {
    auto decoded = byteArrayString(*value);
    if (!decoded.empty()) {
      return decoded;
    }
  }
  return std::nullopt;
}

std::vector<std::string> byteArrayArrayOption(dbus::VariantDictionary const& options,
                                              std::string const& key) {
  std::vector<std::string> values;
  auto it = options.values.find(key);
  if (it == options.values.end()) {
    return values;
  }
  if (auto array = std::get_if<dbus::ByteArrayArray>(&it->second)) {
    values.reserve(array->values.size());
    for (auto const& item : array->values) {
      auto decoded = byteArrayString(item);
      if (!decoded.empty()) {
        values.push_back(std::move(decoded));
      }
    }
  }
  return values;
}

std::optional<bool> boolOption(dbus::VariantDictionary const& options, std::string const& key) {
  auto it = options.values.find(key);
  if (it == options.values.end()) {
    return std::nullopt;
  }
  if (auto value = std::get_if<bool>(&it->second)) {
    return *value;
  }
  return std::nullopt;
}

std::optional<std::string> environmentValue(char const* name) {
  if (char const* value = std::getenv(name); value && *value) {
    return std::string(value);
  }
  return std::nullopt;
}

bool isUnreservedPathByte(unsigned char byte) {
  return std::isalnum(byte) || byte == '-' || byte == '_' || byte == '.' ||
         byte == '~' || byte == '/';
}

std::string fileUriFromPath(std::filesystem::path path) {
  if (path.empty()) {
    return {};
  }
  if (path.is_relative()) {
    path = std::filesystem::absolute(path);
  }
  auto generic = path.lexically_normal().generic_string();
  if (generic.empty() || generic.front() != '/') {
    return {};
  }

  std::ostringstream uri;
  uri << "file://";
  for (unsigned char byte : generic) {
    if (isUnreservedPathByte(byte)) {
      uri << static_cast<char>(byte);
    } else {
      uri << '%' << std::uppercase << std::hex << std::setw(2)
          << std::setfill('0') << static_cast<int>(byte) << std::nouppercase
          << std::dec;
    }
  }
  return uri.str();
}

std::optional<std::string> normalizedFileUri(std::string value) {
  if (value.starts_with("file://")) {
    return value;
  }
  auto uri = fileUriFromPath(std::filesystem::path(std::move(value)));
  if (uri.empty()) {
    return std::nullopt;
  }
  return uri;
}

std::filesystem::path fallbackFolder() {
  if (auto folder = environmentValue("LAMBDA_PORTAL_FILECHOOSER_FOLDER")) {
    return *folder;
  }
  if (auto home = environmentValue("HOME")) {
    return *home;
  }
  return std::filesystem::temp_directory_path();
}

std::vector<std::string> splitUriList(std::string const& value) {
  std::vector<std::string> uris;
  std::size_t begin = 0;
  while (begin <= value.size()) {
    auto end = value.find('\n', begin);
    auto item = value.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    if (!item.empty()) {
      if (auto uri = normalizedFileUri(std::move(item))) {
        uris.push_back(std::move(*uri));
      }
    }
    if (end == std::string::npos) {
      break;
    }
    begin = end + 1;
  }
  return uris;
}

std::vector<std::string> openUris(dbus::VariantDictionary const& options) {
  if (auto uriList = environmentValue("LAMBDA_PORTAL_FILECHOOSER_OPEN_URIS")) {
    auto uris = splitUriList(*uriList);
    if (!uris.empty()) {
      return uris;
    }
  }
  if (auto currentFile = byteArrayOption(options, "current_file")) {
    if (auto uri = normalizedFileUri(std::move(*currentFile))) {
      return {*uri};
    }
  }
  if (boolOption(options, "directory").value_or(false)) {
    if (auto currentFolder = byteArrayOption(options, "current_folder")) {
      if (auto uri = normalizedFileUri(std::move(*currentFolder))) {
        return {*uri};
      }
    }
  }
  return {};
}

std::vector<std::string> saveFileUris(dbus::VariantDictionary const& options) {
  if (auto explicitUri = environmentValue("LAMBDA_PORTAL_FILECHOOSER_SAVE_URI")) {
    if (auto uri = normalizedFileUri(std::move(*explicitUri))) {
      return {*uri};
    }
  }
  if (auto currentFile = byteArrayOption(options, "current_file")) {
    if (auto uri = normalizedFileUri(std::move(*currentFile))) {
      return {*uri};
    }
  }

  auto folder = byteArrayOption(options, "current_folder")
                    .value_or(fallbackFolder().generic_string());
  auto name = stringOption(options, "current_name").value_or("lambda-selected-file");
  if (name.empty()) {
    name = "lambda-selected-file";
  }
  if (auto uri = normalizedFileUri((std::filesystem::path(folder) / name).generic_string())) {
    return {*uri};
  }
  return {};
}

std::vector<std::string> saveFilesUris(dbus::VariantDictionary const& options) {
  auto folder = std::filesystem::path(byteArrayOption(options, "current_folder")
                                         .value_or(fallbackFolder().generic_string()));
  auto files = byteArrayArrayOption(options, "files");
  std::vector<std::string> uris;
  uris.reserve(files.size());
  for (auto const& file : files) {
    if (auto uri = normalizedFileUri((folder / file).generic_string())) {
      uris.push_back(std::move(*uri));
    }
  }
  return uris;
}

std::shared_ptr<dbus::StructValue> structValue(std::string signature,
                                               std::vector<dbus::BasicValue> fields) {
  return std::make_shared<dbus::StructValue>(
      dbus::StructValue{.signature = std::move(signature), .fields = std::move(fields)});
}

std::shared_ptr<dbus::ArrayValue> selectedChoices(dbus::VariantDictionary const& options) {
  auto result = std::make_shared<dbus::ArrayValue>();
  result->elementSignature = "(ss)";
  auto it = options.values.find("choices");
  if (it == options.values.end()) {
    return result;
  }
  auto const* choices = std::get_if<std::shared_ptr<dbus::ArrayValue>>(&it->second);
  if (!choices || !*choices) {
    return result;
  }
  for (auto const& entry : (*choices)->values) {
    auto const* choice = std::get_if<std::shared_ptr<dbus::StructValue>>(&entry);
    if (!choice || !*choice || (*choice)->fields.size() != 4) {
      continue;
    }
    auto const* id = std::get_if<std::string>(&(*choice)->fields[0]);
    auto const* selected = std::get_if<std::string>(&(*choice)->fields[3]);
    if (!id || id->empty()) {
      continue;
    }
    result->values.push_back(structValue("ss", {*id, selected ? *selected : std::string()}));
  }
  return result;
}

std::shared_ptr<dbus::VariantDictionary> resultsFor(dbus::VariantDictionary const& options,
                                                    std::vector<std::string> const& uris) {
  auto results = std::make_shared<dbus::VariantDictionary>();
  results->values["uris"] = dbus::StringArray{uris};
  auto choices = selectedChoices(options);
  if (!choices->values.empty()) {
    results->values["choices"] = std::move(choices);
  }
  if (auto filter = options.values.find("current_filter"); filter != options.values.end()) {
    results->values["current_filter"] = filter->second;
  }
  return results;
}

std::vector<std::string> chooseUris(PortalFileChooserKind kind,
                                    dbus::VariantDictionary const& options) {
  switch (kind) {
  case PortalFileChooserKind::OpenFile:
    return openUris(options);
  case PortalFileChooserKind::SaveFile:
    return saveFileUris(options);
  case PortalFileChooserKind::SaveFiles:
    return saveFilesUris(options);
  }
  return {};
}

} // namespace

PortalFileChooserService::PortalFileChooserService(dbus::Bus& bus) : bus_(&bus) {}

dbus::ObjectDefinition PortalFileChooserService::objectDefinition() {
  return dbus::ObjectDefinition{
      .methods = {
          dbus::ExportedMethod{
              .interface = interfaceName,
              .member = "OpenFile",
              .handler = [this](dbus::Message& message) {
                return openFile(message);
              },
          },
          dbus::ExportedMethod{
              .interface = interfaceName,
              .member = "SaveFile",
              .handler = [this](dbus::Message& message) {
                return saveFile(message);
              },
          },
          dbus::ExportedMethod{
              .interface = interfaceName,
              .member = "SaveFiles",
              .handler = [this](dbus::Message& message) {
                return saveFiles(message);
              },
          },
      },
      .properties = {},
  };
}

dbus::Slot PortalFileChooserService::exportObject() {
  return bus_->exportObject(objectPath, objectDefinition());
}

std::optional<PortalFileChooserRequest>
PortalFileChooserService::request(std::string const& handle) const {
  if (auto it = requests_.find(handle); it != requests_.end()) {
    return it->second;
  }
  return std::nullopt;
}

dbus::MethodReply PortalFileChooserService::openFile(dbus::Message& message) {
  return choose(message, PortalFileChooserKind::OpenFile);
}

dbus::MethodReply PortalFileChooserService::saveFile(dbus::Message& message) {
  return choose(message, PortalFileChooserKind::SaveFile);
}

dbus::MethodReply PortalFileChooserService::saveFiles(dbus::Message& message) {
  return choose(message, PortalFileChooserKind::SaveFiles);
}

dbus::MethodReply PortalFileChooserService::choose(dbus::Message& message,
                                                   PortalFileChooserKind kind) {
  auto handle = message.readObjectPath();
  PortalFileChooserRequest request{
      .handle = handle,
      .appId = message.readString(),
      .parentWindow = message.readString(),
      .title = message.readString(),
      .kind = kind,
      .options = message.readVariantDictionary(),
      .uris = {},
      .closed = false,
  };
  request.uris = chooseUris(kind, request.options);

  exportRequestObject(handle.value);
  auto const success = !request.uris.empty();
  auto results = success ? resultsFor(request.options, request.uris) : emptyResults();
  requests_[handle.value] = std::move(request);
  return methodReply({success ? responseSuccess : responseOther, results});
}

dbus::MethodReply PortalFileChooserService::closeRequest(std::string const& handle) {
  if (auto it = requests_.find(handle); it != requests_.end()) {
    it->second.closed = true;
  }
  return dbus::MethodReply{};
}

void PortalFileChooserService::exportRequestObject(std::string const& handle) {
  if (requestSlots_.find(handle) != requestSlots_.end()) {
    return;
  }
  auto slot = bus_->exportObject(
      handle,
      dbus::ObjectDefinition{
          .methods = {
              dbus::ExportedMethod{
                  .interface = requestInterfaceName,
                  .member = "Close",
                  .handler = [this, handle](dbus::Message&) {
                    return closeRequest(handle);
                  },
              },
          },
          .properties = {},
      });
  requestSlots_.emplace(handle, std::move(slot));
}

} // namespace lambdaui::system
