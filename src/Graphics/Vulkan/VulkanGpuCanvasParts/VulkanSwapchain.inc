  void recreateSwapchain() {
    if (!device_)
      return;
    auto const recreateStart = std::chrono::steady_clock::now();
    auto phaseStart = recreateStart;
    auto phaseMs = [&](std::chrono::steady_clock::time_point start) {
      return static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start).count()) / 1000.0;
    };
    for (VkFence fence : frameFences_) {
      if (fence)
        vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);
    }
    double const waitFencesMs = phaseMs(phaseStart);
    phaseStart = std::chrono::steady_clock::now();
    VkSwapchainKHR oldSwapchain = swapchain_;
    std::vector<VkImageView> oldViews = std::move(swapchainViews_);
    std::vector<VkSemaphore> oldImageRenderFinished = std::move(imageRenderFinished_);
    std::vector<VkFence> oldImagePresentFences = std::move(imagePresentFences_);
    swapchain_ = VK_NULL_HANDLE;
    swapchainImages_.clear();
    swapchainViews_.clear();
    imageRenderFinished_.clear();
    imagePresentFences_.clear();
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps);
    std::uint32_t presentCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &presentCount, nullptr);
    std::vector<VkPresentModeKHR> modes(presentCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &presentCount, modes.data());
    double const capsMs = phaseMs(phaseStart);
    phaseStart = std::chrono::steady_clock::now();
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    for (auto m : modes) {
      if (m == VK_PRESENT_MODE_MAILBOX_KHR) {
        mode = m;
        break;
      }
    }
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
    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
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
    info.presentMode = mode;
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
    if (shared_ && shared_->swapchainMaintenance1 && !vulkanPresentFencesDisabled() &&
        !presentFenceRuntimeDisabled_) {
      imagePresentFences_.resize(count, VK_NULL_HANDLE);
    }
    swapchainViews_.resize(count);
    VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo presentFenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    presentFenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (std::size_t i = 0; i < swapchainImages_.size(); ++i) {
      vkCheck(vkCreateSemaphore(device_, &sem, nullptr, &imageRenderFinished_[i]), "vkCreateSemaphore");
      if (!imagePresentFences_.empty()) {
        vkCheck(vkCreateFence(device_, &presentFenceInfo, nullptr, &imagePresentFences_[i]),
                "vkCreateFence(presentFence)");
      }
      VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      view.image = swapchainImages_[i];
      view.viewType = VK_IMAGE_VIEW_TYPE_2D;
      view.format = surfaceFormat_.format;
      view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      view.subresourceRange.levelCount = 1;
      view.subresourceRange.layerCount = 1;
      vkCheck(vkCreateImageView(device_, &view, nullptr, &swapchainViews_[i]), "vkCreateImageView");
    }
    double const imageSetupMs = phaseMs(phaseStart);
    phaseStart = std::chrono::steady_clock::now();
    for (VkSemaphore semaphore : oldImageRenderFinished) {
      if (semaphore)
        vkDestroySemaphore(device_, semaphore, nullptr);
    }
    for (VkFence fence : oldImagePresentFences) {
      if (fence) {
        VkResult const waitResult = vkWaitForFences(device_, 1, &fence, VK_TRUE, 1000000000ull);
        if (waitResult == VK_TIMEOUT) {
          presentFenceRuntimeDisabled_ = true;
          std::fprintf(stderr,
                       "Lambda Vulkan: present fence did not signal before swapchain destroy on window %u\n",
                       handle_);
        } else {
          vkCheck(waitResult, "vkWaitForFences(oldPresentFence)");
        }
        vkDestroyFence(device_, fence, nullptr);
      }
    }
    for (VkImageView view : oldViews)
      vkDestroyImageView(device_, view, nullptr);
    if (oldSwapchain)
      vkDestroySwapchainKHR(device_, oldSwapchain, nullptr);
    double const oldDestroyMs = phaseMs(phaseStart);
    swapchainDirty_ = false;
    if (detail::resizeTraceEnabled()) {
      auto const elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - recreateStart).count();
      LAMBDA_RESIZE_TRACE(
          "vulkan-recreate-swapchain",
          "window=%u framebuffer=%dx%d target=%dx%d extent=%ux%u images=%zu "
          "capsCurrent=%ux%u requested=%ux%u "
          "presentFences=%zu waitFences=%.3fms caps=%.3fms create=%.3fms imageSetup=%.3fms "
          "oldDestroy=%.3fms elapsed=%.3fms\n",
                   handle_, framebufferWidth_, framebufferHeight_, swapchainTargetWidth_, swapchainTargetHeight_,
                   swapExtent_.width, swapExtent_.height, swapchainImages_.size(),
                   caps.currentExtent.width, caps.currentExtent.height,
                   requestedExtent.width, requestedExtent.height,
                   imagePresentFences_.size(),
                   waitFencesMs, capsMs, createMs, imageSetupMs, oldDestroyMs,
                   static_cast<double>(elapsed) / 1000.0);
    }
  }

  void destroySwapchain() {
    for (VkImageView view : swapchainViews_)
      vkDestroyImageView(device_, view, nullptr);
    swapchainViews_.clear();
    swapchainImages_.clear();
    for (VkSemaphore semaphore : imageRenderFinished_) {
      if (semaphore)
        vkDestroySemaphore(device_, semaphore, nullptr);
    }
    imageRenderFinished_.clear();
    for (VkFence fence : imagePresentFences_) {
      if (fence)
        vkDestroyFence(device_, fence, nullptr);
    }
    imagePresentFences_.clear();
    if (swapchain_) {
      vkDestroySwapchainKHR(device_, swapchain_, nullptr);
      swapchain_ = VK_NULL_HANDLE;
    }
  }
