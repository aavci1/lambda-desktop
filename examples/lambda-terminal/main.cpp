#include <Flux.hpp>
#include <Flux/UI/Window.hpp>

#include "TerminalCore.hpp"
#include "TerminalSession.hpp"

int main(int argc, char* argv[]) {
  flux::Application app(argc, argv);
  app.setName("lambda-terminal");
  auto const preferences = lambda_terminal::loadTerminalPreferences();
  auto const profile = lambda_terminal::activeTerminalProfile(preferences.preferences);

  auto& window = app.createWindow<flux::Window>({
      .size = {920.f, 560.f},
      .title = "Terminal",
      .titlebar = flux::WindowTitlebarMode::System,
      .resizable = true,
  });
  if (profile.config.blackGlassBackground) {
    window.setBackground(flux::WindowBackground::glassEffect(flux::GlassEffectOptions{
        .blurRadius = 46.f,
        .baseColor = profile.config.blackGlassTint,
        .tintColor = profile.config.blackGlassTint,
        .borderColor = flux::Color{1.f, 1.f, 1.f, 0.16f},
        .opacity = 1.f,
    }));
  } else {
    window.setBackground(flux::WindowBackground::solid(profile.config.blackGlassTint));
  }

  lambda_terminal::installTerminalView(app, window, profile.config);
  return app.exec();
}
