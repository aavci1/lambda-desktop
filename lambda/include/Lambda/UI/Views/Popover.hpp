#pragma once

/// \file Lambda/UI/Views/Popover.hpp
///
/// Part of the Lambda public API.


#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Core/Color.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/Graphics/Styles.hpp>
#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/Overlay.hpp>
#include <Lambda/UI/Views/PopoverPlacement.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>

#include <functional>
#include <optional>
#include <string>
#include <tuple>

namespace lambdaui {

/// Floating card + optional arrow. Present/dismiss timing is owned by the overlay stack, not
/// `Theme::duration*` (those apply to in-body transitions such as `useAnimated` on controls).
struct Popover : ViewModifiers<Popover> {
  // ── Content ──────────────────────────────────────────────────────────────

  /// The content to display inside the popover card.
  Element content{Rectangle{}};

  // ── Placement ──────────────────────────────────────────────────────────────

  PopoverPlacement placement = PopoverPlacement::Below;
  OverlayConfig::CrossAlignment crossAlignment = OverlayConfig::CrossAlignment::Center;

  /// Gap between the anchor edge and the popover card (`kFloatFromTheme` = `Theme::space2`).
  float gap = kFloatFromTheme;

  /// When true, a callout triangle is drawn pointing at the anchor.
  bool arrow = true;

  // ── Appearance ───────────────────────────────────────────────────────────

  Color backgroundColor = Color::theme();
  Color borderColor = Color::theme();
  float borderWidth = 1.f;
  /// Uniform card radius (`kFloatFromTheme` = `Theme::radiusLarge`). Per-corner control lives in
  /// `PopoverCalloutShape` / path geometry, not this scalar.
  float cornerRadius = kFloatFromTheme;

  /// Inset between the card outline and the popover content (`kFloatFromTheme` = `Theme::space3`).
  float contentPadding = kFloatFromTheme;

  /// Maximum size of the popover content area (excluding arrow).
  /// nullopt = size to content, clamped to window bounds.
  std::optional<Size> maxSize{};

  /// Full-window dim behind the popover. Default `Color::theme()` uses `Theme::popoverScrimColor`.
  Color backdropColor = Color::theme();
  /// Full-window backdrop blur radius in logical pixels.
  float backdropBlurRadius = kFloatFromTheme;

  /// When set, the overlay anchor height is clamped to this value (use the trigger row height).
  std::optional<float> anchorMaxHeight{};

  /// Expands the resolved anchor rect before placement (useful when the visible trigger chrome is
  /// larger than the composite layout rect, such as fields that add outer padding modifiers).
  EdgeInsets anchorOutsets{};

  // ── Behaviour ────────────────────────────────────────────────────────────

  bool dismissOnEscape = true;
  bool dismissOnOutsideTap = true;
  std::function<void()> onDismiss{};

  /// When true (default), \ref usePopover prefers the last pointer-down tap anchor (same resolution
  /// as popover-demo: \c forLeafKeyPrefix on the tap leaf). Set false when using \ref useHoverLeafAnchor
  /// or an explicit \ref anchorRectOverride.
  bool useTapAnchor = true;

  /// When true (e.g. tooltips), resolve the anchor like tap-driven popovers but using the current
  /// hover leaf key through the retained scene graph's leaf-prefix lookup, and track that leaf for scroll.
  /// Matches popover-demo placement without requiring a pointer-down.
  bool useHoverLeafAnchor = false;

  /// When true, resolve the anchor from the focused interactive node. This is useful for
  /// keyboard-opened popovers where no fresh pointer/tap anchor exists.
  bool useFocusAnchor = false;

  /// When set, \ref usePopover uses this window-space rect as the anchor (skips tap, hover, and key lookup).
  std::optional<Rect> anchorRectOverride{};

  /// Set when presenting via \ref usePopover (initial resolve) and on each overlay rebuild when
  /// \ref OverlayConfig::popoverPreferredPlacement is set.
  PopoverPlacement resolvedPlacement = placement;

  /// Optional debug label for anchor/placement instrumentation.
  std::string debugName{};

  // ── Component protocol ─────────────────────────────────────────────────────

  /// body() wraps content in the card container. Not the primary API —
  /// use usePopover() instead.
  Element body() const;

  static constexpr float kArrowW = 10.f;
  static constexpr float kArrowH = 6.f;
};

/// Hook: returns (show, hide, isPresented) for presenting a Popover.
///
/// show(popover) — by default anchors to the last tap leaf (\c forLeafKeyPrefix, like popover-demo).
/// With \c Popover::useHoverLeafAnchor, anchors to the current hover leaf the same way (for tooltips).
/// Otherwise falls back to the hook caller's composite layout rect.
/// hide()        — dismisses the popover.
/// isPresented   — true while the popover is on screen.
///
/// Must be called inside body() like other hooks.
std::tuple<std::function<void(Popover)>, std::function<void()>, bool> usePopover();

/// Resolves preferred placement (flip above/below or start/end) from anchor and window size.
PopoverPlacement resolvePopoverPlacement(PopoverPlacement preferred, std::optional<Rect> const& anchor,
                                         std::optional<Size> const& maxSize, float gapTotal, Size win);

/// Resolves preferred placement using the measured total popover bounds (including card padding and arrow).
PopoverPlacement resolveMeasuredPopoverPlacement(PopoverPlacement preferred, std::optional<Rect> const& anchor,
                                                 Size popoverSize, float gap, Size win);

Vec2 popoverOverlayGapOffset(PopoverPlacement resolved, float gap);

OverlayConfig::Placement overlayPlacementFromPopover(PopoverPlacement p);

} // namespace lambdaui
