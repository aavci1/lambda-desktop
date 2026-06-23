#pragma once

#include "Compositor/Chrome/ChromeConfig.hpp"
#include "Compositor/SceneDamage.hpp"
#include "Compositor/Surface/CommittedSurfacePainter.hpp"

#include <Lambda/Platform/Linux/KmsOutput.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace lambdaui::compositor {

enum class CompositorSceneNodeKind : std::uint8_t {
  Background,
  WindowShadow,
  WindowChrome,
  WindowChromeControls,
  WindowContent,
  SoftwareCursor,
};

struct CompositorSceneNodeSnapshot {
  std::uint64_t id = 0;
  std::uint64_t surfaceId = 0;
  CompositorSceneNodeKind kind = CompositorSceneNodeKind::Background;
  CommittedSurfaceSnapshot::RegionRect bounds{};
  std::uint64_t signature = 0;
  bool primaryPlane = true;
};

struct RetainedSceneSurfaceSnapshot {
  std::uint64_t id = 0;
  std::uint64_t serial = 0;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::int32_t committedWidth = 0;
  std::int32_t committedHeight = 0;
  std::int32_t bufferWidth = 0;
  std::int32_t bufferHeight = 0;
  std::int32_t bufferTransform = 0;
  float sourceX = 0.f;
  float sourceY = 0.f;
  float sourceWidth = 0.f;
  float sourceHeight = 0.f;
  std::int32_t destinationWidth = 0;
  std::int32_t destinationHeight = 0;
  std::uint64_t chromeSignature = 0;
};

struct CompositorSceneGraphState {
  bool initialized = false;
  std::int32_t outputWidth = 0;
  std::int32_t outputHeight = 0;
  std::vector<CompositorSceneNodeSnapshot> nodes;
  std::vector<RetainedSceneSurfaceSnapshot> surfaces;
  std::optional<RetainedSceneSurfaceSnapshot> cursor;
  std::unordered_map<std::uint64_t, std::uint64_t> rejectedScanoutSignaturesBySurface;
  std::uint64_t primaryReuseSignature = 0;
  std::uint64_t primaryReuseOverlaySurfaceId = 0;
  bool primaryReuseSignatureValid = false;
};

struct CompositorHardwareScanoutSelection {
  std::size_t surfaceIndex = 0;
  std::uint64_t signature = 0;
  platform::KmsAtomicPresenter::OverlayCandidate candidate{};
};

struct CompositorSceneFramePlan {
  SceneDamageResult damage;
  CompositorSceneGraphState nextState;
  std::vector<std::uint64_t> frameCallbackSurfaceIds;
  std::optional<CompositorHardwareScanoutSelection> scanout;
  std::uint64_t primaryReuseSignatureForScanout = 0;
  bool primaryReuseMatchesScanout = false;
};

struct CompositorSceneFrameInput {
  using DmabufFdDuplicator = std::function<std::vector<int>(std::uint64_t)>;

  platform::KmsOutput const* output = nullptr;
  platform::KmsAtomicPresenter* atomicPresenter = nullptr;
  DmabufFdDuplicator duplicateDmabufFds;
  ChromeConfig const& chrome;
  std::unordered_map<std::uint64_t, SurfaceVisualState> const& surfaceVisuals;
  std::vector<CommittedSurfaceSnapshot> const& surfaces;
  std::optional<CommittedSurfaceSnapshot> const& softwareCursor;
  std::chrono::steady_clock::time_point frameTime{};
  std::int32_t logicalOutputWidth = 0;
  std::int32_t logicalOutputHeight = 0;
  float dpiScale = 1.f;
  bool animationsEnabled = true;
  bool forceFullDamage = false;
  bool selectScanout = true;
};

[[nodiscard]] CompositorSceneFramePlan
buildCompositorSceneFrame(CompositorSceneGraphState const& previous,
                          CompositorSceneFrameInput const& input);

void rejectCompositorSceneScanout(CompositorSceneGraphState& state,
                                  CompositorHardwareScanoutSelection const& selection);

void rememberCompositorScenePrimaryReuse(CompositorSceneGraphState& state,
                                         std::uint64_t overlaySurfaceId,
                                         std::uint64_t primaryReuseSignature);

void clearCompositorScenePrimaryReuse(CompositorSceneGraphState& state);

[[nodiscard]] bool compositorSceneScanoutCoversOutput(CompositorHardwareScanoutSelection const& selection,
                                                      platform::KmsOutput const& output);

[[nodiscard]] platform::KmsAtomicPresenter::OverlayCandidate
duplicateCompositorSceneOverlayCandidate(platform::KmsAtomicPresenter::OverlayCandidate const& candidate);

void closeCompositorSceneOverlayCandidate(platform::KmsAtomicPresenter::OverlayCandidate& candidate) noexcept;

[[nodiscard]] std::shared_ptr<platform::KmsAtomicPresenter::OverlayCandidate>
ownCompositorSceneOverlayCandidate(platform::KmsAtomicPresenter::OverlayCandidate candidate);

} // namespace lambdaui::compositor
