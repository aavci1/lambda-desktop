  unsigned int handle_ = 0;
  TextSystem &textSystem_;
  VulkanRenderTargetSpec targetSpec_{};
  int width_ = 1;
  int height_ = 1;
  int framebufferWidth_ = 1;
  int framebufferHeight_ = 1;
  int swapchainTargetWidth_ = 1;
  int swapchainTargetHeight_ = 1;
  int resizeBoundsHintWidth_ = 0;
  int resizeBoundsHintHeight_ = 0;
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
  std::vector<CalloutInstance> callouts_;
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
  PFN_vkGetPastPresentationTimingGOOGLE getPastPresentationTiming_ = nullptr;
  std::uint32_t nextPresentId_ = 1;
  std::uint32_t lastSubmittedPresentId_ = 0;
  std::uint32_t queueFamily_ = 0;
  VkCommandPool commandPool_ = VK_NULL_HANDLE;
  std::array<VkCommandBuffer, kMaxFramesInFlight> commandBuffers_{};
  std::array<VkSemaphore, kMaxFramesInFlight> imageAvailable_{};
  std::array<VkFence, kMaxFramesInFlight> frameFences_{};
  std::array<std::uint64_t, kMaxFramesInFlight> frameFenceSubmitGenerations_{};
  std::array<std::uint64_t, kMaxFramesInFlight> frameFenceCompleteGenerations_{};
  static constexpr std::size_t kNoResetFrameFence = static_cast<std::size_t>(-1);
  std::size_t resetFrameFenceIndex_ = kNoResetFrameFence;
  std::size_t currentFrame_ = 0;
  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkPresentModeKHR presentMode_ = VK_PRESENT_MODE_FIFO_KHR;
  VkSurfaceFormatKHR surfaceFormat_{};
  VkExtent2D swapExtent_{};
  std::vector<VkImage> swapchainImages_;
  std::vector<VkFence> imageInFlightFences_;
  std::vector<VkFence> imagePresentFences_;
  std::vector<VkImageView> swapchainViews_;
  std::vector<VkSemaphore> imageRenderFinished_;
  std::vector<RetiredSwapchain> retiredSwapchains_;
  Texture backdropSceneTexture_;
  Texture backdropScratchTexture_;
  Texture backdropBlurTexture_;
  std::vector<CachedBackdropBlur> backdropBlurCache_;
  std::uint32_t backdropBlurBaseDownsample_ = kDefaultBackdropBlurBaseDownsample;
  std::array<FrameGeometryResources, kMaxFramesInFlight> frameGeometry_{};
  Buffer pendingScreenshotBuffer_;
  VkDeviceSize pendingScreenshotSize_ = 0;
  std::string pendingScreenshotPath_;
  VkFence pendingScreenshotFence_ = VK_NULL_HANDLE;
  bool debugScreenshotWritten_ = false;
  Buffer pendingFrameCaptureBuffer_;
  VkDeviceSize pendingFrameCaptureSize_ = 0;
  std::uint32_t pendingFrameCaptureWidth_ = 0;
  std::uint32_t pendingFrameCaptureHeight_ = 0;
  bool pendingFrameCaptureBgra_ = true;
  VkFence pendingFrameCaptureFence_ = VK_NULL_HANDLE;
  bool pendingFrameCaptureExternal_ = false;
  bool pendingFrameCaptureAwaitingExternalSubmit_ = false;
  int pendingFrameCaptureTracePolls_ = 0;
  bool frameCaptureRequested_ = false;
  bool capturedFrameAvailable_ = false;
  std::vector<std::uint8_t> capturedFrameBytes_;
  std::uint32_t capturedFrameWidth_ = 0;
  std::uint32_t capturedFrameHeight_ = 0;
  bool deviceWaitIdleOnDestruct_ = true;
  bool swapchainDirty_ = true;
  bool presentFenceRuntimeDisabled_ = false;
  bool transparentSurface_ = false;
  bool imagePremultipliedAlpha_ = false;
  std::unordered_map<VulkanImage const *, std::unique_ptr<Texture>> imageTextures_;
  std::vector<PendingTextureDestroy> pendingTextureDestroys_;
  std::vector<PendingTextureTransition> pendingTextureTransitions_;
  std::vector<PendingTextureUpload> pendingTextureUploads_;
  std::vector<PendingBufferDestroy> pendingBufferDestroys_;
  std::vector<PendingRecorderResourceDestroy> pendingRecorderResourceDestroys_;
  std::vector<Buffer> uploadStagingPool_;
  std::vector<PendingOwnedVulkanImageDestroy> pendingOwnedImageDestroys_;
  std::uint64_t renderTargetFrameSignature_ = 0;
  bool renderTargetFrameCacheValid_ = false;
  bool ownsSharedVulkanCore_ = false;
  bool targetMode_ = false;
};

} // namespace

namespace {

void evictImageTexturesFor(VulkanImage const *image) {
  if (!image)
    return;
  std::lock_guard lock(gCanvasRegistryMutex);
  for (VulkanCanvas* canvas : gCanvases) {
    if (canvas)
      canvas->evictImageTexture(image);
  }
}

bool deferOwnedVulkanImageDestroy(PendingOwnedVulkanImageDestroy destroy) {
  if (!destroy.device || (!destroy.image && !destroy.view && !destroy.importedMemory)) {
    return false;
  }
  std::lock_guard lock(gCanvasRegistryMutex);
  for (VulkanCanvas* canvas : gCanvases) {
    if (canvas && canvas->deferOwnedImageDestroy(destroy)) {
      return true;
    }
  }
  return false;
}

} // namespace

bool deferVulkanFrameRecorderResourcesDestroy(VulkanFrameRecorderResources resources) noexcept {
  if (!resources.device ||
      (!resources.preparedQuadBuffer && !resources.preparedRectBuffer &&
       !resources.preparedPathVertexBuffer && !resources.preparedQuadDescriptor &&
       !resources.preparedRectDescriptor)) {
    return false;
  }
  std::lock_guard lock(gCanvasRegistryMutex);
  for (VulkanCanvas* canvas : gCanvases) {
    if (canvas && canvas->deferFrameRecorderResourcesDestroy(resources)) {
      return true;
    }
  }
  return false;
}

namespace {

void updateImageTexturesFor(VulkanImage const* image) {
  if (!image)
    return;
  std::lock_guard lock(gCanvasRegistryMutex);
  for (VulkanCanvas* canvas : gCanvases) {
    if (canvas)
      canvas->updateImageTexture(image);
  }
}

void updateImageTextureRegionFor(VulkanImage const* image,
                                 std::uint32_t x,
                                 std::uint32_t y,
                                 std::uint32_t width,
                                 std::uint32_t height,
                                 void const* pixels,
                                 std::uint32_t sourceBytesPerRow) {
  if (!image || !pixels)
    return;
  std::lock_guard lock(gCanvasRegistryMutex);
  for (VulkanCanvas* canvas : gCanvases) {
    if (canvas)
      canvas->updateImageTextureRegion(image, x, y, width, height, pixels, sourceBytesPerRow);
  }
}

void markImageTextureContentsChangedFor(VulkanImage const* image) {
  if (!image)
    return;
  std::lock_guard lock(gCanvasRegistryMutex);
  for (VulkanCanvas* canvas : gCanvases) {
    if (canvas)
      canvas->markImageTextureContentsChanged(image);
  }
}

} // namespace
