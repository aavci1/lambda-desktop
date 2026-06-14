#include "Shell/ShellController.hpp"

#include "Shell/ShellAppRegistry.hpp"
#include "Shell/ShellAudioControl.hpp"
#include "Shell/ShellDesktopView.hpp"
#include "Shell/ShellJson.hpp"
#include "Shell/ShellSystemStatus.hpp"

#include "Shell/ShellIpc.hpp"
#include "Shell/ShellViews.hpp"

#include <Lambda/UI/EventQueue.hpp>
#include <Lambda/Graphics/TextSystem.hpp>
#include <Lambda/UI/Views/PopoverCalloutShape.hpp>
#include <Lambda/UI/WindowUI.hpp>

#include <Lambda/Core/Color.hpp>
#include <Lambda/System/BlueZ.hpp>
#include <Lambda/System/Logind.hpp>
#include <Lambda/System/MPRIS.hpp>
#include <Lambda/System/NetworkManager.hpp>
#include <Lambda/System/UPower.hpp>
#include <Lambda/UI/Events.hpp>
#include <Lambda/UI/KeyCodes.hpp>
#include <Lambda/UI/VisualTokens.hpp>

#include <any>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>
#include <utility>

namespace lambda_shell {

namespace {

using namespace lambda;
using namespace lambda::keys;

inline constexpr int kDockMenuGap = 10;
inline constexpr int kDockVolumeStepPercent = 5;
inline constexpr auto kDockVolumeCoalesceDelay = std::chrono::milliseconds{28};

struct ShellAudioCommandCompleted {
  bool changed = false;
};

struct ShellStatusCommandCompleted {
  bool changed = false;
};

std::optional<std::filesystem::file_time_type> configLastWriteTime(std::filesystem::path const& path) {
  if (path.empty()) return std::nullopt;
  std::error_code ec;
  auto const time = std::filesystem::last_write_time(path, ec);
  if (ec) return std::nullopt;
  return time;
}

bool appIdsMatch(std::string_view a, std::string_view b) {
  return shellAppIdMatches(a, b) || shellAppIdMatches(b, a);
}

LayerShellOptions layerBase(LayerShellLayer layer, char const* nameSpace) {
  LayerShellOptions options{};
  options.enabled = true;
  options.layer = layer;
  options.nameSpace = nameSpace;
  options.backgroundBlur = true;
  return options;
}

LayerShellOptions hiddenMenuLayer(char const* nameSpace) {
  LayerShellOptions layer = layerBase(LayerShellLayer::Overlay, nameSpace);
  layer.backgroundBlur = false;
  layer.anchorLeft = true;
  layer.anchorBottom = true;
  layer.inputRegion = LayerShellInputRegion{};
  return layer;
}

LayerShellOptions visibleMenuLayer(char const* nameSpace) {
  LayerShellOptions layer = layerBase(LayerShellLayer::Overlay, nameSpace);
  layer.anchorTop = true;
  layer.anchorBottom = true;
  layer.anchorLeft = true;
  layer.anchorRight = true;
  layer.chrome.glass.baseColor = VisualTokens::elevatedSurface;
  layer.chrome.glass.tintColor = Color{1.f, 1.f, 1.f, 0.06f};
  layer.chrome.glass.borderColor = VisualTokens::border;
  return layer;
}

} // namespace

struct ShellPowerStatusWatcher {
  ShellPowerStatusWatcher(lambda::Application& app, std::function<void()> onChanged)
      : upower(lambda::system::UPowerClient::connectSystem()),
        upowerPump(app, upower.bus()),
        upowerChanged(upower.watchStatusChanges(std::move(onChanged))) {}

  lambda::system::UPowerClient upower;
  lambda::dbus::BusEventPump upowerPump;
  lambda::system::UPowerStatusWatch upowerChanged;
};

struct ShellNetworkStatusWatcher {
  ShellNetworkStatusWatcher(lambda::Application& app, std::function<void()> onChanged)
      : network(lambda::system::NetworkManagerClient::connectSystem()),
        networkPump(app, network.bus()),
        networkManagerChanged(network.watchManagerChanged(std::move(onChanged))) {}

  lambda::system::NetworkManagerClient network;
  lambda::dbus::BusEventPump networkPump;
  lambda::dbus::Slot networkManagerChanged;
};

struct ShellBluetoothStatusWatcher {
  ShellBluetoothStatusWatcher(lambda::Application& app, std::function<void()> onChanged)
      : bluetooth(lambda::system::BlueZClient::connectSystem()),
        bluetoothPump(app, bluetooth.bus()),
        bluetoothChanged(bluetooth.watchAdapterOrDeviceChanged(std::move(onChanged))) {}

  lambda::system::BlueZClient bluetooth;
  lambda::dbus::BusEventPump bluetoothPump;
  lambda::dbus::Slot bluetoothChanged;
};

struct ShellMediaStatusWatcher {
  ShellMediaStatusWatcher(lambda::Application& app, std::function<void()> onChanged)
      : mpris(lambda::system::MPRISClient::connectSession()),
        mprisPump(app, mpris.bus()),
        mprisChanged(mpris.watchPlayerChanges(std::move(onChanged))) {}

  lambda::system::MPRISClient mpris;
  lambda::dbus::BusEventPump mprisPump;
  lambda::system::MPRISChangeWatch mprisChanged;
};

struct ShellSystemStatusWatchers {
  std::unique_ptr<ShellPowerStatusWatcher> power;
  std::unique_ptr<ShellNetworkStatusWatcher> network;
  std::unique_ptr<ShellBluetoothStatusWatcher> bluetooth;
  std::unique_ptr<ShellMediaStatusWatcher> media;
};

lambda::WindowConfig dockWindowConfig(int width,
                                      int itemSize,
                                      int bottomGap,
                                      int cornerRadius,
                                      DockMaterialConfig material,
                                      bool fullWidth) {
  LayerShellOptions layer = layerBase(LayerShellLayer::Overlay, "lambda.dock");
  layer.anchorBottom = true;
  layer.anchorLeft = fullWidth;
  layer.anchorRight = fullWidth;
  layer.marginBottom = fullWidth ? 0 : std::clamp(bottomGap, 0, 64);
  layer.chrome.style = fullWidth ? LayerShellChromeStyle::BlurPanel : LayerShellChromeStyle::BlurPanelBorder;
  layer.chrome.cornerRadius = fullWidth
                                  ? CornerRadius{}
                                  : CornerRadius{static_cast<float>(std::clamp(cornerRadius, 0, 48))};
  layer.chrome.glass.blurRadius = std::clamp(material.blurRadius, 0.f, 160.f);
  layer.chrome.glass.opacity = std::clamp(material.opacity, 0.f, 1.f);
  layer.chrome.glass.baseColor = material.baseColor;
  layer.chrome.glass.tintColor = material.tintColor;
  layer.chrome.glass.borderColor = material.borderColor;
  return WindowConfig{
      .size = {static_cast<float>(fullWidth ? 0 : std::max(width, 1)),
               static_cast<float>(dockHeight(itemSize))},
      .title = "Lambda Dock",
      .resizable = false,
      .layerShell = layer,
  };
}

lambda::WindowConfig launcherWindowConfig() {
  LayerShellOptions layer = layerBase(LayerShellLayer::Overlay, "lambda.command-launcher");
  layer.anchorTop = true;
  layer.anchorBottom = true;
  layer.anchorLeft = true;
  layer.anchorRight = true;
  return WindowConfig{
      .size = {1.f, 1.f},
      .title = "Lambda Command Launcher",
      .resizable = false,
      .layerShell = layer,
  };
}

lambda::WindowConfig dockMenuWindowConfig() {
  LayerShellOptions layer = hiddenMenuLayer("lambda.dock-menu");
  return WindowConfig{
      .size = {1.f, 1.f},
      .title = "Lambda Dock Menu",
      .resizable = false,
      .layerShell = layer,
  };
}

ShellController::ShellController(lambda::Application& app, ShellModel& model) : app_(app), model_(model) {
  if (model_.dockItems().empty()) model_.resetDockItems();
  lastDockWidth_ = dockWidth(model_.dockItems(), model_.dockClockWidth(), model_.dockItemSize());
  lastDockHeight_ = dockHeight(model_.dockItemSize());

  app_.eventQueue().on<lambda::WindowEvent>([this](lambda::WindowEvent const& event) {
    if (event.kind == lambda::WindowEvent::Kind::DpiChanged) {
      if (dockHandle_ && event.handle == *dockHandle_) {
        float const scale = std::max(event.dpiX > 0.f ? event.dpiX : event.dpi, 0.5f);
        if (model_.setDockDpiScale(scale)) {
          requestDockRedraw();
          if (model_.launcherOpen()) requestLauncherRedraw();
        }
      }
      return;
    }
    if (event.kind != lambda::WindowEvent::Kind::Resize) return;
    if (launcherHandle_ && event.handle == *launcherHandle_ && model_.launcherOpen()) {
      model_.setLauncherSize(event.size.width, event.size.height);
      if (event.size.width > 64.f && event.size.height > 64.f) {
        model_.setLauncherUiVisible(true);
      }
      requestLauncherRedraw();
      return;
    }
    if (dockMenuHandle_ && event.handle == *dockMenuHandle_) {
      float const nextWidth = std::max(1.f, event.size.width);
      float const nextHeight = std::max(1.f, event.size.height);
      bool const changed = std::abs(nextWidth - dockMenuOverlayWidth_) > 0.5f ||
                           std::abs(nextHeight - dockMenuOverlayHeight_) > 0.5f;
      dockMenuOverlayWidth_ = nextWidth;
      dockMenuOverlayHeight_ = nextHeight;
      if (changed && dockMenuOpen_) {
        syncDockMenuOverlay();
      }
    }
  });

  app_.eventQueue().on<lambda::TimerEvent>([this](lambda::TimerEvent const& event) {
    if (clockTimerId_ != 0 && event.timerId == clockTimerId_) {
      if (model_.refreshTimeText()) {
        if (updateDockClockWidth()) {
          resizeDockWindowIfNeeded();
        }
        requestDockRedraw();
      }
      return;
    }
    if (systemStatusTimerId_ != 0 && event.timerId == systemStatusTimerId_) {
      (void)refreshSystemStatus();
      return;
    }
    if (configReloadTimerId_ != 0 && event.timerId == configReloadTimerId_) {
      checkShellConfigReload();
    }
  });

  app_.eventQueue().on<lambda::InputEvent>([this](lambda::InputEvent const& event) {
    bool const forLauncher =
        (launcherHandle_ && event.handle == *launcherHandle_) || (previewHandle_ && event.handle == *previewHandle_);
    if (forLauncher && model_.launcherOpen()) {
      handleLauncherKey(event);
    }
  });

  app_.eventQueue().on<lambda::CustomEvent>([this](lambda::CustomEvent const& event) {
    if (event.type != 0x4c534850u) {
      return;
    }
    if (auto const* line = std::any_cast<std::string>(&event.payload)) {
      handleIpcLine(*line);
    }
  });

  app_.eventQueue().on<ShellAudioCommandCompleted>([this](ShellAudioCommandCompleted const& event) {
    if (event.changed) {
      (void)refreshSystemStatus();
    } else {
      requestDockRedraw();
    }
  });

  app_.eventQueue().on<ShellStatusCommandCompleted>([this](ShellStatusCommandCompleted const& event) {
    if (event.changed) {
      (void)refreshSystemStatus();
    } else {
      requestDockRedraw();
    }
  });
}

ShellController::~ShellController() = default;

void ShellController::setConfigReloadSource(std::filesystem::path path,
                                            std::vector<AppRegistryEntry> apps,
                                            ShellConfig config) {
  configPath_ = std::move(path);
  appRegistry_ = std::move(apps);
  shellConfig_ = std::move(config);
  configLastWrite_ = configLastWriteTime(configPath_);
  if (configReloadTimerId_ == 0) {
    configReloadTimerId_ = app_.scheduleRepeatingTimer(std::chrono::milliseconds{750});
  }
}

std::function<void(DockItem const&)> ShellController::makeActivateCallback() {
  return [this](DockItem const& item) {
    activateLauncherItem(item);
  };
}

std::function<void(DockItem const&)> ShellController::makeShowDockMenuCallback() {
  return [this](DockItem const& item) {
    openDockMenu(item);
  };
}

std::function<void(std::string const&, DockStatusAction)> ShellController::makeStatusActionCallback() {
  return [this](std::string const& id, DockStatusAction action) {
    if (id == "network") {
      performNetworkControlAsync(action);
      return;
    }
    if (id == "bluetooth") {
      performBluetoothControlAsync(action);
      return;
    }
    if (id == "media") {
      performMediaControlAsync(action);
      return;
    }
    if (id != "volume") return;

    switch (action) {
    case DockStatusAction::Primary:
      performAudioControlAsync(AudioControlAction::ToggleMute);
      return;
    case DockStatusAction::Secondary:
    case DockStatusAction::ScrollUp:
      queueVolumeAdjustment(1);
      return;
    case DockStatusAction::ScrollDown:
      queueVolumeAdjustment(-1);
      return;
    }
  };
}

std::function<void(DockItem const&)> ShellController::makeNewWindowCallback() {
  return [this](DockItem const& item) {
    if (item.appId.empty()) return;
    ipc_.sendLine(lambda::shell::serializeLaunchApp(item.appId, nextRequestId()));
  };
}

std::function<void(DockItem const&)> ShellController::makeTogglePinnedCallback() {
  return [this](DockItem const& item) {
    if (item.appId.empty()) return;

    ShellConfig next = shellConfig_;
    auto const firstMatch = std::find_if(next.dockPinned.begin(), next.dockPinned.end(), [&](std::string const& pin) {
      return appIdsMatch(pin, item.appId);
    });
    if (firstMatch == next.dockPinned.end()) {
      next.dockPinned.push_back(item.appId);
    } else {
      next.dockPinned.erase(std::remove_if(next.dockPinned.begin(),
                                           next.dockPinned.end(),
                                           [&](std::string const& pin) { return appIdsMatch(pin, item.appId); }),
                            next.dockPinned.end());
    }

    if (!saveShellConfig(next)) {
      return;
    }
    shellConfig_ = std::move(next);
    configLastWrite_ = configLastWriteTime(configPath_);
    applyShellConfigToModel();
  };
}

std::function<void(DockItem const&)> ShellController::makeQuitCallback() {
  return [this](DockItem const& item) {
    if (item.appId.empty()) return;
    ipc_.sendLine(lambda::shell::serializeQuitApp(item.appId, nextRequestId()));
  };
}

bool ShellController::saveShellConfig(ShellConfig const& config) {
  if (configPath_.empty()) return false;
  std::error_code ec;
  std::filesystem::path const parent = configPath_.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      std::fprintf(stderr,
                   "lambda-shell: failed to create shell config directory %s: %s\n",
                   parent.string().c_str(),
                   ec.message().c_str());
      return false;
    }
  }

  std::filesystem::path const tmpPath = configPath_.string() + ".tmp";
  {
    std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
    if (!out) {
      std::fprintf(stderr, "lambda-shell: failed to open shell config %s for writing\n", tmpPath.string().c_str());
      return false;
    }
    out << writeShellConfigToml(config);
    if (!out) {
      std::fprintf(stderr, "lambda-shell: failed to write shell config %s\n", tmpPath.string().c_str());
      return false;
    }
  }

  std::filesystem::rename(tmpPath, configPath_, ec);
  if (ec) {
    std::fprintf(stderr,
                 "lambda-shell: failed to replace shell config %s: %s\n",
                 configPath_.string().c_str(),
                 ec.message().c_str());
    return false;
  }
  return true;
}

void ShellController::applyShellConfigToModel() {
  int const previousDockItemSize = model_.dockItemSize();
  model_.setDockItems(appRegistry_, shellConfig_);
  (void)updateDockClockWidth();
  if (!lastSnapshotLine_.empty()) {
    (void)model_.applySnapshot(lastSnapshotLine_);
  }
  bool const dockItemSizeChanged = model_.dockItemSize() != previousDockItemSize;

  if (dockWindow_) {
    resizeDockWindowIfNeeded();
    int const itemSize = model_.dockItemSize();
    int const width = dockWidth(model_.dockItems(), model_.dockClockWidth(), itemSize);
    dockWindow_->setLayerShellOptions(
        dockWindowConfig(width,
                         itemSize,
                         shellConfig_.dockBottomGap,
                         shellConfig_.dockCornerRadius,
                         shellConfig_.dockMaterial,
                         shellConfig_.dockFullWidth)
            .layerShell);
    mountDockView();
  }
  if (dockItemSizeChanged && dockMenuOpen_) {
    syncDockMenuOverlay();
  }
  requestDockRedraw();
  if (model_.launcherOpen()) requestLauncherRedraw();
}

bool ShellController::connectIpc() {
  if (!ipc_.connect()) {
    return false;
  }
  ipc_.sendHello(nextRequestId());
  ipcPollId_ = app_.registerEventPollSource(ipc_.fd(), [this] {
    ipc_.dispatchReadable([this](std::string_view line) {
      app_.eventQueue().post(lambda::CustomEvent{.type = 0x4c534850u, .payload = std::string(line)});
    });
    if (!ipc_.connected()) {
      app_.quit();
    }
  });
  return ipc_.connected();
}

void ShellController::createProductionWindows() {
  (void)updateDockClockWidth();
  int const itemSize = model_.dockItemSize();
  int const dockWidthPx = dockWidth(model_.dockItems(), model_.dockClockWidth(), itemSize);
  auto& dock = app_.createWindow<lambda::Window>(
      dockWindowConfig(dockWidthPx,
                       itemSize,
                       shellConfig_.dockBottomGap,
                       shellConfig_.dockCornerRadius,
                       shellConfig_.dockMaterial,
                       shellConfig_.dockFullWidth));
  dock.setBackground(lambda::WindowBackground::transparent());
  dockWindow_ = &dock;
  dockHandle_ = dock.handle();
  lastDockWidth_ = shellConfig_.dockFullWidth ? 0 : dockWidthPx;
  lastDockHeight_ = dockHeight(itemSize);
  (void)refreshSystemStatus();
  clockTimerId_ = app_.scheduleRepeatingTimer(std::chrono::seconds{1}, *dockHandle_);
  systemStatusTimerId_ = app_.scheduleRepeatingTimer(std::chrono::seconds{5}, *dockHandle_);
  setupSystemStatusWatchers();

  auto& dockMenu = app_.createWindow<lambda::Window>(dockMenuWindowConfig());
  dockMenu.setBackground(lambda::WindowBackground::transparent());
  dockMenuWindow_ = &dockMenu;
  dockMenuHandle_ = dockMenu.handle();
  dockMenu.resize({1.f, 1.f});

  auto& launcher = app_.createWindow<lambda::Window>(launcherWindowConfig());
  launcher.setBackground(lambda::WindowBackground::transparent());
  launcherWindow_ = &launcher;
  launcherHandle_ = launcher.handle();

  mountProductionViews();
  syncLauncherWindow();
}

void ShellController::setupPreviewWindow(lambda::Window& window, float width, float height) {
  launcherWindow_ = &window;
  previewWidth_ = width;
  previewHeight_ = height;
  previewHandle_ = window.handle();
  model_.setLauncherSize(width, height);
  (void)updateDockClockWidth();
  (void)refreshSystemStatus();
  mountPreviewView();
}

void ShellController::mountDockView() {
  if (!dockWindow_) return;
  dockWindow_->setView(ShellDockView{
      model_,
      [this] { openLauncher(); },
      makeActivateCallback(),
      makeShowDockMenuCallback(),
      makeStatusActionCallback(),
      shellConfig_.dockFullWidth,
  });
}

void ShellController::mountProductionViews() {
  if (dockWindow_) {
    mountDockView();
  }
  if (dockMenuWindow_) {
    dockMenuWindow_->setView(lambda::Rectangle{}.size(1.f, 1.f).fill(lambda::Colors::transparent));
  }
  if (launcherWindow_ && !previewHandle_) {
    launcherWindow_->setView(ShellLauncherView{
        model_,
        makeActivateCallback(),
        [this] { closeLauncher(); },
        {},
    });
  }
}

void ShellController::mountPreviewView() {
  if (!launcherWindow_ || !previewHandle_) return;
  launcherWindow_->setView(ShellDesktopView{
      model_,
      [this] { openLauncher(); },
      makeActivateCallback(),
      makeShowDockMenuCallback(),
      makeStatusActionCallback(),
      makeActivateCallback(),
      [this] { closeLauncher(); },
      {},
      previewWidth_,
      previewHeight_,
  });
}

void ShellController::requestRedraws() {
  requestDockRedraw();
  requestDockMenuRedraw();
  requestLauncherRedraw();
}

void ShellController::requestDockRedraw() {
  if (dockWindow_) dockWindow_->requestRedraw();
}

void ShellController::requestDockMenuRedraw() {
  if (dockMenuWindow_) dockMenuWindow_->requestRedraw();
}

void ShellController::requestLauncherRedraw() {
  if (launcherWindow_) launcherWindow_->requestRedraw();
}

bool ShellController::refreshSystemStatus() {
  if (!model_.setSystemStatus(readShellSystemStatus())) {
    return false;
  }
  requestDockRedraw();
  if (previewHandle_) requestLauncherRedraw();
  return true;
}

void ShellController::setupSystemStatusWatchers() {
  if (!systemStatusWatchers_) {
    systemStatusWatchers_ = std::make_unique<ShellSystemStatusWatchers>();
  }

  auto refreshStatus = [this] {
    app_.eventQueue().post(ShellStatusCommandCompleted{.changed = true});
  };

  if (!systemStatusWatchers_->power) {
    try {
      systemStatusWatchers_->power = std::make_unique<ShellPowerStatusWatcher>(app_, refreshStatus);
    } catch (...) {
    }
  }

  if (!systemStatusWatchers_->network) {
    try {
      systemStatusWatchers_->network = std::make_unique<ShellNetworkStatusWatcher>(app_, refreshStatus);
    } catch (...) {
    }
  }

  if (!systemStatusWatchers_->bluetooth) {
    try {
      systemStatusWatchers_->bluetooth = std::make_unique<ShellBluetoothStatusWatcher>(app_, refreshStatus);
    } catch (...) {
    }
  }

  if (!systemStatusWatchers_->media) {
    try {
      systemStatusWatchers_->media = std::make_unique<ShellMediaStatusWatcher>(app_, std::move(refreshStatus));
    } catch (...) {
    }
  }
}

void ShellController::activateLauncherItem(DockItem const& item) {
  if (dockMenuOpen_) closeDockMenu();
  if (item.kind == "shell-action") {
    performShellActionAsync(item.id);
  } else {
    model_.activateItem(item, [this](std::string const& line) { ipc_.sendLine(line); }, nextRequestId());
  }
  if (model_.launcherOpen()) {
    closeLauncher();
  }
}

void ShellController::performAudioControlAsync(AudioControlAction action) {
  std::thread([this, action] {
    bool const changed = controlAudioVolume(action);
    app_.eventQueue().post(ShellAudioCommandCompleted{.changed = changed});
  }).detach();
}

void ShellController::performNetworkControlAsync(DockStatusAction action) {
  if (action != DockStatusAction::Primary) return;

  std::thread([this] {
    bool changed = false;
    try {
      auto client = lambda::system::NetworkManagerClient::connectSystem();
      auto const snapshot = client.readSnapshot();
      if (snapshot.wirelessHardwareEnabled) {
        client.setWirelessEnabled(!snapshot.wirelessEnabled);
        changed = true;
      }
    } catch (...) {
    }
    app_.eventQueue().post(ShellStatusCommandCompleted{.changed = changed});
  }).detach();
}

void ShellController::performBluetoothControlAsync(DockStatusAction action) {
  if (action != DockStatusAction::Primary) return;

  std::thread([this] {
    bool changed = false;
    try {
      auto client = lambda::system::BlueZClient::connectSystem();
      auto const snapshot = client.readSnapshot();
      bool const anyPowered = std::any_of(snapshot.adapters.begin(),
                                         snapshot.adapters.end(),
                                         [](auto const& adapter) {
                                           return adapter.powered;
                                         });
      bool const nextPowered = !anyPowered;
      for (auto const& adapter : snapshot.adapters) {
        if (adapter.path.empty()) continue;
        client.setAdapterPowered(adapter.path, nextPowered);
        changed = true;
      }
    } catch (...) {
    }
    app_.eventQueue().post(ShellStatusCommandCompleted{.changed = changed});
  }).detach();
}

void ShellController::performMediaControlAsync(DockStatusAction action) {
  std::thread([this, action] {
    bool changed = false;
    try {
      auto client = lambda::system::MPRISClient::connectSession();
      auto player = lambda::system::activeMPRISPlayerService(client.readPlayers());
      if (player) {
        switch (action) {
        case DockStatusAction::Primary:
          client.playPause(*player);
          changed = true;
          break;
        case DockStatusAction::Secondary:
        case DockStatusAction::ScrollUp:
          client.next(*player);
          changed = true;
          break;
        case DockStatusAction::ScrollDown:
          client.previous(*player);
          changed = true;
          break;
        }
      }
    } catch (...) {
    }
    app_.eventQueue().post(ShellStatusCommandCompleted{.changed = changed});
  }).detach();
}

void ShellController::performShellActionAsync(std::string actionId) {
  std::thread([actionId = std::move(actionId)] {
    try {
      auto client = lambda::system::LogindClient::connectSystem();
      if (actionId == "shell.suspend") {
        client.suspend(true);
      } else if (actionId == "shell.hibernate") {
        client.hibernate(true);
      } else if (actionId == "shell.reboot") {
        client.reboot(true);
      } else if (actionId == "shell.power-off") {
        client.powerOff(true);
      }
    } catch (std::exception const& error) {
      std::fprintf(stderr,
                   "lambda-shell: shell action %s failed: %s\n",
                   actionId.c_str(),
                   error.what());
    } catch (...) {
      std::fprintf(stderr, "lambda-shell: shell action %s failed\n", actionId.c_str());
    }
  }).detach();
}

void ShellController::queueVolumeAdjustment(int steps) {
  if (steps == 0) return;
  pendingVolumeSteps_.fetch_add(steps, std::memory_order_relaxed);
  bool expected = false;
  if (volumeAdjustmentWorkerRunning_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
    std::thread([this] { runVolumeAdjustmentWorker(); }).detach();
  }
}

void ShellController::runVolumeAdjustmentWorker() {
  bool changed = false;
  while (true) {
    std::this_thread::sleep_for(kDockVolumeCoalesceDelay);
    int const steps = pendingVolumeSteps_.exchange(0, std::memory_order_acq_rel);
    if (steps != 0) {
      changed = adjustAudioVolumeByPercent(steps * kDockVolumeStepPercent) || changed;
    }

    if (pendingVolumeSteps_.load(std::memory_order_acquire) == 0) {
      volumeAdjustmentWorkerRunning_.store(false, std::memory_order_release);
      if (pendingVolumeSteps_.load(std::memory_order_acquire) == 0) {
        break;
      }
      bool expected = false;
      if (!volumeAdjustmentWorkerRunning_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        break;
      }
    }
  }
  app_.eventQueue().post(ShellAudioCommandCompleted{.changed = changed});
}

int ShellController::measureDockClockWidth() {
  lambda::TextLayoutOptions options;
  options.wrapping = lambda::TextWrapping::NoWrap;
  options.suppressCacheStats = true;

  std::string const text = model_.timeText();
  int const itemSize = model_.dockItemSize();
  if (dockUsesSingleRowDocklets(itemSize)) {
    lambda::Size const textSize = app_.textSystem().measure(text,
                                                            lambda::Font{.family = "",
                                                                         .size = dockClockSingleRowFontSize(itemSize),
                                                                         .weight = kDockClockSingleRowFontWeight},
                                                            lambda::VisualTokens::primaryText,
                                                            0.f,
                                                            options);
    return std::max(kDockClockMinWidth,
                    static_cast<int>(std::ceil(textSize.width +
                                               kDockClockLeadingPaddingX +
                                               kDockClockTrailingPaddingX)));
  }

  std::string const date = dockClockDateText(text);
  std::string const time = dockClockTimeText(text);
  lambda::Size const dateSize = app_.textSystem().measure(date,
                                                          lambda::Font{.family = "",
                                                                       .size = dockClockDateFontSize(itemSize),
                                                                       .weight = kDockClockDateFontWeight},
                                                          lambda::VisualTokens::primaryText,
                                                          0.f,
                                                          options);
  lambda::Size const timeSize = app_.textSystem().measure(time,
                                                          lambda::Font{.family = "",
                                                                       .size = dockClockTimeFontSize(itemSize),
                                                                       .weight = kDockClockTimeFontWeight},
                                                          lambda::VisualTokens::primaryText,
                                                          0.f,
                                                          options);
  return std::max(kDockClockMinWidth,
                  static_cast<int>(std::ceil(std::max(dateSize.width, timeSize.width) +
                                             kDockClockLeadingPaddingX +
                                             kDockClockTrailingPaddingX)));
}

bool ShellController::updateDockClockWidth() {
  return model_.setDockClockWidth(measureDockClockWidth());
}

void ShellController::resizeDockWindowIfNeeded() {
  if (!dockWindow_) return;
  int const itemSize = model_.dockItemSize();
  int const width = shellConfig_.dockFullWidth ? 0 : dockWidth(model_.dockItems(), model_.dockClockWidth(), itemSize);
  int const height = dockHeight(itemSize);
  if (width == lastDockWidth_ && height == lastDockHeight_) return;
  lastDockWidth_ = width;
  lastDockHeight_ = height;
  dockWindow_->resize({static_cast<float>(width), static_cast<float>(height)});
}

void ShellController::openLauncher() {
  if (model_.launcherOpen()) return;
  model_.openLauncher();
  syncLauncherWindow();
  requestLauncherRedraw();
}

void ShellController::closeLauncher() {
  if (!model_.launcherOpen()) return;
  model_.closeLauncher();
  syncLauncherWindow();
  requestLauncherRedraw();
}

void ShellController::syncLauncherWindow() {
  if (!launcherWindow_ || previewHandle_) return;

  bool const wantOpen = model_.launcherOpen();
  if (wantOpen == lastLauncherOpen_) {
    return;
  }
  lastLauncherOpen_ = wantOpen;

  if (wantOpen) {
    launcherWindow_->resize({0.f, 0.f});
    launcherWindow_->setLayerShellKeyboardInteractive(true);
    if (ipc_.connected() && !launcherModalClaimed_) {
      ipc_.claimLauncherModal(nextRequestId());
      launcherModalClaimed_ = true;
    }
  } else {
    if (ipc_.connected() && launcherModalClaimed_) {
      ipc_.releaseLauncherModal(nextRequestId());
      launcherModalClaimed_ = false;
    }
    launcherWindow_->setLayerShellKeyboardInteractive(false);
    launcherWindow_->resize({1.f, 1.f});
  }
}

void ShellController::openDockMenu(DockItem const& item) {
  if (!dockMenuWindow_ || item.kind != "app" || item.appId.empty()) {
    return;
  }

  closeLauncher();

  std::vector<DockItem> const& items = model_.dockItems();
  auto found = std::find_if(items.begin(), items.end(), [&](DockItem const& candidate) {
    return candidate.kind == "app" && appIdsMatch(candidate.appId, item.appId);
  });
  if (found == items.end()) {
    return;
  }

  dockMenuItem_ = *found;
  dockMenuOpen_ = true;
  syncDockMenuOverlay();
}

void ShellController::syncDockMenuOverlay() {
  if (!dockMenuWindow_ || !dockMenuOpen_ || !dockMenuItem_) {
    return;
  }

  std::vector<DockItem> const& items = model_.dockItems();
  auto found = std::find_if(items.begin(), items.end(), [&](DockItem const& candidate) {
    return candidate.kind == "app" && appIdsMatch(candidate.appId, dockMenuItem_->appId);
  });
  if (found == items.end()) {
    closeDockMenu();
    return;
  }

  dockMenuItem_ = *found;
  int const itemSize = model_.dockItemSize();
  float const outputWidth = std::max({dockMenuOverlayWidth_,
                                      static_cast<float>(dockWidth(items, model_.dockClockWidth(), itemSize))});
  int const dockBottomGap = std::clamp(shellConfig_.dockBottomGap, 0, 64);
  int const dockSurfaceH = dockHeight(itemSize);
  float const outputHeight = std::max(dockMenuOverlayHeight_,
                                      static_cast<float>(dockBottomGap + dockSurfaceH + kDockMenuGap +
                                                         kDockMenuSurfaceHeight));
  int const currentDockWidth = dockWidth(items, model_.dockClockWidth(), itemSize);
  float const dockLeft = std::max(0.f, (outputWidth - static_cast<float>(currentDockWidth)) * 0.5f);
  float localCenter = kDockPaddingX;
  for (auto it = items.begin(); it != found; ++it) {
    localCenter += static_cast<float>(dockItemWidth(*it, itemSize) + kDockGap);
  }
  localCenter += static_cast<float>(dockItemWidth(*found, itemSize)) * 0.5f;
  float const iconCenter = dockLeft + localCenter;
  int const menuLeft = static_cast<int>(std::lround(
      std::clamp(iconCenter - static_cast<float>(kDockMenuSurfaceWidth) * 0.5f,
                 0.f,
                 std::max(0.f, outputWidth - static_cast<float>(kDockMenuSurfaceWidth)))));
  int const menuTop = static_cast<int>(std::lround(
      std::clamp(outputHeight - static_cast<float>(dockBottomGap + dockSurfaceH + kDockMenuGap +
                                                   kDockMenuSurfaceHeight),
                 0.f,
                 std::max(0.f, outputHeight - static_cast<float>(kDockMenuSurfaceHeight)))));

  LayerShellOptions menuLayer = visibleMenuLayer("lambda.dock-menu");
  menuLayer.backgroundBlur = true;
  menuLayer.backgroundEffectRegion = LayerShellBackgroundEffectRegion{
      .x = menuLeft + kDockMenuSurfaceInset,
      .y = menuTop + kDockMenuSurfaceInset,
      .width = kDockMenuCalloutWidth,
      .height = kDockMenuCalloutHeight,
      .shape = LayerShellBackgroundEffectShape::Callout,
      .calloutPlacement = LayerShellCalloutPlacement::Above,
      .arrowWidth = PopoverCalloutShape::kArrowW,
      .arrowHeight = PopoverCalloutShape::kArrowH,
  };
  menuLayer.chrome.style = LayerShellChromeStyle::BlurPanelBorder;
  menuLayer.chrome.cornerRadius = CornerRadius{14.f};
  dockMenuWindow_->setView(ShellDockMenuView{
      *dockMenuItem_,
      outputWidth,
      outputHeight,
      static_cast<float>(menuLeft),
      static_cast<float>(menuTop),
      makeNewWindowCallback(),
      makeTogglePinnedCallback(),
      makeQuitCallback(),
      [this] { closeDockMenu(); },
  });
  dockMenuWindow_->setLayerShellOptions(menuLayer);
  dockMenuWindow_->resize({0.f, 0.f});
  requestDockMenuRedraw();
}

void ShellController::closeDockMenu() {
  if (!dockMenuOpen_ && !dockMenuWindow_) {
    return;
  }
  dockMenuOpen_ = false;
  dockMenuItem_.reset();
  LayerShellOptions menuLayer = hiddenMenuLayer("lambda.dock-menu");
  if (dockMenuWindow_) {
    dockMenuWindow_->setView(lambda::Rectangle{}.size(1.f, 1.f).fill(lambda::Colors::transparent));
    dockMenuWindow_->resize({1.f, 1.f});
    dockMenuWindow_->setLayerShellOptions(menuLayer);
  }
  requestDockMenuRedraw();
}

void ShellController::handleIpcLine(std::string_view line) {
  auto message = lambda::shell::parseLine(line);
  if (!message) return;
  if (message->kind == lambda::shell::ShellMessageKind::ShellOpenCommandLauncher) {
    openLauncher();
    return;
  }
  if (message->kind == lambda::shell::ShellMessageKind::WindowManagerSnapshot) {
    lastSnapshotLine_ = std::string(line);
    auto const changes = model_.applySnapshot(line);
    if (previewHandle_) {
      if (changes.any()) {
        requestLauncherRedraw();
      }
      return;
    }
    resizeDockWindowIfNeeded();
    if (changes.dockItems) {
      requestDockRedraw();
    }
    if (changes.dockItems && model_.launcherOpen()) {
      requestLauncherRedraw();
    }
  }
}

void ShellController::checkShellConfigReload() {
  if (configPath_.empty()) return;
  auto const nextWrite = configLastWriteTime(configPath_);
  bool const writeChanged = nextWrite && (!configLastWrite_ || *configLastWrite_ != *nextWrite);

  ShellConfigLoadResult const loaded = loadShellConfig(configPath_);
  if (!loaded.error.empty()) {
    std::fprintf(stderr,
                 "lambda-shell: ignoring shell config reload from %s: %s\n",
                 configPath_.string().c_str(),
                 loaded.error.c_str());
    return;
  }

  if (!writeChanged && loaded.config == shellConfig_) {
    return;
  }

  configLastWrite_ = nextWrite;
  shellConfig_ = loaded.config;
  applyShellConfigToModel();
}

void ShellController::handleLauncherKey(lambda::InputEvent const& event) {
  if (event.kind == lambda::InputEvent::Kind::KeyDown) {
    if (event.key == Escape) {
      closeLauncher();
      return;
    }
    if (event.key == Delete) {
      model_.backspaceQuery();
      requestLauncherRedraw();
      return;
    }
    if (event.key == ForwardDelete) {
      model_.deleteQueryForward();
      requestLauncherRedraw();
      return;
    }
    if (event.key == LeftArrow) {
      model_.moveQueryCursor(-1);
      requestLauncherRedraw();
      return;
    }
    if (event.key == RightArrow) {
      model_.moveQueryCursor(1);
      requestLauncherRedraw();
      return;
    }
    if (event.key == Home) {
      model_.moveQueryCursorToStart();
      requestLauncherRedraw();
      return;
    }
    if (event.key == End) {
      model_.moveQueryCursorToEnd();
      requestLauncherRedraw();
      return;
    }
    if (event.key == DownArrow) {
      model_.moveHighlight(1);
      requestLauncherRedraw();
      return;
    }
    if (event.key == UpArrow) {
      model_.moveHighlight(-1);
      requestLauncherRedraw();
      return;
    }
    if (event.key == Return) {
      auto const results = model_.launcherResults();
      if (!results.empty()) {
        int const index = std::clamp(model_.highlighted(), 0, static_cast<int>(results.size()) - 1);
        activateLauncherItem(results[static_cast<std::size_t>(index)]);
      }
      return;
    }
  }
  if (event.kind == lambda::InputEvent::Kind::TextInput && !event.text.empty()) {
    model_.appendQueryText(event.text);
    requestLauncherRedraw();
  }
}

std::uint64_t ShellController::nextRequestId() {
  return nextRequestId_++;
}

} // namespace lambda_shell
