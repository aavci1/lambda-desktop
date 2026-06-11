#pragma once

#include "Compositor/WaylandServer.hpp"
#include "Compositor/Window/WindowGeometry.hpp"
#include "linux-dmabuf-unstable-v1-server-protocol.h"
#include "pointer-constraints-unstable-v1-server-protocol.h"
#include "wlr-layer-shell-unstable-v1-server-protocol.h"
#include "xx-cutouts-v1-server-protocol.h"
#include "xdg-decoration-unstable-v1-server-protocol.h"
#include "xdg-shell-server-protocol.h"

#include <drm_fourcc.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

struct xkb_context;
struct xkb_keymap;
struct xkb_state;

namespace lambda::compositor {

enum class SurfaceRole : std::uint8_t {
  None,
  XdgSurface,
  XdgToplevel,
  XdgPopup,
  LayerSurface,
  Subsurface,
  Cursor,
  DragIcon,
};

enum class SeatSerialKind : std::uint8_t {
  PointerEnter,
  PointerButtonPress,
  PointerButtonRelease,
  KeyboardEnter,
  KeyboardKey,
  KeyboardModifiers,
  DataDeviceEnter,
};

struct WaylandServer::Impl {
  struct Surface;
  struct SurfaceViewportState;
  struct SurfaceRegionState;
  struct SurfacePendingRegionState;
  struct SurfaceDamageState;
  struct SurfacePendingDamageState;
  struct PointerConstraintCommitState;
  struct SurfaceBufferState;
  struct SurfacePendingBufferState;
  struct SurfacePendingCommitState;
  struct SurfaceXdgRoleState;
  struct Subsurface;
  struct XdgPositioner;
  struct XdgSurface;
  struct XdgToplevel;
  struct XdgPopup;
  struct XdgPopupGrab;
  struct ShmPool;
  struct ShmBuffer;
  struct DmabufParams;
  struct DmabufBuffer;
  struct ToplevelDecoration;
  struct XxCutouts;
  struct SeatSerialRecord;
  struct Region;
  struct BackgroundEffect;
  struct Viewport;
  struct FractionalScale;
  struct CursorShapeDevice;
  struct IdleInhibitor;
  struct XdgOutput;
  struct LayerSurface;
  struct PresentationFeedback;
  struct PendingPresentationBatch;
  struct XdgConfigure;
  struct RelativePointer;
  struct PointerConstraint;
  struct PrimarySelectionDevice;
  struct PrimarySelectionSource;
  struct PrimarySelectionOffer;
  struct DataDevice;
  struct DataSource;
  struct DataOffer;
  struct ActivationToken;
  struct ScreenshotSelectionState {
    bool active = false;
    bool dragging = false;
    float startX = 0.f;
    float startY = 0.f;
    float currentX = 0.f;
    float currentY = 0.f;
  };
  struct SeatSerialRecord {
    std::uint32_t serial = 0;
    SeatSerialKind kind = SeatSerialKind::PointerEnter;
    wl_client* client = nullptr;
    Surface* surface = nullptr;
  };
  struct XdgPopupGrab {
    wl_resource* seatResource = nullptr;
    wl_client* client = nullptr;
    std::vector<XdgPopup*> popups;
  };

  explicit Impl(WaylandOutputInfo output);
  ~Impl();

  [[nodiscard]] char const* socketName() const noexcept;
  [[nodiscard]] int eventFd() const noexcept;
  [[nodiscard]] int shellIpcFd() const noexcept;
  [[nodiscard]] float preferredScale() const noexcept;
  [[nodiscard]] std::int32_t logicalOutputWidth() const noexcept;
  [[nodiscard]] std::int32_t logicalOutputHeight() const noexcept;
  [[nodiscard]] std::size_t toplevelCount() const noexcept;
  [[nodiscard]] std::vector<CommittedSurfaceSnapshot> committedSurfaces() const;
  [[nodiscard]] std::optional<CommittedSurfaceSnapshot> cursorSurface() const;
  [[nodiscard]] std::optional<SnapPreviewSnapshot> snapPreview() const;
  [[nodiscard]] std::optional<WindowCyclerOverlaySnapshot> windowCyclerOverlay();
  [[nodiscard]] std::optional<int> windowCyclerWakeDelayMs() const;
  [[nodiscard]] std::optional<int> snapPreviewWakeDelayMs() const;
  [[nodiscard]] bool hasPendingFrameCallbacks() const noexcept;
  [[nodiscard]] std::vector<int> duplicateDmabufFds(std::uint64_t surfaceId) const;
  [[nodiscard]] bool copyDmabufToRgba(std::uint64_t surfaceId, std::vector<std::uint8_t>& out) const;
  void consumeSurfaceDamage(std::uint64_t surfaceId, std::uint64_t serial);

  void dispatch();
  void initializeShellIpc();
  void shutdownShellIpc();
  void dispatchShellIpc();
  void flushClients();
  void requestShellOpenCommandLauncher();
  void requestScreenshot(ScreenshotMode mode, std::uint32_t timeMs);
  [[nodiscard]] std::optional<ScreenshotRequest> consumeScreenshotRequest();
  [[nodiscard]] std::optional<ScreenshotSelectionOverlay> screenshotSelectionOverlay() const;
  void notifyShellStateChanged();
  bool launchShellApp(std::string const& appId);
  bool focusShellApp(std::string const& appId, std::uint32_t timeMs);
  bool focusShellWindow(std::uint64_t windowId, std::uint32_t timeMs);
  bool quitShellApp(std::string const& appId);
  bool claimCommandLauncherModal(std::uint32_t timeMs);
  void releaseCommandLauncherModal(std::uint32_t timeMs);
  void setShortcutBindings(std::vector<ShortcutBinding> bindings);
  void setChromeConfig(ChromeConfig config);
  void setChromeThemeConfig(ChromeConfig base, std::optional<ChromeConfig> dark);
  void setShellThemeDark(bool dark);
  void setInputConfig(CompositorInputConfig config);
  void setPreferredScale(float scale);
  void setDmabufFormatModifierPreferences(std::vector<DmabufFormatModifierPreference> preferences);
  void setRetainedDmabufBufferIds(std::vector<std::uint64_t> bufferIds);
  [[nodiscard]] bool bufferReleaseIsRetained(wl_resource* buffer) const;
  void queueOrphanedBufferRelease(wl_resource* buffer);
  void updateAnimations(std::uint32_t timeMs, bool animationsEnabled);
  [[nodiscard]] bool hasActiveAnimations() const noexcept;
  [[nodiscard]] bool hasActiveResizePacing() const noexcept;
  [[nodiscard]] bool hasRecentResizePacing(std::uint64_t nowNsec = 0) const noexcept;
  void noteResizePacingActivity(std::uint64_t nowNsec = 0) noexcept;
  [[nodiscard]] bool hasIdleInhibitors() const noexcept;
  void releasePendingBuffers();
  void sendFrameCallbacksOnly(std::uint32_t timeMs);
  void sendFrameCallbacksOnly(std::uint32_t timeMs, std::span<std::uint64_t const> frameSurfaceIds);
  void sendPresentationFeedbacks(std::uint32_t timeMs, PresentationTiming timing);
  void sendPresentationFeedbacks(std::uint32_t timeMs,
                                 PresentationTiming timing,
                                 std::span<std::uint64_t const> frameSurfaceIds);
  void sendFrameCallbacks(std::uint32_t timeMs, PresentationTiming timing);
  void sendFrameCallbacks(std::uint32_t timeMs,
                          PresentationTiming timing,
                          std::span<std::uint64_t const> frameSurfaceIds);
  void completePresentationFeedbacks(std::vector<PresentationCompletion> const& completions, std::uint32_t timeMs);
  void handlePointerMotion(double dx, double dy, std::uint32_t timeMs);
  void handlePointerPosition(double x, double y, std::uint32_t timeMs);
  void handlePointerButton(std::uint32_t button, bool pressed, std::uint32_t timeMs);
  void handlePointerAxis(double dx, double dy, std::uint32_t timeMs);
  void handleKeyboardKey(std::uint32_t key, bool pressed, std::uint32_t timeMs);
  void resetKeyboardState(std::uint32_t timeMs);

  wl_resource* createSurface(wl_client* client, std::uint32_t version, std::uint32_t id);
  void destroySurface(Surface* surface);
  void destroySubsurface(Subsurface* subsurface);
  void destroyXdgPositioner(XdgPositioner* positioner);
  void destroyXdgSurface(XdgSurface* surface);
  void destroyXdgToplevel(XdgToplevel* toplevel);
  void destroyXdgPopup(XdgPopup* popup);
  void destroyShmPool(ShmPool* pool);
  void destroyShmBuffer(ShmBuffer* buffer);
  void destroyDmabufParams(DmabufParams* params);
  void destroyDmabufBuffer(DmabufBuffer* buffer);
  void destroyToplevelDecoration(ToplevelDecoration* decoration);
  void destroyXxCutouts(XxCutouts* cutouts);
  void destroyRegion(Region* region);
  void destroyBackgroundEffect(BackgroundEffect* effect);
  void destroyViewport(Viewport* viewport);
  void destroyFractionalScale(FractionalScale* fractionalScale);
  void destroyCursorShapeDevice(CursorShapeDevice* device);
  void destroyIdleInhibitor(IdleInhibitor* inhibitor);
  void destroyXdgOutput(XdgOutput* output);
  void destroyLayerSurface(LayerSurface* layerSurface);
  void destroyPresentationFeedback(PresentationFeedback* feedback);
  void destroyRelativePointer(RelativePointer* relativePointer);
  void destroyPointerConstraint(PointerConstraint* constraint);
  void destroyPrimarySelectionDevice(PrimarySelectionDevice* device);
  void destroyPrimarySelectionSource(PrimarySelectionSource* source);
  void destroyPrimarySelectionOffer(PrimarySelectionOffer* offer);
  void destroyDataDevice(DataDevice* device);
  void destroyDataSource(DataSource* source);
  void destroyDataOffer(DataOffer* offer);
  void destroyActivationToken(ActivationToken* token);

  wl_display* display_ = nullptr;
  xkb_context* xkbContext_ = nullptr;
  xkb_keymap* xkbKeymap_ = nullptr;
  xkb_state* xkbState_ = nullptr;
  wl_global* compositorGlobal_ = nullptr;
  wl_global* subcompositorGlobal_ = nullptr;
  wl_global* shmGlobal_ = nullptr;
  wl_global* outputGlobal_ = nullptr;
  wl_global* seatGlobal_ = nullptr;
  wl_global* xdgWmBaseGlobal_ = nullptr;
  wl_global* linuxDmabufGlobal_ = nullptr;
  wl_global* xdgDecorationManagerGlobal_ = nullptr;
  wl_global* xdgOutputManagerGlobal_ = nullptr;
  wl_global* viewporterGlobal_ = nullptr;
  wl_global* fractionalScaleManagerGlobal_ = nullptr;
  wl_global* cursorShapeManagerGlobal_ = nullptr;
  wl_global* idleInhibitManagerGlobal_ = nullptr;
  wl_global* layerShellGlobal_ = nullptr;
  wl_global* presentationGlobal_ = nullptr;
  wl_global* relativePointerManagerGlobal_ = nullptr;
  wl_global* pointerConstraintsGlobal_ = nullptr;
  wl_global* primarySelectionManagerGlobal_ = nullptr;
  wl_global* dataDeviceManagerGlobal_ = nullptr;
  wl_global* activationGlobal_ = nullptr;
  wl_global* cutoutsManagerGlobal_ = nullptr;
  wl_global* backgroundEffectManagerGlobal_ = nullptr;
  std::string socketName_;
  std::string displayNameFile_;
  int shellListenFd_ = -1;
  int shellClientFd_ = -1;
  std::string shellSocketPath_;
  std::string shellReadBuffer_;
  bool shellHelloReceived_ = false;
  bool shellSnapshotDirty_ = false;
  WaylandOutputInfo output_;
  std::vector<std::unique_ptr<Surface>> surfaces_;
  std::vector<Surface*> focusOrder_;
  std::vector<std::unique_ptr<Subsurface>> subsurfaces_;
  std::vector<std::unique_ptr<XdgPositioner>> xdgPositioners_;
  std::vector<std::unique_ptr<XdgSurface>> xdgSurfaces_;
  std::vector<std::unique_ptr<XdgToplevel>> toplevels_;
  std::vector<std::unique_ptr<XdgPopup>> popups_;
  XdgPopupGrab popupGrab_;
  WaylandServer::Impl::XdgPopup* grabPopup_ = nullptr;
  bool popupGrabsEnabled_ = true;
  std::vector<std::unique_ptr<ShmPool>> shmPools_;
  std::vector<std::unique_ptr<ShmBuffer>> shmBuffers_;
  std::vector<std::unique_ptr<DmabufParams>> dmabufParams_;
  std::vector<std::unique_ptr<DmabufBuffer>> dmabufBuffers_;
  std::vector<DmabufFormatModifierPreference> dmabufFormatModifierPreferences_;
  std::vector<std::uint64_t> retainedDmabufBufferIds_;
  std::vector<wl_resource*> orphanedBufferReleases_;
  std::uint64_t nextDmabufBufferId_ = 1;
  std::vector<std::unique_ptr<ToplevelDecoration>> toplevelDecorations_;
  std::vector<std::unique_ptr<XxCutouts>> cutouts_;
  std::vector<std::unique_ptr<Region>> regions_;
  std::vector<std::unique_ptr<BackgroundEffect>> backgroundEffects_;
  std::vector<std::unique_ptr<Viewport>> viewports_;
  std::vector<std::unique_ptr<FractionalScale>> fractionalScales_;
  std::vector<std::unique_ptr<CursorShapeDevice>> cursorShapeDevices_;
  std::vector<std::unique_ptr<IdleInhibitor>> idleInhibitors_;
  std::vector<std::unique_ptr<LayerSurface>> layerSurfaces_;
  std::vector<std::unique_ptr<PresentationFeedback>> presentationFeedbacks_;
  std::vector<PendingPresentationBatch> pendingPresentationBatches_;
  std::vector<std::unique_ptr<RelativePointer>> relativePointers_;
  std::vector<std::unique_ptr<PointerConstraint>> pointerConstraints_;
  std::vector<std::unique_ptr<PrimarySelectionDevice>> primarySelectionDevices_;
  std::vector<std::unique_ptr<PrimarySelectionSource>> primarySelectionSources_;
  std::vector<std::unique_ptr<PrimarySelectionOffer>> primarySelectionOffers_;
  PrimarySelectionSource* primarySelectionSource_ = nullptr;
  std::vector<std::unique_ptr<DataDevice>> dataDevices_;
  std::vector<std::unique_ptr<DataSource>> dataSources_;
  std::vector<std::unique_ptr<DataOffer>> dataOffers_;
  std::vector<std::unique_ptr<ActivationToken>> activationTokens_;
  DataSource* selectionSource_ = nullptr;
  DataSource* dndSource_ = nullptr;
  Surface* dndOrigin_ = nullptr;
  Surface* dndTarget_ = nullptr;
  Surface* dndIcon_ = nullptr;
  DataOffer* dndOffer_ = nullptr;
  std::vector<wl_resource*> seatResources_;
  std::vector<wl_resource*> outputResources_;
  std::vector<std::unique_ptr<XdgOutput>> xdgOutputs_;
  std::vector<wl_resource*> pointerResources_;
  std::vector<wl_resource*> keyboardResources_;
  Surface* pointerFocus_ = nullptr;
  Surface* pointerButtonGrabSurface_ = nullptr;
  wl_client* pointerButtonGrabClient_ = nullptr;
  std::uint32_t pointerButtonCount_ = 0;
  Surface* keyboardFocus_ = nullptr;
  Surface* commandLauncherModalSurface_ = nullptr;
  Surface* dragSurface_ = nullptr;
  Surface* resizeSurface_ = nullptr;
  Surface* closePressSurface_ = nullptr;
  Surface* maximizePressSurface_ = nullptr;
  Surface* minimizePressSurface_ = nullptr;
  Surface* lastTitleClickSurface_ = nullptr;
  Surface* cursorSurface_ = nullptr;
  CursorShape cursorShape_ = CursorShape::Arrow;
  bool compositorCursorOverride_ = false;
  CursorShape compositorCursorShape_ = CursorShape::Arrow;
  std::int32_t cursorHotspotX_ = 0;
  std::int32_t cursorHotspotY_ = 0;
  std::deque<SeatSerialRecord> seatSerials_;
  float dragOffsetX_ = 0.f;
  float dragOffsetY_ = 0.f;
  std::optional<SnapTarget> dragSnapTarget_;
  std::uint32_t dragSnapTargetStartedAtMs_ = 0;
  bool snapPreviewVisible_ = false;
  bool snapPreviewDropPending_ = false;
  std::uint64_t snapPreviewSurfaceId_ = 0;
  std::uint32_t snapPreviewStartedAtMs_ = 0;
  WindowGeometry snapPreviewStartWindow_{};
  WindowGeometry snapPreviewTargetWindow_{};
  std::uint32_t lastTitleClickTimeMs_ = 0;
  float resizeStartX_ = 0.f;
  float resizeStartY_ = 0.f;
  std::int32_t resizeStartWindowX_ = 0;
  std::int32_t resizeStartWindowY_ = 0;
  std::int32_t resizeStartWidth_ = 0;
  std::int32_t resizeStartHeight_ = 0;
  std::int32_t resizeLastX_ = 0;
  std::int32_t resizeLastY_ = 0;
  std::int32_t resizeLastWidth_ = 0;
  std::int32_t resizeLastHeight_ = 0;
  std::uint32_t resizeEdges_ = 0;
  bool metaDown_ = false;
  bool ctrlDown_ = false;
  bool altDown_ = false;
  bool shiftDown_ = false;
  std::vector<Surface*> focusCycleList_;
  std::size_t focusCycleIndex_ = 0;
  std::uint32_t focusCycleStartedAtMs_ = 0;
  bool focusCycleOverlayShown_ = false;
  std::optional<ScreenshotRequest> screenshotRequest_;
  ScreenshotSelectionState screenshotSelection_;
  std::vector<ShortcutBinding> shortcutBindings_;
  ChromeConfig chromeConfig_;
  ChromeConfig chromeBaseConfig_;
  std::optional<ChromeConfig> chromeDarkConfig_;
  bool shellThemeDark_ = false;
  void refreshActiveChromeConfig();
  std::uint32_t shiftModifierIndex_ = ~0u;
  std::uint32_t ctrlModifierIndex_ = ~0u;
  std::uint32_t altModifierIndex_ = ~0u;
  std::uint32_t logoModifierIndex_ = ~0u;
  float pointerX_ = 32.f;
  float pointerY_ = 32.f;
  std::uint64_t nextSurfaceId_ = 1;
  std::uint64_t nextSubsurfaceOrder_ = 1;
  std::uint64_t contentSerial_ = 1;
  std::uint64_t lastResizePacingActivityNsec_ = 0;
  std::uint64_t nextActivationTokenId_ = 1;
  std::uint32_t nextConfigureSerial_ = 1;
  std::uint32_t nextInputSerial_ = 1;
  float preferredScale_ = 2.0f;
  CompositorKeyboardConfig keyboardConfig_;
  int keyboardRepeatRate_ = 25;
  int keyboardRepeatDelayMs_ = 600;
  std::int32_t dockReservedZone_ = 0;
  float shellPanelHideProgress_ = 0.f;
  float shellPanelHideStartProgress_ = 0.f;
  float shellPanelHideTargetProgress_ = 0.f;
  std::uint32_t shellPanelHideAnimationStartedAtMs_ = 0;
  bool shellPanelHideAnimationActive_ = false;
  Surface* lastActivationSurface_ = nullptr;
  std::uint32_t lastActivationTimeMs_ = 0;
};

[[nodiscard]] inline std::uint64_t resizePacingGraceNanoseconds(std::uint32_t refreshMilliHz) noexcept {
  constexpr std::uint64_t kDefaultGraceNsec = 50'000'000ull;
  constexpr std::uint64_t kMinGraceNsec = 33'000'000ull;
  constexpr std::uint64_t kMaxGraceNsec = 75'000'000ull;
  if (refreshMilliHz == 0) return kDefaultGraceNsec;
  std::uint64_t const refreshNsec = 1'000'000'000'000ull / static_cast<std::uint64_t>(refreshMilliHz);
  std::uint64_t const graceNsec = refreshNsec * std::uint64_t{3};
  return std::clamp(graceNsec, kMinGraceNsec, kMaxGraceNsec);
}

[[nodiscard]] inline bool resizePacingGraceActive(std::uint64_t lastActivityNsec,
                                                  std::uint64_t nowNsec,
                                                  std::uint32_t refreshMilliHz) noexcept {
  if (lastActivityNsec == 0 || nowNsec < lastActivityNsec) return false;
  return nowNsec - lastActivityNsec <= resizePacingGraceNanoseconds(refreshMilliHz);
}

struct WaylandServer::Impl::SurfaceViewportState {
  float sourceX = 0.f;
  float sourceY = 0.f;
  float sourceWidth = 0.f;
  float sourceHeight = 0.f;
  bool sourceSet = false;
  std::int32_t destinationWidth = 0;
  std::int32_t destinationHeight = 0;
  bool destinationSet = false;
};

struct WaylandServer::Impl::SurfaceRegionState {
  std::vector<CommittedSurfaceSnapshot::RegionRect> opaqueRegionRects;
  bool inputRegionInfinite = true;
  std::vector<CommittedSurfaceSnapshot::RegionRect> inputRegionRects;
};

struct WaylandServer::Impl::SurfacePendingRegionState {
  std::vector<CommittedSurfaceSnapshot::RegionRect> opaqueRegionRects;
  bool opaqueRegionSet = false;
  bool inputRegionInfinite = true;
  std::vector<CommittedSurfaceSnapshot::RegionRect> inputRegionRects;
  bool inputRegionSet = false;
};

struct WaylandServer::Impl::SurfaceDamageState {
  std::vector<CommittedSurfaceSnapshot::RegionRect> bufferRects;
};

struct WaylandServer::Impl::SurfacePendingDamageState {
  std::vector<CommittedSurfaceSnapshot::RegionRect> surfaceRects;
  std::vector<CommittedSurfaceSnapshot::RegionRect> bufferRects;
};

struct WaylandServer::Impl::PointerConstraintCommitState {
  PointerConstraint* constraint = nullptr;
  bool regionInfinite = true;
  std::vector<CommittedSurfaceSnapshot::RegionRect> regionRects;
  bool regionSet = false;
  bool cursorHintSet = false;
  float cursorHintX = 0.f;
  float cursorHintY = 0.f;
};

struct WaylandServer::Impl::SurfaceBufferState {
  wl_resource* buffer = nullptr;
  std::int32_t scale = 1;
  std::int32_t transform = WL_OUTPUT_TRANSFORM_NORMAL;
  std::int32_t offsetX = 0;
  std::int32_t offsetY = 0;
};

struct WaylandServer::Impl::SurfacePendingBufferState {
  wl_resource* buffer = nullptr;
  bool bufferAttached = false;
  std::int32_t scale = 1;
  bool scaleSet = false;
  std::int32_t transform = WL_OUTPUT_TRANSFORM_NORMAL;
  bool transformSet = false;
  std::int32_t offsetX = 0;
  std::int32_t offsetY = 0;
  bool offsetSet = false;
};

struct WaylandServer::Impl::SurfacePendingCommitState {
  SurfacePendingBufferState bufferState;
  SurfaceViewportState viewportState;
  bool viewportChanged = false;
  SurfacePendingRegionState regionState;
  SurfacePendingDamageState damageState;
  std::vector<PointerConstraintCommitState> pointerConstraintStates;
  std::vector<CommittedSurfaceSnapshot::RegionRect> pendingBackgroundBlurRects;
  SurfaceBackgroundEffectSnapshot pendingBackgroundEffectState;
  bool backgroundBlurPending = false;
  bool backgroundEffectStatePending = false;
  std::vector<PresentationFeedback*> pendingPresentationFeedbacks;
  std::vector<wl_resource*> pendingFrameCallbacks;
};

struct WaylandServer::Impl::SurfaceXdgRoleState {
  bool windowGeometrySet = false;
  WindowGeometry windowGeometry;
};

struct WaylandServer::Impl::Surface {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  std::uint64_t id = 0;
  SurfaceBufferState bufferState;
  SurfacePendingBufferState pendingBufferState;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t windowX = 96;
  std::int32_t windowY = 96;
  SurfaceRole role = SurfaceRole::None;
  std::uint64_t serial = 0;
  std::uint64_t commitCount = 0;
  std::int32_t lastLoggedCommitWidth = -1;
  std::int32_t lastLoggedCommitHeight = -1;
  std::uint32_t lastLoggedCommitKind = 0;
  std::uint32_t lastLoggedCommitFormat = 0;
  std::shared_ptr<std::vector<std::uint8_t> const> rgbaPixels;
  std::uint8_t const* shmPixels = nullptr;
  std::size_t shmPixelBytes = 0;
  Image::PixelFormat pixelFormat = Image::PixelFormat::Rgba8888;
  bool rgbaFullyOpaque = false;
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::int32_t frameWidth = 0;
  std::int32_t frameHeight = 0;
  SurfaceXdgRoleState xdgRoleState;
  SurfaceViewportState viewportState;
  SurfaceViewportState pendingViewportState;
  bool snapped = false;
  bool maximized = false;
  bool fullscreen = false;
  bool minimized = false;
  bool preFullscreenSnapped = false;
  bool preFullscreenMaximized = false;
  std::int32_t preFullscreenX = 0;
  std::int32_t preFullscreenY = 0;
  std::int32_t preFullscreenWidth = 0;
  std::int32_t preFullscreenHeight = 0;
  std::int32_t geometryAnimationStartX = 0;
  std::int32_t geometryAnimationStartY = 0;
  std::int32_t geometryAnimationStartWidth = 0;
  std::int32_t geometryAnimationStartHeight = 0;
  bool geometryAnimationActive = false;
  std::uint32_t geometryAnimationStartedAtMs = 0;
  std::int32_t geometryAnimationTargetX = 0;
  std::int32_t geometryAnimationTargetY = 0;
  std::int32_t geometryAnimationTargetWidth = 0;
  std::int32_t geometryAnimationTargetHeight = 0;
  bool awaitingConfigureCommit = false;
  std::int32_t awaitingConfigureWidth = 0;
  std::int32_t awaitingConfigureHeight = 0;
  bool resizeConfigureInFlight = false;
  bool resizeConfigureAcked = false;
  std::uint32_t resizeConfigureSerial = 0;
  std::int32_t resizeConfigureX = 0;
  std::int32_t resizeConfigureY = 0;
  std::int32_t resizeConfigureWidth = 0;
  std::int32_t resizeConfigureHeight = 0;
  bool pendingResizeConfigure = false;
  std::int32_t pendingResizeConfigureX = 0;
  std::int32_t pendingResizeConfigureY = 0;
  std::int32_t pendingResizeConfigureWidth = 0;
  std::int32_t pendingResizeConfigureHeight = 0;
  std::uint32_t lastConfigureSerial = 0;
  std::uint64_t lastConfigureSentNsec = 0;
  std::uint64_t lastConfigureAckNsec = 0;
  std::uint64_t lastResizeInputNsec = 0;
  std::uint64_t lastCommitNsec = 0;
  std::int32_t lastConfigureWidth = 0;
  std::int32_t lastConfigureHeight = 0;
  std::int32_t restoreX = 96;
  std::int32_t restoreY = 96;
  std::int32_t restoreWidth = 0;
  std::int32_t restoreHeight = 0;
  DmabufBuffer* dmabufBuffer = nullptr;
  std::vector<wl_resource*> pendingBufferReleases;
  BackgroundEffect* backgroundEffect = nullptr;
  std::vector<CommittedSurfaceSnapshot::RegionRect> backgroundBlurRects;
  std::vector<CommittedSurfaceSnapshot::RegionRect> pendingBackgroundBlurRects;
  SurfaceBackgroundEffectSnapshot backgroundEffectState;
  SurfaceBackgroundEffectSnapshot pendingBackgroundEffectState;
  bool backgroundBlurPending = false;
  bool backgroundEffectStatePending = false;
  SurfaceRegionState regionState;
  SurfacePendingRegionState pendingRegionState;
  SurfaceDamageState damageState;
  SurfacePendingDamageState pendingDamageState;
  std::vector<PointerConstraintCommitState> pendingPointerConstraintStates;
  Viewport* viewport = nullptr;
  FractionalScale* fractionalScale = nullptr;
  LayerSurface* layerSurface = nullptr;
  XdgPopup* xdgPopup = nullptr;
  Subsurface* subsurfaceRole = nullptr;
  std::vector<PresentationFeedback*> pendingPresentationFeedbacks;
  std::vector<PresentationFeedback*> presentationFeedbacks;
  std::vector<wl_resource*> pendingFrameCallbacks;
  std::vector<wl_resource*> frameCallbacks;
  std::optional<SurfacePendingCommitState> cachedSubsurfaceCommit;
};

struct WaylandServer::Impl::Region {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  std::vector<CommittedSurfaceSnapshot::RegionRect> rects;
};

struct WaylandServer::Impl::BackgroundEffect {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  Surface* surface = nullptr;
};

inline bool surfaceHasNoRole(WaylandServer::Impl::Surface const* surface) {
  return surface && surface->role == SurfaceRole::None;
}

inline bool surfaceIsXdgSurfaceBase(WaylandServer::Impl::Surface const* surface) {
  return surface && surface->role == SurfaceRole::XdgSurface;
}

inline bool surfaceIsXdgToplevel(WaylandServer::Impl::Surface const* surface) {
  return surface && surface->role == SurfaceRole::XdgToplevel;
}

inline bool surfaceIsXdgPopup(WaylandServer::Impl::Surface const* surface) {
  return surface && surface->role == SurfaceRole::XdgPopup;
}

inline bool surfaceIsLayerSurface(WaylandServer::Impl::Surface const* surface) {
  return surface && surface->role == SurfaceRole::LayerSurface;
}

inline bool surfaceIsSubsurface(WaylandServer::Impl::Surface const* surface) {
  return surface && surface->role == SurfaceRole::Subsurface;
}

inline bool surfaceIsCursor(WaylandServer::Impl::Surface const* surface) {
  return surface && surface->role == SurfaceRole::Cursor;
}

inline bool surfaceIsDragIcon(WaylandServer::Impl::Surface const* surface) {
  return surface && surface->role == SurfaceRole::DragIcon;
}

inline bool surfaceIsTopLevelRenderable(WaylandServer::Impl::Surface const* surface) {
  return surface &&
         (surface->role == SurfaceRole::XdgToplevel ||
          surface->role == SurfaceRole::XdgPopup ||
          surface->role == SurfaceRole::LayerSurface);
}

inline bool surfaceBufferTransformSwapsAxes(std::int32_t transform) {
  return transform == WL_OUTPUT_TRANSFORM_90 ||
         transform == WL_OUTPUT_TRANSFORM_270 ||
         transform == WL_OUTPUT_TRANSFORM_FLIPPED_90 ||
         transform == WL_OUTPUT_TRANSFORM_FLIPPED_270;
}

inline std::int32_t surfaceTransformedBufferWidth(WaylandServer::Impl::Surface const* surface) {
  if (!surface) return 0;
  return surfaceBufferTransformSwapsAxes(surface->bufferState.transform) ? surface->height : surface->width;
}

inline std::int32_t surfaceTransformedBufferHeight(WaylandServer::Impl::Surface const* surface) {
  if (!surface) return 0;
  return surfaceBufferTransformSwapsAxes(surface->bufferState.transform) ? surface->width : surface->height;
}

std::uint32_t issueSeatSerial(WaylandServer::Impl* server,
                              SeatSerialKind kind,
                              wl_client* client,
                              WaylandServer::Impl::Surface* surface);
std::uint32_t issueSeatSerial(std::uint32_t& nextSerial,
                              std::deque<WaylandServer::Impl::SeatSerialRecord>& records,
                              SeatSerialKind kind,
                              wl_client* client,
                              WaylandServer::Impl::Surface* surface);
std::uint32_t issueSeatSerialForSurface(WaylandServer::Impl* server,
                                        SeatSerialKind kind,
                                        WaylandServer::Impl::Surface* surface);
bool seatSerialIsValid(WaylandServer::Impl const* server,
                       std::uint32_t serial,
                       wl_client* client,
                       WaylandServer::Impl::Surface const* surface,
                       std::span<SeatSerialKind const> allowedKinds);
bool seatSerialIsValid(std::deque<WaylandServer::Impl::SeatSerialRecord> const& records,
                       std::uint32_t serial,
                       wl_client* client,
                       WaylandServer::Impl::Surface const* surface,
                       std::span<SeatSerialKind const> allowedKinds);
void clearSeatSerialsForSurface(WaylandServer::Impl* server, WaylandServer::Impl::Surface const* surface);
void clearSeatSerialsForSurface(std::deque<WaylandServer::Impl::SeatSerialRecord>& records,
                                WaylandServer::Impl::Surface const* surface);

inline std::int32_t surfaceCommittedDisplayWidth(WaylandServer::Impl::Surface const* surface) {
  if (!surface) return 0;
  if (surface->viewportState.destinationSet) return surface->viewportState.destinationWidth;
  if (surface->viewportState.sourceSet) return static_cast<std::int32_t>(surface->viewportState.sourceWidth);
  return std::max(1, surfaceTransformedBufferWidth(surface) / std::max(1, surface->bufferState.scale));
}

inline std::int32_t surfaceCommittedDisplayHeight(WaylandServer::Impl::Surface const* surface) {
  if (!surface) return 0;
  if (surface->viewportState.destinationSet) return surface->viewportState.destinationHeight;
  if (surface->viewportState.sourceSet) return static_cast<std::int32_t>(surface->viewportState.sourceHeight);
  return std::max(1, surfaceTransformedBufferHeight(surface) / std::max(1, surface->bufferState.scale));
}

enum class SubsurfaceStackLayer : std::uint8_t {
  Below,
  Above,
};

struct WaylandServer::Impl::Subsurface {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  Surface* surface = nullptr;
  Surface* parent = nullptr;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t pendingX = 0;
  std::int32_t pendingY = 0;
  SubsurfaceStackLayer stackLayer = SubsurfaceStackLayer::Above;
  SubsurfaceStackLayer pendingStackLayer = SubsurfaceStackLayer::Above;
  std::uint64_t order = 0;
  std::uint64_t pendingOrder = 0;
  bool synchronized = true;
};

struct WaylandServer::Impl::Viewport {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  Surface* surface = nullptr;
};

struct WaylandServer::Impl::FractionalScale {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  Surface* surface = nullptr;
};

struct WaylandServer::Impl::CursorShapeDevice {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  wl_resource* pointer = nullptr;
};

struct WaylandServer::Impl::IdleInhibitor {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  Surface* surface = nullptr;
};

struct WaylandServer::Impl::XdgOutput {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  wl_resource* outputResource = nullptr;
  std::int32_t lastLogicalWidth = 0;
  std::int32_t lastLogicalHeight = 0;
};

struct LayerSurfacePendingState {
  bool sizeSet = false;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  bool anchorSet = false;
  std::uint32_t anchor = 0;
  bool exclusiveZoneSet = false;
  std::int32_t exclusiveZone = 0;
  bool keyboardInteractivitySet = false;
  std::uint32_t keyboardInteractivity = 0;
  bool marginSet = false;
  std::int32_t marginTop = 0;
  std::int32_t marginRight = 0;
  std::int32_t marginBottom = 0;
  std::int32_t marginLeft = 0;
  bool layerSet = false;
  std::uint32_t layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
  bool configureAcked = false;
  std::uint32_t configureSerial = 0;
  std::uint32_t configureWidth = 0;
  std::uint32_t configureHeight = 0;
};

struct LayerSurfaceConfigure {
  std::uint32_t serial = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
};

struct WaylandServer::Impl::LayerSurface {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  Surface* surface = nullptr;
  std::uint32_t layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
  std::string nameSpace;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t anchor = 0;
  std::int32_t exclusiveZone = 0;
  std::uint32_t keyboardInteractivity = 0;
  std::int32_t marginTop = 0;
  std::int32_t marginRight = 0;
  std::int32_t marginBottom = 0;
  std::int32_t marginLeft = 0;
  std::uint32_t configureSerial = 0;
  std::uint32_t configureWidth = 0;
  std::uint32_t configureHeight = 0;
  std::uint32_t latestConfigureSerial = 0;
  LayerSurfacePendingState pending;
  std::vector<LayerSurfaceConfigure> pendingConfigures;
  bool initialized = false;
  bool configured = false;
  bool mapped = false;
};

struct LayerSurfaceCommitResult {
  bool valid = true;
  bool stateChanged = false;
  bool configureNeeded = false;
};

struct WaylandServer::Impl::XdgConfigure {
  std::uint32_t serial = 0;
  SurfaceRole role = SurfaceRole::None;
  std::int32_t width = 0;
  std::int32_t height = 0;
  bool hasWindowGeometry = false;
  std::int32_t windowX = 0;
  std::int32_t windowY = 0;
  std::int32_t windowWidth = 0;
  std::int32_t windowHeight = 0;
};

struct WaylandServer::Impl::PresentationFeedback {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  Surface* surface = nullptr;
};

struct WaylandServer::Impl::PendingPresentationBatch {
  std::uint32_t backendPresentId = 0;
  std::uint32_t queuedAtMs = 0;
  PresentationTiming fallbackTiming;
  std::vector<PresentationFeedback*> feedbacks;
};

struct WaylandServer::Impl::RelativePointer {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  wl_resource* pointer = nullptr;
};

struct WaylandServer::Impl::PointerConstraint {
  enum class Kind { Lock, Confine };

  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  Surface* surface = nullptr;
  wl_resource* pointer = nullptr;
  Kind kind = Kind::Lock;
  std::uint32_t lifetime = ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT;
  bool active = false;
  bool defunct = false;
  bool regionInfinite = true;
  std::vector<CommittedSurfaceSnapshot::RegionRect> regionRects;
  bool pendingRegionInfinite = true;
  std::vector<CommittedSurfaceSnapshot::RegionRect> pendingRegionRects;
  bool pendingRegionSet = false;
  std::vector<CommittedSurfaceSnapshot::RegionRect> effectiveRegionRects;
  bool cursorHintSet = false;
  float cursorHintX = 0.f;
  float cursorHintY = 0.f;
  bool pendingCursorHintSet = false;
  float pendingCursorHintX = 0.f;
  float pendingCursorHintY = 0.f;
};

struct WaylandServer::Impl::PrimarySelectionSource {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  std::vector<std::string> mimeTypes;
};

struct WaylandServer::Impl::PrimarySelectionDevice {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  wl_resource* seat = nullptr;
};

struct WaylandServer::Impl::PrimarySelectionOffer {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  PrimarySelectionSource* source = nullptr;
};

struct WaylandServer::Impl::DataSource {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  std::vector<std::string> mimeTypes;
  std::uint32_t dndActions = WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;
  bool dndActionsSet = false;
  bool used = false;
};

struct WaylandServer::Impl::DataDevice {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  wl_resource* seat = nullptr;
};

struct WaylandServer::Impl::DataOffer {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  DataSource* source = nullptr;
  bool dnd = false;
  std::string acceptedMimeType;
  std::uint32_t dndActions = WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;
  std::uint32_t preferredAction = WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;
  std::uint32_t selectedAction = WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;
  bool dropPerformed = false;
  bool finished = false;
};

struct WaylandServer::Impl::ActivationToken {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  std::string token;
  std::string appId;
  Surface* surface = nullptr;
  std::uint32_t serial = 0;
  std::uint32_t expiresAtMs = 0;
  wl_event_source* timeout = nullptr;
  bool hasSerial = false;
  bool committed = false;
};

WaylandServer::Impl::Surface* surfaceAt(WaylandServer::Impl* server, float x, float y);
std::optional<SnapPreviewSnapshot> snapPreviewForDrag(WaylandServer::Impl const* server);
std::int32_t displayWidth(WaylandServer::Impl::Surface const* surface);
std::int32_t displayHeight(WaylandServer::Impl::Surface const* surface);
void setConfiguredFrameSize(WaylandServer::Impl::Surface* surface, std::int32_t width, std::int32_t height);
void traceResizeSurface(char const* event, WaylandServer::Impl::Surface const* surface);
bool applyLayerGeometry(WaylandServer::Impl::LayerSurface* layerSurface);
void sendLayerConfigure(WaylandServer::Impl::LayerSurface* layerSurface);
void refreshShellReservedZones(WaylandServer::Impl* server);
bool ackLayerSurfaceConfigure(WaylandServer::Impl::LayerSurface* layerSurface,
                              std::uint32_t serial,
                              wl_resource* errorResource = nullptr);
LayerSurfaceCommitResult applyLayerSurfacePendingState(WaylandServer::Impl::LayerSurface* layerSurface,
                                                       wl_resource* errorResource = nullptr);
bool markLayerSurfaceMapped(WaylandServer::Impl::LayerSurface* layerSurface);
bool resetLayerSurfaceForUnmap(WaylandServer::Impl::LayerSurface* layerSurface);
bool applySubsurfacePendingPosition(WaylandServer::Impl::Subsurface* subsurface);
bool applySubsurfacePendingOrder(WaylandServer::Impl* server, WaylandServer::Impl::Surface* parent);
bool applySubsurfacePendingOrder(std::vector<WaylandServer::Impl::Subsurface*> subsurfaces,
                                 WaylandServer::Impl::Surface* parent);
bool subsurfaceIsEffectivelySynchronized(WaylandServer::Impl::Subsurface const* subsurface);
bool cacheSynchronizedSubsurfaceCommit(WaylandServer::Impl::Surface* surface);
bool surfaceHasCachedSubsurfaceCommit(WaylandServer::Impl::Surface const* surface);
bool restoreCachedSubsurfaceCommit(WaylandServer::Impl::Surface* surface);
WaylandServer::Impl::SurfacePendingCommitState takeSurfacePendingCommit(WaylandServer::Impl::Surface* surface);
void restoreSurfacePendingCommit(WaylandServer::Impl::Surface* surface,
                                 WaylandServer::Impl::SurfacePendingCommitState&& state);
bool setSubsurfacePendingPlaceAbove(WaylandServer::Impl::Subsurface* subsurface,
                                    WaylandServer::Impl::Surface* siblingSurface,
                                    wl_resource* errorResource = nullptr);
bool setSubsurfacePendingPlaceAbove(std::vector<WaylandServer::Impl::Subsurface*> subsurfaces,
                                    WaylandServer::Impl::Subsurface* subsurface,
                                    WaylandServer::Impl::Surface* siblingSurface,
                                    wl_resource* errorResource = nullptr);
bool setSubsurfacePendingPlaceBelow(WaylandServer::Impl::Subsurface* subsurface,
                                    WaylandServer::Impl::Surface* siblingSurface,
                                    wl_resource* errorResource = nullptr);
bool setSubsurfacePendingPlaceBelow(std::vector<WaylandServer::Impl::Subsurface*> subsurfaces,
                                    WaylandServer::Impl::Subsurface* subsurface,
                                    WaylandServer::Impl::Surface* siblingSurface,
                                    wl_resource* errorResource = nullptr);
std::vector<WaylandServer::Impl::Subsurface const*> orderedSubsurfacesForParent(
    WaylandServer::Impl const* server,
    WaylandServer::Impl::Surface const* parent,
    SubsurfaceStackLayer layer);
std::vector<WaylandServer::Impl::Subsurface const*> orderedSubsurfacesForParent(
    std::vector<WaylandServer::Impl::Subsurface const*> subsurfaces,
    WaylandServer::Impl::Surface const* parent,
    SubsurfaceStackLayer layer);
inline bool surfaceParticipatesInPresentedFrame(WaylandServer::Impl::Surface const* surface,
                                                std::span<std::uint64_t const> frameSurfaceIds) {
  if (!surface || surface->id == 0) return false;
  return std::ranges::find(frameSurfaceIds, surface->id) != frameSurfaceIds.end();
}
void focusSurface(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface, std::uint32_t timeMs);
void establishPopupGrab(WaylandServer::Impl* server,
                        WaylandServer::Impl::XdgPopup* popup,
                        wl_resource* seat,
                        std::uint32_t serial);
void releasePopupGrab(WaylandServer::Impl* server, WaylandServer::Impl::XdgPopup* popup, std::uint32_t timeMs);
bool resetXdgPopupRole(WaylandServer::Impl* server,
                       WaylandServer::Impl::XdgPopup* popup,
                       bool sendPopupDone);
bool surfaceInGrabSubtree(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface);
void removeSurfaceFromFocusOrder(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface);
void activateMostRecentToplevel(WaylandServer::Impl* server, std::uint32_t timeMs);
void minimizeToplevel(WaylandServer::Impl* server,
                      WaylandServer::Impl::Surface* surface,
                      std::uint32_t timeMs);
void maximizeToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface);
bool restoreToplevel(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface);
WaylandServer::Impl::ToplevelDecoration* decorationFor(WaylandServer::Impl* server,
                                                       WaylandServer::Impl::XdgToplevel* toplevel);
WaylandServer::Impl::XxCutouts* cutoutsFor(WaylandServer::Impl* server,
                                           WaylandServer::Impl::XdgToplevel* toplevel);
WaylandServer::Impl::XdgToplevel* toplevelForSurface(WaylandServer::Impl* server,
                                                     WaylandServer::Impl::Surface* surface);
std::string titleForSurface(WaylandServer::Impl const* server, WaylandServer::Impl::Surface const* surface);
std::string appIdForSurface(WaylandServer::Impl const* server, WaylandServer::Impl::Surface const* surface);
void sendToplevelConfigure(WaylandServer::Impl* server,
                           WaylandServer::Impl::XdgToplevel* toplevel,
                           std::int32_t width,
                           std::int32_t height);
bool requestToplevelResizeConfigure(WaylandServer::Impl* server,
                                     WaylandServer::Impl::Surface* surface,
                                     std::int32_t x,
                                     std::int32_t y,
                                     std::int32_t width,
                                     std::int32_t height);
void sendToplevelStateConfigure(WaylandServer::Impl* server,
                                WaylandServer::Impl::XdgToplevel* toplevel);
bool toplevelServerSideDecorated(WaylandServer::Impl* server,
                                 WaylandServer::Impl::XdgToplevel* toplevel);
bool toplevelUsesCutouts(WaylandServer::Impl* server,
                         WaylandServer::Impl::XdgToplevel* toplevel);
void sendCutoutsConfigureIfNeeded(WaylandServer::Impl* server,
                                  WaylandServer::Impl::XdgToplevel* toplevel,
                                  std::int32_t width,
                                  std::int32_t height,
                                  bool force = false);
void maybeSendInitialCutoutsConfigure(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface);
WaylandServer::Impl::PointerConstraint* activePointerConstraint(WaylandServer::Impl* server);
void updatePointerConstraintsForFocus(WaylandServer::Impl* server);
void sendPrimarySelectionForFocus(WaylandServer::Impl* server);
void sendSelectionForFocus(WaylandServer::Impl* server);
WaylandServer::Impl::DataDevice* dataDeviceForClient(WaylandServer::Impl* server, wl_client* client);
void clearDnd(WaylandServer::Impl* server, bool destroyOffer = true, bool sendLeave = true);
void updateDndTarget(WaylandServer::Impl* server, WaylandServer::Impl::Surface* target, std::uint32_t timeMs);

struct WaylandServer::Impl::XdgPositioner {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  std::uint64_t id = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::int32_t anchorRectX = 0;
  std::int32_t anchorRectY = 0;
  std::int32_t anchorRectWidth = 0;
  std::int32_t anchorRectHeight = 0;
  std::uint32_t anchor = XDG_POSITIONER_ANCHOR_NONE;
  std::uint32_t gravity = XDG_POSITIONER_GRAVITY_NONE;
  std::uint32_t constraintAdjustment = XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_NONE;
  std::int32_t offsetX = 0;
  std::int32_t offsetY = 0;
  bool reactive = false;
  std::int32_t parentWidth = 0;
  std::int32_t parentHeight = 0;
  bool hasParentConfigureSerial = false;
  std::uint32_t parentConfigureSerial = 0;
};

struct WaylandServer::Impl::XdgSurface {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  Surface* surface = nullptr;
  bool configured = false;
  std::vector<XdgConfigure> configureList;
  std::optional<XdgConfigure> pendingConfigure;
  std::optional<XdgConfigure> currentConfigure;
  bool windowGeometrySet = false;
  std::int32_t windowGeometryX = 0;
  std::int32_t windowGeometryY = 0;
  std::int32_t windowGeometryWidth = 0;
  std::int32_t windowGeometryHeight = 0;
  bool pendingWindowGeometrySet = false;
  std::int32_t pendingWindowGeometryX = 0;
  std::int32_t pendingWindowGeometryY = 0;
  std::int32_t pendingWindowGeometryWidth = 0;
  std::int32_t pendingWindowGeometryHeight = 0;
};

struct WaylandServer::Impl::XdgToplevel {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  XdgSurface* xdgSurface = nullptr;
  XdgToplevel* parent = nullptr;
  bool mapped = false;
  std::string title;
  std::string appId;
  std::int32_t minWidth = 0;
  std::int32_t minHeight = 0;
  std::int32_t maxWidth = 0;
  std::int32_t maxHeight = 0;
  std::int32_t pendingMinWidth = 0;
  std::int32_t pendingMinHeight = 0;
  std::int32_t pendingMaxWidth = 0;
  std::int32_t pendingMaxHeight = 0;
  bool pendingMinSizeSet = false;
  bool pendingMaxSizeSet = false;
  XxCutouts* cutouts = nullptr;
  bool cutoutsRejected = false;
};

struct WaylandServer::Impl::XdgPopup {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  XdgSurface* xdgSurface = nullptr;
  Surface* parentSurface = nullptr;
  std::int32_t configuredX = 0;
  std::int32_t configuredY = 0;
  std::int32_t configuredWidth = 1;
  std::int32_t configuredHeight = 1;
  bool reactive = false;
  std::int32_t positionerParentWidth = 0;
  std::int32_t positionerParentHeight = 0;
  bool hasParentConfigureSerial = false;
  std::uint32_t parentConfigureSerial = 0;
  bool grabbed = false;
  wl_resource* grabSeatResource = nullptr;
  bool committed = false;
  bool mapped = false;
  bool dismissed = false;
};

struct WaylandServer::Impl::ShmPool {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  int fd = -1;
  std::int32_t size = 0;
  void* data = nullptr;
};

struct WaylandServer::Impl::ShmBuffer {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  ShmPool* pool = nullptr;
  int fd = -1;
  std::int32_t size = 0;
  void* data = nullptr;
  std::int32_t offset = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::int32_t stride = 0;
  std::uint32_t format = 0;
};

struct DmabufPlane {
  int fd = -1;
  std::uint32_t index = 0;
  std::uint32_t offset = 0;
  std::uint32_t stride = 0;
  std::uint64_t modifier = DRM_FORMAT_MOD_INVALID;
};

struct WaylandServer::Impl::DmabufParams {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  std::vector<DmabufPlane> planes;
  bool used = false;
};

struct WaylandServer::Impl::DmabufBuffer {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  std::uint64_t id = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::uint32_t format = 0;
  std::uint32_t flags = 0;
  std::vector<DmabufPlane> planes;
};

struct WaylandServer::Impl::ToplevelDecoration {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  XdgToplevel* toplevel = nullptr;
  std::uint32_t mode = ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
};

struct WaylandServer::Impl::XxCutouts {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  XdgToplevel* toplevel = nullptr;
  bool pendingControlsUnhandled = false;
  bool lastSent = false;
  std::int32_t lastX = 0;
  std::int32_t lastY = 0;
  std::int32_t lastWidth = 0;
  std::int32_t lastHeight = 0;
};

} // namespace lambda::compositor
