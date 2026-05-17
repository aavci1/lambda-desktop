#include <Flux/Platform/Linux/KmsOutput.hpp>

#include "Platform/Linux/KmsPlatform.hpp"

#include <algorithm>
#include <chrono>
#include <atomic>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <utility>

#include <drm.h>
#include <drm_mode.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>

namespace flux::platform {
namespace {

std::uint32_t refreshRateMilliHz(drmModeModeInfo const& mode) {
  if (mode.vrefresh > 0) return static_cast<std::uint32_t>(mode.vrefresh) * 1000u;
  if (mode.clock > 0 && mode.htotal > 0 && mode.vtotal > 0) {
    return static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(mode.clock) * 1'000'000ull) /
        (static_cast<std::uint64_t>(mode.htotal) * static_cast<std::uint64_t>(mode.vtotal)));
  }
  return 60'000u;
}

std::chrono::nanoseconds frameInterval(std::uint32_t refreshMilliHz) {
  if (refreshMilliHz == 0) refreshMilliHz = 60'000u;
  return std::chrono::nanoseconds(1'000'000'000'000ll / refreshMilliHz);
}

std::uint32_t cursorDimension(int fd, std::uint64_t cap) {
  std::uint64_t value = 0;
  if (drmGetCap(fd, cap, &value) == 0 && value >= 16 && value <= 256) {
    return static_cast<std::uint32_t>(value);
  }
  return 64;
}

} // namespace

class KmsDevice::Impl {
public:
  explicit Impl(char const* devicePath);

  std::vector<KmsOutput> outputs(std::shared_ptr<Impl> self) const;

  std::unique_ptr<flux::KmsApplication> app_;
};

class KmsOutput::Impl {
public:
  Impl(std::shared_ptr<KmsDevice::Impl> device, KmsConnector connector)
      : device_(std::move(device)), connector_(std::move(connector)) {}
  ~Impl();

  [[nodiscard]] int drmFd() const noexcept {
    return device_ && device_->app_ ? device_->app_->drmFd() : -1;
  }

  void destroyCursorBuffer();

  struct CursorBuffer {
    std::uint32_t handle = 0;
    std::uint64_t size = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t pitch = 0;
  };

  std::shared_ptr<KmsDevice::Impl> device_;
  KmsConnector connector_{};
  mutable CursorBuffer cursorBuffer_{};
  mutable bool cursorVisible_ = false;
};

KmsOutput::Impl::~Impl() {
  destroyCursorBuffer();
}

void KmsOutput::Impl::destroyCursorBuffer() {
  if (!cursorBuffer_.handle) return;
  drm_mode_destroy_dumb destroy{};
  destroy.handle = cursorBuffer_.handle;
  drmIoctl(drmFd(), DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
  cursorBuffer_ = {};
  cursorVisible_ = false;
}

KmsOutput::KmsOutput() = default;
KmsOutput::~KmsOutput() = default;
KmsOutput::KmsOutput(KmsOutput const&) = default;
KmsOutput& KmsOutput::operator=(KmsOutput const&) = default;
KmsOutput::KmsOutput(KmsOutput&&) noexcept = default;
KmsOutput& KmsOutput::operator=(KmsOutput&&) noexcept = default;

KmsOutput::KmsOutput(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}

std::string const& KmsOutput::name() const noexcept {
  static std::string const empty;
  return impl_ ? impl_->connector_.name : empty;
}

std::uint32_t KmsOutput::width() const noexcept {
  return impl_ ? impl_->connector_.mode.hdisplay : 0u;
}

std::uint32_t KmsOutput::height() const noexcept {
  return impl_ ? impl_->connector_.mode.vdisplay : 0u;
}

std::uint32_t KmsOutput::refreshRateMilliHz() const noexcept {
  return impl_ ? flux::platform::refreshRateMilliHz(impl_->connector_.mode) : 0u;
}

std::uint32_t KmsOutput::cursorWidth() const noexcept {
  return impl_ ? cursorDimension(impl_->drmFd(), DRM_CAP_CURSOR_WIDTH) : 0u;
}

std::uint32_t KmsOutput::cursorHeight() const noexcept {
  return impl_ ? cursorDimension(impl_->drmFd(), DRM_CAP_CURSOR_HEIGHT) : 0u;
}

VkSurfaceKHR KmsOutput::createVulkanSurface(VkInstance instance) const {
  if (!impl_ || !impl_->device_ || !impl_->device_->app_) throw std::runtime_error("Invalid KMS output");
  return impl_->device_->app_->createVulkanSurface(instance, &impl_->connector_);
}

void KmsOutput::waitForVblank() const {
  std::this_thread::sleep_for(frameInterval(refreshRateMilliHz()));
}

bool KmsOutput::setCursorImage(std::span<std::uint32_t const> premultipliedArgbPixels,
                               std::uint32_t width,
                               std::uint32_t height,
                               std::int32_t hotspotX,
                               std::int32_t hotspotY) const {
  if (!impl_ || width == 0 || height == 0 ||
      premultipliedArgbPixels.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
    return false;
  }

  int const fd = impl_->drmFd();
  if (fd < 0) return false;
  if (impl_->cursorBuffer_.handle &&
      (impl_->cursorBuffer_.width != width || impl_->cursorBuffer_.height != height)) {
    impl_->destroyCursorBuffer();
  }

  if (!impl_->cursorBuffer_.handle) {
    drm_mode_create_dumb create{};
    create.width = width;
    create.height = height;
    create.bpp = 32;
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) return false;
    impl_->cursorBuffer_ = KmsOutput::Impl::CursorBuffer{
        .handle = create.handle,
        .size = create.size,
        .width = width,
        .height = height,
        .pitch = create.pitch,
    };
  }

  drm_mode_map_dumb map{};
  map.handle = impl_->cursorBuffer_.handle;
  if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0) return false;
  void* mapped = mmap(nullptr, impl_->cursorBuffer_.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map.offset);
  if (mapped == MAP_FAILED) return false;
  auto* dst = static_cast<std::uint8_t*>(mapped);
  std::size_t const srcRowBytes = static_cast<std::size_t>(width) * sizeof(std::uint32_t);
  std::size_t const dstRowBytes = impl_->cursorBuffer_.pitch;
  for (std::uint32_t y = 0; y < height; ++y) {
    std::memcpy(dst + static_cast<std::size_t>(y) * dstRowBytes,
                premultipliedArgbPixels.data() + static_cast<std::size_t>(y) * width,
                srcRowBytes);
  }
  munmap(mapped, impl_->cursorBuffer_.size);

  int rc = drmModeSetCursor2(fd, impl_->connector_.crtcId, impl_->cursorBuffer_.handle, width, height, hotspotX, hotspotY);
  if (rc != 0) {
    rc = drmModeSetCursor(fd, impl_->connector_.crtcId, impl_->cursorBuffer_.handle, width, height);
  }
  impl_->cursorVisible_ = rc == 0;
  return impl_->cursorVisible_;
}

bool KmsOutput::moveCursor(std::int32_t x, std::int32_t y) const {
  if (!impl_ || !impl_->cursorVisible_) return false;
  return drmModeMoveCursor(impl_->drmFd(), impl_->connector_.crtcId, x, y) == 0;
}

void KmsOutput::hideCursor() const {
  if (!impl_ || !impl_->cursorVisible_) return;
  drmModeSetCursor(impl_->drmFd(), impl_->connector_.crtcId, 0, 0, 0);
  impl_->cursorVisible_ = false;
}

KmsDevice::Impl::Impl(char const* devicePath) {
  if (devicePath && *devicePath) {
    throw std::runtime_error("KmsDevice::open(devicePath) is not implemented yet; pass nullptr");
  }
  app_ = std::make_unique<flux::KmsApplication>();
  app_->setApplicationName("flux-compositor");
  app_->initialize();
}

std::vector<KmsOutput> KmsDevice::Impl::outputs(std::shared_ptr<Impl> self) const {
  std::vector<KmsOutput> result;
  result.reserve(app_->connectors_.size());
  for (KmsConnector const& connector : app_->connectors_) {
    result.push_back(KmsOutput(std::shared_ptr<KmsOutput::Impl>(new KmsOutput::Impl(self, connector))));
  }
  return result;
}

std::unique_ptr<KmsDevice> KmsDevice::open(char const* devicePath) {
  return std::unique_ptr<KmsDevice>(new KmsDevice(std::make_shared<Impl>(devicePath)));
}

KmsDevice::KmsDevice(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
KmsDevice::~KmsDevice() = default;
KmsDevice::KmsDevice(KmsDevice&&) noexcept = default;
KmsDevice& KmsDevice::operator=(KmsDevice&&) noexcept = default;

std::vector<KmsOutput> KmsDevice::outputs() const {
  return impl_ ? impl_->outputs(impl_) : std::vector<KmsOutput>{};
}

int KmsDevice::fd() const noexcept {
  return impl_ && impl_->app_ ? impl_->app_->drmFd() : -1;
}

std::span<char const* const> KmsDevice::requiredVulkanInstanceExtensions() const {
  static std::vector<char const*> empty;
  return impl_ && impl_->app_ ? impl_->app_->requiredVulkanInstanceExtensions()
                              : std::span<char const* const>(empty.data(), empty.size());
}

std::filesystem::path KmsDevice::cacheDir() const {
  return impl_ && impl_->app_ ? std::filesystem::path(impl_->app_->cacheDir()) : std::filesystem::path{};
}

bool KmsDevice::isVtForeground() const noexcept {
  return impl_ && impl_->app_ ? impl_->app_->isVtForeground() : false;
}

bool KmsDevice::shouldTerminate() const noexcept {
  return impl_ && impl_->app_ ? impl_->app_->terminateRequested_.load(std::memory_order_relaxed) : true;
}

void KmsDevice::setInputHandler(std::function<void(KmsInputEvent const&)> handler) {
  if (impl_ && impl_->app_) impl_->app_->rawInputHandler_ = std::move(handler);
}

bool KmsDevice::pollEvents(int timeoutMs) {
  return impl_ && impl_->app_ ? impl_->app_->pollInputAndWake(timeoutMs) : false;
}

} // namespace flux::platform
