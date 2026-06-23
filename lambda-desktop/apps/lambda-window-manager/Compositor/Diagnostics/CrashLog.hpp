#pragma once

#include <cstdarg>

namespace lambdaui::compositor::diagnostics {

void initializeCrashLog();
void installCrashHandlers();
bool crashLogEnabled() noexcept;
char const* crashLogPath() noexcept;
void crashLog(char const* format, ...);
void crashLogV(char const* format, va_list args);
void crashLogSignalSafe(char const* message) noexcept;

} // namespace lambdaui::compositor::diagnostics
