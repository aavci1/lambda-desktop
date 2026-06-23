#pragma once

#include "Shell/ShellAudioControl.hpp"
#include "Shell/UI/LambdaShellTypes.hpp"

#if LAMBDA_HAS_DBUS
#include <Lambda/System/UPower.hpp>
#endif

#include <filesystem>

namespace lambda_shell {

#if LAMBDA_HAS_DBUS
[[nodiscard]] BatteryStatus batteryStatusFromUPower(lambdaui::system::UPowerDisplayDevice const& device);
#endif
[[nodiscard]] SystemStatus readShellSystemStatus(std::filesystem::path sysRoot = "/sys");
[[nodiscard]] SystemStatus readShellSystemStatus(std::filesystem::path sysRoot,
                                                 AudioControlContext const& audioContext);

} // namespace lambda_shell
