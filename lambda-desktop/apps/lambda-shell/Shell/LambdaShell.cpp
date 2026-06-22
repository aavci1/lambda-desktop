#include "Shell/ShellController.hpp"
#include "Shell/ShellAppRegistry.hpp"
#include "Shell/ShellConnection.hpp"
#include "Shell/ShellModels.hpp"

#include <Lambda/UI/Application.hpp>

#include <cstdio>
#include <cstdlib>
#include <exception>

int main(int argc, char* argv[]) {
  try {
    std::string const display = lambda_shell::compositorDisplayName();
    if (!display.empty()) {
      setenv("WAYLAND_DISPLAY", display.c_str(), 1);
    }

    lambda::Application app(argc, argv);
    app.setName("lambda-shell");

    lambda_shell::ShellModel model;
    auto shellConfig = lambda_shell::loadShellConfig();
    auto appRegistry = lambda_shell::buildDefaultAppRegistry(
        lambda_shell::defaultLocalLambdaAppDirs(),
        lambda_shell::defaultXdgApplicationDirs(),
        lambda_shell::executableInPath);
    model.setDockItems(appRegistry, shellConfig.config);

    lambda_shell::ShellController controller(app, model);
    controller.setConfigReloadSource(shellConfig.path, appRegistry, shellConfig.config);
    if (!controller.connectIpc()) {
      std::string const& detail = controller.ipc().lastErrorMessage();
      if (detail.empty()) {
        std::fprintf(stderr, "lambda-shell: failed to connect lambda-window-manager shell IPC\n");
      } else {
        std::fprintf(stderr, "lambda-shell: failed to connect lambda-window-manager shell IPC: %s\n", detail.c_str());
      }
      return 2;
    }

    controller.createProductionWindows();
    return app.exec();
  } catch (std::exception const& error) {
    std::fprintf(stderr, "lambda-shell: %s\n", error.what());
    return 1;
  }
}
