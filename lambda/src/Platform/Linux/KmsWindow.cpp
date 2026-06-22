#include "Platform/Linux/KmsPlatform.hpp"

#include <Lambda/UI/Application.hpp>
#include <Lambda/UI/EventQueue.hpp>
#include <Lambda/UI/Window.hpp>

#include "UI/Platform/Application.hpp"
#include "Graphics/Vulkan/VulkanCanvas.hpp"

#include <drm.h>
#include <drm_mode.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <xf86drm.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace lambda {
namespace {

std::atomic<unsigned int> gNextHandle{1};

std::int64_t nowNanos() {
  using namespace std::chrono;
  return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

std::chrono::nanoseconds frameInterval(drmModeModeInfo const& mode) {
  int refresh = mode.vrefresh > 0 ? mode.vrefresh : 60;
  return std::chrono::nanoseconds(1'000'000'000ll / refresh);
}

std::uint32_t refreshRateMilliHz(drmModeModeInfo const& mode) {
  if (mode.vrefresh > 0) return static_cast<std::uint32_t>(mode.vrefresh) * 1000u;
  if (mode.clock > 0 && mode.htotal > 0 && mode.vtotal > 0) {
    return static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(mode.clock) * 1'000'000ull) /
        (static_cast<std::uint64_t>(mode.htotal) * static_cast<std::uint64_t>(mode.vtotal)));
  }
  return 60'000u;
}

Size sizeForMode(drmModeModeInfo const& mode) {
  return Size(static_cast<float>(std::max(1, static_cast<int>(mode.hdisplay))),
              static_cast<float>(std::max(1, static_cast<int>(mode.vdisplay))));
}

drmModeModeInfo preferredMode(KmsConnector const& connector) {
  if (!connector.modes.empty()) {
    for (drmModeModeInfo const& mode : connector.modes) {
      if ((mode.type & DRM_MODE_TYPE_PREFERRED) != 0) return mode;
    }
    return connector.modes.front();
  }
  return connector.mode;
}

bool refreshMatches(drmModeModeInfo const& mode, int refreshHz) {
  if (refreshHz <= 0) return true;
  std::uint32_t const target = static_cast<std::uint32_t>(refreshHz) * 1000u;
  std::uint32_t const actual = refreshRateMilliHz(mode);
  std::uint32_t const delta = actual > target ? actual - target : target - actual;
  return delta <= 500u;
}

drmModeModeInfo selectMode(KmsConnector const& connector, int width, int height, int refreshHz) {
  if (width <= 0 && height <= 0) return preferredMode(connector);
  for (drmModeModeInfo const& mode : connector.modes) {
    if (mode.hdisplay == width && mode.vdisplay == height && refreshMatches(mode, refreshHz)) {
      return mode;
    }
  }
  std::fprintf(stderr,
               "[lambda:kms] requested mode %dx%d@%d for connector %s is unavailable; using preferred mode.\n",
               width, height, refreshHz, connector.name.c_str());
  return preferredMode(connector);
}

std::uint32_t cursorDimension(int fd, std::uint64_t cap) {
  std::uint64_t value = 0;
  if (drmGetCap(fd, cap, &value) == 0 && value >= 16 && value <= 256) {
    return static_cast<std::uint32_t>(value);
  }
  return 64;
}

std::uint32_t premulArgb(std::uint8_t a, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  auto premul = [a](std::uint8_t c) {
    return static_cast<std::uint8_t>((static_cast<unsigned int>(c) * static_cast<unsigned int>(a)) / 255u);
  };
  return (static_cast<std::uint32_t>(a) << 24u) |
         (static_cast<std::uint32_t>(premul(r)) << 16u) |
         (static_cast<std::uint32_t>(premul(g)) << 8u) |
         static_cast<std::uint32_t>(premul(b));
}

void drawArrowCursor(std::uint32_t* pixels, std::uint32_t width, std::uint32_t height) {
  std::memset(pixels, 0, static_cast<std::size_t>(width) * height * sizeof(std::uint32_t));
  std::uint32_t const black = premulArgb(255, 0, 0, 0);
  std::uint32_t const white = premulArgb(255, 255, 255, 255);
  int const cursorH = std::min<int>(static_cast<int>(height), 28);
  int const cursorW = std::min<int>(static_cast<int>(width), 20);
  auto put = [&](int x, int y, std::uint32_t color) {
    if (x >= 0 && y >= 0 && x < static_cast<int>(width) && y < static_cast<int>(height)) {
      pixels[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)] = color;
    }
  };
  for (int y = 0; y < cursorH; ++y) {
    int right = std::min(y / 2 + 2, cursorW - 1);
    for (int x = 0; x <= right; ++x) {
      put(x, y, white);
    }
  }
  for (int y = 0; y < cursorH; ++y) {
    int right = std::min(y / 2 + 2, cursorW - 1);
    put(0, y, black);
    put(right, y, black);
  }
  for (int x = 0; x < std::min(cursorW, 8); ++x) put(x, cursorH - 1, black);
  for (int y = 13; y < std::min<int>(cursorH, 26); ++y) {
    for (int x = 7; x < 12; ++x) put(x, y, white);
    put(7, y, black);
    put(12, y, black);
  }
}

} // namespace

KmsWindow::KmsWindow(KmsApplication& app, KmsConnector connector, WindowConfig const& config)
    : app_(app), connector_(std::move(connector)), handle_(gNextHandle.fetch_add(1)),
      title_(config.title) {
  requestedModeWidth_ = config.displayMode.width;
  requestedModeHeight_ = config.displayMode.height;
  requestedModeRefreshHz_ = config.displayMode.refreshHz;
  connector_.mode = selectMode(connector_, requestedModeWidth_, requestedModeHeight_, requestedModeRefreshHz_);
  size_ = sizeForMode(connector_.mode);
  frameTimerFd_ = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
  if (frameTimerFd_ < 0) throw std::runtime_error("timerfd_create failed for KMS frame pump");
  app_.registerWindow(this);
}

KmsWindow::~KmsWindow() {
  if (cursorVisible_ && app_.isVtForeground()) drmModeSetCursor(app_.drmFd(), connector_.crtcId, 0, 0, 0);
  destroyCursorBuffer();
  app_.unregisterWindow(this);
  if (frameTimerFd_ >= 0) close(frameTimerFd_);
}

void KmsWindow::setLambdaWindow(::lambda::Window* window) {
  lambdaWindow_ = window;
}

void KmsWindow::show() {
  if (lambdaWindow_) lambdaWindow_->updateCanvasDpiScale(1.f, 1.f);
  Point const center{std::max(0.f, size_.width * 0.5f), std::max(0.f, size_.height * 0.5f)};
  app_.setPointerPosition(this, center);
  cursorPos_ = center;
  applyCursor();
  Application::instance().eventQueue().post(WindowEvent{WindowEvent::Kind::Resize, handle_, size_});
  Application::instance().requestWindowRedraw(handle_);
  Application::instance().flushRedraw();
}

std::unique_ptr<Canvas> KmsWindow::createCanvas(::lambda::Window&) {
  auto* provider = app_.gpuSurfaceProvider();
  if (!provider) {
    throw std::runtime_error("KMS application does not provide Vulkan surfaces");
  }
  configureVulkanCanvasRuntime(provider->requiredInstanceExtensions(), app_.cacheDir());
  VkInstance instance = ensureSharedVulkanInstance();
  VkSurfaceKHR surface = provider->createSurface(instance, &connector_);
  auto canvas = createVulkanCanvas(surface, handle_, Application::instance().textSystem());
  canvas->updateDpiScale(1.f, 1.f);
  canvas->resize(static_cast<int>(std::lround(size_.width)), static_cast<int>(std::lround(size_.height)));
  canvas_ = canvas.get();
  return canvas;
}

void KmsWindow::resize(Size const&) {}
void KmsWindow::setFullscreen(bool) {}
void KmsWindow::setTitle(std::string const& title) { title_ = title; }
Size KmsWindow::currentSize() const { return size_; }
bool KmsWindow::isFullscreen() const { return true; }
unsigned int KmsWindow::handle() const { return handle_; }
void* KmsWindow::nativeGraphicsSurface() const { return const_cast<KmsConnector*>(&connector_); }
int KmsWindow::eventFd() const { return app_.inputFd(); }
int KmsWindow::wakeFd() const { return app_.wakeFd(); }
void KmsWindow::wakeEventLoop() { app_.wakeEventLoop(); }
void KmsWindow::setCursor(Cursor kind) {
  cursor_ = kind == Cursor::Inherit ? Cursor::Arrow : kind;
  if (app_.focusedWindow() == this) applyCursor();
}

PlatformWindowCapabilities KmsWindow::capabilities() const {
  return {
      .supportsLayerShell = false,
      .supportsBackgroundBlur = false,
      .supportsOutputSelection = true,
      .supportsDisplayMode = true,
  };
}

void KmsWindow::processEvents() {
  drainFrameTimer();
  app_.dispatchPendingInput();
}

void KmsWindow::waitForEvents(int timeoutMs) {
  app_.pollInputAndWake(timeoutMs);
  drainFrameTimer();
}

void KmsWindow::requestAnimationFrame() {
  if (!app_.isVtForeground()) return;
  if (framePending_) return;
  framePending_ = true;
  armFrameTimer();
}

void KmsWindow::acknowledgeAnimationFrameTick() {
  framePending_ = false;
}

void KmsWindow::completeAnimationFrame(bool needsAnotherFrame) {
  if (needsAnotherFrame && app_.isVtForeground()) requestAnimationFrame();
}

void KmsWindow::suspendForVtSwitch() {
  framePending_ = false;
  if (cursorVisible_) {
    drmModeSetCursor(app_.drmFd(), connector_.crtcId, 0, 0, 0);
    cursorVisible_ = false;
  }
}

void KmsWindow::resumeFromVtSwitch() {
  if (app_.focusedWindow() == this) applyCursor();
  requestAnimationFrame();
  Application::instance().requestWindowRedraw(handle_);
}

void KmsWindow::updateConnector(KmsConnector connector) {
  connector_ = std::move(connector);
  connector_.mode = selectMode(connector_, requestedModeWidth_, requestedModeHeight_, requestedModeRefreshHz_);
  size_ = sizeForMode(connector_.mode);
  if (canvas_) {
    canvas_->resize(static_cast<int>(std::lround(size_.width)), static_cast<int>(std::lround(size_.height)));
  }
}

void KmsWindow::postFrameTick() {
  if (!framePending_) return;
  if (!app_.isVtForeground()) {
    framePending_ = false;
    return;
  }
  Application::instance().eventQueue().post(FrameEvent{nowNanos(), handle_});
  Application::instance().eventQueue().dispatch();
  wakeEventLoop();
}

Point KmsWindow::clampPointer(Point p) const {
  p.x = std::clamp(p.x, 0.f, std::max(0.f, size_.width - 1.f));
  p.y = std::clamp(p.y, 0.f, std::max(0.f, size_.height - 1.f));
  return p;
}

void KmsWindow::moveCursor(Point p) {
  cursorPos_ = clampPointer(p);
  if (!app_.isVtForeground()) return;
  if (!cursorVisible_) applyCursor();
  if (cursorVisible_) {
    drmModeMoveCursor(app_.drmFd(), connector_.crtcId, static_cast<int>(std::lround(cursorPos_.x)),
                      static_cast<int>(std::lround(cursorPos_.y)));
  }
}

void KmsWindow::hideCursor() {
  if (cursorVisible_ && app_.isVtForeground()) {
    drmModeSetCursor(app_.drmFd(), connector_.crtcId, 0, 0, 0);
  }
  cursorVisible_ = false;
}

void KmsWindow::armFrameTimer() {
  auto const interval = frameInterval(connector_.mode);
  itimerspec spec{};
  spec.it_value.tv_sec = static_cast<time_t>(interval.count() / 1'000'000'000ll);
  spec.it_value.tv_nsec = static_cast<long>(interval.count() % 1'000'000'000ll);
  timerfd_settime(frameTimerFd_, 0, &spec, nullptr);
}

void KmsWindow::drainFrameTimer() {
  std::uint64_t expirations = 0;
  bool fired = false;
  while (read(frameTimerFd_, &expirations, sizeof(expirations)) == sizeof(expirations)) fired = true;
  if (fired) postFrameTick();
}

void KmsWindow::applyCursor() {
  if (!app_.isVtForeground()) return;
  if (!ensureCursorBuffer()) return;
  int const fd = app_.drmFd();
  int rc = drmModeSetCursor2(fd, connector_.crtcId, cursorBuffer_.handle, cursorBuffer_.width,
                             cursorBuffer_.height, 0, 0);
  if (rc != 0) {
    rc = drmModeSetCursor(fd, connector_.crtcId, cursorBuffer_.handle, cursorBuffer_.width,
                          cursorBuffer_.height);
  }
  if (rc != 0) {
    if (!cursorVisible_) {
      std::fprintf(stderr, "[lambda:kms] failed to set hardware cursor on connector %s.\n", connector_.name.c_str());
    }
    cursorVisible_ = false;
    return;
  }
  cursorVisible_ = true;
  moveCursor(cursorPos_);
}

void KmsWindow::destroyCursorBuffer() {
  if (!cursorBuffer_.handle) return;
  drm_mode_destroy_dumb destroy{};
  destroy.handle = cursorBuffer_.handle;
  drmIoctl(app_.drmFd(), DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
  cursorBuffer_ = {};
  cursorVisible_ = false;
}

bool KmsWindow::ensureCursorBuffer() {
  if (cursorBuffer_.handle) return true;
  int const fd = app_.drmFd();
  std::uint32_t const width = cursorDimension(fd, DRM_CAP_CURSOR_WIDTH);
  std::uint32_t const height = cursorDimension(fd, DRM_CAP_CURSOR_HEIGHT);
  drm_mode_create_dumb create{};
  create.width = width;
  create.height = height;
  create.bpp = 32;
  if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) {
    std::fprintf(stderr, "[lambda:kms] failed to create cursor buffer.\n");
    return false;
  }
  drm_mode_map_dumb map{};
  map.handle = create.handle;
  if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0) {
    drm_mode_destroy_dumb destroy{};
    destroy.handle = create.handle;
    drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
    std::fprintf(stderr, "[lambda:kms] failed to map cursor buffer.\n");
    return false;
  }
  void* mapped = mmap(nullptr, create.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map.offset);
  if (mapped == MAP_FAILED) {
    drm_mode_destroy_dumb destroy{};
    destroy.handle = create.handle;
    drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
    std::fprintf(stderr, "[lambda:kms] failed to mmap cursor buffer.\n");
    return false;
  }
  drawArrowCursor(static_cast<std::uint32_t*>(mapped), create.pitch / sizeof(std::uint32_t), height);
  munmap(mapped, create.size);
  cursorBuffer_ = CursorBuffer{.handle = create.handle,
                               .size = create.size,
                               .width = width,
                               .height = height};
  return true;
}

std::unique_ptr<platform::Window> KmsApplication::createWindow(WindowConfig const& config) {
  if (connectors_.empty()) throw std::runtime_error("No KMS connector is available for window creation");
  auto connector = connectors_.begin();
  if (!config.outputName.empty()) {
    connector = std::find_if(connectors_.begin(), connectors_.end(), [&](KmsConnector const& candidate) {
      return candidate.name == config.outputName;
    });
    if (connector == connectors_.end()) {
      throw std::runtime_error("KMS: no connector named '" + config.outputName + "'");
    }
  }
  if (windowForConnector(connector->connectorId)) {
    throw std::runtime_error("KMS: connector '" + connector->name + "' already in use");
  }
  return std::make_unique<KmsWindow>(*this, *connector, config);
}

namespace platform {

std::unique_ptr<Window> createWindow(WindowConfig const& config) {
  return kmsApplication().createWindow(config);
}

} // namespace platform
} // namespace lambda
