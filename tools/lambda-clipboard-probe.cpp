#include <Lambda.hpp>
#include <Lambda/UI/Views/TextInput.hpp>
#include <Lambda/UI/WindowUI.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>

namespace {

bool envEnabled(char const* name) {
  char const* value = std::getenv(name);
  return value && *value && std::strcmp(value, "0") != 0 && std::strcmp(value, "false") != 0;
}

std::string envString(char const* name, std::string fallback = {}) {
  char const* value = std::getenv(name);
  if (!value || !*value) return fallback;
  return std::string(value);
}

int envIntClamped(char const* name, int fallback, int minimum, int maximum) {
  char const* value = std::getenv(name);
  if (!value || !*value) return fallback;
  char* end = nullptr;
  long parsed = std::strtol(value, &end, 10);
  if (end == value) return fallback;
  return static_cast<int>(std::clamp(parsed, static_cast<long>(minimum), static_cast<long>(maximum)));
}

struct ProbeTextState {
  lambda::Reactive::Signal<std::string> text;
  lambda::Reactive::Signal<lambda::detail::TextEditSelection> selection;
  bool multiline = true;
};

struct ClipboardProbeRoot {
  std::shared_ptr<ProbeTextState> state;

  lambda::Element body() const {
    lambda::TextInput input;
    input.value = state->text;
    input.selection = state->selection;
    input.placeholder = "Clipboard probe";
    input.multiline = state->multiline;
    return std::move(input).size(640.f, 220.f);
  }
};

struct DriverState {
  std::shared_ptr<ProbeTextState> textState;
  lambda::Application* app = nullptr;
  lambda::Window* window = nullptr;
  std::string role;
  std::string expected;
  std::uint64_t timerId = 0;
  int ticks = 0;
  int maxTicks = 100;
  int exitCode = 0;
  bool autoCopy = false;
  bool autoPaste = false;
  bool autoDispatched = false;
  bool copiedLogged = false;
  bool exitAfterCopy = false;
};

void finish(DriverState& state, int exitCode) {
  state.exitCode = exitCode;
  if (state.app && state.timerId != 0) {
    state.app->cancelTimer(state.timerId);
    state.timerId = 0;
  }
  if (state.app) {
    state.app->quit();
  }
}

void handleTimer(DriverState& state, lambda::TimerEvent const& event) {
  if (state.timerId == 0 || event.timerId != state.timerId) {
    return;
  }

  if (state.role == "source") {
    if (state.autoCopy && !state.autoDispatched && state.ticks >= 4 && state.window) {
      state.autoDispatched = true;
      bool const handled = state.window->dispatchCommand("edit.copy");
      std::fprintf(stderr, "lambda-clipboard-probe: copy-dispatched handled=%d\n", handled ? 1 : 0);
      std::fflush(stderr);
    }
    std::optional<std::string> clipboard = state.app ? state.app->clipboard().readText() : std::nullopt;
    if (!state.copiedLogged && clipboard && *clipboard == state.expected) {
      state.copiedLogged = true;
      std::fprintf(stderr,
                   "lambda-clipboard-probe: copied role=source bytes=%zu\n",
                   state.expected.size());
      std::fflush(stderr);
      if (state.exitAfterCopy) {
        finish(state, 0);
        return;
      }
    }
  } else {
    if (state.autoPaste && !state.autoDispatched && state.ticks >= 4 && state.window) {
      state.autoDispatched = true;
      bool const handled = state.window->dispatchCommand("edit.paste");
      std::fprintf(stderr, "lambda-clipboard-probe: paste-dispatched handled=%d\n", handled ? 1 : 0);
      std::fflush(stderr);
    }
    std::string const text = state.textState ? state.textState->text.peek() : std::string{};
    if (text == state.expected) {
      std::fprintf(stderr,
                   "lambda-clipboard-probe: pasted role=sink bytes=%zu\n",
                   state.expected.size());
      std::fflush(stderr);
      finish(state, 0);
      return;
    }
  }

  ++state.ticks;
  if (state.ticks >= state.maxTicks) {
    std::fprintf(stderr,
                 "lambda-clipboard-probe: timeout role=%s expected_bytes=%zu copied=%d dispatched=%d\n",
                 state.role.c_str(),
                 state.expected.size(),
                 state.copiedLogged ? 1 : 0,
                 state.autoDispatched ? 1 : 0);
    std::fflush(stderr);
    finish(state, 1);
  }
}

} // namespace

int main(int argc, char* argv[]) {
  lambda::Application app(argc, argv);
  app.setName("lambda-clipboard-probe");

  auto state = std::make_shared<DriverState>();
  state->app = &app;
  state->role = envString("LAMBDA_CLIPBOARD_PROBE_ROLE", "sink");
  state->expected = envString("LAMBDA_CLIPBOARD_PROBE_EXPECT",
                              envString("LAMBDA_CLIPBOARD_PROBE_TEXT", "Flux clipboard probe"));
  state->autoCopy = envEnabled("LAMBDA_CLIPBOARD_PROBE_AUTO_COPY");
  state->autoPaste = envEnabled("LAMBDA_CLIPBOARD_PROBE_AUTO_PASTE");
  state->exitAfterCopy = envEnabled("LAMBDA_CLIPBOARD_PROBE_EXIT_AFTER_COPY");
  int const timeoutMs = envIntClamped("LAMBDA_CLIPBOARD_PROBE_TIMEOUT_MS", 5000, 100, 60000);
  state->maxTicks = std::max(1, timeoutMs / 50);

  state->textState = std::make_shared<ProbeTextState>();
  if (state->role == "source") {
    state->textState->text.set(state->expected);
    state->textState->selection.set(lambda::detail::TextEditSelection{
        .caretByte = static_cast<int>(state->expected.size()),
        .anchorByte = 0,
    });
  } else {
    state->textState->text.set("");
    state->textState->selection.set(lambda::detail::TextEditSelection{.caretByte = 0, .anchorByte = 0});
  }

  lambda::WindowConfig config;
  config.size = {720.f, 320.f};
  config.title = state->role == "source" ? "Clipboard Source" : "Clipboard Sink";
  config.titlebar = lambda::WindowTitlebarMode::System;
  config.resizable = true;
  auto& window = app.createWindow<lambda::Window>(config);
  state->window = &window;
  window.setView<ClipboardProbeRoot>({.state = state->textState});

  state->timerId = app.scheduleRepeatingTimer(std::chrono::milliseconds{50}, window.handle());
  app.eventQueue().on<lambda::TimerEvent>([state](lambda::TimerEvent const& event) {
    handleTimer(*state, event);
  });

  std::fprintf(stderr,
               "lambda-clipboard-probe: ready role=%s expected_bytes=%zu auto_copy=%d auto_paste=%d\n",
               state->role.c_str(),
               state->expected.size(),
               state->autoCopy ? 1 : 0,
               state->autoPaste ? 1 : 0);
  std::fflush(stderr);

  int const appResult = app.exec();
  return state->exitCode != 0 ? state->exitCode : appResult;
}
