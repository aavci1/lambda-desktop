#include "Graphics/Vulkan/VulkanCanvas.hpp"

#include <Flux/Debug/PerfCounters.hpp>
#include <Flux/Graphics/Image.hpp>
#include <Flux/Graphics/RenderTarget.hpp>
#include <Flux/Graphics/TextSystem.hpp>

#include "Graphics/PathFlattener.hpp"
#include "Graphics/Vulkan/VulkanCanvasTypes.hpp"
#include "Graphics/Vulkan/VulkanContextPrivate.hpp"
#include "Graphics/Vulkan/VulkanFrameRecorder.hpp"
#include "Graphics/Vulkan/generated/backdrop_blur_frag_spv.hpp"
#include "Graphics/Vulkan/generated/backdrop_frag_spv.hpp"
#include "Graphics/Vulkan/generated/image_frag_spv.hpp"
#include "Graphics/Vulkan/generated/image_vert_spv.hpp"
#include "Graphics/Vulkan/generated/path_frag_spv.hpp"
#include "Graphics/Vulkan/generated/path_vert_spv.hpp"
#include "Graphics/Vulkan/generated/rect_frag_spv.hpp"
#include "Graphics/Vulkan/generated/rect_vert_spv.hpp"

#include <drm_fourcc.h>
#include <vulkan/vulkan.h>
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
#define VMA_IMPLEMENTATION
#include "vma/vk_mem_alloc.h"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <array>
#include <cstdarg>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
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

namespace flux {

class VulkanCanvas;

namespace {

constexpr std::size_t kMaxFramesInFlight = 2;
constexpr int kBackdropBlurIterations = 3;

struct VulkanImage;
void evictImageTexturesFor(VulkanImage const *image);
struct SharedVulkanCore;
SharedVulkanCore *acquireSharedVulkanCore(VkSurfaceKHR surface);
void releaseSharedVulkanCore();

struct Rgba {
  std::uint8_t r = 0, g = 0, b = 0, a = 255;
};

struct VulkanImage final : Image {
  int width = 0;
  int height = 0;
  std::vector<Rgba> pixels;
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

  VulkanImage(int w, int h, std::vector<Rgba> p) : width(w), height(h), pixels(std::move(p)) {}
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
      if (owningDevice) {
        vkDeviceWaitIdle(owningDevice);
      }
      if (view && owningDevice) {
        vkDestroyImageView(owningDevice, view, nullptr);
      }
      if (image && owningAllocator) {
        vmaDestroyImage(owningAllocator, image, allocation);
      } else if (image && owningDevice) {
        vkDestroyImage(owningDevice, image, nullptr);
      }
      if (ownsImportedMemory && importedMemory && owningDevice) {
        vkFreeMemory(owningDevice, importedMemory, nullptr);
      }
    }
    if (ownsCoreReference) {
      releaseSharedVulkanCore();
    }
  }
  Size size() const override { return {static_cast<float>(width), static_cast<float>(height)}; }
};

struct Buffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VmaAllocation allocation = VK_NULL_HANDLE;
  VkDeviceSize capacity = 0;
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
  std::uint16_t glyphId = 0;
  std::uint16_t size = 0;
  bool operator==(GlyphKey const &) const = default;
};

struct GlyphKeyHash {
  std::size_t operator()(GlyphKey const &k) const noexcept {
    return (static_cast<std::size_t>(k.fontId) << 32u) ^
           (static_cast<std::size_t>(k.glyphId) << 16u) ^ k.size;
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

void hashBytes(std::uint64_t &h, void const *data, std::size_t size) {
  auto const *bytes = static_cast<std::uint8_t const *>(data);
  for (std::size_t i = 0; i < size; ++i) {
    h ^= bytes[i];
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

bool waylandResizeTrace() {
  char const *value = std::getenv("FLUX_WAYLAND_RESIZE_TRACE");
  return value && *value && std::strcmp(value, "0") != 0;
}

void waylandResizeTraceLog(char const *format, ...) {
  va_list args;
  va_start(args, format);
  va_list stderrArgs;
  va_copy(stderrArgs, args);
  std::vfprintf(stderr, format, stderrArgs);
  va_end(stderrArgs);

  char const *path = std::getenv("FLUX_WAYLAND_RESIZE_TRACE_LOG");
  if (!path || !*path) {
    path = "/tmp/flux-wayland-resize.log";
  }
  if (FILE *file = std::fopen(path, "a")) {
    std::vfprintf(file, format, args);
    std::fclose(file);
  }
  va_end(args);
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

void vkCheck(VkResult result, char const *what) {
  if (result != VK_SUCCESS) {
    throw std::runtime_error(std::string(what) + " failed");
  }
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
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout rectDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout quadDescriptorLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout textureDescriptorLayout = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkPipelineLayout rectPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout imagePipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout backdropPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout pathPipelineLayout = VK_NULL_HANDLE;
    VkPipeline rectPipeline = VK_NULL_HANDLE;
    VkPipeline imagePipeline = VK_NULL_HANDLE;
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
};

std::mutex gVulkanCoreMutex;
SharedVulkanCore gVulkanCore;
std::vector<std::string> gRequiredInstanceExtensions;
std::vector<std::string> gRequiredDeviceExtensions;
std::filesystem::path gPipelineCacheDir;
void destroySharedVulkanResources(SharedVulkanCore &core);

std::mutex gCanvasRegistryMutex;
std::vector<::flux::VulkanCanvas *> gCanvases;

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
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "Flux";
  app.apiVersion = VK_API_VERSION_1_3;
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
  VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
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
    VkPhysicalDeviceIDProperties idProps{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 props2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
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
  return cacheDir / ("flux-vulkan-" + identity + ".cache");
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
  VkPipelineCacheCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
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
      VkPhysicalDeviceVulkan13Features vk13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
      VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
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
      if (!deviceExtensions.empty()) {
        std::uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(d, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> availableExtensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(d, nullptr, &extensionCount, availableExtensions.data());
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
          VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
          q.queueFamilyIndex = i;
          q.queueCount = 1;
          q.pQueuePriorities = &priority;
          VkPhysicalDeviceVulkan13Features enabled13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
          enabled13.synchronization2 = VK_TRUE;
          enabled13.dynamicRendering = VK_TRUE;
          std::vector<char const *> deviceExtensionNames = extensionNamePointers(deviceExtensions);
          VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
          info.pNext = &enabled13;
          info.queueCreateInfoCount = 1;
          info.pQueueCreateInfos = &q;
          info.enabledExtensionCount = static_cast<std::uint32_t>(deviceExtensionNames.size());
          info.ppEnabledExtensionNames = deviceExtensionNames.empty() ? nullptr : deviceExtensionNames.data();
          vkCheck(vkCreateDevice(gVulkanCore.physical, &info, nullptr, &gVulkanCore.device), "vkCreateDevice");
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

void destroySharedTexture(VkDevice device, VmaAllocator allocator, Texture &tex) {
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
  destroySharedTexture(device, core.allocator, res.atlas);
  if (res.pathPipeline)
    vkDestroyPipeline(device, res.pathPipeline, nullptr);
  if (res.rectPipeline)
    vkDestroyPipeline(device, res.rectPipeline, nullptr);
  if (res.imagePipeline)
    vkDestroyPipeline(device, res.imagePipeline, nullptr);
  if (res.backdropPipeline)
    vkDestroyPipeline(device, res.backdropPipeline, nullptr);
  if (res.backdropBlurPipeline)
    vkDestroyPipeline(device, res.backdropBlurPipeline, nullptr);
  if (res.pathPipelineLayout)
    vkDestroyPipelineLayout(device, res.pathPipelineLayout, nullptr);
  if (res.rectPipelineLayout)
    vkDestroyPipelineLayout(device, res.rectPipelineLayout, nullptr);
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
  float const maxR = std::max(0.f, std::min(w, h) * 0.5f);
  r.topLeft = std::clamp(r.topLeft, 0.f, maxR);
  r.topRight = std::clamp(r.topRight, 0.f, maxR);
  r.bottomRight = std::clamp(r.bottomRight, 0.f, maxR);
  r.bottomLeft = std::clamp(r.bottomLeft, 0.f, maxR);
  auto fit = [](float &a, float &b, float len) {
    if (a + b > len && len > 0.f) {
      float s = len / (a + b);
      a *= s;
      b *= s;
    }
  };
  fit(r.topLeft, r.topRight, w);
  fit(r.bottomLeft, r.bottomRight, w);
  fit(r.topLeft, r.bottomLeft, h);
  fit(r.topRight, r.bottomRight, h);
  return r;
}

VkShaderModule shaderModule(VkDevice device, unsigned char const *bytes, unsigned int len) {
  VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
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

class VulkanCanvas final : public Canvas {
public:
  VulkanCanvas(VkSurfaceKHR surface, unsigned int handle, TextSystem &textSystem)
      : handle_(handle), textSystem_(textSystem), surface_(surface) {
    instance_ = ensureSharedVulkanInstanceImpl();
    if (!surface_) {
      throw std::runtime_error("Vulkan canvas requires a valid platform surface");
    }
    SharedVulkanCore *shared = acquireSharedVulkanCore(surface_);
    ownsSharedVulkanCore_ = true;
    shared_ = shared;
    physical_ = shared->physical;
    device_ = shared->device;
    allocator_ = shared->allocator;
    queue_ = shared->queue;
    queueFamily_ = shared->queueFamily;
    createCommandObjects();
    chooseSurfaceFormat();
    ensureSharedResources();
    registerCanvas();
  }

  VulkanCanvas(VulkanRenderTargetSpec const &spec, TextSystem &textSystem)
      : textSystem_(textSystem), targetSpec_(spec), targetMode_(true) {
    instance_ = ensureSharedVulkanInstanceImpl();
    if (!targetSpec_.image || !targetSpec_.view || targetSpec_.width == 0 || targetSpec_.height == 0) {
      throw std::runtime_error("Vulkan RenderTarget requires image, view, width, and height");
    }
    SharedVulkanCore *shared = acquireSharedVulkanCore(VK_NULL_HANDLE);
    ownsSharedVulkanCore_ = true;
    shared_ = shared;
    physical_ = shared->physical;
    device_ = shared->device;
    allocator_ = shared->allocator;
    queue_ = shared->queue;
    queueFamily_ = shared->queueFamily;
    surfaceFormat_.format = targetSpec_.format == VK_FORMAT_UNDEFINED
                                 ? VK_FORMAT_B8G8R8A8_UNORM
                                 : targetSpec_.format;
    surfaceFormat_.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    width_ = static_cast<int>(targetSpec_.width);
    height_ = static_cast<int>(targetSpec_.height);
    framebufferWidth_ = width_;
    framebufferHeight_ = height_;
    swapExtent_ = VkExtent2D{targetSpec_.width, targetSpec_.height};
    createCommandObjects();
    ensureSharedResources();
    registerCanvas();
  }

  ~VulkanCanvas() override {
    if (device_) {
      vkDeviceWaitIdle(device_);
    }
    unregisterCanvas();
    destroySwapchain();
    destroyTexture(backdropSceneTexture_);
    destroyTexture(backdropScratchTexture_);
    destroyTexture(backdropBlurTexture_);
    for (auto &kv : imageTextures_) {
      if (kv.second) {
        destroyTexture(*kv.second);
      }
    }
    destroyDeferredTextures(true);
    destroyBuffer(pathBuffer_);
    destroyBuffer(rectBuffer_);
    destroyBuffer(quadBuffer_);
    for (VkSemaphore semaphore : imageAvailable_) {
      if (semaphore)
        vkDestroySemaphore(device_, semaphore, nullptr);
    }
    for (VkSemaphore semaphore : imageRenderFinished_) {
      if (semaphore)
        vkDestroySemaphore(device_, semaphore, nullptr);
    }
    for (VkFence fence : frameFences_) {
      if (fence)
        vkDestroyFence(device_, fence, nullptr);
    }
    if (commandPool_)
      vkDestroyCommandPool(device_, commandPool_, nullptr);
    if (surface_)
      vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (ownsSharedVulkanCore_)
      releaseSharedVulkanCore();
  }

  Backend backend() const noexcept override { return Backend::Vulkan; }
  unsigned int windowHandle() const override { return handle_; }

  void resize(int width, int height) override {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    int const fbW = std::max(1, static_cast<int>(std::lround(static_cast<float>(width_) * dpiScaleX_)));
    int const fbH = std::max(1, static_cast<int>(std::lround(static_cast<float>(height_) * dpiScaleY_)));
    bool const framebufferChanged = fbW != framebufferWidth_ || fbH != framebufferHeight_;
    framebufferWidth_ = fbW;
    framebufferHeight_ = fbH;
    if (targetMode_) {
      return;
    }
    bool const needsLargerSwapchain =
        !swapchain_ || swapExtent_.width == 0 || swapExtent_.height == 0 ||
        fbW > static_cast<int>(swapExtent_.width) || fbH > static_cast<int>(swapExtent_.height);
    if (needsLargerSwapchain) {
      bool const addHeadroom = swapchain_ != VK_NULL_HANDLE;
      swapchainTargetWidth_ = fbW + (addHeadroom ? std::max(128, fbW / 4) : 0);
      swapchainTargetHeight_ = fbH + (addHeadroom ? std::max(128, fbH / 4) : 0);
      swapchainDirty_ = true;
      if (waylandResizeTrace()) {
        waylandResizeTraceLog(
          "wayland-resize-trace: vulkan-resize window=%u logical=%dx%d framebuffer=%dx%d target=%dx%d dirty=1\n",
                     handle_, width_, height_, framebufferWidth_, framebufferHeight_,
                     swapchainTargetWidth_, swapchainTargetHeight_);
      }
    } else if (framebufferChanged && waylandResizeTrace()) {
      waylandResizeTraceLog(
        "wayland-resize-trace: vulkan-resize window=%u logical=%dx%d framebuffer=%dx%d extent=%ux%u dirty=0\n",
                   handle_, width_, height_, framebufferWidth_, framebufferHeight_,
                   swapExtent_.width, swapExtent_.height);
    }
  }

  void updateDpiScale(float sx, float sy) override {
    dpiScaleX_ = std::max(0.25f, sx);
    dpiScaleY_ = std::max(0.25f, sy);
    resize(width_, height_);
  }

  float dpiScale() const noexcept override { return std::max(dpiScaleX_, dpiScaleY_); }

  void beginRecordedOpsCapture(VulkanFrameRecorder *target) {
    if (!target || captureTarget_) {
      return;
    }
    target->clear();
    target->rootClip = Rect::sharp(0.f, 0.f, static_cast<float>(width_), static_cast<float>(height_));
    captureSavedState_ = state_;
    captureSavedStack_ = stateStack_;
    hasCaptureSavedState_ = true;
    captureTarget_ = target;
    stateStack_.clear();
    state_ = {};
    state_.clip = target->rootClip;
  }

  void endRecordedOpsCapture() {
    captureTarget_ = nullptr;
    if (hasCaptureSavedState_) {
      state_ = captureSavedState_;
      stateStack_ = std::move(captureSavedStack_);
      captureSavedState_ = {};
      captureSavedStack_.clear();
      hasCaptureSavedState_ = false;
    }
  }

  bool replayRecordedOps(VulkanFrameRecorder const &recorded) {
    if (!recordedGlyphAtlasCurrent(recorded)) {
      return false;
    }
    if (!prepareRecorderBuffers(recorded)) {
      return false;
    }
    appendRecordedOps(recorded, false);
    return true;
  }

  bool replayRecordedLocalOps(VulkanFrameRecorder const &recorded) {
    if (!recordedGlyphAtlasCurrent(recorded) || !state_.transform.isTranslationOnly()) {
      return false;
    }
    if (recordedOpsContainClipState(recorded)) {
      return false;
    }
    if (!prepareRecorderBuffers(recorded)) {
      return false;
    }
    appendRecordedOps(recorded, true);
    return true;
  }

  void beginFrame() override {
    captureTarget_ = nullptr;
    hasCaptureSavedState_ = false;
    captureSavedStack_.clear();
    rects_.clear();
    quads_.clear();
    batches_.clear();
    ops_.clear();
    pathVerts_.clear();
    stateStack_.clear();
    state_ = {};
    state_.clip = Rect::sharp(0.f, 0.f, static_cast<float>(width_), static_cast<float>(height_));
  }

  void clear(Color color = Colors::transparent) override { clearColor_ = color; }

  std::shared_ptr<Image> rasterize(Size logicalSize, RasterizeDrawCallback const &draw, float dpiScale) {
    if (!draw || logicalSize.width <= 0.f || logicalSize.height <= 0.f)
      return nullptr;
    float const scale = dpiScale > 0.f ? dpiScale : std::max(dpiScaleX_, dpiScaleY_);
    int const logicalW = std::max(1, static_cast<int>(std::ceil(logicalSize.width)));
    int const logicalH = std::max(1, static_cast<int>(std::ceil(logicalSize.height)));
    int const pixelW = std::max(1, static_cast<int>(std::ceil(logicalSize.width * scale)));
    int const pixelH = std::max(1, static_cast<int>(std::ceil(logicalSize.height * scale)));

    Texture target{};
    try {
      createRenderTargetTexture(target, pixelW, pixelH);
      VulkanRenderTargetSpec spec{
          .image = target.image,
          .view = target.view,
          .format = target.format,
          .width = static_cast<std::uint32_t>(pixelW),
          .height = static_cast<std::uint32_t>(pixelH),
          .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
          .finalLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL,
      };
      {
        VulkanCanvas targetCanvas(spec, textSystem_);
        targetCanvas.resize(logicalW, logicalH);
        targetCanvas.updateDpiScale(scale, scale);
        targetCanvas.beginFrame();
        targetCanvas.clear(Colors::transparent);
        draw(targetCanvas, Rect::sharp(0.f, 0.f, logicalSize.width, logicalSize.height));
        targetCanvas.present();
      }

      auto image = std::make_shared<VulkanImage>(device_, allocator_, target.image, target.allocation,
                                                 target.view, target.format, pixelW, pixelH);
      target.image = VK_NULL_HANDLE;
      target.allocation = VK_NULL_HANDLE;
      target.view = VK_NULL_HANDLE;
      return image;
    } catch (...) {
      destroyTexture(target);
      throw;
    }
  }

  void present() override {
    if (width_ <= 0 || height_ <= 0 || framebufferWidth_ <= 0 || framebufferHeight_ <= 0)
      return;
    debug::perf::ScopedTimer timer(debug::perf::TimedMetric::CanvasPresent);
    if (targetMode_) {
      presentRenderTarget();
      return;
    }
    try {
      auto const presentStart = std::chrono::steady_clock::now();
      if (swapchainDirty_ || !swapchain_) {
        recreateSwapchain();
      }
      if (!swapchain_)
        return;
      presentImpl();
      if (waylandResizeTrace()) {
        auto const elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - presentStart).count();
        waylandResizeTraceLog(
          "wayland-resize-trace: vulkan-present window=%u logical=%dx%d framebuffer=%dx%d extent=%ux%u elapsed=%.3fms\n",
                     handle_, width_, height_, framebufferWidth_, framebufferHeight_,
                     swapExtent_.width, swapExtent_.height,
                     static_cast<double>(elapsed) / 1000.0);
      }
    } catch (std::exception const &e) {
      recoverResetFrameFence();
      std::fprintf(stderr, "Flux Vulkan: present failed: %s\n", e.what());
      swapchainDirty_ = true;
    }
  }

  void presentImpl() {
    VkFence const frameFence = frameFences_[currentFrame_];
    VkSemaphore const imageAvailable = imageAvailable_[currentFrame_];
    VkCommandBuffer const commandBuffer = commandBuffers_[currentFrame_];

    vkWaitForFences(device_, 1, &frameFence, VK_TRUE, UINT64_MAX);
    destroyDeferredTextures(false);
    std::uint32_t imageIndex = 0;
    VkResult acquired = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, imageAvailable, VK_NULL_HANDLE,
                                              &imageIndex);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
      swapchainDirty_ = true;
      return;
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
      vkCheck(acquired, "vkAcquireNextImageKHR");
    }
    if (imageInFlightFences_[imageIndex]) {
      vkWaitForFences(device_, 1, &imageInFlightFences_[imageIndex], VK_TRUE, UINT64_MAX);
    }
    imageInFlightFences_[imageIndex] = frameFence;
    if (imageIndex >= imageRenderFinished_.size() || !imageRenderFinished_[imageIndex]) {
      swapchainDirty_ = true;
      return;
    }
    VkSemaphore const renderFinished = imageRenderFinished_[imageIndex];

    uploadAtlasIfNeeded();
    bool const backdropFrame = hasBackdropBlurOps();
    std::uint32_t sceneCopyQuad = 0;
    std::uint32_t horizontalBlurQuad = 0;
    std::uint32_t verticalBlurQuad = 0;
    if (backdropFrame) {
      ensureBackdropSceneTarget();
      float const blurRadius = maxBackdropBlurRadius() /
                               std::sqrt(static_cast<float>(kBackdropBlurIterations));
      sceneCopyQuad = appendSceneCopyQuad();
      horizontalBlurQuad = appendBackdropBlurQuad(blurRadius, 1.f, 0.f);
      verticalBlurQuad = appendBackdropBlurQuad(blurRadius, 0.f, 1.f);
    }
    uploadFrameBuffers();

    vkResetCommandBuffer(commandBuffer, 0);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkCheck(vkBeginCommandBuffer(commandBuffer, &begin), "vkBeginCommandBuffer");
    VkClearValue clear{};
    clear.color.float32[0] = clearColor_.r;
    clear.color.float32[1] = clearColor_.g;
    clear.color.float32[2] = clearColor_.b;
    clear.color.float32[3] = clearColor_.a;
    if (backdropFrame && backdropSceneTexture_.view && backdropScratchTexture_.view && backdropBlurTexture_.view) {
      std::size_t const firstBlur = firstBackdropBlurOp();
      transition(commandBuffer, backdropSceneTexture_, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
      beginColorRendering(commandBuffer, backdropSceneTexture_.view, swapExtent_, clear, VK_ATTACHMENT_LOAD_OP_CLEAR);
      drawOps(commandBuffer, 0, firstBlur);
      vkCmdEndRendering(commandBuffer);
      transition(commandBuffer, backdropSceneTexture_, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
      ensureTextureDescriptor(backdropSceneTexture_);

      Texture *blurSource = &backdropSceneTexture_;
      for (int i = 0; i < kBackdropBlurIterations; ++i) {
        transition(commandBuffer, backdropScratchTexture_, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
        beginColorRendering(commandBuffer, backdropScratchTexture_.view, swapExtent_, clear, VK_ATTACHMENT_LOAD_OP_CLEAR);
        drawBackdropBlurPass(commandBuffer, blurSource, horizontalBlurQuad);
        vkCmdEndRendering(commandBuffer);
        transition(commandBuffer, backdropScratchTexture_, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
        ensureTextureDescriptor(backdropScratchTexture_);

        transition(commandBuffer, backdropBlurTexture_, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
        beginColorRendering(commandBuffer, backdropBlurTexture_.view, swapExtent_, clear, VK_ATTACHMENT_LOAD_OP_CLEAR);
        drawBackdropBlurPass(commandBuffer, &backdropScratchTexture_, verticalBlurQuad);
        vkCmdEndRendering(commandBuffer);
        transition(commandBuffer, backdropBlurTexture_, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
        ensureTextureDescriptor(backdropBlurTexture_);
        blurSource = &backdropBlurTexture_;
      }

      VkClearValue finalClear{};
      transition(commandBuffer, swapchainImages_[imageIndex], VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
      beginColorRendering(commandBuffer, swapchainViews_[imageIndex], swapExtent_, finalClear,
                          VK_ATTACHMENT_LOAD_OP_CLEAR);
      drawBackdropRange(commandBuffer, &backdropSceneTexture_, sceneCopyQuad, 1);
      drawOps(commandBuffer, firstBlur, ops_.size(), &backdropBlurTexture_);
      vkCmdEndRendering(commandBuffer);
    } else {
      transition(commandBuffer, swapchainImages_[imageIndex], VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
      beginColorRendering(commandBuffer, swapchainViews_[imageIndex], swapExtent_, clear,
                          VK_ATTACHMENT_LOAD_OP_CLEAR);
      drawOps(commandBuffer);
      vkCmdEndRendering(commandBuffer);
    }
    transition(commandBuffer, swapchainImages_[imageIndex], VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    writeDebugScreenshotIfRequested(commandBuffer, swapchainImages_[imageIndex]);
    transition(commandBuffer, swapchainImages_[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    vkCheck(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");

    VkSemaphoreSubmitInfo waitSemaphore{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    waitSemaphore.semaphore = imageAvailable;
    waitSemaphore.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkCommandBufferSubmitInfo commandBufferInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    commandBufferInfo.commandBuffer = commandBuffer;
    VkSemaphoreSubmitInfo signalSemaphore{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signalSemaphore.semaphore = renderFinished;
    signalSemaphore.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &waitSemaphore;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &commandBufferInfo;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signalSemaphore;
    resetFrameFenceIndex_ = currentFrame_;
    vkCheck(vkResetFences(device_, 1, &frameFence), "vkResetFences");
    VkResult const submitted = vkQueueSubmit2(queue_, 1, &submit, frameFence);
    if (submitted != VK_SUCCESS) {
      recoverResetFrameFence();
      vkCheck(submitted, "vkQueueSubmit");
    }
    resetFrameFenceIndex_ = kNoResetFrameFence;
    flushScreenshot();

    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex;
    VkResult presented = vkQueuePresentKHR(queue_, &presentInfo);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
      swapchainDirty_ = true;
    } else {
      vkCheck(presented, "vkQueuePresentKHR");
    }
    currentFrame_ = (currentFrame_ + 1u) % kMaxFramesInFlight;
    debug::perf::recordPresentedFrame();
  }

  void recordRenderTargetCommands(VkCommandBuffer commandBuffer, VkImage targetImage,
                                  VkImageView targetView, VkImageLayout initialLayout,
                                  VkImageLayout finalLayout) {
    uploadAtlasIfNeeded();
    bool const backdropFrame = hasBackdropBlurOps();
    std::uint32_t sceneCopyQuad = 0;
    std::uint32_t horizontalBlurQuad = 0;
    std::uint32_t verticalBlurQuad = 0;
    if (backdropFrame) {
      ensureBackdropSceneTarget();
      float const blurRadius = maxBackdropBlurRadius() /
                               std::sqrt(static_cast<float>(kBackdropBlurIterations));
      sceneCopyQuad = appendSceneCopyQuad();
      horizontalBlurQuad = appendBackdropBlurQuad(blurRadius, 1.f, 0.f);
      verticalBlurQuad = appendBackdropBlurQuad(blurRadius, 0.f, 1.f);
    }
    uploadFrameBuffers();

    VkClearValue clear{};
    clear.color.float32[0] = clearColor_.r;
    clear.color.float32[1] = clearColor_.g;
    clear.color.float32[2] = clearColor_.b;
    clear.color.float32[3] = clearColor_.a;
    if (backdropFrame && backdropSceneTexture_.view && backdropScratchTexture_.view && backdropBlurTexture_.view) {
      std::size_t const firstBlur = firstBackdropBlurOp();
      transition(commandBuffer, backdropSceneTexture_, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
      beginColorRendering(commandBuffer, backdropSceneTexture_.view, swapExtent_, clear, VK_ATTACHMENT_LOAD_OP_CLEAR);
      drawOps(commandBuffer, 0, firstBlur);
      vkCmdEndRendering(commandBuffer);
      transition(commandBuffer, backdropSceneTexture_, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
      ensureTextureDescriptor(backdropSceneTexture_);

      Texture *blurSource = &backdropSceneTexture_;
      for (int i = 0; i < kBackdropBlurIterations; ++i) {
        transition(commandBuffer, backdropScratchTexture_, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
        beginColorRendering(commandBuffer, backdropScratchTexture_.view, swapExtent_, clear, VK_ATTACHMENT_LOAD_OP_CLEAR);
        drawBackdropBlurPass(commandBuffer, blurSource, horizontalBlurQuad);
        vkCmdEndRendering(commandBuffer);
        transition(commandBuffer, backdropScratchTexture_, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
        ensureTextureDescriptor(backdropScratchTexture_);

        transition(commandBuffer, backdropBlurTexture_, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
        beginColorRendering(commandBuffer, backdropBlurTexture_.view, swapExtent_, clear, VK_ATTACHMENT_LOAD_OP_CLEAR);
        drawBackdropBlurPass(commandBuffer, &backdropScratchTexture_, verticalBlurQuad);
        vkCmdEndRendering(commandBuffer);
        transition(commandBuffer, backdropBlurTexture_, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
        ensureTextureDescriptor(backdropBlurTexture_);
        blurSource = &backdropBlurTexture_;
      }

      VkClearValue finalClear{};
      transition(commandBuffer, targetImage, initialLayout, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
      beginColorRendering(commandBuffer, targetView, swapExtent_, finalClear, VK_ATTACHMENT_LOAD_OP_CLEAR);
      drawBackdropRange(commandBuffer, &backdropSceneTexture_, sceneCopyQuad, 1);
      drawOps(commandBuffer, firstBlur, ops_.size(), &backdropBlurTexture_);
      vkCmdEndRendering(commandBuffer);
    } else {
      transition(commandBuffer, targetImage, initialLayout, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
      beginColorRendering(commandBuffer, targetView, swapExtent_, clear, VK_ATTACHMENT_LOAD_OP_CLEAR);
      drawOps(commandBuffer);
      vkCmdEndRendering(commandBuffer);
    }
    transition(commandBuffer, targetImage, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL, finalLayout);
  }

  void presentRenderTarget() {
    try {
      targetSpec_.width = std::max<std::uint32_t>(1, targetSpec_.width);
      targetSpec_.height = std::max<std::uint32_t>(1, targetSpec_.height);
      framebufferWidth_ = static_cast<int>(targetSpec_.width);
      framebufferHeight_ = static_cast<int>(targetSpec_.height);
      swapExtent_ = VkExtent2D{targetSpec_.width, targetSpec_.height};

      bool const externalCommandBuffer = targetSpec_.commandBuffer != VK_NULL_HANDLE;
      VkCommandBuffer commandBuffer = targetSpec_.commandBuffer;
      VkFence frameFence = VK_NULL_HANDLE;
      if (!externalCommandBuffer) {
        frameFence = frameFences_[currentFrame_];
        commandBuffer = commandBuffers_[currentFrame_];
        vkWaitForFences(device_, 1, &frameFence, VK_TRUE, UINT64_MAX);
        destroyDeferredTextures(false);
        vkResetCommandBuffer(commandBuffer, 0);
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkCheck(vkBeginCommandBuffer(commandBuffer, &begin), "vkBeginCommandBuffer");
      }

      recordRenderTargetCommands(commandBuffer, targetSpec_.image, targetSpec_.view,
                                 targetSpec_.initialLayout, targetSpec_.finalLayout);

      if (externalCommandBuffer) {
        return;
      }

      vkCheck(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");

      VkSemaphoreSubmitInfo waitSemaphore{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
      waitSemaphore.semaphore = targetSpec_.waitSemaphore;
      waitSemaphore.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
      VkCommandBufferSubmitInfo commandBufferInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
      commandBufferInfo.commandBuffer = commandBuffer;
      VkSemaphoreSubmitInfo signalSemaphore{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
      signalSemaphore.semaphore = targetSpec_.signalSemaphore;
      signalSemaphore.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
      VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
      submit.waitSemaphoreInfoCount = targetSpec_.waitSemaphore ? 1u : 0u;
      submit.pWaitSemaphoreInfos = targetSpec_.waitSemaphore ? &waitSemaphore : nullptr;
      submit.commandBufferInfoCount = 1;
      submit.pCommandBufferInfos = &commandBufferInfo;
      submit.signalSemaphoreInfoCount = targetSpec_.signalSemaphore ? 1u : 0u;
      submit.pSignalSemaphoreInfos = targetSpec_.signalSemaphore ? &signalSemaphore : nullptr;
      resetFrameFenceIndex_ = currentFrame_;
      vkCheck(vkResetFences(device_, 1, &frameFence), "vkResetFences");
      VkResult const submitted = vkQueueSubmit2(queue_, 1, &submit, frameFence);
      if (submitted != VK_SUCCESS) {
        recoverResetFrameFence();
        vkCheck(submitted, "vkQueueSubmit2");
      }
      resetFrameFenceIndex_ = kNoResetFrameFence;
      if (!targetSpec_.signalSemaphore) {
        vkWaitForFences(device_, 1, &frameFence, VK_TRUE, UINT64_MAX);
      }
      currentFrame_ = (currentFrame_ + 1u) % kMaxFramesInFlight;
      debug::perf::recordPresentedFrame();
    } catch (std::exception const &e) {
      recoverResetFrameFence();
      std::fprintf(stderr, "Flux Vulkan: render target present failed: %s\n", e.what());
    }
  }

  void save() override { stateStack_.push_back(state_); }
  void restore() override {
    if (!stateStack_.empty()) {
      state_ = stateStack_.back();
      stateStack_.pop_back();
    }
  }
  void setTransform(Mat3 const &m) override { state_.transform = m; }
  void transform(Mat3 const &m) override { state_.transform = state_.transform * m; }
  void translate(Point p) override { transform(Mat3::translate(p)); }
  void translate(float x, float y) override { translate({x, y}); }
  void scale(float sx, float sy) override { transform(Mat3::scale(sx, sy)); }
  void scale(float s) override { scale(s, s); }
  void rotate(float r) override { transform(Mat3::rotate(r)); }
  void rotate(float r, Point p) override { transform(Mat3::rotate(r, p)); }
  Mat3 currentTransform() const override { return state_.transform; }

  void clipRect(Rect rect, CornerRadius const &, bool) override {
    Rect r = transformedBounds(rect);
    float x0 = std::max(state_.clip.x, r.x);
    float y0 = std::max(state_.clip.y, r.y);
    float x1 = std::min(state_.clip.x + state_.clip.width, r.x + r.width);
    float y1 = std::min(state_.clip.y + state_.clip.height, r.y + r.height);
    state_.clip = Rect::sharp(x0, y0, std::max(0.f, x1 - x0), std::max(0.f, y1 - y0));
  }
  Rect clipBounds() const override { return state_.clip; }
  bool quickReject(Rect rect) const override { return !state_.clip.intersects(transformedBounds(rect)); }
  void setOpacity(float opacity) override { state_.opacity = std::clamp(opacity, 0.f, 1.f); }
  float opacity() const override { return state_.opacity; }
  void setBlendMode(BlendMode mode) override { state_.blendMode = mode; }
  BlendMode blendMode() const override { return state_.blendMode; }

  void pushRectInstance(Rect const &rect, CornerRadius const &cornerRadius, FillStyle const &fill,
                        StrokeStyle const &stroke, float opacity) {
    if (rect.width <= 0.f || rect.height <= 0.f)
      return;
    Point const p0 = state_.transform.apply({rect.x, rect.y});
    Point const p1 = state_.transform.apply({rect.x + rect.width, rect.y});
    Point const p3 = state_.transform.apply({rect.x, rect.y + rect.height});
    RectInstance inst{};
    inst.rect[0] = 0.f;
    inst.rect[1] = 0.f;
    inst.rect[2] = rect.width;
    inst.rect[3] = rect.height;
    inst.axisX[0] = p0.x;
    inst.axisX[1] = p0.y;
    inst.axisX[2] = p1.x - p0.x;
    inst.axisX[3] = p1.y - p0.y;
    inst.axisY[0] = p3.x - p0.x;
    inst.axisY[1] = p3.y - p0.y;
    CornerRadius cr = clampRadii(cornerRadius, rect.width, rect.height);
    inst.radii[0] = cr.topLeft;
    inst.radii[1] = cr.topRight;
    inst.radii[2] = cr.bottomRight;
    inst.radii[3] = cr.bottomLeft;
    encodeFill(fill, inst);
    Color sc{};
    if (stroke.solidColor(&sc) && stroke.width > 0.f) {
      putColor(inst.stroke, sc, opacity);
      inst.params[2] = stroke.width;
    }
    inst.params[3] = opacity;
    RecordingTarget target = recordingTarget();
    std::uint32_t first = static_cast<std::uint32_t>(target.rects.size());
    target.rects.push_back(inst);
    target.ops.push_back(makeDrawOp(DrawOp::Kind::Rect, nullptr, first, 1));
    debug::perf::recordDrawCall(debug::perf::RenderCounterKind::Rect);
  }

  void drawRect(Rect const &rect, CornerRadius const &cornerRadius, FillStyle const &fill,
                StrokeStyle const &stroke, ShadowStyle const &shadow) override {
    if (rect.width <= 0.f || rect.height <= 0.f)
      return;
    Rect rejectBounds = transformedBounds(rect);
    if (!shadow.isNone()) {
      float const pad = shadow.radius;
      Rect const shadowRect = Rect::sharp(rect.x + shadow.offset.x - pad, rect.y + shadow.offset.y - pad,
                                          rect.width + pad * 2.f, rect.height + pad * 2.f);
      rejectBounds = unionRects(rejectBounds, transformedBounds(shadowRect));
    }
    if (rejectBounds.width <= 0.f || rejectBounds.height <= 0.f || !state_.clip.intersects(rejectBounds))
      return;

    if (!shadow.isNone()) {
      if (shadow.radius <= 0.f) {
        Rect const layer = Rect::sharp(rect.x + shadow.offset.x, rect.y + shadow.offset.y,
                                       rect.width, rect.height);
        pushRectInstance(layer, cornerRadius, FillStyle::solid(shadow.color), StrokeStyle::none(), state_.opacity);
      } else {
        int const steps = std::clamp(static_cast<int>(std::ceil(shadow.radius / 3.f)), 3, 8);
        for (int i = steps; i >= 1; --i) {
          float const t = static_cast<float>(i) / static_cast<float>(steps);
          float const spread = shadow.radius * t;
          float const alpha = shadow.color.a * state_.opacity * (1.f - t * 0.72f) / static_cast<float>(steps);
          Color c = shadow.color;
          c.a = alpha;
          Rect const layer = Rect::sharp(rect.x + shadow.offset.x - spread,
                                         rect.y + shadow.offset.y - spread,
                                         rect.width + spread * 2.f,
                                         rect.height + spread * 2.f);
          CornerRadius cr{cornerRadius.topLeft + spread, cornerRadius.topRight + spread,
                          cornerRadius.bottomRight + spread, cornerRadius.bottomLeft + spread};
          pushRectInstance(layer, cr, FillStyle::solid(c), StrokeStyle::none(), 1.f);
        }
      }
    }

    pushRectInstance(rect, cornerRadius, fill, stroke, state_.opacity);
  }

  void drawLine(Point from, Point to, StrokeStyle const &stroke) override {
    if (stroke.isNone())
      return;
    Point a = state_.transform.apply(from);
    Point b = state_.transform.apply(to);
    if (!clipLineToCurrentClip(a, b))
      return;
    StrokeStyle scaled = stroke;
    float const sx = std::hypot(state_.transform.m[0], state_.transform.m[1]);
    float const sy = std::hypot(state_.transform.m[3], state_.transform.m[4]);
    float const s = (sx > 0.f || sy > 0.f) ? (sx + sy) * 0.5f : 1.f;
    scaled.width *= s;
    Path path;
    path.moveTo(a);
    path.lineTo(b);
    DrawState saved = state_;
    state_.transform = Mat3::identity();
    appendPath(path, FillStyle::none(), scaled);
    state_ = saved;
  }
  void drawPath(Path const &path, FillStyle const &fill, StrokeStyle const &stroke, ShadowStyle const &shadow) override {
    if (!shadow.isNone()) {
      DrawState saved = state_;
      state_.transform = state_.transform * Mat3::translate(shadow.offset);
      appendPath(path, FillStyle::solid(shadow.color), StrokeStyle::none());
      state_ = saved;
    }
    appendPath(path, fill, stroke);
  }
  void drawCircle(Point center, float radius, FillStyle const &fill, StrokeStyle const &stroke) override {
    Rect r{center.x - radius, center.y - radius, radius * 2.f, radius * 2.f};
    drawRect(r, CornerRadius::pill(r), fill, stroke, ShadowStyle::none());
  }

  void drawTextLayout(TextLayout const &layout, Point origin) override {
    try {
      ensureAtlasDescriptor();
    } catch (std::exception const &e) {
      std::fprintf(stderr, "Flux Vulkan: glyph atlas descriptor setup failed: %s\n", e.what());
      return;
    }
    RecordingTarget target = recordingTarget();
    std::uint32_t first = static_cast<std::uint32_t>(target.quads.size());
    for (TextLayout::PlacedRun const &placed : layout.runs) {
      for (std::size_t i = 0; i < placed.run.glyphIds.size(); ++i) {
        GlyphSlot const *slot = nullptr;
        try {
          slot = glyphSlot(placed.run.fontId, placed.run.glyphIds[i], placed.run.fontSize);
        } catch (std::exception const &e) {
          std::fprintf(stderr, "Flux Vulkan: glyph atlas update failed: %s\n", e.what());
          continue;
        }
        if (!slot || slot->w == 0 || slot->h == 0)
          continue;
        Point pos = origin + placed.origin + placed.run.positions[i];
        Rect glyphRect = Rect::sharp(pos.x + slot->bearing.x / dpiScaleX_,
                                     pos.y - slot->bearing.y / dpiScaleY_,
                                     static_cast<float>(slot->w) / dpiScaleX_,
                                     static_cast<float>(slot->h) / dpiScaleY_);
        Point p00 = state_.transform.apply({glyphRect.x, glyphRect.y});
        Point p10 = state_.transform.apply({glyphRect.x + glyphRect.width, glyphRect.y});
        Point p01 = state_.transform.apply({glyphRect.x, glyphRect.y + glyphRect.height});
        QuadInstance q{};
        q.rect[0] = 0.f;
        q.rect[1] = 0.f;
        q.rect[2] = glyphRect.width;
        q.rect[3] = glyphRect.height;
        q.axisX[0] = p00.x;
        q.axisX[1] = p00.y;
        q.axisX[2] = p10.x - p00.x;
        q.axisX[3] = p10.y - p00.y;
        q.axisY[0] = p01.x - p00.x;
        q.axisY[1] = p01.y - p00.y;
        q.uv[0] = slot->u0;
        q.uv[1] = slot->v0;
        q.uv[2] = slot->u1;
        q.uv[3] = slot->v1;
        putColor(q.color, placed.run.color, state_.opacity);
        target.quads.push_back(q);
      }
    }
    std::uint32_t count = static_cast<std::uint32_t>(target.quads.size()) - first;
    if (count > 0) {
      Texture *atlas = &resources().atlas;
      if (captureTarget_) {
        captureTarget_->glyphAtlasGeneration = resources().atlasGeneration;
      } else {
        batches_.push_back(ImageBatch{atlas, first, count});
      }
      target.ops.push_back(makeDrawOp(DrawOp::Kind::Image, atlas, first, count));
      debug::perf::recordDrawCall(debug::perf::RenderCounterKind::Glyph);
    }
  }

  void drawImage(Image const &image, Rect const &src, Rect const &dst, CornerRadius const &corners, float opacity) override {
    auto const *vi = dynamic_cast<VulkanImage const *>(&image);
    if (!vi || src.width <= 0.f || src.height <= 0.f || dst.width <= 0.f || dst.height <= 0.f)
      return;
    Texture *texture = nullptr;
    try {
      texture = ensureImageTexture(*vi);
    } catch (std::exception const &e) {
      std::fprintf(stderr, "Flux Vulkan: image texture upload failed: %s\n", e.what());
      return;
    }
    if (!texture)
      return;
    Point p00 = state_.transform.apply({dst.x, dst.y});
    Point p10 = state_.transform.apply({dst.x + dst.width, dst.y});
    Point p01 = state_.transform.apply({dst.x, dst.y + dst.height});
    Size sz = image.size();
    float const u0 = src.x / sz.width;
    float const v0 = src.y / sz.height;
    float const u1 = (src.x + src.width) / sz.width;
    float const v1 = (src.y + src.height) / sz.height;
    QuadInstance q{};
    q.rect[0] = 0.f;
    q.rect[1] = 0.f;
    q.rect[2] = dst.width;
    q.rect[3] = dst.height;
    q.axisX[0] = p00.x;
    q.axisX[1] = p00.y;
    q.axisX[2] = p10.x - p00.x;
    q.axisX[3] = p10.y - p00.y;
    q.axisY[0] = p01.x - p00.x;
    q.axisY[1] = p01.y - p00.y;
    q.uv[0] = u0;
    q.uv[1] = v0;
    q.uv[2] = u1;
    q.uv[3] = v1;
    q.color[0] = q.color[1] = q.color[2] = 1.f;
    q.color[3] = opacity * state_.opacity;
    CornerRadius cr = clampRadii(corners, dst.width, dst.height);
    q.radii[0] = cr.topLeft;
    q.radii[1] = cr.topRight;
    q.radii[2] = cr.bottomRight;
    q.radii[3] = cr.bottomLeft;
    RecordingTarget target = recordingTarget();
    std::uint32_t first = static_cast<std::uint32_t>(target.quads.size());
    target.quads.push_back(q);
    if (!captureTarget_) {
      batches_.push_back(ImageBatch{texture, first, 1});
    }
    target.ops.push_back(makeDrawOp(DrawOp::Kind::Image, texture, first, 1));
    debug::perf::recordDrawCall(debug::perf::RenderCounterKind::Image);
  }

  void drawImageTiled(Image const &image, Rect const &dst, CornerRadius const &corners, float opacity) override {
    Size sz = image.size();
    if (sz.width <= 0.f || sz.height <= 0.f || dst.width <= 0.f || dst.height <= 0.f)
      return;
    int const cols = static_cast<int>(std::ceil(dst.width / sz.width));
    int const rows = static_cast<int>(std::ceil(dst.height / sz.height));
    for (int row = 0; row < rows; ++row) {
      for (int col = 0; col < cols; ++col) {
        Rect tile = Rect::sharp(dst.x + static_cast<float>(col) * sz.width,
                                dst.y + static_cast<float>(row) * sz.height,
                                std::min(sz.width, dst.x + dst.width - (dst.x + static_cast<float>(col) * sz.width)),
                                std::min(sz.height, dst.y + dst.height - (dst.y + static_cast<float>(row) * sz.height)));
        Rect src = Rect::sharp(0.f, 0.f, tile.width, tile.height);
        drawImage(image, src, tile, corners, opacity);
      }
    }
  }

  void drawBackdropBlur(Rect const &rect, float radius, Color tint, CornerRadius const &corners) override {
    if (radius <= 0.f || rect.width <= 0.f || rect.height <= 0.f)
      return;
    Rect const bounds = transformedBounds(rect);
    if (!state_.clip.intersects(bounds))
      return;

    Point p00 = state_.transform.apply({rect.x, rect.y});
    Point p10 = state_.transform.apply({rect.x + rect.width, rect.y});
    Point p01 = state_.transform.apply({rect.x, rect.y + rect.height});
    QuadInstance q{};
    q.rect[0] = 0.f;
    q.rect[1] = 0.f;
    q.rect[2] = rect.width;
    q.rect[3] = rect.height;
    q.axisX[0] = p00.x;
    q.axisX[1] = p00.y;
    q.axisX[2] = p10.x - p00.x;
    q.axisX[3] = p10.y - p00.y;
    q.axisY[0] = p01.x - p00.x;
    q.axisY[1] = p01.y - p00.y;
    q.uv[0] = p00.x / std::max(1.f, static_cast<float>(width_));
    q.uv[1] = p00.y / std::max(1.f, static_cast<float>(height_));
    q.uv[2] = p10.x / std::max(1.f, static_cast<float>(width_));
    q.uv[3] = p01.y / std::max(1.f, static_cast<float>(height_));
    putColor(q.color, tint, state_.opacity);
    CornerRadius cr = clampRadii(corners, rect.width, rect.height);
    q.radii[0] = cr.topLeft;
    q.radii[1] = cr.topRight;
    q.radii[2] = cr.bottomRight;
    q.radii[3] = cr.bottomLeft;
    RecordingTarget target = recordingTarget();
    std::uint32_t first = static_cast<std::uint32_t>(target.quads.size());
    target.quads.push_back(q);
    DrawOp op = makeDrawOp(DrawOp::Kind::BackdropBlur, nullptr, first, 1);
    op.blurRadius = radius * std::max(dpiScaleX_, dpiScaleY_);
    target.ops.push_back(op);
  }

  void *gpuDevice() const override { return device_; }

  void evictImageTexture(VulkanImage const *image) {
    auto it = imageTextures_.find(image);
    if (it == imageTextures_.end())
      return;
    pendingTextureDestroys_.push_back(PendingTextureDestroy{std::move(it->second), kMaxFramesInFlight + 1u});
    imageTextures_.erase(it);
  }

private:
  struct PendingTextureDestroy {
    std::unique_ptr<Texture> texture;
    std::uint32_t framesRemaining = 0;
  };

  struct DrawState {
    Mat3 transform = Mat3::identity();
    Rect clip{};
    float opacity = 1.f;
    BlendMode blendMode = BlendMode::Normal;
  };

  struct RecordingTarget {
    std::vector<DrawOp> &ops;
    std::vector<QuadInstance> &quads;
    std::vector<RectInstance> &rects;
    std::vector<VulkanPathVertex> &pathVerts;
  };

  RecordingTarget recordingTarget() {
    if (captureTarget_) {
      return RecordingTarget{captureTarget_->ops, captureTarget_->quads, captureTarget_->rects,
                             captureTarget_->pathVerts};
    }
    return RecordingTarget{ops_, quads_, rects_, pathVerts_};
  }

  void registerCanvas() {
    std::lock_guard lock(gCanvasRegistryMutex);
    gCanvases.push_back(this);
  }

  void unregisterCanvas() {
    std::lock_guard lock(gCanvasRegistryMutex);
    gCanvases.erase(std::remove(gCanvases.begin(), gCanvases.end(), this), gCanvases.end());
  }

  Rect transformedBounds(Rect rect) const {
    std::array<Point, 4> pts{
        state_.transform.apply({rect.x, rect.y}),
        state_.transform.apply({rect.x + rect.width, rect.y}),
        state_.transform.apply({rect.x + rect.width, rect.y + rect.height}),
        state_.transform.apply({rect.x, rect.y + rect.height}),
    };
    float minX = pts[0].x, maxX = pts[0].x, minY = pts[0].y, maxY = pts[0].y;
    for (Point p : pts) {
      minX = std::min(minX, p.x);
      maxX = std::max(maxX, p.x);
      minY = std::min(minY, p.y);
      maxY = std::max(maxY, p.y);
    }
    return Rect::sharp(minX, minY, maxX - minX, maxY - minY);
  }

  DrawOp makeDrawOp(DrawOp::Kind kind, Texture *texture, std::uint32_t first, std::uint32_t count) const {
    DrawOp op{};
    op.kind = kind;
    op.texture = texture;
    op.first = first;
    op.count = count;
    op.clip = state_.clip;
    return op;
  }

  static bool sameRect(Rect a, Rect b) {
    constexpr float eps = 1e-4f;
    return std::abs(a.x - b.x) <= eps &&
           std::abs(a.y - b.y) <= eps &&
           std::abs(a.width - b.width) <= eps &&
           std::abs(a.height - b.height) <= eps;
  }

  bool recordedOpsContainClipState(VulkanFrameRecorder const &recorded) const {
    return std::any_of(recorded.ops.begin(), recorded.ops.end(), [&recorded](DrawOp const &op) {
      return !sameRect(op.clip, recorded.rootClip);
    });
  }

  bool recordedGlyphAtlasCurrent(VulkanFrameRecorder const &recorded) const {
    return recorded.glyphAtlasGeneration == 0 ||
           recorded.glyphAtlasGeneration == resources().atlasGeneration;
  }

  void uploadRecorderBuffer(VmaAllocation allocation, void const *data, VkDeviceSize size) {
    if (!data || size == 0) {
      return;
    }
    void *mapped = nullptr;
    vkCheck(vmaMapMemory(allocator_, allocation, &mapped), "vmaMapMemory");
    std::memcpy(mapped, data, static_cast<std::size_t>(size));
    vkCheck(vmaFlushAllocation(allocator_, allocation, 0, size), "vmaFlushAllocation");
    vmaUnmapMemory(allocator_, allocation);
  }

  bool prepareRecorderOwnership(VulkanFrameRecorder const &recorded) {
    if ((recorded.allocator && recorded.allocator != allocator_) ||
        (recorded.device && recorded.device != device_) ||
        (recorded.descriptorPool && recorded.descriptorPool != resources().descriptorPool)) {
      return false;
    }
    if (!recorded.allocator) {
      recorded.allocator = allocator_;
    }
    if (!recorded.device) {
      recorded.device = device_;
    }
    if (!recorded.descriptorPool) {
      recorded.descriptorPool = resources().descriptorPool;
    }
    return true;
  }

  bool ensureRecorderBuffer(VulkanFrameRecorder const &recorded, VkBuffer &buffer,
                            VmaAllocation &allocation, VkDeviceSize &capacity,
                            void const *data, VkDeviceSize size, VkBufferUsageFlags usage) {
    if (!data || size == 0) {
      return true;
    }
    if (!prepareRecorderOwnership(recorded))
      return false;
    if (buffer && capacity >= size) {
      return true;
    }
    if (buffer) {
      vmaDestroyBuffer(recorded.allocator, buffer, allocation);
      buffer = VK_NULL_HANDLE;
      allocation = VK_NULL_HANDLE;
      capacity = 0;
    }
    capacity = std::max<VkDeviceSize>(size, 1024);
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = capacity;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    vkCheck(vmaCreateBuffer(allocator_, &info, &allocationInfo, &buffer, &allocation, nullptr),
            "vmaCreateBuffer");
    uploadRecorderBuffer(allocation, data, size);
    return true;
  }

  bool ensureRecorderStorageDescriptor(VulkanFrameRecorder const &recorded, VkDescriptorSet &set,
                                       VkDescriptorSetLayout layout, VkBuffer buffer,
                                       VkDeviceSize capacity) {
    if (!buffer || capacity == 0) {
      return true;
    }
    if (!prepareRecorderOwnership(recorded))
      return false;
    if (!set) {
      VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
      alloc.descriptorPool = recorded.descriptorPool;
      alloc.descriptorSetCount = 1;
      alloc.pSetLayouts = &layout;
      vkCheck(vkAllocateDescriptorSets(device_, &alloc, &set), "vkAllocateDescriptorSets");
    }
    VkDescriptorBufferInfo bi{buffer, 0, capacity};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &bi;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    return true;
  }

  bool prepareRecorderBuffers(VulkanFrameRecorder const &recorded) {
    if (!prepareRecorderOwnership(recorded))
      return false;
    if (!ensureRecorderBuffer(recorded, recorded.preparedRectBuffer, recorded.preparedRectAllocation,
                              recorded.preparedRectCapacity, recorded.rects.data(),
                              static_cast<VkDeviceSize>(recorded.rects.size() * sizeof(RectInstance)),
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
      return false;
    }
    if (!ensureRecorderBuffer(recorded, recorded.preparedQuadBuffer, recorded.preparedQuadAllocation,
                              recorded.preparedQuadCapacity, recorded.quads.data(),
                              static_cast<VkDeviceSize>(recorded.quads.size() * sizeof(QuadInstance)),
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
      return false;
    }
    if (!ensureRecorderBuffer(recorded, recorded.preparedPathVertexBuffer,
                              recorded.preparedPathVertexAllocation,
                              recorded.preparedPathVertexCapacity, recorded.pathVerts.data(),
                              static_cast<VkDeviceSize>(recorded.pathVerts.size() * sizeof(VulkanPathVertex)),
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) {
      return false;
    }
    if (!ensureRecorderStorageDescriptor(recorded, recorded.preparedRectDescriptor,
                                         resources().rectDescriptorLayout,
                                         recorded.preparedRectBuffer,
                                         recorded.preparedRectCapacity)) {
      return false;
    }
    if (!ensureRecorderStorageDescriptor(recorded, recorded.preparedQuadDescriptor,
                                         resources().quadDescriptorLayout,
                                         recorded.preparedQuadBuffer,
                                         recorded.preparedQuadCapacity)) {
      return false;
    }
    return true;
  }

  static void translateRectInstance(RectInstance &inst, float dx, float dy, float opacityScale) {
    inst.axisX[0] += dx;
    inst.axisX[1] += dy;
    inst.stroke[3] *= opacityScale;
    inst.params[3] *= opacityScale;
  }

  static void translateQuadInstance(QuadInstance &inst, float dx, float dy, float opacityScale) {
    inst.axisX[0] += dx;
    inst.axisX[1] += dy;
    inst.color[3] *= opacityScale;
  }

  static void translatePathVertex(VulkanPathVertex &vertex, float dx, float dy, float opacityScale) {
    vertex.x += dx;
    vertex.y += dy;
    vertex.color[3] *= opacityScale;
    vertex.params[3] *= opacityScale;
  }

  void appendRecordedOps(VulkanFrameRecorder const &recorded, bool localReplay) {
    float const dx = localReplay ? state_.transform.m[6] : 0.f;
    float const dy = localReplay ? state_.transform.m[7] : 0.f;
    float const opacityScale = localReplay ? state_.opacity : 1.f;
    constexpr float eps = 1e-4f;
    bool const hasTranslatedBackdropBlur =
        localReplay && (std::abs(dx) > eps || std::abs(dy) > eps) &&
        std::any_of(recorded.ops.begin(), recorded.ops.end(), [](DrawOp const &op) {
          return op.kind == DrawOp::Kind::BackdropBlur;
        });
    bool const canUsePreparedGeometry =
        (!localReplay || std::abs(opacityScale - 1.f) <= eps) &&
        !hasTranslatedBackdropBlur &&
        (recorded.rects.empty() ||
         (recorded.preparedRectBuffer && recorded.preparedRectDescriptor)) &&
        (recorded.quads.empty() ||
         (recorded.preparedQuadBuffer && recorded.preparedQuadDescriptor)) &&
        (recorded.pathVerts.empty() || recorded.preparedPathVertexBuffer);

    if (canUsePreparedGeometry) {
      ops_.reserve(ops_.size() + recorded.ops.size());
      for (DrawOp op : recorded.ops) {
        switch (op.kind) {
        case DrawOp::Kind::Rect:
          op.externalStorageDescriptor = recorded.preparedRectDescriptor;
          break;
        case DrawOp::Kind::Path:
          op.externalVertexBuffer = recorded.preparedPathVertexBuffer;
          break;
        case DrawOp::Kind::Image:
        case DrawOp::Kind::BackdropBlur:
          op.externalStorageDescriptor = recorded.preparedQuadDescriptor;
          break;
        }
        if (localReplay) {
          op.clip = state_.clip;
          op.externalTranslationX = dx;
          op.externalTranslationY = dy;
        }
        ops_.push_back(op);
      }
      return;
    }

    std::uint32_t const rectBase = static_cast<std::uint32_t>(rects_.size());
    std::uint32_t const quadBase = static_cast<std::uint32_t>(quads_.size());
    std::uint32_t const pathBase = static_cast<std::uint32_t>(pathVerts_.size());

    rects_.reserve(rects_.size() + recorded.rects.size());
    for (RectInstance inst : recorded.rects) {
      if (localReplay) {
        translateRectInstance(inst, dx, dy, opacityScale);
      }
      rects_.push_back(inst);
    }

    quads_.reserve(quads_.size() + recorded.quads.size());
    for (QuadInstance inst : recorded.quads) {
      if (localReplay) {
        translateQuadInstance(inst, dx, dy, opacityScale);
      }
      quads_.push_back(inst);
    }

    pathVerts_.reserve(pathVerts_.size() + recorded.pathVerts.size());
    for (VulkanPathVertex vertex : recorded.pathVerts) {
      if (localReplay) {
        translatePathVertex(vertex, dx, dy, opacityScale);
      }
      pathVerts_.push_back(vertex);
    }

    ops_.reserve(ops_.size() + recorded.ops.size());
    float const uvDx = localReplay ? dx / std::max(1.f, static_cast<float>(width_)) : 0.f;
    float const uvDy = localReplay ? dy / std::max(1.f, static_cast<float>(height_)) : 0.f;
    for (DrawOp op : recorded.ops) {
      std::uint32_t const originalFirst = op.first;
      switch (op.kind) {
      case DrawOp::Kind::Rect:
        op.first += rectBase;
        break;
      case DrawOp::Kind::Path:
        op.first += pathBase;
        break;
      case DrawOp::Kind::Image:
        op.first += quadBase;
        break;
      case DrawOp::Kind::BackdropBlur:
        op.first += quadBase;
        if (localReplay) {
          for (std::uint32_t i = 0; i < op.count; ++i) {
            std::size_t const index = static_cast<std::size_t>(quadBase + originalFirst + i);
            if (index >= quads_.size()) {
              break;
            }
            QuadInstance &quad = quads_[index];
            quad.uv[0] += uvDx;
            quad.uv[1] += uvDy;
            quad.uv[2] += uvDx;
            quad.uv[3] += uvDy;
          }
        }
        break;
      }
      if (localReplay) {
        op.clip = state_.clip;
      }
      ops_.push_back(op);
    }
  }

  bool clipLineToCurrentClip(Point &a, Point &b) const {
    float t0 = 0.f;
    float t1 = 1.f;
    float const dx = b.x - a.x;
    float const dy = b.y - a.y;
    float const xMin = state_.clip.x;
    float const yMin = state_.clip.y;
    float const xMax = state_.clip.x + state_.clip.width;
    float const yMax = state_.clip.y + state_.clip.height;
    auto edge = [&](float p, float q) {
      if (std::abs(p) < 1e-6f)
        return q >= 0.f;
      float const r = q / p;
      if (p < 0.f) {
        if (r > t1)
          return false;
        if (r > t0)
          t0 = r;
      } else {
        if (r < t0)
          return false;
        if (r < t1)
          t1 = r;
      }
      return true;
    };
    if (!edge(-dx, a.x - xMin) || !edge(dx, xMax - a.x) ||
        !edge(-dy, a.y - yMin) || !edge(dy, yMax - a.y)) {
      return false;
    }
    Point const original = a;
    a = {original.x + dx * t0, original.y + dy * t0};
    b = {original.x + dx * t1, original.y + dy * t1};
    return state_.clip.width > 0.f && state_.clip.height > 0.f;
  }

  void encodeFill(FillStyle const &fill, RectInstance &inst) {
    Color c{};
    if (fill.solidColor(&c)) {
      putColor(inst.fill0, c, 1.f);
      inst.stops[0] = 0.f;
      inst.params[1] = 1.f;
      return;
    }
    auto writeStops = [&](auto const &g) {
      inst.params[1] = static_cast<float>(g.stopCount);
      for (std::uint8_t i = 0; i < g.stopCount && i < 4; ++i) {
        float *colors[] = {inst.fill0, inst.fill1, inst.fill2, inst.fill3};
        putColor(colors[i], g.stops[i].color, 1.f);
        inst.stops[i] = g.stops[i].position;
      }
    };
    LinearGradient lg{};
    if (fill.linearGradient(&lg) && lg.stopCount > 0) {
      inst.params[0] = 1.f;
      inst.gradient[0] = lg.start.x;
      inst.gradient[1] = lg.start.y;
      inst.gradient[2] = lg.end.x;
      inst.gradient[3] = lg.end.y;
      writeStops(lg);
      return;
    }
    RadialGradient rg{};
    if (fill.radialGradient(&rg) && rg.stopCount > 0) {
      inst.params[0] = 2.f;
      inst.gradient[0] = rg.center.x;
      inst.gradient[1] = rg.center.y;
      inst.gradient[2] = rg.radius;
      writeStops(rg);
      return;
    }
    ConicalGradient cg{};
    if (fill.conicalGradient(&cg) && cg.stopCount > 0) {
      inst.params[0] = 3.f;
      inst.gradient[0] = cg.center.x;
      inst.gradient[1] = cg.center.y;
      inst.gradient[2] = cg.startAngleRadians;
      writeStops(cg);
      return;
    }
    putColor(inst.fill0, Colors::transparent);
    inst.params[1] = 1.f;
  }

  void createCommandObjects() {
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = queueFamily_;
    vkCheck(vkCreateCommandPool(device_, &pool, nullptr, &commandPool_), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = commandPool_;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = static_cast<std::uint32_t>(commandBuffers_.size());
    vkCheck(vkAllocateCommandBuffers(device_, &alloc, commandBuffers_.data()), "vkAllocateCommandBuffers");
    VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (std::size_t i = 0; i < kMaxFramesInFlight; ++i) {
      vkCheck(vkCreateSemaphore(device_, &sem, nullptr, &imageAvailable_[i]), "vkCreateSemaphore");
      vkCheck(vkCreateFence(device_, &fence, nullptr, &frameFences_[i]), "vkCreateFence");
    }
  }

  void recoverResetFrameFence() {
    if (resetFrameFenceIndex_ == kNoResetFrameFence || resetFrameFenceIndex_ >= frameFences_.size()) {
      return;
    }
    VkFence oldFence = frameFences_[resetFrameFenceIndex_];
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkFence newFence = VK_NULL_HANDLE;
    if (vkCreateFence(device_, &fence, nullptr, &newFence) == VK_SUCCESS) {
      for (VkFence &imageFence : imageInFlightFences_) {
        if (imageFence == oldFence) {
          imageFence = newFence;
        }
      }
      frameFences_[resetFrameFenceIndex_] = newFence;
      if (oldFence) {
        vkDestroyFence(device_, oldFence, nullptr);
      }
    }
    resetFrameFenceIndex_ = kNoResetFrameFence;
  }

  SharedVulkanCore::Resources &resources() {
    if (!shared_) {
      throw std::runtime_error("Vulkan shared resources are unavailable");
    }
    return shared_->resources;
  }

  SharedVulkanCore::Resources const &resources() const {
    if (!shared_) {
      throw std::runtime_error("Vulkan shared resources are unavailable");
    }
    return shared_->resources;
  }

  void ensureSharedResources() {
    auto &res = resources();
    VkFormat const format = surfaceFormat_.format == VK_FORMAT_UNDEFINED ? VK_FORMAT_B8G8R8A8_UNORM
                                                                         : surfaceFormat_.format;
    if (res.initialized) {
      if (res.renderFormat != format) {
        throw std::runtime_error("Shared Vulkan resources cannot be reused with a different surface format");
      }
      return;
    }
    res.renderFormat = format;
    createDescriptors();
    createSampler();
    createPipelineCache(*shared_);
    createPipelines();
    createAtlas();
    res.initialized = true;
  }

  void createDescriptors() {
    auto &res = resources();
    VkDescriptorPoolSize sizes[2]{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 512},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 512},
    };
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool.maxSets = 1024;
    pool.poolSizeCount = 2;
    pool.pPoolSizes = sizes;
    vkCheck(vkCreateDescriptorPool(device_, &pool, nullptr, &res.descriptorPool), "vkCreateDescriptorPool");
    res.rectDescriptorLayout = createStorageLayout();
    res.quadDescriptorLayout = createStorageLayout();
    VkDescriptorSetLayoutBinding b{};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount = 1;
    layout.pBindings = &b;
    vkCheck(vkCreateDescriptorSetLayout(device_, &layout, nullptr, &res.textureDescriptorLayout),
            "vkCreateDescriptorSetLayout");
  }

  VkDescriptorSetLayout createStorageLayout() {
    VkDescriptorSetLayoutBinding b{};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.bindingCount = 1;
    info.pBindings = &b;
    VkDescriptorSetLayout out = VK_NULL_HANDLE;
    vkCheck(vkCreateDescriptorSetLayout(device_, &info, nullptr, &out), "vkCreateDescriptorSetLayout");
    return out;
  }

  void createSampler() {
    auto &res = resources();
    VkSamplerCreateInfo s{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    s.magFilter = VK_FILTER_LINEAR;
    s.minFilter = VK_FILTER_LINEAR;
    s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCheck(vkCreateSampler(device_, &s, nullptr, &res.sampler), "vkCreateSampler");
  }

  void chooseSurfaceFormat() {
    std::uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, formats.data());
    surfaceFormat_ = formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
                                     : formats[0];
    auto choose = [&](VkFormat format) {
      for (auto const &fmt : formats) {
        if (fmt.format == format) {
          surfaceFormat_ = fmt;
          return true;
        }
      }
      return false;
    };
    if (choose(VK_FORMAT_B8G8R8A8_UNORM) || choose(VK_FORMAT_R8G8B8A8_UNORM) ||
        choose(VK_FORMAT_A8B8G8R8_UNORM_PACK32)) {
      return;
    }
    for (auto const &fmt : formats) {
      if (fmt.format == VK_FORMAT_B8G8R8A8_SRGB || fmt.format == VK_FORMAT_R8G8B8A8_SRGB ||
          fmt.format == VK_FORMAT_A8B8G8R8_SRGB_PACK32) {
        surfaceFormat_ = fmt;
        break;
      }
    }
  }

  struct VertexInput {
    VkVertexInputBindingDescription const *bindings = nullptr;
    std::uint32_t bindingCount = 0;
    VkVertexInputAttributeDescription const *attrs = nullptr;
    std::uint32_t attrCount = 0;
  };

  void createPipelines() {
    auto &res = resources();
    res.rectPipelineLayout = createPipelineLayout({res.rectDescriptorLayout}, true);
    res.imagePipelineLayout = createPipelineLayout({res.quadDescriptorLayout, res.textureDescriptorLayout}, true);
    res.backdropPipelineLayout = createPipelineLayout({res.quadDescriptorLayout, res.textureDescriptorLayout}, true);
    res.pathPipelineLayout = createPipelineLayout({}, true);
    res.rectPipeline = createPipeline(res.rectPipelineLayout, flux_rect_vert_spv, flux_rect_vert_spv_len,
                                      flux_rect_frag_spv, flux_rect_frag_spv_len, {});
    res.imagePipeline = createPipeline(res.imagePipelineLayout, flux_image_vert_spv, flux_image_vert_spv_len,
                                       flux_image_frag_spv, flux_image_frag_spv_len, {});
    res.backdropPipeline = createPipeline(res.backdropPipelineLayout, flux_image_vert_spv, flux_image_vert_spv_len,
                                          flux_backdrop_frag_spv, flux_backdrop_frag_spv_len, {});
    res.backdropBlurPipeline =
        createPipeline(res.backdropPipelineLayout, flux_image_vert_spv, flux_image_vert_spv_len,
                       flux_backdrop_blur_frag_spv, flux_backdrop_blur_frag_spv_len, {});
    std::array<VkVertexInputBindingDescription, 1> binding{};
    binding[0].binding = 0;
    binding[0].stride = sizeof(VulkanPathVertex);
    binding[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    std::array<VkVertexInputAttributeDescription, 11> attrs{};
    attrs[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VulkanPathVertex, x)};
    attrs[1] = {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanPathVertex, color)};
    attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VulkanPathVertex, viewport)};
    attrs[3] = {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VulkanPathVertex, local)};
    attrs[4] = {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanPathVertex, fill0)};
    attrs[5] = {5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanPathVertex, fill1)};
    attrs[6] = {6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanPathVertex, fill2)};
    attrs[7] = {7, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanPathVertex, fill3)};
    attrs[8] = {8, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanPathVertex, stops)};
    attrs[9] = {9, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanPathVertex, gradient)};
    attrs[10] = {10, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanPathVertex, params)};
    res.pathPipeline = createPipeline(res.pathPipelineLayout, flux_path_vert_spv, flux_path_vert_spv_len,
                                      flux_path_frag_spv, flux_path_frag_spv_len,
                                      {binding.data(), 1, attrs.data(), static_cast<std::uint32_t>(attrs.size())});
  }

  VkPipelineLayout createPipelineLayout(std::initializer_list<VkDescriptorSetLayout> layouts, bool viewportPush) {
    std::vector<VkDescriptorSetLayout> setLayouts(layouts);
    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.offset = 0;
    push.size = sizeof(VulkanDrawPushConstants);
    VkPipelineLayoutCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    info.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size());
    info.pSetLayouts = setLayouts.data();
    if (viewportPush) {
      info.pushConstantRangeCount = 1;
      info.pPushConstantRanges = &push;
    }
    VkPipelineLayout out = VK_NULL_HANDLE;
    vkCheck(vkCreatePipelineLayout(device_, &info, nullptr, &out), "vkCreatePipelineLayout");
    return out;
  }

  VkPipeline createPipeline(VkPipelineLayout layout, unsigned char const *vertBytes, unsigned int vertLen,
                            unsigned char const *fragBytes, unsigned int fragLen, VertexInput input) {
    VkShaderModule vert = shaderModule(device_, vertBytes, vertLen);
    VkShaderModule frag = shaderModule(device_, fragBytes, fragLen);
    VkPipelineShaderStageCreateInfo stages[2]{{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
                                              {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = input.bindingCount;
    vi.pVertexBindingDescriptions = input.bindings;
    vi.vertexAttributeDescriptionCount = input.attrCount;
    vi.pVertexAttributeDescriptions = input.attrs;
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                           VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &blend;
    VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = states;
    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    VkFormat const colorFormat = resources().renderFormat;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colorFormat;
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vi;
    info.pInputAssemblyState = &ia;
    info.pViewportState = &vp;
    info.pRasterizationState = &rs;
    info.pMultisampleState = &ms;
    info.pColorBlendState = &cb;
    info.pDynamicState = &dyn;
    info.layout = layout;
    VkPipeline out = VK_NULL_HANDLE;
    vkCheck(vkCreateGraphicsPipelines(device_, resources().pipelineCache, 1, &info, nullptr, &out),
            "vkCreateGraphicsPipelines");
    vkDestroyShaderModule(device_, vert, nullptr);
    vkDestroyShaderModule(device_, frag, nullptr);
    return out;
  }

  void recreateSwapchain() {
    if (!device_)
      return;
    auto const recreateStart = std::chrono::steady_clock::now();
    for (VkFence fence : frameFences_) {
      if (fence)
        vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);
    }
    VkSwapchainKHR oldSwapchain = swapchain_;
    std::vector<VkImageView> oldViews = std::move(swapchainViews_);
    std::vector<VkSemaphore> oldImageRenderFinished = std::move(imageRenderFinished_);
    swapchain_ = VK_NULL_HANDLE;
    swapchainImages_.clear();
    swapchainViews_.clear();
    imageRenderFinished_.clear();
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps);
    std::uint32_t presentCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &presentCount, nullptr);
    std::vector<VkPresentModeKHR> modes(presentCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &presentCount, modes.data());
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    for (auto m : modes) {
      if (m == VK_PRESENT_MODE_MAILBOX_KHR) {
        mode = m;
        break;
      }
    }
    if (caps.currentExtent.width != UINT32_MAX) {
      swapExtent_ = caps.currentExtent;
    } else {
      std::uint32_t const requestedW =
          static_cast<std::uint32_t>(std::max({1, framebufferWidth_, swapchainTargetWidth_}));
      std::uint32_t const requestedH =
          static_cast<std::uint32_t>(std::max({1, framebufferHeight_, swapchainTargetHeight_}));
      swapExtent_ = VkExtent2D{
          std::clamp(requestedW, caps.minImageExtent.width, caps.maxImageExtent.width),
          std::clamp(requestedH, caps.minImageExtent.height, caps.maxImageExtent.height),
      };
    }
    std::uint32_t imageCount = std::clamp(caps.minImageCount + 1, caps.minImageCount,
                                          caps.maxImageCount ? caps.maxImageCount : caps.minImageCount + 1);
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface_;
    info.minImageCount = imageCount;
    info.imageFormat = surfaceFormat_.format;
    info.imageColorSpace = surfaceFormat_.colorSpace;
    info.imageExtent = swapExtent_;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = mode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = oldSwapchain;
    vkCheck(vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_), "vkCreateSwapchainKHR");
    std::uint32_t count = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
    swapchainImages_.resize(count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, swapchainImages_.data());
    imageInFlightFences_.assign(count, VK_NULL_HANDLE);
    imageRenderFinished_.resize(count, VK_NULL_HANDLE);
    swapchainViews_.resize(count);
    VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (std::size_t i = 0; i < swapchainImages_.size(); ++i) {
      vkCheck(vkCreateSemaphore(device_, &sem, nullptr, &imageRenderFinished_[i]), "vkCreateSemaphore");
      VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      view.image = swapchainImages_[i];
      view.viewType = VK_IMAGE_VIEW_TYPE_2D;
      view.format = surfaceFormat_.format;
      view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      view.subresourceRange.levelCount = 1;
      view.subresourceRange.layerCount = 1;
      vkCheck(vkCreateImageView(device_, &view, nullptr, &swapchainViews_[i]), "vkCreateImageView");
    }
    for (VkSemaphore semaphore : oldImageRenderFinished) {
      if (semaphore)
        vkDestroySemaphore(device_, semaphore, nullptr);
    }
    for (VkImageView view : oldViews)
      vkDestroyImageView(device_, view, nullptr);
    if (oldSwapchain)
      vkDestroySwapchainKHR(device_, oldSwapchain, nullptr);
    swapchainDirty_ = false;
    if (waylandResizeTrace()) {
      auto const elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - recreateStart).count();
      waylandResizeTraceLog(
          "wayland-resize-trace: vulkan-recreate-swapchain window=%u framebuffer=%dx%d extent=%ux%u images=%zu elapsed=%.3fms\n",
                   handle_, framebufferWidth_, framebufferHeight_, swapExtent_.width, swapExtent_.height, swapchainImages_.size(),
                   static_cast<double>(elapsed) / 1000.0);
    }
  }

  void destroySwapchain() {
    for (VkImageView view : swapchainViews_)
      vkDestroyImageView(device_, view, nullptr);
    swapchainViews_.clear();
    swapchainImages_.clear();
    for (VkSemaphore semaphore : imageRenderFinished_) {
      if (semaphore)
        vkDestroySemaphore(device_, semaphore, nullptr);
    }
    imageRenderFinished_.clear();
    if (swapchain_) {
      vkDestroySwapchainKHR(device_, swapchain_, nullptr);
      swapchain_ = VK_NULL_HANDLE;
    }
  }

  void createAtlas() {
    auto &res = resources();
    res.atlas.width = 2048;
    res.atlas.height = 2048;
    res.atlasPixels.assign(static_cast<std::size_t>(res.atlas.width) * res.atlas.height, Rgba{255, 255, 255, 0});
    createTexture(res.atlas, res.atlas.width, res.atlas.height, res.atlasPixels.data(), false);
    ensureTextureDescriptor(res.atlas);
  }

  void appendPath(Path const &path, FillStyle const &fill, StrokeStyle const &stroke) {
    if (path.isEmpty())
      return;
    RecordingTarget target = recordingTarget();
    PathCacheKey const cacheKey{
        .pathHash = path.contentHash(),
        .styleHash = hashFill(fill) ^ (hashStroke(stroke) + 0x9e3779b97f4a7c15ULL) ^
                     (hashTransform(state_.transform, state_.opacity) << 1u),
        .viewportW = width_,
        .viewportH = height_,
    };
    if (auto it = pathCache_.find(cacheKey); it != pathCache_.end()) {
      pathCacheLru_.splice(pathCacheLru_.end(), pathCacheLru_, it->second.lruIt);
      std::uint32_t const firstVertex = static_cast<std::uint32_t>(target.pathVerts.size());
      target.pathVerts.insert(target.pathVerts.end(), it->second.vertices.begin(), it->second.vertices.end());
      if (!it->second.vertices.empty()) {
        target.ops.push_back(makeDrawOp(DrawOp::Kind::Path, nullptr, firstVertex,
                                        static_cast<std::uint32_t>(it->second.vertices.size())));
      }
      return;
    }
    auto subpaths = PathFlattener::flattenSubpaths(path);
    if (subpaths.empty())
      return;
    std::uint32_t const firstVertex = static_cast<std::uint32_t>(target.pathVerts.size());
    for (auto &sp : subpaths) {
      for (Point &p : sp)
        p = state_.transform.apply(p);
    }
    Rect const bounds = boundsOfSubpaths(subpaths);
    auto append = [&](TessellatedPath &&tess, FillStyle const *gradientSource = nullptr) {
      if (gradientSource) {
        for (PathVertex const &vertex : tess.vertices) {
          target.pathVerts.push_back(makeVulkanPathVertex(vertex, gradientSource, bounds, state_.opacity));
        }
      } else {
        for (PathVertex const &vertex : tess.vertices) {
          target.pathVerts.push_back(makeVulkanPathVertex(vertex, nullptr, bounds, 1.f));
        }
      }
    };
    if (!fill.isNone()) {
      Color fc{};
      if (representativeFillColor(fill, &fc)) {
        fc.a *= state_.opacity;
        std::vector<std::vector<Point>> nonempty;
        for (auto const &sp : subpaths) {
          if (sp.size() >= 3)
            nonempty.push_back(sp);
        }
        if (!nonempty.empty()) {
          append(PathFlattener::tessellateFillContours(nonempty, fc, static_cast<float>(width_),
                                                       static_cast<float>(height_),
                                                       PathFlattener::tessWindingFromFillRule(fill.fillRule)),
                 &fill);
        }
      }
    }
    if (!stroke.isNone()) {
      Color sc{};
      if (stroke.solidColor(&sc)) {
        sc.a *= state_.opacity;
        float const sx = std::hypot(state_.transform.m[0], state_.transform.m[1]);
        float const sy = std::hypot(state_.transform.m[3], state_.transform.m[4]);
        float const s = (sx > 0.f || sy > 0.f) ? (sx + sy) * 0.5f : 1.f;
        for (auto const &sp : subpaths) {
          if (sp.size() >= 2) {
            append(PathFlattener::tessellateStroke(sp, stroke.width * s, sc, static_cast<float>(width_),
                                                   static_cast<float>(height_), stroke.join, stroke.cap));
          }
        }
      }
    }
    std::uint32_t const vertexCount = static_cast<std::uint32_t>(target.pathVerts.size()) - firstVertex;
    if (vertexCount > 0) {
      std::vector<VulkanPathVertex> cached(target.pathVerts.begin() + firstVertex, target.pathVerts.end());
      pathCacheLru_.push_back(cacheKey);
      auto lruIt = std::prev(pathCacheLru_.end());
      auto [it, inserted] = pathCache_.emplace(cacheKey, CachedPath{std::move(cached), lruIt});
      if (inserted) {
        cachedPathVertexCount_ += it->second.vertices.size();
      } else {
        pathCacheLru_.erase(lruIt);
      }
      trimPathCache();
      target.ops.push_back(makeDrawOp(DrawOp::Kind::Path, nullptr, firstVertex, vertexCount));
    }
  }

  void trimPathCache() {
    constexpr std::size_t kMaxCachedPathVertices = 500'000;
    while (cachedPathVertexCount_ > kMaxCachedPathVertices && !pathCache_.empty()) {
      if (pathCacheLru_.empty()) {
        pathCache_.clear();
        cachedPathVertexCount_ = 0;
        return;
      }
      PathCacheKey const key = pathCacheLru_.front();
      auto it = pathCache_.find(key);
      if (it != pathCache_.end()) {
        cachedPathVertexCount_ -= it->second.vertices.size();
        pathCache_.erase(it);
      }
      pathCacheLru_.pop_front();
    }
  }

  void uploadFrameBuffers() {
    ensureBuffer(rectBuffer_, std::max<VkDeviceSize>(sizeof(RectInstance), rects_.size() * sizeof(RectInstance)),
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    upload(rectBuffer_, rects_.data(), rects_.size() * sizeof(RectInstance));
    ensureStorageDescriptor(rectDescriptorSet_, resources().rectDescriptorLayout, rectBuffer_);
    ensureBuffer(quadBuffer_, std::max<VkDeviceSize>(sizeof(QuadInstance), quads_.size() * sizeof(QuadInstance)),
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    upload(quadBuffer_, quads_.data(), quads_.size() * sizeof(QuadInstance));
    ensureStorageDescriptor(quadDescriptorSet_, resources().quadDescriptorLayout, quadBuffer_);
    ensureBuffer(pathBuffer_, std::max<VkDeviceSize>(sizeof(VulkanPathVertex), pathVerts_.size() * sizeof(VulkanPathVertex)),
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    upload(pathBuffer_, pathVerts_.data(), pathVerts_.size() * sizeof(VulkanPathVertex));
  }

  void ensureBuffer(Buffer &buffer, VkDeviceSize size, VkBufferUsageFlags usage) {
    if (buffer.buffer && buffer.capacity >= size)
      return;
    destroyBuffer(buffer);
    buffer.capacity = std::max<VkDeviceSize>(size, 1024);
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = buffer.capacity;
    info.usage = usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo allocation{};
    allocation.usage = VMA_MEMORY_USAGE_AUTO;
    allocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    vkCheck(vmaCreateBuffer(allocator_, &info, &allocation, &buffer.buffer, &buffer.allocation, nullptr),
            "vmaCreateBuffer");
  }

  void upload(Buffer &buffer, void const *data, std::size_t size) {
    if (!size)
      return;
    void *mapped = nullptr;
    vkCheck(vmaMapMemory(allocator_, buffer.allocation, &mapped), "vmaMapMemory");
    std::memcpy(mapped, data, size);
    vkCheck(vmaFlushAllocation(allocator_, buffer.allocation, 0, size), "vmaFlushAllocation");
    vmaUnmapMemory(allocator_, buffer.allocation);
  }

  void destroyBuffer(Buffer &buffer) {
    if (buffer.buffer)
      vmaDestroyBuffer(allocator_, buffer.buffer, buffer.allocation);
    buffer = {};
  }

  void ensureStorageDescriptor(VkDescriptorSet &set, VkDescriptorSetLayout layout, Buffer const &buffer) {
    if (!set) {
      VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
      alloc.descriptorPool = resources().descriptorPool;
      alloc.descriptorSetCount = 1;
      alloc.pSetLayouts = &layout;
      vkCheck(vkAllocateDescriptorSets(device_, &alloc, &set), "vkAllocateDescriptorSets");
    }
    VkDescriptorBufferInfo bi{buffer.buffer, 0, buffer.capacity};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &bi;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
  }

  void ensureBackdropSceneTarget() {
    int const targetW = static_cast<int>(std::max(1u, swapExtent_.width));
    int const targetH = static_cast<int>(std::max(1u, swapExtent_.height));
    auto ensure = [&](Texture &texture) {
      if (texture.image && texture.width == targetW && texture.height == targetH) {
        return;
      }
      destroyTexture(texture);
      createRenderTargetTexture(texture, targetW, targetH);
    };
    ensure(backdropSceneTexture_);
    ensure(backdropScratchTexture_);
    ensure(backdropBlurTexture_);
  }

  std::uint32_t appendSceneCopyQuad() {
    QuadInstance q{};
    q.rect[0] = 0.f;
    q.rect[1] = 0.f;
    q.rect[2] = static_cast<float>(width_);
    q.rect[3] = static_cast<float>(height_);
    q.axisX[0] = 0.f;
    q.axisX[1] = 0.f;
    q.axisX[2] = static_cast<float>(width_);
    q.axisX[3] = 0.f;
    q.axisY[0] = 0.f;
    q.axisY[1] = static_cast<float>(height_);
    q.uv[0] = 0.f;
    q.uv[1] = 0.f;
    q.uv[2] = 1.f;
    q.uv[3] = 1.f;
    q.color[0] = q.color[1] = q.color[2] = q.color[3] = 0.f;
    q.radii[0] = 0.f;
    std::uint32_t const first = static_cast<std::uint32_t>(quads_.size());
    quads_.push_back(q);
    return first;
  }

  std::uint32_t appendBackdropBlurQuad(float radiusPx, float axisX, float axisY) {
    QuadInstance q{};
    q.rect[0] = 0.f;
    q.rect[1] = 0.f;
    q.rect[2] = static_cast<float>(width_);
    q.rect[3] = static_cast<float>(height_);
    q.axisX[0] = 0.f;
    q.axisX[1] = 0.f;
    q.axisX[2] = static_cast<float>(width_);
    q.axisX[3] = 0.f;
    q.axisY[0] = 0.f;
    q.axisY[1] = static_cast<float>(height_);
    q.uv[0] = 0.f;
    q.uv[1] = 0.f;
    q.uv[2] = 1.f;
    q.uv[3] = 1.f;
    q.color[0] = q.color[1] = q.color[2] = q.color[3] = 0.f;
    q.radii[0] = radiusPx;
    q.radii[1] = axisX;
    q.radii[2] = axisY;
    q.radii[3] = 0.f;
    std::uint32_t const first = static_cast<std::uint32_t>(quads_.size());
    quads_.push_back(q);
    return first;
  }

  float maxBackdropBlurRadius() const {
    float radius = 0.f;
    for (DrawOp const &op : ops_) {
      if (op.kind == DrawOp::Kind::BackdropBlur) {
        radius = std::max(radius, op.blurRadius);
      }
    }
    return radius;
  }

  VulkanDrawPushConstants drawPushConstants(float translationX = 0.f,
                                            float translationY = 0.f) const {
    VulkanDrawPushConstants push{};
    push.viewport[0] = static_cast<float>(width_);
    push.viewport[1] = static_cast<float>(height_);
    push.translation[0] = translationX;
    push.translation[1] = translationY;
    return push;
  }

  void drawRectRange(VkCommandBuffer commandBuffer, std::uint32_t first, std::uint32_t count,
                     VkDescriptorSet descriptor = VK_NULL_HANDLE,
                     float translationX = 0.f, float translationY = 0.f) {
    if (count == 0)
      return;
    VulkanDrawPushConstants const push = drawPushConstants(translationX, translationY);
    auto const &res = resources();
    VkDescriptorSet const storageDescriptor = descriptor ? descriptor : rectDescriptorSet_;
    if (!storageDescriptor)
      return;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, res.rectPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, res.rectPipelineLayout, 0, 1,
                            &storageDescriptor, 0, nullptr);
    vkCmdPushConstants(commandBuffer, res.rectPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
    vkCmdDraw(commandBuffer, 6, count, 0, first);
  }

  void drawPathRange(VkCommandBuffer commandBuffer, std::uint32_t first, std::uint32_t count,
                     VkBuffer vertexBuffer = VK_NULL_HANDLE,
                     float translationX = 0.f, float translationY = 0.f) {
    if (count == 0)
      return;
    VkDeviceSize offset = 0;
    VkBuffer const buffer = vertexBuffer ? vertexBuffer : pathBuffer_.buffer;
    if (!buffer)
      return;
    VulkanDrawPushConstants const push = drawPushConstants(translationX, translationY);
    auto const &res = resources();
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, res.pathPipeline);
    vkCmdPushConstants(commandBuffer, res.pathPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &buffer, &offset);
    vkCmdDraw(commandBuffer, count, 1, first, 0);
  }

  void drawImageRange(VkCommandBuffer commandBuffer, Texture *texture, std::uint32_t first, std::uint32_t count,
                      VkDescriptorSet descriptor = VK_NULL_HANDLE,
                      float translationX = 0.f, float translationY = 0.f) {
    if (!texture || !texture->descriptor || count == 0)
      return;
    VulkanDrawPushConstants const push = drawPushConstants(translationX, translationY);
    auto const &res = resources();
    VkDescriptorSet const storageDescriptor = descriptor ? descriptor : quadDescriptorSet_;
    if (!storageDescriptor)
      return;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, res.imagePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, res.imagePipelineLayout, 0, 1,
                            &storageDescriptor, 0, nullptr);
    vkCmdPushConstants(commandBuffer, res.imagePipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, res.imagePipelineLayout, 1, 1,
                            &texture->descriptor, 0, nullptr);
    vkCmdDraw(commandBuffer, 6, count, 0, first);
  }

  void drawBackdropRange(VkCommandBuffer commandBuffer, Texture *texture, std::uint32_t first, std::uint32_t count,
                         VkDescriptorSet descriptor = VK_NULL_HANDLE,
                         float translationX = 0.f, float translationY = 0.f) {
    if (!texture || !texture->descriptor || count == 0)
      return;
    VulkanDrawPushConstants const push = drawPushConstants(translationX, translationY);
    auto const &res = resources();
    VkDescriptorSet const storageDescriptor = descriptor ? descriptor : quadDescriptorSet_;
    if (!storageDescriptor)
      return;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, res.backdropPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, res.backdropPipelineLayout, 0, 1,
                            &storageDescriptor, 0, nullptr);
    vkCmdPushConstants(commandBuffer, res.backdropPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(push), &push);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, res.backdropPipelineLayout, 1, 1,
                            &texture->descriptor, 0, nullptr);
    vkCmdDraw(commandBuffer, 6, count, 0, first);
  }

  void drawBackdropBlurPass(VkCommandBuffer commandBuffer, Texture *texture, std::uint32_t first) {
    if (!texture || !texture->descriptor)
      return;
    setViewportScissor(commandBuffer, Rect::sharp(0.f, 0.f, static_cast<float>(width_), static_cast<float>(height_)));
    VulkanDrawPushConstants const push = drawPushConstants();
    auto const &res = resources();
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, res.backdropBlurPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, res.backdropPipelineLayout, 0, 1,
                            &quadDescriptorSet_, 0, nullptr);
    vkCmdPushConstants(commandBuffer, res.backdropPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(push), &push);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, res.backdropPipelineLayout, 1, 1,
                            &texture->descriptor, 0, nullptr);
    vkCmdDraw(commandBuffer, 6, 1, 0, first);
  }

  bool hasBackdropBlurOps() const {
    return std::any_of(ops_.begin(), ops_.end(), [](DrawOp const &op) {
      return op.kind == DrawOp::Kind::BackdropBlur;
    });
  }

  std::size_t firstBackdropBlurOp() const {
    auto const it = std::find_if(ops_.begin(), ops_.end(), [](DrawOp const &op) {
      return op.kind == DrawOp::Kind::BackdropBlur;
    });
    return it == ops_.end() ? ops_.size() : static_cast<std::size_t>(std::distance(ops_.begin(), it));
  }

  void drawOps(VkCommandBuffer commandBuffer, std::size_t start = 0,
               std::size_t end = std::numeric_limits<std::size_t>::max(),
               Texture *backdropSource = nullptr) {
    std::size_t const opEnd = std::min(end, ops_.size());
    for (std::size_t index = std::min(start, opEnd); index < opEnd; ++index) {
      DrawOp const &op = ops_[index];
      if (op.clip.width <= 0.f || op.clip.height <= 0.f) {
        continue;
      }
      setViewportScissor(commandBuffer, op.clip);
      switch (op.kind) {
      case DrawOp::Kind::Rect:
        drawRectRange(commandBuffer, op.first, op.count, op.externalStorageDescriptor,
                      op.externalTranslationX, op.externalTranslationY);
        break;
      case DrawOp::Kind::Path:
        drawPathRange(commandBuffer, op.first, op.count, op.externalVertexBuffer,
                      op.externalTranslationX, op.externalTranslationY);
        break;
      case DrawOp::Kind::Image:
        drawImageRange(commandBuffer, op.texture, op.first, op.count, op.externalStorageDescriptor,
                       op.externalTranslationX, op.externalTranslationY);
        break;
      case DrawOp::Kind::BackdropBlur:
        drawBackdropRange(commandBuffer, backdropSource, op.first, op.count, op.externalStorageDescriptor,
                          op.externalTranslationX, op.externalTranslationY);
        break;
      }
    }
  }

  void beginColorRendering(VkCommandBuffer commandBuffer, VkImageView view, VkExtent2D extent,
                           VkClearValue const &clear, VkAttachmentLoadOp loadOp) {
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = view;
    color.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
    color.loadOp = loadOp;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue = clear;
    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent = extent;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    vkCmdBeginRendering(commandBuffer, &rendering);
  }

  void setViewportScissor(VkCommandBuffer commandBuffer, Rect clip) {
    float const renderWidth = static_cast<float>(std::max(1, framebufferWidth_));
    float const renderHeight = static_cast<float>(std::max(1, framebufferHeight_));
    VkViewport vp{0.f, 0.f, renderWidth, renderHeight, 0.f, 1.f};
    vkCmdSetViewport(commandBuffer, 0, 1, &vp);

    float const scaleX = renderWidth / std::max(1.f, static_cast<float>(width_));
    float const scaleY = renderHeight / std::max(1.f, static_cast<float>(height_));
    float const maxX = renderWidth;
    float const maxY = renderHeight;
    float const x0f = std::clamp(std::floor(clip.x * scaleX), 0.f, maxX);
    float const y0f = std::clamp(std::floor(clip.y * scaleY), 0.f, maxY);
    float const x1f = std::clamp(std::ceil((clip.x + clip.width) * scaleX), 0.f, maxX);
    float const y1f = std::clamp(std::ceil((clip.y + clip.height) * scaleY), 0.f, maxY);
    std::uint32_t const x0 = static_cast<std::uint32_t>(x0f);
    std::uint32_t const y0 = static_cast<std::uint32_t>(y0f);
    std::uint32_t const x1 = static_cast<std::uint32_t>(x1f);
    std::uint32_t const y1 = static_cast<std::uint32_t>(y1f);
    VkRect2D sc{{static_cast<std::int32_t>(x0), static_cast<std::int32_t>(y0)},
                {x1 > x0 ? x1 - x0 : 0u, y1 > y0 ? y1 - y0 : 0u}};
    vkCmdSetScissor(commandBuffer, 0, 1, &sc);
  }

  bool atlasHasSpace(SharedVulkanCore::Resources const &res, std::uint32_t width, std::uint32_t height) const {
    int const pad = 1;
    int x = res.atlasX;
    int y = res.atlasY;
    int rowH = res.atlasRowH;
    if (x + static_cast<int>(width) + pad >= res.atlas.width) {
      x = pad;
      y += rowH + pad;
    }
    return y + static_cast<int>(height) + pad < res.atlas.height;
  }

  void placeGlyphInAtlas(SharedVulkanCore::Resources &res, GlyphSlot &slot) {
    int const pad = 1;
    if (res.atlasX + static_cast<int>(slot.w) + pad >= res.atlas.width) {
      res.atlasX = pad;
      res.atlasY += res.atlasRowH + pad;
      res.atlasRowH = 0;
    }
    slot.x = res.atlasX;
    slot.y = res.atlasY;
    res.atlasX += static_cast<int>(slot.w) + pad;
    res.atlasRowH = std::max(res.atlasRowH, static_cast<int>(slot.h));
    for (std::uint32_t row = 0; row < slot.h; ++row) {
      for (std::uint32_t col = 0; col < slot.w; ++col) {
        Rgba &px = res.atlasPixels[static_cast<std::size_t>(slot.y + row) * res.atlas.width + slot.x + col];
        px = {255, 255, 255, slot.alpha[static_cast<std::size_t>(row) * slot.w + col]};
      }
    }
    slot.u0 = static_cast<float>(slot.x) / res.atlas.width;
    slot.v0 = static_cast<float>(slot.y) / res.atlas.height;
    slot.u1 = static_cast<float>(slot.x + static_cast<int>(slot.w)) / res.atlas.width;
    slot.v1 = static_cast<float>(slot.y + static_cast<int>(slot.h)) / res.atlas.height;
  }

  void rebuildAtlas(SharedVulkanCore::Resources &res) {
    std::vector<std::pair<GlyphKey, GlyphSlot>> slots;
    slots.reserve(res.glyphs.size());
    for (auto const &[key, slot] : res.glyphs) {
      if (slot.w > 0 && slot.h > 0 && !slot.alpha.empty()) {
        slots.push_back({key, slot});
      }
    }
    std::sort(slots.begin(), slots.end(), [](auto const &a, auto const &b) {
      return a.second.lastUsed > b.second.lastUsed;
    });
    res.atlasPixels.assign(static_cast<std::size_t>(res.atlas.width) * res.atlas.height, Rgba{255, 255, 255, 0});
    res.atlasX = 1;
    res.atlasY = 1;
    res.atlasRowH = 0;
    res.glyphs.clear();
    ++res.atlasGeneration;
    for (auto &[key, slot] : slots) {
      if (!atlasHasSpace(res, slot.w, slot.h)) {
        continue;
      }
      placeGlyphInAtlas(res, slot);
      res.glyphs.emplace(key, std::move(slot));
    }
    res.atlasDirty = true;
  }

  bool evictGlyphsForAtlasSpace(SharedVulkanCore::Resources &res, std::uint32_t width, std::uint32_t height) {
    if (width + 2u >= static_cast<std::uint32_t>(res.atlas.width) ||
        height + 2u >= static_cast<std::uint32_t>(res.atlas.height)) {
      return false;
    }
    while (!atlasHasSpace(res, width, height) && !res.glyphs.empty()) {
      std::vector<std::pair<GlyphKey, std::uint64_t>> byAge;
      byAge.reserve(res.glyphs.size());
      for (auto const &[key, slot] : res.glyphs) {
        if (slot.w > 0 && slot.h > 0) {
          byAge.push_back({key, slot.lastUsed});
        }
      }
      if (byAge.empty())
        return false;
      std::sort(byAge.begin(), byAge.end(), [](auto const &a, auto const &b) {
        return a.second < b.second;
      });
      std::size_t const eraseCount = std::max<std::size_t>(1, byAge.size() / 4);
      for (std::size_t i = 0; i < eraseCount && i < byAge.size(); ++i) {
        res.glyphs.erase(byAge[i].first);
      }
      rebuildAtlas(res);
    }
    return atlasHasSpace(res, width, height);
  }

  GlyphSlot const *glyphSlot(std::uint32_t fontId, std::uint16_t glyphId, float fontSize) {
    auto &res = resources();
    std::uint16_t size = static_cast<std::uint16_t>(std::clamp(std::round(fontSize * dpiScaleY_), 1.f, 512.f));
    GlyphKey key{fontId, glyphId, size};
    auto it = res.glyphs.find(key);
    if (it != res.glyphs.end()) {
      it->second.lastUsed = ++res.atlasUseCounter;
      return &it->second;
    }
    std::uint32_t gw = 0, gh = 0;
    Point bearing{};
    std::vector<std::uint8_t> alpha = textSystem_.rasterizeGlyph(fontId, glyphId, static_cast<float>(size), gw, gh,
                                                                 bearing);
    if (gw == 0 || gh == 0 || alpha.empty()) {
      GlyphSlot empty{};
      empty.lastUsed = ++res.atlasUseCounter;
      auto [inserted, ok] = res.glyphs.emplace(key, std::move(empty));
      (void)ok;
      return &inserted->second;
    }
    if (!atlasHasSpace(res, gw, gh) && !evictGlyphsForAtlasSpace(res, gw, gh))
      return nullptr;
    GlyphSlot slot{};
    slot.w = gw;
    slot.h = gh;
    slot.bearing = bearing;
    slot.lastUsed = ++res.atlasUseCounter;
    slot.alpha = std::move(alpha);
    placeGlyphInAtlas(res, slot);
    res.atlasDirty = true;
    auto [inserted, ok] = res.glyphs.emplace(key, slot);
    (void)ok;
    return &inserted->second;
  }

  void ensureAtlasDescriptor() { ensureTextureDescriptor(resources().atlas); }

  void uploadAtlasIfNeeded() {
    auto &res = resources();
    if (!res.atlasDirty)
      return;
    uploadTexture(res.atlas, res.atlasPixels.data());
    res.atlasDirty = false;
  }

  Texture *ensureImageTexture(VulkanImage const &image) {
    auto it = imageTextures_.find(&image);
    if (it != imageTextures_.end())
      return it->second.get();
    auto tex = std::make_unique<Texture>();
    try {
      if (image.external || image.ownsGpuResource) {
        tex->image = image.image;
        tex->view = image.view;
        tex->format = image.format;
        tex->layout = image.ownsImportedMemory ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
        tex->width = image.width;
        tex->height = image.height;
        tex->ownsImage = false;
        tex->ownsView = false;
        if (image.ownsImportedMemory) {
          transitionImmediate(*tex, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
        }
      } else {
        createTexture(*tex, image.width, image.height, image.pixels.data(), true);
      }
      ensureTextureDescriptor(*tex);
    } catch (...) {
      destroyTexture(*tex);
      throw;
    }
    auto [inserted, ok] = imageTextures_.emplace(&image, std::move(tex));
    (void)ok;
    return inserted->second.get();
  }

  void destroyDeferredTextures(bool force) {
    for (auto it = pendingTextureDestroys_.begin(); it != pendingTextureDestroys_.end();) {
      if (!force && it->framesRemaining > 0) {
        --it->framesRemaining;
      }
      if (force || it->framesRemaining == 0) {
        if (it->texture) {
          destroyTexture(*it->texture);
        }
        it = pendingTextureDestroys_.erase(it);
      } else {
        ++it;
      }
    }
  }

  void createTexture(Texture &tex, int width, int height, Rgba const *pixels, bool uploadNow) {
    tex.width = width;
    tex.height = height;
    tex.format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = VK_FORMAT_R8G8B8A8_UNORM;
    image.extent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo allocation{};
    allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    vkCheck(vmaCreateImage(allocator_, &image, &allocation, &tex.image, &tex.allocation, nullptr),
            "vmaCreateImage");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = tex.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = VK_FORMAT_R8G8B8A8_UNORM;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.layerCount = 1;
    vkCheck(vkCreateImageView(device_, &view, nullptr, &tex.view), "vkCreateImageView");
    transitionImmediate(tex, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
    if (uploadNow)
      uploadTexture(tex, pixels);
  }

  void createRenderTargetTexture(Texture &tex, int width, int height) {
    tex.width = width;
    tex.height = height;
    tex.format = surfaceFormat_.format == VK_FORMAT_UNDEFINED ? VK_FORMAT_B8G8R8A8_UNORM : surfaceFormat_.format;
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = tex.format;
    image.extent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo allocation{};
    allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    vkCheck(vmaCreateImage(allocator_, &image, &allocation, &tex.image, &tex.allocation, nullptr),
            "vmaCreateImage");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = tex.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = image.format;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.layerCount = 1;
    vkCheck(vkCreateImageView(device_, &view, nullptr, &tex.view), "vkCreateImageView");
    tex.layout = VK_IMAGE_LAYOUT_UNDEFINED;
  }

  void uploadTexture(Texture &tex, Rgba const *pixels) {
    if (!pixels || tex.width <= 0 || tex.height <= 0)
      return;
    Buffer staging{};
    VkDeviceSize size = static_cast<VkDeviceSize>(tex.width) * tex.height * sizeof(Rgba);
    ensureBuffer(staging, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    upload(staging, pixels, static_cast<std::size_t>(size));
    VkCommandBuffer cmd = beginImmediate();
    transition(cmd, tex, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {static_cast<std::uint32_t>(tex.width), static_cast<std::uint32_t>(tex.height), 1};
    vkCmdCopyBufferToImage(cmd, staging.buffer, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    transition(cmd, tex, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
    endImmediate(cmd);
    destroyBuffer(staging);
  }

  void ensureTextureDescriptor(Texture &tex) {
    if (tex.descriptor)
      return;
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = resources().descriptorPool;
    alloc.descriptorSetCount = 1;
    VkDescriptorSetLayout const layout = resources().textureDescriptorLayout;
    alloc.pSetLayouts = &layout;
    vkCheck(vkAllocateDescriptorSets(device_, &alloc, &tex.descriptor), "vkAllocateDescriptorSets");
    VkDescriptorImageInfo ii{resources().sampler, tex.view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = tex.descriptor;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &ii;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
  }

  void destroyTexture(Texture &tex) {
    if (tex.view && tex.ownsView)
      vkDestroyImageView(device_, tex.view, nullptr);
    if (tex.image && tex.ownsImage)
      vmaDestroyImage(allocator_, tex.image, tex.allocation);
    tex = {};
  }

  VkCommandBuffer beginImmediate() {
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = commandPool_;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkCheck(vkAllocateCommandBuffers(device_, &alloc, &cmd), "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer");
    return cmd;
  }

  void endImmediate(VkCommandBuffer cmd) {
    vkCheck(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");
    VkCommandBufferSubmitInfo commandBufferInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    commandBufferInfo.commandBuffer = cmd;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &commandBufferInfo;
    vkCheck(vkQueueSubmit2(queue_, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit2");
    vkQueueWaitIdle(queue_);
    vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
  }

  void transitionImmediate(Texture &texture, VkImageLayout newLayout) {
    VkCommandBuffer cmd = beginImmediate();
    transition(cmd, texture, newLayout);
    endImmediate(cmd);
  }

  static VkPipelineStageFlags2 stageMaskForLayout(VkImageLayout layout) {
    switch (layout) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
      return VK_PIPELINE_STAGE_2_NONE;
    case VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL:
      return VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      return VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    case VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL:
      return VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
      return VK_PIPELINE_STAGE_2_NONE;
    default:
      return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    }
  }

  static VkAccessFlags2 accessMaskForLayout(VkImageLayout layout, bool source) {
    switch (layout) {
    case VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL:
      return source ? VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
                    : VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
      return VK_ACCESS_2_TRANSFER_WRITE_BIT;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
      return VK_ACCESS_2_TRANSFER_READ_BIT;
    case VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL:
      return VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    default:
      return VK_ACCESS_2_NONE;
    }
  }

  void transition(VkCommandBuffer cmd, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) {
    if (oldLayout == newLayout)
      return;
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = stageMaskForLayout(oldLayout);
    barrier.srcAccessMask = accessMaskForLayout(oldLayout, true);
    barrier.dstStageMask = stageMaskForLayout(newLayout);
    barrier.dstAccessMask = accessMaskForLayout(newLayout, false);
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependency);
  }

  void transition(VkCommandBuffer cmd, Texture &texture, VkImageLayout newLayout) {
    transition(cmd, texture.image, texture.layout, newLayout);
    texture.layout = newLayout;
  }

  void writeDebugScreenshotIfRequested(VkCommandBuffer commandBuffer, VkImage source) {
    if (debugScreenshotWritten_)
      return;
    char const *path = std::getenv("FLUX_DEBUG_SCREENSHOT_PATH");
    if (!path || !*path)
      return;
    Buffer staging{};
    VkDeviceSize size = static_cast<VkDeviceSize>(framebufferWidth_) * framebufferHeight_ * 4u;
    ensureBuffer(staging, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {static_cast<std::uint32_t>(framebufferWidth_),
                        static_cast<std::uint32_t>(framebufferHeight_), 1};
    vkCmdCopyImageToBuffer(commandBuffer, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.buffer, 1, &copy);
    pendingScreenshotPath_ = path;
    pendingScreenshotBuffer_ = staging;
    pendingScreenshotSize_ = size;
    debugScreenshotWritten_ = true;
  }

  void flushScreenshot() {
    if (!pendingScreenshotBuffer_.buffer || pendingScreenshotPath_.empty())
      return;
    vkDeviceWaitIdle(device_);
    void *mapped = nullptr;
    vkCheck(vmaInvalidateAllocation(allocator_, pendingScreenshotBuffer_.allocation, 0, pendingScreenshotSize_),
            "vmaInvalidateAllocation");
    vkCheck(vmaMapMemory(allocator_, pendingScreenshotBuffer_.allocation, &mapped), "vmaMapMemory");
    std::ofstream out(pendingScreenshotPath_, std::ios::binary);
    out << "P6\n"
        << framebufferWidth_ << " " << framebufferHeight_ << "\n255\n";
    auto *bytes = static_cast<std::uint8_t const *>(mapped);
    bool const bgra = surfaceFormat_.format == VK_FORMAT_B8G8R8A8_UNORM ||
                      surfaceFormat_.format == VK_FORMAT_B8G8R8A8_SRGB;
    for (int i = 0; i < framebufferWidth_ * framebufferHeight_; ++i) {
      out.put(static_cast<char>(bytes[i * 4 + (bgra ? 2 : 0)]));
      out.put(static_cast<char>(bytes[i * 4 + 1]));
      out.put(static_cast<char>(bytes[i * 4 + (bgra ? 0 : 2)]));
    }
    vmaUnmapMemory(allocator_, pendingScreenshotBuffer_.allocation);
    destroyBuffer(pendingScreenshotBuffer_);
    pendingScreenshotPath_.clear();
  }

  unsigned int handle_ = 0;
  TextSystem &textSystem_;
  VulkanRenderTargetSpec targetSpec_{};
  int width_ = 1;
  int height_ = 1;
  int framebufferWidth_ = 1;
  int framebufferHeight_ = 1;
  int swapchainTargetWidth_ = 1;
  int swapchainTargetHeight_ = 1;
  float dpiScaleX_ = 1.f;
  float dpiScaleY_ = 1.f;
  Color clearColor_ = Colors::transparent;
  DrawState state_{};
  std::vector<DrawState> stateStack_;
  VulkanFrameRecorder *captureTarget_ = nullptr;
  DrawState captureSavedState_{};
  std::vector<DrawState> captureSavedStack_;
  bool hasCaptureSavedState_ = false;
  std::vector<RectInstance> rects_;
  std::vector<QuadInstance> quads_;
  std::vector<ImageBatch> batches_;
  std::vector<DrawOp> ops_;
  std::vector<VulkanPathVertex> pathVerts_;
  std::unordered_map<PathCacheKey, CachedPath, PathCacheKeyHash> pathCache_;
  std::list<PathCacheKey> pathCacheLru_;
  std::size_t cachedPathVertexCount_ = 0;

  VkInstance instance_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VmaAllocator allocator_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  SharedVulkanCore *shared_ = nullptr;
  std::uint32_t queueFamily_ = 0;
  VkCommandPool commandPool_ = VK_NULL_HANDLE;
  std::array<VkCommandBuffer, kMaxFramesInFlight> commandBuffers_{};
  std::array<VkSemaphore, kMaxFramesInFlight> imageAvailable_{};
  std::array<VkFence, kMaxFramesInFlight> frameFences_{};
  static constexpr std::size_t kNoResetFrameFence = static_cast<std::size_t>(-1);
  std::size_t resetFrameFenceIndex_ = kNoResetFrameFence;
  std::size_t currentFrame_ = 0;
  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkSurfaceFormatKHR surfaceFormat_{};
  VkExtent2D swapExtent_{};
  std::vector<VkImage> swapchainImages_;
  std::vector<VkFence> imageInFlightFences_;
  std::vector<VkImageView> swapchainViews_;
  std::vector<VkSemaphore> imageRenderFinished_;
  Texture backdropSceneTexture_;
  Texture backdropScratchTexture_;
  Texture backdropBlurTexture_;
  VkDescriptorSet rectDescriptorSet_ = VK_NULL_HANDLE;
  VkDescriptorSet quadDescriptorSet_ = VK_NULL_HANDLE;
  Buffer rectBuffer_;
  Buffer quadBuffer_;
  Buffer pathBuffer_;
  Buffer pendingScreenshotBuffer_;
  VkDeviceSize pendingScreenshotSize_ = 0;
  std::string pendingScreenshotPath_;
  bool debugScreenshotWritten_ = false;
  bool swapchainDirty_ = true;
  std::unordered_map<VulkanImage const *, std::unique_ptr<Texture>> imageTextures_;
  std::vector<PendingTextureDestroy> pendingTextureDestroys_;
  bool ownsSharedVulkanCore_ = false;
  bool targetMode_ = false;
};

namespace {

void evictImageTexturesFor(VulkanImage const *image) {
  if (!image)
    return;
  std::lock_guard lock(gCanvasRegistryMutex);
  for (::flux::VulkanCanvas *canvas : gCanvases) {
    if (canvas)
      canvas->evictImageTexture(image);
  }
}

} // namespace

void configureVulkanCanvasRuntime(std::span<char const* const> requiredInstanceExtensions,
                                  std::filesystem::path cacheDir) {
  std::lock_guard lock(gVulkanCoreMutex);
  if (gVulkanCore.instance) {
    throw std::runtime_error("Cannot configure Vulkan instance extensions after instance creation");
  }
  for (char const *extension : requiredInstanceExtensions) {
    appendUniqueExtension(gRequiredInstanceExtensions, extension);
  }
  gPipelineCacheDir = std::move(cacheDir);
}

VkInstance ensureSharedVulkanInstance() {
  return ensureSharedVulkanInstanceImpl();
}

std::unique_ptr<Canvas> createVulkanCanvas(VkSurfaceKHR surface, unsigned int handle, TextSystem &textSystem) {
  return std::make_unique<VulkanCanvas>(surface, handle, textSystem);
}

std::unique_ptr<Canvas> createVulkanRenderTargetCanvas(VulkanRenderTargetSpec const& spec,
                                                       TextSystem& textSystem) {
  return std::make_unique<VulkanCanvas>(spec, textSystem);
}

bool beginRecordedOpsCaptureForCanvas(Canvas *canvas, VulkanFrameRecorder *target) {
  if (!canvas || !target) {
    return false;
  }
  if (auto *vulkan = dynamic_cast<VulkanCanvas *>(canvas)) {
    vulkan->beginRecordedOpsCapture(target);
    return true;
  }
  return false;
}

void endRecordedOpsCaptureForCanvas(Canvas *canvas) {
  if (!canvas) {
    return;
  }
  if (auto *vulkan = dynamic_cast<VulkanCanvas *>(canvas)) {
    vulkan->endRecordedOpsCapture();
  }
}

bool replayRecordedOpsForCanvas(Canvas *canvas, VulkanFrameRecorder const &recorded) {
  if (!canvas) {
    return false;
  }
  if (auto *vulkan = dynamic_cast<VulkanCanvas *>(canvas)) {
    return vulkan->replayRecordedOps(recorded);
  }
  return false;
}

bool replayRecordedLocalOpsForCanvas(Canvas *canvas, VulkanFrameRecorder const &recorded) {
  if (!canvas) {
    return false;
  }
  if (auto *vulkan = dynamic_cast<VulkanCanvas *>(canvas)) {
    return vulkan->replayRecordedLocalOps(recorded);
  }
  return false;
}

std::shared_ptr<Image> Image::fromExternalVulkan(VkImage image, VkImageView view, VkFormat format,
                                                 std::uint32_t width, std::uint32_t height) {
  if (!image || !view || width == 0 || height == 0) {
    return nullptr;
  }
  return std::make_shared<VulkanImage>(image, view, format, width, height);
}

std::shared_ptr<Image> Image::fromDmabuf(DmabufImageSpec const& spec) {
  if (spec.width == 0 || spec.height == 0 || spec.planes.size() != 1) {
    for (DmabufPlane const& plane : spec.planes) {
      if (plane.fd >= 0) close(plane.fd);
    }
    return nullptr;
  }
  DmabufPlane const plane = spec.planes.front();
  int fd = plane.fd;
  if (fd < 0 || plane.stride == 0) {
    if (fd >= 0) close(fd);
    return nullptr;
  }

  VkFormat const format = vkFormatForDrmFormat(spec.drmFormat);
  if (format == VK_FORMAT_UNDEFINED) {
    close(fd);
    return nullptr;
  }

  VkDevice device = VK_NULL_HANDLE;
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  bool acquiredCoreReference = false;
  try {
    SharedVulkanCore* core = acquireSharedVulkanCore(VK_NULL_HANDLE);
    acquiredCoreReference = true;
    device = core->device;

    VkExternalMemoryImageCreateInfo externalInfo{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    externalInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkSubresourceLayout planeLayout{};
    planeLayout.offset = plane.offset;
    planeLayout.size = static_cast<VkDeviceSize>(plane.stride) * spec.height;
    planeLayout.rowPitch = plane.stride;
    planeLayout.arrayPitch = planeLayout.size;
    planeLayout.depthPitch = planeLayout.size;

    VkImageDrmFormatModifierExplicitCreateInfoEXT modifierInfo{
        VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT};
    modifierInfo.pNext = &externalInfo;
    modifierInfo.drmFormatModifier = plane.modifier;
    modifierInfo.drmFormatModifierPlaneCount = 1;
    modifierInfo.pPlaneLayouts = &planeLayout;

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.pNext = &modifierInfo;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {spec.width, spec.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCheck(vkCreateImage(device, &imageInfo, nullptr, &image), "vkCreateImage dmabuf");

    auto getMemoryFdProperties = reinterpret_cast<PFN_vkGetMemoryFdPropertiesKHR>(
        vkGetDeviceProcAddr(device, "vkGetMemoryFdPropertiesKHR"));
    if (!getMemoryFdProperties) {
      throw std::runtime_error("vkGetMemoryFdPropertiesKHR is unavailable");
    }
    VkMemoryFdPropertiesKHR fdProps{VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};
    vkCheck(getMemoryFdProperties(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, fd, &fdProps),
            "vkGetMemoryFdPropertiesKHR");

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, image, &requirements);
    std::uint32_t const memoryTypeBits = requirements.memoryTypeBits & fdProps.memoryTypeBits;

    VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    dedicated.image = image;
    VkImportMemoryFdInfoKHR importInfo{VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR};
    importInfo.pNext = &dedicated;
    importInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    importInfo.fd = fd;

    VkMemoryAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocateInfo.pNext = &importInfo;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = findMemoryType(core->physical, memoryTypeBits, 0);
    vkCheck(vkAllocateMemory(device, &allocateInfo, nullptr, &memory), "vkAllocateMemory dmabuf");
    fd = -1; // Ownership transferred to Vulkan on successful import.

    vkCheck(vkBindImageMemory(device, image, memory, 0), "vkBindImageMemory dmabuf");

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    vkCheck(vkCreateImageView(device, &viewInfo, nullptr, &view), "vkCreateImageView dmabuf");

    auto result = std::make_shared<VulkanImage>(device, image, memory, view, format,
                                                static_cast<int>(spec.width), static_cast<int>(spec.height));
    releaseSharedVulkanCore();
    acquiredCoreReference = false;
    return result;
  } catch (...) {
    if (fd >= 0) close(fd);
    if (view) vkDestroyImageView(device, view, nullptr);
    if (memory) vkFreeMemory(device, memory, nullptr);
    if (image) vkDestroyImage(device, image, nullptr);
    if (acquiredCoreReference) releaseSharedVulkanCore();
    throw;
  }
}

std::shared_ptr<Image> Image::fromRgbaPixels(std::uint32_t width, std::uint32_t height,
                                             std::span<std::uint8_t const> rgbaPixels, void*) {
  std::size_t const expectedSize = static_cast<std::size_t>(width) * height * sizeof(Rgba);
  if (width == 0 || height == 0 || rgbaPixels.size() != expectedSize) {
    return nullptr;
  }

  std::vector<Rgba> pixels(static_cast<std::size_t>(width) * height);
  std::memcpy(pixels.data(), rgbaPixels.data(), expectedSize);
  return std::make_shared<VulkanImage>(static_cast<int>(width), static_cast<int>(height), std::move(pixels));
}

std::shared_ptr<Image> loadImageFromFile(std::string_view path, void *) {
  using WebPGetInfoFn = int (*)(std::uint8_t const *, std::size_t, int *, int *);
  using WebPDecodeRGBAFn = std::uint8_t *(*)(std::uint8_t const *, std::size_t, int *, int *);
  using WebPFreeFn = void (*)(void *);

  std::ifstream in(std::filesystem::path(std::string(path)), std::ios::binary);
  if (!in)
    return nullptr;
  std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (data.empty())
    return nullptr;
  void *lib = dlopen("libwebp.so.7", RTLD_LAZY | RTLD_LOCAL);
  if (!lib)
    lib = dlopen("libwebp.so", RTLD_LAZY | RTLD_LOCAL);
  if (!lib)
    return nullptr;
  auto getInfo = reinterpret_cast<WebPGetInfoFn>(dlsym(lib, "WebPGetInfo"));
  auto decode = reinterpret_cast<WebPDecodeRGBAFn>(dlsym(lib, "WebPDecodeRGBA"));
  auto freeWebP = reinterpret_cast<WebPFreeFn>(dlsym(lib, "WebPFree"));
  if (!getInfo || !decode || !freeWebP) {
    dlclose(lib);
    return nullptr;
  }
  int width = 0, height = 0;
  if (!getInfo(data.data(), data.size(), &width, &height) || width <= 0 || height <= 0) {
    dlclose(lib);
    return nullptr;
  }
  int decodedWidth = 0, decodedHeight = 0;
  std::uint8_t *decoded = decode(data.data(), data.size(), &decodedWidth, &decodedHeight);
  if (!decoded || decodedWidth != width || decodedHeight != height) {
    if (decoded)
      freeWebP(decoded);
    dlclose(lib);
    return nullptr;
  }
  std::vector<Rgba> rgba(static_cast<std::size_t>(width) * height);
  std::memcpy(rgba.data(), decoded, rgba.size() * sizeof(Rgba));
  freeWebP(decoded);
  dlclose(lib);
  return std::make_shared<VulkanImage>(width, height, std::move(rgba));
}

std::shared_ptr<Image> rasterizeToImage(Canvas &canvas, Size logicalSize, RasterizeDrawCallback draw, float dpiScale) {
  auto *vulkan = dynamic_cast<VulkanCanvas *>(&canvas);
  if (!vulkan)
    return nullptr;
  return vulkan->rasterize(logicalSize, draw, dpiScale);
}

namespace detail {

VkInstance vulkanContextInstance() noexcept {
  std::lock_guard lock(gVulkanCoreMutex);
  return gVulkanCore.instance;
}

VkPhysicalDevice vulkanContextPhysicalDevice() noexcept {
  std::lock_guard lock(gVulkanCoreMutex);
  return gVulkanCore.physical;
}

VkDevice vulkanContextDevice() noexcept {
  std::lock_guard lock(gVulkanCoreMutex);
  return gVulkanCore.device;
}

std::uint32_t vulkanContextQueueFamily() noexcept {
  std::lock_guard lock(gVulkanCoreMutex);
  return gVulkanCore.queueFamily;
}

VkQueue vulkanContextQueue() noexcept {
  std::lock_guard lock(gVulkanCoreMutex);
  return gVulkanCore.queue;
}

VmaAllocator vulkanContextAllocator() noexcept {
  std::lock_guard lock(gVulkanCoreMutex);
  return gVulkanCore.allocator;
}

VkFormat vulkanContextPreferredColorFormat() noexcept {
  std::lock_guard lock(gVulkanCoreMutex);
  if (gVulkanCore.resources.renderFormat != VK_FORMAT_UNDEFINED) {
    return gVulkanCore.resources.renderFormat;
  }
  return VK_FORMAT_B8G8R8A8_UNORM;
}

void vulkanContextAddRequiredInstanceExtension(char const *name) {
  std::lock_guard lock(gVulkanCoreMutex);
  if (gVulkanCore.instance) {
    throw std::runtime_error("Cannot add Vulkan instance extension after instance creation");
  }
  appendUniqueExtension(gRequiredInstanceExtensions, name);
}

void vulkanContextAddRequiredDeviceExtension(char const *name) {
  std::lock_guard lock(gVulkanCoreMutex);
  if (gVulkanCore.device) {
    throw std::runtime_error("Cannot add Vulkan device extension after device creation");
  }
  appendUniqueExtension(gRequiredDeviceExtensions, name);
}

void vulkanContextEnsureInitialized() {
  ensureSharedVulkanInstanceImpl();
  static bool holdsReference = false;
  if (!holdsReference) {
    acquireSharedVulkanCore(VK_NULL_HANDLE);
    holdsReference = true;
  }
}

} // namespace detail

} // namespace flux
