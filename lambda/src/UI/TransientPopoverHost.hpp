#pragma once

#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Graphics/Canvas.hpp>
#include <Lambda/Reactive/SmallFn.hpp>
#include <Lambda/SceneGraph/SceneGraph.hpp>
#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/EnvironmentBinding.hpp>
#include <Lambda/UI/Input.hpp>
#include <Lambda/UI/KeyCodes.hpp>
#include <Lambda/UI/MountRoot.hpp>
#include <Lambda/UI/Views/Popover.hpp>

#include <functional>
#include <memory>
#include <optional>

namespace lambdaui {

class TransientPopoverHost {
public:
  struct Config {
    Popover popover;
    EnvironmentBinding environment;
    Size maxSize;
    bool useNativeShell = false;
    std::function<void()> requestRedraw;
    std::function<void()> requestDismiss;
  };

  explicit TransientPopoverHost(Config config);
  ~TransientPopoverHost();

  Size measuredSize() const noexcept { return measuredSize_; }
  void mount(Size size);
  void resize(Size size);
  void render(Canvas& canvas);

  void pointerDown(Point point, MouseButton button, Modifiers modifiers = Modifiers::None);
  void pointerMove(Point point);
  void pointerUp(Point point, MouseButton button, Modifiers modifiers = Modifiers::None);
  void scroll(Point point, Vec2 delta);
  void keyDown(KeyCode key, Modifiers modifiers);
  void keyUp(KeyCode key, Modifiers modifiers);
  void textInput(std::string const& text);

  void notifyDismissed();

private:
  struct Impl;
  std::unique_ptr<Impl> d;
  Size measuredSize_{};
};

} // namespace lambdaui
