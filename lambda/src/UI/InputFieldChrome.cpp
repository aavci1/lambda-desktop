#include <Lambda/UI/InputFieldChrome.hpp>

#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/Theme.hpp>

namespace lambda {

ResolvedInputFieldChrome resolveInputFieldChrome(InputFieldChromeSpec const& spec, Theme const& theme) {
  return ResolvedInputFieldChrome{
      .textColor = resolveColor(spec.textColor, theme.labelColor, theme),
      .placeholderColor = resolveColor(spec.placeholderColor, theme.placeholderTextColor, theme),
      .backgroundColor = resolveColor(spec.backgroundColor, theme.textBackgroundColor, theme),
      .borderColor = resolveColor(spec.borderColor, theme.separatorColor, theme),
      .borderFocusColor = resolveColor(spec.borderFocusColor, theme.keyboardFocusIndicatorColor, theme),
      .caretColor = resolveColor(spec.caretColor, theme.accentColor, theme),
      .selectionColor = resolveColor(spec.selectionColor, theme.selectedContentBackgroundColor, theme),
      .disabledColor = resolveColor(spec.disabledColor, theme.disabledControlBackgroundColor, theme),
      .borderWidth = resolveFloat(spec.borderWidth, 1.f),
      .borderFocusWidth = resolveFloat(spec.borderFocusWidth, 2.f),
      .cornerRadius = resolveFloat(spec.cornerRadius, theme.radiusLarge),
      .paddingH = resolveFloat(spec.paddingH, theme.paddingFieldH),
      .paddingV = resolveFloat(spec.paddingV, theme.paddingFieldV),
  };
}

} // namespace lambda
