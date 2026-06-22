#pragma once

/// \file Lambda/UI/Views/VStack.hpp
///
/// Part of the Lambda public API.


#include <Lambda/Layout/Alignment.hpp>
#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/ViewModifiers.hpp>

#include <vector>

namespace lambda {

/// Vertical stack. Use **`.clipContent(bool)`** on the wrapping `Element` to clip children to bounds.
struct VStack : ViewModifiers<VStack> {
  Size measure(MeasureContext&, LayoutConstraints const&, LayoutHints const&, TextSystem&) const;

  /// Gap inserted between adjacent children on the vertical axis.
  float spacing = 8.f;
  /// Cross-axis alignment (horizontal in a `VStack`).
  Alignment alignment = Alignment::Center;
  /// Main-axis distribution, similar to CSS `justify-content`.
  JustifyContent justifyContent = JustifyContent::Start;
  /// Children laid out top-to-bottom.
  std::vector<Element> children;

};

} // namespace lambda
