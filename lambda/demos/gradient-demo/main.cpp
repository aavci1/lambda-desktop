#include <Lambda.hpp>
#include <Lambda/UI/Application.hpp>
#include <Lambda/UI/WindowUI.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/UI.hpp>
#include <Lambda/UI/Views/Views.hpp>

#include <cstdint>
#include <string>

using namespace lambda;

namespace {

Color rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, float a = 1.f) {
  Color c = Color::rgb(r, g, b);
  c.a = a;
  return c;
}

struct GradientSwatch : ViewModifiers<GradientSwatch> {
  std::string title;
  std::string caption;
  FillStyle fill = FillStyle::none();
  CornerRadius radius{16.f};

  auto body() const {
    auto theme = useEnvironment<ThemeKey>();
    return VStack{
        .spacing = theme().space3,
        .alignment = Alignment::Stretch,
        .children = children(
            Rectangle{}
                .height(150.f)
                .fill(fill)
                .cornerRadius(radius)
                .stroke(rgba(255, 255, 255, 0.16f), 1.f),
            VStack{
                .spacing = theme().space1,
                .alignment = Alignment::Start,
                .children = children(
                    Text{
                        .text = title,
                        .font = Font{.size = 15.f, .weight = 650.f},
                        .color = Color::primary(),
                    },
                    Text{
                        .text = caption,
                        .font = Font::caption(),
                        .color = Color::secondary(),
                        .wrapping = TextWrapping::Wrap,
                    }
                ),
            }
        ),
    }
        .padding(theme().space3)
        .fill(rgba(255, 255, 255, 0.055f))
        .stroke(rgba(255, 255, 255, 0.12f), 1.f)
        .cornerRadius(CornerRadius{20.f});
  }
};

struct GradientDemoRoot {
  auto body() const {
    auto theme = useEnvironment<ThemeKey>();
    return VStack{
        .spacing = theme().space5,
        .alignment = Alignment::Stretch,
        .children = children(
            VStack{
                .spacing = theme().space2,
                .alignment = Alignment::Start,
                .children = children(
                    Text{
                        .text = "Gradients",
                        .font = Font{.size = 32.f, .weight = 700.f},
                        .color = Color::primary(),
                    },
                    Text{
                        .text = "Linear, radial, and conical fills rendered by the same rounded-rect pipeline.",
                        .font = Font::body(),
                        .color = Color::secondary(),
                    }
                ),
            },
            Grid{
                .columns = 3,
                .horizontalSpacing = theme().space4,
                .verticalSpacing = theme().space4,
                .horizontalAlignment = Alignment::Stretch,
                .verticalAlignment = Alignment::Stretch,
                .children = children(
                    GradientSwatch{
                        .title = "Linear",
                        .caption = "Four ordered stops along a diagonal axis.",
                        .fill = FillStyle::linearGradient({
                            GradientStop{0.00f, Color::hex(0xFF453A)},
                            GradientStop{0.32f, Color::hex(0xFF9F0A)},
                            GradientStop{0.66f, Color::hex(0x0A84FF)},
                            GradientStop{1.00f, Color::hex(0x7D5FFF)},
                        }, Point{0.f, 0.f}, Point{1.f, 1.f}),
                    },
                    GradientSwatch{
                        .title = "Radial",
                        .caption = "Unit-space center and radius, clipped by rounded corners.",
                        .fill = FillStyle::radialGradient({
                            GradientStop{0.00f, rgba(255, 255, 255, 0.95f)},
                            GradientStop{0.18f, Color::hex(0x67E8F9)},
                            GradientStop{0.58f, Color::hex(0x2563EB)},
                            GradientStop{1.00f, Color::hex(0x111827)},
                        }, Point{0.38f, 0.34f}, 0.72f),
                    },
                    GradientSwatch{
                        .title = "Conical",
                        .caption = "Angular interpolation around a normalized center point.",
                        .fill = FillStyle::conicalGradient({
                            GradientStop{0.00f, Color::hex(0xFF453A)},
                            GradientStop{0.33f, Color::hex(0x28CD41)},
                            GradientStop{0.66f, Color::hex(0x0A84FF)},
                            GradientStop{1.00f, Color::hex(0xFF453A)},
                        }, Point{0.5f, 0.5f}, -0.75f),
                    },
                    GradientSwatch{
                        .title = "Soft Control",
                        .caption = "Radial fill with a secondary outer tint.",
                        .fill = FillStyle::radialGradient(
                            rgba(255, 255, 255, 0.86f),
                            Color::hex(0x3A7BD5),
                            Point{0.5f, 0.42f},
                            0.58f),
                        .radius = CornerRadius{999.f},
                    },
                    GradientSwatch{
                        .title = "Glass Edge",
                        .caption = "A compact vertical lighting ramp.",
                        .fill = FillStyle::linearGradient({
                            GradientStop{0.00f, rgba(255, 255, 255, 0.72f)},
                            GradientStop{0.16f, rgba(255, 255, 255, 0.16f)},
                            GradientStop{0.58f, rgba(10, 132, 255, 0.20f)},
                            GradientStop{1.00f, rgba(17, 24, 39, 0.92f)},
                        }, Point{0.f, 0.f}, Point{0.f, 1.f}),
                    },
                    GradientSwatch{
                        .title = "Dial",
                        .caption = "Conical color wheel inside a circular mask.",
                        .fill = FillStyle::conicalGradient({
                            GradientStop{0.00f, Color::hex(0xFF453A)},
                            GradientStop{0.28f, Color::hex(0xFFCC00)},
                            GradientStop{0.58f, Color::hex(0x32D74B)},
                            GradientStop{1.00f, Color::hex(0x0A84FF)},
                        }),
                        .radius = CornerRadius{999.f},
                    }
                ),
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

  auto& window = app.createWindow<Window>({
      .size = {1120.f, 760.f},
      .title = "Gradient Demo",
  });
  window.setTheme(Theme::dark());
  window.setView<GradientDemoRoot>();

  return app.exec();
}
