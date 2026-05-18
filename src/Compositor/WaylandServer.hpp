#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct wl_client;
struct wl_display;
struct wl_global;
struct wl_resource;

namespace flux::compositor {

enum class CursorShape : std::uint8_t {
  Arrow,
  IBeam,
  Hand,
  Crosshair,
  ResizeEW,
  ResizeNS,
  ResizeNESW,
  ResizeNWSE,
  ResizeAll,
  NotAllowed,
};

struct WaylandOutputInfo {
  std::string name;
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::int32_t refreshMilliHz = 60'000;
  std::int32_t physicalWidthMm = 0;
  std::int32_t physicalHeightMm = 0;
};

struct CommittedSurfaceSnapshot {
  struct DmabufPlane {
    std::uint32_t offset = 0;
    std::uint32_t stride = 0;
    std::uint64_t modifier = 0;
  };

  std::uint64_t id = 0;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::int32_t bufferWidth = 0;
  std::int32_t bufferHeight = 0;
  float sourceX = 0.f;
  float sourceY = 0.f;
  float sourceWidth = 0.f;
  float sourceHeight = 0.f;
  std::int32_t titleBarHeight = 0;
  std::string title;
  bool focused = false;
  bool activeSizing = false;
  std::uint64_t serial = 0;
  std::vector<std::uint8_t> rgbaPixels;
  std::uint32_t dmabufFormat = 0;
  std::vector<DmabufPlane> dmabufPlanes;
};

struct SnapPreviewSnapshot {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
};

class WaylandServer {
public:
  enum class ShortcutAction : std::uint8_t {
    CloseFocused,
    CycleFocus,
    SnapLeft,
    SnapRight,
    Terminate,
  };

  struct ShortcutBinding {
    ShortcutAction action = ShortcutAction::CloseFocused;
    std::uint32_t key = 0;
    bool meta = false;
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
  };

  explicit WaylandServer(WaylandOutputInfo output);
  ~WaylandServer();

  WaylandServer(WaylandServer const&) = delete;
  WaylandServer& operator=(WaylandServer const&) = delete;

  [[nodiscard]] char const* socketName() const noexcept;
  [[nodiscard]] int eventFd() const noexcept;
  [[nodiscard]] std::size_t toplevelCount() const noexcept;
  [[nodiscard]] std::vector<CommittedSurfaceSnapshot> committedSurfaces() const;
  [[nodiscard]] std::optional<CommittedSurfaceSnapshot> cursorSurface() const;
  [[nodiscard]] std::optional<SnapPreviewSnapshot> snapPreview() const;
  [[nodiscard]] std::vector<int> duplicateDmabufFds(std::uint64_t surfaceId) const;
  [[nodiscard]] bool copyDmabufToRgba(std::uint64_t surfaceId, std::vector<std::uint8_t>& out) const;

  void dispatch();
  void flushClients();
  void setShortcutBindings(std::vector<ShortcutBinding> bindings);
  void updateAnimations(std::uint32_t timeMs, bool animationsEnabled);
  void sendFrameCallbacks(std::uint32_t timeMs);
  void handlePointerMotion(double dx, double dy, std::uint32_t timeMs);
  void handlePointerPosition(double x, double y, std::uint32_t timeMs);
  void handlePointerButton(std::uint32_t button, bool pressed, std::uint32_t timeMs);
  void handlePointerAxis(double dx, double dy, std::uint32_t timeMs);
  void handleKeyboardKey(std::uint32_t key, bool pressed, std::uint32_t timeMs);
  [[nodiscard]] float pointerX() const noexcept { return pointerX_; }
  [[nodiscard]] float pointerY() const noexcept { return pointerY_; }
  [[nodiscard]] CursorShape cursorShape() const noexcept { return cursorShape_; }

  // Protocol callbacks are plain C function pointers, so this implementation
  // state is public to the translation unit callbacks. It remains unexposed to
  // consumers because this header is private to the compositor executable.
  struct Surface;
  struct XdgSurface;
  struct XdgToplevel;
  struct ShmPool;
  struct ShmBuffer;
  struct DmabufParams;
  struct DmabufBuffer;
  struct ToplevelDecoration;
  struct Viewport;
  struct FractionalScale;
  struct CursorShapeDevice;
  struct IdleInhibitor;
  struct LayerSurface;
  struct PresentationFeedback;
  struct RelativePointer;
  struct PointerConstraint;
  struct PrimarySelectionDevice;
  struct PrimarySelectionSource;
  struct PrimarySelectionOffer;
  struct DataDevice;
  struct DataSource;
  struct DataOffer;

  wl_resource* createSurface(wl_client* client, std::uint32_t version, std::uint32_t id);
  void destroySurface(Surface* surface);
  void destroyXdgSurface(XdgSurface* surface);
  void destroyXdgToplevel(XdgToplevel* toplevel);
  void destroyShmPool(ShmPool* pool);
  void destroyShmBuffer(ShmBuffer* buffer);
  void destroyDmabufParams(DmabufParams* params);
  void destroyDmabufBuffer(DmabufBuffer* buffer);
  void destroyToplevelDecoration(ToplevelDecoration* decoration);
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

  wl_display* display_ = nullptr;
  wl_global* compositorGlobal_ = nullptr;
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
  std::string socketName_;
  std::string displayNameFile_;
  WaylandOutputInfo output_;
  std::vector<std::unique_ptr<Surface>> surfaces_;
  std::vector<std::unique_ptr<XdgSurface>> xdgSurfaces_;
  std::vector<std::unique_ptr<XdgToplevel>> toplevels_;
  std::vector<std::unique_ptr<ShmPool>> shmPools_;
  std::vector<std::unique_ptr<ShmBuffer>> shmBuffers_;
  std::vector<std::unique_ptr<DmabufParams>> dmabufParams_;
  std::vector<std::unique_ptr<DmabufBuffer>> dmabufBuffers_;
  std::vector<std::unique_ptr<ToplevelDecoration>> toplevelDecorations_;
  std::vector<std::unique_ptr<Viewport>> viewports_;
  std::vector<std::unique_ptr<FractionalScale>> fractionalScales_;
  std::vector<std::unique_ptr<CursorShapeDevice>> cursorShapeDevices_;
  std::vector<std::unique_ptr<IdleInhibitor>> idleInhibitors_;
  std::vector<std::unique_ptr<LayerSurface>> layerSurfaces_;
  std::vector<std::unique_ptr<PresentationFeedback>> presentationFeedbacks_;
  std::vector<std::unique_ptr<RelativePointer>> relativePointers_;
  std::vector<std::unique_ptr<PointerConstraint>> pointerConstraints_;
  std::vector<std::unique_ptr<PrimarySelectionDevice>> primarySelectionDevices_;
  std::vector<std::unique_ptr<PrimarySelectionSource>> primarySelectionSources_;
  std::vector<std::unique_ptr<PrimarySelectionOffer>> primarySelectionOffers_;
  PrimarySelectionSource* primarySelectionSource_ = nullptr;
  std::vector<std::unique_ptr<DataDevice>> dataDevices_;
  std::vector<std::unique_ptr<DataSource>> dataSources_;
  std::vector<std::unique_ptr<DataOffer>> dataOffers_;
  DataSource* selectionSource_ = nullptr;
  DataSource* dndSource_ = nullptr;
  Surface* dndOrigin_ = nullptr;
  Surface* dndTarget_ = nullptr;
  DataOffer* dndOffer_ = nullptr;
  std::vector<wl_resource*> seatResources_;
  std::vector<wl_resource*> pointerResources_;
  std::vector<wl_resource*> keyboardResources_;
  Surface* pointerFocus_ = nullptr;
  Surface* keyboardFocus_ = nullptr;
  Surface* dragSurface_ = nullptr;
  Surface* resizeSurface_ = nullptr;
  Surface* closePressSurface_ = nullptr;
  Surface* lastTitleClickSurface_ = nullptr;
  Surface* cursorSurface_ = nullptr;
  CursorShape cursorShape_ = CursorShape::Arrow;
  std::int32_t cursorHotspotX_ = 0;
  std::int32_t cursorHotspotY_ = 0;
  std::uint32_t pointerEnterSerial_ = 0;
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
  std::vector<ShortcutBinding> shortcutBindings_;
  std::uint32_t shiftModifierIndex_ = ~0u;
  std::uint32_t ctrlModifierIndex_ = ~0u;
  std::uint32_t altModifierIndex_ = ~0u;
  std::uint32_t logoModifierIndex_ = ~0u;
  float pointerX_ = 32.f;
  float pointerY_ = 32.f;
  std::uint64_t nextSurfaceId_ = 1;
  std::uint32_t nextConfigureSerial_ = 1;
  std::uint32_t nextInputSerial_ = 1;
};

} // namespace flux::compositor
