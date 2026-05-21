#pragma once

#include <Flux/Graphics/Canvas.hpp>

#include <filesystem>
#include <memory>
#include <span>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

namespace flux {

class TextSystem;
struct VulkanFrameRecorder;
struct VulkanRenderTargetSpec;

struct VulkanPastPresentationTiming {
  std::uint32_t presentId = 0;
  std::uint64_t desiredPresentTime = 0;
  std::uint64_t actualPresentTime = 0;
  std::uint64_t earliestPresentTime = 0;
  std::uint64_t presentMargin = 0;
};

void configureVulkanCanvasRuntime(std::span<char const* const> requiredInstanceExtensions,
                                  std::filesystem::path cacheDir);
VkInstance ensureSharedVulkanInstance();
std::unique_ptr<Canvas> createVulkanCanvas(VkSurfaceKHR surface, unsigned int handle, TextSystem& textSystem);
std::unique_ptr<Canvas> createVulkanRenderTargetCanvas(VulkanRenderTargetSpec const& spec,
                                                       TextSystem& textSystem);

bool beginRecordedOpsCaptureForCanvas(Canvas* canvas, VulkanFrameRecorder* target);
void endRecordedOpsCaptureForCanvas(Canvas* canvas);
bool replayRecordedOpsForCanvas(Canvas* canvas, VulkanFrameRecorder const& recorded);
bool replayRecordedLocalOpsForCanvas(Canvas* canvas, VulkanFrameRecorder const& recorded);
void setVulkanCanvasResizeBoundsHint(Canvas* canvas, int logicalWidth, int logicalHeight);
bool vulkanCanvasSupportsDisplayTiming(Canvas* canvas);
std::vector<VulkanPastPresentationTiming> pollVulkanCanvasPastPresentationTimings(Canvas* canvas);

} // namespace flux
