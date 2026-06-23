#pragma once

/// \file Lambda/UI/Detail/LayoutDebugDump.hpp
///
/// Opt-in layout tree dump when \c LAMBDA_DEBUG_LAYOUT is set (stderr). Internal API.

#include <Lambda/Debug/DebugFlags.hpp>
#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Layout/LayoutEngine.hpp>

namespace lambdaui {

namespace scenegraph {
class SceneGraph;
}

namespace detail {
void layoutDebugRecordMeasureSlow(LayoutConstraints const& constraints, Size sz);
} // namespace detail

bool layoutDebugEnabled();
void layoutDebugBeginPass();
void layoutDebugEndPass();
void layoutDebugDumpRetained(scenegraph::SceneGraph const& graph);
void layoutDebugAttachSceneGraph(scenegraph::SceneGraph const* graph);
void layoutDebugDumpAttached(char const* reason);

inline void layoutDebugRecordMeasure(LayoutConstraints const& constraints, Size sz) {
  if (!debug::layoutEnabled()) {
    return;
  }
  detail::layoutDebugRecordMeasureSlow(constraints, sz);
}

} // namespace lambdaui
