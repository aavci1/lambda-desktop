#pragma once

/// \file Lambda/UI/Views/PopoverPlacement.hpp
///
/// Part of the Lambda public API.


#include <cstdint>

namespace lambdaui {

/// Preferred placement of the popover relative to its anchor.
/// Automatically flipped to the opposite direction if insufficient room.
enum class PopoverPlacement : std::uint8_t {
  Below, ///< Default — content appears below the anchor
  Above,
  End, ///< To the right of the anchor (LTR)
  Start, ///< To the left of the anchor (LTR)
};

} // namespace lambdaui
