#include "Compositor/CompositorRuntime.hpp"
#include "Compositor/Diagnostics/CrashLog.hpp"
#include "Compositor/Diagnostics/CpuTrace.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <string_view>

namespace {

std::atomic<bool> gRunning{true};

void onSignal(int) {
  gRunning.store(false, std::memory_order_relaxed);
}

void printUsage(char const* argv0) {
  std::printf("Usage: %s [--config PATH] [--output NAME_OR_INDEX] [--list-outputs] [--help]\n",
              argv0 ? argv0 : "flux-compositor");
}

} // namespace

int main(int argc, char** argv) {
  flux::compositor::diagnostics::initializeCrashLog();
  flux::compositor::diagnostics::installCrashHandlers();
  flux::compositor::diagnostics::initializeCpuSampler();
  flux::compositor::diagnostics::crashLog("main argc=%d", argc);

  flux::compositor::KmsCompositorOptions options{};
  for (int i = 1; i < argc; ++i) {
    std::string_view const arg(argv[i] ? argv[i] : "");
    if (arg == "--help" || arg == "-h") {
      printUsage(argv[0]);
      return 0;
    }
    if (arg == "--config") {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "flux-compositor: --config requires a path\n");
        return 2;
      }
      setenv("FLUX_COMPOSITOR_CONFIG", argv[++i], 1);
      continue;
    }
    if (arg == "--output") {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "flux-compositor: --output requires a connector name or index\n");
        return 2;
      }
      setenv("FLUX_COMPOSITOR_OUTPUT", argv[++i], 1);
      continue;
    }
    if (arg == "--list-outputs") {
      options.listOutputs = true;
      continue;
    }
    std::fprintf(stderr, "flux-compositor: unknown option %s\n", arg.data());
    printUsage(argv[0]);
    return 2;
  }

  std::signal(SIGTERM, onSignal);
  // Ctrl+C belongs to the focused Wayland client. The compositor exits via
  // SIGTERM or its configured terminate shortcut.
  std::signal(SIGINT, SIG_IGN);
  int const status = flux::compositor::runKmsCompositor(gRunning, options);
  flux::compositor::diagnostics::crashLog("main exit status=%d", status);
  return status;
}
