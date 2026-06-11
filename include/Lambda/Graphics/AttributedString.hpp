#pragma once

/// \file Lambda/Graphics/AttributedString.hpp
///
/// Part of the Lambda public API.

#include <Lambda/Core/Color.hpp>
#include <Lambda/Graphics/Font.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lambda {

struct AttributedRun {
    std::uint32_t start = 0; // byte offset into utf8, inclusive
    std::uint32_t end = 0;   // byte offset, exclusive
    Font font {};
    Color color = Colors::black;
    std::optional<Color> backgroundColor{};
};

struct AttributedString {
    std::string utf8{};
    std::vector<AttributedRun> runs{}; // sorted by start, non-overlapping

    static AttributedString plain(std::string_view text, Font const &font, Color const &color);
};

} // namespace lambda
