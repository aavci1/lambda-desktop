#pragma once

#include <Lambda/Graphics/Canvas.hpp>

#include <filesystem>
#include <memory>
#include <span>
#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

namespace lambda {

class TextSystem;
struct VulkanFrameRecorder;
struct VulkanRenderTargetSpec;

struct VulkanCanvasOptions {
  bool transparentSurface = false;
};

enum class VulkanCalloutPlacement : std::uint8_t {
  Below,
  Above,
  End,
  Start,
};

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
std::unique_ptr<Canvas> createVulkanCanvas(VkSurfaceKHR surface,
                                           unsigned int handle,
                                           TextSystem& textSystem,
                                           VulkanCanvasOptions options = {});
std::unique_ptr<Canvas> createVulkanRenderTargetCanvas(VulkanRenderTargetSpec const& spec,
                                                       TextSystem& textSystem);
bool setVulkanRenderTargetSpecForCanvas(Canvas* canvas, VulkanRenderTargetSpec const& spec);
bool markVulkanRenderTargetCanvasSubmitted(Canvas* canvas);

/// Requests a CPU readback of the next presented Vulkan frame as BGRA8.
bool requestNextFrameCaptureForCanvas(Canvas* canvas);
/// Retrieves the most recent captured Vulkan frame bytes as BGRA8.
bool takeCapturedFrameForCanvas(Canvas* canvas, std::vector<std::uint8_t>& out, std::uint32_t& width,
                                std::uint32_t& height);
bool beginRecordedOpsCaptureForCanvas(Canvas* canvas, VulkanFrameRecorder* target);
void endRecordedOpsCaptureForCanvas(Canvas* canvas);
bool prepareRecordedOpsForCanvas(Canvas* canvas, VulkanFrameRecorder* recorded);
bool recordedOpsGlyphAtlasCurrentForCanvas(Canvas* canvas, VulkanFrameRecorder const& recorded);
bool replayRecordedOpsForCanvas(Canvas* canvas, VulkanFrameRecorder const& recorded);
bool replayRecordedLocalOpsForCanvas(Canvas* canvas, VulkanFrameRecorder const& recorded);
void setVulkanCanvasResizeBoundsHint(Canvas* canvas, int logicalWidth, int logicalHeight);
bool setVulkanCanvasBackdropBlurBaseDownsample(Canvas* canvas, std::uint32_t downsample);
bool setVulkanCanvasImagePremultipliedAlpha(Canvas* canvas, bool enabled);
bool setVulkanCanvasTransparentSurface(Canvas* canvas, bool enabled);
bool markVulkanImageContentsChanged(Image* image);
bool drawVulkanBackdropBlurFrame(Canvas* canvas,
                                 Rect const& frame,
                                 CornerRadius const& frameRadius,
                                 Rect const& cutout,
                                 float radius,
                                 Color tint);
bool drawVulkanCalloutMaterial(Canvas* canvas,
                               Rect const& bounds,
                               Rect const& card,
                               CornerRadius const& corners,
                               Color baseColor,
                               Color tintColor,
                               Color borderColor,
                               float borderWidth,
                               VulkanCalloutPlacement placement,
                               float arrowWidth,
                               float arrowHeight);
bool vulkanCanvasSupportsDisplayTiming(Canvas* canvas);
bool vulkanCanvasUsesMailboxPresentMode(Canvas* canvas);
std::uint32_t lastVulkanCanvasPresentId(Canvas* canvas);
std::vector<VulkanPastPresentationTiming> pollVulkanCanvasPastPresentationTimings(Canvas* canvas);

} // namespace lambda
