#include "Shell/UI/LambdaCommandLauncher.hpp"

#include "Shell/ShellAppRegistry.hpp"

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
  if (shellAppIdMatches("files", item.appId)) return flux::IconName::FolderOpen;
  if (shellAppIdMatches("browser", item.appId)) return flux::IconName::Globe;
  if (shellAppIdMatches("terminal", item.appId)) return flux::IconName::Terminal;
  if (shellAppIdMatches("settings", item.appId)) return flux::IconName::Tune;
  if (item.appId == "calendar") return flux::IconName::CalendarToday;
  if (item.appId == "mail") return flux::IconName::Mail;
  if (item.appId == "music") return flux::IconName::LibraryMusic;
  return flux::IconName::Apps;
}

flux::Element resultTile(DockItem item,
                         flux::Reactive::Bindable<bool> active,
                         float x,
                         float y,
                         std::function<void(DockItem const&)> onActivateResult) {
  std::vector<flux::Element> layers;
  layers.push_back(flux::Rectangle{}
      .size(126.f, 96.f)
      .fill(flux::Reactive::Bindable<flux::FillStyle>{[active] {
        return active.evaluate() ? flux::FillStyle::solid(rgba(0.18f, 0.40f, 0.86f, 0.82f))
                                 : flux::FillStyle::solid(rgba(1.f, 1.f, 1.f, 0.54f));
      }})
      .cornerRadius(14.f));
  layers.push_back(flux::Rectangle{}
      .size(40.f, 40.f)
      .position(43.f, 14.f)
      .fill(flux::Reactive::Bindable<flux::FillStyle>{[active] {
        return active.evaluate()
            ? gradient(rgba(0.35f, 0.64f, 1.f, 1.f), rgba(0.13f, 0.40f, 0.93f, 1.f))
            : gradient(rgba(0.96f, 0.97f, 0.99f, 1.f), rgba(0.82f, 0.86f, 0.93f, 1.f));
      }})
      .stroke(flux::StrokeStyle::solid(rgba(1.f, 1.f, 1.f, 0.68f), 0.9f))
      .cornerRadius(11.f));
  layers.push_back(flux::Text{
      .text = icon(dockIconName(item)),
      .font = flux::Font{.family = "Material Symbols Rounded", .size = 31.f, .weight = 780.f},
      .color = flux::Reactive::Bindable<flux::Color>{[active] {
        return active.evaluate() ? rgba(1.f, 1.f, 1.f, 1.f) : rgba(0.10f, 0.15f, 0.25f, 1.f);
      }},
      .horizontalAlignment = flux::HorizontalAlignment::Center,
      .verticalAlignment = flux::VerticalAlignment::Center,
  }.size(40.f, 40.f).position(43.f, 14.f));
  layers.push_back(flux::Text{
      .text = item.label,
      .font = flux::Font{.size = 12.5f, .weight = 620.f},
      .color = flux::Reactive::Bindable<flux::Color>{[active] {
        return active.evaluate() ? rgba(1.f, 1.f, 1.f, 1.f) : rgba(0.08f, 0.12f, 0.22f, 1.f);
      }},
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
  auto const results = props.results;
  auto const query = props.query;
  auto const highlighted = props.highlighted;
  auto const widthBinding = props.width;
  auto const heightBinding = props.height;
  auto const onActivateResult = props.onActivateResult;
  auto const onDismiss = props.onDismiss;

  flux::Reactive::Bindable<std::string> queryText{[query] {
    std::string const value = query();
    return value.empty() ? "Command" : value;
  }};
  flux::Reactive::Bindable<flux::Color> queryColor{[query] {
    return query().empty() ? rgba(0.32f, 0.34f, 0.40f, 1.f) : rgba(0.05f, 0.08f, 0.14f, 1.f);
  }};

  flux::Reactive::Bindable<float> widthFloatBinding{
      [widthBinding] { return static_cast<float>(std::max(1, widthBinding.evaluate())); }};
  flux::Reactive::Bindable<float> heightFloatBinding{
      [heightBinding] { return static_cast<float>(std::max(1, heightBinding.evaluate())); }};
  flux::Reactive::Bindable<float> fieldWBinding{[widthBinding] {
    return launcherLayout(std::max(1, widthBinding.evaluate())).fieldW;
  }};
  flux::Reactive::Bindable<float> fieldXBinding{[widthBinding] {
    return launcherLayout(std::max(1, widthBinding.evaluate())).fieldX;
  }};
  float const fieldY = 80.f;

  std::vector<flux::Element> layers;
  layers.push_back(flux::Rectangle{}
                       .size(fieldWBinding, 48.f)
                       .position(fieldXBinding, fieldY)
                       .fill(rgba(1.f, 1.f, 1.f, 0.72f))
                       .stroke(flux::StrokeStyle::solid(rgba(1.f, 1.f, 1.f, 0.72f), 1.f))
                       .cornerRadius(14.f));
  flux::Reactive::Bindable<float> queryTextWBinding{[fieldWBinding] {
    return fieldWBinding.evaluate() - 36.f;
  }};
  flux::Reactive::Bindable<float> queryTextXBinding{[fieldXBinding] {
    return fieldXBinding.evaluate() + 18.f;
  }};
  layers.push_back(flux::Text{
                       .text = queryText,
                       .font = flux::Font{.size = 18.f, .weight = 540.f},
                       .color = queryColor,
                       .verticalAlignment = flux::VerticalAlignment::Center,
                   }
                       .size(queryTextWBinding, 24.f)
                       .position(queryTextXBinding, fieldY + 12.f));

  layers.push_back(flux::Element{flux::For(
      results,
      [](DockItem const& item) { return item.id; },
      [widthBinding, highlighted, onActivateResult](
          DockItem const& item, flux::Reactive::Signal<std::size_t> const& indexSignal) {
        LauncherLayout const layout = launcherLayout(std::max(1, widthBinding.evaluate()));
        flux::Reactive::Bindable<bool> active{[highlighted, indexSignal] {
          return static_cast<int>(indexSignal()) == highlighted.evaluate();
        }};
        int const index = static_cast<int>(indexSignal());
        int const col = index % layout.columns;
        int const row = index / layout.columns;
        float const x = static_cast<float>(layout.startX + col * (layout.tileW + layout.gap));
        float const y = layout.fieldY + 76.f + static_cast<float>(row * (layout.tileH + layout.gap));
        return resultTile(item, active, x, y, onActivateResult);
      },
      0.f,
      flux::Alignment::Start,
      flux::ForLayout::Overlay)});

  std::vector<flux::Element> stackChildren;
  auto backdrop = flux::Rectangle{}
                      .size(widthFloatBinding, heightFloatBinding)
                      .fill(rgba(0.03f, 0.05f, 0.10f, 0.26f));
  if (onDismiss) {
    backdrop = std::move(backdrop).onTap(onDismiss);
  }
  stackChildren.push_back(std::move(backdrop));
  for (auto& layer : layers) {
    stackChildren.push_back(std::move(layer));
  }

  return flux::ZStack{
      .children = std::move(stackChildren),
  }.size(widthFloatBinding, heightFloatBinding);
}

} // namespace lambda_shell
