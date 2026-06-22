#include "FilesApp.hpp"

#include <Lambda.hpp>
#include <Lambda/UI/Application.hpp>
#include <Lambda/UI/Window.hpp>

int main(int argc, char* argv[]) {
  lambda::Application app(argc, argv);
  app.setName("lambda-files");

  auto& window = app.createWindow<lambda::Window>({
      .size = {1040.f, 680.f},
      .title = "Files",
      .titlebar = lambda::WindowTitlebarMode::Integrated,
      .resizable = true,
  });
  window.setBackground(lambda::WindowBackground::glassEffect());
  window.setView<lambda_files::FilesAppRoot>({.window = &window});

  return app.exec();
}
