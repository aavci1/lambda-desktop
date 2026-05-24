#include "FilesApp.hpp"

#include <Flux.hpp>
#include <Flux/UI/Application.hpp>
#include <Flux/UI/Window.hpp>

int main(int argc, char* argv[]) {
  flux::Application app(argc, argv);
  app.setName("files");

  auto& window = app.createWindow<flux::Window>({
      .size = {1040.f, 680.f},
      .title = "Files",
      .decorationMode = flux::WindowDecorationMode::IntegratedTitlebar,
      .resizable = true,
      .glass = {
          .enabled = true,
      },
  });
  if (window.platformCapabilities().supportsWindowGlass) {
    window.setClearColor(flux::Colors::transparent);
  }
  window.setView<lambda_files::FilesAppRoot>({.window = &window});

  return app.exec();
}
