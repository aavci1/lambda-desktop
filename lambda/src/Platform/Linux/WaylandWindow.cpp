#include "UI/Platform/WindowFactory.hpp"
#include "UI/Platform/Application.hpp"
#include "Platform/Linux/Common/XkbState.hpp"
#include "Platform/Linux/GpuSurfaceProvider.hpp"
#include "Platform/Linux/WaylandNativeSurface.hpp"
#include "Platform/Linux/WaylandOutputs.hpp"
#include "Platform/Linux/WaylandScrollAccumulator.hpp"

#include <Lambda/Debug/DebugFlags.hpp>
#include <Lambda/Graphics/TextLayoutOptions.hpp>
#include <Lambda/Graphics/TextSystem.hpp>
#include <Lambda/UI/Application.hpp>
#include <Lambda/UI/EventQueue.hpp>
#include <Lambda/UI/Events.hpp>
#include <Lambda/UI/KeyCodes.hpp>
#include <Lambda/UI/MenuItem.hpp>
#include <Lambda/UI/Window.hpp>
#include <Lambda/UI/Views/Popover.hpp>

#include "Graphics/Vulkan/VulkanCanvas.hpp"

#include "Detail/ResizeTrace.hpp"
#include "UI/TransientPopoverHost.hpp"
#include "ext-background-effect-v1-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xx-cutouts-v1-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <linux/input-event-codes.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace lambda {

class WaylandWindow;

namespace {

struct SharedWaylandConnection;

struct WaylandMenuBuffer {
  wl_buffer* buffer = nullptr;
  void* pixels = nullptr;
  int fd = -1;
  int width = 0;
  int height = 0;
  int stride = 0;
  int size = 0;
};

struct WaylandPopupMenuState {
  ~WaylandPopupMenuState();

  PopupMenu menu;
  wl_surface* surface = nullptr;
  xdg_surface* xdgSurface = nullptr;
  xdg_popup* popup = nullptr;
  WaylandMenuBuffer buffer;
  WaylandWindow* owner = nullptr;
  SharedWaylandConnection* shared = nullptr;
  std::uint32_t grabSerial = 0;
  int width = 1;
  int height = 1;
  int rowHeight = 30;
  int hoverRow = -1;
  int pressedRow = -1;
  bool committed = false;
  bool grabbed = false;
};

struct WaylandPopoverSurfaceState {
  ~WaylandPopoverSurfaceState();

  PopoverSurfaceId id{};
  Popover popover;
  wl_surface* surface = nullptr;
  xdg_surface* xdgSurface = nullptr;
  xdg_popup* popup = nullptr;
  wl_callback* frameCallback = nullptr;
  WaylandNativeSurface nativeSurface{};
  std::unique_ptr<Canvas> canvas;
  std::unique_ptr<TransientPopoverHost> host;
  WaylandWindow* owner = nullptr;
  SharedWaylandConnection* shared = nullptr;
  std::uint32_t grabSerial = 0;
  std::uint32_t repositionToken = 1;
  int width = 1;
  int height = 1;
  Point pointerPos{};
  WaylandScrollAccumulator pendingScroll;
  int dispatchDepth = 0;
  bool committed = false;
  bool grabbed = false;
  bool redrawRequested = false;
  bool rendering = false;
  bool closeAfterEvent = false;
  bool closing = false;
  std::uint64_t renderCount = 0;
  std::uint64_t redrawRequestCount = 0;
  std::uint64_t frameRequestCount = 0;
  std::uint64_t frameDoneCount = 0;
  bool autotestEscapeDispatched = false;
};

struct WaylandClipboardOffer {
  wl_data_offer* offer = nullptr;
  std::vector<std::string> mimeTypes;
};

std::atomic<unsigned int> gNextHandle{1};
std::int64_t nowNanos() {
  using namespace std::chrono;
  return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

bool waylandPopoverTraceEnabled() {
  static int cached = -1;
  if (cached < 0) {
    cached = debug::envTruthy(std::getenv("LAMBDA_WAYLAND_POPOVER_TRACE")) ? 1 : 0;
  }
  return cached != 0;
}

bool waylandPopoverAutotestEscapeEnabled() {
  static int cached = -1;
  if (cached < 0) {
    cached = debug::envTruthy(std::getenv("LAMBDA_WAYLAND_POPOVER_AUTOTEST_ESCAPE")) ? 1 : 0;
  }
  return cached != 0;
}

void tracePopover(WaylandPopoverSurfaceState const& state,
                  char const* event,
                  char const* reason = "") {
  if (!waylandPopoverTraceEnabled()) return;
  std::fprintf(stderr,
               "%.3fms wayland-popover: event=%s reason=%s id=%" PRIu64
               " committed=%d redraw=%d rendering=%d frameCallback=%d renders=%" PRIu64
               " redrawRequests=%" PRIu64 " frameRequests=%" PRIu64 " frameDones=%" PRIu64 "\n",
               static_cast<double>(nowNanos()) / 1'000'000.0,
               event ? event : "",
               reason ? reason : "",
               state.id.value,
               state.committed ? 1 : 0,
               state.redrawRequested ? 1 : 0,
               state.rendering ? 1 : 0,
               state.frameCallback ? 1 : 0,
               state.renderCount,
               state.redrawRequestCount,
               state.frameRequestCount,
               state.frameDoneCount);
}

float safeScale(float scale) { return std::max(0.25f, scale); }

Point logicalPointFromFixed(wl_fixed_t x, wl_fixed_t y, float scaleX, float scaleY) {
  (void)scaleX;
  (void)scaleY;
  return {static_cast<float>(wl_fixed_to_double(x)), static_cast<float>(wl_fixed_to_double(y))};
}

MouseButton mouseButtonFromLinux(std::uint32_t button) {
  if (button == BTN_LEFT) return MouseButton::Left;
  if (button == BTN_RIGHT) return MouseButton::Right;
  if (button == BTN_MIDDLE) return MouseButton::Middle;
  return MouseButton::Other;
}

bool debugDecorations() {
  static bool const enabled = debug::envNonZero(std::getenv("LAMBDA_DEBUG_WAYLAND_DECORATIONS"));
  return enabled;
}

char const* const* cursorNames(Cursor cursor) {
  static char const* const arrow[] = {"default", "left_ptr", nullptr};
  static char const* const ibeam[] = {"text", "xterm", nullptr};
  static char const* const hand[] = {"pointer", "hand2", "hand1", nullptr};
  static char const* const resizeEW[] = {"ew-resize", "col-resize", "sb_h_double_arrow", nullptr};
  static char const* const resizeNS[] = {"ns-resize", "row-resize", "sb_v_double_arrow", nullptr};
  static char const* const resizeNESW[] = {"nesw-resize", "fd_double_arrow", nullptr};
  static char const* const resizeNWSE[] = {"nwse-resize", "bd_double_arrow", nullptr};
  static char const* const resizeAll[] = {"all-scroll", "move", "fleur", nullptr};
  static char const* const crosshair[] = {"crosshair", "cross", nullptr};
  static char const* const notAllowed[] = {"not-allowed", "crossed_circle", nullptr};

  switch (cursor) {
  case Cursor::Inherit:
  case Cursor::Arrow: return arrow;
  case Cursor::IBeam: return ibeam;
  case Cursor::Hand: return hand;
  case Cursor::ResizeEW: return resizeEW;
  case Cursor::ResizeNS: return resizeNS;
  case Cursor::ResizeNESW: return resizeNESW;
  case Cursor::ResizeNWSE: return resizeNWSE;
  case Cursor::ResizeAll: return resizeAll;
  case Cursor::Crosshair: return crosshair;
  case Cursor::NotAllowed: return notAllowed;
  }
  return arrow;
}

struct SharedWaylandConnection {
  wl_display* display = nullptr;
  wl_registry* registry = nullptr;
  wl_compositor* compositor = nullptr;
  wl_shm* shm = nullptr;
  wp_viewporter* viewporter = nullptr;
  wp_fractional_scale_manager_v1* fractionalScaleManager = nullptr;
  xdg_wm_base* wmBase = nullptr;
  zxdg_decoration_manager_v1* decorationManager = nullptr;
  std::uint32_t decorationManagerVersion = 0;
  xx_cutouts_manager_v1* cutoutsManager = nullptr;
  zwlr_layer_shell_v1* layerShell = nullptr;
  ext_background_effect_manager_v1* backgroundEffectManager = nullptr;
  wl_data_device_manager* dataDeviceManager = nullptr;
  wl_seat* seat = nullptr;
  wl_data_device* dataDevice = nullptr;
  wl_pointer* pointer = nullptr;
  wl_cursor_theme* cursorTheme = nullptr;
  wl_surface* cursorSurface = nullptr;
  int cursorThemeScale = 1;
  wl_keyboard* keyboard = nullptr;
  std::unique_ptr<linux_platform::XkbState> xkb;
  struct Output {
    wl_output* output = nullptr;
    std::uint32_t name = 0;
    std::string displayName;
    float scale = 1.f;
  };
  std::vector<std::unique_ptr<Output>> outputs;
  std::vector<WaylandWindow*> windows;
  WaylandWindow* pointerFocus = nullptr;
  WaylandWindow* popupPointerFocus = nullptr;
  wl_surface* popupPointerSurface = nullptr;
  WaylandWindow* keyboardFocus = nullptr;
  wl_surface* keyboardSurface = nullptr;
  int keyboardRepeatRate = 0;
  int keyboardRepeatDelayMs = 0;
  std::uint32_t repeatKey = 0;
  wl_surface* repeatSurface = nullptr;
  WaylandWindow* repeatWindow = nullptr;
  std::uint64_t repeatTimerId = 0;
  bool repeatDelayPhase = false;
  EventQueue* repeatHandlerQueue = nullptr;
  std::uint32_t lastSelectionSerial = 0;
  wl_data_source* clipboardSource = nullptr;
  std::string clipboardText;
  std::vector<std::unique_ptr<WaylandClipboardOffer>> clipboardOffers;
  WaylandClipboardOffer* selectionOffer = nullptr;
  unsigned int refs = 0;
  bool fatalError = false;
  bool shutdownRequested = false;
  int fatalErrno = 0;
  std::string fatalContext;
};

std::mutex gWaylandConnectionMutex;
SharedWaylandConnection gWaylandConnection;

void sharedRegistryGlobal(void* data, wl_registry* registry, std::uint32_t name,
                          char const* interface, std::uint32_t version);
void sharedRegistryRemove(void* data, wl_registry*, std::uint32_t name);
void sharedWmPing(void*, xdg_wm_base* base, std::uint32_t serial);
void sharedSeatCapabilities(void* data, wl_seat* seat, std::uint32_t caps);
void sharedSeatName(void*, wl_seat*, char const*);
void clipboardOfferOffer(void* data, wl_data_offer*, char const* mimeType);
void clipboardOfferSourceActions(void*, wl_data_offer*, std::uint32_t);
void clipboardOfferAction(void*, wl_data_offer*, std::uint32_t);
void clipboardSourceTarget(void*, wl_data_source*, char const*);
void clipboardSourceSend(void* data, wl_data_source* source, char const* mimeType, int fd);
void clipboardSourceCancelled(void* data, wl_data_source* source);
void clipboardSourceDndDropPerformed(void*, wl_data_source*);
void clipboardSourceDndFinished(void*, wl_data_source*);
void clipboardSourceAction(void*, wl_data_source*, std::uint32_t);
void clipboardDeviceDataOffer(void* data, wl_data_device*, wl_data_offer* offer);
void clipboardDeviceEnter(void*, wl_data_device*, std::uint32_t, wl_surface*, wl_fixed_t, wl_fixed_t, wl_data_offer*);
void clipboardDeviceLeave(void*, wl_data_device*);
void clipboardDeviceMotion(void*, wl_data_device*, std::uint32_t, wl_fixed_t, wl_fixed_t);
void clipboardDeviceDrop(void*, wl_data_device*);
void clipboardDeviceSelection(void* data, wl_data_device*, wl_data_offer* offer);
void sharedOutputGeometry(void*, wl_output*, std::int32_t, std::int32_t, std::int32_t, std::int32_t,
                          std::int32_t, char const*, char const*, std::int32_t);
void sharedOutputMode(void*, wl_output*, std::uint32_t, std::int32_t, std::int32_t, std::int32_t);
void sharedOutputDone(void*, wl_output*);
void sharedOutputScale(void* data, wl_output*, std::int32_t scale);
void sharedOutputName(void*, wl_output*, char const*);
void sharedOutputDescription(void*, wl_output*, char const*);
void sharedPointerEnter(void* data, wl_pointer*, std::uint32_t serial, wl_surface* surface, wl_fixed_t x, wl_fixed_t y);
void sharedPointerLeave(void* data, wl_pointer*, std::uint32_t, wl_surface* surface);
void sharedPointerMotion(void* data, wl_pointer*, std::uint32_t, wl_fixed_t x, wl_fixed_t y);
void sharedPointerButton(void* data, wl_pointer*, std::uint32_t, std::uint32_t, std::uint32_t button,
                         std::uint32_t state);
void sharedPointerAxis(void* data, wl_pointer*, std::uint32_t, std::uint32_t axis, wl_fixed_t value);
void sharedPointerFrame(void*, wl_pointer*);
void sharedPointerAxisSource(void*, wl_pointer*, std::uint32_t);
void sharedPointerAxisStop(void*, wl_pointer*, std::uint32_t, std::uint32_t);
void sharedPointerAxisDiscrete(void*, wl_pointer*, std::uint32_t, std::int32_t);
void sharedPointerAxisValue120(void*, wl_pointer*, std::uint32_t, std::int32_t);
void sharedPointerAxisRelativeDirection(void*, wl_pointer*, std::uint32_t, std::uint32_t);
void sharedKeymap(void* data, wl_keyboard*, std::uint32_t format, int fd, std::uint32_t size);
void sharedKeyboardEnter(void* data, wl_keyboard*, std::uint32_t, wl_surface* surface, wl_array*);
void sharedKeyboardLeave(void* data, wl_keyboard*, std::uint32_t, wl_surface* surface);
void sharedKeyboardKey(void* data, wl_keyboard*, std::uint32_t, std::uint32_t, std::uint32_t key,
                       std::uint32_t state);
void sharedKeyboardModifiers(void* data, wl_keyboard*, std::uint32_t, std::uint32_t depressed,
                             std::uint32_t latched, std::uint32_t locked, std::uint32_t group);
void sharedKeyboardRepeatInfo(void*, wl_keyboard*, std::int32_t, std::int32_t);
void stopKeyboardRepeat(SharedWaylandConnection* shared);
void handleKeyboardRepeatTimer(SharedWaylandConnection* shared, TimerEvent const& event);

wl_registry_listener const sharedRegistryListener{sharedRegistryGlobal, sharedRegistryRemove};
xdg_wm_base_listener const sharedWmBaseListener{sharedWmPing};
wl_seat_listener const sharedSeatListener{sharedSeatCapabilities, sharedSeatName};
wl_data_offer_listener const clipboardOfferListener{clipboardOfferOffer, clipboardOfferSourceActions,
                                                    clipboardOfferAction};
wl_data_source_listener const clipboardSourceListener{clipboardSourceTarget,
                                                     clipboardSourceSend,
                                                     clipboardSourceCancelled,
                                                     clipboardSourceDndDropPerformed,
                                                     clipboardSourceDndFinished,
                                                     clipboardSourceAction};
wl_data_device_listener const clipboardDeviceListener{clipboardDeviceDataOffer,
                                                     clipboardDeviceEnter,
                                                     clipboardDeviceLeave,
                                                     clipboardDeviceMotion,
                                                     clipboardDeviceDrop,
                                                     clipboardDeviceSelection};
wl_output_listener const sharedOutputListener{sharedOutputGeometry, sharedOutputMode, sharedOutputDone,
                                             sharedOutputScale, sharedOutputName, sharedOutputDescription};
wl_pointer_listener const sharedPointerListener{sharedPointerEnter, sharedPointerLeave, sharedPointerMotion,
                                               sharedPointerButton, sharedPointerAxis, sharedPointerFrame,
                                               sharedPointerAxisSource, sharedPointerAxisStop,
                                               sharedPointerAxisDiscrete, sharedPointerAxisValue120,
                                               sharedPointerAxisRelativeDirection};
wl_keyboard_listener const sharedKeyboardListener{sharedKeymap, sharedKeyboardEnter, sharedKeyboardLeave,
                                                 sharedKeyboardKey, sharedKeyboardModifiers,
                                                 sharedKeyboardRepeatInfo};

bool canSendWaylandRequests(SharedWaylandConnection const* shared) {
  return shared && shared->display && !shared->fatalError && wl_display_get_error(shared->display) == 0;
}

void requestWaylandShutdown(SharedWaylandConnection* shared) {
  if (!shared || shared->shutdownRequested) {
    return;
  }
  shared->shutdownRequested = true;
  if (Application::hasInstance()) {
    try {
      Application::instance().quit();
    } catch (...) {
    }
  }
}

void markWaylandConnectionDead(SharedWaylandConnection* shared, char const* context, int error = 0) {
  if (!shared) {
    return;
  }
  if (error == 0 && shared->display) {
    error = wl_display_get_error(shared->display);
  }
  if (error == 0) {
    error = EPIPE;
  }
  if (!shared->fatalError) {
    shared->fatalError = true;
    shared->fatalErrno = error;
    shared->fatalContext = context ? context : "Wayland display";
    std::fprintf(stderr, "lambda-wayland: compositor connection lost during %s: %s\n",
                 shared->fatalContext.c_str(), std::strerror(error));
  }
  requestWaylandShutdown(shared);
}

bool checkWaylandConnection(SharedWaylandConnection* shared, char const* context) {
  if (!shared || !shared->display) {
    return false;
  }
  if (shared->fatalError) {
    requestWaylandShutdown(shared);
    return false;
  }
  int const error = wl_display_get_error(shared->display);
  if (error != 0) {
    markWaylandConnectionDead(shared, context, error);
    return false;
  }
  return true;
}

bool flushWaylandDisplay(SharedWaylandConnection* shared, char const* context) {
  if (!checkWaylandConnection(shared, context)) {
    return false;
  }
  for (;;) {
    if (wl_display_flush(shared->display) >= 0) {
      return true;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return true;
    }
    markWaylandConnectionDead(shared, context, errno);
    return false;
  }
}

bool clipboardTextMime(std::string_view mimeType) {
  return mimeType == "text/plain;charset=utf-8" ||
         mimeType == "text/plain" ||
         mimeType == "UTF8_STRING" ||
         mimeType == "STRING";
}

std::optional<std::string> bestClipboardMime(WaylandClipboardOffer const* offer) {
  if (!offer) return std::nullopt;
  static constexpr std::string_view preferred[] = {
      "text/plain;charset=utf-8",
      "text/plain",
      "UTF8_STRING",
      "STRING",
  };
  for (std::string_view mime : preferred) {
    auto found = std::find(offer->mimeTypes.begin(), offer->mimeTypes.end(), mime);
    if (found != offer->mimeTypes.end()) return *found;
  }
  for (std::string const& mime : offer->mimeTypes) {
    if (mime.starts_with("text/plain")) return mime;
  }
  return std::nullopt;
}

void noteSelectionSerial(SharedWaylandConnection* shared, std::uint32_t serial) {
  if (shared && serial != 0) {
    shared->lastSelectionSerial = serial;
  }
}

void destroyClipboardSource(SharedWaylandConnection* shared) {
  if (!shared || !shared->clipboardSource) return;
  if (canSendWaylandRequests(shared)) {
    wl_data_source_destroy(shared->clipboardSource);
  }
  shared->clipboardSource = nullptr;
}

void clearClipboardOffers(SharedWaylandConnection& shared, bool destroyProxies) {
  for (auto& offer : shared.clipboardOffers) {
    if (offer && offer->offer && destroyProxies) {
      wl_data_offer_destroy(offer->offer);
    }
  }
  shared.clipboardOffers.clear();
  shared.selectionOffer = nullptr;
}

bool ensureClipboardDataDevice(SharedWaylandConnection* shared) {
  if (!canSendWaylandRequests(shared)) return false;
  if (shared->dataDevice) return true;
  if (!shared->dataDeviceManager || !shared->seat) return false;
  shared->dataDevice = wl_data_device_manager_get_data_device(shared->dataDeviceManager, shared->seat);
  if (!shared->dataDevice) return false;
  wl_data_device_add_listener(shared->dataDevice, &clipboardDeviceListener, shared);
  flushWaylandDisplay(shared, "clipboard data-device setup");
  return true;
}

std::optional<std::string> readClipboardOfferText(SharedWaylandConnection* shared,
                                                  WaylandClipboardOffer* offer,
                                                  std::string const& mimeType) {
  if (!canSendWaylandRequests(shared) || !offer || !offer->offer) return std::nullopt;
  int pipeFds[2]{-1, -1};
  if (pipe2(pipeFds, O_CLOEXEC | O_NONBLOCK) != 0) {
    return std::nullopt;
  }
  wl_data_offer_accept(offer->offer, shared->lastSelectionSerial, mimeType.c_str());
  wl_data_offer_receive(offer->offer, mimeType.c_str(), pipeFds[1]);
  close(pipeFds[1]);
  pipeFds[1] = -1;
  if (!flushWaylandDisplay(shared, "clipboard receive")) {
    close(pipeFds[0]);
    return std::nullopt;
  }

  std::string out;
  char buffer[4096];
  auto const deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{1000};
  for (;;) {
    ssize_t const n = read(pipeFds[0], buffer, sizeof(buffer));
    if (n > 0) {
      out.append(buffer, static_cast<std::size_t>(n));
      continue;
    }
    if (n == 0) {
      close(pipeFds[0]);
      return out.empty() ? std::nullopt : std::optional<std::string>(std::move(out));
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      close(pipeFds[0]);
      return std::nullopt;
    }
    auto const now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      close(pipeFds[0]);
      return out.empty() ? std::nullopt : std::optional<std::string>(std::move(out));
    }
    auto const waitMs = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
    pollfd fd{.fd = pipeFds[0], .events = POLLIN, .revents = 0};
    (void)poll(&fd, 1, static_cast<int>(std::min<std::int64_t>(waitMs, 50)));
    if (shared->display) {
      (void)wl_display_dispatch_pending(shared->display);
    }
  }
}

void clearDisconnectedWaylandGlobals(SharedWaylandConnection& shared) {
  shared.registry = nullptr;
  shared.compositor = nullptr;
  shared.shm = nullptr;
  shared.viewporter = nullptr;
  shared.fractionalScaleManager = nullptr;
  shared.wmBase = nullptr;
  shared.decorationManager = nullptr;
  shared.decorationManagerVersion = 0;
  shared.cutoutsManager = nullptr;
  shared.layerShell = nullptr;
  shared.backgroundEffectManager = nullptr;
  shared.dataDeviceManager = nullptr;
  shared.seat = nullptr;
  shared.dataDevice = nullptr;
  shared.pointer = nullptr;
  shared.cursorSurface = nullptr;
  shared.keyboard = nullptr;
  shared.outputs.clear();
  shared.pointerFocus = nullptr;
  shared.popupPointerFocus = nullptr;
  shared.popupPointerSurface = nullptr;
  shared.keyboardFocus = nullptr;
  shared.keyboardSurface = nullptr;
  shared.keyboardRepeatRate = 0;
  shared.keyboardRepeatDelayMs = 0;
  shared.repeatKey = 0;
  shared.repeatSurface = nullptr;
  shared.repeatWindow = nullptr;
  shared.repeatTimerId = 0;
  shared.repeatDelayPhase = false;
  shared.lastSelectionSerial = 0;
  shared.clipboardSource = nullptr;
  shared.clipboardText.clear();
  clearClipboardOffers(shared, false);
}

std::uint32_t layerShellLayer(LayerShellLayer layer) {
  switch (layer) {
  case LayerShellLayer::Background: return ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
  case LayerShellLayer::Bottom: return ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM;
  case LayerShellLayer::Top: return ZWLR_LAYER_SHELL_V1_LAYER_TOP;
  case LayerShellLayer::Overlay: return ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
  }
  return ZWLR_LAYER_SHELL_V1_LAYER_TOP;
}

std::uint32_t layerShellAnchor(LayerShellOptions const& options) {
  std::uint32_t anchor = 0;
  if (options.anchorTop) anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP;
  if (options.anchorBottom) anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM;
  if (options.anchorLeft) anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT;
  if (options.anchorRight) anchor |= ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT;
  return anchor;
}

int roundedLayerShellDimension(float value) {
  return std::max(0, static_cast<int>(std::lround(value)));
}

std::uint32_t layerShellProtocolWidth(Size const& size, LayerShellOptions const& options) {
  int const width = roundedLayerShellDimension(size.width);
  // Layer-shell size 0 is only valid on axes anchored to both opposing edges.
  if (width == 0 && !(options.anchorLeft && options.anchorRight)) return 1u;
  return static_cast<std::uint32_t>(width);
}

std::uint32_t layerShellProtocolHeight(Size const& size, LayerShellOptions const& options) {
  int const height = roundedLayerShellDimension(size.height);
  if (height == 0 && !(options.anchorTop && options.anchorBottom)) return 1u;
  return static_cast<std::uint32_t>(height);
}

std::uint32_t colorToRgba(Color color) {
  auto channel = [&](float value) {
    return static_cast<std::uint32_t>(std::clamp(value, 0.f, 1.f) * 255.f + 0.5f);
  };
  return (channel(color.r) << 24u) | (channel(color.g) << 16u) | (channel(color.b) << 8u) |
         channel(color.a);
}

std::uint32_t backgroundEffectShape(LayerShellBackgroundEffectShape shape) {
  switch (shape) {
  case LayerShellBackgroundEffectShape::Callout:
    return EXT_BACKGROUND_EFFECT_SURFACE_V1_SHAPE_CALLOUT;
  case LayerShellBackgroundEffectShape::RoundedRect:
  default:
    return EXT_BACKGROUND_EFFECT_SURFACE_V1_SHAPE_ROUNDED_RECT;
  }
}

std::uint32_t backgroundEffectCalloutPlacement(LayerShellCalloutPlacement placement) {
  switch (placement) {
  case LayerShellCalloutPlacement::Above:
    return EXT_BACKGROUND_EFFECT_SURFACE_V1_CALLOUT_PLACEMENT_ABOVE;
  case LayerShellCalloutPlacement::End:
    return EXT_BACKGROUND_EFFECT_SURFACE_V1_CALLOUT_PLACEMENT_END;
  case LayerShellCalloutPlacement::Start:
    return EXT_BACKGROUND_EFFECT_SURFACE_V1_CALLOUT_PLACEMENT_START;
  case LayerShellCalloutPlacement::Below:
  default:
    return EXT_BACKGROUND_EFFECT_SURFACE_V1_CALLOUT_PLACEMENT_BELOW;
  }
}

int createPopupSharedMemoryFile(char const* name, std::size_t size) {
  int fd = memfd_create(name, MFD_CLOEXEC | MFD_ALLOW_SEALING);
  if (fd < 0) {
    throw std::runtime_error(std::string("memfd_create failed: ") + std::strerror(errno));
  }
  if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
    close(fd);
    throw std::runtime_error(std::string("ftruncate failed: ") + std::strerror(errno));
  }
  return fd;
}

void destroyWaylandMenuBuffer(WaylandMenuBuffer& buffer, bool destroyProxy = true) {
  if (buffer.buffer && destroyProxy) wl_buffer_destroy(buffer.buffer);
  if (buffer.pixels) munmap(buffer.pixels, static_cast<std::size_t>(buffer.size));
  if (buffer.fd >= 0) close(buffer.fd);
  buffer = {};
  buffer.fd = -1;
}

WaylandMenuBuffer createWaylandMenuBuffer(wl_shm* shm, int width, int height) {
  WaylandMenuBuffer buffer;
  buffer.width = std::max(1, width);
  buffer.height = std::max(1, height);
  buffer.stride = buffer.width * 4;
  buffer.size = buffer.stride * buffer.height;
  buffer.fd = createPopupSharedMemoryFile("lambda-popup-menu", static_cast<std::size_t>(buffer.size));
  buffer.pixels = mmap(nullptr, static_cast<std::size_t>(buffer.size), PROT_READ | PROT_WRITE, MAP_SHARED,
                       buffer.fd, 0);
  if (buffer.pixels == MAP_FAILED) {
    buffer.pixels = nullptr;
    destroyWaylandMenuBuffer(buffer);
    throw std::runtime_error(std::string("mmap failed: ") + std::strerror(errno));
  }
  wl_shm_pool* pool = wl_shm_create_pool(shm, buffer.fd, buffer.size);
  buffer.buffer = wl_shm_pool_create_buffer(pool, 0, buffer.width, buffer.height, buffer.stride,
                                            WL_SHM_FORMAT_XRGB8888);
  wl_shm_pool_destroy(pool);
  return buffer;
}

std::uint8_t colorByte(float value) {
  return static_cast<std::uint8_t>(std::clamp(value, 0.f, 1.f) * 255.f + 0.5f);
}

void putPixel(WaylandMenuBuffer& buffer, int x, int y, Color color) {
  if (!buffer.pixels || x < 0 || y < 0 || x >= buffer.width || y >= buffer.height) {
    return;
  }
  auto* bytes = static_cast<std::uint8_t*>(buffer.pixels);
  std::size_t const offset = static_cast<std::size_t>(y) * static_cast<std::size_t>(buffer.stride) +
                             static_cast<std::size_t>(x) * 4u;
  bytes[offset + 0u] = colorByte(color.b);
  bytes[offset + 1u] = colorByte(color.g);
  bytes[offset + 2u] = colorByte(color.r);
  bytes[offset + 3u] = 0xff;
}

void blendPixel(WaylandMenuBuffer& buffer, int x, int y, Color color, float alpha) {
  if (!buffer.pixels || x < 0 || y < 0 || x >= buffer.width || y >= buffer.height) {
    return;
  }
  alpha = std::clamp(alpha * color.a, 0.f, 1.f);
  if (alpha <= 0.f) {
    return;
  }
  auto* bytes = static_cast<std::uint8_t*>(buffer.pixels);
  std::size_t const offset = static_cast<std::size_t>(y) * static_cast<std::size_t>(buffer.stride) +
                             static_cast<std::size_t>(x) * 4u;
  float const srcR = std::clamp(color.r, 0.f, 1.f);
  float const srcG = std::clamp(color.g, 0.f, 1.f);
  float const srcB = std::clamp(color.b, 0.f, 1.f);
  float const dstB = static_cast<float>(bytes[offset + 0u]) / 255.f;
  float const dstG = static_cast<float>(bytes[offset + 1u]) / 255.f;
  float const dstR = static_cast<float>(bytes[offset + 2u]) / 255.f;
  bytes[offset + 0u] = colorByte(srcB * alpha + dstB * (1.f - alpha));
  bytes[offset + 1u] = colorByte(srcG * alpha + dstG * (1.f - alpha));
  bytes[offset + 2u] = colorByte(srcR * alpha + dstR * (1.f - alpha));
  bytes[offset + 3u] = 0xff;
}

void fillRect(WaylandMenuBuffer& buffer, int x, int y, int width, int height, Color color) {
  int const x0 = std::clamp(x, 0, buffer.width);
  int const y0 = std::clamp(y, 0, buffer.height);
  int const x1 = std::clamp(x + width, 0, buffer.width);
  int const y1 = std::clamp(y + height, 0, buffer.height);
  for (int py = y0; py < y1; ++py) {
    for (int px = x0; px < x1; ++px) {
      putPixel(buffer, px, py, color);
    }
  }
}

bool popupMenuItemEnabled(MenuItem const& item) {
  return !item.isEnabled || item.isEnabled();
}

bool popupMenuItemActivatable(MenuItem const& item) {
  return item.role != MenuRole::Separator && item.role != MenuRole::Submenu &&
         (static_cast<bool>(item.handler) || !item.actionName.empty()) &&
         popupMenuItemEnabled(item);
}

int popupMenuRowForY(WaylandPopupMenuState const& state, wl_fixed_t surfaceY) {
  if (state.menu.items.empty()) {
    return -1;
  }
  int const row = static_cast<int>(wl_fixed_to_double(surfaceY)) / std::max(1, state.rowHeight);
  return std::clamp(row, 0, static_cast<int>(state.menu.items.size()) - 1);
}

void drawCheckMark(WaylandMenuBuffer& buffer, int originX, int originY, Color color) {
  for (int i = 0; i < 5; ++i) {
    putPixel(buffer, originX + i, originY + 5 + i, color);
    putPixel(buffer, originX + i, originY + 6 + i, color);
  }
  for (int i = 0; i < 9; ++i) {
    putPixel(buffer, originX + 4 + i, originY + 9 - i, color);
    putPixel(buffer, originX + 4 + i, originY + 10 - i, color);
  }
}

void drawPopupText(WaylandMenuBuffer& buffer, std::string const& text, int x, int y, int maxWidth,
                   Color color) {
  if (text.empty() || maxWidth <= 0) {
    return;
  }
  TextLayoutOptions options;
  options.wrapping = TextWrapping::NoWrap;
  options.maxLines = 1;
  Font font{.size = 13.f, .weight = 450.f};
  auto layout = Application::instance().textSystem().layout(text, font, color, static_cast<float>(maxWidth),
                                                            options);
  if (!layout) {
    return;
  }
  TextSystem& textSystem = Application::instance().textSystem();
  float const textTop = static_cast<float>(y) + std::max(0.f, (30.f - layout->measuredSize.height) * 0.5f);
  Point const origin{static_cast<float>(x), textTop};
  for (TextLayout::PlacedRun const& placed : layout->runs) {
    std::size_t const glyphCount = std::min(placed.run.glyphIds.size(), placed.run.positions.size());
    for (std::size_t i = 0; i < glyphCount; ++i) {
      std::uint32_t gw = 0;
      std::uint32_t gh = 0;
      Point bearing{};
      std::vector<std::uint8_t> alpha =
          textSystem.rasterizeGlyph(placed.run.fontId, placed.run.glyphIds[i], placed.run.fontSize,
                                    gw, gh, bearing);
      if (gw == 0 || gh == 0 || alpha.empty()) {
        continue;
      }
      Point const pos = origin + placed.origin + placed.run.positions[i];
      int const glyphX = static_cast<int>(std::lround(pos.x + bearing.x));
      int const glyphY = static_cast<int>(std::lround(pos.y - bearing.y));
      for (std::uint32_t py = 0; py < gh; ++py) {
        for (std::uint32_t px = 0; px < gw; ++px) {
          std::size_t const alphaOffset = static_cast<std::size_t>(py) * gw + px;
          blendPixel(buffer, glyphX + static_cast<int>(px), glyphY + static_cast<int>(py),
                     color, static_cast<float>(alpha[alphaOffset]) / 255.f);
        }
      }
    }
  }
}

void renderPopupMenu(WaylandPopupMenuState& state) {
  if (!state.buffer.pixels) {
    return;
  }
  Color const background{0.98f, 0.985f, 0.995f, 1.f};
  Color const border{0.78f, 0.81f, 0.87f, 1.f};
  Color const hover{0.84f, 0.90f, 1.f, 1.f};
  Color const text{0.12f, 0.16f, 0.24f, 1.f};
  Color const disabled{0.54f, 0.58f, 0.67f, 1.f};
  Color const accent{0.16f, 0.48f, 1.f, 1.f};

  fillRect(state.buffer, 0, 0, state.width, state.height, background);
  for (int row = 0; row < static_cast<int>(state.menu.items.size()); ++row) {
    MenuItem const& item = state.menu.items[static_cast<std::size_t>(row)];
    int const y = row * state.rowHeight;
    if (item.role == MenuRole::Separator) {
      fillRect(state.buffer, 10, y + state.rowHeight / 2, state.width - 20, 1, border);
      continue;
    }
    if (row == state.hoverRow && popupMenuItemEnabled(item)) {
      fillRect(state.buffer, 4, y + 3, state.width - 8, state.rowHeight - 6, hover);
    }
    if (item.checked) {
      drawCheckMark(state.buffer, 12, y + 8, accent);
    }
    Color const labelColor = popupMenuItemEnabled(item) ? text : disabled;
    drawPopupText(state.buffer, item.label, 34, y, state.width - 46, labelColor);
  }
  fillRect(state.buffer, 0, 0, state.width, 1, border);
  fillRect(state.buffer, 0, state.height - 1, state.width, 1, border);
  fillRect(state.buffer, 0, 0, 1, state.height, border);
  fillRect(state.buffer, state.width - 1, 0, 1, state.height, border);
}

void commitPopupMenuBuffer(WaylandPopupMenuState& state) {
  if (!state.surface || !state.buffer.buffer) {
    return;
  }
  wl_surface_attach(state.surface, state.buffer.buffer, 0, 0);
  wl_surface_damage_buffer(state.surface, 0, 0, state.buffer.width, state.buffer.height);
  wl_surface_commit(state.surface);
}

WaylandPopupMenuState::~WaylandPopupMenuState() {
  bool const destroyProxies = canSendWaylandRequests(shared);
  if (destroyProxies) {
    if (popup) xdg_popup_destroy(popup);
    if (xdgSurface) xdg_surface_destroy(xdgSurface);
    if (surface) wl_surface_destroy(surface);
  }
  destroyWaylandMenuBuffer(buffer, destroyProxies);
}

WaylandPopoverSurfaceState::~WaylandPopoverSurfaceState() {
  if (host) {
    host->notifyDismissed();
  }
  canvas.reset();
  if (canSendWaylandRequests(shared)) {
    if (frameCallback) wl_callback_destroy(frameCallback);
    if (popup) xdg_popup_destroy(popup);
    if (xdgSurface) xdg_surface_destroy(xdgSurface);
    if (surface) wl_surface_destroy(surface);
  }
}

SharedWaylandConnection* acquireWaylandConnection() {
  std::lock_guard lock(gWaylandConnectionMutex);
  if (!gWaylandConnection.display) {
    gWaylandConnection.fatalError = false;
    gWaylandConnection.shutdownRequested = false;
    gWaylandConnection.fatalErrno = 0;
    gWaylandConnection.fatalContext.clear();
    gWaylandConnection.display = wl_display_connect(nullptr);
    if (!gWaylandConnection.display) {
      throw std::runtime_error("Failed to connect to Wayland display");
    }
    gWaylandConnection.xkb = std::make_unique<linux_platform::XkbState>();
    if (!gWaylandConnection.xkb->createDefaultKeymap()) {
      wl_display_disconnect(gWaylandConnection.display);
      gWaylandConnection.display = nullptr;
      throw std::runtime_error("Failed to create XKB context");
    }
    gWaylandConnection.registry = wl_display_get_registry(gWaylandConnection.display);
    wl_registry_add_listener(gWaylandConnection.registry, &sharedRegistryListener, &gWaylandConnection);
    if (wl_display_roundtrip(gWaylandConnection.display) < 0) {
      int const error = wl_display_get_error(gWaylandConnection.display);
      gWaylandConnection.xkb.reset();
      wl_display_disconnect(gWaylandConnection.display);
      gWaylandConnection.display = nullptr;
      clearDisconnectedWaylandGlobals(gWaylandConnection);
      throw std::runtime_error(std::string("Wayland registry roundtrip failed: ") +
                               std::strerror(error == 0 ? EPIPE : error));
    }
    if (!gWaylandConnection.compositor || !gWaylandConnection.wmBase) {
      gWaylandConnection.xkb.reset();
      wl_display_disconnect(gWaylandConnection.display);
      gWaylandConnection.display = nullptr;
      clearDisconnectedWaylandGlobals(gWaylandConnection);
      throw std::runtime_error("Wayland compositor does not expose required xdg-shell globals");
    }
  } else if (gWaylandConnection.fatalError) {
    throw std::runtime_error("Wayland display connection is shutting down");
  }
  ++gWaylandConnection.refs;
  return &gWaylandConnection;
}

void releaseWaylandConnection() {
  std::lock_guard lock(gWaylandConnectionMutex);
  if (gWaylandConnection.refs == 0) return;
  --gWaylandConnection.refs;
  if (gWaylandConnection.refs != 0) return;
  stopKeyboardRepeat(&gWaylandConnection);
  bool const destroyProxies = canSendWaylandRequests(&gWaylandConnection);
  if (gWaylandConnection.keyboard) {
    if (destroyProxies) wl_keyboard_destroy(gWaylandConnection.keyboard);
    gWaylandConnection.keyboard = nullptr;
  }
  if (gWaylandConnection.pointer) {
    if (destroyProxies) wl_pointer_destroy(gWaylandConnection.pointer);
    gWaylandConnection.pointer = nullptr;
  }
  destroyClipboardSource(&gWaylandConnection);
  clearClipboardOffers(gWaylandConnection, destroyProxies);
  if (gWaylandConnection.dataDevice) {
    if (destroyProxies) wl_data_device_release(gWaylandConnection.dataDevice);
    gWaylandConnection.dataDevice = nullptr;
  }
  if (gWaylandConnection.cursorSurface) {
    if (destroyProxies) wl_surface_destroy(gWaylandConnection.cursorSurface);
    gWaylandConnection.cursorSurface = nullptr;
  }
  if (gWaylandConnection.cursorTheme) {
    wl_cursor_theme_destroy(gWaylandConnection.cursorTheme);
    gWaylandConnection.cursorTheme = nullptr;
  }
  if (gWaylandConnection.seat) {
    if (destroyProxies) wl_seat_destroy(gWaylandConnection.seat);
    gWaylandConnection.seat = nullptr;
  }
  for (auto& output : gWaylandConnection.outputs) {
    if (output->output && destroyProxies) wl_output_destroy(output->output);
  }
  gWaylandConnection.outputs.clear();
  if (gWaylandConnection.decorationManager) {
    if (destroyProxies) zxdg_decoration_manager_v1_destroy(gWaylandConnection.decorationManager);
    gWaylandConnection.decorationManager = nullptr;
    gWaylandConnection.decorationManagerVersion = 0;
  }
  if (gWaylandConnection.cutoutsManager) {
    if (destroyProxies) xx_cutouts_manager_v1_destroy(gWaylandConnection.cutoutsManager);
    gWaylandConnection.cutoutsManager = nullptr;
  }
  if (gWaylandConnection.viewporter) {
    if (destroyProxies) wp_viewporter_destroy(gWaylandConnection.viewporter);
    gWaylandConnection.viewporter = nullptr;
  }
  if (gWaylandConnection.fractionalScaleManager) {
    if (destroyProxies) wp_fractional_scale_manager_v1_destroy(gWaylandConnection.fractionalScaleManager);
    gWaylandConnection.fractionalScaleManager = nullptr;
  }
  if (gWaylandConnection.wmBase) {
    if (destroyProxies) xdg_wm_base_destroy(gWaylandConnection.wmBase);
    gWaylandConnection.wmBase = nullptr;
  }
  if (gWaylandConnection.compositor) {
    if (destroyProxies) wl_compositor_destroy(gWaylandConnection.compositor);
    gWaylandConnection.compositor = nullptr;
  }
  if (gWaylandConnection.layerShell) {
    if (destroyProxies) zwlr_layer_shell_v1_destroy(gWaylandConnection.layerShell);
    gWaylandConnection.layerShell = nullptr;
  }
  if (gWaylandConnection.backgroundEffectManager) {
    if (destroyProxies) ext_background_effect_manager_v1_destroy(gWaylandConnection.backgroundEffectManager);
    gWaylandConnection.backgroundEffectManager = nullptr;
  }
  if (gWaylandConnection.dataDeviceManager) {
    if (destroyProxies) wl_data_device_manager_destroy(gWaylandConnection.dataDeviceManager);
    gWaylandConnection.dataDeviceManager = nullptr;
  }
  if (gWaylandConnection.shm) {
    if (destroyProxies) wl_shm_destroy(gWaylandConnection.shm);
    gWaylandConnection.shm = nullptr;
  }
  if (gWaylandConnection.registry) {
    if (destroyProxies) wl_registry_destroy(gWaylandConnection.registry);
    gWaylandConnection.registry = nullptr;
  }
  gWaylandConnection.xkb.reset();
  if (gWaylandConnection.display) {
    wl_display_disconnect(gWaylandConnection.display);
    gWaylandConnection.display = nullptr;
  }
  gWaylandConnection.fatalError = false;
  gWaylandConnection.shutdownRequested = false;
  gWaylandConnection.fatalErrno = 0;
  gWaylandConnection.fatalContext.clear();
  gWaylandConnection.lastSelectionSerial = 0;
  gWaylandConnection.clipboardText.clear();
}

class WaylandClipboard final : public Clipboard {
public:
  ~WaylandClipboard() override {
    if (shared_) {
      releaseWaylandConnection();
      shared_ = nullptr;
    }
  }

  std::optional<std::string> readText() const override {
    SharedWaylandConnection* shared = ensureShared();
    if (shared && ensureClipboardDataDevice(shared)) {
      primeSelection(shared);
      if (shared->clipboardSource && !shared->clipboardText.empty()) {
        return shared->clipboardText;
      }
      if (shared->selectionOffer) {
        std::optional<std::string> mime = bestClipboardMime(shared->selectionOffer);
        if (!mime) return std::nullopt;
        std::optional<std::string> text = readClipboardOfferText(shared, shared->selectionOffer, *mime);
        if (text && !text->empty()) {
          fallbackText_ = *text;
          fallbackValid_ = true;
          return text;
        }
        return std::nullopt;
      }
    }
    if (fallbackValid_ && !fallbackText_.empty()) {
      return fallbackText_;
    }
    return std::nullopt;
  }

  void writeText(std::string text) override {
    fallbackText_ = text;
    fallbackValid_ = true;

    SharedWaylandConnection* shared = ensureShared();
    if (!shared || !ensureClipboardDataDevice(shared) || shared->lastSelectionSerial == 0) {
      return;
    }

    destroyClipboardSource(shared);
    shared->clipboardText = std::move(text);
    if (shared->clipboardText.empty()) {
      wl_data_device_set_selection(shared->dataDevice, nullptr, shared->lastSelectionSerial);
      flushWaylandDisplay(shared, "clipboard clear selection");
      fallbackValid_ = false;
      return;
    }

    wl_data_source* source = wl_data_device_manager_create_data_source(shared->dataDeviceManager);
    if (!source) {
      return;
    }
    wl_data_source_add_listener(source, &clipboardSourceListener, shared);
    wl_data_source_offer(source, "text/plain;charset=utf-8");
    wl_data_source_offer(source, "text/plain");
    wl_data_device_set_selection(shared->dataDevice, source, shared->lastSelectionSerial);
    shared->clipboardSource = source;
    shared->selectionOffer = nullptr;
    fallbackValid_ = false;
    flushWaylandDisplay(shared, "clipboard set selection");
  }

  bool hasText() const override {
    SharedWaylandConnection* shared = ensureShared();
    if (shared && ensureClipboardDataDevice(shared)) {
      primeSelection(shared);
      if (shared->clipboardSource && !shared->clipboardText.empty()) {
        return true;
      }
      if (bestClipboardMime(shared->selectionOffer)) {
        return true;
      }
    }
    return fallbackValid_ && !fallbackText_.empty();
  }

private:
  SharedWaylandConnection* ensureShared() const {
    if (shared_ && canSendWaylandRequests(shared_)) {
      return shared_;
    }
    if (shared_) {
      releaseWaylandConnection();
      shared_ = nullptr;
      primedSelection_ = false;
    }
    try {
      shared_ = acquireWaylandConnection();
    } catch (...) {
      return nullptr;
    }
    return shared_;
  }

  void primeSelection(SharedWaylandConnection* shared) const {
    if (!shared || primedSelection_ || !canSendWaylandRequests(shared)) {
      return;
    }
    (void)wl_display_roundtrip(shared->display);
    primedSelection_ = true;
  }

  mutable SharedWaylandConnection* shared_ = nullptr;
  mutable bool primedSelection_ = false;
  mutable std::string fallbackText_;
  mutable bool fallbackValid_ = false;
};

} // namespace

std::unique_ptr<Clipboard> createWaylandClipboard() {
  return std::make_unique<WaylandClipboard>();
}

class WaylandWindow final : public platform::Window {
public:
  explicit WaylandWindow(WindowConfig const& config)
      : handle_(gNextHandle.fetch_add(1)), size_(config.size), title_(config.title),
        fullscreen_(config.fullscreen), titlebarMode_(config.titlebar),
        layerShellConfig_(config.layerShell) {
    if (pipe(wakePipe_) != 0) {
      throw std::runtime_error("Failed to create Wayland wake pipe");
    }
    fcntl(wakePipe_[0], F_SETFL, fcntl(wakePipe_[0], F_GETFL, 0) | O_NONBLOCK);
    fcntl(wakePipe_[1], F_SETFL, fcntl(wakePipe_[1], F_GETFL, 0) | O_NONBLOCK);
    SharedWaylandConnection* shared = acquireWaylandConnection();
    shared_ = shared;
    display_ = shared->display;
    surface_ = wl_compositor_create_surface(shared->compositor);
    shared_->windows.push_back(this);
    wl_surface_add_listener(surface_, &surfaceListener_, this);
    wl_surface_set_buffer_scale(surface_, static_cast<std::int32_t>(std::lround(dpiScaleX_)));
    if (shared->viewporter) {
      viewport_ = wp_viewporter_get_viewport(shared->viewporter, surface_);
      updateViewportDestination();
    }
    if (shared->fractionalScaleManager && viewport_) {
      fractionalScale_ = wp_fractional_scale_manager_v1_get_fractional_scale(shared->fractionalScaleManager, surface_);
      wp_fractional_scale_v1_add_listener(fractionalScale_, &fractionalScaleListener_, this);
      wl_surface_set_buffer_scale(surface_, 1);
    }
    appId_ = Application::instance().name();
    if (layerShellConfig_.enabled) {
      configureLayerShellSurface();
    } else {
      xdgSurface_ = xdg_wm_base_get_xdg_surface(shared->wmBase, surface_);
      xdg_surface_add_listener(xdgSurface_, &xdgSurfaceListener_, this);
      toplevel_ = xdg_surface_get_toplevel(xdgSurface_);
      xdg_toplevel_add_listener(toplevel_, &toplevelListener_, this);
      xdg_toplevel_set_title(toplevel_, title_.c_str());
      xdg_toplevel_set_app_id(toplevel_, appId_.c_str());
      setTransientParent(detail::currentWindowCreationTransientParentHandle(),
                         detail::currentWindowCreationModal());
      if (config.minSize.width > 0.f || config.minSize.height > 0.f) setMinSize(config.minSize);
      if (config.maxSize.width > 0.f || config.maxSize.height > 0.f) setMaxSize(config.maxSize);
      configureDecorationProtocol();
      if (fullscreen_) xdg_toplevel_set_fullscreen(toplevel_, nullptr);
    }
    updateBackgroundEffectRegion();
    updateOpaqueRegion();
    wl_surface_commit(surface_);
    surfaceCommitted_ = true;
    while (!configured_) {
      dispatchingWaylandEvents_ = true;
      int const rc = wl_display_dispatch(display_);
      dispatchingWaylandEvents_ = false;
      if (rc < 0) {
        int const error = wl_display_get_error(display_);
        markWaylandConnectionDead(shared_, "initial configure dispatch", error == 0 ? errno : error);
        throw std::runtime_error("Wayland initial configure failed");
      }
      if (!checkWaylandConnection(shared_, "initial configure dispatch")) {
        throw std::runtime_error("Wayland initial configure failed");
      }
    }
  }

  ~WaylandWindow() override {
    popupMenu_.reset();
    popovers_.clear();
    if (shared_) {
      shared_->windows.erase(std::remove(shared_->windows.begin(), shared_->windows.end(), this),
                             shared_->windows.end());
      if (shared_->pointerFocus == this) shared_->pointerFocus = nullptr;
      if (shared_->popupPointerFocus == this) {
        shared_->popupPointerFocus = nullptr;
        shared_->popupPointerSurface = nullptr;
      }
      if (shared_->keyboardFocus == this) {
        shared_->keyboardFocus = nullptr;
        shared_->keyboardSurface = nullptr;
      }
      if (shared_->repeatWindow == this) {
        stopKeyboardRepeat(shared_);
      }
    }
    bool const destroyProxies = canSendWaylandRequests(shared_);
    if (destroyProxies) {
      if (frameCallback_) wl_callback_destroy(frameCallback_);
      if (backgroundEffect_) ext_background_effect_surface_v1_destroy(backgroundEffect_);
      if (layerSurface_) zwlr_layer_surface_v1_destroy(layerSurface_);
      if (cutouts_) xx_cutouts_v1_destroy(cutouts_);
      if (decoration_) zxdg_toplevel_decoration_v1_destroy(decoration_);
      if (toplevel_) xdg_toplevel_destroy(toplevel_);
      if (xdgSurface_) xdg_surface_destroy(xdgSurface_);
      if (fractionalScale_) wp_fractional_scale_v1_destroy(fractionalScale_);
      if (viewport_) wp_viewport_destroy(viewport_);
      if (surface_) wl_surface_destroy(surface_);
    }
    if (shared_) releaseWaylandConnection();
    if (wakePipe_[0] >= 0) close(wakePipe_[0]);
    if (wakePipe_[1] >= 0) close(wakePipe_[1]);
  }

  void setLambdaWindow(::lambda::Window* window) override { lambdaWindow_ = window; }

  void show() override {
    updateCanvasDpi();
    Application::instance().requestWindowRedraw(handle_);
    Application::instance().flushRedraw();
  }

  std::unique_ptr<Canvas> createCanvas(::lambda::Window&) override {
    nativeSurface_ = WaylandNativeSurface{display_, surface_};
    auto* provider = Application::instance().platformApp().gpuSurfaceProvider();
    if (!provider) {
      throw std::runtime_error("Wayland platform application does not provide Vulkan surfaces");
    }
    configureVulkanCanvasRuntime(provider->requiredInstanceExtensions(), Application::instance().cacheDir());
    VkInstance instance = ensureSharedVulkanInstance();
    VkSurfaceKHR surface = provider->createSurface(instance, &nativeSurface_);
    auto canvas = createVulkanCanvas(surface,
                                     handle_,
                                     Application::instance().textSystem(),
                                     {.transparentSurface = wantsTransparentSurface()});
    setVulkanCanvasResizeBoundsHint(canvas.get(), configureBoundsWidth_, configureBoundsHeight_);
    canvas->updateDpiScale(dpiScaleX_, dpiScaleY_);
    canvas->resize(static_cast<int>(std::lround(size_.width)), static_cast<int>(std::lround(size_.height)));
    canvas_ = canvas.get();
    return canvas;
  }

  void resize(Size const& newSize) override {
    size_ = newSize;
    if (layerSurface_) {
      zwlr_layer_surface_v1_set_size(layerSurface_,
                                     layerShellProtocolWidth(size_, layerShellConfig_),
                                     layerShellProtocolHeight(size_, layerShellConfig_));
      updateOpaqueRegion();
      commitSurface();
      if (size_.width <= 0.f || size_.height <= 0.f) {
        return;
      }
    }
    if (lambdaWindow_) lambdaWindow_->updateCanvasDpiScale(dpiScaleX_, dpiScaleY_);
    if (canvas_) {
      canvas_->resize(static_cast<int>(std::max(1, static_cast<int>(std::lround(size_.width)))),
                      static_cast<int>(std::max(1, static_cast<int>(std::lround(size_.height)))));
    }
    updateViewportDestination();
    updateBackgroundEffectRegion();
    updateOpaqueRegion();
    queueResizeEvent();
    applyCursor(currentCursor_);
    requestResizeRedraw();
  }

  void setLayerShellKeyboardInteractive(bool enabled) override {
    if (!layerSurface_) {
      return;
    }
    zwlr_layer_surface_v1_set_keyboard_interactivity(layerSurface_, enabled ? 1u : 0u);
    commitSurface();
  }

  void setLayerShellOptions(LayerShellOptions const& options) override {
    layerShellConfig_ = options;
    if (!layerSurface_) {
      return;
    }
    zwlr_layer_surface_v1_set_anchor(layerSurface_, layerShellAnchor(layerShellConfig_));
    zwlr_layer_surface_v1_set_margin(layerSurface_,
                                     layerShellConfig_.marginTop,
                                     layerShellConfig_.marginRight,
                                     layerShellConfig_.marginBottom,
                                     layerShellConfig_.marginLeft);
    zwlr_layer_surface_v1_set_exclusive_zone(layerSurface_, layerShellConfig_.exclusiveZone);
    zwlr_layer_surface_v1_set_keyboard_interactivity(layerSurface_,
                                                     layerShellConfig_.keyboardInteractive ? 1u : 0u);
    zwlr_layer_surface_v1_set_size(layerSurface_,
                                   layerShellProtocolWidth(size_, layerShellConfig_),
                                   layerShellProtocolHeight(size_, layerShellConfig_));
    updateBackgroundEffectRegion();
    updateOpaqueRegion();
    updateLayerShellInputRegion();
    commitSurface();
  }

  PlatformWindowCapabilities capabilities() const override {
    return {
        .supportsWindowGlass = shared_ && shared_->backgroundEffectManager,
        .supportsLayerShell = true,
        .supportsBackgroundBlur = shared_ && shared_->backgroundEffectManager,
        .supportsOutputSelection = false,
        .supportsDisplayMode = false,
    };
  }

  void setMinSize(Size size) override {
    if (toplevel_) {
      xdg_toplevel_set_min_size(toplevel_, static_cast<int>(std::lround(size.width)),
                                static_cast<int>(std::lround(size.height)));
    }
  }

  void setMaxSize(Size size) override {
    if (toplevel_) {
      xdg_toplevel_set_max_size(toplevel_, static_cast<int>(std::lround(size.width)),
                                static_cast<int>(std::lround(size.height)));
    }
  }

  void setFullscreen(bool fullscreen) override {
    fullscreen_ = fullscreen;
    if (!toplevel_) return;
    if (fullscreen_) xdg_toplevel_set_fullscreen(toplevel_, nullptr);
    else xdg_toplevel_unset_fullscreen(toplevel_);
  }

  void setCursor(Cursor kind) override {
    currentCursor_ = kind == Cursor::Inherit ? Cursor::Arrow : kind;
    applyCursor(currentCursor_);
  }

  void setTitle(std::string const& title) override {
    title_ = title;
    if (toplevel_) xdg_toplevel_set_title(toplevel_, title_.c_str());
  }

  void setTransientParent(unsigned int parentHandle, bool modal) override {
    (void)modal;
    if (!toplevel_ || !shared_) {
      return;
    }

    xdg_toplevel* parentToplevel = nullptr;
    bool shouldSetParent = parentHandle == 0;
    if (parentHandle != 0) {
      for (WaylandWindow* window : shared_->windows) {
        if (window && window != this && window->handle_ == parentHandle && window->toplevel_) {
          parentToplevel = window->toplevel_;
          shouldSetParent = true;
          break;
        }
      }
    }
    if (!shouldSetParent) {
      return;
    }

    xdg_toplevel_set_parent(toplevel_, parentToplevel);
    if (surfaceCommitted_) {
      commitSurface();
      flushWaylandDisplay(shared_, "transient parent update");
    }
  }

  void setTitlebarMode(WindowTitlebarMode mode) override {
    if (titlebarMode_ == mode) {
      return;
    }
    titlebarMode_ = mode;
    configureDecorationProtocol();
    if (surface_) wl_surface_commit(surface_);
    flushWaylandDisplay(shared_, "titlebar mode update");
  }

  WindowTitlebarMode titlebarMode() const override { return titlebarMode_; }

  void setBackground(WindowBackground const& background) override {
    background_ = background;
    if (canvas_) {
      setVulkanCanvasTransparentSurface(canvas_, wantsTransparentSurface());
    }
    updateBackgroundEffectRegion();
    updateOpaqueRegion();
    commitSurface();
  }

  WindowChromeMetrics chromeMetrics() const override {
    WindowChromeMetrics metrics{};
    metrics.titlebarMode = titlebarMode_;
    metrics.active = true;
    if (titlebarMode_ == WindowTitlebarMode::System) {
      return metrics;
    }

    metrics.titlebarHeight = kClientTitlebarHeight;
    if (titlebarMode_ == WindowTitlebarMode::Integrated && serverSideDecorationsActive_) {
      metrics.systemControlsVisible = true;
      if (receivedCutout_ && lastCutoutWidth_ > 0 && lastCutoutHeight_ > 0) {
        metrics.reservedRegions.push_back(Rect::sharp(static_cast<float>(lastCutoutX_),
                                                       static_cast<float>(lastCutoutY_),
                                                       static_cast<float>(lastCutoutWidth_),
                                                       static_cast<float>(lastCutoutHeight_)));
      } else {
        metrics.reservedRegions.push_back(Rect::sharp(std::max(0.f, size_.width - kCompositorControlReserveWidth),
                                                       0.f,
                                                       std::min(kCompositorControlReserveWidth, size_.width),
                                                       kClientTitlebarHeight));
      }
    }
    return metrics;
  }

  void beginWindowDrag(std::uint32_t platformSerial = 0) override {
    std::uint32_t const serial = platformSerial != 0 ? platformSerial : lastPointerButtonSerial_;
    if (!shared_ || !shared_->seat || !toplevel_ || serial == 0) {
      return;
    }
    xdg_toplevel_move(toplevel_, shared_->seat, serial);
    flushWaylandDisplay(shared_, "window move request");
  }

  void beginWindowResize(WindowResizeEdge edge, std::uint32_t platformSerial = 0) override {
    std::uint32_t const xdgEdge = xdgResizeEdge(edge);
    std::uint32_t const serial = platformSerial != 0 ? platformSerial : lastPointerButtonSerial_;
    if (!shared_ || !shared_->seat || !toplevel_ || serial == 0 ||
        xdgEdge == XDG_TOPLEVEL_RESIZE_EDGE_NONE) {
      return;
    }
    xdg_toplevel_resize(toplevel_, shared_->seat, serial, xdgEdge);
    flushWaylandDisplay(shared_, "window resize request");
  }

  bool showPopupMenu(PopupMenu menu, Rect anchor, std::uint32_t platformSerial = 0) override {
    if (!shared_ || !shared_->compositor || !shared_->shm || !shared_->wmBase || !surface_ ||
        menu.items.empty() || !canSendWaylandRequests(shared_)) {
      return false;
    }
    if (!xdgSurface_ && !layerSurface_) {
      return false;
    }

    popupMenu_.reset();

    auto state = std::make_unique<WaylandPopupMenuState>();
    state->menu = std::move(menu);
    state->owner = this;
    state->shared = shared_;
    state->grabSerial = platformSerial != 0 ? platformSerial : lastPointerButtonSerial_;
    state->rowHeight = 30;
    state->height = std::max(1, static_cast<int>(state->menu.items.size()) * state->rowHeight);

    int measuredWidth = 180;
    Font font{.size = 13.f, .weight = 450.f};
    TextLayoutOptions options;
    options.wrapping = TextWrapping::NoWrap;
    options.maxLines = 1;
    for (MenuItem const& item : state->menu.items) {
      if (item.role == MenuRole::Separator) {
        continue;
      }
      Size const measured = Application::instance().textSystem().measure(item.label, font, Colors::black, 0.f,
                                                                         options);
      measuredWidth = std::max(measuredWidth, static_cast<int>(std::ceil(measured.width)) + 68);
    }
    state->width = std::clamp(measuredWidth, 140, 320);

    state->surface = wl_compositor_create_surface(shared_->compositor);
    state->xdgSurface = xdg_wm_base_get_xdg_surface(shared_->wmBase, state->surface);
    xdg_surface_add_listener(state->xdgSurface, &popupXdgSurfaceListener_, state.get());

    xdg_positioner* positioner = xdg_wm_base_create_positioner(shared_->wmBase);
    xdg_positioner_set_size(positioner, state->width, state->height);
    xdg_positioner_set_anchor_rect(positioner,
                                   static_cast<std::int32_t>(std::lround(anchor.x)),
                                   static_cast<std::int32_t>(std::lround(anchor.y)),
                                   std::max(1, static_cast<int>(std::lround(anchor.width))),
                                   std::max(1, static_cast<int>(std::lround(anchor.height))));
    xdg_positioner_set_anchor(positioner, XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT);
    xdg_positioner_set_gravity(positioner, XDG_POSITIONER_GRAVITY_BOTTOM_LEFT);
    xdg_positioner_set_offset(positioner, 0, 4);
    state->popup = xdg_surface_get_popup(state->xdgSurface, xdgSurface_, positioner);
    xdg_popup_add_listener(state->popup, &popupListener_, state.get());
    if (layerSurface_) {
      zwlr_layer_surface_v1_get_popup(layerSurface_, state->popup);
    }
    xdg_positioner_destroy(positioner);

    state->buffer = createWaylandMenuBuffer(shared_->shm, state->width, state->height);
    wl_surface_commit(state->surface);
    flushWaylandDisplay(shared_, "popup menu show");
    popupMenu_ = std::move(state);
    return true;
  }

  PopoverSurfaceId showPopover(Popover popover, Rect anchor, std::uint32_t platformSerial = 0) override {
    if (!shared_ || !shared_->compositor || !shared_->wmBase || !surface_ || !lambdaWindow_ ||
        !canSendWaylandRequests(shared_)) {
      return kInvalidPopoverSurfaceId;
    }
    if (!xdgSurface_ && !layerSurface_) {
      return kInvalidPopoverSurfaceId;
    }

    Theme const theme = lambdaWindow_->theme();
    popover.gap = resolveFloat(popover.gap, theme.space2);
    Size const maxSize = popover.maxSize.value_or(Size{std::max(1.f, size_.width - 24.f),
                                                       std::max(1.f, size_.height - 24.f)});
    PopoverSurfaceId const id{nextPopoverId_++};
    auto state = std::make_unique<WaylandPopoverSurfaceState>();
    state->id = id;
    state->popover = popover;
    state->owner = this;
    state->shared = shared_;
    state->grabSerial = popover.dismissOnOutsideTap
                             ? (platformSerial != 0 ? platformSerial : lastPointerButtonSerial_)
                             : 0;
    state->host = std::make_unique<TransientPopoverHost>(TransientPopoverHost::Config{
        .popover = popover,
        .environment = lambdaWindow_->environmentBinding(),
        .maxSize = maxSize,
        .useNativeShell = false,
        .requestRedraw = [this, id] {
          if (WaylandPopoverSurfaceState* popoverState = popoverForId(id)) {
            requestPopoverRedraw(*popoverState);
          }
        },
        .requestDismiss = [this, id] {
          dismissPopover(id);
        },
    });
    Size const measured = state->host->measuredSize();
    state->width = std::max(1, static_cast<int>(std::lround(measured.width)));
    state->height = std::max(1, static_cast<int>(std::lround(measured.height)));

    state->surface = wl_compositor_create_surface(shared_->compositor);
    state->xdgSurface = xdg_wm_base_get_xdg_surface(shared_->wmBase, state->surface);
    xdg_surface_add_listener(state->xdgSurface, &popoverXdgSurfaceListener_, state.get());

    xdg_positioner* positioner = xdg_wm_base_create_positioner(shared_->wmBase);
    xdg_positioner_set_size(positioner, state->width, state->height);
    xdg_positioner_set_anchor_rect(positioner,
                                   static_cast<std::int32_t>(std::lround(anchor.x)),
                                   static_cast<std::int32_t>(std::lround(anchor.y)),
                                   std::max(1, static_cast<int>(std::lround(anchor.width))),
                                   std::max(1, static_cast<int>(std::lround(anchor.height))));
    configurePopoverPositioner(positioner, popover, anchor);
    state->popup = xdg_surface_get_popup(state->xdgSurface, xdgSurface_, positioner);
    xdg_popup_add_listener(state->popup, &popoverListener_, state.get());
    if (layerSurface_) {
      zwlr_layer_surface_v1_get_popup(layerSurface_, state->popup);
    }
    xdg_positioner_destroy(positioner);

    wl_surface_commit(state->surface);
    tracePopover(*state, "show");
    flushWaylandDisplay(shared_, "popover show");
    popovers_.push_back(std::move(state));
    return id;
  }

  void dismissPopover(PopoverSurfaceId id) override {
    if (!id.isValid()) {
      return;
    }
    if (WaylandPopoverSurfaceState* state = popoverForId(id)) {
      dismissPopoverState(state);
    }
  }

  void repositionPopover(PopoverSurfaceId id, Popover const& popover, Rect anchor) override {
    WaylandPopoverSurfaceState* state = popoverForId(id);
    if (!state || !state->popup || !state->committed || !shared_ || !shared_->wmBase) {
      return;
    }
    state->popover.resolvedPlacement = popover.resolvedPlacement;
    xdg_positioner* positioner = xdg_wm_base_create_positioner(shared_->wmBase);
    xdg_positioner_set_size(positioner, state->width, state->height);
    xdg_positioner_set_anchor_rect(positioner,
                                   static_cast<std::int32_t>(std::lround(anchor.x)),
                                   static_cast<std::int32_t>(std::lround(anchor.y)),
                                   std::max(1, static_cast<int>(std::lround(anchor.width))),
                                   std::max(1, static_cast<int>(std::lround(anchor.height))));
    configurePopoverPositioner(positioner, state->popover, anchor);
    if (xdg_popup_get_version(state->popup) >= XDG_POPUP_REPOSITION_SINCE_VERSION) {
      xdg_popup_reposition(state->popup, positioner, state->repositionToken++);
    }
    xdg_positioner_destroy(positioner);
    flushWaylandDisplay(shared_, "popover reposition");
  }

  Size currentSize() const override { return size_; }
  bool isFullscreen() const override { return fullscreen_; }
  unsigned int handle() const override { return handle_; }
  void* nativeGraphicsSurface() const override { return const_cast<WaylandNativeSurface*>(&nativeSurface_); }

  void processEvents() override {
    drainWakePipe();
    dispatchReadyEvents(0);
    if (shared_ && shared_->fatalError) return;
    flushDeferredRedraw();
  }

  void waitForEvents(int timeoutMs) override {
    dispatchReadyEvents(timeoutMs);
    if (shared_ && shared_->fatalError) return;
    flushDeferredRedraw();
  }

  void wakeEventLoop() override {
    char const c = 1;
    (void)write(wakePipe_[1], &c, 1);
  }
  int eventFd() const override {
    return display_ && shared_ && !shared_->fatalError ? wl_display_get_fd(display_) : -1;
  }
  int wakeFd() const override { return wakePipe_[0]; }

  void requestAnimationFrame() override {
    if (framePending_ || !surface_ || !canSendWaylandRequests(shared_)) return;
    if (resizeRedrawPending_ || pendingResizeEvent_) {
      if (detail::resizeTraceEnabled()) {
        LAMBDA_RESIZE_TRACE("wayland-window", "request-frame-batched-resize window=%u size=%dx%d\n",
                     handle_, static_cast<int>(std::lround(size_.width)),
                     static_cast<int>(std::lround(size_.height)));
      }
      return;
    }
    framePending_ = true;
    if (detail::resizeTraceEnabled()) {
      LAMBDA_RESIZE_TRACE("wayland-window", "request-frame window=%u mode=%s size=%dx%d\n",
                   handle_, vulkanCanvasUsesMailboxPresentMode(canvas_) ? "mailbox" : "fifo",
                   static_cast<int>(std::lround(size_.width)),
                   static_cast<int>(std::lround(size_.height)));
    }
    frameCallback_ = wl_surface_frame(surface_);
    wl_callback_add_listener(frameCallback_, &frameCallbackListener_, this);
    wl_surface_commit(surface_);
    flushWaylandDisplay(shared_, "animation frame request");
  }

  void acknowledgeAnimationFrameTick() override {
  }

  void completeAnimationFrame(bool needsAnotherFrame) override {
    if (detail::resizeTraceEnabled()) {
      LAMBDA_RESIZE_TRACE("wayland-window", "complete-frame window=%u needsAnother=%d\n",
                   handle_, needsAnotherFrame ? 1 : 0);
    }
    flushWaylandDisplay(shared_, "animation frame complete");
    framePending_ = false;
    if (needsAnotherFrame) requestAnimationFrame();
  }

  wl_surface* waylandSurface() const noexcept { return surface_; }
  bool ownsPopupSurface(wl_surface* surface) const noexcept {
    if (popupMenu_ && popupMenu_->surface == surface) {
      return true;
    }
    return popoverForSurface(surface) != nullptr;
  }

  void handlePopupPointerEnter(std::uint32_t, wl_fixed_t x, wl_fixed_t y) {
    flushPendingPopupScroll();
    wl_surface* surface = shared_ ? shared_->popupPointerSurface : nullptr;
    if (popupMenu_ && popupMenu_->surface == surface) {
      updatePopupHover(popupMenuRowForY(*popupMenu_, y));
      return;
    }
    if (WaylandPopoverSurfaceState* popover = popoverForSurface(surface)) {
      popover->pointerPos = logicalPointFromFixed(x, y, 1.f, 1.f);
      dispatchPopoverPointerMove(*popover, popover->pointerPos);
      return;
    }
  }

  void handlePopupPointerLeave() {
    flushPendingPopupScroll();
    wl_surface* surface = shared_ ? shared_->popupPointerSurface : nullptr;
    if (popupMenu_ && popupMenu_->surface == surface) {
      popupMenu_->pressedRow = -1;
      updatePopupHover(-1);
      return;
    }
  }

  void handlePopupPointerMotion(wl_fixed_t x, wl_fixed_t y) {
    flushPendingPopupScroll();
    wl_surface* surface = shared_ ? shared_->popupPointerSurface : nullptr;
    if (popupMenu_ && popupMenu_->surface == surface) {
      updatePopupHover(popupMenuRowForY(*popupMenu_, y));
      return;
    }
    if (WaylandPopoverSurfaceState* popover = popoverForSurface(surface)) {
      popover->pointerPos = logicalPointFromFixed(x, y, 1.f, 1.f);
      dispatchPopoverPointerMove(*popover, popover->pointerPos);
    }
  }

  void handlePopupPointerButton(std::uint32_t, std::uint32_t button, std::uint32_t state) {
    flushPendingPopupScroll();
    wl_surface* surface = shared_ ? shared_->popupPointerSurface : nullptr;
    if (WaylandPopoverSurfaceState* popover = popoverForSurface(surface)) {
      dispatchPopoverPointerButton(*popover, button, state);
      return;
    }
    if (!popupMenu_ || popupMenu_->surface != surface || button != BTN_LEFT) {
      return;
    }
    int const row = popupMenu_->hoverRow;
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
      popupMenu_->pressedRow = row;
      return;
    }
    int const pressedRow = popupMenu_->pressedRow;
    popupMenu_->pressedRow = -1;
    if (row >= 0 && row == pressedRow) {
      activatePopupMenuRow(row);
    }
  }

  void handlePopupPointerAxis(std::uint32_t axis, wl_fixed_t value) {
    wl_surface* surface = shared_ ? shared_->popupPointerSurface : nullptr;
    WaylandPopoverSurfaceState* popover = popoverForSurface(surface);
    if (!popover || !popover->host) {
      return;
    }
    float const v = static_cast<float>(wl_fixed_to_double(value));
    popover->pendingScroll.addAxis(axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL, v);
  }

  void handlePopupPointerFrame() {
    flushPendingPopupScroll();
  }

  void flushPendingPopupScroll() {
    wl_surface* surface = shared_ ? shared_->popupPointerSurface : nullptr;
    WaylandPopoverSurfaceState* popover = popoverForSurface(surface);
    if (!popover) {
      return;
    }
    std::optional<Vec2> delta = popover->pendingScroll.take();
    if (!delta) {
      return;
    }
    dispatchPopoverScroll(*popover, *delta);
  }

  void handlePointerEnter(std::uint32_t serial, wl_fixed_t x, wl_fixed_t y) {
    flushPendingPointerScroll();
    pointerEnterSerial_ = serial;
    pointerPos_ = logicalPointFromFixed(x, y, dpiScaleX_, dpiScaleY_);
    applyCursor(currentCursor_);
    Application::instance().eventQueue().post(InputEvent{.kind = InputEvent::Kind::PointerEnter,
                                                         .handle = handle_,
                                                         .position = pointerPos_,
                                                         .pressedButtons = pressedButtons_,
                                                         .platformSerial = serial});
  }

  void handlePointerLeave() {
    flushPendingPointerScroll();
    pressedButtons_ = 0;
    lastPointerButtonSerial_ = 0;
    Application::instance().eventQueue().post(InputEvent{.kind = InputEvent::Kind::PointerLeave,
                                                         .handle = handle_,
                                                         .position = pointerPos_,
                                                         .pressedButtons = pressedButtons_});
  }

  void handlePointerMotion(wl_fixed_t x, wl_fixed_t y) {
    flushPendingPointerScroll();
    pointerPos_ = logicalPointFromFixed(x, y, dpiScaleX_, dpiScaleY_);
    Application::instance().eventQueue().post(InputEvent{.kind = InputEvent::Kind::PointerMove,
                                                         .handle = handle_,
                                                         .position = pointerPos_,
                                                         .pressedButtons = pressedButtons_});
  }

  void handlePointerButton(std::uint32_t serial, std::uint32_t button, std::uint32_t state) {
    flushPendingPointerScroll();
    std::uint8_t const bit = button == BTN_LEFT ? 1u : button == BTN_RIGHT ? 2u : button == BTN_MIDDLE ? 4u : 0u;
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) pressedButtons_ |= bit;
    else pressedButtons_ &= static_cast<std::uint8_t>(~bit);
    if (button == BTN_LEFT) {
      lastPointerButtonSerial_ = state == WL_POINTER_BUTTON_STATE_PRESSED ? serial : 0u;
    }
    Application::instance().eventQueue().post(InputEvent{.kind = state == WL_POINTER_BUTTON_STATE_PRESSED
                                                                     ? InputEvent::Kind::PointerDown
                                                                     : InputEvent::Kind::PointerUp,
                                                         .handle = handle_,
                                                         .position = pointerPos_,
                                                         .button = mouseButtonFromLinux(button),
                                                         .pressedButtons = pressedButtons_,
                                                         .platformSerial = serial});
  }

  void handlePointerAxis(std::uint32_t axis, wl_fixed_t value) {
    float const v = static_cast<float>(wl_fixed_to_double(value));
    pendingScroll_.addAxis(axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL, v);
  }

  void handlePointerFrame() {
    flushPendingPointerScroll();
  }

  void flushPendingPointerScroll() {
    std::optional<Vec2> delta = pendingScroll_.take();
    if (!delta) {
      return;
    }
    Application::instance().eventQueue().post(InputEvent{.kind = InputEvent::Kind::Scroll,
                                                         .handle = handle_,
                                                         .position = pointerPos_,
                                                         .scrollDelta = *delta,
                                                         .preciseScrollDelta = true,
                                                         .pressedButtons = pressedButtons_});
  }

  void handleKeyboardKey(linux_platform::XkbState* xkb, std::uint32_t key, std::uint32_t state,
                         std::uint32_t serial) {
    bool const pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;
    KeyCode const keyCode = xkb ? xkb->keyCodeForEvdevKey(key) : keys::Unknown;
    if (pressed && keyCode == keys::Escape && popupMenu_) {
      dismissPopupMenu();
      return;
    }
    Application::instance().eventQueue().post(InputEvent{.kind = pressed ? InputEvent::Kind::KeyDown
                                                                          : InputEvent::Kind::KeyUp,
                                                         .handle = handle_,
                                                         .key = keyCode,
                                                         .modifiers = currentModifiers_,
                                                         .platformSerial = serial});
    if (pressed && linux_platform::shouldEmitTextInputForModifiers(currentModifiers_)) {
      std::string text = xkb ? xkb->utf8ForEvdevKey(key) : std::string{};
      if (!text.empty()) {
        Application::instance().eventQueue().post(InputEvent{.kind = InputEvent::Kind::TextInput,
                                                             .handle = handle_,
                                                             .text = std::move(text)});
      }
    }
  }

  void handleKeyboardKeyForSurface(wl_surface* surface, linux_platform::XkbState* xkb,
                                   std::uint32_t key, std::uint32_t state, std::uint32_t serial) {
    if (WaylandPopoverSurfaceState* popover = popoverForSurface(surface)) {
      bool const pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;
      KeyCode const keyCode = xkb ? xkb->keyCodeForEvdevKey(key) : keys::Unknown;
      dispatchPopoverKey(*popover, keyCode, pressed);
      if (pressed && linux_platform::shouldEmitTextInputForModifiers(currentModifiers_)) {
        std::string text = xkb ? xkb->utf8ForEvdevKey(key) : std::string{};
        if (!text.empty()) {
          dispatchPopoverText(*popover, std::move(text));
        }
      }
      return;
    }
    handleKeyboardKey(xkb, key, state, serial);
  }

  void handleKeyboardModifiers(linux_platform::XkbState* xkb, std::uint32_t depressed,
                               std::uint32_t latched, std::uint32_t locked, std::uint32_t group) {
    if (!xkb) return;
    xkb->updateModifiers(depressed, latched, locked, group);
    currentModifiers_ = xkb->modifiers();
  }

  void handleOutputRemoved(wl_output* output) {
    enteredOutputs_.erase(std::remove(enteredOutputs_.begin(), enteredOutputs_.end(), output),
                          enteredOutputs_.end());
    updateEnteredScale();
  }

  void handleOutputScaleChanged(wl_output* output) {
    if (std::find(enteredOutputs_.begin(), enteredOutputs_.end(), output) != enteredOutputs_.end()) {
      updateEnteredScale();
    }
  }

private:
  void dismissPopupMenu(WaylandPopupMenuState* state = nullptr) {
    if (state && popupMenu_.get() != state) {
      return;
    }
    if (shared_ && shared_->popupPointerFocus == this) {
      shared_->popupPointerFocus = nullptr;
      if (popupMenu_ && shared_->popupPointerSurface == popupMenu_->surface) {
        shared_->popupPointerSurface = nullptr;
      }
    }
    popupMenu_.reset();
    flushWaylandDisplay(shared_, "popup menu dismiss");
  }

  WaylandPopoverSurfaceState* popoverForId(PopoverSurfaceId id) noexcept {
    auto it = std::find_if(popovers_.begin(), popovers_.end(),
                           [&](std::unique_ptr<WaylandPopoverSurfaceState> const& state) {
                             return state && state->id == id;
                           });
    return it == popovers_.end() ? nullptr : it->get();
  }

  WaylandPopoverSurfaceState* popoverForSurface(wl_surface* surface) noexcept {
    auto it = std::find_if(popovers_.begin(), popovers_.end(),
                           [&](std::unique_ptr<WaylandPopoverSurfaceState> const& state) {
                             return state && state->surface == surface;
                           });
    return it == popovers_.end() ? nullptr : it->get();
  }

  WaylandPopoverSurfaceState const* popoverForSurface(wl_surface* surface) const noexcept {
    auto it = std::find_if(popovers_.begin(), popovers_.end(),
                           [&](std::unique_ptr<WaylandPopoverSurfaceState> const& state) {
                             return state && state->surface == surface;
                           });
    return it == popovers_.end() ? nullptr : it->get();
  }

  void dismissPopoverState(WaylandPopoverSurfaceState* state) {
    if (!state) {
      return;
    }
    if (state->dispatchDepth > 0) {
      state->closeAfterEvent = true;
      return;
    }
    if (shared_ && shared_->popupPointerSurface == state->surface) {
      shared_->popupPointerSurface = nullptr;
      if (shared_->popupPointerFocus == this) {
        shared_->popupPointerFocus = nullptr;
      }
    }
    if (shared_ && shared_->keyboardSurface == state->surface) {
      shared_->keyboardSurface = nullptr;
      if (shared_->keyboardFocus == this) {
        shared_->keyboardFocus = nullptr;
      }
    }
    auto it = std::find_if(popovers_.begin(), popovers_.end(),
                           [&](std::unique_ptr<WaylandPopoverSurfaceState> const& candidate) {
                             return candidate.get() == state;
                           });
    if (it != popovers_.end()) {
      popovers_.erase(it);
    }
    flushWaylandDisplay(shared_, "popover dismiss");
  }

  void finishPopoverEvent(WaylandPopoverSurfaceState& state) {
    if (state.dispatchDepth > 0) {
      --state.dispatchDepth;
    }
    if (state.dispatchDepth == 0 && state.closeAfterEvent) {
      dismissPopoverState(&state);
    }
  }

  bool ensurePopoverCanvas(WaylandPopoverSurfaceState& state) {
    if (state.canvas) {
      return true;
    }
    if (!state.surface || !state.shared || !state.shared->display) {
      return false;
    }
    try {
      auto* provider = Application::instance().platformApp().gpuSurfaceProvider();
      if (!provider) {
        throw std::runtime_error("Wayland platform application does not provide Vulkan surfaces");
      }
      configureVulkanCanvasRuntime(provider->requiredInstanceExtensions(), Application::instance().cacheDir());
      state.nativeSurface = WaylandNativeSurface{state.shared->display, state.surface};
      VkInstance instance = ensureSharedVulkanInstance();
      VkSurfaceKHR vkSurface = provider->createSurface(instance, &state.nativeSurface);
      state.canvas = createVulkanCanvas(vkSurface,
                                        handle_,
                                        Application::instance().textSystem(),
                                        {.transparentSurface = true});
      state.canvas->updateDpiScale(dpiScaleX_, dpiScaleY_);
      state.canvas->resize(state.width, state.height);
      return true;
    } catch (std::exception const& e) {
      std::fprintf(stderr, "Lambda Wayland popover render error: %s\n", e.what());
      return false;
    }
  }

  void renderPopover(WaylandPopoverSurfaceState& state, char const* reason = "redraw") {
    if (state.closing || !state.host || !ensurePopoverCanvas(state)) {
      return;
    }
    bool const committedBefore = state.committed;
    bool const frameCallbackBefore = state.frameCallback != nullptr;
    state.redrawRequested = false;
    state.rendering = true;
    try {
      state.canvas->resize(state.width, state.height);
      state.canvas->beginFrame();
      state.host->render(*state.canvas);
      state.canvas->present();
      state.rendering = false;
    } catch (std::exception const& e) {
      state.rendering = false;
      std::fprintf(stderr, "Lambda Wayland popover render error: %s\n", e.what());
      return;
    } catch (...) {
      state.rendering = false;
      std::fprintf(stderr, "Lambda Wayland popover render error: unknown exception\n");
      return;
    }
    flushWaylandDisplay(shared_, "popover render");
    ++state.renderCount;
    tracePopover(state,
                 "render",
                 reason ? reason : (committedBefore ? "committed" : "initial"));
    if (waylandPopoverTraceEnabled()) {
      std::fprintf(stderr,
                   "%.3fms wayland-popover-detail: event=render id=%" PRIu64
                   " reason=%s committed_before=%d frame_callback_before=%d\n",
                   static_cast<double>(nowNanos()) / 1'000'000.0,
                   state.id.value,
                   reason ? reason : "",
                   committedBefore ? 1 : 0,
                   frameCallbackBefore ? 1 : 0);
    }
    if (state.redrawRequested && state.committed) {
      requestPopoverFrame(state);
    }
    maybeDispatchPopoverAutotestEscape(state);
  }

  void requestPopoverFrame(WaylandPopoverSurfaceState& state) {
    if (state.closing || state.rendering || state.frameCallback || !state.committed || !state.surface ||
        !canSendWaylandRequests(shared_)) {
      return;
    }
    state.frameCallback = wl_surface_frame(state.surface);
    wl_callback_add_listener(state.frameCallback, &popoverFrameCallbackListener_, &state);
    wl_surface_damage_buffer(state.surface, 0, 0, 1, 1);
    wl_surface_commit(state.surface);
    ++state.frameRequestCount;
    tracePopover(state, "frame-request");
    flushWaylandDisplay(shared_, "popover frame request");
  }

  void requestPopoverRedraw(WaylandPopoverSurfaceState& state) {
    if (state.closing || !state.host) {
      return;
    }
    state.redrawRequested = true;
    ++state.redrawRequestCount;
    tracePopover(state, "redraw-request");
    if (state.committed) requestPopoverFrame(state);
  }

  void maybeDispatchPopoverAutotestEscape(WaylandPopoverSurfaceState& state) {
    if (!waylandPopoverAutotestEscapeEnabled() || state.autotestEscapeDispatched ||
        state.popover.debugName != "popover-autotest" || state.frameDoneCount < 8 || state.closing ||
        state.closeAfterEvent) {
      return;
    }
    state.autotestEscapeDispatched = true;
    std::fprintf(stderr,
                 "[wayland-popover-autotest] backend-escape id=%" PRIu64
                 " frameDones=%" PRIu64 "\n",
                 state.id.value,
                 state.frameDoneCount);
    dispatchPopoverKey(state, keys::Escape, true);
  }

  void dispatchPopoverPointerMove(WaylandPopoverSurfaceState& state, Point point) {
    if (!state.host) {
      return;
    }
    ++state.dispatchDepth;
    state.host->pointerMove(point);
    bool const shouldRender = !state.closeAfterEvent;
    if (shouldRender) {
      requestPopoverRedraw(state);
    }
    finishPopoverEvent(state);
  }

  void dispatchPopoverPointerButton(WaylandPopoverSurfaceState& state, std::uint32_t button,
                                    std::uint32_t buttonState) {
    if (!state.host) {
      return;
    }
    ++state.dispatchDepth;
    MouseButton const mouseButton = mouseButtonFromLinux(button);
    if (buttonState == WL_POINTER_BUTTON_STATE_PRESSED) {
      state.host->pointerDown(state.pointerPos, mouseButton, currentModifiers_);
    } else {
      state.host->pointerUp(state.pointerPos, mouseButton, currentModifiers_);
    }
    bool const shouldRender = !state.closeAfterEvent;
    if (shouldRender) {
      requestPopoverRedraw(state);
    }
    finishPopoverEvent(state);
  }

  void dispatchPopoverScroll(WaylandPopoverSurfaceState& state, Vec2 delta) {
    if (!state.host) {
      return;
    }
    ++state.dispatchDepth;
    state.host->scroll(state.pointerPos, delta);
    bool const shouldRender = !state.closeAfterEvent;
    if (shouldRender) {
      requestPopoverRedraw(state);
    }
    finishPopoverEvent(state);
  }

  void dispatchPopoverKey(WaylandPopoverSurfaceState& state, KeyCode key, bool pressed) {
    if (!state.host) {
      return;
    }
    ++state.dispatchDepth;
    if (pressed) {
      state.host->keyDown(key, currentModifiers_);
    } else {
      state.host->keyUp(key, currentModifiers_);
    }
    bool const shouldRender = !state.closeAfterEvent;
    if (shouldRender) {
      requestPopoverRedraw(state);
    }
    finishPopoverEvent(state);
  }

  void dispatchPopoverText(WaylandPopoverSurfaceState& state, std::string text) {
    if (!state.host || text.empty()) {
      return;
    }
    ++state.dispatchDepth;
    state.host->textInput(text);
    bool const shouldRender = !state.closeAfterEvent;
    if (shouldRender) {
      requestPopoverRedraw(state);
    }
    finishPopoverEvent(state);
  }

  static void configurePopoverPositioner(xdg_positioner* positioner, Popover const& popover, Rect const& anchor) {
    (void)anchor;
    int offsetX = 0;
    int offsetY = 0;
    int const gap = std::max(0, static_cast<int>(std::lround(popover.gap)));
    switch (popover.resolvedPlacement) {
    case PopoverPlacement::Above:
      xdg_positioner_set_anchor(positioner, XDG_POSITIONER_ANCHOR_TOP);
      xdg_positioner_set_gravity(positioner, XDG_POSITIONER_GRAVITY_TOP);
      offsetY = -gap;
      break;
    case PopoverPlacement::Below:
      xdg_positioner_set_anchor(positioner, XDG_POSITIONER_ANCHOR_BOTTOM);
      xdg_positioner_set_gravity(positioner, XDG_POSITIONER_GRAVITY_BOTTOM);
      offsetY = gap;
      break;
    case PopoverPlacement::Start:
      xdg_positioner_set_anchor(positioner, XDG_POSITIONER_ANCHOR_LEFT);
      xdg_positioner_set_gravity(positioner, XDG_POSITIONER_GRAVITY_LEFT);
      offsetX = -gap;
      break;
    case PopoverPlacement::End:
      xdg_positioner_set_anchor(positioner, XDG_POSITIONER_ANCHOR_RIGHT);
      xdg_positioner_set_gravity(positioner, XDG_POSITIONER_GRAVITY_RIGHT);
      offsetX = gap;
      break;
    }
    xdg_positioner_set_offset(positioner, offsetX, offsetY);
    xdg_positioner_set_constraint_adjustment(positioner,
                                             XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X |
                                                 XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y |
                                                 XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X |
                                                 XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y);
  }

  void updatePopupHover(int row) {
    if (!popupMenu_) {
      return;
    }
    if (row >= 0) {
      MenuItem const& item = popupMenu_->menu.items[static_cast<std::size_t>(row)];
      if (!popupMenuItemActivatable(item)) {
        row = -1;
      }
    }
    if (popupMenu_->hoverRow == row) {
      return;
    }
    popupMenu_->hoverRow = row;
    renderPopupMenu(*popupMenu_);
    commitPopupMenuBuffer(*popupMenu_);
    flushWaylandDisplay(shared_, "popup menu hover");
  }

  void activatePopupMenuRow(int row) {
    if (!popupMenu_ || row < 0 || row >= static_cast<int>(popupMenu_->menu.items.size())) {
      return;
    }
    MenuItem item = popupMenu_->menu.items[static_cast<std::size_t>(row)];
    if (!popupMenuItemActivatable(item)) {
      return;
    }
    dismissPopupMenu();
    if (item.handler) {
      item.handler();
    } else if (lambdaWindow_ && !item.actionName.empty()) {
      lambdaWindow_->dispatchAction(item.actionName);
    }
  }

  static void popupXdgSurfaceConfigure(void* data, xdg_surface* surface, std::uint32_t serial) {
    auto* state = static_cast<WaylandPopupMenuState*>(data);
    if (!state) {
      return;
    }
    xdg_surface_ack_configure(surface, serial);
    if (!state->committed) {
      renderPopupMenu(*state);
      commitPopupMenuBuffer(*state);
      state->committed = true;
      if (!state->grabbed && state->popup && state->shared && state->shared->seat && state->grabSerial != 0) {
        xdg_popup_grab(state->popup, state->shared->seat, state->grabSerial);
        state->grabbed = true;
      }
      flushWaylandDisplay(state->shared, "popup configure");
    }
  }

  static void popupConfigure(void*, xdg_popup*, std::int32_t, std::int32_t, std::int32_t, std::int32_t) {}

  static void popupDone(void* data, xdg_popup*) {
    auto* state = static_cast<WaylandPopupMenuState*>(data);
    if (state && state->owner) {
      state->owner->dismissPopupMenu(state);
    }
  }

  static void popupRepositioned(void*, xdg_popup*, std::uint32_t) {}

  static void popoverXdgSurfaceConfigure(void* data, xdg_surface* surface, std::uint32_t serial) {
    auto* state = static_cast<WaylandPopoverSurfaceState*>(data);
    if (!state) {
      return;
    }
    xdg_surface_ack_configure(surface, serial);
    if (!state->committed && state->host && state->owner) {
      state->host->mount(Size{static_cast<float>(state->width), static_cast<float>(state->height)});
      state->owner->renderPopover(*state, "initial-configure");
      state->committed = true;
      if (state->redrawRequested) state->owner->requestPopoverFrame(*state);
      if (!state->grabbed && state->popup && state->shared && state->shared->seat &&
          state->grabSerial != 0) {
        xdg_popup_grab(state->popup, state->shared->seat, state->grabSerial);
        state->grabbed = true;
      }
      flushWaylandDisplay(state->shared, "popover configure");
    }
  }

  static void popoverConfigure(void*, xdg_popup*, std::int32_t, std::int32_t,
                               std::int32_t, std::int32_t) {}

  static void popoverDone(void* data, xdg_popup*) {
    auto* state = static_cast<WaylandPopoverSurfaceState*>(data);
    if (state && state->owner) {
      state->owner->dismissPopoverState(state);
    }
  }

  static void popoverRepositioned(void*, xdg_popup*, std::uint32_t) {}

  static void popoverFrameDone(void* data, wl_callback* callback, std::uint32_t) {
    auto* state = static_cast<WaylandPopoverSurfaceState*>(data);
    if (!state) {
      if (callback) wl_callback_destroy(callback);
      return;
    }
    if (state->frameCallback == callback) {
      state->frameCallback = nullptr;
    }
    wl_callback_destroy(callback);
    ++state->frameDoneCount;
    tracePopover(*state, "frame-done");
    if (!state->redrawRequested || state->closing || state->closeAfterEvent || !state->owner) {
      return;
    }
    state->owner->renderPopover(*state, "frame-callback");
  }

  static void frameDone(void* data, wl_callback* callback, std::uint32_t) {
    auto* self = static_cast<WaylandWindow*>(data);
    if (callback == self->frameCallback_) {
      wl_callback_destroy(self->frameCallback_);
      self->frameCallback_ = nullptr;
    } else {
      wl_callback_destroy(callback);
    }
    if (!self->framePending_) return;
    if (detail::resizeTraceEnabled()) {
      LAMBDA_RESIZE_TRACE("wayland-window", "frame-done window=%u size=%dx%d\n",
                   self->handle_, static_cast<int>(std::lround(self->size_.width)),
                   static_cast<int>(std::lround(self->size_.height)));
    }
    auto& queue = Application::instance().eventQueue();
    queue.post(FrameEvent{nowNanos(), self->handle_});
    queue.dispatch();
    if (self->dispatchingWaylandEvents_) {
      self->frameDoneFlushPending_ = true;
    } else {
      Application::instance().flushRedraw();
    }
    self->wakeEventLoop();
  }

  static void xdgConfigure(void* data, xdg_surface* surface, std::uint32_t serial) {
    auto* self = static_cast<WaylandWindow*>(data);
    xdg_surface_ack_configure(surface, serial);
    self->configured_ = true;
    if (detail::resizeTraceEnabled()) {
      LAMBDA_RESIZE_TRACE("wayland-window", "xdg-configure window=%u serial=%u pending=%dx%d\n",
                   self->handle_, serial, self->pendingWidth_, self->pendingHeight_);
    }
    if (self->pendingWidth_ > 0 && self->pendingHeight_ > 0) {
      self->applyConfiguredSize(self->pendingWidth_, self->pendingHeight_);
      self->pendingWidth_ = self->pendingHeight_ = 0;
    }
  }

  static void topConfigure(void* data, xdg_toplevel*, std::int32_t width, std::int32_t height,
                           wl_array*) {
    auto* self = static_cast<WaylandWindow*>(data);
    if (width > 0 && height > 0) {
      self->pendingWidth_ = width;
      self->pendingHeight_ = height;
      if (detail::resizeTraceEnabled()) {
        LAMBDA_RESIZE_TRACE("wayland-window", "toplevel-configure window=%u size=%dx%d\n",
                     self->handle_, width, height);
      }
    }
  }

	  static void topClose(void* data, xdg_toplevel*) {
	    auto* self = static_cast<WaylandWindow*>(data);
	    Application::instance().eventQueue().post(WindowEvent{WindowEvent::Kind::CloseRequest, self->handle_});
	  }
	  static void topConfigureBounds(void* data, xdg_toplevel*, std::int32_t width, std::int32_t height) {
    auto* self = static_cast<WaylandWindow*>(data);
    self->configureBoundsWidth_ = std::max(0, width);
    self->configureBoundsHeight_ = std::max(0, height);
    setVulkanCanvasResizeBoundsHint(self->canvas_, self->configureBoundsWidth_, self->configureBoundsHeight_);
    if (detail::resizeTraceEnabled()) {
      LAMBDA_RESIZE_TRACE("wayland-window", "configure-bounds window=%u size=%dx%d\n",
                          self->handle_, self->configureBoundsWidth_, self->configureBoundsHeight_);
    }
	  }
	  static void topCapabilities(void*, xdg_toplevel*, wl_array*) {}

	  static void layerConfigure(void* data,
	                             zwlr_layer_surface_v1* layerSurface,
	                             std::uint32_t serial,
	                             std::uint32_t width,
	                             std::uint32_t height) {
	    auto* self = static_cast<WaylandWindow*>(data);
	    zwlr_layer_surface_v1_ack_configure(layerSurface, serial);
	    self->configured_ = true;
	    if (width > 0 && height > 0) {
	      self->applyConfiguredSize(static_cast<int>(width), static_cast<int>(height));
	    }
	  }

	  static void layerClosed(void* data, zwlr_layer_surface_v1*) {
	    auto* self = static_cast<WaylandWindow*>(data);
	    Application::instance().eventQueue().post(WindowEvent{WindowEvent::Kind::CloseRequest, self->handle_});
	  }

  static void decorationConfigure(void* data, zxdg_toplevel_decoration_v1*, std::uint32_t mode) {
    auto* self = static_cast<WaylandWindow*>(data);
    self->serverSideDecorationsActive_ = mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
    if (self->serverSideDecorationsActive_) {
      if (debugDecorations() && !self->loggedDecorationMode_) {
        self->loggedDecorationMode_ = true;
        std::fprintf(stderr, "Lambda Wayland: compositor accepted server-side decorations.\n");
      }
    } else if (!self->warnedDecorationFallback_) {
      self->warnedDecorationFallback_ = true;
      std::fprintf(stderr, "Lambda Wayland: compositor refused server-side decorations; resize chrome may be absent.\n");
    }
    Application::instance().eventQueue().post(WindowEvent{WindowEvent::Kind::Resize, self->handle_, self->size_});
  }

  static void cutoutBox(void* data,
                        xx_cutouts_v1*,
                        std::int32_t x,
                        std::int32_t y,
                        std::int32_t width,
                        std::int32_t height,
                        std::uint32_t,
                        std::uint32_t id) {
    auto* self = static_cast<WaylandWindow*>(data);
    self->pendingCutoutReceived_ = true;
    self->pendingCutoutX_ = x;
    self->pendingCutoutY_ = y;
    self->pendingCutoutWidth_ = width;
    self->pendingCutoutHeight_ = height;
    self->pendingCutoutId_ = id;
  }

  static void cutoutCorner(void*, xx_cutouts_v1*, std::uint32_t, std::uint32_t, std::uint32_t) {}
  static void cutoutsConfigure(void* data, xx_cutouts_v1*) {
    auto* self = static_cast<WaylandWindow*>(data);
    self->receivedCutout_ = self->pendingCutoutReceived_;
    self->lastCutoutX_ = self->pendingCutoutX_;
    self->lastCutoutY_ = self->pendingCutoutY_;
    self->lastCutoutWidth_ = self->pendingCutoutWidth_;
    self->lastCutoutHeight_ = self->pendingCutoutHeight_;
    self->lastCutoutId_ = self->pendingCutoutId_;
    self->pendingCutoutReceived_ = false;
    self->pendingCutoutX_ = 0;
    self->pendingCutoutY_ = 0;
    self->pendingCutoutWidth_ = 0;
    self->pendingCutoutHeight_ = 0;
    self->pendingCutoutId_ = 0;
    Application::instance().eventQueue().post(WindowEvent{WindowEvent::Kind::Resize, self->handle_, self->size_});
  }

  static void surfaceEnter(void* data, wl_surface*, wl_output* output) {
    auto* self = static_cast<WaylandWindow*>(data);
    if (std::find(self->enteredOutputs_.begin(), self->enteredOutputs_.end(), output) == self->enteredOutputs_.end()) {
      self->enteredOutputs_.push_back(output);
    }
    self->updateEnteredScale();
  }
  static void surfaceLeave(void* data, wl_surface*, wl_output* output) {
    auto* self = static_cast<WaylandWindow*>(data);
    self->enteredOutputs_.erase(std::remove(self->enteredOutputs_.begin(), self->enteredOutputs_.end(), output),
                                self->enteredOutputs_.end());
    self->updateEnteredScale();
  }
  static void surfacePreferredBufferScale(void* data, wl_surface*, std::int32_t factor) {
    auto* self = static_cast<WaylandWindow*>(data);
    if (self->usesFractionalScale()) return;
    self->applyDpiScale(static_cast<float>(std::max(1, factor)), false);
  }
  static void surfacePreferredBufferTransform(void*, wl_surface*, std::uint32_t) {}

  static void fractionalPreferredScale(void* data, wp_fractional_scale_v1*, std::uint32_t preferredScale) {
    auto* self = static_cast<WaylandWindow*>(data);
    float const scale = safeScale(static_cast<float>(preferredScale) / 120.f);
    self->applyDpiScale(scale, true);
  }

  void applyConfiguredSize(int width, int height) {
    auto const start = std::chrono::steady_clock::now();
    size_ = {static_cast<float>(std::max(1, width)), static_cast<float>(std::max(1, height))};
    if (canvas_) canvas_->resize(static_cast<int>(std::lround(size_.width)),
                                 static_cast<int>(std::lround(size_.height)));
    updateViewportDestination();
    updateBackgroundEffectRegion();
    if (lambdaWindow_) lambdaWindow_->updateCanvasDpiScale(dpiScaleX_, dpiScaleY_);
    queueResizeEvent();
    applyCursor(currentCursor_);
    requestResizeRedraw();
    if (!dispatchingWaylandEvents_) {
      flushDeferredRedraw();
    }
    if (detail::resizeTraceEnabled()) {
      auto const elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start).count();
      LAMBDA_RESIZE_TRACE("wayland-window",
          "apply-configure window=%u size=%dx%d framePending=%d batched=%d elapsed=%.3fms\n",
          handle_, width, height, framePending_ ? 1 : 0, dispatchingWaylandEvents_ ? 1 : 0,
          static_cast<double>(elapsed) / 1000.0);
    }
  }

  void updateCanvasDpi() {
    if (lambdaWindow_) lambdaWindow_->updateCanvasDpiScale(dpiScaleX_, dpiScaleY_);
  }

  void resizeCanvasForCurrentSize() {
    if (canvas_) canvas_->resize(static_cast<int>(std::lround(size_.width)),
                                 static_cast<int>(std::lround(size_.height)));
  }

  bool usesFractionalScale() const {
    return fractionalScale_ && viewport_;
  }

  void applyDpiScale(float scale, bool fractionalProtocol) {
    scale = safeScale(scale);
    if (std::abs(scale - dpiScaleX_) < 0.001f && std::abs(scale - dpiScaleY_) < 0.001f) return;
    dpiScaleX_ = scale;
    dpiScaleY_ = scale;
    wl_surface_set_buffer_scale(surface_,
                                fractionalProtocol ? 1
                                                   : static_cast<std::int32_t>(std::max(1.f, std::round(scale))));
    updateViewportDestination();
    if (lambdaWindow_) lambdaWindow_->updateCanvasDpiScale(dpiScaleX_, dpiScaleY_);
    resizeCanvasForCurrentSize();
    Application::instance().eventQueue().post(WindowEvent{.kind = WindowEvent::Kind::DpiChanged,
                                                          .handle = handle_,
                                                          .dpi = dpiScaleX_,
                                                          .dpiX = dpiScaleX_,
                                                          .dpiY = dpiScaleY_});
    queueResizeEvent();
    applyCursor(currentCursor_);
    requestResizeRedraw();
  }

  void updateViewportDestination() {
    if (!viewport_) return;
    int const logicalWidth = std::max(1, static_cast<int>(std::lround(size_.width)));
    int const logicalHeight = std::max(1, static_cast<int>(std::lround(size_.height)));
    int const sourceWidth = std::max(1, static_cast<int>(std::lround(size_.width * dpiScaleX_)));
    int const sourceHeight = std::max(1, static_cast<int>(std::lround(size_.height * dpiScaleY_)));
    wp_viewport_set_source(viewport_,
                           wl_fixed_from_int(0),
                           wl_fixed_from_int(0),
                           wl_fixed_from_int(sourceWidth),
                           wl_fixed_from_int(sourceHeight));
    wp_viewport_set_destination(viewport_, logicalWidth, logicalHeight);
  }

  bool ensureCursorTheme(int scale) {
    if (!shared_ || !shared_->compositor || !shared_->shm) {
      return false;
    }
    scale = std::max(1, scale);
    if (!shared_->cursorSurface) {
      shared_->cursorSurface = wl_compositor_create_surface(shared_->compositor);
    }
    if (!shared_->cursorTheme || shared_->cursorThemeScale != scale) {
      if (shared_->cursorTheme) {
        wl_cursor_theme_destroy(shared_->cursorTheme);
        shared_->cursorTheme = nullptr;
      }
      shared_->cursorTheme = wl_cursor_theme_load(nullptr, 24 * scale, shared_->shm);
      shared_->cursorThemeScale = scale;
    }
    return shared_->cursorSurface && shared_->cursorTheme;
  }

  wl_cursor* loadCursor(Cursor cursor) {
    if (!shared_ || !shared_->cursorTheme) {
      return nullptr;
    }
    for (char const* const* name = cursorNames(cursor); *name; ++name) {
      if (wl_cursor* loaded = wl_cursor_theme_get_cursor(shared_->cursorTheme, *name)) {
        return loaded;
      }
    }
    return nullptr;
  }

  void applyCursor(Cursor cursor) {
    if (!shared_ || !shared_->pointer || pointerEnterSerial_ == 0 || !canSendWaylandRequests(shared_)) {
      return;
    }
    int const scale = static_cast<int>(std::max(1.f, std::round(dpiScaleX_)));
    if (!ensureCursorTheme(scale)) {
      return;
    }
    wl_cursor* loaded = loadCursor(cursor);
    if (!loaded || loaded->image_count == 0) {
      loaded = loadCursor(Cursor::Arrow);
    }
    if (!loaded || loaded->image_count == 0) {
      return;
    }
    wl_cursor_image* image = loaded->images[0];
    wl_buffer* buffer = wl_cursor_image_get_buffer(image);
    if (!buffer) {
      return;
    }
    wl_surface_set_buffer_scale(shared_->cursorSurface, scale);
    wl_surface_attach(shared_->cursorSurface, buffer, 0, 0);
    std::uint32_t const cursorScale = static_cast<std::uint32_t>(scale);
    wl_surface_damage(shared_->cursorSurface, 0, 0,
                      static_cast<std::int32_t>(std::max(1u, image->width / cursorScale)),
                      static_cast<std::int32_t>(std::max(1u, image->height / cursorScale)));
    wl_surface_commit(shared_->cursorSurface);
    wl_pointer_set_cursor(shared_->pointer, pointerEnterSerial_, shared_->cursorSurface,
                          static_cast<std::int32_t>(image->hotspot_x / static_cast<std::uint32_t>(scale)),
                          static_cast<std::int32_t>(image->hotspot_y / static_cast<std::uint32_t>(scale)));
    flushWaylandDisplay(shared_, "cursor update");
  }

  void requestServerSideDecorations() {
    if (!canSendWaylandRequests(shared_)) {
      return;
    }
    if (decoration_) {
      zxdg_toplevel_decoration_v1_set_mode(decoration_, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
      return;
    }
    if (!shared_ || !shared_->decorationManager) {
      if (!warnedDecorationFallback_) {
        warnedDecorationFallback_ = true;
        std::fprintf(stderr, "Lambda Wayland: compositor does not expose xdg-decoration; server-side decorations are unavailable.\n");
      }
      return;
    }
    if (shared_->decorationManagerVersion < 2 && surfaceCommitted_) {
      if (!warnedDecorationFallback_) {
        warnedDecorationFallback_ = true;
        std::fprintf(stderr, "Lambda Wayland: xdg-decoration v1 cannot create decorations after the first commit.\n");
      }
      return;
    }
    decoration_ = zxdg_decoration_manager_v1_get_toplevel_decoration(shared_->decorationManager, toplevel_);
    zxdg_toplevel_decoration_v1_add_listener(decoration_, &decorationListener_, this);
    zxdg_toplevel_decoration_v1_set_mode(decoration_, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
  }

  void configureDecorationProtocol() {
    serverSideDecorationsActive_ = false;
    receivedCutout_ = false;
    if (cutouts_ && titlebarMode_ != WindowTitlebarMode::Integrated) {
      if (canSendWaylandRequests(shared_)) xx_cutouts_v1_destroy(cutouts_);
      cutouts_ = nullptr;
    }
    if (!canSendWaylandRequests(shared_)) {
      return;
    }

    if (titlebarMode_ == WindowTitlebarMode::Client || titlebarMode_ == WindowTitlebarMode::None) {
      if (decoration_) {
        zxdg_toplevel_decoration_v1_set_mode(decoration_, ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
      }
      return;
    }

    requestServerSideDecorations();
    if (titlebarMode_ == WindowTitlebarMode::Integrated) {
      requestCutouts();
    }
  }

  void commitSurface() {
    if (surface_ && canSendWaylandRequests(shared_)) {
      wl_surface_commit(surface_);
      flushWaylandDisplay(shared_, "surface commit");
    }
  }

  void updateOpaqueRegion() {
    if (!shared_ || !shared_->compositor || !surface_ || !canSendWaylandRequests(shared_)) {
      return;
    }
    int const width = std::max(0, static_cast<int>(std::lround(size_.width)));
    int const height = std::max(0, static_cast<int>(std::lround(size_.height)));
    bool const enabled = !wantsTransparentSurface() && width > 0 && height > 0;
    if (opaqueRegionConfigured_ &&
        opaqueRegionEnabled_ == enabled &&
        opaqueRegionWidth_ == width &&
        opaqueRegionHeight_ == height) {
      return;
    }

    wl_region* region = nullptr;
    if (enabled) {
      region = wl_compositor_create_region(shared_->compositor);
      wl_region_add(region, 0, 0, width, height);
    }
    wl_surface_set_opaque_region(surface_, region);
    if (region) wl_region_destroy(region);
    opaqueRegionConfigured_ = true;
    opaqueRegionEnabled_ = enabled;
    opaqueRegionWidth_ = width;
    opaqueRegionHeight_ = height;
  }

  void updateBackgroundEffectRegion() {
    LayerShellChromeOptions const& chrome = layerShellConfig_.chrome;
    bool const wantsGlass = background_.kind == WindowBackgroundKind::Glass;
    bool const wantsBlur = wantsGlass ||
                           layerShellConfig_.backgroundBlur ||
                           chrome.style != LayerShellChromeStyle::None;
    if (!shared_ || !shared_->backgroundEffectManager || !shared_->compositor || !surface_ ||
        !canSendWaylandRequests(shared_)) {
      return;
    }
    if (!wantsBlur) {
      if (!backgroundEffect_) {
        return;
      }
      ext_background_effect_surface_v1_set_blur_radius(backgroundEffect_, wl_fixed_from_double(0.f));
      ext_background_effect_surface_v1_set_base_color(backgroundEffect_, colorToRgba(Color{0.f, 0.f, 0.f, 0.f}));
      ext_background_effect_surface_v1_set_tint(backgroundEffect_, colorToRgba(Color{0.f, 0.f, 0.f, 0.f}));
      ext_background_effect_surface_v1_set_border(backgroundEffect_, colorToRgba(Color{0.f, 0.f, 0.f, 0.f}));
      wl_region* region = wl_compositor_create_region(shared_->compositor);
      ext_background_effect_surface_v1_set_blur_region(backgroundEffect_, region);
      wl_region_destroy(region);
      return;
    }
    int const width = std::max(1, static_cast<int>(std::lround(size_.width)));
    int const height = std::max(1, static_cast<int>(std::lround(size_.height)));
    if (!backgroundEffect_) {
      backgroundEffect_ = ext_background_effect_manager_v1_get_background_effect(shared_->backgroundEffectManager,
                                                                                 surface_);
    }
    wl_region* region = wl_compositor_create_region(shared_->compositor);
    if (layerShellConfig_.backgroundEffectRegion) {
      auto const& requested = *layerShellConfig_.backgroundEffectRegion;
      int const x = std::clamp(requested.x, 0, width);
      int const y = std::clamp(requested.y, 0, height);
      int const right = std::clamp(requested.x + requested.width, x, width);
      int const bottom = std::clamp(requested.y + requested.height, y, height);
      if (right > x && bottom > y) {
        wl_region_add(region, x, y, right - x, bottom - y);
      }
    } else {
      wl_region_add(region, 0, 0, width, height);
    }
    ext_background_effect_surface_v1_set_blur_region(backgroundEffect_, region);
    wl_region_destroy(region);
    if (ext_background_effect_surface_v1_get_version(backgroundEffect_) >= 4) {
      if (layerShellConfig_.backgroundEffectRegion) {
        auto const& requested = *layerShellConfig_.backgroundEffectRegion;
        ext_background_effect_surface_v1_set_shape(backgroundEffect_,
                                                   backgroundEffectShape(requested.shape),
                                                   backgroundEffectCalloutPlacement(requested.calloutPlacement),
                                                   wl_fixed_from_double(requested.arrowWidth),
                                                   wl_fixed_from_double(requested.arrowHeight));
      } else {
        ext_background_effect_surface_v1_set_shape(backgroundEffect_,
                                                   EXT_BACKGROUND_EFFECT_SURFACE_V1_SHAPE_ROUNDED_RECT,
                                                   EXT_BACKGROUND_EFFECT_SURFACE_V1_CALLOUT_PLACEMENT_BELOW,
                                                   wl_fixed_from_double(0.f),
                                                   wl_fixed_from_double(0.f));
      }
    }
    if (wantsGlass) {
      float const opacity = std::clamp(background_.glass.opacity, 0.f, 1.f);
      Color baseColor = background_.glass.baseColor;
      baseColor.a *= opacity;
      Color tint = background_.glass.tintColor;
      tint.a *= opacity;
      ext_background_effect_surface_v1_set_blur_radius(backgroundEffect_, wl_fixed_from_double(background_.glass.blurRadius));
      ext_background_effect_surface_v1_set_base_color(backgroundEffect_, colorToRgba(baseColor));
      ext_background_effect_surface_v1_set_tint(backgroundEffect_, colorToRgba(tint));
      ext_background_effect_surface_v1_set_border(backgroundEffect_, colorToRgba(background_.glass.borderColor));
    } else if (chrome.style != LayerShellChromeStyle::None) {
      float const opacity = std::clamp(chrome.glass.opacity, 0.f, 1.f);
      Color baseColor = chrome.glass.baseColor;
      baseColor.a *= opacity;
      Color tint = chrome.glass.tintColor;
      tint.a *= opacity;
      ext_background_effect_surface_v1_set_blur_radius(backgroundEffect_, wl_fixed_from_double(chrome.glass.blurRadius));
      ext_background_effect_surface_v1_set_base_color(backgroundEffect_, colorToRgba(baseColor));
      ext_background_effect_surface_v1_set_tint(backgroundEffect_, colorToRgba(tint));
      ext_background_effect_surface_v1_set_border(
          backgroundEffect_,
          colorToRgba(chrome.style == LayerShellChromeStyle::BlurPanelBorder
                          ? chrome.glass.borderColor
                          : Color{0.f, 0.f, 0.f, 0.f}));
      CornerRadius const radius = chrome.cornerRadius;
      ext_background_effect_surface_v1_set_corner_radii(backgroundEffect_,
                                                        wl_fixed_from_double(radius.topLeft),
                                                        wl_fixed_from_double(radius.topRight),
                                                        wl_fixed_from_double(radius.bottomRight),
                                                        wl_fixed_from_double(radius.bottomLeft));
    } else {
      ext_background_effect_surface_v1_set_base_color(backgroundEffect_, colorToRgba(Color{0.f, 0.f, 0.f, 0.f}));
      ext_background_effect_surface_v1_set_tint(backgroundEffect_, colorToRgba(Color{0.f, 0.f, 0.f, 0.f}));
      ext_background_effect_surface_v1_set_border(backgroundEffect_, colorToRgba(Color{0.f, 0.f, 0.f, 0.f}));
    }
  }

  void updateLayerShellInputRegion() {
    if (!layerSurface_ || !shared_ || !shared_->compositor || !surface_ || !canSendWaylandRequests(shared_)) {
      return;
    }
    if (!layerShellConfig_.inputRegion) {
      wl_surface_set_input_region(surface_, nullptr);
      return;
    }

    auto const& requested = *layerShellConfig_.inputRegion;
    wl_region* region = wl_compositor_create_region(shared_->compositor);
    if (requested.width > 0 && requested.height > 0) {
      wl_region_add(region, requested.x, requested.y, requested.width, requested.height);
    }
    wl_surface_set_input_region(surface_, region);
    wl_region_destroy(region);
  }

  bool wantsTransparentSurface() const {
    return background_.kind == WindowBackgroundKind::Glass ||
           background_.kind == WindowBackgroundKind::Transparent;
  }

  void configureLayerShellSurface() {
    if (!shared_ || !shared_->layerShell) {
      throw std::runtime_error("Wayland compositor does not expose zwlr_layer_shell_v1");
    }
    std::string ns = layerShellConfig_.nameSpace.empty() ? appId_ : layerShellConfig_.nameSpace;
    layerSurface_ = zwlr_layer_shell_v1_get_layer_surface(shared_->layerShell,
                                                          surface_,
                                                          nullptr,
                                                          layerShellLayer(layerShellConfig_.layer),
                                                          ns.c_str());
    zwlr_layer_surface_v1_add_listener(layerSurface_, &layerSurfaceListener_, this);
    zwlr_layer_surface_v1_set_size(layerSurface_,
                                   layerShellProtocolWidth(size_, layerShellConfig_),
                                   layerShellProtocolHeight(size_, layerShellConfig_));
    zwlr_layer_surface_v1_set_anchor(layerSurface_, layerShellAnchor(layerShellConfig_));
    zwlr_layer_surface_v1_set_margin(layerSurface_,
                                     layerShellConfig_.marginTop,
                                     layerShellConfig_.marginRight,
                                     layerShellConfig_.marginBottom,
                                     layerShellConfig_.marginLeft);
    zwlr_layer_surface_v1_set_exclusive_zone(layerSurface_, layerShellConfig_.exclusiveZone);
    zwlr_layer_surface_v1_set_keyboard_interactivity(layerSurface_,
                                                     layerShellConfig_.keyboardInteractive ? 1u : 0u);
    updateBackgroundEffectRegion();
    updateLayerShellInputRegion();
  }

  void requestCutouts() {
    if (cutouts_) {
      if (canSendWaylandRequests(shared_)) xx_cutouts_v1_destroy(cutouts_);
      cutouts_ = nullptr;
    }
    if (!shared_ || !shared_->cutoutsManager || !surface_ || !canSendWaylandRequests(shared_)) return;
    cutouts_ = xx_cutouts_manager_v1_get_cutouts(shared_->cutoutsManager, surface_);
    if (cutouts_) xx_cutouts_v1_add_listener(cutouts_, &cutoutsListener_, this);
  }

  float outputScale(wl_output* output) const {
    if (!shared_) return 1.f;
    for (auto const& candidate : shared_->outputs) {
      if (candidate->output == output) return candidate->scale;
    }
    return 1.f;
  }

  void updateEnteredScale() {
    if (usesFractionalScale()) return;
    float scale = 1.f;
    for (wl_output* output : enteredOutputs_) {
      scale = std::max(scale, outputScale(output));
    }
    applyDpiScale(scale, false);
  }

  void queueResizeEvent() {
    pendingResizeEvent_ = true;
    pendingResizeSize_ = size_;
  }

  void requestResizeRedraw() {
    resizeRedrawPending_ = true;
    Application::instance().requestWindowRedraw(handle_);
    wakeEventLoop();
    if (detail::resizeTraceEnabled()) {
      LAMBDA_RESIZE_TRACE("wayland-window", "request-resize-redraw window=%u framePending=%d\n",
                   handle_, framePending_ ? 1 : 0);
    }
  }

  void drainWakePipe() {
    char buffer[64];
    while (read(wakePipe_[0], buffer, sizeof(buffer)) > 0) {}
  }

  bool dispatchPendingWayland(char const* context) {
    if (!checkWaylandConnection(shared_, context)) {
      return false;
    }
    dispatchingWaylandEvents_ = true;
    int const rc = wl_display_dispatch_pending(display_);
    dispatchingWaylandEvents_ = false;
    if (rc < 0) {
      int const error = wl_display_get_error(display_);
      markWaylandConnectionDead(shared_, context, error == 0 ? errno : error);
      return false;
    }
    return checkWaylandConnection(shared_, context);
  }

  void dispatchReadyEvents(int timeoutMs) {
    if (!display_ || !checkWaylandConnection(shared_, "event dispatch")) {
      return;
    }
    for (;;) {
      errno = 0;
      if (wl_display_prepare_read(display_) == 0) {
        break;
      }
      int const prepareErrno = errno;
      if (prepareErrno != EAGAIN && prepareErrno != 0) {
        markWaylandConnectionDead(shared_, "event prepare read", prepareErrno);
        return;
      }
      if (!dispatchPendingWayland("pending event dispatch before read")) {
        return;
      }
    }

    bool preparedRead = true;
    auto cancelPreparedRead = [&] {
      if (preparedRead && display_) {
        wl_display_cancel_read(display_);
        preparedRead = false;
      }
    };

    if (!flushWaylandDisplay(shared_, "event dispatch flush")) {
      cancelPreparedRead();
      return;
    }

    pollfd fds[2]{{wl_display_get_fd(display_), POLLIN, 0}, {wakePipe_[0], POLLIN, 0}};
    int rc = -1;
    do {
      rc = poll(fds, 2, timeoutMs < 0 ? -1 : timeoutMs);
    } while (rc < 0 && errno == EINTR);

    if (rc < 0) {
      int const pollErrno = errno;
      cancelPreparedRead();
      markWaylandConnectionDead(shared_, "event poll", pollErrno);
      return;
    }
    if (rc > 0 && (fds[1].revents & POLLIN)) {
      drainWakePipe();
    }
    if (rc > 0 && (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL))) {
      cancelPreparedRead();
      markWaylandConnectionDead(shared_, "display poll", wl_display_get_error(display_));
      return;
    }
    if (rc > 0 && (fds[0].revents & POLLIN)) {
      preparedRead = false;
      if (wl_display_read_events(display_) < 0) {
        int const error = wl_display_get_error(display_);
        markWaylandConnectionDead(shared_, "event read", error == 0 ? errno : error);
        return;
      }
    } else {
      cancelPreparedRead();
    }
    dispatchPendingWayland("pending event dispatch after read");
  }

  void flushDeferredRedraw() {
    if (!resizeRedrawPending_ && !pendingResizeEvent_ && !frameDoneFlushPending_) return;
    auto const start = std::chrono::steady_clock::now();
    bool const shouldFlushRedraw = resizeRedrawPending_ || pendingResizeEvent_;
    bool const resizeEventPending = pendingResizeEvent_;
    bool const frameDoneFlushPending = frameDoneFlushPending_;
    resizeRedrawPending_ = false;
    frameDoneFlushPending_ = false;
    if (pendingResizeEvent_) {
      pendingResizeEvent_ = false;
      Application::instance().eventQueue().post(WindowEvent{WindowEvent::Kind::Resize, handle_, pendingResizeSize_});
    }
    Application::instance().eventQueue().dispatch();
    bool const renderedImmediately = frameDoneFlushPending || (shouldFlushRedraw && (!framePending_ || resizeEventPending));
    if (renderedImmediately) {
      Application::instance().flushRedraw();
    }
    if (detail::resizeTraceEnabled()) {
      auto const elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start).count();
      LAMBDA_RESIZE_TRACE("wayland-window",
          "flush-deferred window=%u size=%.0fx%.0f framePending=%d immediate=%d elapsed=%.3fms\n",
          handle_, pendingResizeSize_.width, pendingResizeSize_.height, framePending_ ? 1 : 0,
          renderedImmediately ? 1 : 0, static_cast<double>(elapsed) / 1000.0);
    }
  }

  static inline wl_callback_listener frameCallbackListener_{frameDone};
  static inline wl_surface_listener surfaceListener_{surfaceEnter, surfaceLeave, surfacePreferredBufferScale,
                                                    surfacePreferredBufferTransform};
  static inline wp_fractional_scale_v1_listener fractionalScaleListener_{fractionalPreferredScale};
  static inline xdg_surface_listener xdgSurfaceListener_{xdgConfigure};
  static inline xdg_surface_listener popupXdgSurfaceListener_{popupXdgSurfaceConfigure};
  static inline xdg_popup_listener popupListener_{popupConfigure, popupDone, popupRepositioned};
  static inline xdg_surface_listener popoverXdgSurfaceListener_{popoverXdgSurfaceConfigure};
  static inline xdg_popup_listener popoverListener_{popoverConfigure, popoverDone, popoverRepositioned};
  static inline wl_callback_listener popoverFrameCallbackListener_{popoverFrameDone};
  static inline xdg_toplevel_listener toplevelListener_{topConfigure, topClose, topConfigureBounds,
                                                       topCapabilities};
  static inline zwlr_layer_surface_v1_listener layerSurfaceListener_{layerConfigure, layerClosed};
  static inline zxdg_toplevel_decoration_v1_listener decorationListener_{decorationConfigure};
  static inline xx_cutouts_v1_listener cutoutsListener_{cutoutBox, cutoutCorner, cutoutsConfigure};
  static constexpr float kClientTitlebarHeight = 48.f;
  static constexpr float kCompositorControlReserveWidth = 96.f;

  static std::uint32_t xdgResizeEdge(WindowResizeEdge edge) {
    switch (edge) {
    case WindowResizeEdge::Top: return XDG_TOPLEVEL_RESIZE_EDGE_TOP;
    case WindowResizeEdge::Bottom: return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;
    case WindowResizeEdge::Left: return XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
    case WindowResizeEdge::Right: return XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;
    case WindowResizeEdge::TopLeft: return XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;
    case WindowResizeEdge::TopRight: return XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;
    case WindowResizeEdge::BottomLeft: return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;
    case WindowResizeEdge::BottomRight: return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;
    case WindowResizeEdge::None: break;
    }
    return XDG_TOPLEVEL_RESIZE_EDGE_NONE;
  }

  wl_display* display_ = nullptr;
  std::vector<wl_output*> enteredOutputs_;
  wl_surface* surface_ = nullptr;
  WaylandNativeSurface nativeSurface_{};
	  wl_callback* frameCallback_ = nullptr;
	  xdg_surface* xdgSurface_ = nullptr;
	  xdg_toplevel* toplevel_ = nullptr;
	  zwlr_layer_surface_v1* layerSurface_ = nullptr;
	  ext_background_effect_surface_v1* backgroundEffect_ = nullptr;
	  zxdg_toplevel_decoration_v1* decoration_ = nullptr;
  xx_cutouts_v1* cutouts_ = nullptr;
  wp_viewport* viewport_ = nullptr;
  wp_fractional_scale_v1* fractionalScale_ = nullptr;
  Canvas* canvas_ = nullptr;
  SharedWaylandConnection* shared_ = nullptr;
  std::unique_ptr<WaylandPopupMenuState> popupMenu_;
  std::vector<std::unique_ptr<WaylandPopoverSurfaceState>> popovers_;
  std::uint64_t nextPopoverId_ = 1;

  ::lambda::Window* lambdaWindow_ = nullptr;
  unsigned int handle_ = 0;
  Size size_{};
  std::string title_;
  std::string appId_;
  float dpiScaleX_ = 1.f;
  float dpiScaleY_ = 1.f;
  bool fullscreen_ = false;
  WindowTitlebarMode titlebarMode_ = WindowTitlebarMode::System;
  WindowBackground background_{};
  LayerShellOptions layerShellConfig_{};
  bool surfaceCommitted_ = false;
  bool configured_ = false;
  bool serverSideDecorationsActive_ = false;
  bool receivedCutout_ = false;
  bool warnedDecorationFallback_ = false;
  bool loggedDecorationMode_ = false;
  bool opaqueRegionConfigured_ = false;
  bool opaqueRegionEnabled_ = false;
  int opaqueRegionWidth_ = 0;
  int opaqueRegionHeight_ = 0;
  std::int32_t lastCutoutX_ = 0;
  std::int32_t lastCutoutY_ = 0;
  std::int32_t lastCutoutWidth_ = 0;
  std::int32_t lastCutoutHeight_ = 0;
  std::uint32_t lastCutoutId_ = 0;
  bool pendingCutoutReceived_ = false;
  std::int32_t pendingCutoutX_ = 0;
  std::int32_t pendingCutoutY_ = 0;
  std::int32_t pendingCutoutWidth_ = 0;
  std::int32_t pendingCutoutHeight_ = 0;
  std::uint32_t pendingCutoutId_ = 0;
  int pendingWidth_ = 0;
  int pendingHeight_ = 0;
  int configureBoundsWidth_ = 0;
  int configureBoundsHeight_ = 0;
  Point pointerPos_{};
  std::uint32_t pointerEnterSerial_ = 0;
  std::uint32_t lastPointerButtonSerial_ = 0;
  Cursor currentCursor_ = Cursor::Arrow;
  std::uint8_t pressedButtons_ = 0;
  WaylandScrollAccumulator pendingScroll_;
  Modifiers currentModifiers_ = Modifiers::None;
  bool resizeRedrawPending_ = false;
  bool pendingResizeEvent_ = false;
  bool frameDoneFlushPending_ = false;
  Size pendingResizeSize_{};
  bool dispatchingWaylandEvents_ = false;
  bool framePending_ = false;
  int wakePipe_[2]{-1, -1};
};

namespace {

std::chrono::nanoseconds keyboardRepeatInterval(int repeatRate) {
  std::int64_t const rate = std::max(1, repeatRate);
  return std::chrono::nanoseconds{std::max<std::int64_t>(1'000'000, 1'000'000'000 / rate)};
}

void installKeyboardRepeatHandler(SharedWaylandConnection* shared) {
  if (!shared || !Application::hasInstance()) {
    return;
  }
  EventQueue& queue = Application::instance().eventQueue();
  if (shared->repeatHandlerQueue == &queue) {
    return;
  }
  queue.on<TimerEvent>([](TimerEvent const& event) {
    handleKeyboardRepeatTimer(&gWaylandConnection, event);
  });
  shared->repeatHandlerQueue = &queue;
}

void stopKeyboardRepeat(SharedWaylandConnection* shared) {
  if (!shared) {
    return;
  }
  if (shared->repeatTimerId != 0 && Application::hasInstance()) {
    Application::instance().cancelTimer(shared->repeatTimerId);
  }
  shared->repeatKey = 0;
  shared->repeatSurface = nullptr;
  shared->repeatWindow = nullptr;
  shared->repeatTimerId = 0;
  shared->repeatDelayPhase = false;
}

void startKeyboardRepeat(SharedWaylandConnection* shared, WaylandWindow* window,
                         wl_surface* surface, std::uint32_t key) {
  if (!shared || !window || !surface || !shared->xkb || !Application::hasInstance()) {
    return;
  }
  if (shared->keyboardRepeatRate <= 0 || shared->keyboardRepeatDelayMs <= 0 ||
      !shared->xkb->keyRepeats(key)) {
    return;
  }

  installKeyboardRepeatHandler(shared);
  stopKeyboardRepeat(shared);
  shared->repeatKey = key;
  shared->repeatSurface = surface;
  shared->repeatWindow = window;
  shared->repeatDelayPhase = true;
  shared->repeatTimerId = Application::instance().scheduleRepeatingTimer(
      std::chrono::milliseconds{shared->keyboardRepeatDelayMs}, window->handle());
}

void handleKeyboardRepeatTimer(SharedWaylandConnection* shared, TimerEvent const& event) {
  if (!shared || shared->repeatTimerId == 0 || event.timerId != shared->repeatTimerId) {
    return;
  }
  if (!shared->repeatWindow || !shared->xkb || shared->keyboardRepeatRate <= 0 ||
      shared->keyboardFocus != shared->repeatWindow || shared->keyboardSurface != shared->repeatSurface) {
    stopKeyboardRepeat(shared);
    return;
  }

  if (shared->repeatDelayPhase) {
    Application::instance().cancelTimer(shared->repeatTimerId);
    shared->repeatTimerId =
        Application::instance().scheduleRepeatingTimer(keyboardRepeatInterval(shared->keyboardRepeatRate),
                                                       shared->repeatWindow->handle());
    shared->repeatDelayPhase = false;
  }

  shared->repeatWindow->handleKeyboardKeyForSurface(shared->repeatSurface, shared->xkb.get(),
                                                    shared->repeatKey,
                                                    WL_KEYBOARD_KEY_STATE_PRESSED,
                                                    shared->lastSelectionSerial);
}

WaylandWindow* windowForSurface(SharedWaylandConnection* shared, wl_surface* surface) {
  if (!shared || !surface) return nullptr;
  for (WaylandWindow* window : shared->windows) {
    if (window && window->waylandSurface() == surface) return window;
  }
  return nullptr;
}

WaylandWindow* windowForPopupSurface(SharedWaylandConnection* shared, wl_surface* surface) {
  if (!shared || !surface) return nullptr;
  for (WaylandWindow* window : shared->windows) {
    if (window && window->ownsPopupSurface(surface)) return window;
  }
  return nullptr;
}

void refreshWindowsForOutput(SharedWaylandConnection* shared, wl_output* output) {
  if (!shared) return;
  for (WaylandWindow* window : shared->windows) {
    if (window) window->handleOutputScaleChanged(output);
  }
}

void clipboardOfferOffer(void* data, wl_data_offer*, char const* mimeType) {
  auto* offer = static_cast<WaylandClipboardOffer*>(data);
  if (!offer || !mimeType) return;
  if (std::find(offer->mimeTypes.begin(), offer->mimeTypes.end(), mimeType) == offer->mimeTypes.end()) {
    offer->mimeTypes.emplace_back(mimeType);
  }
}

void clipboardOfferSourceActions(void*, wl_data_offer*, std::uint32_t) {}
void clipboardOfferAction(void*, wl_data_offer*, std::uint32_t) {}

void clipboardSourceTarget(void*, wl_data_source*, char const*) {}

void clipboardSourceSend(void* data, wl_data_source* source, char const* mimeType, int fd) {
  auto* shared = static_cast<SharedWaylandConnection*>(data);
  if (!shared || source != shared->clipboardSource || !mimeType || !clipboardTextMime(mimeType)) {
    close(fd);
    return;
  }
  char const* cursor = shared->clipboardText.data();
  std::size_t remaining = shared->clipboardText.size();
  while (remaining > 0) {
    ssize_t const n = write(fd, cursor, remaining);
    if (n > 0) {
      cursor += n;
      remaining -= static_cast<std::size_t>(n);
      continue;
    }
    if (n < 0 && errno == EINTR) continue;
    break;
  }
  close(fd);
}

void clipboardSourceCancelled(void* data, wl_data_source* source) {
  auto* shared = static_cast<SharedWaylandConnection*>(data);
  if (!shared || source != shared->clipboardSource) return;
  shared->clipboardSource = nullptr;
  shared->clipboardText.clear();
  if (canSendWaylandRequests(shared)) {
    wl_data_source_destroy(source);
  }
}

void clipboardSourceDndDropPerformed(void*, wl_data_source*) {}
void clipboardSourceDndFinished(void*, wl_data_source*) {}
void clipboardSourceAction(void*, wl_data_source*, std::uint32_t) {}

void clipboardDeviceDataOffer(void* data, wl_data_device*, wl_data_offer* offer) {
  auto* shared = static_cast<SharedWaylandConnection*>(data);
  if (!shared || !offer) return;
  auto state = std::make_unique<WaylandClipboardOffer>();
  state->offer = offer;
  wl_data_offer_add_listener(offer, &clipboardOfferListener, state.get());
  shared->clipboardOffers.push_back(std::move(state));
}

void clipboardDeviceEnter(void*, wl_data_device*, std::uint32_t, wl_surface*, wl_fixed_t, wl_fixed_t, wl_data_offer*) {}
void clipboardDeviceLeave(void*, wl_data_device*) {}
void clipboardDeviceMotion(void*, wl_data_device*, std::uint32_t, wl_fixed_t, wl_fixed_t) {}
void clipboardDeviceDrop(void*, wl_data_device*) {}

void clipboardDeviceSelection(void* data, wl_data_device*, wl_data_offer* offer) {
  auto* shared = static_cast<SharedWaylandConnection*>(data);
  if (!shared) return;
  shared->selectionOffer = nullptr;
  if (!offer) {
    clearClipboardOffers(*shared, canSendWaylandRequests(shared));
    return;
  }
  for (auto it = shared->clipboardOffers.begin(); it != shared->clipboardOffers.end();) {
    if (*it && (*it)->offer == offer) {
      shared->selectionOffer = it->get();
      ++it;
      continue;
    }
    if (*it && (*it)->offer && canSendWaylandRequests(shared)) {
      wl_data_offer_destroy((*it)->offer);
    }
    it = shared->clipboardOffers.erase(it);
  }
}

void sharedRegistryGlobal(void* data, wl_registry* registry, std::uint32_t name,
                          char const* interface, std::uint32_t version) {
  auto* shared = static_cast<SharedWaylandConnection*>(data);
  if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
    shared->compositor = static_cast<wl_compositor*>(
        wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4u)));
  } else if (std::strcmp(interface, wl_shm_interface.name) == 0) {
    shared->shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
  } else if (std::strcmp(interface, wl_data_device_manager_interface.name) == 0) {
    shared->dataDeviceManager = static_cast<wl_data_device_manager*>(
        wl_registry_bind(registry, name, &wl_data_device_manager_interface, std::min(version, 3u)));
  } else if (std::strcmp(interface, xdg_wm_base_interface.name) == 0) {
    shared->wmBase = static_cast<xdg_wm_base*>(
        wl_registry_bind(registry, name, &xdg_wm_base_interface, std::min(version, 6u)));
    xdg_wm_base_add_listener(shared->wmBase, &sharedWmBaseListener, shared);
  } else if (std::strcmp(interface, wl_seat_interface.name) == 0) {
    shared->seat = static_cast<wl_seat*>(
        wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 7u)));
    wl_seat_add_listener(shared->seat, &sharedSeatListener, shared);
  } else if (std::strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
    shared->decorationManagerVersion = std::min(version, 2u);
    shared->decorationManager = static_cast<zxdg_decoration_manager_v1*>(
        wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, shared->decorationManagerVersion));
  } else if (std::strcmp(interface, xx_cutouts_manager_v1_interface.name) == 0) {
    shared->cutoutsManager = static_cast<xx_cutouts_manager_v1*>(
        wl_registry_bind(registry, name, &xx_cutouts_manager_v1_interface, 1));
  } else if (std::strcmp(interface, wp_viewporter_interface.name) == 0) {
    shared->viewporter = static_cast<wp_viewporter*>(
        wl_registry_bind(registry, name, &wp_viewporter_interface, 1));
	  } else if (std::strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0) {
	    shared->fractionalScaleManager = static_cast<wp_fractional_scale_manager_v1*>(
	        wl_registry_bind(registry, name, &wp_fractional_scale_manager_v1_interface, 1));
	  } else if (std::strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
	    shared->layerShell = static_cast<zwlr_layer_shell_v1*>(
	        wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, std::min(version, 4u)));
	  } else if (std::strcmp(interface, ext_background_effect_manager_v1_interface.name) == 0) {
	    shared->backgroundEffectManager = static_cast<ext_background_effect_manager_v1*>(
	        wl_registry_bind(registry, name, &ext_background_effect_manager_v1_interface, std::min(version, 4u)));
	  } else if (std::strcmp(interface, wl_output_interface.name) == 0) {
    auto output = std::make_unique<SharedWaylandConnection::Output>();
    output->name = name;
    output->output = static_cast<wl_output*>(
        wl_registry_bind(registry, name, &wl_output_interface, std::min(version, 4u)));
    wl_output_add_listener(output->output, &sharedOutputListener, output.get());
    shared->outputs.push_back(std::move(output));
  }
}

void sharedRegistryRemove(void* data, wl_registry*, std::uint32_t name) {
  auto* shared = static_cast<SharedWaylandConnection*>(data);
  auto it = std::find_if(shared->outputs.begin(), shared->outputs.end(),
                         [&](auto const& output) { return output->name == name; });
  if (it == shared->outputs.end()) return;
  wl_output* removed = (*it)->output;
  for (WaylandWindow* window : shared->windows) {
    if (window) window->handleOutputRemoved(removed);
  }
  if ((*it)->output) wl_output_destroy((*it)->output);
  shared->outputs.erase(it);
}

void sharedWmPing(void*, xdg_wm_base* base, std::uint32_t serial) {
  xdg_wm_base_pong(base, serial);
}

void sharedSeatCapabilities(void* data, wl_seat* seat, std::uint32_t caps) {
  auto* shared = static_cast<SharedWaylandConnection*>(data);
  if ((caps & WL_SEAT_CAPABILITY_POINTER) && !shared->pointer) {
    shared->pointer = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(shared->pointer, &sharedPointerListener, shared);
  }
  if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !shared->keyboard) {
    shared->keyboard = wl_seat_get_keyboard(seat);
    wl_keyboard_add_listener(shared->keyboard, &sharedKeyboardListener, shared);
  }
  ensureClipboardDataDevice(shared);
}

void sharedSeatName(void*, wl_seat*, char const*) {}
void sharedOutputGeometry(void*, wl_output*, std::int32_t, std::int32_t, std::int32_t, std::int32_t,
                          std::int32_t, char const*, char const*, std::int32_t) {}
void sharedOutputMode(void*, wl_output*, std::uint32_t, std::int32_t, std::int32_t, std::int32_t) {}
void sharedOutputDone(void*, wl_output*) {}
void sharedOutputScale(void* data, wl_output* output, std::int32_t scale) {
  auto* sharedOutput = static_cast<SharedWaylandConnection::Output*>(data);
  sharedOutput->scale = safeScale(static_cast<float>(std::max(1, scale)));
  refreshWindowsForOutput(&gWaylandConnection, output);
}
void sharedOutputName(void* data, wl_output*, char const* name) {
  auto* sharedOutput = static_cast<SharedWaylandConnection::Output*>(data);
  sharedOutput->displayName = name ? name : "";
}
void sharedOutputDescription(void*, wl_output*, char const*) {}

void sharedPointerEnter(void* data, wl_pointer*, std::uint32_t serial, wl_surface* surface, wl_fixed_t x, wl_fixed_t y) {
  auto* shared = static_cast<SharedWaylandConnection*>(data);
  shared->popupPointerFocus = windowForPopupSurface(shared, surface);
  if (shared->popupPointerFocus) {
    shared->pointerFocus = nullptr;
    shared->popupPointerSurface = surface;
    shared->popupPointerFocus->handlePopupPointerEnter(serial, x, y);
    return;
  }
  shared->popupPointerSurface = nullptr;
  shared->pointerFocus = windowForSurface(shared, surface);
  if (shared->pointerFocus) shared->pointerFocus->handlePointerEnter(serial, x, y);
}
void sharedPointerLeave(void* data, wl_pointer*, std::uint32_t, wl_surface* surface) {
  auto* shared = static_cast<SharedWaylandConnection*>(data);
  WaylandWindow* popupWindow = windowForPopupSurface(shared, surface);
  if (popupWindow) popupWindow->handlePopupPointerLeave();
  if (!surface || shared->popupPointerFocus == popupWindow) {
    shared->popupPointerFocus = nullptr;
    shared->popupPointerSurface = nullptr;
  }
  if (popupWindow) return;
  WaylandWindow* window = windowForSurface(shared, surface);
  if (window) window->handlePointerLeave();
  if (!surface || shared->pointerFocus == window) shared->pointerFocus = nullptr;
}
void sharedPointerMotion(void* data, wl_pointer*, std::uint32_t, wl_fixed_t x, wl_fixed_t y) {
  auto* shared = static_cast<SharedWaylandConnection*>(data);
  if (shared->popupPointerFocus) {
    shared->popupPointerFocus->handlePopupPointerMotion(x, y);
    return;
  }
  if (shared->pointerFocus) shared->pointerFocus->handlePointerMotion(x, y);
}
void sharedPointerButton(void* data, wl_pointer*, std::uint32_t serial, std::uint32_t, std::uint32_t button,
                         std::uint32_t state) {
  auto* shared = static_cast<SharedWaylandConnection*>(data);
  if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
    noteSelectionSerial(shared, serial);
  }
  if (shared->popupPointerFocus) {
    shared->popupPointerFocus->handlePopupPointerButton(serial, button, state);
    return;
  }
  if (shared->pointerFocus) shared->pointerFocus->handlePointerButton(serial, button, state);
}
void sharedPointerAxis(void* data, wl_pointer*, std::uint32_t, std::uint32_t axis, wl_fixed_t value) {
  auto* shared = static_cast<SharedWaylandConnection*>(data);
  if (shared->popupPointerFocus) {
    shared->popupPointerFocus->handlePopupPointerAxis(axis, value);
    return;
  }
  if (shared->pointerFocus) shared->pointerFocus->handlePointerAxis(axis, value);
}
void sharedPointerFrame(void* data, wl_pointer*) {
  auto* shared = static_cast<SharedWaylandConnection*>(data);
  if (shared->popupPointerFocus) {
    shared->popupPointerFocus->handlePopupPointerFrame();
    return;
  }
  if (shared->pointerFocus) shared->pointerFocus->handlePointerFrame();
}
void sharedPointerAxisSource(void*, wl_pointer*, std::uint32_t) {}
void sharedPointerAxisStop(void*, wl_pointer*, std::uint32_t, std::uint32_t) {}
void sharedPointerAxisDiscrete(void*, wl_pointer*, std::uint32_t, std::int32_t) {}
void sharedPointerAxisValue120(void*, wl_pointer*, std::uint32_t, std::int32_t) {}
void sharedPointerAxisRelativeDirection(void*, wl_pointer*, std::uint32_t, std::uint32_t) {}

void sharedKeymap(void* data, wl_keyboard*, std::uint32_t format, int fd, std::uint32_t size) {
  auto* shared = static_cast<SharedWaylandConnection*>(data);
  stopKeyboardRepeat(shared);
  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
    close(fd);
    return;
  }
  if (shared->xkb) shared->xkb->loadKeymapFromFd(fd, size);
  else close(fd);
}

void sharedKeyboardEnter(void* data, wl_keyboard*, std::uint32_t serial, wl_surface* surface, wl_array*) {
  auto* shared = static_cast<SharedWaylandConnection*>(data);
  noteSelectionSerial(shared, serial);
  WaylandWindow* previousFocus = shared->keyboardFocus;
  WaylandWindow* window = windowForSurface(shared, surface);
  if (!window) {
    window = windowForPopupSurface(shared, surface);
  }
  shared->keyboardFocus = window;
  shared->keyboardSurface = window ? surface : nullptr;
  if (window && window != previousFocus) {
    Application::instance().eventQueue().post(WindowEvent{WindowEvent::Kind::FocusGained,
                                                          window->handle()});
  }
}
void sharedKeyboardLeave(void* data, wl_keyboard*, std::uint32_t, wl_surface* surface) {
  auto* shared = static_cast<SharedWaylandConnection*>(data);
  WaylandWindow* window = windowForSurface(shared, surface);
  if (!window) {
    window = windowForPopupSurface(shared, surface);
  }
  if (!surface || shared->keyboardFocus == window) {
    WaylandWindow* previousFocus = shared->keyboardFocus;
    stopKeyboardRepeat(shared);
    shared->keyboardFocus = nullptr;
    shared->keyboardSurface = nullptr;
    if (previousFocus) {
      Application::instance().eventQueue().post(WindowEvent{WindowEvent::Kind::FocusLost,
                                                            previousFocus->handle()});
    }
  }
}
void sharedKeyboardKey(void* data, wl_keyboard*, std::uint32_t serial, std::uint32_t, std::uint32_t key,
                       std::uint32_t state) {
  auto* shared = static_cast<SharedWaylandConnection*>(data);
  noteSelectionSerial(shared, serial);
  if (state == WL_KEYBOARD_KEY_STATE_RELEASED && shared->repeatKey == key) {
    stopKeyboardRepeat(shared);
  }
  if (shared->keyboardFocus) {
    shared->keyboardFocus->handleKeyboardKeyForSurface(shared->keyboardSurface, shared->xkb.get(), key, state, serial);
    if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
      startKeyboardRepeat(shared, shared->keyboardFocus, shared->keyboardSurface, key);
    }
  }
}
void sharedKeyboardModifiers(void* data, wl_keyboard*, std::uint32_t, std::uint32_t depressed,
                             std::uint32_t latched, std::uint32_t locked, std::uint32_t group) {
  auto* shared = static_cast<SharedWaylandConnection*>(data);
  if (shared->keyboardFocus) {
    shared->keyboardFocus->handleKeyboardModifiers(shared->xkb.get(), depressed, latched, locked, group);
  }
}
void sharedKeyboardRepeatInfo(void* data, wl_keyboard*, std::int32_t rate, std::int32_t delay) {
  auto* shared = static_cast<SharedWaylandConnection*>(data);
  if (!shared) {
    return;
  }
  int const newRate = std::max(0, rate);
  int const newDelay = std::max(0, delay);
  bool const changed = shared->keyboardRepeatRate != newRate || shared->keyboardRepeatDelayMs != newDelay;
  shared->keyboardRepeatRate = newRate;
  shared->keyboardRepeatDelayMs = newDelay;
  if (changed || shared->keyboardRepeatRate == 0 || shared->keyboardRepeatDelayMs == 0) {
    stopKeyboardRepeat(shared);
  }
}

} // namespace

namespace linux_platform {

std::vector<std::string> availableWaylandOutputs() {
  SharedWaylandConnection* shared = nullptr;
  try {
    shared = acquireWaylandConnection();
    if (wl_display_roundtrip(shared->display) < 0) {
      int const error = wl_display_get_error(shared->display);
      markWaylandConnectionDead(shared, "output enumeration roundtrip", error == 0 ? errno : error);
      releaseWaylandConnection();
      return {};
    }

    std::vector<std::string> outputs;
    outputs.reserve(shared->outputs.size());
    for (auto const& output : shared->outputs) {
      if (!output->displayName.empty()) {
        outputs.push_back(output->displayName);
      } else {
        outputs.push_back(std::to_string(output->name));
      }
    }
    releaseWaylandConnection();
    return outputs;
  } catch (...) {
    if (shared) releaseWaylandConnection();
    return {};
  }
}

} // namespace linux_platform

namespace platform {

std::unique_ptr<Window> createWindow(WindowConfig const& config) {
  return std::make_unique<WaylandWindow>(config);
}

} // namespace platform
} // namespace lambda
