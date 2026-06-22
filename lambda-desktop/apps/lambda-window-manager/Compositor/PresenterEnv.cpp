#include "Compositor/Presenter.hpp"

#include <cstdlib>
#include <cstring>

namespace lambda::compositor {

bool forceVulkanDisplayPresenter() {
  char const* value = std::getenv("LAMBDA_WINDOW_MANAGER_PRESENT");
  return value && (std::strcmp(value, "vulkan") == 0 || std::strcmp(value, "vulkan-display") == 0);
}

} // namespace lambda::compositor
