#pragma once

#include "Shell/ShellConnection.hpp"
#include "Shell/ShellModels.hpp"
#include "Shell/ShellModel.hpp"

#include <Flux/UI/Application.hpp>
#include <Flux/UI/Window.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace lambda_shell {

class ShellController {
public:
  ShellController(flux::Application& app, ShellModel& model);

  bool connectIpc();
  void setConfigReloadSource(std::filesystem::path path,
                             std::vector<AppRegistryEntry> apps,
                             ShellConfig config);
  void createProductionWindows();
  void setupPreviewWindow(flux::Window& window, float width, float height);

  void openLauncher();
  void closeLauncher();

  ShellModel& model() noexcept { return model_; }
  ShellConnection& ipc() noexcept { return ipc_; }

private:
  void mountProductionViews();
  void mountPreviewView();
  void requestRedraws();
  void requestTopBarRedraw();
  void requestDockRedraw();
  void requestLauncherRedraw();
  void handleIpcLine(std::string_view line);
  void checkShellConfigReload();
  void syncLauncherWindow();
  void handleLauncherKey(flux::InputEvent const& event);
  std::uint64_t nextRequestId();

  std::function<void(DockItem const&)> makeActivateCallback();

  flux::Application& app_;
  ShellModel& model_;
  ShellConnection ipc_;
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
  std::uint64_t clockTimerId_ = 0;
  std::uint64_t configReloadTimerId_ = 0;
  std::uint64_t nextRequestId_ = 1;
  std::filesystem::path configPath_;
  std::optional<std::filesystem::file_time_type> configLastWrite_;
  std::vector<AppRegistryEntry> appRegistry_;
  ShellConfig shellConfig_;
  std::string lastSnapshotLine_;
};

flux::WindowConfig topBarWindowConfig();
flux::WindowConfig dockWindowConfig(int width);
flux::WindowConfig launcherWindowConfig();

} // namespace lambda_shell
