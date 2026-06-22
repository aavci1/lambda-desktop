#pragma once

/// \file Lambda/Graphics/Font.hpp
///
/// Part of the Lambda public API.


#include <cstdint>
#include <string>

namespace lambda {

/// Font family, size, and weight. For `AttributedString` runs, an empty `family`, `size <= 0`, or
/// `weight <= 0` inherits from the preceding resolved style; for UI `Text`, use concrete defaults
/// (e.g. size 16, weight 400).
struct Font {
  std::string family{};
  float size = 0.f;
  float weight = 0.f;
  bool italic = false;
  std::uint8_t semantic = 0;

  static Font theme();
  static Font largeTitle();
  static Font title();
  static Font title2();
  static Font title3();
  static Font headline();
  static Font subheadline();
  static Font body();
  static Font callout();
  static Font footnote();
  static Font caption();
  static Font caption2();
  static Font monospacedBody();

  int semanticToken() const { return static_cast<int>(semantic); }
  bool isSemantic() const { return semanticToken() != 0; }

  bool operator==(Font const& other) const = default;
};

} // namespace lambda
