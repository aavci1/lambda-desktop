#include <Lambda.hpp>
#include <Lambda/UI/Application.hpp>
#include <Lambda/UI/WindowUI.hpp>
#include <Lambda/Graphics/TextSystem.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/UI.hpp>
#include <Lambda/UI/ViewModifiers.hpp>
#include <Lambda/UI/Views/Grid.hpp>
#include <Lambda/UI/Views/Render.hpp>

#include <array>
#include <vector>

using namespace lambdaui;

namespace {

struct DemoCell {
  BlendMode mode;
  const char* title;
};

// Row-major grid: each cell shows cyan + magenta circles; second circle uses `mode`.
constexpr std::array<DemoCell, 12> kGrid{{
    {BlendMode::Normal, "Normal"},
    {BlendMode::Multiply, "Multiply"},
    {BlendMode::Screen, "Screen"},
    {BlendMode::Darken, "Darken"},
    {BlendMode::Lighten, "Lighten"},
    {BlendMode::DstOver, "DstOver"},
    {BlendMode::SrcIn, "SrcIn"},
    {BlendMode::DstIn, "DstIn"},
    {BlendMode::SrcOut, "SrcOut"},
    {BlendMode::DstOut, "DstOut"},
    {BlendMode::Src, "Src"},
    {BlendMode::Dst, "Dst"},
}};

struct BlendCell : ViewModifiers<BlendCell> {
  static constexpr float kMargin = 24.f;
  static constexpr float kGap = 10.f;
  /// Fallback viewport used on the first build before `useBounds()` has a prior layout rect.
  static constexpr float kDefaultWinW = 800.f;
  static constexpr float kDefaultWinH = 800.f;

  BlendMode mode = BlendMode::Normal;
  const char* title = "";

  auto body() const {
    BlendMode const cellMode = mode;
    std::string const label = title ? title : "";
    return Render{
        .measureFn =
            [](LayoutConstraints const& c, LayoutHints const&) {
              float const w = std::isfinite(c.maxWidth) && c.maxWidth > 0.f ? c.maxWidth : 0.f;
              float const h = std::isfinite(c.maxHeight) && c.maxHeight > 0.f ? c.maxHeight : w;
              return Size{w, h};
            },
        .draw =
            [cellMode, label](Canvas& canvas, Rect cell) {
              float const pad = 8.f;
              float const innerX = cell.x + pad;
              float const innerY = cell.y + pad;
              float const innerW = std::max(cell.width - pad * 2.f, 4.f);
              float const innerH = std::max(cell.height - pad * 2.f, 4.f);
              Rect const inner = Rect::sharp(innerX, innerY, innerW, innerH);

              canvas.setBlendMode(BlendMode::Normal);
              canvas.drawRect(inner, {}, FillStyle::solid(Color::rgb(210, 210, 218)), StrokeStyle::none());

              float const r = std::min(innerW, innerH) * 0.22f;
              Point const c1{inner.x + innerW * 0.34f, inner.y + innerH * 0.5f};
              Point const c2{inner.x + innerW * 0.66f, inner.y + innerH * 0.5f};

              canvas.drawCircle(c1, r, FillStyle::solid(Color{0.05f, 0.75f, 0.85f, 0.62f}), StrokeStyle::none());

              canvas.setBlendMode(cellMode);
              canvas.drawCircle(c2, r, FillStyle::solid(Color{0.92f, 0.15f, 0.55f, 0.62f}), StrokeStyle::none());

              canvas.setBlendMode(BlendMode::Normal);
              canvas.drawRect(cell, CornerRadius(4.f, 4.f, 4.f, 4.f), FillStyle::none(),
                              StrokeStyle::solid(Color::rgb(90, 90, 100), 1.2f));

              Theme const theme = Theme::light();
              Font const labelFont = theme.headlineFont;
              auto labelLayout =
                  Application::instance().textSystem().layout(label, labelFont, theme.labelColor, 0.f);
              Size const m = labelLayout->measuredSize;
              float const y = cell.y + cell.height - 16.f - m.height;
              float const x = cell.x + (cell.width - m.width) * 0.5f;
              canvas.drawTextLayout(*labelLayout, Point{x, y});
        },
    };
  }
};

struct BlendDemoView {
  auto body() const {
    Rect const bounds = useBounds();
    float const viewportWidth = bounds.width > 0.f ? bounds.width : BlendCell::kDefaultWinW;
    float const viewportHeight = bounds.height > 0.f ? bounds.height : BlendCell::kDefaultWinH;
    float const innerWidth = std::max(0.f, viewportWidth - BlendCell::kMargin * 2.f);
    float const innerHeight = std::max(0.f, viewportHeight - BlendCell::kMargin * 2.f);
    std::size_t constexpr columns = 4u;
    std::size_t const rows = (kGrid.size() + columns - 1u) / columns;
    float const cellWidth =
        columns > 0u ? std::max(0.f, (innerWidth - static_cast<float>(columns - 1u) * BlendCell::kGap) /
                                      static_cast<float>(columns))
                     : 0.f;
    float const cellHeight =
        rows > 0u ? std::max(0.f, (innerHeight - static_cast<float>(rows - 1u) * BlendCell::kGap) /
                                      static_cast<float>(rows))
                  : 0.f;

    std::vector<Element> cells;
    cells.reserve(kGrid.size());
    for (DemoCell const& d : kGrid) {
      cells.push_back(Element{BlendCell{.mode = d.mode, .title = d.title}}.size(cellWidth, cellHeight));
    }
    return Grid{
                    .columns = columns,
                    .horizontalSpacing = BlendCell::kGap,
                    .verticalSpacing = BlendCell::kGap,
                    .horizontalAlignment = Alignment::Stretch,
                    .verticalAlignment = Alignment::Stretch,
                    .children = std::move(cells),
    }.padding(BlendCell::kMargin);
  }
};

} // namespace

int main(int argc, char* argv[]) {
  Application app(argc, argv);

  app.createWindow<Window>({
      .size = {800, 800},
      .title = "Lambda — blend modes",
  }).setView(BlendDemoView{});

  return app.exec();
}
