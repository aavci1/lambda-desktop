#include "Shell/UI/LambdaCommandLauncher.hpp"
#include "Shell/UI/LambdaDock.hpp"
#include "Shell/UI/LambdaShellTypes.hpp"
#include "Shell/UI/LambdaTopBar.hpp"

#include <Flux.hpp>
#include <Flux/UI/Theme.hpp>
#include <Flux/UI/UI.hpp>
#include <Flux/UI/Views/Views.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

using namespace flux;

namespace {

constexpr int kWindowWidth = 960;
constexpr int kWindowHeight = 620;

Color rgba(float r, float g, float b, float a) {
  return Color{r, g, b, a};
}

std::vector<lambda_shell::DockItem> previewDockItems(std::string const& activeApp) {
  using lambda_shell::DockItem;
  std::vector<DockItem> items{
      DockItem{.id = "launcher", .kind = "launcher", .label = "Launcher"},
      DockItem{.id = "files", .kind = "app", .label = "Files", .appId = "files", .running = true},
      DockItem{.id = "browser", .kind = "app", .label = "Browser", .appId = "browser", .running = true},
      DockItem{.id = "terminal", .kind = "app", .label = "Terminal", .appId = "terminal", .running = true},
      DockItem{.id = "settings", .kind = "app", .label = "Settings", .appId = "settings"},
      DockItem{.id = "calendar", .kind = "app", .label = "Calendar", .appId = "calendar"},
      DockItem{.id = "separator", .kind = "separator", .label = "Separator"},
      DockItem{.id = "mail", .kind = "app", .label = "Mail", .appId = "mail", .running = true},
      DockItem{.id = "music", .kind = "app", .label = "Music", .appId = "music"},
      DockItem{.id = "trash", .kind = "trash", .label = "Trash", .appId = "trash"},
  };
  for (auto& item : items) {
    item.focused = !activeApp.empty() && item.appId == activeApp;
  }
  return items;
}

struct PreviewAppCard {
  std::string activeApp;

  Element body() const {
    std::string title = activeApp.empty() ? "Select an app from the dock or launcher" : "Active app: " + activeApp;
    return VStack{
               .spacing = 10.f,
               .alignment = Alignment::Center,
               .children = children(
                   Text{
                       .text = "Lambda Shell UI Preview",
                       .font = Font{.size = 32.f, .weight = 820.f},
                       .color = rgba(0.94f, 0.96f, 1.f, 1.f),
                       .horizontalAlignment = HorizontalAlignment::Center,
                   },
                   Text{
                       .text = std::move(title),
                       .font = Font{.size = 16.f, .weight = 620.f},
                       .color = rgba(0.70f, 0.76f, 0.88f, 1.f),
                       .horizontalAlignment = HorizontalAlignment::Center,
                   })}
        .size(520.f, 150.f)
        .padding(24.f)
        .fill(FillStyle::linearGradient(rgba(0.12f, 0.17f, 0.28f, 0.92f),
                                        rgba(0.05f, 0.07f, 0.13f, 0.92f),
                                        {0.f, 0.f},
                                        {1.f, 1.f}))
        .stroke(StrokeStyle::solid(rgba(1.f, 1.f, 1.f, 0.16f), 1.f))
        .cornerRadius(28.f);
  }
};

struct LambdaShellPreviewRoot {
  Element body() const {
    auto launcherOpen = useState(false);
    auto activeApp = useState(std::string{"browser"});

    std::string const active = activeApp();
    std::vector<lambda_shell::DockItem> items = previewDockItems(active);
    int const dockWidth = lambda_shell::dockWidth(items);
    float const dockX = static_cast<float>((kWindowWidth - dockWidth) / 2);
    float const dockY = static_cast<float>(kWindowHeight - lambda_shell::dockHeight() - lambda_shell::kDockBottom);

    auto activate = [activeApp, launcherOpen](lambda_shell::DockItem const& item) {
      if (!item.appId.empty()) {
        activeApp.set(item.appId);
      }
      launcherOpen.set(false);
    };

    std::vector<Element> layers;
    layers.push_back(Rectangle{}
        .size(static_cast<float>(kWindowWidth), static_cast<float>(kWindowHeight))
        .fill(FillStyle::linearGradient(rgba(0.05f, 0.07f, 0.12f, 1.f),
                                        rgba(0.14f, 0.20f, 0.33f, 1.f),
                                        {0.f, 0.f},
                                        {1.f, 1.f})));
    layers.push_back(Element{lambda_shell::LambdaTopBar{lambda_shell::TopBarProps{
        .title = "Lambda",
        .timeText = "Fri May 22 19:30",
        .onOpenLauncher = [launcherOpen] { launcherOpen.set(true); },
    }}}.size(static_cast<float>(kWindowWidth), static_cast<float>(lambda_shell::kTopBarHeight))
                    .position(0.f, 0.f));
    layers.push_back(Element{PreviewAppCard{.activeApp = active}}
                         .position(220.f, 185.f));
    layers.push_back(Element{lambda_shell::LambdaDock{lambda_shell::DockProps{
        .items = items,
        .hoverIndex = -1,
        .width = dockWidth,
        .onOpenLauncher = [launcherOpen] { launcherOpen.set(true); },
        .onActivateItem = activate,
    }}}.position(dockX, dockY));

    if (launcherOpen()) {
      layers.push_back(Element{lambda_shell::LambdaCommandLauncher{lambda_shell::CommandLauncherProps{
          .items = items,
          .query = "",
          .highlighted = 0,
          .width = kWindowWidth,
          .height = kWindowHeight,
          .open = true,
          .onActivateResult = activate,
          .onDismiss = [launcherOpen] { launcherOpen.set(false); },
      }}}.position(0.f, 0.f));
    }

    return ZStack{
        .children = std::move(layers),
    }.size(static_cast<float>(kWindowWidth), static_cast<float>(kWindowHeight));
  }
};

} // namespace

int main(int argc, char* argv[]) {
  Application app(argc, argv);
  app.setName("Lambda Shell Preview");

  auto& window = app.createWindow<Window>({
      .size = {static_cast<float>(kWindowWidth), static_cast<float>(kWindowHeight)},
      .title = "Lambda Shell Preview",
      .resizable = false,
  });
  window.setView<LambdaShellPreviewRoot>();
  return app.exec();
}
