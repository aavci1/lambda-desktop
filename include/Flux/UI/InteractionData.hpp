#pragma once

/// \file Flux/UI/InteractionData.hpp
///
/// Concrete UI interaction payload attached to scene-graph nodes.

#include <Flux/Core/Geometry.hpp>
#include <Flux/Core/Identity.hpp>
#include <Flux/Reactive/Bindable.hpp>
#include <Flux/Reactive/SmallFn.hpp>
#include <Flux/Reactive/Signal.hpp>
#include <Flux/SceneGraph/Interaction.hpp>
#include <Flux/SceneGraph/SceneNode.hpp>
#include <Flux/UI/Cursor.hpp>
#include <Flux/UI/Input.hpp>
#include <Flux/UI/WindowChrome.hpp>

#include <string>

namespace flux {

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
  Reactive::Signal<bool> hoverSignal;
  Reactive::Signal<bool> pressSignal;
  Reactive::Signal<bool> focusSignal;
  Reactive::Signal<bool> keyboardFocusSignal;

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
           hoverSignal.disposed() && pressSignal.disposed() &&
           focusSignal.disposed() && keyboardFocusSignal.disposed() &&
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

} // namespace flux
