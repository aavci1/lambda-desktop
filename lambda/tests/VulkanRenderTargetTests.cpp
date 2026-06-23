#include <doctest/doctest.h>

#include <Lambda/Graphics/Image.hpp>
#include <Lambda/Graphics/RenderTarget.hpp>
#include <Lambda/Graphics/VulkanContext.hpp>
#include <Lambda/SceneGraph/ImageNode.hpp>
#include <Lambda/SceneGraph/PathNode.hpp>
#include <Lambda/SceneGraph/RasterCacheNode.hpp>
#include <Lambda/SceneGraph/RectNode.hpp>
#include <Lambda/SceneGraph/SceneGraph.hpp>
#include <Lambda/SceneGraph/SceneNode.hpp>
#include <Lambda/SceneGraph/SceneRenderer.hpp>
#include <Lambda/SceneGraph/TextNode.hpp>

#if LAMBDAUI_VULKAN

#include "Graphics/Linux/FreeTypeTextSystem.hpp"
#include "Graphics/Vulkan/VulkanCanvas.hpp"
#include "Graphics/Vulkan/VulkanCheck.hpp"
#include "Graphics/Vulkan/VulkanFrameRecorder.hpp"

#include <vulkan/vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace lambdaui;
using namespace lambdaui::scenegraph;

struct ScopedEnvOverride {
  char const* name = nullptr;
  std::string previous;
  bool hadPrevious = false;

  ScopedEnvOverride(char const* envName, char const* value) : name(envName) {
    if (char const* current = std::getenv(name)) {
      previous = current;
      hadPrevious = true;
    }
    setenv(name, value, 1);
  }

  ~ScopedEnvOverride() {
    if (hadPrevious) {
      setenv(name, previous.c_str(), 1);
    } else {
      unsetenv(name);
    }
  }
};

static std::filesystem::path imageFixturePath() {
  std::filesystem::path path = std::filesystem::path(__FILE__).parent_path();
  path /= "../demos/image-demo/test.png";
  return std::filesystem::weakly_canonical(path);
}

int preparedGeometryOverride() {
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
}

bool preparedGeometryExpected(VkPhysicalDevice physical) {
  int const overrideValue = preparedGeometryOverride();
  if (overrideValue < 0) {
    return false;
  }
  if (overrideValue > 0) {
    return true;
  }
  auto driverProps =
      lambdaui::vkStructure<VkPhysicalDeviceDriverProperties>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES);
  auto props2 = lambdaui::vkStructure<VkPhysicalDeviceProperties2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
  props2.pNext = &driverProps;
  vkGetPhysicalDeviceProperties2(physical, &props2);
  return driverProps.driverID != VK_DRIVER_ID_MESA_RADV;
}

void checkPreparedRectState(VulkanFrameRecorder const& recorded, bool expected) {
  if (expected) {
    CHECK(recorded.preparedRectBuffer != VK_NULL_HANDLE);
    CHECK(recorded.preparedRectDescriptor != VK_NULL_HANDLE);
  } else {
    CHECK(recorded.preparedRectBuffer == VK_NULL_HANDLE);
    CHECK(recorded.preparedRectDescriptor == VK_NULL_HANDLE);
  }
}

std::uint32_t findMemoryType(VkPhysicalDevice physical, std::uint32_t typeBits,
                             VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memoryProperties{};
  vkGetPhysicalDeviceMemoryProperties(physical, &memoryProperties);
  for (std::uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
    bool const typeMatches = (typeBits & (1u << i)) != 0;
    bool const propertiesMatch =
        (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties;
    if (typeMatches && propertiesMatch) {
      return i;
    }
  }
  throw std::runtime_error("No compatible Vulkan memory type");
}

struct VulkanImageTarget {
  VkDevice device = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
  std::uint32_t width = 0;
  std::uint32_t height = 0;

  VulkanImageTarget(VkPhysicalDevice physicalDevice, VkDevice logicalDevice,
                    std::uint32_t targetWidth, std::uint32_t targetHeight)
      : device(logicalDevice), physical(physicalDevice), width(targetWidth), height(targetHeight) {
    auto imageInfo = lambdaui::vkStructure<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                      VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCheck(vkCreateImage(device, &imageInfo, nullptr, &image), "vkCreateImage");

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, image, &requirements);

    auto allocateInfo = lambdaui::vkStructure<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex =
        findMemoryType(physical, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkCheck(vkAllocateMemory(device, &allocateInfo, nullptr, &memory), "vkAllocateMemory");
    vkCheck(vkBindImageMemory(device, image, memory, 0), "vkBindImageMemory");

    auto viewInfo = lambdaui::vkStructure<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    vkCheck(vkCreateImageView(device, &viewInfo, nullptr, &view), "vkCreateImageView");
  }

  ~VulkanImageTarget() {
    if (view) {
      vkDestroyImageView(device, view, nullptr);
    }
    if (image) {
      vkDestroyImage(device, image, nullptr);
    }
    if (memory) {
      vkFreeMemory(device, memory, nullptr);
    }
  }
};

struct VulkanReadbackBuffer {
  VkDevice device = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceSize size = 0;

  VulkanReadbackBuffer(VkPhysicalDevice physicalDevice, VkDevice logicalDevice, VkDeviceSize byteSize)
      : device(logicalDevice), physical(physicalDevice), size(byteSize) {
    auto bufferInfo = lambdaui::vkStructure<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCheck(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer), "vkCreateBuffer");

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, buffer, &requirements);

    auto allocateInfo = lambdaui::vkStructure<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = findMemoryType(
        physical, requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkCheck(vkAllocateMemory(device, &allocateInfo, nullptr, &memory), "vkAllocateMemory");
    vkCheck(vkBindBufferMemory(device, buffer, memory, 0), "vkBindBufferMemory");
  }

  ~VulkanReadbackBuffer() {
    if (buffer) {
      vkDestroyBuffer(device, buffer, nullptr);
    }
    if (memory) {
      vkFreeMemory(device, memory, nullptr);
    }
  }
};

struct VulkanCopyContext {
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  VkCommandPool pool = VK_NULL_HANDLE;
  VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;

  VulkanCopyContext(VkDevice logicalDevice, VkQueue renderQueue, std::uint32_t queueFamily)
      : device(logicalDevice), queue(renderQueue) {
    auto poolInfo = lambdaui::vkStructure<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamily;
    vkCheck(vkCreateCommandPool(device, &poolInfo, nullptr, &pool), "vkCreateCommandPool");

    auto allocateInfo =
        lambdaui::vkStructure<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
    allocateInfo.commandPool = pool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    vkCheck(vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer),
            "vkAllocateCommandBuffers");

    auto fenceInfo = lambdaui::vkStructure<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
    vkCheck(vkCreateFence(device, &fenceInfo, nullptr, &fence), "vkCreateFence");
  }

  ~VulkanCopyContext() {
    if (fence) {
      vkDestroyFence(device, fence, nullptr);
    }
    if (pool) {
      vkDestroyCommandPool(device, pool, nullptr);
    }
  }

  void copyImageToBuffer(VkImage image, VkBuffer buffer, std::uint32_t width, std::uint32_t height,
                         VkImageLayout* currentLayout = nullptr) {
    vkCheck(vkResetCommandBuffer(commandBuffer, 0), "vkResetCommandBuffer");
    auto beginInfo = lambdaui::vkStructure<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer");

    if (currentLayout && *currentLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
      auto barrier = lambdaui::vkStructure<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
      barrier.oldLayout = *currentLayout;
      barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
      barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      barrier.image = image;
      barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      barrier.subresourceRange.levelCount = 1;
      barrier.subresourceRange.layerCount = 1;
      barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      vkCmdPipelineBarrier(commandBuffer,
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT,
                           0, 0, nullptr, 0, nullptr, 1, &barrier);
      *currentLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }

    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {width, height, 1};
    vkCmdCopyImageToBuffer(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           buffer, 1, &copy);
    vkCheck(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");

    auto submit = lambdaui::vkStructure<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffer;
    vkCheck(vkResetFences(device, 1, &fence), "vkResetFences");
    vkCheck(vkQueueSubmit(queue, 1, &submit, fence), "vkQueueSubmit");
    vkCheck(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
  }
};

struct HeadlessVulkanTarget {
  VulkanContext& context;
  int logicalW = 1;
  int logicalH = 1;
  float dpiScale = 1.f;
  std::uint32_t pixelW = 1;
  std::uint32_t pixelH = 1;
  VulkanImageTarget targetImage;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;

  HeadlessVulkanTarget(VulkanContext& vk, int width, int height, float scale = 2.f)
      : context(vk), logicalW(width), logicalH(height), dpiScale(scale),
        pixelW(static_cast<std::uint32_t>(std::ceil(static_cast<float>(width) * scale))),
        pixelH(static_cast<std::uint32_t>(std::ceil(static_cast<float>(height) * scale))),
        targetImage(vk.physicalDevice(), vk.device(), pixelW, pixelH) {}

  void render(TextSystem& textSystem, SceneGraph const& graph, Color clearColor,
              VkImageLayout finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
    VulkanRenderTargetSpec spec{
        .image = targetImage.image,
        .view = targetImage.view,
        .format = targetImage.format,
        .width = pixelW,
        .height = pixelH,
        .initialLayout = layout,
        .finalLayout = finalLayout,
    };
    std::unique_ptr<Canvas> canvas = createVulkanRenderTargetCanvas(spec, textSystem);
    if (!canvas) {
      throw std::runtime_error("Failed to create Vulkan render-target canvas");
    }
    canvas->resize(logicalW, logicalH);
    canvas->updateDpiScale(dpiScale, dpiScale);
    canvas->beginFrame();
    canvas->clear(clearColor);
    SceneRenderer renderer{*canvas};
    renderer.render(graph);
    canvas->present();
    layout = finalLayout;
  }

  std::vector<std::uint8_t> readPixels() {
    VulkanReadbackBuffer readback{context.physicalDevice(), context.device(), pixelW * pixelH * 4u};
    VulkanCopyContext copy{context.device(), context.queue(), context.queueFamily()};
    copy.copyImageToBuffer(targetImage.image, readback.buffer, pixelW, pixelH, &layout);

    std::vector<std::uint8_t> pixels(pixelW * pixelH * 4u);
    void* mapped = nullptr;
    vkCheck(vkMapMemory(context.device(), readback.memory, 0, readback.size, 0, &mapped),
            "vkMapMemory");
    std::memcpy(pixels.data(), mapped, pixels.size());
    vkUnmapMemory(context.device(), readback.memory);
    return pixels;
  }
};

static std::uint8_t capturedChannel(std::vector<std::uint8_t> const& pixels, std::uint32_t width,
                                    std::uint32_t x, std::uint32_t y, int channel) {
  std::size_t const idx =
      (static_cast<std::size_t>(y) * width + x) * 4u + static_cast<std::size_t>(channel);
  return pixels[idx];
}

static std::size_t countBrightPixels(std::vector<std::uint8_t> const& pixels, std::uint32_t width,
                                     std::uint32_t x0, std::uint32_t y0,
                                     std::uint32_t x1, std::uint32_t y1,
                                     std::uint8_t threshold) {
  std::size_t count = 0;
  for (std::uint32_t y = y0; y < y1; ++y) {
    for (std::uint32_t x = x0; x < x1; ++x) {
      if (capturedChannel(pixels, width, x, y, 0) >= threshold &&
          capturedChannel(pixels, width, x, y, 1) >= threshold &&
          capturedChannel(pixels, width, x, y, 2) >= threshold) {
        ++count;
      }
    }
  }
  return count;
}

static std::size_t countDarkPixels(std::vector<std::uint8_t> const& pixels, std::uint32_t width,
                                   std::uint32_t x0, std::uint32_t y0,
                                   std::uint32_t x1, std::uint32_t y1,
                                   std::uint8_t threshold) {
  std::size_t count = 0;
  for (std::uint32_t y = y0; y < y1; ++y) {
    for (std::uint32_t x = x0; x < x1; ++x) {
      if (capturedChannel(pixels, width, x, y, 0) <= threshold &&
          capturedChannel(pixels, width, x, y, 1) <= threshold &&
          capturedChannel(pixels, width, x, y, 2) <= threshold) {
        ++count;
      }
    }
  }
  return count;
}

static int colorDelta(std::vector<std::uint8_t> const& pixels, std::uint32_t width,
                      std::uint32_t ax, std::uint32_t ay, std::uint32_t bx, std::uint32_t by) {
  int delta = 0;
  for (int channel = 0; channel < 3; ++channel) {
    delta += std::abs(static_cast<int>(capturedChannel(pixels, width, ax, ay, channel)) -
                      static_cast<int>(capturedChannel(pixels, width, bx, by, channel)));
  }
  return delta;
}

static std::shared_ptr<TextLayout const> makeLabel(FreeTypeTextSystem& textSystem,
                                                   std::string const& text) {
  Font font{};
  font.family = "sans";
  font.size = 13.f;
  font.weight = 400.f;
  return textSystem.layout(text, font, Colors::white, 120.f, {});
}

struct StressScene {
  std::unique_ptr<SceneGraph> graph;
  SceneNode* animatedGroup = nullptr;
};

static StressScene makeStressScene(FreeTypeTextSystem& textSystem, std::shared_ptr<Image> const& image) {
  auto graph = std::make_unique<SceneGraph>();
  auto root = std::make_unique<SceneNode>(lambdaui::Rect{0.f, 0.f, 640.f, 480.f});
  root->appendChild(std::make_unique<RectNode>(
      lambdaui::Rect{0.f, 0.f, 640.f, 480.f},
      FillStyle::solid(Color{0.08f, 0.09f, 0.11f, 1.f})));

  auto animatedGroup = std::make_unique<SceneNode>(lambdaui::Rect{0.f, 0.f, 640.f, 480.f});
  SceneNode* animatedGroupPtr = animatedGroup.get();
  for (int i = 0; i < 256; ++i) {
    animatedGroup->appendChild(std::make_unique<RectNode>(
        lambdaui::Rect{static_cast<float>((i % 32) * 18), static_cast<float>((i / 32) * 18),
                   14.f, 14.f},
        FillStyle::solid(Color{static_cast<float>((17 * i) % 255) / 255.f,
                               static_cast<float>((37 * i) % 255) / 255.f,
                               static_cast<float>((53 * i) % 255) / 255.f, 1.f}),
        StrokeStyle::none(), CornerRadius{3.f, 3.f, 3.f, 3.f}));
  }
  for (int i = 0; i < 64; ++i) {
    animatedGroup->appendChild(std::make_unique<TextNode>(
        lambdaui::Rect{static_cast<float>((i % 8) * 72),
                   170.f + static_cast<float>(i / 8) * 16.f, 64.f, 14.f},
        makeLabel(textSystem, "Row " + std::to_string(i))));
  }
  Path triangle;
  triangle.moveTo({0.f, 0.f});
  triangle.lineTo({100.f, 140.f});
  triangle.lineTo({-80.f, 140.f});
  triangle.close();
  animatedGroup->appendChild(std::make_unique<PathNode>(
      lambdaui::Rect{320.f, 40.f, 180.f, 140.f}, triangle,
      FillStyle::solid(Color{0.2f, 0.6f, 0.9f, 1.f}), StrokeStyle::none(),
      ShadowStyle::none()));
  if (image) {
    std::shared_ptr<Image const> constImage = image;
    for (int i = 0; i < 9; ++i) {
      animatedGroup->appendChild(std::make_unique<ImageNode>(
          lambdaui::Rect{static_cast<float>((i % 3) * 88),
                     320.f + static_cast<float>(i / 3) * 88.f, 72.f, 72.f},
          constImage));
    }
  }
  root->appendChild(std::move(animatedGroup));
  graph->setRoot(std::move(root));
  return StressScene{.graph = std::move(graph), .animatedGroup = animatedGroupPtr};
}

} // namespace

TEST_CASE("VulkanFrameRecorder supports empty lifecycle and move semantics") {
  VulkanFrameRecorder recorded;
  recorded.clear();
  CHECK(recorded.ops.empty());
  CHECK(recorded.rects.empty());
  CHECK(recorded.quads.empty());
  CHECK(recorded.pathVerts.empty());
  CHECK(recorded.preparedRectBuffer == VK_NULL_HANDLE);
  CHECK(recorded.preparedRectDescriptor == VK_NULL_HANDLE);

  recorded.ops.push_back(DrawOp{});
  recorded.rects.push_back(RectInstance{});
  VulkanFrameRecorder moved{std::move(recorded)};
  CHECK(moved.ops.size() == 1);
  CHECK(moved.rects.size() == 1);
  CHECK(recorded.ops.empty());
  CHECK(recorded.rects.empty());

  VulkanFrameRecorder assigned;
  assigned = std::move(moved);
  CHECK(assigned.ops.size() == 1);
  CHECK(assigned.rects.size() == 1);
  CHECK(moved.ops.empty());
  CHECK(moved.rects.empty());
}

TEST_CASE("VulkanFrameRecorder defers prepared GPU resources when clearing") {
  VulkanFrameRecorder recorded;
  recorded.allocator = reinterpret_cast<VmaAllocator>(0x1);
  recorded.device = reinterpret_cast<VkDevice>(0x2);
  recorded.descriptorPool = reinterpret_cast<VkDescriptorPool>(0x3);
  recorded.preparedQuadBuffer = reinterpret_cast<VkBuffer>(0x4);
  recorded.preparedQuadAllocation = reinterpret_cast<VmaAllocation>(0x5);
  recorded.preparedQuadCapacity = 64;
  recorded.preparedQuadDescriptor = reinterpret_cast<VkDescriptorSet>(0x6);
  recorded.preparedRectBuffer = reinterpret_cast<VkBuffer>(0x7);
  recorded.preparedRectAllocation = reinterpret_cast<VmaAllocation>(0x8);
  recorded.preparedRectCapacity = 128;
  recorded.preparedRectDescriptor = reinterpret_cast<VkDescriptorSet>(0x9);
  recorded.preparedPathVertexBuffer = reinterpret_cast<VkBuffer>(0xa);
  recorded.preparedPathVertexAllocation = reinterpret_cast<VmaAllocation>(0xb);
  recorded.preparedPathVertexCapacity = 256;

  VulkanFrameRecorderResources retired{};
  int retireCalls = 0;
  setVulkanFrameRecorderRetireHookForTesting(
      [&retired, &retireCalls](VulkanFrameRecorderResources const& resources) {
        retired = resources;
        ++retireCalls;
        return true;
      });

  recorded.clear();
  setVulkanFrameRecorderRetireHookForTesting({});

  CHECK(retireCalls == 1);
  CHECK(retired.allocator == reinterpret_cast<VmaAllocator>(0x1));
  CHECK(retired.device == reinterpret_cast<VkDevice>(0x2));
  CHECK(retired.descriptorPool == reinterpret_cast<VkDescriptorPool>(0x3));
  CHECK(retired.preparedQuadBuffer == reinterpret_cast<VkBuffer>(0x4));
  CHECK(retired.preparedQuadAllocation == reinterpret_cast<VmaAllocation>(0x5));
  CHECK(retired.preparedQuadDescriptor == reinterpret_cast<VkDescriptorSet>(0x6));
  CHECK(retired.preparedRectBuffer == reinterpret_cast<VkBuffer>(0x7));
  CHECK(retired.preparedRectAllocation == reinterpret_cast<VmaAllocation>(0x8));
  CHECK(retired.preparedRectDescriptor == reinterpret_cast<VkDescriptorSet>(0x9));
  CHECK(retired.preparedPathVertexBuffer == reinterpret_cast<VkBuffer>(0xa));
  CHECK(retired.preparedPathVertexAllocation == reinterpret_cast<VmaAllocation>(0xb));

  CHECK(recorded.preparedQuadBuffer == VK_NULL_HANDLE);
  CHECK(recorded.preparedQuadAllocation == VK_NULL_HANDLE);
  CHECK(recorded.preparedQuadCapacity == 0);
  CHECK(recorded.preparedQuadDescriptor == VK_NULL_HANDLE);
  CHECK(recorded.preparedRectBuffer == VK_NULL_HANDLE);
  CHECK(recorded.preparedRectAllocation == VK_NULL_HANDLE);
  CHECK(recorded.preparedRectCapacity == 0);
  CHECK(recorded.preparedRectDescriptor == VK_NULL_HANDLE);
  CHECK(recorded.preparedPathVertexBuffer == VK_NULL_HANDLE);
  CHECK(recorded.preparedPathVertexAllocation == VK_NULL_HANDLE);
  CHECK(recorded.preparedPathVertexCapacity == 0);
}

TEST_CASE("VulkanFrameRecorder captures and replays canvas ops into a RenderTarget") {
  auto& vk = VulkanContext::instance();
  vk.ensureInitialized();
  bool const expectPreparedGeometry = preparedGeometryExpected(vk.physicalDevice());

  constexpr std::uint32_t width = 64;
  constexpr std::uint32_t height = 64;
  FreeTypeTextSystem textSystem;
  VulkanImageTarget targetImage{vk.physicalDevice(), vk.device(), width, height};
  VulkanRenderTargetSpec spec{
      .image = targetImage.image,
      .view = targetImage.view,
      .format = targetImage.format,
      .width = width,
      .height = height,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
  };
  std::unique_ptr<Canvas> canvas = createVulkanRenderTargetCanvas(spec, textSystem);
  REQUIRE(canvas);
  canvas->resize(static_cast<int>(width), static_cast<int>(height));
  canvas->beginFrame();
  canvas->clear(Colors::black);

  VulkanFrameRecorder recorded;
  REQUIRE(beginRecordedOpsCaptureForCanvas(canvas.get(), &recorded));
  canvas->drawRect(lambdaui::Rect{16.f, 16.f, 32.f, 32.f}, CornerRadius{},
                   FillStyle::solid(Color{1.f, 0.f, 0.f, 1.f}), StrokeStyle::none(), ShadowStyle::none());
  endRecordedOpsCaptureForCanvas(canvas.get());
  CHECK(recorded.ops.size() == 1);
  CHECK(recorded.rects.size() == 1);

  REQUIRE(replayRecordedLocalOpsForCanvas(canvas.get(), recorded));
  checkPreparedRectState(recorded, expectPreparedGeometry);
  canvas->present();

  VulkanReadbackBuffer readback{vk.physicalDevice(), vk.device(), width * height * 4u};
  VulkanCopyContext copy{vk.device(), vk.queue(), vk.queueFamily()};
  copy.copyImageToBuffer(targetImage.image, readback.buffer, width, height);
  std::vector<std::uint8_t> pixels(width * height * 4u);
  void* mapped = nullptr;
  vkCheck(vkMapMemory(vk.device(), readback.memory, 0, readback.size, 0, &mapped), "vkMapMemory");
  std::memcpy(pixels.data(), mapped, pixels.size());
  vkUnmapMemory(vk.device(), readback.memory);

  std::size_t const center = (32u * static_cast<std::size_t>(width) + 32u) * 4u;
  CHECK(pixels[center + 2] > 200);
  CHECK(pixels[center + 1] < 32);
  CHECK(pixels[center + 0] < 32);

  VulkanImageTarget secondTargetImage{vk.physicalDevice(), vk.device(), width, height};
  VulkanRenderTargetSpec secondSpec{
      .image = secondTargetImage.image,
      .view = secondTargetImage.view,
      .format = secondTargetImage.format,
      .width = width,
      .height = height,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
  };
  std::unique_ptr<Canvas> secondCanvas = createVulkanRenderTargetCanvas(secondSpec, textSystem);
  REQUIRE(secondCanvas);
  secondCanvas->resize(static_cast<int>(width), static_cast<int>(height));
  secondCanvas->beginFrame();
  secondCanvas->clear(Colors::black);
  secondCanvas->save();
  secondCanvas->translate(lambdaui::Point{16.f, 0.f});
  REQUIRE(replayRecordedLocalOpsForCanvas(secondCanvas.get(), recorded));
  secondCanvas->restore();
  checkPreparedRectState(recorded, expectPreparedGeometry);
  secondCanvas->present();

  VulkanReadbackBuffer secondReadback{vk.physicalDevice(), vk.device(), width * height * 4u};
  copy.copyImageToBuffer(secondTargetImage.image, secondReadback.buffer, width, height);
  std::vector<std::uint8_t> translatedPixels(width * height * 4u);
  mapped = nullptr;
  vkCheck(vkMapMemory(vk.device(), secondReadback.memory, 0, secondReadback.size, 0, &mapped),
          "vkMapMemory");
  std::memcpy(translatedPixels.data(), mapped, translatedPixels.size());
  vkUnmapMemory(vk.device(), secondReadback.memory);

  std::size_t const shiftedCenter = (32u * static_cast<std::size_t>(width) + 48u) * 4u;
  CHECK(translatedPixels[shiftedCenter + 2] > 200);
  CHECK(translatedPixels[shiftedCenter + 1] < 32);
  CHECK(translatedPixels[shiftedCenter + 0] < 32);

  std::size_t const oldLeftEdge = (32u * static_cast<std::size_t>(width) + 20u) * 4u;
  CHECK(translatedPixels[oldLeftEdge + 2] < 32);
  CHECK(translatedPixels[oldLeftEdge + 1] < 32);
  CHECK(translatedPixels[oldLeftEdge + 0] < 32);

  for (int i = 0; i < 4; ++i) {
    recorded.rects[0].fill0[i] = i == 1 || i == 3 ? 1.f : 0.f;
    recorded.rects[0].fill1[i] = recorded.rects[0].fill0[i];
    recorded.rects[0].fill2[i] = recorded.rects[0].fill0[i];
    recorded.rects[0].fill3[i] = recorded.rects[0].fill0[i];
  }

  VulkanImageTarget mutatedTargetImage{vk.physicalDevice(), vk.device(), width, height};
  VulkanRenderTargetSpec mutatedSpec{
      .image = mutatedTargetImage.image,
      .view = mutatedTargetImage.view,
      .format = mutatedTargetImage.format,
      .width = width,
      .height = height,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
  };
  std::unique_ptr<Canvas> mutatedCanvas = createVulkanRenderTargetCanvas(mutatedSpec, textSystem);
  REQUIRE(mutatedCanvas);
  mutatedCanvas->resize(static_cast<int>(width), static_cast<int>(height));
  mutatedCanvas->beginFrame();
  mutatedCanvas->clear(Colors::black);
  REQUIRE(replayRecordedLocalOpsForCanvas(mutatedCanvas.get(), recorded));
  checkPreparedRectState(recorded, expectPreparedGeometry);
  mutatedCanvas->present();

  VulkanReadbackBuffer mutatedReadback{vk.physicalDevice(), vk.device(), width * height * 4u};
  copy.copyImageToBuffer(mutatedTargetImage.image, mutatedReadback.buffer, width, height);
  std::vector<std::uint8_t> mutatedPixels(width * height * 4u);
  mapped = nullptr;
  vkCheck(vkMapMemory(vk.device(), mutatedReadback.memory, 0, mutatedReadback.size, 0, &mapped),
          "vkMapMemory");
  std::memcpy(mutatedPixels.data(), mapped, mutatedPixels.size());
  vkUnmapMemory(vk.device(), mutatedReadback.memory);

  CHECK(mutatedPixels[center + 1] > 200);
  CHECK(mutatedPixels[center + 2] < 32);
  CHECK(mutatedPixels[center + 0] < 32);
}

TEST_CASE("VulkanFrameRecorder local replay preserves translated non-root clip") {
  auto& vk = VulkanContext::instance();
  vk.ensureInitialized();

  constexpr std::uint32_t width = 64;
  constexpr std::uint32_t height = 64;
  FreeTypeTextSystem textSystem;
  VulkanImageTarget targetImage{vk.physicalDevice(), vk.device(), width, height};
  VulkanRenderTargetSpec spec{
      .image = targetImage.image,
      .view = targetImage.view,
      .format = targetImage.format,
      .width = width,
      .height = height,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
  };
  std::unique_ptr<Canvas> canvas = createVulkanRenderTargetCanvas(spec, textSystem);
  REQUIRE(canvas);
  canvas->resize(static_cast<int>(width), static_cast<int>(height));
  canvas->beginFrame();
  canvas->clear(Colors::black);

  VulkanFrameRecorder recorded;
  REQUIRE(beginRecordedOpsCaptureForCanvas(canvas.get(), &recorded));
  canvas->save();
  canvas->clipRect(Rect::sharp(0.f, 0.f, 24.f, 64.f));
  canvas->drawRect(Rect::sharp(16.f, 16.f, 32.f, 32.f),
                   CornerRadius{},
                   FillStyle::solid(Color{1.f, 0.f, 0.f, 1.f}),
                   StrokeStyle::none(),
                   ShadowStyle::none());
  canvas->restore();
  endRecordedOpsCaptureForCanvas(canvas.get());
  REQUIRE(recorded.ops.size() == 1);
  CHECK(recorded.ops[0].clip.width == doctest::Approx(24.f));
  CHECK(recorded.rootClip.width == doctest::Approx(64.f));
  REQUIRE(prepareRecordedOpsForCanvas(canvas.get(), &recorded));

  canvas->save();
  canvas->translate(Point{16.f, 0.f});
  REQUIRE(replayRecordedLocalOpsForCanvas(canvas.get(), recorded));
  canvas->restore();
  canvas->present();

  VulkanReadbackBuffer readback{vk.physicalDevice(), vk.device(), width * height * 4u};
  VulkanCopyContext copy{vk.device(), vk.queue(), vk.queueFamily()};
  copy.copyImageToBuffer(targetImage.image, readback.buffer, width, height);
  std::vector<std::uint8_t> pixels(width * height * 4u);
  void* mapped = nullptr;
  vkCheck(vkMapMemory(vk.device(), readback.memory, 0, readback.size, 0, &mapped), "vkMapMemory");
  std::memcpy(pixels.data(), mapped, pixels.size());
  vkUnmapMemory(vk.device(), readback.memory);

  CHECK(capturedChannel(pixels, width, 36, 32, 2) > 200);
  CHECK(capturedChannel(pixels, width, 50, 32, 2) < 32);
}

TEST_CASE("VulkanFrameRecorder replays captured image texture") {
  auto& vk = VulkanContext::instance();
  vk.ensureInitialized();

  constexpr std::uint32_t width = 64;
  constexpr std::uint32_t height = 64;
  FreeTypeTextSystem textSystem;
  VulkanImageTarget targetImage{vk.physicalDevice(), vk.device(), width, height};
  VulkanRenderTargetSpec spec{
      .image = targetImage.image,
      .view = targetImage.view,
      .format = targetImage.format,
      .width = width,
      .height = height,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
  };
  std::unique_ptr<Canvas> canvas = createVulkanRenderTargetCanvas(spec, textSystem);
  REQUIRE(canvas);
  std::vector<std::uint8_t> rgba(16u * 16u * 4u);
  for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
    rgba[i + 0] = 255;
    rgba[i + 1] = 0;
    rgba[i + 2] = 0;
    rgba[i + 3] = 255;
  }
  std::shared_ptr<Image> image = Image::fromRgbaPixels(16, 16, rgba, canvas->gpuDevice());
  REQUIRE(image);

  canvas->resize(static_cast<int>(width), static_cast<int>(height));
  canvas->beginFrame();
  canvas->clear(Colors::black);

  VulkanFrameRecorder recorded;
  REQUIRE(beginRecordedOpsCaptureForCanvas(canvas.get(), &recorded));
  Size const imageSize = image->size();
  canvas->drawImage(*image,
                    lambdaui::Rect{0.f, 0.f, imageSize.width, imageSize.height},
                    lambdaui::Rect{8.f, 8.f, 24.f, 24.f},
                    CornerRadius{},
                    1.f);
  endRecordedOpsCaptureForCanvas(canvas.get());

  CHECK(recorded.replayable);
  CHECK(recorded.ops.size() == 1);
  CHECK(prepareRecordedOpsForCanvas(canvas.get(), &recorded));
  bool const expectPreparedGeometry = preparedGeometryExpected(vk.physicalDevice());
  if (expectPreparedGeometry) {
    CHECK(recorded.preparedQuadBuffer != VK_NULL_HANDLE);
    CHECK(recorded.preparedQuadDescriptor != VK_NULL_HANDLE);
  } else {
    CHECK(recorded.preparedQuadBuffer == VK_NULL_HANDLE);
    CHECK(recorded.preparedQuadDescriptor == VK_NULL_HANDLE);
  }
  REQUIRE(replayRecordedLocalOpsForCanvas(canvas.get(), recorded));
  canvas->present();

  VulkanReadbackBuffer readback{vk.physicalDevice(), vk.device(), width * height * 4u};
  VulkanCopyContext copy{vk.device(), vk.queue(), vk.queueFamily()};
  copy.copyImageToBuffer(targetImage.image, readback.buffer, width, height);
  std::vector<std::uint8_t> pixels(width * height * 4u);
  void* mapped = nullptr;
  vkCheck(vkMapMemory(vk.device(), readback.memory, 0, readback.size, 0, &mapped), "vkMapMemory");
  std::memcpy(pixels.data(), mapped, pixels.size());
  vkUnmapMemory(vk.device(), readback.memory);

  std::size_t const center = (20u * static_cast<std::size_t>(width) + 20u) * 4u;
  CHECK(pixels[center + 2] > 200);
  CHECK(pixels[center + 1] < 32);
  CHECK(pixels[center + 0] < 32);
}

TEST_CASE("Vulkan image draw keeps pending upload after same-frame source destruction") {
  auto& vk = VulkanContext::instance();
  vk.ensureInitialized();

  constexpr std::uint32_t width = 64;
  constexpr std::uint32_t height = 64;
  FreeTypeTextSystem textSystem;
  VulkanImageTarget targetImage{vk.physicalDevice(), vk.device(), width, height};
  VulkanRenderTargetSpec spec{
      .image = targetImage.image,
      .view = targetImage.view,
      .format = targetImage.format,
      .width = width,
      .height = height,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
  };
  std::unique_ptr<Canvas> canvas = createVulkanRenderTargetCanvas(spec, textSystem);
  REQUIRE(canvas);

  std::vector<std::uint8_t> rgba(16u * 16u * 4u);
  for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
    rgba[i + 0] = 255;
    rgba[i + 1] = 0;
    rgba[i + 2] = 0;
    rgba[i + 3] = 255;
  }
  std::shared_ptr<Image> image = Image::fromRgbaPixels(16, 16, rgba, canvas->gpuDevice());
  REQUIRE(image);

  canvas->resize(static_cast<int>(width), static_cast<int>(height));
  canvas->beginFrame();
  canvas->clear(Colors::black);
  Size const imageSize = image->size();
  canvas->drawImage(*image,
                    lambdaui::Rect{0.f, 0.f, imageSize.width, imageSize.height},
                    lambdaui::Rect{8.f, 8.f, 24.f, 24.f},
                    CornerRadius{},
                    1.f);
  image.reset();
  canvas->present();

  VulkanReadbackBuffer readback{vk.physicalDevice(), vk.device(), width * height * 4u};
  VulkanCopyContext copy{vk.device(), vk.queue(), vk.queueFamily()};
  copy.copyImageToBuffer(targetImage.image, readback.buffer, width, height);
  std::vector<std::uint8_t> pixels(width * height * 4u);
  void* mapped = nullptr;
  vkCheck(vkMapMemory(vk.device(), readback.memory, 0, readback.size, 0, &mapped), "vkMapMemory");
  std::memcpy(pixels.data(), mapped, pixels.size());
  vkUnmapMemory(vk.device(), readback.memory);

  std::size_t const center = (20u * static_cast<std::size_t>(width) + 20u) * 4u;
  CHECK(pixels[center + 2] > 200);
  CHECK(pixels[center + 1] < 32);
  CHECK(pixels[center + 0] < 32);
}

TEST_CASE("VulkanFrameRecorder replays captured image after recording canvas is destroyed") {
  auto& vk = VulkanContext::instance();
  vk.ensureInitialized();

  constexpr std::uint32_t width = 64;
  constexpr std::uint32_t height = 64;
  FreeTypeTextSystem textSystem;
  VulkanFrameRecorder recorded;
  std::shared_ptr<Image> image;

  {
    VulkanImageTarget sourceTargetImage{vk.physicalDevice(), vk.device(), width, height};
    VulkanRenderTargetSpec sourceSpec{
        .image = sourceTargetImage.image,
        .view = sourceTargetImage.view,
        .format = sourceTargetImage.format,
        .width = width,
        .height = height,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };
    std::unique_ptr<Canvas> sourceCanvas = createVulkanRenderTargetCanvas(sourceSpec, textSystem);
    REQUIRE(sourceCanvas);

    std::vector<std::uint8_t> rgba(16u * 16u * 4u);
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
      rgba[i + 0] = 0;
      rgba[i + 1] = 255;
      rgba[i + 2] = 0;
      rgba[i + 3] = 255;
    }
    image = Image::fromRgbaPixels(16, 16, rgba, sourceCanvas->gpuDevice());
    REQUIRE(image);

    sourceCanvas->resize(static_cast<int>(width), static_cast<int>(height));
    sourceCanvas->beginFrame();
    sourceCanvas->clear(Colors::black);
    REQUIRE(beginRecordedOpsCaptureForCanvas(sourceCanvas.get(), &recorded));
    Size const imageSize = image->size();
    sourceCanvas->drawImage(*image,
                            lambdaui::Rect{0.f, 0.f, imageSize.width, imageSize.height},
                            lambdaui::Rect{8.f, 8.f, 24.f, 24.f},
                            CornerRadius{},
                            1.f);
    endRecordedOpsCaptureForCanvas(sourceCanvas.get());
    sourceCanvas->present();
  }

  REQUIRE(image);
  CHECK(recorded.replayable);
  REQUIRE(recorded.ops.size() == 1);
  CHECK(recorded.ops[0].sourceImage != nullptr);
  CHECK(recorded.ops[0].sourceImageRef != nullptr);
  image.reset();
  CHECK(!image);

  VulkanImageTarget replayTargetImage{vk.physicalDevice(), vk.device(), width, height};
  VulkanRenderTargetSpec replaySpec{
      .image = replayTargetImage.image,
      .view = replayTargetImage.view,
      .format = replayTargetImage.format,
      .width = width,
      .height = height,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
  };
  std::unique_ptr<Canvas> replayCanvas = createVulkanRenderTargetCanvas(replaySpec, textSystem);
  REQUIRE(replayCanvas);
  replayCanvas->resize(static_cast<int>(width), static_cast<int>(height));
  replayCanvas->beginFrame();
  replayCanvas->clear(Colors::black);
  REQUIRE(replayRecordedLocalOpsForCanvas(replayCanvas.get(), recorded));
  replayCanvas->present();

  VulkanReadbackBuffer readback{vk.physicalDevice(), vk.device(), width * height * 4u};
  VulkanCopyContext copy{vk.device(), vk.queue(), vk.queueFamily()};
  copy.copyImageToBuffer(replayTargetImage.image, readback.buffer, width, height);
  std::vector<std::uint8_t> pixels(width * height * 4u);
  void* mapped = nullptr;
  vkCheck(vkMapMemory(vk.device(), readback.memory, 0, readback.size, 0, &mapped), "vkMapMemory");
  std::memcpy(pixels.data(), mapped, pixels.size());
  vkUnmapMemory(vk.device(), readback.memory);

  std::size_t const center = (20u * static_cast<std::size_t>(width) + 20u) * 4u;
  CHECK(pixels[center + 1] > 200);
  CHECK(pixels[center + 2] < 32);
  CHECK(pixels[center + 0] < 32);
}

TEST_CASE("Vulkan RenderTarget renders canvas ops into an offscreen image") {
  auto& vk = VulkanContext::instance();
  vk.ensureInitialized();

  VkPhysicalDevice physical = vk.physicalDevice();
  VkDevice device = vk.device();
  REQUIRE(physical != VK_NULL_HANDLE);
  REQUIRE(device != VK_NULL_HANDLE);

  constexpr std::uint32_t width = 64;
  constexpr std::uint32_t height = 64;
  VulkanImageTarget targetImage{physical, device, width, height};
  std::shared_ptr<Image> externalImage =
      Image::fromExternalVulkan(targetImage.image, targetImage.view, targetImage.format,
                                width, height);
  REQUIRE(externalImage);
  CHECK(externalImage->size().width == static_cast<float>(width));
  CHECK(externalImage->size().height == static_cast<float>(height));

  lambdaui::RenderTarget target{lambdaui::VulkanRenderTargetSpec{
      .image = targetImage.image,
      .view = targetImage.view,
      .format = targetImage.format,
      .width = width,
      .height = height,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
  }};

  target.beginFrame();
  target.canvas().clear(lambdaui::Colors::black);
  auto root = std::make_unique<SceneNode>(lambdaui::Rect{0.f, 0.f, 64.f, 64.f});
  root->appendChild(std::make_unique<RectNode>(
      lambdaui::Rect{16.f, 16.f, 32.f, 32.f},
      lambdaui::FillStyle::solid(lambdaui::Color{1.f, 0.f, 0.f, 1.f})));
  SceneGraph graph{std::move(root)};
  target.renderScene(graph);
  target.endFrame();

  VulkanReadbackBuffer readback{physical, device, width * height * 4u};
  VulkanCopyContext copy{device, vk.queue(), vk.queueFamily()};
  copy.copyImageToBuffer(targetImage.image, readback.buffer, width, height);

  std::vector<std::uint8_t> pixels(width * height * 4u);
  void* mapped = nullptr;
  vkCheck(vkMapMemory(device, readback.memory, 0, readback.size, 0, &mapped), "vkMapMemory");
  std::memcpy(pixels.data(), mapped, pixels.size());
  vkUnmapMemory(device, readback.memory);

  std::size_t const center = (32u * static_cast<std::size_t>(width) + 32u) * 4u;
  CHECK(pixels[center + 2] > 200);
  CHECK(pixels[center + 1] < 32);
  CHECK(pixels[center + 0] < 32);
}

TEST_CASE("Vulkan RenderTarget applies backdrop blur to previously rendered pixels") {
  auto& vk = VulkanContext::instance();
  vk.ensureInitialized();

  FreeTypeTextSystem textSystem;
  constexpr std::uint32_t width = 128;
  constexpr std::uint32_t height = 64;
  VulkanImageTarget targetImage{vk.physicalDevice(), vk.device(), width, height};
  std::unique_ptr<Canvas> canvas = createVulkanRenderTargetCanvas(lambdaui::VulkanRenderTargetSpec{
      .image = targetImage.image,
      .view = targetImage.view,
      .format = targetImage.format,
      .width = width,
      .height = height,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
  },
                                                                  textSystem);
  REQUIRE(canvas);

  canvas->resize(static_cast<int>(width), static_cast<int>(height));
  canvas->updateDpiScale(1.f, 1.f);
  canvas->beginFrame();
  canvas->clear(Colors::black);
  canvas->drawRect(Rect::sharp(0.f, 0.f, 64.f, 64.f),
                   CornerRadius{},
                   FillStyle::solid(Colors::black),
                   StrokeStyle::none());
  canvas->drawRect(Rect::sharp(64.f, 0.f, 64.f, 64.f),
                   CornerRadius{},
                   FillStyle::solid(Colors::white),
                   StrokeStyle::none());
  canvas->drawBackdropBlur(Rect::sharp(16.f, 0.f, 96.f, 64.f), 18.f, Colors::transparent);
  canvas->present();

  VulkanReadbackBuffer readback{vk.physicalDevice(), vk.device(), width * height * 4u};
  VulkanCopyContext copy{vk.device(), vk.queue(), vk.queueFamily()};
  copy.copyImageToBuffer(targetImage.image, readback.buffer, width, height);

  std::vector<std::uint8_t> pixels(width * height * 4u);
  void* mapped = nullptr;
  vkCheck(vkMapMemory(vk.device(), readback.memory, 0, readback.size, 0, &mapped), "vkMapMemory");
  std::memcpy(pixels.data(), mapped, pixels.size());
  vkUnmapMemory(vk.device(), readback.memory);

  CHECK(capturedChannel(pixels, width, 60, 32, 0) > 20);
  CHECK(capturedChannel(pixels, width, 60, 32, 0) < 235);
  CHECK(capturedChannel(pixels, width, 68, 32, 0) > 20);
  CHECK(capturedChannel(pixels, width, 68, 32, 0) < 235);
}

TEST_CASE("Vulkan RenderTarget renders multiple stress frames") {
  auto& vk = VulkanContext::instance();
  vk.ensureInitialized();

  FreeTypeTextSystem textSystem;
  HeadlessVulkanTarget target{vk, 640, 480};
  std::shared_ptr<Image> image = loadImage(imageFixturePath().string(), vk.device());
  StressScene scene = makeStressScene(textSystem, image);
  REQUIRE(scene.animatedGroup != nullptr);

  for (int frame = 0; frame < 18; ++frame) {
    scene.animatedGroup->setPosition(lambdaui::Point{0.f, static_cast<float>(frame % 3)});
    target.render(textSystem, *scene.graph, Color{0.08f, 0.09f, 0.11f, 1.f});
  }

  CHECK(true);
}

TEST_CASE("Vulkan RenderTarget renders glyph atlas text") {
  auto& vk = VulkanContext::instance();
  vk.ensureInitialized();

  FreeTypeTextSystem textSystem;
  HeadlessVulkanTarget target{vk, 640, 480};
  Font font{};
  font.family = "sans";
  font.size = 32.f;
  font.weight = 500.f;
  auto layout = textSystem.layout("Cached glyphs", font, Colors::white, 320.f, {});

  auto root = std::make_unique<SceneNode>(lambdaui::Rect{0.f, 0.f, 640.f, 480.f});
  root->appendChild(std::make_unique<RectNode>(
      lambdaui::Rect{0.f, 0.f, 640.f, 480.f}, FillStyle::solid(Colors::black)));
  root->appendChild(std::make_unique<TextNode>(lambdaui::Rect{20.f, 20.f, 320.f, 48.f}, layout));
  SceneGraph graph{std::move(root)};
  target.render(textSystem, graph, Colors::black);
  std::vector<std::uint8_t> pixels = target.readPixels();

  std::size_t brightPixels = 0;
  for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
    if (pixels[i + 0] > 120 && pixels[i + 1] > 120 && pixels[i + 2] > 120) {
      ++brightPixels;
    }
  }
  CHECK(brightPixels > 20);
}

TEST_CASE("Vulkan prepared text replay uses current clip after scroll translation") {
  ScopedEnvOverride forcePreparedGeometry{"LAMBDA_VULKAN_PREPARED_GEOMETRY", "1"};
  auto& vk = VulkanContext::instance();
  vk.ensureInitialized();

  FreeTypeTextSystem textSystem;
  HeadlessVulkanTarget target{vk, 220, 160, 1.f};
  Font font{};
  font.family = "sans";
  font.size = 20.f;
  font.weight = 500.f;
  TextLayoutOptions options{};
  options.lineHeight = 20.f;
  auto layout = textSystem.layout("\n\n\n\n\n\n\n\n\nVisible after scroll",
                                  font, Colors::white, 160.f, options);
  REQUIRE(layout->runs.size() > 0);
  REQUIRE(layout->lines.size() > 9);

  auto root = std::make_unique<SceneNode>(lambdaui::Rect{0.f, 0.f, 220.f, 160.f});
  root->appendChild(std::make_unique<RectNode>(
      lambdaui::Rect{0.f, 0.f, 220.f, 160.f}, FillStyle::solid(Colors::black)));

  auto viewport = std::make_unique<RectNode>(
      lambdaui::Rect{20.f, 20.f, 160.f, 80.f}, FillStyle::none());
  viewport->setClipsContents(true);

  auto content = std::make_unique<SceneNode>(lambdaui::Rect{0.f, -230.f, 160.f, 360.f});
  content->appendChild(std::make_unique<TextNode>(
      lambdaui::Rect{0.f, 0.f, 160.f, 340.f}, layout));
  viewport->appendChild(std::move(content));
  root->appendChild(std::move(viewport));

  SceneGraph graph{std::move(root)};
  target.render(textSystem, graph, Colors::black);
  std::vector<std::uint8_t> pixels = target.readPixels();

  std::size_t const brightPixels = countBrightPixels(pixels, target.pixelW, 20, 20, 180, 100, 140);
  CHECK(brightPixels > 20);
}

TEST_CASE("Vulkan RenderTarget applies rounded clip masks to child content") {
  auto& vk = VulkanContext::instance();
  vk.ensureInitialized();

  FreeTypeTextSystem textSystem;
  HeadlessVulkanTarget target{vk, 640, 480};
  auto root = std::make_unique<SceneNode>(lambdaui::Rect{0.f, 0.f, 640.f, 480.f});
  root->appendChild(std::make_unique<RectNode>(
      lambdaui::Rect{0.f, 0.f, 640.f, 480.f}, FillStyle::solid(Colors::white)));
  auto clip = std::make_unique<RectNode>(
      lambdaui::Rect{20.f, 20.f, 80.f, 20.f}, FillStyle::none(), StrokeStyle::none(),
      CornerRadius::pill(lambdaui::Rect::sharp(0.f, 0.f, 80.f, 20.f)));
  clip->setClipsContents(true);
  clip->appendChild(std::make_unique<RectNode>(
      lambdaui::Rect{0.f, 0.f, 80.f, 20.f}, FillStyle::solid(Colors::red)));
  root->appendChild(std::move(clip));
  SceneGraph graph{std::move(root)};
  target.render(textSystem, graph, Colors::white);
  std::vector<std::uint8_t> pixels = target.readPixels();
  REQUIRE(!pixels.empty());

  CHECK(capturedChannel(pixels, target.pixelW, 42, 42, 0) >= 240);
  CHECK(capturedChannel(pixels, target.pixelW, 42, 42, 1) >= 240);
  CHECK(capturedChannel(pixels, target.pixelW, 42, 42, 2) >= 240);
  CHECK(capturedChannel(pixels, target.pixelW, 120, 60, 2) >= 180);
  CHECK(capturedChannel(pixels, target.pixelW, 120, 60, 1) <= 80);
  CHECK(capturedChannel(pixels, target.pixelW, 120, 60, 0) <= 80);
}

TEST_CASE("Vulkan RenderTarget preserves nested rounded clip masks") {
  auto& vk = VulkanContext::instance();
  vk.ensureInitialized();

  FreeTypeTextSystem textSystem;
  HeadlessVulkanTarget target{vk, 240, 120, 1.f};
  auto root = std::make_unique<SceneNode>(lambdaui::Rect{0.f, 0.f, 240.f, 120.f});
  root->appendChild(std::make_unique<RectNode>(
      lambdaui::Rect{0.f, 0.f, 240.f, 120.f}, FillStyle::solid(Colors::white)));

  auto outerFirst = std::make_unique<RectNode>(
      lambdaui::Rect{20.f, 20.f, 80.f, 80.f}, FillStyle::none(), StrokeStyle::none(),
      CornerRadius{36.f, 36.f, 36.f, 36.f});
  outerFirst->setClipsContents(true);
  auto innerFirst = std::make_unique<RectNode>(
      lambdaui::Rect{0.f, 0.f, 80.f, 80.f}, FillStyle::none(), StrokeStyle::none(),
      CornerRadius{4.f, 4.f, 4.f, 4.f});
  innerFirst->setClipsContents(true);
  innerFirst->appendChild(std::make_unique<RectNode>(
      lambdaui::Rect{0.f, 0.f, 80.f, 80.f}, FillStyle::solid(Colors::red)));
  outerFirst->appendChild(std::move(innerFirst));
  root->appendChild(std::move(outerFirst));

  auto innerFirstRestrictive = std::make_unique<RectNode>(
      lambdaui::Rect{130.f, 20.f, 80.f, 80.f}, FillStyle::none(), StrokeStyle::none(),
      CornerRadius{4.f, 4.f, 4.f, 4.f});
  innerFirstRestrictive->setClipsContents(true);
  auto innerSecond = std::make_unique<RectNode>(
      lambdaui::Rect{0.f, 0.f, 80.f, 80.f}, FillStyle::none(), StrokeStyle::none(),
      CornerRadius{36.f, 36.f, 36.f, 36.f});
  innerSecond->setClipsContents(true);
  innerSecond->appendChild(std::make_unique<RectNode>(
      lambdaui::Rect{0.f, 0.f, 80.f, 80.f}, FillStyle::solid(Colors::red)));
  innerFirstRestrictive->appendChild(std::move(innerSecond));
  root->appendChild(std::move(innerFirstRestrictive));

  SceneGraph graph{std::move(root)};
  target.render(textSystem, graph, Colors::white);
  std::vector<std::uint8_t> pixels = target.readPixels();
  REQUIRE(!pixels.empty());

  CHECK(capturedChannel(pixels, target.pixelW, 25, 25, 0) >= 240);
  CHECK(capturedChannel(pixels, target.pixelW, 25, 25, 1) >= 240);
  CHECK(capturedChannel(pixels, target.pixelW, 25, 25, 2) >= 240);
  CHECK(capturedChannel(pixels, target.pixelW, 60, 60, 2) >= 220);

  CHECK(capturedChannel(pixels, target.pixelW, 135, 25, 0) >= 240);
  CHECK(capturedChannel(pixels, target.pixelW, 135, 25, 1) >= 240);
  CHECK(capturedChannel(pixels, target.pixelW, 135, 25, 2) >= 240);
  CHECK(capturedChannel(pixels, target.pixelW, 170, 60, 2) >= 220);
}

TEST_CASE("Vulkan RenderTarget masks image quads with rounded clips") {
  auto& vk = VulkanContext::instance();
  vk.ensureInitialized();

  FreeTypeTextSystem textSystem;
  HeadlessVulkanTarget target{vk, 140, 120, 1.f};
  std::vector<std::uint8_t> rgba(80u * 80u * 4u);
  for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
    rgba[i + 0] = 255;
    rgba[i + 1] = 0;
    rgba[i + 2] = 0;
    rgba[i + 3] = 255;
  }
  std::shared_ptr<Image> image = Image::fromRgbaPixels(80, 80, rgba, vk.device());
  REQUIRE(image);

  auto root = std::make_unique<SceneNode>(lambdaui::Rect{0.f, 0.f, 140.f, 120.f});
  root->appendChild(std::make_unique<RectNode>(
      lambdaui::Rect{0.f, 0.f, 140.f, 120.f}, FillStyle::solid(Colors::white)));
  auto clip = std::make_unique<RectNode>(
      lambdaui::Rect{20.f, 20.f, 80.f, 80.f}, FillStyle::none(), StrokeStyle::none(),
      CornerRadius{36.f, 36.f, 36.f, 36.f});
  clip->setClipsContents(true);
  clip->appendChild(std::make_unique<ImageNode>(
      lambdaui::Rect{0.f, 0.f, 80.f, 80.f}, image, ImageFillMode::Stretch));
  root->appendChild(std::move(clip));

  SceneGraph graph{std::move(root)};
  target.render(textSystem, graph, Colors::white);
  std::vector<std::uint8_t> pixels = target.readPixels();
  REQUIRE(!pixels.empty());

  CHECK(capturedChannel(pixels, target.pixelW, 25, 25, 0) >= 240);
  CHECK(capturedChannel(pixels, target.pixelW, 25, 25, 1) >= 240);
  CHECK(capturedChannel(pixels, target.pixelW, 25, 25, 2) >= 240);
  CHECK(capturedChannel(pixels, target.pixelW, 60, 60, 2) >= 220);
  CHECK(capturedChannel(pixels, target.pixelW, 60, 60, 1) <= 80);
  CHECK(capturedChannel(pixels, target.pixelW, 60, 60, 0) <= 80);
}

TEST_CASE("Vulkan RenderTarget masks path rendering with rounded clips") {
  auto& vk = VulkanContext::instance();
  vk.ensureInitialized();

  FreeTypeTextSystem textSystem;
  HeadlessVulkanTarget target{vk, 140, 120, 1.f};
  Path path;
  path.rect(lambdaui::Rect{0.f, 0.f, 80.f, 80.f}, CornerRadius{});

  auto root = std::make_unique<SceneNode>(lambdaui::Rect{0.f, 0.f, 140.f, 120.f});
  root->appendChild(std::make_unique<RectNode>(
      lambdaui::Rect{0.f, 0.f, 140.f, 120.f}, FillStyle::solid(Colors::white)));
  auto clip = std::make_unique<RectNode>(
      lambdaui::Rect{20.f, 20.f, 80.f, 80.f}, FillStyle::none(), StrokeStyle::none(),
      CornerRadius{36.f, 36.f, 36.f, 36.f});
  clip->setClipsContents(true);
  clip->appendChild(std::make_unique<PathNode>(
      lambdaui::Rect{0.f, 0.f, 80.f, 80.f}, path, FillStyle::solid(Colors::red)));
  root->appendChild(std::move(clip));

  SceneGraph graph{std::move(root)};
  target.render(textSystem, graph, Colors::white);
  std::vector<std::uint8_t> pixels = target.readPixels();
  REQUIRE(!pixels.empty());

  CHECK(capturedChannel(pixels, target.pixelW, 25, 25, 0) >= 240);
  CHECK(capturedChannel(pixels, target.pixelW, 25, 25, 1) >= 240);
  CHECK(capturedChannel(pixels, target.pixelW, 25, 25, 2) >= 240);
  CHECK(capturedChannel(pixels, target.pixelW, 60, 60, 2) >= 220);
  CHECK(capturedChannel(pixels, target.pixelW, 60, 60, 1) <= 80);
  CHECK(capturedChannel(pixels, target.pixelW, 60, 60, 0) <= 80);
}

TEST_CASE("Vulkan RenderTarget masks glyph quads with rounded clips") {
  auto& vk = VulkanContext::instance();
  vk.ensureInitialized();

  FreeTypeTextSystem textSystem;
  HeadlessVulkanTarget target{vk, 180, 120, 1.f};
  Font font{};
  font.family = "sans";
  font.size = 88.f;
  font.weight = 700.f;
  auto layout = textSystem.layout("MMMM", font, Colors::black, 220.f, {});

  auto root = std::make_unique<SceneNode>(lambdaui::Rect{0.f, 0.f, 180.f, 120.f});
  root->appendChild(std::make_unique<RectNode>(
      lambdaui::Rect{0.f, 0.f, 180.f, 120.f}, FillStyle::solid(Colors::white)));
  auto clip = std::make_unique<RectNode>(
      lambdaui::Rect{20.f, 20.f, 120.f, 80.f}, FillStyle::none(), StrokeStyle::none(),
      CornerRadius{38.f, 38.f, 38.f, 38.f});
  clip->setClipsContents(true);
  clip->appendChild(std::make_unique<TextNode>(
      lambdaui::Rect{-12.f, -8.f, 220.f, 100.f}, layout));
  root->appendChild(std::move(clip));

  SceneGraph graph{std::move(root)};
  target.render(textSystem, graph, Colors::white);
  std::vector<std::uint8_t> pixels = target.readPixels();
  REQUIRE(!pixels.empty());

  CHECK(countDarkPixels(pixels, target.pixelW, 20, 20, 34, 34, 80) == 0);
  CHECK(countDarkPixels(pixels, target.pixelW, 46, 44, 128, 90, 120) > 20);
}

TEST_CASE("Vulkan RenderTarget shades linear gradient rect fills") {
  auto& vk = VulkanContext::instance();
  vk.ensureInitialized();

  FreeTypeTextSystem textSystem;
  HeadlessVulkanTarget target{vk, 640, 480};
  auto root = std::make_unique<SceneNode>(lambdaui::Rect{0.f, 0.f, 640.f, 480.f});
  root->appendChild(std::make_unique<RectNode>(
      lambdaui::Rect{0.f, 0.f, 640.f, 480.f}, FillStyle::solid(Colors::black)));
  root->appendChild(std::make_unique<RectNode>(
      lambdaui::Rect{20.f, 20.f, 100.f, 40.f},
      FillStyle::linearGradient(Colors::red, Colors::blue, lambdaui::Point{0.f, 0.f},
                                lambdaui::Point{1.f, 0.f})));
  SceneGraph graph{std::move(root)};
  target.render(textSystem, graph, Colors::black);
  std::vector<std::uint8_t> pixels = target.readPixels();
  REQUIRE(!pixels.empty());

  CHECK(capturedChannel(pixels, target.pixelW, 60, 60, 2) >
        capturedChannel(pixels, target.pixelW, 60, 60, 0) + 80);
  CHECK(capturedChannel(pixels, target.pixelW, 220, 60, 0) >
        capturedChannel(pixels, target.pixelW, 220, 60, 2) + 80);
}

TEST_CASE("Vulkan RenderTarget shades radial and conical gradient rect fills") {
  auto& vk = VulkanContext::instance();
  vk.ensureInitialized();

  FreeTypeTextSystem textSystem;
  HeadlessVulkanTarget target{vk, 640, 480};
  auto root = std::make_unique<SceneNode>(lambdaui::Rect{0.f, 0.f, 640.f, 480.f});
  root->appendChild(std::make_unique<RectNode>(
      lambdaui::Rect{0.f, 0.f, 640.f, 480.f}, FillStyle::solid(Colors::black)));
  root->appendChild(std::make_unique<RectNode>(
      lambdaui::Rect{20.f, 80.f, 100.f, 100.f},
      FillStyle::radialGradient(Colors::white, Colors::black, lambdaui::Point{0.5f, 0.5f}, 0.5f)));
  root->appendChild(std::make_unique<RectNode>(
      lambdaui::Rect{150.f, 80.f, 100.f, 100.f},
      FillStyle::conicalGradient({
          GradientStop{0.00f, Colors::red},
          GradientStop{0.33f, Colors::green},
          GradientStop{0.66f, Colors::blue},
          GradientStop{1.00f, Colors::red},
      })));
  SceneGraph graph{std::move(root)};
  target.render(textSystem, graph, Colors::black);
  std::vector<std::uint8_t> pixels = target.readPixels();
  REQUIRE(!pixels.empty());

  CHECK(capturedChannel(pixels, target.pixelW, 140, 260, 0) >
        capturedChannel(pixels, target.pixelW, 50, 260, 0) + 80);
  CHECK(capturedChannel(pixels, target.pixelW, 140, 260, 1) >
        capturedChannel(pixels, target.pixelW, 50, 260, 1) + 80);
  CHECK(capturedChannel(pixels, target.pixelW, 140, 260, 2) >
        capturedChannel(pixels, target.pixelW, 50, 260, 2) + 80);
  CHECK(colorDelta(pixels, target.pixelW, 490, 260, 310, 260) > 120);
}

TEST_CASE("Vulkan RenderTarget preserves rounded rect geometry when clipped by the viewport") {
  auto& vk = VulkanContext::instance();
  vk.ensureInitialized();

  FreeTypeTextSystem textSystem;
  HeadlessVulkanTarget target{vk, 640, 480};
  auto root = std::make_unique<SceneNode>(lambdaui::Rect{0.f, 0.f, 640.f, 480.f});
  root->appendChild(std::make_unique<RectNode>(
      lambdaui::Rect{0.f, 0.f, 640.f, 480.f}, FillStyle::solid(Colors::white)));
  auto clip = std::make_unique<RectNode>(
      lambdaui::Rect{20.f, 30.f, 140.f, 120.f}, FillStyle::none(), StrokeStyle::none(),
      CornerRadius{});
  clip->setClipsContents(true);
  clip->appendChild(std::make_unique<RectNode>(
      lambdaui::Rect{0.f, -10.f, 100.f, 80.f}, FillStyle::solid(Colors::red),
      StrokeStyle::none(), CornerRadius{28.f, 28.f, 28.f, 28.f}));
  root->appendChild(std::move(clip));
  SceneGraph graph{std::move(root)};
  target.render(textSystem, graph, Colors::white);
  std::vector<std::uint8_t> pixels = target.readPixels();
  REQUIRE(!pixels.empty());

  CHECK(capturedChannel(pixels, target.pixelW, 42, 68, 0) >= 240);
  CHECK(capturedChannel(pixels, target.pixelW, 42, 68, 1) >= 240);
  CHECK(capturedChannel(pixels, target.pixelW, 42, 68, 2) >= 240);
  CHECK(capturedChannel(pixels, target.pixelW, 56, 76, 2) >= 180);
  CHECK(capturedChannel(pixels, target.pixelW, 56, 76, 1) <= 80);
  CHECK(capturedChannel(pixels, target.pixelW, 56, 76, 0) <= 80);
}

TEST_CASE("Vulkan frame capture preserves transformed and rounded clip geometry") {
  auto& vk = VulkanContext::instance();
  vk.ensureInitialized();

  FreeTypeTextSystem textSystem;
  HeadlessVulkanTarget target{vk, 240, 160, 1.f};
  VulkanRenderTargetSpec spec{
      .image = target.targetImage.image,
      .view = target.targetImage.view,
      .format = target.targetImage.format,
      .width = target.pixelW,
      .height = target.pixelH,
      .initialLayout = target.layout,
      .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
  };
  std::unique_ptr<Canvas> canvas = createVulkanRenderTargetCanvas(spec, textSystem);
  REQUIRE(canvas);

  canvas->resize(target.logicalW, target.logicalH);
  canvas->updateDpiScale(target.dpiScale, target.dpiScale);
  canvas->beginFrame();
  canvas->clear(Colors::white);

  canvas->save();
  canvas->translate(72.f, 68.f);
  canvas->rotate(0.32f);
  canvas->clipRect(Rect::sharp(-48.f, -30.f, 96.f, 60.f), CornerRadius{14.f, 14.f, 14.f, 14.f});
  canvas->drawRect(Rect::sharp(-66.f, -44.f, 132.f, 88.f),
                   CornerRadius{},
                   FillStyle::linearGradient(Colors::red, Colors::blue, Point{0.f, 0.f}, Point{1.f, 0.f}),
                   StrokeStyle::none());
  canvas->restore();

  canvas->clipRect(Rect::sharp(148.f, 38.f, 66.f, 58.f), CornerRadius{20.f, 20.f, 20.f, 20.f});
  canvas->drawRect(Rect::sharp(132.f, 24.f, 104.f, 86.f),
                   CornerRadius{},
                   FillStyle::linearGradient(Colors::green, Colors::blue, Point{0.f, 0.f}, Point{1.f, 0.f}),
                   StrokeStyle::none());

  REQUIRE(requestNextFrameCaptureForCanvas(canvas.get()));
  canvas->present();
  target.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  std::vector<std::uint8_t> pixels;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  REQUIRE(takeCapturedFrameForCanvas(canvas.get(), pixels, width, height));
  CHECK(width == target.pixelW);
  CHECK(height == target.pixelH);
  REQUIRE(pixels.size() == static_cast<std::size_t>(width) * height * 4u);

  CHECK(colorDelta(pixels, width, 72, 68, 8, 8) > 140);
  CHECK(capturedChannel(pixels, width, 8, 8, 0) >= 240);
  CHECK(capturedChannel(pixels, width, 8, 8, 1) >= 240);
  CHECK(capturedChannel(pixels, width, 8, 8, 2) >= 240);
  CHECK(capturedChannel(pixels, width, 151, 41, 0) >= 240);
  CHECK(capturedChannel(pixels, width, 151, 41, 1) >= 240);
  CHECK(capturedChannel(pixels, width, 151, 41, 2) >= 240);
  CHECK(colorDelta(pixels, width, 174, 66, 8, 8) > 120);
}

TEST_CASE("Vulkan external render-target frame capture waits for submitted completion fence") {
  auto& vk = VulkanContext::instance();
  vk.ensureInitialized();

  FreeTypeTextSystem textSystem;
  HeadlessVulkanTarget target{vk, 96, 72, 1.f};
  VulkanCopyContext external{vk.device(), vk.queue(), vk.queueFamily()};

  vkCheck(vkResetCommandBuffer(external.commandBuffer, 0), "vkResetCommandBuffer external render target");
  auto beginInfo = lambdaui::vkStructure<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkCheck(vkBeginCommandBuffer(external.commandBuffer, &beginInfo), "vkBeginCommandBuffer external render target");

  VulkanRenderTargetSpec spec{
      .image = target.targetImage.image,
      .view = target.targetImage.view,
      .format = target.targetImage.format,
      .width = target.pixelW,
      .height = target.pixelH,
      .initialLayout = target.layout,
      .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .commandBuffer = external.commandBuffer,
      .completionFence = external.fence,
  };
  std::unique_ptr<Canvas> canvas = createVulkanRenderTargetCanvas(spec, textSystem);
  REQUIRE(canvas);

  canvas->resize(target.logicalW, target.logicalH);
  canvas->updateDpiScale(target.dpiScale, target.dpiScale);
  canvas->beginFrame();
  canvas->clear(Colors::white);
  canvas->drawRect(Rect::sharp(20.f, 18.f, 38.f, 28.f),
                   CornerRadius{},
                   FillStyle::solid(Colors::red),
                   StrokeStyle::none());

  REQUIRE(requestNextFrameCaptureForCanvas(canvas.get()));
  canvas->present();
  target.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  std::vector<std::uint8_t> pixels;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  CHECK_FALSE(takeCapturedFrameForCanvas(canvas.get(), pixels, width, height));

  vkCheck(vkEndCommandBuffer(external.commandBuffer), "vkEndCommandBuffer external render target");
  auto submit = lambdaui::vkStructure<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &external.commandBuffer;
  vkCheck(vkResetFences(vk.device(), 1, &external.fence), "vkResetFences external render target");
  vkCheck(vkQueueSubmit(vk.queue(), 1, &submit, external.fence), "vkQueueSubmit external render target");
  REQUIRE(markVulkanRenderTargetCanvasSubmitted(canvas.get()));
  vkCheck(vkWaitForFences(vk.device(), 1, &external.fence, VK_TRUE, UINT64_MAX),
          "vkWaitForFences external render target");

  REQUIRE(takeCapturedFrameForCanvas(canvas.get(), pixels, width, height));
  CHECK(width == target.pixelW);
  CHECK(height == target.pixelH);
  REQUIRE(pixels.size() == static_cast<std::size_t>(width) * height * 4u);
  CHECK(capturedChannel(pixels, width, 28, 24, 2) >= 240);
  CHECK(capturedChannel(pixels, width, 28, 24, 2) >
        capturedChannel(pixels, width, 28, 24, 1) + 140);
  CHECK(capturedChannel(pixels, width, 28, 24, 2) >
        capturedChannel(pixels, width, 28, 24, 0) + 160);
  CHECK(capturedChannel(pixels, width, 4, 4, 0) >= 240);
  CHECK(capturedChannel(pixels, width, 4, 4, 1) >= 240);
  CHECK(capturedChannel(pixels, width, 4, 4, 2) >= 240);
}

TEST_CASE("Vulkan RenderTarget preserves image sampling when clipped by the viewport") {
  auto& vk = VulkanContext::instance();
  vk.ensureInitialized();

  FreeTypeTextSystem textSystem;
  HeadlessVulkanTarget source{vk, 120, 160, 1.f};
  auto sourceRoot = std::make_unique<SceneNode>(lambdaui::Rect{0.f, 0.f, 120.f, 160.f});
  sourceRoot->appendChild(std::make_unique<RectNode>(
      lambdaui::Rect{0.f, 0.f, 120.f, 160.f},
      FillStyle::linearGradient(Colors::red, Colors::blue, lambdaui::Point{0.f, 0.f},
                                lambdaui::Point{1.f, 1.f})));
  SceneGraph sourceGraph{std::move(sourceRoot)};
  source.render(textSystem, sourceGraph, Colors::transparent, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
  std::shared_ptr<Image> image = Image::fromExternalVulkan(
      source.targetImage.image, source.targetImage.view, source.targetImage.format,
      source.pixelW, source.pixelH);
  REQUIRE(image);

  HeadlessVulkanTarget target{vk, 640, 480};
  auto root = std::make_unique<SceneNode>(lambdaui::Rect{0.f, 0.f, 640.f, 480.f});
  root->appendChild(std::make_unique<RectNode>(
      lambdaui::Rect{0.f, 0.f, 640.f, 480.f}, FillStyle::solid(Colors::white)));
  root->appendChild(std::make_unique<ImageNode>(
      lambdaui::Rect{20.f, 20.f, 120.f, 160.f}, image, ImageFillMode::Stretch));
  auto clip = std::make_unique<RectNode>(
      lambdaui::Rect{180.f, 40.f, 120.f, 140.f}, FillStyle::none(), StrokeStyle::none(),
      CornerRadius{});
  clip->setClipsContents(true);
  clip->appendChild(std::make_unique<ImageNode>(
      lambdaui::Rect{0.f, -20.f, 120.f, 160.f}, image, ImageFillMode::Stretch));
  root->appendChild(std::move(clip));
  SceneGraph graph{std::move(root)};
  target.render(textSystem, graph, Colors::white);
  std::vector<std::uint8_t> pixels = target.readPixels();
  REQUIRE(!pixels.empty());

  CHECK(colorDelta(pixels, target.pixelW, 100, 120, 420, 120) <= 24);
}

TEST_CASE("SceneRenderer rasterizes RasterCacheNode into a reusable Vulkan image") {
  auto& vk = VulkanContext::instance();
  vk.ensureInitialized();

  FreeTypeTextSystem textSystem;
  HeadlessVulkanTarget target{vk, 640, 480};
  auto root = std::make_unique<SceneNode>(lambdaui::Rect{0.f, 0.f, 160.f, 120.f});
  auto raster = std::make_unique<RasterCacheNode>(lambdaui::Rect{20.f, 24.f, 80.f, 40.f});
  RasterCacheNode* rasterNode = raster.get();
  raster->setSubtree(std::make_unique<RectNode>(
      lambdaui::Rect{0.f, 0.f, 80.f, 40.f}, FillStyle::solid(Colors::red)));
  root->appendChild(std::move(raster));
  SceneGraph graph{std::move(root)};

  target.render(textSystem, graph, Colors::black);
  std::shared_ptr<Image> firstCache = rasterNode->cachedImage();
  REQUIRE(firstCache);
  CHECK(firstCache->size() == lambdaui::Size{160.f, 80.f});

  target.render(textSystem, graph, Colors::black);
  CHECK(rasterNode->cachedImage() == firstCache);
}

#endif // LAMBDAUI_VULKAN
