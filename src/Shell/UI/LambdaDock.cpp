#include "Shell/UI/LambdaDock.hpp"

#include <Flux/Core/Color.hpp>
#include <Flux/Graphics/Styles.hpp>
#include <Flux/UI/IconName.hpp>
#include <Flux/UI/Views/Views.hpp>

#include <algorithm>

namespace lambda_shell {
namespace {

flux::Color rgba(float r, float g, float b, float a) {
  return flux::Color{r, g, b, a};
}

flux::FillStyle gradient(flux::Color from, flux::Color to) {
  return flux::FillStyle::linearGradient(from, to, {0.f, 0.f}, {1.f, 1.f});
}

std::string utf8(char32_t codepoint) {
  std::string out;
  if (codepoint <= 0x7Fu) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FFu) {
    out.push_back(static_cast<char>(0xC0u | (codepoint >> 6u)));
    out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
  } else if (codepoint <= 0xFFFFu) {
    out.push_back(static_cast<char>(0xE0u | (codepoint >> 12u)));
    out.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
  } else {
    out.push_back(static_cast<char>(0xF0u | (codepoint >> 18u)));
    out.push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
  }
  return out;
}

std::string icon(flux::IconName name) {
  return utf8(static_cast<char32_t>(name));
}

struct IconPalette {
  flux::Color from;
  flux::Color to;
  flux::Color ink;
};

IconPalette iconPalette(DockItem const& item) {
  if (item.kind == "launcher") return {rgba(0.96f, 0.97f, 0.99f, 1.f), rgba(0.80f, 0.84f, 0.92f, 1.f), rgba(0.13f, 0.20f, 0.33f, 1.f)};
  if (item.appId == "files") return {rgba(0.43f, 0.65f, 1.f, 1.f), rgba(0.16f, 0.50f, 1.f, 1.f), rgba(1.f, 1.f, 1.f, 1.f)};
  if (item.appId == "browser") return {rgba(0.37f, 0.72f, 1.f, 1.f), rgba(0.16f, 0.53f, 1.f, 1.f), rgba(1.f, 1.f, 1.f, 1.f)};
  if (item.appId == "terminal") return {rgba(0.16f, 0.18f, 0.27f, 1.f), rgba(0.05f, 0.07f, 0.14f, 1.f), rgba(0.75f, 0.90f, 1.f, 1.f)};
  if (item.appId == "settings") return {rgba(0.58f, 0.63f, 0.72f, 1.f), rgba(0.31f, 0.35f, 0.45f, 1.f), rgba(1.f, 1.f, 1.f, 1.f)};
  if (item.appId == "calendar") return {rgba(1.f, 1.f, 1.f, 1.f), rgba(0.92f, 0.94f, 0.98f, 1.f), rgba(0.90f, 0.29f, 0.24f, 1.f)};
  if (item.appId == "mail") return {rgba(0.44f, 0.71f, 1.f, 1.f), rgba(0.16f, 0.53f, 1.f, 1.f), rgba(1.f, 1.f, 1.f, 1.f)};
  if (item.appId == "music") return {rgba(0.79f, 0.50f, 0.90f, 1.f), rgba(0.48f, 0.25f, 1.f, 1.f), rgba(1.f, 1.f, 1.f, 1.f)};
  if (item.kind == "trash") return {rgba(0.80f, 0.84f, 0.89f, 1.f), rgba(0.58f, 0.64f, 0.74f, 1.f), rgba(0.10f, 0.15f, 0.25f, 1.f)};
  return {rgba(0.96f, 0.97f, 0.99f, 1.f), rgba(0.82f, 0.86f, 0.93f, 1.f), rgba(0.10f, 0.15f, 0.25f, 1.f)};
}

flux::IconName dockIconName(DockItem const& item) {
  if (item.kind == "launcher") return flux::IconName::Dashboard;
  if (item.appId == "files") return flux::IconName::FolderOpen;
  if (item.appId == "browser") return flux::IconName::Globe;
  if (item.appId == "terminal") return flux::IconName::Terminal;
  if (item.appId == "settings") return flux::IconName::Tune;
  if (item.appId == "calendar") return flux::IconName::CalendarToday;
  if (item.appId == "mail") return flux::IconName::Mail;
  if (item.appId == "music") return flux::IconName::LibraryMusic;
  if (item.kind == "trash") return flux::IconName::DeleteForever;
  return flux::IconName::Apps;
}

flux::Element dockIconAt(std::size_t index,
                         DockItem const& item,
                         flux::Signal<std::vector<DockItem>> const& items,
                         bool hover,
                         std::function<void()> onTap) {
  IconPalette const palette = iconPalette(item);
  float const lift = hover ? -5.f : 0.f;
  flux::Reactive::Bindable<bool> running{[items, index] {
    auto const& dockItems = items();
    return index < dockItems.size() && dockItems[index].running;
  }};
  flux::Reactive::Bindable<bool> focused{[items, index] {
    auto const& dockItems = items();
    return index < dockItems.size() && dockItems[index].focused;
  }};

  std::vector<flux::Element> layers;
  layers.push_back(flux::Rectangle{}
      .size(40.f, 40.f)
      .position(4.f, 4.f + lift)
      .fill(gradient(palette.from, palette.to))
      .stroke(flux::StrokeStyle::solid(rgba(1.f, 1.f, 1.f, 0.68f), 0.9f))
      .cornerRadius(11.f));
  layers.push_back(flux::Text{
      .text = icon(dockIconName(item)),
      .font = flux::Font{.family = "Material Symbols Rounded", .size = 31.f, .weight = 780.f},
      .color = palette.ink,
      .horizontalAlignment = flux::HorizontalAlignment::Center,
      .verticalAlignment = flux::VerticalAlignment::Center,
  }.size(40.f, 40.f).position(4.f, 4.f + lift));

  float const dotY = 48.f + lift;
  layers.push_back(flux::Show(
      [running] { return running.evaluate(); },
      [focused, dotY] {
        return flux::ZStack{
            .children = flux::children(flux::Rectangle{}
                .size(flux::Reactive::Bindable<float>{[focused] { return focused.evaluate() ? 6.f : 5.f; }},
                      flux::Reactive::Bindable<float>{[focused] { return focused.evaluate() ? 6.f : 5.f; }})
                .position(21.f, dotY)
                .fill(flux::Reactive::Bindable<flux::FillStyle>{[focused] {
                  return focused.evaluate() ? flux::FillStyle::solid(rgba(0.35f, 0.72f, 1.f, 1.f))
                                          : flux::FillStyle::solid(rgba(1.f, 1.f, 1.f, 0.72f));
                }})
                .cornerRadius(3.f)),
        };
      },
      [] {
        return flux::Rectangle{}.size(0.f, 0.f);
      }));

  auto element = flux::ZStack{
      .children = std::move(layers),
  }.size(static_cast<float>(kDockCell), static_cast<float>(dockHeight()));
  if (onTap) element = std::move(element).onTap(std::move(onTap));
  return element;
}

} // namespace

flux::Element LambdaDock::body() const {
  auto const items = props.items;
  auto const width = props.width;
  auto const onOpenLauncher = props.onOpenLauncher;
  auto const onActivateItem = props.onActivateItem;
  int const hoverIndex = props.hoverIndex;

  std::vector<flux::Element> children;
  std::vector<DockItem> const snapshot = items.peek();
  children.reserve(snapshot.size());
  for (std::size_t i = 0; i < snapshot.size(); ++i) {
    DockItem const& item = snapshot[i];
    if (item.kind == "separator") {
      children.push_back(flux::Rectangle{}
          .size(static_cast<float>(kDockSeparatorWidth), 30.f)
          .fill(rgba(1.f, 1.f, 1.f, 0.30f)));
      continue;
    }
    std::function<void()> onTap;
    if (item.kind == "launcher") {
      onTap = onOpenLauncher;
    } else if (onActivateItem) {
      onTap = [callback = onActivateItem, item] { callback(item); };
    }
    bool const hover = hoverIndex >= 0 && static_cast<int>(i) == hoverIndex;
    children.push_back(dockIconAt(i, item, items, hover, std::move(onTap)));
  }

  return flux::HStack{
      .spacing = static_cast<float>(kDockGap),
      .alignment = flux::Alignment::Center,
      .children = std::move(children),
  }
      .padding(static_cast<float>(kDockPaddingY), static_cast<float>(kDockPaddingX),
               static_cast<float>(kDockPaddingY), static_cast<float>(kDockPaddingX))
      .size(flux::Reactive::Bindable<float>{[width] { return static_cast<float>(width.evaluate()); }},
            static_cast<float>(dockHeight()))
      .fill(flux::Colors::transparent);
}

} // namespace lambda_shell
