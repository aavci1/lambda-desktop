#include <Lambda/UI/InputFieldLayout.hpp>

#include <Lambda/UI/Application.hpp>
#include <Lambda/Graphics/TextLayoutOptions.hpp>
#include <Lambda/Graphics/TextSystem.hpp>

#include <algorithm>

namespace lambdaui {

namespace {

// Selection clip slack below the line (single-line inputs, TextInput).
constexpr float kSelectionVerticalSlackPx = 4.f;

} // namespace

float resolvedInputFieldHeight(Font const& font, Color textInkColor, float paddingV, float explicitHeight) {
  TextSystem& ts = Application::instance().textSystem();
  TextLayoutOptions mopts{};
  mopts.wrapping = TextWrapping::NoWrap;
  Size const line = ts.measure("Agy", font, textInkColor, 0.f, mopts);
  float const minBodyH = line.height + 2.f * paddingV + kSelectionVerticalSlackPx;
  if (explicitHeight > 0.f) {
    return std::max(explicitHeight, minBodyH);
  }
  return minBodyH;
}

} // namespace lambdaui
