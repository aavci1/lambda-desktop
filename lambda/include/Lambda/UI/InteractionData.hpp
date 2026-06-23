#pragma once

/// \file Lambda/UI/InteractionData.hpp
///
/// Concrete UI interaction payload attached to scene-graph nodes.

#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Core/Identity.hpp>
#include <Lambda/Reactive/Bindable.hpp>
#include <Lambda/Reactive/SmallFn.hpp>
#include <Lambda/Reactive/Signal.hpp>
#include <Lambda/SceneGraph/Interaction.hpp>
#include <Lambda/SceneGraph/SceneNode.hpp>
#include <Lambda/UI/Cursor.hpp>
#include <Lambda/UI/Input.hpp>
#include <Lambda/UI/WindowChrome.hpp>

#include <string>
#include <vector>

namespace lambdaui {

struct InteractionData : public scenegraph::Interaction {
  ComponentKey stableTargetKey_{};
  Reactive::Bindable<Cursor> cursor{Cursor::Inherit};
  Reactive::Bindable<bool> focusable_{false};
  bool windowDragRegion = false;
  WindowResizeEdge windowResizeEdge = WindowResizeEdge::None;
  Reactive::SmallFn<void()> onPointerEnter;
  Reactive::SmallFn<void()> onPointerExit;
  Reactive::SmallFn<void()> onFocus;
  Reactive::SmallFn<void()> onBlur;
  Reactive::SmallFn<void(Point, MouseButton)> onPointerDown;
  Reactive::SmallFn<void(Point, MouseButton)> onPointerUp;
  Reactive::SmallFn<void(Point)> onPointerMove;
  Reactive::SmallFn<void(Vec2)> onScroll;
  Reactive::SmallFn<void(KeyCode, Modifiers)> onKeyDown;
  Reactive::SmallFn<void(KeyCode, Modifiers)> onKeyUp;
  Reactive::SmallFn<void(std::string const&)> onTextInput;
  Reactive::SmallFn<void(MouseButton)> onTap;
  Reactive::SmallFn<void(MouseButton, Modifiers)> onTapWithModifiers;
  Reactive::Signal<bool> hoverSignal;
  Reactive::Signal<bool> pressSignal;
  Reactive::Signal<bool> focusSignal;
  Reactive::Signal<bool> keyboardFocusSignal;
  std::vector<Reactive::Signal<bool>> hoverSignals;
  std::vector<Reactive::Signal<bool>> pressSignals;
  std::vector<Reactive::Signal<bool>> focusSignals;
  std::vector<Reactive::Signal<bool>> keyboardFocusSignals;

  [[nodiscard]] ComponentKey const& stableTargetKey() const noexcept override {
    return stableTargetKey_;
  }

  [[nodiscard]] bool focusable() const override {
    return focusable_.evaluate();
  }

  [[nodiscard]] bool isEmpty() const noexcept override {
    return !onPointerEnter && !onPointerExit && !onFocus && !onBlur && !onPointerDown &&
           !onPointerUp && !onPointerMove &&
           !onScroll && !onKeyDown && !onKeyUp && !onTextInput && !onTap &&
           !onTapWithModifiers &&
           hoverSignal.disposed() && pressSignal.disposed() &&
           focusSignal.disposed() && keyboardFocusSignal.disposed() &&
           hoverSignals.empty() && pressSignals.empty() &&
           focusSignals.empty() && keyboardFocusSignals.empty() &&
           !focusable_.isReactive() && !focusable_.evaluate() &&
           !cursor.isReactive() && cursor.evaluate() == Cursor::Inherit &&
           !windowDragRegion && windowResizeEdge == WindowResizeEdge::None;
  }
};

inline InteractionData const* interactionData(scenegraph::SceneNode const& node) noexcept {
  return static_cast<InteractionData const*>(node.interaction());
}

inline InteractionData* interactionData(scenegraph::SceneNode& node) noexcept {
  return static_cast<InteractionData*>(node.interaction());
}

inline InteractionData const& interactionData(scenegraph::Interaction const& interaction) noexcept {
  return static_cast<InteractionData const&>(interaction);
}

inline InteractionData& interactionData(scenegraph::Interaction& interaction) noexcept {
  return static_cast<InteractionData&>(interaction);
}

} // namespace lambdaui
