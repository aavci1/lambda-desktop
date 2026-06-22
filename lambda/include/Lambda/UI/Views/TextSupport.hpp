#pragma once

#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Core/Color.hpp>
#include <Lambda/Graphics/Font.hpp>
#include <Lambda/Graphics/TextLayoutOptions.hpp>
#include <Lambda/Graphics/TextSystem.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/TextEditUtils.hpp>

#include <string>
#include <utility>
#include <vector>

namespace lambda::text_detail {

inline std::pair<Font, Color> resolveBodyTextStyle(Font const& font, Color color) {
  auto theme = useEnvironment<ThemeKey>();
  return {
      resolveFont(font, theme().bodyFont, theme()),
      resolveColor(color, theme().labelColor, theme()),
  };
}

inline TextLayoutOptions makeTextLayoutOptions(Text const& text) {
  TextLayoutOptions o{};
  o.horizontalAlignment = text.horizontalAlignment;
  o.verticalAlignment = text.verticalAlignment;
  o.wrapping = text.wrapping;
  o.lineHeight = 0.f;
  o.lineHeightMultiple = 0.f;
  o.maxLines = text.maxLines;
  o.firstBaselineOffset = text.firstBaselineOffset;
  return o;
}

inline TextLayoutOptions makeTextLayoutOptions(TextWrapping wrapping, float lineHeight = 0.f) {
  TextLayoutOptions o{};
  o.wrapping = wrapping;
  o.lineHeight = lineHeight;
  return o;
}

inline bool hasRenderableTextGeometry(TextLayout const& layout) {
  return !layout.runs.empty() || !layout.lines.empty() || layout.measuredSize.width > 0.f ||
         layout.measuredSize.height > 0.f;
}

inline float textMeasureWidth(TextWrapping wrapping, Rect const& bounds) {
  return wrapping == TextWrapping::NoWrap ? 0.f : std::max(0.f, bounds.width);
}

inline float multiLineFitTolerance(TextLayoutOptions const& options) {
  return options.maxLines > 0 && options.wrapping != TextWrapping::NoWrap ? 1.5f : 0.5f;
}

inline bool plainTextWouldOverflow(std::string const& text, Font const& font, Color const& color,
                                   Rect const& bounds, TextLayoutOptions const& options, TextSystem& ts) {
  float const tolerance = multiLineFitTolerance(options);
  if (text.empty()) {
    return false;
  }

  if (options.wrapping == TextWrapping::NoWrap && bounds.width > 0.f) {
    Size const fullSize = ts.measure(text, font, color, 0.f, options);
    if (fullSize.width > bounds.width + tolerance) {
      return true;
    }
  }

  if (options.maxLines > 0 && bounds.height > 0.f) {
    TextLayoutOptions unlimited = options;
    unlimited.maxLines = 0;
    Size const fullSize = ts.measure(text, font, color, textMeasureWidth(options.wrapping, bounds), unlimited);
    if (fullSize.height > bounds.height + tolerance) {
      return true;
    }
  }

  return false;
}

inline bool candidateFitsWithEllipsis(std::string const& candidate, Font const& font, Color const& color,
                                      Rect const& bounds, TextLayoutOptions const& options, TextSystem& ts) {
  float const tolerance = multiLineFitTolerance(options);
  if (options.wrapping == TextWrapping::NoWrap) {
    if (bounds.width <= 0.f) {
      return true;
    }
    Size const size = ts.measure(candidate, font, color, 0.f, options);
    return size.width <= bounds.width + tolerance;
  }

  if (options.maxLines > 0 && bounds.height > 0.f) {
    TextLayoutOptions unlimited = options;
    unlimited.maxLines = 0;
    Size const size = ts.measure(candidate, font, color, textMeasureWidth(options.wrapping, bounds), unlimited);
    return size.height <= bounds.height + tolerance;
  }

  return true;
}

inline std::string trimTrailingWhitespace(std::string s) {
  while (!s.empty()) {
    unsigned char const ch = static_cast<unsigned char>(s.back());
    if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
      s.pop_back();
      continue;
    }
    break;
  }
  return s;
}

inline std::string ellipsizedPlainText(std::string const& text, Font const& font, Color const& color,
                                       Rect const& bounds, TextLayoutOptions const& options, TextSystem& ts) {
  if (!plainTextWouldOverflow(text, font, color, bounds, options, ts)) {
    return text;
  }

  std::string const ellipsis = "...";
  if (!candidateFitsWithEllipsis(ellipsis, font, color, bounds, options, ts)) {
    return ellipsis;
  }

  std::vector<int> boundaries;
  boundaries.reserve(text.size() + 1);
  boundaries.push_back(0);
  for (int pos = 0; pos < static_cast<int>(text.size());) {
    pos = detail::utf8NextChar(text, pos);
    boundaries.push_back(pos);
  }

  int low = 0;
  int high = static_cast<int>(boundaries.size()) - 1;
  int best = 0;
  while (low <= high) {
    int const mid = low + (high - low) / 2;
    std::string candidate =
        trimTrailingWhitespace(text.substr(0, static_cast<std::size_t>(boundaries[static_cast<std::size_t>(mid)])));
    candidate += ellipsis;
    if (candidateFitsWithEllipsis(candidate, font, color, bounds, options, ts)) {
      best = mid;
      low = mid + 1;
    } else {
      high = mid - 1;
    }
  }

  std::string fitted =
      trimTrailingWhitespace(text.substr(0, static_cast<std::size_t>(boundaries[static_cast<std::size_t>(best)])));
  fitted += ellipsis;
  return fitted;
}

} // namespace lambda::text_detail
