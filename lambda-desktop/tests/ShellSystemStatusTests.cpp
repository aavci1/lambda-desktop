#include "Shell/ShellSystemStatus.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>

namespace {

std::filesystem::path tempRoot(char const* name) {
  auto path = std::filesystem::temp_directory_path() /
              (std::string(name) + "-" + std::to_string(static_cast<unsigned long long>(getpid())));
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

void writeFile(std::filesystem::path const& path, std::string const& text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream(path) << text;
}

lambda_shell::AudioControlContext noAudioContext() {
  return lambda_shell::AudioControlContext{
      .run = [](std::vector<std::string> const&) {
        return lambda_shell::AudioCommandResult{.exitCode = 127, .output = {}};
      },
  };
}

} // namespace

TEST_CASE("Shell system status reads docklet data without compositor snapshots") {
  auto root = tempRoot("lambda-shell-system-status-test");

  std::filesystem::create_directories(root / "class" / "net" / "wlan0" / "wireless");
  writeFile(root / "class" / "net" / "wlan0" / "operstate", "up\n");
  writeFile(root / "class" / "rfkill" / "rfkill0" / "type", "bluetooth\n");
  writeFile(root / "class" / "rfkill" / "rfkill0" / "state", "1\n");
  writeFile(root / "class" / "power_supply" / "BAT0" / "type", "Battery\n");
  writeFile(root / "class" / "power_supply" / "BAT0" / "capacity", "88\n");
  writeFile(root / "class" / "power_supply" / "BAT0" / "status", "Discharging\n");

  auto status = lambda_shell::readShellSystemStatus(root, noAudioContext());
  CHECK(status.network == "online");
  CHECK(status.wifi == "wlan0");
  CHECK(status.bluetooth == "on");
  CHECK(status.volume == "unavailable");
  CHECK(status.battery == "88%");
  CHECK(status.batteryStatus.available);
  CHECK(status.batteryStatus.present);
  CHECK(status.batteryStatus.percentage == 88);
  CHECK(status.batteryStatus.chargeState == lambda_shell::BatteryChargeState::Discharging);
  CHECK(status.batteryStatus.powerSource == lambda_shell::BatteryPowerSource::Battery);
  CHECK(status.media == "unavailable");

  std::filesystem::remove_all(root);
}

TEST_CASE("Shell system status reports unavailable or off states from shell providers") {
  auto root = tempRoot("lambda-shell-system-status-off-test");

  std::filesystem::create_directories(root / "class" / "net" / "eth0");
  writeFile(root / "class" / "net" / "eth0" / "operstate", "down\n");
  writeFile(root / "class" / "rfkill" / "rfkill0" / "type", "bluetooth\n");
  writeFile(root / "class" / "rfkill" / "rfkill0" / "state", "0\n");

  auto status = lambda_shell::readShellSystemStatus(root, noAudioContext());
  CHECK(status.network == "off");
  CHECK(status.wifi == "unavailable");
  CHECK(status.bluetooth == "off");
  CHECK(status.battery == "unavailable");
  CHECK(status.media == "unavailable");

  std::filesystem::remove_all(root);
}

#if LAMBDA_HAS_DBUS
TEST_CASE("Shell battery status maps UPower display-device details") {
  lambdaui::system::UPowerDisplayDevice device{
      .present = true,
      .onBattery = false,
      .percentage = 92.4,
      .state = lambdaui::system::UPowerDeviceState::Charging,
      .warningLevel = lambdaui::system::UPowerWarningLevel::None,
      .timeToEmptySeconds = 0,
      .timeToFullSeconds = 1800,
      .iconName = "battery-full-charging-symbolic",
  };

  auto status = lambda_shell::batteryStatusFromUPower(device);
  CHECK(status.label == "92%");
  CHECK(status.available);
  CHECK(status.present);
  CHECK(status.percentage == 92);
  CHECK(status.chargeState == lambda_shell::BatteryChargeState::Charging);
  CHECK(status.powerSource == lambda_shell::BatteryPowerSource::AC);
  CHECK(status.warningLevel == lambda_shell::BatteryWarningLevel::None);
  CHECK(status.timeToEmptySeconds == 0);
  CHECK(status.timeToFullSeconds == 1800);
  CHECK(status.iconName == "battery-full-charging-symbolic");

  device.present = false;
  device.onBattery = false;
  status = lambda_shell::batteryStatusFromUPower(device);
  CHECK(status.label == "unavailable");
  CHECK_FALSE(status.available);
  CHECK_FALSE(status.present);
  CHECK(status.powerSource == lambda_shell::BatteryPowerSource::AC);
}
#endif

TEST_CASE("Shell system status reports audio state from injected backend") {
  auto root = tempRoot("lambda-shell-system-status-audio-test");
  lambda_shell::AudioControlContext audio{
      .run = [](std::vector<std::string> const& command) {
        if (command == std::vector<std::string>{"wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@"}) {
          return lambda_shell::AudioCommandResult{.exitCode = 0, .output = "Volume: 0.64\n"};
        }
        return lambda_shell::AudioCommandResult{.exitCode = 127, .output = {}};
      },
  };

  auto status = lambda_shell::readShellSystemStatus(root, audio);

  CHECK(status.volume == "64%");
  CHECK(status.media == "unavailable");

  std::filesystem::remove_all(root);
}
