#pragma once

/// \file Lambda/Shell/ShellIpc.hpp
///
/// Shared JSON-line shell ↔ compositor IPC parsing and serialization.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lambdaui::shell {

enum class ShellMessageKind : std::uint8_t {
  Unknown,
  ShellHello,
  ShellRefreshState,
  ShellOpenCommandLauncher,
  WindowManagerWelcome,
  WindowManagerSnapshot,
  WindowManagerLaunchApp,
  WindowManagerFocusApp,
  WindowManagerFocusWindow,
  WindowManagerQuitApp,
  WindowManagerClaimCommandLauncherModal,
  WindowManagerReleaseCommandLauncherModal,
  WindowManagerError,
};

struct ShellHelloMessage {
  int protocolVersion = 1;
  std::string shellVersion;
  std::vector<std::string> capabilities;
};

struct ShellLaunchAppMessage {
  std::string appId;
};

struct ShellFocusAppMessage {
  std::string appId;
};

struct ShellFocusWindowMessage {
  std::uint64_t windowId = 0;
};

struct ShellQuitAppMessage {
  std::string appId;
};

struct ShellErrorMessage {
  std::string code;
  std::string message;
};

struct ShellMessage {
  ShellMessageKind kind = ShellMessageKind::Unknown;
  std::uint64_t requestId = 0;
  ShellHelloMessage hello{};
  ShellLaunchAppMessage launchApp{};
  ShellFocusAppMessage focusApp{};
  ShellFocusWindowMessage focusWindow{};
  ShellQuitAppMessage quitApp{};
  ShellErrorMessage error{};
};

std::string escapeJson(std::string_view text);
std::string jsonStringField(std::string_view line, std::string_view name, std::size_t start = 0);
float jsonFloatField(std::string_view line, std::string_view name, float fallback);
std::uint64_t jsonUintField(std::string_view line, std::string_view name);
bool lineContains(std::string_view line, std::string_view needle);

std::optional<ShellMessage> parseLine(std::string_view line);
std::string serialize(ShellMessage const& message);

std::string serializeShellHello(int protocolVersion,
                                std::string_view shellVersion,
                                std::vector<std::string> const& capabilities,
                                std::uint64_t requestId = 0);
std::string serializeLaunchApp(std::string_view appId, std::uint64_t requestId = 0);
std::string serializeFocusApp(std::string_view appId, std::uint64_t requestId = 0);
std::string serializeFocusWindow(std::uint64_t windowId, std::uint64_t requestId = 0);
std::string serializeQuitApp(std::string_view appId, std::uint64_t requestId = 0);
std::string serializeClaimCommandLauncherModal(std::uint64_t requestId = 0);
std::string serializeReleaseCommandLauncherModal(std::uint64_t requestId = 0);
std::string serializeRefreshState(std::uint64_t requestId = 0);
std::string serializeOpenCommandLauncher(std::uint64_t requestId = 0);
std::string serializeWindowManagerError(std::string_view code,
                                        std::string_view message,
                                        std::uint64_t requestId = 0);

} // namespace lambdaui::shell
