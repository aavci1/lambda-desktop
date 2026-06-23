#pragma once

#include <Lambda/Debug/DebugFlags.hpp>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace lambdaui::detail {

inline bool resizeTraceEnabled() {
  static bool const enabled = [] {
    char const* value = std::getenv("LAMBDA_RESIZE_TRACE");
    return debug::envNonZero(value);
  }();
  return enabled;
}

inline std::uint64_t resizeTraceTimestampNanoseconds() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<std::uint64_t>(now.tv_sec) * 1'000'000'000ull +
         static_cast<std::uint64_t>(now.tv_nsec);
}

inline bool resizeTraceMirrorStderr() {
  char const* value = std::getenv("LAMBDA_RESIZE_TRACE_STDERR");
  return !value || debug::envNonZero(value);
}

inline bool resizeTraceMetadataEnabled() {
  static bool const enabled = resizeTraceEnabled() ||
                              debug::envNonZero(std::getenv("LAMBDA_WINDOW_MANAGER_PACING_TRACE")) ||
                              debug::envNonZero(std::getenv("LAMBDA_WINDOW_MANAGER_CPU_TRACE")) ||
                              debug::envNonZero(std::getenv("LWM_SNAP_TRACE"));
  return enabled;
}

inline void resizeTrace(char const* prefix, char const* format, ...) {
  if (!resizeTraceEnabled()) return;
  if (!prefix || !*prefix) prefix = "resize";
  std::uint64_t const now = resizeTraceTimestampNanoseconds();

  auto write = [&](FILE* file, va_list args) {
    std::fprintf(file, "resize-trace: %.3fms %s: ", static_cast<double>(now) / 1'000'000.0, prefix);
    std::vfprintf(file, format, args);
  };

  va_list args;
  va_start(args, format);
  if (resizeTraceMirrorStderr()) {
    va_list stderrArgs;
    va_copy(stderrArgs, args);
    write(stderr, stderrArgs);
    va_end(stderrArgs);
  }

  char const* path = std::getenv("LAMBDA_RESIZE_TRACE_LOG");
  if (!path || !*path) {
    path = "/tmp/lambda-resize-trace.log";
  }
  if (FILE* file = std::fopen(path, "a")) {
    write(file, args);
    std::fclose(file);
  }
  va_end(args);
}

} // namespace lambdaui::detail

#define LAMBDA_RESIZE_TRACE(...)                   \
  do {                                             \
    if (::lambdaui::detail::resizeTraceEnabled()) { \
      ::lambdaui::detail::resizeTrace(__VA_ARGS__); \
    }                                              \
  } while (false)
