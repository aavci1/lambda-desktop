#pragma once

/// \file Lambda/System/UDisks2.hpp
///
/// Minimal UDisks2 client used by future Files removable-volume providers.

#include <Lambda/System/DBus.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace lambda::system {

struct UDisks2JobSnapshot {
  std::string path;
  std::string operation;
  double progress = 0.0;
  bool progressValid = false;
  bool cancelable = false;
  std::uint64_t bytes = 0;
  std::uint64_t rate = 0;
  std::uint64_t startTime = 0;
  std::uint64_t expectedEndTime = 0;
  std::uint32_t startedByUid = 0;
  std::vector<std::string> objectPaths;

  bool operator==(UDisks2JobSnapshot const&) const = default;
};

struct UDisks2DriveSnapshot {
  std::string path;
  std::string vendor;
  std::string model;
  std::string serial;
  std::string connectionBus;
  bool removable = false;
  bool mediaRemovable = false;
  bool ejectable = false;
  std::uint64_t sizeBytes = 0;

  bool operator==(UDisks2DriveSnapshot const&) const = default;
};

struct UDisks2VolumeSnapshot {
  std::string path;
  std::string device;
  std::string preferredDevice;
  std::string drivePath;
  std::string label;
  std::string uuid;
  std::string filesystemType;
  std::string filesystemUsage;
  std::string cryptoBackingDevice;
  std::string cleartextDevice;
  std::string encryptionType;
  std::uint64_t encryptionMetadataSize = 0;
  std::uint64_t sizeBytes = 0;
  bool readOnly = false;
  bool hintSystem = false;
  bool hintIgnore = false;
  bool hintAuto = false;
  bool hasFilesystem = false;
  bool encrypted = false;
  bool unlocked = false;
  bool cleartext = false;
  std::vector<std::string> mountPoints;
  std::vector<std::string> userspaceMountOptions;
  std::vector<UDisks2JobSnapshot> jobs;
  UDisks2DriveSnapshot drive;

  [[nodiscard]] bool mounted() const noexcept { return !mountPoints.empty(); }
  [[nodiscard]] bool userVisible() const noexcept {
    return (hasFilesystem || encrypted) && !hintSystem && !hintIgnore;
  }

  bool operator==(UDisks2VolumeSnapshot const&) const = default;
};

struct UDisks2Snapshot {
  std::vector<UDisks2DriveSnapshot> drives;
  std::vector<UDisks2VolumeSnapshot> volumes;
  std::vector<UDisks2JobSnapshot> jobs;

  bool operator==(UDisks2Snapshot const&) const = default;
};

struct UDisks2StatusWatch {
  dbus::Slot propertiesChanged;
  dbus::Slot interfacesAdded;
  dbus::Slot interfacesRemoved;
};

struct UDisks2MountOptions {
  std::string filesystemType;
  std::string mountOptions;
  std::string asUser;
  bool force = false;
  bool readOnly = false;
};

struct UDisks2OperationResult {
  bool ok = false;
  std::string value;
  std::string errorName;
  std::string errorMessage;
  std::string userMessage;
  bool deviceBusy = false;
  bool cancelled = false;
  bool notAuthorized = false;
  bool retryable = false;
  bool canForce = false;

  bool operator==(UDisks2OperationResult const&) const = default;
};

class UDisks2Client {
public:
  static constexpr char const* serviceName = "org.freedesktop.UDisks2";
  static constexpr char const* objectManagerPath = "/org/freedesktop/UDisks2";
  static constexpr char const* objectManagerInterfaceName = "org.freedesktop.DBus.ObjectManager";
  static constexpr char const* blockInterfaceName = "org.freedesktop.UDisks2.Block";
  static constexpr char const* filesystemInterfaceName = "org.freedesktop.UDisks2.Filesystem";
  static constexpr char const* driveInterfaceName = "org.freedesktop.UDisks2.Drive";
  static constexpr char const* encryptedInterfaceName = "org.freedesktop.UDisks2.Encrypted";
  static constexpr char const* jobInterfaceName = "org.freedesktop.UDisks2.Job";

  explicit UDisks2Client(dbus::Bus bus);

  [[nodiscard]] static UDisks2Client connectSystem();

  [[nodiscard]] dbus::Bus& bus() noexcept { return bus_; }
  [[nodiscard]] dbus::Bus const& bus() const noexcept { return bus_; }

  [[nodiscard]] UDisks2Snapshot readSnapshot();
  [[nodiscard]] UDisks2StatusWatch watchStatusChanges(std::function<void()> handler);
  [[nodiscard]] std::string mountFilesystem(std::string const& filesystemPath);
  [[nodiscard]] std::string mountFilesystem(std::string const& filesystemPath,
                                            UDisks2MountOptions const& options);
  [[nodiscard]] UDisks2OperationResult tryMountFilesystem(std::string const& filesystemPath,
                                                          UDisks2MountOptions const& options = {});
  void unmountFilesystem(std::string const& filesystemPath);
  void unmountFilesystem(std::string const& filesystemPath, UDisks2MountOptions const& options);
  [[nodiscard]] UDisks2OperationResult tryUnmountFilesystem(std::string const& filesystemPath,
                                                            UDisks2MountOptions const& options = {});
  void ejectDrive(std::string const& drivePath);
  [[nodiscard]] UDisks2OperationResult tryEjectDrive(std::string const& drivePath);
  [[nodiscard]] std::string unlockEncrypted(std::string const& encryptedPath,
                                            std::string const& passphrase,
                                            UDisks2MountOptions const& options = {});
  [[nodiscard]] UDisks2OperationResult tryUnlockEncrypted(std::string const& encryptedPath,
                                                          std::string const& passphrase,
                                                          UDisks2MountOptions const& options = {});
  void lockEncrypted(std::string const& encryptedPath);
  [[nodiscard]] UDisks2OperationResult tryLockEncrypted(std::string const& encryptedPath);
  void cancelJob(std::string const& jobPath);
  [[nodiscard]] UDisks2OperationResult tryCancelJob(std::string const& jobPath);

private:
  [[nodiscard]] dbus::ManagedObjectDictionary managedObjects();

  dbus::Bus bus_;
};

[[nodiscard]] std::string formatUDisks2VolumeName(UDisks2VolumeSnapshot const& volume);

} // namespace lambda::system
