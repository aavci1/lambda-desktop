#pragma once

#include <atomic>

namespace flux::compositor {

struct KmsCompositorOptions {
  bool listOutputs = false;
};

int runKmsCompositor(std::atomic<bool>& running, KmsCompositorOptions options = {});

} // namespace flux::compositor
