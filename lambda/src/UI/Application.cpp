#include <Lambda/UI/Application.hpp>

#include <Lambda/UI/EventQueue.hpp>
#include <Lambda/UI/Events.hpp>
#include <Lambda/UI/KeyCodes.hpp>
#include <Lambda/UI/Shortcut.hpp>
#include <Lambda/UI/Window.hpp>
#include <Lambda/Graphics/Canvas.hpp>
#include <Lambda/Reactive/AnimationClock.hpp>

#include "UI/Platform/Application.hpp"
#include "UI/Platform/Window.hpp"
#include "UI/MenuRoleDefaults.hpp"
#include "Detail/ResizeTrace.hpp"
#include "Debug/PerfCounters.hpp"
#include "Graphics/Linux/FreeTypeTextSystem.hpp"

#include <algorithm>
#include <chrono>
#include <climits>
#include <poll.h>
#include <cstdio>
#include <unistd.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace lambdaui {

namespace detail {
namespace {
struct WindowCreationModalParent {
  unsigned int parentHandle = 0;
  bool modal = false;
};

thread_local std::vector<WindowCreationModalParent> gWindowCreationModalParents;
} // namespace

bool signalBridgeApplicationHasInstance() {
  return Application::hasInstance();
}

void pushWindowCreationModalParent(unsigned int parentHandle, bool modal) {
  gWindowCreationModalParents.push_back(WindowCreationModalParent{.parentHandle = parentHandle, .modal = modal});
}

void popWindowCreationModalParent() {
  if (!gWindowCreationModalParents.empty()) {
    gWindowCreationModalParents.pop_back();
  }
}

unsigned int currentWindowCreationTransientParentHandle() {
  return gWindowCreationModalParents.empty() ? 0 : gWindowCreationModalParents.back().parentHandle;
}

bool currentWindowCreationModal() {
  return !gWindowCreationModalParents.empty() && gWindowCreationModalParents.back().modal;
}
} // namespace detail

namespace {

Application* gCurrent = nullptr;

struct AnimationFramePulse {
  std::int64_t deadlineNanos = 0;
};

class MemoryClipboard final : public Clipboard {
public:
  std::optional<std::string> readText() const override {
    if (text_.empty()) {
      return std::nullopt;
    }
    return text_;
  }
  void writeText(std::string text) override { text_ = std::move(text); }
  bool hasText() const override { return !text_.empty(); }
private:
  std::string text_;
};

std::string jsonEscape(std::string const& input) {
  std::string out;
  out.reserve(input.size());
  for (char c : input) {
    if (c == '"' || c == '\\') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  return out;
}

std::filesystem::path windowStatePath(std::string const& userDataDir) {
  if (userDataDir.empty()) {
    return {};
  }
  return std::filesystem::path(userDataDir) / "window-state.json";
}

std::string appNameFromArgv(int argc, char** argv) {
  if (argc <= 0 || !argv || !argv[0] || !*argv[0]) {
    return "lambda";
  }
  std::filesystem::path path(argv[0]);
  std::string name = path.stem().string();
  return name.empty() ? "lambda" : name;
}

Shortcut ctrlShortcut(KeyCode key, Modifiers extra = Modifiers::None) {
  return Shortcut{key, Modifiers::Ctrl | extra};
}

CommandDescriptor editCommandDescriptor(char const* title, Shortcut shortcut) {
  return CommandDescriptor{
      .title = title,
      .category = "Edit",
      .shortcut = shortcut,
  };
}

void loadWindowStatesFromDisk(std::filesystem::path const& path,
                              std::unordered_map<std::string, WindowState>& out) {
  out.clear();
  if (path.empty()) {
    return;
  }
  std::ifstream in(path);
  if (!in) {
    return;
  }
  std::string const text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::regex const entry(
      R"json("([^"]+)"\s*:\s*\{[^}]*"frame"\s*:\s*\[\s*([-0-9.]+)\s*,\s*([-0-9.]+)\s*,\s*([-0-9.]+)\s*,\s*([-0-9.]+)\s*\][^}]*"fullscreen"\s*:\s*(true|false)[^}]*"contentSize"\s*:\s*\[\s*([-0-9.]+)\s*,\s*([-0-9.]+)\s*\][^}]*\})json");
  for (std::sregex_iterator it(text.begin(), text.end(), entry), end; it != end; ++it) {
    WindowState state;
    state.frame = Rect::sharp(std::stof((*it)[2].str()), std::stof((*it)[3].str()),
                              std::stof((*it)[4].str()), std::stof((*it)[5].str()));
    state.fullscreen = (*it)[6].str() == "true";
    state.contentSize = Size{std::stof((*it)[7].str()), std::stof((*it)[8].str())};
    out[(*it)[1].str()] = state;
  }
}

void saveWindowStatesToDisk(std::filesystem::path const& path,
                            std::unordered_map<std::string, WindowState> const& states) {
  if (path.empty()) {
    return;
  }
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    return;
  }
  std::filesystem::path const tmp = path.string() + ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) {
      return;
    }
    out << "{\n";
    bool first = true;
    for (auto const& [id, state] : states) {
      if (!first) {
        out << ",\n";
      }
      first = false;
      out << "  \"" << jsonEscape(id) << "\": {\n";
      out << "    \"frame\": [" << state.frame.x << ", " << state.frame.y << ", "
          << state.frame.width << ", " << state.frame.height << "],\n";
      out << "    \"fullscreen\": " << (state.fullscreen ? "true" : "false") << ",\n";
      out << "    \"contentSize\": [" << state.contentSize.width << ", "
          << state.contentSize.height << "]\n";
      out << "  }";
    }
    out << "\n}\n";
  }
  std::filesystem::rename(tmp, path, ec);
  if (ec) {
    std::filesystem::remove(path, ec);
    ec.clear();
    std::filesystem::rename(tmp, path, ec);
  }
}

void collectMenuState(MenuItem& item, std::unordered_map<std::string, std::function<void()>>& handlers,
                      std::unordered_map<std::string, std::function<bool()>>& enabled,
                      std::unordered_map<platform::ShortcutKey, std::string, platform::ShortcutKeyHash>& shortcuts,
                      std::uint64_t& nextId) {
  if (item.actionName.empty()) {
    item.actionName = detail::standardRoleActionName(item.role);
  }
  if (item.shortcut.key == 0 && item.shortcut.modifiers == Modifiers::None) {
    item.shortcut = detail::standardRoleShortcut(item.role);
  }
  if (item.actionName.empty() && item.handler) {
    item.actionName = "__lambda.menu.handler." + std::to_string(nextId++);
    handlers[item.actionName] = item.handler;
  }
  if (!item.actionName.empty() && item.isEnabled) {
    enabled[item.actionName] = item.isEnabled;
  }
  if (!item.actionName.empty() && (item.shortcut.key != 0 || item.shortcut.modifiers != Modifiers::None)) {
    shortcuts[platform::ShortcutKey{.key = item.shortcut.key, .modifiers = item.shortcut.modifiers}] = item.actionName;
  }
  for (MenuItem& child : item.children) {
    collectMenuState(child, handlers, enabled, shortcuts, nextId);
  }
}

} // namespace

struct Application::Impl {
  struct WindowRenderState {
    bool redrawRequested = false;
    bool frameReady = false;
    bool frameBudgetPending = false;
    bool deferTraceLogged = false;
    std::chrono::steady_clock::time_point frameBudgetStartedAt{};
  };

  EventQueue eventQueue_;
  std::vector<std::unique_ptr<Window>> windows_;
  std::unordered_map<unsigned int, Window*> byHandle_;
  std::unordered_map<unsigned int, WindowRenderState> renderStates_;
  std::unordered_set<unsigned int> pendingAdoptRedraws_;
  std::unordered_set<unsigned int> pendingCloseHandles_;
  std::unordered_map<unsigned int, unsigned int> modalParentByChild_;

  std::unique_ptr<TextSystem> textSystem_;
  std::unique_ptr<Clipboard> clipboard_;
  std::unique_ptr<platform::Application> platformApp_;
  std::thread::id mainThreadId_;
  MenuBar menuBar_;
  std::unordered_map<std::string, std::function<void()>> menuHandlers_;
  std::unordered_map<std::string, std::function<bool()>> menuEnabled_;
  std::unordered_map<platform::ShortcutKey, std::string, platform::ShortcutKeyHash> menuShortcuts_;
  std::unordered_map<std::string, CommandDescriptor> commandDescriptors_;
  std::uint64_t nextMenuHandlerId_ = 1;
  mutable bool windowStatesLoaded_ = false;
  mutable std::unordered_map<std::string, WindowState> windowStates_;

  bool quit_ = false;
  struct NextFrameEntry {
    std::uint64_t id = 0;
    std::function<void()> callback;
  };
  std::vector<NextFrameEntry> nextFrame_;
  std::uint64_t nextFrameId_ = 1;

  struct TimerEntry {
    std::uint64_t id = 0;
    std::chrono::nanoseconds interval{};
    std::chrono::steady_clock::time_point next{};
    unsigned int windowHandle = 0;
  };
  std::vector<TimerEntry> timers_;
  std::uint64_t nextTimerId_ = 1;
  bool animationFramePulseQueued_ = false;

  struct PollSourceEntry {
    std::uint64_t id = 0;
    int fd = -1;
    std::function<int()> eventMask;
    std::function<void(int)> onReady;
  };
  std::vector<PollSourceEntry> pollSources_;
  std::uint64_t nextPollSourceId_ = 1;

  void dispatchPollSources() {
    std::vector<std::uint64_t> ids;
    ids.reserve(pollSources_.size());
    for (auto const& source : pollSources_) {
      ids.push_back(source.id);
    }
    for (std::uint64_t const id : ids) {
      auto it = std::find_if(pollSources_.begin(), pollSources_.end(),
                             [&](PollSourceEntry const& entry) {
                               return entry.id == id;
                             });
      if (it == pollSources_.end() || it->fd < 0 || !it->onReady) {
        continue;
      }
      int const requestedEvents = it->eventMask ? it->eventMask() : POLLIN;
      if (requestedEvents <= 0) {
        continue;
      }
      pollfd pfd{.fd = it->fd, .events = static_cast<short>(requestedEvents), .revents = 0};
      int const ready = poll(&pfd, 1, 0);
      int const readyEvents = pfd.revents & (requestedEvents | POLLHUP | POLLERR | POLLNVAL);
      if (ready > 0 && readyEvents != 0) {
        auto onReady = it->onReady;
        onReady(readyEvents);
      }
    }
  }

  int nextTimerTimeoutMs() const {
    using namespace std::chrono;
    if (timers_.empty()) {
      return -1;
    }
    auto const now = steady_clock::now();
    auto minNext = timers_.front().next;
    for (auto const& t : timers_) {
      minNext = std::min(minNext, t.next);
    }
    if (minNext <= now) {
      return 0;
    }
    auto const ms = duration_cast<milliseconds>(minNext - now).count();
    return static_cast<int>(std::min<std::int64_t>(ms, static_cast<std::int64_t>(INT_MAX)));
  }
};

Application::Application(int argc, char** argv) {
  if (gCurrent) {
    throw std::runtime_error("Application already exists");
  }
  gCurrent = this;
  d = std::make_unique<Impl>();
  d->mainThreadId_ = std::this_thread::get_id();
  d->platformApp_ = platform::createApplication();
  d->platformApp_->initialize();
  d->platformApp_->setApplicationName(appNameFromArgv(argc, argv));
  d->textSystem_ = std::make_unique<FreeTypeTextSystem>([this] { return name(); });
  d->clipboard_ = d->platformApp_->createClipboard();
  if (!d->clipboard_) {
    d->clipboard_ = std::make_unique<MemoryClipboard>();
  }
  d->commandDescriptors_.emplace("edit.cut", editCommandDescriptor("Cut", ctrlShortcut(keys::X)));
  d->commandDescriptors_.emplace("edit.copy", editCommandDescriptor("Copy", ctrlShortcut(keys::C)));
  d->commandDescriptors_.emplace("edit.paste", editCommandDescriptor("Paste", ctrlShortcut(keys::V)));
  d->commandDescriptors_.emplace("edit.pastePlainText",
                                 editCommandDescriptor("Paste as Plain Text",
                                                       ctrlShortcut(keys::V, Modifiers::Shift)));
  d->commandDescriptors_.emplace("edit.selectAll",
                                 editCommandDescriptor("Select All", ctrlShortcut(keys::A)));
  d->platformApp_->setTerminateHandler([this] {
    saveOpenWindowStates();
    d->quit_ = true;
  });
  AnimationClock::instance().setFrameDriver(
      [this] {
        requestAnimationFrames();
      },
      [this] {
        requestRedraw();
      });

  d->eventQueue_.on<WindowLifecycleEvent>([this](WindowLifecycleEvent const& e) {
    if (e.kind == WindowLifecycleEvent::Kind::Registered && e.window != nullptr) {
      onWindowRegistered(e.window);
    }
  });

  d->eventQueue_.on<WindowEvent>([this](WindowEvent const& ev) {
    if (ev.kind == WindowEvent::Kind::DpiChanged) {
      auto it = d->byHandle_.find(ev.handle);
      if (it != d->byHandle_.end() && it->second) {
        float const sx = ev.dpiX > 0.f ? ev.dpiX : ev.dpi;
        float const sy = ev.dpiY > 0.f ? ev.dpiY : ev.dpi;
        it->second->updateCanvasDpiScale(sx, sy);
      }
    }
    if (ev.kind == WindowEvent::Kind::CloseRequest) {
      d->pendingCloseHandles_.insert(ev.handle);
    }
  });

  d->eventQueue_.on<FrameEvent>([this](FrameEvent const& ev) {
    if (!d->animationFramePulseQueued_ && AnimationClock::instance().needsFramePump()) {
      d->animationFramePulseQueued_ = true;
      d->eventQueue_.post(AnimationFramePulse{.deadlineNanos = ev.deadlineNanos});
    }
    if (ev.windowHandle == 0) {
      return;
    }
    auto it = d->renderStates_.find(ev.windowHandle);
    auto windowIt = d->byHandle_.find(ev.windowHandle);
    if (it == d->renderStates_.end() || windowIt == d->byHandle_.end() || !windowIt->second) {
      return;
    }
    it->second.frameReady = true;
    if (debug::perf::enabled() && !it->second.frameBudgetPending) {
      it->second.frameBudgetPending = true;
      it->second.frameBudgetStartedAt = std::chrono::steady_clock::time_point{
          std::chrono::nanoseconds{ev.deadlineNanos}};
    }
    windowIt->second->platformWindow()->acknowledgeAnimationFrameTick();
  });

  d->eventQueue_.on<AnimationFramePulse>([this](AnimationFramePulse const& pulse) {
    d->animationFramePulseQueued_ = false;
    AnimationClock::instance().notifyFrame(pulse.deadlineNanos);
  });
}

Application::~Application() {
  saveOpenWindowStates();
  AnimationClock::instance().shutdown();
  d->windows_.clear();
  if (gCurrent == this) {
    gCurrent = nullptr;
  }
}

void Application::adoptOwnedWindow(std::unique_ptr<Window> window) {
  Window* raw = window.get();
  unsigned int const h = raw->handle();
  d->windows_.push_back(std::move(window));
  d->byHandle_[h] = raw;
  d->renderStates_[h] = {};
  if (d->pendingAdoptRedraws_.erase(h) > 0) {
    requestWindowRedraw(h);
  } else if (AnimationClock::instance().needsFramePump()) {
    raw->platformWindow()->requestAnimationFrame();
  }
}

void Application::registerModalChildWindow(unsigned int childHandle, unsigned int parentHandle, bool modal) {
  if (!modal || childHandle == 0 || parentHandle == 0 || childHandle == parentHandle) {
    return;
  }
  d->modalParentByChild_[childHandle] = parentHandle;
}

void Application::onWindowRegistered(Window* window) {
  if (window) {
    window->platformWindow()->show();
  }
}

void Application::unregisterWindowHandle(unsigned int handle) {
  d->byHandle_.erase(handle);
  d->renderStates_.erase(handle);
  d->pendingAdoptRedraws_.erase(handle);
  d->modalParentByChild_.erase(handle);
  for (auto it = d->modalParentByChild_.begin(); it != d->modalParentByChild_.end();) {
    if (it->second == handle) {
      it = d->modalParentByChild_.erase(it);
    } else {
      ++it;
    }
  }
}

EventQueue& Application::eventQueue() { return d->eventQueue_; }
TextSystem& Application::textSystem() { return *d->textSystem_; }
platform::Application& Application::platformApp() { return *d->platformApp_; }
Clipboard& Application::clipboard() { return *d->clipboard_; }

bool Application::isWindowInputBlockedByModal(unsigned int handle) const {
  if (handle == 0) {
    return false;
  }
  for (auto const& [child, parent] : d->modalParentByChild_) {
    if (parent != handle) {
      continue;
    }
    if (d->byHandle_.find(child) != d->byHandle_.end()) {
      return true;
    }
  }
  return false;
}

void Application::setMenuBar(MenuBar menu) {
  d->menuHandlers_.clear();
  d->menuEnabled_.clear();
  d->menuShortcuts_.clear();
  for (MenuItem& item : menu.menus) {
    collectMenuState(item, d->menuHandlers_, d->menuEnabled_, d->menuShortcuts_, d->nextMenuHandlerId_);
  }
  d->menuBar_ = std::move(menu);
  d->platformApp_->setMenuBar(d->menuBar_, [this](std::string const& actionName) {
    return dispatchCommand(actionName);
  });
  d->platformApp_->revalidateMenuItems([this](std::string const& actionName) {
    return isCommandEnabled(actionName);
  });
}

void Application::registerCommand(std::string name, CommandDescriptor descriptor) {
  if (descriptor.id.empty()) {
    descriptor.id = name;
  }
  if (descriptor.title.empty() && !descriptor.label.empty()) {
    descriptor.title = descriptor.label;
  }
  if (descriptor.label.empty() && !descriptor.title.empty()) {
    descriptor.label = descriptor.title;
  }
  d->commandDescriptors_[std::move(name)] = std::move(descriptor);
}

std::unordered_map<std::string, CommandDescriptor> const& Application::commandDescriptors() const {
  return d->commandDescriptors_;
}

bool Application::dispatchCommand(std::string const& name) {
  if (name == "app.quit") {
    quit();
    return true;
  }

  auto menuHandler = d->menuHandlers_.find(name);
  if (menuHandler != d->menuHandlers_.end()) {
    auto enabled = d->menuEnabled_.find(name);
    if (enabled != d->menuEnabled_.end() && enabled->second && !enabled->second()) {
      return false;
    }
    if (menuHandler->second) {
      menuHandler->second();
      return true;
    }
    return false;
  }
  for (auto it = d->windows_.rbegin(); it != d->windows_.rend(); ++it) {
    if (*it && (*it)->dispatchCommand(name)) {
      return true;
    }
  }
  return false;
}

bool Application::isCommandEnabled(std::string const& name) const {
  auto enabled = d->menuEnabled_.find(name);
  if (enabled != d->menuEnabled_.end() && enabled->second && !enabled->second()) {
    return false;
  }
  if (d->menuHandlers_.contains(name)) {
    return true;
  }
  for (auto it = d->windows_.rbegin(); it != d->windows_.rend(); ++it) {
    if (*it && (*it)->isCommandEnabled(name)) {
      return true;
    }
  }
  return false;
}

bool Application::isCommandShortcutClaimed(KeyCode key, Modifiers modifiers) const {
  platform::ShortcutKey const shortcut{.key = key, .modifiers = modifiers};
  return d->menuShortcuts_.contains(shortcut) || d->platformApp_->menuClaimedShortcuts().contains(shortcut);
}

bool Application::dispatchCommandForShortcut(KeyCode key, Modifiers modifiers) {
  auto it = d->menuShortcuts_.find(platform::ShortcutKey{.key = key, .modifiers = modifiers});
  if (it == d->menuShortcuts_.end()) {
    return false;
  }
  return dispatchCommand(it->second);
}

void Application::setName(std::string name) {
  d->platformApp_->setApplicationName(std::move(name));
  d->windowStatesLoaded_ = false;
  d->windowStates_.clear();
}

std::string Application::name() const {
  return d->platformApp_->applicationName();
}

std::string Application::userDataDir() const { return d->platformApp_->userDataDir(); }
std::string Application::cacheDir() const { return d->platformApp_->cacheDir(); }
std::vector<std::string> Application::availableOutputs() const { return d->platformApp_->availableOutputs(); }

std::optional<WindowState> Application::loadWindowState(std::string const& restoreId) const {
  if (restoreId.empty()) {
    return std::nullopt;
  }
  if (!d->windowStatesLoaded_) {
    loadWindowStatesFromDisk(windowStatePath(userDataDir()), d->windowStates_);
    d->windowStatesLoaded_ = true;
  }
  auto it = d->windowStates_.find(restoreId);
  return it == d->windowStates_.end() ? std::nullopt : std::optional<WindowState>(it->second);
}

void Application::saveWindowState(std::string const& restoreId, WindowState const& state) {
  if (restoreId.empty()) {
    return;
  }
  if (!d->windowStatesLoaded_) {
    loadWindowStatesFromDisk(windowStatePath(userDataDir()), d->windowStates_);
    d->windowStatesLoaded_ = true;
  }
  d->windowStates_[restoreId] = state;
  saveWindowStatesToDisk(windowStatePath(userDataDir()), d->windowStates_);
}

WindowConfig Application::resolveWindowConfig(WindowConfig config) { return config; }

ObserverHandle Application::onNextFrameNeeded(std::function<void()> callback) {
  std::uint64_t const id = d->nextFrameId_++;
  d->nextFrame_.push_back(Impl::NextFrameEntry{id, std::move(callback)});
  return ObserverHandle{id};
}

void Application::unobserveNextFrame(ObserverHandle handle) {
  if (handle.isValid()) {
    std::erase_if(d->nextFrame_, [&](Impl::NextFrameEntry const& e) { return e.id == handle.id; });
  }
}

bool Application::isMainThread() const noexcept {
  return d && d->mainThreadId_ == std::this_thread::get_id();
}

void Application::wakeEventLoop() {
  for (auto& w : d->windows_) {
    if (w) {
      w->platformWindow()->wakeEventLoop();
    }
  }
}

void Application::requestAnimationFrames() {
  for (auto& w : d->windows_) {
    if (w) {
      w->platformWindow()->requestAnimationFrame();
    }
  }
  if (!isMainThread()) {
    wakeEventLoop();
  }
}

void Application::saveOpenWindowStates() {
  if (!d) {
    return;
  }
  for (auto const& window : d->windows_) {
    if (window && !window->restoreId().empty()) {
      saveWindowState(window->restoreId(), window->currentWindowState());
    }
  }
}

void Application::requestRedraw() {
  for (auto const& [handle, window] : d->byHandle_) {
    (void)window;
    requestWindowRedraw(handle);
  }
}

void Application::requestWindowRedraw(unsigned int handle) {
  auto stateIt = d->renderStates_.find(handle);
  auto windowIt = d->byHandle_.find(handle);
  if (stateIt == d->renderStates_.end() || windowIt == d->byHandle_.end() || !windowIt->second) {
    d->pendingAdoptRedraws_.insert(handle);
    return;
  }
  bool const alreadyRequested = stateIt->second.redrawRequested;
  stateIt->second.redrawRequested = true;
  if (!alreadyRequested && detail::resizeTraceEnabled()) {
    LAMBDA_RESIZE_TRACE("app-render",
                        "request window=%u already=%d frameReady=%d\n",
                        handle,
                        alreadyRequested ? 1 : 0,
                        stateIt->second.frameReady ? 1 : 0);
  }
  windowIt->second->platformWindow()->requestAnimationFrame();
  if (!alreadyRequested && !isMainThread()) {
    wakeEventLoop();
  }
}

void Application::processFrameCallbacks() {
  debug::perf::ScopedTimer perfTimer(debug::perf::TimedMetric::ProcessReactiveUpdates);
  for (auto& e : d->nextFrame_) {
    if (e.callback) {
      e.callback();
    }
  }
}

void Application::presentRequestedWindows(bool requireFrameReady, bool keepFramePump) {
  for (auto& w : d->windows_) {
    if (!w) {
      continue;
    }
    auto stateIt = d->renderStates_.find(w->handle());
    if (stateIt == d->renderStates_.end()) {
      continue;
    }
    Impl::WindowRenderState& state = stateIt->second;
    bool const hasFrameReady = state.frameReady;
    if (!state.redrawRequested && !hasFrameReady) {
      continue;
    }
    if (requireFrameReady && state.redrawRequested && !hasFrameReady) {
      if (!state.deferTraceLogged && detail::resizeTraceEnabled()) {
        LAMBDA_RESIZE_TRACE("app-render",
                            "defer window=%u requireFrameReady=1 redraw=1 frameReady=0\n",
                            w->handle());
        state.deferTraceLogged = true;
      }
      continue;
    }
    bool rendered = false;
    if (state.redrawRequested && (!requireFrameReady || hasFrameReady)) {
      state.redrawRequested = false;
      state.deferTraceLogged = false;
      if (hasFrameReady) {
        state.frameReady = false;
      }
      try {
        bool const traceResize = detail::resizeTraceEnabled();
        auto const renderStart = traceResize ? std::chrono::steady_clock::now()
                                             : std::chrono::steady_clock::time_point{};
        Canvas& canvas = w->canvas();
        auto phaseStart = renderStart;
        canvas.beginFrame();
        std::int64_t beginElapsed = 0;
        std::int64_t windowRenderElapsed = 0;
        std::int64_t presentElapsed = 0;
        if (traceResize) {
          beginElapsed = std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - phaseStart).count();
          phaseStart = std::chrono::steady_clock::now();
        }
        w->render(canvas);
        if (traceResize) {
          windowRenderElapsed = std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - phaseStart).count();
          phaseStart = std::chrono::steady_clock::now();
        }
        canvas.present();
        if (traceResize) {
          presentElapsed = std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - phaseStart).count();
        }
        rendered = true;
        if (traceResize) {
          auto const elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - renderStart).count();
          LAMBDA_RESIZE_TRACE("app-render",
                              "present window=%u requireFrameReady=%d frameReady=%d "
                              "begin=%.3fms render=%.3fms present=%.3fms elapsed=%.3fms\n",
                              w->handle(),
                              requireFrameReady ? 1 : 0,
                              hasFrameReady ? 1 : 0,
                              static_cast<double>(beginElapsed) / 1000.0,
                              static_cast<double>(windowRenderElapsed) / 1000.0,
                              static_cast<double>(presentElapsed) / 1000.0,
                              static_cast<double>(elapsed) / 1000.0);
        }
      } catch (std::exception const& e) {
        std::fprintf(stderr, "Lambda Linux render error on window %u: %s\n", w->handle(), e.what());
        d->pendingCloseHandles_.insert(w->handle());
        state.frameReady = false;
        state.frameBudgetPending = false;
        continue;
      }
    }
    if (hasFrameReady) {
      state.frameReady = false;
      state.frameBudgetPending = false;
      w->platformWindow()->completeAnimationFrame((rendered && keepFramePump) || state.redrawRequested);
    } else if (rendered && !requireFrameReady) {
      w->platformWindow()->completeAnimationFrame(keepFramePump || state.redrawRequested);
    }
  }
}

void Application::flushRedraw() {
  // Platform frame callbacks use this to present in the same dispatch turn.
  // The normal event loop still uses the frame-ready gate before this point.
  processFrameCallbacks();
  presentRequestedWindows(false, AnimationClock::instance().needsFramePump());
}

std::uint64_t Application::scheduleRepeatingTimer(std::chrono::nanoseconds interval, unsigned int windowHandle) {
  std::uint64_t const id = d->nextTimerId_++;
  d->timers_.push_back(Impl::TimerEntry{id, interval, std::chrono::steady_clock::now() + interval, windowHandle});
  return id;
}

void Application::cancelTimer(std::uint64_t timerId) {
  std::erase_if(d->timers_, [&](Impl::TimerEntry const& t) { return t.id == timerId; });
}

std::uint64_t Application::registerEventPollSource(int fd, std::function<void()> onReadable) {
  return registerEventPollSource(
      fd,
      [] {
        return POLLIN;
      },
      [onReadable = std::move(onReadable)](int) mutable {
        if (onReadable) {
          onReadable();
        }
      });
}

std::uint64_t Application::registerEventPollSource(int fd,
                                                   std::function<int()> eventMask,
                                                   std::function<void(int)> onReady) {
  if (fd < 0) {
    return 0;
  }
  std::uint64_t const id = d->nextPollSourceId_++;
  d->pollSources_.push_back(Impl::PollSourceEntry{
      .id = id,
      .fd = fd,
      .eventMask = std::move(eventMask),
      .onReady = std::move(onReady),
  });
  wakeEventLoop();
  return id;
}

void Application::unregisterEventPollSource(std::uint64_t id) {
  if (id == 0) {
    return;
  }
  std::erase_if(d->pollSources_, [&](Impl::PollSourceEntry const& entry) { return entry.id == id; });
}

int Application::exec() {
  auto closePendingWindows = [this] {
    for (unsigned int closeHandle : std::exchange(d->pendingCloseHandles_, {})) {
      auto it = std::find_if(d->windows_.begin(), d->windows_.end(),
                             [&](std::unique_ptr<Window> const& w) {
                               return w && w->handle() == closeHandle;
                             });
      if (it == d->windows_.end()) {
        continue;
      }
      if (!(*it)->restoreId().empty()) {
        saveWindowState((*it)->restoreId(), (*it)->currentWindowState());
      }
      d->byHandle_.erase(closeHandle);
      d->renderStates_.erase(closeHandle);
      d->modalParentByChild_.erase(closeHandle);
      for (auto modalIt = d->modalParentByChild_.begin(); modalIt != d->modalParentByChild_.end();) {
        if (modalIt->second == closeHandle) {
          modalIt = d->modalParentByChild_.erase(modalIt);
        } else {
          ++modalIt;
        }
      }
      d->windows_.erase(it);
      if (d->windows_.empty()) {
        quit();
        return;
      }
    }
  };

  while (!d->quit_) {
    d->dispatchPollSources();
    using namespace std::chrono;
    auto const now = steady_clock::now();
    for (auto& t : d->timers_) {
      if (now >= t.next) {
        d->eventQueue_.post(TimerEvent{duration_cast<nanoseconds>(now.time_since_epoch()).count(),
                                       t.id, t.windowHandle});
        t.next = now + t.interval;
      }
    }

    d->eventQueue_.dispatch();
    if (d->quit_) {
      break;
    }

    closePendingWindows();
    if (d->quit_) {
      break;
    }

    for (auto& w : d->windows_) {
      if (w) {
        w->platformWindow()->processEvents();
      }
    }
    d->eventQueue_.dispatch();
    closePendingWindows();
    if (d->quit_) {
      break;
    }

    processFrameCallbacks();
    presentRequestedWindows(true, AnimationClock::instance().needsFramePump());

    int timeoutMs = d->nextTimerTimeoutMs();
    if (!d->windows_.empty()) {
      bool waited = false;
      std::unordered_set<int> waitedEventFds;
      for (auto const& window : d->windows_) {
        if (!window) {
          continue;
        }
        int const eventFd = window->platformWindow()->eventFd();
        if (eventFd >= 0 && !waitedEventFds.insert(eventFd).second) {
          continue;
        }
        window->platformWindow()->waitForEvents(waited ? 0 : timeoutMs);
        waited = true;
      }
      if (!waited && timeoutMs > 0) {
        std::this_thread::sleep_for(milliseconds(timeoutMs));
      }
    } else if (timeoutMs > 0) {
      std::this_thread::sleep_for(milliseconds(timeoutMs));
    }
  }
  return 0;
}

void Application::quit() {
  d->platformApp_->requestTerminate();
  d->quit_ = true;
  for (auto& w : d->windows_) {
    if (w) {
      w->platformWindow()->wakeEventLoop();
    }
  }
}

Application& Application::instance() {
  if (!gCurrent) {
    throw std::runtime_error("Application not initialized");
  }
  return *gCurrent;
}

bool Application::hasInstance() { return gCurrent != nullptr; }

} // namespace lambdaui
