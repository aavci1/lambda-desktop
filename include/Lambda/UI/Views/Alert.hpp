#pragma once

/// \file Lambda/UI/Views/Alert.hpp
///
/// Part of the Lambda public API.


#include <Lambda/UI/Views/Button.hpp>
#include <Lambda/UI/Hooks.hpp>

#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Core/Color.hpp>
#include <Lambda/UI/Theme.hpp>

#include <functional>
#include <string>
#include <tuple>
#include <vector>

namespace lambda {

struct AlertButton {
  /// Button label text.
  std::string label{};
  /// Button visual treatment.
  ButtonVariant variant = ButtonVariant::Secondary;
  /// Disables activation when true.
  bool disabled = false;
  /// Called when this button is tapped or activated by keyboard.
  /// The alert is dismissed automatically before this is called.
  std::function<void()> action{};

  bool operator==(AlertButton const& other) const {
    return label == other.label && variant == other.variant && disabled == other.disabled &&
           static_cast<bool>(action) == static_cast<bool>(other.action);
  }
};

struct Alert : ViewModifiers<Alert> {
  // ── Content ──────────────────────────────────────────────────────────────

  /// Alert headline.
  std::string title{};
  /// Optional; empty = no message row.
  std::string message{};

  /// Up to three buttons, rendered left-to-right (last = rightmost = primary).
  /// When empty, a single "OK" Secondary button is added automatically.
  /// `useAlert` keeps only the first three if more are supplied.
  std::vector<AlertButton> buttons{};

  // ── Appearance ───────────────────────────────────────────────────────────

  /// Width of the card. 360 pt matches macOS alert width convention.
  float cardWidth = 360.f;

  /// Alert card fill color.
  Color cardColor = Color::theme();
  /// Alert card border color.
  Color cardStrokeColor = Color::theme();
  /// Title text color.
  Color titleColor = Color::theme();
  /// Message text color.
  Color messageColor = Color::theme();
  /// Full-window scrim color behind the alert.
  Color backdropColor = Color::theme();
  /// Full-window backdrop blur radius in logical pixels.
  float backdropBlurRadius = kFloatFromTheme;
  /// Uniform card corner radius (`kFloatFromTheme` = `Theme::radiusXLarge`). Not a `CornerRadius`
  /// struct field — all corners share one value; asymmetric cards need a custom element.
  float cornerRadius = kFloatFromTheme;

  // ── Behaviour ────────────────────────────────────────────────────────────

  /// When true, pressing Escape dismisses without calling any button action.
  bool dismissOnEscape = true;

  // ── Component protocol ───────────────────────────────────────────────────

  /// body() is not the primary API. Use show() / hide() / useAlert() instead.
  Element body() const;

private:
  std::vector<Element> buildContent(Color titleC, Color msgC, Theme const& theme) const;
};

/// Hook: returns (show, hide, isPresented) for presenting an Alert.
///
/// show(alert) — pushes the alert as a modal overlay.
/// hide()      — dismisses the alert.
/// isPresented — true while the alert is on screen.
///
/// Alert uses useOverlay internally; the overlay config is managed here.
/// Must be called inside body() like other hooks.
std::tuple<std::function<void(Alert)>, std::function<void()>, bool> useAlert();

} // namespace lambda
