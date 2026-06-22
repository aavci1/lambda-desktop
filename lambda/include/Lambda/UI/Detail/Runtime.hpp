#pragma once

#include <Lambda/Core/Identity.hpp>
#include <Lambda/UI/Events.hpp>
#include <Lambda/Core/Geometry.hpp>
#include <Lambda/UI/CommandRegistry.hpp>

#include <memory>
#include <optional>
#include <string>

namespace lambda {

struct RootHolder;
class Window;
namespace scenegraph {
class SceneGraph;
}

class Runtime {
public:
  explicit Runtime(Window& window);
  ~Runtime();

  Runtime(Runtime const&) = delete;
  Runtime& operator=(Runtime const&) = delete;

  void setRoot(std::unique_ptr<RootHolder> holder);
  void handleInput(InputEvent const& event);
  void handleWindowEvent(WindowEvent const& event);
  void beginShutdown();
  void beginShutdown(scenegraph::SceneGraph* sceneGraph);

  bool wantsTextInput() const noexcept { return true; }
  bool textCacheOverlayEnabled() const noexcept { return false; }
  bool isCommandCurrentlyEnabled(std::string const& name) const;
  bool isCommandCurrentlyEnabledFrom(ComponentKey const& focusedKey, std::string const& name) const;
  bool dispatchCommand(std::string const& name);
  bool dispatchCommandFrom(ComponentKey const& focusedKey, std::string const& name);
  CommandRegistry& commandRegistry() noexcept;
  CommandRegistry const& commandRegistry() const noexcept;
  bool isActionCurrentlyEnabled(std::string const& name) const { return isCommandCurrentlyEnabled(name); }
  bool dispatchAction(std::string const& name) { return dispatchCommand(name); }
  ActionRegistry& actionRegistry() noexcept { return commandRegistry(); }
  ActionRegistry const& actionRegistry() const noexcept { return commandRegistry(); }
  std::optional<Rect> lastTapAnchor() const noexcept;
  std::uint32_t lastTapSerial() const noexcept;
  std::optional<Rect> hoverAnchor() const noexcept;
  std::optional<Rect> focusAnchor() const noexcept;
  std::optional<ComponentKey> lastTapTargetKey() const noexcept;
  std::optional<ComponentKey> hoverTargetKey() const noexcept;
  std::optional<ComponentKey> focusTargetKey() const noexcept;
  bool requestFocus(ComponentKey const& key);
  void requestFocusAfterLayout(ComponentKey key);
  Window& window() noexcept;
  Window const& window() const noexcept;

  static Runtime* current() noexcept;

private:
  static thread_local Runtime* current_;

  struct Impl;
  std::unique_ptr<Impl> d;
};

} // namespace lambda
