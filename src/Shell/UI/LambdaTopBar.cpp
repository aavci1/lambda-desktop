#include "Shell/UI/LambdaTopBar.hpp"

#include <Flux/Core/Color.hpp>
#include <Flux/Graphics/Styles.hpp>
#include <Flux/UI/IconName.hpp>
#include <Flux/UI/Views/Views.hpp>

namespace lambda_shell {
namespace {

flux::Color rgba(float r, float g, float b, float a) {
  return flux::Color{r, g, b, a};
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

} // namespace

flux::Element LambdaTopBar::body() const {
  flux::Color const text = rgba(1.f, 1.f, 1.f, 1.f);
  std::vector<flux::Element> statusItems;
  statusItems.reserve(4);
  for (flux::IconName name : {flux::IconName::NetworkWifi,
                              flux::IconName::Bluetooth,
                              flux::IconName::VolumeUp,
                              flux::IconName::BatteryFull}) {
    statusItems.push_back(flux::Text{
        .text = icon(name),
        .font = flux::Font{.family = "Material Symbols Rounded", .size = 16.f, .weight = 900.f},
        .color = text,
        .horizontalAlignment = flux::HorizontalAlignment::Center,
        .verticalAlignment = flux::VerticalAlignment::Center,
    }.size(16.f, 16.f));
  }

  auto launcherButton = flux::Text{
      .text = "λ",
      .font = flux::Font{.size = 16.f, .weight = 900.f},
      .color = text,
      .horizontalAlignment = flux::HorizontalAlignment::Center,
      .verticalAlignment = flux::VerticalAlignment::Center,
  }.size(22.f, static_cast<float>(kTopBarHeight));
  if (props.onOpenLauncher) {
    launcherButton = std::move(launcherButton).onTap(props.onOpenLauncher);
  }

  std::vector<flux::Element> leading;
  leading.push_back(std::move(launcherButton));
  auto const title = props.title;
  leading.push_back(flux::Show(
      [title] { return !title.evaluate().empty(); },
      [title] {
        return flux::Text{
            .text = title,
            .font = flux::Font{.size = 13.f, .weight = 620.f},
            .color = rgba(0.92f, 0.94f, 0.98f, 1.f),
            .verticalAlignment = flux::VerticalAlignment::Center,
        };
      },
      [] {
        return flux::Rectangle{}.size(0.f, 0.f);
      }));

  std::vector<flux::Element> trailing;
  trailing.reserve(statusItems.size() + 1);
  for (auto& item : statusItems) {
    trailing.push_back(std::move(item));
  }
  trailing.push_back(flux::Text{
      .text = props.timeText,
      .font = flux::Font{.size = 16.f, .weight = 900.f},
      .color = text,
      .verticalAlignment = flux::VerticalAlignment::Center,
  });

  return flux::HStack{
      .spacing = 10.f,
      .alignment = flux::Alignment::Center,
      .children = flux::children(
          flux::HStack{
              .spacing = 8.f,
              .alignment = flux::Alignment::Center,
              .children = std::move(leading),
          },
          flux::Spacer{}.flex(1.f, 1.f),
          flux::HStack{
              .spacing = 6.f,
              .alignment = flux::Alignment::Center,
              .children = std::move(trailing),
          }),
  }
      .padding(0.f, 12.f, 0.f, 14.f)
      .width(props.width)
      .height(static_cast<float>(kTopBarHeight))
      .fill(flux::Colors::transparent);
}

} // namespace lambda_shell
