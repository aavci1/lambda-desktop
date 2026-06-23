#include <Lambda/Graphics/AttributedString.hpp>

namespace lambdaui {

AttributedString AttributedString::plain(std::string_view text, Font const &font, Color const &color) {
    return {std::string(text), {{.start = 0, .end = static_cast<std::uint32_t>(text.size()), .font = font, .color = color}}};
}

} // namespace lambdaui
