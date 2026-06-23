#pragma once

/// \file Lambda/UI/Application.hpp
///
/// Part of the Lambda public API.


#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Lambda/UI/Clipboard.hpp>
#include <Lambda/UI/MenuItem.hpp>
#include <Lambda/UI/Window.hpp>
#include <Lambda/Reactive/Observer.hpp>

namespace lambdaui {

class EventQueue;
class TextSystem;
namespace platform {
class Application;
}

namespace detail {

void pushWindowCreationModalParent(unsigned int parentHandle, bool modal);
void popWindowCreationModalParent();
unsigned int currentWindowCreationTransientParentHandle();
bool currentWindowCreationModal();

class ScopedWindowCreationModalParent {
public:
  ScopedWindowCreationModalParent(unsigned int parentHandle, bool modal) {
    pushWindowCreationModalParent(parentHandle, modal);
  }

  ~ScopedWindowCreationModalParent() {
    popWindowCreationModalParent();
  }

  ScopedWindowCreationModalParent(ScopedWindowCreationModalParent const&) = delete;
  ScopedWindowCreationModalParent& operator=(ScopedWindowCreationModalParent const&) = delete;
};

} // namespace detail

class Application {
public:
  explicit Application(int argc = 0, char** argv = nullptr);
  ~Application();

  Application(const Application&) = delete;
  Application& operator=(const Application&) = delete;
  Application(Application&&) = delete;
  Application& operator=(Application&&) = delete;

  template<typename T = Window, typename... Args>
  T& createWindow(WindowConfig const& config, Args&&... args) {
    static_assert(std::is_base_of_v<Window, T>);
    WindowConfig resolvedConfig = resolveWindowConfig(config);
    std::optional<WindowState> restoredState;
    if (!config.restoreId.empty()) {
      restoredState = loadWindowState(config.restoreId);
      if (restoredState && !restoredState->contentSize.isEmpty()) {
        resolvedConfig.size = restoredState->contentSize;
        if (config.minSize.width > 0.f) {
          resolvedConfig.size.width = std::max(resolvedConfig.size.width, config.minSize.width);
        }
        if (config.minSize.height > 0.f) {
          resolvedConfig.size.height = std::max(resolvedConfig.size.height, config.minSize.height);
        }
        if (config.maxSize.width > 0.f) {
          resolvedConfig.size.width = std::min(resolvedConfig.size.width, config.maxSize.width);
        }
        if (config.maxSize.height > 0.f) {
          resolvedConfig.size.height = std::min(resolvedConfig.size.height, config.maxSize.height);
        }
      }
    }
    // `new T` must run in a member of `Application` so `friend Application` can call `Window`'s protected ctor.
    auto window = std::unique_ptr<T>(new T(resolvedConfig, std::forward<Args>(args)...));
    T* raw = window.get();
    if (restoredState && restoredState->frame.width > 0.f && restoredState->frame.height > 0.f) {
      raw->applyRestoredWindowState(*restoredState);
    }
    adoptOwnedWindow(std::move(window));
    if (restoredState && restoredState->fullscreen) {
      raw->setFullscreen(true);
    }
    return *raw;
  }

  template<typename T = Window, typename... Args>
  T& createModalChildWindow(unsigned int parentHandle, WindowConfig const& config, Args&&... args) {
    detail::ScopedWindowCreationModalParent creationScope(parentHandle, true);
    T& window = createWindow<T>(config, std::forward<Args>(args)...);
    registerModalChildWindow(window.handle(), parentHandle, true);
    static_cast<Window&>(window).setTransientParent(parentHandle, true);
    return window;
  }

  int exec();
  void quit();

  /// Marks all windows for a render pass on the next `exec()` iteration and wakes the platform event wait.
  void requestRedraw();
  /// Marks a specific window for a render pass on the next platform frame tick.
  void requestWindowRedraw(unsigned int handle);

  /// Presents pending frames immediately. Use when the main loop may not iterate (e.g. live window resize runs
  /// the run loop in `NSEventTrackingRunLoopMode`, so `waitForEvents` in `NSDefaultRunLoopMode` does not return).
  void flushRedraw();
  /// Wakes any blocking platform event wait without forcing a rebuild/redraw.
  void wakeEventLoop();

  /// Repeating timer using `std::chrono::steady_clock` in the main `exec()` loop; posts `TimerEvent` each tick.
  /// Returns an id for `cancelTimer`. `windowHandle` is optional metadata for handlers (e.g. redraw routing).
  std::uint64_t scheduleRepeatingTimer(std::chrono::nanoseconds interval, unsigned int windowHandle = 0);
  void cancelTimer(std::uint64_t timerId);

  static bool hasInstance();
  static Application& instance();

  EventQueue& eventQueue();

  TextSystem& textSystem();
  platform::Application& platformApp();

  /// Returns the process-wide system clipboard.
  /// The returned reference is valid for the lifetime of the Application.
  Clipboard& clipboard();

  void setMenuBar(MenuBar menu);
  void registerCommand(std::string name, CommandDescriptor descriptor);
  [[nodiscard]] std::unordered_map<std::string, CommandDescriptor> const& commandDescriptors() const;
  bool dispatchCommand(std::string const& name);
  bool isCommandEnabled(std::string const& name) const;
  bool isCommandShortcutClaimed(KeyCode key, Modifiers modifiers) const;
  bool dispatchCommandForShortcut(KeyCode key, Modifiers modifiers);

  bool dispatchAction(std::string const& name) { return dispatchCommand(name); }
  bool isActionEnabled(std::string const& name) const { return isCommandEnabled(name); }
  bool isMenuShortcutClaimed(KeyCode key, Modifiers modifiers) const {
    return isCommandShortcutClaimed(key, modifiers);
  }
  bool dispatchActionForShortcut(KeyCode key, Modifiers modifiers) {
    return dispatchCommandForShortcut(key, modifiers);
  }

  void setName(std::string name);
  std::string name() const;
  std::string userDataDir() const;
  std::string cacheDir() const;
  /// Returns platform output names when available. KMS returns DRM connector names such as "eDP-1" or "HDMI-A-1".
  std::vector<std::string> availableOutputs() const;
  /// True when another live window is registered as a modal child of this window.
  bool isWindowInputBlockedByModal(unsigned int handle) const;
  std::optional<WindowState> loadWindowState(std::string const& restoreId) const;
  void saveWindowState(std::string const& restoreId, WindowState const& state);

  /// Batched callback: runs at most once per `exec()` iteration after any reactive update.
  ObserverHandle onNextFrameNeeded(std::function<void()> callback);
  void unobserveNextFrame(ObserverHandle handle);

  /// Registers a non-blocking file descriptor polled each `exec()` iteration before platform I/O.
  /// Returns an opaque id for `unregisterEventPollSource`. `fd` must stay valid until unregister.
  std::uint64_t registerEventPollSource(int fd, std::function<void()> onReadable);
  std::uint64_t registerEventPollSource(int fd,
                                        std::function<int()> eventMask,
                                        std::function<void(int)> onReady);
  void unregisterEventPollSource(std::uint64_t id);

  friend class Window;
  friend class EventQueue;
private:
  bool isMainThread() const noexcept;
  /// Arms platform frame pumps without marking any window dirty for redraw.
  void requestAnimationFrames();
  void saveOpenWindowStates();
  void adoptOwnedWindow(std::unique_ptr<Window> window);
  void registerModalChildWindow(unsigned int childHandle, unsigned int parentHandle, bool modal);
  WindowConfig resolveWindowConfig(WindowConfig config);
  /// Invoked when `WindowLifecycleEvent::Registered` is dispatched (first `exec()` `dispatch()` drains the ctor post).
  void onWindowRegistered(Window* window);
  /// Removes `handle` from the running window list before `Window` is destroyed (synchronous; avoids dangling `Window*`).
  void unregisterWindowHandle(unsigned int handle);

  void processFrameCallbacks();
  void presentRequestedWindows(bool requireFrameReady, bool keepFramePump);

  struct Impl;
  std::unique_ptr<Impl> d;
};

} // namespace lambdaui
