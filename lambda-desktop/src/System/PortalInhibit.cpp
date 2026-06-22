#include <Lambda/System/PortalInhibit.hpp>

#include <string>
#include <utility>
#include <vector>

namespace lambda::system {

namespace {

constexpr std::uint32_t responseSuccess = 0;

dbus::MethodReply methodReply(std::vector<dbus::ReplyValue> values) {
  dbus::MethodReply reply;
  reply.values = std::move(values);
  return reply;
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

} // namespace

PortalInhibitService::PortalInhibitService(dbus::Bus& bus) : bus_(&bus) {}

dbus::ObjectDefinition PortalInhibitService::objectDefinition() {
  return dbus::ObjectDefinition{
      .methods = {
          dbus::ExportedMethod{
              .interface = interfaceName,
              .member = "Inhibit",
              .handler = [this](dbus::Message& message) {
                return inhibit(message);
              },
          },
          dbus::ExportedMethod{
              .interface = interfaceName,
              .member = "CreateMonitor",
              .handler = [this](dbus::Message& message) {
                return createMonitor(message);
              },
          },
          dbus::ExportedMethod{
              .interface = interfaceName,
              .member = "QueryEndResponse",
              .handler = [this](dbus::Message& message) {
                return queryEndResponse(message);
              },
          },
      },
      .properties = {},
  };
}

dbus::Slot PortalInhibitService::exportObject() {
  return bus_->exportObject(objectPath, objectDefinition());
}

std::optional<PortalInhibitRequest> PortalInhibitService::request(std::string const& handle) const {
  if (auto it = requests_.find(handle); it != requests_.end()) {
    return it->second;
  }
  return std::nullopt;
}

std::optional<PortalInhibitMonitor> PortalInhibitService::monitor(std::string const& sessionHandle) const {
  if (auto it = monitors_.find(sessionHandle); it != monitors_.end()) {
    return it->second;
  }
  return std::nullopt;
}

dbus::MethodReply PortalInhibitService::inhibit(dbus::Message& message) {
  auto handle = message.readObjectPath();
  PortalInhibitRequest request{
      .handle = handle,
      .appId = message.readString(),
      .window = message.readString(),
      .flags = message.readUint32(),
      .reason = stringValue(message.readVariantDictionary(), "reason"),
  };

  exportRequestObject(handle.value);
  requests_[handle.value] = std::move(request);
  return dbus::MethodReply{};
}

dbus::MethodReply PortalInhibitService::createMonitor(dbus::Message& message) {
  auto handle = message.readObjectPath();
  PortalInhibitMonitor monitor{
      .handle = handle,
      .sessionHandle = message.readObjectPath(),
      .appId = message.readString(),
      .window = message.readString(),
  };

  exportRequestObject(handle.value);
  monitors_[monitor.sessionHandle.value] = std::move(monitor);
  return methodReply({responseSuccess});
}

dbus::MethodReply PortalInhibitService::queryEndResponse(dbus::Message& message) {
  auto sessionHandle = message.readObjectPath();
  if (auto it = monitors_.find(sessionHandle.value); it != monitors_.end()) {
    it->second.queryEndAcknowledged = true;
  }
  return dbus::MethodReply{};
}

dbus::MethodReply PortalInhibitService::closeRequest(std::string const& handle) {
  if (auto it = requests_.find(handle); it != requests_.end()) {
    it->second.closed = true;
  }
  return dbus::MethodReply{};
}

void PortalInhibitService::exportRequestObject(std::string const& handle) {
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

} // namespace lambda::system
