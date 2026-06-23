#pragma once

/// \file Lambda/UI/Views/HStack.hpp
///
/// Part of the Lambda public API.


#include <Lambda/Layout/Alignment.hpp>
#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/ViewModifiers.hpp>

#include <vector>

namespace lambdaui {

/// Horizontal stack. Use **`.padding(float)`** / **`.clipContent(bool)`** on the wrapping `Element` for
/// inset and clipping.
struct HStack : ViewModifiers<HStack> {
  Size measure(MeasureContext&, LayoutConstraints const&, LayoutHints const&, TextSystem&) const;

  /// Gap inserted between adjacent children on the horizontal axis.
  float spacing = 8.f;
  /// Cross-axis alignment (vertical in an `HStack`).
  Alignment alignment = Alignment::Center;
  /// Main-axis distribution, similar to CSS `justify-content`.
  JustifyContent justifyContent = JustifyContent::Start;
  /// Children laid out left-to-right.
  std::vector<Element> children;

};

} // namespace lambdaui
