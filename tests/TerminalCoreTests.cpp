#include "TerminalCore.hpp"

#include <Flux/UI/KeyCodes.hpp>

#include <doctest/doctest.h>

using namespace lambda_terminal;

TEST_CASE("terminal input encoder handles control alt navigation and functions") {
  using namespace flux::keys;
  CHECK(encodeTerminalKey(C, flux::Modifiers::Ctrl) == std::string("\x03", 1));
  CHECK(encodeTerminalKey(C, flux::Modifiers::Ctrl | flux::Modifiers::Alt) == std::string("\x1b\x03", 2));
  CHECK(encodeTerminalKey(LeftArrow) == "\x1b[D");
  CHECK(encodeTerminalKey(LeftArrow, flux::Modifiers::Shift) == "\x1b[1;2D");
  CHECK(encodeTerminalKey(UpArrow, flux::Modifiers::Ctrl | flux::Modifiers::Alt) == "\x1b[1;7A");
  CHECK(encodeTerminalKey(F1) == "\x1bOP");
  CHECK(encodeTerminalKey(F5) == "\x1b[15~");
  CHECK(encodeTerminalKey(F12, flux::Modifiers::Shift) == "\x1b[24;2~");
  CHECK(encodeTerminalKey(Tab, flux::Modifiers::Shift) == "\x1b[Z");
}

TEST_CASE("terminal input encoder supports application cursor and keypad modes") {
  using namespace flux::keys;
  CHECK(encodeTerminalKey(LeftArrow, flux::Modifiers::None, TerminalInputMode{.applicationCursor = true}) == "\x1bOD");
  CHECK(encodeTerminalKey(Return, flux::Modifiers::None, TerminalInputMode{.applicationCursor = true}) == "\r");
  CHECK(encodeTerminalKeypadKey(TerminalKeypadKey::Digit3) == "3");
  CHECK(encodeTerminalKeypadKey(TerminalKeypadKey::Digit3, TerminalInputMode{.applicationKeypad = true}) == "\x1bOs");
  CHECK(encodeTerminalKeypadKey(TerminalKeypadKey::Enter, TerminalInputMode{.applicationKeypad = true}) == "\x1bOM");
}

TEST_CASE("terminal bracketed paste wraps text without rewriting payload") {
  CHECK(encodeBracketedPaste("echo one\nsecond") == "\x1b[200~echo one\nsecond\x1b[201~");
}

TEST_CASE("terminal grid size calculation clamps to minimum") {
  CHECK(terminalGridSize(700.f, 460.f) == TerminalGridSize{.columns = 80, .rows = 24});
  CHECK(terminalGridSize(10.f, 10.f) == TerminalGridSize{.columns = 20, .rows = 6});
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

TEST_CASE("terminal attributes resolve dim and reverse") {
  flux::Color fg{0.8f, 0.6f, 0.4f, 1.f};
  flux::Color bg{0.1f, 0.2f, 0.3f, 1.f};
  auto normal = resolveTerminalCellStyle(fg, bg, TerminalAttributes{.bold = true});
  CHECK(normal.foreground == fg);
  CHECK(normal.background == bg);
  CHECK(normal.attributes.bold);

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
black_glass_tint = "#000000cc"
)");
  CHECK(parsed.scrollbackLimit == 5000);
  CHECK(parsed.fontSize == doctest::Approx(15.5f));
  CHECK(parsed.cellWidth == doctest::Approx(9.f));
  CHECK(parsed.lineHeight == doctest::Approx(19.f));
  CHECK(parsed.contentInset == doctest::Approx(12.f));
  CHECK_FALSE(parsed.bracketedPaste);
  CHECK(parsed.blackGlassBackground);
  CHECK(parsed.blackGlassTint.a == doctest::Approx(204.f / 255.f));

  auto fallback = parseTerminalConfigToml(R"(
scrollback_limit = -1
font_size = 200.0
cell_width = "wide"
bracketed_paste = "sometimes"
black_glass_tint = "#bad"
)");
  CHECK(fallback == defaultTerminalConfig());
}
