#pragma once

#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Layout/Alignment.hpp>

#include <optional>
#include <span>
#include <vector>

namespace lambda::layout {

enum class StackAxis {
  Vertical,
  Horizontal,
};

struct StackMainAxisChild {
  float naturalMainSize = 0.f;
  std::optional<float> flexBasis{};
  float minMainSize = 0.f;
  float flexGrow = 0.f;
  float flexShrink = 0.f;
};

struct StackMainAxisLayout {
  std::vector<float> mainSizes{};
  float containerMainSize = 0.f;
  float usedMainSize = 0.f;
  float startOffset = 0.f;
  float itemSpacing = 0.f;
  bool constrained = false;
};

struct StackSlot {
  Point origin{};
  Size assignedSize{};
};

struct StackLayoutResult {
  Size containerSize{};
  std::vector<StackSlot> slots{};
};

StackMainAxisLayout layoutStackMainAxis(std::span<StackMainAxisChild const> children, float spacing,
                                        float assignedMainSize, bool hasAssignedMainSize,
                                        JustifyContent justifyContent = JustifyContent::Start);

StackLayoutResult layoutStack(StackAxis axis, Alignment crossAlignment,
                              std::span<Size const> measuredSizes,
                              std::span<float const> mainSizes,
                              float spacing,
                              float containerMainSize,
                              float startOffset,
                              float assignedCrossSize,
                              bool hasAssignedCrossSize);

} // namespace lambda::layout
