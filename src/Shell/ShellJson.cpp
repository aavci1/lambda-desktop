#include "Shell/ShellJson.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace lambda_shell {

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

} // namespace lambda_shell
