#pragma once

#include "Compositor/WaylandServer.hpp"
#include "linux-dmabuf-unstable-v1-server-protocol.h"
#include "pointer-constraints-unstable-v1-server-protocol.h"
#include "wlr-layer-shell-unstable-v1-server-protocol.h"
#include "xx-cutouts-v1-server-protocol.h"
#include "xdg-decoration-unstable-v1-server-protocol.h"
#include "xdg-shell-server-protocol.h"

#include <drm_fourcc.h>
#include <wayland-server-core.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct xkb_context;
struct xkb_keymap;
struct xkb_state;

namespace flux::compositor {

enum class SurfaceRole : std::uint8_t {
  None,
  XdgToplevel,
  XdgPopup,
  LayerSurface,
  Subsurface,
  Cursor,
};

struct WaylandServer::Impl {
  struct Surface;
  struct Subsurface;
  struct XdgPositioner;
  struct XdgSurface;
  struct XdgToplevel;
  struct XdgPopup;
  struct ShmPool;
  struct ShmBuffer;
  struct DmabufParams;
  struct DmabufBuffer;
  struct ToplevelDecoration;
  struct XxCutouts;
  struct Region;
  struct BackgroundEffect;
  struct Viewport;
  struct FractionalScale;
  struct CursorShapeDevice;
  struct IdleInhibitor;
  struct LayerSurface;
  struct PresentationFeedback;
  struct PendingPresentationBatch;
  struct RelativePointer;
  struct PointerConstraint;
  struct PrimarySelectionDevice;
  struct PrimarySelectionSource;
  struct PrimarySelectionOffer;
  struct DataDevice;
  struct DataSource;
  struct DataOffer;
  struct ActivationToken;

  explicit Impl(WaylandOutputInfo output);
  ~Impl();

  [[nodiscard]] char const* socketName() const noexcept;
  [[nodiscard]] int eventFd() const noexcept;
  [[nodiscard]] float preferredScale() const noexcept;
  [[nodiscard]] std::int32_t logicalOutputWidth() const noexcept;
  [[nodiscard]] std::int32_t logicalOutputHeight() const noexcept;
  [[nodiscard]] std::size_t toplevelCount() const noexcept;
  [[nodiscard]] std::vector<CommittedSurfaceSnapshot> committedSurfaces() const;
  [[nodiscard]] std::optional<CommittedSurfaceSnapshot> cursorSurface() const;
  [[nodiscard]] std::optional<SnapPreviewSnapshot> snapPreview() const;
  [[nodiscard]] CommandLauncherSnapshot commandLauncher() const;
  [[nodiscard]] std::vector<int> duplicateDmabufFds(std::uint64_t surfaceId) const;
  [[nodiscard]] bool copyDmabufToRgba(std::uint64_t surfaceId, std::vector<std::uint8_t>& out) const;

  void dispatch();
  void flushClients();
  void setShortcutBindings(std::vector<ShortcutBinding> bindings);
  void setChromeConfig(ChromeConfig config);
  void setPreferredScale(float scale);
  void updateAnimations(std::uint32_t timeMs, bool animationsEnabled);
  [[nodiscard]] bool hasActiveAnimations() const noexcept;
  [[nodiscard]] bool hasIdleInhibitors() const noexcept;
  void sendFrameCallbacksOnly(std::uint32_t timeMs);
  void sendPresentationFeedbacks(std::uint32_t timeMs, PresentationTiming timing);
  void sendFrameCallbacks(std::uint32_t timeMs, PresentationTiming timing);
  void completePresentationFeedbacks(std::vector<PresentationCompletion> const& completions, std::uint32_t timeMs);
  void handlePointerMotion(double dx, double dy, std::uint32_t timeMs);
  void handlePointerPosition(double x, double y, std::uint32_t timeMs);
  void handlePointerButton(std::uint32_t button, bool pressed, std::uint32_t timeMs);
  void handlePointerAxis(double dx, double dy, std::uint32_t timeMs);
  void handleKeyboardKey(std::uint32_t key, bool pressed, std::uint32_t timeMs);

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
  WaylandOutputInfo output_;
  std::vector<std::unique_ptr<Surface>> surfaces_;
  std::vector<Surface*> focusOrder_;
  std::vector<std::unique_ptr<Subsurface>> subsurfaces_;
  std::vector<std::unique_ptr<XdgPositioner>> xdgPositioners_;
  std::vector<std::unique_ptr<XdgSurface>> xdgSurfaces_;
  std::vector<std::unique_ptr<XdgToplevel>> toplevels_;
  std::vector<std::unique_ptr<XdgPopup>> popups_;
  std::vector<std::unique_ptr<ShmPool>> shmPools_;
  std::vector<std::unique_ptr<ShmBuffer>> shmBuffers_;
  std::vector<std::unique_ptr<DmabufParams>> dmabufParams_;
  std::vector<std::unique_ptr<DmabufBuffer>> dmabufBuffers_;
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
  DataOffer* dndOffer_ = nullptr;
  std::vector<wl_resource*> seatResources_;
  std::vector<wl_resource*> outputResources_;
  std::vector<wl_resource*> pointerResources_;
  std::vector<wl_resource*> keyboardResources_;
  Surface* pointerFocus_ = nullptr;
  Surface* keyboardFocus_ = nullptr;
  Surface* dragSurface_ = nullptr;
  Surface* resizeSurface_ = nullptr;
  Surface* closePressSurface_ = nullptr;
  Surface* minimizePressSurface_ = nullptr;
  Surface* lastTitleClickSurface_ = nullptr;
  Surface* cursorSurface_ = nullptr;
  CursorShape cursorShape_ = CursorShape::Arrow;
  bool compositorCursorOverride_ = false;
  CursorShape compositorCursorShape_ = CursorShape::Arrow;
  std::int32_t cursorHotspotX_ = 0;
  std::int32_t cursorHotspotY_ = 0;
  std::uint32_t pointerEnterSerial_ = 0;
  std::uint32_t lastPointerButtonSerial_ = 0;
  Surface* lastPointerButtonSurface_ = nullptr;
  float dragOffsetX_ = 0.f;
  float dragOffsetY_ = 0.f;
  std::uint32_t lastTitleClickTimeMs_ = 0;
  float resizeStartX_ = 0.f;
  float resizeStartY_ = 0.f;
  std::int32_t resizeStartWindowX_ = 0;
  std::int32_t resizeStartWindowY_ = 0;
  std::int32_t resizeStartWidth_ = 0;
  std::int32_t resizeStartHeight_ = 0;
  std::int32_t resizeLastWidth_ = 0;
  std::int32_t resizeLastHeight_ = 0;
  std::uint32_t resizeEdges_ = 0;
  bool metaDown_ = false;
  bool ctrlDown_ = false;
  bool altDown_ = false;
  bool shiftDown_ = false;
  bool commandLauncherVisible_ = false;
  std::string commandLauncherText_;
  std::string commandLauncherMessage_;
  std::vector<ShortcutBinding> shortcutBindings_;
  ChromeConfig chromeConfig_;
  std::uint32_t shiftModifierIndex_ = ~0u;
  std::uint32_t ctrlModifierIndex_ = ~0u;
  std::uint32_t altModifierIndex_ = ~0u;
  std::uint32_t logoModifierIndex_ = ~0u;
  float pointerX_ = 32.f;
  float pointerY_ = 32.f;
  std::uint64_t nextSurfaceId_ = 1;
  std::uint64_t nextActivationTokenId_ = 1;
  std::uint32_t nextConfigureSerial_ = 1;
  std::uint32_t nextInputSerial_ = 1;
  float preferredScale_ = 2.0f;
  Surface* lastActivationSurface_ = nullptr;
  std::uint32_t lastActivationTimeMs_ = 0;
};

struct WaylandServer::Impl::Surface {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  std::uint64_t id = 0;
  wl_resource* pendingBuffer = nullptr;
  bool pendingBufferAttached = false;
  wl_resource* currentBuffer = nullptr;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t scale = 1;
  std::int32_t windowX = 96;
  std::int32_t windowY = 96;
  SurfaceRole role = SurfaceRole::None;
  std::uint64_t serial = 0;
  std::shared_ptr<std::vector<std::uint8_t> const> rgbaPixels;
  bool rgbaFullyOpaque = false;
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::int32_t frameWidth = 0;
  std::int32_t frameHeight = 0;
  float sourceX = 0.f;
  float sourceY = 0.f;
  float sourceWidth = 0.f;
  float sourceHeight = 0.f;
  bool sourceSet = false;
  float pendingSourceX = 0.f;
  float pendingSourceY = 0.f;
  float pendingSourceWidth = 0.f;
  float pendingSourceHeight = 0.f;
  bool pendingSourceSet = false;
  std::int32_t destinationWidth = 0;
  std::int32_t destinationHeight = 0;
  bool destinationSet = false;
  std::int32_t pendingDestinationWidth = 0;
  std::int32_t pendingDestinationHeight = 0;
  bool pendingDestinationSet = false;
  bool snapped = false;
  bool maximized = false;
  bool minimized = false;
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
  std::int32_t geometryAnimationLastConfigureWidth = 0;
  std::int32_t geometryAnimationLastConfigureHeight = 0;
  bool awaitingConfigureCommit = false;
  std::int32_t awaitingConfigureWidth = 0;
  std::int32_t awaitingConfigureHeight = 0;
  std::int32_t restoreX = 96;
  std::int32_t restoreY = 96;
  std::int32_t restoreWidth = 0;
  std::int32_t restoreHeight = 0;
  DmabufBuffer* dmabufBuffer = nullptr;
  BackgroundEffect* backgroundEffect = nullptr;
  std::vector<CommittedSurfaceSnapshot::RegionRect> backgroundBlurRects;
  std::vector<CommittedSurfaceSnapshot::RegionRect> pendingBackgroundBlurRects;
  bool backgroundBlurPending = false;
  Viewport* viewport = nullptr;
  FractionalScale* fractionalScale = nullptr;
  LayerSurface* layerSurface = nullptr;
  XdgPopup* xdgPopup = nullptr;
  Subsurface* subsurfaceRole = nullptr;
  std::vector<PresentationFeedback*> pendingPresentationFeedbacks;
  std::vector<PresentationFeedback*> presentationFeedbacks;
  std::vector<wl_resource*> frameCallbacks;
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

inline bool surfaceIsTopLevelRenderable(WaylandServer::Impl::Surface const* surface) {
  return surface &&
         (surface->role == SurfaceRole::XdgToplevel ||
          surface->role == SurfaceRole::XdgPopup ||
          surface->role == SurfaceRole::LayerSurface);
}

struct WaylandServer::Impl::Subsurface {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  Surface* surface = nullptr;
  Surface* parent = nullptr;
  std::int32_t x = 0;
  std::int32_t y = 0;
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

struct WaylandServer::Impl::LayerSurface {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  Surface* surface = nullptr;
  std::uint32_t layer = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
  std::string nameSpace;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t anchor = 0;
  std::int32_t marginTop = 0;
  std::int32_t marginRight = 0;
  std::int32_t marginBottom = 0;
  std::int32_t marginLeft = 0;
  bool configured = false;
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
  float cursorHintX = 0.f;
  float cursorHintY = 0.f;
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
};

struct WaylandServer::Impl::ActivationToken {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  std::string token;
  std::string appId;
  Surface* surface = nullptr;
  std::uint32_t serial = 0;
  bool committed = false;
};

WaylandServer::Impl::Surface* surfaceAt(WaylandServer::Impl* server, float x, float y);
std::optional<SnapPreviewSnapshot> snapPreviewForDrag(WaylandServer::Impl const* server);
std::int32_t displayWidth(WaylandServer::Impl::Surface const* surface);
std::int32_t displayHeight(WaylandServer::Impl::Surface const* surface);
void setConfiguredFrameSize(WaylandServer::Impl::Surface* surface, std::int32_t width, std::int32_t height);
void traceResizeSurface(char const* event, WaylandServer::Impl::Surface const* surface);
void applyLayerGeometry(WaylandServer::Impl::LayerSurface* layerSurface);
void sendLayerConfigure(WaylandServer::Impl::LayerSurface* layerSurface);
void focusSurface(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface, std::uint32_t timeMs);
void removeSurfaceFromFocusOrder(WaylandServer::Impl* server, WaylandServer::Impl::Surface* surface);
void activateMostRecentToplevel(WaylandServer::Impl* server, std::uint32_t timeMs);
WaylandServer::Impl::ToplevelDecoration* decorationFor(WaylandServer::Impl* server,
                                                       WaylandServer::Impl::XdgToplevel* toplevel);
WaylandServer::Impl::XxCutouts* cutoutsFor(WaylandServer::Impl* server,
                                           WaylandServer::Impl::XdgToplevel* toplevel);
WaylandServer::Impl::XdgToplevel* toplevelForSurface(WaylandServer::Impl* server,
                                                     WaylandServer::Impl::Surface* surface);
std::string titleForSurface(WaylandServer::Impl const* server, WaylandServer::Impl::Surface const* surface);
void sendToplevelConfigure(WaylandServer::Impl* server,
                           WaylandServer::Impl::XdgToplevel* toplevel,
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
void clearDnd(WaylandServer::Impl* server, bool destroyOffer = true);
void updateDndTarget(WaylandServer::Impl* server, WaylandServer::Impl::Surface* target, std::uint32_t timeMs);

struct WaylandServer::Impl::XdgPositioner {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
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
};

struct WaylandServer::Impl::XdgSurface {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  Surface* surface = nullptr;
  bool configured = false;
};

struct WaylandServer::Impl::XdgToplevel {
  WaylandServer::Impl* server = nullptr;
  wl_resource* resource = nullptr;
  XdgSurface* xdgSurface = nullptr;
  std::string title;
  std::string appId;
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
  bool grabbed = false;
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

} // namespace flux::compositor
