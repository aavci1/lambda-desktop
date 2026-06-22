#pragma once

/// \file Lambda/Reactive/Transition.hpp
///
/// Part of the Lambda public API.


#include <Lambda/Reactive/Easing.hpp>

#include <functional>
#include <optional>
#include <vector>

namespace lambda {

struct Transition {
  float duration = 0.25f;
  float delay = 0.f;
  EasingFn easing = Easing::easeInOut;

  std::optional<std::function<float(float)>> springFn;

  static Transition instant();
  static Transition linear(float dur);
  static Transition ease(float dur = 0.25f);
  static Transition custom(EasingFn easing, float dur);
  static Transition spring(float k = 300.f, float d = 20.f, float dur = 0.6f);

  [[nodiscard]] Transition delayed(float seconds) const;
};

class WithTransition {
public:
  /// Lexical scope used by Animated::operator= on the current thread.
  ///
  /// Reactive effects suspend any ambient transition while executing. If an
  /// effect should animate its writes, create a WithTransition inside that
  /// effect body.
  explicit WithTransition(Transition t);
  ~WithTransition();

  WithTransition(WithTransition const&) = delete;
  WithTransition& operator=(WithTransition const&) = delete;

  static Transition current();
};

namespace detail {

class TransitionScopeSuspension {
public:
  TransitionScopeSuspension();
  ~TransitionScopeSuspension();

  TransitionScopeSuspension(TransitionScopeSuspension const&) = delete;
  TransitionScopeSuspension& operator=(TransitionScopeSuspension const&) = delete;

private:
  std::vector<Transition> saved_;
};

} // namespace detail

} // namespace lambda
