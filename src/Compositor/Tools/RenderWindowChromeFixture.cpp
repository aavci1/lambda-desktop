#include "Compositor/Surface/CommittedSurfacePainter.hpp"

#include "Graphics/Linux/FreeTypeTextSystem.hpp"

#include <Flux/Graphics/Image.hpp>
#include <Flux/Graphics/RenderTarget.hpp>
#include <Flux/Graphics/VulkanContext.hpp>

#include <vulkan/vulkan.h>
#include <zlib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct U8Color {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 255;
};

void vkCheck(VkResult result, char const* label) {
  if (result != VK_SUCCESS) throw std::runtime_error(label);
}

std::uint32_t findMemoryType(VkPhysicalDevice physical,
                             std::uint32_t typeBits,
                             VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memoryProperties{};
  vkGetPhysicalDeviceMemoryProperties(physical, &memoryProperties);
  for (std::uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
    bool const typeMatches = (typeBits & (1u << i)) != 0;
    bool const propertiesMatch = (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties;
    if (typeMatches && propertiesMatch) return i;
  }
  throw std::runtime_error("no compatible Vulkan memory type");
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

  VulkanImageTarget(VkPhysicalDevice physicalDevice,
                    VkDevice logicalDevice,
                    std::uint32_t targetWidth,
                    std::uint32_t targetHeight)
      : device(logicalDevice), physical(physicalDevice), width(targetWidth), height(targetHeight) {
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCheck(vkCreateImage(device, &imageInfo, nullptr, &image), "vkCreateImage");

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, image, &requirements);

    VkMemoryAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex =
        findMemoryType(physical, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkCheck(vkAllocateMemory(device, &allocateInfo, nullptr, &memory), "vkAllocateMemory");
    vkCheck(vkBindImageMemory(device, image, memory, 0), "vkBindImageMemory");

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    vkCheck(vkCreateImageView(device, &viewInfo, nullptr, &view), "vkCreateImageView");
  }

  ~VulkanImageTarget() {
    if (view) vkDestroyImageView(device, view, nullptr);
    if (image) vkDestroyImage(device, image, nullptr);
    if (memory) vkFreeMemory(device, memory, nullptr);
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
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkCheck(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer), "vkCreateBuffer");

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device, buffer, &requirements);

    VkMemoryAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex =
        findMemoryType(physical,
                       requirements.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkCheck(vkAllocateMemory(device, &allocateInfo, nullptr, &memory), "vkAllocateMemory");
    vkCheck(vkBindBufferMemory(device, buffer, memory, 0), "vkBindBufferMemory");
  }

  ~VulkanReadbackBuffer() {
    if (buffer) vkDestroyBuffer(device, buffer, nullptr);
    if (memory) vkFreeMemory(device, memory, nullptr);
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
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamily;
    vkCheck(vkCreateCommandPool(device, &poolInfo, nullptr, &pool), "vkCreateCommandPool");

    VkCommandBufferAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocateInfo.commandPool = pool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    vkCheck(vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer), "vkAllocateCommandBuffers");

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vkCheck(vkCreateFence(device, &fenceInfo, nullptr, &fence), "vkCreateFence");
  }

  ~VulkanCopyContext() {
    if (fence) vkDestroyFence(device, fence, nullptr);
    if (pool) vkDestroyCommandPool(device, pool, nullptr);
  }

  void copyImageToBuffer(VkImage image, VkBuffer buffer, std::uint32_t width, std::uint32_t height) {
    vkCheck(vkResetCommandBuffer(commandBuffer, 0), "vkResetCommandBuffer");
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer");

    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {width, height, 1};
    vkCmdCopyImageToBuffer(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
    vkCheck(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");

    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffer;
    vkCheck(vkResetFences(device, 1, &fence), "vkResetFences");
    vkCheck(vkQueueSubmit(queue, 1, &submit, fence), "vkQueueSubmit");
    vkCheck(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
  }
};

void putPixel(std::vector<std::uint8_t>& pixels, int width, int height, int x, int y, U8Color color) {
  if (x < 0 || y < 0 || x >= width || y >= height) return;
  std::size_t const offset =
      (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u;
  pixels[offset + 0u] = color.r;
  pixels[offset + 1u] = color.g;
  pixels[offset + 2u] = color.b;
  pixels[offset + 3u] = color.a;
}

void fillRect(std::vector<std::uint8_t>& pixels,
              int width,
              int height,
              int left,
              int top,
              int rectWidth,
              int rectHeight,
              U8Color color) {
  int const right = std::min(width, left + rectWidth);
  int const bottom = std::min(height, top + rectHeight);
  for (int y = std::max(0, top); y < bottom; ++y) {
    for (int x = std::max(0, left); x < right; ++x) {
      putPixel(pixels, width, height, x, y, color);
    }
  }
}

std::vector<std::uint8_t> makePixels(int width, int height, U8Color top, U8Color bottom) {
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
  for (int y = 0; y < height; ++y) {
    float const t = height <= 1 ? 0.f : static_cast<float>(y) / static_cast<float>(height - 1);
    U8Color row{
        .r = static_cast<std::uint8_t>(std::lround(static_cast<float>(top.r) * (1.f - t) + static_cast<float>(bottom.r) * t)),
        .g = static_cast<std::uint8_t>(std::lround(static_cast<float>(top.g) * (1.f - t) + static_cast<float>(bottom.g) * t)),
        .b = static_cast<std::uint8_t>(std::lround(static_cast<float>(top.b) * (1.f - t) + static_cast<float>(bottom.b) * t)),
        .a = 255,
    };
    for (int x = 0; x < width; ++x) putPixel(pixels, width, height, x, y, row);
  }
  return pixels;
}

std::vector<std::uint8_t> makeForeignCsdPixels(int width, int height) {
  auto pixels = makePixels(width, height, U8Color{35, 41, 58, 255}, U8Color{22, 25, 36, 255});
  fillRect(pixels, width, height, 0, 0, width, 38, U8Color{17, 22, 34, 255});
  fillRect(pixels, width, height, 16, 12, 14, 14, U8Color{226, 85, 85, 255});
  fillRect(pixels, width, height, 38, 12, 14, 14, U8Color{239, 188, 72, 255});
  fillRect(pixels, width, height, 60, 12, 14, 14, U8Color{71, 190, 118, 255});
  fillRect(pixels, width, height, 18, 58, width - 36, 42, U8Color{51, 63, 86, 255});
  fillRect(pixels, width, height, 18, 116, width - 120, 14, U8Color{80, 95, 124, 255});
  fillRect(pixels, width, height, 18, 140, width - 80, 14, U8Color{66, 78, 103, 255});
  return pixels;
}

std::vector<std::uint8_t> makeTier2Pixels(int width, int height) {
  auto pixels = makePixels(width, height, U8Color{244, 247, 252, 255}, U8Color{219, 227, 238, 255});
  fillRect(pixels, width, height, 22, 24, width - 44, 52, U8Color{255, 255, 255, 255});
  fillRect(pixels, width, height, 22, 92, width / 2 - 30, 104, U8Color{232, 238, 248, 255});
  fillRect(pixels, width, height, width / 2 + 10, 92, width / 2 - 32, 104, U8Color{210, 222, 238, 255});
  fillRect(pixels, width, height, 46, 112, width / 2 - 78, 12, U8Color{116, 130, 156, 255});
  fillRect(pixels, width, height, width / 2 + 34, 112, width / 2 - 80, 12, U8Color{116, 130, 156, 255});
  return pixels;
}

std::vector<std::uint8_t> makeTier3Pixels(int width, int height, flux::compositor::ChromeConfig const& chrome) {
  auto pixels = makePixels(width, height, U8Color{237, 243, 249, 255}, U8Color{205, 218, 234, 255});
  fillRect(pixels, width, height, 0, 0, width, chrome.titleBarHeight, U8Color{247, 250, 253, 242});
  fillRect(pixels, width, height, 16, 7, 52, 16, U8Color{224, 232, 244, 255});
  fillRect(pixels, width, height, 82, 7, 150, 16, U8Color{255, 255, 255, 255});
  fillRect(pixels, width, height, 248, 7, std::max(0, width - 320), 16, U8Color{232, 239, 248, 255});
  fillRect(pixels, width, height, width - chrome.controlsWidth, 0, chrome.controlsWidth, chrome.titleBarHeight,
           U8Color{247, 250, 253, 242});
  fillRect(pixels, width, height, 22, 54, width - 44, 54, U8Color{255, 255, 255, 255});
  fillRect(pixels, width, height, 22, 128, 120, height - 154, U8Color{226, 235, 246, 255});
  fillRect(pixels, width, height, 164, 128, width - 186, height - 154, U8Color{241, 245, 250, 255});
  return pixels;
}

flux::compositor::CommittedSurfaceSnapshot snapshot(std::uint64_t id, int x, int y, int width, int height) {
  return flux::compositor::CommittedSurfaceSnapshot{
      .id = id,
      .x = x,
      .y = y,
      .width = width,
      .height = height,
      .bufferWidth = width,
      .bufferHeight = height,
      .sourceX = 0.f,
      .sourceY = 0.f,
      .sourceWidth = static_cast<float>(width),
      .sourceHeight = static_cast<float>(height),
      .destinationWidth = width,
      .destinationHeight = height,
      .serial = id,
  };
}

void writeBigEndian(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xffu));
  out.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
  out.push_back(static_cast<std::uint8_t>(value & 0xffu));
}

void appendChunk(std::vector<std::uint8_t>& out, char const type[4], std::vector<std::uint8_t> const& data) {
  writeBigEndian(out, static_cast<std::uint32_t>(data.size()));
  std::size_t const typeOffset = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  std::uint32_t crc = crc32(0u, nullptr, 0u);
  crc = crc32(crc, out.data() + typeOffset, static_cast<uInt>(4u + data.size()));
  writeBigEndian(out, crc);
}

void writePng(std::filesystem::path const& path,
              std::vector<std::uint8_t> const& bgra,
              std::uint32_t width,
              std::uint32_t height) {
  if (width == 0 || height == 0 || bgra.size() < static_cast<std::size_t>(width) * height * 4u) {
    throw std::runtime_error("invalid PNG input pixels");
  }
  if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());

  std::vector<std::uint8_t> scanlines;
  scanlines.reserve(static_cast<std::size_t>(height) * (1u + static_cast<std::size_t>(width) * 4u));
  for (std::uint32_t y = 0; y < height; ++y) {
    scanlines.push_back(0);
    for (std::uint32_t x = 0; x < width; ++x) {
      std::size_t const offset = (static_cast<std::size_t>(y) * width + x) * 4u;
      scanlines.push_back(bgra[offset + 2u]);
      scanlines.push_back(bgra[offset + 1u]);
      scanlines.push_back(bgra[offset + 0u]);
      scanlines.push_back(bgra[offset + 3u]);
    }
  }

  uLongf compressedSize = compressBound(static_cast<uLong>(scanlines.size()));
  std::vector<std::uint8_t> compressed(compressedSize);
  int const result = compress2(compressed.data(),
                               &compressedSize,
                               scanlines.data(),
                               static_cast<uLong>(scanlines.size()),
                               Z_BEST_SPEED);
  if (result != Z_OK) throw std::runtime_error("failed to compress PNG data");
  compressed.resize(compressedSize);

  std::vector<std::uint8_t> png{137, 80, 78, 71, 13, 10, 26, 10};
  std::vector<std::uint8_t> ihdr;
  writeBigEndian(ihdr, width);
  writeBigEndian(ihdr, height);
  ihdr.push_back(8);
  ihdr.push_back(6);
  ihdr.push_back(0);
  ihdr.push_back(0);
  ihdr.push_back(0);
  appendChunk(png, "IHDR", ihdr);
  appendChunk(png, "IDAT", compressed);
  appendChunk(png, "IEND", {});

  std::ofstream output(path, std::ios::binary);
  if (!output) throw std::runtime_error("failed to open PNG output");
  output.write(reinterpret_cast<char const*>(png.data()), static_cast<std::streamsize>(png.size()));
  if (!output) throw std::runtime_error("failed to write PNG");
}

std::vector<std::uint8_t> readBgraImage(flux::VulkanContext& vk,
                                        VulkanImageTarget const& target,
                                        std::uint32_t width,
                                        std::uint32_t height) {
  VulkanReadbackBuffer readback{vk.physicalDevice(), vk.device(), width * height * 4u};
  VulkanCopyContext copy{vk.device(), vk.queue(), vk.queueFamily()};
  copy.copyImageToBuffer(target.image, readback.buffer, width, height);

  std::vector<std::uint8_t> pixels(width * height * 4u);
  void* mapped = nullptr;
  vkCheck(vkMapMemory(vk.device(), readback.memory, 0, readback.size, 0, &mapped), "vkMapMemory");
  std::memcpy(pixels.data(), mapped, pixels.size());
  vkUnmapMemory(vk.device(), readback.memory);
  return pixels;
}

} // namespace

int main(int argc, char** argv) {
  try {
    std::filesystem::path output =
        argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("build/compositor-window-chrome-fixture.png");
    constexpr int logicalWidth = 960;
    constexpr int logicalHeight = 640;
    constexpr float scale = 2.f;
    std::uint32_t const pixelWidth = static_cast<std::uint32_t>(std::ceil(logicalWidth * scale));
    std::uint32_t const pixelHeight = static_cast<std::uint32_t>(std::ceil(logicalHeight * scale));

    auto& vk = flux::VulkanContext::instance();
    vk.ensureInitialized();
    VulkanImageTarget imageTarget{vk.physicalDevice(), vk.device(), pixelWidth, pixelHeight};

    flux::FreeTypeTextSystem textSystem;
    flux::RenderTarget target{flux::VulkanRenderTargetSpec{
        .image = imageTarget.image,
        .view = imageTarget.view,
        .format = imageTarget.format,
        .width = pixelWidth,
        .height = pixelHeight,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    }};

    flux::Canvas& canvas = target.canvas();
    canvas.resize(logicalWidth, logicalHeight);
    canvas.updateDpiScale(scale, scale);

    flux::compositor::ChromeConfig chrome{};
    auto foreignImage = flux::Image::fromRgbaPixels(250, 150, makeForeignCsdPixels(250, 150), canvas.gpuDevice());
    auto ssdFocusedImage = flux::Image::fromRgbaPixels(260, 160, makeTier2Pixels(260, 160), canvas.gpuDevice());
    auto ssdUnfocusedImage = flux::Image::fromRgbaPixels(260, 160, makeTier2Pixels(260, 160), canvas.gpuDevice());
    auto cutoutImage = flux::Image::fromRgbaPixels(300, 180, makeTier3Pixels(300, 180, chrome), canvas.gpuDevice());
    auto rejectedImage = flux::Image::fromRgbaPixels(260, 170, makeTier2Pixels(260, 170), canvas.gpuDevice());
    auto resizingImage = flux::Image::fromRgbaPixels(180, 125, makeTier2Pixels(180, 125), canvas.gpuDevice());
    if (!foreignImage || !ssdFocusedImage || !ssdUnfocusedImage || !cutoutImage || !rejectedImage || !resizingImage) {
      throw std::runtime_error("failed to create fixture images");
    }

    auto foreign = snapshot(1, 34, 58, 250, 150);
    foreign.focused = false;

    auto ssdFocused = snapshot(2, 340, 86, 260, 160);
    ssdFocused.titleBarHeight = chrome.titleBarHeight;
    ssdFocused.title = "SSD focused";
    ssdFocused.serverSideDecorated = true;
    ssdFocused.focused = true;

    auto ssdUnfocused = snapshot(3, 650, 86, 260, 160);
    ssdUnfocused.titleBarHeight = chrome.titleBarHeight;
    ssdUnfocused.title = "SSD inactive";
    ssdUnfocused.serverSideDecorated = true;
    ssdUnfocused.focused = false;

    auto cutout = snapshot(4, 55, 340, 300, 180);
    cutout.serverSideDecorated = true;
    cutout.cutoutsBound = true;
    cutout.closeButtonHovered = true;
    cutout.focused = true;

    auto rejected = snapshot(5, 395, 350, 260, 170);
    rejected.titleBarHeight = chrome.titleBarHeight;
    rejected.title = "Cutout rejected";
    rejected.serverSideDecorated = true;
    rejected.cutoutsBound = true;
    rejected.cutoutsRejected = true;
    rejected.focused = false;

    auto resizing = snapshot(6, 700, 350, 220, 160);
    resizing.titleBarHeight = chrome.titleBarHeight;
    resizing.title = "Resize gap";
    resizing.serverSideDecorated = true;
    resizing.focused = true;
    resizing.activeSizing = true;
    resizing.bufferWidth = 180;
    resizing.bufferHeight = 125;
    resizing.sourceWidth = 180.f;
    resizing.sourceHeight = 125.f;
    resizing.destinationWidth = 180;
    resizing.destinationHeight = 125;

    flux::compositor::SurfaceVisualState foreignVisual{};
    flux::compositor::SurfaceVisualState ssdFocusedVisual{};
    flux::compositor::SurfaceVisualState ssdUnfocusedVisual{};
    flux::compositor::SurfaceVisualState cutoutVisual{};
    flux::compositor::SurfaceVisualState rejectedVisual{};
    flux::compositor::SurfaceVisualState resizingVisual{};
    auto const frameTime = std::chrono::steady_clock::now();

    target.beginFrame();
    canvas.clear(flux::Color{0.10f, 0.12f, 0.17f, 1.f});
    canvas.drawRect(flux::Rect::sharp(0.f, 0.f, logicalWidth, logicalHeight),
                    flux::CornerRadius{},
                    flux::FillStyle::linearGradient(flux::Color{0.13f, 0.16f, 0.22f, 1.f},
                                                    flux::Color{0.34f, 0.39f, 0.48f, 1.f},
                                                    flux::Point{0.f, 0.f},
                                                    flux::Point{1.f, 1.f}),
                    flux::StrokeStyle::none());
    canvas.drawRect(flux::Rect::sharp(18.f, 18.f, logicalWidth - 36.f, logicalHeight - 36.f),
                    flux::CornerRadius{18.f},
                    flux::FillStyle::solid(flux::Color{1.f, 1.f, 1.f, 0.04f}),
                    flux::StrokeStyle::solid(flux::Color{1.f, 1.f, 1.f, 0.12f}, 1.f));

    flux::compositor::drawCommittedSurfaceSnapshot(
        canvas, textSystem, foreign, foreignVisual, *foreignImage, frameTime, chrome, false);
    flux::compositor::drawCommittedSurfaceSnapshot(
        canvas, textSystem, ssdFocused, ssdFocusedVisual, *ssdFocusedImage, frameTime, chrome, false);
    flux::compositor::drawCommittedSurfaceSnapshot(
        canvas, textSystem, ssdUnfocused, ssdUnfocusedVisual, *ssdUnfocusedImage, frameTime, chrome, false);
    flux::compositor::drawCommittedSurfaceSnapshot(
        canvas, textSystem, cutout, cutoutVisual, *cutoutImage, frameTime, chrome, false);
    flux::compositor::drawCommittedSurfaceSnapshot(
        canvas, textSystem, rejected, rejectedVisual, *rejectedImage, frameTime, chrome, false);
    flux::compositor::drawCommittedSurfaceSnapshot(
        canvas, textSystem, resizing, resizingVisual, *resizingImage, frameTime, chrome, false);
    target.endFrame();

    writePng(output, readBgraImage(vk, imageTarget, pixelWidth, pixelHeight), pixelWidth, pixelHeight);
    std::cout << output << '\n';
    return 0;
  } catch (std::exception const& error) {
    std::cerr << "flux-compositor-render-fixture: " << error.what() << '\n';
    return 1;
  }
}
