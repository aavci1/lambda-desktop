#pragma once

#include <Lambda/Core/Identity.hpp>
#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Layout/LayoutEngine.hpp>

#include <cstddef>
#include <optional>
#include <vector>

namespace lambdaui::detail {

class TraversalContext {
public:
  struct Frame {
    LayoutConstraints constraints{};
    LayoutHints hints{};
    Point origin{};
    Size assignedSize{};
    bool hasAssignedWidth = false;
    bool hasAssignedHeight = false;
    ComponentKey key{};
  };

  TraversalContext();

  [[nodiscard]] Frame const& frame() const;
  void pushFrame(LayoutConstraints const& constraints, LayoutHints hints = {}, Point origin = {},
                 ComponentKey key = {}, Size assignedSize = {}, bool hasAssignedWidth = false,
                 bool hasAssignedHeight = false);
  void popFrame();

  void pushChildIndex(bool pushKeySegment = true);
  void pushChildIndexWithLocalId(LocalId localId);
  void popChildIndex();

  void setChildIndex(std::size_t index);
  void pushExplicitChildLocalId(std::optional<LocalId> localId);
  void popExplicitChildLocalId();

  ComponentKey nextCompositeKey();

  void advanceChildSlot();
  [[nodiscard]] ComponentKey currentElementKey() const;
  [[nodiscard]] LocalId currentElementLocalId() const;
  void rewindChildKeyIndex();
  void resetTraversalState(ComponentKey const& key = {});
  void setMeasurementRootKey(ComponentKey key);
  void clearMeasurementRootKey() noexcept;

  void pushCompositeKeyTail(ComponentKey const& compositeKey);
  void popCompositeKeyTail();

#ifndef NDEBUG
  std::size_t debugFrameDepth() const noexcept { return frames_.size(); }
  std::size_t debugKeyPathDepth() const noexcept { return keyStack_.size(); }
  std::size_t debugSavedChildDepth() const noexcept { return savedChildIndices_.size(); }
#endif

private:
  [[nodiscard]] LocalId currentChildLocalId() const;

  std::vector<Frame> frames_{};
  std::vector<LocalId> keyStack_{};
  std::vector<std::optional<LocalId>> explicitChildLocalIdStack_{};
  std::vector<std::size_t> savedChildIndices_{};
  std::vector<bool> pushedChildIndexKeyStack_{};
  std::vector<bool> pushedCompositeKeyTailStack_{};
  ComponentKey keyPrefix_{};
  ComponentKey measurementRootKey_{};
  std::size_t nextChildIndex_{0};
  bool useMeasurementRootKey_{false};
};

} // namespace lambdaui::detail
