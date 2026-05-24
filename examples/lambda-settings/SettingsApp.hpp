#pragma once

#include "SettingsTheme.hpp"

#include <Flux.hpp>
#include <Flux/UI/EnvironmentKeys.hpp>
#include <Flux/UI/Hooks.hpp>
#include <Flux/UI/Views/HStack.hpp>
#include <Flux/UI/Views/Icon.hpp>
#include <Flux/UI/Views/Rectangle.hpp>
#include <Flux/UI/Views/ScrollView.hpp>
#include <Flux/UI/Views/Show.hpp>
#include <Flux/UI/Views/Slider.hpp>
#include <Flux/UI/Views/Spacer.hpp>
#include <Flux/UI/Views/Switch.hpp>
#include <Flux/UI/Views/Text.hpp>
#include <Flux/UI/Views/Toggle.hpp>
#include <Flux/UI/Views/VStack.hpp>
#include <Flux/UI/Views/ZStack.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace lambda_settings {

namespace {

using namespace flux;

enum class SettingsSection {
  General,
  Appearance,
  Desktop,
  DockPanel,
  Workspaces,
  Privacy,
  Notifications,
  Power,
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

std::vector<SidebarItem> sidebarItems() {
  return {
      {SettingsSection::General, "General", IconName::Settings},
      {SettingsSection::Appearance, "Appearance", IconName::Palette},
      {SettingsSection::Desktop, "Desktop", IconName::Computer},
      {SettingsSection::DockPanel, "Dock & Panel", IconName::Dock},
      {SettingsSection::Workspaces, "Workspaces", IconName::Layers},
      {SettingsSection::Privacy, "Privacy", IconName::Lock},
      {SettingsSection::Notifications, "Notifications", IconName::Notifications},
      {SettingsSection::Power, "Power", IconName::PowerSettingsNew},
      {SettingsSection::About, "About", IconName::Info},
  };
}

bool hasGroupGapBefore(SettingsSection section) {
  return section == SettingsSection::Privacy;
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
          rowsList({
              settingsRow("Processor", "", valueText("8-core · 3.2 GHz")),
              settingsRow("Memory", "", valueText("16 GB")),
              settingsRow("Storage", "", valueText("512 GB SSD · 192 GB free")),
              settingsRow("Graphics", "", valueText("Integrated GPU")),
              settingsRow("Display", "", valueText("2560 × 1440 · 60 Hz")),
              settingsRow("Kernel", "", valueText("lambda-kernel 6.8.2")),
          }))};
}

Element contentForSection(SettingsSection section,
                          Reactive::Signal<int> themeMode,
                          Reactive::Signal<int> accentIndex,
                          Reactive::Signal<int> wallpaperIndex,
                          Reactive::Signal<float> transparency,
                          Reactive::Signal<float> radius,
                          Reactive::Signal<bool> reduceMotion,
                          Reactive::Signal<bool> highContrast) {
  switch (section) {
  case SettingsSection::General:
    return genericPage("General", {{"Language", "English (US)"},
                                   {"Region", "United States"},
                                   {"Time zone", "Europe/Istanbul"},
                                   {"Auto-update", "Daily at 3:00 AM"},
                                   {"Hostname", "lambda-station"}});
  case SettingsSection::Desktop:
    return genericPage("Desktop", {{"Wallpaper", "Leaves"},
                                   {"Show widgets", "On"},
                                   {"Icon arrangement", "Auto · Grid"},
                                   {"Click behavior", "Single-click"},
                                   {"Show hidden files", "Off"}});
  case SettingsSection::DockPanel:
    return genericPage("Dock & Panel", {{"Dock position", "Bottom"},
                                        {"Auto-hide dock", "Off"},
                                        {"Icon size", "Medium"},
                                        {"Top panel", "Always visible"},
                                        {"Date format", "Sun 24 May, 14:42"}});
  case SettingsSection::Workspaces:
    return genericPage("Workspaces", {{"Active workspaces", "4"},
                                      {"Workspace gesture", "Three-finger swipe"},
                                      {"Per-display workspaces", "On"},
                                      {"Show preview on hover", "On"}});
  case SettingsSection::Privacy:
    return genericPage("Privacy", {{"Location services", "Allowed for: Weather, Maps"},
                                   {"Camera", "0 apps"},
                                   {"Microphone", "0 apps"},
                                   {"Analytics", "Off"},
                                   {"Lock screen", "After 5 min idle"}});
  case SettingsSection::Notifications:
    return genericPage("Notifications", {{"Show on lock screen", "On"},
                                         {"Banner style", "Persistent"},
                                         {"Notification grouping", "By app"},
                                         {"Do not disturb schedule", "10 PM - 8 AM"}});
  case SettingsSection::Power:
    return genericPage("Power", {{"Battery", "82% · 4h 12m remaining"},
                                 {"Power mode", "Balanced"},
                                 {"Display sleep", "10 min"},
                                 {"Computer sleep", "30 min"},
                                 {"Show battery percentage", "On"}});
  case SettingsSection::About:
    return aboutPage();
  case SettingsSection::Appearance:
  default:
    return appearancePage(themeMode, accentIndex, wallpaperIndex, transparency, radius, reduceMotion,
                          highContrast);
  }
}

} // namespace

struct SettingsAppRoot {
  Element body() const {
    auto activeSection = useState(SettingsSection::Appearance);
    auto themeMode = useState(1);
    auto accentIndex = useState(0);
    auto wallpaperIndex = useState(0);
    auto transparency = useState(80.f);
    auto radius = useState(14.f);
    auto reduceMotion = useState(false);
    auto highContrast = useState(false);
    auto chrome = useEnvironment<WindowChromeMetricsKey>();
    WindowChromeMetrics const metrics = chrome();

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
                           ScrollView{
                               .axis = ScrollAxis::Vertical,
                               .dragScrollEnabled = true,
                               .children = children(Element{Switch(
                                   [activeSection] { return activeSection(); },
                                   {
                                       Case(SettingsSection::General, [=] {
                                         return contentForSection(SettingsSection::General, themeMode,
                                                                  accentIndex, wallpaperIndex, transparency,
                                                                  radius, reduceMotion, highContrast);
                                       }),
                                       Case(SettingsSection::Appearance, [=] {
                                         return contentForSection(SettingsSection::Appearance, themeMode,
                                                                  accentIndex, wallpaperIndex, transparency,
                                                                  radius, reduceMotion, highContrast);
                                       }),
                                       Case(SettingsSection::Desktop, [=] {
                                         return contentForSection(SettingsSection::Desktop, themeMode,
                                                                  accentIndex, wallpaperIndex, transparency,
                                                                  radius, reduceMotion, highContrast);
                                       }),
                                       Case(SettingsSection::DockPanel, [=] {
                                         return contentForSection(SettingsSection::DockPanel, themeMode,
                                                                  accentIndex, wallpaperIndex, transparency,
                                                                  radius, reduceMotion, highContrast);
                                       }),
                                       Case(SettingsSection::Workspaces, [=] {
                                         return contentForSection(SettingsSection::Workspaces, themeMode,
                                                                  accentIndex, wallpaperIndex, transparency,
                                                                  radius, reduceMotion, highContrast);
                                       }),
                                       Case(SettingsSection::Privacy, [=] {
                                         return contentForSection(SettingsSection::Privacy, themeMode,
                                                                  accentIndex, wallpaperIndex, transparency,
                                                                  radius, reduceMotion, highContrast);
                                       }),
                                       Case(SettingsSection::Notifications, [=] {
                                         return contentForSection(SettingsSection::Notifications, themeMode,
                                                                  accentIndex, wallpaperIndex, transparency,
                                                                  radius, reduceMotion, highContrast);
                                       }),
                                       Case(SettingsSection::Power, [=] {
                                         return contentForSection(SettingsSection::Power, themeMode,
                                                                  accentIndex, wallpaperIndex, transparency,
                                                                  radius, reduceMotion, highContrast);
                                       }),
                                       Case(SettingsSection::About, [] { return aboutPage(); }),
                                   })}.padding(SettingsTheme::kMainPadV, SettingsTheme::kMainPadH,
                                               SettingsTheme::kMainPadV, SettingsTheme::kMainPadH))}
                               .flex(1.f, 1.f, 0.f))}
                       .flex(1.f, 1.f, 0.f))}
      .fill(SettingsTheme::windowBg);
  }
};

} // namespace lambda_settings
