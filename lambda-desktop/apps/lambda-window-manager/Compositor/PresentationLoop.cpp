#include "Compositor/PresentationLoop.hpp"
#include "Compositor/OutputSelector.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace lambda::compositor::presentation {

PresentationTiming presentationTimingFromVblank(platform::KmsOutput::VblankTiming const& vblank,
                                                std::uint32_t refreshMilliHz,
                                                std::uint64_t fallbackSequence) {
  return PresentationTiming{
      .monotonicNsec = vblank.monotonicNsec != 0 ? vblank.monotonicNsec : monotonicNanoseconds(),
      .sequence = vblank.hardware ? vblank.sequence : fallbackSequence,
      .refreshNsec = refreshNsec(refreshMilliHz),
      .flags = vblank.hardware ? static_cast<std::uint32_t>(WP_PRESENTATION_FEEDBACK_KIND_VSYNC |
                                                            WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK)
                               : 0u,
  };
}

void printOutputs(std::vector<lambda::platform::KmsOutput> const& outputs) {
  std::fprintf(stderr, "lambda-window-manager: connected KMS outputs:\n");
  for (std::size_t i = 0; i < outputs.size(); ++i) {
    auto const& output = outputs[i];
    std::fprintf(stderr,
                 "lambda-window-manager:   [%zu] %s %ux%u @ %.3f Hz\n",
                 i,
                 output.name().c_str(),
                 output.width(),
                 output.height(),
                 static_cast<double>(output.refreshRateMilliHz()) / 1000.0);
  }
}

std::optional<std::size_t> selectOutputIndex(std::vector<lambda::platform::KmsOutput> const& outputs,
                                             std::optional<std::string> const& selector) {
  std::vector<std::string> names;
  names.reserve(outputs.size());
  for (auto const& output : outputs) names.push_back(output.name());
  return selectOutputNameIndex(std::span<std::string const>(names.data(), names.size()), selector);
}

} // namespace lambda::compositor::presentation
