#include <Lambda/UI/MountRoot.hpp>

#include <Lambda/UI/Detail/RootHolder.hpp>
#include <Lambda/Graphics/TextSystem.hpp>
#include <Lambda/SceneGraph/SceneGraph.hpp>
#include <Lambda/UI/Detail/LayoutDebugDump.hpp>
#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/MeasureContext.hpp>
#include <Lambda/UI/MountContext.hpp>

#include "Detail/ResizeTrace.hpp"

#include <chrono>
#include <utility>

namespace lambda {

namespace {

LayoutConstraints rootConstraints(Size size) {
  return LayoutConstraints{
      .maxWidth = size.width,
      .maxHeight = size.height,
      .minWidth = size.width,
      .minHeight = size.height,
  };
}

} // namespace

MountRoot::MountRoot(std::unique_ptr<RootHolder> root, TextSystem& textSystem,
                     EnvironmentBinding environment, Size viewportSize,
                     Reactive::SmallFn<void()> requestRedraw)
    : root_(std::move(root))
    , textSystem_(textSystem)
    , environment_(std::move(environment))
    , viewportSize_(viewportSize)
    , requestRedraw_(std::move(requestRedraw)) {}

MountRoot::~MountRoot() = default;

void MountRoot::mount(scenegraph::SceneGraph& sceneGraph) {
  if (!root_) {
    return;
  }
  layoutDebugAttachSceneGraph(&sceneGraph);
  layoutDebugBeginPass();
  if (mounted_) {
    unmount(sceneGraph);
  }

  MeasureContext measureContext{textSystem_, environment_};
  MountContext context{rootScope_, textSystem_, measureContext,
                       rootConstraints(viewportSize_), LayoutHints{}, requestRedraw_,
                       environment_};

  auto node = Reactive::withOwner(rootScope_, [&] {
    detail::CurrentMountContextScope const currentMountContext{context};
    detail::HookLayoutScope const hookScope{rootConstraints(viewportSize_)};
    Element rootElement = root_->makeElement();
    detail::HookInteractionSignalScope const interactionScope{rootScope_};
    return rootElement.mount(context);
  });
  if (node) {
    sceneGraph.setRoot(std::move(node));
    layoutDebugDumpRetained(sceneGraph);
    mounted_ = true;
  }
  layoutDebugEndPass();
}

void MountRoot::unmount(scenegraph::SceneGraph& sceneGraph) {
  rootScope_.dispose();
  sceneGraph.releaseRoot();
  layoutDebugAttachSceneGraph(&sceneGraph);
  rootScope_ = Reactive::Scope{};
  mounted_ = false;
}

void MountRoot::resize(Size viewportSize, scenegraph::SceneGraph& sceneGraph) {
  bool const traceResize = detail::resizeTraceEnabled();
  auto const resizeStart = traceResize ? std::chrono::steady_clock::now()
                                       : std::chrono::steady_clock::time_point{};
  viewportSize_ = viewportSize;
  layoutDebugAttachSceneGraph(&sceneGraph);
  if (!mounted_) {
    mount(sceneGraph);
    if (traceResize) {
      auto const elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - resizeStart).count();
      LAMBDA_RESIZE_TRACE("mount-root",
                          "resize-mounted size=%.0fx%.0f elapsed=%.3fms\n",
                          viewportSize_.width,
                          viewportSize_.height,
                          static_cast<double>(elapsed) / 1000.0);
    }
    return;
  }
  layoutDebugBeginPass();
  auto const relayoutStart = traceResize ? std::chrono::steady_clock::now()
                                         : std::chrono::steady_clock::time_point{};
  if (!sceneGraph.root().relayout(rootConstraints(viewportSize_))) {
    sceneGraph.root().setSize(viewportSize_);
  }
  std::int64_t relayoutElapsed = 0;
  if (traceResize) {
    relayoutElapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - relayoutStart).count();
  }
  layoutDebugDumpRetained(sceneGraph);
  layoutDebugEndPass();
  if (traceResize) {
    auto const elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - resizeStart).count();
    LAMBDA_RESIZE_TRACE("mount-root",
                        "resize size=%.0fx%.0f relayout=%.3fms elapsed=%.3fms\n",
                        viewportSize_.width,
                        viewportSize_.height,
                        static_cast<double>(relayoutElapsed) / 1000.0,
                        static_cast<double>(elapsed) / 1000.0);
  }
}

} // namespace lambda
