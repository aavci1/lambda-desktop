#pragma once

#include "Shell/ShellAudioControl.hpp"
#include "Shell/UI/LambdaShellTypes.hpp"

#include <Lambda/System/UPower.hpp>

#include <filesystem>

namespace lambda_shell {

[[nodiscard]] BatteryStatus batteryStatusFromUPower(lambda::system::UPowerDisplayDevice const& device);
[[nodiscard]] SystemStatus readShellSystemStatus(std::filesystem::path sysRoot = "/sys");
[[nodiscard]] SystemStatus readShellSystemStatus(std::filesystem::path sysRoot,
                                                 AudioControlContext const& audioContext);

} // namespace lambda_shell
