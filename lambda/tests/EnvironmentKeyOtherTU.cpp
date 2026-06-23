#include "EnvironmentKeyTestSupport.hpp"

namespace lambdaui::tests {

std::uint16_t sharedEnvironmentTestKeyIndexFromOtherTranslationUnit() {
  return EnvironmentKey<lambdaui::SharedEnvironmentTestKey>::slot().index();
}

} // namespace lambdaui::tests
