#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/Views/Icon.hpp>
#include <Lambda/UI/Views/Text.hpp>

namespace lambda {

std::string encodeUtf8(char32_t cp) {
    if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
        return {};
    }

    std::string out;
    out.reserve(4);

    if (cp <= 0x7Fu) {
        out.push_back(static_cast<char>(cp));
    }
    else if (cp <= 0x7FFu) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
    else if (cp <= 0xFFFFu) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
    else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }

    return out;
}

Element Icon::body() const {
    auto theme = useEnvironment<ThemeKey>();

    Reactive::Bindable<IconName> nameBinding = name;
    float const s = resolveFloat(size, theme().bodyFont.size);
    float const w = resolveFloat(weight, theme().bodyFont.weight);

    return Text {
        .text = [nameBinding] {
            return encodeUtf8(static_cast<char32_t>(nameBinding.evaluate()));
        },
        .font = Font {
            .family = theme().iconFontFamily,
            .size = s,
            .weight = w,
        },
        .color = color,
        .horizontalAlignment = HorizontalAlignment::Center,
        .verticalAlignment = VerticalAlignment::Center,
    }
        .size(s, s);
}

} // namespace lambda
