#include "EditorCommands.hpp"
#include "EditorDocument.hpp"

#include <Lambda.hpp>
#include <Lambda/UI/Events.hpp>
#include <Lambda/UI/Shortcut.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/UI.hpp>
#include <Lambda/UI/WindowUI.hpp>
#include <Lambda/UI/Views/Views.hpp>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace lambda;
using namespace lambda_editor;

namespace {

enum class EditorPanel {
  None,
  Find,
  Replace,
  GoToLine,
};

std::string editorWindowTitle(EditorDocument const& document) {
  return document.displayName() + (document.isDirty() ? " *" : "") + " - Lambda Editor";
}

std::filesystem::path documentDirectory(EditorDocument const& document) {
  if (document.hasPath()) {
    return document.path().parent_path();
  }
  std::error_code ec;
  std::filesystem::path current = std::filesystem::current_path(ec);
  return ec ? std::filesystem::path{"."} : current;
}

std::string saveDialogInitialName(EditorDocument const& document) {
  if (!document.hasPath()) {
    return "Untitled.txt";
  }
  return document.displayName();
}

void requestWindowClose(unsigned int handle) {
  if (handle != 0 && Application::hasInstance()) {
    Application::instance().eventQueue().post(WindowEvent{WindowEvent::Kind::CloseRequest, handle});
  }
}

int parsePositiveInt(std::string const& text) {
  if (text.empty()) {
    return 0;
  }
  int value = 0;
  for (char ch : text) {
    if (ch < '0' || ch > '9') {
      return 0;
    }
    value = value * 10 + (ch - '0');
  }
  return value;
}

TextInput::Style compactInputStyle() {
  TextInput::Style style;
  style.font = Font{.size = 12.f, .weight = 430.f};
  style.paddingH = 8.f;
  style.paddingV = 5.f;
  style.cornerRadius = 6.f;
  return style;
}

constexpr Shortcut ctrlShortcut(KeyCode key, Modifiers extra = Modifiers::None) {
  return Shortcut{key, Modifiers::Ctrl | extra};
}

struct ToolbarButton {
  IconName icon;
  std::string tooltip;
  std::function<void()> onTap;
  Reactive::Bindable<bool> disabled{false};

  Element body() const {
    useTooltip(TooltipConfig{.text = tooltip, .placement = PopoverPlacement::Below});
    Reactive::Signal<bool> hovered = useHover();
    auto theme = useEnvironment<ThemeKey>();
    auto disabledBinding = disabled;
    auto handleTap = [onTap = onTap, disabledBinding] {
      if (!disabledBinding.evaluate() && onTap) {
        onTap();
      }
    };
    auto handleKey = [handleTap](KeyCode key, Modifiers) {
      if (key == keys::Return || key == keys::Space) {
        handleTap();
      }
    };

    return ZStack{
        .horizontalAlignment = Alignment::Center,
        .verticalAlignment = Alignment::Center,
        .children = children(
            Rectangle{}
                .size(42.f, 42.f)
                .cornerRadius(CornerRadius{7.f})
                .fill([hovered, theme, disabledBinding] {
                  if (disabledBinding.evaluate()) {
                    return FillStyle::solid(Colors::transparent);
                  }
                  return FillStyle::solid(hovered() ? theme().hoveredControlBackgroundColor
                                                    : Colors::transparent);
                }),
            Icon{
                .name = icon,
                .size = 26.f,
                .weight = 430.f,
                .color = [disabledBinding] {
                  return disabledBinding.evaluate() ? Color::secondary() : Color::primary();
                },
            })}
        .size(42.f, 42.f)
        .cursor([disabledBinding] {
          return disabledBinding.evaluate() ? Cursor::Inherit : Cursor::Hand;
        })
        .focusable([disabledBinding] {
          return !disabledBinding.evaluate();
        })
        .onKeyDown(std::function<void(KeyCode, Modifiers)>{handleKey})
        .onTap(std::function<void()>{handleTap});
  }
};

struct AutoFocusTextInput {
  TextInput input;
  Reactive::Signal<int> focusGeneration;

  Element body() const {
    useAutoFocus(focusGeneration);
    return input;
  }
};

Element toolbarDivider() {
  return Spacer{}.flex(0.f, 0.f, 12.f);
}

Element toolbarGroup(std::vector<Element> items) {
  return HStack{
      .spacing = 3.f,
      .alignment = Alignment::Center,
      .children = std::move(items),
  };
}

struct LambdaEditor {
  EditorDocument initialDocument = EditorDocument::untitled();
  std::string initialStatus;

  Element body() const {
    auto theme = useEnvironment<ThemeKey>();
    Runtime* runtime = Runtime::current();
    Window* window = runtime ? &runtime->window() : nullptr;
    auto document = useState(initialDocument);
    auto text = useState(initialDocument.text());
    auto selection = useState(detail::TextEditSelection{
        .caretByte = static_cast<int>(initialDocument.text().size()),
        .anchorByte = static_cast<int>(initialDocument.text().size()),
    });
    auto status = useState(initialStatus.empty() ? std::string{"Ready"} : initialStatus);
    auto history = useState<std::shared_ptr<EditorEditHistory>>(std::make_shared<EditorEditHistory>());
    auto historyReady = useState(false);
    auto canUndo = useState(false);
    auto canRedo = useState(false);
    auto clipboardHasText = useState(
        Application::hasInstance() && Application::instance().clipboard().hasText());
    auto activePanel = useState(EditorPanel::None);
    auto panelFocusGeneration = useState(0);
    auto findQuery = useState(std::string{});
    auto replaceValue = useState(std::string{});
    auto gotoLineValue = useState(std::string{});
    auto wordWrap = useState(true);
    auto fontSize = useState(14.f);

    auto refreshHistoryState = [history, canUndo, canRedo] {
      canUndo.set(history.peek()->canUndo());
      canRedo.set(history.peek()->canRedo());
    };

    if (!historyReady.peek()) {
      history.peek()->reset(EditorSnapshot{.text = text.peek(), .selection = selection.peek()});
      historyReady.set(true);
      refreshHistoryState();
    }

    useEffect([document, window] {
      if (window) {
        window->setTitle(editorWindowTitle(document.get()));
      }
    });

    auto updateDocumentText = [document](std::string const& value) {
      EditorDocument current = document.peek();
      current.setText(value);
      document.set(std::move(current));
    };

    auto applySnapshot = [text, selection, updateDocumentText, history, refreshHistoryState, status](
                             EditorSnapshot snapshot, bool recordHistory, std::string message) {
      snapshot.selection = detail::clampSelection(snapshot.text, snapshot.selection);
      text.set(snapshot.text);
      selection.set(snapshot.selection);
      updateDocumentText(snapshot.text);
      if (recordHistory) {
        history.peek()->record(snapshot);
      }
      refreshHistoryState();
      status.set(std::move(message));
    };

    auto resetToDocument = [document, text, selection, history, refreshHistoryState, status](
                               EditorDocument next, std::string message) {
      detail::TextEditSelection endSelection{
          .caretByte = static_cast<int>(next.text().size()),
          .anchorByte = static_cast<int>(next.text().size()),
      };
      text.set(next.text());
      selection.set(endSelection);
      history.peek()->reset(EditorSnapshot{.text = next.text(), .selection = endSelection});
      document.set(std::move(next));
      refreshHistoryState();
      status.set(std::move(message));
    };

    auto showFileDialog = [window](FileDialog dialog) mutable {
      if (!window || !Application::hasInstance()) {
        return;
      }
      FileDialogMode const dialogMode = dialog.mode;
      Window& dialogWindow = Application::instance().createModalChildWindow<Window>(window->handle(), {
          .size = {760.f, 540.f},
          .title = dialogMode == FileDialogMode::Open ? "Open File" : "Save File",
          .resizable = true,
          .minSize = {640.f, 430.f},
      });
      unsigned int const dialogHandle = dialogWindow.handle();
      auto accept = std::move(dialog.onAccept);
      dialog.onAccept = [accept = std::move(accept), dialogHandle](std::filesystem::path path) mutable {
        bool const accepted = accept ? accept(std::move(path)) : true;
        if (accepted) {
          requestWindowClose(dialogHandle);
        }
        return accepted;
      };
      dialog.onCancel = [dialogHandle] {
        requestWindowClose(dialogHandle);
      };
      dialogWindow.setView(std::move(dialog));
    };

    auto newFile = [resetToDocument] {
      resetToDocument(EditorDocument::untitled(), "New document");
    };
    auto openFile = [document, resetToDocument, status, showFileDialog] mutable {
      showFileDialog(FileDialog{
          .mode = FileDialogMode::Open,
          .initialDirectory = documentDirectory(document.peek()),
          .onAccept = [resetToDocument, status](std::filesystem::path path) {
            EditorDocumentResult result = openDocument(path.string());
            if (result.ok) {
              resetToDocument(result.document, result.status);
            } else {
              status.set(result.status);
            }
            return result.ok;
          },
      });
    };
    auto saveFileAs = [document, text, status, showFileDialog] mutable {
      showFileDialog(FileDialog{
          .mode = FileDialogMode::Save,
          .initialDirectory = documentDirectory(document.peek()),
          .initialName = saveDialogInitialName(document.peek()),
          .onAccept = [document, text, status](std::filesystem::path path) {
            EditorDocument current = document.peek();
            current.setText(text.peek());
            EditorDocumentResult result = saveDocumentAs(current, path.string());
            status.set(result.status);
            if (result.ok) {
              document.set(result.document);
              text.set(result.document.text());
            }
            return result.ok;
          },
      });
    };
    auto saveFile = [document, text, status, saveFileAs] mutable {
      EditorDocument current = document.peek();
      current.setText(text.peek());
      EditorDocumentResult result = saveDocument(current);
      status.set(result.status);
      if (result.ok) {
        document.set(result.document);
        text.set(result.document.text());
      } else if (result.needsPath) {
        saveFileAs();
      }
    };

    auto undo = [history, applySnapshot] {
      if (std::optional<EditorSnapshot> snapshot = history.peek()->undo()) {
        applySnapshot(*snapshot, false, "Undo");
      }
    };
    auto redo = [history, applySnapshot] {
      if (std::optional<EditorSnapshot> snapshot = history.peek()->redo()) {
        applySnapshot(*snapshot, false, "Redo");
      }
    };
    auto copySelection = [text, selection, clipboardHasText, status] {
      std::string selected = selectedText(text.peek(), selection.peek());
      if (selected.empty()) {
        status.set("No selection to copy.");
        return;
      }
      if (Application::hasInstance()) {
        Application::instance().clipboard().writeText(selected);
        clipboardHasText.set(true);
      }
      status.set("Copied selection.");
    };
    auto cutSelection = [text, selection, copySelection, applySnapshot, status] {
      if (!selection.peek().hasSelection()) {
        status.set("No selection to cut.");
        return;
      }
      copySelection();
      applySnapshot(replaceSelection(text.peek(), selection.peek(), ""), true, "Cut selection.");
    };
    auto pasteClipboard = [text, selection, clipboardHasText, applySnapshot, status] {
      if (!Application::hasInstance()) {
        clipboardHasText.set(false);
        return;
      }
      std::optional<std::string> clipboard = Application::instance().clipboard().readText();
      if (!clipboard || clipboard->empty()) {
        clipboardHasText.set(false);
        status.set("Clipboard is empty.");
        return;
      }
      clipboardHasText.set(true);
      applySnapshot(insertAtSelection(text.peek(), selection.peek(), *clipboard), true, "Pasted text.");
    };
    auto requestPanelFocus = [panelFocusGeneration] {
      panelFocusGeneration.set(panelFocusGeneration.peek() + 1);
    };
    auto showFind = [activePanel, requestPanelFocus, status] {
      activePanel.set(EditorPanel::Find);
      requestPanelFocus();
      status.set("Find");
    };
    auto showReplace = [activePanel, requestPanelFocus, status] {
      activePanel.set(EditorPanel::Replace);
      requestPanelFocus();
      status.set("Replace");
    };
    auto showGoToLine = [activePanel, requestPanelFocus, gotoLineValue, text, selection, status] {
      activePanel.set(EditorPanel::GoToLine);
      gotoLineValue.set(std::to_string(lineNumberForSelection(text.peek(), selection.peek())));
      requestPanelFocus();
      status.set("Go to line");
    };
    auto closePanel = [activePanel, status] {
      activePanel.set(EditorPanel::None);
      status.set("Ready");
    };
    auto findNext = [text, selection, findQuery, status] {
      if (std::optional<detail::TextEditSelection> match =
              findNextMatch(text.peek(), findQuery.peek(), selection.peek(), true)) {
        selection.set(*match);
        status.set("Match found.");
      } else {
        status.set("No match.");
      }
    };
    auto replaceOne = [text, selection, findQuery, replaceValue, applySnapshot, findNext, status] {
      if (findQuery.peek().empty()) {
        status.set("Enter text to replace.");
        return;
      }
      if (selectedText(text.peek(), selection.peek()) != findQuery.peek()) {
        findNext();
        return;
      }
      applySnapshot(replaceSelection(text.peek(), selection.peek(), replaceValue.peek()),
                    true,
                    "Replaced match.");
    };
    auto replaceAll = [text, findQuery, replaceValue, applySnapshot, status] {
      if (findQuery.peek().empty()) {
        status.set("Enter text to replace.");
        return;
      }
      EditorSnapshot snapshot = replaceAllMatches(text.peek(), findQuery.peek(), replaceValue.peek());
      if (snapshot.text == text.peek()) {
        status.set("No match.");
        return;
      }
      applySnapshot(std::move(snapshot), true, "Replaced all matches.");
    };
    auto goToLine = [text, selection, gotoLineValue, status] {
      int const line = parsePositiveInt(gotoLineValue.peek());
      if (line <= 0) {
        status.set("Enter a line number.");
        return;
      }
      selection.set(lineSelection(text.peek(), line));
      status.set("Moved to line " + std::to_string(line) + ".");
    };

    auto toggleWordWrap = [wordWrap, status] {
      bool const next = !wordWrap.peek();
      wordWrap.set(next);
      status.set(next ? "Word wrap on." : "Word wrap off.");
    };
    auto zoomOut = [fontSize, status] {
      float const next = std::max(10.f, fontSize.peek() - 1.f);
      fontSize.set(next);
      status.set("Zoom " + std::to_string(static_cast<int>(next)) + " pt.");
    };
    auto zoomIn = [fontSize, status] {
      float const next = std::min(32.f, fontSize.peek() + 1.f);
      fontSize.set(next);
      status.set("Zoom " + std::to_string(static_cast<int>(next)) + " pt.");
    };
    useWindowAction("file.new", newFile, ActionDescriptor{
        .label = "New",
        .shortcut = ctrlShortcut(keys::N),
        .isEnabled = [] { return true; },
    });
    useWindowAction("file.open", openFile, ActionDescriptor{
        .label = "Open",
        .shortcut = ctrlShortcut(keys::O),
        .isEnabled = [] { return true; },
    });
    useWindowAction("file.save", saveFile, ActionDescriptor{
        .label = "Save",
        .shortcut = ctrlShortcut(keys::S),
        .isEnabled = [] { return true; },
    });
    useWindowAction("file.saveAs", saveFileAs, ActionDescriptor{
        .label = "Save As",
        .shortcut = ctrlShortcut(keys::S, Modifiers::Shift),
        .isEnabled = [] { return true; },
    });
    useWindowAction("edit.undo", undo, ActionDescriptor{
        .label = "Undo",
        .shortcut = ctrlShortcut(keys::Z),
        .isEnabled = [canUndo] { return canUndo.peek(); },
    });
    useWindowAction("edit.redo", redo, ActionDescriptor{
        .label = "Redo",
        .shortcut = ctrlShortcut(keys::Z, Modifiers::Shift),
        .isEnabled = [canRedo] { return canRedo.peek(); },
    });
    useWindowAction("edit.cut", cutSelection, ActionDescriptor{
        .label = "Cut",
        .shortcut = ctrlShortcut(keys::X),
        .isEnabled = [selection] { return selection.peek().hasSelection(); },
    });
    useWindowAction("edit.copy", copySelection, ActionDescriptor{
        .label = "Copy",
        .shortcut = ctrlShortcut(keys::C),
        .isEnabled = [selection] { return selection.peek().hasSelection(); },
    });
    useWindowAction("edit.paste", pasteClipboard, ActionDescriptor{
        .label = "Paste",
        .shortcut = ctrlShortcut(keys::V),
        .isEnabled = [clipboardHasText] {
          return clipboardHasText.peek() ||
                 (Application::hasInstance() && Application::instance().clipboard().hasText());
        },
    });
    useWindowAction("editor.find", showFind, ActionDescriptor{
        .label = "Find",
        .shortcut = ctrlShortcut(keys::F),
        .isEnabled = [] { return true; },
    });
    useWindowAction("editor.replace", showReplace, ActionDescriptor{
        .label = "Replace",
        .shortcut = ctrlShortcut(keys::H),
        .isEnabled = [] { return true; },
    });
    useWindowAction("editor.goToLine", showGoToLine, ActionDescriptor{
        .label = "Go To Line",
        .shortcut = ctrlShortcut(keys::G),
        .isEnabled = [] { return true; },
    });
    useWindowAction("view.wordWrap", toggleWordWrap, ActionDescriptor{
        .label = "Word Wrap",
        .isEnabled = [] { return true; },
    });
    useWindowAction("view.zoomOut", zoomOut, ActionDescriptor{
        .label = "Zoom Out",
        .shortcut = ctrlShortcut(keys::Minus),
        .isEnabled = [fontSize] { return fontSize.peek() > 10.f; },
    });
    useWindowAction("view.zoomIn", zoomIn, ActionDescriptor{
        .label = "Zoom In",
        .shortcut = ctrlShortcut(keys::Equal),
        .isEnabled = [fontSize] { return fontSize.peek() < 32.f; },
    });

    TextInput::Style editorStyle = TextInput::Style::plain();
    editorStyle.font = [fontSize] {
      return Font{
          .family = "monospace",
          .size = fontSize(),
      };
    };
    editorStyle.textColor = Color::primary();
    editorStyle.placeholderColor = Color::secondary();
    editorStyle.paddingH = 12.f;
    editorStyle.paddingV = 12.f;
    editorStyle.lineHeight = [fontSize] {
      return fontSize() + 6.f;
    };

    TextInput::Style panelInputStyle = compactInputStyle();

    auto panel = Show(
        [activePanel] {
          return activePanel() != EditorPanel::None;
        },
        [activePanel, panelFocusGeneration, findQuery, replaceValue, gotoLineValue, findNext,
         replaceOne, replaceAll, goToLine, closePanel, panelInputStyle, theme] {
          return HStack{
              .spacing = 8.f,
              .alignment = Alignment::Center,
              .children = children(
                  Show(
                      [activePanel] {
                        return activePanel() == EditorPanel::Find ||
                               activePanel() == EditorPanel::Replace;
                      },
                      [findQuery, findNext, panelFocusGeneration, panelInputStyle] {
                        return HStack{
                            .spacing = 6.f,
                            .alignment = Alignment::Center,
                            .children = children(
                                Element{AutoFocusTextInput{
                                            .input = TextInput{
                                                .value = findQuery,
                                                .placeholder = "Find",
                                                .style = panelInputStyle,
                                                .onSubmit = [findNext](std::string const &) { findNext(); },
                                            },
                                            .focusGeneration = panelFocusGeneration,
                                        }}
                                    .width(220.f),
                                Button{
                                    .label = "Next",
                                    .variant = ButtonVariant::Secondary,
                                    .disabled = [findQuery] { return findQuery().empty(); },
                                    .onTap = findNext,
                                })};
                      }),
                  Show(
                      [activePanel] {
                        return activePanel() == EditorPanel::Replace;
                      },
                      [replaceValue, replaceOne, replaceAll, panelInputStyle] {
                        return HStack{
                            .spacing = 6.f,
                            .alignment = Alignment::Center,
                            .children = children(
                                TextInput{
                                    .value = replaceValue,
                                    .placeholder = "Replace",
                                    .style = panelInputStyle,
                                    .onSubmit = [replaceOne](std::string const &) { replaceOne(); },
                                }
                                    .width(220.f),
                                Button{
                                    .label = "Replace",
                                    .variant = ButtonVariant::Secondary,
                                    .onTap = replaceOne,
                                },
                                Button{
                                    .label = "All",
                                    .variant = ButtonVariant::Secondary,
                                    .onTap = replaceAll,
                                })};
                      }),
                  Show(
                      [activePanel] {
                        return activePanel() == EditorPanel::GoToLine;
                      },
                      [gotoLineValue, goToLine, panelFocusGeneration, panelInputStyle] {
                        return HStack{
                            .spacing = 6.f,
                            .alignment = Alignment::Center,
                            .children = children(
                                Element{AutoFocusTextInput{
                                            .input = TextInput{
                                                .value = gotoLineValue,
                                                .placeholder = "Line",
                                                .style = panelInputStyle,
                                                .onSubmit = [goToLine](std::string const &) { goToLine(); },
                                            },
                                            .focusGeneration = panelFocusGeneration,
                                        }}
                                    .width(120.f),
                                Button{
                                    .label = "Go",
                                    .variant = ButtonVariant::Secondary,
                                    .onTap = goToLine,
                                })};
                      }),
                  Spacer{}.flex(1.f, 1.f, 0.f),
                  Button{
                      .label = "Close",
                      .variant = ButtonVariant::Ghost,
                      .onTap = closePanel,
                  })}
              .height(44.f)
              .padding(5.f, theme().space3, 5.f, theme().space3)
              .fill(FillStyle::solid(Color::windowBackground()))
              .stroke(StrokeStyle::solid(Color::separator(), 1.f));
        });

    auto editorArea = [text, selection, document, history, refreshHistoryState,
                       editorStyle](bool wrap) {
      return ScrollView{
          .axis = wrap ? ScrollAxis::Vertical : ScrollAxis::Both,
          .children = children(
              TextInput{
                  .value = text,
                  .selection = selection,
                  .placeholder = "Start typing...",
                  .style = editorStyle,
                  .multiline = true,
                  .wrapping = wrap ? TextWrapping::Wrap : TextWrapping::NoWrap,
                  .multilineHeight = {.fixed = 0.f, .minIntrinsic = 560.f},
                  .onEdit = [document, history, refreshHistoryState](
                                std::string const& value,
                                detail::TextEditSelection editSelection) {
                    EditorDocument current = document.peek();
                    current.setText(value);
                    document.set(std::move(current));
                    history.peek()->record(EditorSnapshot{
                        .text = value,
                        .selection = editSelection,
                    });
                    refreshHistoryState();
                  },
              }
                  .fill(FillStyle::solid(Color::controlBackground()))),
      }
          .flex(1.f, 1.f, 0.f)
          .fill(FillStyle::solid(Color::controlBackground()));
    };

    auto editorContent = Element{Show(
        [wordWrap] {
          return wordWrap();
        },
        [editorArea] {
          return editorArea(true);
        },
        [editorArea] {
          return editorArea(false);
        })}.flex(1.f, 1.f, 0.f);

    return VStack{
               .spacing = 0.f,
               .alignment = Alignment::Stretch,
               .children = children(
                   HStack{
                       .spacing = 8.f,
                       .alignment = Alignment::Center,
                       .children = children(
                           toolbarGroup(children(
                               ToolbarButton{.icon = IconName::NoteAdd,
                                             .tooltip = "New",
                                             .onTap = newFile},
                               ToolbarButton{.icon = IconName::FileOpen,
                                             .tooltip = "Open",
                                             .onTap = openFile},
                               ToolbarButton{.icon = IconName::Save,
                                             .tooltip = "Save",
                                             .onTap = saveFile},
                               ToolbarButton{.icon = IconName::SaveAs,
                                             .tooltip = "Save As",
                                             .onTap = saveFileAs})),
                           toolbarDivider(),
                           toolbarGroup(children(
                               ToolbarButton{.icon = IconName::Undo,
                                             .tooltip = "Undo",
                                             .onTap = undo,
                                             .disabled = [canUndo] { return !canUndo(); }},
                               ToolbarButton{.icon = IconName::Redo,
                                             .tooltip = "Redo",
                                             .onTap = redo,
                                             .disabled = [canRedo] { return !canRedo(); }})),
                           toolbarDivider(),
                           toolbarGroup(children(
                               ToolbarButton{.icon = IconName::ContentCut,
                                             .tooltip = "Cut",
                                             .onTap = cutSelection,
                                             .disabled = [selection] {
                                               return !selection().hasSelection();
                                             }},
                               ToolbarButton{.icon = IconName::ContentCopy,
                                             .tooltip = "Copy",
                                             .onTap = copySelection,
                                             .disabled = [selection] {
                                               return !selection().hasSelection();
                                             }},
                               ToolbarButton{.icon = IconName::ContentPaste,
                                             .tooltip = "Paste",
                                             .onTap = pasteClipboard,
                                             .disabled = [clipboardHasText] {
                                               return !clipboardHasText() &&
                                                      (!Application::hasInstance() ||
                                                       !Application::instance().clipboard().hasText());
                                             }})),
                           toolbarDivider(),
                           toolbarGroup(children(
                               ToolbarButton{.icon = IconName::Search,
                                             .tooltip = "Find",
                                             .onTap = showFind},
                               ToolbarButton{.icon = IconName::FindReplace,
                                             .tooltip = "Replace",
                                             .onTap = showReplace},
                               ToolbarButton{.icon = IconName::MoreHoriz,
                                             .tooltip = "Go To Line",
                                             .onTap = showGoToLine})),
                           toolbarDivider(),
                           toolbarGroup(children(
                               ToolbarButton{.icon = IconName::WrapText,
                                             .tooltip = "Word Wrap",
                                             .onTap = toggleWordWrap})),
                           toolbarDivider(),
                           toolbarGroup(children(
                               ToolbarButton{.icon = IconName::ZoomOut,
                                             .tooltip = "Zoom Out",
                                             .onTap = zoomOut,
                                             .disabled = [fontSize] { return fontSize() <= 10.f; }},
                               ToolbarButton{.icon = IconName::ZoomIn,
                                             .tooltip = "Zoom In",
                                             .onTap = zoomIn,
                                             .disabled = [fontSize] { return fontSize() >= 32.f; }})),
                           Spacer{})}
                       .height(56.f)
                       .padding(5.f, theme().space3, 5.f, theme().space3)
                       .fill(FillStyle::solid(Color::windowBackground()))
                       .stroke(StrokeStyle::solid(Color::separator(), 1.f)),
                   std::move(panel),
                   std::move(editorContent),
                   HStack{
                       .spacing = theme().space2,
                       .alignment = Alignment::Center,
                       .children = children(
                           Text{
                               .text = status,
                               .font = Font{.size = 12.f, .weight = 450.f},
                               .color = Color::secondary(),
                               .verticalAlignment = VerticalAlignment::Center,
                           }.flex(1.f, 1.f, 0.f),
                           Text{
                               .text = [text, selection] {
                                 return std::to_string(text().size()) + " chars, line " +
                                        std::to_string(lineNumberForSelection(text(), selection()));
                               },
                               .font = Font{.size = 12.f, .weight = 450.f},
                               .color = Color::secondary(),
                               .verticalAlignment = VerticalAlignment::Center,
                           })}
                       .padding(8.f, theme().space3, 8.f, theme().space3)
                       .fill(FillStyle::solid(Color::windowBackground()))
                       .stroke(StrokeStyle::solid(Color::separator(), 1.f)))};
  }
};

} // namespace

int main(int argc, char* argv[]) {
  Application app(argc, argv);

  std::string initialPath = argc > 1 ? std::string(argv[1]) : std::string{};
  EditorDocumentResult initial =
      initialPath.empty() ? EditorDocumentResult{} : openDocument(initialPath);

  auto& window = app.createWindow<Window>({
      .size = {920.f, 720.f},
      .title = "Lambda Editor",
      .resizable = true,
  });
  window.registerAction("app.quit", {.label = "Quit", .shortcut = shortcuts::Quit, .isEnabled = [] { return true; }});
  window.setView<LambdaEditor>({
      .initialDocument = initial.ok ? std::move(initial.document) : EditorDocument::untitled(),
      .initialStatus = initial.status,
  });
  return app.exec();
}
