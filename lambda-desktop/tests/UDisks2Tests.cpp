#include <Lambda/System/UDisks2.hpp>

#include "DBusTestHelpers.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using lambda::testing::dbus::pollBus;
using lambda::testing::dbus::pumpUntil;
using lambda::testing::dbus::startPrivateBus;

constexpr char kDrivePath[] = "/org/freedesktop/UDisks2/drives/Lambda_USB";
constexpr char kVolumePath[] = "/org/freedesktop/UDisks2/block_devices/sdb1";
constexpr char kEncryptedPath[] = "/org/freedesktop/UDisks2/block_devices/sdc1";
constexpr char kCleartextPath[] = "/org/freedesktop/UDisks2/block_devices/dm_2d0";
constexpr char kJobPath[] = "/org/freedesktop/UDisks2/jobs/1";
constexpr char kSystemVolumePath[] = "/org/freedesktop/UDisks2/block_devices/nvme0n1p1";

template <typename Callback>
class ScopeExit {
public:
  explicit ScopeExit(Callback callback) : callback_(std::move(callback)) {}
  ~ScopeExit() { callback_(); }

private:
  Callback callback_;
};

lambda::dbus::ByteArray bytes(std::string const& value) {
  lambda::dbus::ByteArray output;
  output.values.reserve(value.size() + 1);
  for (char ch : value) {
    output.values.push_back(static_cast<std::uint8_t>(ch));
  }
  output.values.push_back(0);
  return output;
}

lambda::dbus::ByteArrayArray mountPoints(std::vector<std::string> const& paths) {
  lambda::dbus::ByteArrayArray output;
  output.values.reserve(paths.size());
  for (auto const& path : paths) {
    output.values.push_back(bytes(path));
  }
  return output;
}

lambda::dbus::ObjectPathArray objectPaths(std::vector<std::string> const& paths) {
  lambda::dbus::ObjectPathArray output;
  output.values.reserve(paths.size());
  for (auto const& path : paths) {
    output.values.push_back(lambda::dbus::ObjectPath{path});
  }
  return output;
}

lambda::dbus::ManagedObjectDictionary managedObjects(bool mounted, bool encryptedUnlocked = false) {
  lambda::dbus::ManagedObjectDictionary objects;

  auto& drive = objects.values[kDrivePath][lambda::system::UDisks2Client::driveInterfaceName];
  drive["Vendor"] = std::string("Lambda");
  drive["Model"] = std::string("USB Stick");
  drive["Serial"] = std::string("abc123");
  drive["ConnectionBus"] = std::string("usb");
  drive["Removable"] = true;
  drive["MediaRemovable"] = true;
  drive["Ejectable"] = true;
  drive["Size"] = std::uint64_t(16ull * 1024ull * 1024ull * 1024ull);

  auto& block = objects.values[kVolumePath][lambda::system::UDisks2Client::blockInterfaceName];
  block["Device"] = bytes("/dev/sdb1");
  block["PreferredDevice"] = bytes("/dev/disk/by-label/LAMBDA_USB");
  block["Drive"] = lambda::dbus::ObjectPath{kDrivePath};
  block["IdUsage"] = std::string("filesystem");
  block["IdType"] = std::string("vfat");
  block["IdLabel"] = std::string("LAMBDA_USB");
  block["IdUUID"] = std::string("1111-2222");
  block["Size"] = std::uint64_t(4ull * 1024ull * 1024ull * 1024ull);
  block["ReadOnly"] = false;
  block["HintSystem"] = false;
  block["HintIgnore"] = false;
  block["HintAuto"] = true;
  block["Symlinks"] = mountPoints({"/dev/disk/by-label/LAMBDA_USB"});
  block["UserspaceMountOptions"] = lambda::dbus::StringArray{.values = {"uhelper=udisks2"}};

  auto& filesystem =
      objects.values[kVolumePath][lambda::system::UDisks2Client::filesystemInterfaceName];
  filesystem["MountPoints"] =
      mounted ? mountPoints({"/run/media/test/LAMBDA_USB"}) : lambda::dbus::ByteArrayArray{};
  filesystem["Size"] = std::uint64_t(4ull * 1024ull * 1024ull * 1024ull);

  auto& encryptedBlock =
      objects.values[kEncryptedPath][lambda::system::UDisks2Client::blockInterfaceName];
  encryptedBlock["Device"] = bytes("/dev/sdc1");
  encryptedBlock["PreferredDevice"] = bytes("/dev/disk/by-label/LOCKED_USB");
  encryptedBlock["Drive"] = lambda::dbus::ObjectPath{kDrivePath};
  encryptedBlock["IdUsage"] = std::string("crypto");
  encryptedBlock["IdType"] = std::string("crypto_LUKS");
  encryptedBlock["IdLabel"] = std::string("LOCKED_USB");
  encryptedBlock["IdUUID"] = std::string("3333-4444");
  encryptedBlock["Size"] = std::uint64_t(8ull * 1024ull * 1024ull * 1024ull);
  encryptedBlock["ReadOnly"] = false;
  encryptedBlock["HintSystem"] = false;
  encryptedBlock["HintIgnore"] = false;
  encryptedBlock["HintAuto"] = true;

  auto& encrypted =
      objects.values[kEncryptedPath][lambda::system::UDisks2Client::encryptedInterfaceName];
  encrypted["CleartextDevice"] =
      lambda::dbus::ObjectPath{encryptedUnlocked ? kCleartextPath : "/"};
  encrypted["HintEncryptionType"] = std::string("luks");
  encrypted["MetadataSize"] = std::uint64_t(16ull * 1024ull * 1024ull);

  if (encryptedUnlocked) {
    auto& cleartext =
        objects.values[kCleartextPath][lambda::system::UDisks2Client::blockInterfaceName];
    cleartext["Device"] = bytes("/dev/dm-0");
    cleartext["PreferredDevice"] = bytes("/dev/mapper/luks-3333-4444");
    cleartext["Drive"] = lambda::dbus::ObjectPath{kDrivePath};
    cleartext["CryptoBackingDevice"] = lambda::dbus::ObjectPath{kEncryptedPath};
    cleartext["IdUsage"] = std::string("filesystem");
    cleartext["IdType"] = std::string("ext4");
    cleartext["IdLabel"] = std::string("UNLOCKED_USB");
    cleartext["IdUUID"] = std::string("5555-6666");
    cleartext["Size"] = std::uint64_t(7ull * 1024ull * 1024ull * 1024ull);
    cleartext["ReadOnly"] = true;
    cleartext["HintSystem"] = false;
    cleartext["HintIgnore"] = false;
    auto& cleartextFilesystem =
        objects.values[kCleartextPath][lambda::system::UDisks2Client::filesystemInterfaceName];
    cleartextFilesystem["MountPoints"] = lambda::dbus::ByteArrayArray{};
    cleartextFilesystem["Size"] = std::uint64_t(7ull * 1024ull * 1024ull * 1024ull);
  }

  auto& job = objects.values[kJobPath][lambda::system::UDisks2Client::jobInterfaceName];
  job["Operation"] = std::string("filesystem-mount");
  job["Progress"] = 0.5;
  job["ProgressValid"] = true;
  job["Cancelable"] = true;
  job["Bytes"] = std::uint64_t(1024);
  job["Rate"] = std::uint64_t(256);
  job["StartTime"] = std::uint64_t(10);
  job["ExpectedEndTime"] = std::uint64_t(20);
  job["StartedByUID"] = std::uint32_t(1000);
  job["Objects"] = objectPaths({kVolumePath});

  auto& systemBlock =
      objects.values[kSystemVolumePath][lambda::system::UDisks2Client::blockInterfaceName];
  systemBlock["Device"] = bytes("/dev/nvme0n1p1");
  systemBlock["PreferredDevice"] = bytes("/dev/nvme0n1p1");
  systemBlock["Drive"] = lambda::dbus::ObjectPath{"/"};
  systemBlock["IdUsage"] = std::string("filesystem");
  systemBlock["IdType"] = std::string("ext4");
  systemBlock["IdLabel"] = std::string("ROOT");
  systemBlock["Size"] = std::uint64_t(512ull * 1024ull * 1024ull * 1024ull);
  systemBlock["HintSystem"] = true;
  systemBlock["HintIgnore"] = false;

  auto& systemFilesystem =
      objects.values[kSystemVolumePath][lambda::system::UDisks2Client::filesystemInterfaceName];
  systemFilesystem["MountPoints"] = mountPoints({"/"});
  return objects;
}

lambda::system::UDisks2VolumeSnapshot const*
volumeByPath(lambda::system::UDisks2Snapshot const& snapshot, std::string const& path) {
  auto found = std::find_if(snapshot.volumes.begin(), snapshot.volumes.end(), [&](auto const& volume) {
    return volume.path == path;
  });
  return found == snapshot.volumes.end() ? nullptr : &*found;
}

} // namespace

TEST_CASE("UDisks2 support is compile-time declared") {
  CHECK((LAMBDA_HAS_DBUS == 0 || LAMBDA_HAS_DBUS == 1));
}

#if LAMBDA_HAS_DBUS

TEST_CASE("UDisks2Client reads visible filesystems and sends mount operations") {
  auto privateBus = startPrivateBus();
  if (!privateBus) {
    MESSAGE("Skipping UDisks2 integration test because a private bus could not be started");
    return;
  }

  auto service = lambda::dbus::Bus::openAddress(privateBus->address);
  lambda::system::UDisks2Client client(lambda::dbus::Bus::openAddress(privateBus->address));
  service.requestName(lambda::system::UDisks2Client::serviceName);

  bool mounted = false;
  bool encryptedUnlocked = false;
  bool busyUnmount = false;
  bool lastForceUnmount = false;
  bool lastUnlockReadOnly = false;
  std::string lastMountOptions;
  int mountCalls = 0;
  int unmountCalls = 0;
  int ejectCalls = 0;
  int unlockCalls = 0;
  int lockCalls = 0;
  int cancelCalls = 0;

  auto managerSlot = service.exportObject(
      lambda::system::UDisks2Client::objectManagerPath,
      lambda::dbus::ObjectDefinition{
          .methods = {
              lambda::dbus::ExportedMethod{
                  .interface = lambda::system::UDisks2Client::objectManagerInterfaceName,
                  .member = "GetManagedObjects",
                  .handler = [&](lambda::dbus::Message&) {
                    return lambda::dbus::MethodReply{
                        .values = {managedObjects(mounted, encryptedUnlocked)},
                    };
                  },
              },
          },
          .properties = {},
      });

  auto filesystemSlot = service.exportObject(
      kVolumePath,
      lambda::dbus::ObjectDefinition{
          .methods = {
              lambda::dbus::ExportedMethod{
                  .interface = lambda::system::UDisks2Client::filesystemInterfaceName,
                  .member = "Mount",
                  .handler = [&](lambda::dbus::Message& message) {
                    auto options = message.readVariantDictionary();
                    if (auto option = options.values.find("options"); option != options.values.end()) {
                      lastMountOptions = std::get<std::string>(option->second);
                    }
                    ++mountCalls;
                    mounted = true;
                    return lambda::dbus::MethodReply{
                        .values = {std::string("/run/media/test/LAMBDA_USB")},
                    };
                  },
              },
              lambda::dbus::ExportedMethod{
                  .interface = lambda::system::UDisks2Client::filesystemInterfaceName,
                  .member = "Unmount",
                  .handler = [&](lambda::dbus::Message& message) {
                    auto options = message.readVariantDictionary();
                    lastForceUnmount = false;
                    if (auto force = options.values.find("force"); force != options.values.end()) {
                      lastForceUnmount = std::get<bool>(force->second);
                    }
                    if (busyUnmount && !lastForceUnmount) {
                      return lambda::dbus::MethodReply::error(
                          "org.freedesktop.UDisks2.Error.DeviceBusy",
                          "Device is busy");
                    }
                    ++unmountCalls;
                    mounted = false;
                    return lambda::dbus::MethodReply{};
                  },
              },
          },
          .properties = {},
      });

  auto encryptedSlot = service.exportObject(
      kEncryptedPath,
      lambda::dbus::ObjectDefinition{
          .methods = {
              lambda::dbus::ExportedMethod{
                  .interface = lambda::system::UDisks2Client::encryptedInterfaceName,
                  .member = "Unlock",
                  .handler = [&](lambda::dbus::Message& message) {
                    std::string const passphrase = message.readString();
                    auto options = message.readVariantDictionary();
                    if (auto readOnly = options.values.find("read-only");
                        readOnly != options.values.end()) {
                      lastUnlockReadOnly = std::get<bool>(readOnly->second);
                    }
                    ++unlockCalls;
                    if (passphrase != "secret") {
                      return lambda::dbus::MethodReply::error(
                          "org.freedesktop.UDisks2.Error.NotAuthorizedCanObtain",
                          "Authentication is required");
                    }
                    encryptedUnlocked = true;
                    return lambda::dbus::MethodReply{
                        .values = {lambda::dbus::ObjectPath{kCleartextPath}},
                    };
                  },
              },
              lambda::dbus::ExportedMethod{
                  .interface = lambda::system::UDisks2Client::encryptedInterfaceName,
                  .member = "Lock",
                  .handler = [&](lambda::dbus::Message& message) {
                    message.skip("a{sv}");
                    ++lockCalls;
                    encryptedUnlocked = false;
                    return lambda::dbus::MethodReply{};
                  },
              },
          },
          .properties = {},
      });

  auto driveSlot = service.exportObject(
      kDrivePath,
      lambda::dbus::ObjectDefinition{
          .methods = {
              lambda::dbus::ExportedMethod{
                  .interface = lambda::system::UDisks2Client::driveInterfaceName,
                  .member = "Eject",
                  .handler = [&](lambda::dbus::Message& message) {
                    message.skip("a{sv}");
                    ++ejectCalls;
                    return lambda::dbus::MethodReply{};
                  },
              },
          },
          .properties = {},
      });

  auto jobSlot = service.exportObject(
      kJobPath,
      lambda::dbus::ObjectDefinition{
          .methods = {
              lambda::dbus::ExportedMethod{
                  .interface = lambda::system::UDisks2Client::jobInterfaceName,
                  .member = "Cancel",
                  .handler = [&](lambda::dbus::Message& message) {
                    message.skip("a{sv}");
                    ++cancelCalls;
                    return lambda::dbus::MethodReply{};
                  },
              },
          },
          .properties = {},
      });

  std::atomic<bool> running = true;
  std::thread serviceThread([&] {
    while (running.load()) {
      pollBus(service, 25);
    }
  });
  ScopeExit stopServiceThread([&] {
    running = false;
    if (serviceThread.joinable()) {
      serviceThread.join();
    }
  });

  auto snapshot = client.readSnapshot();
  REQUIRE(snapshot.drives.size() == 1);
  CHECK(snapshot.drives.front().path == kDrivePath);
  CHECK(snapshot.drives.front().ejectable);
  REQUIRE(snapshot.jobs.size() == 1);
  CHECK(snapshot.jobs.front().path == kJobPath);
  CHECK(snapshot.jobs.front().operation == "filesystem-mount");
  CHECK(snapshot.jobs.front().progress == doctest::Approx(0.5));
  CHECK(snapshot.jobs.front().progressValid);
  CHECK(snapshot.jobs.front().cancelable);
  CHECK(snapshot.jobs.front().startedByUid == 1000);
  CHECK(snapshot.jobs.front().objectPaths == std::vector<std::string>{kVolumePath});
  REQUIRE(snapshot.volumes.size() == 2);
  auto const* mountedVolume = volumeByPath(snapshot, kVolumePath);
  REQUIRE(mountedVolume != nullptr);
  CHECK(mountedVolume->device == "/dev/sdb1");
  CHECK(mountedVolume->preferredDevice == "/dev/disk/by-label/LAMBDA_USB");
  CHECK(mountedVolume->drive.model == "USB Stick");
  CHECK(mountedVolume->filesystemType == "vfat");
  CHECK(mountedVolume->hintAuto);
  CHECK(mountedVolume->userspaceMountOptions == std::vector<std::string>{"uhelper=udisks2"});
  REQUIRE(mountedVolume->jobs.size() == 1);
  CHECK(mountedVolume->jobs.front().operation == "filesystem-mount");
  CHECK(!mountedVolume->mounted());
  CHECK(lambda::system::formatUDisks2VolumeName(*mountedVolume) == "LAMBDA_USB");

  auto const* encryptedVolume = volumeByPath(snapshot, kEncryptedPath);
  REQUIRE(encryptedVolume != nullptr);
  CHECK(encryptedVolume->encrypted);
  CHECK_FALSE(encryptedVolume->unlocked);
  CHECK(encryptedVolume->cleartextDevice == "/");
  CHECK(encryptedVolume->encryptionType == "luks");
  CHECK(encryptedVolume->encryptionMetadataSize == 16ull * 1024ull * 1024ull);
  CHECK(encryptedVolume->filesystemUsage == "crypto");
  CHECK(lambda::system::formatUDisks2VolumeName(*encryptedVolume) == "LOCKED_USB");

  int statusChanges = 0;
  auto statusWatch = client.watchStatusChanges([&] {
    ++statusChanges;
  });
  service.emitPropertiesChanged(
      kDrivePath,
      lambda::system::UDisks2Client::driveInterfaceName,
      lambda::dbus::VariantDictionary{
          .values = {{"Ejectable", lambda::dbus::BasicValue(true)}},
      });
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 1; },
                  std::chrono::milliseconds(500)));

  service.emitPropertiesChanged(
      kEncryptedPath,
      lambda::system::UDisks2Client::encryptedInterfaceName,
      lambda::dbus::VariantDictionary{
          .values = {{"CleartextDevice", lambda::dbus::BasicValue(lambda::dbus::ObjectPath{kCleartextPath})}},
      });
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 2; },
                  std::chrono::milliseconds(500)));

  service.emitPropertiesChanged(
      kJobPath,
      lambda::system::UDisks2Client::jobInterfaceName,
      lambda::dbus::VariantDictionary{
          .values = {{"Progress", lambda::dbus::BasicValue(0.75)}},
      });
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 3; },
                  std::chrono::milliseconds(500)));

  lambda::dbus::NamespacedVariantDictionary addedInterfaces;
  addedInterfaces.values[lambda::system::UDisks2Client::blockInterfaceName] = {
      {"HintIgnore", lambda::dbus::BasicValue(false)},
  };
  service.emitSignal(lambda::system::UDisks2Client::objectManagerPath,
                     lambda::system::UDisks2Client::objectManagerInterfaceName,
                     "InterfacesAdded",
                     {lambda::dbus::ObjectPath{"/org/freedesktop/UDisks2/block_devices/sdc1"},
                      addedInterfaces});
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 4; },
                  std::chrono::milliseconds(500)));

  service.emitSignal(lambda::system::UDisks2Client::objectManagerPath,
                     lambda::system::UDisks2Client::objectManagerInterfaceName,
                     "InterfacesRemoved",
                     {lambda::dbus::ObjectPath{"/org/freedesktop/UDisks2/block_devices/sdc1"},
                      lambda::dbus::StringArray{
                          .values = {lambda::system::UDisks2Client::blockInterfaceName},
                      }});
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return statusChanges == 5; },
                  std::chrono::milliseconds(500)));

  auto mountResult = client.tryMountFilesystem(
      kVolumePath,
      lambda::system::UDisks2MountOptions{.mountOptions = "ro,nosuid"});
  REQUIRE(mountResult.ok);
  CHECK(mountResult.value == "/run/media/test/LAMBDA_USB");
  CHECK(mountCalls == 1);
  CHECK(lastMountOptions == "ro,nosuid");
  snapshot = client.readSnapshot();
  mountedVolume = volumeByPath(snapshot, kVolumePath);
  REQUIRE(mountedVolume != nullptr);
  REQUIRE(mountedVolume->mountPoints.size() == 1);
  CHECK(mountedVolume->mountPoints.front() == "/run/media/test/LAMBDA_USB");

  busyUnmount = true;
  auto busyResult = client.tryUnmountFilesystem(kVolumePath);
  CHECK_FALSE(busyResult.ok);
  CHECK(busyResult.deviceBusy);
  CHECK(busyResult.retryable);
  CHECK(busyResult.canForce);
  CHECK(busyResult.userMessage.find("busy") != std::string::npos);
  CHECK(unmountCalls == 0);

  auto unmountResult = client.tryUnmountFilesystem(
      kVolumePath,
      lambda::system::UDisks2MountOptions{.force = true});
  CHECK(unmountResult.ok);
  CHECK(unmountCalls == 1);
  CHECK(lastForceUnmount);
  CHECK(!mounted);

  auto deniedUnlock = client.tryUnlockEncrypted(kEncryptedPath, "wrong");
  CHECK_FALSE(deniedUnlock.ok);
  CHECK(deniedUnlock.notAuthorized);
  CHECK(deniedUnlock.retryable);

  auto unlockResult = client.tryUnlockEncrypted(
      kEncryptedPath,
      "secret",
      lambda::system::UDisks2MountOptions{.readOnly = true});
  REQUIRE(unlockResult.ok);
  CHECK(unlockResult.value == kCleartextPath);
  CHECK(unlockCalls == 2);
  CHECK(lastUnlockReadOnly);
  snapshot = client.readSnapshot();
  encryptedVolume = volumeByPath(snapshot, kEncryptedPath);
  REQUIRE(encryptedVolume != nullptr);
  CHECK(encryptedVolume->unlocked);
  CHECK(encryptedVolume->cleartextDevice == kCleartextPath);
  auto const* cleartextVolume = volumeByPath(snapshot, kCleartextPath);
  REQUIRE(cleartextVolume != nullptr);
  CHECK(cleartextVolume->cleartext);
  CHECK(cleartextVolume->cryptoBackingDevice == kEncryptedPath);
  CHECK(cleartextVolume->readOnly);

  auto lockResult = client.tryLockEncrypted(kEncryptedPath);
  CHECK(lockResult.ok);
  CHECK(lockCalls == 1);
  CHECK_FALSE(encryptedUnlocked);

  auto cancelResult = client.tryCancelJob(kJobPath);
  CHECK(cancelResult.ok);
  CHECK(cancelCalls == 1);

  client.ejectDrive(kDrivePath);
  CHECK(ejectCalls == 1);
}

#endif
