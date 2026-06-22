#pragma once

#include <charconv>
#include <cctype>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace lambda::compositor::presentation {

inline std::string lowerAscii(std::string_view value) {
  std::string result(value);
  for (char& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return result;
}

inline std::optional<std::size_t> parseOutputIndex(std::string_view selector, std::size_t count) {
  std::size_t index = 0;
  auto const* begin = selector.data();
  auto const* end = selector.data() + selector.size();
  auto [ptr, error] = std::from_chars(begin, end, index);
  if (error != std::errc{} || ptr != end || index >= count) return std::nullopt;
  return index;
}

[[nodiscard]] inline std::optional<std::size_t> selectOutputNameIndex(
    std::span<std::string const> outputNames,
    std::optional<std::string> const& selector) {
  if (outputNames.empty()) return std::nullopt;
  if (!selector || selector->empty()) return 0u;

  for (std::size_t i = 0; i < outputNames.size(); ++i) {
    if (outputNames[i] == *selector) return i;
  }

  std::string const normalized = lowerAscii(*selector);
  for (std::size_t i = 0; i < outputNames.size(); ++i) {
    if (lowerAscii(outputNames[i]) == normalized) return i;
  }
  if (normalized == "primary" || normalized == "first") return 0u;
  if ((normalized == "secondary" || normalized == "second") && outputNames.size() > 1u) return 1u;
  return parseOutputIndex(*selector, outputNames.size());
}

} // namespace lambda::compositor::presentation
