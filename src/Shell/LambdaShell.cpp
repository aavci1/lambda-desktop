#include "Shell/ShellController.hpp"
#include "Shell/ShellIpc.hpp"

#include <Flux/UI/Application.hpp>

#include <cstdlib>
#include <stdexcept>

int main(int argc, char* argv[]) {
  std::string const display = lambda_shell::compositorDisplayName();
  if (!display.empty()) {
    setenv("WAYLAND_DISPLAY", display.c_str(), 1);
  }

  flux::Application app(argc, argv);
  app.setName("lambda-shell");

  lambda_shell::ShellModel model;
  lambda_shell::ShellController controller(app, model);
  if (!controller.connectIpc()) {
    throw std::runtime_error("failed to connect lambda-window-manager shell IPC");
  }

  controller.createProductionWindows();
  return app.exec();
}
