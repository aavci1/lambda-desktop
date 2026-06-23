#include <Lambda/UI/MeasureContext.hpp>

#include <cassert>
#include <cmath>
#include <utility>

#include <Lambda/Graphics/TextSystem.hpp>

namespace lambdaui {

namespace {

thread_local MeasureContext* sCurrentMeasureContext = nullptr;

} // namespace

MeasureContext::MeasureContext(TextSystem& ts, EnvironmentBinding environment)
    : textSystem_(ts)
    , environmentBinding_(std::move(environment)) {}

MeasureContext::~MeasureContext() = default;

TextSystem& MeasureContext::textSystem() { return textSystem_; }

LayoutConstraints const& MeasureContext::constraints() const { return traversal_.frame().constraints; }

LayoutHints const& MeasureContext::hints() const { return traversal_.frame().hints; }

void MeasureContext::pushConstraints(LayoutConstraints const& c, LayoutHints hints) {
  auto const& frame = traversal_.frame();
  traversal_.pushFrame(c, std::move(hints), frame.origin, frame.key, frame.assignedSize, frame.hasAssignedWidth,
                       frame.hasAssignedHeight);
}

void MeasureContext::popConstraints() {
  traversal_.popFrame();
}

void MeasureContext::pushChildIndex(bool pushKeySegment) {
  traversal_.pushChildIndex(pushKeySegment);
}

void MeasureContext::pushChildIndexWithLocalId(LocalId localId) {
  traversal_.pushChildIndexWithLocalId(localId);
}

void MeasureContext::popChildIndex() {
  traversal_.popChildIndex();
}

void MeasureContext::setChildIndex(std::size_t index) { traversal_.setChildIndex(index); }

void MeasureContext::pushExplicitChildLocalId(std::optional<LocalId> localId) {
  traversal_.pushExplicitChildLocalId(std::move(localId));
}

void MeasureContext::popExplicitChildLocalId() {
  traversal_.popExplicitChildLocalId();
}

ComponentKey MeasureContext::nextCompositeKey() { return traversal_.nextCompositeKey(); }

void MeasureContext::advanceChildSlot() { traversal_.advanceChildSlot(); }

ComponentKey MeasureContext::currentElementKey() const { return traversal_.currentElementKey(); }

void MeasureContext::rewindChildKeyIndex() { traversal_.rewindChildKeyIndex(); }

void MeasureContext::resetTraversalState(ComponentKey const& key) {
  traversal_.resetTraversalState(key);
}

void MeasureContext::setMeasurementRootKey(ComponentKey key) {
  traversal_.setMeasurementRootKey(std::move(key));
}

void MeasureContext::clearMeasurementRootKey() noexcept {
  traversal_.clearMeasurementRootKey();
}

void MeasureContext::pushCompositeKeyTail(ComponentKey const& compositeKey) {
  traversal_.pushCompositeKeyTail(compositeKey);
}

void MeasureContext::popCompositeKeyTail() {
  traversal_.popCompositeKeyTail();
}

namespace detail {

MeasureContext* currentMeasureContext() noexcept {
  return sCurrentMeasureContext;
}

CurrentMeasureContextScope::CurrentMeasureContextScope(MeasureContext& ctx) noexcept
    : previous_(sCurrentMeasureContext) {
  sCurrentMeasureContext = &ctx;
}

CurrentMeasureContextScope::~CurrentMeasureContextScope() {
  sCurrentMeasureContext = previous_;
}

} // namespace detail

} // namespace lambdaui
