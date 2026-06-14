#pragma once

/// \file Lambda/System/UDisks2.hpp
///
/// Minimal UDisks2 client used by future Files removable-volume providers.

#include <Lambda/System/DBus.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace lambda::system {

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
  std::uint64_t sizeBytes = 0;
  bool readOnly = false;
  bool hintSystem = false;
  bool hintIgnore = false;
  bool hintAuto = false;
  bool hasFilesystem = false;
  std::vector<std::string> mountPoints;
  UDisks2DriveSnapshot drive;

  [[nodiscard]] bool mounted() const noexcept { return !mountPoints.empty(); }
  [[nodiscard]] bool userVisible() const noexcept {
    return hasFilesystem && !hintSystem && !hintIgnore;
  }

  bool operator==(UDisks2VolumeSnapshot const&) const = default;
};

struct UDisks2Snapshot {
  std::vector<UDisks2DriveSnapshot> drives;
  std::vector<UDisks2VolumeSnapshot> volumes;

  bool operator==(UDisks2Snapshot const&) const = default;
};

class UDisks2Client {
public:
  static constexpr char const* serviceName = "org.freedesktop.UDisks2";
  static constexpr char const* objectManagerPath = "/org/freedesktop/UDisks2";
  static constexpr char const* objectManagerInterfaceName = "org.freedesktop.DBus.ObjectManager";
  static constexpr char const* blockInterfaceName = "org.freedesktop.UDisks2.Block";
  static constexpr char const* filesystemInterfaceName = "org.freedesktop.UDisks2.Filesystem";
  static constexpr char const* driveInterfaceName = "org.freedesktop.UDisks2.Drive";

  explicit UDisks2Client(dbus::Bus bus);

  [[nodiscard]] static UDisks2Client connectSystem();

  [[nodiscard]] dbus::Bus& bus() noexcept { return bus_; }
  [[nodiscard]] dbus::Bus const& bus() const noexcept { return bus_; }

  [[nodiscard]] UDisks2Snapshot readSnapshot();
  [[nodiscard]] std::string mountFilesystem(std::string const& filesystemPath);
  void unmountFilesystem(std::string const& filesystemPath);
  void ejectDrive(std::string const& drivePath);

private:
  [[nodiscard]] dbus::ManagedObjectDictionary managedObjects();

  dbus::Bus bus_;
};

[[nodiscard]] std::string formatUDisks2VolumeName(UDisks2VolumeSnapshot const& volume);

} // namespace lambda::system
