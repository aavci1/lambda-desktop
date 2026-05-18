#include "UI/Platform/WindowFactory.hpp"
#include "UI/Platform/Application.hpp"
#include "Platform/Linux/Common/XkbState.hpp"
#include "Platform/Linux/WaylandNativeSurface.hpp"
#include "Platform/Linux/WaylandOutputs.hpp"

#include <Flux/UI/Application.hpp>
#include <Flux/UI/EventQueue.hpp>
#include <Flux/UI/Events.hpp>
#include <Flux/UI/KeyCodes.hpp>
#include <Flux/UI/Window.hpp>

#include "Graphics/Vulkan/VulkanCanvas.hpp"

#include "Detail/ResizeTrace.hpp"
#include "viewporter-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <linux/input-event-codes.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace flux {

class WaylandWindow;

namespace {

std::atomic<unsigned int> gNextHandle{1};
std::int64_t nowNanos() {
  using namespace std::chrono;
  return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
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
  char const* value = std::getenv("FLUX_DEBUG_WAYLAND_DECORATIONS");
  return value && *value && std::strcmp(value, "0") != 0;
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
  xdg_wm_base* wmBase = nullptr;
  zxdg_decoration_manager_v1* decorationManager = nullptr;
  wl_seat* seat = nullptr;
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
  WaylandWindow* keyboardFocus = nullptr;
  unsigned int refs = 0;
};

std::mutex gWaylandConnectionMutex;
SharedWaylandConnection gWaylandConnection;

void sharedRegistryGlobal(void* data, wl_registry* registry, std::uint32_t name,
                          char const* interface, std::uint32_t version);
void sharedRegistryRemove(void* data, wl_registry*, std::uint32_t name);
void sharedWmPing(void*, xdg_wm_base* base, std::uint32_t serial);
void sharedSeatCapabilities(void* data, wl_seat* seat, std::uint32_t caps);
void sharedSeatName(void*, wl_seat*, char const*);
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

wl_registry_listener const sharedRegistryListener{sharedRegistryGlobal, sharedRegistryRemove};
xdg_wm_base_listener const sharedWmBaseListener{sharedWmPing};
wl_seat_listener const sharedSeatListener{sharedSeatCapabilities, sharedSeatName};
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

SharedWaylandConnection* acquireWaylandConnection() {
  std::lock_guard lock(gWaylandConnectionMutex);
  if (!gWaylandConnection.display) {
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
    wl_display_roundtrip(gWaylandConnection.display);
    if (!gWaylandConnection.compositor || !gWaylandConnection.wmBase) {
      throw std::runtime_error("Wayland compositor does not expose required xdg-shell globals");
    }
  }
  ++gWaylandConnection.refs;
  return &gWaylandConnection;
}

void releaseWaylandConnection() {
  std::lock_guard lock(gWaylandConnectionMutex);
  if (gWaylandConnection.refs == 0) return;
  --gWaylandConnection.refs;
  if (gWaylandConnection.refs != 0) return;
  if (gWaylandConnection.keyboard) {
    wl_keyboard_destroy(gWaylandConnection.keyboard);
    gWaylandConnection.keyboard = nullptr;
  }
  if (gWaylandConnection.pointer) {
    wl_pointer_destroy(gWaylandConnection.pointer);
    gWaylandConnection.pointer = nullptr;
  }
  if (gWaylandConnection.cursorSurface) {
    wl_surface_destroy(gWaylandConnection.cursorSurface);
    gWaylandConnection.cursorSurface = nullptr;
  }
  if (gWaylandConnection.cursorTheme) {
    wl_cursor_theme_destroy(gWaylandConnection.cursorTheme);
    gWaylandConnection.cursorTheme = nullptr;
  }
  if (gWaylandConnection.seat) {
    wl_seat_destroy(gWaylandConnection.seat);
    gWaylandConnection.seat = nullptr;
  }
  for (auto& output : gWaylandConnection.outputs) {
    if (output->output) wl_output_destroy(output->output);
  }
  gWaylandConnection.outputs.clear();
  if (gWaylandConnection.decorationManager) {
    zxdg_decoration_manager_v1_destroy(gWaylandConnection.decorationManager);
    gWaylandConnection.decorationManager = nullptr;
  }
  if (gWaylandConnection.viewporter) {
    wp_viewporter_destroy(gWaylandConnection.viewporter);
    gWaylandConnection.viewporter = nullptr;
  }
  if (gWaylandConnection.wmBase) {
    xdg_wm_base_destroy(gWaylandConnection.wmBase);
    gWaylandConnection.wmBase = nullptr;
  }
  if (gWaylandConnection.compositor) {
    wl_compositor_destroy(gWaylandConnection.compositor);
    gWaylandConnection.compositor = nullptr;
  }
  if (gWaylandConnection.shm) {
    wl_shm_destroy(gWaylandConnection.shm);
    gWaylandConnection.shm = nullptr;
  }
  if (gWaylandConnection.registry) {
    wl_registry_destroy(gWaylandConnection.registry);
    gWaylandConnection.registry = nullptr;
  }
  gWaylandConnection.xkb.reset();
  if (gWaylandConnection.display) {
    wl_display_disconnect(gWaylandConnection.display);
    gWaylandConnection.display = nullptr;
  }
}

} // namespace

class WaylandWindow final : public platform::Window {
public:
  explicit WaylandWindow(WindowConfig const& config)
      : handle_(gNextHandle.fetch_add(1)), size_(config.size), title_(config.title),
        fullscreen_(config.fullscreen) {
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
    xdgSurface_ = xdg_wm_base_get_xdg_surface(shared->wmBase, surface_);
    xdg_surface_add_listener(xdgSurface_, &xdgSurfaceListener_, this);
    toplevel_ = xdg_surface_get_toplevel(xdgSurface_);
    xdg_toplevel_add_listener(toplevel_, &toplevelListener_, this);
    xdg_toplevel_set_title(toplevel_, title_.c_str());
    appId_ = Application::instance().name();
    xdg_toplevel_set_app_id(toplevel_, appId_.c_str());
    if (config.minSize.width > 0.f || config.minSize.height > 0.f) setMinSize(config.minSize);
    if (config.maxSize.width > 0.f || config.maxSize.height > 0.f) setMaxSize(config.maxSize);
    requestServerSideDecorations();
    if (fullscreen_) xdg_toplevel_set_fullscreen(toplevel_, nullptr);
    wl_surface_commit(surface_);
    while (!configured_) {
      if (wl_display_dispatch(display_) < 0) {
        throw std::runtime_error("Wayland initial configure failed");
      }
    }
  }

  ~WaylandWindow() override {
    if (shared_) {
      shared_->windows.erase(std::remove(shared_->windows.begin(), shared_->windows.end(), this),
                             shared_->windows.end());
      if (shared_->pointerFocus == this) shared_->pointerFocus = nullptr;
      if (shared_->keyboardFocus == this) shared_->keyboardFocus = nullptr;
    }
    if (frameCallback_) wl_callback_destroy(frameCallback_);
    if (decoration_) zxdg_toplevel_decoration_v1_destroy(decoration_);
    if (toplevel_) xdg_toplevel_destroy(toplevel_);
    if (xdgSurface_) xdg_surface_destroy(xdgSurface_);
    if (viewport_) wp_viewport_destroy(viewport_);
    if (surface_) wl_surface_destroy(surface_);
    if (shared_) releaseWaylandConnection();
    if (wakePipe_[0] >= 0) close(wakePipe_[0]);
    if (wakePipe_[1] >= 0) close(wakePipe_[1]);
  }

  void setFluxWindow(::flux::Window* window) override { fluxWindow_ = window; }

  void show() override {
    updateCanvasDpi();
    Application::instance().requestWindowRedraw(handle_);
    Application::instance().flushRedraw();
  }

  std::unique_ptr<Canvas> createCanvas(::flux::Window&) override {
    nativeSurface_ = WaylandNativeSurface{display_, surface_};
    configureVulkanCanvasRuntime(Application::instance().platformApp().requiredVulkanInstanceExtensions(),
                                 Application::instance().cacheDir());
    VkInstance instance = ensureSharedVulkanInstance();
    VkSurfaceKHR surface = Application::instance().platformApp().createVulkanSurface(instance, &nativeSurface_);
    auto canvas = createVulkanCanvas(surface, handle_, Application::instance().textSystem());
    canvas->updateDpiScale(dpiScaleX_, dpiScaleY_);
    canvas->resize(static_cast<int>(std::lround(size_.width)), static_cast<int>(std::lround(size_.height)));
    canvas_ = canvas.get();
    return canvas;
  }

  void resize(Size const& newSize) override {
    size_ = newSize;
    if (fluxWindow_) fluxWindow_->updateCanvasDpiScale(dpiScaleX_, dpiScaleY_);
    if (canvas_) canvas_->resize(static_cast<int>(std::lround(size_.width)),
                                 static_cast<int>(std::lround(size_.height)));
    updateViewportDestination();
    queueResizeEvent();
    applyCursor(currentCursor_);
    requestResizeRedraw();
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

  Size currentSize() const override { return size_; }
  bool isFullscreen() const override { return fullscreen_; }
  unsigned int handle() const override { return handle_; }
  void* nativeGraphicsSurface() const override { return const_cast<WaylandNativeSurface*>(&nativeSurface_); }

  void processEvents() override {
    drainWakePipe();
    dispatchReadyEvents(0);
    flushDeferredRedraw();
  }

  void waitForEvents(int timeoutMs) override {
    dispatchReadyEvents(timeoutMs);
    flushDeferredRedraw();
  }

  void wakeEventLoop() override {
    char const c = 1;
    (void)write(wakePipe_[1], &c, 1);
  }
  int eventFd() const override { return display_ ? wl_display_get_fd(display_) : -1; }
  int wakeFd() const override { return wakePipe_[0]; }

  void requestAnimationFrame() override {
    if (framePending_ || !surface_) return;
    framePending_ = true;
    if (detail::resizeTraceEnabled()) {
      detail::resizeTrace("wayland-window", "request-frame window=%u size=%dx%d\n",
                   handle_, static_cast<int>(std::lround(size_.width)),
                   static_cast<int>(std::lround(size_.height)));
    }
    frameCallback_ = wl_surface_frame(surface_);
    wl_callback_add_listener(frameCallback_, &frameCallbackListener_, this);
    wl_surface_commit(surface_);
    wl_display_flush(display_);
  }

  void acknowledgeAnimationFrameTick() override {
    framePending_ = false;
  }

  void completeAnimationFrame(bool needsAnotherFrame) override {
    if (detail::resizeTraceEnabled()) {
      detail::resizeTrace("wayland-window", "complete-frame window=%u needsAnother=%d\n",
                   handle_, needsAnotherFrame ? 1 : 0);
    }
    wl_display_flush(display_);
    if (needsAnotherFrame) requestAnimationFrame();
  }

  wl_surface* waylandSurface() const noexcept { return surface_; }

  void handlePointerEnter(std::uint32_t serial, wl_fixed_t x, wl_fixed_t y) {
    pointerEnterSerial_ = serial;
    pointerPos_ = logicalPointFromFixed(x, y, dpiScaleX_, dpiScaleY_);
    applyCursor(currentCursor_);
  }

  void handlePointerLeave() {
    pressedButtons_ = 0;
  }

  void handlePointerMotion(wl_fixed_t x, wl_fixed_t y) {
    pointerPos_ = logicalPointFromFixed(x, y, dpiScaleX_, dpiScaleY_);
    Application::instance().eventQueue().post(InputEvent{.kind = InputEvent::Kind::PointerMove,
                                                         .handle = handle_,
                                                         .position = pointerPos_,
                                                         .pressedButtons = pressedButtons_});
  }

  void handlePointerButton(std::uint32_t button, std::uint32_t state) {
    std::uint8_t const bit = button == BTN_LEFT ? 1u : button == BTN_RIGHT ? 2u : button == BTN_MIDDLE ? 4u : 0u;
    if (state == WL_POINTER_BUTTON_STATE_PRESSED) pressedButtons_ |= bit;
    else pressedButtons_ &= static_cast<std::uint8_t>(~bit);
    Application::instance().eventQueue().post(InputEvent{.kind = state == WL_POINTER_BUTTON_STATE_PRESSED
                                                                     ? InputEvent::Kind::PointerDown
                                                                     : InputEvent::Kind::PointerUp,
                                                         .handle = handle_,
                                                         .position = pointerPos_,
                                                         .button = mouseButtonFromLinux(button),
                                                         .pressedButtons = pressedButtons_});
  }

  void handlePointerAxis(std::uint32_t axis, wl_fixed_t value) {
    float dx = 0.f, dy = 0.f;
    float const v = static_cast<float>(wl_fixed_to_double(value));
    if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL) dx = v;
    else dy = v;
    Application::instance().eventQueue().post(InputEvent{.kind = InputEvent::Kind::Scroll,
                                                         .handle = handle_,
                                                         .position = pointerPos_,
                                                         .scrollDelta = {dx, dy},
                                                         .preciseScrollDelta = true,
                                                         .pressedButtons = pressedButtons_});
  }

  void handleKeyboardKey(linux_platform::XkbState* xkb, std::uint32_t key, std::uint32_t state) {
    bool const pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;
    Application::instance().eventQueue().post(InputEvent{.kind = pressed ? InputEvent::Kind::KeyDown
                                                                          : InputEvent::Kind::KeyUp,
                                                         .handle = handle_,
                                                         .key = xkb ? xkb->keyCodeForEvdevKey(key) : KeyCode{0},
                                                         .modifiers = currentModifiers_});
    if (pressed) {
      std::string text = xkb ? xkb->utf8ForEvdevKey(key) : std::string{};
      if (!text.empty()) {
        Application::instance().eventQueue().post(InputEvent{.kind = InputEvent::Kind::TextInput,
                                                             .handle = handle_,
                                                             .text = std::move(text)});
      }
    }
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
  static void frameDone(void* data, wl_callback* callback, std::uint32_t) {
    auto* self = static_cast<WaylandWindow*>(data);
    if (callback == self->frameCallback_) {
      wl_callback_destroy(self->frameCallback_);
      self->frameCallback_ = nullptr;
    } else {
      wl_callback_destroy(callback);
    }
    if (!self->framePending_) return;
    self->framePending_ = false;
    if (detail::resizeTraceEnabled()) {
      detail::resizeTrace("wayland-window", "frame-done window=%u size=%dx%d\n",
                   self->handle_, static_cast<int>(std::lround(self->size_.width)),
                   static_cast<int>(std::lround(self->size_.height)));
    }
    auto& queue = Application::instance().eventQueue();
    queue.post(FrameEvent{nowNanos(), self->handle_});
    queue.dispatch();
    self->wakeEventLoop();
  }

  static void xdgConfigure(void* data, xdg_surface* surface, std::uint32_t serial) {
    auto* self = static_cast<WaylandWindow*>(data);
    xdg_surface_ack_configure(surface, serial);
    self->configured_ = true;
    if (detail::resizeTraceEnabled()) {
      detail::resizeTrace("wayland-window", "xdg-configure window=%u serial=%u pending=%dx%d\n",
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
        detail::resizeTrace("wayland-window", "toplevel-configure window=%u size=%dx%d\n",
                     self->handle_, width, height);
      }
    }
  }

  static void topClose(void* data, xdg_toplevel*) {
    auto* self = static_cast<WaylandWindow*>(data);
    Application::instance().eventQueue().post(WindowEvent{WindowEvent::Kind::CloseRequest, self->handle_});
  }
  static void topConfigureBounds(void*, xdg_toplevel*, std::int32_t, std::int32_t) {}
  static void topCapabilities(void*, xdg_toplevel*, wl_array*) {}

  static void decorationConfigure(void* data, zxdg_toplevel_decoration_v1*, std::uint32_t mode) {
    auto* self = static_cast<WaylandWindow*>(data);
    self->serverSideDecorationsActive_ = mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
    if (self->serverSideDecorationsActive_) {
      if (debugDecorations() && !self->loggedDecorationMode_) {
        self->loggedDecorationMode_ = true;
        std::fprintf(stderr, "Flux Wayland: compositor accepted server-side decorations.\n");
      }
    } else if (!self->warnedDecorationFallback_) {
      self->warnedDecorationFallback_ = true;
      std::fprintf(stderr, "Flux Wayland: compositor refused server-side decorations; resize chrome may be absent.\n");
    }
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
    factor = std::max(1, factor);
    self->dpiScaleX_ = safeScale(static_cast<float>(factor));
    self->dpiScaleY_ = self->dpiScaleX_;
    wl_surface_set_buffer_scale(self->surface_, factor);
    self->updateViewportDestination();
    if (self->fluxWindow_) self->fluxWindow_->updateCanvasDpiScale(self->dpiScaleX_, self->dpiScaleY_);
    Application::instance().eventQueue().post(WindowEvent{.kind = WindowEvent::Kind::DpiChanged,
                                                          .handle = self->handle_,
                                                          .dpi = self->dpiScaleX_,
                                                          .dpiX = self->dpiScaleX_,
                                                          .dpiY = self->dpiScaleY_});
    self->queueResizeEvent();
    self->applyCursor(self->currentCursor_);
    self->requestResizeRedraw();
  }
  static void surfacePreferredBufferTransform(void*, wl_surface*, std::uint32_t) {}

  void applyConfiguredSize(int width, int height) {
    auto const start = std::chrono::steady_clock::now();
    size_ = {static_cast<float>(std::max(1, width)), static_cast<float>(std::max(1, height))};
    if (canvas_) canvas_->resize(static_cast<int>(std::lround(size_.width)),
                                 static_cast<int>(std::lround(size_.height)));
    updateViewportDestination();
    if (fluxWindow_) fluxWindow_->updateCanvasDpiScale(dpiScaleX_, dpiScaleY_);
    queueResizeEvent();
    applyCursor(currentCursor_);
    requestResizeRedraw();
    if (detail::resizeTraceEnabled()) {
      auto const elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start).count();
      detail::resizeTrace("wayland-window", 
          "apply-configure window=%u size=%dx%d framePending=%d elapsed=%.3fms\n",
          handle_, width, height, framePending_ ? 1 : 0,
          static_cast<double>(elapsed) / 1000.0);
    }
  }

  void updateCanvasDpi() {
    if (fluxWindow_) fluxWindow_->updateCanvasDpiScale(dpiScaleX_, dpiScaleY_);
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
    if (!shared_ || !shared_->pointer || pointerEnterSerial_ == 0) {
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
    wl_display_flush(display_);
  }

  void requestServerSideDecorations() {
    if (!shared_ || !shared_->decorationManager) {
      if (!warnedDecorationFallback_) {
        warnedDecorationFallback_ = true;
        std::fprintf(stderr, "Flux Wayland: compositor does not expose xdg-decoration; server-side decorations are unavailable.\n");
      }
      return;
    }
    decoration_ = zxdg_decoration_manager_v1_get_toplevel_decoration(shared_->decorationManager, toplevel_);
    zxdg_toplevel_decoration_v1_add_listener(decoration_, &decorationListener_, this);
    zxdg_toplevel_decoration_v1_set_mode(decoration_, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
  }

  float outputScale(wl_output* output) const {
    if (!shared_) return 1.f;
    for (auto const& candidate : shared_->outputs) {
      if (candidate->output == output) return candidate->scale;
    }
    return 1.f;
  }

  void updateEnteredScale() {
    float scale = 1.f;
    for (wl_output* output : enteredOutputs_) {
      scale = std::max(scale, outputScale(output));
    }
    if (std::abs(scale - dpiScaleX_) < 0.001f && std::abs(scale - dpiScaleY_) < 0.001f) return;
    dpiScaleX_ = scale;
    dpiScaleY_ = scale;
    wl_surface_set_buffer_scale(surface_, static_cast<std::int32_t>(std::max(1.f, std::round(scale))));
    updateViewportDestination();
    if (fluxWindow_) fluxWindow_->updateCanvasDpiScale(dpiScaleX_, dpiScaleY_);
    Application::instance().eventQueue().post(WindowEvent{.kind = WindowEvent::Kind::DpiChanged,
                                                          .handle = handle_,
                                                          .dpi = dpiScaleX_,
                                                          .dpiX = dpiScaleX_,
                                                          .dpiY = dpiScaleY_});
    queueResizeEvent();
    requestResizeRedraw();
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
      detail::resizeTrace("wayland-window", "request-resize-redraw window=%u framePending=%d\n",
                   handle_, framePending_ ? 1 : 0);
    }
  }

  void drainWakePipe() {
    char buffer[64];
    while (read(wakePipe_[0], buffer, sizeof(buffer)) > 0) {}
  }

  void dispatchReadyEvents(int timeoutMs) {
    while (wl_display_prepare_read(display_) != 0) {
      wl_display_dispatch_pending(display_);
    }
    wl_display_flush(display_);

    pollfd fds[2]{{wl_display_get_fd(display_), POLLIN, 0}, {wakePipe_[0], POLLIN, 0}};
    int const rc = poll(fds, 2, timeoutMs < 0 ? -1 : timeoutMs);
    if (rc > 0 && (fds[1].revents & POLLIN)) {
      drainWakePipe();
    }
    if (rc > 0 && (fds[0].revents & POLLIN)) {
      wl_display_read_events(display_);
    } else {
      wl_display_cancel_read(display_);
    }
    wl_display_dispatch_pending(display_);
  }

  void flushDeferredRedraw() {
    if (!resizeRedrawPending_ && !pendingResizeEvent_) return;
    auto const start = std::chrono::steady_clock::now();
    resizeRedrawPending_ = false;
    if (pendingResizeEvent_) {
      pendingResizeEvent_ = false;
      Application::instance().eventQueue().post(WindowEvent{WindowEvent::Kind::Resize, handle_, pendingResizeSize_});
    }
    Application::instance().eventQueue().dispatch();
    if (detail::resizeTraceEnabled()) {
      auto const elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start).count();
      detail::resizeTrace("wayland-window", 
          "flush-deferred window=%u size=%.0fx%.0f framePending=%d elapsed=%.3fms\n",
          handle_, pendingResizeSize_.width, pendingResizeSize_.height, framePending_ ? 1 : 0,
          static_cast<double>(elapsed) / 1000.0);
    }
  }

  static inline wl_callback_listener frameCallbackListener_{frameDone};
  static inline wl_surface_listener surfaceListener_{surfaceEnter, surfaceLeave, surfacePreferredBufferScale,
                                                    surfacePreferredBufferTransform};
  static inline xdg_surface_listener xdgSurfaceListener_{xdgConfigure};
  static inline xdg_toplevel_listener toplevelListener_{topConfigure, topClose, topConfigureBounds,
                                                       topCapabilities};
  static inline zxdg_toplevel_decoration_v1_listener decorationListener_{decorationConfigure};

  wl_display* display_ = nullptr;
  std::vector<wl_output*> enteredOutputs_;
  wl_surface* surface_ = nullptr;
  WaylandNativeSurface nativeSurface_{};
  wl_callback* frameCallback_ = nullptr;
  xdg_surface* xdgSurface_ = nullptr;
  xdg_toplevel* toplevel_ = nullptr;
  zxdg_toplevel_decoration_v1* decoration_ = nullptr;
  wp_viewport* viewport_ = nullptr;
  Canvas* canvas_ = nullptr;
  SharedWaylandConnection* shared_ = nullptr;

  ::flux::Window* fluxWindow_ = nullptr;
  unsigned int handle_ = 0;
  Size size_{};
  std::string title_;
  std::string appId_;
  float dpiScaleX_ = 1.f;
  float dpiScaleY_ = 1.f;
  bool fullscreen_ = false;
  bool configured_ = false;
  bool serverSideDecorationsActive_ = false;
  bool warnedDecorationFallback_ = false;
  bool loggedDecorationMode_ = false;
  int pendingWidth_ = 0;
  int pendingHeight_ = 0;
  Point pointerPos_{};
  std::uint32_t pointerEnterSerial_ = 0;
  Cursor currentCursor_ = Cursor::Arrow;
  std::uint8_t pressedButtons_ = 0;
  Modifiers currentModifiers_ = Modifiers::None;
  bool resizeRedrawPending_ = false;
  bool pendingResizeEvent_ = false;
  Size pendingResizeSize_{};
  bool framePending_ = false;
  int wakePipe_[2]{-1, -1};
};

namespace {

WaylandWindow* windowForSurface(SharedWaylandConnection* shared, wl_surface* surface) {
  if (!shared || !surface) return nullptr;
  for (WaylandWindow* window : shared->windows) {
    if (window && window->waylandSurface() == surface) return window;
  }
  return nullptr;
}

void refreshWindowsForOutput(SharedWaylandConnection* shared, wl_output* output) {
  if (!shared) return;
  for (WaylandWindow* window : shared->windows) {
    if (window) window->handleOutputScaleChanged(output);
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
  } else if (std::strcmp(interface, xdg_wm_base_interface.name) == 0) {
    shared->wmBase = static_cast<xdg_wm_base*>(
        wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
    xdg_wm_base_add_listener(shared->wmBase, &sharedWmBaseListener, shared);
  } else if (std::strcmp(interface, wl_seat_interface.name) == 0) {
    shared->seat = static_cast<wl_seat*>(
        wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 7u)));
    wl_seat_add_listener(shared->seat, &sharedSeatListener, shared);
  } else if (std::strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
    shared->decorationManager = static_cast<zxdg_decoration_manager_v1*>(
        wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1));
  } else if (std::strcmp(interface, wp_viewporter_interface.name) == 0) {
    shared->viewporter = static_cast<wp_viewporter*>(
        wl_registry_bind(registry, name, &wp_viewporter_interface, 1));
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
  shared->pointerFocus = windowForSurface(shared, surface);
  if (shared->pointerFocus) shared->pointerFocus->handlePointerEnter(serial, x, y);
}
void sharedPointerLeave(void* data, wl_pointer*, std::uint32_t, wl_surface* surface) {
  auto* shared = static_cast<SharedWaylandConnection*>(data);
  WaylandWindow* window = windowForSurface(shared, surface);
  if (window) window->handlePointerLeave();
  if (!surface || shared->pointerFocus == window) shared->pointerFocus = nullptr;
}
void sharedPointerMotion(void* data, wl_pointer*, std::uint32_t, wl_fixed_t x, wl_fixed_t y) {
  auto* shared = static_cast<SharedWaylandConnection*>(data);
  if (shared->pointerFocus) shared->pointerFocus->handlePointerMotion(x, y);
}
void sharedPointerButton(void* data, wl_pointer*, std::uint32_t, std::uint32_t, std::uint32_t button,
                         std::uint32_t state) {
  auto* shared = static_cast<SharedWaylandConnection*>(data);
  if (shared->pointerFocus) shared->pointerFocus->handlePointerButton(button, state);
}
void sharedPointerAxis(void* data, wl_pointer*, std::uint32_t, std::uint32_t axis, wl_fixed_t value) {
  auto* shared = static_cast<SharedWaylandConnection*>(data);
  if (shared->pointerFocus) shared->pointerFocus->handlePointerAxis(axis, value);
}
void sharedPointerFrame(void*, wl_pointer*) {}
void sharedPointerAxisSource(void*, wl_pointer*, std::uint32_t) {}
void sharedPointerAxisStop(void*, wl_pointer*, std::uint32_t, std::uint32_t) {}
void sharedPointerAxisDiscrete(void*, wl_pointer*, std::uint32_t, std::int32_t) {}
void sharedPointerAxisValue120(void*, wl_pointer*, std::uint32_t, std::int32_t) {}
void sharedPointerAxisRelativeDirection(void*, wl_pointer*, std::uint32_t, std::uint32_t) {}

void sharedKeymap(void* data, wl_keyboard*, std::uint32_t format, int fd, std::uint32_t size) {
  auto* shared = static_cast<SharedWaylandConnection*>(data);
  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
    close(fd);
    return;
  }
  if (shared->xkb) shared->xkb->loadKeymapFromFd(fd, size);
  else close(fd);
}

void sharedKeyboardEnter(void* data, wl_keyboard*, std::uint32_t, wl_surface* surface, wl_array*) {
  auto* shared = static_cast<SharedWaylandConnection*>(data);
  shared->keyboardFocus = windowForSurface(shared, surface);
}
void sharedKeyboardLeave(void* data, wl_keyboard*, std::uint32_t, wl_surface* surface) {
  auto* shared = static_cast<SharedWaylandConnection*>(data);
  WaylandWindow* window = windowForSurface(shared, surface);
  if (!surface || shared->keyboardFocus == window) shared->keyboardFocus = nullptr;
}
void sharedKeyboardKey(void* data, wl_keyboard*, std::uint32_t, std::uint32_t, std::uint32_t key,
                       std::uint32_t state) {
  auto* shared = static_cast<SharedWaylandConnection*>(data);
  if (shared->keyboardFocus) shared->keyboardFocus->handleKeyboardKey(shared->xkb.get(), key, state);
}
void sharedKeyboardModifiers(void* data, wl_keyboard*, std::uint32_t, std::uint32_t depressed,
                             std::uint32_t latched, std::uint32_t locked, std::uint32_t group) {
  auto* shared = static_cast<SharedWaylandConnection*>(data);
  if (shared->keyboardFocus) {
    shared->keyboardFocus->handleKeyboardModifiers(shared->xkb.get(), depressed, latched, locked, group);
  }
}
void sharedKeyboardRepeatInfo(void*, wl_keyboard*, std::int32_t, std::int32_t) {}

} // namespace

namespace linux_platform {

std::vector<std::string> availableWaylandOutputs() {
  SharedWaylandConnection* shared = nullptr;
  try {
    shared = acquireWaylandConnection();
    wl_display_roundtrip(shared->display);

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
} // namespace flux
