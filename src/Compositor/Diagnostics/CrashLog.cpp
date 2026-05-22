#include "Compositor/Diagnostics/CrashLog.hpp"

#include <Flux/Debug/DebugFlags.hpp>

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>

namespace flux::compositor::diagnostics {
namespace {

std::atomic<int> gCrashLogFd{-1};
std::string gCrashLogPath;

bool crashLogRequested() noexcept {
  static bool const requested = debug::envNonZero(std::getenv("FLUX_COMPOSITOR_CRASH_LOG"));
  return requested;
}

std::string defaultCrashLogPath() {
  if (char const* configured = std::getenv("FLUX_COMPOSITOR_CRASH_LOG");
      debug::envNonZero(configured) && std::strcmp(configured, "1") != 0) {
    return configured;
  }
  if (char const* stateHome = std::getenv("XDG_STATE_HOME"); stateHome && *stateHome) {
    return std::string(stateHome) + "/flux-compositor/crash.log";
  }
  if (char const* home = std::getenv("HOME"); home && *home) {
    return std::string(home) + "/.local/state/flux-compositor/crash.log";
  }
  return "/tmp/flux-compositor-crash.log";
}

std::uint64_t monotonicMilliseconds() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<std::uint64_t>(now.tv_sec) * 1000ull +
         static_cast<std::uint64_t>(now.tv_nsec) / 1'000'000ull;
}

void writeAll(int fd, char const* data, std::size_t size) noexcept {
  while (size > 0) {
    ssize_t const written = write(fd, data, size);
    if (written <= 0) return;
    data += written;
    size -= static_cast<std::size_t>(written);
  }
}

void writeUnsigned(int fd, int value) noexcept {
  char buffer[16]{};
  std::size_t cursor = sizeof(buffer);
  unsigned int remaining = value < 0 ? static_cast<unsigned int>(-value)
                                     : static_cast<unsigned int>(value);
  do {
    buffer[--cursor] = static_cast<char>('0' + (remaining % 10u));
    remaining /= 10u;
  } while (remaining > 0 && cursor > 0);
  if (value < 0 && cursor > 0) buffer[--cursor] = '-';
  writeAll(fd, buffer + cursor, sizeof(buffer) - cursor);
}

void fatalSignalHandler(int signal) noexcept {
  int const fd = gCrashLogFd.load(std::memory_order_relaxed);
  if (fd >= 0) {
    char const prefix[] = "fatal-signal signal=";
    writeAll(fd, prefix, sizeof(prefix) - 1u);
    writeUnsigned(fd, signal);
    char const suffix[] = "\n";
    writeAll(fd, suffix, sizeof(suffix) - 1u);
    fsync(fd);
  }
  _exit(128 + signal);
}

void terminateHandler() noexcept {
  crashLogSignalSafe("std::terminate\n");
  std::abort();
}

} // namespace

void initializeCrashLog() {
  if (!crashLogRequested()) return;
  if (gCrashLogFd.load(std::memory_order_relaxed) >= 0) return;

  gCrashLogPath = defaultCrashLogPath();
  try {
    std::filesystem::path const path(gCrashLogPath);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
  } catch (...) {
    gCrashLogPath = "/tmp/flux-compositor-crash.log";
  }

  int fd = open(gCrashLogPath.c_str(), O_CREAT | O_WRONLY | O_APPEND | O_CLOEXEC, 0644);
  if (fd < 0 && gCrashLogPath != "/tmp/flux-compositor-crash.log") {
    gCrashLogPath = "/tmp/flux-compositor-crash.log";
    fd = open(gCrashLogPath.c_str(), O_CREAT | O_WRONLY | O_APPEND | O_CLOEXEC, 0644);
  }
  if (fd < 0) return;

  int expected = -1;
  if (!gCrashLogFd.compare_exchange_strong(expected, fd, std::memory_order_relaxed)) {
    close(fd);
    return;
  }
  crashLog("log-opened path=%s pid=%d", gCrashLogPath.c_str(), static_cast<int>(getpid()));
}

void installCrashHandlers() {
  if (!crashLogEnabled()) return;
  std::set_terminate(terminateHandler);

  struct sigaction action {};
  action.sa_handler = fatalSignalHandler;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_RESETHAND;
  sigaction(SIGSEGV, &action, nullptr);
  sigaction(SIGABRT, &action, nullptr);
  sigaction(SIGBUS, &action, nullptr);
  sigaction(SIGILL, &action, nullptr);
  sigaction(SIGFPE, &action, nullptr);
}

bool crashLogEnabled() noexcept {
  if (!crashLogRequested()) return false;
  return gCrashLogFd.load(std::memory_order_relaxed) >= 0;
}

char const* crashLogPath() noexcept {
  return gCrashLogPath.c_str();
}

void crashLog(char const* format, ...) {
  if (!crashLogRequested() || gCrashLogFd.load(std::memory_order_relaxed) < 0 || !format) return;
  va_list args;
  va_start(args, format);
  crashLogV(format, args);
  va_end(args);
}

void crashLogV(char const* format, va_list args) {
  int const fd = gCrashLogFd.load(std::memory_order_relaxed);
  if (fd < 0 || !format) return;

  std::array<char, 2048> buffer{};
  int const prefix = std::snprintf(buffer.data(),
                                   buffer.size(),
                                   "[%llu.%03llu] ",
                                   static_cast<unsigned long long>(monotonicMilliseconds() / 1000ull),
                                   static_cast<unsigned long long>(monotonicMilliseconds() % 1000ull));
  if (prefix < 0 || static_cast<std::size_t>(prefix) >= buffer.size()) return;
  std::size_t cursor = static_cast<std::size_t>(prefix);
  int const body = std::vsnprintf(buffer.data() + cursor, buffer.size() - cursor, format, args);
  if (body < 0) return;
  cursor += std::min<std::size_t>(static_cast<std::size_t>(body), buffer.size() - cursor - 2u);
  if (cursor == 0 || buffer[cursor - 1u] != '\n') buffer[cursor++] = '\n';
  writeAll(fd, buffer.data(), cursor);
  fsync(fd);
}

void crashLogSignalSafe(char const* message) noexcept {
  if (!crashLogRequested()) return;
  int const fd = gCrashLogFd.load(std::memory_order_relaxed);
  if (fd < 0 || !message) return;
  writeAll(fd, message, std::strlen(message));
  fsync(fd);
}

} // namespace flux::compositor::diagnostics
