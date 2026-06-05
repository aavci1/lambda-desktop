#include "EditorDocument.hpp"

#include <Lambda.hpp>
#include <Lambda/UI/Events.hpp>
#include <Lambda/UI/Shortcut.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/UI.hpp>
#include <Lambda/UI/WindowUI.hpp>
#include <Lambda/UI/Views/Views.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

using namespace lambda;
using namespace lambda_editor;

namespace {

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

struct ToolbarButton {
  IconName icon;
  std::string tooltip;
  std::function<void()> onTap;
  bool disabled = false;

  Element body() const {
    useTooltip(TooltipConfig{.text = tooltip, .placement = PopoverPlacement::Below});
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
                .fill(FillStyle::solid(disabled ? Color{0.f, 0.f, 0.f, 0.02f}
                                                 : Color{0.f, 0.f, 0.f, 0.035f})),
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
    auto status = useState(initialStatus.empty() ? std::string{"Ready"} : initialStatus);

    useEffect([document, window] {
      if (window) {
        window->setTitle(editorWindowTitle(document.get()));
      }
    });

    auto markComingSoon = [status](std::string label) {
      status.set(label + " is not implemented yet.");
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
    auto openFile = [document, text, status, showFileDialog] mutable {
      showFileDialog(FileDialog{
          .mode = FileDialogMode::Open,
          .initialDirectory = documentDirectory(document.peek()),
          .onAccept = [document, text, status](std::filesystem::path path) {
            EditorDocumentResult result = openDocument(path.string());
            status.set(result.status);
            if (result.ok) {
              document.set(result.document);
              text.set(result.document.text());
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
    auto newFile = [document, text, status] {
      document.set(EditorDocument::untitled());
      text.set("");
      status.set("New document");
    };

    TextInput::Style editorStyle = TextInput::Style::plain();
    editorStyle.font = Font{.family = "monospace", .size = 14.f};
    editorStyle.textColor = Color::primary();
    editorStyle.placeholderColor = Color::secondary();
    editorStyle.paddingH = 12.f;
    editorStyle.paddingV = 12.f;
    editorStyle.lineHeight = 20.f;

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
                               ToolbarButton{.icon = IconName::Print,
                                             .tooltip = "Print",
                                             .onTap = [markComingSoon] { markComingSoon("Print"); }})),
                           toolbarDivider(),
                           toolbarGroup(children(
                               ToolbarButton{.icon = IconName::Undo,
                                             .tooltip = "Undo",
                                             .onTap = [markComingSoon] { markComingSoon("Undo"); }},
                               ToolbarButton{.icon = IconName::Redo,
                                             .tooltip = "Redo",
                                             .onTap = [markComingSoon] { markComingSoon("Redo"); }})),
                           toolbarDivider(),
                           toolbarGroup(children(
                               ToolbarButton{.icon = IconName::ContentCut,
                                             .tooltip = "Cut",
                                             .onTap = [markComingSoon] { markComingSoon("Cut toolbar action"); }},
                               ToolbarButton{.icon = IconName::ContentCopy,
                                             .tooltip = "Copy",
                                             .onTap = [markComingSoon] { markComingSoon("Copy toolbar action"); }},
                               ToolbarButton{.icon = IconName::ContentPaste,
                                             .tooltip = "Paste",
                                             .onTap = [markComingSoon] { markComingSoon("Paste toolbar action"); }},
                               ToolbarButton{.icon = IconName::Delete,
                                             .tooltip = "Delete",
                                             .onTap = [markComingSoon] { markComingSoon("Delete"); }})),
                           toolbarDivider(),
                           toolbarGroup(children(
                               ToolbarButton{.icon = IconName::Search,
                                             .tooltip = "Find",
                                             .onTap = [markComingSoon] { markComingSoon("Find"); }},
                               ToolbarButton{.icon = IconName::FindReplace,
                                             .tooltip = "Replace",
                                             .onTap = [markComingSoon] { markComingSoon("Replace"); }},
                               ToolbarButton{.icon = IconName::MoreHoriz,
                                             .tooltip = "Go To Line",
                                             .onTap = [markComingSoon] { markComingSoon("Go To Line"); }})),
                           toolbarDivider(),
                           toolbarGroup(children(
                               ToolbarButton{.icon = IconName::WrapText,
                                             .tooltip = "Word Wrap",
                                             .onTap = [markComingSoon] { markComingSoon("Word Wrap"); }},
                               ToolbarButton{.icon = IconName::ZoomOut,
                                             .tooltip = "Zoom Out",
                                             .onTap = [markComingSoon] { markComingSoon("Zoom Out"); }},
                               ToolbarButton{.icon = IconName::ZoomIn,
                                             .tooltip = "Zoom In",
                                             .onTap = [markComingSoon] { markComingSoon("Zoom In"); }},
                               ToolbarButton{.icon = IconName::TextFields,
                                             .tooltip = "Font",
                                             .onTap = [markComingSoon] { markComingSoon("Font"); }})),
                           Spacer{})}
                       .height(56.f)
                       .padding(7.f, theme().space3, 7.f, theme().space3)
                       .fill(FillStyle::solid(Color::windowBackground()))
                       .stroke(StrokeStyle::solid(Color::separator(), 1.f)),
                   ScrollView{
                       .axis = ScrollAxis::Vertical,
                       .children = children(
                           TextInput{
                               .value = text,
                               .placeholder = "Start typing...",
                               .style = editorStyle,
                               .multiline = true,
                               .multilineHeight = {.fixed = 0.f, .minIntrinsic = 560.f},
                               .onChange = [document](std::string const& value) {
                                 EditorDocument current = document.peek();
                                 current.setText(value);
                                 document.set(std::move(current));
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
                               .text = [text] { return std::to_string(text().size()) + " chars"; },
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
  window.registerAction("edit.copy", {.label = "Copy", .shortcut = shortcuts::Copy});
  window.registerAction("edit.cut", {.label = "Cut", .shortcut = shortcuts::Cut});
  window.registerAction("edit.paste", {.label = "Paste", .shortcut = shortcuts::Paste});
  window.registerAction("edit.selectAll", {.label = "Select All", .shortcut = shortcuts::SelectAll});
  window.registerAction("app.quit", {.label = "Quit", .shortcut = shortcuts::Quit, .isEnabled = [] { return true; }});
  window.setView<LambdaEditor>({
      .initialDocument = initial.ok ? std::move(initial.document) : EditorDocument::untitled(),
      .initialStatus = initial.status,
  });
  return app.exec();
}
