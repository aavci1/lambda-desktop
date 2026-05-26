#include "Shell/UI/LambdaDock.hpp"

#include "Shell/ShellAppRegistry.hpp"

#include <Flux/Core/Color.hpp>
#include <Flux/Graphics/Styles.hpp>
#include <Flux/UI/IconName.hpp>
#include <Flux/UI/Views/Views.hpp>

#include <algorithm>

using namespace flux;

namespace lambda_shell {
namespace {

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

std::string icon(IconName name) {
  return utf8(static_cast<char32_t>(name));
}

struct IconPalette {
  Color from;
  Color to;
  Color ink;
};

IconPalette iconPalette(DockItem const& item) {
  if (item.kind == "launcher") return {Color(0.96f, 0.97f, 0.99f, 1.f), Color(0.80f, 0.84f, 0.92f, 1.f), Color(0.13f, 0.20f, 0.33f, 1.f)};
  if (shellAppIdMatches("files", item.appId)) return {Color(0.43f, 0.65f, 1.f, 1.f), Color(0.16f, 0.50f, 1.f, 1.f), Color(1.f, 1.f, 1.f, 1.f)};
  if (shellAppIdMatches("browser", item.appId)) return {Color(0.37f, 0.72f, 1.f, 1.f), Color(0.16f, 0.53f, 1.f, 1.f), Color(1.f, 1.f, 1.f, 1.f)};
  if (shellAppIdMatches("terminal", item.appId)) return {Color(0.16f, 0.18f, 0.27f, 1.f), Color(0.05f, 0.07f, 0.14f, 1.f), Color(0.75f, 0.90f, 1.f, 1.f)};
  if (shellAppIdMatches("settings", item.appId)) return {Color(0.58f, 0.63f, 0.72f, 1.f), Color(0.31f, 0.35f, 0.45f, 1.f), Color(1.f, 1.f, 1.f, 1.f)};
  if (item.appId == "calendar") return {Color(1.f, 1.f, 1.f, 1.f), Color(0.92f, 0.94f, 0.98f, 1.f), Color(0.90f, 0.29f, 0.24f, 1.f)};
  if (item.appId == "mail") return {Color(0.44f, 0.71f, 1.f, 1.f), Color(0.16f, 0.53f, 1.f, 1.f), Color(1.f, 1.f, 1.f, 1.f)};
  if (item.appId == "music") return {Color(0.79f, 0.50f, 0.90f, 1.f), Color(0.48f, 0.25f, 1.f, 1.f), Color(1.f, 1.f, 1.f, 1.f)};
  if (item.kind == "trash") return {Color(0.80f, 0.84f, 0.89f, 1.f), Color(0.58f, 0.64f, 0.74f, 1.f), Color(0.10f, 0.15f, 0.25f, 1.f)};
  return {Color(0.96f, 0.97f, 0.99f, 1.f), Color(0.82f, 0.86f, 0.93f, 1.f), Color(0.10f, 0.15f, 0.25f, 1.f)};
}

IconName dockIconName(DockItem const& item) {
  if (item.kind == "launcher") return IconName::Dashboard;
  if (shellAppIdMatches("files", item.appId)) return IconName::FolderOpen;
  if (shellAppIdMatches("browser", item.appId)) return IconName::Globe;
  if (shellAppIdMatches("terminal", item.appId)) return IconName::Terminal;
  if (shellAppIdMatches("settings", item.appId)) return IconName::Tune;
  if (item.appId == "calendar") return IconName::CalendarToday;
  if (item.appId == "mail") return IconName::Mail;
  if (item.appId == "music") return IconName::LibraryMusic;
  if (item.kind == "trash") return IconName::DeleteForever;
  return IconName::Apps;
}

Element dockIconAt(std::size_t index,
                         DockItem const& item,
                         Signal<std::vector<DockItem>> const& items,
                         bool hover,
                         std::function<void()> onTap) {
  IconPalette const palette = iconPalette(item);
  float const lift = hover ? -5.f : 0.f;
  Reactive::Bindable<bool> running{[items, index] {
    auto const& dockItems = items();
    return index < dockItems.size() && dockItems[index].running;
  }};
  Reactive::Bindable<bool> focused{[items, index] {
    auto const& dockItems = items();
    return index < dockItems.size() && dockItems[index].focused;
  }};

  float const slotWidth = static_cast<float>(kDockCell);
  float const slotHeight = static_cast<float>(kDockSlotHeight);
  float const iconSize = static_cast<float>(kDockIconSize);
  float const iconDotGap = static_cast<float>(kDockIconDotGap);
  float const slotMargin = static_cast<float>(kDockSlotMargin);
  float const dotBelowPad = static_cast<float>(kDockDotBelowPad);
  float const iconInsetX = (slotWidth - iconSize) * 0.5f;

  std::vector<Element> iconLayers;
  iconLayers.push_back(Rectangle{}
      .size(iconSize, iconSize)
      .position(iconInsetX, lift)
      .fill(FillStyle::linearGradient(palette.from, palette.to, {0.f, 0.f}, {1.f, 1.f}))
      .stroke(StrokeStyle::solid(Color(1.f, 1.f, 1.f, 0.4f), 0.5f))
      .cornerRadius(11.f));
  iconLayers.push_back(Text{
      .text = icon(dockIconName(item)),
      .font = Font{.family = "Material Symbols Rounded", .size = 31.f, .weight = 780.f},
      .color = palette.ink,
      .horizontalAlignment = HorizontalAlignment::Center,
      .verticalAlignment = VerticalAlignment::Center,
  }.size(iconSize, iconSize).position(iconInsetX, lift));

  float const dotSize = static_cast<float>(kDockDotSize);
  Reactive::Bindable<float> dotOpacity{[running] { return running.evaluate() ? 1.f : 0.f; }};
  Element dotLayer = Element{Rectangle{}}
      .width(dotSize)
      .height(dotSize)
      .fill(Reactive::Bindable<FillStyle>{[focused] {
        return focused.evaluate() ? FillStyle::solid(Color(0.35f, 0.72f, 1.f, 1.f))
                                  : FillStyle::solid(Color(1.f, 1.f, 1.f, 0.72f));
      }})
      .cornerRadius(3.f)
      .opacity(dotOpacity);

  auto element = VStack{
      .spacing = iconDotGap,
      .alignment = Alignment::Center,
      .children = children(
          ZStack{.children = std::move(iconLayers)}.size(slotWidth, iconSize),
          HStack{
              .alignment = Alignment::Center,
              .justifyContent = JustifyContent::Center,
              .children = children(std::move(dotLayer)),
          }
              .size(slotWidth, dotSize)),
  }
      .padding(slotMargin, 0.f, dotBelowPad, 0.f)
      .size(slotWidth, slotHeight);
  if (onTap) element = std::move(element).onTap(std::move(onTap));
  return element;
}

Element dockSeparator() {
  float const thickness = static_cast<float>(kDockSeparatorWidth);
  float const height = static_cast<float>(kDockIconSize);
  return Rectangle{}
      .size(thickness, height)
      .fill(FillStyle::solid(Color{1.f, 1.f, 1.f, 0.30f}));
}

} // namespace

Element LambdaDock::body() const {
  auto const items = props.items;
  auto const width = props.width;
  auto const onOpenLauncher = props.onOpenLauncher;
  auto const onActivateItem = props.onActivateItem;
  int const hoverIndex = props.hoverIndex;

  std::vector<Element> children;
  std::vector<DockItem> const snapshot = items.peek();
  children.reserve(snapshot.size());
  for (std::size_t i = 0; i < snapshot.size(); ++i) {
    DockItem const& item = snapshot[i];
    if (item.kind == "separator") {
      children.push_back(dockSeparator());
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

  return HStack{
      .spacing = static_cast<float>(kDockGap),
      .alignment = Alignment::Center,
      .children = std::move(children),
  }
      .padding(kDockPaddingTop, kDockPaddingX, kDockPaddingBottom, kDockPaddingX)
      .size(Reactive::Bindable<float>{[width] { return static_cast<float>(width.evaluate()); }},
            static_cast<float>(dockHeight()));
}

} // namespace lambda_shell
