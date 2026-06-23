#include <Lambda/UI/Views/Alert.hpp>

#include <Lambda/UI/Window.hpp>
#include <Lambda/UI/Detail/Runtime.hpp>
#include <Lambda/UI/Overlay.hpp>
#include <Lambda/UI/OverlaySurfaceHelpers.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/Views/Button.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Spacer.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/VStack.hpp>

#include <algorithm>
#include <utility>

#include "UI/Views/AlertActionHelpers.hpp"

namespace lambdaui {

Element Alert::body() const {
  auto theme = useEnvironment<ThemeKey>();
  ResolvedAlertCardColors const surface =
      resolveAlertCardColors(cardColor, cardStrokeColor, cornerRadius, theme());
  Color const titleC = resolveColor(titleColor, Color::primary(), theme());
  Color const msgC = resolveColor(messageColor, Color::secondary(), theme());

  return VStack{
      .spacing = theme().space3,
      .alignment = Alignment::Start,
      .children = buildContent(titleC, msgC, theme()),
  }
      .fill(FillStyle::solid(surface.cardFill))
      .stroke(StrokeStyle::solid(surface.cardStroke, 1.f))
      .width(cardWidth)
      .cornerRadius(surface.cornerRadius)
      .padding(theme().space6);
}

std::vector<Element> Alert::buildContent(Color titleC, Color msgC, Theme const& theme) const {
  std::vector<Element> rows;
  float const contentW = std::max(0.f, cardWidth - 2.f * theme.space6);

  rows.push_back(Text{
                     .text = title,
                     .font = Font::title2(),
                     .color = titleC,
                 }
                     .width(contentW));

  if (!message.empty()) {
    rows.push_back(Text{
                       .text = message,
                       .font = Font::body(),
                       .color = msgC,
                       .wrapping = TextWrapping::Wrap,
                   }
                       .width(contentW));
  }

  if (buttons.size() == 1) {
    AlertButton const& button = buttons.front();
    rows.push_back(HStack{
        .spacing = theme.space2,
        .children = children(
            Spacer{},
            Button{
                .label = button.label,
                .variant = button.variant,
                .disabled = button.disabled,
                .onTap = button.action,
            }),
    });
  } else {
    std::vector<Element> buttonElements;
    buttonElements.reserve(buttons.size());
    for (AlertButton const& button : buttons) {
      buttonElements.push_back(Button{
                                   .label = button.label,
                                   .variant = button.variant,
                                   .disabled = button.disabled,
                                   .onTap = button.action,
                               }
                                   .flex(1.f));
    }
    rows.push_back(HStack{
        .spacing = theme.space2,
        .children = std::move(buttonElements),
    });
  }

  return rows;
}

std::tuple<std::function<void(Alert)>, std::function<void()>, bool> useAlert() {
  auto [showOverlay, hideOverlay, isPresented] = useOverlay();
  Runtime* runtime = Runtime::current();
  Window* window = runtime ? &runtime->window() : nullptr;

  auto show = [showOverlay = std::move(showOverlay), hideOverlay, window](Alert alert) mutable {
    if (alert.buttons.empty()) {
      alert.buttons.push_back(AlertButton{
          .label = "OK",
          .variant = ButtonVariant::Secondary,
      });
    }
    if (alert.buttons.size() > 3) {
      alert.buttons.resize(3);
    }
    for (AlertButton& button : alert.buttons) {
      button.action = detail::wrapDismissThenInvoke(hideOverlay, std::move(button.action));
    }

    Theme const theme = window ? window->theme() : Theme::light();
    Color const backdrop = resolveAlertBackdropColor(alert.backdropColor, theme);
    bool const dismissEsc = alert.dismissOnEscape;

    showOverlay(
        Element{std::move(alert)},
        OverlayConfig{
            .modal = true,
            .backdropColor = backdrop,
            .backdropBlurRadius = resolveFloat(alert.backdropBlurRadius, theme.modalBackdropBlurRadius),
            .dismissOnOutsideTap = false,
            .dismissOnEscape = dismissEsc,
            .onDismiss = hideOverlay,
            .debugName = "alert",
        });
  };

  return {std::move(show), std::move(hideOverlay), isPresented};
}

} // namespace lambdaui
