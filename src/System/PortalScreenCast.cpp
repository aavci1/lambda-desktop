#include <Lambda/System/PortalScreenCast.hpp>

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lambda::system {

namespace {

constexpr std::uint32_t responseSuccess = 0;
constexpr std::uint32_t responseOther = 2;
constexpr std::uint32_t availableSourceTypes = 1; // MONITOR
constexpr std::uint32_t availableCursorModes = 1; // Hidden
constexpr std::uint32_t screenCastVersion = 6;
constexpr std::uint32_t sessionVersion = 1;

dbus::MethodReply methodReply(std::vector<dbus::ReplyValue> values) {
  dbus::MethodReply reply;
  reply.values = std::move(values);
  return reply;
}

std::shared_ptr<dbus::VariantDictionary> emptyResults() {
  return std::make_shared<dbus::VariantDictionary>();
}

std::optional<std::string> environmentValue(char const* name) {
  if (char const* value = std::getenv(name); value && *value) {
    return std::string(value);
  }
  return std::nullopt;
}

std::optional<std::uint64_t> parseUnsigned(std::string const& value) {
  char* end = nullptr;
  unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
  if (end == value.c_str() || *end != '\0') {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(parsed);
}

std::optional<std::int32_t> parseInt32(std::string const& value) {
  char* end = nullptr;
  long parsed = std::strtol(value.c_str(), &end, 10);
  if (end == value.c_str() || *end != '\0') {
    return std::nullopt;
  }
  return static_cast<std::int32_t>(parsed);
}

std::optional<std::uint32_t> environmentUint32(char const* name) {
  auto value = environmentValue(name);
  if (!value) {
    return std::nullopt;
  }
  auto parsed = parseUnsigned(*value);
  if (!parsed) {
    return std::nullopt;
  }
  return static_cast<std::uint32_t>(*parsed);
}

std::optional<std::uint64_t> environmentUint64(char const* name) {
  auto value = environmentValue(name);
  if (!value) {
    return std::nullopt;
  }
  return parseUnsigned(*value);
}

std::optional<std::int32_t> environmentInt32(char const* name) {
  auto value = environmentValue(name);
  if (!value) {
    return std::nullopt;
  }
  return parseInt32(*value);
}

std::optional<std::uint32_t> uintOption(dbus::VariantDictionary const& options,
                                        std::string const& key) {
  auto it = options.values.find(key);
  if (it == options.values.end()) {
    return std::nullopt;
  }
  if (auto value = std::get_if<std::uint32_t>(&it->second)) {
    return *value;
  }
  return std::nullopt;
}

std::string sessionIdFromHandle(dbus::ObjectPath const& handle) {
  auto const slash = handle.value.find_last_of('/');
  if (slash == std::string::npos || slash + 1u >= handle.value.size()) {
    return handle.value;
  }
  return handle.value.substr(slash + 1u);
}

std::shared_ptr<dbus::StructValue> structValue(std::string signature,
                                               std::vector<dbus::BasicValue> fields) {
  return std::make_shared<dbus::StructValue>(
      dbus::StructValue{.signature = std::move(signature), .fields = std::move(fields)});
}

std::shared_ptr<dbus::ArrayValue> emptyStreamsValue() {
  return std::make_shared<dbus::ArrayValue>(
      dbus::ArrayValue{.elementSignature = "(ua{sv})", .values = {}});
}

std::shared_ptr<dbus::ArrayValue> streamsValue(std::vector<PortalScreenCastStream> const& streams) {
  auto array = emptyStreamsValue();
  array->values.reserve(streams.size());
  for (auto const& stream : streams) {
    auto properties = std::make_shared<dbus::VariantDictionary>();
    properties->values["source_type"] = stream.sourceType;
    if (stream.width > 0 && stream.height > 0) {
      properties->values["size"] = structValue("ii", {stream.width, stream.height});
    }
    properties->values["position"] = structValue("ii", {stream.x, stream.y});
    if (stream.pipeWireSerial > 0) {
      properties->values["pipewire-serial"] = stream.pipeWireSerial;
    }
    if (!stream.mappingId.empty()) {
      properties->values["mapping_id"] = stream.mappingId;
    }
    array->values.push_back(structValue("ua{sv}", {stream.nodeId, properties}));
  }
  return array;
}

std::vector<PortalScreenCastStream> configuredStreams(dbus::VariantDictionary const& selectOptions) {
  auto nodeId = environmentUint32("LAMBDA_PORTAL_SCREENCAST_NODE_ID");
  if (!nodeId || *nodeId == 0) {
    return {};
  }

  PortalScreenCastStream stream;
  stream.nodeId = *nodeId;
  stream.pipeWireSerial =
      environmentUint64("LAMBDA_PORTAL_SCREENCAST_PIPEWIRE_SERIAL").value_or(0);
  stream.sourceType = environmentUint32("LAMBDA_PORTAL_SCREENCAST_SOURCE_TYPE")
                          .value_or(uintOption(selectOptions, "types").value_or(availableSourceTypes));
  stream.x = environmentInt32("LAMBDA_PORTAL_SCREENCAST_X").value_or(0);
  stream.y = environmentInt32("LAMBDA_PORTAL_SCREENCAST_Y").value_or(0);
  stream.width = environmentInt32("LAMBDA_PORTAL_SCREENCAST_WIDTH").value_or(0);
  stream.height = environmentInt32("LAMBDA_PORTAL_SCREENCAST_HEIGHT").value_or(0);
  stream.mappingId = environmentValue("LAMBDA_PORTAL_SCREENCAST_MAPPING_ID").value_or(std::string());
  return {std::move(stream)};
}

std::shared_ptr<dbus::VariantDictionary>
startResults(dbus::VariantDictionary const& selectOptions,
             dbus::VariantDictionary const& startOptions,
             std::vector<PortalScreenCastStream> const& streams) {
  auto results = std::make_shared<dbus::VariantDictionary>();
  results->values["streams"] = streamsValue(streams);
  if (auto persistMode = uintOption(startOptions, "persist_mode")) {
    results->values["persist_mode"] = *persistMode;
  } else if (auto persistMode = uintOption(selectOptions, "persist_mode")) {
    results->values["persist_mode"] = *persistMode;
  }
  return results;
}

} // namespace

PortalScreenCastService::PortalScreenCastService(dbus::Bus& bus) : bus_(&bus) {}

dbus::ObjectDefinition PortalScreenCastService::objectDefinition() {
  return dbus::ObjectDefinition{
      .methods = {
          dbus::ExportedMethod{
              .interface = interfaceName,
              .member = "CreateSession",
              .handler = [this](dbus::Message& message) {
                return createSession(message);
              },
          },
          dbus::ExportedMethod{
              .interface = interfaceName,
              .member = "SelectSources",
              .handler = [this](dbus::Message& message) {
                return selectSources(message);
              },
          },
          dbus::ExportedMethod{
              .interface = interfaceName,
              .member = "Start",
              .handler = [this](dbus::Message& message) {
                return start(message);
              },
          },
      },
      .properties = {
          dbus::ExportedProperty{
              .interface = interfaceName,
              .name = "AvailableSourceTypes",
              .value = availableSourceTypes,
              .writable = false,
              .getter = nullptr,
              .setter = nullptr,
          },
          dbus::ExportedProperty{
              .interface = interfaceName,
              .name = "AvailableCursorModes",
              .value = availableCursorModes,
              .writable = false,
              .getter = nullptr,
              .setter = nullptr,
          },
          dbus::ExportedProperty{
              .interface = interfaceName,
              .name = "version",
              .value = screenCastVersion,
              .writable = false,
              .getter = nullptr,
              .setter = nullptr,
          },
      },
  };
}

dbus::Slot PortalScreenCastService::exportObject() {
  return bus_->exportObject(objectPath, objectDefinition());
}

std::optional<PortalScreenCastRequest>
PortalScreenCastService::request(std::string const& handle) const {
  if (auto it = requests_.find(handle); it != requests_.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::optional<PortalScreenCastSession>
PortalScreenCastService::session(std::string const& handle) const {
  if (auto it = sessions_.find(handle); it != sessions_.end()) {
    return it->second;
  }
  return std::nullopt;
}

dbus::MethodReply PortalScreenCastService::createSession(dbus::Message& message) {
  auto handle = message.readObjectPath();
  auto sessionHandle = message.readObjectPath();
  auto appId = message.readString();
  auto options = message.readVariantDictionary();

  exportRequestObject(handle.value);
  exportSessionObject(sessionHandle.value);

  PortalScreenCastSession session{
      .handle = sessionHandle,
      .sessionId = sessionIdFromHandle(sessionHandle),
      .appId = appId,
      .createOptions = options,
      .selectOptions = {},
      .streams = {},
      .sourcesSelected = false,
      .started = false,
      .closed = false,
  };
  sessions_[sessionHandle.value] = session;

  requests_[handle.value] = PortalScreenCastRequest{
      .handle = handle,
      .sessionHandle = sessionHandle,
      .appId = appId,
      .parentWindow = {},
      .kind = PortalScreenCastRequestKind::CreateSession,
      .options = std::move(options),
      .response = responseSuccess,
      .closed = false,
  };

  auto results = std::make_shared<dbus::VariantDictionary>();
  results->values["session_id"] = session.sessionId;
  return methodReply({responseSuccess, results});
}

dbus::MethodReply PortalScreenCastService::selectSources(dbus::Message& message) {
  auto handle = message.readObjectPath();
  auto sessionHandle = message.readObjectPath();
  auto appId = message.readString();
  auto options = message.readVariantDictionary();

  exportRequestObject(handle.value);

  auto response = responseOther;
  if (auto session = sessions_.find(sessionHandle.value);
      session != sessions_.end() && !session->second.closed) {
    session->second.appId = appId;
    session->second.selectOptions = options;
    session->second.sourcesSelected = true;
    response = responseSuccess;
  }

  requests_[handle.value] = PortalScreenCastRequest{
      .handle = handle,
      .sessionHandle = sessionHandle,
      .appId = std::move(appId),
      .parentWindow = {},
      .kind = PortalScreenCastRequestKind::SelectSources,
      .options = std::move(options),
      .response = response,
      .closed = false,
  };
  return methodReply({response, emptyResults()});
}

dbus::MethodReply PortalScreenCastService::start(dbus::Message& message) {
  auto handle = message.readObjectPath();
  auto sessionHandle = message.readObjectPath();
  auto appId = message.readString();
  auto parentWindow = message.readString();
  auto options = message.readVariantDictionary();

  exportRequestObject(handle.value);

  auto response = responseOther;
  auto results = emptyResults();
  auto session = sessions_.find(sessionHandle.value);
  if (session != sessions_.end() && !session->second.closed) {
    auto streams = configuredStreams(session->second.selectOptions);
    if (!streams.empty()) {
      response = responseSuccess;
      session->second.appId = appId;
      session->second.started = true;
      session->second.streams = std::move(streams);
      results = startResults(session->second.selectOptions, options, session->second.streams);
    }
  }

  requests_[handle.value] = PortalScreenCastRequest{
      .handle = handle,
      .sessionHandle = sessionHandle,
      .appId = std::move(appId),
      .parentWindow = std::move(parentWindow),
      .kind = PortalScreenCastRequestKind::Start,
      .options = std::move(options),
      .response = response,
      .closed = false,
  };
  return methodReply({response, results});
}

dbus::MethodReply PortalScreenCastService::closeRequest(std::string const& handle) {
  if (auto it = requests_.find(handle); it != requests_.end()) {
    it->second.closed = true;
  }
  return dbus::MethodReply{};
}

dbus::MethodReply PortalScreenCastService::closeSession(std::string const& handle) {
  if (auto it = sessions_.find(handle); it != sessions_.end()) {
    it->second.closed = true;
  }
  return dbus::MethodReply{};
}

void PortalScreenCastService::exportRequestObject(std::string const& handle) {
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

void PortalScreenCastService::exportSessionObject(std::string const& handle) {
  if (sessionSlots_.find(handle) != sessionSlots_.end()) {
    return;
  }
  auto slot = bus_->exportObject(
      handle,
      dbus::ObjectDefinition{
          .methods = {
              dbus::ExportedMethod{
                  .interface = sessionInterfaceName,
                  .member = "Close",
                  .handler = [this, handle](dbus::Message&) {
                    return closeSession(handle);
                  },
              },
          },
          .properties = {
              dbus::ExportedProperty{
                  .interface = sessionInterfaceName,
                  .name = "version",
                  .value = sessionVersion,
                  .writable = false,
                  .getter = nullptr,
                  .setter = nullptr,
              },
          },
      });
  sessionSlots_.emplace(handle, std::move(slot));
}

} // namespace lambda::system
