#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace lambda_shell {

std::string escapeJson(std::string_view text);

std::string jsonStringField(std::string_view line, std::string_view name, std::size_t start = 0);

float jsonFloatField(std::string_view line, std::string_view name, float fallback);

std::uint64_t jsonUintField(std::string_view line, std::string_view name);

bool lineContains(std::string_view line, std::string_view needle);

} // namespace lambda_shell
