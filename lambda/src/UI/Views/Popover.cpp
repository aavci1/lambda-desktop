#include <Lambda/UI/Views/Popover.hpp>

#include <Lambda/UI/Window.hpp>
#include <Lambda/UI/Detail/Runtime.hpp>
#include <Lambda/UI/OverlaySurfaceHelpers.hpp>
#include <Lambda/UI/Views/PopoverCalloutShape.hpp>

#include <algorithm>
#include <cstdlib>
#include <memory>

namespace lambdaui {

namespace {

float availableAbove(Rect const& anchor) {
  return std::max(0.f, anchor.y);
}

float availableBelow(Rect const& anchor, Size window) {
  return std::max(0.f, window.height - (anchor.y + anchor.height));
}

float availableStart(Rect const& anchor) {
  return std::max(0.f, anchor.x);
}

float availableEnd(Rect const& anchor, Size window) {
  return std::max(0.f, window.width - (anchor.x + anchor.width));
}

bool nativePopoversEnabled() {
  char const* disabled = std::getenv("LAMBDA_DISABLE_NATIVE_POPOVERS");
  return !disabled || !*disabled || *disabled == '0';
}

Rect adjustAnchor(Rect anchor, Popover const& popover) {
  if (popover.anchorMaxHeight && anchor.height > *popover.anchorMaxHeight) {
    anchor.height = *popover.anchorMaxHeight;
  }
  EdgeInsets const outsets = popover.anchorOutsets;
  anchor.x -= outsets.left;
  anchor.y -= outsets.top;
  anchor.width += outsets.left + outsets.right;
  anchor.height += outsets.top + outsets.bottom;
  anchor.width = std::max(0.f, anchor.width);
  anchor.height = std::max(0.f, anchor.height);
  return anchor;
}

std::optional<Rect> resolvePopoverAnchor(Popover const& popover, Runtime* runtime) {
  std::optional<Rect> anchor = popover.anchorRectOverride;
  if (!anchor && runtime && popover.useHoverLeafAnchor) {
    anchor = runtime->hoverAnchor();
  }
  if (!anchor && runtime && popover.useFocusAnchor) {
    anchor = runtime->focusAnchor();
  }
  if (!anchor && runtime && popover.useTapAnchor) {
    anchor = runtime->lastTapAnchor();
  }
  if (anchor) {
    return adjustAnchor(*anchor, popover);
  }
  return std::nullopt;
}

std::optional<Size> estimatedPopoverOuterSize(Popover const& popover, Theme const& theme) {
  if (!popover.maxSize) {
    return std::nullopt;
  }
  float const pad = resolveFloat(popover.contentPadding, theme.space3);
  float const arrowDepth = popover.arrow ? PopoverCalloutShape::kArrowH : 0.f;
  return Size{
      std::max(0.f, popover.maxSize->width + 2.f * pad + arrowDepth),
      std::max(0.f, popover.maxSize->height + 2.f * pad + arrowDepth),
  };
}

PopoverPlacement popoverPlacementFromOverlay(OverlayConfig::Placement placement) {
  switch (placement) {
  case OverlayConfig::Placement::Below:
    return PopoverPlacement::Below;
  case OverlayConfig::Placement::Above:
    return PopoverPlacement::Above;
  case OverlayConfig::Placement::End:
    return PopoverPlacement::End;
  case OverlayConfig::Placement::Start:
    return PopoverPlacement::Start;
  }
  return PopoverPlacement::Below;
}

} // namespace

Element Popover::body() const {
  auto theme = useEnvironment<ThemeKey>();
  auto overlayPlacement = useEnvironment<ResolvedOverlayPlacementKey>();
  ResolvedPopoverCardBody const surface =
      resolvePopoverCardBody(backgroundColor, borderColor, borderWidth, cornerRadius,
                             contentPadding, theme());

  return PopoverCalloutShape{
      .placement = [overlayPlacement, fallback = resolvedPlacement] {
        std::optional<OverlayConfig::Placement> const resolved = overlayPlacement();
        return resolved ? popoverPlacementFromOverlay(*resolved) : fallback;
      },
      .arrow = arrow,
      .padding = surface.contentPadding,
      .cornerRadius = surface.cornerRadius,
      .backgroundColor = surface.background,
      .borderColor = surface.border,
      .borderWidth = surface.borderWidth,
      .maxSize = maxSize,
      .content = content,
  };
}

std::tuple<std::function<void(Popover)>, std::function<void()>, bool> usePopover() {
  Runtime* runtime = Runtime::current();
  Window* window = runtime ? &runtime->window() : nullptr;
  auto id = std::make_shared<PopoverSurfaceId>(kInvalidPopoverSurfaceId);
  auto overlayId = std::make_shared<OverlayId>(kInvalidOverlayId);

  Reactive::onCleanup([id, overlayId, window] {
    if (window && id->isValid()) {
      PopoverSurfaceId const dismissId = *id;
      *id = kInvalidPopoverSurfaceId;
      window->dismissPopover(dismissId);
    }
    if (window && overlayId->isValid()) {
      OverlayId const removeId = *overlayId;
      *overlayId = kInvalidOverlayId;
      window->removeOverlay(removeId);
    }
  });

  auto hide = [id, overlayId, window] {
    if (!window) {
      return;
    }
    if (id->isValid()) {
      PopoverSurfaceId const dismissId = *id;
      *id = kInvalidPopoverSurfaceId;
      window->dismissPopover(dismissId);
    }
    if (overlayId->isValid()) {
      OverlayId const removeId = *overlayId;
      *overlayId = kInvalidOverlayId;
      window->removeOverlay(removeId);
    }
  };

  auto show = [id, overlayId, runtime, window](Popover popover) mutable {
    if (!runtime || !window) {
      return;
    }
    Theme const theme = window ? window->theme() : Theme::light();
    PopoverPlacement const preferred = popover.placement;
    std::optional<Rect> const anchor = resolvePopoverAnchor(popover, runtime);
    Size const windowSize = window ? window->getSize() : Size{};
    float const gap = resolveFloat(popover.gap, theme.space2);
    std::optional<Size> const estimatedOuterSize = estimatedPopoverOuterSize(popover, theme);
    PopoverPlacement const resolved =
        anchor ? resolvePopoverPlacement(preferred, anchor, estimatedOuterSize, gap, windowSize)
               : preferred;
    popover.resolvedPlacement = resolved;
    if (id->isValid()) {
      PopoverSurfaceId const dismissId = *id;
      *id = kInvalidPopoverSurfaceId;
      window->dismissPopover(dismissId);
    }
    if (overlayId->isValid()) {
      OverlayId const removeId = *overlayId;
      *overlayId = kInvalidOverlayId;
      window->removeOverlay(removeId);
    }

    std::optional<ComponentKey> anchorTrackComponentKey;
    std::optional<ComponentKey> anchorTrackLeafKey;
    if (!popover.anchorRectOverride) {
      if (popover.useHoverLeafAnchor) {
        anchorTrackLeafKey = runtime->hoverTargetKey();
      } else if (popover.useFocusAnchor) {
        anchorTrackComponentKey = runtime->focusTargetKey();
      } else {
        anchorTrackComponentKey = runtime->lastTapTargetKey();
      }
    }

    if (nativePopoversEnabled()) {
      Popover platformPopover = popover;
      *id = window->showPopover(std::move(platformPopover), anchor.value_or(Rect{0.f, 0.f, 1.f, 1.f}),
                                runtime->lastTapSerial(), anchorTrackComponentKey, anchorTrackLeafKey);
      if (id->isValid()) {
        return;
      }
    }

    OverlayConfig config{
        .anchor = anchor,
        .anchorTrackLeafKey = anchorTrackLeafKey,
        .anchorTrackComponentKey = anchorTrackComponentKey,
        .placement = overlayPlacementFromPopover(resolved),
        .autoFlipPreferredPlacement = overlayPlacementFromPopover(preferred),
        .autoFlipGap = gap,
        .crossAlignment = popover.crossAlignment,
        .offset = popoverOverlayGapOffset(resolved, gap),
        .modal = false,
        .backdropColor = resolvePopoverBackdropColor(popover.backdropColor, theme),
        .backdropBlurRadius = resolveFloat(popover.backdropBlurRadius, theme.popoverBackdropBlurRadius),
        .dismissOnOutsideTap = popover.dismissOnOutsideTap,
        .dismissOnEscape = popover.dismissOnEscape,
        .onDismiss = popover.onDismiss,
        .debugName = popover.debugName,
    };
    *overlayId = window->pushOverlay(Element{std::move(popover)}, std::move(config));
  };

  return {std::move(show), std::move(hide), id->isValid() || overlayId->isValid()};
}

PopoverPlacement resolvePopoverPlacement(PopoverPlacement preferred, std::optional<Rect> const& anchor,
                                         std::optional<Size> const& maxSize, float gapTotal,
                                         Size window) {
  if (!anchor) {
    return preferred;
  }
  Size const desired = maxSize.value_or(Size{0.f, 0.f});
  float const desiredWidth = desired.width + gapTotal;
  float const desiredHeight = desired.height + gapTotal;

  switch (preferred) {
  case PopoverPlacement::Below:
    if (desiredHeight > 0.f && availableBelow(*anchor, window) < desiredHeight &&
        availableAbove(*anchor) > availableBelow(*anchor, window)) {
      return PopoverPlacement::Above;
    }
    return PopoverPlacement::Below;
  case PopoverPlacement::Above:
    if (desiredHeight > 0.f && availableAbove(*anchor) < desiredHeight &&
        availableBelow(*anchor, window) > availableAbove(*anchor)) {
      return PopoverPlacement::Below;
    }
    return PopoverPlacement::Above;
  case PopoverPlacement::End:
    if (desiredWidth > 0.f && availableEnd(*anchor, window) < desiredWidth &&
        availableStart(*anchor) > availableEnd(*anchor, window)) {
      return PopoverPlacement::Start;
    }
    return PopoverPlacement::End;
  case PopoverPlacement::Start:
    if (desiredWidth > 0.f && availableStart(*anchor) < desiredWidth &&
        availableEnd(*anchor, window) > availableStart(*anchor)) {
      return PopoverPlacement::End;
    }
    return PopoverPlacement::Start;
  }
  return preferred;
}

PopoverPlacement resolveMeasuredPopoverPlacement(PopoverPlacement preferred,
                                                 std::optional<Rect> const& anchor,
                                                 Size popoverSize, float gap, Size window) {
  return resolvePopoverPlacement(preferred, anchor, popoverSize, gap, window);
}

Vec2 popoverOverlayGapOffset(PopoverPlacement resolved, float gap) {
  switch (resolved) {
  case PopoverPlacement::Below:
    return Vec2{0.f, gap};
  case PopoverPlacement::Above:
    return Vec2{0.f, -gap};
  case PopoverPlacement::End:
    return Vec2{gap, 0.f};
  case PopoverPlacement::Start:
    return Vec2{-gap, 0.f};
  }
  return Vec2{};
}

OverlayConfig::Placement overlayPlacementFromPopover(PopoverPlacement placement) {
  switch (placement) {
  case PopoverPlacement::Below:
    return OverlayConfig::Placement::Below;
  case PopoverPlacement::Above:
    return OverlayConfig::Placement::Above;
  case PopoverPlacement::End:
    return OverlayConfig::Placement::End;
  case PopoverPlacement::Start:
    return OverlayConfig::Placement::Start;
  }
  return OverlayConfig::Placement::Below;
}

} // namespace lambdaui
