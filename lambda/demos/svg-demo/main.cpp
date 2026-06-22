#include <Lambda.hpp>
#include <Lambda/UI/Application.hpp>
#include <Lambda/UI/WindowUI.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/UI.hpp>
#include <Lambda/UI/Views/Views.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace lambda;

namespace {

constexpr char const* kDefaultSvgPath = "/Users/abdurrahmanavci/Downloads/nemo/nemo.svg";

struct LoadedSvg {
  Rect viewBox{0.f, 0.f, 0.f, 0.f};
  std::vector<SvgNode> nodes;
  std::string status;
};

std::string readFile(std::string const& path) {
  std::ifstream file(path);
  std::ostringstream out;
  out << file.rdbuf();
  return out.str();
}

Color colorFromHex(std::string hex) {
  if (!hex.empty() && hex.front() == '#') {
    hex.erase(hex.begin());
  }
  unsigned value = 0;
  std::istringstream in(hex);
  in >> std::hex >> value;
  return Color::hex(value);
}

std::string attr(std::string const& tag, char const* name) {
  std::regex const re(std::string{name} + R"attr(="([^"]*)")attr");
  std::smatch match;
  if (std::regex_search(tag, match, re)) {
    return match[1].str();
  }
  return {};
}

Rect parseViewBox(std::string const& svg) {
  std::regex const re(R"attr(viewBox="([-+0-9.eE]+)[ ,]+([-+0-9.eE]+)[ ,]+([-+0-9.eE]+)[ ,]+([-+0-9.eE]+)")attr");
  std::smatch match;
  if (!std::regex_search(svg, match, re)) {
    return Rect{0.f, 0.f, 0.f, 0.f};
  }
  return Rect{
      std::stof(match[1].str()),
      std::stof(match[2].str()),
      std::stof(match[3].str()),
      std::stof(match[4].str()),
  };
}

std::map<std::string, Color> parseClassFills(std::string const& svg) {
  std::map<std::string, Color> fills;
  std::regex const re(R"(\.([A-Za-z0-9_-]+)\s*\{[^}]*fill:\s*(#[0-9A-Fa-f]{6})\s*;)");
  for (auto it = std::sregex_iterator(svg.begin(), svg.end(), re); it != std::sregex_iterator(); ++it) {
    fills[it->str(1)] = colorFromHex(it->str(2));
  }
  return fills;
}

LoadedSvg loadSvgForDemo(std::string const& path) {
  std::string const source = readFile(path);
  if (source.empty()) {
    LoadedSvg fallback{
        .viewBox = Rect{0.f, 0.f, 240.f, 180.f},
        .status = "Using built-in SVG sample; unable to read " + path,
    };
    fallback.nodes.emplace_back(svg::rect(12.f, 12.f, 216.f, 156.f, CornerRadius{18.f},
                                          FillStyle::linearGradient(Color::hex(0x2F80ED), Color::hex(0x27AE60)),
                                          StrokeStyle::solid(Color::hex(0x1B4D89), 3.f)));
    fallback.nodes.emplace_back(svg::circle(78.f, 90.f, 34.f, FillStyle::solid(Color::hex(0xF2C94C)),
                                            StrokeStyle::solid(Color::hex(0x8A6D1F), 2.f)));
    fallback.nodes.emplace_back(svg::rect(120.f, 58.f, 72.f, 64.f, CornerRadius{12.f},
                                          FillStyle::solid(Color::hex(0xEB5757)),
                                          StrokeStyle::solid(Color::hex(0x8B2C2C), 2.f)));
    return fallback;
  }

  std::map<std::string, Color> classFills = parseClassFills(source);
  LoadedSvg loaded{
      .viewBox = parseViewBox(source),
      .status = "Loaded " + path,
  };

  std::regex const pathRe(R"(<path\b([^>]*)>)");
  for (auto it = std::sregex_iterator(source.begin(), source.end(), pathRe); it != std::sregex_iterator(); ++it) {
    std::string const tag = it->str(0);
    std::string const d = attr(tag, "d");
    if (d.empty()) {
      continue;
    }

    Color fill = Colors::black;
    std::string const className = attr(tag, "class");
    if (auto found = classFills.find(className); found != classFills.end()) {
      fill = found->second;
    } else if (std::string inlineFill = attr(tag, "fill"); !inlineFill.empty() && inlineFill != "none") {
      fill = colorFromHex(inlineFill);
    }

    loaded.nodes.emplace_back(svg::path(d, FillStyle::solid(fill)));
  }

  loaded.status += " (" + std::to_string(loaded.nodes.size()) + " paths)";
  return loaded;
}

struct SvgDemoRoot {
  std::string svgPath = kDefaultSvgPath;

  auto body() const {
    auto theme = useEnvironment<ThemeKey>();
    LoadedSvg loaded = loadSvgForDemo(svgPath);

    Element preview = Svg{
        .viewBox = loaded.viewBox,
        .preserveAspectRatio = SvgPreserveAspectRatio::Meet,
        .children = std::move(loaded.nodes),
    }
        .flex(1.f, 1.f, 0.f);

    return VStack{
        .spacing = theme().space4,
        .alignment = Alignment::Stretch,
        .children = children(
            VStack{
                .spacing = theme().space1,
                .alignment = Alignment::Start,
                .children = children(
                    Text{
                        .text = "SVG Demo",
                        .font = Font{.size = 30.f, .weight = 700.f},
                        .color = Color::primary(),
                    },
                    Text{
                        .text = loaded.status,
                        .font = Font::footnote(),
                        .color = loaded.nodes.empty() ? Color::warning() : Color::secondary(),
                        .wrapping = TextWrapping::Wrap,
                    }
                ),
            },
            Card{
                .child = std::move(preview),
                .style = Card::Style{
                    .padding = theme().space5,
                    .cornerRadius = theme().radiusLarge,
                    .backgroundColor = Color::elevatedBackground(),
                    .borderColor = Color::separator(),
                },
            }
                .flex(1.f, 1.f, 0.f)
        ),
    }
        .padding(theme().space6)
        .fill(Color::windowBackground());
  }
};

} // namespace

int main(int argc, char* argv[]) {
  Application app(argc, argv);

  std::string svgPath = argc > 1 ? argv[1] : kDefaultSvgPath;
  auto& window = app.createWindow<Window>({
      .size = {980.f, 760.f},
      .title = "SVG Demo",
      .resizable = true,
  });
  window.setTheme(Theme::light());
  window.setView<SvgDemoRoot>(SvgDemoRoot{.svgPath = std::move(svgPath)});

  return app.exec();
}
