#pragma once

#include <cstdlib>
#include <cstring>

namespace lambdaui::debug {

inline bool envNonZero(char const* value) {
  return value && value[0] != '\0' && std::strcmp(value, "0") != 0 &&
         std::strcmp(value, "false") != 0 && std::strcmp(value, "FALSE") != 0 &&
         std::strcmp(value, "off") != 0 && std::strcmp(value, "OFF") != 0 &&
         std::strcmp(value, "no") != 0 && std::strcmp(value, "NO") != 0;
}

inline bool envTruthy(char const* value) {
  return envNonZero(value);
}

inline bool inputEnabled() {
  static int cached = -1;
  if (cached < 0) {
    cached = envTruthy(std::getenv("LAMBDA_DEBUG_INPUT")) ? 1 : 0;
  }
  return cached != 0;
}

inline bool inputVerbose() {
  static int cached = -1;
  if (cached < 0) {
    cached = envTruthy(std::getenv("LAMBDA_DEBUG_INPUT_VERBOSE")) ? 1 : 0;
  }
  return cached != 0;
}

inline bool layoutEnabled() {
  static int cached = -1;
  if (cached < 0) {
    cached = envTruthy(std::getenv("LAMBDA_DEBUG_LAYOUT")) ? 1 : 0;
  }
  return cached != 0;
}

inline bool perfEnabled() {
  static int cached = -1;
  if (cached < 0) {
    cached = envTruthy(std::getenv("LAMBDA_DEBUG_PERF")) ? 1 : 0;
  }
  return cached != 0;
}

inline int perfLevel() {
  static int cached = -2;
  if (cached == -2) {
    char const* value = std::getenv("LAMBDA_DEBUG_PERF");
    if (!envTruthy(value)) {
      cached = 0;
    } else if (std::strcmp(value, "2") == 0 || std::strcmp(value, "verbose") == 0) {
      cached = 2;
    } else if (std::strcmp(value, "anomaly") == 0 || std::strcmp(value, "quiet") == 0) {
      cached = -1;
    } else {
      cached = 1;
    }
  }
  return cached;
}

inline bool perfVerbose() {
  return perfLevel() >= 2;
}

inline bool perfAnomalyOnly() {
  return perfLevel() < 0;
}

} // namespace lambdaui::debug
