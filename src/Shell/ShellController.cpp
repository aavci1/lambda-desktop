#include "Shell/ShellController.hpp"

#include "Shell/ShellAppRegistry.hpp"
#include "Shell/ShellDesktopView.hpp"
#include "Shell/ShellJson.hpp"

#include <Flux/Shell/ShellIpc.hpp>
#include "Shell/ShellViews.hpp"

#include <Flux/UI/EventQueue.hpp>
#include <Flux/UI/Views/PopoverCalloutShape.hpp>
#include <Flux/UI/WindowUI.hpp>

#include <Flux/Core/Color.hpp>
#include <Flux/UI/Events.hpp>
#include <Flux/UI/KeyCodes.hpp>

#include <any>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace lambda_shell {

namespace {

using namespace flux;
using namespace flux::keys;

inline constexpr int kDockMenuGap = 10;

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
  return layer;
}

} // namespace

flux::WindowConfig topBarWindowConfig() {
  LayerShellOptions layer = layerBase(LayerShellLayer::Top, "lambda.topbar");
  layer.anchorTop = true;
  layer.anchorLeft = true;
  layer.anchorRight = true;
  layer.exclusiveZone = kTopBarHeight;
  layer.chrome.style = LayerShellChromeStyle::BlurPanelBorder;
  layer.chrome.cornerRadius = CornerRadius{};
  return WindowConfig{
      .size = {0.f, static_cast<float>(kTopBarHeight)},
      .title = "Lambda Top Bar",
      .resizable = false,
      .layerShell = layer,
  };
}

flux::WindowConfig dockWindowConfig(int width) {
  LayerShellOptions layer = layerBase(LayerShellLayer::Overlay, "lambda.dock");
  layer.anchorBottom = true;
  layer.marginBottom = kDockBottom;
  layer.chrome.style = LayerShellChromeStyle::BlurPanelBorder;
  return WindowConfig{
      .size = {static_cast<float>(std::max(width, 1)), static_cast<float>(dockHeight())},
      .title = "Lambda Dock",
      .resizable = false,
      .layerShell = layer,
  };
}

flux::WindowConfig launcherWindowConfig() {
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

flux::WindowConfig dockMenuWindowConfig() {
  LayerShellOptions layer = hiddenMenuLayer("lambda.dock-menu");
  return WindowConfig{
      .size = {1.f, 1.f},
      .title = "Lambda Dock Menu",
      .resizable = false,
      .layerShell = layer,
  };
}

ShellController::ShellController(flux::Application& app, ShellModel& model) : app_(app), model_(model) {
  if (model_.dockItems().empty()) model_.resetDockItems();
  lastDockWidth_ = dockWidth(model_.dockItems());

  app_.eventQueue().on<flux::WindowEvent>([this](flux::WindowEvent const& event) {
    if (event.kind == flux::WindowEvent::Kind::DpiChanged) {
      if (dockHandle_ && event.handle == *dockHandle_) {
        float const scale = std::max(event.dpiX > 0.f ? event.dpiX : event.dpi, 0.5f);
        if (model_.setDockDpiScale(scale)) {
          requestDockRedraw();
          if (model_.launcherOpen()) requestLauncherRedraw();
        }
      }
      return;
    }
    if (event.kind != flux::WindowEvent::Kind::Resize) return;
    if (topBarHandle_ && event.handle == *topBarHandle_) {
      if (model_.setTopBarWidth(event.size.width)) {
        requestTopBarRedraw();
      }
      return;
    }
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

  app_.eventQueue().on<flux::TimerEvent>([this](flux::TimerEvent const& event) {
    if (clockTimerId_ != 0 && event.timerId == clockTimerId_) {
      if (model_.refreshTimeText()) {
        requestTopBarRedraw();
      }
      return;
    }
    if (configReloadTimerId_ != 0 && event.timerId == configReloadTimerId_) {
      checkShellConfigReload();
    }
  });

  app_.eventQueue().on<flux::InputEvent>([this](flux::InputEvent const& event) {
    bool const forLauncher =
        (launcherHandle_ && event.handle == *launcherHandle_) || (previewHandle_ && event.handle == *previewHandle_);
    if (forLauncher && model_.launcherOpen()) {
      handleLauncherKey(event);
    }
  });

  app_.eventQueue().on<flux::CustomEvent>([this](flux::CustomEvent const& event) {
    if (event.type != 0x4c534850u) {
      return;
    }
    if (auto const* line = std::any_cast<std::string>(&event.payload)) {
      handleIpcLine(*line);
    }
  });
}

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
    if (dockMenuOpen_) closeDockMenu();
    model_.activateItem(item, [this](std::string const& line) { ipc_.sendLine(line); }, nextRequestId());
    if (model_.launcherOpen()) {
      closeLauncher();
    }
  };
}

std::function<void(DockItem const&)> ShellController::makeShowDockMenuCallback() {
  return [this](DockItem const& item) {
    openDockMenu(item);
  };
}

std::function<void(DockItem const&)> ShellController::makeNewWindowCallback() {
  return [this](DockItem const& item) {
    if (item.appId.empty()) return;
    ipc_.sendLine(flux::shell::serializeLaunchApp(item.appId, nextRequestId()));
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
    ipc_.sendLine(flux::shell::serializeQuitApp(item.appId, nextRequestId()));
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
  model_.setDockItems(appRegistry_, shellConfig_);
  ShellModel::SnapshotChanges changes{};
  if (!lastSnapshotLine_.empty()) {
    changes = model_.applySnapshot(lastSnapshotLine_);
  }

  int const width = dockWidth(model_.dockItems());
  if (width != lastDockWidth_ && dockWindow_) {
    lastDockWidth_ = width;
    dockWindow_->resize({static_cast<float>(width), static_cast<float>(dockHeight())});
  }
  if (changes.activeTitle || changes.systemStatus) {
    requestTopBarRedraw();
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
      app_.eventQueue().post(flux::CustomEvent{.type = 0x4c534850u, .payload = std::string(line)});
    });
    if (!ipc_.connected()) {
      app_.quit();
    }
  });
  return ipc_.connected();
}

void ShellController::createProductionWindows() {
  auto& topBar = app_.createWindow<flux::Window>(topBarWindowConfig());
  topBar.setBackground(flux::WindowBackground::transparent());
  topBarWindow_ = &topBar;
  topBarHandle_ = topBar.handle();
  model_.setTopBarWidth(topBar.getSize().width);
  clockTimerId_ = app_.scheduleRepeatingTimer(std::chrono::seconds{1}, *topBarHandle_);

  int const dockWidthPx = dockWidth(model_.dockItems());
  auto& dock = app_.createWindow<flux::Window>(dockWindowConfig(dockWidthPx));
  dock.setBackground(flux::WindowBackground::transparent());
  dockWindow_ = &dock;
  dockHandle_ = dock.handle();
  lastDockWidth_ = dockWidthPx;

  auto& dockMenu = app_.createWindow<flux::Window>(dockMenuWindowConfig());
  dockMenu.setBackground(flux::WindowBackground::transparent());
  dockMenuWindow_ = &dockMenu;
  dockMenuHandle_ = dockMenu.handle();
  dockMenu.resize({1.f, 1.f});

  auto& launcher = app_.createWindow<flux::Window>(launcherWindowConfig());
  launcher.setBackground(flux::WindowBackground::transparent());
  launcherWindow_ = &launcher;
  launcherHandle_ = launcher.handle();

  mountProductionViews();
  syncLauncherWindow();
}

void ShellController::setupPreviewWindow(flux::Window& window, float width, float height) {
  launcherWindow_ = &window;
  previewWidth_ = width;
  previewHeight_ = height;
  previewHandle_ = window.handle();
  model_.setLauncherSize(width, height);
  mountPreviewView();
}

void ShellController::mountProductionViews() {
  if (topBarWindow_) {
    topBarWindow_->setView(ShellTopBarView{model_, [this] { openLauncher(); }});
  }
  if (dockWindow_) {
    dockWindow_->setView(ShellDockView{
        model_,
        [this] { openLauncher(); },
        makeActivateCallback(),
        makeShowDockMenuCallback(),
    });
  }
  if (dockMenuWindow_) {
    dockMenuWindow_->setView(flux::Rectangle{}.size(1.f, 1.f).fill(flux::Colors::transparent));
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
      makeActivateCallback(),
      [this] { closeLauncher(); },
      {},
      previewWidth_,
      previewHeight_,
  });
}

void ShellController::requestRedraws() {
  requestTopBarRedraw();
  requestDockRedraw();
  requestDockMenuRedraw();
  requestLauncherRedraw();
}

void ShellController::requestTopBarRedraw() {
  if (topBarWindow_) topBarWindow_->requestRedraw();
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
  float const outputWidth = std::max({model_.topBarWidth(),
                                      dockMenuOverlayWidth_,
                                      static_cast<float>(dockWidth(items))});
  float const outputHeight = std::max(dockMenuOverlayHeight_,
                                      static_cast<float>(kDockBottom + dockHeight() + kDockMenuGap +
                                                         kDockMenuSurfaceHeight));
  int const currentDockWidth = dockWidth(items);
  float const dockLeft = std::max(0.f, (outputWidth - static_cast<float>(currentDockWidth)) * 0.5f);
  float localCenter = kDockPaddingX;
  for (auto it = items.begin(); it != found; ++it) {
    localCenter += static_cast<float>(dockItemWidth(*it) + kDockGap);
  }
  localCenter += static_cast<float>(dockItemWidth(*found)) * 0.5f;
  float const iconCenter = dockLeft + localCenter;
  int const menuLeft = static_cast<int>(std::lround(
      std::clamp(iconCenter - static_cast<float>(kDockMenuSurfaceWidth) * 0.5f,
                 0.f,
                 std::max(0.f, outputWidth - static_cast<float>(kDockMenuSurfaceWidth)))));
  int const menuTop = static_cast<int>(std::lround(
      std::clamp(outputHeight - static_cast<float>(kDockBottom + dockHeight() + kDockMenuGap +
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
  dockMenuWindow_->resize({0.f, 0.f});
  dockMenuWindow_->setLayerShellOptions(menuLayer);
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
    dockMenuWindow_->setView(flux::Rectangle{}.size(1.f, 1.f).fill(flux::Colors::transparent));
    dockMenuWindow_->resize({1.f, 1.f});
    dockMenuWindow_->setLayerShellOptions(menuLayer);
  }
  requestDockMenuRedraw();
}

void ShellController::handleIpcLine(std::string_view line) {
  auto message = flux::shell::parseLine(line);
  if (!message) return;
  if (message->kind == flux::shell::ShellMessageKind::ShellOpenCommandLauncher) {
    openLauncher();
    return;
  }
  if (message->kind == flux::shell::ShellMessageKind::WindowManagerSnapshot) {
    lastSnapshotLine_ = std::string(line);
    auto const changes = model_.applySnapshot(line);
    if (previewHandle_) {
      if (changes.any()) {
        requestLauncherRedraw();
      }
      return;
    }
    int const width = dockWidth(model_.dockItems());
    if (width != lastDockWidth_ && dockWindow_) {
      lastDockWidth_ = width;
      dockWindow_->resize({static_cast<float>(width), static_cast<float>(dockHeight())});
    }
    if (changes.activeTitle || changes.systemStatus) {
      requestTopBarRedraw();
    }
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

void ShellController::handleLauncherKey(flux::InputEvent const& event) {
  if (event.kind == flux::InputEvent::Kind::KeyDown) {
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
        model_.activateItem(results[static_cast<std::size_t>(index)],
                            [this](std::string const& line) { ipc_.sendLine(line); },
                            nextRequestId());
        closeLauncher();
      }
      return;
    }
  }
  if (event.kind == flux::InputEvent::Kind::TextInput && !event.text.empty()) {
    model_.appendQueryText(event.text);
    requestLauncherRedraw();
  }
}

std::uint64_t ShellController::nextRequestId() {
  return nextRequestId_++;
}

} // namespace lambda_shell
