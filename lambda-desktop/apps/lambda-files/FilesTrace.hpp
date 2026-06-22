#pragma once

#include <Lambda/Debug/DebugFlags.hpp>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace lambda_files::trace {

inline bool enabled() {
  static bool const value = [] {
    return lambda::debug::envNonZero(std::getenv("LAMBDA_FILES_TRACE"));
  }();
  return value;
}

inline double nowMs() {
  if (!enabled()) return 0.0;
  using Clock = std::chrono::steady_clock;
  static Clock::time_point const start = Clock::now();
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

inline FILE* traceFile() {
  static FILE* file = [] {
    char const* path = std::getenv("LAMBDA_FILES_TRACE_PATH");
    FILE* opened = std::fopen(path && *path ? path : "/tmp/lambda-files-trace.log", "a");
    if (opened) std::setvbuf(opened, nullptr, _IOLBF, 0);
    return opened;
  }();
  return file;
}

inline void event(char const* format, ...) {
  if (!enabled()) return;
  FILE* file = traceFile();
  if (!file) return;
  std::fprintf(file, "files-trace: %.3fms ", nowMs());
  va_list args;
  va_start(args, format);
  std::vfprintf(file, format, args);
  va_end(args);
}

} // namespace lambda_files::trace

#define LAMBDA_FILES_TRACE_EVENT(...)             \
  do {                                            \
    if (::lambda_files::trace::enabled()) {       \
      ::lambda_files::trace::event(__VA_ARGS__); \
    }                                             \
  } while (false)
