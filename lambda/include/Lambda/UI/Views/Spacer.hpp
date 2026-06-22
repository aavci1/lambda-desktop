#pragma once

/// \file Lambda/UI/Views/Spacer.hpp
///
/// Flexible empty region for HStack/VStack: expands along the stack main axis.
/// Use chained `.flex(...)` to override grow/shrink and, when needed, explicit flex-basis.

#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/ViewModifiers.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>

namespace lambda {

struct Spacer : ViewModifiers<Spacer> {
  Element body() const {
    return Element{Rectangle{}}.flex(1.f);
  }
};

} // namespace lambda
