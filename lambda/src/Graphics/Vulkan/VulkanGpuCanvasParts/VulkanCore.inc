#include "Graphics/Vulkan/VulkanCanvas.hpp"

#include <Lambda/Debug/PerfCounters.hpp>
#include <Lambda/Graphics/Image.hpp>
#include <Lambda/Graphics/RenderTarget.hpp>
#include <Lambda/Graphics/TextSystem.hpp>

#include "Graphics/CanvasGeometry.hpp"
#include "Graphics/PathFlattener.hpp"
#include "Graphics/Vulkan/VulkanCanvasTypes.hpp"
#include "Graphics/Vulkan/VulkanCheck.hpp"
#include "Graphics/Vulkan/VulkanContextPrivate.hpp"
#include "Graphics/Vulkan/VulkanFrameRecorder.hpp"
#include "Detail/ResizeTrace.hpp"
#include "Graphics/Vulkan/generated/backdrop_blur_frag_spv.hpp"
#include "Graphics/Vulkan/generated/backdrop_frag_spv.hpp"
#include "Graphics/Vulkan/generated/callout_frag_spv.hpp"
#include "Graphics/Vulkan/generated/callout_vert_spv.hpp"
#include "Graphics/Vulkan/generated/image_frag_spv.hpp"
#include "Graphics/Vulkan/generated/image_unpremultiply_frag_spv.hpp"
#include "Graphics/Vulkan/generated/image_vert_spv.hpp"
#include "Graphics/Vulkan/generated/path_frag_spv.hpp"
#include "Graphics/Vulkan/generated/path_vert_spv.hpp"
#include "Graphics/Vulkan/generated/rect_frag_spv.hpp"
#include "Graphics/Vulkan/generated/rect_vert_spv.hpp"

#include <cassert>
#include <drm_fourcc.h>
#include <vulkan/vulkan.h>
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
#include "vma/vk_mem_alloc.h"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <list>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <unistd.h>

namespace lambdaui {

namespace {

class VulkanCanvas;

constexpr std::size_t kMaxFramesInFlight = 3;
constexpr int kBackdropBlurIterations = 2;
constexpr std::uint32_t kDefaultBackdropBlurBaseDownsample = 2;
constexpr std::uint32_t kMinBackdropBlurBaseDownsample = 1;
constexpr std::uint32_t kMaxBackdropBlurBaseDownsample = 8;
constexpr std::uint32_t kMaxBackdropBlurDownsample = 16;
constexpr float kBackdropBlurRadiusBoost = 1.35f;
constexpr std::uint32_t kDescriptorPoolStorageBufferSets = 4096;
constexpr std::uint32_t kDescriptorPoolImageSamplerSets = 8192;

bool renderTargetFrameCacheDisabled() {
  static bool const disabled = [] {
    char const* value = std::getenv("LAMBDA_RENDER_TARGET_DISABLE_FRAME_CACHE");
    return debug::envNonZero(value);
  }();
  return disabled;
}

bool vulkanPresentFencesEnabled() {
  static bool const enabled = [] {
    char const* value = std::getenv("LAMBDA_VULKAN_PRESENT_FENCES");
    return debug::envNonZero(value);
  }();
  return enabled;
}

bool vulkanForceFifoPresentMode() {
  static bool const enabled = [] {
    char const* value = std::getenv("LAMBDA_VULKAN_FORCE_FIFO_PRESENT_MODE");
    return debug::envNonZero(value);
  }();
  return enabled;
}

int vulkanPreparedGeometryOverride() {
  static int const overrideValue = [] {
    char const* value = std::getenv("LAMBDA_VULKAN_PREPARED_GEOMETRY");
    if (!value || !*value) {
      return 0;
    }
    if (*value == '0' || std::strcmp(value, "false") == 0 || std::strcmp(value, "off") == 0) {
      return -1;
    }
    if (*value == '1' || std::strcmp(value, "true") == 0 || std::strcmp(value, "on") == 0 ||
        std::strcmp(value, "force") == 0) {
      return 1;
    }
    return 0;
  }();
  return overrideValue;
}

struct VulkanImage;
void evictImageTexturesFor(VulkanImage const *image);
void updateImageTexturesFor(VulkanImage const* image);
void updateImageTextureRegionFor(VulkanImage const* image,
                                 std::uint32_t x,
                                 std::uint32_t y,
                                 std::uint32_t width,
                                 std::uint32_t height,
                                 void const* pixels,
                                 std::uint32_t sourceBytesPerRow = 0);
void markImageTextureContentsChangedFor(VulkanImage const* image);
struct SharedVulkanCore;
SharedVulkanCore *acquireSharedVulkanCore(VkSurfaceKHR surface);
void releaseSharedVulkanCore();

struct PendingOwnedVulkanImageDestroy {
  VkDevice device = VK_NULL_HANDLE;
  VmaAllocator allocator = VK_NULL_HANDLE;
  VkImage image = VK_NULL_HANDLE;
  VmaAllocation allocation = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkDeviceMemory importedMemory = VK_NULL_HANDLE;
  bool ownsImportedMemory = false;
  std::uint32_t framesRemaining = 0;
  std::uint32_t successfulSubmits = 0;
};

bool deferOwnedVulkanImageDestroy(PendingOwnedVulkanImageDestroy destroy);

void destroyOwnedVulkanImage(PendingOwnedVulkanImageDestroy& destroy) {
  if (destroy.view && destroy.device) {
    vkDestroyImageView(destroy.device, destroy.view, nullptr);
  }
  if (destroy.image && destroy.allocator) {
    vmaDestroyImage(destroy.allocator, destroy.image, destroy.allocation);
  } else if (destroy.image && destroy.device) {
    vkDestroyImage(destroy.device, destroy.image, nullptr);
  }
  if (destroy.ownsImportedMemory && destroy.importedMemory && destroy.device) {
    vkFreeMemory(destroy.device, destroy.importedMemory, nullptr);
  }
  destroy = {};
}

struct Rgba {
  std::uint8_t r = 0, g = 0, b = 0, a = 255;
};

VkFormat vkFormatForImagePixelFormat(Image::PixelFormat format) {
  switch (format) {
  case Image::PixelFormat::Bgra8888:
    return VK_FORMAT_B8G8R8A8_UNORM;
  case Image::PixelFormat::Rgba8888:
  default:
    return VK_FORMAT_R8G8B8A8_UNORM;
  }
}

struct VulkanImage final : Image {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> pixels;
  VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
  mutable VkImage image = VK_NULL_HANDLE;
  mutable VmaAllocation allocation = VK_NULL_HANDLE;
  VkDeviceMemory importedMemory = VK_NULL_HANDLE;
  mutable VkImageView view = VK_NULL_HANDLE;
  mutable VkDescriptorSet descriptor = VK_NULL_HANDLE;
  mutable bool uploaded = false;
  bool external = false;
  bool ownsGpuResource = false;
  bool ownsImportedMemory = false;
  bool ownsCoreReference = false;
  VkDevice owningDevice = VK_NULL_HANDLE;
  VmaAllocator owningAllocator = VK_NULL_HANDLE;
  std::uint64_t contentGeneration = 1;

  VulkanImage(int w, int h, std::vector<std::uint8_t> p, VkFormat pixelFormat)
      : width(w),
        height(h),
        pixels(std::move(p)),
        format(pixelFormat == VK_FORMAT_UNDEFINED ? VK_FORMAT_R8G8B8A8_UNORM : pixelFormat) {}
  VulkanImage(VkImage externalImage, VkImageView externalView, VkFormat externalFormat,
              std::uint32_t w, std::uint32_t h)
      : width(static_cast<int>(w)), height(static_cast<int>(h)),
        format(externalFormat == VK_FORMAT_UNDEFINED ? VK_FORMAT_R8G8B8A8_UNORM : externalFormat),
        image(externalImage), view(externalView), uploaded(true), external(true) {}
  VulkanImage(VkDevice device, VmaAllocator allocator, VkImage ownedImage,
              VmaAllocation ownedAllocation, VkImageView ownedView, VkFormat imageFormat,
              int w, int h)
      : width(w), height(h),
        format(imageFormat == VK_FORMAT_UNDEFINED ? VK_FORMAT_B8G8R8A8_UNORM : imageFormat),
        image(ownedImage), allocation(ownedAllocation), view(ownedView), uploaded(true),
        ownsGpuResource(true), owningDevice(device), owningAllocator(allocator) {
    acquireSharedVulkanCore(VK_NULL_HANDLE);
    ownsCoreReference = true;
  }
  VulkanImage(VkDevice device, VkImage importedImage, VkDeviceMemory importedImageMemory,
              VkImageView importedView, VkFormat imageFormat, int w, int h)
      : width(w), height(h),
        format(imageFormat == VK_FORMAT_UNDEFINED ? VK_FORMAT_B8G8R8A8_UNORM : imageFormat),
        image(importedImage), importedMemory(importedImageMemory), view(importedView), uploaded(true),
        ownsGpuResource(true), ownsImportedMemory(true), owningDevice(device) {
    acquireSharedVulkanCore(VK_NULL_HANDLE);
    ownsCoreReference = true;
  }
  ~VulkanImage() override {
    evictImageTexturesFor(this);
    if (ownsGpuResource) {
      PendingOwnedVulkanImageDestroy pending{
          .device = owningDevice,
          .allocator = owningAllocator,
          .image = image,
          .allocation = allocation,
          .view = view,
          .importedMemory = importedMemory,
          .ownsImportedMemory = ownsImportedMemory,
      };
      if (!deferOwnedVulkanImageDestroy(pending) && owningDevice) {
        // Fallback for final shutdown or headless lifetime paths with no canvas
        // available to retire the image after a frame fence.
        vkDeviceWaitIdle(owningDevice);
        destroyOwnedVulkanImage(pending);
      }
      image = VK_NULL_HANDLE;
      allocation = VK_NULL_HANDLE;
      view = VK_NULL_HANDLE;
      importedMemory = VK_NULL_HANDLE;
    }
    if (ownsCoreReference) {
      releaseSharedVulkanCore();
    }
  }
  Size size() const override { return {static_cast<float>(width), static_cast<float>(height)}; }
  void markContentChanged() {
    contentGeneration = contentGeneration == std::numeric_limits<std::uint64_t>::max() ? 1 : contentGeneration + 1;
    markImageTextureContentsChangedFor(this);
  }
  bool updateRgbaPixels(std::span<std::uint8_t const> rgbaPixels, void*) override {
    return updatePixels(rgbaPixels, PixelFormat::Rgba8888, nullptr);
  }
  bool updatePixels(std::span<std::uint8_t const> newPixels, PixelFormat pixelFormat, void*) override {
    if (external || ownsGpuResource || width <= 0 || height <= 0) {
      return false;
    }
    if (format != vkFormatForImagePixelFormat(pixelFormat)) {
      return false;
    }
    std::size_t const expectedSize = static_cast<std::size_t>(width) * height * 4u;
    if (newPixels.size() != expectedSize) {
      return false;
    }
    if (pixels.size() != expectedSize) {
      pixels.resize(expectedSize);
    }
    std::memcpy(pixels.data(), newPixels.data(), expectedSize);
    markContentChanged();
    updateImageTexturesFor(this);
    return true;
  }
  bool updatePixelsRegion(std::span<std::uint8_t const> newPixels,
                          PixelFormat pixelFormat,
                          std::uint32_t x,
                          std::uint32_t y,
                          std::uint32_t regionWidth,
                          std::uint32_t regionHeight,
                          void*,
                          std::uint32_t sourceBytesPerRow) override {
    if (external || ownsGpuResource || width <= 0 || height <= 0 || regionWidth == 0 || regionHeight == 0) {
      return false;
    }
    if (format != vkFormatForImagePixelFormat(pixelFormat)) {
      return false;
    }
    if (x > static_cast<std::uint32_t>(width) || y > static_cast<std::uint32_t>(height) ||
        regionWidth > static_cast<std::uint32_t>(width) - x ||
        regionHeight > static_cast<std::uint32_t>(height) - y) {
      return false;
    }
    std::size_t const srcStride =
        sourceBytesPerRow == 0 ? static_cast<std::size_t>(regionWidth) * 4u : sourceBytesPerRow;
    std::size_t const rowBytes = static_cast<std::size_t>(regionWidth) * 4u;
    if (srcStride < rowBytes ||
        newPixels.size() < srcStride * static_cast<std::size_t>(regionHeight - 1u) + rowBytes) {
      return false;
    }
    std::size_t const expectedSize = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    if (pixels.size() != expectedSize) {
      return false;
    }
    std::size_t const dstStride = static_cast<std::size_t>(width) * 4u;
    for (std::uint32_t row = 0; row < regionHeight; ++row) {
      auto* dst = pixels.data() + (static_cast<std::size_t>(y + row) * dstStride) +
                  static_cast<std::size_t>(x) * 4u;
      auto const* src = newPixels.data() + static_cast<std::size_t>(row) * srcStride;
      std::memcpy(dst, src, rowBytes);
    }
    markContentChanged();
    updateImageTextureRegionFor(this, x, y, regionWidth, regionHeight, newPixels.data(),
                                static_cast<std::uint32_t>(srcStride));
    return true;
  }
};

struct Buffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VmaAllocation allocation = VK_NULL_HANDLE;
  VkDeviceSize capacity = 0;
  void *mapped = nullptr;
};

struct ImageBatch {
  Texture *texture = nullptr;
  std::uint32_t first = 0;
  std::uint32_t count = 0;
};

struct VulkanDrawPushConstants {
  float viewport[2]{};
  float translation[2]{};
};

struct PathCacheKey {
  std::uint64_t pathHash = 0;
  std::uint64_t styleHash = 0;
  int viewportW = 0;
  int viewportH = 0;

  bool operator==(PathCacheKey const &) const = default;
};

struct PathCacheKeyHash {
  std::size_t operator()(PathCacheKey const &key) const noexcept {
    std::size_t h = static_cast<std::size_t>(key.pathHash);
    h ^= static_cast<std::size_t>(key.styleHash + 0x9e3779b97f4a7c15ULL + (h << 6u) + (h >> 2u));
    h ^= static_cast<std::size_t>(key.viewportW) + 0x9e3779b9u + (h << 6u) + (h >> 2u);
    h ^= static_cast<std::size_t>(key.viewportH) + 0x9e3779b9u + (h << 6u) + (h >> 2u);
    return h;
  }
};

struct CachedPath {
  std::vector<VulkanPathVertex> vertices;
  std::list<PathCacheKey>::iterator lruIt;
};

struct GlyphKey {
  std::uint32_t fontId = 0;
  std::uint32_t glyphId = 0;
  std::uint16_t size = 0;
  bool operator==(GlyphKey const &) const = default;
};

struct GlyphKeyHash {
  std::size_t operator()(GlyphKey const &k) const noexcept {
    std::size_t h = std::hash<std::uint32_t>{}(k.fontId);
    h ^= std::hash<std::uint32_t>{}(k.glyphId) + 0x9e3779b9 + (h << 6u) + (h >> 2u);
    h ^= std::hash<std::uint16_t>{}(k.size) + 0x9e3779b9 + (h << 6u) + (h >> 2u);
    return h;
  }
};

struct GlyphSlot {
  float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
  std::uint32_t w = 0, h = 0;
  int x = 0, y = 0;
  Point bearing{};
  std::uint64_t lastUsed = 0;
  std::vector<std::uint8_t> alpha;
};

void putColor(float out[4], Color c, float opacity = 1.f) {
  out[0] = c.r;
  out[1] = c.g;
  out[2] = c.b;
  out[3] = c.a * opacity;
}

std::uint64_t mixHashWord(std::uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30u)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27u)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31u);
}

void hashBytes(std::uint64_t &h, void const *data, std::size_t size) {
  auto const *bytes = static_cast<std::uint8_t const *>(data);
  while (size >= sizeof(std::uint64_t)) {
    std::uint64_t word = 0;
    std::memcpy(&word, bytes, sizeof(word));
    h ^= mixHashWord(word);
    h *= 1099511628211ULL;
    bytes += sizeof(word);
    size -= sizeof(word);
  }
  if (size > 0) {
    std::uint64_t tail = 0;
    std::memcpy(&tail, bytes, size);
    h ^= mixHashWord(tail ^ size);
    h *= 1099511628211ULL;
  }
}

template <typename T>
void hashValue(std::uint64_t &h, T const &value) {
  hashBytes(h, &value, sizeof(value));
}

void hashColor(std::uint64_t &h, Color c) {
  hashValue(h, c.r);
  hashValue(h, c.g);
  hashValue(h, c.b);
  hashValue(h, c.a);
}

void hashPoint(std::uint64_t &h, Point p) {
  hashValue(h, p.x);
  hashValue(h, p.y);
}

void hashStops(std::uint64_t &h, std::array<GradientStop, kMaxGradientStops> const &stops, std::uint8_t count) {
  hashValue(h, count);
  for (std::uint8_t i = 0; i < count; ++i) {
    hashValue(h, stops[i].position);
    hashColor(h, stops[i].color);
  }
}

std::uint64_t hashFill(FillStyle const &fill) {
  std::uint64_t h = 14695981039346656037ULL;
  hashValue(h, fill.fillRule);
  hashValue(h, fill.data.index());
  Color solid{};
  if (fill.solidColor(&solid)) {
    hashColor(h, solid);
  }
  LinearGradient linear{};
  if (fill.linearGradient(&linear)) {
    hashPoint(h, linear.start);
    hashPoint(h, linear.end);
    hashStops(h, linear.stops, linear.stopCount);
  }
  RadialGradient radial{};
  if (fill.radialGradient(&radial)) {
    hashPoint(h, radial.center);
    hashValue(h, radial.radius);
    hashStops(h, radial.stops, radial.stopCount);
  }
  ConicalGradient conical{};
  if (fill.conicalGradient(&conical)) {
    hashPoint(h, conical.center);
    hashValue(h, conical.startAngleRadians);
    hashStops(h, conical.stops, conical.stopCount);
  }
  return h;
}

std::uint64_t hashStroke(StrokeStyle const &stroke) {
  std::uint64_t h = 14695981039346656037ULL;
  hashValue(h, stroke.type);
  hashColor(h, stroke.color);
  hashValue(h, stroke.width);
  hashValue(h, stroke.cap);
  hashValue(h, stroke.join);
  hashValue(h, stroke.miterLimit);
  return h;
}

std::uint64_t hashTransform(Mat3 const &transform, float opacity) {
  std::uint64_t h = 14695981039346656037ULL;
  for (float value : transform.m) {
    hashValue(h, value);
  }
  hashValue(h, opacity);
  return h;
}

Rect unionRects(Rect a, Rect b) {
  float const x0 = std::min(a.x, b.x);
  float const y0 = std::min(a.y, b.y);
  float const x1 = std::max(a.x + a.width, b.x + b.width);
  float const y1 = std::max(a.y + a.height, b.y + b.height);
  return Rect::sharp(x0, y0, std::max(0.f, x1 - x0), std::max(0.f, y1 - y0));
}

std::uint32_t findMemoryType(VkPhysicalDevice physical, std::uint32_t typeBits,
                             VkMemoryPropertyFlags requiredProperties) {
  VkPhysicalDeviceMemoryProperties props{};
  vkGetPhysicalDeviceMemoryProperties(physical, &props);
  for (std::uint32_t i = 0; i < props.memoryTypeCount; ++i) {
    if ((typeBits & (1u << i)) &&
        (props.memoryTypes[i].propertyFlags & requiredProperties) == requiredProperties) {
      return i;
    }
  }
  throw std::runtime_error("No suitable Vulkan memory type");
}

VkFormat vkFormatForDrmFormat(std::uint32_t drmFormat) {
  switch (drmFormat) {
    case DRM_FORMAT_ARGB8888:
    case DRM_FORMAT_XRGB8888:
      return VK_FORMAT_B8G8R8A8_UNORM;
    case DRM_FORMAT_ABGR8888:
    case DRM_FORMAT_XBGR8888:
      return VK_FORMAT_R8G8B8A8_UNORM;
    default:
      return VK_FORMAT_UNDEFINED;
  }
}

struct SharedVulkanCore {
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VmaAllocator allocator = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  std::uint32_t queueFamily = 0;
    struct Resources {
    bool initialized = false;
    VkFormat renderFormat = VK_FORMAT_UNDEFINED;
    VkFormat backdropRenderFormat = VK_FORMAT_UNDEFINED;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout rectDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout quadDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout textureDescriptorLayout = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkPipelineLayout rectPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout calloutPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout imagePipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout backdropPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout pathPipelineLayout = VK_NULL_HANDLE;
    VkPipeline rectPipeline = VK_NULL_HANDLE;
    VkPipeline calloutPipeline = VK_NULL_HANDLE;
    VkPipeline imagePipeline = VK_NULL_HANDLE;
    VkPipeline imageUnpremultiplyPipeline = VK_NULL_HANDLE;
    VkPipeline backdropPipeline = VK_NULL_HANDLE;
    VkPipeline backdropBlurPipeline = VK_NULL_HANDLE;
    VkPipeline pathPipeline = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    std::filesystem::path pipelineCacheFile;
    Texture atlas;
    std::vector<Rgba> atlasPixels;
    int atlasX = 1;
    int atlasY = 1;
    int atlasRowH = 0;
    bool atlasDirty = false;
    std::uint64_t atlasGeneration = 1;
    std::uint64_t atlasUseCounter = 0;
    std::unordered_map<GlyphKey, GlyphSlot, GlyphKeyHash> glyphs;
  } resources;
  std::uint32_t refs = 0;
  bool googleDisplayTiming = false;
  bool swapchainMaintenance1 = false;
  std::string swapchainMaintenance1Extension;
  VkDriverId driverId = VK_DRIVER_ID_MAX_ENUM;
  std::string driverName;
  std::string driverInfo;
};

std::mutex gVulkanCoreMutex;
SharedVulkanCore gVulkanCore;
std::vector<std::string> gRequiredInstanceExtensions;
std::vector<std::string> gRequiredDeviceExtensions;
std::filesystem::path gPipelineCacheDir;
void destroySharedVulkanResources(SharedVulkanCore &core);

std::mutex gCanvasRegistryMutex;
std::vector<VulkanCanvas*> gCanvases;

bool containsExtension(std::vector<std::string> const &extensions, char const *name) {
  if (!name || !*name) {
    return true;
  }
  return std::any_of(extensions.begin(), extensions.end(), [name](std::string const &extension) {
    return extension == name;
  });
}

void appendUniqueExtension(std::vector<std::string> &extensions, char const *name) {
  if (!name || !*name || containsExtension(extensions, name)) {
    return;
  }
  extensions.emplace_back(name);
}

bool allExtensionsConfigured(std::vector<std::string> const &extensions,
                             std::span<char const* const> required) {
  for (char const *extension : required) {
    if (!containsExtension(extensions, extension)) {
      return false;
    }
  }
  return true;
}

std::vector<char const *> extensionNamePointers(std::vector<std::string> const &extensions) {
  std::vector<char const *> names;
  names.reserve(extensions.size());
  for (std::string const &extension : extensions) {
    names.push_back(extension.c_str());
  }
  return names;
}

std::string vulkanVersionString(std::uint32_t version) {
  return std::to_string(VK_VERSION_MAJOR(version)) + "." +
         std::to_string(VK_VERSION_MINOR(version)) + "." +
         std::to_string(VK_VERSION_PATCH(version));
}

bool extensionAvailable(std::vector<VkExtensionProperties> const &available, char const *required) {
  return std::any_of(available.begin(), available.end(), [&](VkExtensionProperties const &extension) {
    return std::strcmp(extension.extensionName, required) == 0;
  });
}

VkInstance ensureSharedVulkanInstanceImpl() {
  std::lock_guard lock(gVulkanCoreMutex);
  if (gVulkanCore.instance)
    return gVulkanCore.instance;
  auto app = vkStructure<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
  app.pApplicationName = "Lambda";
  app.apiVersion = VK_API_VERSION_1_3;
  if (containsExtension(gRequiredInstanceExtensions, VK_KHR_SURFACE_EXTENSION_NAME)) {
    std::uint32_t availableCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, nullptr);
    std::vector<VkExtensionProperties> available(availableCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, available.data());
    if (extensionAvailable(available, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME) &&
        extensionAvailable(available, VK_KHR_SURFACE_MAINTENANCE_1_EXTENSION_NAME)) {
      appendUniqueExtension(gRequiredInstanceExtensions, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
      appendUniqueExtension(gRequiredInstanceExtensions, VK_KHR_SURFACE_MAINTENANCE_1_EXTENSION_NAME);
    }
  }
  std::vector<char const *> instanceExtensions = extensionNamePointers(gRequiredInstanceExtensions);
  if (!instanceExtensions.empty()) {
    std::uint32_t availableCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, nullptr);
    std::vector<VkExtensionProperties> available(availableCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &availableCount, available.data());
    for (char const *extension : instanceExtensions) {
      if (!extensionAvailable(available, extension)) {
        throw std::runtime_error(std::string("Missing required Vulkan instance extension: ") +
                                 extension);
      }
    }
  }
  auto info = vkStructure<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
  info.pApplicationInfo = &app;
  info.enabledExtensionCount = static_cast<std::uint32_t>(instanceExtensions.size());
  info.ppEnabledExtensionNames = instanceExtensions.empty() ? nullptr : instanceExtensions.data();
  vkCheck(vkCreateInstance(&info, nullptr, &gVulkanCore.instance), "vkCreateInstance");
  return gVulkanCore.instance;
}

std::string hexBytes(std::uint8_t const *bytes, std::size_t size) {
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (std::size_t i = 0; i < size; ++i) {
    out << std::setw(2) << static_cast<unsigned int>(bytes[i]);
  }
  return out.str();
}

std::filesystem::path pipelineCachePath(VkPhysicalDevice physical) {
  VkPhysicalDeviceProperties props{};
  vkGetPhysicalDeviceProperties(physical, &props);
  std::string identity;
  auto getProps2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
      vkGetInstanceProcAddr(gVulkanCore.instance, "vkGetPhysicalDeviceProperties2"));
  if (!getProps2) {
    getProps2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
        vkGetInstanceProcAddr(gVulkanCore.instance, "vkGetPhysicalDeviceProperties2KHR"));
  }
  if (getProps2) {
    auto idProps = vkStructure<VkPhysicalDeviceIDProperties>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES);
    auto props2 = vkStructure<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
    props2.pNext = &idProps;
    getProps2(physical, &props2);
    identity = hexBytes(idProps.deviceUUID, VK_UUID_SIZE);
  }
  if (identity.empty() || identity == std::string(VK_UUID_SIZE * 2, '0')) {
    identity = std::to_string(props.vendorID) + "-" + std::to_string(props.deviceID);
  }
  std::filesystem::path cacheDir = gPipelineCacheDir.empty()
      ? std::filesystem::temp_directory_path()
      : gPipelineCacheDir;
  return cacheDir / ("lambda-vulkan-" + identity + ".cache");
}

void createPipelineCache(SharedVulkanCore &core) {
  auto &res = core.resources;
  if (res.pipelineCache)
    return;
  res.pipelineCacheFile = pipelineCachePath(core.physical);
  std::vector<std::uint8_t> initialData;
  if (std::ifstream in(res.pipelineCacheFile, std::ios::binary); in) {
    initialData.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  auto info = vkStructure<VkPipelineCacheCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO);
  info.initialDataSize = initialData.size();
  info.pInitialData = initialData.empty() ? nullptr : initialData.data();
  VkResult const result = vkCreatePipelineCache(core.device, &info, nullptr, &res.pipelineCache);
  if (result != VK_SUCCESS && !initialData.empty()) {
    info.initialDataSize = 0;
    info.pInitialData = nullptr;
    vkCheck(vkCreatePipelineCache(core.device, &info, nullptr, &res.pipelineCache),
            "vkCreatePipelineCache");
    return;
  }
  vkCheck(result, "vkCreatePipelineCache");
}

void saveAndDestroyPipelineCache(VkDevice device, SharedVulkanCore::Resources &res) {
  if (!res.pipelineCache)
    return;
  std::size_t size = 0;
  if (vkGetPipelineCacheData(device, res.pipelineCache, &size, nullptr) == VK_SUCCESS && size > 0) {
    std::vector<std::uint8_t> data(size);
    if (vkGetPipelineCacheData(device, res.pipelineCache, &size, data.data()) == VK_SUCCESS) {
      try {
        std::filesystem::create_directories(res.pipelineCacheFile.parent_path());
        std::ofstream out(res.pipelineCacheFile, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<char const *>(data.data()), static_cast<std::streamsize>(size));
      } catch (...) {
      }
    }
  }
  vkDestroyPipelineCache(device, res.pipelineCache, nullptr);
  res.pipelineCache = VK_NULL_HANDLE;
}

SharedVulkanCore *acquireSharedVulkanCore(VkSurfaceKHR surface) {
  std::lock_guard lock(gVulkanCoreMutex);
  if (!gVulkanCore.instance) {
    throw std::runtime_error("Vulkan instance was not initialized");
  }
  bool const needsPresent = surface != VK_NULL_HANDLE;
  if (!gVulkanCore.device) {
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(gVulkanCore.instance, &count, nullptr);
    if (!count)
      throw std::runtime_error("No Vulkan physical devices");
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(gVulkanCore.instance, &count, devices.data());
    std::vector<std::string> rejectedDevices;
    for (VkPhysicalDevice d : devices) {
      VkPhysicalDeviceProperties props{};
      vkGetPhysicalDeviceProperties(d, &props);
      std::string const deviceName = props.deviceName[0] ? props.deviceName : "unnamed device";
      if (props.apiVersion < VK_API_VERSION_1_3) {
        rejectedDevices.push_back(deviceName + ": Vulkan 1.3 required, device exposes " +
                                  vulkanVersionString(props.apiVersion));
        continue;
      }
      auto vk13 = vkStructure<VkPhysicalDeviceVulkan13Features>(
          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);
      auto features = vkStructure<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
      features.pNext = &vk13;
      vkGetPhysicalDeviceFeatures2(d, &features);
      if (!vk13.dynamicRendering || !vk13.synchronization2) {
        std::string missing;
        if (!vk13.dynamicRendering) {
          missing += missing.empty() ? "dynamicRendering" : ", dynamicRendering";
        }
        if (!vk13.synchronization2) {
          missing += missing.empty() ? "synchronization2" : ", synchronization2";
        }
        rejectedDevices.push_back(deviceName + ": missing Vulkan 1.3 feature(s): " + missing);
        continue;
      }
      std::vector<std::string> deviceExtensions = gRequiredDeviceExtensions;
      if (needsPresent) {
        appendUniqueExtension(deviceExtensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
      }
      std::uint32_t extensionCount = 0;
      vkEnumerateDeviceExtensionProperties(d, nullptr, &extensionCount, nullptr);
      std::vector<VkExtensionProperties> availableExtensions(extensionCount);
      vkEnumerateDeviceExtensionProperties(d, nullptr, &extensionCount, availableExtensions.data());
      if (needsPresent && extensionAvailable(availableExtensions, VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME)) {
        appendUniqueExtension(deviceExtensions, VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);
      }
      char const *swapchainMaintenanceExtension = nullptr;
      bool swapchainMaintenanceFeature = false;
      if (needsPresent) {
        if (extensionAvailable(availableExtensions, VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME)) {
          swapchainMaintenanceExtension = VK_KHR_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME;
        } else if (extensionAvailable(availableExtensions, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME)) {
          swapchainMaintenanceExtension = VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME;
        }
        if (swapchainMaintenanceExtension) {
          auto swapchainMaintenance = vkStructure<VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR>(
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR);
          auto maintenanceVk13 = vkStructure<VkPhysicalDeviceVulkan13Features>(
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);
          maintenanceVk13.pNext = &swapchainMaintenance;
          auto maintenanceFeatures =
              vkStructure<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
          maintenanceFeatures.pNext = &maintenanceVk13;
          vkGetPhysicalDeviceFeatures2(d, &maintenanceFeatures);
          swapchainMaintenanceFeature = swapchainMaintenance.swapchainMaintenance1 == VK_TRUE;
          if (swapchainMaintenanceFeature) {
            appendUniqueExtension(deviceExtensions, swapchainMaintenanceExtension);
          }
        }
      }
      if (!deviceExtensions.empty()) {
        std::vector<std::string> missingExtensions;
        for (std::string const &extension : deviceExtensions) {
          if (!extensionAvailable(availableExtensions, extension.c_str())) {
            missingExtensions.push_back(extension);
          }
        }
        if (!missingExtensions.empty()) {
          std::string missing = missingExtensions.front();
          for (std::size_t i = 1; i < missingExtensions.size(); ++i) {
            missing += ", " + missingExtensions[i];
          }
          rejectedDevices.push_back(deviceName + ": missing Vulkan device extension(s): " + missing);
          continue;
        }
      }
      std::uint32_t familiesCount = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(d, &familiesCount, nullptr);
      std::vector<VkQueueFamilyProperties> families(familiesCount);
      vkGetPhysicalDeviceQueueFamilyProperties(d, &familiesCount, families.data());
      bool hasGraphicsQueue = false;
      bool hasGraphicsPresentQueue = false;
      for (std::uint32_t i = 0; i < familiesCount; ++i) {
        VkBool32 present = VK_FALSE;
        if (needsPresent) {
          vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface, &present);
        }
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
          hasGraphicsQueue = true;
          hasGraphicsPresentQueue = hasGraphicsPresentQueue || !needsPresent || present == VK_TRUE;
        }
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            (!needsPresent || present == VK_TRUE)) {
          gVulkanCore.physical = d;
          gVulkanCore.queueFamily = i;
          float priority = 1.f;
          auto q = vkStructure<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
          q.queueFamilyIndex = i;
          q.queueCount = 1;
          q.pQueuePriorities = &priority;
          auto enabled13 = vkStructure<VkPhysicalDeviceVulkan13Features>(
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);
          enabled13.synchronization2 = VK_TRUE;
          enabled13.dynamicRendering = VK_TRUE;
          auto enabledSwapchainMaintenance = vkStructure<VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR>(
              VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR);
          if (swapchainMaintenanceFeature) {
            enabledSwapchainMaintenance.swapchainMaintenance1 = VK_TRUE;
            enabled13.pNext = &enabledSwapchainMaintenance;
          }
          std::vector<char const *> deviceExtensionNames = extensionNamePointers(deviceExtensions);
          auto info = vkStructure<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
          info.pNext = &enabled13;
          info.queueCreateInfoCount = 1;
          info.pQueueCreateInfos = &q;
          info.enabledExtensionCount = static_cast<std::uint32_t>(deviceExtensionNames.size());
          info.ppEnabledExtensionNames = deviceExtensionNames.empty() ? nullptr : deviceExtensionNames.data();
          auto driverProps =
              vkStructure<VkPhysicalDeviceDriverProperties>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES);
          auto props2 = vkStructure<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
          props2.pNext = &driverProps;
          vkGetPhysicalDeviceProperties2(gVulkanCore.physical, &props2);
          gVulkanCore.driverId = driverProps.driverID;
          gVulkanCore.driverName = driverProps.driverName;
          gVulkanCore.driverInfo = driverProps.driverInfo;
          vkCheck(vkCreateDevice(gVulkanCore.physical, &info, nullptr, &gVulkanCore.device), "vkCreateDevice");
          gVulkanCore.googleDisplayTiming =
              containsExtension(deviceExtensions, VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);
          gVulkanCore.swapchainMaintenance1 =
              swapchainMaintenanceFeature && swapchainMaintenanceExtension != nullptr &&
              containsExtension(deviceExtensions, swapchainMaintenanceExtension);
          gVulkanCore.swapchainMaintenance1Extension =
              gVulkanCore.swapchainMaintenance1 ? swapchainMaintenanceExtension : "";
          if (needsPresent) {
            std::fprintf(stderr, "Lambda Vulkan: swapchain maintenance1 %s%s%s\n",
                         gVulkanCore.swapchainMaintenance1 ? "enabled" : "unavailable",
                         gVulkanCore.swapchainMaintenance1 ? " via " : "",
                         gVulkanCore.swapchainMaintenance1
                             ? gVulkanCore.swapchainMaintenance1Extension.c_str()
                             : "");
          }
          VmaAllocatorCreateInfo allocatorInfo{};
          allocatorInfo.physicalDevice = gVulkanCore.physical;
          allocatorInfo.device = gVulkanCore.device;
          allocatorInfo.instance = gVulkanCore.instance;
          allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
          vkCheck(vmaCreateAllocator(&allocatorInfo, &gVulkanCore.allocator), "vmaCreateAllocator");
          vkGetDeviceQueue(gVulkanCore.device, gVulkanCore.queueFamily, 0, &gVulkanCore.queue);
          ++gVulkanCore.refs;
          return &gVulkanCore;
        }
      }
      if (!hasGraphicsQueue) {
        rejectedDevices.push_back(deviceName + ": no graphics queue family");
      } else if (needsPresent && !hasGraphicsPresentQueue) {
        rejectedDevices.push_back(deviceName + ": no graphics queue family can present to this surface");
      }
    }
    std::string message = needsPresent ? "No suitable Vulkan graphics/present device"
                                       : "No suitable Vulkan graphics device";
    if (!rejectedDevices.empty()) {
      message += ": ";
      for (std::size_t i = 0; i < rejectedDevices.size(); ++i) {
        if (i > 0) {
          message += "; ";
        }
        message += rejectedDevices[i];
      }
    }
    throw std::runtime_error(message);
  }
  if (needsPresent) {
    VkBool32 present = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(gVulkanCore.physical, gVulkanCore.queueFamily, surface, &present);
    if (!present) {
      throw std::runtime_error("Shared Vulkan queue cannot present to this surface");
    }
  }
  ++gVulkanCore.refs;
  return &gVulkanCore;
}

void releaseSharedVulkanCore() {
  std::lock_guard lock(gVulkanCoreMutex);
  if (gVulkanCore.refs == 0)
    return;
  --gVulkanCore.refs;
  if (gVulkanCore.refs != 0)
    return;
  if (gVulkanCore.device) {
    // Intentional shutdown-only wait before destroying shared Vulkan objects.
    vkDeviceWaitIdle(gVulkanCore.device);
    destroySharedVulkanResources(gVulkanCore);
    if (gVulkanCore.allocator) {
      vmaDestroyAllocator(gVulkanCore.allocator);
      gVulkanCore.allocator = VK_NULL_HANDLE;
    }
    vkDestroyDevice(gVulkanCore.device, nullptr);
  }
  if (gVulkanCore.instance) {
    vkDestroyInstance(gVulkanCore.instance, nullptr);
  }
  gVulkanCore = {};
}

void destroySharedTexture(VkDevice device, VmaAllocator allocator, VkDescriptorPool descriptorPool, Texture &tex) {
  if (tex.descriptor && descriptorPool)
    vkFreeDescriptorSets(device, descriptorPool, 1, &tex.descriptor);
  if (tex.view && tex.ownsView)
    vkDestroyImageView(device, tex.view, nullptr);
  if (tex.image && tex.ownsImage)
    vmaDestroyImage(allocator, tex.image, tex.allocation);
  tex = {};
}

void destroySharedVulkanResources(SharedVulkanCore &core) {
  auto &res = core.resources;
  VkDevice const device = core.device;
  if (!device)
    return;
  destroySharedTexture(device, core.allocator, res.descriptorPool, res.atlas);
  if (res.pathPipeline)
    vkDestroyPipeline(device, res.pathPipeline, nullptr);
  if (res.rectPipeline)
    vkDestroyPipeline(device, res.rectPipeline, nullptr);
  if (res.calloutPipeline)
    vkDestroyPipeline(device, res.calloutPipeline, nullptr);
  if (res.imagePipeline)
    vkDestroyPipeline(device, res.imagePipeline, nullptr);
  if (res.imageUnpremultiplyPipeline)
    vkDestroyPipeline(device, res.imageUnpremultiplyPipeline, nullptr);
  if (res.backdropPipeline)
    vkDestroyPipeline(device, res.backdropPipeline, nullptr);
  if (res.backdropBlurPipeline)
    vkDestroyPipeline(device, res.backdropBlurPipeline, nullptr);
  if (res.pathPipelineLayout)
    vkDestroyPipelineLayout(device, res.pathPipelineLayout, nullptr);
  if (res.rectPipelineLayout)
    vkDestroyPipelineLayout(device, res.rectPipelineLayout, nullptr);
  if (res.calloutPipelineLayout)
    vkDestroyPipelineLayout(device, res.calloutPipelineLayout, nullptr);
  if (res.imagePipelineLayout)
    vkDestroyPipelineLayout(device, res.imagePipelineLayout, nullptr);
  if (res.backdropPipelineLayout)
    vkDestroyPipelineLayout(device, res.backdropPipelineLayout, nullptr);
  if (res.sampler)
    vkDestroySampler(device, res.sampler, nullptr);
  if (res.rectDescriptorLayout)
    vkDestroyDescriptorSetLayout(device, res.rectDescriptorLayout, nullptr);
  if (res.quadDescriptorLayout)
    vkDestroyDescriptorSetLayout(device, res.quadDescriptorLayout, nullptr);
  if (res.textureDescriptorLayout)
    vkDestroyDescriptorSetLayout(device, res.textureDescriptorLayout, nullptr);
  if (res.descriptorPool)
    vkDestroyDescriptorPool(device, res.descriptorPool, nullptr);
  saveAndDestroyPipelineCache(device, res);
  res = {};
}

CornerRadius clampRadii(CornerRadius r, float w, float h) {
  clampRoundRectCornerRadii(w, h, r);
  return r;
}

VkShaderModule shaderModule(VkDevice device, unsigned char const *bytes, unsigned int len) {
  auto info = vkStructure<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
  info.codeSize = len;
  info.pCode = reinterpret_cast<std::uint32_t const *>(bytes);
  VkShaderModule module = VK_NULL_HANDLE;
  vkCheck(vkCreateShaderModule(device, &info, nullptr, &module), "vkCreateShaderModule");
  return module;
}

bool representativeFillColor(FillStyle const &fill, Color *out) {
  if (fill.solidColor(out))
    return true;
  LinearGradient linear{};
  if (fill.linearGradient(&linear) && linear.stopCount > 0) {
    *out = linear.stops[0].color;
    return true;
  }
  RadialGradient radial{};
  if (fill.radialGradient(&radial) && radial.stopCount > 0) {
    *out = radial.stops[0].color;
    return true;
  }
  ConicalGradient conical{};
  if (fill.conicalGradient(&conical) && conical.stopCount > 0) {
    *out = conical.stops[0].color;
    return true;
  }
  return false;
}

Rect boundsOfSubpaths(std::vector<std::vector<Point>> const &subpaths) {
  float minX = std::numeric_limits<float>::infinity();
  float minY = std::numeric_limits<float>::infinity();
  float maxX = -std::numeric_limits<float>::infinity();
  float maxY = -std::numeric_limits<float>::infinity();
  for (auto const &subpath : subpaths) {
    for (Point const &point : subpath) {
      minX = std::min(minX, point.x);
      minY = std::min(minY, point.y);
      maxX = std::max(maxX, point.x);
      maxY = std::max(maxY, point.y);
    }
  }
  if (!std::isfinite(minX) || maxX <= minX || maxY <= minY) {
    return Rect::sharp(0.f, 0.f, 1.f, 1.f);
  }
  return Rect::sharp(minX, minY, maxX - minX, maxY - minY);
}

void putPathGradient(VulkanPathVertex &out, FillStyle const &fill, Point local) {
  out.local[0] = local.x;
  out.local[1] = local.y;
  auto putStops = [&](auto const &gradient, int type) {
    out.params[0] = static_cast<float>(type);
    out.params[1] = static_cast<float>(gradient.stopCount);
    std::array<float *, 4> colors{out.fill0, out.fill1, out.fill2, out.fill3};
    for (std::uint8_t i = 0; i < gradient.stopCount && i < colors.size(); ++i) {
      putColor(colors[i], gradient.stops[i].color, 1.f);
      out.stops[i] = gradient.stops[i].position;
    }
  };
  LinearGradient linear{};
  if (fill.linearGradient(&linear) && linear.stopCount > 0) {
    putStops(linear, 1);
    out.gradient[0] = linear.start.x;
    out.gradient[1] = linear.start.y;
    out.gradient[2] = linear.end.x;
    out.gradient[3] = linear.end.y;
    return;
  }
  RadialGradient radial{};
  if (fill.radialGradient(&radial) && radial.stopCount > 0) {
    putStops(radial, 2);
    out.gradient[0] = radial.center.x;
    out.gradient[1] = radial.center.y;
    out.gradient[2] = radial.radius;
    return;
  }
  ConicalGradient conical{};
  if (fill.conicalGradient(&conical) && conical.stopCount > 0) {
    putStops(conical, 3);
    out.gradient[0] = conical.center.x;
    out.gradient[1] = conical.center.y;
    out.gradient[2] = conical.startAngleRadians;
  }
}

VulkanPathVertex makeVulkanPathVertex(PathVertex const &src, FillStyle const *fill, Rect bounds, float opacity) {
  VulkanPathVertex out{};
  out.x = src.x;
  out.y = src.y;
  std::copy(std::begin(src.color), std::end(src.color), std::begin(out.color));
  std::copy(std::begin(src.viewport), std::end(src.viewport), std::begin(out.viewport));
  float const invW = 1.f / std::max(bounds.width, 1e-4f);
  float const invH = 1.f / std::max(bounds.height, 1e-4f);
  Point const local{(src.x - bounds.x) * invW, (src.y - bounds.y) * invH};
  out.local[0] = local.x;
  out.local[1] = local.y;
  out.params[3] = opacity;
  if (fill) {
    putPathGradient(out, *fill, local);
  }
  return out;
}

} // namespace
