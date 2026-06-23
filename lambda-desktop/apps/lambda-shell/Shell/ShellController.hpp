#pragma once

#include "Shell/ShellConnection.hpp"
#include "Shell/ShellModels.hpp"
#include "Shell/ShellModel.hpp"

#include <Lambda/UI/Application.hpp>
#include <Lambda/UI/Window.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace lambda_shell {

enum class AudioControlAction;
struct ShellSystemStatusWatchers;
struct ShellNotificationWatcher;
struct ShellTrayStatusWatcher;

class ShellController {
public:
  ShellController(lambdaui::Application& app, ShellModel& model);
  ~ShellController();

  bool connectIpc();
  void setConfigReloadSource(std::filesystem::path path,
                             std::vector<AppRegistryEntry> apps,
                             ShellConfig config);
  void createProductionWindows();
  void setupPreviewWindow(lambdaui::Window& window, float width, float height);

  void openLauncher();
  void closeLauncher();

  ShellModel& model() noexcept { return model_; }
  ShellConnection& ipc() noexcept { return ipc_; }

private:
  void mountDockView();
  void mountProductionViews();
  void mountPreviewView();
  void requestRedraws();
  void requestDockRedraw();
  void requestDockMenuRedraw();
  void requestLauncherRedraw();
  void requestNotificationRedraw();
  [[nodiscard]] int measureDockClockWidth();
  [[nodiscard]] bool updateDockClockWidth();
  void resizeDockWindowIfNeeded();
  void handleIpcLine(std::string_view line);
  void checkShellConfigReload();
  [[nodiscard]] bool refreshSystemStatus();
  void setupSystemStatusWatchers();
  void setupNotificationWatcher();
  void setupTrayStatusWatcher();
  void updateTrayItems(std::vector<TrayStatusItem> items);
  void updateNotificationPolicy();
  void syncNotificationWindow();
  void scheduleNotificationTimeout(Notification const& notification);
  void cancelNotificationTimeout();
  void handleNotificationDismiss(std::uint64_t id);
  void handleNotificationAction(std::uint64_t id, std::string actionKey);
  void closeNotificationAsync(std::uint32_t id);
  void invokeNotificationActionAsync(std::uint32_t id, std::string actionKey);
  void performAudioControlAsync(AudioControlAction action);
  void performNetworkControlAsync(DockStatusAction action);
  void performBluetoothControlAsync(DockStatusAction action);
  void performMediaControlAsync(DockStatusAction action);
  void performShellActionAsync(std::string actionId);
  void queueVolumeAdjustment(int steps);
  void runVolumeAdjustmentWorker();
  void activateLauncherItem(DockItem const& item);
  void syncLauncherWindow();
  void openDockMenu(DockItem const& item);
  void syncDockMenuOverlay();
  void closeDockMenu();
  void openSessionMenu();
  void syncSessionMenuOverlay();
  void closeSessionMenu();
  void handleLauncherKey(lambdaui::InputEvent const& event);
  std::uint64_t nextRequestId();

  std::function<void(DockItem const&)> makeActivateCallback();
  std::function<void(DockItem const&)> makeShowDockMenuCallback();
  std::function<void(std::string const&, DockStatusAction)> makeStatusActionCallback();
  std::function<void(DockItem const&)> makeNewWindowCallback();
  std::function<void(DockItem const&)> makeTogglePinnedCallback();
  std::function<void(DockItem const&)> makeQuitCallback();
  void applyShellConfigToModel();
  bool saveShellConfig(ShellConfig const& config);

  lambdaui::Application& app_;
  ShellModel& model_;
  ShellConnection ipc_;
  std::optional<unsigned int> dockHandle_;
  std::optional<unsigned int> dockMenuHandle_;
  std::optional<unsigned int> launcherHandle_;
  std::optional<unsigned int> notificationHandle_;
  std::optional<unsigned int> previewHandle_;
  lambdaui::Window* dockWindow_ = nullptr;
  lambdaui::Window* dockMenuWindow_ = nullptr;
  lambdaui::Window* launcherWindow_ = nullptr;
  lambdaui::Window* notificationWindow_ = nullptr;
  float previewWidth_ = 960.f;
  float previewHeight_ = 620.f;
  bool launcherModalClaimed_ = false;
  bool lastLauncherOpen_ = false;
  bool dockMenuOpen_ = false;
  bool sessionMenuOpen_ = false;
  std::optional<DockItem> dockMenuItem_;
  float dockMenuOverlayWidth_ = 1.f;
  float dockMenuOverlayHeight_ = 1.f;
  int lastDockWidth_ = 0;
  int lastDockHeight_ = 0;
  std::uint64_t ipcPollId_ = 0;
  std::uint64_t clockTimerId_ = 0;
  std::uint64_t systemStatusTimerId_ = 0;
  std::uint64_t configReloadTimerId_ = 0;
  std::uint64_t notificationTimeoutTimerId_ = 0;
  std::uint64_t notificationTimeoutNotificationId_ = 0;
  std::uint64_t nextRequestId_ = 1;
  std::unique_ptr<ShellSystemStatusWatchers> systemStatusWatchers_;
  std::unique_ptr<ShellNotificationWatcher> notificationWatcher_;
  std::unique_ptr<ShellTrayStatusWatcher> trayStatusWatcher_;
  std::vector<TrayStatusItem> trayItems_;
  NotificationCenterModel notificationCenter_;
  std::filesystem::path configPath_;
  std::optional<std::filesystem::file_time_type> configLastWrite_;
  std::vector<AppRegistryEntry> appRegistry_;
  ShellConfig shellConfig_;
  std::string lastSnapshotLine_;
  std::atomic<int> pendingVolumeSteps_{0};
  std::atomic<bool> volumeAdjustmentWorkerRunning_{false};
};

lambdaui::WindowConfig dockWindowConfig(int width,
                                      int itemSize = kDockIconSize,
                                      int bottomGap = kDockBottom,
                                      int cornerRadius = kDockCornerRadius,
                                      DockMaterialConfig material = {},
                                      bool fullWidth = false);
lambdaui::WindowConfig dockMenuWindowConfig();
lambdaui::WindowConfig launcherWindowConfig();

} // namespace lambda_shell
