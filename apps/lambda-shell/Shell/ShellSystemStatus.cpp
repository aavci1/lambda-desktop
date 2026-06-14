#include "Shell/ShellSystemStatus.hpp"

#include <Lambda/System/BlueZ.hpp>
#include <Lambda/System/MPRIS.hpp>
#include <Lambda/System/NetworkManager.hpp>
#include <Lambda/System/UPower.hpp>

#include "Shell/ShellAudioControl.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
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

std::string readBatteryStatus(std::filesystem::path const& sysRoot) {
  if (sysRoot == "/sys") {
    try {
      auto client = lambda::system::UPowerClient::connectSystem();
      auto const device = client.readDisplayDevice();
      std::string const status = lambda::system::formatUPowerBatteryStatus(device);
      if (status != "unavailable") {
        return status;
      }
    } catch (...) {
    }
  }

  for (auto const& supplyPath : childDirectories(sysRoot / "class" / "power_supply")) {
    if (lowerAscii(readText(supplyPath / "type").value_or("")) != "battery") continue;
    if (auto capacity = readText(supplyPath / "capacity")) {
      return *capacity + "%";
    }
    if (auto state = readText(supplyPath / "status")) {
      return *state;
    }
    return "unknown";
  }
  return "unavailable";
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

SystemStatus readShellSystemStatus(std::filesystem::path sysRoot) {
  return readShellSystemStatus(std::move(sysRoot), defaultAudioControlContext());
}

SystemStatus readShellSystemStatus(std::filesystem::path sysRoot,
                                   AudioControlContext const& audioContext) {
  SystemStatus status;
  populateNetworkStatus(sysRoot, status);
  status.bluetooth = readBluetoothStatus(sysRoot);
  status.volume = readVolumeStatus(audioContext);
  status.battery = readBatteryStatus(sysRoot);
  status.media = readMediaStatus(sysRoot);
  return status;
}

} // namespace lambda_shell
