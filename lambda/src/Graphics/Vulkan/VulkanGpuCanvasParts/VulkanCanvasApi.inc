
void configureVulkanCanvasRuntime(std::span<char const* const> requiredInstanceExtensions,
                                  std::filesystem::path cacheDir) {
  std::lock_guard lock(gVulkanCoreMutex);
  if (gVulkanCore.instance) {
    if (allExtensionsConfigured(gRequiredInstanceExtensions, requiredInstanceExtensions)) {
      return;
    }
    throw std::runtime_error("Cannot configure Vulkan instance extensions after instance creation");
  }
  for (char const *extension : requiredInstanceExtensions) {
    appendUniqueExtension(gRequiredInstanceExtensions, extension);
  }
  gPipelineCacheDir = std::move(cacheDir);
}

VkInstance ensureSharedVulkanInstance() {
  return ensureSharedVulkanInstanceImpl();
}

std::unique_ptr<Canvas> createVulkanCanvas(VkSurfaceKHR surface,
                                           unsigned int handle,
                                           TextSystem &textSystem,
                                           VulkanCanvasOptions options) {
  return std::make_unique<VulkanCanvas>(surface, handle, textSystem, options);
}

std::unique_ptr<Canvas> createVulkanRenderTargetCanvas(VulkanRenderTargetSpec const& spec,
                                                       TextSystem& textSystem) {
  return std::make_unique<VulkanCanvas>(spec, textSystem);
}

bool setVulkanRenderTargetSpecForCanvas(Canvas* canvas, VulkanRenderTargetSpec const& spec) {
  auto* vulkan = dynamic_cast<VulkanCanvas*>(canvas);
  return vulkan && vulkan->setRenderTargetSpec(spec);
}

bool markVulkanRenderTargetCanvasSubmitted(Canvas* canvas) {
  auto* vulkan = dynamic_cast<VulkanCanvas*>(canvas);
  if (!vulkan) {
    return false;
  }
  vulkan->markExternalRenderTargetSubmitted();
  return true;
}

bool requestNextFrameCaptureForCanvas(Canvas* canvas) {
  auto* vulkan = dynamic_cast<VulkanCanvas*>(canvas);
  return vulkan && vulkan->requestNextFrameCapture();
}

bool takeCapturedFrameForCanvas(Canvas* canvas,
                                std::vector<std::uint8_t>& out,
                                std::uint32_t& width,
                                std::uint32_t& height) {
  auto* vulkan = dynamic_cast<VulkanCanvas*>(canvas);
  return vulkan && vulkan->takeCapturedFrame(out, width, height);
}

bool beginRecordedOpsCaptureForCanvas(Canvas *canvas, VulkanFrameRecorder *target) {
  if (!canvas || !target) {
    return false;
  }
  if (auto *vulkan = dynamic_cast<VulkanCanvas *>(canvas)) {
    vulkan->beginRecordedOpsCapture(target);
    return true;
  }
  return false;
}

void endRecordedOpsCaptureForCanvas(Canvas *canvas) {
  if (!canvas) {
    return;
  }
  if (auto *vulkan = dynamic_cast<VulkanCanvas *>(canvas)) {
    vulkan->endRecordedOpsCapture();
  }
}

bool prepareRecordedOpsForCanvas(Canvas *canvas, VulkanFrameRecorder *recorded) {
  if (!canvas || !recorded) {
    return false;
  }
  if (auto *vulkan = dynamic_cast<VulkanCanvas *>(canvas)) {
    return vulkan->prepareRecordedOps(recorded);
  }
  return false;
}

bool recordedOpsGlyphAtlasCurrentForCanvas(Canvas *canvas, VulkanFrameRecorder const &recorded) {
  if (!canvas) {
    return false;
  }
  if (auto *vulkan = dynamic_cast<VulkanCanvas *>(canvas)) {
    return vulkan->recordedOpsGlyphAtlasCurrent(recorded);
  }
  return false;
}

bool replayRecordedOpsForCanvas(Canvas *canvas, VulkanFrameRecorder const &recorded) {
  if (!canvas) {
    return false;
  }
  if (auto *vulkan = dynamic_cast<VulkanCanvas *>(canvas)) {
    return vulkan->replayRecordedOps(recorded);
  }
  return false;
}

bool replayRecordedLocalOpsForCanvas(Canvas *canvas, VulkanFrameRecorder const &recorded) {
  if (!canvas) {
    return false;
  }
  if (auto *vulkan = dynamic_cast<VulkanCanvas *>(canvas)) {
    return vulkan->replayRecordedLocalOps(recorded);
  }
  return false;
}

void setVulkanCanvasResizeBoundsHint(Canvas* canvas, int logicalWidth, int logicalHeight) {
  if (!canvas) {
    return;
  }
  if (auto* vulkan = dynamic_cast<VulkanCanvas*>(canvas)) {
    vulkan->setResizeBoundsHint(logicalWidth, logicalHeight);
  }
}

bool setVulkanCanvasBackdropBlurBaseDownsample(Canvas* canvas, std::uint32_t downsample) {
  if (auto* vulkan = dynamic_cast<VulkanCanvas*>(canvas)) {
    vulkan->setBackdropBlurBaseDownsample(downsample);
    return true;
  }
  return false;
}

bool setVulkanCanvasImagePremultipliedAlpha(Canvas* canvas, bool enabled) {
  auto* vulkan = dynamic_cast<VulkanCanvas*>(canvas);
  return vulkan ? vulkan->setImagePremultipliedAlpha(enabled) : false;
}

bool setVulkanCanvasTransparentSurface(Canvas* canvas, bool enabled) {
  auto* vulkan = dynamic_cast<VulkanCanvas*>(canvas);
  return vulkan ? vulkan->setTransparentSurface(enabled) : false;
}

bool markVulkanImageContentsChanged(Image* image) {
  auto* vulkanImage = dynamic_cast<VulkanImage*>(image);
  if (!vulkanImage) return false;
  vulkanImage->markContentChanged();
  return true;
}

bool drawVulkanBackdropBlurFrame(Canvas* canvas,
                                 Rect const& frame,
                                 CornerRadius const& frameRadius,
                                 Rect const& cutout,
                                 float radius,
                                 Color tint) {
  auto* vulkan = dynamic_cast<VulkanCanvas*>(canvas);
  return vulkan && vulkan->drawBackdropBlurFrame(frame, frameRadius, cutout, radius, tint);
}

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
                               float arrowHeight) {
  auto* vulkan = dynamic_cast<VulkanCanvas*>(canvas);
  return vulkan && vulkan->drawCalloutMaterial(bounds,
                                               card,
                                               corners,
                                               baseColor,
                                               tintColor,
                                               borderColor,
                                               borderWidth,
                                               placement,
                                               arrowWidth,
                                               arrowHeight);
}

bool vulkanCanvasSupportsDisplayTiming(Canvas* canvas) {
  auto* vulkan = dynamic_cast<VulkanCanvas*>(canvas);
  return vulkan && vulkan->supportsDisplayTiming();
}

bool vulkanCanvasUsesMailboxPresentMode(Canvas* canvas) {
  auto* vulkan = dynamic_cast<VulkanCanvas*>(canvas);
  return vulkan && vulkan->usesMailboxPresentMode();
}

std::uint32_t lastVulkanCanvasPresentId(Canvas* canvas) {
  auto* vulkan = dynamic_cast<VulkanCanvas*>(canvas);
  return vulkan ? vulkan->lastPresentId() : 0u;
}

std::vector<VulkanPastPresentationTiming> pollVulkanCanvasPastPresentationTimings(Canvas* canvas) {
  auto* vulkan = dynamic_cast<VulkanCanvas*>(canvas);
  return vulkan ? vulkan->pollPastPresentationTimings() : std::vector<VulkanPastPresentationTiming>{};
}

std::shared_ptr<Image> Image::fromExternalVulkan(VkImage image, VkImageView view, VkFormat format,
                                                 std::uint32_t width, std::uint32_t height) {
  if (!image || !view || width == 0 || height == 0) {
    return nullptr;
  }
  return std::make_shared<VulkanImage>(image, view, format, width, height);
}

std::shared_ptr<Image> Image::fromDmabuf(DmabufImageSpec const& spec) {
  if (spec.width == 0 || spec.height == 0 || spec.planes.size() != 1) {
    for (DmabufPlane const& plane : spec.planes) {
      if (plane.fd >= 0) close(plane.fd);
    }
    return nullptr;
  }
  DmabufPlane const plane = spec.planes.front();
  int fd = plane.fd;
  if (fd < 0 || plane.stride == 0) {
    if (fd >= 0) close(fd);
    return nullptr;
  }

  VkFormat const format = vkFormatForDrmFormat(spec.drmFormat);
  if (format == VK_FORMAT_UNDEFINED) {
    close(fd);
    return nullptr;
  }

  VkDevice device = VK_NULL_HANDLE;
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  bool acquiredCoreReference = false;
  try {
    SharedVulkanCore* core = acquireSharedVulkanCore(VK_NULL_HANDLE);
    acquiredCoreReference = true;
    device = core->device;

    auto externalInfo =
        vkStructure<VkExternalMemoryImageCreateInfo>(VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO);
    externalInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkSubresourceLayout planeLayout{};
    planeLayout.offset = plane.offset;
    planeLayout.rowPitch = plane.stride;

    auto modifierInfo = vkStructure<VkImageDrmFormatModifierExplicitCreateInfoEXT>(
        VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT);
    modifierInfo.pNext = &externalInfo;
    modifierInfo.drmFormatModifier = plane.modifier;
    modifierInfo.drmFormatModifierPlaneCount = 1;
    modifierInfo.pPlaneLayouts = &planeLayout;

    auto imageInfo = vkStructure<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    imageInfo.pNext = &modifierInfo;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {spec.width, spec.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCheck(vkCreateImage(device, &imageInfo, nullptr, &image), "vkCreateImage dmabuf");

    auto getMemoryFdProperties = reinterpret_cast<PFN_vkGetMemoryFdPropertiesKHR>(
        vkGetDeviceProcAddr(device, "vkGetMemoryFdPropertiesKHR"));
    if (!getMemoryFdProperties) {
      throw std::runtime_error("vkGetMemoryFdPropertiesKHR is unavailable");
    }
    auto fdProps = vkStructure<VkMemoryFdPropertiesKHR>(VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR);
    vkCheck(getMemoryFdProperties(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, fd, &fdProps),
            "vkGetMemoryFdPropertiesKHR");

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, image, &requirements);
    std::uint32_t const memoryTypeBits = requirements.memoryTypeBits & fdProps.memoryTypeBits;

    auto dedicated =
        vkStructure<VkMemoryDedicatedAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO);
    dedicated.image = image;
    auto importInfo = vkStructure<VkImportMemoryFdInfoKHR>(VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR);
    importInfo.pNext = &dedicated;
    importInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    importInfo.fd = fd;

    auto allocateInfo = vkStructure<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    allocateInfo.pNext = &importInfo;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = findMemoryType(core->physical, memoryTypeBits, 0);
    vkCheck(vkAllocateMemory(device, &allocateInfo, nullptr, &memory), "vkAllocateMemory dmabuf");
    fd = -1; // Ownership transferred to Vulkan on successful import.

    vkCheck(vkBindImageMemory(device, image, memory, 0), "vkBindImageMemory dmabuf");

    auto viewInfo = vkStructure<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    vkCheck(vkCreateImageView(device, &viewInfo, nullptr, &view), "vkCreateImageView dmabuf");

    auto result = std::make_shared<VulkanImage>(device, image, memory, view, format,
                                                static_cast<int>(spec.width), static_cast<int>(spec.height));
    releaseSharedVulkanCore();
    acquiredCoreReference = false;
    return result;
  } catch (...) {
    if (fd >= 0) close(fd);
    if (view) vkDestroyImageView(device, view, nullptr);
    if (memory) vkFreeMemory(device, memory, nullptr);
    if (image) vkDestroyImage(device, image, nullptr);
    if (acquiredCoreReference) releaseSharedVulkanCore();
    throw;
  }
}

std::shared_ptr<Image> Image::fromRgbaPixels(std::uint32_t width, std::uint32_t height,
                                             std::span<std::uint8_t const> rgbaPixels, void*) {
  return fromPixels(width, height, rgbaPixels, PixelFormat::Rgba8888, nullptr);
}

std::shared_ptr<Image> Image::fromPixels(std::uint32_t width,
                                         std::uint32_t height,
                                         std::span<std::uint8_t const> pixels,
                                         PixelFormat pixelFormat,
                                         void*) {
  std::size_t const expectedSize = static_cast<std::size_t>(width) * height * 4u;
  if (width == 0 || height == 0 || pixels.size() != expectedSize) {
    return nullptr;
  }

  std::vector<std::uint8_t> copy(expectedSize);
  std::memcpy(copy.data(), pixels.data(), expectedSize);
  return std::make_shared<VulkanImage>(static_cast<int>(width),
                                       static_cast<int>(height),
                                       std::move(copy),
                                       vkFormatForImagePixelFormat(pixelFormat));
}

std::shared_ptr<Image> rasterizeToImage(Canvas &canvas, Size logicalSize, RasterizeDrawCallback draw, float dpiScale) {
  auto *vulkan = dynamic_cast<VulkanCanvas *>(&canvas);
  if (!vulkan)
    return nullptr;
  return vulkan->rasterize(logicalSize, draw, dpiScale);
}

namespace detail {

VkInstance vulkanContextInstance() noexcept {
  std::lock_guard lock(gVulkanCoreMutex);
  return gVulkanCore.instance;
}

VkPhysicalDevice vulkanContextPhysicalDevice() noexcept {
  std::lock_guard lock(gVulkanCoreMutex);
  return gVulkanCore.physical;
}

VkDevice vulkanContextDevice() noexcept {
  std::lock_guard lock(gVulkanCoreMutex);
  return gVulkanCore.device;
}

std::uint32_t vulkanContextQueueFamily() noexcept {
  std::lock_guard lock(gVulkanCoreMutex);
  return gVulkanCore.queueFamily;
}

VkQueue vulkanContextQueue() noexcept {
  std::lock_guard lock(gVulkanCoreMutex);
  return gVulkanCore.queue;
}

VmaAllocator vulkanContextAllocator() noexcept {
  std::lock_guard lock(gVulkanCoreMutex);
  return gVulkanCore.allocator;
}

VkFormat vulkanContextPreferredColorFormat() noexcept {
  std::lock_guard lock(gVulkanCoreMutex);
  if (gVulkanCore.resources.renderFormat != VK_FORMAT_UNDEFINED) {
    return gVulkanCore.resources.renderFormat;
  }
  return VK_FORMAT_B8G8R8A8_UNORM;
}

void vulkanContextAddRequiredInstanceExtension(char const *name) {
  std::lock_guard lock(gVulkanCoreMutex);
  if (gVulkanCore.instance) {
    throw std::runtime_error("Cannot add Vulkan instance extension after instance creation");
  }
  appendUniqueExtension(gRequiredInstanceExtensions, name);
}

void vulkanContextAddRequiredDeviceExtension(char const *name) {
  std::lock_guard lock(gVulkanCoreMutex);
  if (gVulkanCore.device) {
    throw std::runtime_error("Cannot add Vulkan device extension after device creation");
  }
  appendUniqueExtension(gRequiredDeviceExtensions, name);
}

void vulkanContextEnsureInitialized() {
  ensureSharedVulkanInstanceImpl();
  static bool holdsReference = false;
  if (!holdsReference) {
    acquireSharedVulkanCore(VK_NULL_HANDLE);
    holdsReference = true;
  }
}

} // namespace detail

} // namespace lambdaui
