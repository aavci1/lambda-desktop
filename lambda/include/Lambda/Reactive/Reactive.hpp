#pragma once

/// \file Lambda/Reactive/Reactive.hpp
///
/// Part of the Lambda public API.


#include <Lambda/Reactive/Interpolatable.hpp>
#include <Lambda/Reactive/Easing.hpp>
#include <Lambda/Reactive/Transition.hpp>
#include <Lambda/Reactive/AnimationClock.hpp>
#include <Lambda/Reactive/Animation.hpp>
#include <Lambda/Reactive/Bindable.hpp>
#include <Lambda/Reactive/Computed.hpp>
#include <Lambda/Reactive/Effect.hpp>
#include <Lambda/Reactive/Scope.hpp>
#include <Lambda/Reactive/Signal.hpp>
#include <Lambda/Reactive/Untrack.hpp>

namespace lambdaui {

template<typename T>
using Bindable = Reactive::Bindable<T>;

template<typename T>
using Computed = Reactive::Computed<T>;

using Effect = Reactive::Effect;
using Scope = Reactive::Scope;

using Reactive::makeComputed;
using Reactive::onCleanup;
using Reactive::untrack;
using Reactive::withOwner;

} // namespace lambdaui
