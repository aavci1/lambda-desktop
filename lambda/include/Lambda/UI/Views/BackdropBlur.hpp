#pragma once

/// \file Lambda/UI/Views/BackdropBlur.hpp
///
/// Part of the Lambda public API.

#include <Lambda/Graphics/Styles.hpp>
#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/ViewModifiers.hpp>

namespace lambdaui {

/// A rectangular backdrop-filter region. The renderer samples already-rendered
/// window content, blurs it once per frame, then masks this region from that
/// blurred backdrop texture.
struct BackdropBlur : ViewModifiers<BackdropBlur> {
  float radius = 18.f;
  Color tint = Colors::transparent;
  CornerRadius corners{};

  bool operator==(BackdropBlur const& other) const {
    return radius == other.radius && tint == other.tint && corners == other.corners;
  }

  Element body() const;
};

} // namespace lambdaui
