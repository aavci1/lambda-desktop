  void createCommandObjects() {
    auto pool = vkStructure<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = queueFamily_;
    vkCheck(vkCreateCommandPool(device_, &pool, nullptr, &commandPool_), "vkCreateCommandPool");
    auto alloc = vkStructure<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
    alloc.commandPool = commandPool_;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = static_cast<std::uint32_t>(commandBuffers_.size());
    vkCheck(vkAllocateCommandBuffers(device_, &alloc, commandBuffers_.data()), "vkAllocateCommandBuffers");
    auto sem = vkStructure<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    auto fence = vkStructure<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (std::size_t i = 0; i < kMaxFramesInFlight; ++i) {
      vkCheck(vkCreateSemaphore(device_, &sem, nullptr, &imageAvailable_[i]), "vkCreateSemaphore");
      vkCheck(vkCreateFence(device_, &fence, nullptr, &frameFences_[i]), "vkCreateFence");
    }
  }

  void recoverResetFrameFence() {
    if (resetFrameFenceIndex_ == kNoResetFrameFence || resetFrameFenceIndex_ >= frameFences_.size()) {
      return;
    }
    VkFence oldFence = frameFences_[resetFrameFenceIndex_];
    auto fence = vkStructure<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VkFence newFence = VK_NULL_HANDLE;
    if (vkCreateFence(device_, &fence, nullptr, &newFence) == VK_SUCCESS) {
      for (VkFence &imageFence : imageInFlightFences_) {
        if (imageFence == oldFence) {
          imageFence = newFence;
        }
      }
      frameFences_[resetFrameFenceIndex_] = newFence;
      if (oldFence) {
        vkDestroyFence(device_, oldFence, nullptr);
      }
    }
    resetFrameFenceIndex_ = kNoResetFrameFence;
  }

  SharedVulkanCore::Resources &resources() {
    if (!shared_) {
      throw std::runtime_error("Vulkan shared resources are unavailable");
    }
    return shared_->resources;
  }

  SharedVulkanCore::Resources const &resources() const {
    if (!shared_) {
      throw std::runtime_error("Vulkan shared resources are unavailable");
    }
    return shared_->resources;
  }

  void ensureSharedResources() {
    auto &res = resources();
    VkFormat const format = surfaceFormat_.format == VK_FORMAT_UNDEFINED ? VK_FORMAT_B8G8R8A8_UNORM
                                                                         : surfaceFormat_.format;
    VkFormat const backdropFormat = backdropRenderTargetFormat();
    if (res.initialized) {
      if (res.renderFormat != format || res.backdropRenderFormat != backdropFormat) {
        throw std::runtime_error("Shared Vulkan resources cannot be reused with a different surface format");
      }
      return;
    }
    res.renderFormat = format;
    res.backdropRenderFormat = backdropFormat;
    createDescriptors();
    createSampler();
    createPipelineCache(*shared_);
    createPipelines();
    createAtlas();
    res.initialized = true;
  }

  void createDescriptors() {
    auto &res = resources();
    VkDescriptorPoolSize sizes[2]{
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kDescriptorPoolStorageBufferSets},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kDescriptorPoolImageSamplerSets},
    };
    auto pool = vkStructure<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool.maxSets = kDescriptorPoolStorageBufferSets + kDescriptorPoolImageSamplerSets;
    pool.poolSizeCount = 2;
    pool.pPoolSizes = sizes;
    vkCheck(vkCreateDescriptorPool(device_, &pool, nullptr, &res.descriptorPool), "vkCreateDescriptorPool");
    res.rectDescriptorLayout = createStorageLayout();
    res.quadDescriptorLayout = createStorageLayout();
    VkDescriptorSetLayoutBinding b{};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    auto layout = vkStructure<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    layout.bindingCount = 1;
    layout.pBindings = &b;
    vkCheck(vkCreateDescriptorSetLayout(device_, &layout, nullptr, &res.textureDescriptorLayout),
            "vkCreateDescriptorSetLayout");
  }

  VkDescriptorSetLayout createStorageLayout() {
    VkDescriptorSetLayoutBinding b{};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    auto info = vkStructure<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    info.bindingCount = 1;
    info.pBindings = &b;
    VkDescriptorSetLayout out = VK_NULL_HANDLE;
    vkCheck(vkCreateDescriptorSetLayout(device_, &info, nullptr, &out), "vkCreateDescriptorSetLayout");
    return out;
  }

  void createSampler() {
    auto &res = resources();
    auto s = vkStructure<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    s.magFilter = VK_FILTER_LINEAR;
    s.minFilter = VK_FILTER_LINEAR;
    s.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    s.addressModeU = s.addressModeV = s.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCheck(vkCreateSampler(device_, &s, nullptr, &res.sampler), "vkCreateSampler");
  }

  void chooseSurfaceFormat() {
    std::uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, formats.data());
    surfaceFormat_ = formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
                                     : formats[0];
    auto choose = [&](VkFormat format) {
      for (auto const &fmt : formats) {
        if (fmt.format == format) {
          surfaceFormat_ = fmt;
          return true;
        }
      }
      return false;
    };
    if (choose(VK_FORMAT_B8G8R8A8_UNORM) || choose(VK_FORMAT_R8G8B8A8_UNORM) ||
        choose(VK_FORMAT_A8B8G8R8_UNORM_PACK32)) {
      return;
    }
    for (auto const &fmt : formats) {
      if (fmt.format == VK_FORMAT_B8G8R8A8_SRGB || fmt.format == VK_FORMAT_R8G8B8A8_SRGB ||
          fmt.format == VK_FORMAT_A8B8G8R8_SRGB_PACK32) {
        surfaceFormat_ = fmt;
        break;
      }
    }
  }

  struct VertexInput {
    VkVertexInputBindingDescription const *bindings = nullptr;
    std::uint32_t bindingCount = 0;
    VkVertexInputAttributeDescription const *attrs = nullptr;
    std::uint32_t attrCount = 0;
  };

  void createPipelines() {
    auto &res = resources();
    res.rectPipelineLayout = createPipelineLayout({res.rectDescriptorLayout}, true);
    res.calloutPipelineLayout = createPipelineLayout({res.rectDescriptorLayout}, true);
    res.imagePipelineLayout = createPipelineLayout({res.quadDescriptorLayout, res.textureDescriptorLayout}, true);
    res.backdropPipelineLayout = createPipelineLayout({res.quadDescriptorLayout, res.textureDescriptorLayout}, true);
    res.pathPipelineLayout = createPipelineLayout({}, true);
    res.rectPipeline = createPipeline(res.rectPipelineLayout, lambda_rect_vert_spv, lambda_rect_vert_spv_len,
                                      lambda_rect_frag_spv, lambda_rect_frag_spv_len, {});
    res.calloutPipeline = createPipeline(res.calloutPipelineLayout,
                                         lambda_callout_vert_spv,
                                         lambda_callout_vert_spv_len,
                                         lambda_callout_frag_spv,
                                         lambda_callout_frag_spv_len,
                                         {});
    res.imagePipeline = createPipeline(res.imagePipelineLayout, lambda_image_vert_spv, lambda_image_vert_spv_len,
                                       lambda_image_frag_spv, lambda_image_frag_spv_len, {});
    res.imageUnpremultiplyPipeline =
        createPipeline(res.imagePipelineLayout,
                       lambda_image_vert_spv,
                       lambda_image_vert_spv_len,
                       lambda_image_unpremultiply_frag_spv,
                       lambda_image_unpremultiply_frag_spv_len,
                       {});
    res.backdropPipeline = createPipeline(res.backdropPipelineLayout, lambda_image_vert_spv, lambda_image_vert_spv_len,
                                          lambda_backdrop_frag_spv, lambda_backdrop_frag_spv_len, {});
    res.backdropBlurPipeline =
        createPipeline(res.backdropPipelineLayout, lambda_image_vert_spv, lambda_image_vert_spv_len,
                       lambda_backdrop_blur_frag_spv, lambda_backdrop_blur_frag_spv_len, {},
                       res.backdropRenderFormat);
    std::array<VkVertexInputBindingDescription, 1> binding{};
    binding[0].binding = 0;
    binding[0].stride = sizeof(VulkanPathVertex);
    binding[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    std::array<VkVertexInputAttributeDescription, 20> attrs{};
    attrs[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VulkanPathVertex, x)};
    attrs[1] = {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanPathVertex, color)};
    attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VulkanPathVertex, viewport)};
    attrs[3] = {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(VulkanPathVertex, local)};
    attrs[4] = {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanPathVertex, fill0)};
    attrs[5] = {5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanPathVertex, fill1)};
    attrs[6] = {6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanPathVertex, fill2)};
    attrs[7] = {7, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanPathVertex, fill3)};
    attrs[8] = {8, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanPathVertex, stops)};
    attrs[9] = {9, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanPathVertex, gradient)};
    attrs[10] = {10, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanPathVertex, params)};
    attrs[11] = {11, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(VulkanPathVertex, clipHeader)};
    for (std::uint32_t i = 0; i < kVulkanRoundedClipEntryCount; ++i) {
      attrs[12 + i] = {12 + i,
                       0,
                       VK_FORMAT_R32G32B32A32_SFLOAT,
                       static_cast<std::uint32_t>(offsetof(VulkanPathVertex, clipEntries) +
                                                  i * sizeof(float) * 4u)};
    }
    res.pathPipeline = createPipeline(res.pathPipelineLayout, lambda_path_vert_spv, lambda_path_vert_spv_len,
                                      lambda_path_frag_spv, lambda_path_frag_spv_len,
                                      {binding.data(), 1, attrs.data(), static_cast<std::uint32_t>(attrs.size())});
  }

  VkPipelineLayout createPipelineLayout(std::initializer_list<VkDescriptorSetLayout> layouts, bool viewportPush) {
    std::vector<VkDescriptorSetLayout> setLayouts(layouts);
    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.offset = 0;
    push.size = sizeof(VulkanDrawPushConstants);
    auto info = vkStructure<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    info.setLayoutCount = static_cast<std::uint32_t>(setLayouts.size());
    info.pSetLayouts = setLayouts.data();
    if (viewportPush) {
      info.pushConstantRangeCount = 1;
      info.pPushConstantRanges = &push;
    }
    VkPipelineLayout out = VK_NULL_HANDLE;
    vkCheck(vkCreatePipelineLayout(device_, &info, nullptr, &out), "vkCreatePipelineLayout");
    return out;
  }

  VkPipeline createPipeline(VkPipelineLayout layout, unsigned char const *vertBytes, unsigned int vertLen,
                            unsigned char const *fragBytes, unsigned int fragLen, VertexInput input,
                            VkFormat colorFormatOverride = VK_FORMAT_UNDEFINED) {
    VkShaderModule vert = shaderModule(device_, vertBytes, vertLen);
    VkShaderModule frag = shaderModule(device_, fragBytes, fragLen);
    VkPipelineShaderStageCreateInfo stages[2]{
        vkStructure<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO),
        vkStructure<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO)};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    auto vi =
        vkStructure<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    vi.vertexBindingDescriptionCount = input.bindingCount;
    vi.pVertexBindingDescriptions = input.bindings;
    vi.vertexAttributeDescriptionCount = input.attrCount;
    vi.pVertexAttributeDescriptions = input.attrs;
    auto ia = vkStructure<VkPipelineInputAssemblyStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    auto vp = vkStructure<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    auto rs = vkStructure<VkPipelineRasterizationStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.f;
    auto ms =
        vkStructure<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend{};
    blend.blendEnable = VK_TRUE;
    blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.colorBlendOp = VK_BLEND_OP_ADD;
    blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alphaBlendOp = VK_BLEND_OP_ADD;
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                           VK_COLOR_COMPONENT_A_BIT;
    auto cb =
        vkStructure<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    cb.attachmentCount = 1;
    cb.pAttachments = &blend;
    VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    auto dyn = vkStructure<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = states;
    auto info = vkStructure<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    auto rendering = vkStructure<VkPipelineRenderingCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
    VkFormat const colorFormat = colorFormatOverride == VK_FORMAT_UNDEFINED
                                     ? resources().renderFormat
                                     : colorFormatOverride;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &colorFormat;
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vi;
    info.pInputAssemblyState = &ia;
    info.pViewportState = &vp;
    info.pRasterizationState = &rs;
    info.pMultisampleState = &ms;
    info.pColorBlendState = &cb;
    info.pDynamicState = &dyn;
    info.layout = layout;
    VkPipeline out = VK_NULL_HANDLE;
    vkCheck(vkCreateGraphicsPipelines(device_, resources().pipelineCache, 1, &info, nullptr, &out),
            "vkCreateGraphicsPipelines");
    vkDestroyShaderModule(device_, vert, nullptr);
    vkDestroyShaderModule(device_, frag, nullptr);
    return out;
  }
