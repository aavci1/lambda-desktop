#include "Shell/UI/LambdaCommandLauncher.hpp"

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

flux::IconName dockIconName(DockItem const& item) {
  if (item.appId == "files") return flux::IconName::FolderOpen;
  if (item.appId == "browser") return flux::IconName::Globe;
  if (item.appId == "terminal") return flux::IconName::Terminal;
  if (item.appId == "settings") return flux::IconName::Tune;
  if (item.appId == "calendar") return flux::IconName::CalendarToday;
  if (item.appId == "mail") return flux::IconName::Mail;
  if (item.appId == "music") return flux::IconName::LibraryMusic;
  return flux::IconName::Apps;
}

flux::Element resultTile(DockItem item,
                         bool active,
                         float x,
                         float y,
                         std::function<void(DockItem const&)> onActivateResult) {
  std::vector<flux::Element> layers;
  layers.push_back(flux::Rectangle{}
      .size(126.f, 96.f)
      .fill(active ? rgba(0.18f, 0.40f, 0.86f, 0.82f) : rgba(1.f, 1.f, 1.f, 0.54f))
      .cornerRadius(14.f));
  layers.push_back(flux::Rectangle{}
      .size(40.f, 40.f)
      .position(43.f, 14.f)
      .fill(active ? gradient(rgba(0.35f, 0.64f, 1.f, 1.f), rgba(0.13f, 0.40f, 0.93f, 1.f))
                   : gradient(rgba(0.96f, 0.97f, 0.99f, 1.f), rgba(0.82f, 0.86f, 0.93f, 1.f)))
      .stroke(flux::StrokeStyle::solid(rgba(1.f, 1.f, 1.f, 0.68f), 0.9f))
      .cornerRadius(11.f));
  layers.push_back(flux::Text{
      .text = icon(dockIconName(item)),
      .font = flux::Font{.family = "Material Symbols Rounded", .size = 31.f, .weight = 780.f},
      .color = active ? rgba(1.f, 1.f, 1.f, 1.f) : rgba(0.10f, 0.15f, 0.25f, 1.f),
      .horizontalAlignment = flux::HorizontalAlignment::Center,
      .verticalAlignment = flux::VerticalAlignment::Center,
  }.size(40.f, 40.f).position(43.f, 14.f));
  layers.push_back(flux::Text{
      .text = item.label,
      .font = flux::Font{.size = 12.5f, .weight = 620.f},
      .color = active ? rgba(1.f, 1.f, 1.f, 1.f) : rgba(0.08f, 0.12f, 0.22f, 1.f),
      .horizontalAlignment = flux::HorizontalAlignment::Center,
      .verticalAlignment = flux::VerticalAlignment::Center,
  }.size(98.f, 18.f).position(14.f, 62.f));

  auto tile = flux::ZStack{
      .children = std::move(layers),
  }.size(126.f, 96.f).position(x, y);
  if (onActivateResult) {
    tile = std::move(tile).onTap([callback = std::move(onActivateResult), item] { callback(item); });
  }
  return tile;
}

} // namespace

flux::Element LambdaCommandLauncher::body() const {
  std::vector<flux::Element> layers;
  LauncherLayout const layout = launcherLayout(props.width);
  layers.push_back(flux::Rectangle{}
      .size(layout.fieldW, 48.f)
      .position(layout.fieldX, layout.fieldY)
      .fill(rgba(1.f, 1.f, 1.f, 0.72f))
      .stroke(flux::StrokeStyle::solid(rgba(1.f, 1.f, 1.f, 0.72f), 1.f))
      .cornerRadius(14.f));
  layers.push_back(flux::Text{
      .text = props.query.empty() ? "Command" : props.query,
      .font = flux::Font{.size = 18.f, .weight = 540.f},
      .color = props.query.empty() ? rgba(0.32f, 0.34f, 0.40f, 1.f) : rgba(0.05f, 0.08f, 0.14f, 1.f),
      .verticalAlignment = flux::VerticalAlignment::Center,
  }.size(layout.fieldW - 36.f, 24.f).position(layout.fieldX + 18.f, layout.fieldY + 12.f));

  auto results = launcherResults(props.items, props.query);
  int const highlighted = results.empty() ? 0 : std::clamp(props.highlighted, 0, static_cast<int>(results.size()) - 1);
  for (std::size_t i = 0; i < results.size(); ++i) {
    int const col = static_cast<int>(i) % layout.columns;
    int const row = static_cast<int>(i) / layout.columns;
    float const x = static_cast<float>(layout.startX + col * (layout.tileW + layout.gap));
    float const y = layout.fieldY + 76.f + static_cast<float>(row * (layout.tileH + layout.gap));
    layers.push_back(resultTile(results[i], i == static_cast<std::size_t>(highlighted), x, y, props.onActivateResult));
  }

  std::vector<flux::Element> stackChildren;
  auto backdrop = flux::Rectangle{}
                      .size(static_cast<float>(props.width), static_cast<float>(props.height))
                      .fill(rgba(0.03f, 0.05f, 0.10f, 0.26f));
  if (props.onDismiss) {
    backdrop = std::move(backdrop).onTap(props.onDismiss);
  }
  stackChildren.push_back(std::move(backdrop));
  for (auto& layer : layers) {
    stackChildren.push_back(std::move(layer));
  }

  return flux::ZStack{
      .children = std::move(stackChildren),
  }.size(static_cast<float>(props.width), static_cast<float>(props.height));
}

} // namespace lambda_shell
