#include "TerminalCore.hpp"

#include <Flux/UI/KeyCodes.hpp>

#include <toml++/toml.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

namespace lambda_terminal {
namespace {

using flux::KeyCode;
using flux::Modifiers;

[[nodiscard]] bool has(Modifiers modifiers, Modifiers flag) {
  return flux::any(modifiers & flag);
}

[[nodiscard]] int xtermModifierValue(Modifiers modifiers) {
  int value = 1;
  if (has(modifiers, Modifiers::Shift)) value += 1;
  if (has(modifiers, Modifiers::Alt)) value += 2;
  if (has(modifiers, Modifiers::Ctrl)) value += 4;
  if (has(modifiers, Modifiers::Meta)) value += 8;
  return value;
}

[[nodiscard]] std::string withAltPrefix(std::string sequence, Modifiers modifiers) {
  if (has(modifiers, Modifiers::Alt)) {
    sequence.insert(sequence.begin(), '\x1b');
  }
  return sequence;
}

[[nodiscard]] std::string csiModified(char final, Modifiers modifiers) {
  int const value = xtermModifierValue(modifiers);
  if (value <= 1) {
    return std::string{"\x1b["} + final;
  }
  return "\x1b[1;" + std::to_string(value) + final;
}

[[nodiscard]] std::string csiTilde(int number, Modifiers modifiers) {
  int const value = xtermModifierValue(modifiers);
  if (value <= 1) {
    return "\x1b[" + std::to_string(number) + "~";
  }
  return "\x1b[" + std::to_string(number) + ";" + std::to_string(value) + "~";
}

[[nodiscard]] std::optional<char> controlForKey(KeyCode key) {
  using namespace flux::keys;
  static constexpr std::array<std::pair<KeyCode, char>, 30> controls{{
      {A, '\x01'}, {B, '\x02'}, {C, '\x03'}, {D, '\x04'}, {E, '\x05'}, {F, '\x06'},
      {G, '\x07'}, {H, '\x08'}, {I, '\x09'}, {J, '\x0a'}, {K, '\x0b'}, {L, '\x0c'},
      {M, '\x0d'}, {N, '\x0e'}, {O, '\x0f'}, {P, '\x10'}, {Q, '\x11'}, {R, '\x12'},
      {S, '\x13'}, {T, '\x14'}, {U, '\x15'}, {V, '\x16'}, {W, '\x17'}, {X, '\x18'},
      {Y, '\x19'}, {Z, '\x1a'}, {LeftBracket, '\x1b'}, {Backslash, '\x1c'},
      {RightBracket, '\x1d'}, {Digit6, '\x1e'},
  }};
  auto found = std::find_if(controls.begin(), controls.end(), [&](auto const& entry) {
    return entry.first == key;
  });
  if (found == controls.end()) return std::nullopt;
  return found->second;
}

[[nodiscard]] bool isCombining(std::uint32_t codepoint) {
  return (codepoint >= 0x0300 && codepoint <= 0x036f) ||
         (codepoint >= 0x1ab0 && codepoint <= 0x1aff) ||
         (codepoint >= 0x1dc0 && codepoint <= 0x1dff) ||
         (codepoint >= 0x20d0 && codepoint <= 0x20ff) ||
         (codepoint >= 0xfe20 && codepoint <= 0xfe2f);
}

[[nodiscard]] bool isWide(std::uint32_t codepoint) {
  return (codepoint >= 0x1100 && codepoint <= 0x115f) ||
         codepoint == 0x2329 ||
         codepoint == 0x232a ||
         (codepoint >= 0x2e80 && codepoint <= 0xa4cf && codepoint != 0x303f) ||
         (codepoint >= 0xac00 && codepoint <= 0xd7a3) ||
         (codepoint >= 0xf900 && codepoint <= 0xfaff) ||
         (codepoint >= 0xfe10 && codepoint <= 0xfe19) ||
         (codepoint >= 0xfe30 && codepoint <= 0xfe6f) ||
         (codepoint >= 0xff00 && codepoint <= 0xff60) ||
         (codepoint >= 0xffe0 && codepoint <= 0xffe6) ||
         (codepoint >= 0x1f300 && codepoint <= 0x1f64f) ||
         (codepoint >= 0x1f900 && codepoint <= 0x1f9ff);
}

[[nodiscard]] flux::Color colorFromByte(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
  return flux::Color{
      static_cast<float>(red) / 255.f,
      static_cast<float>(green) / 255.f,
      static_cast<float>(blue) / 255.f,
      1.f,
  };
}

[[nodiscard]] std::optional<flux::Color> parseHexColor(std::string const& text) {
  if (text.size() != 9 || text.front() != '#') return std::nullopt;
  auto hex = [](char ch) -> std::optional<int> {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
    if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
    return std::nullopt;
  };
  std::array<std::uint8_t, 4> channels{};
  for (std::size_t i = 0; i < channels.size(); ++i) {
    auto high = hex(text[1u + i * 2u]);
    auto low = hex(text[2u + i * 2u]);
    if (!high || !low) return std::nullopt;
    channels[i] = static_cast<std::uint8_t>((*high << 4) | *low);
  }
  return flux::Color{
      static_cast<float>(channels[0]) / 255.f,
      static_cast<float>(channels[1]) / 255.f,
      static_cast<float>(channels[2]) / 255.f,
      static_cast<float>(channels[3]) / 255.f,
  };
}

} // namespace

TerminalTextBuffer::TerminalTextBuffer(int visibleRows, int scrollbackLimit)
    : visibleRows_(std::max(1, visibleRows))
    , scrollbackLimit_(std::max(0, scrollbackLimit)) {
}

int TerminalTextBuffer::historyLineCount() const noexcept {
  return static_cast<int>(history_.size());
}

int TerminalTextBuffer::logicalLineCount() const noexcept {
  return static_cast<int>(logicalLines().size());
}

void TerminalTextBuffer::resizeRows(int rows) {
  rows = std::max(1, rows);
  if (rows == visibleRows_) return;
  visibleRows_ = rows;
  auto& visible = alternateScreen_ ? alternateVisible_ : visible_;
  if (static_cast<int>(visible.size()) > visibleRows_) {
    if (!alternateScreen_) {
      int const overflow = static_cast<int>(visible.size()) - visibleRows_;
      history_.insert(history_.end(), visible.begin(), visible.begin() + overflow);
      visible.erase(visible.begin(), visible.begin() + overflow);
      setScrollbackLimit(scrollbackLimit_);
    } else {
      visible.erase(visible.begin(), visible.begin() + (static_cast<int>(visible.size()) - visibleRows_));
    }
  }
  scrollViewport(0);
}

void TerminalTextBuffer::setScrollbackLimit(int limit) {
  scrollbackLimit_ = std::max(0, limit);
  if (static_cast<int>(history_.size()) > scrollbackLimit_) {
    history_.erase(history_.begin(), history_.end() - scrollbackLimit_);
  }
  scrollViewport(0);
}

void TerminalTextBuffer::pushLine(std::string line) {
  auto& visible = alternateScreen_ ? alternateVisible_ : visible_;
  if (visibleRows_ <= 0) visibleRows_ = 1;
  if (static_cast<int>(visible.size()) >= visibleRows_) {
    if (!alternateScreen_) {
      history_.push_back(std::move(visible.front()));
      setScrollbackLimit(scrollbackLimit_);
    }
    visible.erase(visible.begin());
  }
  visible.push_back(std::move(line));
  viewportOffset_ = 0;
}

void TerminalTextBuffer::replaceVisibleLine(int row, std::string line) {
  if (row < 0 || row >= visibleRows_) return;
  auto& visible = alternateScreen_ ? alternateVisible_ : visible_;
  if (static_cast<int>(visible.size()) < visibleRows_) {
    visible.resize(static_cast<std::size_t>(visibleRows_));
  }
  visible[static_cast<std::size_t>(row)] = std::move(line);
}

void TerminalTextBuffer::enterAlternateScreen() {
  if (alternateScreen_) return;
  alternateScreen_ = true;
  viewportOffset_ = 0;
  alternateVisible_.assign(static_cast<std::size_t>(visibleRows_), std::string{});
}

void TerminalTextBuffer::leaveAlternateScreen() {
  if (!alternateScreen_) return;
  alternateScreen_ = false;
  viewportOffset_ = 0;
  alternateVisible_.clear();
}

void TerminalTextBuffer::scrollViewport(int delta) {
  int const maxOffset = alternateScreen_ ? 0 : static_cast<int>(history_.size());
  viewportOffset_ = std::clamp(viewportOffset_ + delta, 0, maxOffset);
}

std::vector<std::string> TerminalTextBuffer::logicalLines() const {
  if (alternateScreen_) return alternateVisible_;
  std::vector<std::string> lines;
  lines.reserve(history_.size() + visible_.size());
  lines.insert(lines.end(), history_.begin(), history_.end());
  lines.insert(lines.end(), visible_.begin(), visible_.end());
  return lines;
}

std::vector<std::string> TerminalTextBuffer::viewportLines() const {
  std::vector<std::string> lines = logicalLines();
  if (lines.empty()) return {};
  int const count = static_cast<int>(lines.size());
  int const end = std::clamp(count - viewportOffset_, 0, count);
  int const begin = std::max(0, end - visibleRows_);
  return {lines.begin() + begin, lines.begin() + end};
}

std::string TerminalTextBuffer::selectedText(TerminalSelection selection) const {
  TerminalBufferCoordinate start = selection.anchor;
  TerminalBufferCoordinate end = selection.focus;
  if (std::pair{end.line, end.column} < std::pair{start.line, start.column}) {
    std::swap(start, end);
  }

  std::vector<std::string> const lines = logicalLines();
  if (lines.empty()) return {};
  start.line = std::clamp(start.line, 0, static_cast<int>(lines.size()) - 1);
  end.line = std::clamp(end.line, 0, static_cast<int>(lines.size()) - 1);
  std::string text;
  for (int lineIndex = start.line; lineIndex <= end.line; ++lineIndex) {
    std::string const& line = lines[static_cast<std::size_t>(lineIndex)];
    int const from = lineIndex == start.line ? std::clamp(start.column, 0, static_cast<int>(line.size())) : 0;
    int const to = lineIndex == end.line ? std::clamp(end.column, 0, static_cast<int>(line.size()))
                                         : static_cast<int>(line.size());
    if (to > from) {
      text.append(line.substr(static_cast<std::size_t>(from), static_cast<std::size_t>(to - from)));
    }
    if (lineIndex != end.line) text.push_back('\n');
  }
  return text;
}

std::string encodeTerminalKey(KeyCode key, Modifiers modifiers, TerminalInputMode mode) {
  using namespace flux::keys;
  if (has(modifiers, Modifiers::Ctrl)) {
    if (auto control = controlForKey(key)) {
      std::string sequence(1, *control);
      return withAltPrefix(std::move(sequence), modifiers & Modifiers::Alt);
    }
  }

  switch (key) {
  case Return: return withAltPrefix("\r", modifiers);
  case Tab: return has(modifiers, Modifiers::Shift) ? "\x1b[Z" : withAltPrefix("\t", modifiers);
  case Delete: return withAltPrefix("\x7f", modifiers);
  case ForwardDelete: return csiTilde(3, modifiers);
  case Escape: return "\x1b";
  case LeftArrow: return xtermModifierValue(modifiers) > 1 ? csiModified('D', modifiers)
                                                           : (mode.applicationCursor ? "\x1bOD" : "\x1b[D");
  case RightArrow: return xtermModifierValue(modifiers) > 1 ? csiModified('C', modifiers)
                                                            : (mode.applicationCursor ? "\x1bOC" : "\x1b[C");
  case UpArrow: return xtermModifierValue(modifiers) > 1 ? csiModified('A', modifiers)
                                                         : (mode.applicationCursor ? "\x1bOA" : "\x1b[A");
  case DownArrow: return xtermModifierValue(modifiers) > 1 ? csiModified('B', modifiers)
                                                           : (mode.applicationCursor ? "\x1bOB" : "\x1b[B");
  case Home: return xtermModifierValue(modifiers) > 1 ? csiModified('H', modifiers) : "\x1b[H";
  case End: return xtermModifierValue(modifiers) > 1 ? csiModified('F', modifiers) : "\x1b[F";
  case PageUp: return csiTilde(5, modifiers);
  case PageDown: return csiTilde(6, modifiers);
  case F1: return xtermModifierValue(modifiers) > 1 ? csiModified('P', modifiers) : "\x1bOP";
  case F2: return xtermModifierValue(modifiers) > 1 ? csiModified('Q', modifiers) : "\x1bOQ";
  case F3: return xtermModifierValue(modifiers) > 1 ? csiModified('R', modifiers) : "\x1bOR";
  case F4: return xtermModifierValue(modifiers) > 1 ? csiModified('S', modifiers) : "\x1bOS";
  case F5: return csiTilde(15, modifiers);
  case F6: return csiTilde(17, modifiers);
  case F7: return csiTilde(18, modifiers);
  case F8: return csiTilde(19, modifiers);
  case F9: return csiTilde(20, modifiers);
  case F10: return csiTilde(21, modifiers);
  case F11: return csiTilde(23, modifiers);
  case F12: return csiTilde(24, modifiers);
  default: return {};
  }
}

std::string encodeTerminalKeypadKey(TerminalKeypadKey key, TerminalInputMode mode) {
  static constexpr std::array<char, 10> normalDigits{'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};
  static constexpr std::array<char, 10> appDigits{'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y'};
  auto digitIndex = static_cast<int>(key);
  if (digitIndex >= 0 && digitIndex <= 9) {
    return mode.applicationKeypad ? std::string{"\x1bO"} + appDigits[static_cast<std::size_t>(digitIndex)]
                                  : std::string(1, normalDigits[static_cast<std::size_t>(digitIndex)]);
  }
  switch (key) {
  case TerminalKeypadKey::Decimal: return mode.applicationKeypad ? "\x1bOn" : ".";
  case TerminalKeypadKey::Add: return mode.applicationKeypad ? "\x1bOk" : "+";
  case TerminalKeypadKey::Subtract: return mode.applicationKeypad ? "\x1bOm" : "-";
  case TerminalKeypadKey::Multiply: return mode.applicationKeypad ? "\x1bOj" : "*";
  case TerminalKeypadKey::Divide: return mode.applicationKeypad ? "\x1bOo" : "/";
  case TerminalKeypadKey::Enter: return mode.applicationKeypad ? "\x1bOM" : "\r";
  default: return {};
  }
}

std::string encodeBracketedPaste(std::string_view text) {
  std::string encoded;
  encoded.reserve(text.size() + 12u);
  encoded += "\x1b[200~";
  encoded.append(text);
  encoded += "\x1b[201~";
  return encoded;
}

TerminalGridSize terminalGridSize(float contentWidth, float contentHeight, TerminalGridMetrics metrics) {
  float const usableWidth = std::max(0.f, contentWidth - metrics.contentInset * 2.f);
  float const usableHeight = std::max(0.f, contentHeight - metrics.contentInset * 2.f);
  return TerminalGridSize{
      .columns = std::max(metrics.minColumns, static_cast<int>(std::floor(usableWidth / metrics.cellWidth))),
      .rows = std::max(metrics.minRows, static_cast<int>(std::floor(usableHeight / metrics.lineHeight))),
  };
}

std::optional<std::uint32_t> decodeFirstUtf8Codepoint(std::string_view text, std::size_t& byteLength) {
  byteLength = 0;
  if (text.empty()) return std::nullopt;
  auto const lead = static_cast<unsigned char>(text.front());
  if (lead < 0x80) {
    byteLength = 1;
    return lead;
  }
  std::size_t length = 0;
  std::uint32_t codepoint = 0;
  if ((lead & 0xe0) == 0xc0) {
    length = 2;
    codepoint = lead & 0x1f;
  } else if ((lead & 0xf0) == 0xe0) {
    length = 3;
    codepoint = lead & 0x0f;
  } else if ((lead & 0xf8) == 0xf0) {
    length = 4;
    codepoint = lead & 0x07;
  } else {
    byteLength = 1;
    return std::nullopt;
  }
  if (text.size() < length) {
    byteLength = 1;
    return std::nullopt;
  }
  for (std::size_t i = 1; i < length; ++i) {
    auto const ch = static_cast<unsigned char>(text[i]);
    if ((ch & 0xc0) != 0x80) {
      byteLength = 1;
      return std::nullopt;
    }
    codepoint = (codepoint << 6u) | (ch & 0x3fu);
  }
  bool const overlong = (length == 2 && codepoint < 0x80) ||
                        (length == 3 && codepoint < 0x800) ||
                        (length == 4 && codepoint < 0x10000);
  if (overlong || (codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint > 0x10ffff) {
    byteLength = 1;
    return std::nullopt;
  }
  byteLength = length;
  return codepoint;
}

int terminalCodepointWidth(std::uint32_t codepoint) {
  if (codepoint == 0) return 0;
  if (codepoint < 32 || (codepoint >= 0x7f && codepoint < 0xa0)) return 0;
  if (isCombining(codepoint)) return 0;
  return isWide(codepoint) ? 2 : 1;
}

int terminalDisplayWidth(std::string_view utf8) {
  int width = 0;
  std::size_t index = 0;
  while (index < utf8.size()) {
    std::size_t byteLength = 0;
    auto codepoint = decodeFirstUtf8Codepoint(utf8.substr(index), byteLength);
    if (!codepoint) {
      width += 1;
      index += std::max<std::size_t>(1u, byteLength);
      continue;
    }
    width += terminalCodepointWidth(*codepoint);
    index += byteLength;
  }
  return width;
}

flux::Color terminalIndexedColor(std::uint8_t index) {
  static constexpr std::array<std::array<std::uint8_t, 3>, 16> ansi{{
      {{0x1d, 0x24, 0x2d}}, {{0xcc, 0x66, 0x66}}, {{0x99, 0xcc, 0x99}}, {{0xf0, 0xc6, 0x74}},
      {{0x81, 0xa2, 0xbe}}, {{0xb2, 0x94, 0xbb}}, {{0x8a, 0xbe, 0xb7}}, {{0xc5, 0xc8, 0xc6}},
      {{0x66, 0x66, 0x66}}, {{0xd5, 0x4e, 0x53}}, {{0xb9, 0xca, 0x4a}}, {{0xe7, 0xc5, 0x47}},
      {{0x7a, 0xa6, 0xda}}, {{0xc3, 0x97, 0xd8}}, {{0x70, 0xc0, 0xba}}, {{0xea, 0xea, 0xea}},
  }};
  if (index < ansi.size()) {
    auto const& c = ansi[index];
    return colorFromByte(c[0], c[1], c[2]);
  }
  if (index >= 16 && index <= 231) {
    int const cube = index - 16;
    auto channel = [](int component) -> std::uint8_t {
      return static_cast<std::uint8_t>(component == 0 ? 0 : 55 + component * 40);
    };
    return colorFromByte(channel(cube / 36), channel((cube / 6) % 6), channel(cube % 6));
  }
  std::uint8_t const gray = static_cast<std::uint8_t>(8 + (index - 232) * 10);
  return colorFromByte(gray, gray, gray);
}

flux::Color terminalTrueColor(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
  return colorFromByte(red, green, blue);
}

TerminalResolvedCellStyle resolveTerminalCellStyle(flux::Color foreground,
                                                   flux::Color background,
                                                   TerminalAttributes attributes) {
  if (attributes.dim) {
    foreground = flux::Color{foreground.r * 0.65f, foreground.g * 0.65f, foreground.b * 0.65f, foreground.a};
  }
  if (attributes.reverse) {
    std::swap(foreground, background);
  }
  return TerminalResolvedCellStyle{
      .foreground = foreground,
      .background = background,
      .attributes = attributes,
  };
}

TerminalConfig defaultTerminalConfig() {
  return TerminalConfig{};
}

TerminalConfig parseTerminalConfigToml(std::string_view tomlText) {
  TerminalConfig config = defaultTerminalConfig();
  toml::table table;
  try {
    table = toml::parse(std::string(tomlText));
  } catch (...) {
    return config;
  }

  auto parseInt = [&](char const* key, std::int32_t min, std::int32_t max, std::int32_t& out) {
    if (!table.contains(key)) return;
    if (auto value = table[key].value<std::int64_t>(); value && *value >= min && *value <= max) {
      out = static_cast<std::int32_t>(*value);
    }
  };
  auto parseFloat = [&](char const* key, float min, float max, float& out) {
    if (!table.contains(key)) return;
    std::optional<double> value = table[key].value<double>();
    if (!value) {
      if (auto intValue = table[key].value<std::int64_t>()) value = static_cast<double>(*intValue);
    }
    if (value && *value >= min && *value <= max) {
      out = static_cast<float>(*value);
    }
  };
  auto parseBool = [&](char const* key, bool& out) {
    if (!table.contains(key)) return;
    if (auto value = table[key].value<bool>()) out = *value;
  };

  parseInt("scrollback_limit", 0, 1'000'000, config.scrollbackLimit);
  parseFloat("font_size", 6.f, 40.f, config.fontSize);
  parseFloat("cell_width", 3.f, 40.f, config.cellWidth);
  parseFloat("line_height", 6.f, 80.f, config.lineHeight);
  parseFloat("content_inset", 0.f, 80.f, config.contentInset);
  parseBool("bracketed_paste", config.bracketedPaste);
  parseBool("black_glass_background", config.blackGlassBackground);
  if (auto value = table["black_glass_tint"].value<std::string>()) {
    if (auto color = parseHexColor(*value)) config.blackGlassTint = *color;
  }
  return config;
}

} // namespace lambda_terminal
