#include <doctest/doctest.h>

#include <Lambda/Graphics/Image.hpp>
#include <Lambda/Graphics/RenderTarget.hpp>
#include <Lambda/Graphics/VulkanContext.hpp>

#if LAMBDAUI_VULKAN

#include "Compositor/Surface/CommittedSurfacePainter.hpp"
#include "Graphics/Linux/FreeTypeTextSystem.hpp"
#include "Graphics/Vulkan/VulkanCanvas.hpp"
#include "Graphics/Vulkan/VulkanCheck.hpp"

#include <vulkan/vulkan.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using namespace lambdaui;

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

  void copyImageToBuffer(VkImage image, VkBuffer buffer, std::uint32_t width, std::uint32_t height) {
    vkCheck(vkResetCommandBuffer(commandBuffer, 0), "vkResetCommandBuffer");
    auto beginInfo = lambdaui::vkStructure<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer");

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

static std::uint8_t capturedChannel(std::vector<std::uint8_t> const& pixels, std::uint32_t width,
                                    std::uint32_t x, std::uint32_t y, int channel) {
  std::size_t const idx =
      (static_cast<std::size_t>(y) * width + x) * 4u + static_cast<std::size_t>(channel);
  return pixels[idx];
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

static std::vector<std::uint8_t> readPixels(VulkanContext& vk,
                                            VulkanImageTarget const& targetImage,
                                            std::uint32_t width,
                                            std::uint32_t height) {
  VulkanReadbackBuffer readback{vk.physicalDevice(), vk.device(), width * height * 4u};
  VulkanCopyContext copy{vk.device(), vk.queue(), vk.queueFamily()};
  copy.copyImageToBuffer(targetImage.image, readback.buffer, width, height);

  std::vector<std::uint8_t> pixels(width * height * 4u);
  void* mapped = nullptr;
  vkCheck(vkMapMemory(vk.device(), readback.memory, 0, readback.size, 0, &mapped), "vkMapMemory");
  std::memcpy(pixels.data(), mapped, pixels.size());
  vkUnmapMemory(vk.device(), readback.memory);
  return pixels;
}

} // namespace

TEST_CASE("Compositor glass material does not fade client content") {
  auto& vk = VulkanContext::instance();
  vk.ensureInitialized();

  FreeTypeTextSystem textSystem;
  constexpr std::uint32_t width = 128;
  constexpr std::uint32_t height = 96;
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

  constexpr int clientWidth = 48;
  constexpr int clientHeight = 32;
  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(clientWidth) * clientHeight * 4u);
  for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
    rgba[i + 0] = 0;
    rgba[i + 1] = 255;
    rgba[i + 2] = 0;
    rgba[i + 3] = 255;
  }
  std::shared_ptr<Image> clientImage =
      Image::fromRgbaPixels(clientWidth, clientHeight, rgba, canvas->gpuDevice());
  REQUIRE(clientImage);

  lambdaui::compositor::ChromeConfig chrome{};

  lambdaui::compositor::CommittedSurfaceSnapshot surface{
      .id = 1,
      .x = 32,
      .y = 42,
      .width = clientWidth,
      .height = clientHeight,
      .bufferWidth = clientWidth,
      .bufferHeight = clientHeight,
      .sourceX = 0.f,
      .sourceY = 0.f,
      .sourceWidth = static_cast<float>(clientWidth),
      .sourceHeight = static_cast<float>(clientHeight),
      .destinationWidth = clientWidth,
      .destinationHeight = clientHeight,
      .titleBarHeight = chrome.titleBarHeight,
      .title = "Opaque client",
      .serverSideDecorated = true,
      .focused = true,
      .backgroundEffect = lambdaui::compositor::SurfaceBackgroundEffectSnapshot{
          .blurRadius = 18.f,
          .baseColor = Color{1.f, 1.f, 1.f, 0.04f},
          .tint = Color{1.f, 1.f, 1.f, 0.18f},
          .borderColor = Colors::transparent,
      },
      .serial = 1,
      .backgroundBlurRects = {lambdaui::compositor::CommittedSurfaceSnapshot::RegionRect{
          .x = 0,
          .y = 0,
          .width = clientWidth,
          .height = clientHeight,
      }},
  };
  lambdaui::compositor::SurfaceVisualState visual{};

  canvas->resize(static_cast<int>(width), static_cast<int>(height));
  canvas->updateDpiScale(1.f, 1.f);
  canvas->beginFrame();
  canvas->clear(Colors::black);
  canvas->drawRect(Rect::sharp(0.f, 0.f, static_cast<float>(width), static_cast<float>(height)),
                   CornerRadius{},
                   FillStyle::linearGradient(Colors::red, Colors::blue, Point{0.f, 0.f}, Point{1.f, 1.f}),
                   StrokeStyle::none());
  lambdaui::compositor::drawCommittedSurfaceSnapshot(*canvas,
                                                   textSystem,
                                                   surface,
                                                   visual,
                                                   *clientImage,
                                                   std::chrono::steady_clock::now(),
                                                   chrome,
                                                   false);
  canvas->present();

  std::vector<std::uint8_t> pixels = readPixels(vk, targetImage, width, height);

  CHECK(capturedChannel(pixels, width, 56, 58, 1) > 230);
  CHECK(capturedChannel(pixels, width, 56, 58, 0) < 32);
  CHECK(capturedChannel(pixels, width, 56, 58, 2) < 32);
}

TEST_CASE("Compositor per-surface glass tints transparent window background") {
  auto& vk = VulkanContext::instance();
  vk.ensureInitialized();

  FreeTypeTextSystem textSystem;
  constexpr std::uint32_t width = 96;
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

  constexpr int clientWidth = 48;
  constexpr int clientHeight = 32;
  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(clientWidth) * clientHeight * 4u, 0u);
  std::shared_ptr<Image> clientImage =
      Image::fromRgbaPixels(clientWidth, clientHeight, rgba, canvas->gpuDevice());
  REQUIRE(clientImage);

  lambdaui::compositor::ChromeConfig chrome{};

  lambdaui::compositor::CommittedSurfaceSnapshot surface{
      .id = 1,
      .x = 24,
      .y = 16,
      .width = clientWidth,
      .height = clientHeight,
      .bufferWidth = clientWidth,
      .bufferHeight = clientHeight,
      .sourceX = 0.f,
      .sourceY = 0.f,
      .sourceWidth = static_cast<float>(clientWidth),
      .sourceHeight = static_cast<float>(clientHeight),
      .destinationWidth = clientWidth,
      .destinationHeight = clientHeight,
      .focused = true,
      .backgroundEffect = lambdaui::compositor::SurfaceBackgroundEffectSnapshot{
          .blurRadius = 18.f,
          .tint = Color{0.f, 0.f, 0.f, 0.5f},
          .borderColor = Color{1.f, 1.f, 1.f, 0.f},
      },
      .serial = 1,
      .backgroundBlurRects = {lambdaui::compositor::CommittedSurfaceSnapshot::RegionRect{
          .x = 0,
          .y = 0,
          .width = clientWidth,
          .height = clientHeight,
      }},
  };
  lambdaui::compositor::SurfaceVisualState visual{};

  canvas->resize(static_cast<int>(width), static_cast<int>(height));
  canvas->updateDpiScale(1.f, 1.f);
  canvas->beginFrame();
  canvas->clear(Colors::white);
  lambdaui::compositor::drawCommittedSurfaceSnapshot(*canvas,
                                                   textSystem,
                                                   surface,
                                                   visual,
                                                   *clientImage,
                                                   std::chrono::steady_clock::now(),
                                                   chrome,
                                                   false);
  canvas->present();

  std::vector<std::uint8_t> pixels = readPixels(vk, targetImage, width, height);

  CHECK(capturedChannel(pixels, width, 40, 32, 0) < 180);
  CHECK(capturedChannel(pixels, width, 40, 32, 1) < 180);
  CHECK(capturedChannel(pixels, width, 40, 32, 2) < 180);
}

TEST_CASE("Compositor system chrome material stays independent from client glass material") {
  auto& vk = VulkanContext::instance();
  vk.ensureInitialized();

  FreeTypeTextSystem textSystem;
  constexpr std::uint32_t width = 320;
  constexpr std::uint32_t height = 96;
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

  constexpr int clientWidth = 120;
  constexpr int clientHeight = 40;
  std::vector<std::uint8_t> transparent(static_cast<std::size_t>(clientWidth) * clientHeight * 4u, 0u);
  std::shared_ptr<Image> clientImage =
      Image::fromRgbaPixels(clientWidth, clientHeight, transparent, canvas->gpuDevice());
  REQUIRE(clientImage);

  lambdaui::compositor::ChromeConfig chrome{};
  chrome.windowBorderColor = Colors::transparent;
  chrome.borderLineColor = Colors::transparent;
  chrome.focusedShadowColor = Colors::transparent;
  chrome.unfocusedShadowColor = Colors::transparent;

  lambdaui::compositor::CommittedSurfaceSnapshot systemTitlebar{
      .id = 1,
      .x = 16,
      .y = 40,
      .width = clientWidth,
      .height = clientHeight,
      .bufferWidth = clientWidth,
      .bufferHeight = clientHeight,
      .sourceX = 0.f,
      .sourceY = 0.f,
      .sourceWidth = static_cast<float>(clientWidth),
      .sourceHeight = static_cast<float>(clientHeight),
      .destinationWidth = clientWidth,
      .destinationHeight = clientHeight,
      .titleBarHeight = chrome.titleBarHeight,
      .serverSideDecorated = true,
      .focused = true,
      .backgroundEffect = lambdaui::compositor::SurfaceBackgroundEffectSnapshot{
          .blurRadius = 18.f,
          .baseColor = Color{1.f, 1.f, 1.f, 0.42f},
          .tint = Color{0.78f, 0.90f, 1.f, 0.34f},
          .borderColor = Colors::transparent,
      },
      .serial = 1,
      .backgroundBlurRects = {lambdaui::compositor::CommittedSurfaceSnapshot::RegionRect{
          .x = 0,
          .y = 0,
          .width = clientWidth,
          .height = clientHeight,
      }},
  };

  lambdaui::compositor::CommittedSurfaceSnapshot integratedTitlebar = systemTitlebar;
  integratedTitlebar.id = 2;
  integratedTitlebar.x = 180;
  integratedTitlebar.y = 28;
  integratedTitlebar.titleBarHeight = 0;
  integratedTitlebar.cutoutsBound = true;
  integratedTitlebar.serial = 2;

  lambdaui::compositor::SurfaceVisualState systemVisual{};
  lambdaui::compositor::SurfaceVisualState integratedVisual{};

  canvas->resize(static_cast<int>(width), static_cast<int>(height));
  canvas->updateDpiScale(1.f, 1.f);
  canvas->beginFrame();
  canvas->clear(Color{0.18f, 0.20f, 0.24f, 1.f});
  lambdaui::compositor::drawCommittedSurfaceSnapshot(*canvas,
                                                   textSystem,
                                                   systemTitlebar,
                                                   systemVisual,
                                                   *clientImage,
                                                   std::chrono::steady_clock::now(),
                                                   chrome,
                                                   false);
  lambdaui::compositor::drawCommittedSurfaceSnapshot(*canvas,
                                                   textSystem,
                                                   integratedTitlebar,
                                                   integratedVisual,
                                                   *clientImage,
                                                   std::chrono::steady_clock::now(),
                                                   chrome,
                                                   false);
  canvas->present();

  std::vector<std::uint8_t> pixels = readPixels(vk, targetImage, width, height);

  CHECK(colorDelta(pixels, width, 44, 30, 44, 58) > 60);
  CHECK(colorDelta(pixels, width, 44, 30, 208, 48) > 60);
  CHECK(colorDelta(pixels, width, 44, 58, 208, 48) <= 6);
}

#endif
