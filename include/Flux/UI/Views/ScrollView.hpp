#pragma once

/// \file Flux/UI/Views/ScrollView.hpp
///
/// Part of the Flux public API.

#include <Flux/Core/Geometry.hpp>
#include <Flux/UI/Detail/PrimitiveForwards.hpp>
#include <Flux/UI/Hooks.hpp>
#include <Flux/UI/Views/OffsetView.hpp>
#include <Flux/UI/Views/Rectangle.hpp>
#include <Flux/UI/Views/ZStack.hpp>

#include <optional>
#include <functional>
#include <vector>

namespace flux {

/// Clamps \p o so the scrolled content does not overscroll past the viewport for \p axis.
/// Non-scrolling axes are zeroed (horizontal-only keeps `o.y == 0`, vertical-only keeps `o.x == 0`).
Point clampScrollOffset(ScrollAxis axis, Point o, Size const &viewport, Size const &content);

/// Scrollable region: children are laid out in an \ref OffsetView and can be dragged or wheel-scrolled.
struct ScrollView : ViewModifiers<ScrollView> {
    // ── Layout / axis ─────────────────────────────────────────────────────────

    /// Which axes may scroll.
    ScrollAxis axis = ScrollAxis::Vertical;
    /// Controlled scroll position in content coordinates.
    Signal<Point> scrollOffset {};
    /// Resolved viewport size written by the view during layout.
    Signal<Size> viewportSize {};
    /// Optional app-provided content extent; when set, preferred over stale child measurements.
    Signal<Size> declaredContentSize {};
    /// Resolved content size written by the view during layout.
    Signal<Size> contentSize {};
    /// Enables pointer drag-to-scroll in addition to wheel / trackpad scrolling.
    bool dragScrollEnabled = true;
    /// Optional tap handler attached to the scroll viewport itself.
    std::function<void(MouseButton, Modifiers)> onTap;
    /// Content children. Most callers pass a single layout container such as `VStack`.
    std::vector<Element> children;

    /// Custom measurement hook used by the measured-component pipeline.
    Size measure(MeasureContext &, LayoutConstraints const &, LayoutHints const &, TextSystem &) const;
    std::unique_ptr<scenegraph::SceneNode> mount(MountContext &) const;

};

} // namespace flux
