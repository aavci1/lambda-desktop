#pragma once

#include <atomic>

namespace lambdaui::compositor {

struct KmsCompositorOptions {
  bool listOutputs = false;
};

int runKmsCompositor(std::atomic<bool>& running, KmsCompositorOptions options = {});

} // namespace lambdaui::compositor
