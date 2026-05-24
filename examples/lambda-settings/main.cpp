#include "SettingsApp.hpp"

#include <Flux.hpp>
#include <Flux/UI/Application.hpp>
#include <Flux/UI/Window.hpp>

int main(int argc, char* argv[]) {
  flux::Application app(argc, argv);
  app.setName("settings");

  auto& window = app.createWindow<flux::Window>({
      .size = {780.f, 520.f},
      .title = "Settings",
      .titlebar = flux::WindowTitlebarMode::System,
      .resizable = true,
  });
  window.setBackground(flux::WindowBackground::glassEffect());
  window.setView<lambda_settings::SettingsAppRoot>();

  return app.exec();
}
