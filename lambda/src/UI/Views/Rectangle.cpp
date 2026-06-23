#include <Lambda/UI/Views/Rectangle.hpp>

#include <Lambda/UI/MeasureContext.hpp>

#include <algorithm>
#include <cmath>

namespace lambdaui {

Size Rectangle::measure(MeasureContext& ctx, LayoutConstraints const& constraints,
                        LayoutHints const&, TextSystem&) const {
  ctx.advanceChildSlot();
  float const width = std::isfinite(constraints.maxWidth) ? std::max(0.f, constraints.maxWidth) : 0.f;
  float const height = std::isfinite(constraints.maxHeight) ? std::max(0.f, constraints.maxHeight) : 0.f;
  return Size{width, height};
}

} // namespace lambdaui
