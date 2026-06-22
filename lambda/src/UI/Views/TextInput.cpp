#include <Lambda/UI/Views/TextInput.hpp>

#include <Lambda/UI/Application.hpp>
#include <Lambda/UI/Detail/Runtime.hpp>
#include <Lambda/UI/KeyCodes.hpp>
#include <Lambda/Graphics/TextSystem.hpp>
#include <Lambda/Reactive/Animation.hpp>
#include <Lambda/Reactive/Effect.hpp>
#include <Lambda/UI/InteractionData.hpp>
#include <Lambda/SceneGraph/RectNode.hpp>
#include <Lambda/SceneGraph/RenderNode.hpp>
#include <Lambda/SceneGraph/TextNode.hpp>
#include <Lambda/UI/InputFieldChrome.hpp>
#include <Lambda/UI/InputFieldLayout.hpp>
#include <Lambda/UI/MeasureContext.hpp>
#include <Lambda/UI/MountContext.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/Views/TextEditUtils.hpp>
#include <Lambda/UI/Views/Text.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <utility>

namespace lambda {

namespace {

struct ResolvedTextInputStyle {
  Font font{};
  Color textColor{};
  Color placeholderColor{};
  Color caretColor{};
  Color selectionColor{};
  ResolvedInputFieldChrome chrome{};
  float lineHeight = 0.f;
  float height = 0.f;

  bool operator==(ResolvedTextInputStyle const& other) const = default;
};

ResolvedTextInputStyle resolveTextInputStyle(TextInput::Style const& style,
                                             Theme const& theme) {
  InputFieldChromeSpec chromeSpec{};
  chromeSpec.backgroundColor = style.backgroundColor;
  chromeSpec.borderColor = style.borderColor;
  chromeSpec.borderFocusColor = style.borderFocusColor;
  chromeSpec.disabledColor = style.disabledColor;
  chromeSpec.cornerRadius = style.cornerRadius;
  chromeSpec.borderWidth = style.borderWidth;
  chromeSpec.borderFocusWidth = style.borderFocusWidth;
  chromeSpec.paddingH = style.paddingH;
  chromeSpec.paddingV = style.paddingV;

  ResolvedInputFieldChrome chrome = resolveInputFieldChrome(chromeSpec, theme);
  return ResolvedTextInputStyle{
      .font = resolveFont(style.font.evaluate(), theme.bodyFont, theme),
      .textColor = resolveColor(style.textColor, theme.labelColor, theme),
      .placeholderColor = resolveColor(style.placeholderColor, theme.placeholderTextColor, theme),
      .caretColor = resolveColor(style.caretColor, theme.accentColor, theme),
      .selectionColor = resolveColor(style.selectionColor, theme.accentColor, theme),
      .chrome = chrome,
      .lineHeight = std::max(0.f, style.lineHeight.evaluate()),
      .height = std::max(0.f, style.height),
  };
}

TextLayoutOptions textInputLayoutOptions(TextInput const& input,
                                         ResolvedTextInputStyle const& style) {
  TextLayoutOptions options{};
  options.wrapping = input.multiline ? input.wrapping : TextWrapping::NoWrap;
  options.horizontalAlignment = HorizontalAlignment::Leading;
  options.verticalAlignment = input.multiline ? VerticalAlignment::Top : VerticalAlignment::Center;
  options.lineHeight = style.lineHeight;
  return options;
}

bool sameLayoutScalar(float a, float b) noexcept {
  if (a == b) {
    return true;
  }
  if (!std::isfinite(a) || !std::isfinite(b)) {
    return false;
  }
  return std::fabs(a - b) <= 0.01f;
}

bool sameTextInputLayoutBox(bool multiline, Rect const& a, Rect const& b) noexcept {
  if (!sameLayoutScalar(a.x, b.x) || !sameLayoutScalar(a.y, b.y)) {
    return false;
  }
  if (!multiline) {
    return true;
  }
  return sameLayoutScalar(a.width, b.width);
}

AttributedString attributedText(TextInput const& input,
                                ResolvedTextInputStyle const& style) {
  std::string const text = input.value.get();
  if (text.empty()) {
    return AttributedString::plain(input.placeholder, style.font, style.placeholderColor);
  }

  AttributedString attributed;
  attributed.utf8 = text;
  if (input.styler) {
    attributed.runs = input.styler(attributed.utf8);
  }
  auto runsCoverText = [](std::vector<AttributedRun> const& runs, std::uint32_t length) {
    if (length == 0) {
      return runs.empty();
    }
    if (runs.empty()) {
      return false;
    }
    std::vector<std::pair<std::uint32_t, std::uint32_t>> spans;
    spans.reserve(runs.size());
    for (AttributedRun const& run : runs) {
      if (run.start >= run.end || run.end > length) {
        return false;
      }
      spans.push_back({run.start, run.end});
    }
    std::sort(spans.begin(), spans.end());
    std::uint32_t covered = 0;
    for (auto const& [start, end] : spans) {
      if (start > covered) {
        return false;
      }
      covered = std::max(covered, end);
    }
    return covered >= length;
  };
  if (!runsCoverText(attributed.runs, static_cast<std::uint32_t>(attributed.utf8.size()))) {
    attributed.runs.clear();
    Color const textColor = input.validationColor ? input.validationColor(attributed.utf8) : style.textColor;
    attributed.runs.push_back(AttributedRun{
        .start = 0,
        .end = static_cast<std::uint32_t>(attributed.utf8.size()),
        .font = style.font,
        .color = textColor,
        .backgroundColor = std::nullopt,
    });
  }
  return attributed;
}

Size textInputFrameSize(TextInput const& input, ResolvedTextInputStyle const& style,
                        LayoutConstraints const& constraints, TextSystem& textSystem) {
  float const borderInset = std::max(style.chrome.borderWidth, style.chrome.borderFocusWidth);
  float const padX = style.chrome.paddingH + borderInset;
  float const padY = style.chrome.paddingV + borderInset;
  float maxTextWidth = std::isfinite(constraints.maxWidth)
                           ? std::max(0.f, constraints.maxWidth - 2.f * padX)
                           : 0.f;
  if (!input.multiline) {
    maxTextWidth = 0.f;
  }

  bool const assignedSingleLineWidth =
      !input.multiline && std::isfinite(constraints.maxWidth) && constraints.maxWidth > 0.f;
  Size measured{};
  if (assignedSingleLineWidth) {
    measured.width = constraints.maxWidth;
  } else {
    TextLayoutOptions const options = textInputLayoutOptions(input, style);
    measured = textSystem.measure(attributedText(input, style), maxTextWidth, options);
    measured.width += 2.f * padX;
    measured.height += 2.f * padY;
  }

  if (input.multiline) {
    if (input.multilineHeight.fixed > 0.f) {
      measured.height = input.multilineHeight.fixed;
    } else {
      measured.height = std::max(measured.height, input.multilineHeight.minIntrinsic);
      if (input.multilineHeight.maxIntrinsic > 0.f) {
        measured.height = std::min(measured.height, input.multilineHeight.maxIntrinsic);
      }
    }
  } else {
    measured.height = resolvedInputFieldHeight(style.font, style.textColor,
                                               style.chrome.paddingV + borderInset,
                                               style.height);
  }

  if (std::isfinite(constraints.maxWidth) && constraints.maxWidth > 0.f) {
    measured.width = constraints.maxWidth;
  }
  if (std::isfinite(constraints.maxHeight)) {
    measured.height = std::min(measured.height, constraints.maxHeight);
  }
  measured.width = std::max(measured.width, constraints.minWidth);
  measured.height = std::max(measured.height, constraints.minHeight);
  return measured;
}

Rect textBox(Size frameSize, ResolvedTextInputStyle const& style) {
  float const borderInset = std::max(style.chrome.borderWidth, style.chrome.borderFocusWidth);
  float const padX = style.chrome.paddingH + borderInset;
  float const padY = style.chrome.paddingV + borderInset;
  return Rect{padX, padY, std::max(0.f, frameSize.width - 2.f * padX),
              std::max(0.f, frameSize.height - 2.f * padY)};
}

void setTextLayout(scenegraph::TextNode& node, TextInput const& input,
                   ResolvedTextInputStyle const& style, TextSystem& textSystem,
                   Size frameSize,
                   std::shared_ptr<detail::TextEditLayoutResult> const& layoutResult = {}) {
  Rect const box = textBox(frameSize, style);
  Rect const layoutBox = Rect::sharp(0.f, 0.f, box.width, box.height);
  node.setBounds(box);
  auto layout = textSystem.layout(attributedText(input, style), layoutBox,
                                  textInputLayoutOptions(input, style));
  node.setLayout(layout);
  if (layoutResult) {
    *layoutResult = detail::makeTextEditLayoutResult(
        layout, static_cast<int>(input.value.get().size()), box.width);
  }
}

void setTextLayoutIfNeeded(scenegraph::TextNode& node,
                           TextInput const& input,
                           ResolvedTextInputStyle const& style,
                           TextSystem& textSystem,
                           Size frameSize,
                           std::shared_ptr<detail::TextEditLayoutResult> const& layoutResult,
                           std::shared_ptr<std::string> const& lastLayoutText,
                           std::shared_ptr<Rect> const& lastLayoutBox) {
  Rect const box = textBox(frameSize, style);
  node.setBounds(box);
  std::string const currentText = input.value.get();
  if (lastLayoutText && lastLayoutBox &&
      currentText == *lastLayoutText &&
      sameTextInputLayoutBox(input.multiline, box, *lastLayoutBox)) {
    if (layoutResult) {
      layoutResult->contentWidth = std::max(0.f, box.width);
    }
    return;
  }
  setTextLayout(node, input, style, textSystem, frameSize, layoutResult);
  if (lastLayoutText) {
    *lastLayoutText = currentText;
  }
  if (lastLayoutBox) {
    *lastLayoutBox = box;
  }
}

bool sameSelection(detail::TextEditSelection lhs, detail::TextEditSelection rhs) noexcept {
  return lhs.caretByte == rhs.caretByte && lhs.anchorByte == rhs.anchorByte;
}

void relayoutStoredSelfAndAncestors(scenegraph::SceneNode& node) {
  scenegraph::SceneNode* current = &node;
  for (int depth = 0; depth < 64 && current; ++depth) {
    // Scroll containers need a fresh intrinsic content measurement even when their
    // own viewport size stays unchanged.
    current->invalidateSubtreeLayout();
    (void)current->relayoutStoredConstraints();
    current = current->parent();
  }
}

Color selectionFill(Color color) noexcept {
  color.a = std::min(color.a, 0.28f);
  return color;
}

Point textLayoutPoint(Point localPoint, Size frameSize, ResolvedTextInputStyle const& style) {
  Rect const box = textBox(frameSize, style);
  return Point{
      std::clamp(localPoint.x - box.x, 0.f, std::max(0.f, box.width)),
      std::clamp(localPoint.y - box.y, 0.f, std::max(0.f, box.height)),
  };
}

void drawSelection(Canvas& canvas, detail::TextEditLayoutResult const& layoutResult,
                   detail::TextEditSelection selection, std::string const& text,
                   Size frameSize, ResolvedTextInputStyle const& style) {
  if (!selection.hasSelection() || layoutResult.empty()) {
    return;
  }
  Rect const box = textBox(frameSize, style);
  Color const fill = selectionFill(style.selectionColor);
  for (Rect rect : detail::selectionRects(layoutResult, selection, &text, box.x, box.y, 2.f)) {
    if (rect.width <= 0.f || rect.height <= 0.f) {
      continue;
    }
    canvas.drawRect(rect, CornerRadius{2.f}, FillStyle::solid(fill), StrokeStyle::none());
  }
}

void drawCaret(Canvas& canvas, detail::TextEditLayoutResult const& layoutResult,
               detail::TextEditSelection selection, Size frameSize,
               ResolvedTextInputStyle const& style) {
  if (selection.hasSelection() || layoutResult.empty()) {
    return;
  }
  Rect const box = textBox(frameSize, style);
  Rect caret = detail::caretRect(layoutResult, selection.caretByte, box.x, box.y,
                                 detail::kTextCaretStrokeWidthPx);
  caret.y = std::max(box.y, caret.y);
  caret.height = std::min(caret.height, std::max(0.f, box.y + box.height - caret.y));
  if (caret.width <= 0.f || caret.height <= 0.f) {
    return;
  }
  canvas.drawRect(caret, CornerRadius{1.f}, FillStyle::solid(style.caretColor),
                  StrokeStyle::none());
}

void applyTextMutation(Signal<std::string> const& valueState,
                       Signal<detail::TextEditSelection> const& selectionState,
                       detail::TextEditMutation const& mutation,
                       std::function<void(std::string const&)> const& onChangeHandler,
                       std::function<void(std::string const&, detail::TextEditSelection)> const& onEditHandler) {
  if (mutation.valueChanged) {
    valueState = mutation.text;
  }
  if (!sameSelection(selectionState.peek(), mutation.selection)) {
    selectionState = mutation.selection;
  }
  if (mutation.valueChanged) {
    if (onChangeHandler) {
      onChangeHandler(mutation.text);
    }
    if (onEditHandler) {
      onEditHandler(mutation.text, mutation.selection);
    }
  }
}

std::string selectedInputText(std::string const& text,
                              detail::TextEditSelection selection) {
  auto const [start, end] = detail::clampSelection(text, selection).ordered();
  if (start >= end) {
    return {};
  }
  return text.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));
}

} // namespace

Size TextInput::measure(MeasureContext& ctx, LayoutConstraints const& constraints,
                        LayoutHints const&, TextSystem& textSystem) const {
  ctx.advanceChildSlot();
  auto theme = useEnvironment<ThemeKey>();
  return textInputFrameSize(*this, resolveTextInputStyle(style, theme()), constraints, textSystem);
}

std::unique_ptr<scenegraph::SceneNode> TextInput::mount(MountContext& ctx) const {
  Theme const theme = ctx.environmentBinding().value<ThemeKey>();
  ResolvedTextInputStyle const resolved = resolveTextInputStyle(style, theme);
  auto resolvedStyle = std::make_shared<ResolvedTextInputStyle>(resolved);
  auto frameSize = std::make_shared<Size>(
      textInputFrameSize(*this, resolved, ctx.constraints(), ctx.textSystem()));

  auto wrapper = std::make_unique<scenegraph::RectNode>(
      Rect{0.f, 0.f, frameSize->width, frameSize->height},
      FillStyle::solid(disabled ? resolved.chrome.disabledColor
                                : resolved.chrome.backgroundColor),
      StrokeStyle::solid(resolved.chrome.borderColor, resolved.chrome.borderWidth),
      CornerRadius{resolved.chrome.cornerRadius});

  auto textNode = std::make_unique<scenegraph::TextNode>();
  scenegraph::TextNode* rawText = textNode.get();
  Signal<std::string> valueState = value;
  Signal<detail::TextEditSelection> selectionState = selection.value_or(
      Signal<detail::TextEditSelection>{detail::TextEditSelection{
          .caretByte = static_cast<int>(value.get().size()),
          .anchorByte = static_cast<int>(value.get().size()),
      }});
  auto layoutResult = std::make_shared<detail::TextEditLayoutResult>();
  setTextLayout(*rawText, *this, resolved, ctx.textSystem(), *frameSize, layoutResult);
  auto lastLayoutText = std::make_shared<std::string>(valueState.peek());
  auto lastLayoutBox = std::make_shared<Rect>(textBox(*frameSize, resolved));

  auto interaction = std::make_unique<InteractionData>();
  if (ComponentKey const* scopeKey = detail::currentInteractionScopeKey()) {
    ComponentKey targetKey = *scopeKey;
    for (LocalId const id : ctx.measureContext().currentElementKey().materialize()) {
      targetKey.push_back(id);
    }
    interaction->stableTargetKey_ = std::move(targetKey);
  } else {
    interaction->stableTargetKey_ = ctx.measureContext().currentElementKey();
  }
  interaction->cursor = disabled ? Cursor::Inherit : Cursor::IBeam;
  interaction->focusable_ = !disabled;
  Signal<bool> focusState = interaction->focusSignal;
  Animated<float> caretOpacity{1.f};
  bool const hasControlledSelection = selection.has_value();

  auto selectionLayer = std::make_unique<scenegraph::RenderNode>(
      Rect{0.f, 0.f, frameSize->width, frameSize->height},
      [layoutResult, valueState, selectionState, focusState, frameSize, resolvedStyle,
       hasControlledSelection](Canvas& canvas, Rect) {
        if (!focusState.peek() && !hasControlledSelection) {
          return;
        }
        drawSelection(canvas, *layoutResult, selectionState.peek(), valueState.peek(),
                      *frameSize, *resolvedStyle);
      });
  selectionLayer->setPurity(scenegraph::RenderNode::Purity::Live);
  scenegraph::RenderNode* rawSelectionLayer = selectionLayer.get();
  wrapper->appendChild(std::move(selectionLayer));
  wrapper->appendChild(std::move(textNode));

  auto caretLayer = std::make_unique<scenegraph::RenderNode>(
      Rect{0.f, 0.f, frameSize->width, frameSize->height},
      [layoutResult, selectionState, focusState, frameSize, resolvedStyle, caretOpacity,
       hasControlledSelection](
          Canvas& canvas, Rect) {
        if ((!focusState.peek() && !hasControlledSelection) || caretOpacity.peek() < 0.5f) {
          return;
        }
        drawCaret(canvas, *layoutResult, selectionState.peek(), *frameSize, *resolvedStyle);
      });
  caretLayer->setPurity(scenegraph::RenderNode::Purity::Live);
  scenegraph::RenderNode* rawCaretLayer = caretLayer.get();
  wrapper->appendChild(std::move(caretLayer));

  int const lengthLimit = maxLength;
  bool const acceptsMultiline = multiline;
  auto onChangeHandler = onChange;
  auto onEditHandler = onEdit;
  auto onSubmitHandler = onSubmit;
  auto onEscapeHandler = onEscape;
  auto onPreviewKeyDownHandler = onPreviewKeyDown;
  auto onPreviewCommandHandler = onPreviewCommand;
  bool const isDisabled = disabled;
  auto draggingSelection = std::make_shared<bool>(false);
  if (Runtime* runtime = Runtime::current()) {
    auto registerTextCommand = [runtime, &ctx, targetKey = interaction->stableTargetKey_,
                                onPreviewCommandHandler](
                                   std::string commandId,
                                   std::function<void()> handler,
                                   std::function<bool()> isEnabled = {}) {
      std::string const registeredCommandId = commandId;
      auto commandHandler = [registeredCommandId, onPreviewCommandHandler,
                             handler = std::move(handler)] {
        if (onPreviewCommandHandler && onPreviewCommandHandler(registeredCommandId)) {
          return;
        }
        handler();
      };
      CommandId const id = runtime->commandRegistry().registerViewHandler(
          targetKey, commandId, std::move(commandHandler), std::move(isEnabled));
      ctx.owner().onCleanup([runtime, id] {
        runtime->commandRegistry().unregister(id);
      });
    };
    auto setSelection = [valueState, selectionState](detail::TextEditSelection next) {
      detail::TextEditSelection const clamped = detail::clampSelection(valueState.peek(), next);
      if (!sameSelection(selectionState.peek(), clamped)) {
        selectionState = clamped;
      }
    };
    auto applyMutation = [valueState, selectionState, onChangeHandler, onEditHandler](
                             detail::TextEditMutation mutation) {
      applyTextMutation(valueState, selectionState, mutation, onChangeHandler, onEditHandler);
    };
    auto moveHorizontal = [valueState, selectionState, setSelection](int direction,
                                                                    bool byWord,
                                                                    bool extendSelection) {
      std::string const& text = valueState.peek();
      detail::TextEditSelection const current = selectionState.peek();
      setSelection(byWord ? detail::moveSelectionByWord(text, current, direction, extendSelection)
                          : detail::moveSelectionByChar(text, current, direction, extendSelection));
    };
    auto moveVertical = [valueState, selectionState, layoutResult, acceptsMultiline, setSelection](
                            int direction,
                            bool extendSelection) {
      std::string const& text = valueState.peek();
      detail::TextEditSelection const current = selectionState.peek();
      int const byte = acceptsMultiline && layoutResult && !layoutResult->empty()
                           ? detail::moveCaretVertically(*layoutResult, text, current.caretByte, direction)
                           : (direction < 0 ? 0 : static_cast<int>(text.size()));
      setSelection(detail::moveSelectionToByte(text, current, byte, extendSelection));
    };
    auto moveLineBoundary = [valueState, selectionState, setSelection](bool end,
                                                                      bool extendSelection) {
      std::string const& text = valueState.peek();
      setSelection(detail::moveSelectionToLineBoundary(text, selectionState.peek(), end, extendSelection));
    };
    auto moveDocumentBoundary = [valueState, selectionState, setSelection](bool end,
                                                                          bool extendSelection) {
      std::string const& text = valueState.peek();
      setSelection(detail::moveSelectionToDocumentBoundary(text, selectionState.peek(), end, extendSelection));
    };
    auto pasteTextCommand = [valueState, selectionState, lengthLimit, onChangeHandler,
                             onEditHandler, isDisabled] {
      if (isDisabled || !Application::hasInstance()) {
        return;
      }
      std::optional<std::string> clipboard = Application::instance().clipboard().readText();
      if (!clipboard || clipboard->empty()) {
        return;
      }
      applyTextMutation(valueState, selectionState,
                        detail::insertText(valueState.peek(), selectionState.peek(),
                                           *clipboard, lengthLimit),
                        onChangeHandler, onEditHandler);
    };
    auto clipboardHasText = [isDisabled] {
      return !isDisabled && Application::hasInstance() &&
             Application::instance().clipboard().hasText();
    };
    registerTextCommand("edit.selectAll",
                        [valueState, selectionState, isDisabled] {
                          if (!isDisabled) {
                            selectionState = detail::selectAllSelection(valueState.peek());
                          }
                        },
                        [isDisabled] {
                          return !isDisabled;
                        });
    registerTextCommand("edit.copy",
                        [valueState, selectionState] {
                          std::string selected = selectedInputText(valueState.peek(), selectionState.peek());
                          if (!selected.empty() && Application::hasInstance()) {
                            Application::instance().clipboard().writeText(std::move(selected));
                          }
                        },
                        [valueState, selectionState, isDisabled] {
                          return !isDisabled && !selectedInputText(valueState.peek(), selectionState.peek()).empty();
                        });
    registerTextCommand("edit.cut",
                        [valueState, selectionState, onChangeHandler, onEditHandler, isDisabled] {
                          if (isDisabled) {
                            return;
                          }
                          std::string selected = selectedInputText(valueState.peek(), selectionState.peek());
                          if (selected.empty()) {
                            return;
                          }
                          if (Application::hasInstance()) {
                            Application::instance().clipboard().writeText(std::move(selected));
                          }
                          applyTextMutation(valueState, selectionState,
                                            detail::insertText(valueState.peek(), selectionState.peek(), ""),
                                            onChangeHandler, onEditHandler);
                        },
                        [valueState, selectionState, isDisabled] {
                          return !isDisabled && !selectedInputText(valueState.peek(), selectionState.peek()).empty();
                        });
    registerTextCommand("edit.paste", pasteTextCommand, clipboardHasText);
    registerTextCommand("edit.pastePlainText", pasteTextCommand, clipboardHasText);
    registerTextCommand("edit.deleteBackward",
                        [valueState, selectionState, applyMutation, isDisabled] {
                          if (!isDisabled) {
                            applyMutation(detail::eraseSelectionOrChar(valueState.peek(), selectionState.peek(), false));
                          }
                        },
                        [valueState, selectionState, isDisabled] {
                          detail::TextEditSelection const selection =
                              detail::clampSelection(valueState.peek(), selectionState.peek());
                          return !isDisabled && (selection.hasSelection() || selection.caretByte > 0);
                        });
    registerTextCommand("edit.deleteForward",
                        [valueState, selectionState, applyMutation, isDisabled] {
                          if (!isDisabled) {
                            applyMutation(detail::eraseSelectionOrChar(valueState.peek(), selectionState.peek(), true));
                          }
                        },
                        [valueState, selectionState, isDisabled] {
                          detail::TextEditSelection const selection =
                              detail::clampSelection(valueState.peek(), selectionState.peek());
                          return !isDisabled && (selection.hasSelection() ||
                                                 selection.caretByte < static_cast<int>(valueState.peek().size()));
                        });
    registerTextCommand("edit.deleteWordBackward",
                        [valueState, selectionState, applyMutation, isDisabled] {
                          if (!isDisabled) {
                            applyMutation(detail::eraseWord(valueState.peek(), selectionState.peek(), false));
                          }
                        },
                        [valueState, selectionState, isDisabled] {
                          detail::TextEditSelection const selection =
                              detail::clampSelection(valueState.peek(), selectionState.peek());
                          return !isDisabled && (selection.hasSelection() || selection.caretByte > 0);
                        });
    registerTextCommand("edit.deleteWordForward",
                        [valueState, selectionState, applyMutation, isDisabled] {
                          if (!isDisabled) {
                            applyMutation(detail::eraseWord(valueState.peek(), selectionState.peek(), true));
                          }
                        },
                        [valueState, selectionState, isDisabled] {
                          detail::TextEditSelection const selection =
                              detail::clampSelection(valueState.peek(), selectionState.peek());
                          return !isDisabled && (selection.hasSelection() ||
                                                 selection.caretByte < static_cast<int>(valueState.peek().size()));
                        });
    registerTextCommand("edit.selectLine",
                        [valueState, selectionState, isDisabled] {
                          if (!isDisabled) {
                            selectionState = detail::selectCurrentLine(valueState.peek(), selectionState.peek());
                          }
                        },
                        [valueState, isDisabled] {
                          return !isDisabled && !valueState.peek().empty();
                        });
    registerTextCommand("edit.deleteLine",
                        [valueState, selectionState, applyMutation, isDisabled] {
                          if (!isDisabled) {
                            applyMutation(detail::eraseCurrentLine(valueState.peek(), selectionState.peek()));
                          }
                        },
                        [valueState, isDisabled] {
                          return !isDisabled && !valueState.peek().empty();
                        });
    registerTextCommand("edit.insertLineAbove",
                        [valueState, selectionState, lengthLimit, applyMutation, acceptsMultiline, isDisabled] {
                          if (!isDisabled && acceptsMultiline) {
                            applyMutation(detail::insertLineAdjacent(valueState.peek(), selectionState.peek(),
                                                                     true, lengthLimit));
                          }
                        },
                        [acceptsMultiline, isDisabled] {
                          return !isDisabled && acceptsMultiline;
                        });
    registerTextCommand("edit.insertLineBelow",
                        [valueState, selectionState, lengthLimit, applyMutation, acceptsMultiline, isDisabled] {
                          if (!isDisabled && acceptsMultiline) {
                            applyMutation(detail::insertLineAdjacent(valueState.peek(), selectionState.peek(),
                                                                     false, lengthLimit));
                          }
                        },
                        [acceptsMultiline, isDisabled] {
                          return !isDisabled && acceptsMultiline;
                        });
    registerTextCommand("edit.moveLineUp",
                        [valueState, selectionState, applyMutation, acceptsMultiline, isDisabled] {
                          if (!isDisabled && acceptsMultiline) {
                            applyMutation(detail::moveCurrentLine(valueState.peek(), selectionState.peek(), -1));
                          }
                        },
                        [valueState, selectionState, acceptsMultiline, isDisabled] {
                          detail::TextEditSelection const selection =
                              detail::clampSelection(valueState.peek(), selectionState.peek());
                          return !isDisabled && acceptsMultiline && selection.caretByte > 0;
                        });
    registerTextCommand("edit.moveLineDown",
                        [valueState, selectionState, applyMutation, acceptsMultiline, isDisabled] {
                          if (!isDisabled && acceptsMultiline) {
                            applyMutation(detail::moveCurrentLine(valueState.peek(), selectionState.peek(), 1));
                          }
                        },
                        [valueState, selectionState, acceptsMultiline, isDisabled] {
                          detail::TextEditSelection const selection =
                              detail::clampSelection(valueState.peek(), selectionState.peek());
                          return !isDisabled && acceptsMultiline &&
                                 selection.caretByte < static_cast<int>(valueState.peek().size());
                        });
    registerTextCommand("edit.copyLineUp",
                        [valueState, selectionState, lengthLimit, applyMutation, acceptsMultiline, isDisabled] {
                          if (!isDisabled && acceptsMultiline) {
                            applyMutation(detail::copyCurrentLine(valueState.peek(), selectionState.peek(),
                                                                  -1, lengthLimit));
                          }
                        },
                        [valueState, acceptsMultiline, isDisabled] {
                          return !isDisabled && acceptsMultiline && !valueState.peek().empty();
                        });
    registerTextCommand("edit.copyLineDown",
                        [valueState, selectionState, lengthLimit, applyMutation, acceptsMultiline, isDisabled] {
                          if (!isDisabled && acceptsMultiline) {
                            applyMutation(detail::copyCurrentLine(valueState.peek(), selectionState.peek(),
                                                                  1, lengthLimit));
                          }
                        },
                        [valueState, acceptsMultiline, isDisabled] {
                          return !isDisabled && acceptsMultiline && !valueState.peek().empty();
                        });
    registerTextCommand("cursor.left",
                        [moveHorizontal, isDisabled] { if (!isDisabled) moveHorizontal(-1, false, false); },
                        [isDisabled] { return !isDisabled; });
    registerTextCommand("cursor.right",
                        [moveHorizontal, isDisabled] { if (!isDisabled) moveHorizontal(1, false, false); },
                        [isDisabled] { return !isDisabled; });
    registerTextCommand("cursor.wordLeft",
                        [moveHorizontal, isDisabled] { if (!isDisabled) moveHorizontal(-1, true, false); },
                        [isDisabled] { return !isDisabled; });
    registerTextCommand("cursor.wordRight",
                        [moveHorizontal, isDisabled] { if (!isDisabled) moveHorizontal(1, true, false); },
                        [isDisabled] { return !isDisabled; });
    registerTextCommand("cursor.up",
                        [moveVertical, isDisabled] { if (!isDisabled) moveVertical(-1, false); },
                        [isDisabled] { return !isDisabled; });
    registerTextCommand("cursor.down",
                        [moveVertical, isDisabled] { if (!isDisabled) moveVertical(1, false); },
                        [isDisabled] { return !isDisabled; });
    registerTextCommand("cursor.lineStart",
                        [moveLineBoundary, isDisabled] { if (!isDisabled) moveLineBoundary(false, false); },
                        [isDisabled] { return !isDisabled; });
    registerTextCommand("cursor.lineEnd",
                        [moveLineBoundary, isDisabled] { if (!isDisabled) moveLineBoundary(true, false); },
                        [isDisabled] { return !isDisabled; });
    registerTextCommand("cursor.documentStart",
                        [moveDocumentBoundary, isDisabled] { if (!isDisabled) moveDocumentBoundary(false, false); },
                        [isDisabled] { return !isDisabled; });
    registerTextCommand("cursor.documentEnd",
                        [moveDocumentBoundary, isDisabled] { if (!isDisabled) moveDocumentBoundary(true, false); },
                        [isDisabled] { return !isDisabled; });
    registerTextCommand("selection.left",
                        [moveHorizontal, isDisabled] { if (!isDisabled) moveHorizontal(-1, false, true); },
                        [isDisabled] { return !isDisabled; });
    registerTextCommand("selection.right",
                        [moveHorizontal, isDisabled] { if (!isDisabled) moveHorizontal(1, false, true); },
                        [isDisabled] { return !isDisabled; });
    registerTextCommand("selection.wordLeft",
                        [moveHorizontal, isDisabled] { if (!isDisabled) moveHorizontal(-1, true, true); },
                        [isDisabled] { return !isDisabled; });
    registerTextCommand("selection.wordRight",
                        [moveHorizontal, isDisabled] { if (!isDisabled) moveHorizontal(1, true, true); },
                        [isDisabled] { return !isDisabled; });
    registerTextCommand("selection.up",
                        [moveVertical, isDisabled] { if (!isDisabled) moveVertical(-1, true); },
                        [isDisabled] { return !isDisabled; });
    registerTextCommand("selection.down",
                        [moveVertical, isDisabled] { if (!isDisabled) moveVertical(1, true); },
                        [isDisabled] { return !isDisabled; });
    registerTextCommand("selection.lineStart",
                        [moveLineBoundary, isDisabled] { if (!isDisabled) moveLineBoundary(false, true); },
                        [isDisabled] { return !isDisabled; });
    registerTextCommand("selection.lineEnd",
                        [moveLineBoundary, isDisabled] { if (!isDisabled) moveLineBoundary(true, true); },
                        [isDisabled] { return !isDisabled; });
    registerTextCommand("selection.documentStart",
                        [moveDocumentBoundary, isDisabled] { if (!isDisabled) moveDocumentBoundary(false, true); },
                        [isDisabled] { return !isDisabled; });
    registerTextCommand("selection.documentEnd",
                        [moveDocumentBoundary, isDisabled] { if (!isDisabled) moveDocumentBoundary(true, true); },
                        [isDisabled] { return !isDisabled; });
  }

  interaction->onPointerDown = [selectionState, valueState, layoutResult, frameSize, resolvedStyle,
                                draggingSelection, isDisabled](Point point, MouseButton button) {
    if (isDisabled || button != MouseButton::Left || !layoutResult || layoutResult->empty()) {
      return;
    }
    int const byte = detail::caretByteAtPoint(
        *layoutResult, textLayoutPoint(point, *frameSize, *resolvedStyle), valueState.peek());
    selectionState = detail::moveSelectionToByte(valueState.peek(), selectionState.peek(), byte, false);
    *draggingSelection = true;
  };
  interaction->onPointerMove = [selectionState, valueState, layoutResult, frameSize, resolvedStyle,
                                draggingSelection, isDisabled](Point point) {
    if (isDisabled || !*draggingSelection || !layoutResult || layoutResult->empty()) {
      return;
    }
    int const byte = detail::caretByteAtPoint(
        *layoutResult, textLayoutPoint(point, *frameSize, *resolvedStyle), valueState.peek());
    selectionState = detail::moveSelectionToByte(valueState.peek(), selectionState.peek(), byte, true);
  };
  interaction->onPointerUp = [draggingSelection](Point, MouseButton) {
    *draggingSelection = false;
  };
  interaction->onTextInput = [valueState, selectionState, lengthLimit, onChangeHandler,
                              onEditHandler, isDisabled](std::string const& text) {
    if (isDisabled || text.empty()) {
      return;
    }
    applyTextMutation(valueState, selectionState,
                      detail::insertText(valueState.peek(), selectionState.peek(), text, lengthLimit),
                      onChangeHandler, onEditHandler);
  };
  interaction->onKeyDown = [valueState, selectionState, layoutResult, acceptsMultiline, lengthLimit,
                            onChangeHandler, onEditHandler, onSubmitHandler, onEscapeHandler,
                            onPreviewKeyDownHandler,
                            isDisabled](KeyCode key, Modifiers modifiers) {
    if (isDisabled) {
      return;
    }
    if (onPreviewKeyDownHandler && onPreviewKeyDownHandler(key, modifiers)) {
      return;
    }
    bool const shift = any(modifiers & Modifiers::Shift);
    bool const alt = any(modifiers & Modifiers::Alt);
    bool const meta = any(modifiers & Modifiers::Meta);
    bool const ctrl = any(modifiers & Modifiers::Ctrl);
    detail::TextEditSelection const current = selectionState.peek();
    std::string const& text = valueState.peek();

    if ((ctrl || meta) && key == keys::A) {
      selectionState = detail::selectAllSelection(text);
      return;
    }
    if (key == keys::Delete) {
      detail::TextEditMutation mutation = meta ? detail::eraseToLineBoundary(text, current, false)
                                               : alt ? detail::eraseWord(text, current, false)
                                                     : detail::eraseSelectionOrChar(text, current, false);
      applyTextMutation(valueState, selectionState, mutation, onChangeHandler, onEditHandler);
      return;
    }
    if (key == keys::ForwardDelete) {
      detail::TextEditMutation mutation = meta ? detail::eraseToLineBoundary(text, current, true)
                                               : alt ? detail::eraseWord(text, current, true)
                                                     : detail::eraseSelectionOrChar(text, current, true);
      applyTextMutation(valueState, selectionState, mutation, onChangeHandler, onEditHandler);
      return;
    }
    if (key == keys::Return) {
      if (acceptsMultiline) {
        applyTextMutation(valueState, selectionState,
                          detail::insertText(text, current, "\n", lengthLimit),
                          onChangeHandler, onEditHandler);
      } else if (onSubmitHandler) {
        onSubmitHandler(text);
      }
      return;
    }
    if (key == keys::LeftArrow || key == keys::RightArrow) {
      int const direction = key == keys::LeftArrow ? -1 : 1;
      detail::TextEditSelection next =
          meta ? detail::moveSelectionToLineBoundary(text, current, direction > 0, shift)
               : alt ? detail::moveSelectionByWord(text, current, direction, shift)
                     : detail::moveSelectionByChar(text, current, direction, shift);
      if (!sameSelection(current, next)) {
        selectionState = next;
      }
      return;
    }
    if (key == keys::UpArrow || key == keys::DownArrow) {
      int const direction = key == keys::UpArrow ? -1 : 1;
      int const byte = acceptsMultiline && layoutResult && !layoutResult->empty()
                           ? detail::moveCaretVertically(*layoutResult, text, current.caretByte, direction)
                           : (direction < 0 ? 0 : static_cast<int>(text.size()));
      selectionState = detail::moveSelectionToByte(text, current, byte, shift);
      return;
    }
    if (key == keys::Home || key == keys::End) {
      selectionState =
          detail::moveSelectionToLineBoundary(text, current, key == keys::End, shift);
      return;
    }
    if (key == keys::Escape && onEscapeHandler) {
      onEscapeHandler(text);
    }
  };
  wrapper->setInteraction(std::move(interaction));

  auto* rawWrapper = wrapper.get();
  TextSystem* textSystem = &ctx.textSystem();
  rawWrapper->setLayoutConstraints(ctx.constraints());
  rawWrapper->setRelayout([rawWrapper, rawText, rawSelectionLayer, rawCaretLayer, input = *this,
                           resolvedStyle,
                           frameSize, layoutResult, textSystem, lastLayoutText,
                           lastLayoutBox](LayoutConstraints const& constraints) mutable {
    *frameSize = textInputFrameSize(input, *resolvedStyle, constraints, *textSystem);
    rawWrapper->setSize(*frameSize);
    rawSelectionLayer->setBounds(Rect{0.f, 0.f, frameSize->width, frameSize->height});
    rawCaretLayer->setBounds(Rect{0.f, 0.f, frameSize->width, frameSize->height});
    setTextLayoutIfNeeded(*rawText, input, *resolvedStyle, *textSystem, *frameSize,
                          layoutResult, lastLayoutText, lastLayoutBox);
  });

  Reactive::withOwner(ctx.owner(), [rawWrapper, rawText, rawSelectionLayer, rawCaretLayer,
                                    valueState, selectionState, focusState, caretOpacity,
                                    input = *this,
                                    theme, resolvedStyle, frameSize, layoutResult, textSystem = &ctx.textSystem(),
                                    lastLayoutText, lastLayoutBox,
                                    requestRedraw = ctx.redrawCallback()] {
    auto lastMeasuredText = std::make_shared<std::string>(valueState.peek());
    Reactive::onCleanup([caretOpacity] {
      caretOpacity.stop();
    });
    Reactive::Effect([rawWrapper, rawText, rawSelectionLayer, rawCaretLayer, valueState,
                      selectionState, focusState, caretOpacity, input, theme, resolvedStyle, frameSize,
                      layoutResult, textSystem, lastLayoutText, lastLayoutBox,
                      lastMeasuredText, requestRedraw] {
      ResolvedTextInputStyle const nextStyle = resolveTextInputStyle(input.style, theme);
      bool const styleChanged = !(nextStyle == *resolvedStyle);
      if (styleChanged) {
        *resolvedStyle = nextStyle;
      }
      std::string const& text = valueState.get();
      if (input.multiline && (styleChanged || text != *lastMeasuredText)) {
        *lastMeasuredText = text;
        relayoutStoredSelfAndAncestors(*rawWrapper);
      }
      detail::TextEditSelection currentSelection = selectionState.get();
      detail::TextEditSelection const clamped =
          detail::clampSelection(text, currentSelection);
      if (!sameSelection(currentSelection, clamped)) {
        selectionState = clamped;
      }
      if (styleChanged) {
        setTextLayout(*rawText, input, *resolvedStyle, *textSystem, *frameSize, layoutResult);
        if (lastLayoutText) {
          *lastLayoutText = text;
        }
        if (lastLayoutBox) {
          *lastLayoutBox = textBox(*frameSize, *resolvedStyle);
        }
      } else {
        setTextLayoutIfNeeded(*rawText, input, *resolvedStyle, *textSystem, *frameSize,
                              layoutResult, lastLayoutText, lastLayoutBox);
      }
      bool const focused = focusState.get();
      if (focused && !input.disabled) {
        caretOpacity.set(1.f, Transition::instant());
        caretOpacity.play(0.f, AnimationOptions{
            .transition = Transition::custom([](float t) { return t < 0.5f ? 0.f : 1.f; }, 1.06f),
            .repeat = AnimationOptions::kRepeatForever,
        });
      } else {
        caretOpacity.stop();
        caretOpacity.set(1.f, Transition::instant());
      }
      rawWrapper->setStroke(focused && !input.disabled
                                ? StrokeStyle::solid(resolvedStyle->chrome.borderFocusColor,
                                                     resolvedStyle->chrome.borderFocusWidth)
                                : StrokeStyle::solid(resolvedStyle->chrome.borderColor,
                                                     resolvedStyle->chrome.borderWidth));
      rawSelectionLayer->invalidate();
      rawCaretLayer->invalidate();
      if (requestRedraw) {
        requestRedraw();
      }
    });
    Reactive::Effect([rawCaretLayer, caretOpacity, focusState, requestRedraw] {
      (void)caretOpacity.get();
      (void)focusState.get();
      rawCaretLayer->invalidate();
      if (requestRedraw) {
        requestRedraw();
      }
    });
  });

  return wrapper;
}

Element TextInput::body() const {
  return Text{
      .text = value.get().empty() ? placeholder : value.get(),
      .font = style.font.evaluate(),
      .color = style.textColor,
      .wrapping = multiline ? TextWrapping::Wrap : TextWrapping::NoWrap,
  };
}

} // namespace lambda
