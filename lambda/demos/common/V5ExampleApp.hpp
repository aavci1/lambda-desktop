#pragma once

#include <Lambda.hpp>
#include <Lambda/UI/WindowUI.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/UI.hpp>
#include <Lambda/UI/Views/For.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/VStack.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace lambda::examples {

struct V5ExampleConfig {
  std::string title;
  std::string summary;
  std::vector<std::string> rows;
  Size size{820.f, 620.f};
};

struct V5ExampleRoot {
  V5ExampleConfig config;

  Element body() const {
    auto theme = useEnvironment<ThemeKey>();
    auto rows = useState(config.rows);

    return VStack{
        .spacing = theme().space5,
        .alignment = Alignment::Center,
        .children = children(
            Text{
                .text = config.title,
                .font = Font::largeTitle(),
                .color = Color::primary(),
                .horizontalAlignment = HorizontalAlignment::Center,
            },
            Text{
                .text = config.summary,
                .font = Font::body(),
                .color = Color::secondary(),
                .horizontalAlignment = HorizontalAlignment::Center,
                .wrapping = TextWrapping::Wrap,
            }.width(560.f),
            Text{
                .text = "Reverse Items",
                .font = Font::headline(),
                .color = Color::primary(),
                .horizontalAlignment = HorizontalAlignment::Center,
            }.padding(theme().space3)
             .fill(Color::controlBackground())
             .stroke(Color::separator(), 1.f)
             .cornerRadius(theme().radiusFull)
             .onTap([rows] {
               auto next = rows.peek();
               std::reverse(next.begin(), next.end());
               rows.set(std::move(next));
             }),
            Element{For(
                rows,
                [](std::string const& row) { return row; },
                [theme](std::string const& row, Signal<std::size_t> index) {
                  Bindable<float> width{[index] {
                    return 320.f + static_cast<float>(index()) * 20.f;
                  }};
                  return HStack{
                      .spacing = theme().space3,
                      .alignment = Alignment::Center,
                      .children = children(
                          Rectangle{}
                              .size(12.f, 36.f)
                              .fill(Color::accent())
                              .cornerRadius(theme().radiusSmall),
                          Text{
                              .text = row,
                              .font = Font::body(),
                              .color = Color::primary(),
                          })}.padding(theme().space3)
                         .width(std::move(width))
                         .fill(Color::controlBackground())
                         .stroke(Color::separator(), 1.f)
                         .cornerRadius(theme().radiusMedium);
                },
                theme().space3)}
                .height(280.f))}
        .padding(theme().space7)
        .fill(Color::windowBackground());
  }
};

inline int runV5Example(int argc, char* argv[], V5ExampleConfig config) {
  Application app(argc, argv);

  auto& window = app.createWindow<Window>({
      .size = config.size,
      .title = config.title,
      .resizable = true,
  });

  window.setView<V5ExampleRoot>(V5ExampleRoot{std::move(config)});
  return app.exec();
}

} // namespace lambda::examples
