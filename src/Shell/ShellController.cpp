#include "Shell/ShellController.hpp"

#include "Shell/ShellDesktopView.hpp"
#include "Shell/ShellJson.hpp"

#include <Flux/Shell/ShellIpc.hpp>
#include "Shell/ShellViews.hpp"

#include <Flux/UI/EventQueue.hpp>
#include <Flux/UI/WindowUI.hpp>

#include <Flux/Core/Color.hpp>
#include <Flux/UI/Events.hpp>
#include <Flux/UI/KeyCodes.hpp>

#include <any>
#include <chrono>

namespace lambda_shell {

namespace {

using namespace flux;
using namespace flux::keys;

LayerShellOptions layerBase(LayerShellLayer layer, char const* nameSpace) {
  LayerShellOptions options{};
  options.enabled = true;
  options.layer = layer;
  options.nameSpace = nameSpace;
  options.backgroundBlur = true;
  return options;
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

ShellController::ShellController(flux::Application& app, ShellModel& model) : app_(app), model_(model) {
  if (model_.dockItems().empty()) model_.resetDockItems();
  lastDockWidth_ = dockWidth(model_.dockItems());

  app_.eventQueue().on<flux::WindowEvent>([this](flux::WindowEvent const& event) {
    if (event.kind != flux::WindowEvent::Kind::Resize) return;
    if (!launcherHandle_ || event.handle != *launcherHandle_) return;
    if (!model_.launcherOpen()) return;
    model_.setLauncherSize(event.size.width, event.size.height);
    if (event.size.width > 64.f && event.size.height > 64.f) {
      model_.setLauncherUiVisible(true);
    }
    requestLauncherRedraw();
  });

  app_.eventQueue().on<flux::TimerEvent>([this](flux::TimerEvent const& event) {
    if (clockTimerId_ == 0 || event.timerId != clockTimerId_) return;
    if (model_.refreshTimeText()) {
      requestTopBarRedraw();
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

std::function<void(DockItem const&)> ShellController::makeActivateCallback() {
  return [this](DockItem const& item) {
    model_.activateItem(item, [this](std::string const& line) { ipc_.sendLine(line); });
    if (model_.launcherOpen()) {
      closeLauncher();
    }
  };
}

bool ShellController::connectIpc() {
  if (!ipc_.connect()) {
    return false;
  }
  ipc_.sendHello();
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
  clockTimerId_ = app_.scheduleRepeatingTimer(std::chrono::seconds{1}, *topBarHandle_);

  int const dockWidthPx = dockWidth(model_.dockItems());
  auto& dock = app_.createWindow<flux::Window>(dockWindowConfig(dockWidthPx));
  dock.setBackground(flux::WindowBackground::transparent());
  dockWindow_ = &dock;
  dockHandle_ = dock.handle();
  lastDockWidth_ = dockWidthPx;

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
    dockWindow_->setView(ShellDockView{model_, [this] { openLauncher(); }, makeActivateCallback()});
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
  requestLauncherRedraw();
}

void ShellController::requestTopBarRedraw() {
  if (topBarWindow_) topBarWindow_->requestRedraw();
}

void ShellController::requestDockRedraw() {
  if (dockWindow_) dockWindow_->requestRedraw();
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
      ipc_.claimLauncherModal();
      launcherModalClaimed_ = true;
    }
  } else {
    if (ipc_.connected() && launcherModalClaimed_) {
      ipc_.releaseLauncherModal();
      launcherModalClaimed_ = false;
    }
    launcherWindow_->setLayerShellKeyboardInteractive(false);
    launcherWindow_->resize({1.f, 1.f});
  }
}

void ShellController::handleIpcLine(std::string_view line) {
  auto message = flux::shell::parseLine(line);
  if (!message) return;
  if (message->kind == flux::shell::ShellMessageKind::ShellOpenCommandLauncher) {
    openLauncher();
    return;
  }
  if (message->kind == flux::shell::ShellMessageKind::WindowManagerSnapshot) {
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
                            [this](std::string const& line) { ipc_.sendLine(line); });
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

} // namespace lambda_shell
