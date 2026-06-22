  void markFrameFenceComplete(std::size_t index) {
    if (index >= frameFenceCompleteGenerations_.size()) {
      return;
    }
    frameFenceCompleteGenerations_[index] = frameFenceSubmitGenerations_[index];
  }

  void markFrameFenceComplete(VkFence fence) {
    if (!fence) {
      return;
    }
    for (std::size_t i = 0; i < frameFences_.size(); ++i) {
      if (frameFences_[i] == fence) {
        markFrameFenceComplete(i);
        return;
      }
    }
  }

  void markFrameFenceSubmitted(std::size_t index) {
    if (index >= frameFenceSubmitGenerations_.size()) {
      return;
    }
    ++frameFenceSubmitGenerations_[index];
  }

  VkFence createPresentFence(bool signaled, char const* label) {
    VkFence fence = VK_NULL_HANDLE;
    auto info = vkStructure<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
    if (signaled) {
      info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    }
    vkCheck(vkCreateFence(device_, &info, nullptr, &fence), label);
    return fence;
  }

  void destroyRetiredSwapchainResources(RetiredSwapchain& retired) {
    for (VkSemaphore semaphore : retired.renderFinished) {
      if (semaphore) {
        vkDestroySemaphore(device_, semaphore, nullptr);
      }
    }
    retired.renderFinished.clear();
    for (VkFence fence : retired.presentFences) {
      if (fence) {
        vkDestroyFence(device_, fence, nullptr);
      }
    }
    retired.presentFences.clear();
    for (VkImageView view : retired.views) {
      if (view) {
        vkDestroyImageView(device_, view, nullptr);
      }
    }
    retired.views.clear();
    if (retired.swapchain) {
      vkDestroySwapchainKHR(device_, retired.swapchain, nullptr);
      retired.swapchain = VK_NULL_HANDLE;
    }
  }

  bool retiredPresentFencesReady(RetiredSwapchain const& retired) const {
    for (VkFence fence : retired.presentFences) {
      if (!fence) {
        continue;
      }
      VkResult const status = vkGetFenceStatus(device_, fence);
      if (status == VK_NOT_READY) {
        return false;
      }
      vkCheck(status, "vkGetFenceStatus(retiredPresentFence)");
    }
    return true;
  }

  bool retiredFrameFencesReady(RetiredSwapchain const& retired) const {
    for (std::size_t i = 0; i < frameFenceCompleteGenerations_.size(); ++i) {
      if (frameFenceCompleteGenerations_[i] < retired.frameFenceGenerations[i]) {
        return false;
      }
    }
    return true;
  }

  bool retiredSwapchainReady(RetiredSwapchain const& retired, bool force) const {
    if (force) {
      return true;
    }
    if (!retired.presentFences.empty() && !retiredPresentFencesReady(retired)) {
      return false;
    }
    return retiredFrameFencesReady(retired);
  }

  void retireSwapchains(bool force) {
    for (auto it = retiredSwapchains_.begin(); it != retiredSwapchains_.end();) {
      if (retiredSwapchainReady(*it, force)) {
        destroyRetiredSwapchainResources(*it);
        it = retiredSwapchains_.erase(it);
      } else {
        ++it;
      }
    }
  }

  void retireSwapchainResources(VkSwapchainKHR swapchain,
                                std::vector<VkImageView> views,
                                std::vector<VkSemaphore> renderFinished,
                                std::vector<VkFence> presentFences) {
    if (!swapchain && views.empty() && renderFinished.empty() && presentFences.empty()) {
      return;
    }
    RetiredSwapchain retired{};
    retired.swapchain = swapchain;
    retired.views = std::move(views);
    retired.renderFinished = std::move(renderFinished);
    retired.presentFences = std::move(presentFences);
    retired.frameFenceGenerations = frameFenceSubmitGenerations_;
    retiredSwapchains_.push_back(std::move(retired));
  }

  void retirePresentFence(VkFence fence) {
    if (!fence) {
      return;
    }
    RetiredSwapchain retired{};
    retired.presentFences.push_back(fence);
    retiredSwapchains_.push_back(std::move(retired));
  }

  void recreateSwapchain() {
    if (!device_)
      return;
    auto const recreateStart = std::chrono::steady_clock::now();
    auto phaseStart = recreateStart;
    auto phaseMs = [&](std::chrono::steady_clock::time_point start) {
      return static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start).count()) / 1000.0;
    };
    VkSwapchainKHR oldSwapchain = swapchain_;
    std::vector<VkImageView> oldViews = std::move(swapchainViews_);
    std::vector<VkSemaphore> oldImageRenderFinished = std::move(imageRenderFinished_);
    std::vector<VkFence> oldImagePresentFences = std::move(imagePresentFences_);
    swapchain_ = VK_NULL_HANDLE;
    swapchainImages_.clear();
    swapchainViews_.clear();
    imageRenderFinished_.clear();
    imagePresentFences_.clear();
    retireSwapchainResources(oldSwapchain,
                             std::move(oldViews),
                             std::move(oldImageRenderFinished),
                             std::move(oldImagePresentFences));
    double const retireMs = phaseMs(phaseStart);
    phaseStart = std::chrono::steady_clock::now();
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps);
    std::uint32_t presentCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &presentCount, nullptr);
    std::vector<VkPresentModeKHR> modes(presentCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &presentCount, modes.data());
    double const capsMs = phaseMs(phaseStart);
    phaseStart = std::chrono::steady_clock::now();
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    if (!vulkanForceFifoPresentMode()) {
      for (auto m : modes) {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) {
          mode = m;
          break;
        }
      }
    }
    presentMode_ = mode;
    std::uint32_t const requestedW =
        static_cast<std::uint32_t>(std::max({1, framebufferWidth_, swapchainTargetWidth_}));
    std::uint32_t const requestedH =
        static_cast<std::uint32_t>(std::max({1, framebufferHeight_, swapchainTargetHeight_}));
    VkExtent2D const requestedExtent{
        std::clamp(requestedW, caps.minImageExtent.width, caps.maxImageExtent.width),
        std::clamp(requestedH, caps.minImageExtent.height, caps.maxImageExtent.height),
    };
    if (caps.currentExtent.width != UINT32_MAX) {
      swapExtent_ = caps.currentExtent;
    } else {
      swapExtent_ = requestedExtent;
    }
    if (detail::resizeTraceEnabled()) {
      std::fprintf(stderr,
                   "Lambda Vulkan: swapchain extent window=%u framebuffer=%dx%d target=%dx%d "
                   "caps current=%ux%u min=%ux%u max=%ux%u requested=%ux%u chosen=%ux%u\n",
                   handle_,
                   framebufferWidth_,
                   framebufferHeight_,
                   swapchainTargetWidth_,
                   swapchainTargetHeight_,
                   caps.currentExtent.width,
                   caps.currentExtent.height,
                   caps.minImageExtent.width,
                   caps.minImageExtent.height,
                   caps.maxImageExtent.width,
                   caps.maxImageExtent.height,
                   requestedExtent.width,
                   requestedExtent.height,
                   swapExtent_.width,
                   swapExtent_.height);
    }
    std::uint32_t const preferredImageCount =
        std::max<std::uint32_t>(caps.minImageCount + 2u, static_cast<std::uint32_t>(kMaxFramesInFlight));
    std::uint32_t imageCount = std::clamp(preferredImageCount,
                                          caps.minImageCount,
                                          caps.maxImageCount ? caps.maxImageCount : preferredImageCount);
    auto info = vkStructure<VkSwapchainCreateInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
    info.surface = surface_;
    info.minImageCount = imageCount;
    info.imageFormat = surfaceFormat_.format;
    info.imageColorSpace = surfaceFormat_.colorSpace;
    info.imageExtent = swapExtent_;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = chooseCompositeAlpha(caps.supportedCompositeAlpha);
    info.presentMode = presentMode_;
    info.clipped = VK_TRUE;
    info.oldSwapchain = oldSwapchain;
    vkCheck(vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_), "vkCreateSwapchainKHR");
    double const createMs = phaseMs(phaseStart);
    phaseStart = std::chrono::steady_clock::now();
    std::uint32_t count = 0;
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
    swapchainImages_.resize(count);
    vkGetSwapchainImagesKHR(device_, swapchain_, &count, swapchainImages_.data());
    imageInFlightFences_.assign(count, VK_NULL_HANDLE);
    imageRenderFinished_.resize(count, VK_NULL_HANDLE);
    if (shared_ && shared_->swapchainMaintenance1 && vulkanPresentFencesEnabled() &&
        !presentFenceRuntimeDisabled_) {
      imagePresentFences_.resize(count, VK_NULL_HANDLE);
    }
    swapchainViews_.resize(count);
    auto sem = vkStructure<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    for (std::size_t i = 0; i < swapchainImages_.size(); ++i) {
      vkCheck(vkCreateSemaphore(device_, &sem, nullptr, &imageRenderFinished_[i]), "vkCreateSemaphore");
      if (!imagePresentFences_.empty()) {
        imagePresentFences_[i] = createPresentFence(true, "vkCreateFence(presentFence)");
      }
      auto view = vkStructure<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
      view.image = swapchainImages_[i];
      view.viewType = VK_IMAGE_VIEW_TYPE_2D;
      view.format = surfaceFormat_.format;
      view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      view.subresourceRange.levelCount = 1;
      view.subresourceRange.layerCount = 1;
      vkCheck(vkCreateImageView(device_, &view, nullptr, &swapchainViews_[i]), "vkCreateImageView");
    }
    double const imageSetupMs = phaseMs(phaseStart);
    swapchainDirty_ = false;
    if (detail::resizeTraceEnabled()) {
      auto const elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - recreateStart).count();
      LAMBDA_RESIZE_TRACE(
          "vulkan-recreate-swapchain",
          "window=%u framebuffer=%dx%d target=%dx%d extent=%ux%u images=%zu "
          "capsCurrent=%ux%u requested=%ux%u "
          "presentFences=%zu retire=%zu retireQueue=%.3fms caps=%.3fms create=%.3fms imageSetup=%.3fms "
          "elapsed=%.3fms\n",
                   handle_, framebufferWidth_, framebufferHeight_, swapchainTargetWidth_, swapchainTargetHeight_,
                   swapExtent_.width, swapExtent_.height, swapchainImages_.size(),
                   caps.currentExtent.width, caps.currentExtent.height,
                   requestedExtent.width, requestedExtent.height,
                   imagePresentFences_.size(),
                   retiredSwapchains_.size(), retireMs, capsMs, createMs, imageSetupMs,
                   static_cast<double>(elapsed) / 1000.0);
    }
  }

  void destroySwapchain() {
    retireSwapchainResources(swapchain_,
                             std::move(swapchainViews_),
                             std::move(imageRenderFinished_),
                             std::move(imagePresentFences_));
    swapchain_ = VK_NULL_HANDLE;
    swapchainViews_.clear();
    swapchainImages_.clear();
    imageInFlightFences_.clear();
    imageRenderFinished_.clear();
    imagePresentFences_.clear();
    retireSwapchains(true);
  }
