#include <Flux.hpp>
#include <Flux/UI/EnvironmentKeys.hpp>
#include <Flux/UI/Theme.hpp>
#include <Flux/UI/UI.hpp>
#include <Flux/UI/Views/HStack.hpp>
#include <Flux/UI/Views/Rectangle.hpp>
#include <Flux/UI/Views/Spacer.hpp>
#include <Flux/UI/Views/Text.hpp>
#include <Flux/UI/Views/VStack.hpp>

#include <algorithm>
#include <functional>
#include <string>
#include <utility>
#include <vector>

using namespace flux;

namespace {

constexpr float kTitlebarHeight = 48.f;

std::string modeLabel(WindowDecorationMode mode) {
  switch (mode) {
  case WindowDecorationMode::System: return "System";
  case WindowDecorationMode::ClientSide: return "Client-side";
  case WindowDecorationMode::IntegratedTitlebar: return "Integrated";
  }
  return "Unknown";
}

struct ReservedInsets {
  float leading = 0.f;
  float trailing = 0.f;
};

ReservedInsets reservedInsets(WindowChromeMetrics const& chrome) {
  ReservedInsets insets;
  for (Rect const& rect : chrome.reservedRegions) {
    if (rect.x < 120.f) {
      insets.leading = std::max(insets.leading, rect.x + rect.width + 8.f);
    } else {
      insets.trailing = std::max(insets.trailing, rect.width + 8.f);
    }
  }
  return insets;
}

Element chromeDot(Color color, std::function<void()> onTap) {
  return Rectangle{}
      .size(14.f, 14.f)
      .fill(color)
      .cornerRadius(7.f)
      .cursor(Cursor::Hand)
      .onTap(std::move(onTap));
}

Element titlebar(Window* window,
                 std::string title,
                 std::string subtitle,
                 Reactive::Signal<bool> fullscreen,
                 Reactive::Signal<std::string> status,
                 Theme const& theme,
                 WindowChromeMetrics const& chrome) {
  ReservedInsets const reserved = reservedInsets(chrome);
  std::vector<Element> titlebarChildren;
  if (reserved.leading > 0.f) {
    titlebarChildren.push_back(Rectangle{}.width(reserved.leading));
  }
  titlebarChildren.push_back(
      VStack{
          .spacing = 1.f,
          .alignment = Alignment::Start,
          .children = children(
              Text{.text = std::move(title), .font = Font::headline(), .color = Color::primary()},
              Text{.text = std::move(subtitle), .font = Font::caption(), .color = Color::secondary()})}
          .flex(0.f, 1.f));
  titlebarChildren.push_back(Spacer{}.flex(1.f, 1.f));
  if (!chrome.nativeControlsVisible) {
    titlebarChildren.push_back(HStack{
        .spacing = 9.f,
        .alignment = Alignment::Center,
        .children = children(
            chromeDot(Color::hex(0xEB5757), [window, status] {
              status.set("Close requested from client-drawn chrome.");
              if (window) window->requestClose();
            }),
            chromeDot(Color::hex(0xF2C94C), [status] {
              status.set("Minimize is intentionally not wired in this demo.");
            }),
            chromeDot(Color::hex(0x36C275), [window, fullscreen, status] {
              bool const next = !fullscreen.peek();
              fullscreen.set(next);
              status.set(next ? "Entered fullscreen via client chrome." : "Exited fullscreen via client chrome.");
              if (window) window->setFullscreen(next);
            }))});
  }
  if (reserved.trailing > 0.f) {
    titlebarChildren.push_back(Rectangle{}.width(reserved.trailing));
  }

  return HStack{
             .spacing = theme.space3,
             .alignment = Alignment::Center,
             .children = std::move(titlebarChildren)}
      .height(kTitlebarHeight)
      .padding(0.f, theme.space4, 0.f, theme.space4)
      .fill(chrome.active ? Color::hex(0xF7F8FA) : Color::hex(0xEFEFF2))
      .stroke(Color::separator(), 1.f)
      .windowDragRegion();
}

struct WindowChromeDemoRoot {
  Window* window = nullptr;
  std::string title;
  std::string subtitle;

  Element body() const {
    auto theme = useEnvironment<ThemeKey>();
    auto chrome = useEnvironment<WindowChromeMetricsKey>();
    auto fullscreen = useState(false);
    auto status = useState(std::string{"Drag the custom titlebar. On Wayland this uses xdg_toplevel.move; on macOS it uses AppKit window dragging."});

    WindowChromeMetrics const metrics = chrome();
    return VStack{
               .spacing = 0.f,
               .alignment = Alignment::Stretch,
               .children = children(
                   titlebar(window, title, subtitle, fullscreen, status, theme(), metrics),
                   VStack{
                       .spacing = theme().space4,
                       .alignment = Alignment::Stretch,
                       .children = children(
                           Text{.text = "Decoration mode: " + modeLabel(metrics.decorationMode),
                                .font = Font::title3(),
                                .color = Color::primary()},
                           Text{.text = metrics.nativeControlsVisible
                                             ? "The platform/compositor owns the window controls; Flux titlebar content avoids the reserved region."
                                             : "The app owns the titlebar controls and requests native window operations through Flux.",
                                .font = Font::body(),
                                .color = Color::secondary(),
                                .wrapping = TextWrapping::Wrap},
                           Text{.text = [status] { return status(); },
                                .font = Font::callout(),
                                .color = Color::primary(),
                                .wrapping = TextWrapping::Wrap}
                               .padding(theme().space3)
                               .fill(Color::controlBackground())
                               .cornerRadius(theme().radiusMedium))}
                       .padding(theme().space6)
                       .flex(1.f))
           }
        .fill(Color::windowBackground());
  }
};

} // namespace

int main(int argc, char* argv[]) {
  Application app(argc, argv);
  app.setName("Window Chrome Demo");

  auto& clientSide = app.createWindow<Window>({
      .size = {720.f, 420.f},
      .title = "Flux CSD Demo",
      .decorationMode = WindowDecorationMode::ClientSide,
      .resizable = true,
  });
  clientSide.setView(WindowChromeDemoRoot{
      .window = &clientSide,
      .title = "Client-side decorations",
      .subtitle = "Flux draws all controls",
  });

  auto& integrated = app.createWindow<Window>({
      .size = {720.f, 420.f},
      .title = "Flux Integrated Titlebar Demo",
      .decorationMode = WindowDecorationMode::IntegratedTitlebar,
      .resizable = true,
  });
  integrated.setView(WindowChromeDemoRoot{
      .window = &integrated,
      .title = "Integrated custom titlebar",
      .subtitle = "Native or compositor controls are reserved",
  });

  return app.exec();
}
