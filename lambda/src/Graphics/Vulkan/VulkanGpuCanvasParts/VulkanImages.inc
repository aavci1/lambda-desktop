  Texture *ensureImageTexture(VulkanImage const &image) {
    auto it = imageTextures_.find(&image);
    if (it != imageTextures_.end())
      return it->second.get();
    auto tex = std::make_unique<Texture>();
    bool importedNeedsTransition = false;
    try {
      if (image.external || image.ownsGpuResource) {
        tex->image = image.image;
        tex->view = image.view;
        tex->format = image.format;
        tex->layout = image.ownsImportedMemory ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
        tex->width = image.width;
        tex->height = image.height;
        tex->contentGeneration = image.contentGeneration;
        tex->ownsImage = false;
        tex->ownsView = false;
        importedNeedsTransition = image.ownsImportedMemory;
      } else {
        createTexture(*tex, image.width, image.height, image.format);
        tex->contentGeneration = image.contentGeneration;
        queueTextureUpload(*tex, image.pixels.data());
      }
      ensureTextureDescriptor(*tex);
    } catch (...) {
      destroyTexture(*tex);
      throw;
    }
    auto [inserted, ok] = imageTextures_.emplace(&image, std::move(tex));
    (void)ok;
    if (importedNeedsTransition) {
      queueTextureTransition(*inserted->second, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
    }
    return inserted->second.get();
  }

  void destroyDeferredTextures(bool force) {
    for (auto it = pendingTextureDestroys_.begin(); it != pendingTextureDestroys_.end();) {
      if (!force && it->framesRemaining > 0) {
        --it->framesRemaining;
        ++it->successfulSubmits;
      }
      if (force || it->framesRemaining == 0) {
        assert(force || it->successfulSubmits >= kMaxFramesInFlight);
        if (it->texture) {
          destroyTexture(*it->texture);
        }
        it = pendingTextureDestroys_.erase(it);
      } else {
        ++it;
      }
    }
  }

  void destroyDeferredBuffers(bool force) {
    for (auto it = pendingBufferDestroys_.begin(); it != pendingBufferDestroys_.end();) {
      if (!force && it->framesRemaining > 0) {
        --it->framesRemaining;
        ++it->successfulSubmits;
      }
      if (force || it->framesRemaining == 0) {
        assert(force || it->successfulSubmits >= kMaxFramesInFlight);
        if (!force && it->recycleUploadStaging) {
          recycleUploadStagingBuffer(takeBuffer(it->buffer));
        } else {
          destroyBuffer(it->buffer);
        }
        it = pendingBufferDestroys_.erase(it);
      } else {
        ++it;
      }
    }
  }

  void destroyDeferredRecorderResources(bool force) {
    for (auto it = pendingRecorderResourceDestroys_.begin();
         it != pendingRecorderResourceDestroys_.end();) {
      if (!force && it->framesRemaining > 0) {
        --it->framesRemaining;
        ++it->successfulSubmits;
      }
      if (force || it->framesRemaining == 0) {
        assert(force || it->successfulSubmits >= kMaxFramesInFlight);
        destroyVulkanFrameRecorderResourcesNow(it->resources);
        it = pendingRecorderResourceDestroys_.erase(it);
      } else {
        ++it;
      }
    }
  }

  void destroyDeferredOwnedImages(bool force) {
    for (auto it = pendingOwnedImageDestroys_.begin(); it != pendingOwnedImageDestroys_.end();) {
      if (!force && it->framesRemaining > 0) {
        --it->framesRemaining;
        ++it->successfulSubmits;
      }
      if (force || it->framesRemaining == 0) {
        assert(force || it->successfulSubmits >= kMaxFramesInFlight);
        destroyOwnedVulkanImage(*it);
        it = pendingOwnedImageDestroys_.erase(it);
      } else {
        ++it;
      }
    }
  }

  void recycleUploadStagingBuffer(Buffer buffer) {
    if (!buffer.buffer) {
      return;
    }
    constexpr std::size_t kMaxUploadStagingPoolBuffers = 32;
    if (uploadStagingPool_.size() >= kMaxUploadStagingPoolBuffers) {
      destroyBuffer(buffer);
      return;
    }
    uploadStagingPool_.push_back(buffer);
  }

  Buffer takeUploadStagingBuffer(VkDeviceSize size) {
    for (auto it = uploadStagingPool_.begin(); it != uploadStagingPool_.end(); ++it) {
      if (it->buffer && it->capacity >= size) {
        Buffer buffer = *it;
        uploadStagingPool_.erase(it);
        return buffer;
      }
    }
    Buffer buffer{};
    ensureBuffer(buffer, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    return buffer;
  }

  void createTexture(Texture& tex,
                     int width,
                     int height,
                     VkFormat format) {
    tex.width = width;
    tex.height = height;
    tex.format = format == VK_FORMAT_UNDEFINED ? VK_FORMAT_R8G8B8A8_UNORM : format;
    auto image = vkStructure<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = tex.format;
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
    auto view = vkStructure<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    view.image = tex.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = tex.format;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.layerCount = 1;
    vkCheck(vkCreateImageView(device_, &view, nullptr, &tex.view), "vkCreateImageView");
  }

  void queueTextureUpload(Texture& tex, void const* pixels) {
    if (!pixels || tex.width <= 0 || tex.height <= 0)
      return;
    VkDeviceSize size = static_cast<VkDeviceSize>(tex.width) * tex.height * 4u;
    for (auto it = pendingTextureUploads_.begin(); it != pendingTextureUploads_.end();) {
      if (it->texture == &tex) {
        recycleUploadStagingBuffer(takeBuffer(it->staging));
        it = pendingTextureUploads_.erase(it);
      } else {
        ++it;
      }
    }
    PendingTextureUpload uploadJob{};
    uploadJob.texture = &tex;
    uploadJob.width = static_cast<std::uint32_t>(tex.width);
    uploadJob.height = static_cast<std::uint32_t>(tex.height);
    uploadJob.staging = takeUploadStagingBuffer(size);
    upload(uploadJob.staging, pixels, static_cast<std::size_t>(size));
    pendingTextureUploads_.push_back(std::move(uploadJob));
  }

  void queueTextureRegionUpload(Texture& tex,
                                std::uint32_t x,
                                std::uint32_t y,
                                std::uint32_t width,
                                std::uint32_t height,
                                void const* pixels,
                                std::uint32_t sourceBytesPerRow = 0) {
    if (!pixels || tex.width <= 0 || tex.height <= 0 || width == 0 || height == 0)
      return;
    if (x > static_cast<std::uint32_t>(tex.width) || y > static_cast<std::uint32_t>(tex.height) ||
        width > static_cast<std::uint32_t>(tex.width) - x ||
        height > static_cast<std::uint32_t>(tex.height) - y)
      return;
    for (auto const& uploadJob : pendingTextureUploads_) {
      if (uploadJob.texture == &tex && uploadJob.x == 0 && uploadJob.y == 0 &&
          uploadJob.width == static_cast<std::uint32_t>(tex.width) &&
          uploadJob.height == static_cast<std::uint32_t>(tex.height)) {
        return;
      }
    }
    PendingTextureUpload uploadJob{};
    uploadJob.texture = &tex;
    uploadJob.x = x;
    uploadJob.y = y;
    uploadJob.width = width;
    uploadJob.height = height;
    VkDeviceSize size = static_cast<VkDeviceSize>(width) * height * 4u;
    std::size_t const rowBytes = static_cast<std::size_t>(width) * 4u;
    std::size_t const stride = sourceBytesPerRow == 0 ? rowBytes : sourceBytesPerRow;
    if (stride < rowBytes) {
      return;
    }
    uploadJob.staging = takeUploadStagingBuffer(size);
    uploadRows(uploadJob.staging, pixels, rowBytes, height, stride);
    pendingTextureUploads_.push_back(std::move(uploadJob));
  }

  void queueTextureTransition(Texture& tex, VkImageLayout layout) {
    if (!tex.image || tex.layout == layout)
      return;
    for (PendingTextureTransition& transition : pendingTextureTransitions_) {
      if (transition.texture == &tex) {
        transition.layout = layout;
        return;
      }
    }
    pendingTextureTransitions_.push_back(PendingTextureTransition{&tex, layout});
  }

  void recordTextureUpload(VkCommandBuffer cmd, Texture& tex, PendingTextureUpload& uploadJob) {
    Buffer& staging = uploadJob.staging;
    if (!staging.buffer || tex.width <= 0 || tex.height <= 0)
      return;
    std::uint32_t const copyWidth = uploadJob.width > 0 ? uploadJob.width : static_cast<std::uint32_t>(tex.width);
    std::uint32_t const copyHeight = uploadJob.height > 0 ? uploadJob.height : static_cast<std::uint32_t>(tex.height);
    if (uploadJob.x > static_cast<std::uint32_t>(tex.width) ||
        uploadJob.y > static_cast<std::uint32_t>(tex.height) ||
        copyWidth > static_cast<std::uint32_t>(tex.width) - uploadJob.x ||
        copyHeight > static_cast<std::uint32_t>(tex.height) - uploadJob.y) {
      return;
    }
    transition(cmd, tex, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageOffset = {static_cast<std::int32_t>(uploadJob.x), static_cast<std::int32_t>(uploadJob.y), 0};
    copy.imageExtent = {copyWidth, copyHeight, 1};
    vkCmdCopyBufferToImage(cmd, staging.buffer, tex.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    transition(cmd, tex, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
  }

  void recordPendingTextureUploads(VkCommandBuffer cmd) {
    for (auto& uploadJob : pendingTextureUploads_) {
      if (uploadJob.texture) {
        recordTextureUpload(cmd, *uploadJob.texture, uploadJob);
      }
      pendingBufferDestroys_.push_back(PendingBufferDestroy{takeBuffer(uploadJob.staging),
                                                            kMaxFramesInFlight + 1u,
                                                            0u,
                                                            true});
    }
    pendingTextureUploads_.clear();
  }

  void destroyPendingTextureUploads() {
    for (PendingTextureUpload& uploadJob : pendingTextureUploads_) {
      destroyBuffer(uploadJob.staging);
    }
    pendingTextureUploads_.clear();
  }

  void recordPendingTextureTransitions(VkCommandBuffer cmd) {
    for (PendingTextureTransition const& pending : pendingTextureTransitions_) {
      if (pending.texture) {
        transition(cmd, *pending.texture, pending.layout);
      }
    }
    pendingTextureTransitions_.clear();
  }

  void retireDeferredResourcesAfterSubmit() {
    destroyDeferredTextures(false);
    destroyDeferredBuffers(false);
    destroyDeferredRecorderResources(false);
    destroyDeferredOwnedImages(false);
    retireSwapchains(false);
  }

  bool swapchainImageStateReadyForAcquire() const {
    if (swapchainImages_.empty()) {
      return false;
    }
    if (imageInFlightFences_.size() != swapchainImages_.size() ||
        imageRenderFinished_.size() != swapchainImages_.size()) {
      return false;
    }
    return std::all_of(imageRenderFinished_.begin(), imageRenderFinished_.end(), [](VkSemaphore semaphore) {
      return semaphore != VK_NULL_HANDLE;
    });
  }

  VkFormat backdropRenderTargetFormat() const {
    VkFormat constexpr preferred = VK_FORMAT_R16G16B16A16_SFLOAT;
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(physical_, preferred, &properties);
    VkFormatFeatureFlags constexpr required = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                                              VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                              VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
                                              VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
                                              VK_FORMAT_FEATURE_BLIT_DST_BIT;
    if ((properties.optimalTilingFeatures & required) == required) return preferred;
    return surfaceFormat_.format == VK_FORMAT_UNDEFINED ? VK_FORMAT_B8G8R8A8_UNORM : surfaceFormat_.format;
  }

  void createRenderTargetTexture(Texture &tex,
                                 int width,
                                 int height,
                                 VkFormat requestedFormat = VK_FORMAT_UNDEFINED) {
    tex.width = width;
    tex.height = height;
    tex.format = requestedFormat == VK_FORMAT_UNDEFINED
                     ? (surfaceFormat_.format == VK_FORMAT_UNDEFINED ? VK_FORMAT_B8G8R8A8_UNORM : surfaceFormat_.format)
                     : requestedFormat;
    auto image = vkStructure<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = tex.format;
    image.extent = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                  VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                  VK_IMAGE_USAGE_SAMPLED_BIT;
    image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo allocation{};
    allocation.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    vkCheck(vmaCreateImage(allocator_, &image, &allocation, &tex.image, &tex.allocation, nullptr),
            "vmaCreateImage");
    auto view = vkStructure<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    view.image = tex.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = image.format;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.layerCount = 1;
    vkCheck(vkCreateImageView(device_, &view, nullptr, &tex.view), "vkCreateImageView");
    tex.layout = VK_IMAGE_LAYOUT_UNDEFINED;
  }

  void ensureTextureDescriptor(Texture &tex) {
    if (tex.descriptor)
      return;
    auto alloc = vkStructure<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    alloc.descriptorPool = resources().descriptorPool;
    alloc.descriptorSetCount = 1;
    VkDescriptorSetLayout const layout = resources().textureDescriptorLayout;
    alloc.pSetLayouts = &layout;
    vkCheck(vkAllocateDescriptorSets(device_, &alloc, &tex.descriptor), "vkAllocateDescriptorSets");
    VkDescriptorImageInfo ii{resources().sampler, tex.view, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL};
    auto write = vkStructure<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    write.dstSet = tex.descriptor;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &ii;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
  }

  void destroyTexture(Texture &tex) {
    if (tex.descriptor && resources().descriptorPool)
      vkFreeDescriptorSets(device_, resources().descriptorPool, 1, &tex.descriptor);
    if (tex.view && tex.ownsView)
      vkDestroyImageView(device_, tex.view, nullptr);
    if (tex.image && tex.ownsImage)
      vmaDestroyImage(allocator_, tex.image, tex.allocation);
    tex = {};
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
    auto barrier = vkStructure<VkImageMemoryBarrier2>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
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
    auto dependency = vkStructure<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependency);
  }

  void transition(VkCommandBuffer cmd, Texture &texture, VkImageLayout newLayout) {
    transition(cmd, texture.image, texture.layout, newLayout);
    texture.layout = newLayout;
  }

  bool frameCaptureTraceEnabled() const {
    char const* raw = std::getenv("LAMBDA_VULKAN_FRAME_CAPTURE_TRACE");
    return raw && *raw && std::strcmp(raw, "0") != 0 && std::strcmp(raw, "false") != 0 &&
           std::strcmp(raw, "FALSE") != 0;
  }

  bool readbackFenceReady(VkFence fence, bool wait, char const* label) {
    if (!fence) {
      return true;
    }
    if (wait) {
      vkCheck(vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX), label);
      return true;
    }
    VkResult const status = vkGetFenceStatus(device_, fence);
    if (status == VK_SUCCESS) {
      return true;
    }
    if (status == VK_NOT_READY) {
      return false;
    }
    vkCheck(status, label);
    return false;
  }

  void writeDebugScreenshotIfRequested(VkCommandBuffer commandBuffer, VkImage source, VkFence frameFence) {
    if (debugScreenshotWritten_)
      return;
    static char const *const path = std::getenv("LAMBDA_DEBUG_SCREENSHOT_PATH");
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
    pendingScreenshotFence_ = frameFence;
    debugScreenshotWritten_ = true;
  }

  void captureFrameIfRequested(VkCommandBuffer commandBuffer,
                               VkImage source,
                               VkFence frameFence,
                               bool awaitingExternalSubmit = false) {
    if (!frameCaptureRequested_) {
      return;
    }
    frameCaptureRequested_ = false;
    if (pendingFrameCaptureBuffer_.buffer) {
      destroyBuffer(pendingFrameCaptureBuffer_);
      pendingFrameCaptureFence_ = VK_NULL_HANDLE;
      pendingFrameCaptureExternal_ = false;
      pendingFrameCaptureAwaitingExternalSubmit_ = false;
      pendingFrameCaptureTracePolls_ = 0;
    }
    Buffer staging{};
    VkDeviceSize size = static_cast<VkDeviceSize>(framebufferWidth_) * framebufferHeight_ * 4u;
    ensureBuffer(staging, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {static_cast<std::uint32_t>(framebufferWidth_),
                        static_cast<std::uint32_t>(framebufferHeight_), 1};
    vkCmdCopyImageToBuffer(commandBuffer, source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.buffer, 1, &copy);
    pendingFrameCaptureBuffer_ = staging;
    pendingFrameCaptureSize_ = size;
    pendingFrameCaptureWidth_ = static_cast<std::uint32_t>(framebufferWidth_);
    pendingFrameCaptureHeight_ = static_cast<std::uint32_t>(framebufferHeight_);
    pendingFrameCaptureBgra_ = surfaceFormat_.format == VK_FORMAT_B8G8R8A8_UNORM ||
                               surfaceFormat_.format == VK_FORMAT_B8G8R8A8_SRGB;
    pendingFrameCaptureFence_ = frameFence;
    pendingFrameCaptureExternal_ = awaitingExternalSubmit;
    pendingFrameCaptureAwaitingExternalSubmit_ = awaitingExternalSubmit;
    pendingFrameCaptureTracePolls_ = 0;
    if (frameCaptureTraceEnabled()) {
      std::fprintf(stderr,
                   "Lambda Vulkan: frame capture queued external=%d awaitingSubmit=%d fence=%d size=%ux%u\n",
                   awaitingExternalSubmit ? 1 : 0,
                   pendingFrameCaptureAwaitingExternalSubmit_ ? 1 : 0,
                   frameFence ? 1 : 0,
                   pendingFrameCaptureWidth_,
                   pendingFrameCaptureHeight_);
    }
  }

  bool pollFrameCapture(bool wait) {
    if (!pendingFrameCaptureBuffer_.buffer || pendingFrameCaptureWidth_ == 0 || pendingFrameCaptureHeight_ == 0) {
      return false;
    }
    ++pendingFrameCaptureTracePolls_;
    if (pendingFrameCaptureAwaitingExternalSubmit_) {
      if (frameCaptureTraceEnabled() && pendingFrameCaptureTracePolls_ <= 8) {
        std::fprintf(stderr, "Lambda Vulkan: frame capture poll awaiting external submit poll=%d\n",
                     pendingFrameCaptureTracePolls_);
      }
      return false;
    }
    if (pendingFrameCaptureExternal_ && pendingFrameCaptureFence_ == VK_NULL_HANDLE) {
      if (frameCaptureTraceEnabled()) {
        std::fprintf(stderr, "Lambda Vulkan: frame capture external readback has no fence; waiting queue idle\n");
      }
      vkCheck(vkQueueWaitIdle(queue_), "vkQueueWaitIdle(frameCapture)");
    }
    if (!readbackFenceReady(pendingFrameCaptureFence_, wait, "vkGetFenceStatus(frameCapture)")) {
      if (frameCaptureTraceEnabled() && pendingFrameCaptureTracePolls_ <= 8) {
        std::fprintf(stderr, "Lambda Vulkan: frame capture poll fence not ready poll=%d wait=%d\n",
                     pendingFrameCaptureTracePolls_,
                     wait ? 1 : 0);
      }
      return false;
    }
    if (frameCaptureTraceEnabled()) {
      std::fprintf(stderr, "Lambda Vulkan: frame capture readback ready poll=%d\n",
                   pendingFrameCaptureTracePolls_);
    }
    void* mapped = nullptr;
    vkCheck(vmaInvalidateAllocation(allocator_,
                                    pendingFrameCaptureBuffer_.allocation,
                                    0,
                                    pendingFrameCaptureSize_),
            "vmaInvalidateAllocation");
    vkCheck(vmaMapMemory(allocator_, pendingFrameCaptureBuffer_.allocation, &mapped), "vmaMapMemory");
    auto const* source = static_cast<std::uint8_t const*>(mapped);
    capturedFrameBytes_.resize(static_cast<std::size_t>(pendingFrameCaptureWidth_) *
                               pendingFrameCaptureHeight_ * 4u);
    if (pendingFrameCaptureBgra_) {
      std::memcpy(capturedFrameBytes_.data(), source, capturedFrameBytes_.size());
    } else {
      for (std::size_t i = 0; i < capturedFrameBytes_.size(); i += 4u) {
        capturedFrameBytes_[i + 0u] = source[i + 2u];
        capturedFrameBytes_[i + 1u] = source[i + 1u];
        capturedFrameBytes_[i + 2u] = source[i + 0u];
        capturedFrameBytes_[i + 3u] = source[i + 3u];
      }
    }
    vmaUnmapMemory(allocator_, pendingFrameCaptureBuffer_.allocation);
    destroyBuffer(pendingFrameCaptureBuffer_);
    capturedFrameWidth_ = pendingFrameCaptureWidth_;
    capturedFrameHeight_ = pendingFrameCaptureHeight_;
    capturedFrameAvailable_ = true;
    pendingFrameCaptureSize_ = 0;
    pendingFrameCaptureWidth_ = 0;
    pendingFrameCaptureHeight_ = 0;
    pendingFrameCaptureBgra_ = true;
    pendingFrameCaptureFence_ = VK_NULL_HANDLE;
    pendingFrameCaptureExternal_ = false;
    pendingFrameCaptureAwaitingExternalSubmit_ = false;
    pendingFrameCaptureTracePolls_ = 0;
    return true;
  }

  bool pollScreenshot(bool wait) {
    if (!pendingScreenshotBuffer_.buffer || pendingScreenshotPath_.empty())
      return false;
    if (!readbackFenceReady(pendingScreenshotFence_, wait, "vkGetFenceStatus(debugScreenshot)")) {
      return false;
    }
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
    pendingScreenshotFence_ = VK_NULL_HANDLE;
    return true;
  }

  void pollReadbacks(bool wait) {
    pollFrameCapture(wait);
    pollScreenshot(wait);
  }

  VkCompositeAlphaFlagBitsKHR chooseCompositeAlpha(VkCompositeAlphaFlagsKHR supported) const {
    auto supports = [supported](VkCompositeAlphaFlagBitsKHR mode) {
      return (supported & mode) == mode;
    };
    if (!transparentSurface_) {
      if (supports(VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)) {
        return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
      }
      if (supports(VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)) {
        return VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
      }
    } else {
      if (supports(VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)) {
        return VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
      }
      if (supports(VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR)) {
        return VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
      }
      if (supports(VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)) {
        return VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
      }
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  }
