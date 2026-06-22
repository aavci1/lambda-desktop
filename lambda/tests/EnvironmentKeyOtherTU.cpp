#include "EnvironmentKeyTestSupport.hpp"

namespace lambda::tests {

std::uint16_t sharedEnvironmentTestKeyIndexFromOtherTranslationUnit() {
  return EnvironmentKey<lambda::SharedEnvironmentTestKey>::slot().index();
}

} // namespace lambda::tests
