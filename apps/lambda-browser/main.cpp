#include <Lambda.hpp>
#include <Lambda/Graphics/VulkanContext.hpp>
#include <Lambda/UI/EventQueue.hpp>
#include <Lambda/Reactive/Signal.hpp>
#include <Lambda/UI/Shortcut.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/UI.hpp>
#include <Lambda/UI/Views/Views.hpp>

#include <wpe/unstable/fdo-dmabuf.h>
#include <wpe/webkit.h>

#include <drm_fourcc.h>
#include <gbm.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

using namespace lambda;

namespace {

bool hasUriScheme(std::string_view text) {
  auto const colon = text.find(':');
  if (colon == std::string_view::npos || colon == 0) return false;
  for (char const ch : text.substr(0, colon)) {
    bool const valid = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                       (ch >= '0' && ch <= '9') || ch == '+' || ch == '-' || ch == '.';
    if (!valid) return false;
  }
  return true;
}

std::string trim(std::string text) {
  auto const begin = std::find_if_not(text.begin(), text.end(), [](unsigned char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
  });
  auto const end = std::find_if_not(text.rbegin(), text.rend(), [](unsigned char ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
  }).base();
  if (begin >= end) return {};
  return std::string(begin, end);
}

std::string normalizeUserUri(std::string text) {
  text = trim(std::move(text));
  if (text.empty() || hasUriScheme(text)) return text;
  if (text.starts_with("localhost") || text.starts_with("127.0.0.1") || text.starts_with("[::1]")) {
    return "http://" + text;
  }
  return "https://" + text;
}

std::string safeString(char const* value) {
  return value ? std::string(value) : std::string{};
}

std::string fourccString(std::uint32_t format) {
  std::array<char, 5> text{
      static_cast<char>(format & 0xffu),
      static_cast<char>((format >> 8u) & 0xffu),
      static_cast<char>((format >> 16u) & 0xffu),
      static_cast<char>((format >> 24u) & 0xffu),
      '\0',
  };
  for (std::size_t i = 0; i < 4; ++i) {
    if (text[i] < 32 || text[i] > 126) text[i] = '?';
  }
  return std::string{text.data(), 4};
}

#if LAMBDA_VULKAN
bool configureBrowserVulkanDmabufImport() {
  try {
    auto& vulkan = VulkanContext::instance();
    vulkan.addRequiredDeviceExtension(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
    vulkan.addRequiredDeviceExtension(VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
    vulkan.addRequiredDeviceExtension(VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME);
    std::fputs("lambda-browser: Vulkan dmabuf import extensions configured\n", stderr);
    return true;
  } catch (std::exception const& error) {
    std::fprintf(stderr, "lambda-browser: failed to configure Vulkan dmabuf import: %s\n", error.what());
    return false;
  }
}
#endif

bool initializeWpeBackend() {
  if (!wpe_loader_get_loaded_implementation_library_name() &&
      !wpe_loader_init("libWPEBackend-fdo-1.0.so")) {
    std::fputs("lambda-browser: failed to initialize WPE FDO backend\n", stderr);
    return false;
  }

  if (wpe_fdo_initialize_dmabuf()) {
    std::fputs("lambda-browser: WPE FDO dmabuf rendering enabled\n", stderr);
    return true;
  }

  std::fputs("lambda-browser: failed to initialize WPE FDO dmabuf support\n", stderr);
  return false;
}

class BrowserController;

struct DmabufPoolBackendData {
  wpe_view_backend_dmabuf_pool_fdo* pool = nullptr;
  BrowserController* browser = nullptr;
};

std::uint32_t wpeSize(float value) {
  return static_cast<std::uint32_t>(std::max(1.f, std::round(value)));
}

constexpr Size kInitialWindowSize{1000.f, 720.f};
constexpr Size kInitialBrowserViewport{1000.f, 620.f};
constexpr std::string_view kDefaultUri = "https://www.google.com";

struct BrowserSnapshot {
  std::string uri;
  std::string title;
  double progress = 0.0;
  bool loading = false;
  bool canGoBack = false;
  bool canGoForward = false;
};

#if LAMBDA_VULKAN
struct BrowserDmabufFrame {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t format = 0;
  std::vector<Image::DmabufPlane> planes;
  wpe_dmabuf_pool_entry* poolEntry = nullptr;
  std::uint64_t serial = 0;
};

void closeDmabufFrame(BrowserDmabufFrame& frame) {
  for (auto& plane : frame.planes) {
    if (plane.fd >= 0) {
      close(plane.fd);
      plane.fd = -1;
    }
  }
  frame.planes.clear();
}

struct BrowserPoolEntry {
  gbm_bo* bo = nullptr;
  int fd = -1;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t format = DRM_FORMAT_ARGB8888;
  std::uint32_t stride = 0;
  std::uint32_t offset = 0;
  std::uint64_t modifier = DRM_FORMAT_MOD_INVALID;
};
#endif

#if LAMBDA_VULKAN
std::unique_ptr<BrowserPoolEntry> makePoolEntry(gbm_device* device, std::uint32_t width, std::uint32_t height) {
  if (!device || width == 0 || height == 0) return nullptr;
  auto entry = std::make_unique<BrowserPoolEntry>();
  entry->width = width;
  entry->height = height;
  entry->format = DRM_FORMAT_ARGB8888;
  std::uint64_t const modifiers[] = {DRM_FORMAT_MOD_LINEAR};
  entry->bo = gbm_bo_create_with_modifiers(device, width, height, GBM_FORMAT_ARGB8888, modifiers, 1);
  if (!entry->bo) {
    entry->bo = gbm_bo_create(device,
                              width,
                              height,
                              GBM_FORMAT_ARGB8888,
                              GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);
  }
  if (!entry->bo) {
    std::fprintf(stderr, "lambda-browser: failed to allocate GBM dmabuf %ux%u\n", width, height);
    return nullptr;
  }
  entry->fd = gbm_bo_get_fd(entry->bo);
  if (entry->fd < 0) {
    std::fprintf(stderr, "lambda-browser: failed to export GBM dmabuf fd %ux%u\n", width, height);
    gbm_bo_destroy(entry->bo);
    return nullptr;
  }
  entry->stride = gbm_bo_get_stride_for_plane(entry->bo, 0);
  if (entry->stride == 0) entry->stride = gbm_bo_get_stride(entry->bo);
  entry->offset = gbm_bo_get_offset(entry->bo, 0);
  entry->modifier = gbm_bo_get_modifier(entry->bo);
  if (entry->modifier == DRM_FORMAT_MOD_INVALID) entry->modifier = DRM_FORMAT_MOD_LINEAR;
  return entry;
}

int openBrowserRenderNode() {
  std::array<char const*, 5> candidates{
      "/dev/dri/renderD128",
      "/dev/dri/renderD129",
      "/dev/dri/renderD130",
      "/dev/dri/card1",
      "/dev/dri/card0",
  };
  for (char const* path : candidates) {
    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd >= 0) {
      std::fprintf(stderr, "lambda-browser: using DRM device %s for WPE dmabuf pool\n", path);
      return fd;
    }
  }
  std::fputs("lambda-browser: failed to open a DRM render node for WPE dmabuf pool\n", stderr);
  return -1;
}
#endif

struct wpe_dmabuf_pool_entry* createDmabufPoolEntry(void* data);
void destroyDmabufPoolEntry(void* data, wpe_dmabuf_pool_entry* entry);
void commitDmabufPoolEntry(void* data, wpe_dmabuf_pool_entry* entry);
void destroyDmabufPoolBackendData(void* data);

constexpr wpe_view_backend_dmabuf_pool_fdo_client dmabufPoolClient{
    .create_entry = createDmabufPoolEntry,
    .destroy_entry = destroyDmabufPoolEntry,
    .commit_entry = commitDmabufPoolEntry,
};

class BrowserController {
public:
  BrowserController(std::string initialUri, Size initialSize) {
    drmFd_ = openBrowserRenderNode();
    if (drmFd_ < 0) return;
    gbmDevice_ = gbm_create_device(drmFd_);
    if (!gbmDevice_) {
      std::fputs("lambda-browser: failed to create GBM device for WPE dmabuf pool\n", stderr);
      close(drmFd_);
      drmFd_ = -1;
      return;
    }

    auto* backendData = new DmabufPoolBackendData;
    backendData->browser = this;
    viewportWidth_ = wpeSize(initialSize.width);
    viewportHeight_ = wpeSize(initialSize.height);
    backendData->pool = wpe_view_backend_dmabuf_pool_fdo_create(
        &dmabufPoolClient, backendData, viewportWidth_, viewportHeight_);
    dmabufPoolBackend_ = backendData->pool;
    wpeBackend_ = backendData->pool
                      ? wpe_view_backend_dmabuf_pool_fdo_get_view_backend(backendData->pool)
                      : nullptr;
    if (!wpeBackend_) {
      std::fputs("lambda-browser: failed to create WPE FDO dmabuf pool view backend\n", stderr);
      destroyDmabufPoolBackendData(backendData);
      return;
    }
    wpe_view_backend_add_activity_state(wpeBackend_,
                                        wpe_view_activity_state_visible |
                                            wpe_view_activity_state_focused |
                                            wpe_view_activity_state_in_window);
    wpe_view_backend_dispatch_set_size(wpeBackend_, viewportWidth_, viewportHeight_);

    auto* webkitBackend = webkit_web_view_backend_new(wpeBackend_, destroyDmabufPoolBackendData, backendData);
    if (!webkitBackend) {
      std::fputs("lambda-browser: failed to create WebKit view backend\n", stderr);
      destroyDmabufPoolBackendData(backendData);
      return;
    }

    webView_ = webkit_web_view_new(webkitBackend);
    if (!webView_) {
      std::fputs("lambda-browser: failed to create WebKit web view\n", stderr);
      return;
    }

    g_signal_connect(webView_, "load-changed", G_CALLBACK(+[](WebKitWebView*, WebKitLoadEvent, gpointer data) {
                       static_cast<BrowserController*>(data)->refreshSnapshot();
                     }),
                     this);
    g_signal_connect(webView_, "notify::uri", G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
                       static_cast<BrowserController*>(data)->refreshSnapshot();
                     }),
                     this);
    g_signal_connect(webView_, "notify::title", G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
                       static_cast<BrowserController*>(data)->refreshSnapshot();
                     }),
                     this);
    g_signal_connect(webView_, "notify::estimated-load-progress",
                     G_CALLBACK(+[](GObject*, GParamSpec*, gpointer data) {
                       static_cast<BrowserController*>(data)->refreshSnapshot();
                     }),
                     this);
    load(std::move(initialUri));
    refreshSnapshot();
  }

  ~BrowserController() {
#if LAMBDA_VULKAN
    {
      std::lock_guard lock(frameMutex_);
      if (pendingDmabufFrame_) {
        releasePoolEntry(pendingDmabufFrame_->poolEntry);
        closeDmabufFrame(*pendingDmabufFrame_);
        pendingDmabufFrame_.reset();
      }
    }
    releaseRetainedPoolEntries(true);
    flushPoolEntryReleases();
#endif
    if (webView_) {
      g_object_unref(webView_);
      webView_ = nullptr;
    }
    if (gbmDevice_) {
      gbm_device_destroy(gbmDevice_);
      gbmDevice_ = nullptr;
    }
    if (drmFd_ >= 0) {
      close(drmFd_);
      drmFd_ = -1;
    }
  }

  BrowserController(BrowserController const&) = delete;
  BrowserController& operator=(BrowserController const&) = delete;

  void load(std::string uri) {
    uri = normalizeUserUri(std::move(uri));
    if (uri.empty() || !webView_) return;
    webkit_web_view_load_uri(webView_, uri.c_str());
    snapshot_.uri = std::move(uri);
  }

  void goBack() {
    if (webView_ && webkit_web_view_can_go_back(webView_)) {
      webkit_web_view_go_back(webView_);
    }
  }

  void goForward() {
    if (webView_ && webkit_web_view_can_go_forward(webView_)) {
      webkit_web_view_go_forward(webView_);
    }
  }

  void reload() {
    if (webView_) webkit_web_view_reload(webView_);
  }

  void resize(Size size) {
    if (!wpeBackend_) return;
    std::uint32_t const width = wpeSize(size.width);
    std::uint32_t const height = wpeSize(size.height);
    if (width == viewportWidth_ && height == viewportHeight_) return;
    viewportWidth_ = width;
    viewportHeight_ = height;
    wpe_view_backend_dispatch_set_size(wpeBackend_, width, height);
    frameRevision_.set(++frameSerial_);
    if (Application::hasInstance()) {
      Application::instance().requestRedraw();
    }
  }

#if LAMBDA_VULKAN
  wpe_dmabuf_pool_entry* createPoolEntry() {
    auto data = makePoolEntry(gbmDevice_, viewportWidth_, viewportHeight_);
    if (!data) return nullptr;

    int const wpeFd = dup(data->fd);
    if (wpeFd < 0) {
      std::fprintf(stderr, "lambda-browser: failed to duplicate GBM dmabuf fd for WPE pool entry\n");
      close(data->fd);
      gbm_bo_destroy(data->bo);
      return nullptr;
    }

    wpe_dmabuf_pool_entry_init init{};
    init.width = data->width;
    init.height = data->height;
    init.format = data->format;
    init.num_planes = 1;
    init.fds[0] = wpeFd;
    init.strides[0] = data->stride;
    init.offsets[0] = data->offset;
    init.modifiers[0] = data->modifier;

    wpe_dmabuf_pool_entry* entry = wpe_dmabuf_pool_entry_create(&init);
    if (!entry) {
      std::fprintf(stderr,
                   "lambda-browser: WPE rejected GBM pool entry size=%ux%u format=%s/0x%08x stride=%u "
                   "modifier=0x%016llx\n",
                   data->width,
                   data->height,
                   fourccString(data->format).c_str(),
                   data->format,
                   data->stride,
                   static_cast<unsigned long long>(data->modifier));
      close(wpeFd);
      close(data->fd);
      gbm_bo_destroy(data->bo);
      return nullptr;
    }

    BrowserPoolEntry* rawData = data.release();
    wpe_dmabuf_pool_entry_set_user_data(entry, rawData);
    ++dmabufPoolCreates_;
    if (dmabufPoolCreates_ <= 12 || dmabufPoolCreates_ % 120 == 0) {
      std::fprintf(stderr,
                   "lambda-browser: created WPE dmabuf pool entry #%llu size=%ux%u format=%s/0x%08x "
                   "stride=%u modifier=0x%016llx\n",
                   static_cast<unsigned long long>(dmabufPoolCreates_),
                   rawData->width,
                   rawData->height,
                   fourccString(rawData->format).c_str(),
                   rawData->format,
                   rawData->stride,
                   static_cast<unsigned long long>(rawData->modifier));
    }
    return entry;
  }

  void destroyPoolEntry(wpe_dmabuf_pool_entry* entry) {
    if (!entry) return;
    auto* data = static_cast<BrowserPoolEntry*>(wpe_dmabuf_pool_entry_get_user_data(entry));
    wpe_dmabuf_pool_entry_set_user_data(entry, nullptr);
    wpe_dmabuf_pool_entry_destroy(entry);
    if (data) {
      if (data->fd >= 0) close(data->fd);
      if (data->bo) gbm_bo_destroy(data->bo);
      delete data;
    }
    ++dmabufPoolDestroys_;
    if (dmabufPoolDestroys_ <= 12 || dmabufPoolDestroys_ % 120 == 0) {
      std::fprintf(stderr,
                   "lambda-browser: destroyed WPE dmabuf pool entry #%llu\n",
                   static_cast<unsigned long long>(dmabufPoolDestroys_));
    }
  }

  void commitPoolEntry(wpe_dmabuf_pool_entry* entry) {
    auto* data = entry ? static_cast<BrowserPoolEntry*>(wpe_dmabuf_pool_entry_get_user_data(entry)) : nullptr;
    if (!data || data->fd < 0 || data->width == 0 || data->height == 0 || data->stride == 0) {
      std::fprintf(stderr, "lambda-browser: ignoring invalid WPE dmabuf pool commit\n");
      releasePoolEntry(entry);
      if (dmabufPoolBackend_) {
        wpe_view_backend_dmabuf_pool_fdo_dispatch_frame_complete(dmabufPoolBackend_);
      }
      return;
    }

    int const fd = dup(data->fd);
    if (fd < 0) {
      std::fprintf(stderr, "lambda-browser: failed to duplicate committed WPE dmabuf fd\n");
      releasePoolEntry(entry);
      if (dmabufPoolBackend_) {
        wpe_view_backend_dmabuf_pool_fdo_dispatch_frame_complete(dmabufPoolBackend_);
      }
      return;
    }

    BrowserDmabufFrame next;
    next.width = data->width;
    next.height = data->height;
    next.format = data->format;
    next.poolEntry = entry;
    next.serial = ++frameSerial_;
    next.planes.push_back(Image::DmabufPlane{
        .fd = fd,
        .offset = data->offset,
        .stride = data->stride,
        .modifier = data->modifier,
    });

    ++dmabufFramesReceived_;
    ++dmabufPoolCommits_;
    if (dmabufFramesReceived_ <= 12 || dmabufFramesReceived_ % 120 == 0) {
      Image::DmabufPlane const& plane = next.planes.front();
      std::fprintf(stderr,
                   "lambda-browser: committed WPE dmabuf pool frame #%llu serial=%llu size=%ux%u "
                   "format=%s/0x%08x stride=%u offset=%u modifier=0x%016llx\n",
                   static_cast<unsigned long long>(dmabufFramesReceived_),
                   static_cast<unsigned long long>(next.serial),
                   next.width,
                   next.height,
                   fourccString(next.format).c_str(),
                   next.format,
                   plane.stride,
                   plane.offset,
                   static_cast<unsigned long long>(plane.modifier));
    }

    std::uint64_t const serial = next.serial;
    {
      std::lock_guard lock(frameMutex_);
      if (pendingDmabufFrame_) {
        releasePoolEntry(pendingDmabufFrame_->poolEntry);
        closeDmabufFrame(*pendingDmabufFrame_);
      }
      pendingDmabufFrame_ = std::move(next);
    }
    frameRevision_.set(serial);
    if (Application::hasInstance()) {
      Application::instance().requestRedraw();
    }
  }

  void releasePoolEntry(wpe_dmabuf_pool_entry* entry) {
    if (!entry || !dmabufPoolBackend_) return;
    if (std::find(pendingPoolReleases_.begin(), pendingPoolReleases_.end(), entry) ==
        pendingPoolReleases_.end()) {
      pendingPoolReleases_.push_back(entry);
    }
  }

  void flushPoolEntryReleases() {
    if (!dmabufPoolBackend_ || pendingPoolReleases_.empty()) return;
    auto releases = std::exchange(pendingPoolReleases_, {});
    for (auto* entry : releases) {
      if (!entry) continue;
      wpe_view_backend_dmabuf_pool_fdo_dispatch_release_entry(dmabufPoolBackend_, entry);
      ++dmabufPoolReleases_;
      if (dmabufPoolReleases_ <= 12 || dmabufPoolReleases_ % 120 == 0) {
        std::fprintf(stderr,
                     "lambda-browser: released WPE dmabuf pool entry #%llu\n",
                     static_cast<unsigned long long>(dmabufPoolReleases_));
      }
    }
  }

  void releaseRetainedPoolEntries(bool all = false) {
    if (all) {
      if (currentPoolEntry_) {
        releasePoolEntry(currentPoolEntry_);
        currentPoolEntry_ = nullptr;
      }
      image_.reset();
      for (auto& retained : retainedDmabufImages_) {
        releasePoolEntry(retained.entry);
      }
      retainedDmabufImages_.clear();
      return;
    }

    while (retainedDmabufImages_.size() > kRetainedDmabufImageCount) {
      releasePoolEntry(retainedDmabufImages_.front().entry);
      retainedDmabufImages_.erase(retainedDmabufImages_.begin());
    }
  }
#endif

  void draw(Canvas& canvas, Rect frame) {
    frameRevision_.evaluate();
    resize(Size{frame.width, frame.height});

#if LAMBDA_VULKAN
    BrowserDmabufFrame nextDmabuf;
    bool hasNextDmabuf = false;
#endif
    {
      std::lock_guard lock(frameMutex_);
#if LAMBDA_VULKAN
      std::uint64_t const dmabufSerial = pendingDmabufFrame_ ? pendingDmabufFrame_->serial : 0;
      if (dmabufSerial != 0 && dmabufSerial != uploadedFrameSerial_) {
        nextDmabuf = std::move(*pendingDmabufFrame_);
        pendingDmabufFrame_.reset();
        hasNextDmabuf = true;
      }
#endif
    }

#if LAMBDA_VULKAN
    if (hasNextDmabuf) {
      std::shared_ptr<Image> imported;
      try {
        std::span<Image::DmabufPlane const> planes{nextDmabuf.planes.data(), nextDmabuf.planes.size()};
        imported = Image::fromDmabuf(Image::DmabufImageSpec{
            .width = nextDmabuf.width,
            .height = nextDmabuf.height,
            .drmFormat = nextDmabuf.format,
            .planes = planes,
        });
      } catch (std::exception const& error) {
        std::fprintf(stderr, "lambda-browser: WPE dmabuf import failed: %s\n", error.what());
      }
      for (auto& plane : nextDmabuf.planes) plane.fd = -1;
      closeDmabufFrame(nextDmabuf);
      if (imported) {
        ++dmabufFramesImported_;
        if (dmabufFramesImported_ <= 12 || dmabufFramesImported_ % 120 == 0) {
          std::fprintf(stderr,
                       "lambda-browser: imported WPE dmabuf frame #%llu serial=%llu size=%ux%u format=%s/0x%08x\n",
                       static_cast<unsigned long long>(dmabufFramesImported_),
                       static_cast<unsigned long long>(nextDmabuf.serial),
                       nextDmabuf.width,
                       nextDmabuf.height,
                       fourccString(nextDmabuf.format).c_str(),
                       nextDmabuf.format);
        }
        if (image_ || currentPoolEntry_) {
          retainedDmabufImages_.push_back(RetainedDmabufImage{
              .image = image_,
              .entry = currentPoolEntry_,
          });
        }
        image_ = std::move(imported);
        currentPoolEntry_ = nextDmabuf.poolEntry;
        imageWidth_ = nextDmabuf.width;
        imageHeight_ = nextDmabuf.height;
        uploadedFrameSerial_ = nextDmabuf.serial;
        releaseRetainedPoolEntries();
      } else {
        releasePoolEntry(nextDmabuf.poolEntry);
        ++dmabufImportFailures_;
        std::fprintf(stderr,
                     "lambda-browser: WPE dmabuf import returned no image failure=%llu serial=%llu "
                     "size=%ux%u format=%s/0x%08x\n",
                     static_cast<unsigned long long>(dmabufImportFailures_),
                     static_cast<unsigned long long>(nextDmabuf.serial),
                     nextDmabuf.width,
                     nextDmabuf.height,
                     fourccString(nextDmabuf.format).c_str(),
                     nextDmabuf.format);
      }
      if (dmabufPoolBackend_) {
        wpe_view_backend_dmabuf_pool_fdo_dispatch_frame_complete(dmabufPoolBackend_);
      }
    }
#endif

    if (image_ && imageWidth_ > 0 && imageHeight_ > 0) {
      canvas.drawRect(frame, CornerRadius{}, FillStyle::solid(Color(1.f, 1.f, 1.f, 1.f)), StrokeStyle::none());
      float const imageWidth = static_cast<float>(imageWidth_);
      float const imageHeight = static_cast<float>(imageHeight_);
      float const contentWidth = std::min(imageWidth, frame.width);
      float const contentHeight = std::min(imageHeight, frame.height);
      Rect const contentRect{frame.x, frame.y, contentWidth, contentHeight};
      canvas.drawImage(*image_, Rect{0.f, 0.f, contentWidth, contentHeight}, contentRect, CornerRadius{}, 1.f);
      if (wpeBackend_ && displayedFrameSerial_ != uploadedFrameSerial_) {
        displayedFrameSerial_ = uploadedFrameSerial_;
        wpe_view_backend_dispatch_frame_displayed(wpeBackend_);
      }
    } else {
      ++blankDraws_;
      if (blankDraws_ <= 12 || blankDraws_ % 120 == 0) {
        std::fprintf(stderr,
                     "lambda-browser: no imported dmabuf image yet blankDraw=%llu received=%llu imported=%llu "
                     "failures=%llu uploadedSerial=%llu\n",
                     static_cast<unsigned long long>(blankDraws_),
                     static_cast<unsigned long long>(dmabufFramesReceived_),
                     static_cast<unsigned long long>(dmabufFramesImported_),
                     static_cast<unsigned long long>(dmabufImportFailures_),
                     static_cast<unsigned long long>(uploadedFrameSerial_));
      }
      canvas.drawRect(frame, CornerRadius{}, FillStyle::solid(Color(1.f, 1.f, 1.f, 1.f)), StrokeStyle::none());
    }
  }

  BrowserSnapshot snapshot() const {
    return snapshot_;
  }

  void refreshSnapshot() {
    if (!webView_) return;
    snapshot_ = BrowserSnapshot{
        .uri = safeString(webkit_web_view_get_uri(webView_)),
        .title = safeString(webkit_web_view_get_title(webView_)),
        .progress = webkit_web_view_get_estimated_load_progress(webView_),
        .loading = webkit_web_view_is_loading(webView_) != FALSE,
        .canGoBack = webkit_web_view_can_go_back(webView_) != FALSE,
        .canGoForward = webkit_web_view_can_go_forward(webView_) != FALSE,
    };
    if (Application::hasInstance()) {
      Application::instance().requestRedraw();
    }
  }

private:
  wpe_view_backend* wpeBackend_ = nullptr;
  std::uint32_t viewportWidth_ = 0;
  std::uint32_t viewportHeight_ = 0;
  WebKitWebView* webView_ = nullptr;
  BrowserSnapshot snapshot_{};
  std::mutex frameMutex_;
#if LAMBDA_VULKAN
  std::optional<BrowserDmabufFrame> pendingDmabufFrame_{};
  wpe_view_backend_dmabuf_pool_fdo* dmabufPoolBackend_ = nullptr;
  int drmFd_ = -1;
  gbm_device* gbmDevice_ = nullptr;
#endif
  std::uint64_t frameSerial_ = 0;
  std::uint64_t uploadedFrameSerial_ = 0;
  std::uint64_t displayedFrameSerial_ = 0;
  Signal<std::uint64_t> frameRevision_{0};
  std::shared_ptr<Image> image_;
  static constexpr std::size_t kRetainedDmabufImageCount = 8;
  struct RetainedDmabufImage {
    std::shared_ptr<Image> image;
    wpe_dmabuf_pool_entry* entry = nullptr;
  };
	  std::vector<RetainedDmabufImage> retainedDmabufImages_;
	  std::vector<wpe_dmabuf_pool_entry*> pendingPoolReleases_;
	  wpe_dmabuf_pool_entry* currentPoolEntry_ = nullptr;
  std::uint32_t imageWidth_ = 0;
  std::uint32_t imageHeight_ = 0;
  std::uint64_t dmabufFramesReceived_ = 0;
  std::uint64_t dmabufFramesImported_ = 0;
  std::uint64_t dmabufImportFailures_ = 0;
  std::uint64_t blankDraws_ = 0;
  std::uint64_t dmabufPoolCreates_ = 0;
  std::uint64_t dmabufPoolCommits_ = 0;
  std::uint64_t dmabufPoolReleases_ = 0;
  std::uint64_t dmabufPoolDestroys_ = 0;
};

struct wpe_dmabuf_pool_entry* createDmabufPoolEntry(void* data) {
  auto* backendData = static_cast<DmabufPoolBackendData*>(data);
  return backendData && backendData->browser ? backendData->browser->createPoolEntry() : nullptr;
}

void destroyDmabufPoolEntry(void* data, wpe_dmabuf_pool_entry* entry) {
  auto* backendData = static_cast<DmabufPoolBackendData*>(data);
  if (backendData && backendData->browser) {
    backendData->browser->destroyPoolEntry(entry);
  }
}

void commitDmabufPoolEntry(void* data, wpe_dmabuf_pool_entry* entry) {
  auto* backendData = static_cast<DmabufPoolBackendData*>(data);
  if (backendData && backendData->browser) {
    backendData->browser->commitPoolEntry(entry);
  }
}

void destroyDmabufPoolBackendData(void* data) {
  auto* backendData = static_cast<DmabufPoolBackendData*>(data);
  if (!backendData) return;
  if (backendData->pool) {
    wpe_view_backend_dmabuf_pool_fdo_destroy(backendData->pool);
  }
  delete backendData;
}

void drainWpeEvents() {
  GMainContext* context = g_main_context_default();
  while (g_main_context_pending(context)) {
    g_main_context_iteration(context, false);
  }
}

struct LambdaBrowser {
  std::shared_ptr<BrowserController> browser;

  Element body() const {
    auto theme = useEnvironment<ThemeKey>();
    BrowserSnapshot initial = browser ? browser->snapshot() : BrowserSnapshot{};
    auto url = useState(initial.uri.empty() ? std::string{kDefaultUri} : initial.uri);
    auto title = useState(initial.title);
    auto progress = useState(initial.progress);
    auto loading = useState(initial.loading);
    auto canGoBack = useState(initial.canGoBack);
    auto canGoForward = useState(initial.canGoForward);

    auto syncState = [browser = browser, url, title, progress, loading, canGoBack, canGoForward] {
      if (!browser) return;
      BrowserSnapshot const next = browser->snapshot();
      if (!next.uri.empty() && next.uri != url.peek()) url.set(next.uri);
      if (next.title != title.peek()) title.set(next.title);
      if (next.progress != progress.peek()) progress.set(next.progress);
      if (next.loading != loading.peek()) loading.set(next.loading);
      if (next.canGoBack != canGoBack.peek()) canGoBack.set(next.canGoBack);
      if (next.canGoForward != canGoForward.peek()) canGoForward.set(next.canGoForward);
    };

    auto syncTimerId = std::make_shared<std::uint64_t>(
        Application::instance().scheduleRepeatingTimer(std::chrono::milliseconds{100}));
    Application::instance().eventQueue().on<TimerEvent>(
        [syncTimerId, syncState](TimerEvent const& event) {
          if (syncTimerId && *syncTimerId != 0 && event.timerId == *syncTimerId) {
            syncState();
          }
        });
    onCleanup([syncTimerId] {
      if (syncTimerId && *syncTimerId != 0 && Application::hasInstance()) {
        Application::instance().cancelTimer(*syncTimerId);
        *syncTimerId = 0;
      }
    });

    TextInput::Style urlStyle;
    urlStyle.font = Font{.size = 13.f, .weight = 450.f};
    urlStyle.height = 34.f;

    IconButton::Style navStyle;
    navStyle.size = 34.f;
    navStyle.weight = 420.f;

    Element webArea = Render{
        .measureFn = [](LayoutConstraints const& constraints, LayoutHints const&) {
          float const width = std::isfinite(constraints.maxWidth) ? constraints.maxWidth : kInitialBrowserViewport.width;
          float const height = std::isfinite(constraints.maxHeight) ? constraints.maxHeight : kInitialBrowserViewport.height;
          return Size{std::max(1.f, width), std::max(1.f, height)};
        },
        .draw = [browser = browser](Canvas& canvas, Rect frame) {
          if (browser) {
            browser->draw(canvas, frame);
          } else {
            canvas.drawRect(frame, CornerRadius{}, FillStyle::solid(Color(1.f, 1.f, 1.f, 1.f)), StrokeStyle::none());
          }
        },
    }.flex(1.f, 1.f, 0.f);

    return VStack{
               .spacing = 0.f,
               .alignment = Alignment::Stretch,
               .children = children(
                   HStack{
                       .spacing = theme().space2,
                       .alignment = Alignment::Center,
                       .children = children(
                           IconButton{.icon = IconName::ArrowBack,
                                      .disabled = [canGoBack] { return !canGoBack(); },
                                      .style = navStyle,
                                      .onTap = [browser = browser] {
                                        if (browser) browser->goBack();
                                      }},
                           IconButton{.icon = IconName::ArrowForward,
                                      .disabled = [canGoForward] { return !canGoForward(); },
                                      .style = navStyle,
                                      .onTap = [browser = browser] {
                                        if (browser) browser->goForward();
                                      }},
                           IconButton{.icon = IconName::Refresh,
                                      .style = navStyle,
                                      .onTap = [browser = browser] {
                                        if (browser) browser->reload();
                                      }},
                           TextInput{
                               .value = url,
                               .placeholder = "https://example.com",
                               .style = urlStyle,
                               .onSubmit = [browser = browser, url](std::string const& value) {
                                 std::string normalized = normalizeUserUri(value);
                                 url.set(normalized);
                                 if (browser) browser->load(std::move(normalized));
                               },
                           }.flex(1.f, 1.f, 0.f),
                           Text{
                               .text = [loading, progress] {
                                 if (!loading()) return std::string{};
                                 int const percent = static_cast<int>(std::clamp(progress(), 0.0, 1.0) * 100.0);
                                 return std::to_string(percent) + "%";
                               },
                               .font = Font{.size = 12.f, .weight = 550.f},
                               .color = Color::secondary(),
                               .horizontalAlignment = HorizontalAlignment::Trailing,
                               .verticalAlignment = VerticalAlignment::Center,
                           }.width(44.f))}
                       .padding(theme().space3)
                       .fill(FillStyle::solid(Color::windowBackground()))
                       .stroke(StrokeStyle::solid(Color::separator(), 1.f)),
                   std::move(webArea),
                   HStack{
                       .spacing = theme().space2,
                       .alignment = Alignment::Center,
                       .children = children(
                           Text{
                               .text = [title, url] {
                                 return title().empty() ? url() : title();
                               },
                               .font = Font{.size = 12.f, .weight = 450.f},
                               .color = Color::secondary(),
                               .verticalAlignment = VerticalAlignment::Center,
                           }.flex(1.f, 1.f, 0.f))}
                       .padding(7.f, theme().space3, 7.f, theme().space3)
                       .fill(FillStyle::solid(Color::controlBackground())))};
  }
};

} // namespace

int main(int argc, char* argv[]) {
  if (!initializeWpeBackend()) return 1;

  Application app(argc, argv);
  app.setName("lambda-browser");
#if LAMBDA_VULKAN
  if (!configureBrowserVulkanDmabufImport()) return 1;
#else
  std::fputs("lambda-browser: dmabuf rendering requires a Vulkan build\n", stderr);
  return 1;
#endif

  std::string initialUri = argc > 1 ? std::string(argv[1]) : std::string{kDefaultUri};
  auto browser = std::make_shared<BrowserController>(std::move(initialUri), kInitialBrowserViewport);

  auto glibTimerId = app.scheduleRepeatingTimer(std::chrono::milliseconds{4});
  app.eventQueue().on<TimerEvent>([glibTimerId, browser, &app](TimerEvent const& event) {
    if (event.timerId == glibTimerId) {
      drainWpeEvents();
#if LAMBDA_VULKAN
      app.flushRedraw();
      if (browser) browser->flushPoolEntryReleases();
#endif
    }
  });

  auto& window = app.createWindow<Window>({
      .size = kInitialWindowSize,
      .title = "Lambda Browser",
      .resizable = true,
      .minSize = {640.f, 420.f},
  });
  window.registerAction("app.quit", {.label = "Quit", .shortcut = shortcuts::Quit, .isEnabled = [] { return true; }});
  window.setView<LambdaBrowser>({.browser = browser});

  int const result = app.exec();
  app.cancelTimer(glibTimerId);
  drainWpeEvents();
  return result;
}
