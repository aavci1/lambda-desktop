#include "Shell/UI/LambdaDock.hpp"

#include "Shell/ShellAppRegistry.hpp"
#include <Lambda/Core/Color.hpp>
#include <Lambda/Graphics/Image.hpp>
#include <Lambda/Graphics/ImageFillMode.hpp>
#include <Lambda/Reactive/Effect.hpp>
#include <Lambda/SceneGraph/SceneNode.hpp>
#include <Lambda/Graphics/Styles.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/IconName.hpp>
#include <Lambda/UI/MeasureContext.hpp>
#include <Lambda/UI/MountContext.hpp>
#include <Lambda/UI/VisualTokens.hpp>
#include <Lambda/UI/Views/Image.hpp>
#include <Lambda/UI/Views/PopoverCalloutShape.hpp>
#include <Lambda/UI/Views/Views.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

using namespace lambda;

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
  constexpr std::size_t kMaxIconCacheEntries = 256;
  std::string const key = path + "@" + std::to_string(std::max(1, pixelSize));
  auto found = cache.find(key);
  if (found != cache.end()) return found->second;
  if (cache.size() >= kMaxIconCacheEntries && !cache.empty()) {
    cache.erase(cache.begin());
  }
  auto image = loadImage(path, nullptr, static_cast<std::uint32_t>(std::max(1, pixelSize)));
  cache.emplace(key, image);
  return image;
}

Element lambdaLauncherIcon(float iconSize, float iconInsetX, float lift) {
  return Text{
      .text = "λ",
      .font = Font{.family = "", .size = 40.f, .weight = 900.f},
      .color = VisualTokens::primaryText,
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
  return IconName::Apps;
}

bool hasDockItemMenu(DockItem const& item) {
  return item.kind == "app" && !item.appId.empty() && !item.disabled;
}

float currentDockFrameWidth(Signal<std::vector<DockItem>> const& items,
                            Signal<int> const& clockWidth) {
  return static_cast<float>(dockWidth(items.peek(), clockWidth.peek()));
}

struct DockLayoutFrameState {
  Signal<std::vector<DockItem>> items;
  Signal<int> clockWidth;
  scenegraph::SceneNode* group = nullptr;
  std::vector<scenegraph::SceneNode*> children;
};

int dockStatusGridWidth() {
  return kDockStatusColumns * kDockStatusCell +
         (kDockStatusColumns - 1) * kDockStatusGridGap;
}

LayoutConstraints dockSectionConstraints(float width, float maxHeight, float minHeight = 0.f) {
  return LayoutConstraints{
      .maxWidth = std::max(0.f, width),
      .maxHeight = std::max(0.f, maxHeight),
      .minWidth = std::max(0.f, width),
      .minHeight = std::max(0.f, minHeight),
  };
}

Size relayoutDockSection(scenegraph::SceneNode* node, float width, float maxHeight,
                         float minHeight = 0.f) {
  if (!node) return {};
  (void)node->relayout(dockSectionConstraints(width, maxHeight, minHeight));
  return node->size();
}

void positionDockSection(scenegraph::SceneNode* node, float x, Size size) {
  if (!node) return;
  float const y = kDockPaddingTop +
      std::max(0.f, (static_cast<float>(kDockSlotHeight) - size.height) * 0.5f);
  node->setPosition(Point{x, y});
}

void layoutDockFrame(DockLayoutFrameState const& state) {
  float const frameWidth = currentDockFrameWidth(state.items, state.clockWidth);
  float const frameHeight = static_cast<float>(dockHeight());
  auto const currentItems = state.items.peek();
  float const appWidth = static_cast<float>(dockItemsWidth(currentItems));
  float const separatorWidth = static_cast<float>(kDockSeparatorWidth);
  float const statusWidth = static_cast<float>(dockStatusGridWidth());
  float const clockWidth = static_cast<float>(std::max(kDockClockMinWidth, state.clockWidth.peek()));
  float const slotHeight = static_cast<float>(kDockSlotHeight);

  std::array<Size, 5> sizes{};
  if (state.children.size() >= 5) {
    sizes[0] = relayoutDockSection(state.children[0], appWidth, slotHeight, slotHeight);
    sizes[1] = relayoutDockSection(state.children[1], separatorWidth, slotHeight, slotHeight);
    sizes[2] = relayoutDockSection(state.children[2], statusWidth, slotHeight);
    sizes[3] = relayoutDockSection(state.children[3], separatorWidth, slotHeight, slotHeight);
    sizes[4] = relayoutDockSection(state.children[4], clockWidth, slotHeight, slotHeight);

    float x = kDockPaddingX;
    positionDockSection(state.children[0], x, sizes[0]);
    if (appWidth > 0.f) {
      x += appWidth + static_cast<float>(kDockGap);
    }
    positionDockSection(state.children[1], x, sizes[1]);
    x += separatorWidth + static_cast<float>(kDockGap);
    positionDockSection(state.children[2], x, sizes[2]);
    x += statusWidth + static_cast<float>(kDockGap);
    positionDockSection(state.children[3], x, sizes[3]);
    x += separatorWidth + static_cast<float>(kDockGap);
    positionDockSection(state.children[4], x, sizes[4]);
  }
  if (state.group) {
    state.group->setSize(Size{frameWidth, frameHeight});
  }
}

struct DockLayoutFrame {
  std::vector<Element> children;
  Signal<std::vector<DockItem>> items;
  Signal<int> clockWidth;

  Size measure(MeasureContext& ctx,
               LayoutConstraints const& constraints,
               LayoutHints const& hints,
               TextSystem& textSystem) const {
    (void)constraints;
    (void)hints;
    (void)textSystem;
    float const frameWidth = currentDockFrameWidth(items, clockWidth);
    ctx.advanceChildSlot();
    return Size{frameWidth, static_cast<float>(dockHeight())};
  }

  std::unique_ptr<scenegraph::SceneNode> mount(MountContext& ctx) const {
    float const frameWidth = currentDockFrameWidth(items, clockWidth);
    auto group = std::make_unique<scenegraph::SceneNode>(
        Rect{0.f, 0.f, frameWidth, static_cast<float>(dockHeight())});
    std::vector<scenegraph::SceneNode*> mountedChildren;
    mountedChildren.reserve(children.size());
    for (std::size_t i = 0; i < children.size(); ++i) {
      ctx.measureContext().setChildIndex(i);
      MountContext childCtx = ctx.childWithSharedScope(
          dockSectionConstraints(0.f, static_cast<float>(kDockSlotHeight)),
          ctx.hints());
      auto childNode = children[i].mount(childCtx);
      mountedChildren.push_back(childNode.get());
      if (childNode) {
        group->appendChild(std::move(childNode));
      }
    }
    auto state = std::make_shared<DockLayoutFrameState>(DockLayoutFrameState{
        .items = items,
        .clockWidth = clockWidth,
        .group = group.get(),
        .children = std::move(mountedChildren),
    });
    group->setLayoutConstraints(ctx.constraints());
    group->setRelayout([state](LayoutConstraints const&) {
      layoutDockFrame(*state);
    });
    layoutDockFrame(*state);

    Reactive::SmallFn<void()> requestRedraw = ctx.redrawCallback();
    Reactive::withOwner(ctx.owner(), [state, requestRedraw = std::move(requestRedraw)]() mutable {
      Reactive::Effect([state, requestRedraw]() mutable {
        (void)state->items();
        (void)state->clockWidth();
        layoutDockFrame(*state);
        if (requestRedraw) {
          requestRedraw();
        }
      });
    });

    return group;
  }
};

struct DockMenuRow {
  std::string label;
  bool enabled = true;
  std::function<void()> action;

  Element body() const {
    Reactive::Signal<bool> hovered = useHover();
    Reactive::Signal<bool> pressed = usePress();
    Color const textColor = enabled ? VisualTokens::primaryText : Color{70.f / 255.f, 86.f / 255.f, 110.f / 255.f, 0.38f};
    Reactive::Bindable<Color> const fill{[enabled = enabled, hovered, pressed] {
      if (!enabled) {
        return Color(0.f, 0.f, 0.f, 0.001f);
      }
      if (pressed()) {
        return VisualTokens::pressedFill;
      }
      if (hovered()) {
        return VisualTokens::hoverFill;
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
                               .fill(FillStyle::solid(VisualTokens::separator))),
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
        .color = VisualTokens::primaryText,
        .horizontalAlignment = HorizontalAlignment::Center,
        .verticalAlignment = VerticalAlignment::Center,
    }.size(iconSize, iconSize).position(iconInsetX, lift));
  }

  float const dotSize = static_cast<float>(kDockDotSize);
  Reactive::Bindable<float> dotOpacity{[running] { return running.evaluate() ? 1.f : 0.f; }};
  Element dotLayer = Element{Rectangle{}}
      .width(dotSize)
      .height(dotSize)
      .fill(FillStyle::solid(VisualTokens::primaryText))
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
  float const thickness = static_cast<float>(kDockSeparatorLineWidth);
  float const slotWidth = static_cast<float>(kDockSeparatorWidth);
  float const height = static_cast<float>(kDockIconSize);
  float const slotHeight = static_cast<float>(kDockSlotHeight);
  float const x = (slotWidth - thickness) * 0.5f;
  float const y = (slotHeight - height) * 0.5f;
  return ZStack{.children = children(Rectangle{}
                                         .size(thickness, height)
                                         .position(x, y)
                                         .fill(FillStyle::solid(VisualTokens::separator)))}
      .size(slotWidth, slotHeight);
}

Element statusIcon(Reactive::Bindable<IconName> iconName, Reactive::Bindable<bool> available) {
  Reactive::Signal<bool> hovered = useHover();
  Reactive::Signal<bool> pressed = usePress();
  Reactive::Bindable<Color> const fill{[hovered, pressed] {
    if (pressed()) return VisualTokens::pressedFill;
    if (hovered()) return VisualTokens::hoverFill;
    return Color{0.f, 0.f, 0.f, 0.001f};
  }};
  Reactive::Bindable<Color> const color{[available] {
    return available.evaluate() ? VisualTokens::primaryText : VisualTokens::tertiaryText;
  }};

  return ZStack{
             .horizontalAlignment = Alignment::Center,
             .verticalAlignment = Alignment::Center,
             .children = children(
                 Rectangle{}
                     .size(static_cast<float>(kDockStatusCell), static_cast<float>(kDockStatusCell))
                     .cornerRadius(6.f)
                     .fill(fill),
                 Icon{.name = std::move(iconName),
                      .size = static_cast<float>(kDockStatusIconSize),
                      .weight = 520.f,
                      .color = color})}
      .size(static_cast<float>(kDockStatusCell), static_cast<float>(kDockStatusCell))
      .cursor(Cursor::Hand);
}

Element statusDockletIcon(std::string id, IconName fallbackIcon, Reactive::Bindable<SystemStatus> system) {
  std::string const itemId = std::move(id);
  return statusIcon(Reactive::Bindable<IconName>{[itemId, fallbackIcon, system] {
                      for (DockletStatusItem const& item : dockletStatusItems(system.evaluate())) {
                        if (item.id == itemId) return item.icon;
                      }
                      return fallbackIcon;
                    }},
                    Reactive::Bindable<bool>{[itemId, system] {
                      for (DockletStatusItem const& item : dockletStatusItems(system.evaluate())) {
                        if (item.id == itemId) return item.availability == StatusAvailability::Available;
                      }
                      return false;
                    }});
}

Element fixedStatusIcon(IconName iconName, bool available) {
  return statusIcon(Reactive::Bindable<IconName>{iconName}, Reactive::Bindable<bool>{available});
}

Element statusIconGrid(Reactive::Bindable<SystemStatus> system) {
  float const gap = static_cast<float>(kDockStatusGridGap);
  float const width =
      static_cast<float>(kDockStatusColumns * kDockStatusCell + (kDockStatusColumns - 1) * kDockStatusGridGap);
  float const height =
      static_cast<float>(kDockStatusRows * kDockStatusCell + (kDockStatusRows - 1) * kDockStatusGridGap);

  return VStack{
             .spacing = gap,
             .alignment = Alignment::Center,
             .children = children(
                 HStack{
                     .spacing = gap,
                     .alignment = Alignment::Center,
                     .children = children(statusDockletIcon("network", IconName::WifiOff, system),
                                          statusDockletIcon("bluetooth", IconName::BluetoothDisabled, system),
                                          statusDockletIcon("volume", IconName::VolumeOff, system)),
                 },
                 HStack{
                     .spacing = gap,
                     .alignment = Alignment::Center,
                     .children = children(statusDockletIcon("battery", IconName::BatteryUnknown, system),
                                          fixedStatusIcon(IconName::NotificationsOff, false),
                                          fixedStatusIcon(IconName::ContentPasteOff, false)),
                 })}
      .size(width, height);
}

Element clockDocklet(Signal<std::string> timeText, Signal<int> clockWidth) {
  Reactive::Bindable<std::string> const dateText{[timeText] { return dockClockDateText(timeText()); }};
  Reactive::Bindable<std::string> const clockText{[timeText] { return dockClockTimeText(timeText()); }};

  Reactive::Bindable<float> const width{[clockWidth] {
    return static_cast<float>(std::max(kDockClockMinWidth, clockWidth()));
  }};
  Reactive::Bindable<float> const textWidth{[clockWidth] {
    int const measuredWidth = std::max(kDockClockMinWidth, clockWidth());
    return std::max(1.f,
                    static_cast<float>(measuredWidth) - kDockClockLeadingPaddingX - kDockClockTrailingPaddingX);
  }};
  float const bandTop = static_cast<float>(kDockSlotMargin);
  float const dateHeight = 19.f;
  float const timeHeight = 26.f;
  float const rowGap = static_cast<float>(kDockIconSize) - dateHeight - timeHeight;
  return ZStack{
             .horizontalAlignment = Alignment::Center,
             .verticalAlignment = Alignment::Center,
             .children = children(
                 Text{
                     .text = dateText,
                     .font = Font{.family = "",
                                  .size = kDockClockDateFontSize,
                                  .weight = kDockClockDateFontWeight},
                     .color = VisualTokens::primaryText,
                     .horizontalAlignment = HorizontalAlignment::Center,
                     .verticalAlignment = VerticalAlignment::Center,
                 }.size(textWidth, dateHeight).position(kDockClockLeadingPaddingX, bandTop),
                 Text{
                     .text = clockText,
                     .font = Font{.family = "",
                                  .size = kDockClockTimeFontSize,
                                  .weight = kDockClockTimeFontWeight},
                     .color = VisualTokens::primaryText,
                     .horizontalAlignment = HorizontalAlignment::Center,
                     .verticalAlignment = VerticalAlignment::Center,
                 }.size(textWidth, timeHeight).position(kDockClockLeadingPaddingX, bandTop + dateHeight + rowGap))}
      .size(width, static_cast<float>(kDockSlotHeight));
}

} // namespace

Element LambdaDock::body() const {
  auto const items = props.items;
  auto const timeText = props.timeText;
  auto const clockWidth = props.clockWidth;
  auto const system = props.system;
  auto const onOpenLauncher = props.onOpenLauncher;
  auto const onActivateItem = props.onActivateItem;
  auto const onShowMenu = props.onShowMenu;
  int const hoverIndex = props.hoverIndex;

  std::vector<Element> sections;
  sections.push_back(Element{For(
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
                         .height(static_cast<float>(kDockSlotHeight))
                         .clipContent(true));
  sections.push_back(dockSeparator());
  sections.push_back(statusIconGrid(system));
  sections.push_back(dockSeparator());
  sections.push_back(clockDocklet(timeText, clockWidth));

  return Element{DockLayoutFrame{
      .children = std::move(sections),
      .items = items,
      .clockWidth = clockWidth,
  }};
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
