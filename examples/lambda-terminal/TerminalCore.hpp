#pragma once

#include <Flux/Core/Color.hpp>
#include <Flux/UI/Input.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <vector>

namespace lambda_shell {
struct AppRegistryEntry;
}

namespace lambda_terminal {

struct TerminalInputMode {
  bool applicationCursor = false;
  bool applicationKeypad = false;
  bool focusEvents = false;
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

struct TerminalProfile {
  std::string name = "default";
  std::string shell;
  std::string workingDirectory;
  std::vector<std::string> environment;
  TerminalConfig config;

  bool operator==(TerminalProfile const&) const = default;
};

struct TerminalPreferences {
  std::string defaultProfile = "default";
  std::vector<TerminalProfile> profiles;

  bool operator==(TerminalPreferences const&) const = default;
};

struct TerminalPreferencesLoadResult {
  TerminalPreferences preferences;
  std::filesystem::path path;
  std::string error;
  bool loaded = false;
  bool createdDefault = false;

  bool operator==(TerminalPreferencesLoadResult const&) const = default;
};

struct TerminalPreferencesSaveResult {
  bool ok = false;
  std::filesystem::path path;
  std::string error;

  bool operator==(TerminalPreferencesSaveResult const&) const = default;
};

struct TerminalChildCleanupResult {
  bool reaped = false;
  bool exited = false;
  bool signaled = false;
  int exitStatus = -1;
  int termSignal = 0;
  bool sentHangup = false;
  bool sentKill = false;

  constexpr bool operator==(TerminalChildCleanupResult const&) const = default;
};

struct TerminalBufferCoordinate {
  int line = 0;
  int column = 0;

  constexpr bool operator==(TerminalBufferCoordinate const&) const = default;
};

struct TerminalSelection {
  TerminalBufferCoordinate anchor;
  TerminalBufferCoordinate focus;

  constexpr bool operator==(TerminalSelection const&) const = default;
};

enum class TerminalMouseButton : std::uint8_t {
  Left,
  Middle,
  Right,
  WheelUp,
  WheelDown,
};

struct TerminalMouseEvent {
  TerminalMouseButton button = TerminalMouseButton::Left;
  bool pressed = true;
  bool motion = false;
  int column = 1;
  int row = 1;
  flux::Modifiers modifiers = flux::Modifiers::None;
};

struct TerminalSearchMatch {
  int line = 0;
  int column = 0;
  int length = 0;

  constexpr bool operator==(TerminalSearchMatch const&) const = default;
};

struct TerminalUrlMatch {
  int line = 0;
  int column = 0;
  int length = 0;
  std::string url;

  bool operator==(TerminalUrlMatch const&) const = default;
};

class TerminalTextBuffer {
public:
  explicit TerminalTextBuffer(int visibleRows = 24, int scrollbackLimit = 10'000);

  [[nodiscard]] int visibleRows() const noexcept { return visibleRows_; }
  [[nodiscard]] int scrollbackLimit() const noexcept { return scrollbackLimit_; }
  [[nodiscard]] bool alternateScreen() const noexcept { return alternateScreen_; }
  [[nodiscard]] int viewportOffset() const noexcept { return viewportOffset_; }
  [[nodiscard]] int historyLineCount() const noexcept;
  [[nodiscard]] int logicalLineCount() const noexcept;

  void resizeRows(int rows);
  void setScrollbackLimit(int limit);
  void pushLine(std::string line);
  void replaceVisibleLine(int row, std::string line);
  void enterAlternateScreen();
  void leaveAlternateScreen();
  void scrollViewport(int delta);

  [[nodiscard]] std::vector<std::string> logicalLines() const;
  [[nodiscard]] std::vector<std::string> viewportLines() const;
  [[nodiscard]] std::string selectedText(TerminalSelection selection) const;

private:
  int visibleRows_ = 24;
  int scrollbackLimit_ = 10'000;
  int viewportOffset_ = 0;
  bool alternateScreen_ = false;
  std::vector<std::string> history_;
  std::vector<std::string> visible_;
  std::vector<std::string> alternateVisible_;
};

[[nodiscard]] std::string encodeTerminalKey(flux::KeyCode key,
                                            flux::Modifiers modifiers = flux::Modifiers::None,
                                            TerminalInputMode mode = {});
[[nodiscard]] std::string encodeTerminalFocusEvent(bool focused, TerminalInputMode mode = {});
[[nodiscard]] std::string encodeTerminalKeypadKey(TerminalKeypadKey key, TerminalInputMode mode = {});
[[nodiscard]] std::string encodeBracketedPaste(std::string_view text);
[[nodiscard]] std::string terminalCopyPayload(TerminalTextBuffer const& buffer,
                                              TerminalSelection selection);
[[nodiscard]] std::string terminalPastePayload(std::string_view clipboardText,
                                               TerminalConfig const& config);
[[nodiscard]] std::string encodeSgrMouseEvent(TerminalMouseEvent event);
[[nodiscard]] TerminalBufferCoordinate terminalMouseCell(float x,
                                                        float y,
                                                        TerminalGridMetrics metrics = {});
[[nodiscard]] TerminalGridSize terminalGridSize(float contentWidth,
                                                float contentHeight,
                                                TerminalGridMetrics metrics = {});
[[nodiscard]] std::vector<TerminalSearchMatch> findTerminalText(TerminalTextBuffer const& buffer,
                                                                std::string_view query,
                                                                bool caseSensitive = false,
                                                                std::size_t limit = 1'000);
[[nodiscard]] std::vector<TerminalUrlMatch> findTerminalUrls(TerminalTextBuffer const& buffer,
                                                            std::size_t limit = 1'000);
[[nodiscard]] std::optional<std::vector<std::string>> terminalUrlOpenCommand(
    std::string_view url,
    std::vector<lambda_shell::AppRegistryEntry> const& apps);
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
[[nodiscard]] std::string writeTerminalConfigToml(TerminalConfig const& config);
[[nodiscard]] TerminalPreferences defaultTerminalPreferences();
[[nodiscard]] TerminalPreferences parseTerminalPreferencesToml(std::string_view tomlText);
[[nodiscard]] std::string writeTerminalPreferencesToml(TerminalPreferences const& preferences);
[[nodiscard]] TerminalProfile activeTerminalProfile(TerminalPreferences const& preferences);
[[nodiscard]] std::filesystem::path terminalConfigPath();
[[nodiscard]] TerminalPreferencesLoadResult loadTerminalPreferences(std::filesystem::path path = {});
[[nodiscard]] TerminalPreferencesSaveResult saveTerminalPreferences(TerminalPreferences const& preferences,
                                                                   std::filesystem::path path = {});
[[nodiscard]] TerminalChildCleanupResult cleanupTerminalChildProcess(
    pid_t pid,
    std::chrono::milliseconds hangupGrace = std::chrono::milliseconds{200});

} // namespace lambda_terminal
