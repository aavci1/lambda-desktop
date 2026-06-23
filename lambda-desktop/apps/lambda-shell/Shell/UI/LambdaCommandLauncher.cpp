#include "Shell/UI/LambdaCommandLauncher.hpp"

#include "Shell/ShellAppRegistry.hpp"

#include <Lambda/Core/Color.hpp>
#include <Lambda/Graphics/Image.hpp>
#include <Lambda/Graphics/ImageFillMode.hpp>
#include <Lambda/Graphics/Styles.hpp>
#include <Lambda/UI/IconName.hpp>
#include <Lambda/UI/Views/Image.hpp>
#include <Lambda/UI/Views/Views.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace lambda_shell {
namespace {

lambdaui::Color rgba(float r, float g, float b, float a) {
  return lambdaui::Color{r, g, b, a};
}

lambdaui::FillStyle gradient(lambdaui::Color from, lambdaui::Color to) {
  return lambdaui::FillStyle::linearGradient(from, to, {0.f, 0.f}, {1.f, 1.f});
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

std::string icon(lambdaui::IconName name) {
  return utf8(static_cast<char32_t>(name));
}

std::shared_ptr<lambdaui::Image> iconImage(std::string const& path, int pixelSize) {
  if (path.empty()) return nullptr;
  static std::unordered_map<std::string, std::shared_ptr<lambdaui::Image>> cache;
  std::string const key = path + "@" + std::to_string(std::max(1, pixelSize));
  auto found = cache.find(key);
  if (found != cache.end()) return found->second;
  auto image = lambdaui::loadImage(path, nullptr, static_cast<std::uint32_t>(std::max(1, pixelSize)));
  cache.emplace(key, image);
  return image;
}

std::string iconKey(DockItem const& item) {
  if (!item.icon.empty()) return item.icon;
  return item.appId;
}

lambdaui::IconName dockIconName(DockItem const& item) {
  std::string const key = iconKey(item);
  if (shellAppIdMatches("files", key)) return lambdaui::IconName::FolderOpen;
  if (shellAppIdMatches("browser", key)) return lambdaui::IconName::Globe;
  if (shellAppIdMatches("terminal", key)) return lambdaui::IconName::Terminal;
  if (shellAppIdMatches("settings", key)) return lambdaui::IconName::Tune;
  if (key == "lock") return lambdaui::IconName::Lock;
  if (key == "logout") return lambdaui::IconName::Logout;
  if (key == "sleep") return lambdaui::IconName::Bedtime;
  if (key == "restart") return lambdaui::IconName::RestartAlt;
  if (key == "power") return lambdaui::IconName::PowerSettingsNew;
  if (key == "calendar") return lambdaui::IconName::CalendarToday;
  if (key == "mail") return lambdaui::IconName::Mail;
  if (key == "music") return lambdaui::IconName::LibraryMusic;
  return lambdaui::IconName::Apps;
}

lambdaui::Element resultTile(DockItem item,
                         lambdaui::Reactive::Bindable<bool> active,
                         float x,
                         float y,
                         std::function<void(DockItem const&)> onActivateResult) {
  std::vector<lambdaui::Element> layers;
  layers.push_back(lambdaui::Rectangle{}
      .size(126.f, 96.f)
      .fill(lambdaui::Reactive::Bindable<lambdaui::FillStyle>{[active] {
        return active.evaluate() ? lambdaui::FillStyle::solid(rgba(0.18f, 0.40f, 0.86f, 0.82f))
                                 : lambdaui::FillStyle::solid(rgba(1.f, 1.f, 1.f, 0.54f));
      }})
      .cornerRadius(14.f));
  layers.push_back(lambdaui::Rectangle{}
      .size(40.f, 40.f)
      .position(43.f, 14.f)
      .fill(lambdaui::Reactive::Bindable<lambdaui::FillStyle>{[active] {
        return active.evaluate()
            ? gradient(rgba(0.35f, 0.64f, 1.f, 1.f), rgba(0.13f, 0.40f, 0.93f, 1.f))
            : gradient(rgba(0.96f, 0.97f, 0.99f, 1.f), rgba(0.82f, 0.86f, 0.93f, 1.f));
      }})
      .stroke(lambdaui::StrokeStyle::solid(rgba(1.f, 1.f, 1.f, 0.68f), 0.9f))
      .cornerRadius(11.f));
  if (auto image = iconImage(item.iconPath, item.iconPixelSize)) {
    layers.push_back(lambdaui::Element{lambdaui::views::Image{
        .source = std::move(image),
        .fillMode = lambdaui::ImageFillMode::Fit,
    }}.size(36.f, 36.f).position(45.f, 16.f));
  } else {
    layers.push_back(lambdaui::Text{
        .text = icon(dockIconName(item)),
        .font = lambdaui::Font{.family = "Material Symbols Rounded", .size = 31.f, .weight = 780.f},
        .color = lambdaui::Reactive::Bindable<lambdaui::Color>{[active] {
          return active.evaluate() ? rgba(1.f, 1.f, 1.f, 1.f) : rgba(0.10f, 0.15f, 0.25f, 1.f);
        }},
        .horizontalAlignment = lambdaui::HorizontalAlignment::Center,
        .verticalAlignment = lambdaui::VerticalAlignment::Center,
    }.size(40.f, 40.f).position(43.f, 14.f));
  }
  layers.push_back(lambdaui::Text{
      .text = item.label,
      .font = lambdaui::Font{.size = 12.5f, .weight = 620.f},
      .color = lambdaui::Reactive::Bindable<lambdaui::Color>{[active] {
        return active.evaluate() ? rgba(1.f, 1.f, 1.f, 1.f) : rgba(0.08f, 0.12f, 0.22f, 1.f);
      }},
      .horizontalAlignment = lambdaui::HorizontalAlignment::Center,
      .verticalAlignment = lambdaui::VerticalAlignment::Center,
  }.size(98.f, 18.f).position(14.f, 62.f));

  auto tile = lambdaui::ZStack{
      .children = std::move(layers),
  }.size(126.f, 96.f).position(x, y);
  if (onActivateResult) {
    tile = std::move(tile).onTap([callback = std::move(onActivateResult), item] { callback(item); });
  }
  return tile;
}

} // namespace

lambdaui::Element LambdaCommandLauncher::body() const {
  auto const results = props.results;
  auto const query = props.query;
  auto const highlighted = props.highlighted;
  auto const widthBinding = props.width;
  auto const heightBinding = props.height;
  auto const onActivateResult = props.onActivateResult;
  auto const onDismiss = props.onDismiss;

  lambdaui::Reactive::Bindable<std::string> queryText{[query] {
    std::string const value = query();
    return value.empty() ? "Command" : value;
  }};
  lambdaui::Reactive::Bindable<lambdaui::Color> queryColor{[query] {
    return query().empty() ? rgba(0.32f, 0.34f, 0.40f, 1.f) : rgba(0.05f, 0.08f, 0.14f, 1.f);
  }};

  lambdaui::Reactive::Bindable<float> widthFloatBinding{
      [widthBinding] { return static_cast<float>(std::max(1, widthBinding.evaluate())); }};
  lambdaui::Reactive::Bindable<float> heightFloatBinding{
      [heightBinding] { return static_cast<float>(std::max(1, heightBinding.evaluate())); }};
  lambdaui::Reactive::Bindable<float> fieldWBinding{[widthBinding] {
    return launcherLayout(std::max(1, widthBinding.evaluate())).fieldW;
  }};
  lambdaui::Reactive::Bindable<float> fieldXBinding{[widthBinding] {
    return launcherLayout(std::max(1, widthBinding.evaluate())).fieldX;
  }};
  float const fieldY = 80.f;

  std::vector<lambdaui::Element> layers;
  layers.push_back(lambdaui::Rectangle{}
                       .size(fieldWBinding, 48.f)
                       .position(fieldXBinding, fieldY)
                       .fill(rgba(1.f, 1.f, 1.f, 0.72f))
                       .stroke(lambdaui::StrokeStyle::solid(rgba(1.f, 1.f, 1.f, 0.72f), 1.f))
                       .cornerRadius(14.f));
  lambdaui::Reactive::Bindable<float> queryTextWBinding{[fieldWBinding] {
    return fieldWBinding.evaluate() - 36.f;
  }};
  lambdaui::Reactive::Bindable<float> queryTextXBinding{[fieldXBinding] {
    return fieldXBinding.evaluate() + 18.f;
  }};
  layers.push_back(lambdaui::Text{
                       .text = queryText,
                       .font = lambdaui::Font{.size = 18.f, .weight = 540.f},
                       .color = queryColor,
                       .verticalAlignment = lambdaui::VerticalAlignment::Center,
                   }
                       .size(queryTextWBinding, 24.f)
                       .position(queryTextXBinding, fieldY + 12.f));

  layers.push_back(lambdaui::Element{lambdaui::For(
      results,
      [](DockItem const& item) { return item.id; },
      [widthBinding, highlighted, onActivateResult](
          DockItem const& item, lambdaui::Reactive::Signal<std::size_t> const& indexSignal) {
        LauncherLayout const layout = launcherLayout(std::max(1, widthBinding.evaluate()));
        lambdaui::Reactive::Bindable<bool> active{[highlighted, indexSignal] {
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
      lambdaui::Alignment::Start,
      lambdaui::ForLayout::Overlay)});

  std::vector<lambdaui::Element> stackChildren;
  auto backdrop = lambdaui::Rectangle{}
                      .size(widthFloatBinding, heightFloatBinding)
                      .fill(rgba(0.03f, 0.05f, 0.10f, 0.26f));
  if (onDismiss) {
    backdrop = std::move(backdrop).onTap(onDismiss);
  }
  stackChildren.push_back(std::move(backdrop));
  for (auto& layer : layers) {
    stackChildren.push_back(std::move(layer));
  }

  return lambdaui::ZStack{
      .children = std::move(stackChildren),
  }.size(widthFloatBinding, heightFloatBinding);
}

} // namespace lambda_shell
