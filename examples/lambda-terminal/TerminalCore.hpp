#pragma once

#include <Flux/Core/Color.hpp>
#include <Flux/UI/Input.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace lambda_terminal {

struct TerminalInputMode {
  bool applicationCursor = false;
  bool applicationKeypad = false;
};

struct TerminalGridMetrics {
  float cellWidth = 8.4f;
  float lineHeight = 18.f;
  float contentInset = 14.f;
  int minColumns = 20;
  int minRows = 6;
};

struct TerminalGridSize {
  int columns = 80;
  int rows = 24;

  constexpr bool operator==(TerminalGridSize const&) const = default;
};

enum class TerminalKeypadKey : std::uint8_t {
  Digit0,
  Digit1,
  Digit2,
  Digit3,
  Digit4,
  Digit5,
  Digit6,
  Digit7,
  Digit8,
  Digit9,
  Decimal,
  Add,
  Subtract,
  Multiply,
  Divide,
  Enter,
};

struct TerminalAttributes {
  bool bold = false;
  bool dim = false;
  bool italic = false;
  bool underline = false;
  bool reverse = false;
  bool strikethrough = false;

  constexpr bool operator==(TerminalAttributes const&) const = default;
};

struct TerminalResolvedCellStyle {
  flux::Color foreground;
  flux::Color background;
  TerminalAttributes attributes;

  constexpr bool operator==(TerminalResolvedCellStyle const&) const = default;
};

struct TerminalConfig {
  std::int32_t scrollbackLimit = 10'000;
  float fontSize = 14.f;
  float cellWidth = 8.4f;
  float lineHeight = 18.f;
  float contentInset = 14.f;
  bool bracketedPaste = true;
  bool blackGlassBackground = true;
  flux::Color blackGlassTint{0.f, 0.f, 0.f, 0.58f};

  constexpr bool operator==(TerminalConfig const&) const = default;
};

[[nodiscard]] std::string encodeTerminalKey(flux::KeyCode key,
                                            flux::Modifiers modifiers = flux::Modifiers::None,
                                            TerminalInputMode mode = {});
[[nodiscard]] std::string encodeTerminalKeypadKey(TerminalKeypadKey key, TerminalInputMode mode = {});
[[nodiscard]] std::string encodeBracketedPaste(std::string_view text);
[[nodiscard]] TerminalGridSize terminalGridSize(float contentWidth,
                                                float contentHeight,
                                                TerminalGridMetrics metrics = {});
[[nodiscard]] std::optional<std::uint32_t> decodeFirstUtf8Codepoint(std::string_view text,
                                                                    std::size_t& byteLength);
[[nodiscard]] int terminalCodepointWidth(std::uint32_t codepoint);
[[nodiscard]] int terminalDisplayWidth(std::string_view utf8);
[[nodiscard]] flux::Color terminalIndexedColor(std::uint8_t index);
[[nodiscard]] flux::Color terminalTrueColor(std::uint8_t red, std::uint8_t green, std::uint8_t blue);
[[nodiscard]] TerminalResolvedCellStyle resolveTerminalCellStyle(flux::Color foreground,
                                                                 flux::Color background,
                                                                 TerminalAttributes attributes);
[[nodiscard]] TerminalConfig defaultTerminalConfig();
[[nodiscard]] TerminalConfig parseTerminalConfigToml(std::string_view tomlText);

} // namespace lambda_terminal
