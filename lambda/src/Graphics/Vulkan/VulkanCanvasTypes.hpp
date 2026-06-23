#pragma once

#include <Lambda/Core/Geometry.hpp>

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <memory>

struct VmaAllocation_T;
using VmaAllocation = VmaAllocation_T *;

namespace lambdaui {

class Image;

inline constexpr std::size_t kVulkanRoundedClipMaskCapacity = 4;
inline constexpr std::size_t kVulkanRoundedClipEntryCount = kVulkanRoundedClipMaskCapacity * 2;

struct Texture {
  VkImage image = VK_NULL_HANDLE;
  VmaAllocation allocation = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkDescriptorSet descriptor = VK_NULL_HANDLE;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
  int width = 0;
  int height = 0;
  std::uint64_t contentGeneration = 1;
  bool ownsImage = true;
  bool ownsView = true;
};

struct RectInstance {
  float rect[4]{};
  float axisX[4]{};
  float axisY[4]{};
  float radii[4]{};
  float fill0[4]{};
  float fill1[4]{};
  float fill2[4]{};
  float fill3[4]{};
  float stops[4]{};
  float gradient[4]{};
  float stroke[4]{};
  float params[4]{};
  float clipHeader[4]{};
  float clipEntries[kVulkanRoundedClipEntryCount][4]{};
};

struct QuadInstance {
  float rect[4]{};
  float axisX[4]{};
  float axisY[4]{};
  float uv[4]{};
  float color[4]{};
  float radii[4]{};
  float clipHeader[4]{};
  float clipEntries[kVulkanRoundedClipEntryCount][4]{};
};

struct CalloutInstance {
  float rect[4]{};
  float axisX[4]{};
  float axisY[4]{};
  float card[4]{};
  float radii[4]{};
  float base[4]{};
  float tint[4]{};
  float stroke[4]{};
  float params[4]{};
  float clipHeader[4]{};
  float clipEntries[kVulkanRoundedClipEntryCount][4]{};
};

struct DrawOp {
  enum class Kind : std::uint8_t { Rect,
                                   Path,
                                   Image,
                                   BackdropBlur,
                                   Callout };
  Kind kind = Kind::Rect;
  Texture *texture = nullptr;
  std::shared_ptr<Image const> sourceImageRef;
  void const *sourceImage = nullptr;
  std::uint32_t first = 0;
  std::uint32_t count = 0;
  Rect clip{};
  float blurRadius = 0.f;
  Rect blurCacheClip{};
  bool hasBlurCacheClip = false;
  VkDescriptorSet externalStorageDescriptor = VK_NULL_HANDLE;
  VkBuffer externalVertexBuffer = VK_NULL_HANDLE;
  float externalTranslationX = 0.f;
  float externalTranslationY = 0.f;
  bool premultipliedAlpha = false;
  std::uint64_t geometrySignature = 0;
};

struct VulkanPathVertex {
  float x = 0.f;
  float y = 0.f;
  float color[4]{};
  float viewport[2]{};
  float local[2]{};
  float fill0[4]{};
  float fill1[4]{};
  float fill2[4]{};
  float fill3[4]{};
  float stops[4]{};
  float gradient[4]{};
  float params[4]{};
  float clipHeader[4]{};
  float clipEntries[kVulkanRoundedClipEntryCount][4]{};
};

} // namespace lambdaui
