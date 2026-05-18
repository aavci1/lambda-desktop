#include "Compositor/CompositorRuntime.hpp"

#include <atomic>
#include <csignal>

namespace {

std::atomic<bool> gRunning{true};

void onSignal(int) {
  gRunning.store(false, std::memory_order_relaxed);
}

} // namespace

int main(int, char**) {
  std::signal(SIGTERM, onSignal);
  // Ctrl+C belongs to the focused Wayland client. The compositor exits via
  // SIGTERM or its configured terminate shortcut.
  std::signal(SIGINT, SIG_IGN);
  return flux::compositor::runKmsCompositor(gRunning);
}
