#pragma once

#include "SettingsBackend.hpp"
#include "SettingsTheme.hpp"

#include <Lambda.hpp>
#include <Lambda/Reactive/Effect.hpp>
#include <Lambda/SceneGraph/SceneNode.hpp>
#include <Lambda/UI/EnvironmentKeys.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/Views/Button.hpp>
#include <Lambda/UI/Views/Card.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Icon.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/ScrollView.hpp>
#include <Lambda/UI/Views/SegmentedControl.hpp>
#include <Lambda/UI/Views/Select.hpp>
#include <Lambda/UI/Views/Slider.hpp>
#include <Lambda/UI/Views/Spacer.hpp>
#include <Lambda/UI/Views/Switch.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/TextInput.hpp>
#include <Lambda/UI/Views/Toggle.hpp>
#include <Lambda/UI/Views/VStack.hpp>
#include <Lambda/UI/Views/ZStack.hpp>

#include <array>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lambda_settings {

namespace {

using namespace lambda;

enum class SettingsSection {
  General,
  Appearance,
  Display,
  Input,
  Windows,
  DockPanel,
  Notifications,
  LauncherClipboard,
  About,
};

struct ChromeInsets {
  float leading = 0.f;
  float trailing = 0.f;
};

struct SidebarItem {
  SettingsSection section = SettingsSection::Appearance;
  std::string label;
  IconName icon = IconName::Settings;
};

struct SettingsRowValue {
  std::string label;
  std::string value;
};

struct NumericControlSpec {
  float min = 0.f;
  float max = 1.f;
  float step = 1.f;
  bool slider = false;
};

ChromeInsets chromeInsets(WindowChromeMetrics const& chrome) {
  ChromeInsets insets;
  for (Rect const& rect : chrome.reservedRegions) {
    if (rect.x < 120.f) {
      insets.leading = std::max(insets.leading, rect.x + rect.width + 8.f);
    } else {
      insets.trailing = std::max(insets.trailing, rect.width + 8.f);
    }
  }
  return insets;
}

std::array<SidebarItem, 9> const& sidebarItems() {
  static std::array<SidebarItem, 9> const items{{
      {SettingsSection::General, "General", IconName::Settings},
      {SettingsSection::Appearance, "Appearance", IconName::Palette},
      {SettingsSection::Display, "Display", IconName::Computer},
      {SettingsSection::Input, "Input", IconName::Keyboard},
      {SettingsSection::Windows, "Windows", IconName::Window},
      {SettingsSection::DockPanel, "Dock & Panel", IconName::Dock},
      {SettingsSection::Notifications, "Notifications", IconName::Notifications},
      {SettingsSection::LauncherClipboard, "Launcher & Clipboard", IconName::RocketLaunch},
      {SettingsSection::About, "About", IconName::Info},
  }};
  return items;
}

bool hasGroupGapBefore(SettingsSection section) {
  return section == SettingsSection::Notifications || section == SettingsSection::About;
}

bool settingBoolValue(std::string const& value) {
  std::string normalized;
  normalized.reserve(value.size());
  for (unsigned char ch : value) {
    normalized.push_back(static_cast<char>(std::tolower(ch)));
  }
  return normalized == "true" || normalized == "1" || normalized == "yes" || normalized == "on";
}

std::optional<float> parseSettingFloat(std::string const& value) {
  char* end = nullptr;
  float const parsed = std::strtof(value.c_str(), &end);
  if (end == value.c_str() || *end != '\0' || !std::isfinite(parsed)) return std::nullopt;
  return parsed;
}

std::string formatSettingNumber(float value, bool integer) {
  char buffer[48];
  if (integer) {
    std::snprintf(buffer, sizeof(buffer), "%ld", static_cast<long>(std::lround(value)));
    return buffer;
  }
  std::snprintf(buffer, sizeof(buffer), "%.2f", value);
  std::string out = buffer;
  while (out.size() > 1 && out.back() == '0') out.pop_back();
  if (!out.empty() && out.back() == '.') out.pop_back();
  return out;
}

std::string enumLabel(std::string const& value) {
  std::string out;
  bool capitalizeNext = true;
  out.reserve(value.size());
  for (unsigned char ch : value) {
    if (ch == '_' || ch == '-') {
      out.push_back(' ');
      capitalizeNext = true;
      continue;
    }
    out.push_back(capitalizeNext ? static_cast<char>(std::toupper(ch)) : static_cast<char>(ch));
    capitalizeNext = false;
  }
  return out;
}

int enumIndexForValue(std::vector<std::string> const& values, std::string const& value) {
  auto found = std::find(values.begin(), values.end(), value);
  return found == values.end() ? -1 : static_cast<int>(std::distance(values.begin(), found));
}

std::optional<NumericControlSpec> numericSpecForSetting(SettingSchema const& schema) {
  if (schema.id == "scale") return NumericControlSpec{.min = 1.f, .max = 3.f, .step = 0.25f, .slider = true};
  if (schema.id == "cursor_size") return NumericControlSpec{.min = 16.f, .max = 64.f, .step = 1.f, .slider = true};
  if (schema.id == "dock.item_size") return NumericControlSpec{.min = 32.f, .max = 96.f, .step = 1.f, .slider = true};
  if (schema.id == "dock.blur_radius") return NumericControlSpec{.min = 0.f, .max = 160.f, .step = 1.f, .slider = true};
  if (schema.id == "dock.opacity") return NumericControlSpec{.min = 0.f, .max = 1.f, .step = 0.05f, .slider = true};
  if (schema.id == "input.keyboard.repeat_rate") return NumericControlSpec{.min = 10.f, .max = 80.f, .step = 1.f, .slider = true};
  if (schema.id == "input.keyboard.repeat_delay_ms") {
    return NumericControlSpec{.min = 150.f, .max = 1000.f, .step = 50.f, .slider = true};
  }
  if (schema.id == "idle_blank_timeout_seconds") {
    return NumericControlSpec{.min = 0.f, .max = 3600.f, .step = 60.f, .slider = true};
  }
  if (schema.id == "notifications.banner_timeout_seconds") {
    return NumericControlSpec{.min = 1.f, .max = 30.f, .step = 1.f, .slider = true};
  }
  if (schema.id == "notifications.history_limit" || schema.id == "clipboard_history.max_entries") {
    return NumericControlSpec{.min = 10.f, .max = 1000.f, .step = 10.f, .slider = true};
  }
  if (schema.id == "clipboard_history.max_text_bytes") {
    return NumericControlSpec{.min = 1024.f, .max = 4.f * 1024.f * 1024.f, .step = 1024.f, .slider = true};
  }
  if (schema.id == "launcher.max_results") return NumericControlSpec{.min = 4.f, .max = 32.f, .step = 1.f, .slider = true};
  return std::nullopt;
}

std::optional<Color> parseHexColor(std::string const& value) {
  if (!validateSettingValue(SettingSchema{.type = SettingType::Color}, value)) return std::nullopt;
  unsigned int rgb = 0;
  for (std::size_t i = 1; i < 7; ++i) {
    unsigned char const ch = static_cast<unsigned char>(value[i]);
    rgb <<= 4u;
    if (ch >= '0' && ch <= '9') rgb += ch - '0';
    else if (ch >= 'a' && ch <= 'f') rgb += ch - 'a' + 10u;
    else if (ch >= 'A' && ch <= 'F') rgb += ch - 'A' + 10u;
  }
  return Color::hex(rgb);
}

ShadowStyle subtleShadow() {
  return ShadowStyle{
      .radius = 22.f,
      .offset = {0.f, 2.f},
      .color = Color{0.05f, 0.10f, 0.18f, 0.04f},
  };
}

struct SidebarItemRow {
  SidebarItem item;
  Reactive::Signal<SettingsSection> activeSection;

  Element body() const {
    auto hover = useHover();
    Reactive::Signal<SettingsSection> const activeSignal = activeSection;
    SettingsSection const rowSection = item.section;
    Reactive::Bindable<bool> const active{[activeSignal, rowSection] {
      return activeSignal() == rowSection;
    }};
    Reactive::Bindable<FillStyle> const fill{[hover, active] {
      if (active.evaluate()) {
        return FillStyle::solid(Color::selectedContentBackground());
      }
      if (hover()) {
        return FillStyle::solid(Color::windowBackground());
      }
      return FillStyle::solid(Colors::transparent);
    }};
    Reactive::Bindable<Color> const labelColor{[active] {
      return active.evaluate() ? Color::accent() : Color::secondary();
    }};
    Reactive::Bindable<Color> const iconColor{[active] {
      return active.evaluate() ? Color::accent() : Color::tertiary();
    }};

    return HStack{
               .spacing = 8.f,
               .alignment = Alignment::Center,
               .children = children(
                   Icon{.name = item.icon, .size = 18.f, .weight = 400.f, .color = iconColor},
                   Text{
                       .text = item.label,
                       .font = Font::headline(),
                       .color = labelColor,
                   })}
        .padding(6.f, 10.f, 6.f, 10.f)
        .fill(fill)
        .cornerRadius(SettingsTheme::kSideItemRadius)
        .onTap([activeSignal, rowSection] { activeSignal.set(rowSection); });
  }
};

Element sidebarHeader(WindowChromeMetrics const& chrome) {
  ChromeInsets const reserved = chromeInsets(chrome);
  std::vector<Element> row;
  if (reserved.leading > 0.f) {
    row.push_back(Rectangle{}.width(reserved.leading));
  }
  row.push_back(Text{
      .text = "Settings",
      .font = Font::headline(),
      .color = Color::primary(),
      .verticalAlignment = VerticalAlignment::Center,
  });
  row.push_back(Spacer{}.flex(1.f, 1.f));
  return HStack{
             .spacing = 8.f,
             .alignment = Alignment::Center,
             .children = std::move(row)}
      .height(SettingsTheme::kSidebarHeaderHeight)
      .padding(0.f, SettingsTheme::kSidePad, 0.f, SettingsTheme::kSidePad)
      .windowDragRegion();
}

Element sidebar(Reactive::Signal<SettingsSection> activeSection, WindowChromeMetrics const& chrome) {
  std::vector<Element> rows;
  rows.push_back(sidebarHeader(chrome));
  for (SidebarItem item : sidebarItems()) {
    if (hasGroupGapBefore(item.section)) {
      rows.push_back(Rectangle{}.height(8.f).fill(Colors::transparent));
    }
    rows.push_back(Element{SidebarItemRow{.item = std::move(item), .activeSection = activeSection}});
  }
  rows.push_back(Spacer{}.flex(1.f, 1.f));

  return VStack{
             .spacing = 1.f,
             .alignment = Alignment::Stretch,
             .children = std::move(rows)}
      .width(SettingsTheme::kSidebarWidth)
      .padding(SettingsTheme::kSidePad, SettingsTheme::kSidePad, SettingsTheme::kSidePad,
               SettingsTheme::kSidePad);
}

Element sectionTitle(std::string title) {
  return Text{
      .text = std::move(title),
      .font = Font::title3(),
      .color = Color::primary(),
      .horizontalAlignment = HorizontalAlignment::Leading,
  };
}

Element sectionHeading(std::string text) {
  return Text{
      .text = std::move(text),
      .font = Font::footnote(),
      .color = Color::tertiary(),
      .horizontalAlignment = HorizontalAlignment::Leading,
  };
}

Element selectPill(std::string value) {
  return HStack{
             .spacing = 6.f,
             .alignment = Alignment::Center,
             .children = children(
                 Text{
                     .text = std::move(value),
                     .font = Font::callout(),
                     .color = Color::primary(),
                 },
                 Icon{.name = IconName::ChevronRight, .size = 13.f, .color = Color::tertiary()})}
      .padding(5.f, 10.f, 5.f, 10.f)
      .fill(Color::controlBackground())
      .stroke(StrokeStyle::solid(Color::separator(), 1.f))
      .cornerRadius(7.f);
}

Element valueText(std::string value) {
  return Text{
      .text = std::move(value),
      .font = Font::callout(),
      .color = Color::secondary(),
      .verticalAlignment = VerticalAlignment::Center,
  };
}

Element settingsRow(std::string label, std::string sublabel, Element control);
Element rowsList(std::vector<Element> rows);
Element settingsCard(std::string heading, std::string caption, Element content);

enum class SettingsSource {
  WindowManager,
  Shell,
};

struct BoundSetting {
  SettingsSource source = SettingsSource::WindowManager;
  SettingSchema schema;
};

struct SettingsGroup {
  std::string heading;
  std::string caption;
  std::vector<BoundSetting> settings;
};

std::string applyModeText(ApplyMode mode) {
  switch (mode) {
  case ApplyMode::HotReload:
    return "Applies after Save";
  case ApplyMode::NextWindow:
    return "Applies to new windows";
  case ApplyMode::RestartRequired:
    return "Restart required";
  }
  return "Applies after Save";
}

std::string joinLabels(std::vector<std::string> const& labels) {
  std::string out;
  for (std::size_t index = 0; index < labels.size(); ++index) {
    if (index > 0) out += index + 1u == labels.size() ? " and " : ", ";
    out += labels[index];
  }
  return out;
}

std::string savedStatusText(SettingsApplySummary const& wmSummary,
                            SettingsApplySummary const& shellSummary) {
  std::vector<std::string> restartLabels = wmSummary.restartRequiredLabels;
  restartLabels.insert(restartLabels.end(),
                       shellSummary.restartRequiredLabels.begin(),
                       shellSummary.restartRequiredLabels.end());
  if (!restartLabels.empty()) {
    return "Saved; restart required for " + joinLabels(restartLabels);
  }
  if (wmSummary.hasNextWindow || shellSummary.hasNextWindow) {
    return "Saved; some changes apply to new windows";
  }
  if (wmSummary.hasHotReload || shellSummary.hasHotReload) {
    return "Saved; live settings will update shortly";
  }
  return "Saved";
}

std::string settingValue(std::map<std::string, std::string> const& values, SettingSchema const& schema) {
  auto found = values.find(schema.id);
  if (found != values.end()) return found->second;
  return schema.defaultValue;
}

std::optional<SettingSchema> findSchema(std::vector<SettingSchema> const& schema, std::string const& id) {
  auto found = std::find_if(schema.begin(), schema.end(), [&](SettingSchema const& item) {
    return item.id == id;
  });
  if (found == schema.end()) return std::nullopt;
  return *found;
}

BoundSetting bindSetting(SettingsSource source, std::vector<SettingSchema> const& schema, std::string const& id) {
  auto found = findSchema(schema, id);
  return BoundSetting{.source = source,
                      .schema = found ? *found : SettingSchema{.id = id, .label = id}};
}

std::vector<SettingSchema> const& cachedWindowManagerSettingsSchema() {
  static std::vector<SettingSchema> const schema = windowManagerSettingsSchema();
  return schema;
}

std::vector<SettingSchema> const& cachedShellSettingsSchema() {
  static std::vector<SettingSchema> const schema = shellSettingsSchema();
  return schema;
}

std::map<std::string, std::string> const& cachedWindowManagerSchemaDefaults() {
  static std::map<std::string, std::string> const defaults =
      schemaDefaults(cachedWindowManagerSettingsSchema());
  return defaults;
}

std::map<std::string, std::string> const& cachedShellSchemaDefaults() {
  static std::map<std::string, std::string> const defaults =
      schemaDefaults(cachedShellSettingsSchema());
  return defaults;
}

void setSettingValue(Reactive::Signal<std::map<std::string, std::string>> values,
                     std::string id,
                     std::string value) {
  auto next = values.peek();
  next[std::move(id)] = std::move(value);
  values.set(std::move(next));
}

TextInput::Style settingsTextInputStyle() {
  TextInput::Style inputStyle;
  inputStyle.font = Font::callout();
  inputStyle.textColor = Color::primary();
  inputStyle.placeholderColor = Color::tertiary();
  inputStyle.backgroundColor = Color::controlBackground();
  inputStyle.borderColor = Color::separator();
  inputStyle.borderFocusColor = Color::accent();
  inputStyle.caretColor = Color::accent();
  inputStyle.selectionColor = Color::selectedContentBackground();
  inputStyle.cornerRadius = 7.f;
  inputStyle.paddingH = 10.f;
  inputStyle.paddingV = 5.f;
  inputStyle.height = 32.f;
  return inputStyle;
}

Toggle::Style settingsToggleStyle() {
  return Toggle::Style{
      .trackWidth = 42.f,
      .trackHeight = 24.f,
      .thumbInset = 3.f,
      .borderWidth = 1.f,
      .thumbBorderWidth = 1.f,
      .onColor = Color::accent(),
      .offColor = Color::separator(),
      .thumbColor = Colors::white,
      .thumbBorderColor = Color{0.f, 0.f, 0.f, 0.08f},
      .borderColor = Color::separator(),
  };
}

Slider::Style settingsSliderStyle() {
  return Slider::Style{
      .activeColor = Color::accent(),
      .inactiveColor = Color::separator(),
      .thumbColor = Colors::white,
      .thumbBorderColor = Color{0.f, 0.f, 0.f, 0.08f},
      .trackHeight = 3.f,
      .thumbSize = 16.f,
  };
}

SegmentedControl::Style settingsSegmentStyle() {
  return SegmentedControl::Style{
      .font = Font::headline(),
      .paddingH = 10.f,
      .paddingV = 5.f,
      .cornerRadius = 7.f,
      .accentColor = Color::accent(),
      .trackColor = Color::controlBackground(),
      .borderColor = Color::separator(),
  };
}

Select::Style settingsSelectStyle() {
  Select::Style style;
  style.labelFont = Font::callout();
  style.detailFont = Font::footnote();
  style.cornerRadius = 7.f;
  style.menuCornerRadius = 8.f;
  style.menuMaxHeight = 260.f;
  style.minMenuWidth = 180.f;
  style.accentColor = Color::accent();
  style.fieldColor = Color::controlBackground();
  style.fieldHoverColor = Color::windowBackground();
  style.borderColor = Color::separator();
  style.rowHoverColor = Color::windowBackground();
  return style;
}

Element settingTextField(SettingSchema schema,
                         Reactive::Signal<std::string> localValue,
                         SettingsSource source,
                         std::function<void(SettingsSource, std::string, std::string)> setValue,
                         float width = 240.f) {
  return TextInput{
      .value = localValue,
      .placeholder = schema.defaultValue,
      .validationColor = [schema](std::string_view text) {
        return validateSettingValue(schema, std::string{text}) ? Color::accent() : Color::warning();
      },
      .style = settingsTextInputStyle(),
      .onChange = [localValue, schema = std::move(schema), source, set = std::move(setValue)](
                      std::string const& next) {
        localValue.set(next);
        if (set) {
          set(source, schema.id, next);
        }
      },
  }.width(width);
}

Element settingColorControl(SettingSchema schema,
                            Reactive::Signal<std::string> localValue,
                            SettingsSource source,
                            std::function<void(SettingsSource, std::string, std::string)> setValue) {
  Reactive::Bindable<Color> const swatchFill{[localValue] {
    return parseHexColor(localValue()).value_or(Color::controlBackground());
  }};
  return HStack{
             .spacing = 8.f,
             .alignment = Alignment::Center,
             .children = children(Rectangle{}
                                      .size(30.f, 30.f)
                                      .fill(swatchFill)
                                      .stroke(StrokeStyle::solid(Color::separator(), 1.f))
                                      .cornerRadius(7.f),
                                  settingTextField(std::move(schema), localValue, source, std::move(setValue), 144.f))}
      .width(184.f);
}

Element settingBooleanControl(SettingSchema schema,
                              Reactive::Signal<std::string> localValue,
                              Reactive::Signal<bool> localBool,
                              SettingsSource source,
                              std::function<void(SettingsSource, std::string, std::string)> setValue) {
  Reactive::Bindable<std::string> const label{[localBool] {
    return localBool() ? std::string{"On"} : std::string{"Off"};
  }};
  return HStack{
             .spacing = 8.f,
             .alignment = Alignment::Center,
                                  .children = children(Text{
                                      .text = label,
                                      .font = Font::callout(),
                                      .color = Color::secondary(),
                                      .horizontalAlignment = HorizontalAlignment::Trailing,
                                  }.width(28.f),
                                  Toggle{
                                      .value = localBool,
                                      .style = settingsToggleStyle(),
                                      .onChange = [localValue, localBool, schema = std::move(schema), source,
                                                   set = std::move(setValue)](bool next) {
                                        localBool.set(next);
                                        std::string const stored = next ? "true" : "false";
                                        localValue.set(stored);
                                        if (set) set(source, schema.id, stored);
                                      },
                                  })}
      .width(82.f);
}

Element settingEnumControl(SettingSchema schema,
                           Reactive::Signal<std::string> localValue,
                           Reactive::Signal<int> localIndex,
                           SettingsSource source,
                           std::function<void(SettingsSource, std::string, std::string)> setValue) {
  std::vector<SegmentedControlOption> segments;
  std::vector<SelectOption> options;
  segments.reserve(schema.enumValues.size());
  options.reserve(schema.enumValues.size());
  for (std::string const& value : schema.enumValues) {
    std::string label = enumLabel(value);
    segments.push_back(SegmentedControlOption{.label = label});
    options.push_back(SelectOption{.label = std::move(label), .detail = value});
  }
  auto commitIndex = [localValue, localIndex, schema, source, set = std::move(setValue)](int index) {
    if (index < 0 || index >= static_cast<int>(schema.enumValues.size())) return;
    std::string const stored = schema.enumValues[static_cast<std::size_t>(index)];
    localIndex.set(index);
    localValue.set(stored);
    if (set) set(source, schema.id, stored);
  };
  if (schema.enumValues.size() <= 3u) {
    return SegmentedControl{
        .selectedIndex = localIndex,
        .options = std::move(segments),
        .style = settingsSegmentStyle(),
        .onChange = commitIndex,
    }.width(220.f);
  }
  return Select{
      .selectedIndex = localIndex,
      .options = std::move(options),
      .placeholder = "Select",
      .style = settingsSelectStyle(),
      .onChange = commitIndex,
  }.width(220.f);
}

Element settingNumericControl(SettingSchema schema,
                              Reactive::Signal<std::string> localValue,
                              Reactive::Signal<float> localNumber,
                              SettingsSource source,
                              std::function<void(SettingsSource, std::string, std::string)> setValue) {
  bool const integer = schema.type == SettingType::Integer;
  std::optional<NumericControlSpec> spec = numericSpecForSetting(schema);
  if (!spec || !spec->slider) {
    return settingTextField(std::move(schema), localValue, source, std::move(setValue), 144.f);
  }
  Reactive::Bindable<std::string> const valueLabel{[localNumber, integer] {
    return formatSettingNumber(localNumber(), integer);
  }};
  return HStack{
             .spacing = 10.f,
             .alignment = Alignment::Center,
             .children = children(Slider{
                                      .value = localNumber,
                                      .min = spec->min,
                                      .max = spec->max,
                                      .step = spec->step,
                                      .style = settingsSliderStyle(),
                                      .onChange = [localValue, localNumber, schema, source, set = std::move(setValue),
                                                   integer](float next) {
                                        std::string const stored = formatSettingNumber(next, integer);
                                        localNumber.set(next);
                                        localValue.set(stored);
                                        if (set) set(source, schema.id, stored);
                                      },
                                  }.width(180.f),
                                  Text{
                                      .text = valueLabel,
                                      .font = Font::callout(),
                                      .color = Color::secondary(),
                                      .horizontalAlignment = HorizontalAlignment::Trailing,
                                  }.width(58.f))}
      .width(248.f);
}

struct SettingEditorRow {
  BoundSetting setting;
  Reactive::Signal<std::map<std::string, std::string>> wmValues;
  Reactive::Signal<std::map<std::string, std::string>> shellValues;
  std::function<void(SettingsSource, std::string, std::string)> setValue;

  Element body() const {
    Reactive::Signal<std::map<std::string, std::string>> const valuesSignal =
        setting.source == SettingsSource::WindowManager ? wmValues : shellValues;
    SettingSchema const schema = setting.schema;
    std::string const initialValue = settingValue(valuesSignal.peek(), schema);
    auto localValue = useState(initialValue);
    auto localBool = useState(settingBoolValue(initialValue));
    auto localNumber = useState(parseSettingFloat(initialValue).value_or(0.f));
    auto localIndex = useState(enumIndexForValue(schema.enumValues, initialValue));
    Reactive::Effect([valuesSignal, localValue, schema] {
      std::string const next = settingValue(valuesSignal(), schema);
      if (localValue.peek() != next) {
        localValue.set(next);
      }
    });
    Reactive::Effect([localValue, localBool, localNumber, localIndex, schema] {
      std::string const current = localValue();
      bool const nextBool = settingBoolValue(current);
      if (localBool.peek() != nextBool) localBool.set(nextBool);
      if (auto parsed = parseSettingFloat(current); parsed && localNumber.peek() != *parsed) {
        localNumber.set(*parsed);
      }
      int const nextIndex = enumIndexForValue(schema.enumValues, current);
      if (localIndex.peek() != nextIndex) localIndex.set(nextIndex);
    });

    Element control = [&]() -> Element {
      switch (schema.type) {
      case SettingType::Boolean:
        return settingBooleanControl(schema, localValue, localBool, setting.source, setValue);
      case SettingType::Enum:
        return settingEnumControl(schema, localValue, localIndex, setting.source, setValue);
      case SettingType::Integer:
      case SettingType::Float:
        return settingNumericControl(schema, localValue, localNumber, setting.source, setValue);
      case SettingType::Color:
        return settingColorControl(schema, localValue, setting.source, setValue);
      case SettingType::Path:
        return HStack{
                   .spacing = 8.f,
                   .alignment = Alignment::Center,
                   .children = children(Icon{.name = IconName::FolderOpen,
                                             .size = 17.f,
                                             .color = Color::tertiary()},
                                        settingTextField(schema, localValue, setting.source, setValue, 260.f))}
            .width(288.f);
      case SettingType::Shortcut:
        return HStack{
                   .spacing = 8.f,
                   .alignment = Alignment::Center,
                   .children = children(Icon{.name = IconName::Keyboard,
                                             .size = 17.f,
                                             .color = Color::tertiary()},
                                        settingTextField(schema, localValue, setting.source, setValue, 190.f))}
            .width(218.f);
      case SettingType::String:
      default:
        return settingTextField(schema, localValue, setting.source, setValue, 260.f);
      }
    }();

    return settingsRow(schema.label, applyModeText(schema.applyMode), std::move(control));
  }
};

std::vector<Element> buildSettingRows(std::vector<BoundSetting> settings,
                                      Reactive::Signal<std::map<std::string, std::string>> wmValues,
                                      Reactive::Signal<std::map<std::string, std::string>> shellValues,
                                      std::function<void(SettingsSource, std::string, std::string)> setValue) {
  std::vector<Element> rows;
  rows.reserve(settings.size());
  for (BoundSetting setting : settings) {
    rows.push_back(Element{SettingEditorRow{
        .setting = std::move(setting),
        .wmValues = wmValues,
        .shellValues = shellValues,
        .setValue = setValue,
    }});
  }
  return rows;
}

Element settingsPage(std::string title,
                     std::vector<SettingsGroup> groups,
                     Reactive::Signal<std::map<std::string, std::string>> wmValues,
                     Reactive::Signal<std::map<std::string, std::string>> shellValues,
                     std::function<void(SettingsSource, std::string, std::string)> setValue) {
  std::vector<Element> blocks;
  blocks.reserve(groups.size() + 1u);
  blocks.push_back(sectionTitle(std::move(title)));
  for (SettingsGroup group : groups) {
    blocks.push_back(settingsCard(
        std::move(group.heading),
        std::move(group.caption),
        rowsList(buildSettingRows(std::move(group.settings), wmValues, shellValues, setValue))));
  }
  return VStack{
      .spacing = 16.f,
      .alignment = Alignment::Stretch,
      .children = std::move(blocks),
  };
}

Element settingsActionBar(Reactive::Signal<std::map<std::string, std::string>> wmValues,
                          Reactive::Signal<std::map<std::string, std::string>> shellValues,
                          Reactive::Signal<std::map<std::string, std::string>> savedWmValues,
                          Reactive::Signal<std::map<std::string, std::string>> savedShellValues,
                          Reactive::Signal<std::string> statusText,
                          std::function<void()> save,
                          std::function<void()> revert,
                          std::function<void()> reset) {
  Reactive::Bindable<bool> const dirty{[wmValues, shellValues, savedWmValues, savedShellValues] {
    return wmValues() != savedWmValues() || shellValues() != savedShellValues();
  }};
  Reactive::Bindable<std::string> const status{[dirty, statusText] {
    if (!statusText().empty()) return statusText();
    return dirty.evaluate() ? std::string{"Unsaved changes"} : std::string{"Saved"};
  }};
  Reactive::Bindable<Color> const statusColor{[dirty] {
    return dirty.evaluate() ? Color::primary() : Color::secondary();
  }};

  Button::Style buttonStyle;
  buttonStyle.font = Font::callout();
  buttonStyle.cornerRadius = 7.f;
  buttonStyle.paddingH = 12.f;
  buttonStyle.paddingV = 6.f;
  buttonStyle.accentColor = Color::accent();

  Button::Style resetButtonStyle = buttonStyle;
  resetButtonStyle.accentColor = Color::secondary();

  return HStack{
             .spacing = 10.f,
             .alignment = Alignment::Center,
             .children = children(
                 Text{
                     .text = status,
	                     .font = Font::callout(),
	                     .color = statusColor,
	                     .horizontalAlignment = HorizontalAlignment::Leading,
	                     .verticalAlignment = VerticalAlignment::Center,
                         .wrapping = TextWrapping::Wrap,
	                 }.flex(1.f, 1.f, 0.f),
                 Button{.label = std::string{"Reset Defaults"},
                        .variant = ButtonVariant::Ghost,
                        .style = resetButtonStyle,
                        .onTap = std::move(reset)},
                 Button{.label = std::string{"Revert"},
                        .variant = ButtonVariant::Secondary,
                        .disabled = [dirty] { return !dirty.evaluate(); },
                        .style = buttonStyle,
                        .onTap = std::move(revert)},
                 Button{.label = std::string{"Save Changes"},
                        .variant = ButtonVariant::Primary,
                        .disabled = [dirty] { return !dirty.evaluate(); },
                        .style = buttonStyle,
                        .onTap = std::move(save)})}
      .padding(10.f, SettingsTheme::kMainPadH, 10.f, SettingsTheme::kMainPadH)
      .fill(Color::windowBackground())
      .stroke(StrokeStyle::solid(Color::separator(), 1.f));
}

struct SettingsRowView {
  std::string label;
  std::string sublabel;
  Element control;

  static constexpr float kCompactWidth = 560.f;

  static bool compactFor(LayoutConstraints const& constraints) {
    return std::isfinite(constraints.maxWidth) && constraints.maxWidth > 0.f &&
           constraints.maxWidth < kCompactWidth;
  }

  Element labelBlock(Theme const& theme) const {
    std::vector<Element> labelChildren;
    labelChildren.push_back(Text{
        .text = label,
        .font = theme.headlineFont,
        .color = Color::primary(),
        .horizontalAlignment = HorizontalAlignment::Leading,
    });
    if (!sublabel.empty()) {
      labelChildren.push_back(Text{
          .text = sublabel,
          .font = theme.calloutFont,
          .color = Color::secondary(),
          .horizontalAlignment = HorizontalAlignment::Leading,
          .wrapping = TextWrapping::Wrap,
      });
    }
    return VStack{
        .spacing = theme.space1,
        .alignment = Alignment::Stretch,
        .children = std::move(labelChildren)};
  }

  Element rowElement(Theme const& theme, bool compact) const {
    if (compact) {
      return VStack{
                 .spacing = theme.space2,
                 .alignment = Alignment::Stretch,
                 .children = children(labelBlock(theme), control)}
          .padding(theme.space3)
          .fill(FillStyle::solid(Color::windowBackground()))
          .cornerRadius(CornerRadius{theme.radiusMedium});
    }
    return HStack{
               .spacing = theme.space4,
               .alignment = Alignment::Center,
               .children = children(labelBlock(theme).flex(1.f, 1.f, 0.f), control)}
        .padding(theme.space3)
        .fill(FillStyle::solid(Color::windowBackground()))
        .cornerRadius(CornerRadius{theme.radiusMedium});
  }

  Size measure(MeasureContext& ctx,
               LayoutConstraints const& constraints,
               LayoutHints const& hints,
               TextSystem& textSystem) const {
    Theme const theme = ctx.environmentBinding().value<ThemeKey>();
    return rowElement(theme, compactFor(constraints)).measure(ctx, constraints, hints, textSystem);
  }

  std::unique_ptr<scenegraph::SceneNode> mount(MountContext& ctx) const {
    Theme const theme = ctx.environmentBinding().value<ThemeKey>();
    auto group = std::make_unique<scenegraph::SceneNode>();
    auto* rawGroup = group.get();
    auto compact = std::make_shared<bool>(compactFor(ctx.constraints()));
    SettingsRowView row = *this;

    auto mountChild = [row, theme](MountContext& childCtx, bool compactMode) mutable {
      Element rowTree = row.rowElement(theme, compactMode);
      return rowTree.mount(childCtx);
    };

    MountContext childCtx = ctx.childWithSharedScope(ctx.constraints(), ctx.hints());
    if (auto child = mountChild(childCtx, *compact)) {
      rawGroup->setSize(child->size());
      rawGroup->appendChild(std::move(child));
    }

    rawGroup->setLayoutConstraints(ctx.constraints());
    rawGroup->setRelayout([rawGroup,
                           compact,
                           row = std::move(row),
                           theme,
                           textSystem = &ctx.textSystem(),
                           owner = &ctx.owner(),
                           environment = ctx.environmentBinding(),
                           hints = ctx.hints(),
                           requestRedraw = ctx.redrawCallback()](LayoutConstraints const& constraints) mutable {
      bool const nextCompact = SettingsRowView::compactFor(constraints);
      auto children = rawGroup->children();
      if (nextCompact != *compact) {
        *compact = nextCompact;
        MeasureContext measureContext{*textSystem, environment};
        MountContext rebuildCtx{*owner, *textSystem, measureContext, constraints, hints,
                                requestRedraw, environment};
        Element rowTree = row.rowElement(theme, nextCompact);
        auto rebuilt = Reactive::withOwner(*owner, [&] {
          return rowTree.mount(rebuildCtx);
        });
        if (rebuilt) {
          rawGroup->replaceChildren({});
          rawGroup->setSize(rebuilt->size());
          rawGroup->appendChild(std::move(rebuilt));
        }
        return;
      }

      if (!children.empty() && children[0]) {
        children[0]->relayout(constraints);
        rawGroup->setSize(children[0]->size());
      }
    });

    return group;
  }
};

Element settingsRow(std::string label, std::string sublabel, Element control) {
  return Element{SettingsRowView{
      .label = std::move(label),
      .sublabel = std::move(sublabel),
      .control = std::move(control),
  }};
}

Element rowsList(std::vector<Element> rows) {
  return VStack{
      .spacing = 8.f,
      .alignment = Alignment::Stretch,
      .children = std::move(rows),
  };
}

Element settingsCard(std::string heading, std::string caption, Element content) {
  std::vector<Element> cardChildren;
  cardChildren.push_back(Text{
      .text = std::move(heading),
      .font = Font::headline(),
      .color = Color::primary(),
      .horizontalAlignment = HorizontalAlignment::Leading,
  });
  if (!caption.empty()) {
    cardChildren.push_back(Text{
        .text = std::move(caption),
        .font = Font::callout(),
        .color = Color::secondary(),
        .horizontalAlignment = HorizontalAlignment::Leading,
        .wrapping = TextWrapping::Wrap,
    });
  }
  cardChildren.push_back(std::move(content));

  return Card{
      .child = VStack{
          .spacing = 12.f,
          .alignment = Alignment::Stretch,
          .children = std::move(cardChildren),
      },
      .style = Card::Style{
          .padding = 16.f,
          .cornerRadius = 8.f,
      },
  };
}

Element genericPage(std::string title, std::vector<SettingsRowValue> rows) {
  std::vector<Element> rowViews;
  rowViews.reserve(rows.size());
  for (auto& row : rows) {
    rowViews.push_back(settingsRow(std::move(row.label), "", selectPill(std::move(row.value))));
  }

  return VStack{
      .spacing = 16.f,
      .alignment = Alignment::Stretch,
      .children = children(sectionTitle(std::move(title)),
                           settingsCard("Configuration",
                                        "Settings writes owner config files and preserves unknown keys where practical.",
                                        rowsList(std::move(rowViews)))),
  };
}

Element aboutPage() {
  static std::vector<std::pair<std::string, std::string>> const rows =
      systemInfoRows(loadSystemInfo());
  std::vector<Element> rowElements;
  rowElements.reserve(rows.size());
  for (auto const& [label, value] : rows) {
    rowElements.push_back(settingsRow(label, "", valueText(value)));
  }

  Element logo = ZStack{
                     .horizontalAlignment = Alignment::Center,
                     .verticalAlignment = Alignment::Center,
                     .children = children(
                         Rectangle{}
                             .size(64.f, 64.f)
                             .fill(FillStyle::linearGradient(Color::hex(0x5AA2FF), Color::accent(),
                                                             {0.f, 0.f}, {1.f, 1.f}))
                             .cornerRadius(18.f)
                             .shadow(subtleShadow()),
                         HStack{
                             .spacing = 2.f,
                             .alignment = Alignment::Center,
                             .children = children(
                                 Icon{.name = IconName::ChangeHistory,
                                      .size = 25.f,
                                      .weight = 500.f,
                                      .color = Colors::white},
                                 Icon{.name = IconName::ChangeHistory,
                                      .size = 25.f,
                                      .weight = 500.f,
                                      .color = Colors::white})})}
                     .size(64.f, 64.f);

  return VStack{
      .spacing = 16.f,
      .alignment = Alignment::Stretch,
      .children = children(
          sectionTitle("About"),
          HStack{
              .spacing = 18.f,
              .alignment = Alignment::Center,
              .children = children(
                  std::move(logo),
                  VStack{
                      .spacing = 2.f,
                      .alignment = Alignment::Stretch,
                      .children = children(
                          Text{
                              .text = "Lambda",
                              .font = Font::title2(),
                              .color = Color::primary(),
                          },
                          Text{
                              .text = "Desktop Environment",
                              .font = Font::body(),
                              .color = Color::secondary(),
                          },
                          Text{
                              .text = "Version 1.0.0 · Build 2026.05.24",
                              .font = Font::callout(),
                              .color = Color::tertiary(),
                          })})},
          settingsCard("System", "", rowsList(std::move(rowElements))))};
}

Element contentForSection(SettingsSection section,
                          Reactive::Signal<std::map<std::string, std::string>> wmValues,
                          Reactive::Signal<std::map<std::string, std::string>> shellValues,
                          std::function<void(SettingsSource, std::string, std::string)> setValue) {
  std::vector<SettingSchema> const& wmSchema = cachedWindowManagerSettingsSchema();
  std::vector<SettingSchema> const& shellSchema = cachedShellSettingsSchema();
  auto wm = [&](std::string const& id) {
    return bindSetting(SettingsSource::WindowManager, wmSchema, id);
  };
  auto shell = [&](std::string const& id) {
    return bindSetting(SettingsSource::Shell, shellSchema, id);
  };

  switch (section) {
  case SettingsSection::General:
    return genericPage("General", {{"Window Manager config", windowManagerSettingsPath().string()},
                                   {"Shell config", shellSettingsPath().string()},
                                   {"Settings writes", "Owner config files"},
                                   {"Unknown keys", "Preserved where practical"}});
  case SettingsSection::Appearance:
    return settingsPage("Appearance",
                        {SettingsGroup{.heading = "Desktop Background",
                                       .caption = "Window Manager owned; applies after Save.",
                                       .settings = {wm("background"),
                                                    wm("wallpaper"),
                                                    wm("wallpaper_mode")}},
                         SettingsGroup{.heading = "Icons",
                                       .caption = "Shell owned; applies after Save.",
                                       .settings = {shell("appearance.icon_theme"),
                                                    shell("appearance.symbolic_icon_theme")}},
                         SettingsGroup{.heading = "Motion",
                                       .caption = "Shell owned; applies after Save.",
                                       .settings = {shell("appearance.reduced_motion")}}},
                        wmValues,
                        shellValues,
                        setValue);
  case SettingsSection::Display:
    return settingsPage("Display",
                        {SettingsGroup{.heading = "Output",
                                       .caption = "Window Manager owned; changing output requires restart.",
                                       .settings = {wm("output")}},
                         SettingsGroup{.heading = "Scaling",
                                       .caption = "Window Manager owned; applies after Save.",
                                       .settings = {wm("scale")}}},
                        wmValues,
                        shellValues,
                        setValue);
  case SettingsSection::Input:
    return settingsPage("Input",
                        {SettingsGroup{.heading = "Keyboard",
                                       .caption = "Window Manager owned; applies after Save.",
                                       .settings = {wm("input.keyboard.layout"),
                                                    wm("input.keyboard.repeat_rate"),
                                                    wm("input.keyboard.repeat_delay_ms")}},
                         SettingsGroup{.heading = "Shortcuts",
                                       .caption = "Window Manager owned; applies after Save.",
                                       .settings = {wm("keybindings.close")}}},
                        wmValues,
                        shellValues,
                        setValue);
  case SettingsSection::Windows:
    return settingsPage("Windows",
                        {SettingsGroup{.heading = "Behavior",
                                       .caption = "Window Manager owned; applies after Save.",
                                       .settings = {wm("animations"),
                                                    wm("idle_blank_timeout_seconds")}},
                         SettingsGroup{.heading = "Cursor",
                                       .caption = "Window Manager owned; applies after Save.",
                                       .settings = {wm("hardware_cursor"),
                                                    wm("cursor_theme"),
                                                    wm("cursor_size")}},
                         SettingsGroup{.heading = "Screenshots",
                                       .caption = "Window Manager owned; applies after Save.",
                                       .settings = {wm("keybindings.screenshot"),
                                                    wm("keybindings.screenshot_region"),
                                                    wm("keybindings.screenshot_active_window")}}},
                        wmValues,
                        shellValues,
                        setValue);
  case SettingsSection::DockPanel:
    return settingsPage("Dock & Panel",
                        {SettingsGroup{.heading = "Position",
                                       .caption = "Shell owned; applies after Save.",
                                       .settings = {shell("dock.position")}},
                         SettingsGroup{.heading = "Behavior",
                                       .caption = "Shell owned; applies after Save.",
                                       .settings = {shell("dock.auto_hide"),
                                                    shell("dock.full_width"),
                                                    shell("dock.show_running_unpinned"),
                                                    shell("dock.show_tooltips")}},
                         SettingsGroup{.heading = "Sizing",
                                       .caption = "Shell owned; applies after Save.",
                                       .settings = {shell("dock.item_size"),
                                                    shell("dock.bottom_gap"),
                                                    shell("dock.corner_radius")}},
                         SettingsGroup{.heading = "Material",
                                       .caption = "Shell owned; applies after Save.",
                                       .settings = {shell("dock.blur_radius"),
                                                    shell("dock.opacity"),
                                                    shell("dock.base_color"),
                                                    shell("dock.tint_color"),
                                                    shell("dock.border_color")}},
                         SettingsGroup{.heading = "Content",
                                       .caption = "Shell owned; applies after Save.",
                                       .settings = {shell("dock.pinned"),
                                                    shell("dock.clock_format"),
                                                    shell("quick_settings.modules")}}},
                        wmValues,
                        shellValues,
                        setValue);
  case SettingsSection::Notifications:
    return settingsPage("Notifications",
                        {SettingsGroup{.heading = "Banners",
                                       .caption = "Shell owned; applies after Save.",
                                       .settings = {shell("notifications.enabled"),
                                                    shell("notifications.do_not_disturb"),
                                                    shell("notifications.banner_timeout_seconds"),
                                                    shell("notifications.show_previews")}},
                         SettingsGroup{.heading = "History",
                                       .caption = "Shell owned; applies after Save.",
                                       .settings = {shell("notifications.history_limit")}}},
                        wmValues,
                        shellValues,
                        setValue);
  case SettingsSection::LauncherClipboard:
    return settingsPage("Launcher & Clipboard",
                        {SettingsGroup{.heading = "Launcher",
                                       .caption = "Shell owned; applies after Save.",
                                       .settings = {shell("launcher.empty_query"),
                                                    shell("launcher.max_results"),
                                                    shell("launcher.show_categories")}},
                         SettingsGroup{.heading = "Clipboard History",
                                       .caption = "Shell owned; applies after Save.",
                                       .settings = {shell("clipboard_history.enabled"),
                                                    shell("clipboard_history.persist"),
                                                    shell("clipboard_history.max_entries"),
                                                    shell("clipboard_history.max_text_bytes"),
                                                    shell("clipboard_history.record_primary_selection")}}},
                        wmValues,
                        shellValues,
                        setValue);
  case SettingsSection::About:
    return aboutPage();
  default:
    return aboutPage();
  }
}

} // namespace

struct SettingsAppRoot {
  Element body() const {
    auto activeSection = useState(SettingsSection::Appearance);
    auto wmLoad = useState(loadWindowManagerSettingsFile());
    auto shellLoad = useState(loadShellSettingsFile());
    auto wmValues = useState(wmLoad().document.values);
    auto shellValues = useState(shellLoad().document.values);
    auto savedWmValues = useState(wmLoad().document.values);
    auto savedShellValues = useState(shellLoad().document.values);
    auto statusText = useState([wmLoad, shellLoad] {
      if (!wmLoad().error.empty()) return wmLoad().error;
      if (!shellLoad().error.empty()) return shellLoad().error;
      return std::string{};
    }());
    auto chrome = useEnvironment<WindowChromeMetricsKey>();
    WindowChromeMetrics const metrics = chrome();

    auto setValue = [wmValues, shellValues](SettingsSource source, std::string id, std::string value) {
      if (source == SettingsSource::WindowManager) {
        setSettingValue(wmValues, std::move(id), std::move(value));
      } else {
        setSettingValue(shellValues, std::move(id), std::move(value));
      }
    };

    auto revert = [wmValues, shellValues, savedWmValues, savedShellValues, statusText] {
      wmValues.set(savedWmValues());
      shellValues.set(savedShellValues());
      statusText.set(std::string{});
    };

    auto reset = [wmValues, shellValues, statusText] {
      wmValues.set(cachedWindowManagerSchemaDefaults());
      shellValues.set(cachedShellSchemaDefaults());
      statusText.set("Defaults staged");
    };

    auto save = [wmLoad, shellLoad, wmValues, shellValues, savedWmValues, savedShellValues, statusText] {
      SettingsApplySummary const wmApplySummary =
          summarizeChangedApplyModes(savedWmValues(), wmValues(), cachedWindowManagerSettingsSchema());
      SettingsApplySummary const shellApplySummary =
          summarizeChangedApplyModes(savedShellValues(), shellValues(), cachedShellSettingsSchema());
      auto wmResult = saveWindowManagerSettingsFile(wmValues(), wmLoad().path);
      auto shellResult = saveShellSettingsFile(shellValues(), shellLoad().path);
      if (!wmResult.ok || !shellResult.ok) {
        std::string error;
        if (!wmResult.ok) {
          error += "Window Manager: " + wmResult.error;
        }
        if (!shellResult.ok) {
          if (!error.empty()) error += " ";
          error += "Shell: " + shellResult.error;
        }
        statusText.set(error.empty() ? std::string{"Save failed"} : error);
        return;
      }
      savedWmValues.set(wmValues());
      savedShellValues.set(shellValues());
      statusText.set(savedStatusText(wmApplySummary, shellApplySummary));
    };

    return VStack{
               .spacing = 0.f,
               .alignment = Alignment::Stretch,
               .children = children(
                   HStack{
                       .spacing = 0.f,
                       .alignment = Alignment::Stretch,
                       .children = children(
                           sidebar(activeSection, metrics),
                           Rectangle{}.width(1.f).fill(Color::separator()),
	                           VStack{
	                               .spacing = 0.f,
	                               .alignment = Alignment::Stretch,
	                               .children = children(
	                                   ScrollView{
	                                       .axis = ScrollAxis::Vertical,
	                                       .dragScrollEnabled = true,
                                       .children = children(Element{Switch(
                                           [activeSection] { return activeSection(); },
                                           {
                                               Case(SettingsSection::General, [=] {
                                                 return contentForSection(SettingsSection::General,
                                                                          wmValues, shellValues, setValue);
                                               }),
                                               Case(SettingsSection::Appearance, [=] {
                                                 return contentForSection(SettingsSection::Appearance,
                                                                          wmValues, shellValues, setValue);
                                               }),
                                               Case(SettingsSection::Display, [=] {
                                                 return contentForSection(SettingsSection::Display,
                                                                          wmValues, shellValues, setValue);
                                               }),
                                               Case(SettingsSection::Input, [=] {
                                                 return contentForSection(SettingsSection::Input,
                                                                          wmValues, shellValues, setValue);
                                               }),
                                               Case(SettingsSection::Windows, [=] {
                                                 return contentForSection(SettingsSection::Windows,
                                                                          wmValues, shellValues, setValue);
                                               }),
                                               Case(SettingsSection::DockPanel, [=] {
                                                 return contentForSection(SettingsSection::DockPanel,
                                                                          wmValues, shellValues, setValue);
                                               }),
                                               Case(SettingsSection::Notifications, [=] {
                                                 return contentForSection(SettingsSection::Notifications,
                                                                          wmValues, shellValues, setValue);
                                               }),
                                               Case(SettingsSection::LauncherClipboard, [=] {
                                                 return contentForSection(SettingsSection::LauncherClipboard,
                                                                          wmValues, shellValues, setValue);
                                               }),
                                               Case(SettingsSection::About, [] { return aboutPage(); }),
	                                           })}.padding(SettingsTheme::kMainPadV, SettingsTheme::kMainPadH,
	                                                       SettingsTheme::kMainPadV, SettingsTheme::kMainPadH))}
	                                       .flex(1.f, 1.f, 0.f),
	                                   settingsActionBar(wmValues, shellValues, savedWmValues, savedShellValues,
	                                                     statusText, save, revert, reset))}
	                               .flex(1.f, 1.f, 0.f))}
	                       .flex(1.f, 1.f, 0.f))};
  }
};

} // namespace lambda_settings
