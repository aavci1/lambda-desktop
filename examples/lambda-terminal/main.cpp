#include <Flux.hpp>
#include <Flux/UI/KeyCodes.hpp>
#include <Flux/UI/Views/For.hpp>
#include <Flux/UI/Views/HStack.hpp>
#include <Flux/UI/Views/Rectangle.hpp>
#include <Flux/UI/Views/Render.hpp>
#include <Flux/UI/Views/Text.hpp>
#include <Flux/UI/Views/VStack.hpp>
#include <Flux/UI/Views/ZStack.hpp>
#include <Flux/UI/Window.hpp>

#include <vterm.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <pty.h>
#include <vector>

namespace lambda_terminal {
namespace {

using namespace flux;

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
  bool hasBackground = false;
  bool bold = false;

  bool operator==(TerminalRun const&) const = default;
};

struct TerminalRow {
  int row = 0;
  std::uint64_t revision = 0;
  std::vector<TerminalRun> runs;

  bool operator==(TerminalRow const&) const = default;
};

std::string shellName(std::string const& shellPath) {
  std::size_t const slash = shellPath.find_last_of('/');
  return slash == std::string::npos ? shellPath : shellPath.substr(slash + 1);
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

class TerminalSession : public std::enable_shared_from_this<TerminalSession> {
public:
  TerminalSession(Application& app, Window& window)
      : app_(&app)
      , window_(&window)
      , rows_(std::vector<TerminalRow>{})
      , cursor_(VTermPos{0, 0}) {
    vterm_ = vterm_new(kInitialRows, kInitialCols);
    vterm_set_utf8(vterm_, 1);
    vterm_output_set_callback(vterm_, &TerminalSession::writeToPty, this);
    screen_ = vterm_obtain_screen(vterm_);
    vterm_screen_enable_altscreen(screen_, 1);
    vterm_screen_enable_reflow(screen_, true);
    vterm_screen_set_damage_merge(screen_, VTERM_DAMAGE_ROW);
    vterm_screen_set_callbacks(screen_, &screenCallbacks(), this);
    vterm_screen_reset(screen_, 1);
    spawnShell();
    refreshRows();
  }

  ~TerminalSession() {
    if (app_ && pollId_ != 0) {
      app_->unregisterEventPollSource(pollId_);
    }
    if (ptyFd_ >= 0) {
      close(ptyFd_);
    }
    if (childPid_ > 0) {
      int status = 0;
      if (waitpid(childPid_, &status, WNOHANG) == 0) {
        kill(childPid_, SIGHUP);
      }
    }
    if (vterm_) {
      vterm_free(vterm_);
    }
  }

  Reactive::Signal<std::vector<TerminalRow>> rows() const { return rows_; }
  Reactive::Signal<VTermPos> cursor() const { return cursor_; }

  void resizeForFrame(Rect frame) {
    int const nextCols = std::max(20, static_cast<int>(std::floor((frame.width - kContentInset * 2.f) / kCellWidth)));
    int const nextRows = std::max(6, static_cast<int>(std::floor((frame.height - kContentInset * 2.f) / kLineHeight)));
    if (nextCols == cols_ && nextRows == rowsCount_) {
      return;
    }
    cols_ = nextCols;
    rowsCount_ = nextRows;
    vterm_set_size(vterm_, rowsCount_, cols_);
    if (ptyFd_ >= 0) {
      winsize size{};
      size.ws_col = static_cast<unsigned short>(cols_);
      size.ws_row = static_cast<unsigned short>(rowsCount_);
      ioctl(ptyFd_, TIOCSWINSZ, &size);
    }
    refreshRows();
  }

  void sendText(std::string const& text) {
    if (text.empty() || ptyFd_ < 0) {
      return;
    }
    writeAll(text.data(), text.size());
  }

  void sendKey(KeyCode key, Modifiers modifiers) {
    using namespace flux::keys;
    if (any(modifiers & Modifiers::Ctrl)) {
      static constexpr std::array<KeyCode, 26> keyByLetter{
          A, B, C, D, E, F, G, H, I, J, K, L, M,
          N, O, P, Q, R, S, T, U, V, W, X, Y, Z};
      for (std::size_t i = 0; i < keyByLetter.size(); ++i) {
        if (key == keyByLetter[i]) {
          char const control = static_cast<char>(i + 1u);
          writeAll(&control, 1);
          return;
        }
      }
    }

    switch (key) {
    case Return: writeAll("\r", 1); break;
    case Tab: writeAll("\t", 1); break;
    case Delete: writeAll("\x7f", 1); break;
    case ForwardDelete: writeAll("\x1b[3~", 4); break;
    case Escape: writeAll("\x1b", 1); break;
    case LeftArrow: writeAll("\x1b[D", 3); break;
    case RightArrow: writeAll("\x1b[C", 3); break;
    case UpArrow: writeAll("\x1b[A", 3); break;
    case DownArrow: writeAll("\x1b[B", 3); break;
    case Home: writeAll("\x1b[H", 3); break;
    case End: writeAll("\x1b[F", 3); break;
    case PageUp: writeAll("\x1b[5~", 4); break;
    case PageDown: writeAll("\x1b[6~", 4); break;
    default: break;
    }
  }

  void drawBackground(Canvas& canvas, Rect frame) {
    resizeForFrame(frame);
    canvas.drawRect(frame, CornerRadius{}, FillStyle::solid(Color{0.f, 0.f, 0.f, 0.12f}), StrokeStyle::none());
  }

  void drawCursor(Canvas& canvas, Rect frame) {
    resizeForFrame(frame);
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
  static VTermScreenCallbacks const& screenCallbacks() {
    static VTermScreenCallbacks callbacks{
        .damage = &TerminalSession::onDamage,
        .moverect = nullptr,
        .movecursor = &TerminalSession::onMoveCursor,
        .settermprop = &TerminalSession::onTermProp,
        .bell = nullptr,
        .resize = nullptr,
        .sb_pushline = nullptr,
        .sb_popline = nullptr,
        .sb_clear = nullptr,
    };
    return callbacks;
  }

  static int onDamage(VTermRect, void* user) {
    static_cast<TerminalSession*>(user)->refreshRows();
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
    if (prop != VTERM_PROP_TITLE || !value || !value->string.str) {
      return 1;
    }
    auto* session = static_cast<TerminalSession*>(user);
    if (session->window_) {
      session->window_->setTitle(std::string(value->string.str, value->string.len));
    }
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
      appendStatusLine("lambda-terminal: failed to start shell");
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
        appendStatusLine("lambda-terminal: shell exited");
        return;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      }
      if (errno != EINTR) {
        appendStatusLine("lambda-terminal: pty read failed");
        return;
      }
    }
    if (readAny) {
      vterm_screen_flush_damage(screen_);
      refreshRows();
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
    std::vector<TerminalRow> next;
    next.reserve(static_cast<std::size_t>(rowsCount_));
    for (int row = 0; row < rowsCount_; ++row) {
      TerminalRow terminalRow{.row = row, .revision = ++revision_, .runs = {}};
      TerminalRun current;
      bool hasCurrent = false;
      for (int col = 0; col < cols_; ++col) {
        VTermScreenCell cell{};
        if (!vterm_screen_get_cell(screen_, VTermPos{row, col}, &cell)) {
          cell.chars[0] = ' ';
          cell.width = 1;
          cell.fg.type = VTERM_COLOR_DEFAULT_FG;
          cell.bg.type = VTERM_COLOR_DEFAULT_BG;
        }
        bool const bold = cell.attrs.bold != 0;
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
              .revision = terminalRow.revision,
              .text = {},
              .foreground = foreground,
              .background = background,
              .hasBackground = hasBackground,
              .bold = bold,
          };
          hasCurrent = true;
        }
        appendUtf8(current.text, cell.chars[0]);
        current.cellCount += std::max(1, static_cast<int>(cell.width));
      }
      if (hasCurrent) {
        terminalRow.runs.push_back(std::move(current));
      }
      next.push_back(std::move(terminalRow));
    }
    rows_.set(std::move(next));
    if (window_) {
      window_->requestRedraw();
    }
  }

  void appendStatusLine(std::string_view message) {
    std::vector<TerminalRow> next = rows_.peek();
    if (next.empty()) {
      std::uint64_t const revision = ++revision_;
      next.push_back(TerminalRow{
          .row = 0,
          .revision = revision,
          .runs = {TerminalRun{
              .startCell = 0,
              .cellCount = static_cast<int>(message.size()),
              .revision = revision,
              .text = std::string(message),
              .foreground = Color{1.f, 0.74f, 0.38f, 1.f},
          }},
      });
    } else {
      next.back().revision = ++revision_;
      next.back().runs = {TerminalRun{
          .startCell = 0,
          .cellCount = static_cast<int>(message.size()),
          .revision = next.back().revision,
          .text = std::string(message),
          .foreground = Color{1.f, 0.74f, 0.38f, 1.f},
      }};
    }
    rows_.set(std::move(next));
    if (window_) {
      window_->requestRedraw();
    }
  }

  Application* app_ = nullptr;
  Window* window_ = nullptr;
  VTerm* vterm_ = nullptr;
  VTermScreen* screen_ = nullptr;
  int ptyFd_ = -1;
  pid_t childPid_ = -1;
  std::uint64_t pollId_ = 0;
  int rowsCount_ = kInitialRows;
  int cols_ = kInitialCols;
  std::uint64_t revision_ = 0;
  Reactive::Signal<std::vector<TerminalRow>> rows_;
  Reactive::Signal<VTermPos> cursor_;
};

struct TerminalApp {
  std::shared_ptr<TerminalSession> session;

  Element body() const {
    auto rowsSignal = session->rows();
    Element cursorLayer = Render{.draw = [session = session](Canvas& canvas, Rect frame) {
              session->drawCursor(canvas, frame);
            }}
            .flex(1.f, 1.f, 0.f)
            .onPointerDown([](Point) {})
            .onTextInput([session = session](std::string const& text) { session->sendText(text); })
            .onKeyDown([session = session](KeyCode key, Modifiers modifiers) {
              session->sendKey(key, modifiers);
            })
            .focusable(true)
            .cursor(Cursor::IBeam);
    auto terminal = ZStack{
        .horizontalAlignment = Alignment::Stretch,
        .verticalAlignment = Alignment::Stretch,
        .children = children(
            Render{.draw = [session = session](Canvas& canvas, Rect frame) {
              session->drawBackground(canvas, frame);
            }}.flex(1.f, 1.f, 0.f),
            VStack{
                .spacing = 0.f,
                .alignment = Alignment::Stretch,
                .children = children(Element{For(
                    rowsSignal,
                    [](TerminalRow const& row) {
                      return std::to_string(row.row) + ":" + std::to_string(row.revision);
                    },
                    [](TerminalRow const& row, Reactive::Signal<std::size_t> const&) {
                      std::vector<Element> runs;
                      runs.reserve(row.runs.size());
                      for (TerminalRun const& run : row.runs) {
                        Element runText = Text{
                            .text = run.text,
                            .font = Font{.family = "monospace", .size = 14.f, .weight = run.bold ? 700.f : 500.f},
                            .color = run.foreground,
                            .wrapping = TextWrapping::NoWrap,
                            .maxLines = 1,
                        }
                            .size(static_cast<float>(run.cellCount) * kCellWidth, kLineHeight);
                        if (run.hasBackground) {
                          runText = ZStack{
                              .children = children(
                                  Rectangle{}
                                      .fill(FillStyle::solid(run.background))
                                      .size(static_cast<float>(run.cellCount) * kCellWidth, kLineHeight),
                                  std::move(runText))}
                              .size(static_cast<float>(run.cellCount) * kCellWidth, kLineHeight);
                        }
                        runs.push_back(std::move(runText));
                      }
                      return HStack{
                                 .spacing = 0.f,
                                 .alignment = Alignment::Start,
                                 .children = std::move(runs),
                             }
                          .height(kLineHeight);
                    })})}
                .padding(kContentInset, kContentInset, kContentInset, kContentInset)
                .flex(1.f, 1.f, 0.f),
            std::move(cursorLayer))}
        .flex(1.f, 1.f, 0.f)
        .clipContent(true)
        .cursor(Cursor::IBeam);
    return terminal;
  }
};

} // namespace
} // namespace lambda_terminal

int main(int argc, char* argv[]) {
  flux::Application app(argc, argv);
  app.setName("terminal");

  auto& window = app.createWindow<flux::Window>({
      .size = {920.f, 560.f},
      .title = "Terminal",
      .titlebar = flux::WindowTitlebarMode::System,
      .resizable = true,
  });
  window.setBackground(flux::WindowBackground::glassEffect(flux::GlassEffectOptions{
      .blurRadius = 46.f,
      .baseColor = flux::Color{0.f, 0.f, 0.f, 0.50f},
      .tintColor = flux::Color{0.f, 0.f, 0.f, 0.42f},
      .borderColor = flux::Color{1.f, 1.f, 1.f, 0.16f},
      .opacity = 1.f,
  }));

  auto session = std::make_shared<lambda_terminal::TerminalSession>(app, window);
  window.setView<lambda_terminal::TerminalApp>({.session = std::move(session)});
  return app.exec();
}
