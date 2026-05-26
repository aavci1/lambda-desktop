#include "Shell/UI/LambdaTopBar.hpp"

#include <Flux/Core/Color.hpp>
#include <Flux/Graphics/Styles.hpp>
#include <Flux/UI/IconName.hpp>
#include <Flux/UI/PopupMenu.hpp>
#include <Flux/UI/Views/Views.hpp>

using namespace flux;

namespace lambda_shell {
namespace {

Color statusColor(TopBarStatusItem const& item) {
  if (item.availability == StatusAvailability::Unavailable) return Color(1.f, 1.f, 1.f, 0.38f);
  return item.active ? Color(1.f, 1.f, 1.f, 1.f) : Color(1.f, 1.f, 1.f, 0.62f);
}

std::string statusTitle(std::string const& id) {
  if (id == "network") return "Network";
  if (id == "bluetooth") return "Bluetooth";
  if (id == "volume") return "Volume";
  if (id == "battery") return "Battery";
  return id;
}

std::string statusMenuLabel(TopBarStatusItem const& item) {
  std::string label = statusTitle(item.id);
  label += ": ";
  if (item.availability == StatusAvailability::Unavailable) {
    label += "unavailable";
  } else if (!item.active) {
    label += "off";
  } else if (!item.label.empty()) {
    label += item.label;
  } else {
    label += "on";
  }
  return label;
}

void showQuickStatusMenu(std::function<bool(PopupMenu)> const& showMenu,
                         SystemStatus const& status) {
  std::vector<MenuItem> items;
  for (TopBarStatusItem const& statusItem : topBarStatusItems(status)) {
    MenuItem item;
    item.label = statusMenuLabel(statusItem);
    item.actionName = statusItem.id;
    item.isEnabled = [] { return false; };
    items.push_back(std::move(item));
  }

  if (!items.empty()) {
    items.push_back(MenuItem::separator());
  }

  MenuItem unavailable;
  unavailable.label = "Provider controls unavailable";
  unavailable.actionName = "quick-settings-unavailable";
  unavailable.isEnabled = [] { return false; };
  items.push_back(std::move(unavailable));

  showMenu(PopupMenu{.items = std::move(items)});
}

Element statusElement(TopBarStatusItem const& item) {
  std::vector<Element> children;
  children.push_back(Icon{
      .name = item.icon,
      .size = 16.f,
      .color = statusColor(item),
  });
  if (!item.label.empty()) {
    children.push_back(Text{
        .text = item.label,
        .font = Font{.size = 12.f, .weight = 720.f},
        .color = statusColor(item),
        .verticalAlignment = VerticalAlignment::Center,
    });
  }
  return HStack{
      .spacing = 4.f,
      .alignment = Alignment::Center,
      .children = std::move(children),
  }.height(static_cast<float>(kTopBarHeight));
}

} // namespace

Element LambdaTopBar::body() const {
    Color const text = Color(1.f, 1.f, 1.f, 1.f);
    SystemStatus const system = props.system.evaluate();
    auto showMenu = usePopupMenu();

    std::vector<Element> children;
    children.reserve(7);

    auto lambda = Text {
        .text = "λ",
        .font = Font {.size = 20.f, .weight = 900.f},
        .color = text,
        .horizontalAlignment = HorizontalAlignment::Center,
        .verticalAlignment = VerticalAlignment::Center,
    }
        .size(22.f, static_cast<float>(kTopBarHeight));

    if (props.onOpenLauncher) {
        lambda = std::move(lambda).onTap(props.onOpenLauncher);
    }

    children.push_back(std::move(lambda));
    children.push_back(Spacer {});

    std::vector<Element> statusChildren;
    for (auto const& item : topBarStatusItems(system)) {
        statusChildren.push_back(statusElement(item));
    }
    if (!statusChildren.empty()) {
        children.push_back(HStack {
            .spacing = 10.f,
            .alignment = Alignment::Center,
            .children = std::move(statusChildren),
        }.height(static_cast<float>(kTopBarHeight))
         .onTap([showMenu, system] { showQuickStatusMenu(showMenu, system); }));
    }

    children.push_back(Text {
        .text = props.timeText,
        .font = Font {.size = 14.f, .weight = 900.f},
        .color = text,
        .verticalAlignment = VerticalAlignment::Center,
    });

    return HStack {
        .spacing = 10.f,
        .alignment = Alignment::Center,
        .children = std::move(children),
    }
        .padding(0.f, 12.f, 0.f, 12.f)
        .width(props.width)
        .height(static_cast<float>(kTopBarHeight));
}

} // namespace lambda_shell
