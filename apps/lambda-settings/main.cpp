#include "SettingsApp.hpp"

#include <Lambda.hpp>
#include <Lambda/UI/Application.hpp>
#include <Lambda/UI/Window.hpp>

int main(int argc, char* argv[]) {
  lambda::Application app(argc, argv);
  app.setName("lambda-settings");

  auto& window = app.createWindow<lambda::Window>({
      .size = {780.f, 520.f},
      .title = "Settings",
      .titlebar = lambda::WindowTitlebarMode::System,
      .resizable = true,
  });
  window.setView<lambda_settings::SettingsAppRoot>();

  return app.exec();
}
