#include <Lambda/System/PortalSettings.hpp>

#include <toml++/toml.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace lambda::system {

namespace {

std::string lowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

std::optional<bool> boolFromEnvironment(char const* name) {
  char const* raw = std::getenv(name);
  if (!raw || !*raw) {
    return std::nullopt;
  }
  std::string value = lowerAscii(raw);
  if (value == "1" || value == "true" || value == "yes" || value == "on") {
    return true;
  }
  if (value == "0" || value == "false" || value == "no" || value == "off") {
    return false;
  }
  return std::nullopt;
}

std::optional<bool> boolFromToml(toml::table const& table, char const* key) {
  return table[key].value<bool>();
}

std::optional<int> hexNibble(char ch) {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
  if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
  return std::nullopt;
}

std::optional<std::uint8_t> hexByte(std::string_view value, std::size_t offset) {
  auto high = hexNibble(value[offset]);
  auto low = hexNibble(value[offset + 1u]);
  if (!high || !low) return std::nullopt;
  return static_cast<std::uint8_t>((*high << 4) | *low);
}

std::optional<double> parseUnitDouble(std::string const& raw) {
  char* end = nullptr;
  double const value = std::strtod(raw.c_str(), &end);
  if (end == raw.c_str() || *end != '\0' || value < 0.0 || value > 1.0) {
    return std::nullopt;
  }
  return value;
}

std::optional<PortalAccentColor> accentColorFromString(std::string value) {
  if (value.empty()) {
    return std::nullopt;
  }

  if ((value.size() == 7u || value.size() == 9u) && value.front() == '#') {
    auto r = hexByte(value, 1u);
    auto g = hexByte(value, 3u);
    auto b = hexByte(value, 5u);
    if (!r || !g || !b) {
      return std::nullopt;
    }
    double constexpr scale = 1.0 / 255.0;
    return PortalAccentColor{.red = *r * scale, .green = *g * scale, .blue = *b * scale};
  }

  std::replace(value.begin(), value.end(), ',', ' ');
  std::istringstream input(value);
  std::string red;
  std::string green;
  std::string blue;
  if (!(input >> red >> green >> blue)) {
    return std::nullopt;
  }
  auto r = parseUnitDouble(red);
  auto g = parseUnitDouble(green);
  auto b = parseUnitDouble(blue);
  if (!r || !g || !b) {
    return std::nullopt;
  }
  return PortalAccentColor{.red = *r, .green = *g, .blue = *b};
}

std::optional<PortalAccentColor> accentColorFromEnvironment() {
  char const* raw = std::getenv("LAMBDA_PORTAL_ACCENT_COLOR");
  if (!raw || !*raw) {
    return std::nullopt;
  }
  return accentColorFromString(raw);
}

std::optional<PortalAccentColor> accentColorFromToml(toml::table const& table, char const* key) {
  if (auto value = table[key].value<std::string>()) {
    return accentColorFromString(*value);
  }
  return std::nullopt;
}

std::optional<PortalColorScheme> colorSchemeFromString(std::string value) {
  if (value.empty()) {
    return std::nullopt;
  }
  value = lowerAscii(std::move(value));
  if (value == "0" || value == "default" || value == "none" || value == "no-preference" ||
      value == "no_preference" || value == "system") {
    return PortalColorScheme::NoPreference;
  }
  if (value == "1" || value == "dark" || value == "prefer-dark" || value == "prefer_dark") {
    return PortalColorScheme::PreferDark;
  }
  if (value == "2" || value == "light" || value == "prefer-light" || value == "prefer_light") {
    return PortalColorScheme::PreferLight;
  }
  return std::nullopt;
}

PortalColorScheme colorSchemeFromEnvironment() {
  char const* raw = std::getenv("LAMBDA_PORTAL_COLOR_SCHEME");
  if (!raw || !*raw) {
    return PortalColorScheme::NoPreference;
  }
  if (auto parsed = colorSchemeFromString(raw)) {
    return *parsed;
  }
  return PortalColorScheme::NoPreference;
}

std::optional<PortalColorScheme> colorSchemeFromToml(toml::table const& table, char const* key) {
  if (auto value = table[key].value<std::string>()) {
    return colorSchemeFromString(*value);
  }
  if (auto value = table[key].value<std::int64_t>()) {
    return colorSchemeFromString(std::to_string(*value));
  }
  return std::nullopt;
}

std::optional<toml::table> parseTomlFile(std::filesystem::path const& path) {
  std::ifstream in(path);
  if (!in) return std::nullopt;

  std::ostringstream contents;
  contents << in.rdbuf();
  try {
    return toml::parse(contents.str());
  } catch (...) {
    return std::nullopt;
  }
}

} // namespace

bool PortalAccentColor::valid() const noexcept {
  auto inRange = [](double value) {
    return value >= 0.0 && value <= 1.0;
  };
  return inRange(red) && inRange(green) && inRange(blue);
}

PortalSettingsService::PortalSettingsService(dbus::Bus& bus, PortalSettingsState state)
    : bus_(&bus),
      state_(std::move(state)) {}

dbus::Slot PortalSettingsService::exportObject() {
  return bus_->exportObject(
      objectPath,
      dbus::ObjectDefinition{
          .methods = {
              dbus::ExportedMethod{
                  .interface = interfaceName,
                  .member = "Read",
                  .handler = [this](dbus::Message& message) {
                    std::string const nameSpace = message.readString();
                    std::string const key = message.readString();
                    try {
                      dbus::MethodReply reply;
                      reply.values = {dbus::VariantValue{read(nameSpace, key)}};
                      return reply;
                    } catch (dbus::Error const&) {
                      throw;
                    } catch (std::exception const& error) {
                      return dbus::MethodReply::error("org.freedesktop.portal.Error.NotFound", error.what());
                    }
                  },
              },
              dbus::ExportedMethod{
                  .interface = interfaceName,
                  .member = "ReadAll",
                  .handler = [this](dbus::Message& message) {
                    dbus::MethodReply reply;
                    reply.values = {readAll(message.readStringArray())};
                    return reply;
                  },
              },
          },
          .properties = {
              dbus::ExportedProperty{
                  .interface = interfaceName,
                  .name = "version",
                  .value = std::uint32_t(1),
                  .writable = false,
                  .getter = {},
                  .setter = {},
              },
          },
      });
}

dbus::BasicValue PortalSettingsService::read(std::string const& nameSpace, std::string const& key) const {
  if (nameSpace != appearanceNamespace) {
    throw std::runtime_error("Unknown portal settings namespace " + nameSpace);
  }
  auto settings = appearanceSettings();
  auto found = settings.find(key);
  if (found == settings.end()) {
    throw std::runtime_error("Unknown portal setting " + nameSpace + "." + key);
  }
  return found->second;
}

dbus::NamespacedVariantDictionary PortalSettingsService::readAll(dbus::StringArray const& namespaces) const {
  dbus::NamespacedVariantDictionary result;
  bool const matchAll = namespaces.values.empty() ||
                        std::find(namespaces.values.begin(), namespaces.values.end(), "") != namespaces.values.end();
  if (matchAll) {
    result.values[appearanceNamespace] = appearanceSettings();
    return result;
  }

  for (auto const& requested : namespaces.values) {
    if (namespaceMatches(requested, appearanceNamespace)) {
      result.values[appearanceNamespace] = appearanceSettings();
    }
  }
  return result;
}

void PortalSettingsService::setState(PortalSettingsState state) {
  state_ = std::move(state);
}

void PortalSettingsService::emitChanged(std::string const& key) const {
  if (!bus_) {
    return;
  }
  bus_->emitSignal(objectPath,
                   interfaceName,
                   "SettingChanged",
                   {std::string(appearanceNamespace), key, dbus::VariantValue{read(appearanceNamespace, key)}});
}

PortalSettingsState PortalSettingsService::stateFromEnvironment() {
  PortalSettingsState state;
  state.colorScheme = colorSchemeFromEnvironment();
  if (auto accent = accentColorFromEnvironment()) {
    state.accentColor = *accent;
  }
  if (auto highContrast = boolFromEnvironment("LAMBDA_PORTAL_HIGH_CONTRAST")) {
    state.highContrast = *highContrast;
  }
  if (auto reducedMotion = boolFromEnvironment("LAMBDA_PORTAL_REDUCED_MOTION")) {
    state.reducedMotion = *reducedMotion;
  }
  return state;
}

std::filesystem::path PortalSettingsService::shellConfigPathFromEnvironment() {
  if (char const* explicitPath = std::getenv("LAMBDA_SHELL_CONFIG"); explicitPath && *explicitPath) {
    return explicitPath;
  }
  if (char const* configHome = std::getenv("XDG_CONFIG_HOME"); configHome && *configHome) {
    return std::filesystem::path(configHome) / "lambda-shell" / "config.toml";
  }
  if (char const* home = std::getenv("HOME"); home && *home) {
    return std::filesystem::path(home) / ".config" / "lambda-shell" / "config.toml";
  }
  return std::filesystem::temp_directory_path() / "lambda-shell" / "config.toml";
}

PortalSettingsState PortalSettingsService::stateFromShellConfig(std::filesystem::path path) {
  PortalSettingsState state = stateFromEnvironment();
  if (path.empty()) {
    path = shellConfigPathFromEnvironment();
  }

  auto root = parseTomlFile(path);
  if (!root) {
    return state;
  }

  if (auto* appearance = (*root)["appearance"].as_table()) {
    if (auto colorScheme = colorSchemeFromToml(*appearance, "color_scheme")) {
      state.colorScheme = *colorScheme;
    }
    if (auto accent = accentColorFromToml(*appearance, "accent_color")) {
      state.accentColor = *accent;
    }
    if (auto highContrast = boolFromToml(*appearance, "high_contrast")) {
      state.highContrast = *highContrast;
    }
    if (auto reducedMotion = boolFromToml(*appearance, "reduced_motion")) {
      state.reducedMotion = *reducedMotion;
    }
  }
  return state;
}

std::map<std::string, dbus::BasicValue> PortalSettingsService::appearanceSettings() const {
  std::map<std::string, dbus::BasicValue> values;
  values["color-scheme"] = static_cast<std::uint32_t>(state_.colorScheme);
  if (state_.accentColor && state_.accentColor->valid()) {
    values["accent-color"] = dbus::RgbColor{
        .red = state_.accentColor->red,
        .green = state_.accentColor->green,
        .blue = state_.accentColor->blue,
    };
  }
  values["contrast"] = std::uint32_t(state_.highContrast ? 1 : 0);
  values["reduced-motion"] = std::uint32_t(state_.reducedMotion ? 1 : 0);
  return values;
}

bool PortalSettingsService::namespaceMatches(std::string const& requested, std::string const& available) const {
  if (requested.empty() || requested == available) {
    return true;
  }
  constexpr std::string_view suffix = ".*";
  if (requested.size() > suffix.size() &&
      requested.compare(requested.size() - suffix.size(), suffix.size(), suffix) == 0) {
    std::string const prefix = requested.substr(0, requested.size() - suffix.size());
    return available == prefix || available.starts_with(prefix + ".");
  }
  return false;
}

} // namespace lambda::system
