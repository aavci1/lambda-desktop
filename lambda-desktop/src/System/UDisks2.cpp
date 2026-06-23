#include <Lambda/System/UDisks2.hpp>

#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <utility>

namespace lambdaui::system {

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

std::uint32_t uint32Property(InterfaceProperties const& properties, std::string const& name) {
  auto const it = properties.find(name);
  if (it == properties.end()) {
    return 0;
  }
  if (auto value = std::get_if<std::uint32_t>(&it->second)) {
    return *value;
  }
  return 0;
}

double doubleProperty(InterfaceProperties const& properties, std::string const& name) {
  auto const it = properties.find(name);
  if (it == properties.end()) {
    return 0.0;
  }
  if (auto value = std::get_if<double>(&it->second)) {
    return *value;
  }
  return 0.0;
}

std::vector<std::string> objectPathArrayProperty(InterfaceProperties const& properties,
                                                 std::string const& name) {
  auto const it = properties.find(name);
  if (it == properties.end()) {
    return {};
  }
  if (auto value = std::get_if<dbus::ObjectPathArray>(&it->second)) {
    std::vector<std::string> paths;
    paths.reserve(value->values.size());
    for (auto const& path : value->values) {
      if (!path.value.empty()) {
        paths.push_back(path.value);
      }
    }
    return paths;
  }
  return {};
}

std::vector<std::string> stringArrayProperty(InterfaceProperties const& properties,
                                             std::string const& name) {
  auto const it = properties.find(name);
  if (it == properties.end()) {
    return {};
  }
  if (auto value = std::get_if<dbus::StringArray>(&it->second)) {
    return value->values;
  }
  return {};
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

UDisks2JobSnapshot jobSnapshot(std::string const& path, InterfaceProperties const& properties) {
  return UDisks2JobSnapshot{
      .path = path,
      .operation = stringProperty(properties, "Operation"),
      .progress = doubleProperty(properties, "Progress"),
      .progressValid = boolProperty(properties, "ProgressValid"),
      .cancelable = boolProperty(properties, "Cancelable"),
      .bytes = uint64Property(properties, "Bytes"),
      .rate = uint64Property(properties, "Rate"),
      .startTime = uint64Property(properties, "StartTime"),
      .expectedEndTime = uint64Property(properties, "ExpectedEndTime"),
      .startedByUid = uint32Property(properties, "StartedByUID"),
      .objectPaths = objectPathArrayProperty(properties, "Objects"),
  };
}

UDisks2VolumeSnapshot volumeSnapshot(std::string const& path,
                                     InterfaceProperties const& block,
                                     InterfaceProperties const* filesystem,
                                     InterfaceProperties const* encrypted) {
  UDisks2VolumeSnapshot volume;
  volume.path = path;
  volume.device = byteStringProperty(block, "Device");
  volume.preferredDevice = byteStringProperty(block, "PreferredDevice");
  volume.drivePath = objectPathProperty(block, "Drive");
  volume.label = stringProperty(block, "IdLabel");
  volume.uuid = stringProperty(block, "IdUUID");
  volume.filesystemType = stringProperty(block, "IdType");
  volume.filesystemUsage = stringProperty(block, "IdUsage");
  volume.cryptoBackingDevice = objectPathProperty(block, "CryptoBackingDevice");
  volume.sizeBytes = uint64Property(block, "Size");
  volume.readOnly = boolProperty(block, "ReadOnly");
  volume.hintSystem = boolProperty(block, "HintSystem");
  volume.hintIgnore = boolProperty(block, "HintIgnore");
  volume.hintAuto = boolProperty(block, "HintAuto");
  volume.userspaceMountOptions = stringArrayProperty(block, "UserspaceMountOptions");
  volume.hasFilesystem = filesystem != nullptr;
  volume.encrypted = encrypted != nullptr || volume.filesystemUsage == "crypto";
  volume.cleartext = !volume.cryptoBackingDevice.empty() && volume.cryptoBackingDevice != "/";
  if (filesystem) {
    volume.mountPoints = byteStringArrayProperty(*filesystem, "MountPoints");
  }
  if (encrypted) {
    volume.cleartextDevice = objectPathProperty(*encrypted, "CleartextDevice");
    volume.encryptionType = stringProperty(*encrypted, "HintEncryptionType");
    volume.encryptionMetadataSize = uint64Property(*encrypted, "MetadataSize");
    volume.unlocked = !volume.cleartextDevice.empty() && volume.cleartextDevice != "/";
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

bool jobAffectsVolume(UDisks2JobSnapshot const& job, UDisks2VolumeSnapshot const& volume) {
  return std::any_of(job.objectPaths.begin(), job.objectPaths.end(), [&](std::string const& path) {
    return path == volume.path || path == volume.drivePath ||
           (!volume.cleartextDevice.empty() && path == volume.cleartextDevice) ||
           (!volume.cryptoBackingDevice.empty() && path == volume.cryptoBackingDevice);
  });
}

std::shared_ptr<dbus::VariantDictionary> operationOptions(UDisks2MountOptions const& options,
                                                          bool forMount,
                                                          bool forUnmount,
                                                          bool forUnlock) {
  auto values = std::make_shared<dbus::VariantDictionary>();
  if (forMount) {
    if (!options.filesystemType.empty()) {
      values->values["fstype"] = options.filesystemType;
    }
    if (!options.mountOptions.empty()) {
      values->values["options"] = options.mountOptions;
    }
    if (!options.asUser.empty()) {
      values->values["as-user"] = options.asUser;
    }
  }
  if (forUnmount && options.force) {
    values->values["force"] = true;
  }
  if (forUnlock && options.readOnly) {
    values->values["read-only"] = true;
  }
  return values;
}

dbus::BasicValue optionsArgument(std::shared_ptr<dbus::VariantDictionary> const& options) {
  if (!options || options->values.empty()) {
    return dbus::EmptyVariantDictionary{};
  }
  return options;
}

UDisks2OperationResult success(std::string value = {}) {
  UDisks2OperationResult result;
  result.ok = true;
  result.value = std::move(value);
  return result;
}

UDisks2OperationResult failure(dbus::Error const& error) {
  UDisks2OperationResult result;
  result.errorName = error.name();
  result.errorMessage = error.what();
  result.deviceBusy = result.errorName.find("DeviceBusy") != std::string::npos;
  result.cancelled = result.errorName.find("Cancelled") != std::string::npos;
  result.notAuthorized = result.errorName.find("NotAuthorized") != std::string::npos;
  result.canForce = result.deviceBusy;
  result.retryable = result.deviceBusy || result.cancelled || result.notAuthorized;
  if (result.deviceBusy) {
    result.userMessage = "The volume is busy. Close files using it, then retry or force unmount.";
  } else if (result.cancelled) {
    result.userMessage = "The operation was cancelled.";
  } else if (result.notAuthorized) {
    result.userMessage = "Authorization is required to complete this storage operation.";
  } else {
    result.userMessage = result.errorMessage.empty() ? "The storage operation failed."
                                                     : result.errorMessage;
  }
  return result;
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
    if (auto job = interfaces.find(jobInterfaceName); job != interfaces.end()) {
      snapshot.jobs.push_back(jobSnapshot(path, job->second));
    }
  }

  for (auto const& [path, interfaces] : objects.values) {
    auto block = interfaces.find(blockInterfaceName);
    if (block == interfaces.end()) {
      continue;
    }
    auto filesystem = interfaces.find(filesystemInterfaceName);
    auto encrypted = interfaces.find(encryptedInterfaceName);
    auto volume = volumeSnapshot(path,
                                 block->second,
                                 filesystem == interfaces.end() ? nullptr : &filesystem->second,
                                 encrypted == interfaces.end() ? nullptr : &encrypted->second);
    if (auto drive = drivesByPath.find(volume.drivePath); drive != drivesByPath.end()) {
      volume.drive = drive->second;
    }
    for (auto const& job : snapshot.jobs) {
      if (jobAffectsVolume(job, volume)) {
        volume.jobs.push_back(job);
      }
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
                    changed.interface != UDisks2Client::driveInterfaceName &&
                    changed.interface != UDisks2Client::encryptedInterfaceName &&
                    changed.interface != UDisks2Client::jobInterfaceName) {
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
  return mountFilesystem(filesystemPath, {});
}

std::string UDisks2Client::mountFilesystem(std::string const& filesystemPath,
                                           UDisks2MountOptions const& options) {
  dbus::Message reply = bus_.call(dbus::MethodCall{
      .destination = serviceName,
      .path = filesystemPath,
      .interface = filesystemInterfaceName,
      .member = "Mount",
      .arguments = {optionsArgument(operationOptions(options, true, false, false))},
  });
  return reply.readString();
}

UDisks2OperationResult UDisks2Client::tryMountFilesystem(std::string const& filesystemPath,
                                                         UDisks2MountOptions const& options) {
  try {
    return success(mountFilesystem(filesystemPath, options));
  } catch (dbus::Error const& error) {
    return failure(error);
  }
}

void UDisks2Client::unmountFilesystem(std::string const& filesystemPath) {
  unmountFilesystem(filesystemPath, {});
}

void UDisks2Client::unmountFilesystem(std::string const& filesystemPath,
                                      UDisks2MountOptions const& options) {
  (void)bus_.call(dbus::MethodCall{
      .destination = serviceName,
      .path = filesystemPath,
      .interface = filesystemInterfaceName,
      .member = "Unmount",
      .arguments = {optionsArgument(operationOptions(options, false, true, false))},
  });
}

UDisks2OperationResult UDisks2Client::tryUnmountFilesystem(std::string const& filesystemPath,
                                                           UDisks2MountOptions const& options) {
  try {
    unmountFilesystem(filesystemPath, options);
    return success();
  } catch (dbus::Error const& error) {
    return failure(error);
  }
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

UDisks2OperationResult UDisks2Client::tryEjectDrive(std::string const& drivePath) {
  try {
    ejectDrive(drivePath);
    return success();
  } catch (dbus::Error const& error) {
    return failure(error);
  }
}

std::string UDisks2Client::unlockEncrypted(std::string const& encryptedPath,
                                           std::string const& passphrase,
                                           UDisks2MountOptions const& options) {
  dbus::Message reply = bus_.call(dbus::MethodCall{
      .destination = serviceName,
      .path = encryptedPath,
      .interface = encryptedInterfaceName,
      .member = "Unlock",
      .arguments = {passphrase, optionsArgument(operationOptions(options, false, false, true))},
  });
  return reply.readObjectPath().value;
}

UDisks2OperationResult UDisks2Client::tryUnlockEncrypted(std::string const& encryptedPath,
                                                         std::string const& passphrase,
                                                         UDisks2MountOptions const& options) {
  try {
    return success(unlockEncrypted(encryptedPath, passphrase, options));
  } catch (dbus::Error const& error) {
    return failure(error);
  }
}

void UDisks2Client::lockEncrypted(std::string const& encryptedPath) {
  (void)bus_.call(dbus::MethodCall{
      .destination = serviceName,
      .path = encryptedPath,
      .interface = encryptedInterfaceName,
      .member = "Lock",
      .arguments = {dbus::EmptyVariantDictionary{}},
  });
}

UDisks2OperationResult UDisks2Client::tryLockEncrypted(std::string const& encryptedPath) {
  try {
    lockEncrypted(encryptedPath);
    return success();
  } catch (dbus::Error const& error) {
    return failure(error);
  }
}

void UDisks2Client::cancelJob(std::string const& jobPath) {
  (void)bus_.call(dbus::MethodCall{
      .destination = serviceName,
      .path = jobPath,
      .interface = jobInterfaceName,
      .member = "Cancel",
      .arguments = {dbus::EmptyVariantDictionary{}},
  });
}

UDisks2OperationResult UDisks2Client::tryCancelJob(std::string const& jobPath) {
  try {
    cancelJob(jobPath);
    return success();
  } catch (dbus::Error const& error) {
    return failure(error);
  }
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

} // namespace lambdaui::system
