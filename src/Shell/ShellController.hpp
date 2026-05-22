#pragma once

#include "Shell/ShellIpc.hpp"
#include "Shell/ShellModel.hpp"

#include <Flux/UI/Application.hpp>
#include <Flux/UI/Window.hpp>

#include <cstdint>
#include <optional>

namespace lambda_shell {

enum class ShellSurfaceRole { TopBar, Dock, Launcher };

class ShellController {
public:
  ShellController(flux::Application& app, ShellModel& model);

  bool connectIpc();
  void createProductionWindows();
  void setupPreviewWindow(flux::Window& window, float width, float height);

  ShellModel& model() noexcept { return model_; }
  ShellIpc& ipc() noexcept { return ipc_; }

private:
  void requestShellRedraw();
  void handleIpcLine(std::string_view line);
  void syncLauncherWindow();
  void handleLauncherKey(flux::InputEvent const& event);

  flux::Application& app_;
  ShellModel& model_;
  ShellIpc ipc_;
  std::optional<unsigned int> topBarHandle_;
  std::optional<unsigned int> dockHandle_;
  std::optional<unsigned int> launcherHandle_;
  std::optional<unsigned int> previewHandle_;
  flux::Window* topBarWindow_ = nullptr;
  flux::Window* dockWindow_ = nullptr;
  flux::Window* launcherWindow_ = nullptr;
  bool launcherModalClaimed_ = false;
  std::uint64_t ipcPollId_ = 0;
  std::uint64_t clockTimerId_ = 0;
};

flux::WindowConfig topBarWindowConfig();
flux::WindowConfig dockWindowConfig(int width);
flux::WindowConfig launcherWindowConfig();

} // namespace lambda_shell
