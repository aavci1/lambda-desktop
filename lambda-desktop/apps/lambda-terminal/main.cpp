#include <Lambda.hpp>
#include <Lambda/UI/EventQueue.hpp>
#include <Lambda/UI/Events.hpp>
#include <Lambda/UI/Window.hpp>

#include "TerminalCore.hpp"
#include "TerminalSession.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace {

bool envEnabled(char const* name) {
  char const* value = std::getenv(name);
  return value && *value && std::strcmp(value, "0") != 0 && std::strcmp(value, "false") != 0;
}

int envInt(char const* name, int fallback) {
  char const* value = std::getenv(name);
  if (!value || !*value) return fallback;
  char* end = nullptr;
  long parsed = std::strtol(value, &end, 10);
  if (end == value) return fallback;
  return static_cast<int>(std::clamp(parsed, 1l, 10'000l));
}

struct ScriptedResizeState {
  std::uint64_t timerId = 0;
  int step = 0;
  int maxSteps = 90;
  bool exitWhenDone = true;
  std::vector<lambdaui::Size> sizes;
  std::shared_ptr<std::FILE> log;
};

void installScriptedResize(lambdaui::Application& app, lambdaui::Window& window) {
  if (!envEnabled("LAMBDA_TERMINAL_SCRIPTED_RESIZE")) return;

  auto state = std::make_shared<ScriptedResizeState>();
  state->maxSteps = envInt("LAMBDA_TERMINAL_SCRIPTED_RESIZE_COUNT", 90);
  state->exitWhenDone = !std::getenv("LAMBDA_TERMINAL_SCRIPTED_RESIZE_EXIT") ||
                        envEnabled("LAMBDA_TERMINAL_SCRIPTED_RESIZE_EXIT");
  state->sizes = {
      {920.f, 560.f},
      {1040.f, 620.f},
      {780.f, 520.f},
      {1180.f, 680.f},
      {900.f, 640.f},
      {1100.f, 540.f},
  };

  if (char const* logPath = std::getenv("LAMBDA_TERMINAL_SCRIPTED_RESIZE_LOG");
      logPath && *logPath) {
    state->log = std::shared_ptr<std::FILE>(std::fopen(logPath, "w"), [](std::FILE* file) {
      if (file) std::fclose(file);
    });
  }

  int const intervalMs = envInt("LAMBDA_TERMINAL_SCRIPTED_RESIZE_INTERVAL_MS", 33);
  state->timerId = app.scheduleRepeatingTimer(std::chrono::milliseconds{intervalMs}, window.handle());
  app.eventQueue().on<lambdaui::TimerEvent>([&app, &window, state](lambdaui::TimerEvent const& event) {
    if (state->timerId == 0 || event.timerId != state->timerId || state->sizes.empty()) return;
    lambdaui::Size const size = state->sizes[static_cast<std::size_t>(state->step) % state->sizes.size()];
    window.resize(size);
    ++state->step;
    if (state->log) {
      std::fprintf(state->log.get(),
                   "scripted-resize step=%d size=%.0fx%.0f\n",
                   state->step,
                   size.width,
                   size.height);
      std::fflush(state->log.get());
    }
    if (state->step >= state->maxSteps) {
      app.cancelTimer(state->timerId);
      state->timerId = 0;
      if (state->exitWhenDone) {
        app.quit();
      }
    }
  });
}

} // namespace

int main(int argc, char* argv[]) {
  lambdaui::Application app(argc, argv);
  app.setName("lambda-terminal");
  auto const preferences = lambda_terminal::loadTerminalPreferences();
  auto const profile = lambda_terminal::activeTerminalProfile(preferences.preferences);
  auto const initialWidth = static_cast<float>(envInt("LAMBDA_TERMINAL_WINDOW_WIDTH", 920));
  auto const initialHeight = static_cast<float>(envInt("LAMBDA_TERMINAL_WINDOW_HEIGHT", 560));

  auto& window = app.createWindow<lambdaui::Window>({
      .size = {initialWidth, initialHeight},
      .title = "Terminal",
      .titlebar = lambdaui::WindowTitlebarMode::System,
      .resizable = true,
  });
  if (profile.config.blackGlassBackground) {
    window.setBackground(lambdaui::WindowBackground::glassEffect(lambdaui::GlassEffectOptions{
        .blurRadius = profile.config.blackGlassBlurRadius,
        .baseColor = profile.config.blackGlassTint,
        .tintColor = profile.config.blackGlassTint,
        .borderColor = lambdaui::Color{1.f, 1.f, 1.f, 0.16f},
        .opacity = 1.f,
    }));
  } else {
    window.setBackground(lambdaui::WindowBackground::solid(profile.config.blackGlassTint));
  }

  lambda_terminal::installTerminalView(app, window, profile.config);
  installScriptedResize(app, window);
  return app.exec();
}
