#include <Lambda/UI/Views/Toast.hpp>

#include <Lambda/UI/Window.hpp>
#include <Lambda/UI/Detail/Runtime.hpp>
#include <Lambda/UI/Overlay.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Icon.hpp>
#include <Lambda/UI/Views/Spacer.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/VStack.hpp>

#include <algorithm>
#include <memory>
#include <unordered_map>

namespace lambdaui {

namespace {

struct ToastRegistry {
  std::uint64_t nextToastId = 1;
  std::unordered_map<std::uint64_t, OverlayId> overlays;
  std::unordered_map<std::uint64_t, ToastPlacement> placements;
};

IconName iconForTone(ToastTone tone) {
  switch (tone) {
  case ToastTone::Success:
    return IconName::CheckCircle;
  case ToastTone::Warning:
    return IconName::Warning;
  case ToastTone::Danger:
    return IconName::Error;
  case ToastTone::Accent:
    return IconName::Bolt;
  case ToastTone::Neutral:
    return IconName::Notifications;
  }
  return IconName::Notifications;
}

Color colorForTone(ToastTone tone, Theme const& theme) {
  switch (tone) {
  case ToastTone::Success:
    return theme.successColor;
  case ToastTone::Warning:
    return theme.warningColor;
  case ToastTone::Danger:
    return theme.dangerColor;
  case ToastTone::Accent:
    return theme.accentColor;
  case ToastTone::Neutral:
    return theme.secondaryLabelColor;
  }
  return theme.secondaryLabelColor;
}

Color backgroundForTone(ToastTone tone, Theme const& theme) {
  switch (tone) {
  case ToastTone::Success:
    return theme.successBackgroundColor;
  case ToastTone::Warning:
    return theme.warningBackgroundColor;
  case ToastTone::Danger:
    return theme.dangerBackgroundColor;
  case ToastTone::Accent:
    return theme.selectedContentBackgroundColor;
  case ToastTone::Neutral:
    return theme.controlBackgroundColor;
  }
  return theme.controlBackgroundColor;
}

struct ToastCard {
  Toast toast;
  std::function<void(std::uint64_t)> dismiss;

  Element body() const {
    auto theme = useEnvironment<ThemeKey>();
    Color const toneColor = colorForTone(toast.tone, theme());
    Color const toneBackground = backgroundForTone(toast.tone, theme());
    std::uint64_t const toastId = toast.id;

    std::vector<Element> trailing;
    if (toast.action) {
      ToastAction action = *toast.action;
      trailing.push_back(Button {
          .label = action.label,
          .variant = action.variant,
          .onTap = [action = std::move(action), dismiss = dismiss, toastId] {
            if (action.action) {
              action.action();
            }
            if (action.dismissOnTap && dismiss) {
              dismiss(toastId);
            }
          },
      });
    }
    if (toast.showCloseButton) {
      trailing.push_back(Button {
          .label = "Dismiss",
          .variant = ButtonVariant::Ghost,
          .onTap = [dismiss = dismiss, toastId] {
            if (dismiss) {
              dismiss(toastId);
            }
          },
      });
    }

    std::vector<Element> rowChildren;
    rowChildren.reserve(3);
    rowChildren.push_back(
        Icon {
            .name = toast.icon.value_or(iconForTone(toast.tone)),
            .size = 20.f,
            .color = toneColor,
        }
            .padding(theme().space1)
            .fill(FillStyle::solid(toneBackground))
            .cornerRadius(CornerRadius {theme().radiusFull})
    );
    rowChildren.push_back(
        VStack {
            .spacing = theme().space1,
            .alignment = Alignment::Start,
            .children = children(
                Text {
                    .text = toast.title,
                    .font = Font::headline(),
                    .color = Color::primary(),
                    .horizontalAlignment = HorizontalAlignment::Leading,
                },
                Text {
                    .text = toast.message,
                    .font = Font::footnote(),
                    .color = Color::secondary(),
                    .horizontalAlignment = HorizontalAlignment::Leading,
                    .wrapping = TextWrapping::Wrap,
                }
            ),
        }
            .flex(1.f, 1.f, 0.f)
    );
    if (!trailing.empty()) {
      rowChildren.push_back(
          VStack {
              .spacing = theme().space1,
              .alignment = Alignment::End,
              .children = std::move(trailing),
          }
      );
    }

    return HStack {
        .spacing = theme().space3,
        .alignment = Alignment::Start,
        .children = std::move(rowChildren),
    }
        .padding(theme().space3)
        .fill(FillStyle::solid(Color::elevatedBackground()))
        .stroke(StrokeStyle::solid(Color::separator(), 1.f))
        .shadow(ShadowStyle {.radius = theme().shadowRadiusPopover,
                             .offset = {0.f, theme().shadowOffsetYPopover},
                             .color = theme().shadowColor})
        .cornerRadius(CornerRadius {theme().radiusLarge})
        .width(std::clamp(toast.maxWidth, toast.minWidth, toast.maxWidth));
  }
};

OverlayConfig overlayConfigForToast(Toast const& toast, Window& window, std::size_t stackIndex) {
  Size const size = window.getSize();
  float const margin = 24.f;
  float const stackGap = 92.f;
  float const width = std::clamp(toast.maxWidth, toast.minWidth, toast.maxWidth);
  bool const top = toast.placement == ToastPlacement::TopLeading ||
                   toast.placement == ToastPlacement::TopCenter ||
                   toast.placement == ToastPlacement::TopTrailing;

  OverlayConfig::CrossAlignment cross = OverlayConfig::CrossAlignment::Center;
  float anchorX = 0.f;
  float anchorWidth = size.width;
  if (toast.placement == ToastPlacement::TopLeading || toast.placement == ToastPlacement::BottomLeading) {
    cross = OverlayConfig::CrossAlignment::Start;
    anchorX = margin;
    anchorWidth = width;
  } else if (toast.placement == ToastPlacement::TopTrailing || toast.placement == ToastPlacement::BottomTrailing) {
    cross = OverlayConfig::CrossAlignment::End;
    anchorX = std::max(0.f, size.width - margin - width);
    anchorWidth = width;
  }

  float const stackOffset = static_cast<float>(stackIndex) * stackGap;
  return OverlayConfig {
      .anchor = Rect {anchorX, top ? margin : std::max(0.f, size.height - margin), anchorWidth, 1.f},
      .placement = top ? OverlayConfig::Placement::Below : OverlayConfig::Placement::Above,
      .crossAlignment = cross,
      .offset = Vec2 {0.f, top ? stackOffset : -stackOffset},
      .maxSize = Size {width, 0.f},
      .modal = false,
      .backdropColor = Colors::transparent,
      .dismissOnOutsideTap = false,
      .dismissOnEscape = false,
      .debugName = "toast",
  };
}

} // namespace

Element ToastOverlay::body() const {
  auto theme = useEnvironment<ThemeKey>();
  std::vector<Element> cards;
  cards.reserve(toasts.size());
  for (Toast const& toast : toasts) {
    cards.push_back(ToastCard {.toast = toast, .dismiss = onDismiss});
  }
  return VStack {
      .spacing = theme().space2,
      .alignment = Alignment::Stretch,
      .children = std::move(cards),
  };
}

std::tuple<std::function<std::uint64_t(Toast)>, std::function<void(std::uint64_t)>, std::function<void()>, bool>
useToast() {
  Runtime* runtime = Runtime::current();
  Window* window = runtime ? &runtime->window() : nullptr;
  auto registry = std::make_shared<ToastRegistry>();

  auto dismiss = [registry, window](std::uint64_t toastId) {
    auto it = registry->overlays.find(toastId);
    if (it == registry->overlays.end() || !window) {
      return;
    }
    OverlayId overlay = it->second;
    registry->overlays.erase(it);
    registry->placements.erase(toastId);
    window->removeOverlay(overlay);
  };

  Reactive::onCleanup([registry, window] {
    if (!window) {
      return;
    }
    std::vector<OverlayId> overlays;
    overlays.reserve(registry->overlays.size());
    for (auto const& [toastId, overlay] : registry->overlays) {
      (void)toastId;
      overlays.push_back(overlay);
    }
    registry->overlays.clear();
    registry->placements.clear();
    for (OverlayId overlay : overlays) {
      window->removeOverlay(overlay);
    }
  });

  auto clear = [registry, window] {
    if (!window) {
      return;
    }
    std::vector<OverlayId> overlays;
    overlays.reserve(registry->overlays.size());
    for (auto const& [toastId, overlay] : registry->overlays) {
      (void)toastId;
      overlays.push_back(overlay);
    }
    registry->overlays.clear();
    registry->placements.clear();
    for (OverlayId overlay : overlays) {
      window->removeOverlay(overlay);
    }
  };

  auto show = [registry, window, dismiss](Toast toast) mutable -> std::uint64_t {
    if (!window) {
      return 0;
    }
    toast.id = registry->nextToastId++;
    std::size_t stackIndex = 0;
    for (auto const& [id, placement] : registry->placements) {
      (void)id;
      if (placement == toast.placement) {
        ++stackIndex;
      }
    }
    OverlayConfig config = overlayConfigForToast(toast, *window, stackIndex);
    config.onDismiss = [registry, id = toast.id, onDismiss = toast.onDismiss] {
      registry->overlays.erase(id);
      registry->placements.erase(id);
      if (onDismiss) {
        onDismiss();
      }
    };
    OverlayId overlay = window->pushOverlay(
        Element {ToastCard {.toast = toast, .dismiss = dismiss}},
        std::move(config)
    );
    registry->overlays[toast.id] = overlay;
    registry->placements[toast.id] = toast.placement;
    return toast.id;
  };

  return {std::move(show), std::move(dismiss), std::move(clear), !registry->overlays.empty()};
}

} // namespace lambdaui
