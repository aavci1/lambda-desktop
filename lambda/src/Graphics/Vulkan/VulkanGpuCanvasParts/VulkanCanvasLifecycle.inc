namespace {

class VulkanCanvas final : public Canvas {
  friend bool ::lambdaui::deferVulkanFrameRecorderResourcesDestroy(
      VulkanFrameRecorderResources resources) noexcept;

public:
  VulkanCanvas(VkSurfaceKHR surface,
               unsigned int handle,
               TextSystem &textSystem,
               VulkanCanvasOptions options)
      : handle_(handle), textSystem_(textSystem), surface_(surface),
        transparentSurface_(options.transparentSurface) {
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
    if (shared->googleDisplayTiming) {
      getPastPresentationTiming_ = reinterpret_cast<PFN_vkGetPastPresentationTimingGOOGLE>(
          vkGetDeviceProcAddr(device_, "vkGetPastPresentationTimingGOOGLE"));
    }
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

  bool supportsDisplayTiming() const noexcept {
    return getPastPresentationTiming_ != nullptr && swapchain_ != VK_NULL_HANDLE;
  }

  bool usesMailboxPresentMode() const noexcept {
    return swapchain_ != VK_NULL_HANDLE && presentMode_ == VK_PRESENT_MODE_MAILBOX_KHR;
  }

  bool setImagePremultipliedAlpha(bool enabled) {
    bool const previous = imagePremultipliedAlpha_;
    imagePremultipliedAlpha_ = enabled;
    return previous;
  }

  bool setTransparentSurface(bool enabled) {
    if (targetMode_) {
      return false;
    }
    bool const changed = transparentSurface_ != enabled;
    transparentSurface_ = enabled;
    if (changed) {
      swapchainDirty_ = true;
    }
    return changed;
  }

  void setBackdropBlurBaseDownsample(std::uint32_t downsample) {
    std::uint32_t const next = std::clamp(downsample,
                                          kMinBackdropBlurBaseDownsample,
                                          kMaxBackdropBlurBaseDownsample);
    if (next == backdropBlurBaseDownsample_) {
      return;
    }
    backdropBlurBaseDownsample_ = next;
    backdropBlurCache_.clear();
    renderTargetFrameCacheValid_ = false;
  }

  bool setRenderTargetSpec(VulkanRenderTargetSpec const& spec) {
    if (!targetMode_ || !spec.image || !spec.view || spec.width == 0 || spec.height == 0) {
      return false;
    }
    targetSpec_ = spec;
    framebufferWidth_ = static_cast<int>(spec.width);
    framebufferHeight_ = static_cast<int>(spec.height);
    swapExtent_ = VkExtent2D{spec.width, spec.height};
    renderTargetFrameCacheValid_ = false;
    return true;
  }

  void markExternalRenderTargetSubmitted() {
    pendingFrameCaptureAwaitingExternalSubmit_ = false;
    if (frameCaptureTraceEnabled() && pendingFrameCaptureExternal_) {
      std::fprintf(stderr, "Lambda Vulkan: external render target submitted for frame capture\n");
    }
    retireDeferredResourcesAfterSubmit();
    renderTargetFrameCacheValid_ = false;
    currentFrame_ = (currentFrame_ + 1u) % kMaxFramesInFlight;
    debug::perf::recordPresentedFrame();
  }

  bool requestNextFrameCapture() {
    frameCaptureRequested_ = true;
    return true;
  }

  bool takeCapturedFrame(std::vector<std::uint8_t>& out, std::uint32_t& width, std::uint32_t& height) {
    pollFrameCapture(false);
    if (!capturedFrameAvailable_ || capturedFrameWidth_ == 0 || capturedFrameHeight_ == 0) {
      return false;
    }
    out = std::move(capturedFrameBytes_);
    width = capturedFrameWidth_;
    height = capturedFrameHeight_;
    capturedFrameBytes_.clear();
    capturedFrameWidth_ = 0;
    capturedFrameHeight_ = 0;
    capturedFrameAvailable_ = false;
    return true;
  }

  std::uint32_t lastPresentId() const noexcept {
    return lastSubmittedPresentId_;
  }

  std::vector<VulkanPastPresentationTiming> pollPastPresentationTimings() {
    if (!supportsDisplayTiming()) {
      return {};
    }
    std::uint32_t count = 0;
    VkResult result = getPastPresentationTiming_(device_, swapchain_, &count, nullptr);
    if (result != VK_SUCCESS || count == 0) {
      return {};
    }
    std::vector<VkPastPresentationTimingGOOGLE> timings(count);
    result = getPastPresentationTiming_(device_, swapchain_, &count, timings.data());
    if (result != VK_SUCCESS) {
      return {};
    }
    timings.resize(count);
    std::vector<VulkanPastPresentationTiming> out;
    out.reserve(timings.size());
    for (auto const& timing : timings) {
      out.push_back(VulkanPastPresentationTiming{
          .presentId = timing.presentID,
          .desiredPresentTime = timing.desiredPresentTime,
          .actualPresentTime = timing.actualPresentTime,
          .earliestPresentTime = timing.earliestPresentTime,
          .presentMargin = timing.presentMargin,
      });
    }
    return out;
  }

  bool recordedOpsGlyphAtlasCurrent(VulkanFrameRecorder const& recorded) const {
    return recordedGlyphAtlasCurrent(recorded);
  }

  ~VulkanCanvas() override {
    if (device_) {
      if (deviceWaitIdleOnDestruct_) {
        // Intentional canvas teardown wait before destroying swapchain/present resources.
        vkDeviceWaitIdle(device_);
      } else {
        waitForSubmittedFrameWork();
      }
      pollReadbacks(true);
    }
    unregisterCanvas();
    destroySwapchain();
    destroyPendingTextureUploads();
    destroyTexture(backdropSceneTexture_);
    destroyTexture(backdropScratchTexture_);
    destroyTexture(backdropBlurTexture_);
    clearBackdropBlurCache();
    for (auto &kv : imageTextures_) {
      if (kv.second) {
        destroyTexture(*kv.second);
      }
    }
    destroyDeferredTextures(true);
    destroyDeferredBuffers(true);
    destroyDeferredRecorderResources(true);
    destroyDeferredOwnedImages(true);
    for (Buffer& buffer : uploadStagingPool_) {
      destroyBuffer(buffer);
    }
    uploadStagingPool_.clear();
    destroyBuffer(pendingScreenshotBuffer_);
    destroyBuffer(pendingFrameCaptureBuffer_);
    for (auto& geometry : frameGeometry_) {
      destroyFrameGeometryResources(geometry);
    }
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

  void waitForSubmittedFrameWork() {
    if (!device_) {
      return;
    }
    for (std::size_t i = 0; i < frameFences_.size(); ++i) {
      VkFence const fence = frameFences_[i];
      if (fence == VK_NULL_HANDLE ||
          frameFenceCompleteGenerations_[i] >= frameFenceSubmitGenerations_[i]) {
        continue;
      }
      vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);
      markFrameFenceComplete(i);
    }
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
      swapchainTargetWidth_ = fbW + (addHeadroom ? std::max(512, fbW / 2) : 0);
      swapchainTargetHeight_ = fbH + (addHeadroom ? std::max(512, fbH / 2) : 0);
      swapchainDirty_ = true;
      if (detail::resizeTraceEnabled()) {
        LAMBDA_RESIZE_TRACE(
          "vulkan-resize",
          "window=%u logical=%dx%d framebuffer=%dx%d target=%dx%d boundsHint=%dx%d dirty=1\n",
                     handle_, width_, height_, framebufferWidth_, framebufferHeight_,
                     swapchainTargetWidth_, swapchainTargetHeight_,
                     resizeBoundsHintWidth_,
                     resizeBoundsHintHeight_);
      }
    } else if (framebufferChanged && detail::resizeTraceEnabled()) {
      LAMBDA_RESIZE_TRACE(
        "vulkan-resize",
        "window=%u logical=%dx%d framebuffer=%dx%d extent=%ux%u dirty=0\n",
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

  void setResizeBoundsHint(int logicalWidth, int logicalHeight) {
    resizeBoundsHintWidth_ = std::max(0, logicalWidth);
    resizeBoundsHintHeight_ = std::max(0, logicalHeight);
  }

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
    return appendRecordedOps(recorded, false);
  }

  bool replayRecordedLocalOps(VulkanFrameRecorder const &recorded) {
    if (!recordedGlyphAtlasCurrent(recorded) || !state_.transform.isTranslationOnly()) {
      return false;
    }
    if (!prepareRecorderBuffers(recorded)) {
      return false;
    }
    return appendRecordedOps(recorded, true);
  }

  bool prepareRecordedOps(VulkanFrameRecorder *recorded) {
    if (!recorded) {
      return false;
    }
    if (!recorded->replayable) {
      return false;
    }
    prepareRecordedGeometrySignatures(*recorded);
    return prepareRecorderBuffers(*recorded);
  }

  void beginFrame() override {
    captureTarget_ = nullptr;
    hasCaptureSavedState_ = false;
    captureSavedStack_.clear();
    rects_.clear();
    callouts_.clear();
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
        targetCanvas.waitForSubmittedFrameWork();
        targetCanvas.deviceWaitIdleOnDestruct_ = false;
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
      if (detail::resizeTraceEnabled()) {
        auto const elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - presentStart).count();
        LAMBDA_RESIZE_TRACE(
          "vulkan-present",
          "window=%u logical=%dx%d framebuffer=%dx%d extent=%ux%u elapsed=%.3fms\n",
                     handle_, width_, height_, framebufferWidth_, framebufferHeight_,
                     swapExtent_.width, swapExtent_.height,
                     static_cast<double>(elapsed) / 1000.0);
      }
    } catch (std::exception const &e) {
      recoverResetFrameFence();
      std::fprintf(stderr, "Lambda Vulkan: present failed: %s\n", e.what());
      swapchainDirty_ = true;
    }
  }

  void presentImpl() {
    bool const traceResize = detail::resizeTraceEnabled();
    auto const phaseStart = [&] {
      return traceResize ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    };
    auto const phaseMs = [&](std::chrono::steady_clock::time_point start) {
      if (!traceResize) return 0.0;
      auto const elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start).count();
      return static_cast<double>(elapsed) / 1000.0;
    };

    VkFence const frameFence = frameFences_[currentFrame_];
    VkSemaphore const imageAvailable = imageAvailable_[currentFrame_];
    VkCommandBuffer const commandBuffer = commandBuffers_[currentFrame_];

    auto start = phaseStart();
    vkWaitForFences(device_, 1, &frameFence, VK_TRUE, UINT64_MAX);
    markFrameFenceComplete(currentFrame_);
    double const frameFenceMs = phaseMs(start);
    start = phaseStart();
    pollReadbacks(false);
    double screenshotMs = phaseMs(start);
    for (VkFence& imageFence : imageInFlightFences_) {
      if (imageFence == frameFence) imageFence = VK_NULL_HANDLE;
    }
    double deferredDestroyMs = 0.0;
    if (!swapchainImageStateReadyForAcquire()) {
      swapchainDirty_ = true;
      return;
    }
    std::uint32_t imageIndex = 0;
    start = phaseStart();
    VkResult acquired = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, imageAvailable, VK_NULL_HANDLE,
                                              &imageIndex);
    double const acquireMs = phaseMs(start);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
      swapchainDirty_ = true;
      return;
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
      vkCheck(acquired, "vkAcquireNextImageKHR");
    }
    assert(imageIndex < imageInFlightFences_.size());
    assert(imageIndex < imageRenderFinished_.size());
    assert(imageRenderFinished_[imageIndex] != VK_NULL_HANDLE);
    double imageFenceMs = 0.0;
    if (imageInFlightFences_[imageIndex]) {
      start = phaseStart();
      vkWaitForFences(device_, 1, &imageInFlightFences_[imageIndex], VK_TRUE, UINT64_MAX);
      markFrameFenceComplete(imageInFlightFences_[imageIndex]);
      imageFenceMs = phaseMs(start);
    }
    imageInFlightFences_[imageIndex] = frameFence;
    VkSemaphore const renderFinished = imageRenderFinished_[imageIndex];

    start = phaseStart();
    queueAtlasUploadIfNeeded();
    double const atlasMs = phaseMs(start);
    start = phaseStart();
    auto backdropRuns = prepareBackdropBlurRuns();
    double const backdropPrepareMs = phaseMs(start);
    start = phaseStart();
    uploadFrameBuffers();
    double const uploadMs = phaseMs(start);

    start = phaseStart();
    vkResetCommandBuffer(commandBuffer, 0);
    auto begin = vkStructure<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    vkCheck(vkBeginCommandBuffer(commandBuffer, &begin), "vkBeginCommandBuffer");
    VkClearValue clear{};
    clear.color.float32[0] = clearColor_.r;
    clear.color.float32[1] = clearColor_.g;
    clear.color.float32[2] = clearColor_.b;
    clear.color.float32[3] = clearColor_.a;
    recordPendingTextureTransitions(commandBuffer);
    recordPendingTextureUploads(commandBuffer);
    if (!backdropRuns.empty() && backdropSceneTexture_.view && backdropScratchTexture_.view && backdropBlurTexture_.view) {
      VkImageLayout targetLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      drawOpsWithStackedBackdropBlur(commandBuffer,
                                     swapchainImages_[imageIndex],
                                     swapchainViews_[imageIndex],
                                     targetLayout,
                                     clear,
                                     VK_ATTACHMENT_LOAD_OP_CLEAR,
                                     backdropRuns);
    } else {
      transition(commandBuffer, swapchainImages_[imageIndex], VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
      beginColorRendering(commandBuffer,
                          swapchainViews_[imageIndex],
                          swapExtent_,
                          clear,
                          VK_ATTACHMENT_LOAD_OP_CLEAR,
                          currentFramebufferPixelRect());
      drawOps(commandBuffer);
      vkCmdEndRendering(commandBuffer);
    }
    transition(commandBuffer, swapchainImages_[imageIndex], VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    writeDebugScreenshotIfRequested(commandBuffer, swapchainImages_[imageIndex], frameFence);
    captureFrameIfRequested(commandBuffer, swapchainImages_[imageIndex], frameFence);
    transition(commandBuffer, swapchainImages_[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    vkCheck(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");
    double const recordMs = phaseMs(start);

    auto waitSemaphore = vkStructure<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
    waitSemaphore.semaphore = imageAvailable;
    waitSemaphore.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    auto commandBufferInfo = vkStructure<VkCommandBufferSubmitInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
    commandBufferInfo.commandBuffer = commandBuffer;
    auto signalSemaphore = vkStructure<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
    signalSemaphore.semaphore = renderFinished;
    signalSemaphore.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
    auto submit = vkStructure<VkSubmitInfo2>(VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &waitSemaphore;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &commandBufferInfo;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signalSemaphore;
    resetFrameFenceIndex_ = currentFrame_;
    vkCheck(vkResetFences(device_, 1, &frameFence), "vkResetFences");
    start = phaseStart();
    VkResult const submitted = vkQueueSubmit2(queue_, 1, &submit, frameFence);
    double const submitMs = phaseMs(start);
    if (submitted != VK_SUCCESS) {
      recoverResetFrameFence();
      vkCheck(submitted, "vkQueueSubmit");
    }
    markFrameFenceSubmitted(currentFrame_);
    resetFrameFenceIndex_ = kNoResetFrameFence;
    start = phaseStart();
    retireDeferredResourcesAfterSubmit();
    deferredDestroyMs = phaseMs(start);
    start = phaseStart();
    pollReadbacks(false);
    screenshotMs += phaseMs(start);

    auto presentInfo = vkStructure<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex;
    VkFence presentFence = VK_NULL_HANDLE;
    auto presentFenceInfo =
        vkStructure<VkSwapchainPresentFenceInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR);
    double presentFenceStatusMs = 0.0;
    double presentFenceResetMs = 0.0;
    bool const usePresentFence = shared_ && shared_->swapchainMaintenance1 && !presentFenceRuntimeDisabled_ &&
                                 imageIndex < imagePresentFences_.size() &&
                                 imagePresentFences_[imageIndex] != VK_NULL_HANDLE;
    if (usePresentFence) {
      presentFence = imagePresentFences_[imageIndex];
      start = phaseStart();
      VkResult const fenceStatus = vkGetFenceStatus(device_, presentFence);
      presentFenceStatusMs = phaseMs(start);
      if (fenceStatus == VK_SUCCESS) {
        start = phaseStart();
        vkCheck(vkResetFences(device_, 1, &presentFence), "vkResetFences(presentFence)");
        presentFenceResetMs = phaseMs(start);
      } else if (fenceStatus == VK_NOT_READY) {
        VkFence replacement = createPresentFence(false, "vkCreateFence(presentFence replacement)");
        retirePresentFence(presentFence);
        imagePresentFences_[imageIndex] = replacement;
        presentFence = replacement;
      } else {
        vkCheck(fenceStatus, "vkGetFenceStatus(presentFence)");
      }
      presentFenceInfo.swapchainCount = 1;
      presentFenceInfo.pFences = &presentFence;
    }
    VkPresentTimeGOOGLE presentTime{.presentID = nextPresentId_, .desiredPresentTime = 0};
    auto presentTimes = vkStructure<VkPresentTimesInfoGOOGLE>(VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE);
    if (getPastPresentationTiming_) {
      presentTimes.swapchainCount = 1;
      presentTimes.pTimes = &presentTime;
      presentInfo.pNext = &presentTimes;
    }
    if (presentFence) {
      presentFenceInfo.pNext = presentInfo.pNext;
      presentInfo.pNext = &presentFenceInfo;
    }
    bool const presentFenceQueued = presentFence != VK_NULL_HANDLE;
    start = phaseStart();
    VkResult presented = vkQueuePresentKHR(queue_, &presentInfo);
    double const presentQueueMs = phaseMs(start);
    bool const presentAccepted = presented == VK_SUCCESS || presented == VK_SUBOPTIMAL_KHR;
    if (!presentAccepted && presentFence && imageIndex < imagePresentFences_.size() &&
        imagePresentFences_[imageIndex] == presentFence) {
      retirePresentFence(presentFence);
      imagePresentFences_[imageIndex] = createPresentFence(true, "vkCreateFence(presentFence after failed present)");
      presentFence = VK_NULL_HANDLE;
    }
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
      swapchainDirty_ = true;
    } else {
      vkCheck(presented, "vkQueuePresentKHR");
    }
    if (getPastPresentationTiming_ && (presented == VK_SUCCESS || presented == VK_SUBOPTIMAL_KHR)) {
      lastSubmittedPresentId_ = presentTime.presentID;
      ++nextPresentId_;
    } else {
      lastSubmittedPresentId_ = 0;
    }
    currentFrame_ = (currentFrame_ + 1u) % kMaxFramesInFlight;
    debug::perf::recordPresentedFrame();
    if (traceResize) {
      LAMBDA_RESIZE_TRACE(
          "vulkan-present-detail",
          "window=%u image=%u ops=%zu rects=%zu quads=%zu paths=%zu "
          "waitFrame=%.3fms deferred=%.3fms acquire=%.3fms waitImage=%.3fms "
          "atlas=%.3fms blurPrep=%.3fms upload=%.3fms record=%.3fms submit=%.3fms "
          "screenshot=%.3fms presentFence=%d statusPresentFence=%.3fms resetPresentFence=%.3fms "
          "queuePresent=%.3fms\n",
          handle_,
          imageIndex,
          ops_.size(),
          rects_.size(),
          quads_.size(),
          pathVerts_.size(),
          frameFenceMs,
          deferredDestroyMs,
          acquireMs,
          imageFenceMs,
          atlasMs,
          backdropPrepareMs,
          uploadMs,
          recordMs,
          submitMs,
          screenshotMs,
          presentFenceQueued ? 1 : 0,
          presentFenceStatusMs,
          presentFenceResetMs,
          presentQueueMs);
    }
  }

  struct RenderTargetRecordStats {
    double atlasMs = 0.0;
    double backdropPrepareMs = 0.0;
    double uploadMs = 0.0;
    double textureUploadRecordMs = 0.0;
    double drawRecordMs = 0.0;
    std::size_t backdropRuns = 0;
  };

  void recordRenderTargetCommands(VkCommandBuffer commandBuffer, VkImage targetImage,
                                  VkImageView targetView, VkImageLayout initialLayout,
                                  VkImageLayout finalLayout,
                                  RenderTargetRecordStats* stats = nullptr) {
    auto const recordStart = std::chrono::steady_clock::now();
    bool const traceResize = stats && detail::resizeTraceEnabled();
    auto const phaseStart = [&] {
      return traceResize ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    };
    auto const phaseMs = [&](std::chrono::steady_clock::time_point start) {
      if (!traceResize) return 0.0;
      auto const elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start).count();
      return static_cast<double>(elapsed) / 1000.0;
    };

    auto start = phaseStart();
    queueAtlasUploadIfNeeded();
    if (stats) stats->atlasMs = phaseMs(start);
    start = phaseStart();
    std::vector<BackdropBlurRun> backdropRuns = prepareBackdropBlurRuns();
    if (stats) {
      stats->backdropPrepareMs = phaseMs(start);
      stats->backdropRuns = backdropRuns.size();
    }
    start = phaseStart();
    uploadFrameBuffers();
    if (stats) stats->uploadMs = phaseMs(start);
    start = phaseStart();
    recordPendingTextureTransitions(commandBuffer);
    recordPendingTextureUploads(commandBuffer);
    if (stats) stats->textureUploadRecordMs = phaseMs(start);

    VkClearValue clear{};
    clear.color.float32[0] = clearColor_.r;
    clear.color.float32[1] = clearColor_.g;
    clear.color.float32[2] = clearColor_.b;
    clear.color.float32[3] = clearColor_.a;
    VkImageLayout targetLayout = initialLayout;
    start = phaseStart();
    VkAttachmentLoadOp const loadOp =
        targetSpec_.preserveContents ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
    if (!backdropRuns.empty() && backdropSceneTexture_.view && backdropScratchTexture_.view && backdropBlurTexture_.view) {
      drawOpsWithStackedBackdropBlur(commandBuffer,
                                     targetImage,
                                     targetView,
                                     targetLayout,
                                     clear,
                                     loadOp,
                                     backdropRuns);
    } else {
      transition(commandBuffer, targetImage, targetLayout, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
      targetLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
      beginColorRendering(commandBuffer, targetView, swapExtent_, clear, loadOp);
      drawOps(commandBuffer);
      vkCmdEndRendering(commandBuffer);
    }
    if (stats) stats->drawRecordMs = phaseMs(start);
    transition(commandBuffer, targetImage, targetLayout, finalLayout);
    debug::perf::recordVulkanRecordDuration(std::chrono::steady_clock::now() - recordStart);
  }

  void presentRenderTarget() {
    try {
      targetSpec_.width = std::max<std::uint32_t>(1, targetSpec_.width);
      targetSpec_.height = std::max<std::uint32_t>(1, targetSpec_.height);
      framebufferWidth_ = static_cast<int>(targetSpec_.width);
      framebufferHeight_ = static_cast<int>(targetSpec_.height);
      swapExtent_ = VkExtent2D{targetSpec_.width, targetSpec_.height};

      bool const externalCommandBuffer = targetSpec_.commandBuffer != VK_NULL_HANDLE;
      bool const canReuseCompletedFrame =
          targetMode_ &&
          !externalCommandBuffer &&
          !targetSpec_.waitSemaphore &&
          !targetSpec_.signalSemaphore &&
          !frameCaptureRequested_ &&
          !renderTargetFrameCacheDisabled() &&
          !resources().atlasDirty;
      std::uint64_t const requestedFrameSignature =
          canReuseCompletedFrame ? renderTargetFrameSignature() : 0;
      if (canReuseCompletedFrame &&
          renderTargetFrameCacheValid_ &&
          renderTargetFrameSignature_ == requestedFrameSignature) {
        debug::perf::recordPresentedFrame();
        return;
      }
      VkCommandBuffer commandBuffer = targetSpec_.commandBuffer;
      VkFence frameFence = externalCommandBuffer ? targetSpec_.completionFence : VK_NULL_HANDLE;
      bool const traceResize = detail::resizeTraceEnabled();
      auto const phaseStart = [&] {
        return traceResize ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
      };
      auto const phaseMs = [&](std::chrono::steady_clock::time_point start) {
        if (!traceResize) return 0.0;
        auto const elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();
        return static_cast<double>(elapsed) / 1000.0;
      };
      double waitFrameMs = 0.0;
      double deferredDestroyMs = 0.0;
      double beginMs = 0.0;
      double endMs = 0.0;
      double submitMs = 0.0;
      if (!externalCommandBuffer) {
        frameFence = frameFences_[currentFrame_];
        commandBuffer = commandBuffers_[currentFrame_];
        auto start = phaseStart();
        vkWaitForFences(device_, 1, &frameFence, VK_TRUE, UINT64_MAX);
        markFrameFenceComplete(currentFrame_);
        waitFrameMs = phaseMs(start);
        pollReadbacks(false);
        start = phaseStart();
        vkResetCommandBuffer(commandBuffer, 0);
        auto begin = vkStructure<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkCheck(vkBeginCommandBuffer(commandBuffer, &begin), "vkBeginCommandBuffer");
        beginMs = phaseMs(start);
      }

      RenderTargetRecordStats recordStats{};
      recordRenderTargetCommands(commandBuffer, targetSpec_.image, targetSpec_.view,
                                 targetSpec_.initialLayout, targetSpec_.finalLayout,
                                 traceResize ? &recordStats : nullptr);
      if (frameCaptureRequested_) {
        transition(commandBuffer, targetSpec_.image, targetSpec_.finalLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        captureFrameIfRequested(commandBuffer, targetSpec_.image, frameFence, externalCommandBuffer);
        transition(commandBuffer, targetSpec_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, targetSpec_.finalLayout);
      }

      if (externalCommandBuffer) {
        return;
      }

      auto start = phaseStart();
      vkCheck(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");
      endMs = phaseMs(start);

      auto waitSemaphore = vkStructure<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
      waitSemaphore.semaphore = targetSpec_.waitSemaphore;
      waitSemaphore.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
      auto commandBufferInfo = vkStructure<VkCommandBufferSubmitInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
      commandBufferInfo.commandBuffer = commandBuffer;
      auto signalSemaphore = vkStructure<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
      signalSemaphore.semaphore = targetSpec_.signalSemaphore;
      signalSemaphore.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
      auto submit = vkStructure<VkSubmitInfo2>(VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
      submit.waitSemaphoreInfoCount = targetSpec_.waitSemaphore ? 1u : 0u;
      submit.pWaitSemaphoreInfos = targetSpec_.waitSemaphore ? &waitSemaphore : nullptr;
      submit.commandBufferInfoCount = 1;
      submit.pCommandBufferInfos = &commandBufferInfo;
      submit.signalSemaphoreInfoCount = targetSpec_.signalSemaphore ? 1u : 0u;
      submit.pSignalSemaphoreInfos = targetSpec_.signalSemaphore ? &signalSemaphore : nullptr;
      resetFrameFenceIndex_ = currentFrame_;
      vkCheck(vkResetFences(device_, 1, &frameFence), "vkResetFences");
      start = phaseStart();
      VkResult const submitted = vkQueueSubmit2(queue_, 1, &submit, frameFence);
      submitMs = phaseMs(start);
      if (submitted != VK_SUCCESS) {
        recoverResetFrameFence();
        vkCheck(submitted, "vkQueueSubmit2");
      }
      markFrameFenceSubmitted(currentFrame_);
      resetFrameFenceIndex_ = kNoResetFrameFence;
      start = phaseStart();
      retireDeferredResourcesAfterSubmit();
      deferredDestroyMs = phaseMs(start);
      if (!targetSpec_.signalSemaphore) {
        // Render-target canvases without an exported signal are synchronous by contract;
        // callers use them for offscreen rasterization and immediate readback.
        vkWaitForFences(device_, 1, &frameFence, VK_TRUE, UINT64_MAX);
        markFrameFenceComplete(currentFrame_);
      }
      pollFrameCapture(false);
      if (canReuseCompletedFrame) {
        renderTargetFrameSignature_ = renderTargetFrameSignature();
        renderTargetFrameCacheValid_ = true;
      } else {
        renderTargetFrameCacheValid_ = false;
      }
      currentFrame_ = (currentFrame_ + 1u) % kMaxFramesInFlight;
      debug::perf::recordPresentedFrame();
      if (traceResize) {
        LAMBDA_RESIZE_TRACE(
            "vulkan-render-target-detail",
            "window=%u target=%ux%u ops=%zu rects=%zu quads=%zu paths=%zu backdropRuns=%zu "
            "waitFrame=%.3fms deferred=%.3fms begin=%.3fms atlas=%.3fms blurPrep=%.3fms "
            "upload=%.3fms textureUploadRecord=%.3fms drawRecord=%.3fms end=%.3fms submit=%.3fms "
            "signal=%d\n",
            handle_,
            targetSpec_.width,
            targetSpec_.height,
            ops_.size(),
            rects_.size(),
            quads_.size(),
            pathVerts_.size(),
            recordStats.backdropRuns,
            waitFrameMs,
            deferredDestroyMs,
            beginMs,
            recordStats.atlasMs,
            recordStats.backdropPrepareMs,
            recordStats.uploadMs,
            recordStats.textureUploadRecordMs,
            recordStats.drawRecordMs,
            endMs,
            submitMs,
            targetSpec_.signalSemaphore ? 1 : 0);
      }
    } catch (std::exception const &e) {
      recoverResetFrameFence();
      renderTargetFrameCacheValid_ = false;
      std::fprintf(stderr, "Lambda Vulkan: render target present failed: %s\n", e.what());
    }
  }
