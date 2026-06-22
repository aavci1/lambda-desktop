#pragma once

#include <atomic>

namespace lambda::compositor {

struct KmsCompositorOptions {
  bool listOutputs = false;
};

int runKmsCompositor(std::atomic<bool>& running, KmsCompositorOptions options = {});

} // namespace lambda::compositor
