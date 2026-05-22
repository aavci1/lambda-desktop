#pragma once

#include "Shell/ShellIpc.hpp"
#include "Shell/ShellModel.hpp"

#include <Flux/UI/Application.hpp>
#include <Flux/UI/Window.hpp>

#include <cstdint>
#include <functional>
#include <optional>

namespace lambda_shell {

class ShellController {
public:
  ShellController(flux::Application& app, ShellModel& model);

  bool connectIpc();
  void createProductionWindows();
  void setupPreviewWindow(flux::Window& window, float width, float height);

  void openLauncher();
  void closeLauncher();

  ShellModel& model() noexcept { return model_; }
  ShellIpc& ipc() noexcept { return ipc_; }

private:
  void mountProductionViews();
  void mountPreviewView();
  void requestRedraws();
  void requestTopBarRedraw();
  void requestDockRedraw();
  void requestLauncherRedraw();
  void handleIpcLine(std::string_view line);
  void syncLauncherWindow();
  void handleLauncherKey(flux::InputEvent const& event);

  std::function<void(DockItem const&)> makeActivateCallback();

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
  float previewWidth_ = 960.f;
  float previewHeight_ = 620.f;
  bool launcherModalClaimed_ = false;
  bool lastLauncherOpen_ = false;
  int lastDockWidth_ = 0;
  std::uint64_t ipcPollId_ = 0;
};

flux::WindowConfig topBarWindowConfig();
flux::WindowConfig dockWindowConfig(int width);
flux::WindowConfig launcherWindowConfig();

} // namespace lambda_shell
