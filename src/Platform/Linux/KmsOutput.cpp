#include <Flux/Platform/Linux/KmsOutput.hpp>

#include <Flux/Graphics/RenderTarget.hpp>
#include <Flux/Graphics/VulkanContext.hpp>

#include "Graphics/Vulkan/VulkanCanvas.hpp"
#include "Platform/Linux/KmsPlatform.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <optional>
#include <ctime>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>

#include <drm_fourcc.h>
#include <gbm.h>
#include <drm.h>
#include <drm_mode.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

namespace flux::platform {
namespace {

std::uint64_t monotonicNanoseconds() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<std::uint64_t>(now.tv_sec) * 1'000'000'000ull +
         static_cast<std::uint64_t>(now.tv_nsec);
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

std::chrono::nanoseconds frameInterval(std::uint32_t refreshMilliHz) {
  if (refreshMilliHz == 0) refreshMilliHz = 60'000u;
  return std::chrono::nanoseconds(1'000'000'000'000ll / refreshMilliHz);
}

std::uint64_t monotonicNowNsec() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<std::uint64_t>(now.tv_sec) * 1'000'000'000ull +
         static_cast<std::uint64_t>(now.tv_nsec);
}

void vkCheck(VkResult result, char const* what) {
  if (result != VK_SUCCESS) {
    throw std::runtime_error(std::string(what) + " failed");
  }
}

std::uint32_t cursorDimension(int fd, std::uint64_t cap) {
  std::uint64_t value = 0;
  if (drmGetCap(fd, cap, &value) == 0 && value >= 16 && value <= 256) {
    return static_cast<std::uint32_t>(value);
  }
  return 64;
}

std::uint32_t findVulkanMemoryType(VkPhysicalDevice physical,
                                   std::uint32_t typeBits,
                                   VkMemoryPropertyFlags requiredProperties) {
  VkPhysicalDeviceMemoryProperties props{};
  vkGetPhysicalDeviceMemoryProperties(physical, &props);
  for (std::uint32_t i = 0; i < props.memoryTypeCount; ++i) {
    if ((typeBits & (1u << i)) &&
        (props.memoryTypes[i].propertyFlags & requiredProperties) == requiredProperties) {
      return i;
    }
  }
  throw std::runtime_error("No suitable Vulkan memory type for KMS scanout buffer");
}

std::uint64_t gbmModifier(gbm_bo* bo) {
  std::uint64_t const modifier = gbm_bo_get_modifier(bo);
  return modifier == DRM_FORMAT_MOD_INVALID ? DRM_FORMAT_MOD_LINEAR : modifier;
}

bool forceLinearScanout() {
  char const* value = std::getenv("FLUX_COMPOSITOR_FORCE_LINEAR_SCANOUT");
  return value && *value && std::strcmp(value, "0") != 0;
}

bool useKmsRenderInFence() {
  char const* value = std::getenv("FLUX_COMPOSITOR_USE_KMS_IN_FENCE");
  return !value || !*value || std::strcmp(value, "0") != 0;
}

std::uint32_t propertyId(int fd, std::uint32_t objectId, std::uint32_t objectType, char const* name) {
  drmModeObjectProperties* props = drmModeObjectGetProperties(fd, objectId, objectType);
  if (!props) return 0;
  std::uint32_t found = 0;
  for (std::uint32_t i = 0; i < props->count_props && found == 0; ++i) {
    drmModePropertyRes* prop = drmModeGetProperty(fd, props->props[i]);
    if (prop) {
      if (std::strcmp(prop->name, name) == 0) found = prop->prop_id;
      drmModeFreeProperty(prop);
    }
  }
  drmModeFreeObjectProperties(props);
  return found;
}

std::uint64_t propertyValue(int fd, std::uint32_t objectId, std::uint32_t objectType, char const* name) {
  drmModeObjectProperties* props = drmModeObjectGetProperties(fd, objectId, objectType);
  if (!props) return 0;
  std::uint64_t value = 0;
  for (std::uint32_t i = 0; i < props->count_props; ++i) {
    drmModePropertyRes* prop = drmModeGetProperty(fd, props->props[i]);
    if (prop) {
      bool const matches = std::strcmp(prop->name, name) == 0;
      drmModeFreeProperty(prop);
      if (matches) {
        value = props->prop_values[i];
        break;
      }
    }
  }
  drmModeFreeObjectProperties(props);
  return value;
}

void addAtomicProperty(drmModeAtomicReq* request,
                       std::uint32_t objectId,
                       std::uint32_t property,
                       std::uint64_t value,
                       char const* name) {
  if (property == 0) {
    throw std::runtime_error(std::string("KMS atomic property missing: ") + name);
  }
  if (drmModeAtomicAddProperty(request, objectId, property, value) < 0) {
    throw std::runtime_error(std::string("drmModeAtomicAddProperty failed for ") + name);
  }
}

std::uint32_t crtcIndexForId(int fd, std::uint32_t crtcId) {
  drmModeRes* resources = drmModeGetResources(fd);
  if (!resources) throw std::runtime_error("drmModeGetResources failed while selecting atomic plane");
  std::uint32_t index = UINT32_MAX;
  for (int i = 0; i < resources->count_crtcs; ++i) {
    if (resources->crtcs[i] == crtcId) {
      index = static_cast<std::uint32_t>(i);
      break;
    }
  }
  drmModeFreeResources(resources);
  if (index == UINT32_MAX) throw std::runtime_error("KMS CRTC not found in DRM resources");
  return index;
}

std::uint32_t primaryPlaneForCrtc(int fd, std::uint32_t crtcId) {
  std::uint32_t const crtcIndex = crtcIndexForId(fd, crtcId);
  drmModePlaneRes* planes = drmModeGetPlaneResources(fd);
  if (!planes) throw std::runtime_error("drmModeGetPlaneResources failed");
  std::uint32_t fallback = 0;
  std::uint32_t selected = 0;
  for (std::uint32_t i = 0; i < planes->count_planes; ++i) {
    drmModePlane* plane = drmModeGetPlane(fd, planes->planes[i]);
    if (!plane) continue;
    bool const canUseCrtc = (plane->possible_crtcs & (1u << crtcIndex)) != 0;
    if (canUseCrtc && fallback == 0) fallback = plane->plane_id;
    std::uint64_t const type = propertyValue(fd, plane->plane_id, DRM_MODE_OBJECT_PLANE, "type");
    if (canUseCrtc && type == DRM_PLANE_TYPE_PRIMARY) selected = plane->plane_id;
    drmModeFreePlane(plane);
    if (selected != 0) break;
  }
  drmModeFreePlaneResources(planes);
  selected = selected != 0 ? selected : fallback;
  if (selected == 0) throw std::runtime_error("No usable KMS primary plane found");
  return selected;
}

void pageFlipHandler(int,
                     unsigned int sequence,
                     unsigned int tvSec,
                     unsigned int tvUsec,
                     void* data) {
  auto* timing = static_cast<KmsAtomicPresenter::PageFlipTiming*>(data);
  timing->hardware = true;
  timing->sequence = sequence;
  timing->monotonicNsec = static_cast<std::uint64_t>(tvSec) * 1'000'000'000ull +
                          static_cast<std::uint64_t>(tvUsec) * 1'000ull;
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
  mutable bool vblankWaitDisabled_ = false;
};

class KmsAtomicPresenter::Impl {
public:
  Impl(int fd, KmsConnector connector, TextSystem& textSystem)
      : fd_(fd), connector_(std::move(connector)), textSystem_(textSystem) {
    try {
      if (fd_ < 0 || connector_.connectorId == 0 || connector_.crtcId == 0) {
        throw std::runtime_error("Invalid KMS output for atomic presenter");
      }
      if (drmSetClientCap(fd_, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) != 0) {
        throw std::system_error(errno, std::generic_category(), "drmSetClientCap(UNIVERSAL_PLANES)");
      }
      if (drmSetClientCap(fd_, DRM_CLIENT_CAP_ATOMIC, 1) != 0) {
        throw std::system_error(errno, std::generic_category(), "drmSetClientCap(ATOMIC)");
      }
      planeId_ = primaryPlaneForCrtc(fd_, connector_.crtcId);
      loadProperties();
      useRenderInFence_ = useKmsRenderInFence() && planeInFenceFd_ != 0;
      if (planeInFenceFd_ != 0) {
        std::fprintf(stderr,
                     "flux-compositor: atomic KMS render fence mode: %s\n",
                     useRenderInFence_ ? "kms-in-fence" : "wait-before-commit");
      }
      createModeBlob();
      gbm_ = gbm_create_device(fd_);
      if (!gbm_) throw std::runtime_error("gbm_create_device failed for KMS atomic presenter");
      flux::VulkanContext::instance().ensureInitialized();
      createBuffers();
      canvas_ = flux::createVulkanRenderTargetCanvas(buffers_[0].spec, textSystem_);
      if (!canvas_) throw std::runtime_error("Failed to create atomic KMS render-target canvas");
    } catch (...) {
      cleanup();
      throw;
    }
  }

  ~Impl() { cleanup(); }

  void cleanup() {
    if (canvas_) canvas_.reset();
    VkDevice device = flux::VulkanContext::instance().device();
    for (auto& buffer : buffers_) {
      closeRenderFence(buffer);
      if (buffer.renderFinished) vkDestroySemaphore(device, buffer.renderFinished, nullptr);
      if (buffer.view) vkDestroyImageView(device, buffer.view, nullptr);
      if (buffer.image) vkDestroyImage(device, buffer.image, nullptr);
      if (buffer.memory) vkFreeMemory(device, buffer.memory, nullptr);
      if (buffer.fbId) drmModeRmFB(fd_, buffer.fbId);
      if (buffer.bo) gbm_bo_destroy(buffer.bo);
    }
    buffers_.clear();
    if (modeBlob_ != 0) drmModeDestroyPropertyBlob(fd_, modeBlob_);
    modeBlob_ = 0;
    if (gbm_) gbm_device_destroy(gbm_);
    gbm_ = nullptr;
  }

  Canvas& canvas() {
    if (!canvas_) throw std::runtime_error("Atomic KMS presenter has no canvas");
    return *canvas_;
  }

  void prepareFrame() {
    if (buffers_.empty()) throw std::runtime_error("Atomic KMS presenter has no scanout buffers");
    std::size_t next = displayedBuffer_ >= 0 ? static_cast<std::size_t>(displayedBuffer_) : 0u;
    for (std::size_t i = 0; i < buffers_.size(); ++i) {
      next = (next + 1u) % buffers_.size();
      if (static_cast<int>(next) != displayedBuffer_ && static_cast<int>(next) != pendingBuffer_ &&
          static_cast<int>(next) != renderBuffer_) {
        break;
      }
    }
    renderBuffer_ = static_cast<int>(next);
    closeRenderFence(buffers_[next]);
    buffers_[next].renderComplete = bufferHasNoAsyncRenderFence(buffers_[next]);
    buffers_[next].renderSubmittedNsec = 0;
    buffers_[next].renderReadyNsec = 0;
    if (!flux::setVulkanRenderTargetSpecForCanvas(canvas_.get(), buffers_[next].spec)) {
      throw std::runtime_error("Failed to switch atomic KMS render target");
    }
  }

  void markFrameRendered() {
    if (renderBuffer_ < 0) return;
    Buffer& buffer = buffers_[static_cast<std::size_t>(renderBuffer_)];
    buffer.renderSubmittedNsec = monotonicNanoseconds();
    buffer.renderReadyNsec = 0;
    closeRenderFence(buffer);
    if (buffer.renderFinished == VK_NULL_HANDLE) {
      buffer.renderComplete = true;
      buffer.renderReadyNsec = buffer.renderSubmittedNsec;
      return;
    }
    buffer.renderFenceFd = exportRenderSemaphoreFd(buffer.renderFinished);
    buffer.renderComplete = false;
    updateRenderFenceReadiness(buffer);
  }

  bool updateRenderReady() {
    if (renderBuffer_ < 0) return true;
    Buffer& buffer = buffers_[static_cast<std::size_t>(renderBuffer_)];
    updateRenderFenceReadiness(buffer);
    return buffer.renderComplete;
  }

  bool canSchedulePresent() {
    if (renderBuffer_ < 0) return false;
    bool const renderReady = updateRenderReady();
    if (pageFlipPending_) return false;
    Buffer const& buffer = buffers_[static_cast<std::size_t>(renderBuffer_)];
    return renderReady || canUseRenderFence(buffer);
  }

  int renderReadyFd() const noexcept {
    if (useRenderInFence_) return -1;
    if (renderBuffer_ < 0) return -1;
    Buffer const& buffer = buffers_[static_cast<std::size_t>(renderBuffer_)];
    return buffer.renderComplete ? -1 : buffer.renderFenceFd;
  }

  std::uint32_t schedulePresent() {
    if (pageFlipPending_) throw std::runtime_error("KMS atomic page flip is already pending");
    if (renderBuffer_ < 0) prepareFrame();
    if (!canSchedulePresent()) throw std::runtime_error("KMS atomic render buffer is not ready");
    Buffer& buffer = buffers_[static_cast<std::size_t>(renderBuffer_)];
    drmModeAtomicReq* request = drmModeAtomicAlloc();
    if (!request) throw std::runtime_error("drmModeAtomicAlloc failed");
    try {
      bool const useRenderFence = !buffer.renderComplete && canUseRenderFence(buffer);
      if (!modesetDone_) {
        addAtomicProperty(request, connector_.connectorId, connectorCrtcId_, connector_.crtcId, "connector.CRTC_ID");
        addAtomicProperty(request, connector_.crtcId, crtcModeId_, modeBlob_, "crtc.MODE_ID");
        addAtomicProperty(request, connector_.crtcId, crtcActive_, 1, "crtc.ACTIVE");
      }
      addAtomicProperty(request, planeId_, planeFbId_, buffer.fbId, "plane.FB_ID");
      addAtomicProperty(request, planeId_, planeCrtcId_, connector_.crtcId, "plane.CRTC_ID");
      addAtomicProperty(request, planeId_, planeSrcX_, 0, "plane.SRC_X");
      addAtomicProperty(request, planeId_, planeSrcY_, 0, "plane.SRC_Y");
      addAtomicProperty(request,
                        planeId_,
                        planeSrcW_,
                        static_cast<std::uint64_t>(connector_.mode.hdisplay) << 16u,
                        "plane.SRC_W");
      addAtomicProperty(request,
                        planeId_,
                        planeSrcH_,
                        static_cast<std::uint64_t>(connector_.mode.vdisplay) << 16u,
                        "plane.SRC_H");
      addAtomicProperty(request, planeId_, planeCrtcX_, 0, "plane.CRTC_X");
      addAtomicProperty(request, planeId_, planeCrtcY_, 0, "plane.CRTC_Y");
      addAtomicProperty(request, planeId_, planeCrtcW_, connector_.mode.hdisplay, "plane.CRTC_W");
      addAtomicProperty(request, planeId_, planeCrtcH_, connector_.mode.vdisplay, "plane.CRTC_H");
      if (useRenderFence) {
        addAtomicProperty(request,
                          planeId_,
                          planeInFenceFd_,
                          static_cast<std::uint64_t>(buffer.renderFenceFd),
                          "plane.IN_FENCE_FD");
      }
      std::uint32_t flags = DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT;
      if (!modesetDone_) flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
      pendingTiming_ = KmsAtomicPresenter::PageFlipTiming{
          .presentId = nextPresentId_++,
          .scheduledMonotonicNsec = monotonicNanoseconds(),
          .renderSubmittedMonotonicNsec = buffer.renderSubmittedNsec,
          .renderReadyMonotonicNsec = buffer.renderReadyNsec,
          .usedRenderFence = useRenderFence,
      };
      std::uint64_t const commitStartNsec = monotonicNanoseconds();
      int const rc = drmModeAtomicCommit(fd_, request, flags, &pendingTiming_);
      pendingTiming_.commitDurationNsec = monotonicNanoseconds() - commitStartNsec;
      if (rc != 0) {
        throw std::system_error(errno, std::generic_category(), "drmModeAtomicCommit");
      }
      if (useRenderFence) {
        closeRenderFence(buffer);
        buffer.renderComplete = true;
      }
      drmModeAtomicFree(request);
      request = nullptr;
      modesetDone_ = true;
      pendingBuffer_ = renderBuffer_;
      renderBuffer_ = -1;
      pageFlipPending_ = true;
      return pendingTiming_.presentId;
    } catch (...) {
      if (request) drmModeAtomicFree(request);
      throw;
    }
  }

  KmsAtomicPresenter::PageFlipTiming present() {
    schedulePresent();
    waitForPageFlip();
    return pendingTiming_;
  }

  std::optional<KmsAtomicPresenter::PageFlipTiming> dispatchPageFlipEvents() {
    if (!pageFlipPending_) return std::nullopt;
    pollfd pfd{.fd = fd_, .events = POLLIN, .revents = 0};
    int const pollResult = poll(&pfd, 1, 0);
    if (pollResult < 0) {
      if (errno == EINTR) return std::nullopt;
      throw std::system_error(errno, std::generic_category(), "poll KMS page flip");
    }
    if (pollResult == 0) return std::nullopt;
    drmEventContext eventContext{};
    eventContext.version = DRM_EVENT_CONTEXT_VERSION;
    eventContext.page_flip_handler = pageFlipHandler;
    if (drmHandleEvent(fd_, &eventContext) != 0) {
      throw std::system_error(errno, std::generic_category(), "drmHandleEvent");
    }
    if (!pendingTiming_.hardware) return std::nullopt;
    displayedBuffer_ = pendingBuffer_;
    pendingBuffer_ = -1;
    pageFlipPending_ = false;
    return pendingTiming_;
  }

  bool hasPendingPageFlip() const noexcept { return pageFlipPending_; }

  int eventFd() const noexcept { return fd_; }

private:
  struct Buffer {
    gbm_bo* bo = nullptr;
    std::uint32_t fbId = 0;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSemaphore renderFinished = VK_NULL_HANDLE;
    int renderFenceFd = -1;
    bool renderComplete = true;
    std::uint64_t renderSubmittedNsec = 0;
    std::uint64_t renderReadyNsec = 0;
    VulkanRenderTargetSpec spec{};
  };

  static bool bufferHasNoAsyncRenderFence(Buffer const& buffer) noexcept {
    return buffer.renderFinished == VK_NULL_HANDLE;
  }

  bool canUseRenderFence(Buffer const& buffer) const noexcept {
    return useRenderInFence_ && buffer.renderFenceFd >= 0;
  }

  void closeRenderFence(Buffer& buffer) noexcept {
    if (buffer.renderFenceFd >= 0) {
      close(buffer.renderFenceFd);
      buffer.renderFenceFd = -1;
    }
  }

  void updateRenderFenceReadiness(Buffer& buffer) {
    if (buffer.renderComplete) return;
    if (buffer.renderFenceFd < 0) return;
    pollfd pfd{.fd = buffer.renderFenceFd, .events = POLLIN, .revents = 0};
    int const pollResult = poll(&pfd, 1, 0);
    if (pollResult < 0) {
      if (errno == EINTR) return;
      throw std::system_error(errno, std::generic_category(), "poll KMS render sync fd");
    }
    if (pollResult > 0 && (pfd.revents & (POLLIN | POLLERR | POLLHUP)) != 0) {
      closeRenderFence(buffer);
      buffer.renderComplete = true;
      buffer.renderReadyNsec = monotonicNanoseconds();
    }
  }

  void loadProperties() {
    connectorCrtcId_ = propertyId(fd_, connector_.connectorId, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID");
    crtcModeId_ = propertyId(fd_, connector_.crtcId, DRM_MODE_OBJECT_CRTC, "MODE_ID");
    crtcActive_ = propertyId(fd_, connector_.crtcId, DRM_MODE_OBJECT_CRTC, "ACTIVE");
    planeFbId_ = propertyId(fd_, planeId_, DRM_MODE_OBJECT_PLANE, "FB_ID");
    planeCrtcId_ = propertyId(fd_, planeId_, DRM_MODE_OBJECT_PLANE, "CRTC_ID");
    planeSrcX_ = propertyId(fd_, planeId_, DRM_MODE_OBJECT_PLANE, "SRC_X");
    planeSrcY_ = propertyId(fd_, planeId_, DRM_MODE_OBJECT_PLANE, "SRC_Y");
    planeSrcW_ = propertyId(fd_, planeId_, DRM_MODE_OBJECT_PLANE, "SRC_W");
    planeSrcH_ = propertyId(fd_, planeId_, DRM_MODE_OBJECT_PLANE, "SRC_H");
    planeCrtcX_ = propertyId(fd_, planeId_, DRM_MODE_OBJECT_PLANE, "CRTC_X");
    planeCrtcY_ = propertyId(fd_, planeId_, DRM_MODE_OBJECT_PLANE, "CRTC_Y");
    planeCrtcW_ = propertyId(fd_, planeId_, DRM_MODE_OBJECT_PLANE, "CRTC_W");
    planeCrtcH_ = propertyId(fd_, planeId_, DRM_MODE_OBJECT_PLANE, "CRTC_H");
    planeInFenceFd_ = propertyId(fd_, planeId_, DRM_MODE_OBJECT_PLANE, "IN_FENCE_FD");
    if (planeInFenceFd_ == 0) {
      std::fprintf(stderr,
                   "flux-compositor: KMS primary plane has no IN_FENCE_FD; atomic presenter will use CPU GPU waits\n");
    }
  }

  void createModeBlob() {
    if (drmModeCreatePropertyBlob(fd_, &connector_.mode, sizeof(connector_.mode), &modeBlob_) != 0) {
      throw std::system_error(errno, std::generic_category(), "drmModeCreatePropertyBlob");
    }
  }

  void createBuffers() {
    buffers_.reserve(kBufferCount);
    for (std::size_t i = 0; i < kBufferCount; ++i) {
      buffers_.push_back(createBuffer());
    }
  }

  Buffer createBuffer() {
    Buffer buffer{};
    if (!forceLinearScanout()) {
      buffer.bo = gbm_bo_create(gbm_,
                                connector_.mode.hdisplay,
                                connector_.mode.vdisplay,
                                GBM_FORMAT_ARGB8888,
                                GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT);
    }
    if (!buffer.bo) {
      constexpr std::uint64_t modifiers[] = {DRM_FORMAT_MOD_LINEAR};
      buffer.bo = gbm_bo_create_with_modifiers2(gbm_,
                                                connector_.mode.hdisplay,
                                                connector_.mode.vdisplay,
                                                GBM_FORMAT_ARGB8888,
                                                modifiers,
                                                1,
                                                GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT);
    }
    if (!buffer.bo) {
      buffer.bo = gbm_bo_create(gbm_,
                                connector_.mode.hdisplay,
                                connector_.mode.vdisplay,
                                GBM_FORMAT_ARGB8888,
                                GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT | GBM_BO_USE_LINEAR);
    }
    if (!buffer.bo) throw std::runtime_error("gbm_bo_create failed for KMS atomic scanout buffer");
    try {
      buffer.fbId = createFramebuffer(buffer.bo);
      importBufferToVulkan(buffer);
      if (planeInFenceFd_ != 0) buffer.renderFinished = createExportableSemaphore();
      buffer.spec = VulkanRenderTargetSpec{
          .image = buffer.image,
          .view = buffer.view,
          .format = VK_FORMAT_B8G8R8A8_UNORM,
          .width = connector_.mode.hdisplay,
          .height = connector_.mode.vdisplay,
          .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
          .finalLayout = VK_IMAGE_LAYOUT_GENERAL,
          .signalSemaphore = buffer.renderFinished,
      };
      std::fprintf(stderr,
                   "flux-compositor: atomic scanout buffer %ux%u modifier=0x%016llx stride=%u\n",
                   gbm_bo_get_width(buffer.bo),
                   gbm_bo_get_height(buffer.bo),
                   static_cast<unsigned long long>(gbmModifier(buffer.bo)),
                   gbm_bo_get_stride_for_plane(buffer.bo, 0));
    } catch (...) {
      destroyPartialBuffer(buffer);
      throw;
    }
    return buffer;
  }

  VkSemaphore createExportableSemaphore() {
    VkDevice device = flux::VulkanContext::instance().device();
    VkExportSemaphoreCreateInfo exportInfo{VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};
    exportInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    semaphoreInfo.pNext = &exportInfo;
    VkSemaphore semaphore = VK_NULL_HANDLE;
    vkCheck(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore),
            "vkCreateSemaphore atomic KMS render fence");
    return semaphore;
  }

  int exportRenderSemaphoreFd(VkSemaphore semaphore) {
    auto getSemaphoreFd =
        reinterpret_cast<PFN_vkGetSemaphoreFdKHR>(vkGetDeviceProcAddr(flux::VulkanContext::instance().device(),
                                                                       "vkGetSemaphoreFdKHR"));
    if (!getSemaphoreFd) throw std::runtime_error("vkGetSemaphoreFdKHR is unavailable");
    VkSemaphoreGetFdInfoKHR fdInfo{VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR};
    fdInfo.semaphore = semaphore;
    fdInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
    int fd = -1;
    vkCheck(getSemaphoreFd(flux::VulkanContext::instance().device(), &fdInfo, &fd),
            "vkGetSemaphoreFdKHR atomic KMS render fence");
    return fd;
  }

  std::uint32_t createFramebuffer(gbm_bo* bo) {
    std::array<std::uint32_t, 4> handles{};
    std::array<std::uint32_t, 4> strides{};
    std::array<std::uint32_t, 4> offsets{};
    std::array<std::uint64_t, 4> modifiers{};
    handles[0] = gbm_bo_get_handle_for_plane(bo, 0).u32;
    strides[0] = gbm_bo_get_stride_for_plane(bo, 0);
    offsets[0] = gbm_bo_get_offset(bo, 0);
    modifiers[0] = gbmModifier(bo);
    std::uint32_t fb = 0;
    int rc = drmModeAddFB2WithModifiers(fd_,
                                        gbm_bo_get_width(bo),
                                        gbm_bo_get_height(bo),
                                        DRM_FORMAT_ARGB8888,
                                        handles.data(),
                                        strides.data(),
                                        offsets.data(),
                                        modifiers.data(),
                                        &fb,
                                        DRM_MODE_FB_MODIFIERS);
    if (rc != 0) {
      rc = drmModeAddFB2(fd_,
                         gbm_bo_get_width(bo),
                         gbm_bo_get_height(bo),
                         DRM_FORMAT_ARGB8888,
                         handles.data(),
                         strides.data(),
                         offsets.data(),
                         &fb,
                         0);
    }
    if (rc != 0) throw std::system_error(errno, std::generic_category(), "drmModeAddFB2");
    return fb;
  }

  void importBufferToVulkan(Buffer& buffer) {
    int fd = gbm_bo_get_fd(buffer.bo);
    if (fd < 0) throw std::system_error(errno, std::generic_category(), "gbm_bo_get_fd");
    VkDevice device = flux::VulkanContext::instance().device();
    VkPhysicalDevice physical = flux::VulkanContext::instance().physicalDevice();
    try {
      std::uint64_t const modifier = gbmModifier(buffer.bo);
      VkExternalMemoryImageCreateInfo externalInfo{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
      externalInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
      VkSubresourceLayout planeLayout{};
      planeLayout.offset = gbm_bo_get_offset(buffer.bo, 0);
      planeLayout.size = static_cast<VkDeviceSize>(gbm_bo_get_stride_for_plane(buffer.bo, 0)) *
                         static_cast<VkDeviceSize>(gbm_bo_get_height(buffer.bo));
      planeLayout.rowPitch = gbm_bo_get_stride_for_plane(buffer.bo, 0);
      planeLayout.arrayPitch = planeLayout.size;
      planeLayout.depthPitch = planeLayout.size;
      VkImageDrmFormatModifierExplicitCreateInfoEXT modifierInfo{
          VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT};
      modifierInfo.pNext = &externalInfo;
      modifierInfo.drmFormatModifier = modifier;
      modifierInfo.drmFormatModifierPlaneCount = 1;
      modifierInfo.pPlaneLayouts = &planeLayout;

      VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
      imageInfo.pNext = &modifierInfo;
      imageInfo.imageType = VK_IMAGE_TYPE_2D;
      imageInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
      imageInfo.extent = {gbm_bo_get_width(buffer.bo), gbm_bo_get_height(buffer.bo), 1};
      imageInfo.mipLevels = 1;
      imageInfo.arrayLayers = 1;
      imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
      imageInfo.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
      imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                        VK_IMAGE_USAGE_SAMPLED_BIT;
      imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      vkCheck(vkCreateImage(device, &imageInfo, nullptr, &buffer.image), "vkCreateImage atomic KMS buffer");

      auto getMemoryFdProperties = reinterpret_cast<PFN_vkGetMemoryFdPropertiesKHR>(
          vkGetDeviceProcAddr(device, "vkGetMemoryFdPropertiesKHR"));
      if (!getMemoryFdProperties) throw std::runtime_error("vkGetMemoryFdPropertiesKHR is unavailable");
      VkMemoryFdPropertiesKHR fdProps{VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};
      vkCheck(getMemoryFdProperties(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, fd, &fdProps),
              "vkGetMemoryFdPropertiesKHR atomic KMS buffer");
      VkMemoryRequirements requirements{};
      vkGetImageMemoryRequirements(device, buffer.image, &requirements);
      VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
      dedicated.image = buffer.image;
      VkImportMemoryFdInfoKHR importInfo{VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR};
      importInfo.pNext = &dedicated;
      importInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
      importInfo.fd = fd;
      VkMemoryAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
      allocateInfo.pNext = &importInfo;
      allocateInfo.allocationSize = requirements.size;
      allocateInfo.memoryTypeIndex =
          findVulkanMemoryType(physical, requirements.memoryTypeBits & fdProps.memoryTypeBits, 0);
      vkCheck(vkAllocateMemory(device, &allocateInfo, nullptr, &buffer.memory),
              "vkAllocateMemory atomic KMS buffer");
      fd = -1;
      vkCheck(vkBindImageMemory(device, buffer.image, buffer.memory, 0), "vkBindImageMemory atomic KMS buffer");
      VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      viewInfo.image = buffer.image;
      viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
      viewInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
      viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      viewInfo.subresourceRange.levelCount = 1;
      viewInfo.subresourceRange.layerCount = 1;
      vkCheck(vkCreateImageView(device, &viewInfo, nullptr, &buffer.view),
              "vkCreateImageView atomic KMS buffer");
    } catch (...) {
      if (fd >= 0) close(fd);
      throw;
    }
  }

  void waitForPageFlip() {
    while (pageFlipPending_) {
      pollfd pfd{.fd = fd_, .events = POLLIN, .revents = 0};
      int const rc = poll(&pfd, 1, 1000);
      if (rc < 0) {
        if (errno == EINTR) continue;
        throw std::system_error(errno, std::generic_category(), "poll KMS page flip");
      }
      if (rc == 0) throw std::runtime_error("KMS atomic page flip timed out");
      drmEventContext eventContext{};
      eventContext.version = DRM_EVENT_CONTEXT_VERSION;
      eventContext.page_flip_handler = pageFlipHandler;
      if (drmHandleEvent(fd_, &eventContext) != 0) {
        throw std::system_error(errno, std::generic_category(), "drmHandleEvent");
      }
      if (pendingTiming_.hardware) {
        displayedBuffer_ = pendingBuffer_;
        pendingBuffer_ = -1;
        pageFlipPending_ = false;
      }
    }
  }

  void destroyPartialBuffer(Buffer& buffer) {
    VkDevice device = flux::VulkanContext::instance().device();
    if (buffer.view) vkDestroyImageView(device, buffer.view, nullptr);
    if (buffer.renderFinished) vkDestroySemaphore(device, buffer.renderFinished, nullptr);
    if (buffer.image) vkDestroyImage(device, buffer.image, nullptr);
    if (buffer.memory) vkFreeMemory(device, buffer.memory, nullptr);
    if (buffer.fbId) drmModeRmFB(fd_, buffer.fbId);
    if (buffer.bo) gbm_bo_destroy(buffer.bo);
    buffer = {};
  }

  static constexpr std::size_t kBufferCount = 4;

  int fd_ = -1;
  KmsConnector connector_{};
  TextSystem& textSystem_;
  gbm_device* gbm_ = nullptr;
  std::uint32_t planeId_ = 0;
  std::uint32_t modeBlob_ = 0;
  std::uint32_t connectorCrtcId_ = 0;
  std::uint32_t crtcModeId_ = 0;
  std::uint32_t crtcActive_ = 0;
  std::uint32_t planeFbId_ = 0;
  std::uint32_t planeCrtcId_ = 0;
  std::uint32_t planeSrcX_ = 0;
  std::uint32_t planeSrcY_ = 0;
  std::uint32_t planeSrcW_ = 0;
  std::uint32_t planeSrcH_ = 0;
  std::uint32_t planeCrtcX_ = 0;
  std::uint32_t planeCrtcY_ = 0;
  std::uint32_t planeCrtcW_ = 0;
  std::uint32_t planeCrtcH_ = 0;
  std::uint32_t planeInFenceFd_ = 0;
  bool useRenderInFence_ = false;
  bool modesetDone_ = false;
  bool pageFlipPending_ = false;
  int displayedBuffer_ = -1;
  int pendingBuffer_ = -1;
  int renderBuffer_ = -1;
  std::uint32_t nextPresentId_ = 1;
  KmsAtomicPresenter::PageFlipTiming pendingTiming_{};
  std::vector<Buffer> buffers_;
  std::unique_ptr<Canvas> canvas_;
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

KmsAtomicPresenter::KmsAtomicPresenter(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
KmsAtomicPresenter::~KmsAtomicPresenter() = default;
KmsAtomicPresenter::KmsAtomicPresenter(KmsAtomicPresenter&&) noexcept = default;
KmsAtomicPresenter& KmsAtomicPresenter::operator=(KmsAtomicPresenter&&) noexcept = default;

Canvas& KmsAtomicPresenter::canvas() {
  if (!impl_) throw std::runtime_error("Invalid KMS atomic presenter");
  return impl_->canvas();
}

void KmsAtomicPresenter::prepareFrame() {
  if (impl_) impl_->prepareFrame();
}

void KmsAtomicPresenter::markFrameRendered() {
  if (impl_) impl_->markFrameRendered();
}

bool KmsAtomicPresenter::updateRenderReady() {
  return impl_ ? impl_->updateRenderReady() : true;
}

bool KmsAtomicPresenter::canSchedulePresent() {
  return impl_ && impl_->canSchedulePresent();
}

int KmsAtomicPresenter::renderReadyFd() const noexcept {
  return impl_ ? impl_->renderReadyFd() : -1;
}

std::uint32_t KmsAtomicPresenter::schedulePresent() {
  return impl_ ? impl_->schedulePresent() : 0u;
}

KmsAtomicPresenter::PageFlipTiming KmsAtomicPresenter::present() {
  if (!impl_) return {};
  return impl_->present();
}

std::optional<KmsAtomicPresenter::PageFlipTiming> KmsAtomicPresenter::dispatchPageFlipEvents() {
  return impl_ ? impl_->dispatchPageFlipEvents() : std::nullopt;
}

bool KmsAtomicPresenter::hasPendingPageFlip() const noexcept {
  return impl_ && impl_->hasPendingPageFlip();
}

int KmsAtomicPresenter::eventFd() const noexcept {
  return impl_ ? impl_->eventFd() : -1;
}

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

KmsOutput::VblankTiming KmsOutput::waitForVblank() const {
  if (impl_ && !impl_->vblankWaitDisabled_) {
    drmVBlank vblank{};
    vblank.request.type = DRM_VBLANK_RELATIVE;
    vblank.request.sequence = 1;
    if (drmWaitVBlank(impl_->drmFd(), &vblank) == 0) {
      std::uint64_t const sec = static_cast<std::uint64_t>(vblank.reply.tval_sec);
      std::uint64_t const usec = static_cast<std::uint64_t>(vblank.reply.tval_usec);
      return VblankTiming{
          .hardware = true,
          .sequence = static_cast<std::uint64_t>(vblank.reply.sequence),
          .monotonicNsec = sec * 1'000'000'000ull + usec * 1'000ull,
      };
    }
    impl_->vblankWaitDisabled_ = true;
    std::fprintf(stderr,
                 "[flux:kms] drmWaitVBlank failed for connector %s: %s; falling back to timer pacing.\n",
                 impl_->connector_.name.c_str(),
                 std::strerror(errno));
  }
  std::this_thread::sleep_for(frameInterval(refreshRateMilliHz()));
  return VblankTiming{
      .hardware = false,
      .sequence = 0,
      .monotonicNsec = monotonicNowNsec(),
  };
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
    hideCursor();
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
  if (!impl_->cursorVisible_) {
    drmModeSetCursor(fd, impl_->connector_.crtcId, 0, 0, 0);
  }
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

std::unique_ptr<KmsAtomicPresenter> KmsOutput::createAtomicPresenter(TextSystem& textSystem) const {
  if (!impl_) return nullptr;
  return std::unique_ptr<KmsAtomicPresenter>(
      new KmsAtomicPresenter(std::make_unique<KmsAtomicPresenter::Impl>(impl_->drmFd(), impl_->connector_, textSystem)));
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

void KmsDevice::acknowledgeVtAcquire() {
  if (impl_ && impl_->app_) impl_->app_->acknowledgePendingVtAcquire();
}

bool KmsDevice::pollEvents(int timeoutMs, std::span<int const> extraFds) {
  return impl_ && impl_->app_ ? impl_->app_->pollInputAndWake(timeoutMs, extraFds) : false;
}

KmsPollResult KmsDevice::pollEventDetails(int timeoutMs, std::span<int const> extraFds) {
  return impl_ && impl_->app_ ? impl_->app_->pollInputAndWakeDetailed(timeoutMs, extraFds) : KmsPollResult{};
}

} // namespace flux::platform
