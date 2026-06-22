#pragma once

#include "Compositor/CompositorPresentation.hpp"
#include "Compositor/Wayland/WaylandTypes.hpp"

#include <Lambda/Platform/Linux/KmsOutput.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "presentation-time-server-protocol.h"

namespace lambda::compositor::presentation {

[[nodiscard]] PresentationTiming presentationTimingFromVblank(platform::KmsOutput::VblankTiming const& vblank,
                                                            std::uint32_t refreshMilliHz,
                                                            std::uint64_t fallbackSequence);

void printOutputs(std::vector<lambda::platform::KmsOutput> const& outputs);
[[nodiscard]] std::optional<std::size_t> selectOutputIndex(std::vector<lambda::platform::KmsOutput> const& outputs,
                                                           std::optional<std::string> const& selector);

} // namespace lambda::compositor::presentation
