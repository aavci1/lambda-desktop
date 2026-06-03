#include <Lambda.hpp>
#include <Lambda/UI/Window.hpp>

#include "TerminalCore.hpp"
#include "TerminalSession.hpp"

int main(int argc, char* argv[]) {
  lambda::Application app(argc, argv);
  app.setName("lambda-terminal");
  auto const preferences = lambda_terminal::loadTerminalPreferences();
  auto const profile = lambda_terminal::activeTerminalProfile(preferences.preferences);

  auto& window = app.createWindow<lambda::Window>({
      .size = {920.f, 560.f},
      .title = "Terminal",
      .titlebar = lambda::WindowTitlebarMode::System,
      .resizable = true,
  });
  if (profile.config.blackGlassBackground) {
    window.setBackground(lambda::WindowBackground::glassEffect(lambda::GlassEffectOptions{
        .blurRadius = profile.config.blackGlassBlurRadius,
        .baseColor = profile.config.blackGlassTint,
        .tintColor = profile.config.blackGlassTint,
        .borderColor = lambda::Color{1.f, 1.f, 1.f, 0.16f},
        .opacity = 1.f,
    }));
  } else {
    window.setBackground(lambda::WindowBackground::solid(profile.config.blackGlassTint));
  }

  lambda_terminal::installTerminalView(app, window, profile.config);
  return app.exec();
}
