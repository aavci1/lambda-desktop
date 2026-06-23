#include <Lambda/Reactive/Transition.hpp>

#include <utility>

namespace lambdaui {

Transition Transition::instant() {
  Transition t{};
  t.duration = 0.f;
  return t;
}

Transition Transition::linear(float dur) {
  Transition t{};
  t.duration = dur;
  t.easing = Easing::linear;
  return t;
}

Transition Transition::ease(float dur) {
  Transition t{};
  t.duration = dur;
  t.easing = Easing::easeInOut;
  return t;
}

Transition Transition::custom(EasingFn easing, float dur) {
  Transition t{};
  t.duration = dur;
  t.easing = easing ? easing : Easing::linear;
  return t;
}

Transition Transition::spring(float k, float d, float dur) {
  Transition t{};
  t.duration = dur;
  t.delay = 0.f;
  t.easing = Easing::linear;
  t.springFn = Easing::spring(k, d);
  return t;
}

Transition Transition::delayed(float seconds) const {
  Transition t = *this;
  t.delay = seconds;
  return t;
}

namespace {

thread_local std::vector<Transition> gTransitionStack;

} // namespace

WithTransition::WithTransition(Transition t) {
  gTransitionStack.push_back(t);
}

WithTransition::~WithTransition() {
  if (!gTransitionStack.empty()) {
    gTransitionStack.pop_back();
  }
}

Transition WithTransition::current() {
  if (gTransitionStack.empty()) {
    return Transition::instant();
  }
  return gTransitionStack.back();
}

namespace detail {

TransitionScopeSuspension::TransitionScopeSuspension()
    : saved_(std::move(gTransitionStack)) {
  gTransitionStack.clear();
}

TransitionScopeSuspension::~TransitionScopeSuspension() {
  gTransitionStack = std::move(saved_);
}

} // namespace detail

} // namespace lambdaui
