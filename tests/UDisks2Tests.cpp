#include <Lambda/System/UDisks2.hpp>

#include "DBusTestHelpers.hpp"

#include <doctest/doctest.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using lambda::testing::dbus::pollBus;
using lambda::testing::dbus::startPrivateBus;

constexpr char kDrivePath[] = "/org/freedesktop/UDisks2/drives/Lambda_USB";
constexpr char kVolumePath[] = "/org/freedesktop/UDisks2/block_devices/sdb1";
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

lambda::dbus::ManagedObjectDictionary managedObjects(bool mounted) {
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

  auto& filesystem =
      objects.values[kVolumePath][lambda::system::UDisks2Client::filesystemInterfaceName];
  filesystem["MountPoints"] =
      mounted ? mountPoints({"/run/media/test/LAMBDA_USB"}) : lambda::dbus::ByteArrayArray{};
  filesystem["Size"] = std::uint64_t(4ull * 1024ull * 1024ull * 1024ull);

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
  int mountCalls = 0;
  int unmountCalls = 0;
  int ejectCalls = 0;

  auto managerSlot = service.exportObject(
      lambda::system::UDisks2Client::objectManagerPath,
      lambda::dbus::ObjectDefinition{
          .methods = {
              lambda::dbus::ExportedMethod{
                  .interface = lambda::system::UDisks2Client::objectManagerInterfaceName,
                  .member = "GetManagedObjects",
                  .handler = [&](lambda::dbus::Message&) {
                    return lambda::dbus::MethodReply{
                        .values = {managedObjects(mounted)},
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
                    message.skip("a{sv}");
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
                    message.skip("a{sv}");
                    ++unmountCalls;
                    mounted = false;
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
  REQUIRE(snapshot.volumes.size() == 1);
  CHECK(snapshot.volumes.front().path == kVolumePath);
  CHECK(snapshot.volumes.front().device == "/dev/sdb1");
  CHECK(snapshot.volumes.front().preferredDevice == "/dev/disk/by-label/LAMBDA_USB");
  CHECK(snapshot.volumes.front().drive.model == "USB Stick");
  CHECK(snapshot.volumes.front().filesystemType == "vfat");
  CHECK(snapshot.volumes.front().hintAuto);
  CHECK(!snapshot.volumes.front().mounted());
  CHECK(lambda::system::formatUDisks2VolumeName(snapshot.volumes.front()) == "LAMBDA_USB");

  CHECK(client.mountFilesystem(kVolumePath) == "/run/media/test/LAMBDA_USB");
  CHECK(mountCalls == 1);
  snapshot = client.readSnapshot();
  REQUIRE(snapshot.volumes.size() == 1);
  REQUIRE(snapshot.volumes.front().mountPoints.size() == 1);
  CHECK(snapshot.volumes.front().mountPoints.front() == "/run/media/test/LAMBDA_USB");

  client.unmountFilesystem(kVolumePath);
  CHECK(unmountCalls == 1);
  CHECK(!mounted);

  client.ejectDrive(kDrivePath);
  CHECK(ejectCalls == 1);
}

#endif
