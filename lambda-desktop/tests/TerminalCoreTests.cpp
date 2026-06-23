#include "TerminalCore.hpp"

#include "Shell/ShellAppRegistry.hpp"

#include <Lambda/UI/KeyCodes.hpp>

#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

using namespace lambda_terminal;

namespace {

struct ScopedEnv {
  explicit ScopedEnv(char const* name)
      : name(name) {
    if (char const* value = std::getenv(name); value) {
      hadOriginal = true;
      original = value;
    }
  }

  ~ScopedEnv() {
    if (!hadOriginal) {
      unsetenv(name);
    } else {
      setenv(name, original.c_str(), 1);
    }
  }

  char const* name;
  bool hadOriginal = false;
  std::string original;
};

std::filesystem::path tempRoot(char const* name) {
  auto path = std::filesystem::temp_directory_path() /
              (std::string(name) + "-" + std::to_string(static_cast<unsigned long long>(getpid())));
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

} // namespace

TEST_CASE("terminal input encoder handles control alt navigation and functions") {
  using namespace lambdaui::keys;
  CHECK(encodeTerminalKey(C, lambdaui::Modifiers::Ctrl) == std::string("\x03", 1));
  CHECK(encodeTerminalKey(C, lambdaui::Modifiers::Ctrl | lambdaui::Modifiers::Alt) == std::string("\x1b\x03", 2));
  CHECK(encodeTerminalKey(LeftArrow) == "\x1b[D");
  CHECK(encodeTerminalKey(LeftArrow, lambdaui::Modifiers::Shift) == "\x1b[1;2D");
  CHECK(encodeTerminalKey(UpArrow, lambdaui::Modifiers::Ctrl | lambdaui::Modifiers::Alt) == "\x1b[1;7A");
  CHECK(encodeTerminalKey(F1) == "\x1bOP");
  CHECK(encodeTerminalKey(F5) == "\x1b[15~");
  CHECK(encodeTerminalKey(F12, lambdaui::Modifiers::Shift) == "\x1b[24;2~");
  CHECK(encodeTerminalKey(Tab, lambdaui::Modifiers::Shift) == "\x1b[Z");
}

TEST_CASE("terminal input encoder supports application cursor and keypad modes") {
  using namespace lambdaui::keys;
  CHECK(encodeTerminalKey(LeftArrow, lambdaui::Modifiers::None, TerminalInputMode{.applicationCursor = true}) == "\x1bOD");
  CHECK(encodeTerminalKey(Return, lambdaui::Modifiers::None, TerminalInputMode{.applicationCursor = true}) == "\r");
  CHECK(encodeTerminalKeypadKey(TerminalKeypadKey::Digit3) == "3");
  CHECK(encodeTerminalKeypadKey(TerminalKeypadKey::Digit3, TerminalInputMode{.applicationKeypad = true}) == "\x1bOs");
  CHECK(encodeTerminalKeypadKey(TerminalKeypadKey::Enter, TerminalInputMode{.applicationKeypad = true}) == "\x1bOM");
  CHECK(encodeTerminalFocusEvent(true) == "");
  CHECK(encodeTerminalFocusEvent(true, TerminalInputMode{.focusEvents = true}) == "\x1b[I");
  CHECK(encodeTerminalFocusEvent(false, TerminalInputMode{.focusEvents = true}) == "\x1b[O");
}

TEST_CASE("terminal bracketed paste wraps text without rewriting payload") {
  CHECK(encodeBracketedPaste("echo one\nsecond") == "\x1b[200~echo one\nsecond\x1b[201~");
}

TEST_CASE("terminal copy and paste payload helpers honor selection and bracketed paste policy") {
  TerminalTextBuffer buffer{2, 10};
  buffer.pushLine("alpha");
  buffer.pushLine("bravo");

  CHECK(terminalCopyPayload(buffer, TerminalSelection{
                                        .anchor = {.line = 0, .column = 1},
                                        .focus = {.line = 1, .column = 3},
                                    }) == "lpha\nbra");

  TerminalConfig bracketed = defaultTerminalConfig();
  bracketed.bracketedPaste = true;
  CHECK(terminalPastePayload("echo hi", bracketed) == "\x1b[200~echo hi\x1b[201~");

  TerminalConfig plain = bracketed;
  plain.bracketedPaste = false;
  CHECK(terminalPastePayload("echo hi", plain) == "echo hi");
  CHECK(terminalPastePayload("", plain).empty());
}

TEST_CASE("terminal clipboard shortcuts keep Ctrl+C and Ctrl+V available for programs") {
  using namespace lambdaui::keys;
  CHECK(isTerminalCopyShortcut(C, lambdaui::Modifiers::Ctrl | lambdaui::Modifiers::Shift));
  CHECK_FALSE(isTerminalCopyShortcut(C, lambdaui::Modifiers::Ctrl));
  CHECK_FALSE(isTerminalCopyShortcut(C, lambdaui::Modifiers::Meta));
  CHECK_FALSE(isTerminalCopyShortcut(C, lambdaui::Modifiers::Ctrl | lambdaui::Modifiers::Alt));
  CHECK_FALSE(isTerminalCopyShortcut(V, lambdaui::Modifiers::Ctrl | lambdaui::Modifiers::Shift));

  CHECK(isTerminalPasteShortcut(V, lambdaui::Modifiers::Ctrl | lambdaui::Modifiers::Shift));
  CHECK_FALSE(isTerminalPasteShortcut(V, lambdaui::Modifiers::Ctrl));
  CHECK_FALSE(isTerminalPasteShortcut(V, lambdaui::Modifiers::Meta));
  CHECK_FALSE(isTerminalPasteShortcut(V, lambdaui::Modifiers::Ctrl | lambdaui::Modifiers::Alt));
  CHECK_FALSE(isTerminalPasteShortcut(C, lambdaui::Modifiers::Ctrl | lambdaui::Modifiers::Shift));
}

TEST_CASE("terminal SGR mouse encoding maps buttons modifiers motion and wheel events") {
  CHECK(encodeSgrMouseEvent({
            .button = TerminalMouseButton::Left,
            .pressed = true,
            .column = 4,
            .row = 7,
        }) == "\x1b[<0;4;7M");
  CHECK(encodeSgrMouseEvent({
            .button = TerminalMouseButton::Left,
            .pressed = false,
            .column = 4,
            .row = 7,
        }) == "\x1b[<0;4;7m");
  CHECK(encodeSgrMouseEvent({
            .button = TerminalMouseButton::Right,
            .pressed = true,
            .motion = true,
            .column = 2,
            .row = 3,
            .modifiers = lambdaui::Modifiers::Shift | lambdaui::Modifiers::Ctrl,
        }) == "\x1b[<54;2;3M");
  CHECK(encodeSgrMouseEvent({
            .button = TerminalMouseButton::WheelDown,
            .pressed = true,
            .column = 1,
            .row = 1,
        }) == "\x1b[<65;1;1M");
}

TEST_CASE("terminal grid size calculation clamps to minimum") {
  CHECK(terminalGridSize(700.f, 460.f) == TerminalGridSize{.columns = 80, .rows = 24});
  CHECK(terminalGridSize(10.f, 10.f) == TerminalGridSize{.columns = 20, .rows = 6});
  CHECK(terminalMouseCell(14.f, 14.f) == TerminalBufferCoordinate{.line = 1, .column = 1});
  CHECK(terminalMouseCell(22.4f, 32.f) == TerminalBufferCoordinate{.line = 2, .column = 2});
}

TEST_CASE("terminal unicode width covers ascii combining wide emoji and invalid utf8") {
  CHECK(terminalDisplayWidth("abc") == 3);
  CHECK(terminalDisplayWidth("e\xcc\x81") == 1);
  CHECK(terminalDisplayWidth("\xe6\xbc\xa2") == 2);
  CHECK(terminalDisplayWidth("\xf0\x9f\x98\x80") == 2);
  CHECK(terminalDisplayWidth(std::string("\xff", 1)) == 1);
}

TEST_CASE("terminal indexed and truecolor conversion is deterministic") {
  auto red = terminalIndexedColor(9);
  CHECK(red.r == doctest::Approx(213.f / 255.f));
  CHECK(red.g == doctest::Approx(78.f / 255.f));
  CHECK(red.b == doctest::Approx(83.f / 255.f));

  auto cube = terminalIndexedColor(16 + 36 * 5 + 6 * 2 + 1);
  CHECK(cube.r == doctest::Approx(255.f / 255.f));
  CHECK(cube.g == doctest::Approx(135.f / 255.f));
  CHECK(cube.b == doctest::Approx(95.f / 255.f));

  auto gray = terminalIndexedColor(232);
  CHECK(gray.r == doctest::Approx(8.f / 255.f));

  auto trueColor = terminalTrueColor(12, 34, 56);
  CHECK(trueColor.r == doctest::Approx(12.f / 255.f));
  CHECK(trueColor.g == doctest::Approx(34.f / 255.f));
  CHECK(trueColor.b == doctest::Approx(56.f / 255.f));
}

TEST_CASE("terminal attributes resolve dim reverse and common style flags") {
  lambdaui::Color fg{0.8f, 0.6f, 0.4f, 1.f};
  lambdaui::Color bg{0.1f, 0.2f, 0.3f, 1.f};
  auto normal = resolveTerminalCellStyle(fg,
                                         bg,
                                         TerminalAttributes{
                                             .bold = true,
                                             .italic = true,
                                             .underline = true,
                                             .strikethrough = true,
                                         });
  CHECK(normal.foreground == fg);
  CHECK(normal.background == bg);
  CHECK(normal.attributes.bold);
  CHECK(normal.attributes.italic);
  CHECK(normal.attributes.underline);
  CHECK(normal.attributes.strikethrough);

  auto dimReverse = resolveTerminalCellStyle(fg, bg, TerminalAttributes{.dim = true, .reverse = true});
  CHECK(dimReverse.foreground == bg);
  CHECK(dimReverse.background.r == doctest::Approx(0.8f * 0.65f));
  CHECK(dimReverse.attributes.dim);
  CHECK(dimReverse.attributes.reverse);
}

TEST_CASE("terminal config parses valid values and preserves defaults for invalid values") {
  auto parsed = parseTerminalConfigToml(R"(
scrollback_limit = 5000
font_size = 15.5
cell_width = 9.0
line_height = 19.0
content_inset = 12.0
bracketed_paste = false
black_glass_background = true
black_glass_blur_radius = 64.0
black_glass_tint = "#000000cc"
)");
  CHECK(parsed.scrollbackLimit == 5000);
  CHECK(parsed.fontSize == doctest::Approx(15.5f));
  CHECK(parsed.cellWidth == doctest::Approx(9.f));
  CHECK(parsed.lineHeight == doctest::Approx(19.f));
  CHECK(parsed.contentInset == doctest::Approx(12.f));
  CHECK_FALSE(parsed.bracketedPaste);
  CHECK(parsed.blackGlassBackground);
  CHECK(parsed.blackGlassBlurRadius == doctest::Approx(64.f));
  CHECK(parsed.blackGlassTint.a == doctest::Approx(204.f / 255.f));

  auto fallback = parseTerminalConfigToml(R"(
scrollback_limit = -1
font_size = 200.0
cell_width = "wide"
bracketed_paste = "sometimes"
black_glass_tint = "#bad"
)");
  CHECK(fallback == defaultTerminalConfig());

  auto serialized = writeTerminalConfigToml(parsed);
  CHECK(parseTerminalConfigToml(serialized) == parsed);
}

TEST_CASE("terminal preferences parse profiles and select the active profile") {
  auto preferences = parseTerminalPreferencesToml(R"(
default_profile = "work"

[profile.default]
shell = "/bin/sh"
working_directory = "/tmp"
environment = ["TERM=xterm-256color", "COLORTERM=truecolor"]
scrollback_limit = 2000

[profile.default.font]
size = 13.5
cell_width = 8.0
line_height = 17.0
content_inset = 10.0

[profile.default.background]
kind = "solid"
tint = "#101112ff"

[profile.default.copy_paste]
bracketed_paste = false

[profile.work]
shell = "/bin/zsh"
scrollback_limit = 5000

[profile.work.background]
kind = "glass"
blur_radius = 64.0
tint = "#000000cc"
)");

  CHECK(preferences.defaultProfile == "work");
  REQUIRE(preferences.profiles.size() == 2);
  auto active = activeTerminalProfile(preferences);
  CHECK(active.name == "work");
  CHECK(active.shell == "/bin/zsh");
  CHECK(active.config.scrollbackLimit == 5000);
  CHECK(active.config.blackGlassBackground);
  CHECK(active.config.blackGlassBlurRadius == doctest::Approx(64.f));
  CHECK(active.config.blackGlassTint.a == doctest::Approx(204.f / 255.f));

  auto fallback = activeTerminalProfile(TerminalPreferences{.defaultProfile = "missing", .profiles = preferences.profiles});
  CHECK(fallback.name == "default");
  CHECK_FALSE(fallback.config.blackGlassBackground);
  CHECK_FALSE(fallback.config.bracketedPaste);
  CHECK(fallback.config.fontSize == doctest::Approx(13.5f));

  auto serialized = writeTerminalPreferencesToml(preferences);
  CHECK(parseTerminalPreferencesToml(serialized) == preferences);
  CHECK(parseTerminalConfigToml(serialized) == active.config);
}

TEST_CASE("terminal preferences load creates default config and respects explicit config path") {
  ScopedEnv configEnv("LAMBDA_TERMINAL_CONFIG");
  ScopedEnv xdgEnv("XDG_CONFIG_HOME");
  ScopedEnv homeEnv("HOME");
  auto root = tempRoot("lambda-terminal-config-test");
  auto configPath = root / "config.toml";
  setenv("LAMBDA_TERMINAL_CONFIG", configPath.c_str(), 1);

  CHECK(terminalConfigPath() == configPath);
  auto created = loadTerminalPreferences();
  CHECK(created.path == configPath);
  CHECK(created.createdDefault);
  CHECK(created.error.empty());
  CHECK(std::filesystem::exists(configPath));
  CHECK(activeTerminalProfile(created.preferences).name == "default");

  {
    std::ofstream(configPath) << R"(
default_profile = "alt"
[profile.alt]
shell = "/bin/fish"
[profile.alt.background]
kind = "solid"
tint = "#222222ff"
)";
  }
  auto loaded = loadTerminalPreferences();
  CHECK(loaded.loaded);
  CHECK_FALSE(loaded.createdDefault);
  CHECK(activeTerminalProfile(loaded.preferences).shell == "/bin/fish");
  CHECK_FALSE(activeTerminalProfile(loaded.preferences).config.blackGlassBackground);

  TerminalPreferences changed = loaded.preferences;
  changed.defaultProfile = "alt";
  changed.profiles[0].config.scrollbackLimit = 3210;
  changed.profiles[0].config.bracketedPaste = false;
  auto saved = saveTerminalPreferences(changed);
  REQUIRE(saved.ok);
  auto reloaded = loadTerminalPreferences();
  CHECK(reloaded.loaded);
  CHECK(activeTerminalProfile(reloaded.preferences).config.scrollbackLimit == 3210);
  CHECK_FALSE(activeTerminalProfile(reloaded.preferences).config.bracketedPaste);

  std::filesystem::remove_all(root);
}

TEST_CASE("terminal text buffer enforces scrollback limit and viewport movement") {
  TerminalTextBuffer buffer{3, 2};
  buffer.pushLine("one");
  buffer.pushLine("two");
  buffer.pushLine("three");
  buffer.pushLine("four");
  buffer.pushLine("five");

  CHECK(buffer.historyLineCount() == 2);
  CHECK(buffer.logicalLines() == std::vector<std::string>{"one", "two", "three", "four", "five"});
  CHECK(buffer.viewportLines() == std::vector<std::string>{"three", "four", "five"});

  buffer.scrollViewport(2);
  CHECK(buffer.viewportOffset() == 2);
  CHECK(buffer.viewportLines() == std::vector<std::string>{"one", "two", "three"});

  buffer.scrollViewport(50);
  CHECK(buffer.viewportOffset() == 2);
  buffer.scrollViewport(-1);
  CHECK(buffer.viewportOffset() == 1);
  CHECK(buffer.viewportLines() == std::vector<std::string>{"two", "three", "four"});

  buffer.pushLine("six");
  CHECK(buffer.viewportOffset() == 2);
  CHECK(buffer.viewportLines() == std::vector<std::string>{"two", "three", "four"});

  buffer.scrollViewport(-50);
  CHECK(buffer.viewportOffset() == 0);
  CHECK(buffer.viewportLines() == std::vector<std::string>{"four", "five", "six"});
}

TEST_CASE("terminal text buffer keeps alternate screen separate from scrollback") {
  TerminalTextBuffer buffer{2, 10};
  buffer.pushLine("normal-1");
  buffer.pushLine("normal-2");
  buffer.pushLine("normal-3");
  CHECK(buffer.historyLineCount() == 1);

  buffer.enterAlternateScreen();
  CHECK(buffer.alternateScreen());
  CHECK(buffer.historyLineCount() == 1);
  buffer.replaceVisibleLine(0, "editor-top");
  buffer.replaceVisibleLine(1, "editor-bottom");
  CHECK(buffer.viewportLines() == std::vector<std::string>{"editor-top", "editor-bottom"});
  buffer.pushLine("editor-next");
  CHECK(buffer.viewportLines() == std::vector<std::string>{"editor-bottom", "editor-next"});
  CHECK(buffer.historyLineCount() == 1);

  buffer.leaveAlternateScreen();
  CHECK_FALSE(buffer.alternateScreen());
  CHECK(buffer.viewportLines() == std::vector<std::string>{"normal-2", "normal-3"});
  CHECK(buffer.logicalLines() == std::vector<std::string>{"normal-1", "normal-2", "normal-3"});
}

TEST_CASE("terminal text buffer reconstructs selected text across lines") {
  TerminalTextBuffer buffer{3, 10};
  buffer.pushLine("alpha");
  buffer.pushLine("bravo");
  buffer.pushLine("charlie");

  CHECK(buffer.selectedText(TerminalSelection{
            .anchor = {.line = 0, .column = 1},
            .focus = {.line = 2, .column = 4},
        }) == "lpha\nbravo\nchar");

  CHECK(buffer.selectedText(TerminalSelection{
            .anchor = {.line = 2, .column = 4},
            .focus = {.line = 1, .column = 2},
        }) == "avo\nchar");

  CHECK(buffer.selectedText(TerminalSelection{
            .anchor = {.line = 1, .column = 0},
            .focus = {.line = 1, .column = 5},
        }) == "bravo");
}

TEST_CASE("terminal text buffer row resizing moves normal overflow into history") {
  TerminalTextBuffer buffer{4, 10};
  buffer.pushLine("one");
  buffer.pushLine("two");
  buffer.pushLine("three");
  buffer.pushLine("four");
  buffer.resizeRows(2);

  CHECK(buffer.visibleRows() == 2);
  CHECK(buffer.historyLineCount() == 2);
  CHECK(buffer.viewportLines() == std::vector<std::string>{"three", "four"});

  buffer.resizeRows(4);
  CHECK(buffer.visibleRows() == 4);
  CHECK(buffer.viewportLines() == std::vector<std::string>{"one", "two", "three", "four"});
}

TEST_CASE("terminal search scans visible rows and scrollback with limits") {
  TerminalTextBuffer buffer{2, 10};
  buffer.pushLine("Alpha one");
  buffer.pushLine("beta two");
  buffer.pushLine("alpha three");

  auto matches = findTerminalText(buffer, "alpha");
  REQUIRE(matches.size() == 2);
  CHECK(matches[0] == TerminalSearchMatch{.line = 0, .column = 0, .length = 5});
  CHECK(matches[1] == TerminalSearchMatch{.line = 2, .column = 0, .length = 5});

  auto sensitive = findTerminalText(buffer, "alpha", true);
  REQUIRE(sensitive.size() == 1);
  CHECK(sensitive[0].line == 2);

  auto limited = findTerminalText(buffer, "a", false, 1);
  REQUIRE(limited.size() == 1);
}

TEST_CASE("terminal URL detection extracts common http links and trims trailing punctuation") {
  TerminalTextBuffer buffer{3, 10};
  buffer.pushLine("open https://example.com/path?q=1.");
  buffer.pushLine("and http://localhost:8080/index.html)");
  buffer.pushLine("ignore ftp://example.com");

  auto urls = findTerminalUrls(buffer);
  REQUIRE(urls.size() == 2);
  CHECK(urls[0] == TerminalUrlMatch{.line = 0, .column = 5, .length = 28, .url = "https://example.com/path?q=1"});
  CHECK(urls[1] ==
        TerminalUrlMatch{.line = 1, .column = 4, .length = 32, .url = "http://localhost:8080/index.html"});
}

TEST_CASE("terminal URL open command uses shared app registry browser entries") {
  std::vector<lambda_shell::AppRegistryEntry> apps{
      {.appId = "org.mozilla.firefox", .name = "Firefox", .command = "firefox --new-tab %u"},
  };
  auto command = terminalUrlOpenCommand("https://example.com/docs", apps);
  REQUIRE(command);
  CHECK(*command == std::vector<std::string>{"firefox", "--new-tab", "https://example.com/docs"});

  std::vector<lambda_shell::AppRegistryEntry> noFieldApps{
      {.appId = "browser", .name = "Browser", .command = "browser"},
  };
  auto appended = terminalUrlOpenCommand("https://example.com/docs", noFieldApps);
  REQUIRE(appended);
  CHECK(*appended == std::vector<std::string>{"browser", "https://example.com/docs"});

  CHECK_FALSE(terminalUrlOpenCommand("ftp://example.com/file", apps));
}
