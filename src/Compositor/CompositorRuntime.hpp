#pragma once

#include <atomic>

namespace flux::compositor {

int runKmsCompositor(std::atomic<bool>& running);

} // namespace flux::compositor
