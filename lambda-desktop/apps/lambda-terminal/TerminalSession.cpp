#include <Lambda.hpp>
#include <Lambda/Graphics/TextSystem.hpp>
#include <Lambda/UI/KeyCodes.hpp>
#include <Lambda/UI/Views/Render.hpp>
#include <Lambda/UI/Window.hpp>

#include "TerminalCore.hpp"
#include "TerminalSession.hpp"

#include <vterm.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <poll.h>
#include <string>
#include <string_view>
#include <string_view>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <fcntl.h>

#if defined(__APPLE__)
#include <util.h>
#else
#include <pty.h>
#endif

#include <vector>

namespace lambda_terminal {
namespace {

using namespace lambda;

constexpr float kCellWidth = 8.4f;
constexpr float kLineHeight = 18.f;
constexpr float kContentInset = 14.f;
constexpr int kInitialRows = 24;
constexpr int kInitialCols = 80;

struct TerminalRun {
  int startCell = 0;
  int cellCount = 0;
  std::uint64_t revision = 0;
  std::string text;
  Color foreground{0.88f, 0.94f, 1.f, 0.96f};
  Color background{0.f, 0.f, 0.f, 0.f};
  std::shared_ptr<TextLayout const> layout;
  bool hasBackground = false;
  bool bold = false;
  bool italic = false;
  bool underline = false;
  bool strikethrough = false;

  bool operator==(TerminalRun const&) const = default;
};

struct TerminalRow {
  int row = 0;
  std::uint64_t revision = 0;
  std::shared_ptr<TextLayout const> layout;
  std::vector<TerminalRun> runs;

  bool operator==(TerminalRow const&) const = default;
};

struct CachedGlyph {
  bool loaded = false;
  bool valid = false;
  std::uint32_t fontId = 0;
  float fontSize = 0.f;
  std::uint32_t glyphId = 0;
  Point position{};
  float baseline = 0.f;
  float ascent = 0.f;
  float descent = 0.f;
};

std::string shellName(std::string const& shellPath) {
  std::size_t const slash = shellPath.find_last_of('/');
  return slash == std::string::npos ? shellPath : shellPath.substr(slash + 1);
}

std::optional<std::string> envString(char const* name) {
  char const* value = std::getenv(name);
  if (!value || !*value) return std::nullopt;
  return std::string(value);
}

bool envEnabled(char const* name) {
  char const* value = std::getenv(name);
  return value && *value && std::strcmp(value, "0") != 0 && std::strcmp(value, "false") != 0;
}

int envIntClamped(char const* name, int fallback, int minimum, int maximum) {
  char const* value = std::getenv(name);
  if (!value || !*value) return fallback;
  char* end = nullptr;
  long parsed = std::strtol(value, &end, 10);
  if (end == value) return fallback;
  return static_cast<int>(std::clamp(parsed, static_cast<long>(minimum), static_cast<long>(maximum)));
}

void appendUtf8(std::string& out, std::uint32_t codepoint) {
  if (codepoint == 0) {
    out.push_back(' ');
    return;
  }
  if (codepoint <= 0x7f) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7ff) {
    out.push_back(static_cast<char>(0xc0u | (codepoint >> 6u)));
    out.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
  } else if (codepoint <= 0xffff) {
    out.push_back(static_cast<char>(0xe0u | (codepoint >> 12u)));
    out.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
    out.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
  } else {
    out.push_back(static_cast<char>(0xf0u | (codepoint >> 18u)));
    out.push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3fu)));
    out.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3fu)));
    out.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
  }
}

Color terminalDefaultForeground(bool bold) {
  return bold ? Color{0.95f, 0.98f, 1.f, 1.f} : Color{0.84f, 0.90f, 0.98f, 0.96f};
}

Font terminalFont(bool bold, bool italic = false) {
  return Font{.family = "monospace", .size = 14.f, .weight = bold ? 700.f : 500.f, .italic = italic};
}

TextLayoutOptions terminalTextOptions() {
  return TextLayoutOptions{
      .wrapping = TextWrapping::NoWrap,
      .maxLines = 1,
  };
}

class TerminalSession : public std::enable_shared_from_this<TerminalSession> {
public:
  TerminalSession(Application& app, Window& window, TerminalConfig config)
      : app_(&app)
      , window_(&window)
      , config_(config)
      , rows_(std::vector<TerminalRow>{})
      , cursor_(VTermPos{0, 0}) {
    vterm_ = vterm_new(kInitialRows, kInitialCols);
    vterm_set_utf8(vterm_, 1);
    vterm_output_set_callback(vterm_, &TerminalSession::writeToPty, this);
    screen_ = vterm_obtain_screen(vterm_);
    vterm_screen_enable_altscreen(screen_, 1);
    // Reflow needs terminal-owned scrollback to be robust during rapid resizes.
    vterm_screen_enable_reflow(screen_, false);
    vterm_screen_set_damage_merge(screen_, VTERM_DAMAGE_ROW);
    vterm_screen_set_callbacks(screen_, &screenCallbacks(), this);
    vterm_screen_reset(screen_, 1);
    frameObserver_ = app_->onNextFrameNeeded([this] { flushPendingRowsForFrame(); });
    spawnShell();
    refreshRows();
  }

  ~TerminalSession() {
    if (app_ && autotestTimerId_ != 0) {
      app_->cancelTimer(autotestTimerId_);
      autotestTimerId_ = 0;
    }
    stopWakeThread();
    if (app_ && frameObserver_.isValid()) {
      app_->unobserveNextFrame(frameObserver_);
    }
    if (app_ && pollId_ != 0) {
      app_->unregisterEventPollSource(pollId_);
    }
    if (ptyFd_ >= 0) {
      close(ptyFd_);
    }
    if (childPid_ > 0) {
      (void)cleanupTerminalChildProcess(childPid_);
    }
    if (vterm_) {
      vterm_free(vterm_);
    }
  }

  Reactive::Signal<std::vector<TerminalRow>> rows() const { return rows_; }
  Reactive::Signal<VTermPos> cursor() const { return cursor_; }

  void installAutotestTextObserverFromEnv() {
    if (autotestTimerId_ != 0 || !app_ || !window_) {
      return;
    }
    std::optional<std::string> expected = envString("LAMBDA_TERMINAL_AUTOTEST_EXPECT_TEXT");
    if (!expected || expected->empty()) {
      return;
    }
    autotestExpectedText_ = std::move(*expected);
    int const timeoutMs = envIntClamped("LAMBDA_TERMINAL_AUTOTEST_TIMEOUT_MS", 5000, 100, 60000);
    autotestMaxTicks_ = std::max(1, timeoutMs / 50);
    autotestTicks_ = 0;
    autotestPasteClipboard_ = envEnabled("LAMBDA_TERMINAL_AUTOTEST_PASTE_CLIPBOARD");
    autotestPasteDispatched_ = false;
    std::weak_ptr<TerminalSession> weak = shared_from_this();
    autotestTimerId_ = app_->scheduleRepeatingTimer(std::chrono::milliseconds{50}, window_->handle());
    app_->eventQueue().on<TimerEvent>([weak](TimerEvent const& event) {
      if (std::shared_ptr<TerminalSession> session = weak.lock()) {
        session->handleAutotestTimer(event);
      }
    });
    std::fprintf(stderr,
                 "lambda-terminal-autotest: ready expect_bytes=%zu timeout_ms=%d\n",
                 autotestExpectedText_.size(),
                 timeoutMs);
    std::fflush(stderr);
  }

  void scheduleResizeForFrame(Rect frame) {
    TerminalGridSize const grid = terminalGridSize(frame.width,
                                                   frame.height,
                                                   TerminalGridMetrics{
                                                       .cellWidth = kCellWidth,
                                                       .lineHeight = kLineHeight,
                                                       .contentInset = kContentInset,
                                                       .minColumns = 20,
                                                       .minRows = 6,
                                                   });
    int const nextCols = grid.columns;
    int const nextRows = grid.rows;
    if (!pendingResize_ && nextCols == cols_ && nextRows == rowsCount_) {
      return;
    }
    if (pendingResize_ && nextCols == pendingCols_ && nextRows == pendingRowsCount_) {
      return;
    }
    pendingCols_ = nextCols;
    pendingRowsCount_ = nextRows;
    pendingResize_ = true;
    if (window_) {
      window_->requestRedraw();
    }
  }

  void sendText(std::string const& text) {
    if (text.empty() || ptyFd_ < 0) {
      return;
    }
    jumpToBottom();
    writeAll(text.data(), text.size());
  }

  void sendKey(KeyCode key, Modifiers modifiers) {
    std::string const encoded = encodeTerminalKey(key, modifiers);
    if (!encoded.empty()) {
      jumpToBottom();
    }
    writeAll(encoded.data(), encoded.size());
  }

  void pasteText(std::string_view text) {
    std::string const payload = terminalPastePayload(text, config_);
    if (!payload.empty()) {
      sendText(payload);
    }
  }

  void pasteFromClipboard() {
    if (!app_) {
      return;
    }
    if (std::optional<std::string> text = app_->clipboard().readText()) {
      pasteText(*text);
    }
  }

  void copySelectionToClipboard() {
    if (!app_ || !selection_) {
      return;
    }
    std::string text = selectedText();
    if (!text.empty()) {
      app_->clipboard().writeText(std::move(text));
    }
  }

  bool handleKeyDown(KeyCode key, Modifiers modifiers) {
    if (isTerminalCopyShortcut(key, modifiers)) {
      copySelectionToClipboard();
      return true;
    }
    if (isTerminalPasteShortcut(key, modifiers)) {
      pasteFromClipboard();
      return true;
    }
    if (key == keys::Escape && selection_) {
      selection_.reset();
      selecting_ = false;
      if (window_) {
        window_->requestRedraw();
      }
      return true;
    }
    if (any(modifiers & Modifiers::Shift) && key == keys::PageUp) {
      scrollViewportLines(std::max(1, rowsCount_ - 1));
      return true;
    }
    if (any(modifiers & Modifiers::Shift) && key == keys::PageDown) {
      scrollViewportLines(-std::max(1, rowsCount_ - 1));
      return true;
    }
    sendKey(key, modifiers);
    return true;
  }

  void drawTerminal(Canvas& canvas, Rect frame) {
    scheduleResizeForFrame(frame);
    canvas.save();
    canvas.clipRect(frame);
    std::vector<TerminalRow> const& rows = rows_.evaluate();
    for (TerminalRow const& row : rows) {
      float const y = frame.y + kContentInset + static_cast<float>(row.row) * kLineHeight;
      for (TerminalRun const& run : row.runs) {
        Rect const runRect{
            frame.x + kContentInset + static_cast<float>(run.startCell) * kCellWidth,
            y,
            static_cast<float>(run.cellCount) * kCellWidth,
            kLineHeight,
        };
        if (run.hasBackground) {
          canvas.drawRect(runRect, CornerRadius{}, FillStyle::solid(run.background), StrokeStyle::none());
        }
      }
      drawSelectionForRow(canvas, frame, row.row);
      if (row.layout) {
        canvas.drawTextLayout(*row.layout, Point{frame.x + kContentInset, y});
      } else {
        for (TerminalRun const& run : row.runs) {
          Rect const runRect{
              frame.x + kContentInset + static_cast<float>(run.startCell) * kCellWidth,
              y,
              static_cast<float>(run.cellCount) * kCellWidth,
              kLineHeight,
          };
          if (run.layout) {
            canvas.drawTextLayout(*run.layout, Point{runRect.x, runRect.y});
          }
        }
      }
      for (TerminalRun const& run : row.runs) {
        Rect const runRect{
            frame.x + kContentInset + static_cast<float>(run.startCell) * kCellWidth,
            y,
            static_cast<float>(run.cellCount) * kCellWidth,
            kLineHeight,
        };
        drawRunDecorations(canvas, run, runRect);
      }
    }
    if (viewportOffset_ == 0) {
      drawCursor(canvas, frame);
    }
    canvas.restore();
  }

  void scrollViewport(Vec2 delta) {
    if (alternateScreen_ || delta.y == 0.f) {
      return;
    }
    int const lines = std::max(1, static_cast<int>(std::ceil(std::abs(delta.y) / 40.f)));
    scrollViewportLines(delta.y > 0.f ? lines : -lines);
  }

  void beginSelection(Point point, MouseButton button) {
    if (button != MouseButton::Left) {
      return;
    }
    TerminalBufferCoordinate const coord = coordinateForPoint(point);
    selection_ = TerminalSelection{.anchor = coord, .focus = coord};
    selecting_ = true;
    if (window_) {
      window_->requestRedraw();
    }
  }

  void updateSelection(Point point) {
    if (!selecting_ || !selection_) {
      return;
    }
    selection_->focus = coordinateForPoint(point);
    if (window_) {
      window_->requestRedraw();
    }
  }

  void endSelection(Point point, MouseButton button) {
    if (button != MouseButton::Left || !selecting_) {
      return;
    }
    selecting_ = false;
    if (selection_) {
      selection_->focus = coordinateForPoint(point);
      if (selection_->anchor == selection_->focus) {
        selection_.reset();
      }
    }
    if (window_) {
      window_->requestRedraw();
    }
  }

  void drawRunDecorations(Canvas& canvas, TerminalRun const& run, Rect runRect) const {
    if (!run.underline && !run.strikethrough) {
      return;
    }
    Color color = run.foreground;
    color.a = std::min(1.f, color.a * 0.85f);
    if (run.underline) {
      canvas.drawRect(Rect{runRect.x, runRect.y + kLineHeight - 3.f, runRect.width, 1.f},
                      CornerRadius{},
                      FillStyle::solid(color),
                      StrokeStyle::none());
    }
    if (run.strikethrough) {
      canvas.drawRect(Rect{runRect.x, runRect.y + kLineHeight * 0.54f, runRect.width, 1.f},
                      CornerRadius{},
                      FillStyle::solid(color),
                      StrokeStyle::none());
    }
  }

  void drawSelectionForRow(Canvas& canvas, Rect frame, int row) const {
    if (!selection_) {
      return;
    }
    int const logicalLine = logicalLineForVisualRow(row);
    TerminalSelection selection = *selection_;
    TerminalBufferCoordinate start = selection.anchor;
    TerminalBufferCoordinate end = selection.focus;
    if (std::pair{end.line, end.column} < std::pair{start.line, start.column}) {
      std::swap(start, end);
    }
    if (logicalLine < start.line || logicalLine > end.line) {
      return;
    }
    int from = logicalLine == start.line ? start.column : 0;
    int to = logicalLine == end.line ? end.column : cols_;
    from = std::clamp(from, 0, cols_);
    to = std::clamp(to, 0, cols_);
    if (to <= from) {
      return;
    }
    Rect const rect{
        frame.x + kContentInset + static_cast<float>(from) * kCellWidth,
        frame.y + kContentInset + static_cast<float>(row) * kLineHeight,
        static_cast<float>(to - from) * kCellWidth,
        kLineHeight,
    };
    canvas.drawRect(rect, CornerRadius{}, FillStyle::solid(Color{0.24f, 0.48f, 1.f, 0.34f}),
                    StrokeStyle::none());
  }

  void drawCursor(Canvas& canvas, Rect frame) {
    VTermPos const cursor = cursor_.evaluate();
    if (cursor.row >= 0 && cursor.row < rowsCount_ && cursor.col >= 0 && cursor.col < cols_) {
      Rect const cursorRect{
          frame.x + kContentInset + static_cast<float>(cursor.col) * kCellWidth,
          frame.y + kContentInset + static_cast<float>(cursor.row) * kLineHeight + 2.f,
          kCellWidth,
          kLineHeight - 3.f};
      canvas.drawRect(cursorRect, CornerRadius{2.f}, FillStyle::solid(Color{0.80f, 0.92f, 1.f, 0.72f}),
                      StrokeStyle::none());
    }
  }

private:
  int logicalLineCount() const {
    return static_cast<int>(historyRows_.size()) + static_cast<int>(screenRows_.size());
  }

  int maxViewportOffset() const {
    return alternateScreen_ ? 0 : static_cast<int>(historyRows_.size());
  }

  int viewportLogicalStart() const {
    int const count = logicalLineCount();
    if (count <= 0) {
      return 0;
    }
    int const end = std::clamp(count - viewportOffset_, 0, count);
    return std::max(0, end - rowsCount_);
  }

  int logicalLineForVisualRow(int visualRow) const {
    return viewportLogicalStart() + std::clamp(visualRow, 0, std::max(0, rowsCount_ - 1));
  }

  void scrollViewportLines(int delta) {
    if (alternateScreen_ || delta == 0) {
      return;
    }
    int const nextOffset = std::clamp(viewportOffset_ + delta, 0, maxViewportOffset());
    if (nextOffset == viewportOffset_) {
      return;
    }
    viewportOffset_ = nextOffset;
    publishViewportRows();
  }

  void jumpToBottom() {
    if (viewportOffset_ == 0) {
      return;
    }
    viewportOffset_ = 0;
    publishViewportRows();
  }

  std::vector<TerminalRow> logicalRows() const {
    std::vector<TerminalRow> rows;
    rows.reserve(historyRows_.size() + screenRows_.size());
    rows.insert(rows.end(), historyRows_.begin(), historyRows_.end());
    rows.insert(rows.end(), screenRows_.begin(), screenRows_.end());
    return rows;
  }

  void publishViewportRows() {
    std::vector<TerminalRow> visible;
    visible.reserve(static_cast<std::size_t>(rowsCount_));
    int const count = logicalLineCount();
    int const end = std::clamp(count - viewportOffset_, 0, count);
    int const begin = std::max(0, end - rowsCount_);
    for (int index = begin; index < end; ++index) {
      TerminalRow const* source = logicalRowAt(index);
      if (!source) {
        continue;
      }
      TerminalRow row = *source;
      row.row = static_cast<int>(visible.size());
      if (!rowHasPreparedLayout(row)) {
        prepareRowForDisplay(row);
      }
      visible.push_back(std::move(row));
    }
    if (visible == rows_.peek()) {
      return;
    }
    rows_.set(std::move(visible));
    if (window_) {
      window_->requestRedraw();
    }
  }

  TerminalRow const* logicalRowAt(int index) const {
    int const historyCount = static_cast<int>(historyRows_.size());
    if (index >= 0 && index < historyCount) {
      return &historyRows_[static_cast<std::size_t>(index)];
    }
    int const screenIndex = index - historyCount;
    if (screenIndex >= 0 && screenIndex < static_cast<int>(screenRows_.size())) {
      return &screenRows_[static_cast<std::size_t>(screenIndex)];
    }
    return nullptr;
  }

  void appendHistoryRow(TerminalRow row) {
    bool const pinnedToBottom = viewportOffset_ == 0;
    row.row = static_cast<int>(historyRows_.size());
    historyRows_.push_back(std::move(row));
    int const limit = std::max(0, config_.scrollbackLimit);
    if (static_cast<int>(historyRows_.size()) > limit) {
      int const excess = static_cast<int>(historyRows_.size()) - limit;
      historyRows_.erase(historyRows_.begin(), historyRows_.begin() + excess);
      if (selection_) {
        selection_->anchor.line = std::max(0, selection_->anchor.line - excess);
        selection_->focus.line = std::max(0, selection_->focus.line - excess);
      }
    } else if (!pinnedToBottom) {
      viewportOffset_ = std::clamp(viewportOffset_ + 1, 0, maxViewportOffset());
    }
    if (pinnedToBottom) {
      viewportOffset_ = 0;
    }
    if (pinnedToBottom && static_cast<int>(screenRows_.size()) >= rowsCount_) {
      return;
    }
    publishViewportRows();
  }

  static bool rowHasPreparedLayout(TerminalRow const& row) {
    if (row.runs.empty()) {
      return true;
    }
    if (row.layout) {
      return true;
    }
    return std::all_of(row.runs.begin(), row.runs.end(), [](TerminalRun const& run) {
      return run.layout != nullptr;
    });
  }

  TerminalBufferCoordinate coordinateForPoint(Point point) const {
    TerminalBufferCoordinate const cell = terminalMouseCell(point.x,
                                                           point.y,
                                                           TerminalGridMetrics{
                                                               .cellWidth = kCellWidth,
                                                               .lineHeight = kLineHeight,
                                                               .contentInset = kContentInset,
                                                               .minColumns = 20,
                                                               .minRows = 6,
                                                           });
    int const visualLine = std::clamp(cell.line - 1, 0, std::max(0, rowsCount_ - 1));
    return TerminalBufferCoordinate{
        .line = std::clamp(logicalLineForVisualRow(visualLine), 0, std::max(0, logicalLineCount() - 1)),
        .column = std::clamp(cell.column - 1, 0, cols_),
    };
  }

  static std::string textForRow(TerminalRow const& row) {
    std::string text;
    for (TerminalRun const& run : row.runs) {
      int const start = std::max(0, run.startCell);
      if (static_cast<int>(text.size()) < start) {
        text.resize(static_cast<std::size_t>(start), ' ');
      }
      text.append(run.text);
    }
    while (!text.empty() && text.back() == ' ') {
      text.pop_back();
    }
    return text;
  }

  std::string selectedText() const {
    if (!selection_) {
      return {};
    }
    TerminalTextBuffer buffer{std::max(1, logicalLineCount()), config_.scrollbackLimit};
    for (TerminalRow const& row : logicalRows()) {
      buffer.pushLine(textForRow(row));
    }
    return terminalCopyPayload(buffer, *selection_);
  }

  std::string terminalTextForAutotest() const {
    std::string text;
    for (TerminalRow const& row : logicalRows()) {
      if (!text.empty()) {
        text.push_back('\n');
      }
      text.append(textForRow(row));
    }
    return text;
  }

  void handleAutotestTimer(TimerEvent const& event) {
    if (autotestTimerId_ == 0 || event.timerId != autotestTimerId_) {
      return;
    }
    if (autotestPasteClipboard_ && !autotestPasteDispatched_ && autotestTicks_ >= 4) {
      autotestPasteDispatched_ = true;
      pasteFromClipboard();
      std::fprintf(stderr, "lambda-terminal-autotest: paste-dispatched\n");
      std::fflush(stderr);
    }
    if (hasDirtyRows()) {
      refreshDirtyRows();
    }
    std::string const observed = terminalTextForAutotest();
    if (observed.find(autotestExpectedText_) != std::string::npos) {
      std::fprintf(stderr,
                   "lambda-terminal-autotest: observed-text bytes=%zu ticks=%d\n",
                   autotestExpectedText_.size(),
                   autotestTicks_);
      std::fflush(stderr);
      app_->cancelTimer(autotestTimerId_);
      autotestTimerId_ = 0;
      app_->quit();
      return;
    }
    ++autotestTicks_;
    if (autotestTicks_ >= autotestMaxTicks_) {
      std::fprintf(stderr,
                   "lambda-terminal-autotest: timeout expect_bytes=%zu observed_bytes=%zu\n",
                   autotestExpectedText_.size(),
                   observed.size());
      std::fflush(stderr);
      app_->cancelTimer(autotestTimerId_);
      autotestTimerId_ = 0;
      app_->quit();
    }
  }

  static VTermScreenCallbacks const& screenCallbacks() {
    static VTermScreenCallbacks callbacks{
        .damage = &TerminalSession::onDamage,
        .moverect = nullptr,
        .movecursor = &TerminalSession::onMoveCursor,
        .settermprop = &TerminalSession::onTermProp,
        .bell = nullptr,
        .resize = nullptr,
        .sb_pushline = &TerminalSession::onScrollbackPushLine,
        .sb_popline = nullptr,
        .sb_clear = &TerminalSession::onScrollbackClear,
    };
    return callbacks;
  }

  static int onDamage(VTermRect rect, void* user) {
    static_cast<TerminalSession*>(user)->markRowsDirty(rect.start_row, rect.end_row);
    return 1;
  }

  static int onMoveCursor(VTermPos pos, VTermPos, int, void* user) {
    auto* session = static_cast<TerminalSession*>(user);
    session->cursor_.set(pos);
    if (session->window_) {
      session->window_->requestRedraw();
    }
    return 1;
  }

  static int onTermProp(VTermProp prop, VTermValue* value, void* user) {
    auto* session = static_cast<TerminalSession*>(user);
    if (prop == VTERM_PROP_ALTSCREEN && value) {
      session->alternateScreen_ = value->boolean != 0;
      session->viewportOffset_ = 0;
      session->publishViewportRows();
      return 1;
    }
    if (prop != VTERM_PROP_TITLE || !value || !value->string.str) {
      return 1;
    }
    if (session->window_) {
      session->window_->setTitle(std::string(value->string.str, value->string.len));
    }
    return 1;
  }

  static int onScrollbackPushLine(int cols, VTermScreenCell const* cells, void* user) {
    auto* session = static_cast<TerminalSession*>(user);
    if (!session->alternateScreen_) {
      session->appendHistoryRow(session->buildRowFromCells(0, cols, cells));
    }
    return 1;
  }

  static int onScrollbackClear(void* user) {
    auto* session = static_cast<TerminalSession*>(user);
    session->historyRows_.clear();
    session->viewportOffset_ = 0;
    session->selection_.reset();
    session->publishViewportRows();
    return 1;
  }

  static void writeToPty(char const* bytes, std::size_t len, void* user) {
    static_cast<TerminalSession*>(user)->writeAll(bytes, len);
  }

  Color colorFromVTerm(VTermColor color, bool foreground, bool bold) const {
    if ((color.type & VTERM_COLOR_DEFAULT_MASK) != 0) {
      return foreground ? terminalDefaultForeground(bold) : Color{0.f, 0.f, 0.f, 0.f};
    }
    vterm_screen_convert_color_to_rgb(screen_, &color);
    float const scale = 1.f / 255.f;
    float const multiplier = foreground && bold ? 1.12f : 1.f;
    return Color{
        std::min(1.f, static_cast<float>(color.rgb.red) * scale * multiplier),
        std::min(1.f, static_cast<float>(color.rgb.green) * scale * multiplier),
        std::min(1.f, static_cast<float>(color.rgb.blue) * scale * multiplier),
        1.f};
  }

  void spawnShell() {
    std::string const shellPath = std::getenv("SHELL") ? std::getenv("SHELL") : "/bin/sh";
    std::string const name = shellName(shellPath);
    winsize size{};
    size.ws_col = static_cast<unsigned short>(cols_);
    size.ws_row = static_cast<unsigned short>(rowsCount_);
    childPid_ = forkpty(&ptyFd_, nullptr, nullptr, &size);
    if (childPid_ < 0) {
      quitAfterShellExit();
      return;
    }
    if (childPid_ == 0) {
      setenv("TERM", "xterm-256color", 1);
      setenv("COLORTERM", "truecolor", 1);
      execl(shellPath.c_str(), name.c_str(), static_cast<char*>(nullptr));
      _exit(127);
    }
    int const flags = fcntl(ptyFd_, F_GETFL, 0);
    if (flags >= 0) {
      fcntl(ptyFd_, F_SETFL, flags | O_NONBLOCK);
    }
    pollId_ = app_->registerEventPollSource(ptyFd_, [this] { readFromPty(); });
    startWakeThread();
  }

  void readFromPty() {
    if (ptyFd_ < 0) {
      return;
    }
    std::array<char, 8192> buffer{};
    bool readAny = false;
    for (;;) {
      ssize_t const n = read(ptyFd_, buffer.data(), buffer.size());
      if (n > 0) {
        readAny = true;
        vterm_input_write(vterm_, buffer.data(), static_cast<std::size_t>(n));
        continue;
      }
      if (n == 0) {
        quitAfterShellExit();
        return;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      }
      if (errno != EINTR) {
        quitAfterShellExit();
        return;
      }
    }
    wakeQueued_.store(false, std::memory_order_release);
    if (readAny) {
      vterm_screen_flush_damage(screen_);
      if (!hasDirtyRows()) {
        markAllRowsDirty();
      }
      if (window_) {
        window_->requestRedraw();
      }
    }
  }

  void writeAll(char const* data, std::size_t size) {
    if (ptyFd_ < 0) {
      return;
    }
    std::size_t written = 0;
    while (written < size) {
      ssize_t const n = write(ptyFd_, data + written, size - written);
      if (n > 0) {
        written += static_cast<std::size_t>(n);
        continue;
      }
      if (n < 0 && errno == EINTR) {
        continue;
      }
      break;
    }
  }

  void refreshRows() {
    markAllRowsDirty();
    refreshDirtyRows();
  }

  void flushPendingRowsForFrame() {
    applyPendingResize();
    if (!hasDirtyRows()) {
      return;
    }
    refreshDirtyRows();
  }

  void applyPendingResize() {
    if (!pendingResize_) {
      return;
    }

    pendingResize_ = false;
    int const nextRows = pendingRowsCount_;
    int const nextCols = pendingCols_;
    if (nextCols == cols_ && nextRows == rowsCount_) {
      return;
    }

    cols_ = nextCols;
    rowsCount_ = nextRows;
    viewportOffset_ = std::clamp(viewportOffset_, 0, maxViewportOffset());
    vterm_set_size(vterm_, rowsCount_, cols_);
    if (ptyFd_ >= 0) {
      winsize size{};
      size.ws_col = static_cast<unsigned short>(cols_);
      size.ws_row = static_cast<unsigned short>(rowsCount_);
      ioctl(ptyFd_, TIOCSWINSZ, &size);
    }
    vterm_screen_flush_damage(screen_);
    markAllRowsDirty();

    VTermPos cursor{};
    vterm_state_get_cursorpos(vterm_obtain_state(vterm_), &cursor);
    cursor.row = std::clamp(cursor.row, 0, rowsCount_ - 1);
    cursor.col = std::clamp(cursor.col, 0, cols_ - 1);
    VTermPos const previous = cursor_.peek();
    if (cursor.row != previous.row || cursor.col != previous.col) {
      cursor_.set(cursor);
    }
  }

  void markRowsDirty(int startRow, int endRow) {
    startRow = std::clamp(startRow, 0, rowsCount_);
    endRow = std::clamp(endRow, 0, rowsCount_);
    if (endRow <= startRow) {
      return;
    }
    dirtyStartRow_ = std::min(dirtyStartRow_, startRow);
    dirtyEndRow_ = std::max(dirtyEndRow_, endRow);
  }

  void markAllRowsDirty() {
    dirtyStartRow_ = 0;
    dirtyEndRow_ = rowsCount_;
  }

  bool hasDirtyRows() const { return dirtyStartRow_ >= 0 && dirtyEndRow_ > dirtyStartRow_; }

  void refreshDirtyRows() {
    if (!hasDirtyRows()) {
      return;
    }

    int startRow = dirtyStartRow_;
    int endRow = dirtyEndRow_;
    dirtyStartRow_ = rowsCount_;
    dirtyEndRow_ = -1;

    std::vector<TerminalRow> next = screenRows_;
    bool changed = false;
    if (static_cast<int>(next.size()) != rowsCount_) {
      next.resize(static_cast<std::size_t>(rowsCount_));
      startRow = 0;
      endRow = rowsCount_;
      changed = true;
    }

    startRow = std::clamp(startRow, 0, rowsCount_);
    endRow = std::clamp(endRow, 0, rowsCount_);
    for (int row = startRow; row < endRow; ++row) {
      TerminalRow built = buildRow(row);
      if (sameRowContent(next[static_cast<std::size_t>(row)], built)) {
        continue;
      }

      prepareRowForDisplay(built);
      next[static_cast<std::size_t>(row)] = std::move(built);
      changed = true;
    }

    if (!changed) {
      return;
    }
    screenRows_ = std::move(next);
    publishViewportRows();
  }

  TerminalRow buildRow(int row) const {
    std::vector<VTermScreenCell> cells(static_cast<std::size_t>(cols_));
    for (int col = 0; col < cols_; ++col) {
      if (!vterm_screen_get_cell(screen_, VTermPos{row, col}, &cells[static_cast<std::size_t>(col)])) {
        VTermScreenCell& cell = cells[static_cast<std::size_t>(col)];
        cell.chars[0] = ' ';
        cell.width = 1;
        cell.fg.type = VTERM_COLOR_DEFAULT_FG;
        cell.bg.type = VTERM_COLOR_DEFAULT_BG;
      }
    }
    return buildRowFromCells(row, cols_, cells.data());
  }

  TerminalRow buildRowFromCells(int row, int cols, VTermScreenCell const* cells) const {
    TerminalRow terminalRow{.row = row, .revision = 0, .runs = {}};
    TerminalRun current;
    bool hasCurrent = false;
    for (int col = 0; col < cols; ++col) {
      VTermScreenCell cell = cells ? cells[col] : VTermScreenCell{};
      if (cell.width <= 0) {
        cell.width = 1;
      }
      bool const bold = cell.attrs.bold != 0;
      bool const italic = cell.attrs.italic != 0;
      bool const underline = cell.attrs.underline != 0;
      bool const strikethrough = cell.attrs.strike != 0;
      VTermColor fg = cell.fg;
      VTermColor bg = cell.bg;
      if (cell.attrs.reverse) {
        std::swap(fg, bg);
      }
      Color const foreground = colorFromVTerm(fg, true, bold);
      Color const background = colorFromVTerm(bg, false, false);
      bool const hasBackground = background.a > 0.f;
      bool const sameRun = hasCurrent &&
                           current.bold == bold &&
                           current.italic == italic &&
                           current.underline == underline &&
                           current.strikethrough == strikethrough &&
                           current.hasBackground == hasBackground &&
                           current.foreground == foreground &&
                           current.background == background;
      if (!sameRun) {
        if (hasCurrent) {
          terminalRow.runs.push_back(std::move(current));
        }
        current = TerminalRun{
            .startCell = col,
            .cellCount = 0,
            .revision = 0,
            .text = {},
            .foreground = foreground,
            .background = background,
            .hasBackground = hasBackground,
            .bold = bold,
            .italic = italic,
            .underline = underline,
            .strikethrough = strikethrough,
        };
        hasCurrent = true;
      }
      appendUtf8(current.text, cell.chars[0]);
      current.cellCount += std::max(1, static_cast<int>(cell.width));
    }
    if (hasCurrent) {
      terminalRow.runs.push_back(std::move(current));
    }
    return terminalRow;
  }

  void prepareRowForDisplay(TerminalRow& row) {
    std::uint64_t const revision = ++revision_;
    row.revision = revision;
    row.layout.reset();
    if (isFastAsciiRow(row)) {
      row.layout = layoutForRow(row);
      for (auto& run : row.runs) {
        run.revision = revision;
        run.layout.reset();
      }
      return;
    }
    for (auto& run : row.runs) {
      run.revision = revision;
      run.layout = layoutForRun(run);
    }
  }

  std::shared_ptr<TextLayout const> layoutForRun(TerminalRun const& run) const {
    if (!app_ || run.text.empty()) {
      return nullptr;
    }
    if (isFastAscii(run.text) && !run.italic) {
      return asciiLayoutForRun(run);
    }
    return app_->textSystem().layout(run.text, terminalFont(run.bold, run.italic), run.foreground, 0.f, terminalTextOptions());
  }

  std::shared_ptr<TextLayout const> asciiLayoutForRun(TerminalRun const& run) const {
    auto layout = std::make_shared<TextLayout>();
    auto storage = std::make_unique<TextLayoutStorage>();
    storage->glyphArena.reserve(run.text.size());
    storage->positionArena.reserve(run.text.size());

    CachedGlyph metrics{};
    bool haveMetrics = false;
    for (std::size_t i = 0; i < run.text.size(); ++i) {
      unsigned char const ch = static_cast<unsigned char>(run.text[i]);
      CachedGlyph const glyph = glyphForAscii(ch, run.bold);
      if (!glyph.valid) {
        continue;
      }
      if (!haveMetrics) {
        metrics = glyph;
        haveMetrics = true;
      }
      storage->glyphArena.push_back(glyph.glyphId);
      storage->positionArena.push_back(Point{static_cast<float>(i) * kCellWidth + glyph.position.x,
                                             glyph.position.y});
    }

    if (haveMetrics && !storage->glyphArena.empty()) {
      TextLayout::PlacedRun placed{};
      placed.run.fontId = metrics.fontId;
      placed.run.fontSize = metrics.fontSize;
      placed.run.color = run.foreground;
      placed.run.glyphIds = std::span<std::uint32_t const>(storage->glyphArena.data(),
                                                           storage->glyphArena.size());
      placed.run.positions = std::span<Point const>(storage->positionArena.data(),
                                                    storage->positionArena.size());
      placed.run.ascent = metrics.ascent;
      placed.run.descent = metrics.descent;
      placed.run.width = static_cast<float>(run.cellCount) * kCellWidth;
      placed.origin = {0.f, metrics.baseline};
      placed.utf8Begin = 0;
      placed.utf8End = static_cast<std::uint32_t>(run.text.size());
      placed.ctLineIndex = 0;
      layout->runs.push_back(placed);
      layout->firstBaseline = metrics.baseline;
      layout->lastBaseline = metrics.baseline;
    }

    layout->lines.push_back(TextLayout::LineRange{
        .ctLineIndex = 0,
        .byteStart = 0,
        .byteEnd = static_cast<int>(run.text.size()),
        .lineMinX = 0.f,
        .top = 0.f,
        .bottom = kLineHeight,
        .baseline = haveMetrics ? metrics.baseline : 0.f,
    });
    layout->measuredSize = {static_cast<float>(run.cellCount) * kCellWidth, kLineHeight};
    layout->ownedStorage = std::move(storage);
    return layout;
  }

  std::shared_ptr<TextLayout const> layoutForRow(TerminalRow const& row) const {
    auto layout = std::make_shared<TextLayout>();
    auto storage = std::make_unique<TextLayoutStorage>();

    std::size_t totalGlyphs = 0;
    float width = 0.f;
    for (TerminalRun const& run : row.runs) {
      totalGlyphs += run.text.size();
      width = std::max(width, static_cast<float>(run.startCell + run.cellCount) * kCellWidth);
    }
    storage->glyphArena.reserve(totalGlyphs);
    storage->positionArena.reserve(totalGlyphs);

    float baseline = 0.f;
    bool haveBaseline = false;
    std::uint32_t byteOffset = 0;
    for (TerminalRun const& run : row.runs) {
      CachedGlyph metrics{};
      bool haveMetrics = false;
      std::size_t const glyphStart = storage->glyphArena.size();
      std::size_t const positionStart = storage->positionArena.size();
      for (std::size_t i = 0; i < run.text.size(); ++i) {
        unsigned char const ch = static_cast<unsigned char>(run.text[i]);
        CachedGlyph const glyph = glyphForAscii(ch, run.bold);
        if (!glyph.valid) {
          continue;
        }
        if (!haveMetrics) {
          metrics = glyph;
          haveMetrics = true;
          if (!haveBaseline) {
            baseline = glyph.baseline;
            haveBaseline = true;
          }
        }
        storage->glyphArena.push_back(glyph.glyphId);
        storage->positionArena.push_back(Point{static_cast<float>(i) * kCellWidth + glyph.position.x,
                                               glyph.position.y});
      }

      std::size_t const glyphCount = storage->glyphArena.size() - glyphStart;
      if (haveMetrics && glyphCount > 0) {
        TextLayout::PlacedRun placed{};
        placed.run.fontId = metrics.fontId;
        placed.run.fontSize = metrics.fontSize;
        placed.run.color = run.foreground;
        placed.run.glyphIds = std::span<std::uint32_t const>(storage->glyphArena.data() + glyphStart, glyphCount);
        placed.run.positions = std::span<Point const>(storage->positionArena.data() + positionStart, glyphCount);
        placed.run.ascent = metrics.ascent;
        placed.run.descent = metrics.descent;
        placed.run.width = width;
        placed.origin = {static_cast<float>(run.startCell) * kCellWidth, metrics.baseline};
        placed.utf8Begin = byteOffset;
        placed.utf8End = byteOffset + static_cast<std::uint32_t>(run.text.size());
        placed.ctLineIndex = 0;
        layout->runs.push_back(placed);
      }
      byteOffset += static_cast<std::uint32_t>(run.text.size());
    }

    layout->lines.push_back(TextLayout::LineRange{
        .ctLineIndex = 0,
        .byteStart = 0,
        .byteEnd = static_cast<int>(byteOffset),
        .lineMinX = 0.f,
        .top = 0.f,
        .bottom = kLineHeight,
        .baseline = baseline,
    });
    layout->measuredSize = {width, kLineHeight};
    layout->firstBaseline = baseline;
    layout->lastBaseline = baseline;
    layout->ownedStorage = std::move(storage);
    return layout;
  }

  CachedGlyph glyphForAscii(unsigned char ch, bool bold) const {
    std::size_t const fontIndex = bold ? 1u : 0u;
    CachedGlyph& cached = asciiGlyphs_[fontIndex][ch];
    if (cached.loaded) {
      return cached;
    }

    cached.loaded = true;
    if (!app_) {
      return cached;
    }

    char const c = static_cast<char>(ch);
    std::shared_ptr<TextLayout const> layout =
        app_->textSystem().layout(std::string_view(&c, 1), terminalFont(bold), Color{1.f, 1.f, 1.f, 1.f},
                                  0.f, terminalTextOptions());
    if (!layout || layout->runs.empty()) {
      return cached;
    }

    TextLayout::PlacedRun const& placed = layout->runs.front();
    if (placed.run.glyphIds.empty() || placed.run.positions.empty()) {
      return cached;
    }

    cached.valid = true;
    cached.fontId = placed.run.fontId;
    cached.fontSize = placed.run.fontSize;
    cached.glyphId = placed.run.glyphIds.front();
    cached.position = placed.run.positions.front();
    cached.baseline = placed.origin.y;
    cached.ascent = placed.run.ascent;
    cached.descent = placed.run.descent;
    return cached;
  }

  static bool isFastAscii(std::string const& text) {
    for (unsigned char ch : text) {
      if (ch < 0x20u || ch >= 0x7fu) {
        return false;
      }
    }
    return true;
  }

  static bool sameRunContent(TerminalRun const& a, TerminalRun const& b) {
    return a.startCell == b.startCell &&
           a.cellCount == b.cellCount &&
           a.text == b.text &&
           a.foreground == b.foreground &&
           a.background == b.background &&
           a.hasBackground == b.hasBackground &&
           a.bold == b.bold &&
           a.italic == b.italic &&
           a.underline == b.underline &&
           a.strikethrough == b.strikethrough;
  }

  static bool sameRowContent(TerminalRow const& a, TerminalRow const& b) {
    if (a.row != b.row || a.runs.size() != b.runs.size()) {
      return false;
    }
    for (std::size_t i = 0; i < a.runs.size(); ++i) {
      if (!sameRunContent(a.runs[i], b.runs[i])) {
        return false;
      }
    }
    return true;
  }

  static bool isFastAsciiRow(TerminalRow const& row) {
    return std::all_of(row.runs.begin(), row.runs.end(), [](TerminalRun const& run) {
      return isFastAscii(run.text) && !run.italic;
    });
  }

  void startWakeThread() {
    if (ptyFd_ < 0 || !app_ || wakeThread_.joinable()) {
      return;
    }
    wakeThreadStop_.store(false, std::memory_order_release);
    wakeThread_ = std::thread([this] {
      while (!wakeThreadStop_.load(std::memory_order_acquire)) {
        pollfd pfd{.fd = ptyFd_, .events = POLLIN | POLLHUP | POLLERR, .revents = 0};
        int const ready = poll(&pfd, 1, 100);
        if (ready > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR))) {
          if (app_ && !wakeQueued_.exchange(true, std::memory_order_acq_rel)) {
            app_->wakeEventLoop();
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
      }
    });
  }

  void stopWakeThread() {
    wakeThreadStop_.store(true, std::memory_order_release);
    if (wakeThread_.joinable()) {
      wakeThread_.join();
    }
  }

  void quitAfterShellExit() {
    wakeQueued_.store(false, std::memory_order_release);
    if (app_) {
      app_->quit();
    }
  }

  void appendStatusLine(std::string_view message) {
    std::vector<TerminalRow> next = rows_.peek();
    if (next.empty()) {
      std::uint64_t const revision = ++revision_;
      TerminalRun run{
          .startCell = 0,
          .cellCount = static_cast<int>(message.size()),
          .revision = revision,
          .text = std::string(message),
          .foreground = Color{1.f, 0.74f, 0.38f, 1.f},
      };
      run.layout = layoutForRun(run);
      next.push_back(TerminalRow{
          .row = 0,
          .revision = revision,
          .runs = {std::move(run)},
      });
    } else {
      next.back().revision = ++revision_;
      TerminalRun run{
          .startCell = 0,
          .cellCount = static_cast<int>(message.size()),
          .revision = next.back().revision,
          .text = std::string(message),
          .foreground = Color{1.f, 0.74f, 0.38f, 1.f},
      };
      run.layout = layoutForRun(run);
      next.back().runs = {std::move(run)};
    }
    rows_.set(std::move(next));
    if (window_) {
      window_->requestRedraw();
    }
  }

  Application* app_ = nullptr;
  Window* window_ = nullptr;
  TerminalConfig config_{};
  VTerm* vterm_ = nullptr;
  VTermScreen* screen_ = nullptr;
  int ptyFd_ = -1;
  pid_t childPid_ = -1;
  std::uint64_t pollId_ = 0;
  ObserverHandle frameObserver_{};
  int rowsCount_ = kInitialRows;
  int cols_ = kInitialCols;
  int pendingRowsCount_ = kInitialRows;
  int pendingCols_ = kInitialCols;
  bool pendingResize_ = false;
  bool alternateScreen_ = false;
  int viewportOffset_ = 0;
  std::uint64_t revision_ = 0;
  int dirtyStartRow_ = kInitialRows;
  int dirtyEndRow_ = -1;
  std::atomic_bool wakeQueued_{false};
  std::atomic_bool wakeThreadStop_{false};
  std::thread wakeThread_;
  std::optional<TerminalSelection> selection_;
  bool selecting_ = false;
  std::uint64_t autotestTimerId_ = 0;
  std::string autotestExpectedText_;
  int autotestTicks_ = 0;
  int autotestMaxTicks_ = 0;
  bool autotestPasteClipboard_ = false;
  bool autotestPasteDispatched_ = false;
  std::vector<TerminalRow> historyRows_;
  std::vector<TerminalRow> screenRows_;
  mutable std::array<std::array<CachedGlyph, 128>, 2> asciiGlyphs_{};
  Reactive::Signal<std::vector<TerminalRow>> rows_;
  Reactive::Signal<VTermPos> cursor_;
};

struct TerminalApp {
  std::shared_ptr<TerminalSession> session;

  Element body() const {
    return Render{.draw = [session = session](Canvas& canvas, Rect frame) {
              session->drawTerminal(canvas, frame);
            }}
        .flex(1.f, 1.f, 0.f)
        .onPointerDown([session = session](Point point, MouseButton button) {
          session->beginSelection(point, button);
        })
        .onPointerMove([session = session](Point point) {
          session->updateSelection(point);
        })
        .onPointerUp([session = session](Point point, MouseButton button) {
          session->endSelection(point, button);
        })
        .onScroll([session = session](Vec2 delta) {
          session->scrollViewport(delta);
        })
        .onTextInput([session = session](std::string const& text) { session->sendText(text); })
        .onKeyDown([session = session](KeyCode key, Modifiers modifiers) {
          session->handleKeyDown(key, modifiers);
        })
        .focusable(true)
        .cursor(Cursor::IBeam);
  }
};

} // namespace

void installTerminalView(lambda::Application& app, lambda::Window& window, TerminalConfig config) {
  auto session = std::make_shared<TerminalSession>(app, window, std::move(config));
  session->installAutotestTextObserverFromEnv();
  window.setView<TerminalApp>({.session = std::move(session)});
}

} // namespace lambda_terminal
