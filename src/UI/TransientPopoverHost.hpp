#pragma once

#include <Flux/Core/Geometry.hpp>
#include <Flux/Graphics/Canvas.hpp>
#include <Flux/Reactive/SmallFn.hpp>
#include <Flux/SceneGraph/SceneGraph.hpp>
#include <Flux/UI/Element.hpp>
#include <Flux/UI/EnvironmentBinding.hpp>
#include <Flux/UI/Input.hpp>
#include <Flux/UI/KeyCodes.hpp>
#include <Flux/UI/MountRoot.hpp>
#include <Flux/UI/Views/Popover.hpp>

#include <functional>
#include <memory>
#include <optional>

namespace flux {

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

  void pointerDown(Point point, MouseButton button);
  void pointerMove(Point point);
  void pointerUp(Point point, MouseButton button);
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

} // namespace flux
