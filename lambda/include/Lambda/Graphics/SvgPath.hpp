#pragma once

/// \file Lambda/Graphics/SvgPath.hpp
///
/// SVG path-data parser for Lambda paths.

#include <Lambda/Graphics/Path.hpp>

#include <cstddef>
#include <string>
#include <string_view>

namespace lambda {

struct SvgPathParseError {
  std::size_t position = 0;
  std::string message;
};

/// Parse an SVG path-data string (`d="M 0,0 L 10,10 Z"`) into a Lambda Path.
///
/// Supports SVG path commands M/m, L/l, H/h, V/v, C/c, S/s, Q/q, T/t, A/a, Z/z.
/// Malformed input returns the partial path parsed so far and writes a diagnostic to `error`
/// when provided.
Path parseSvgPath(std::string_view d, SvgPathParseError* error = nullptr);

} // namespace lambda
