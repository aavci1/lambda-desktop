#pragma once

#include <Lambda/UI/Environment.hpp>

namespace lambdaui {

LAMBDA_DEFINE_ENVIRONMENT_KEY(SharedEnvironmentTestKey, int, 17);

} // namespace lambdaui

namespace lambdaui::tests {

std::uint16_t sharedEnvironmentTestKeyIndexFromOtherTranslationUnit();

} // namespace lambdaui::tests
