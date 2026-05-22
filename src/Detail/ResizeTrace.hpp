#pragma once

#include <Flux/Debug/DebugFlags.hpp>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace flux::detail {

inline bool resizeTraceEnabled() {
  static bool const enabled = [] {
    char const* value = std::getenv("FLUX_RESIZE_TRACE");
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
  va_list stderrArgs;
  va_copy(stderrArgs, args);
  write(stderr, stderrArgs);
  va_end(stderrArgs);

  char const* path = std::getenv("FLUX_RESIZE_TRACE_LOG");
  if (!path || !*path) {
    path = "/tmp/flux-resize-trace.log";
  }
  if (FILE* file = std::fopen(path, "a")) {
    write(file, args);
    std::fclose(file);
  }
  va_end(args);
}

} // namespace flux::detail
