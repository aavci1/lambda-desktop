#include <Lambda.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/UI.hpp>

using namespace lambda;

struct HelloRoot {
  auto body() const {
    auto theme = useEnvironment<ThemeKey>();
    return Text{
        .text = "Hello, World!",
        .font = Font::largeTitle(),
        .color = Color::primary(),
        .horizontalAlignment = HorizontalAlignment::Center,
        .verticalAlignment = VerticalAlignment::Center,
    };
  }
};

int main(int argc, char* argv[]) {
  Application app(argc, argv);

  auto& w = app.createWindow<Window>({
      .size = {400, 400},
      .title = "Hello, World!",
  });

  w.setView<HelloRoot>();

  return app.exec();
}
