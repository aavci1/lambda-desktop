#pragma once

/// \file Lambda/UI/MeasureContext.hpp
///
/// Context for \ref Element::measure during retained-scene layout.

#include <Lambda/Core/Identity.hpp>
#include <Lambda/UI/Detail/TraversalContext.hpp>
#include <Lambda/UI/EnvironmentBinding.hpp>
#include <Lambda/Layout/LayoutEngine.hpp>

#include <cstddef>
#include <optional>

namespace lambdaui {

class TextSystem;
class Element;
namespace detail {
struct ElementModifiers;
}

class MeasureContext {
public:
  explicit MeasureContext(TextSystem& ts, EnvironmentBinding environment = {});
  ~MeasureContext();

  TextSystem& textSystem();
  EnvironmentBinding const& environmentBinding() const noexcept { return environmentBinding_; }
  void setEnvironmentBinding(EnvironmentBinding environment) { environmentBinding_ = std::move(environment); }

  LayoutConstraints const& constraints() const;
  LayoutHints const& hints() const;
  void pushConstraints(LayoutConstraints const& c, LayoutHints hints = {});
  void popConstraints();

  void pushChildIndex(bool pushKeySegment = true);
  void pushChildIndexWithLocalId(LocalId localId);
  void popChildIndex();

  void setChildIndex(std::size_t index);
  void pushExplicitChildLocalId(std::optional<LocalId> localId);
  void popExplicitChildLocalId();

  ComponentKey nextCompositeKey();

  void advanceChildSlot();
  ComponentKey currentElementKey() const;
  void rewindChildKeyIndex();
  void resetTraversalState(ComponentKey const& key = {});
  void setMeasurementRootKey(ComponentKey key);
  void clearMeasurementRootKey() noexcept;

  void pushCompositeKeyTail(ComponentKey const& compositeKey);
  void popCompositeKeyTail();

  void setCurrentElement(Element const* el) noexcept { currentElement_ = el; }
  [[nodiscard]] Element const* currentElement() const noexcept { return currentElement_; }

#ifndef NDEBUG
  std::size_t debugConstraintStackDepth() const noexcept { return traversal_.debugFrameDepth(); }
  std::size_t debugKeyPathDepth() const noexcept { return traversal_.debugKeyPathDepth(); }
  std::size_t debugSavedChildDepth() const noexcept { return traversal_.debugSavedChildDepth(); }
#endif
  LocalId peekCurrentChildLocalId() const {
    return traversal_.currentElementLocalId();
  }

protected:
  TextSystem& textSystem_;
  EnvironmentBinding environmentBinding_;
  detail::TraversalContext traversal_{};
  Element const* currentElement_{nullptr};
};

namespace detail {

MeasureContext* currentMeasureContext() noexcept;

class CurrentMeasureContextScope {
public:
  explicit CurrentMeasureContextScope(MeasureContext& ctx) noexcept;
  CurrentMeasureContextScope(CurrentMeasureContextScope const&) = delete;
  CurrentMeasureContextScope& operator=(CurrentMeasureContextScope const&) = delete;
  ~CurrentMeasureContextScope();

private:
  MeasureContext* previous_ = nullptr;
};

} // namespace detail
} // namespace lambdaui
