#include <Flux.hpp>
#include <Flux/UI/Shortcut.hpp>
#include <Flux/UI/Theme.hpp>
#include <Flux/UI/UI.hpp>
#include <Flux/UI/Views/Views.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using namespace flux;

namespace {

struct TextFileResult {
  std::string text;
  std::string status;
  bool ok = false;
};

TextFileResult readTextFile(std::string const& pathText) {
  if (pathText.empty()) {
    return {.status = "Enter a file path to open."};
  }
  std::filesystem::path const path(pathText);
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {.status = "Could not open " + path.string()};
  }
  std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  if (!file.eof() && file.fail()) {
    return {.status = "Could not read " + path.string()};
  }
  return {.text = std::move(text), .status = "Opened " + path.string(), .ok = true};
}

TextFileResult writeTextFile(std::string const& pathText, std::string const& text) {
  if (pathText.empty()) {
    return {.status = "Enter a file path to save."};
  }
  std::filesystem::path const path(pathText);
  std::error_code ec;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      return {.status = "Could not create " + path.parent_path().string() + ": " + ec.message()};
    }
  }
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    return {.status = "Could not save " + path.string()};
  }
  file.write(text.data(), static_cast<std::streamsize>(text.size()));
  if (!file) {
    return {.status = "Could not write " + path.string()};
  }
  return {.status = "Saved " + path.string(), .ok = true};
}

std::string fileLabel(std::string const& path) {
  if (path.empty()) return "Untitled";
  return std::filesystem::path(path).filename().string();
}

struct LambdaEditor {
  std::string initialPath;
  std::string initialText;
  std::string initialStatus;

  Element body() const {
    auto theme = useEnvironment<ThemeKey>();
    auto path = useState(initialPath);
    auto text = useState(initialText);
    auto status = useState(initialStatus.empty() ? std::string{"Ready"} : initialStatus);
    auto dirty = useState(false);

    auto openFile = [path, text, status, dirty] {
      TextFileResult result = readTextFile(path.peek());
      status.set(result.status);
      if (result.ok) {
        text.set(std::move(result.text));
        dirty.set(false);
      }
    };
    auto saveFile = [path, text, status, dirty] {
      TextFileResult result = writeTextFile(path.peek(), text.peek());
      status.set(result.status);
      if (result.ok) dirty.set(false);
    };
    auto newFile = [path, text, status, dirty] {
      path.set("");
      text.set("");
      status.set("New document");
      dirty.set(false);
    };

    TextInput::Style pathStyle;
    pathStyle.font = Font{.size = 13.f, .weight = 450.f};
    pathStyle.height = 34.f;

    TextInput::Style editorStyle = TextInput::Style::plain();
    editorStyle.font = Font{.family = "monospace", .size = 14.f};
    editorStyle.textColor = Color::primary();
    editorStyle.placeholderColor = Color::secondary();
    editorStyle.paddingH = 12.f;
    editorStyle.paddingV = 12.f;
    editorStyle.lineHeight = 20.f;

    Button::Style buttonStyle;
    buttonStyle.font = Font{.size = 12.f, .weight = 650.f};
    buttonStyle.paddingH = 12.f;
    buttonStyle.paddingV = 7.f;
    buttonStyle.cornerRadius = 7.f;

    return VStack{
               .spacing = 0.f,
               .alignment = Alignment::Stretch,
               .children = children(
                   HStack{
                       .spacing = theme().space3,
                       .alignment = Alignment::Center,
                       .children = children(
                           Text{
                               .text = [path, dirty] {
                                 return fileLabel(path()) + (dirty() ? " *" : "");
                               },
                               .font = Font{.size = 15.f, .weight = 650.f},
                               .color = Color::primary(),
                               .verticalAlignment = VerticalAlignment::Center,
                           }.width(180.f),
                           TextInput{
                               .value = path,
                               .placeholder = "/path/to/file.txt",
                               .style = pathStyle,
                               .onSubmit = [openFile](std::string const&) { openFile(); },
                           }.flex(1.f, 1.f, 0.f),
                           Button{.label = "New",
                                  .variant = ButtonVariant::Secondary,
                                  .style = buttonStyle,
                                  .onTap = newFile},
                           Button{.label = "Open",
                                  .variant = ButtonVariant::Secondary,
                                  .style = buttonStyle,
                                  .onTap = openFile},
                           Button{.label = "Save",
                                  .variant = ButtonVariant::Primary,
                                  .style = buttonStyle,
                                  .onTap = saveFile})}
                       .padding(theme().space3)
                       .fill(FillStyle::solid(Color::windowBackground()))
                       .stroke(StrokeStyle::solid(Color::separator(), 1.f)),
                   TextInput{
                       .value = text,
                       .placeholder = "Start typing...",
                       .style = editorStyle,
                       .multiline = true,
                       .multilineHeight = {.fixed = 0.f, .minIntrinsic = 560.f},
                       .onChange = [dirty](std::string const&) { dirty.set(true); },
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
  TextFileResult initial = initialPath.empty() ? TextFileResult{} : readTextFile(initialPath);

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
      .initialPath = std::move(initialPath),
      .initialText = std::move(initial.text),
      .initialStatus = initial.status,
  });
  return app.exec();
}
