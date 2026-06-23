#include "Layout/Algorithms/StackLayout.hpp"

#include "Layout/LayoutHelpers.hpp"

#include <algorithm>

namespace lambdaui::layout {

namespace {

float resolveSafeCenterOffset(float freeSpace) {
  return freeSpace < 0.f ? 0.f : freeSpace * 0.5f;
}

float stackFlexBaseSize(StackMainAxisChild const& child) {
  return std::max(0.f, child.flexBasis.value_or(child.naturalMainSize));
}

} // namespace

StackMainAxisLayout layoutStackMainAxis(std::span<StackMainAxisChild const> children, float spacing,
                                        float assignedMainSize, bool hasAssignedMainSize,
                                        JustifyContent justifyContent) {
  StackMainAxisLayout result{};
  result.mainSizes.reserve(children.size());
  result.constrained = hasAssignedMainSize;
  result.itemSpacing = spacing;

  for (StackMainAxisChild const& child : children) {
    result.mainSizes.push_back(std::max(stackFlexBaseSize(child), child.minMainSize));
  }

  if (result.constrained && !result.mainSizes.empty()) {
    float const gaps = children.size() > 1 ? static_cast<float>(children.size() - 1) * spacing : 0.f;
    float const targetSum = std::max(0.f, assignedMainSize - gaps);
    float sumBase = 0.f;
    for (float size : result.mainSizes) {
      sumBase += size;
    }

    float const extra = targetSum - sumBase;
    if (extra > kFlexEpsilon) {
      float totalGrow = 0.f;
      for (StackMainAxisChild const& child : children) {
        totalGrow += child.flexGrow;
      }
      if (totalGrow > kFlexEpsilon) {
        for (std::size_t i = 0; i < children.size(); ++i) {
          if (children[i].flexGrow > 0.f) {
            result.mainSizes[i] += extra * (children[i].flexGrow / totalGrow);
          }
        }
      }
    } else if (extra < -kFlexEpsilon) {
      std::vector<float> const shrinkBaseSizes = result.mainSizes;
      for (;;) {
        float allocatedSum = 0.f;
        for (float size : result.mainSizes) {
          allocatedSum += size;
        }
        float const need = allocatedSum - targetSum;
        if (need <= kFlexEpsilon) {
          break;
        }

        float shrinkBasis = 0.f;
        for (std::size_t i = 0; i < children.size(); ++i) {
          if (children[i].flexShrink > 0.f &&
              result.mainSizes[i] > children[i].minMainSize + kFlexEpsilon) {
            shrinkBasis += children[i].flexShrink * shrinkBaseSizes[i];
          }
        }
        if (shrinkBasis <= 1e-6f) {
          break;
        }

        float removedThisPass = 0.f;
        for (std::size_t i = 0; i < children.size(); ++i) {
          if (children[i].flexShrink <= 0.f ||
              result.mainSizes[i] <= children[i].minMainSize + kFlexEpsilon) {
            continue;
          }
          float const remove =
              need * ((children[i].flexShrink * shrinkBaseSizes[i]) / shrinkBasis);
          float const nextSize = result.mainSizes[i] - remove;
          if (nextSize < children[i].minMainSize) {
            removedThisPass += result.mainSizes[i] - children[i].minMainSize;
            result.mainSizes[i] = children[i].minMainSize;
          } else {
            removedThisPass += remove;
            result.mainSizes[i] = nextSize;
          }
        }
        if (removedThisPass < kFlexEpsilon) {
          break;
        }
      }
    }
  }

  result.usedMainSize = children.size() > 1 ? static_cast<float>(children.size() - 1) * spacing : 0.f;
  for (float size : result.mainSizes) {
    result.usedMainSize += size;
  }

  result.containerMainSize = result.constrained ? std::max(0.f, assignedMainSize) : std::max(0.f, result.usedMainSize);
  result.startOffset = 0.f;
  if (!result.constrained || result.mainSizes.empty()) {
    return result;
  }

  float const freeSpace = result.containerMainSize - result.usedMainSize;
  switch (justifyContent) {
  case JustifyContent::Start:
    break;
  case JustifyContent::Center:
    result.startOffset = freeSpace * 0.5f;
    break;
  case JustifyContent::End:
    result.startOffset = freeSpace;
    break;
  case JustifyContent::SpaceBetween:
    if (children.size() > 1 && freeSpace > 0.f) {
      result.itemSpacing += freeSpace / static_cast<float>(children.size() - 1);
    }
    break;
  case JustifyContent::SpaceAround:
    if (freeSpace < 0.f) {
      result.startOffset = resolveSafeCenterOffset(freeSpace);
      break;
    }
    if (!children.empty()) {
      float const distributedSpace = freeSpace / static_cast<float>(children.size());
      result.startOffset = distributedSpace * 0.5f;
      result.itemSpacing += distributedSpace;
    }
    break;
  case JustifyContent::SpaceEvenly:
    if (freeSpace < 0.f) {
      result.startOffset = resolveSafeCenterOffset(freeSpace);
      break;
    }
    {
      float const distributedSpace = freeSpace / static_cast<float>(children.size() + 1);
      result.startOffset = distributedSpace;
      result.itemSpacing += distributedSpace;
    }
    break;
  }
  return result;
}

StackLayoutResult layoutStack(StackAxis axis, Alignment crossAlignment,
                              std::span<Size const> measuredSizes,
                              std::span<float const> mainSizes,
                              float spacing,
                              float containerMainSize,
                              float startOffset,
                              float assignedCrossSize,
                              bool hasAssignedCrossSize) {
  StackLayoutResult result{};
  result.slots.reserve(measuredSizes.size());

  float maxCrossSize = 0.f;
  for (Size const size : measuredSizes) {
    float const crossSize = axis == StackAxis::Vertical ? size.width : size.height;
    maxCrossSize = std::max(maxCrossSize, crossSize);
  }

  float containerCrossSize = maxCrossSize;
  if (hasAssignedCrossSize) {
    containerCrossSize = std::max(0.f, assignedCrossSize);
  }

  float mainOffset = startOffset;
  for (std::size_t i = 0; i < measuredSizes.size(); ++i) {
    Size const measured = measuredSizes[i];
    float const mainSize = i < mainSizes.size() ? mainSizes[i] : 0.f;
    StackSlot slot{};
    if (axis == StackAxis::Vertical) {
      float const crossSlotSize =
          crossAlignment == Alignment::Stretch && containerCrossSize > 0.f ? containerCrossSize : measured.width;
      slot.origin = Point{
          hAlignOffset(measured.width, containerCrossSize > 0.f ? containerCrossSize : measured.width, crossAlignment),
          mainOffset,
      };
      slot.assignedSize = Size{crossSlotSize, mainSize};
    } else {
      float const crossSlotSize =
          crossAlignment == Alignment::Stretch && containerCrossSize > 0.f ? containerCrossSize : measured.height;
      float const crossSpace = containerCrossSize > 0.f ? containerCrossSize : measured.height;
      slot.origin = Point{
          mainOffset,
          vAlignOffset(measured.height, crossSpace, crossAlignment),
      };
      slot.assignedSize = Size{mainSize, crossSlotSize};
    }
    result.slots.push_back(slot);
    mainOffset += mainSize;
    if (i + 1 < measuredSizes.size()) {
      mainOffset += spacing;
    }
  }

  if (axis == StackAxis::Vertical) {
    result.containerSize = Size{containerCrossSize, containerMainSize};
  } else {
    result.containerSize = Size{containerMainSize, containerCrossSize};
  }
  return result;
}

} // namespace lambdaui::layout
