#pragma once

#include "SettingsBackend.hpp"
#include "SettingsTheme.hpp"

#include <Lambda.hpp>
#include <Lambda/Reactive/Effect.hpp>
#include <Lambda/UI/EnvironmentKeys.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/Views/Button.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Icon.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/ScrollView.hpp>
#include <Lambda/UI/Views/Show.hpp>
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
#include <cmath>
#include <cstdio>
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
  Keyboard,
  Desktop,
  DockPanel,
  Notifications,
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

std::array<SidebarItem, 8> const& sidebarItems() {
  static std::array<SidebarItem, 8> const items{{
      {SettingsSection::General, "General", IconName::Settings},
      {SettingsSection::Appearance, "Appearance", IconName::Palette},
      {SettingsSection::Display, "Display", IconName::Computer},
      {SettingsSection::Keyboard, "Keyboard", IconName::Keyboard},
      {SettingsSection::Desktop, "Desktop", IconName::Computer},
      {SettingsSection::DockPanel, "Dock & Panel", IconName::Dock},
      {SettingsSection::Notifications, "Notifications", IconName::Notifications},
      {SettingsSection::About, "About", IconName::Info},
  }};
  return items;
}

bool hasGroupGapBefore(SettingsSection section) {
  return section == SettingsSection::Notifications;
}

Color accentColor(int index) {
  switch (index) {
  case 1:
    return Color::hex(0xA374FF);
  case 2:
    return Color::hex(0x16B5A4);
  case 3:
    return Color::hex(0x7A5AFF);
  case 4:
    return Color::hex(0xE6486E);
  case 5:
    return Color::hex(0xFF8F3E);
  case 6:
    return Color::hex(0xFFC02E);
  default:
    return SettingsTheme::accent;
  }
}

std::string percentText(float value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::round(value)));
  return buffer;
}

std::string radiusLabel(float value) {
  if (value <= 8.f) {
    return "Small";
  }
  if (value <= 16.f) {
    return "Medium";
  }
  return "Large";
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
        return FillStyle::solid(SettingsTheme::selectFill);
      }
      if (hover()) {
        return FillStyle::solid(SettingsTheme::hoverFill);
      }
      return FillStyle::solid(Colors::transparent);
    }};
    Reactive::Bindable<Color> const labelColor{[active] {
      return active.evaluate() ? SettingsTheme::accent : SettingsTheme::text2;
    }};
    Reactive::Bindable<Color> const iconColor{[active] {
      return active.evaluate() ? SettingsTheme::accent : SettingsTheme::text3;
    }};

    return HStack{
               .spacing = 8.f,
               .alignment = Alignment::Center,
               .children = children(
                   Icon{.name = item.icon, .size = 18.f, .weight = 400.f, .color = iconColor},
                   Text{
                       .text = item.label,
                       .font = Font{.size = 13.f, .weight = 500.f},
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
      .font = Font{.size = 13.f, .weight = 600.f},
      .color = SettingsTheme::text,
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
      .font = Font{.size = 20.f, .weight = 600.f},
      .color = SettingsTheme::text,
      .horizontalAlignment = HorizontalAlignment::Leading,
  };
}

Element sectionHeading(std::string text) {
  return Text{
      .text = std::move(text),
      .font = Font{.size = 11.f, .weight = 600.f},
      .color = SettingsTheme::text3,
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
                     .font = Font{.size = 12.f, .weight = 400.f},
                     .color = SettingsTheme::text,
                 },
                 Icon{.name = IconName::ChevronRight, .size = 13.f, .color = SettingsTheme::text3})}
      .padding(5.f, 10.f, 5.f, 10.f)
      .fill(SettingsTheme::glassSoft)
      .stroke(StrokeStyle::solid(SettingsTheme::line, 1.f))
      .cornerRadius(7.f);
}

Element valueText(std::string value) {
  return Text{
      .text = std::move(value),
      .font = Font{.size = 12.f, .weight = 400.f},
      .color = SettingsTheme::text2,
      .verticalAlignment = VerticalAlignment::Center,
  };
}

Element settingsRow(std::string label, std::string sublabel, Element control);
Element rowsList(std::vector<Element> rows);

enum class SettingsSource {
  WindowManager,
  Shell,
};

struct BoundSetting {
  SettingsSource source = SettingsSource::WindowManager;
  SettingSchema schema;
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

struct SettingEditorRow {
  BoundSetting setting;
  Reactive::Signal<std::map<std::string, std::string>> wmValues;
  Reactive::Signal<std::map<std::string, std::string>> shellValues;
  std::function<void(SettingsSource, std::string, std::string)> setValue;

  Element body() const {
    Reactive::Signal<std::map<std::string, std::string>> const valuesSignal =
        setting.source == SettingsSource::WindowManager ? wmValues : shellValues;
    SettingSchema const schema = setting.schema;
    auto localValue = useState(settingValue(valuesSignal.peek(), schema));
    Reactive::Effect([valuesSignal, localValue, schema] {
      std::string const next = settingValue(valuesSignal(), schema);
      if (localValue.peek() != next) {
        localValue.set(next);
      }
    });

    TextInput::Style inputStyle;
    inputStyle.font = Font{.size = 12.f, .weight = 450.f};
    inputStyle.textColor = SettingsTheme::text;
    inputStyle.placeholderColor = SettingsTheme::text3;
    inputStyle.backgroundColor = SettingsTheme::glassSoft;
    inputStyle.borderColor = SettingsTheme::line;
    inputStyle.borderFocusColor = SettingsTheme::accent;
    inputStyle.caretColor = SettingsTheme::accent;
    inputStyle.selectionColor = SettingsTheme::selectFill;
    inputStyle.cornerRadius = 7.f;
    inputStyle.paddingH = 10.f;
    inputStyle.paddingV = 5.f;
    inputStyle.height = 32.f;

    return settingsRow(
        schema.label,
        applyModeText(schema.applyMode),
        TextInput{
            .value = localValue,
            .placeholder = schema.defaultValue,
            .validationColor = [schema](std::string_view text) {
              return validateSettingValue(schema, std::string{text}) ? SettingsTheme::accent
                                                                     : Color::warning();
            },
            .style = inputStyle,
            .onChange = [localValue, schema, source = setting.source, set = setValue](
                            std::string const& next) {
              localValue.set(next);
              if (set) {
                set(source, schema.id, next);
              }
            },
        }.width(240.f));
  }
};

Element settingsPage(std::string title,
                     std::vector<BoundSetting> settings,
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
  return VStack{
      .spacing = 16.f,
      .alignment = Alignment::Stretch,
      .children = children(sectionTitle(std::move(title)), rowsList(std::move(rows))),
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

  Button::Style buttonStyle;
  buttonStyle.font = Font{.size = 12.f, .weight = 650.f};
  buttonStyle.cornerRadius = 7.f;
  buttonStyle.paddingH = 12.f;
  buttonStyle.paddingV = 6.f;
  buttonStyle.accentColor = SettingsTheme::accent;

  return HStack{
             .spacing = 8.f,
             .alignment = Alignment::Center,
             .children = children(
                 Text{
                     .text = status,
                     .font = Font{.size = 12.f, .weight = 500.f},
                     .color = SettingsTheme::text2,
                     .verticalAlignment = VerticalAlignment::Center,
                 }.flex(1.f, 1.f, 0.f),
                 Button{.label = std::string{"Reset"},
                        .variant = ButtonVariant::Secondary,
                        .style = buttonStyle,
                        .onTap = std::move(reset)},
                 Button{.label = std::string{"Revert"},
                        .variant = ButtonVariant::Secondary,
                        .disabled = [dirty] { return !dirty.evaluate(); },
                        .style = buttonStyle,
                        .onTap = std::move(revert)},
                 Button{.label = std::string{"Save"},
                        .variant = ButtonVariant::Primary,
                        .disabled = [dirty] { return !dirty.evaluate(); },
                        .style = buttonStyle,
                        .onTap = std::move(save)})}
      .padding(8.f, SettingsTheme::kMainPadH, 8.f, SettingsTheme::kMainPadH)
      .fill(Color{1.f, 1.f, 1.f, 0.18f})
      .stroke(StrokeStyle::solid(SettingsTheme::line, 1.f));
}

Element settingsRow(std::string label, std::string sublabel, Element control) {
  std::vector<Element> labelChildren;
  labelChildren.push_back(Text{
      .text = std::move(label),
      .font = Font{.size = 13.f, .weight = 500.f},
      .color = SettingsTheme::text,
      .horizontalAlignment = HorizontalAlignment::Leading,
  });
  if (!sublabel.empty()) {
    labelChildren.push_back(Text{
        .text = std::move(sublabel),
        .font = Font{.size = 11.5f, .weight = 400.f},
        .color = SettingsTheme::text3,
        .horizontalAlignment = HorizontalAlignment::Leading,
        .wrapping = TextWrapping::Wrap,
    });
  }

  return HStack{
             .spacing = 18.f,
             .alignment = Alignment::Center,
             .children = children(
                 VStack{
                     .spacing = 2.f,
                     .alignment = Alignment::Stretch,
                     .children = std::move(labelChildren)}
                     .flex(1.f, 1.f, 0.f),
                 std::move(control))}
      .padding(8.f, 0.f, 8.f, 0.f);
}

Element rowDivider() {
  return Rectangle{}.height(1.f).fill(SettingsTheme::line2);
}

Element rowsList(std::vector<Element> rows) {
  std::vector<Element> childrenList;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    if (i > 0) {
      childrenList.push_back(rowDivider());
    }
    childrenList.push_back(std::move(rows[i]));
  }
  return VStack{
      .spacing = 0.f,
      .alignment = Alignment::Stretch,
      .children = std::move(childrenList),
  };
}

Element sectionBlock(std::string heading, Element content) {
  return VStack{
      .spacing = 10.f,
      .alignment = Alignment::Stretch,
      .children = children(sectionHeading(std::move(heading)), std::move(content)),
  };
}

enum class ThemePreview {
  Light,
  Auto,
  Dark,
};

FillStyle themePreviewFill(ThemePreview preview) {
  switch (preview) {
  case ThemePreview::Auto:
    return FillStyle::linearGradient(
        {{0.f, SettingsTheme::swatchLightTop},
         {0.49f, SettingsTheme::swatchLightBottom},
         {0.51f, SettingsTheme::swatchDarkTop},
         {1.f, SettingsTheme::swatchDarkBottom}},
        {0.f, 0.f}, {1.f, 1.f});
  case ThemePreview::Dark:
    return FillStyle::linearGradient(SettingsTheme::swatchDarkTop, SettingsTheme::swatchDarkBottom,
                                     {0.f, 0.f}, {1.f, 1.f});
  case ThemePreview::Light:
  default:
    return FillStyle::linearGradient(SettingsTheme::swatchLightTop, SettingsTheme::swatchLightBottom,
                                     {0.f, 0.f}, {1.f, 1.f});
  }
}

struct ThemeCard {
  int index = 0;
  std::string label;
  ThemePreview preview = ThemePreview::Light;
  Reactive::Signal<int> selectedIndex;

  Element body() const {
    Reactive::Signal<int> const selected = selectedIndex;
    int const cardIndex = index;
    Reactive::Bindable<StrokeStyle> const swatchStroke{[selected, cardIndex] {
      return selected() == cardIndex ? StrokeStyle::solid(SettingsTheme::accent, 2.f)
                                     : StrokeStyle::solid(SettingsTheme::line, 1.f);
    }};
    Reactive::Bindable<Color> const labelColor{[selected, cardIndex] {
      return selected() == cardIndex ? SettingsTheme::text : SettingsTheme::text2;
    }};
    Element inner = Rectangle{}
                        .fill(preview == ThemePreview::Dark
                                  ? Color{1.f, 1.f, 1.f, 0.10f}
                                  : Color{1.f, 1.f, 1.f, 0.55f})
                        .stroke(StrokeStyle::solid(preview == ThemePreview::Dark
                                                       ? Color{1.f, 1.f, 1.f, 0.12f}
                                                       : Color{1.f, 1.f, 1.f, 0.60f},
                                                   1.f))
                        .cornerRadius(6.f)
                        .padding(8.f, 18.f, 18.f, 8.f);

    return VStack{
               .spacing = 6.f,
               .alignment = Alignment::Center,
               .children = children(
                   ZStack{
                       .horizontalAlignment = Alignment::Start,
                       .verticalAlignment = Alignment::Start,
                       .children = children(
                           Rectangle{}.fill(themePreviewFill(preview)).cornerRadius(10.f),
                           std::move(inner))}
                       .height(84.f)
                       .stroke(swatchStroke)
                       .cornerRadius(10.f)
                       .clipContent(true),
                   Text{
                       .text = label,
                       .font = Font{.size = 12.f, .weight = 500.f},
                       .color = labelColor,
                       .horizontalAlignment = HorizontalAlignment::Center,
                   })}
        .onTap([selected, cardIndex] { selected.set(cardIndex); });
  }
};

Element themeCards(Reactive::Signal<int> themeMode) {
  return HStack{
      .spacing = 12.f,
      .alignment = Alignment::Start,
      .children = children(
          Element{ThemeCard{.index = 0,
                            .label = "Light",
                            .preview = ThemePreview::Light,
                            .selectedIndex = themeMode}}
              .flex(1.f, 1.f, 0.f),
          Element{ThemeCard{.index = 1,
                            .label = "Auto",
                            .preview = ThemePreview::Auto,
                            .selectedIndex = themeMode}}
              .flex(1.f, 1.f, 0.f),
          Element{ThemeCard{.index = 2,
                            .label = "Dark",
                            .preview = ThemePreview::Dark,
                            .selectedIndex = themeMode}}
              .flex(1.f, 1.f, 0.f)),
  };
}

struct AccentDot {
  int index = 0;
  Reactive::Signal<int> selectedIndex;

  Element body() const {
    Reactive::Signal<int> const selected = selectedIndex;
    int const dotIndex = index;
    Color const color = accentColor(index);
    Reactive::Bindable<StrokeStyle> const stroke{[selected, dotIndex, color] {
      return selected() == dotIndex ? StrokeStyle::solid(color, 2.f)
                                    : StrokeStyle::solid(Color{0.f, 0.f, 0.f, 0.08f}, 1.f);
    }};

    return ZStack{
               .horizontalAlignment = Alignment::Center,
               .verticalAlignment = Alignment::Center,
               .children = children(
                   Rectangle{}.size(26.f, 26.f).fill(color).stroke(stroke).cornerRadius(13.f).shadow(subtleShadow()),
                   Show(
                       [selected, dotIndex] { return selected() == dotIndex; },
                       [] {
                         return Icon{.name = IconName::Check, .size = 15.f, .weight = 700.f, .color = Colors::white};
                       },
                       [] { return Rectangle{}.size(0.f, 0.f); }))}
        .size(30.f, 30.f)
        .onTap([selected, dotIndex] { selected.set(dotIndex); });
  }
};

Element accentDots(Reactive::Signal<int> accentIndex) {
  return HStack{
      .spacing = 10.f,
      .alignment = Alignment::Center,
      .children = children(
          AccentDot{.index = 0, .selectedIndex = accentIndex},
          AccentDot{.index = 1, .selectedIndex = accentIndex},
          AccentDot{.index = 2, .selectedIndex = accentIndex},
          AccentDot{.index = 3, .selectedIndex = accentIndex},
          AccentDot{.index = 4, .selectedIndex = accentIndex},
          AccentDot{.index = 5, .selectedIndex = accentIndex},
          AccentDot{.index = 6, .selectedIndex = accentIndex}),
  };
}

FillStyle wallpaperFill(int index) {
  switch (index) {
  case 1:
    return FillStyle::linearGradient(
        {{0.f, Color::hex(0xE9EEF9)}, {0.35f, Color::hex(0xC8D8F5)},
         {0.65f, Color::hex(0x91B3EE)}, {1.f, Color::hex(0x6C95E4)}},
        {0.f, 0.f}, {1.f, 1.f});
  case 2:
    return FillStyle::linearGradient(Color::hex(0xB9C9E8), Color::hex(0x2A3550), {0.f, 0.f},
                                     {0.f, 1.f});
  case 3:
    return FillStyle::linearGradient(
        {{0.f, Color::hex(0xFFD9A8)}, {0.30f, Color::hex(0xF3A3B5)},
         {0.65f, Color::hex(0x8B7EC0)}, {1.f, Color::hex(0x2A2547)}},
        {0.f, 0.f}, {0.f, 1.f});
  case 4:
    return FillStyle::linearGradient(Color::hex(0x0B1024), Color::hex(0x0A0D22), {0.f, 0.f},
                                     {1.f, 1.f});
  case 0:
  default:
    return FillStyle::linearGradient(
        {{0.f, Color::hex(0xD6E8D5)}, {0.35f, Color::hex(0x88BBA0)},
         {0.70f, Color::hex(0x244F50)}, {1.f, Color::hex(0x132E37)}},
        {0.f, 0.f}, {1.f, 1.f});
  }
}

struct WallpaperCard {
  int index = 0;
  std::string label;
  Reactive::Signal<int> selectedIndex;

  Element body() const {
    Reactive::Signal<int> const selected = selectedIndex;
    int const cardIndex = index;
    Reactive::Bindable<StrokeStyle> const swatchStroke{[selected, cardIndex] {
      return selected() == cardIndex ? StrokeStyle::solid(SettingsTheme::accent, 2.f)
                                     : StrokeStyle::solid(SettingsTheme::line, 1.f);
    }};
    Reactive::Bindable<Color> const labelColor{[selected, cardIndex] {
      return selected() == cardIndex ? SettingsTheme::text : SettingsTheme::text2;
    }};
    return VStack{
               .spacing = 6.f,
               .alignment = Alignment::Center,
               .children = children(
                   Rectangle{}
                       .height(62.f)
                       .fill(wallpaperFill(index))
                       .stroke(swatchStroke)
                       .cornerRadius(8.f)
                       .clipContent(true),
                   Text{
                       .text = label,
                       .font = Font{.size = 12.f, .weight = 500.f},
                       .color = labelColor,
                       .horizontalAlignment = HorizontalAlignment::Center,
                   })}
        .onTap([selected, cardIndex] { selected.set(cardIndex); });
  }
};

Element wallpaperCards(Reactive::Signal<int> wallpaperIndex) {
  return HStack{
      .spacing = 10.f,
      .alignment = Alignment::Start,
      .children = children(
          Element{WallpaperCard{.index = 0, .label = "Leaves", .selectedIndex = wallpaperIndex}}
              .flex(1.f, 1.f, 0.f),
          Element{WallpaperCard{.index = 1, .label = "Wave", .selectedIndex = wallpaperIndex}}
              .flex(1.f, 1.f, 0.f),
          Element{WallpaperCard{.index = 2, .label = "Mountain", .selectedIndex = wallpaperIndex}}
              .flex(1.f, 1.f, 0.f),
          Element{WallpaperCard{.index = 3, .label = "Dusk", .selectedIndex = wallpaperIndex}}
              .flex(1.f, 1.f, 0.f),
          Element{WallpaperCard{.index = 4, .label = "Aurora", .selectedIndex = wallpaperIndex}}
              .flex(1.f, 1.f, 0.f)),
  };
}

Element appearancePage(Reactive::Signal<int> themeMode,
                       Reactive::Signal<int> accentIndex,
                       Reactive::Signal<int> wallpaperIndex,
                       Reactive::Signal<float> transparency,
                       Reactive::Signal<float> radius,
                       Reactive::Signal<bool> reduceMotion,
                       Reactive::Signal<bool> highContrast) {
  Slider::Style sliderStyle{
      .activeColor = SettingsTheme::accent,
      .inactiveColor = SettingsTheme::line,
      .thumbColor = Colors::white,
      .thumbBorderColor = Color{0.f, 0.f, 0.f, 0.08f},
      .trackHeight = 3.f,
      .thumbSize = 16.f,
  };
  Toggle::Style toggleStyle{
      .trackWidth = 42.f,
      .trackHeight = 24.f,
      .thumbInset = 3.f,
      .borderWidth = 1.f,
      .thumbBorderWidth = 1.f,
      .onColor = SettingsTheme::accent,
      .offColor = SettingsTheme::line,
      .thumbColor = Colors::white,
      .thumbBorderColor = Color{0.f, 0.f, 0.f, 0.08f},
      .borderColor = SettingsTheme::line,
  };

  return VStack{
      .spacing = 22.f,
      .alignment = Alignment::Stretch,
      .children = children(
          sectionTitle("Appearance"),
          sectionBlock("Theme", themeCards(themeMode)),
          sectionBlock("Accent Color", accentDots(accentIndex)),
          sectionBlock("Wallpaper", wallpaperCards(wallpaperIndex)),
          sectionBlock(
              "Effects",
              rowsList({
                  settingsRow(
                      "Transparency", "Adjust the level of blur and transparency",
                      HStack{
                          .spacing = 10.f,
                          .alignment = Alignment::Center,
                          .children = children(
                              Slider{
                                  .value = transparency,
                                  .min = 40.f,
                                  .max = 95.f,
                                  .step = 5.f,
                                  .style = sliderStyle,
                              }
                                  .width(200.f),
                              Text{
                                  .text = [transparency] { return percentText(transparency()); },
                                  .font = Font{.size = 12.f, .weight = 400.f},
                                  .color = SettingsTheme::text2,
                                  .horizontalAlignment = HorizontalAlignment::Trailing,
                              }
                                  .width(38.f))}),
                  settingsRow(
                      "Radius", "Adjust the rounding size of elements",
                      HStack{
                          .spacing = 10.f,
                          .alignment = Alignment::Center,
                          .children = children(
                              Slider{
                                  .value = radius,
                                  .min = 4.f,
                                  .max = 22.f,
                                  .step = 2.f,
                                  .style = sliderStyle,
                              }
                                  .width(200.f),
                              Text{
                                  .text = [radius] { return radiusLabel(radius()); },
                                  .font = Font{.size = 12.f, .weight = 400.f},
                                  .color = SettingsTheme::text2,
                                  .horizontalAlignment = HorizontalAlignment::Trailing,
                              }
                                  .width(62.f))}),
                  settingsRow("Reduce motion", "Use shorter, calmer transitions",
                              Toggle{.value = reduceMotion, .style = toggleStyle}),
                  settingsRow("High contrast", "Increase borders and text contrast",
                              Toggle{.value = highContrast, .style = toggleStyle}),
                  settingsRow("Font", "System UI typeface", selectPill("Helvetica Neue")),
              })),
          rowsList({settingsRow("About Lambda", "Desktop environment settings",
                                HStack{
                                    .spacing = 6.f,
                                    .alignment = Alignment::Center,
                                    .children = children(valueText("v1.0.0"),
                                                         Icon{.name = IconName::ChevronRight,
                                                              .size = 13.f,
                                                              .color = SettingsTheme::text3})})}))};
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
      .children = children(sectionTitle(std::move(title)), rowsList(std::move(rowViews))),
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
                             .fill(FillStyle::linearGradient(SettingsTheme::accent2, SettingsTheme::accent,
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
      .spacing = 18.f,
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
                              .font = Font{.size = 22.f, .weight = 600.f},
                              .color = SettingsTheme::text,
                          },
                          Text{
                              .text = "Desktop Environment",
                              .font = Font{.size = 13.f, .weight = 400.f},
                              .color = SettingsTheme::text2,
                          },
                          Text{
                              .text = "Version 1.0.0 · Build 2026.05.24",
                              .font = Font{.size = 12.f, .weight = 400.f},
                              .color = SettingsTheme::text3,
                          })})},
          rowsList(std::move(rowElements)))};
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
                        {wm("background"),
                         wm("wallpaper"),
                         wm("wallpaper_mode"),
                         shell("appearance.icon_theme"),
                         shell("appearance.symbolic_icon_theme"),
                         shell("appearance.icon_size"),
                         shell("appearance.reduced_motion")},
                        wmValues,
                        shellValues,
                        setValue);
  case SettingsSection::Display:
    return settingsPage("Display",
                        {wm("output"),
                         wm("scale")},
                        wmValues,
                        shellValues,
                        setValue);
  case SettingsSection::Keyboard:
    return settingsPage("Keyboard",
                        {wm("input.keyboard.layout"),
                         wm("input.keyboard.repeat_rate"),
                         wm("input.keyboard.repeat_delay_ms"),
                         wm("keybindings.close")},
                        wmValues,
                        shellValues,
                        setValue);
  case SettingsSection::Desktop:
    return settingsPage("Desktop",
                        {wm("animations"),
                         wm("hardware_cursor"),
                         wm("cursor_theme"),
                         wm("cursor_size"),
                         wm("idle_blank_timeout_seconds"),
                         wm("keybindings.screenshot"),
                         wm("keybindings.screenshot_region"),
                         wm("keybindings.screenshot_active_window")},
                        wmValues,
                        shellValues,
                        setValue);
  case SettingsSection::DockPanel:
    return settingsPage("Dock & Panel",
                        {shell("dock.position"),
                         shell("dock.auto_hide"),
                         shell("dock.show_running_unpinned"),
                         shell("dock.show_tooltips"),
                         shell("dock.pinned"),
                         shell("top_bar.clock_format"),
                         shell("top_bar.show_active_title"),
                         shell("top_bar.modules"),
                         shell("quick_settings.modules")},
                        wmValues,
                        shellValues,
                        setValue);
  case SettingsSection::Notifications:
    return settingsPage("Notifications",
                        {shell("notifications.enabled"),
                         shell("notifications.do_not_disturb"),
                         shell("notifications.banner_timeout_seconds"),
                         shell("notifications.history_limit"),
                         shell("notifications.show_previews"),
                         shell("clipboard_history.enabled"),
                         shell("clipboard_history.persist"),
                         shell("clipboard_history.max_entries"),
                         shell("clipboard_history.max_text_bytes"),
                         shell("clipboard_history.record_primary_selection")},
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
      statusText.set("Saved");
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
                           Rectangle{}.width(1.f).fill(SettingsTheme::line),
                           VStack{
                               .spacing = 0.f,
                               .alignment = Alignment::Stretch,
                               .children = children(
                                   settingsActionBar(wmValues, shellValues, savedWmValues, savedShellValues,
                                                     statusText, save, revert, reset),
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
                                               Case(SettingsSection::Keyboard, [=] {
                                                 return contentForSection(SettingsSection::Keyboard,
                                                                          wmValues, shellValues, setValue);
                                               }),
                                               Case(SettingsSection::Desktop, [=] {
                                                 return contentForSection(SettingsSection::Desktop,
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
                                               Case(SettingsSection::About, [] { return aboutPage(); }),
                                           })}.padding(SettingsTheme::kMainPadV, SettingsTheme::kMainPadH,
                                                       SettingsTheme::kMainPadV, SettingsTheme::kMainPadH))}
                                       .flex(1.f, 1.f, 0.f))}
                               .flex(1.f, 1.f, 0.f))}
                       .flex(1.f, 1.f, 0.f))};
  }
};

} // namespace lambda_settings
