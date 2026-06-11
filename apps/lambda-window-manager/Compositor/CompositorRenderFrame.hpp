#pragma once

#include "Compositor/CompositorPresentation.hpp"
#include "Compositor/Config/AppliedCompositorConfig.hpp"
#include "Compositor/Config/CompositorConfig.hpp"
#include "Compositor/Presenter.hpp"
#include "Compositor/SceneGraph/CompositorSceneGraph.hpp"
#include "Compositor/WaylandServer.hpp"

#include <Lambda/Graphics/Canvas.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace lambda {
class FreeTypeTextSystem;
}

namespace lambda::compositor {

struct SurfaceRenderState;
struct CursorRenderState;

namespace presentation {
struct CompositorFrameProfile;
struct LoopInstrumentation;
}

struct AtomicReadyFrame {
  bool ready = false;
  std::uint32_t presentToken = 0;
  PresentationTiming timing{};
  std::size_t surfaceCount = 0;
  std::chrono::steady_clock::time_point frameTime{};
  double renderMs = 0.0;
  bool renderedAhead = false;
  bool overlayOnly = false;
  bool directScanout = false;
  std::uint64_t contentSerial = 0;
  presentation::AtomicFrameProfile profile{};
  CompositorSceneGraphState sceneGraphState;
  bool sceneGraphStateValid = false;
  std::vector<std::uint64_t> frameCallbackSurfaceIds;
  std::shared_ptr<platform::KmsAtomicPresenter::OverlayCandidate> scanoutCandidate;
};

struct VulkanDisplayPendingFrameCallbacks {
  std::uint32_t backendPresentId = 0;
  std::uint32_t queuedAtMs = 0;
  std::vector<std::uint64_t> frameCallbackSurfaceIds;
};

struct CompositorRenderFrameContext {
  WaylandServer& wayland;
  lambda::platform::KmsOutput const& output;
  Presenter& presenter;
  lambda::Canvas& canvas;
  lambda::FreeTypeTextSystem& textSystem;
  AppliedCompositorConfig& appliedConfig;
  SurfaceRenderState& surfaceRenderState;
  CursorRenderState& cursorState;
  presentation::CompositorFrameProfile& frameProfile;
  presentation::LoopInstrumentation& loopStats;
  bool idleBlanked = false;
  bool hardwareCursorAvailable = false;
  float screenshotFlashOpacity = 0.f;
  bool detailedFrameProfile = false;
  std::vector<CommittedSurfaceSnapshot> const* committedSurfaces = nullptr;
  std::optional<CommittedSurfaceSnapshot> const* softwareCursorSnapshot = nullptr;
  AtomicReadyFrame* atomicReadyFrame = nullptr;
  bool* atomicFrameDirty = nullptr;
  std::uint64_t* lastKnownContentSerial = nullptr;
  bool vulkanDisplayTimingSupportLogged = false;
  bool useVulkanPresentationCompletion = false;
  std::vector<VulkanDisplayPendingFrameCallbacks>* vulkanDisplayPendingFrameCallbacks = nullptr;
};

[[nodiscard]] std::vector<PresentationCompletion> pollVulkanDisplayPresentationCompletions(
    Presenter& presenter,
    bool& timingSupportLogged,
    bool& usePresentationCompletion);

bool completeVulkanDisplayPendingFrameCallbacks(
    WaylandServer& wayland,
    lambda::platform::KmsOutput const& output,
    std::vector<VulkanDisplayPendingFrameCallbacks>& pendingCallbacks,
    std::vector<PresentationCompletion> const& completions,
    std::uint32_t nowMs);

void renderCompositorFrame(CompositorRenderFrameContext& ctx,
                           std::chrono::steady_clock::time_point frameTime,
                           std::chrono::steady_clock::time_point renderStart,
                           PresentationTiming presentationTiming,
                           bool renderAheadFrame);

} // namespace lambda::compositor
