#include <Flux/Shell/ShellIpc.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace flux::shell {
namespace {

ShellMessageKind kindForTypeField(std::string_view type) {
  if (type == "lambda.shell.hello") return ShellMessageKind::ShellHello;
  if (type == "lambda.shell.refreshState") return ShellMessageKind::ShellRefreshState;
  if (type == "lambda.shell.openCommandLauncher") return ShellMessageKind::ShellOpenCommandLauncher;
  if (type == "lambda.windowManager.welcome") return ShellMessageKind::WindowManagerWelcome;
  if (type == "lambda.windowManager.snapshot") return ShellMessageKind::WindowManagerSnapshot;
  if (type == "lambda.windowManager.launchApp") return ShellMessageKind::WindowManagerLaunchApp;
  if (type == "lambda.windowManager.focusApp") return ShellMessageKind::WindowManagerFocusApp;
  if (type == "lambda.windowManager.focusWindow") return ShellMessageKind::WindowManagerFocusWindow;
  if (type == "lambda.windowManager.claimCommandLauncherModal") {
    return ShellMessageKind::WindowManagerClaimCommandLauncherModal;
  }
  if (type == "lambda.windowManager.releaseCommandLauncherModal") {
    return ShellMessageKind::WindowManagerReleaseCommandLauncherModal;
  }
  if (type == "lambda.windowManager.error") return ShellMessageKind::WindowManagerError;
  return ShellMessageKind::Unknown;
}

std::vector<std::string> parseCapabilities(std::string_view line) {
  std::vector<std::string> capabilities;
  std::size_t pos = line.find("\"capabilities\"");
  if (pos == std::string_view::npos) return capabilities;
  pos = line.find('[', pos);
  if (pos == std::string_view::npos) return capabilities;
  std::size_t end = line.find(']', pos);
  if (end == std::string_view::npos) return capabilities;
  std::size_t cursor = pos + 1;
  while (cursor < end) {
    std::size_t quote = line.find('"', cursor);
    if (quote == std::string_view::npos || quote >= end) break;
    std::size_t close = line.find('"', quote + 1);
    if (close == std::string_view::npos || close >= end) break;
    capabilities.emplace_back(line.substr(quote + 1, close - quote - 1));
    cursor = close + 1;
  }
  return capabilities;
}

std::string requestIdJson(std::uint64_t requestId) {
  if (requestId == 0) return {};
  return ",\"requestId\":" + std::to_string(requestId);
}

} // namespace

std::string escapeJson(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8u);
  for (char c : text) {
    switch (c) {
    case '\\': out += "\\\\"; break;
    case '"': out += "\\\""; break;
    case '\n': out += "\\n"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    default:
      if (static_cast<unsigned char>(c) >= 0x20u) out.push_back(c);
      break;
    }
  }
  return out;
}

std::string jsonStringField(std::string_view line, std::string_view name, std::size_t start) {
  std::string const key = "\"" + std::string(name) + "\"";
  std::size_t pos = line.find(key, start);
  if (pos == std::string_view::npos) return {};
  pos = line.find(':', pos + key.size());
  if (pos == std::string_view::npos) return {};
  pos = line.find('"', pos + 1u);
  if (pos == std::string_view::npos) return {};
  std::string out;
  bool escaping = false;
  for (++pos; pos < line.size(); ++pos) {
    char const c = line[pos];
    if (escaping) {
      out.push_back(c);
      escaping = false;
    } else if (c == '\\') {
      escaping = true;
    } else if (c == '"') {
      break;
    } else {
      out.push_back(c);
    }
  }
  return out;
}

float jsonFloatField(std::string_view line, std::string_view name, float fallback) {
  std::string const key = "\"" + std::string(name) + "\"";
  std::size_t pos = line.find(key);
  if (pos == std::string_view::npos) return fallback;
  pos = line.find(':', pos + key.size());
  if (pos == std::string_view::npos) return fallback;
  ++pos;
  while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) ++pos;
  std::size_t end = pos;
  while (end < line.size() &&
         (std::isdigit(static_cast<unsigned char>(line[end])) || line[end] == '.' || line[end] == '-' ||
          line[end] == '+')) {
    ++end;
  }
  if (end == pos) return fallback;
  std::string value{line.substr(pos, end - pos)};
  char* parseEnd = nullptr;
  float parsed = strtof(value.c_str(), &parseEnd);
  if (parseEnd == value.c_str()) return fallback;
  return std::clamp(parsed, 0.5f, 4.f);
}

std::uint64_t jsonUintField(std::string_view line, std::string_view name) {
  std::string const key = "\"" + std::string(name) + "\"";
  std::size_t pos = line.find(key);
  if (pos == std::string_view::npos) return 0;
  pos = line.find(':', pos + key.size());
  if (pos == std::string_view::npos) return 0;
  while (++pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) {}
  std::uint64_t value = 0;
  while (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos]))) {
    value = value * 10u + static_cast<unsigned>(line[pos] - '0');
    ++pos;
  }
  return value;
}

bool lineContains(std::string_view line, std::string_view needle) {
  return line.find(needle) != std::string_view::npos;
}

std::optional<ShellMessage> parseLine(std::string_view line) {
  if (line.empty()) return std::nullopt;
  std::string const type = jsonStringField(line, "type");
  if (type.empty()) return std::nullopt;

  ShellMessage message{};
  message.kind = kindForTypeField(type);
  message.requestId = jsonUintField(line, "requestId");
  if (message.kind == ShellMessageKind::Unknown) {
    return std::nullopt;
  }
  switch (message.kind) {
  case ShellMessageKind::ShellHello:
    message.hello.protocolVersion = static_cast<int>(jsonUintField(line, "protocolVersion"));
    message.hello.shellVersion = jsonStringField(line, "shellVersion");
    message.hello.capabilities = parseCapabilities(line);
    break;
  case ShellMessageKind::WindowManagerLaunchApp:
    message.launchApp.appId = jsonStringField(line, "appId");
    break;
  case ShellMessageKind::WindowManagerFocusApp:
    message.focusApp.appId = jsonStringField(line, "appId");
    break;
  case ShellMessageKind::WindowManagerFocusWindow:
    message.focusWindow.windowId = jsonUintField(line, "windowId");
    break;
  case ShellMessageKind::WindowManagerError:
    message.error.code = jsonStringField(line, "code");
    message.error.message = jsonStringField(line, "message");
    break;
  default:
    break;
  }
  return message;
}

std::string serialize(ShellMessage const& message) {
  switch (message.kind) {
  case ShellMessageKind::ShellHello:
    return serializeShellHello(message.hello.protocolVersion,
                               message.hello.shellVersion,
                               message.hello.capabilities,
                               message.requestId);
  case ShellMessageKind::ShellRefreshState:
    return serializeRefreshState(message.requestId);
  case ShellMessageKind::ShellOpenCommandLauncher:
    return serializeOpenCommandLauncher(message.requestId);
  case ShellMessageKind::WindowManagerLaunchApp:
    return serializeLaunchApp(message.launchApp.appId, message.requestId);
  case ShellMessageKind::WindowManagerFocusApp:
    return serializeFocusApp(message.focusApp.appId, message.requestId);
  case ShellMessageKind::WindowManagerFocusWindow:
    return serializeFocusWindow(message.focusWindow.windowId, message.requestId);
  case ShellMessageKind::WindowManagerClaimCommandLauncherModal:
    return serializeClaimCommandLauncherModal(message.requestId);
  case ShellMessageKind::WindowManagerReleaseCommandLauncherModal:
    return serializeReleaseCommandLauncherModal(message.requestId);
  case ShellMessageKind::WindowManagerError:
    return serializeWindowManagerError(message.error.code, message.error.message, message.requestId);
  default:
    return {};
  }
}

std::string serializeShellHello(int protocolVersion,
                                std::string_view shellVersion,
                                std::vector<std::string> const& capabilities,
                                std::uint64_t requestId) {
  std::string json = "{\"type\":\"lambda.shell.hello\",\"protocolVersion\":" + std::to_string(protocolVersion) +
                     ",\"shellVersion\":\"" + escapeJson(shellVersion) + "\"" + requestIdJson(requestId) +
                     ",\"capabilities\":[";
  for (std::size_t i = 0; i < capabilities.size(); ++i) {
    if (i > 0) json.push_back(',');
    json += "\"";
    json += escapeJson(capabilities[i]);
    json += "\"";
  }
  json += "]}";
  return json;
}

std::string serializeLaunchApp(std::string_view appId, std::uint64_t requestId) {
  return "{\"type\":\"lambda.windowManager.launchApp\",\"appId\":\"" + escapeJson(appId) + "\"" +
         requestIdJson(requestId) + "}";
}

std::string serializeFocusApp(std::string_view appId, std::uint64_t requestId) {
  return "{\"type\":\"lambda.windowManager.focusApp\",\"appId\":\"" + escapeJson(appId) + "\"" +
         requestIdJson(requestId) + "}";
}

std::string serializeFocusWindow(std::uint64_t windowId, std::uint64_t requestId) {
  return "{\"type\":\"lambda.windowManager.focusWindow\",\"windowId\":" + std::to_string(windowId) +
         requestIdJson(requestId) + "}";
}

std::string serializeClaimCommandLauncherModal(std::uint64_t requestId) {
  return "{\"type\":\"lambda.windowManager.claimCommandLauncherModal\"" + requestIdJson(requestId) + "}";
}

std::string serializeReleaseCommandLauncherModal(std::uint64_t requestId) {
  return "{\"type\":\"lambda.windowManager.releaseCommandLauncherModal\"" + requestIdJson(requestId) + "}";
}

std::string serializeRefreshState(std::uint64_t requestId) {
  return "{\"type\":\"lambda.shell.refreshState\"" + requestIdJson(requestId) + "}";
}

std::string serializeOpenCommandLauncher(std::uint64_t requestId) {
  return "{\"type\":\"lambda.shell.openCommandLauncher\"" + requestIdJson(requestId) + "}";
}

std::string serializeWindowManagerError(std::string_view code, std::string_view message, std::uint64_t requestId) {
  return "{\"type\":\"lambda.windowManager.error\",\"code\":\"" + escapeJson(code) + "\",\"message\":\"" +
         escapeJson(message) + "\"" + requestIdJson(requestId) + "}";
}

} // namespace flux::shell
