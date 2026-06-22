#pragma once

#include <Lambda/UI/Environment.hpp>

namespace lambda {

LAMBDA_DEFINE_ENVIRONMENT_KEY(SharedEnvironmentTestKey, int, 17);

} // namespace lambda

namespace lambda::tests {

std::uint16_t sharedEnvironmentTestKeyIndexFromOtherTranslationUnit();

} // namespace lambda::tests
