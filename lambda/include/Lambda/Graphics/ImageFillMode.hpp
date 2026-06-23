#pragma once

/// \file Lambda/Graphics/ImageFillMode.hpp
///
/// Part of the Lambda public API.


#include <cstdint>

namespace lambdaui {

enum class ImageFillMode : std::uint8_t {
  Stretch,
  Fit,
  Cover,
  Center,
  Tile,
};

} // namespace lambdaui
