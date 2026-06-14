#include <Lambda/System/UDisks2.hpp>

#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <utility>

namespace lambda::system {

namespace {

using InterfaceProperties = std::map<std::string, dbus::BasicValue>;

constexpr char kPropertiesInterface[] = "org.freedesktop.DBus.Properties";

void notify(std::shared_ptr<std::function<void()>> const& handler) {
  if (handler && *handler) {
    (*handler)();
  }
}

std::string bytesToString(dbus::ByteArray const& bytes) {
  std::string value;
  value.reserve(bytes.values.size());
  for (auto const byte : bytes.values) {
    if (byte == 0) {
      break;
    }
    value.push_back(static_cast<char>(byte));
  }
  return value;
}

std::string stringProperty(InterfaceProperties const& properties, std::string const& name) {
  auto const it = properties.find(name);
  if (it == properties.end()) {
    return {};
  }
  if (auto value = std::get_if<std::string>(&it->second)) {
    return *value;
  }
  return {};
}

std::string byteStringProperty(InterfaceProperties const& properties, std::string const& name) {
  auto const it = properties.find(name);
  if (it == properties.end()) {
    return {};
  }
  if (auto value = std::get_if<dbus::ByteArray>(&it->second)) {
    return bytesToString(*value);
  }
  return {};
}

std::string objectPathProperty(InterfaceProperties const& properties, std::string const& name) {
  auto const it = properties.find(name);
  if (it == properties.end()) {
    return {};
  }
  if (auto value = std::get_if<dbus::ObjectPath>(&it->second)) {
    return value->value;
  }
  return {};
}

bool boolProperty(InterfaceProperties const& properties, std::string const& name) {
  auto const it = properties.find(name);
  if (it == properties.end()) {
    return false;
  }
  if (auto value = std::get_if<bool>(&it->second)) {
    return *value;
  }
  return false;
}

std::uint64_t uint64Property(InterfaceProperties const& properties, std::string const& name) {
  auto const it = properties.find(name);
  if (it == properties.end()) {
    return 0;
  }
  if (auto value = std::get_if<std::uint64_t>(&it->second)) {
    return *value;
  }
  return 0;
}

std::vector<std::string> byteStringArrayProperty(InterfaceProperties const& properties,
                                                 std::string const& name) {
  auto const it = properties.find(name);
  if (it == properties.end()) {
    return {};
  }
  if (auto value = std::get_if<dbus::ByteArrayArray>(&it->second)) {
    std::vector<std::string> paths;
    paths.reserve(value->values.size());
    for (auto const& bytes : value->values) {
      std::string text = bytesToString(bytes);
      if (!text.empty()) {
        paths.push_back(std::move(text));
      }
    }
    return paths;
  }
  return {};
}

UDisks2DriveSnapshot driveSnapshot(std::string const& path, InterfaceProperties const& properties) {
  return UDisks2DriveSnapshot{
      .path = path,
      .vendor = stringProperty(properties, "Vendor"),
      .model = stringProperty(properties, "Model"),
      .serial = stringProperty(properties, "Serial"),
      .connectionBus = stringProperty(properties, "ConnectionBus"),
      .removable = boolProperty(properties, "Removable"),
      .mediaRemovable = boolProperty(properties, "MediaRemovable"),
      .ejectable = boolProperty(properties, "Ejectable"),
      .sizeBytes = uint64Property(properties, "Size"),
  };
}

UDisks2VolumeSnapshot volumeSnapshot(std::string const& path,
                                     InterfaceProperties const& block,
                                     InterfaceProperties const* filesystem) {
  UDisks2VolumeSnapshot volume;
  volume.path = path;
  volume.device = byteStringProperty(block, "Device");
  volume.preferredDevice = byteStringProperty(block, "PreferredDevice");
  volume.drivePath = objectPathProperty(block, "Drive");
  volume.label = stringProperty(block, "IdLabel");
  volume.uuid = stringProperty(block, "IdUUID");
  volume.filesystemType = stringProperty(block, "IdType");
  volume.filesystemUsage = stringProperty(block, "IdUsage");
  volume.sizeBytes = uint64Property(block, "Size");
  volume.readOnly = boolProperty(block, "ReadOnly");
  volume.hintSystem = boolProperty(block, "HintSystem");
  volume.hintIgnore = boolProperty(block, "HintIgnore");
  volume.hintAuto = boolProperty(block, "HintAuto");
  volume.hasFilesystem = filesystem != nullptr;
  if (filesystem) {
    volume.mountPoints = byteStringArrayProperty(*filesystem, "MountPoints");
  }
  return volume;
}

std::string basename(std::string const& path) {
  if (path.empty()) {
    return {};
  }
  std::filesystem::path fsPath(path);
  return fsPath.filename().string();
}

} // namespace

UDisks2Client::UDisks2Client(dbus::Bus bus) : bus_(std::move(bus)) {}

UDisks2Client UDisks2Client::connectSystem() {
  return UDisks2Client(dbus::Bus::open(dbus::BusType::System));
}

UDisks2Snapshot UDisks2Client::readSnapshot() {
  UDisks2Snapshot snapshot;
  auto objects = managedObjects();
  std::map<std::string, UDisks2DriveSnapshot> drivesByPath;

  for (auto const& [path, interfaces] : objects.values) {
    if (auto drive = interfaces.find(driveInterfaceName); drive != interfaces.end()) {
      auto driveInfo = driveSnapshot(path, drive->second);
      drivesByPath[path] = driveInfo;
      snapshot.drives.push_back(std::move(driveInfo));
    }
  }

  for (auto const& [path, interfaces] : objects.values) {
    auto block = interfaces.find(blockInterfaceName);
    if (block == interfaces.end()) {
      continue;
    }
    auto filesystem = interfaces.find(filesystemInterfaceName);
    auto volume = volumeSnapshot(path,
                                 block->second,
                                 filesystem == interfaces.end() ? nullptr : &filesystem->second);
    if (auto drive = drivesByPath.find(volume.drivePath); drive != drivesByPath.end()) {
      volume.drive = drive->second;
    }
    if (volume.userVisible()) {
      snapshot.volumes.push_back(std::move(volume));
    }
  }

  std::sort(snapshot.volumes.begin(), snapshot.volumes.end(), [](auto const& lhs, auto const& rhs) {
    return formatUDisks2VolumeName(lhs) < formatUDisks2VolumeName(rhs);
  });
  return snapshot;
}

UDisks2StatusWatch UDisks2Client::watchStatusChanges(std::function<void()> handler) {
  auto sharedHandler = std::make_shared<std::function<void()>>(std::move(handler));
  return UDisks2StatusWatch{
      .propertiesChanged =
          bus_.matchSignal(
              dbus::SignalMatch{
                  .sender = serviceName,
                  .path = {},
                  .interface = kPropertiesInterface,
                  .member = "PropertiesChanged",
              },
              [sharedHandler](dbus::Message& message) {
                auto const changed = dbus::readPropertiesChanged(message);
                if (changed.interface != UDisks2Client::blockInterfaceName &&
                    changed.interface != UDisks2Client::filesystemInterfaceName &&
                    changed.interface != UDisks2Client::driveInterfaceName) {
                  return;
                }
                notify(sharedHandler);
              }),
      .interfacesAdded =
          bus_.matchSignal(
              dbus::SignalMatch{
                  .sender = serviceName,
                  .path = objectManagerPath,
                  .interface = objectManagerInterfaceName,
                  .member = "InterfacesAdded",
              },
              [sharedHandler](dbus::Message&) {
                notify(sharedHandler);
              }),
      .interfacesRemoved =
          bus_.matchSignal(
              dbus::SignalMatch{
                  .sender = serviceName,
                  .path = objectManagerPath,
                  .interface = objectManagerInterfaceName,
                  .member = "InterfacesRemoved",
              },
              [sharedHandler](dbus::Message&) {
                notify(sharedHandler);
              }),
  };
}

std::string UDisks2Client::mountFilesystem(std::string const& filesystemPath) {
  dbus::Message reply = bus_.call(dbus::MethodCall{
      .destination = serviceName,
      .path = filesystemPath,
      .interface = filesystemInterfaceName,
      .member = "Mount",
      .arguments = {dbus::EmptyVariantDictionary{}},
  });
  return reply.readString();
}

void UDisks2Client::unmountFilesystem(std::string const& filesystemPath) {
  (void)bus_.call(dbus::MethodCall{
      .destination = serviceName,
      .path = filesystemPath,
      .interface = filesystemInterfaceName,
      .member = "Unmount",
      .arguments = {dbus::EmptyVariantDictionary{}},
  });
}

void UDisks2Client::ejectDrive(std::string const& drivePath) {
  (void)bus_.call(dbus::MethodCall{
      .destination = serviceName,
      .path = drivePath,
      .interface = driveInterfaceName,
      .member = "Eject",
      .arguments = {dbus::EmptyVariantDictionary{}},
  });
}

dbus::ManagedObjectDictionary UDisks2Client::managedObjects() {
  dbus::Message reply = bus_.call(dbus::MethodCall{
      .destination = serviceName,
      .path = objectManagerPath,
      .interface = objectManagerInterfaceName,
      .member = "GetManagedObjects",
      .arguments = {},
  });
  return reply.readManagedObjectDictionary();
}

std::string formatUDisks2VolumeName(UDisks2VolumeSnapshot const& volume) {
  if (!volume.label.empty()) {
    return volume.label;
  }
  std::string const driveName = volume.drive.vendor.empty()
                                    ? volume.drive.model
                                    : volume.drive.vendor + " " + volume.drive.model;
  if (!driveName.empty()) {
    return driveName;
  }
  if (!volume.preferredDevice.empty()) {
    return basename(volume.preferredDevice);
  }
  if (!volume.device.empty()) {
    return basename(volume.device);
  }
  return volume.path;
}

} // namespace lambda::system
