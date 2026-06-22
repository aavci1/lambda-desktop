#pragma once

#include <Lambda/Graphics/TextLayout.hpp>
#include <Lambda/UI/Detail/PrimitiveForwards.hpp>
#include <Lambda/UI/ViewModifiers.hpp>

#include <memory>

namespace lambda {

struct InternalTextLayoutLeaf : ViewModifiers<InternalTextLayoutLeaf> {
  std::shared_ptr<TextLayout const> layout;

  bool operator==(InternalTextLayoutLeaf const& other) const {
    return layout == other.layout;
  }

  Size measure(MeasureContext& ctx, LayoutConstraints const&, LayoutHints const&, TextSystem&) const {
    ctx.advanceChildSlot();
    return layout ? layout->measuredSize : Size{};
  }
};

} // namespace lambda
