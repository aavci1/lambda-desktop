#pragma once

#include <Lambda/UI/MenuItem.hpp>
#include <Lambda/UI/Input.hpp>
#include <Lambda/UI/Clipboard.hpp>

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace lambda::platform {

class GpuSurfaceProvider;

struct ShortcutKey {
  KeyCode key = 0;
  Modifiers modifiers = Modifiers::None;

  bool operator==(ShortcutKey const&) const = default;
};

struct ShortcutKeyHash {
  std::size_t operator()(ShortcutKey const& value) const noexcept {
    return (static_cast<std::size_t>(value.key) << 32u) ^
           static_cast<std::size_t>(value.modifiers);
  }
};

using MenuActionDispatcher = std::function<bool(std::string const&)>;

class Application {
public:
  virtual ~Application() = default;

  virtual void initialize() = 0;
  virtual void setApplicationName(std::string name) = 0;
  virtual std::string applicationName() const = 0;
  virtual void setMenuBar(MenuBar const& menu, MenuActionDispatcher dispatcher) = 0;
  virtual void setTerminateHandler(std::function<void()> handler) = 0;
  virtual void requestTerminate() = 0;
  virtual std::unordered_set<ShortcutKey, ShortcutKeyHash> menuClaimedShortcuts() const = 0;
  virtual void revalidateMenuItems(std::function<bool(std::string const&)> isEnabled) = 0;
  virtual std::string userDataDir() const = 0;
  virtual std::string cacheDir() const = 0;
  virtual std::unique_ptr<Clipboard> createClipboard() { return nullptr; }
  virtual std::vector<std::string> availableOutputs() const { return {}; }
  virtual GpuSurfaceProvider* gpuSurfaceProvider() { return nullptr; }
};

std::unique_ptr<Application> createApplication();

} // namespace lambda::platform
