#pragma once

/// \file Lambda/UI/Views/Toast.hpp
///
/// Part of the Lambda public API.

#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/IconName.hpp>
#include <Lambda/UI/Views/Button.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace lambda {

enum class ToastTone : std::uint8_t {
  Neutral,
  Accent,
  Success,
  Warning,
  Danger,
};

enum class ToastPlacement : std::uint8_t {
  TopLeading,
  TopCenter,
  TopTrailing,
  BottomLeading,
  BottomCenter,
  BottomTrailing,
};

struct ToastAction {
  /// Button label.
  std::string label;
  /// Button visual treatment.
  ButtonVariant variant = ButtonVariant::Ghost;
  /// Dismisses the toast before running `action` when true.
  bool dismissOnTap = true;
  /// Action callback.
  std::function<void()> action;

  bool operator==(ToastAction const& other) const {
    return label == other.label && variant == other.variant && dismissOnTap == other.dismissOnTap &&
           static_cast<bool>(action) == static_cast<bool>(other.action);
  }
};

struct Toast {
  /// Stable toast id. `useToast()` assigns one when `show()` is called.
  std::uint64_t id = 0;
  /// Primary headline.
  std::string title;
  /// Optional supporting text.
  std::string message;

  /// Semantic tone for icon / color selection.
  ToastTone tone = ToastTone::Neutral;
  /// Overlay anchor position within the window.
  ToastPlacement placement = ToastPlacement::BottomCenter;
  /// Optional leading icon override.
  std::optional<IconName> icon;
  /// Optional trailing action button.
  std::optional<ToastAction> action;

  /// Shows a close button when true.
  bool showCloseButton = true;
  /// Auto-dismiss timeout in milliseconds. `<= 0` disables auto-dismiss.
  int autoDismissMs = 4000;

  /// Minimum toast width.
  float minWidth = 280.f;
  /// Maximum toast width.
  float maxWidth = 420.f;

  /// Called when the toast is dismissed by any path.
  std::function<void()> onDismiss;

  bool operator==(Toast const& other) const {
    return id == other.id && title == other.title && message == other.message &&
           tone == other.tone && placement == other.placement && icon == other.icon &&
           action == other.action && showCloseButton == other.showCloseButton &&
           autoDismissMs == other.autoDismissMs && minWidth == other.minWidth &&
           maxWidth == other.maxWidth &&
           static_cast<bool>(onDismiss) == static_cast<bool>(other.onDismiss);
  }
};

struct ToastOverlay : ViewModifiers<ToastOverlay> {
  /// Visible toasts to render.
  std::vector<Toast> toasts;
  /// Called to dismiss a toast by id.
  std::function<void(std::uint64_t)> onDismiss;

  bool operator==(ToastOverlay const& other) const {
    return toasts == other.toasts &&
           static_cast<bool>(onDismiss) == static_cast<bool>(other.onDismiss);
  }

  Element body() const;
};

/// Hook: returns `(show, dismiss, clear, hasVisibleToasts)` for a non-modal toast overlay.
///
/// `show(toast)` appends a toast and returns the assigned id.
/// `dismiss(id)` removes one toast.
/// `clear()` removes all visible toasts.
/// `hasVisibleToasts` is true while any toast is currently shown.
std::tuple<std::function<std::uint64_t(Toast)>, std::function<void(std::uint64_t)>, std::function<void()>, bool>
useToast();

} // namespace lambda
