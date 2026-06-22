#include <Lambda/UI/Detail/TraversalContext.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>

namespace lambda::detail {

TraversalContext::TraversalContext() {
  frames_.push_back(Frame{});
  keyStack_.reserve(16);
  explicitChildLocalIdStack_.reserve(4);
  savedChildIndices_.reserve(16);
  pushedChildIndexKeyStack_.reserve(16);
  pushedCompositeKeyTailStack_.reserve(8);
}

TraversalContext::Frame const& TraversalContext::frame() const {
  return frames_.back();
}

void TraversalContext::pushFrame(LayoutConstraints const& constraints, LayoutHints hints, Point origin,
                                 ComponentKey key, Size assignedSize, bool hasAssignedWidth,
                                 bool hasAssignedHeight) {
#ifndef NDEBUG
  assert(std::isfinite(constraints.minWidth) && std::isfinite(constraints.minHeight));
  assert(constraints.minWidth <= constraints.maxWidth);
  assert(constraints.minHeight <= constraints.maxHeight);
#endif
  frames_.push_back(Frame{
      .constraints = constraints,
      .hints = hints,
      .origin = origin,
      .assignedSize = assignedSize,
      .hasAssignedWidth = hasAssignedWidth,
      .hasAssignedHeight = hasAssignedHeight,
      .key = std::move(key),
  });
}

void TraversalContext::popFrame() {
  if (frames_.size() > 1) {
    frames_.pop_back();
  }
}

void TraversalContext::pushChildIndex(bool pushKeySegment) {
  if (pushKeySegment) {
    LocalId const localId = LocalId::fromIndex(nextChildIndex_);
    keyStack_.push_back(localId);
    keyPrefix_.push_back(localId);
  }
  savedChildIndices_.push_back(nextChildIndex_);
  pushedChildIndexKeyStack_.push_back(pushKeySegment);
  nextChildIndex_ = 0;
}

void TraversalContext::pushChildIndexWithLocalId(LocalId localId) {
  keyStack_.push_back(localId);
  keyPrefix_.push_back(localId);
  savedChildIndices_.push_back(nextChildIndex_);
  pushedChildIndexKeyStack_.push_back(true);
  nextChildIndex_ = 0;
}

void TraversalContext::popChildIndex() {
  assert(!pushedChildIndexKeyStack_.empty());
  if (pushedChildIndexKeyStack_.back()) {
    keyStack_.pop_back();
    keyPrefix_.pop_back();
  }
  pushedChildIndexKeyStack_.pop_back();
  nextChildIndex_ = savedChildIndices_.back();
  savedChildIndices_.pop_back();
}

void TraversalContext::setChildIndex(std::size_t index) {
  nextChildIndex_ = index;
}

void TraversalContext::pushExplicitChildLocalId(std::optional<LocalId> localId) {
  explicitChildLocalIdStack_.push_back(std::move(localId));
}

void TraversalContext::popExplicitChildLocalId() {
#ifndef NDEBUG
  assert(!explicitChildLocalIdStack_.empty());
#endif
  explicitChildLocalIdStack_.pop_back();
}

LocalId TraversalContext::currentChildLocalId() const {
  if (!explicitChildLocalIdStack_.empty() && explicitChildLocalIdStack_.back().has_value()) {
    return *explicitChildLocalIdStack_.back();
  }
  return LocalId::fromIndex(nextChildIndex_);
}

ComponentKey TraversalContext::nextCompositeKey() {
  if (useMeasurementRootKey_) {
    useMeasurementRootKey_ = false;
    return measurementRootKey_;
  }
  ComponentKey key{keyPrefix_, currentChildLocalId()};
  ++nextChildIndex_;
  return key;
}

void TraversalContext::advanceChildSlot() {
  ++nextChildIndex_;
}

ComponentKey TraversalContext::currentElementKey() const {
  return ComponentKey{keyPrefix_, currentChildLocalId()};
}

LocalId TraversalContext::currentElementLocalId() const {
  return currentChildLocalId();
}

void TraversalContext::rewindChildKeyIndex() {
  nextChildIndex_ = 0;
}

void TraversalContext::resetTraversalState(ComponentKey const& key) {
  measurementRootKey_ = key;
  useMeasurementRootKey_ = false;
  keyPrefix_.clear();
  keyStack_.clear();
  savedChildIndices_.clear();
  pushedChildIndexKeyStack_.clear();
  pushedCompositeKeyTailStack_.clear();
  explicitChildLocalIdStack_.clear();
  nextChildIndex_ = 0;
  frames_.back().key = key;
  if (!key.empty()) {
    std::size_t const prefixSize = key.size() - 1;
    keyPrefix_ = key.prefix(prefixSize);
    keyStack_.reserve(std::max(keyStack_.capacity(), prefixSize));
    key.appendPrefixTo(keyStack_, prefixSize);
    LocalId const local = key.tail();
    if (local.kind == LocalId::Kind::Positional) {
      nextChildIndex_ = local.value == 0 ? 0u : static_cast<std::size_t>(local.value - 1ull);
    } else {
      explicitChildLocalIdStack_.reserve(std::max(explicitChildLocalIdStack_.capacity(), std::size_t{1}));
      explicitChildLocalIdStack_.push_back(local);
    }
  }
}

void TraversalContext::setMeasurementRootKey(ComponentKey key) {
  measurementRootKey_ = std::move(key);
  useMeasurementRootKey_ = true;
}

void TraversalContext::clearMeasurementRootKey() noexcept {
  useMeasurementRootKey_ = false;
}

void TraversalContext::pushCompositeKeyTail(ComponentKey const& compositeKey) {
  savedChildIndices_.push_back(nextChildIndex_);
  bool const pushedKeyTail = !compositeKey.empty();
  pushedCompositeKeyTailStack_.push_back(pushedKeyTail);
  if (pushedKeyTail) {
    LocalId const tail = compositeKey.tail();
    keyStack_.push_back(tail);
    keyPrefix_.push_back(tail);
  }
  nextChildIndex_ = 0;
}

void TraversalContext::popCompositeKeyTail() {
  assert(!pushedCompositeKeyTailStack_.empty());
  if (pushedCompositeKeyTailStack_.back()) {
    assert(!keyStack_.empty());
    keyStack_.pop_back();
    keyPrefix_.pop_back();
  }
  pushedCompositeKeyTailStack_.pop_back();
  assert(!savedChildIndices_.empty());
  nextChildIndex_ = savedChildIndices_.back();
  savedChildIndices_.pop_back();
}

} // namespace lambda::detail
