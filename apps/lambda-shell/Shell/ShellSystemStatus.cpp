#include "Shell/ShellSystemStatus.hpp"

#include <Lambda/System/BlueZ.hpp>
#include <Lambda/System/MPRIS.hpp>
#include <Lambda/System/NetworkManager.hpp>
#include <Lambda/System/UPower.hpp>

#include "Shell/ShellAudioControl.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace lambda_shell {
namespace {

std::string trim(std::string text) {
  auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
  text.erase(text.begin(), std::find_if(text.begin(), text.end(), [&](char ch) {
               return !isSpace(static_cast<unsigned char>(ch));
             }));
  text.erase(std::find_if(text.rbegin(), text.rend(), [&](char ch) {
               return !isSpace(static_cast<unsigned char>(ch));
             }).base(),
             text.end());
  return text;
}

std::string lowerAscii(std::string_view value) {
  std::string output(value);
  std::transform(output.begin(), output.end(), output.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return output;
}

std::optional<std::string> readText(std::filesystem::path const& path) {
  std::ifstream in(path);
  if (!in) return std::nullopt;
  std::string text;
  std::getline(in, text);
  text = trim(std::move(text));
  if (text.empty()) return std::nullopt;
  return text;
}

std::optional<int> parseInt(std::string_view text) {
  int value = 0;
  auto const* begin = text.data();
  auto const* end = begin + text.size();
  auto result = std::from_chars(begin, end, value);
  if (result.ec != std::errc{} || result.ptr != end) {
    return std::nullopt;
  }
  return value;
}

bool isDirectory(std::filesystem::path const& path) {
  std::error_code error;
  return std::filesystem::is_directory(path, error) && !error;
}

std::vector<std::filesystem::path> childDirectories(std::filesystem::path const& path) {
  std::vector<std::filesystem::path> children;
  std::error_code error;
  if (!std::filesystem::is_directory(path, error) || error) return children;
  std::filesystem::directory_iterator it(path, error);
  std::filesystem::directory_iterator end;
  while (!error && it != end) {
    std::error_code entryError;
    if (it->is_directory(entryError) && !entryError) {
      children.push_back(it->path());
    }
    it.increment(error);
  }
  std::sort(children.begin(), children.end());
  return children;
}

bool interfaceUp(std::filesystem::path const& interfacePath) {
  std::string const operstate = lowerAscii(readText(interfacePath / "operstate").value_or(""));
  if (operstate == "up") return true;
  return readText(interfacePath / "carrier").value_or("") == "1";
}

bool populateNetworkManagerStatus(SystemStatus& status) {
  try {
    auto client = lambda::system::NetworkManagerClient::connectSystem();
    auto const snapshot = client.readSnapshot();
    status.network = lambda::system::formatNetworkStatus(snapshot);
    status.wifi = lambda::system::formatWifiStatus(snapshot);
    return true;
  } catch (...) {
    return false;
  }
}

void populateNetworkStatus(std::filesystem::path const& sysRoot, SystemStatus& status) {
  if (sysRoot == "/sys" && populateNetworkManagerStatus(status)) {
    return;
  }

  bool anyInterface = false;
  bool anyConnected = false;
  bool anyWireless = false;
  bool wirelessConnected = false;
  std::string connectedWireless;

  for (auto const& interfacePath : childDirectories(sysRoot / "class" / "net")) {
    std::string const name = interfacePath.filename().string();
    if (name == "lo") continue;
    anyInterface = true;
    bool const wireless = isDirectory(interfacePath / "wireless") || isDirectory(interfacePath / "phy80211");
    bool const up = interfaceUp(interfacePath);
    anyConnected = anyConnected || up;
    if (wireless) {
      anyWireless = true;
      wirelessConnected = wirelessConnected || up;
      if (up && connectedWireless.empty()) connectedWireless = name;
    }
  }

  status.network = anyConnected ? "online" : (anyInterface ? "off" : "unavailable");
  status.wifi = wirelessConnected ? connectedWireless : (anyWireless ? "off" : "unavailable");
}

std::string readBluetoothStatus(std::filesystem::path const& sysRoot) {
  if (sysRoot == "/sys") {
    try {
      auto client = lambda::system::BlueZClient::connectSystem();
      return lambda::system::formatBluetoothStatus(client.readSnapshot());
    } catch (...) {
    }
  }

  bool foundBluetooth = false;
  for (auto const& rfkillPath : childDirectories(sysRoot / "class" / "rfkill")) {
    if (lowerAscii(readText(rfkillPath / "type").value_or("")) != "bluetooth") continue;
    foundBluetooth = true;
    if (readText(rfkillPath / "state").value_or("") == "1") {
      return "on";
    }
  }
  if (foundBluetooth) return "off";

  for (auto const& devicePath : childDirectories(sysRoot / "class" / "bluetooth")) {
    if (devicePath.filename().string().starts_with("hci")) {
      return "on";
    }
  }
  return "unavailable";
}

BatteryChargeState batteryChargeStateFromUPower(lambda::system::UPowerDeviceState state) {
  switch (state) {
  case lambda::system::UPowerDeviceState::Charging:
    return BatteryChargeState::Charging;
  case lambda::system::UPowerDeviceState::Discharging:
    return BatteryChargeState::Discharging;
  case lambda::system::UPowerDeviceState::Empty:
    return BatteryChargeState::Empty;
  case lambda::system::UPowerDeviceState::FullyCharged:
    return BatteryChargeState::Full;
  case lambda::system::UPowerDeviceState::PendingCharge:
    return BatteryChargeState::PendingCharge;
  case lambda::system::UPowerDeviceState::PendingDischarge:
    return BatteryChargeState::PendingDischarge;
  case lambda::system::UPowerDeviceState::Unknown:
  default:
    return BatteryChargeState::Unknown;
  }
}

BatteryWarningLevel batteryWarningLevelFromUPower(lambda::system::UPowerWarningLevel level) {
  switch (level) {
  case lambda::system::UPowerWarningLevel::None:
    return BatteryWarningLevel::None;
  case lambda::system::UPowerWarningLevel::Discharging:
    return BatteryWarningLevel::Discharging;
  case lambda::system::UPowerWarningLevel::Low:
    return BatteryWarningLevel::Low;
  case lambda::system::UPowerWarningLevel::Critical:
    return BatteryWarningLevel::Critical;
  case lambda::system::UPowerWarningLevel::Action:
    return BatteryWarningLevel::Action;
  case lambda::system::UPowerWarningLevel::Unknown:
  default:
    return BatteryWarningLevel::Unknown;
  }
}

BatteryChargeState batteryChargeStateFromSysfs(std::string_view value) {
  std::string const state = lowerAscii(value);
  if (state == "charging") return BatteryChargeState::Charging;
  if (state == "discharging") return BatteryChargeState::Discharging;
  if (state == "full" || state == "fully charged") return BatteryChargeState::Full;
  if (state == "empty") return BatteryChargeState::Empty;
  return BatteryChargeState::Unknown;
}

BatteryPowerSource powerSourceFor(BatteryChargeState state, bool onBattery) {
  if (onBattery) return BatteryPowerSource::Battery;
  if (state == BatteryChargeState::Charging || state == BatteryChargeState::Full ||
      state == BatteryChargeState::PendingCharge) {
    return BatteryPowerSource::AC;
  }
  if (state == BatteryChargeState::Discharging || state == BatteryChargeState::PendingDischarge ||
      state == BatteryChargeState::Empty) {
    return BatteryPowerSource::Battery;
  }
  return BatteryPowerSource::Unknown;
}

BatteryStatus unavailableBatteryStatus(BatteryPowerSource source = BatteryPowerSource::Unknown) {
  return BatteryStatus{
      .label = "unavailable",
      .available = false,
      .present = false,
      .percentage = -1,
      .chargeState = BatteryChargeState::Unknown,
      .powerSource = source,
  };
}

BatteryStatus readBatteryStatus(std::filesystem::path const& sysRoot) {
  std::optional<BatteryStatus> upowerUnavailable;
  if (sysRoot == "/sys") {
    try {
      auto client = lambda::system::UPowerClient::connectSystem();
      auto const device = client.readDisplayDevice();
      BatteryStatus status = batteryStatusFromUPower(device);
      if (status.available) {
        return status;
      }
      upowerUnavailable = std::move(status);
    } catch (...) {
    }
  }

  for (auto const& supplyPath : childDirectories(sysRoot / "class" / "power_supply")) {
    if (lowerAscii(readText(supplyPath / "type").value_or("")) != "battery") continue;
    BatteryStatus status;
    status.available = true;
    status.present = true;
    status.label = "unknown";
    if (auto state = readText(supplyPath / "status")) {
      status.chargeState = batteryChargeStateFromSysfs(*state);
      status.powerSource = powerSourceFor(status.chargeState, false);
      status.label = *state;
    }
    if (auto capacity = readText(supplyPath / "capacity")) {
      if (auto percentage = parseInt(*capacity)) {
        status.percentage = std::clamp(*percentage, 0, 100);
      }
      status.label = *capacity + "%";
    }
    return status;
  }
  if (upowerUnavailable) {
    return *upowerUnavailable;
  }
  return unavailableBatteryStatus();
}

std::string readVolumeStatus(AudioControlContext const& audioContext) {
  AudioVolumeState const state = readAudioVolumeState(audioContext);
  if (!state.available) return "unavailable";
  if (state.muted) return "off";
  return std::to_string(std::max(0, state.percent)) + "%";
}

std::string readMediaStatus(std::filesystem::path const& sysRoot) {
  if (sysRoot != "/sys") return "unavailable";

  try {
    auto client = lambda::system::MPRISClient::connectSession();
    return lambda::system::formatMPRISStatus(client.readPlayers());
  } catch (...) {
    return "unavailable";
  }
}

} // namespace

BatteryStatus batteryStatusFromUPower(lambda::system::UPowerDisplayDevice const& device) {
  BatteryPowerSource const source = device.onBattery ? BatteryPowerSource::Battery : BatteryPowerSource::AC;
  if (!device.present) {
    return unavailableBatteryStatus(source);
  }

  int const rounded = std::clamp(static_cast<int>(std::lround(device.percentage)), 0, 100);
  return BatteryStatus{
      .label = lambda::system::formatUPowerBatteryStatus(device),
      .available = true,
      .present = true,
      .percentage = rounded,
      .chargeState = batteryChargeStateFromUPower(device.state),
      .powerSource = source,
      .warningLevel = batteryWarningLevelFromUPower(device.warningLevel),
      .timeToEmptySeconds = device.timeToEmptySeconds,
      .timeToFullSeconds = device.timeToFullSeconds,
      .iconName = device.iconName,
  };
}

SystemStatus readShellSystemStatus(std::filesystem::path sysRoot) {
  return readShellSystemStatus(std::move(sysRoot), defaultAudioControlContext());
}

SystemStatus readShellSystemStatus(std::filesystem::path sysRoot,
                                   AudioControlContext const& audioContext) {
  SystemStatus status;
  populateNetworkStatus(sysRoot, status);
  status.bluetooth = readBluetoothStatus(sysRoot);
  status.volume = readVolumeStatus(audioContext);
  status.batteryStatus = readBatteryStatus(sysRoot);
  status.battery = status.batteryStatus.label;
  status.media = readMediaStatus(sysRoot);
  return status;
}

} // namespace lambda_shell
