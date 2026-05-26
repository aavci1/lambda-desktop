#include "Shell/UI/LambdaTopBar.hpp"

#include <Flux/Core/Color.hpp>
#include <Flux/Graphics/Styles.hpp>
#include <Flux/UI/IconName.hpp>
#include <Flux/UI/Views/Views.hpp>

using namespace flux;

namespace lambda_shell {
namespace {

Color statusColor(TopBarStatusItem const& item) {
  if (item.availability == StatusAvailability::Unavailable) return Color(1.f, 1.f, 1.f, 0.38f);
  return item.active ? Color(1.f, 1.f, 1.f, 1.f) : Color(1.f, 1.f, 1.f, 0.62f);
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

    for (auto const& item : topBarStatusItems(props.system)) {
        children.push_back(statusElement(item));
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
