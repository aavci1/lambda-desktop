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

struct ToolbarButton {
  IconName icon;
  std::string tooltip;
  std::function<void()> onTap;
  bool disabled = false;

  Element body() const {
    useTooltip(TooltipConfig{.text = tooltip, .placement = PopoverPlacement::Below});
    Reactive::Signal<bool> hovered = useHover();
    auto theme = useEnvironment<ThemeKey>();
    auto handleTap = [onTap = onTap, disabled = disabled] {
      if (!disabled && onTap) {
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
                .size(34.f, 34.f)
                .cornerRadius(CornerRadius{6.f})
                .fill([hovered, theme, disabled = disabled] {
                  if (disabled) {
                    return FillStyle::solid(Color{0.f, 0.f, 0.f, 0.02f});
                  }
                  return FillStyle::solid(hovered() ? theme().hoveredControlBackgroundColor
                                                    : Color{0.f, 0.f, 0.f, 0.035f});
                }),
            Icon{
                .name = icon,
                .size = 20.f,
                .weight = 440.f,
                .color = disabled ? Color::secondary() : Color::primary(),
            })}
        .size(34.f, 34.f)
        .cursor(disabled ? Cursor::Inherit : Cursor::Hand)
        .focusable(!disabled)
        .onKeyDown(std::function<void(KeyCode, Modifiers)>{handleKey})
        .onTap(std::function<void()>{handleTap});
  }
};

Element toolbarDivider() {
  return Divider{.orientation = Divider::Orientation::Vertical}.height(34.f);
}

Element toolbarGroup(std::vector<Element> items) {
  return HStack{
      .spacing = 4.f,
      .alignment = Alignment::Center,
      .children = std::move(items),
  }
      .padding(3.f, 4.f, 3.f, 4.f)
      .height(42.f)
      .fill(FillStyle::solid(Color::controlBackground()))
      .stroke(StrokeStyle::solid(Color::separator(), 1.f))
      .cornerRadius(CornerRadius{7.f});
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
    auto activePanel = useState(EditorPanel::None);
    auto findQuery = useState(std::string{});
    auto replaceValue = useState(std::string{});
    auto gotoLineValue = useState(std::string{});
    auto wordWrap = useState(true);
    auto fontSize = useState(14.f);
    auto fontIndex = useState(0);

    std::vector<std::string> const fontFamilies{"monospace", "sans-serif", "serif"};

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
    auto copySelection = [text, selection, status] {
      std::string selected = selectedText(text.peek(), selection.peek());
      if (selected.empty()) {
        status.set("No selection to copy.");
        return;
      }
      if (Application::hasInstance()) {
        Application::instance().clipboard().writeText(selected);
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
    auto pasteClipboard = [text, selection, applySnapshot, status] {
      if (!Application::hasInstance()) {
        return;
      }
      std::optional<std::string> clipboard = Application::instance().clipboard().readText();
      if (!clipboard || clipboard->empty()) {
        status.set("Clipboard is empty.");
        return;
      }
      applySnapshot(insertAtSelection(text.peek(), selection.peek(), *clipboard), true, "Pasted text.");
    };
    auto deleteText = [text, selection, applySnapshot] {
      applySnapshot(deleteSelectionOrForwardChar(text.peek(), selection.peek()), true, "Deleted text.");
    };
    auto selectAll = [text, selection, status] {
      selection.set(detail::selectAllSelection(text.peek()));
      status.set("Selected all.");
    };

    auto showFind = [activePanel, status] {
      activePanel.set(EditorPanel::Find);
      status.set("Find");
    };
    auto showReplace = [activePanel, status] {
      activePanel.set(EditorPanel::Replace);
      status.set("Replace");
    };
    auto showGoToLine = [activePanel, gotoLineValue, text, selection, status] {
      activePanel.set(EditorPanel::GoToLine);
      gotoLineValue.set(std::to_string(lineNumberForSelection(text.peek(), selection.peek())));
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
    auto cycleFont = [fontIndex, fontFamilies, status] {
      int const next = (fontIndex.peek() + 1) % static_cast<int>(fontFamilies.size());
      fontIndex.set(next);
      status.set("Font: " + fontFamilies[static_cast<std::size_t>(next)] + ".");
    };

    useWindowAction("file.new", newFile, ActionDescriptor{
        .label = "New",
        .shortcut = shortcuts::New,
        .isEnabled = [] { return true; },
    });
    useWindowAction("file.open", openFile, ActionDescriptor{
        .label = "Open",
        .shortcut = shortcuts::Open,
        .isEnabled = [] { return true; },
    });
    useWindowAction("file.save", saveFile, ActionDescriptor{
        .label = "Save",
        .shortcut = shortcuts::Save,
        .isEnabled = [] { return true; },
    });
    useWindowAction("file.saveAs", saveFileAs, ActionDescriptor{
        .label = "Save As",
        .shortcut = Shortcut{keys::S, Modifiers::Meta | Modifiers::Shift},
        .isEnabled = [] { return true; },
    });
    useWindowAction("edit.undo", undo, ActionDescriptor{
        .label = "Undo",
        .shortcut = shortcuts::Undo,
        .isEnabled = [canUndo] { return canUndo.peek(); },
    });
    useWindowAction("edit.redo", redo, ActionDescriptor{
        .label = "Redo",
        .shortcut = shortcuts::Redo,
        .isEnabled = [canRedo] { return canRedo.peek(); },
    });
    useWindowAction("edit.cut", cutSelection, ActionDescriptor{
        .label = "Cut",
        .shortcut = shortcuts::Cut,
        .isEnabled = [selection] { return selection.peek().hasSelection(); },
    });
    useWindowAction("edit.copy", copySelection, ActionDescriptor{
        .label = "Copy",
        .shortcut = shortcuts::Copy,
        .isEnabled = [selection] { return selection.peek().hasSelection(); },
    });
    useWindowAction("edit.paste", pasteClipboard, ActionDescriptor{
        .label = "Paste",
        .shortcut = shortcuts::Paste,
        .isEnabled = [] { return Application::hasInstance() && Application::instance().clipboard().hasText(); },
    });
    useWindowAction("edit.delete", deleteText, ActionDescriptor{
        .label = "Delete",
        .isEnabled = [text, selection] {
          return selection.peek().hasSelection() ||
                 selection.peek().caretByte < static_cast<int>(text.peek().size());
        },
    });
    useWindowAction("edit.selectAll", selectAll, ActionDescriptor{
        .label = "Select All",
        .shortcut = shortcuts::SelectAll,
        .isEnabled = [text] { return !text.peek().empty(); },
    });
    useWindowAction("editor.find", showFind, ActionDescriptor{
        .label = "Find",
        .shortcut = shortcuts::Find,
        .isEnabled = [] { return true; },
    });
    useWindowAction("editor.replace", showReplace, ActionDescriptor{
        .label = "Replace",
        .shortcut = Shortcut{keys::H, Modifiers::Meta},
        .isEnabled = [] { return true; },
    });
    useWindowAction("editor.goToLine", showGoToLine, ActionDescriptor{
        .label = "Go To Line",
        .shortcut = Shortcut{keys::G, Modifiers::Meta},
        .isEnabled = [] { return true; },
    });
    useWindowAction("view.wordWrap", toggleWordWrap, ActionDescriptor{
        .label = "Word Wrap",
        .isEnabled = [] { return true; },
    });
    useWindowAction("view.zoomOut", zoomOut, ActionDescriptor{
        .label = "Zoom Out",
        .shortcut = Shortcut{keys::Minus, Modifiers::Meta},
        .isEnabled = [fontSize] { return fontSize.peek() > 10.f; },
    });
    useWindowAction("view.zoomIn", zoomIn, ActionDescriptor{
        .label = "Zoom In",
        .shortcut = Shortcut{keys::Equal, Modifiers::Meta},
        .isEnabled = [fontSize] { return fontSize.peek() < 32.f; },
    });
    useWindowAction("view.font", cycleFont, ActionDescriptor{
        .label = "Font",
        .isEnabled = [] { return true; },
    });

    bool const hasSelection = selection().hasSelection();
    bool const canDelete =
        hasSelection || selection().caretByte < static_cast<int>(text().size());
    bool const canPaste =
        Application::hasInstance() && Application::instance().clipboard().hasText();
    bool const canZoomOut = fontSize() > 10.f;
    bool const canZoomIn = fontSize() < 32.f;

    TextInput::Style editorStyle = TextInput::Style::plain();
    editorStyle.font = Font{
        .family = fontFamilies[static_cast<std::size_t>(fontIndex())],
        .size = fontSize(),
    };
    editorStyle.textColor = Color::primary();
    editorStyle.placeholderColor = Color::secondary();
    editorStyle.paddingH = 12.f;
    editorStyle.paddingV = 12.f;
    editorStyle.lineHeight = fontSize() + 6.f;

    TextInput::Style panelInputStyle = compactInputStyle();

    auto panel = Show(
        [activePanel] {
          return activePanel() != EditorPanel::None;
        },
        [activePanel, findQuery, replaceValue, gotoLineValue, findNext, replaceOne, replaceAll,
         goToLine, closePanel, panelInputStyle, theme] {
          return HStack{
                     .spacing = 8.f,
                     .alignment = Alignment::Center,
                     .children = children(
                         Show(
                             [activePanel] {
                               return activePanel() == EditorPanel::Find ||
                                      activePanel() == EditorPanel::Replace;
                             },
                             [findQuery, findNext, panelInputStyle] {
                               return HStack{
                                   .spacing = 6.f,
                                   .alignment = Alignment::Center,
                                   .children = children(
                                       TextInput{
                                           .value = findQuery,
                                           .placeholder = "Find",
                                           .style = panelInputStyle,
                                           .onSubmit = [findNext](std::string const&) { findNext(); },
                                       }.width(220.f),
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
                                           .onSubmit = [replaceOne](std::string const&) { replaceOne(); },
                                       }.width(220.f),
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
                             [gotoLineValue, goToLine, panelInputStyle] {
                               return HStack{
                                   .spacing = 6.f,
                                   .alignment = Alignment::Center,
                                   .children = children(
                                       TextInput{
                                           .value = gotoLineValue,
                                           .placeholder = "Line",
                                           .style = panelInputStyle,
                                           .onSubmit = [goToLine](std::string const&) { goToLine(); },
                                       }.width(120.f),
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
                                             .disabled = !canUndo()},
                               ToolbarButton{.icon = IconName::Redo,
                                             .tooltip = "Redo",
                                             .onTap = redo,
                                             .disabled = !canRedo()})),
                           toolbarDivider(),
                           toolbarGroup(children(
                               ToolbarButton{.icon = IconName::ContentCut,
                                             .tooltip = "Cut",
                                             .onTap = cutSelection,
                                             .disabled = !hasSelection},
                               ToolbarButton{.icon = IconName::ContentCopy,
                                             .tooltip = "Copy",
                                             .onTap = copySelection,
                                             .disabled = !hasSelection},
                               ToolbarButton{.icon = IconName::ContentPaste,
                                             .tooltip = "Paste",
                                             .onTap = pasteClipboard,
                                             .disabled = !canPaste},
                               ToolbarButton{.icon = IconName::Delete,
                                             .tooltip = "Delete",
                                             .onTap = deleteText,
                                             .disabled = !canDelete},
                               ToolbarButton{.icon = IconName::SelectAll,
                                             .tooltip = "Select All",
                                             .onTap = selectAll,
                                             .disabled = text().empty()})),
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
                                             .tooltip = wordWrap() ? "Word Wrap: On" : "Word Wrap: Off",
                                             .onTap = toggleWordWrap},
                               ToolbarButton{.icon = IconName::ZoomOut,
                                             .tooltip = "Zoom Out",
                                             .onTap = zoomOut,
                                             .disabled = !canZoomOut},
                               ToolbarButton{.icon = IconName::ZoomIn,
                                             .tooltip = "Zoom In",
                                             .onTap = zoomIn,
                                             .disabled = !canZoomIn},
                               ToolbarButton{.icon = IconName::TextFields,
                                             .tooltip = "Font",
                                             .onTap = cycleFont})),
                           Spacer{})}
                       .height(56.f)
                       .padding(7.f, theme().space3, 7.f, theme().space3)
                       .fill(FillStyle::solid(Color::windowBackground()))
                       .stroke(StrokeStyle::solid(Color::separator(), 1.f)),
                   std::move(panel),
                   ScrollView{
                       .axis = wordWrap() ? ScrollAxis::Vertical : ScrollAxis::Both,
                       .children = children(
                           TextInput{
                               .value = text,
                               .selection = selection,
                               .placeholder = "Start typing...",
                               .style = editorStyle,
                               .multiline = true,
                               .wrapping = wordWrap() ? TextWrapping::Wrap : TextWrapping::NoWrap,
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
                           }.fill(FillStyle::solid(Color::controlBackground()))),
                   }.flex(1.f, 1.f, 0.f)
                       .fill(FillStyle::solid(Color::controlBackground())),
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
