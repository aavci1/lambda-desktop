
  struct VulkanCommandState {
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSet descriptor0 = VK_NULL_HANDLE;
    VkDescriptorSet descriptor1 = VK_NULL_HANDLE;
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    bool pushConstantsValid = false;
    float translationX = 0.f;
    float translationY = 0.f;
  };

  void bindPipeline(VkCommandBuffer commandBuffer,
                    VulkanCommandState &state,
                    VkPipeline pipeline,
                    VkPipelineLayout layout) {
    if (state.pipeline != pipeline) {
      vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
      state.pipeline = pipeline;
    }
    if (state.layout != layout) {
      state.layout = layout;
      state.descriptor0 = VK_NULL_HANDLE;
      state.descriptor1 = VK_NULL_HANDLE;
      state.pushConstantsValid = false;
    }
  }

  void bindDescriptorSet(VkCommandBuffer commandBuffer,
                         VulkanCommandState &state,
                         VkPipelineLayout layout,
                         std::uint32_t index,
                         VkDescriptorSet descriptor) {
    VkDescriptorSet *cached = index == 0 ? &state.descriptor0 : &state.descriptor1;
    if (*cached == descriptor) {
      return;
    }
    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            layout,
                            index,
                            1,
                            &descriptor,
                            0,
                            nullptr);
    *cached = descriptor;
  }

  void pushDrawConstants(VkCommandBuffer commandBuffer,
                         VulkanCommandState &state,
                         VkPipelineLayout layout,
                         float translationX = 0.f,
                         float translationY = 0.f) {
    if (state.pushConstantsValid &&
        std::abs(state.translationX - translationX) <= 1e-4f &&
        std::abs(state.translationY - translationY) <= 1e-4f) {
      return;
    }
    VulkanDrawPushConstants const push = drawPushConstants(translationX, translationY);
    vkCmdPushConstants(commandBuffer, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
    state.pushConstantsValid = true;
    state.translationX = translationX;
    state.translationY = translationY;
  }

  void bindVertexBuffer(VkCommandBuffer commandBuffer, VulkanCommandState &state, VkBuffer buffer) {
    if (state.vertexBuffer == buffer) {
      return;
    }
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &buffer, &offset);
    state.vertexBuffer = buffer;
  }

  void drawRectRange(VkCommandBuffer commandBuffer, VulkanCommandState &state, std::uint32_t first, std::uint32_t count,
                     VkDescriptorSet descriptor = VK_NULL_HANDLE,
                     float translationX = 0.f, float translationY = 0.f) {
    if (count == 0)
      return;
    auto const &res = resources();
    auto const& geometry = frameGeometryResources();
    VkDescriptorSet const storageDescriptor = descriptor ? descriptor : geometry.rectDescriptorSet;
    if (!storageDescriptor)
      return;
    bindPipeline(commandBuffer, state, res.rectPipeline, res.rectPipelineLayout);
    bindDescriptorSet(commandBuffer, state, res.rectPipelineLayout, 0, storageDescriptor);
    pushDrawConstants(commandBuffer, state, res.rectPipelineLayout, translationX, translationY);
    vkCmdDraw(commandBuffer, 6, count, 0, first);
  }

  void drawCalloutRange(VkCommandBuffer commandBuffer,
                        VulkanCommandState &state,
                        std::uint32_t first,
                        std::uint32_t count,
                        float translationX = 0.f,
                        float translationY = 0.f) {
    if (count == 0)
      return;
    auto const &res = resources();
    auto const& geometry = frameGeometryResources();
    if (!geometry.calloutDescriptorSet)
      return;
    bindPipeline(commandBuffer, state, res.calloutPipeline, res.calloutPipelineLayout);
    bindDescriptorSet(commandBuffer, state, res.calloutPipelineLayout, 0, geometry.calloutDescriptorSet);
    pushDrawConstants(commandBuffer, state, res.calloutPipelineLayout, translationX, translationY);
    vkCmdDraw(commandBuffer, 6, count, 0, first);
  }

  void drawPathRange(VkCommandBuffer commandBuffer, VulkanCommandState &state, std::uint32_t first, std::uint32_t count,
                     VkBuffer vertexBuffer = VK_NULL_HANDLE,
                     float translationX = 0.f, float translationY = 0.f) {
    if (count == 0)
      return;
    auto const& geometry = frameGeometryResources();
    VkBuffer const buffer = vertexBuffer ? vertexBuffer : geometry.pathBuffer.buffer;
    if (!buffer)
      return;
    auto const &res = resources();
    bindPipeline(commandBuffer, state, res.pathPipeline, res.pathPipelineLayout);
    pushDrawConstants(commandBuffer, state, res.pathPipelineLayout, translationX, translationY);
    bindVertexBuffer(commandBuffer, state, buffer);
    vkCmdDraw(commandBuffer, count, 1, first, 0);
  }

  void drawImageRange(VkCommandBuffer commandBuffer, VulkanCommandState &state, Texture *texture, std::uint32_t first, std::uint32_t count,
                      VkDescriptorSet descriptor = VK_NULL_HANDLE,
                      float translationX = 0.f, float translationY = 0.f,
                      bool premultipliedAlpha = false) {
    if (!texture || !texture->descriptor || count == 0)
      return;
    auto const &res = resources();
    auto const& geometry = frameGeometryResources();
    VkDescriptorSet const storageDescriptor = descriptor ? descriptor : geometry.quadDescriptorSet;
    if (!storageDescriptor)
      return;
    bindPipeline(commandBuffer,
                 state,
                 premultipliedAlpha ? res.imageUnpremultiplyPipeline : res.imagePipeline,
                 res.imagePipelineLayout);
    bindDescriptorSet(commandBuffer, state, res.imagePipelineLayout, 0, storageDescriptor);
    pushDrawConstants(commandBuffer, state, res.imagePipelineLayout, translationX, translationY);
    bindDescriptorSet(commandBuffer, state, res.imagePipelineLayout, 1, texture->descriptor);
    vkCmdDraw(commandBuffer, 6, count, 0, first);
  }

  void drawBackdropRange(VkCommandBuffer commandBuffer, VulkanCommandState &state, Texture *texture, std::uint32_t first, std::uint32_t count,
                         VkDescriptorSet descriptor = VK_NULL_HANDLE,
                         float translationX = 0.f, float translationY = 0.f) {
    if (!texture || !texture->descriptor || count == 0)
      return;
    auto const &res = resources();
    auto const& geometry = frameGeometryResources();
    VkDescriptorSet const storageDescriptor = descriptor ? descriptor : geometry.quadDescriptorSet;
    if (!storageDescriptor)
      return;
    bindPipeline(commandBuffer, state, res.backdropPipeline, res.backdropPipelineLayout);
    bindDescriptorSet(commandBuffer, state, res.backdropPipelineLayout, 0, storageDescriptor);
    pushDrawConstants(commandBuffer, state, res.backdropPipelineLayout, translationX, translationY);
    bindDescriptorSet(commandBuffer, state, res.backdropPipelineLayout, 1, texture->descriptor);
    vkCmdDraw(commandBuffer, 6, count, 0, first);
  }

  struct PixelRect {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    bool valid() const { return width > 0 && height > 0; }
  };

  PixelRect pixelRectForLogicalClip(Rect clip) const {
    return pixelRectForLogicalClip(clip,
                                   VkExtent2D{
                                       static_cast<std::uint32_t>(std::max(1, framebufferWidth_)),
                                       static_cast<std::uint32_t>(std::max(1, framebufferHeight_)),
                                   });
  }

  VkExtent2D currentFramebufferExtent() const {
    return VkExtent2D{
        static_cast<std::uint32_t>(std::max(1, framebufferWidth_)),
        static_cast<std::uint32_t>(std::max(1, framebufferHeight_)),
    };
  }

  PixelRect currentFramebufferPixelRect() const {
    VkExtent2D const extent = currentFramebufferExtent();
    return PixelRect{0u, 0u, extent.width, extent.height};
  }

  PixelRect pixelRectForLogicalClip(Rect clip, VkExtent2D extent) const {
    float const renderWidth = static_cast<float>(std::max(1u, extent.width));
    float const renderHeight = static_cast<float>(std::max(1u, extent.height));
    float const scaleX = renderWidth / std::max(1.f, static_cast<float>(width_));
    float const scaleY = renderHeight / std::max(1.f, static_cast<float>(height_));
    float const x0f = std::clamp(std::floor(clip.x * scaleX), 0.f, renderWidth);
    float const y0f = std::clamp(std::floor(clip.y * scaleY), 0.f, renderHeight);
    float const x1f = std::clamp(std::ceil((clip.x + clip.width) * scaleX), 0.f, renderWidth);
    float const y1f = std::clamp(std::ceil((clip.y + clip.height) * scaleY), 0.f, renderHeight);
    std::uint32_t const x0 = static_cast<std::uint32_t>(x0f);
    std::uint32_t const y0 = static_cast<std::uint32_t>(y0f);
    std::uint32_t const x1 = static_cast<std::uint32_t>(x1f);
    std::uint32_t const y1 = static_cast<std::uint32_t>(y1f);
    return PixelRect{x0, y0, x1 > x0 ? x1 - x0 : 0u, y1 > y0 ? y1 - y0 : 0u};
  }

  void drawBackdropBlurPass(VkCommandBuffer commandBuffer,
                            VulkanCommandState &state,
                            Texture *texture,
                            std::uint32_t first,
                            Rect const &clip,
                            VkExtent2D targetExtent) {
    if (!texture || !texture->descriptor)
      return;
    setViewportScissor(commandBuffer, clip, targetExtent);
    auto const &res = resources();
    auto const& geometry = frameGeometryResources();
    if (!geometry.quadDescriptorSet)
      return;
    bindPipeline(commandBuffer, state, res.backdropBlurPipeline, res.backdropPipelineLayout);
    bindDescriptorSet(commandBuffer, state, res.backdropPipelineLayout, 0, geometry.quadDescriptorSet);
    pushDrawConstants(commandBuffer, state, res.backdropPipelineLayout);
    bindDescriptorSet(commandBuffer, state, res.backdropPipelineLayout, 1, texture->descriptor);
    vkCmdDraw(commandBuffer, 6, 1, 0, first);
  }

  void copyTargetToBackdropScene(VkCommandBuffer commandBuffer,
                                 VkImage targetImage,
                                 VkImageLayout &targetLayout,
                                 Rect const &clip) {
    transition(commandBuffer, targetImage, targetLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    targetLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    transition(commandBuffer, backdropSceneTexture_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    PixelRect const srcRect = pixelRectForLogicalClip(clip);
    VkExtent2D const backdropExtent{
        static_cast<std::uint32_t>(std::max(1, backdropSceneTexture_.width)),
        static_cast<std::uint32_t>(std::max(1, backdropSceneTexture_.height)),
    };
    PixelRect const dstRect = pixelRectForLogicalClip(clip, backdropExtent);
    if (!srcRect.valid() || !dstRect.valid()) {
      transition(commandBuffer, backdropSceneTexture_, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
      ensureTextureDescriptor(backdropSceneTexture_);
      return;
    }
    auto blit = vkStructure<VkImageBlit2>(VK_STRUCTURE_TYPE_IMAGE_BLIT_2);
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[0] = {static_cast<std::int32_t>(srcRect.x), static_cast<std::int32_t>(srcRect.y), 0};
    blit.srcOffsets[1] = {static_cast<std::int32_t>(srcRect.x + srcRect.width),
                          static_cast<std::int32_t>(srcRect.y + srcRect.height),
                          1};
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.layerCount = 1;
    blit.dstOffsets[0] = {static_cast<std::int32_t>(dstRect.x), static_cast<std::int32_t>(dstRect.y), 0};
    blit.dstOffsets[1] = {static_cast<std::int32_t>(dstRect.x + dstRect.width),
                          static_cast<std::int32_t>(dstRect.y + dstRect.height),
                          1};
    auto blitInfo = vkStructure<VkBlitImageInfo2>(VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2);
    blitInfo.srcImage = targetImage;
    blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    blitInfo.dstImage = backdropSceneTexture_.image;
    blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    blitInfo.regionCount = 1;
    blitInfo.pRegions = &blit;
    blitInfo.filter = VK_FILTER_LINEAR;
    vkCmdBlitImage2(commandBuffer, &blitInfo);

    transition(commandBuffer, backdropSceneTexture_, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
    ensureTextureDescriptor(backdropSceneTexture_);
  }

  Texture *blurBackdropScene(VkCommandBuffer commandBuffer,
                             BackdropBlurRun const &run,
                             VkClearValue const &clear,
                             CachedBackdropBlur &cacheEntry,
                             std::uint64_t signature) {
    Texture *blurSource = &backdropSceneTexture_;
    Texture *blurDestination = &cacheEntry.texture;
    VkExtent2D const blurExtent = backdropTextureExtent();
    PixelRect const renderArea = pixelRectForLogicalClip(run.clip, blurExtent);
    if (!renderArea.valid()) {
      cacheEntry.valid = false;
      return nullptr;
    }
    PixelRect const sourceArea = pixelRectForLogicalClip(run.clip);
    std::uint64_t const blurPixels = static_cast<std::uint64_t>(renderArea.width) * renderArea.height;
    std::uint64_t const copyPixels = static_cast<std::uint64_t>(sourceArea.width) * sourceArea.height;
    debug::perf::recordBackdropBlurRun(copyPixels,
                                       blurPixels * 2u * static_cast<std::uint64_t>(kBackdropBlurIterations),
                                       2u * static_cast<std::uint64_t>(kBackdropBlurIterations));
    VulkanCommandState blurState{};
    for (int i = 0; i < kBackdropBlurIterations; ++i) {
      transition(commandBuffer, backdropScratchTexture_, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
      beginColorRendering(commandBuffer,
                          backdropScratchTexture_.view,
                          blurExtent,
                          clear,
                          VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                          renderArea);
      drawBackdropBlurPass(commandBuffer, blurState, blurSource, run.horizontalQuad, run.clip, blurExtent);
      vkCmdEndRendering(commandBuffer);
      transition(commandBuffer, backdropScratchTexture_, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
      ensureTextureDescriptor(backdropScratchTexture_);

      transition(commandBuffer, *blurDestination, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
      beginColorRendering(commandBuffer,
                          blurDestination->view,
                          blurExtent,
                          clear,
                          VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                          renderArea);
      drawBackdropBlurPass(commandBuffer, blurState, &backdropScratchTexture_, run.verticalQuad, run.clip, blurExtent);
      vkCmdEndRendering(commandBuffer);
      transition(commandBuffer, *blurDestination, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);
      ensureTextureDescriptor(*blurDestination);
      blurSource = blurDestination;
    }
    cacheEntry.signature = signature;
    cacheEntry.valid = true;
    return blurDestination;
  }

  void beginTargetRendering(VkCommandBuffer commandBuffer,
                            VkImage targetImage,
                            VkImageView targetView,
                            VkImageLayout &targetLayout,
                            VkClearValue const &clear,
                            VkAttachmentLoadOp loadOp) {
    transition(commandBuffer, targetImage, targetLayout, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
    targetLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
    beginColorRendering(commandBuffer,
                        targetView,
                        swapExtent_,
                        clear,
                        loadOp,
                        currentFramebufferPixelRect());
  }

  void drawOpsWithStackedBackdropBlur(VkCommandBuffer commandBuffer,
                                      VkImage targetImage,
                                      VkImageView targetView,
                                      VkImageLayout &targetLayout,
                                      VkClearValue const &clear,
                                      VkAttachmentLoadOp initialLoadOp,
                                      std::vector<BackdropBlurRun> const &runs) {
    auto const start = std::chrono::steady_clock::now();
    std::uint64_t replayedOps = 0;
    beginTargetRendering(commandBuffer, targetImage, targetView, targetLayout, clear, initialLoadOp);
    std::size_t cursor = 0;
    for (std::size_t runIndex = 0; runIndex < runs.size(); ++runIndex) {
      BackdropBlurRun const &run = runs[runIndex];
      if (run.start > cursor) {
        drawOps(commandBuffer, cursor, run.start);
        replayedOps += run.start - cursor;
      }

      CachedBackdropBlur &cacheEntry = ensureBackdropBlurCacheEntry(runIndex);
      std::uint64_t const signature = backdropBlurSignature(run, runIndex);
      bool const cacheHit = cacheEntry.valid && cacheEntry.signature == signature;
      debug::perf::recordBackdropBlurCacheLookup(cacheHit);
      Texture *blurTexture = cacheHit ? &cacheEntry.texture : nullptr;
      if (!blurTexture) {
        vkCmdEndRendering(commandBuffer);
        copyTargetToBackdropScene(commandBuffer, targetImage, targetLayout, run.clip);
        blurTexture = blurBackdropScene(commandBuffer, run, clear, cacheEntry, signature);
        beginTargetRendering(commandBuffer, targetImage, targetView, targetLayout, clear, VK_ATTACHMENT_LOAD_OP_LOAD);
      }
      drawOps(commandBuffer, run.start, run.end, blurTexture);
      replayedOps += run.end - run.start;
      cursor = run.end;
    }

    if (cursor < ops_.size()) {
      drawOps(commandBuffer, cursor, ops_.size());
      replayedOps += ops_.size() - cursor;
    }
    vkCmdEndRendering(commandBuffer);
    debug::perf::recordVulkanStackedBlur(std::chrono::steady_clock::now() - start, replayedOps);
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
               Texture *backdropSource = nullptr,
               VkExtent2D renderExtent = {}) {
    auto const recordStart = std::chrono::steady_clock::now();
    std::size_t const opEnd = std::min(end, ops_.size());
    if (renderExtent.width == 0 || renderExtent.height == 0) {
      renderExtent = currentFramebufferExtent();
    }
    bool hasScissor = false;
    Rect currentScissor{};
    VulkanCommandState commandState{};
    std::uint64_t visited = 0;
    std::uint64_t submitted = 0;
    std::uint64_t scissorChanges = 0;
    for (std::size_t index = std::min(start, opEnd); index < opEnd; ++index) {
      DrawOp const &op = ops_[index];
      ++visited;
      if (op.clip.width <= 0.f || op.clip.height <= 0.f) {
        continue;
      }
      if (!hasScissor || !sameRect(currentScissor, op.clip)) {
        setViewportScissor(commandBuffer, op.clip, renderExtent);
        currentScissor = op.clip;
        hasScissor = true;
        ++scissorChanges;
      }
      ++submitted;
      switch (op.kind) {
      case DrawOp::Kind::Rect:
        drawRectRange(commandBuffer, commandState, op.first, op.count, op.externalStorageDescriptor,
                      op.externalTranslationX, op.externalTranslationY);
        break;
      case DrawOp::Kind::Callout:
        drawCalloutRange(commandBuffer,
                         commandState,
                         op.first,
                         op.count,
                         op.externalTranslationX,
                         op.externalTranslationY);
        break;
      case DrawOp::Kind::Path:
        drawPathRange(commandBuffer, commandState, op.first, op.count, op.externalVertexBuffer,
                      op.externalTranslationX, op.externalTranslationY);
        break;
      case DrawOp::Kind::Image:
        drawImageRange(commandBuffer, commandState, op.texture, op.first, op.count, op.externalStorageDescriptor,
                       op.externalTranslationX, op.externalTranslationY, op.premultipliedAlpha);
        break;
      case DrawOp::Kind::BackdropBlur:
        drawBackdropRange(commandBuffer, commandState, backdropSource, op.first, op.count, op.externalStorageDescriptor,
                          op.externalTranslationX, op.externalTranslationY);
        break;
      }
    }
    debug::perf::recordVulkanDrawOps(std::chrono::steady_clock::now() - recordStart,
                                     visited,
                                     submitted,
                                     scissorChanges);
  }

  void beginColorRendering(VkCommandBuffer commandBuffer, VkImageView view, VkExtent2D extent,
                           VkClearValue const &clear, VkAttachmentLoadOp loadOp) {
    beginColorRendering(commandBuffer,
                        view,
                        extent,
                        clear,
                        loadOp,
                        PixelRect{0u, 0u, extent.width, extent.height});
  }

  void beginColorRendering(VkCommandBuffer commandBuffer,
                           VkImageView view,
                           VkExtent2D extent,
                           VkClearValue const &clear,
                           VkAttachmentLoadOp loadOp,
                           PixelRect renderArea) {
    auto color = vkStructure<VkRenderingAttachmentInfo>(VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO);
    color.imageView = view;
    color.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
    color.loadOp = loadOp;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue = clear;
    auto rendering = vkStructure<VkRenderingInfo>(VK_STRUCTURE_TYPE_RENDERING_INFO);
    std::uint32_t const x0 = std::min(renderArea.x, extent.width);
    std::uint32_t const y0 = std::min(renderArea.y, extent.height);
    std::uint32_t const x1 = std::min(renderArea.x + renderArea.width, extent.width);
    std::uint32_t const y1 = std::min(renderArea.y + renderArea.height, extent.height);
    rendering.renderArea.offset = {static_cast<std::int32_t>(x0), static_cast<std::int32_t>(y0)};
    rendering.renderArea.extent = {x1 > x0 ? x1 - x0 : 0u, y1 > y0 ? y1 - y0 : 0u};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    vkCmdBeginRendering(commandBuffer, &rendering);
  }

  void setViewportScissor(VkCommandBuffer commandBuffer, Rect clip) {
    setViewportScissor(commandBuffer, clip, currentFramebufferExtent());
  }

  void setViewportScissor(VkCommandBuffer commandBuffer, Rect clip, VkExtent2D extent) {
    float const renderWidth = static_cast<float>(std::max(1u, extent.width));
    float const renderHeight = static_cast<float>(std::max(1u, extent.height));
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
