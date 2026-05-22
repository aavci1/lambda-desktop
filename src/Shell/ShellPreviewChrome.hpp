#pragma once

#include "Shell/UI/LambdaShellTypes.hpp"

#include <Flux/Core/Color.hpp>
#include <Flux/Graphics/Styles.hpp>
#include <Flux/UI/Element.hpp>
#include <Flux/UI/Views/Views.hpp>

namespace lambda_shell {

/// Visual chrome drawn by the compositor for layer-shell surfaces in production.
/// The preview app draws these locally so shell UI can be tested without Wayland.
namespace shell_preview {

inline constexpr float kDockCornerRadius = 14.f;

inline flux::Color glassTintColor() {
  return flux::Color{238.f / 255.f, 244.f / 255.f, 1.f, 0.60f};
}

inline flux::Color glassBorderColor() {
  return flux::Color{1.f, 1.f, 1.f, 0.34f};
}

inline flux::FillStyle shellGlassFill() {
  flux::Color tint = glassTintColor();
  tint.a *= 0.84f * 0.48f;
  return flux::FillStyle::solid(tint);
}

inline flux::Element wrapTopBar(flux::Element content, float width) {
  float const height = static_cast<float>(kTopBarHeight);
  return flux::ZStack{
      .horizontalAlignment = flux::Alignment::Stretch,
      .verticalAlignment = flux::Alignment::Stretch,
      .children = flux::children(
          flux::Rectangle{}
              .size(width, height)
              .fill(shellGlassFill()),
          flux::Rectangle{}
              .size(width, 1.f)
              .position(0.f, height - 1.f)
              .fill(glassBorderColor()),
          std::move(content).size(width, height)),
  }.size(width, height);
}

inline flux::Element wrapDock(flux::Element content, float width, float height) {
  return flux::ZStack{
      .horizontalAlignment = flux::Alignment::Stretch,
      .verticalAlignment = flux::Alignment::Stretch,
      .children = flux::children(
          flux::Rectangle{}
              .size(width, height)
              .fill(shellGlassFill())
              .cornerRadius(kDockCornerRadius)
              .stroke(flux::StrokeStyle::solid(glassBorderColor(), 1.f)),
          std::move(content)),
  }.size(width, height);
}

} // namespace shell_preview
} // namespace lambda_shell
