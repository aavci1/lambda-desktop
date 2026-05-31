#pragma once

#include "Shell/ShellConnection.hpp"
#include "Shell/ShellModels.hpp"
#include "Shell/ShellModel.hpp"

#include <Lambda/UI/Application.hpp>
#include <Lambda/UI/Window.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace lambda_shell {

class ShellController {
public:
  ShellController(lambda::Application& app, ShellModel& model);

  bool connectIpc();
  void setConfigReloadSource(std::filesystem::path path,
                             std::vector<AppRegistryEntry> apps,
                             ShellConfig config);
  void createProductionWindows();
  void setupPreviewWindow(lambda::Window& window, float width, float height);

  void openLauncher();
  void closeLauncher();

  ShellModel& model() noexcept { return model_; }
  ShellConnection& ipc() noexcept { return ipc_; }

private:
  void mountProductionViews();
  void mountPreviewView();
  void requestRedraws();
  void requestDockRedraw();
  void requestDockMenuRedraw();
  void requestLauncherRedraw();
  [[nodiscard]] int measureDockClockWidth();
  [[nodiscard]] bool updateDockClockWidth();
  void resizeDockWindowIfNeeded();
  void handleIpcLine(std::string_view line);
  void checkShellConfigReload();
  void syncLauncherWindow();
  void openDockMenu(DockItem const& item);
  void syncDockMenuOverlay();
  void closeDockMenu();
  void handleLauncherKey(lambda::InputEvent const& event);
  std::uint64_t nextRequestId();

  std::function<void(DockItem const&)> makeActivateCallback();
  std::function<void(DockItem const&)> makeShowDockMenuCallback();
  std::function<void(DockItem const&)> makeNewWindowCallback();
  std::function<void(DockItem const&)> makeTogglePinnedCallback();
  std::function<void(DockItem const&)> makeQuitCallback();
  void applyShellConfigToModel();
  bool saveShellConfig(ShellConfig const& config);

  lambda::Application& app_;
  ShellModel& model_;
  ShellConnection ipc_;
  std::optional<unsigned int> dockHandle_;
  std::optional<unsigned int> dockMenuHandle_;
  std::optional<unsigned int> launcherHandle_;
  std::optional<unsigned int> previewHandle_;
  lambda::Window* dockWindow_ = nullptr;
  lambda::Window* dockMenuWindow_ = nullptr;
  lambda::Window* launcherWindow_ = nullptr;
  float previewWidth_ = 960.f;
  float previewHeight_ = 620.f;
  bool launcherModalClaimed_ = false;
  bool lastLauncherOpen_ = false;
  bool dockMenuOpen_ = false;
  std::optional<DockItem> dockMenuItem_;
  float dockMenuOverlayWidth_ = 1.f;
  float dockMenuOverlayHeight_ = 1.f;
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

lambda::WindowConfig dockWindowConfig(int width,
                                      int bottomGap = kDockBottom,
                                      int cornerRadius = kDockCornerRadius);
lambda::WindowConfig dockMenuWindowConfig();
lambda::WindowConfig launcherWindowConfig();

} // namespace lambda_shell
