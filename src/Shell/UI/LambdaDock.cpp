#include "Shell/UI/LambdaDock.hpp"

#include "Shell/ShellAppRegistry.hpp"
#include <Flux/Core/Color.hpp>
#include <Flux/Graphics/Image.hpp>
#include <Flux/Graphics/ImageFillMode.hpp>
#include <Flux/Graphics/Styles.hpp>
#include <Flux/UI/Hooks.hpp>
#include <Flux/UI/IconName.hpp>
#include <Flux/UI/Views/Image.hpp>
#include <Flux/UI/Views/PopoverCalloutShape.hpp>
#include <Flux/UI/Views/Views.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>

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

std::shared_ptr<Image> iconImage(std::string const& path, int pixelSize) {
  if (path.empty()) return nullptr;
  static std::unordered_map<std::string, std::shared_ptr<Image>> cache;
  std::string const key = path + "@" + std::to_string(std::max(1, pixelSize));
  auto found = cache.find(key);
  if (found != cache.end()) return found->second;
  auto image = loadImage(path, nullptr, static_cast<std::uint32_t>(std::max(1, pixelSize)));
  cache.emplace(key, image);
  return image;
}

Element lambdaLauncherIcon(float iconSize, float iconInsetX, float lift) {
  return Text{
      .text = "λ",
      .font = Font{.family = "", .size = 40.f, .weight = 900.f},
      .color = Color(1.f, 1.f, 1.f, 1.f),
      .horizontalAlignment = HorizontalAlignment::Center,
      .verticalAlignment = VerticalAlignment::Center,
  }.size(iconSize, iconSize).position(iconInsetX, lift);
}

std::string iconKey(DockItem const& item) {
  if (!item.icon.empty()) return item.icon;
  return item.appId;
}

IconName dockIconName(DockItem const& item) {
  std::string const key = iconKey(item);
  if (item.kind == "launcher") return IconName::Dashboard;
  if (shellAppIdMatches("files", key)) return IconName::FolderOpen;
  if (shellAppIdMatches("browser", key)) return IconName::Globe;
  if (shellAppIdMatches("terminal", key)) return IconName::Terminal;
  if (shellAppIdMatches("settings", key)) return IconName::Tune;
  if (key == "calendar") return IconName::CalendarToday;
  if (key == "mail") return IconName::Mail;
  if (key == "music") return IconName::LibraryMusic;
  if (item.kind == "trash") return IconName::DeleteForever;
  return IconName::Apps;
}

bool hasDockItemMenu(DockItem const& item) {
  return item.kind == "app" && !item.appId.empty() && !item.disabled;
}

struct DockMenuRow {
  std::string label;
  bool enabled = true;
  std::function<void()> action;

  Element body() const {
    Reactive::Signal<bool> hovered = useHover();
    Reactive::Signal<bool> pressed = usePress();
    Color const textColor = enabled ? Color(0.10f, 0.12f, 0.15f, 0.84f) : Color(0.10f, 0.12f, 0.15f, 0.38f);
    Reactive::Bindable<Color> const fill{[enabled = enabled, hovered, pressed] {
      if (!enabled) {
        return Color(0.f, 0.f, 0.f, 0.001f);
      }
      if (pressed()) {
        return Color(0.f, 0.f, 0.f, 0.14f);
      }
      if (hovered()) {
        return Color(0.f, 0.f, 0.f, 0.10f);
      }
      return Color(0.f, 0.f, 0.f, 0.001f);
    }};

    Element row = ZStack{
        .horizontalAlignment = Alignment::Stretch,
        .verticalAlignment = Alignment::Stretch,
        .children = children(
            Rectangle{}
                .size(static_cast<float>(kDockMenuContentWidth), 32.f)
                .cornerRadius(8.f)
                .fill(fill),
            Text{
                .text = label,
                .font = Font{.family = "", .size = 14.f, .weight = 520.f},
                .color = textColor,
                .horizontalAlignment = HorizontalAlignment::Leading,
                .verticalAlignment = VerticalAlignment::Center,
            }.size(152.f, 32.f).position(12.f, 0.f)),
    }.size(static_cast<float>(kDockMenuContentWidth), 32.f);
    if (enabled && action) {
      row = std::move(row).cursor(Cursor::Hand).onTap(action);
    }
    return row;
  }
};

Element dockMenuRow(std::string label, bool enabled, std::function<void()> action) {
  return Element{DockMenuRow{std::move(label), enabled, std::move(action)}};
}

Element dockMenuSeparator() {
  return ZStack{
      .horizontalAlignment = Alignment::Stretch,
      .verticalAlignment = Alignment::Center,
      .children = children(Rectangle{}
                               .size(static_cast<float>(kDockMenuContentWidth - 24), 1.f)
                               .position(12.f, 3.f)
                               .fill(FillStyle::solid(Color(0.f, 0.f, 0.f, 0.10f)))),
  }.size(static_cast<float>(kDockMenuContentWidth), 7.f);
}

Element dockItemMenu(DockItem item,
                     std::function<void()> hidePopover,
                     std::function<void(DockItem const&)> onNewWindow,
                     std::function<void(DockItem const&)> onTogglePinned,
                     std::function<void(DockItem const&)> onQuitItem) {
  std::vector<Element> rows;
  rows.push_back(dockMenuRow("New Window", static_cast<bool>(onNewWindow), [item, hidePopover, onNewWindow] {
    hidePopover();
    if (onNewWindow) onNewWindow(item);
  }));
  rows.push_back(dockMenuSeparator());
  rows.push_back(dockMenuRow(item.pinned ? "Unpin" : "Pin",
                            static_cast<bool>(onTogglePinned),
                            [item, hidePopover, onTogglePinned] {
                              hidePopover();
                              if (onTogglePinned) onTogglePinned(item);
                            }));
  rows.push_back(dockMenuSeparator());
  rows.push_back(dockMenuRow("Quit", item.running && static_cast<bool>(onQuitItem), [item, hidePopover, onQuitItem] {
    hidePopover();
    if (onQuitItem) onQuitItem(item);
  }));
  return VStack{
      .spacing = 0.f,
      .alignment = Alignment::Stretch,
      .children = std::move(rows),
  }.size(static_cast<float>(kDockMenuContentWidth), static_cast<float>(kDockMenuContentHeight));
}

Element dockIconAt(Reactive::Signal<std::size_t> indexSignal,
                   DockItem const& item,
                   Signal<std::vector<DockItem>> const& items,
                   bool hover,
                   std::function<void(MouseButton, Modifiers)> onTap) {
  float const lift = hover ? -5.f : 0.f;
  Reactive::Bindable<bool> running{[items, indexSignal] {
    auto const& dockItems = items();
    std::size_t const index = indexSignal();
    return index < dockItems.size() && dockItems[index].running;
  }};
  float const slotWidth = static_cast<float>(kDockCell);
  float const slotHeight = static_cast<float>(kDockSlotHeight);
  float const iconSize = static_cast<float>(kDockIconSize);
  float const iconDotGap = static_cast<float>(kDockIconDotGap);
  float const slotMargin = static_cast<float>(kDockSlotMargin);
  float const dotBelowPad = static_cast<float>(kDockDotBelowPad);
  float const iconInsetX = (slotWidth - iconSize) * 0.5f;

  std::vector<Element> iconLayers;
  auto image = item.kind == "launcher" ? nullptr : iconImage(item.iconPath, item.iconPixelSize);
  if (item.kind == "launcher") {
    iconLayers.push_back(lambdaLauncherIcon(iconSize, iconInsetX, lift));
  } else if (image) {
    iconLayers.push_back(Element{views::Image{
        .source = std::move(image),
        .fillMode = ImageFillMode::Fit,
    }}.size(iconSize, iconSize).position(iconInsetX, lift));
  } else {
    iconLayers.push_back(Text{
        .text = icon(dockIconName(item)),
        .font = Font{.family = "Material Symbols Rounded", .size = 36.f, .weight = 680.f},
        .color = Color(1.f, 1.f, 1.f, 0.92f),
        .horizontalAlignment = HorizontalAlignment::Center,
        .verticalAlignment = VerticalAlignment::Center,
    }.size(iconSize, iconSize).position(iconInsetX, lift));
  }

  float const dotSize = static_cast<float>(kDockDotSize);
  Reactive::Bindable<float> dotOpacity{[running] { return running.evaluate() ? 1.f : 0.f; }};
  Element dotLayer = Element{Rectangle{}}
      .width(dotSize)
      .height(dotSize)
      .fill(FillStyle::solid(Color(0.f, 0.f, 0.f, 1.f)))
      .cornerRadius(dotSize * 0.5f)
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

std::string dockRowKey(DockItem const& item) {
  if (item.kind == "separator") return item.id;
  return item.id + "|" + item.kind + "|" + item.icon + "|" + item.iconPath + "|" +
         std::to_string(item.iconPixelSize);
}

Element dockSeparator() {
  float const thickness = static_cast<float>(kDockSeparatorWidth);
  float const height = static_cast<float>(kDockIconSize);
  float const slotHeight = static_cast<float>(kDockSlotHeight);
  float const y = (slotHeight - height) * 0.5f;
  return ZStack{.children = children(Rectangle{}
                                         .size(thickness, height)
                                         .position(0.f, y)
                                         .fill(FillStyle::solid(Color{1.f, 1.f, 1.f, 0.30f})))}
      .size(thickness, slotHeight);
}

} // namespace

Element LambdaDock::body() const {
  auto const items = props.items;
  auto const width = props.width;
  auto const onOpenLauncher = props.onOpenLauncher;
  auto const onActivateItem = props.onActivateItem;
  auto const onShowMenu = props.onShowMenu;
  int const hoverIndex = props.hoverIndex;

  return Element{For(
      items,
      dockRowKey,
      [items, onOpenLauncher, onActivateItem, onShowMenu, hoverIndex](
          DockItem const& item, Reactive::Signal<std::size_t> const& indexSignal) {
        if (item.kind == "separator") {
          return dockSeparator();
        }
        std::function<void(MouseButton, Modifiers)> onTap;
        if (item.kind == "launcher") {
          onTap = [onOpenLauncher](MouseButton button, Modifiers) {
            if (button == MouseButton::Left && onOpenLauncher) onOpenLauncher();
          };
        } else if (onActivateItem) {
          onTap = [items,
                   indexSignal,
                   callback = onActivateItem,
                   onShowMenu](MouseButton button, Modifiers) {
            auto const& currentItems = items();
            std::size_t const index = indexSignal();
            if (index >= currentItems.size()) {
              return;
            }
            DockItem const item = currentItems[index];
            if (button == MouseButton::Right && hasDockItemMenu(item) && onShowMenu) {
              onShowMenu(item);
            } else if (button == MouseButton::Left) {
              callback(item);
            }
          };
        }
        bool const hover = hoverIndex >= 0 && static_cast<int>(indexSignal.peek()) == hoverIndex;
        return dockIconAt(indexSignal, item, items, hover, std::move(onTap));
      },
      static_cast<float>(kDockGap),
      Alignment::Center,
      ForLayout::HorizontalStack)}
      .padding(kDockPaddingTop, kDockPaddingX, kDockPaddingBottom, kDockPaddingX)
      .size(Reactive::Bindable<float>{[width] { return static_cast<float>(width.evaluate()); }},
            static_cast<float>(dockHeight()));
}

Element LambdaDockMenu::body() const {
  Element content = dockItemMenu(props.item,
                                 props.onDismiss ? props.onDismiss : [] {},
                                 props.onNewWindow,
                                 props.onTogglePinned,
                                 props.onQuitItem);
  Element backdrop = Rectangle{}
      .size(std::max(1.f, props.surfaceWidth), std::max(1.f, props.surfaceHeight))
      .fill(Colors::transparent);
  if (props.onDismiss) {
    backdrop = std::move(backdrop).onTap(props.onDismiss);
  }

  Element callout = Element{PopoverCalloutShape{
      .placement = PopoverPlacement::Above,
      .arrow = true,
      .padding = static_cast<float>(kDockMenuPadding),
      .cornerRadius = CornerRadius{14.f},
      .backgroundColor = Colors::transparent,
      .borderColor = Colors::transparent,
      .borderWidth = 0.f,
      .maxSize = Size{static_cast<float>(kDockMenuContentWidth), static_cast<float>(kDockMenuContentHeight)},
      .content = std::move(content),
  }};
  return ZStack{
      .horizontalAlignment = Alignment::Start,
      .verticalAlignment = Alignment::Start,
      .children = children(
          std::move(backdrop),
          std::move(callout).position(props.menuX + static_cast<float>(kDockMenuSurfaceInset),
                                      props.menuY + static_cast<float>(kDockMenuSurfaceInset))),
  }.size(std::max(1.f, props.surfaceWidth), std::max(1.f, props.surfaceHeight));
}

} // namespace lambda_shell
