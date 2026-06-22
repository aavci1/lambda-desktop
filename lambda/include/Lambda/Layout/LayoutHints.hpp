#pragma once

/// \file Lambda/Layout/LayoutHints.hpp
///
/// Cross-axis alignment hints carried through retained-scene measurement.

#include <Lambda/Layout/Alignment.hpp>

#include <optional>

namespace lambda {

struct LayoutHints {
  std::optional<Alignment> hStackCrossAlign;
  std::optional<Alignment> vStackCrossAlign;
  std::optional<Alignment> zStackHorizontalAlign;
  std::optional<Alignment> zStackVerticalAlign;
};

} // namespace lambda
