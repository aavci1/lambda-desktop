#pragma once

/// \file ContainerScope.hpp
///
/// RAII helpers for retained-scene layout measurement.

#include <Lambda/UI/MeasureContext.hpp>

#include <cassert>

namespace lambdaui {

class ContainerMeasureScope {
public:
  explicit ContainerMeasureScope(MeasureContext& ctx)
      : ctx_(ctx) {
#ifndef NDEBUG
    keyPathDepth0_ = ctx_.debugKeyPathDepth();
    savedDepth0_ = ctx_.debugSavedChildDepth();
#endif
    LocalId const containerLocalId = ctx_.peekCurrentChildLocalId();
    ctx_.advanceChildSlot();
    ctx_.pushChildIndexWithLocalId(containerLocalId);
    pushedChildIndex_ = true;
  }

  ~ContainerMeasureScope() {
    if (pushedChildIndex_) {
      ctx_.popChildIndex();
    }
#ifndef NDEBUG
    assert(ctx_.debugKeyPathDepth() == keyPathDepth0_);
    assert(ctx_.debugSavedChildDepth() == savedDepth0_);
#endif
  }

  ContainerMeasureScope(ContainerMeasureScope const&) = delete;
  ContainerMeasureScope& operator=(ContainerMeasureScope const&) = delete;

private:
  MeasureContext& ctx_;
  bool pushedChildIndex_ = false;
#ifndef NDEBUG
  std::size_t keyPathDepth0_{};
  std::size_t savedDepth0_{};
#endif
};

} // namespace lambdaui
