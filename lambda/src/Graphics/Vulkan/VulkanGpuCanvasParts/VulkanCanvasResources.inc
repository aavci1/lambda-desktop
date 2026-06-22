  void createAtlas() {
    auto &res = resources();
    res.atlas.width = 2048;
    res.atlas.height = 2048;
    res.atlasPixels.assign(static_cast<std::size_t>(res.atlas.width) * res.atlas.height, Rgba{255, 255, 255, 0});
    createTexture(res.atlas,
                  res.atlas.width,
                  res.atlas.height,
                  VK_FORMAT_R8G8B8A8_UNORM);
    ensureTextureDescriptor(res.atlas);
  }

  void appendPath(Path const &path, FillStyle const &fill, StrokeStyle const &stroke) {
    if (path.isEmpty())
      return;
    RecordingTarget target = recordingTarget();
    PathCacheKey const cacheKey{
        .pathHash = path.contentHash(),
        .styleHash = hashFill(fill) ^ (hashStroke(stroke) + 0x9e3779b97f4a7c15ULL) ^
                     (hashTransform(state_.transform, state_.opacity) << 1u) ^
                     (currentRoundedClipHash() << 2u),
        .viewportW = width_,
        .viewportH = height_,
    };
    if (auto it = pathCache_.find(cacheKey); it != pathCache_.end()) {
      pathCacheLru_.splice(pathCacheLru_.end(), pathCacheLru_, it->second.lruIt);
      std::uint32_t const firstVertex = static_cast<std::uint32_t>(target.pathVerts.size());
      target.pathVerts.insert(target.pathVerts.end(), it->second.vertices.begin(), it->second.vertices.end());
      if (!it->second.vertices.empty()) {
        appendSignedDrawOp(target,
                           makeDrawOp(DrawOp::Kind::Path,
                                      nullptr,
                                      firstVertex,
                                      static_cast<std::uint32_t>(it->second.vertices.size())));
      }
      return;
    }
    auto subpaths = PathFlattener::flattenSubpaths(path);
    if (subpaths.empty())
      return;
    std::uint32_t const firstVertex = static_cast<std::uint32_t>(target.pathVerts.size());
    for (auto &sp : subpaths) {
      for (Point &p : sp)
        p = state_.transform.apply(p);
    }
    Rect const bounds = boundsOfSubpaths(subpaths);
    auto append = [&](TessellatedPath &&tess, FillStyle const *gradientSource = nullptr) {
      if (gradientSource) {
        for (PathVertex const &vertex : tess.vertices) {
          VulkanPathVertex out = makeVulkanPathVertex(vertex, gradientSource, bounds, state_.opacity);
          applyCurrentRoundedClip(out);
          target.pathVerts.push_back(out);
        }
      } else {
        for (PathVertex const &vertex : tess.vertices) {
          VulkanPathVertex out = makeVulkanPathVertex(vertex, nullptr, bounds, 1.f);
          applyCurrentRoundedClip(out);
          target.pathVerts.push_back(out);
        }
      }
    };
    auto const tessellateStart = std::chrono::steady_clock::now();
    bool tessellated = false;
    if (!fill.isNone()) {
      Color fc{};
      if (representativeFillColor(fill, &fc)) {
        fc.a *= state_.opacity;
        std::vector<std::vector<Point>> nonempty;
        for (auto const &sp : subpaths) {
          if (sp.size() >= 3)
            nonempty.push_back(sp);
        }
        if (!nonempty.empty()) {
          tessellated = true;
          append(PathFlattener::tessellateFillContours(nonempty, fc, static_cast<float>(width_),
                                                       static_cast<float>(height_),
                                                       PathFlattener::tessWindingFromFillRule(fill.fillRule)),
                 &fill);
        }
      }
    }
    if (!stroke.isNone()) {
      Color sc{};
      if (stroke.solidColor(&sc)) {
        sc.a *= state_.opacity;
        float const sx = std::hypot(state_.transform.m[0], state_.transform.m[1]);
        float const sy = std::hypot(state_.transform.m[3], state_.transform.m[4]);
        float const s = (sx > 0.f || sy > 0.f) ? (sx + sy) * 0.5f : 1.f;
        for (auto const &sp : subpaths) {
          if (sp.size() >= 2) {
            tessellated = true;
            append(PathFlattener::tessellateStroke(sp, stroke.width * s, sc, static_cast<float>(width_),
                                                   static_cast<float>(height_), stroke.join, stroke.cap));
          }
        }
      }
    }
    if (tessellated) {
      debug::perf::recordVulkanPathTessellation(std::chrono::steady_clock::now() - tessellateStart);
    }
    std::uint32_t const vertexCount = static_cast<std::uint32_t>(target.pathVerts.size()) - firstVertex;
    if (vertexCount > 0) {
      std::vector<VulkanPathVertex> cached(target.pathVerts.begin() + firstVertex, target.pathVerts.end());
      pathCacheLru_.push_back(cacheKey);
      auto lruIt = std::prev(pathCacheLru_.end());
      auto [it, inserted] = pathCache_.emplace(cacheKey, CachedPath{std::move(cached), lruIt});
      if (inserted) {
        cachedPathVertexCount_ += it->second.vertices.size();
      } else {
        pathCacheLru_.erase(lruIt);
      }
      trimPathCache();
      appendSignedDrawOp(target, makeDrawOp(DrawOp::Kind::Path, nullptr, firstVertex, vertexCount));
    }
  }

  void trimPathCache() {
    constexpr std::size_t kMaxCachedPathVertices = 500'000;
    while (cachedPathVertexCount_ > kMaxCachedPathVertices && !pathCache_.empty()) {
      if (pathCacheLru_.empty()) {
        pathCache_.clear();
        cachedPathVertexCount_ = 0;
        return;
      }
      PathCacheKey const key = pathCacheLru_.front();
      auto it = pathCache_.find(key);
      if (it != pathCache_.end()) {
        cachedPathVertexCount_ -= it->second.vertices.size();
        pathCache_.erase(it);
      }
      pathCacheLru_.pop_front();
    }
  }

  void uploadFrameBuffers() {
    FrameGeometryResources& geometry = frameGeometryResources();
    ensureBuffer(geometry.rectBuffer,
                 std::max<VkDeviceSize>(sizeof(RectInstance), rects_.size() * sizeof(RectInstance)),
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    upload(geometry.rectBuffer, rects_.data(), rects_.size() * sizeof(RectInstance));
    ensureStorageDescriptor(geometry.rectDescriptorSet, resources().rectDescriptorLayout, geometry.rectBuffer);
    ensureBuffer(geometry.calloutBuffer,
                 std::max<VkDeviceSize>(sizeof(CalloutInstance), callouts_.size() * sizeof(CalloutInstance)),
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    upload(geometry.calloutBuffer, callouts_.data(), callouts_.size() * sizeof(CalloutInstance));
    ensureStorageDescriptor(geometry.calloutDescriptorSet, resources().rectDescriptorLayout, geometry.calloutBuffer);
    ensureBuffer(geometry.quadBuffer,
                 std::max<VkDeviceSize>(sizeof(QuadInstance), quads_.size() * sizeof(QuadInstance)),
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    upload(geometry.quadBuffer, quads_.data(), quads_.size() * sizeof(QuadInstance));
    ensureStorageDescriptor(geometry.quadDescriptorSet, resources().quadDescriptorLayout, geometry.quadBuffer);
    ensureBuffer(geometry.pathBuffer,
                 std::max<VkDeviceSize>(sizeof(VulkanPathVertex), pathVerts_.size() * sizeof(VulkanPathVertex)),
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    upload(geometry.pathBuffer, pathVerts_.data(), pathVerts_.size() * sizeof(VulkanPathVertex));
  }

  FrameGeometryResources& frameGeometryResources() {
    return frameGeometry_[currentFrame_ % frameGeometry_.size()];
  }

  FrameGeometryResources const& frameGeometryResources() const {
    return frameGeometry_[currentFrame_ % frameGeometry_.size()];
  }

  void ensureBuffer(Buffer &buffer, VkDeviceSize size, VkBufferUsageFlags usage) {
    if (buffer.buffer && buffer.capacity >= size)
      return;
    destroyBuffer(buffer);
    buffer.capacity = std::max<VkDeviceSize>(size, 1024);
    auto info = vkStructure<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    info.size = buffer.capacity;
    info.usage = usage | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo allocation{};
    allocation.usage = VMA_MEMORY_USAGE_AUTO;
    allocation.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo allocationInfo{};
    vkCheck(vmaCreateBuffer(allocator_, &info, &allocation, &buffer.buffer, &buffer.allocation, &allocationInfo),
            "vmaCreateBuffer");
    buffer.mapped = allocationInfo.pMappedData;
  }

  void upload(Buffer &buffer, void const *data, std::size_t size) {
    if (!size)
      return;
    if (buffer.mapped) {
      std::memcpy(buffer.mapped, data, size);
      vkCheck(vmaFlushAllocation(allocator_, buffer.allocation, 0, size), "vmaFlushAllocation");
      return;
    }
    void *mapped = nullptr;
    vkCheck(vmaMapMemory(allocator_, buffer.allocation, &mapped), "vmaMapMemory");
    std::memcpy(mapped, data, size);
    vkCheck(vmaFlushAllocation(allocator_, buffer.allocation, 0, size), "vmaFlushAllocation");
    vmaUnmapMemory(allocator_, buffer.allocation);
  }

  void uploadRows(Buffer& buffer,
                  void const* data,
                  std::size_t rowBytes,
                  std::uint32_t rowCount,
                  std::size_t sourceBytesPerRow) {
    if (!data || rowBytes == 0 || rowCount == 0) {
      return;
    }
    std::size_t const totalBytes = rowBytes * static_cast<std::size_t>(rowCount);
    auto copyRows = [&](void* mapped) {
      auto* dst = static_cast<std::uint8_t*>(mapped);
      auto const* src = static_cast<std::uint8_t const*>(data);
      for (std::uint32_t row = 0; row < rowCount; ++row) {
        std::memcpy(dst + static_cast<std::size_t>(row) * rowBytes,
                    src + static_cast<std::size_t>(row) * sourceBytesPerRow,
                    rowBytes);
      }
    };
    if (buffer.mapped) {
      copyRows(buffer.mapped);
      vkCheck(vmaFlushAllocation(allocator_, buffer.allocation, 0, totalBytes), "vmaFlushAllocation");
      return;
    }
    void* mapped = nullptr;
    vkCheck(vmaMapMemory(allocator_, buffer.allocation, &mapped), "vmaMapMemory");
    copyRows(mapped);
    vkCheck(vmaFlushAllocation(allocator_, buffer.allocation, 0, totalBytes), "vmaFlushAllocation");
    vmaUnmapMemory(allocator_, buffer.allocation);
  }

  void destroyBuffer(Buffer &buffer) {
    if (buffer.buffer)
      vmaDestroyBuffer(allocator_, buffer.buffer, buffer.allocation);
    buffer = {};
  }

  void destroyStorageDescriptor(VkDescriptorSet& set) {
    if (set && resources().descriptorPool) {
      vkFreeDescriptorSets(device_, resources().descriptorPool, 1, &set);
    }
    set = VK_NULL_HANDLE;
  }

  void destroyFrameGeometryResources(FrameGeometryResources& geometry) {
    destroyStorageDescriptor(geometry.rectDescriptorSet);
    destroyStorageDescriptor(geometry.calloutDescriptorSet);
    destroyStorageDescriptor(geometry.quadDescriptorSet);
    destroyBuffer(geometry.pathBuffer);
    destroyBuffer(geometry.rectBuffer);
    destroyBuffer(geometry.calloutBuffer);
    destroyBuffer(geometry.quadBuffer);
  }

  static Buffer takeBuffer(Buffer& buffer) {
    Buffer out = buffer;
    buffer = {};
    return out;
  }

  void ensureStorageDescriptor(VkDescriptorSet &set, VkDescriptorSetLayout layout, Buffer const &buffer) {
    if (!set) {
      auto alloc = vkStructure<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
      alloc.descriptorPool = resources().descriptorPool;
      alloc.descriptorSetCount = 1;
      alloc.pSetLayouts = &layout;
      vkCheck(vkAllocateDescriptorSets(device_, &alloc, &set), "vkAllocateDescriptorSets");
    }
    VkDescriptorBufferInfo bi{buffer.buffer, 0, buffer.capacity};
    auto write = vkStructure<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &bi;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
  }

  void ensureBackdropSceneTarget() {
    std::uint32_t const downsample = backdropBlurDownsample();
    int const targetW = static_cast<int>(std::max(1u, (swapExtent_.width + downsample - 1u) / downsample));
    int const targetH = static_cast<int>(std::max(1u, (swapExtent_.height + downsample - 1u) / downsample));
    VkFormat const backdropFormat = backdropRenderTargetFormat();
    auto ensure = [&](Texture &texture) {
      if (texture.image && texture.width == targetW && texture.height == targetH && texture.format == backdropFormat) {
        return;
      }
      destroyTexture(texture);
      createRenderTargetTexture(texture, targetW, targetH, backdropFormat);
      clearBackdropBlurCache();
    };
    ensure(backdropSceneTexture_);
    ensure(backdropScratchTexture_);
    ensure(backdropBlurTexture_);
  }

  void clearBackdropBlurCache() {
    for (CachedBackdropBlur &entry : backdropBlurCache_) {
      destroyTexture(entry.texture);
    }
    backdropBlurCache_.clear();
  }

  std::uint32_t appendBackdropBlurQuad(float radiusPx, float axisX, float axisY) {
    QuadInstance q{};
    q.rect[0] = 0.f;
    q.rect[1] = 0.f;
    q.rect[2] = static_cast<float>(width_);
    q.rect[3] = static_cast<float>(height_);
    q.axisX[0] = 0.f;
    q.axisX[1] = 0.f;
    q.axisX[2] = static_cast<float>(width_);
    q.axisX[3] = 0.f;
    q.axisY[0] = 0.f;
    q.axisY[1] = static_cast<float>(height_);
    q.uv[0] = 0.f;
    q.uv[1] = 0.f;
    q.uv[2] = 1.f;
    q.uv[3] = 1.f;
    q.color[0] = q.color[1] = q.color[2] = q.color[3] = 0.f;
    q.radii[0] = radiusPx;
    q.radii[1] = axisX;
    q.radii[2] = axisY;
    q.radii[3] = 0.f;
    std::uint32_t const first = static_cast<std::uint32_t>(quads_.size());
    quads_.push_back(q);
    return first;
  }

  struct BackdropBlurRun {
    std::size_t start = 0;
    std::size_t end = 0;
    std::uint32_t horizontalQuad = 0;
    std::uint32_t verticalQuad = 0;
    Rect clip{};
  };

  Rect intersectRects(Rect a, Rect b) const {
    return lambda::intersectRects(a, b);
  }

  Rect quadBounds(std::uint32_t first, std::uint32_t count) const {
    Rect bounds = Rect::sharp(0.f, 0.f, 0.f, 0.f);
    bool hasBounds = false;
    std::uint32_t const end = std::min<std::uint32_t>(first + count, static_cast<std::uint32_t>(quads_.size()));
    for (std::uint32_t index = first; index < end; ++index) {
      QuadInstance const &q = quads_[index];
      Point const p00{q.axisX[0], q.axisX[1]};
      Point const p10{q.axisX[0] + q.axisX[2], q.axisX[1] + q.axisX[3]};
      Point const p01{q.axisX[0] + q.axisY[0], q.axisX[1] + q.axisY[1]};
      Point const p11{p10.x + q.axisY[0], p10.y + q.axisY[1]};
      float const x0 = std::min({p00.x, p10.x, p01.x, p11.x});
      float const y0 = std::min({p00.y, p10.y, p01.y, p11.y});
      float const x1 = std::max({p00.x, p10.x, p01.x, p11.x});
      float const y1 = std::max({p00.y, p10.y, p01.y, p11.y});
      Rect const next = Rect::sharp(x0, y0, std::max(0.f, x1 - x0), std::max(0.f, y1 - y0));
      bounds = hasBounds ? unionRects(bounds, next) : next;
      hasBounds = true;
    }
    return hasBounds ? bounds : Rect::sharp(0.f, 0.f, 0.f, 0.f);
  }

  Rect rectInstanceBounds(RectInstance const& r) const {
    Point const p00{r.axisX[0], r.axisX[1]};
    Point const p10{r.axisX[0] + r.axisX[2], r.axisX[1] + r.axisX[3]};
    Point const p01{r.axisX[0] + r.axisY[0], r.axisX[1] + r.axisY[1]};
    Point const p11{p10.x + r.axisY[0], p10.y + r.axisY[1]};
    float const x0 = std::min({p00.x, p10.x, p01.x, p11.x});
    float const y0 = std::min({p00.y, p10.y, p01.y, p11.y});
    float const x1 = std::max({p00.x, p10.x, p01.x, p11.x});
    float const y1 = std::max({p00.y, p10.y, p01.y, p11.y});
    return Rect::sharp(x0, y0, std::max(0.f, x1 - x0), std::max(0.f, y1 - y0));
  }

  Rect rectBounds(std::uint32_t first, std::uint32_t count) const {
    Rect bounds = Rect::sharp(0.f, 0.f, 0.f, 0.f);
    bool hasBounds = false;
    std::uint32_t const end = std::min<std::uint32_t>(first + count, static_cast<std::uint32_t>(rects_.size()));
    for (std::uint32_t index = first; index < end; ++index) {
      Rect const next = rectInstanceBounds(rects_[index]);
      bounds = hasBounds ? unionRects(bounds, next) : next;
      hasBounds = true;
    }
    return hasBounds ? bounds : Rect::sharp(0.f, 0.f, 0.f, 0.f);
  }

  Rect calloutBounds(std::uint32_t first, std::uint32_t count) const {
    Rect bounds = Rect::sharp(0.f, 0.f, 0.f, 0.f);
    bool hasBounds = false;
    std::uint32_t const end = std::min<std::uint32_t>(first + count, static_cast<std::uint32_t>(callouts_.size()));
    for (std::uint32_t index = first; index < end; ++index) {
      CalloutInstance const& c = callouts_[index];
      RectInstance r{};
      r.axisX[0] = c.axisX[0];
      r.axisX[1] = c.axisX[1];
      r.axisX[2] = c.axisX[2];
      r.axisX[3] = c.axisX[3];
      r.axisY[0] = c.axisY[0];
      r.axisY[1] = c.axisY[1];
      Rect const next = rectInstanceBounds(r);
      bounds = hasBounds ? unionRects(bounds, next) : next;
      hasBounds = true;
    }
    return hasBounds ? bounds : Rect::sharp(0.f, 0.f, 0.f, 0.f);
  }

  Rect pathBounds(std::uint32_t first, std::uint32_t count) const {
    std::uint32_t const end = std::min<std::uint32_t>(first + count, static_cast<std::uint32_t>(pathVerts_.size()));
    if (first >= end) {
      return Rect::sharp(0.f, 0.f, 0.f, 0.f);
    }
    float x0 = pathVerts_[first].x;
    float y0 = pathVerts_[first].y;
    float x1 = x0;
    float y1 = y0;
    for (std::uint32_t index = first + 1; index < end; ++index) {
      x0 = std::min(x0, pathVerts_[index].x);
      y0 = std::min(y0, pathVerts_[index].y);
      x1 = std::max(x1, pathVerts_[index].x);
      y1 = std::max(y1, pathVerts_[index].y);
    }
    return Rect::sharp(x0, y0, std::max(0.f, x1 - x0), std::max(0.f, y1 - y0));
  }

  bool drawOpBounds(DrawOp const& op, Rect& bounds) const {
    if (op.externalStorageDescriptor || op.externalVertexBuffer) {
      return false;
    }
    switch (op.kind) {
    case DrawOp::Kind::Rect:
      bounds = rectBounds(op.first, op.count);
      return op.first + op.count <= rects_.size();
    case DrawOp::Kind::Callout:
      bounds = calloutBounds(op.first, op.count);
      return op.first + op.count <= callouts_.size();
    case DrawOp::Kind::Path:
      bounds = pathBounds(op.first, op.count);
      return op.first + op.count <= pathVerts_.size();
    case DrawOp::Kind::Image:
    case DrawOp::Kind::BackdropBlur:
      bounds = quadBounds(op.first, op.count);
      return op.first + op.count <= quads_.size();
    }
    return false;
  }

  bool drawOpAffectsBackdropClip(DrawOp const& op, Rect const& clip) const {
    Rect bounds{};
    if (!drawOpBounds(op, bounds)) {
      return true;
    }
    Rect const clipped = intersectRects(bounds, op.clip);
    return clipped.width > 0.f && clipped.height > 0.f && clipped.intersects(clip);
  }

  Rect backdropBlurRunClip(std::size_t start, std::size_t end, float radiusPx) const {
    Rect bounds = Rect::sharp(0.f, 0.f, 0.f, 0.f);
    bool hasBounds = false;
    std::size_t const opEnd = std::min(end, ops_.size());
    for (std::size_t index = std::min(start, opEnd); index < opEnd; ++index) {
      DrawOp const &op = ops_[index];
      if (op.kind != DrawOp::Kind::BackdropBlur) continue;
      Rect const blurBounds = op.hasBlurCacheClip ? op.blurCacheClip : quadBounds(op.first, op.count);
      Rect opBounds = intersectRects(blurBounds, op.clip);
      if (opBounds.width <= 0.f || opBounds.height <= 0.f) continue;
      bounds = hasBounds ? unionRects(bounds, opBounds) : opBounds;
      hasBounds = true;
    }
    if (!hasBounds) {
      return Rect::sharp(0.f, 0.f, static_cast<float>(width_), static_cast<float>(height_));
    }
    float const scale = std::max(0.001f, std::max(dpiScaleX_, dpiScaleY_));
    float const pad = std::ceil(radiusPx / scale) + 2.f;
    Rect const padded = Rect::sharp(bounds.x - pad,
                                    bounds.y - pad,
                                    bounds.width + pad * 2.f,
                                    bounds.height + pad * 2.f);
    return intersectRects(padded, Rect::sharp(0.f, 0.f, static_cast<float>(width_), static_cast<float>(height_)));
  }

  std::size_t nextBackdropBlurOp(std::size_t start = 0) const {
    std::size_t const begin = std::min(start, ops_.size());
    auto const it = std::find_if(ops_.begin() + static_cast<std::ptrdiff_t>(begin),
                                 ops_.end(),
                                 [](DrawOp const &op) {
      return op.kind == DrawOp::Kind::BackdropBlur;
    });
    return it == ops_.end() ? ops_.size() : static_cast<std::size_t>(std::distance(ops_.begin(), it));
  }

  std::size_t backdropBlurRunEnd(std::size_t start) const {
    std::size_t end = start;
    while (end < ops_.size() && ops_[end].kind == DrawOp::Kind::BackdropBlur) {
      ++end;
    }
    return end;
  }

  std::pair<std::uint64_t, std::uint64_t> backdropBlurRunOpCounts(std::size_t start, std::size_t end) const {
    std::uint64_t ops = 0;
    std::uint64_t quads = 0;
    std::size_t const opEnd = std::min(end, ops_.size());
    for (std::size_t index = std::min(start, opEnd); index < opEnd; ++index) {
      DrawOp const& op = ops_[index];
      if (op.kind != DrawOp::Kind::BackdropBlur) {
        continue;
      }
      ++ops;
      quads += op.count;
    }
    return {ops, quads};
  }

  float maxBackdropBlurRadius(std::size_t start, std::size_t end) const {
    float radius = 0.f;
    std::size_t const opEnd = std::min(end, ops_.size());
    for (std::size_t index = std::min(start, opEnd); index < opEnd; ++index) {
      DrawOp const &op = ops_[index];
      if (op.kind == DrawOp::Kind::BackdropBlur) {
        radius = std::max(radius, op.blurRadius);
      }
    }
    return radius;
  }

  VkExtent2D backdropTextureExtent() const {
    return VkExtent2D{
        static_cast<std::uint32_t>(std::max(1, backdropBlurTexture_.width)),
        static_cast<std::uint32_t>(std::max(1, backdropBlurTexture_.height)),
    };
  }

  std::uint32_t backdropBlurDownsample() const {
    float const scale = std::max(dpiScaleX_, dpiScaleY_);
    float const scaleFactor = std::max(1.f, scale);
    long const scaledDownsample =
        std::lround(static_cast<float>(backdropBlurBaseDownsample_) * scaleFactor);
    return static_cast<std::uint32_t>(
        std::clamp<long>(scaledDownsample, kMinBackdropBlurBaseDownsample, kMaxBackdropBlurDownsample));
  }

  void hashTextureReference(std::uint64_t &h, Texture const *texture) const {
    auto image = reinterpret_cast<std::uintptr_t>(texture ? texture->image : VK_NULL_HANDLE);
    auto view = reinterpret_cast<std::uintptr_t>(texture ? texture->view : VK_NULL_HANDLE);
    auto descriptor = reinterpret_cast<std::uintptr_t>(texture ? texture->descriptor : VK_NULL_HANDLE);
    hashValue(h, image);
    hashValue(h, view);
    hashValue(h, descriptor);
    if (texture) {
      hashValue(h, texture->format);
      hashValue(h, texture->layout);
      hashValue(h, texture->width);
      hashValue(h, texture->height);
      hashValue(h, texture->contentGeneration);
    }
  }

  template <typename Rects, typename Quads, typename PathVerts>
  std::uint64_t drawOpGeometrySignature(DrawOp const &op,
                                        Rects const &rects,
                                        Quads const &quads,
                                        PathVerts const &pathVerts) const {
    std::uint64_t h = 14695981039346656037ULL;
    hashValue(h, op.kind);
    hashValue(h, op.count);
    hashValue(h, op.clip);
    hashValue(h, op.blurRadius);
    hashValue(h, op.blurCacheClip);
    hashValue(h, op.hasBlurCacheClip);
    hashValue(h, reinterpret_cast<std::uintptr_t>(op.externalStorageDescriptor));
    hashValue(h, reinterpret_cast<std::uintptr_t>(op.externalVertexBuffer));
    hashValue(h, op.externalTranslationX);
    hashValue(h, op.externalTranslationY);
    hashValue(h, op.premultipliedAlpha);
    hashValue(h, reinterpret_cast<std::uintptr_t>(op.sourceImage));
    hashTextureReference(h, op.texture);

    std::uint32_t const end = op.first + op.count;
    switch (op.kind) {
    case DrawOp::Kind::Rect:
      for (std::uint32_t index = op.first; index < end && index < rects.size(); ++index) {
        hashValue(h, rects[index]);
      }
      break;
    case DrawOp::Kind::Callout:
      for (std::uint32_t index = op.first; index < end && index < callouts_.size(); ++index) {
        hashValue(h, callouts_[index]);
      }
      break;
    case DrawOp::Kind::Path:
      for (std::uint32_t index = op.first; index < end && index < pathVerts.size(); ++index) {
        hashValue(h, pathVerts[index]);
      }
      break;
    case DrawOp::Kind::Image:
    case DrawOp::Kind::BackdropBlur:
      for (std::uint32_t index = op.first; index < end && index < quads.size(); ++index) {
        hashValue(h, quads[index]);
      }
      break;
    }
    return nonZeroSignature(h);
  }

  void prepareRecordedGeometrySignatures(VulkanFrameRecorder &recorded) const {
    if (recorded.geometrySignaturesPrepared) {
      return;
    }
    for (DrawOp &op : recorded.ops) {
      op.geometrySignature =
          drawOpGeometrySignature(op, recorded.rects, recorded.quads, recorded.pathVerts);
    }
    recorded.geometrySignaturesPrepared = true;
  }

  void hashDrawOpGeometry(std::uint64_t &h, DrawOp const &op) const {
    if (op.geometrySignature != 0) {
      hashValue(h, op.geometrySignature);
      return;
    }
    hashValue(h, drawOpGeometrySignature(op, rects_, quads_, pathVerts_));
  }

  std::uint64_t backdropBlurSignature(BackdropBlurRun const &run, std::size_t runIndex) const {
    std::uint64_t h = 14695981039346656037ULL;
    hashValue(h, runIndex);
    hashValue(h, width_);
    hashValue(h, height_);
    hashValue(h, framebufferWidth_);
    hashValue(h, framebufferHeight_);
    hashValue(h, swapExtent_.width);
    hashValue(h, swapExtent_.height);
    hashValue(h, clearColor_);
    hashValue(h, run.clip);
    hashValue(h, kBackdropBlurIterations);
    hashValue(h, backdropBlurDownsample());
    hashValue(h, kBackdropBlurRadiusBoost);
    if (run.horizontalQuad < quads_.size()) {
      hashValue(h, quads_[run.horizontalQuad]);
    }
    if (run.verticalQuad < quads_.size()) {
      hashValue(h, quads_[run.verticalQuad]);
    }
    std::size_t const opEnd = std::min(run.start, ops_.size());
    std::uint64_t affectingOps = 0;
    for (std::size_t index = 0; index < opEnd; ++index) {
      DrawOp const& op = ops_[index];
      if (!drawOpAffectsBackdropClip(op, run.clip)) {
        continue;
      }
      hashDrawOpGeometry(h, op);
      ++affectingOps;
    }
    hashValue(h, affectingOps);
    return h;
  }

  std::uint64_t renderTargetFrameSignature() const {
    std::uint64_t h = 14695981039346656037ULL;
    hashValue(h, reinterpret_cast<std::uintptr_t>(targetSpec_.image));
    hashValue(h, reinterpret_cast<std::uintptr_t>(targetSpec_.view));
    hashValue(h, targetSpec_.format);
    hashValue(h, targetSpec_.width);
    hashValue(h, targetSpec_.height);
    hashValue(h, targetSpec_.initialLayout);
    hashValue(h, targetSpec_.finalLayout);
    hashValue(h, targetSpec_.preserveContents);
    hashValue(h, width_);
    hashValue(h, height_);
    hashValue(h, framebufferWidth_);
    hashValue(h, framebufferHeight_);
    hashValue(h, dpiScaleX_);
    hashValue(h, dpiScaleY_);
    hashValue(h, backdropBlurBaseDownsample_);
    hashValue(h, clearColor_);
    hashValue(h, resources().atlasGeneration);
    hashValue(h, resources().atlasDirty);
    hashValue(h, ops_.size());
    for (DrawOp const &op : ops_) {
      hashDrawOpGeometry(h, op);
    }
    return h;
  }

  CachedBackdropBlur &ensureBackdropBlurCacheEntry(std::size_t index) {
    if (backdropBlurCache_.size() <= index) {
      backdropBlurCache_.resize(index + 1);
    }
    CachedBackdropBlur &entry = backdropBlurCache_[index];
    VkExtent2D const extent = backdropTextureExtent();
    VkFormat const backdropFormat = backdropRenderTargetFormat();
    if (!entry.texture.image ||
        entry.texture.width != static_cast<int>(extent.width) ||
        entry.texture.height != static_cast<int>(extent.height) ||
        entry.texture.format != backdropFormat) {
      destroyTexture(entry.texture);
      createRenderTargetTexture(entry.texture,
                                static_cast<int>(extent.width),
                                static_cast<int>(extent.height),
                                backdropFormat);
      entry.valid = false;
      entry.signature = 0;
    }
    return entry;
  }

  std::vector<BackdropBlurRun> prepareBackdropBlurRuns() {
    std::vector<BackdropBlurRun> runs;
    std::size_t cursor = 0;
    while (cursor < ops_.size()) {
      std::size_t const start = nextBackdropBlurOp(cursor);
      if (start >= ops_.size()) break;
      if (runs.empty()) {
        ensureBackdropSceneTarget();
      }
      std::size_t const end = backdropBlurRunEnd(start);
      auto const [opCount, quadCount] = backdropBlurRunOpCounts(start, end);
      float const maxRadius = maxBackdropBlurRadius(start, end);
      float const effectiveRadius = maxRadius * kBackdropBlurRadiusBoost;
      float const blurRadius = (effectiveRadius / static_cast<float>(backdropBlurDownsample())) /
                               std::sqrt(static_cast<float>(kBackdropBlurIterations));
      BackdropBlurRun run{
          .start = start,
          .end = end,
          .horizontalQuad = appendBackdropBlurQuad(blurRadius, 1.f, 0.f),
          .verticalQuad = appendBackdropBlurQuad(blurRadius, 0.f, 1.f),
          .clip = backdropBlurRunClip(start, end, effectiveRadius),
      };
      debug::perf::recordBackdropBlurPreparedRun(opCount, quadCount);
      runs.push_back(run);
      cursor = end;
    }
    return runs;
  }

  VulkanDrawPushConstants drawPushConstants(float translationX = 0.f,
                                            float translationY = 0.f) const {
    VulkanDrawPushConstants push{};
    push.viewport[0] = static_cast<float>(width_);
    push.viewport[1] = static_cast<float>(height_);
    push.translation[0] = translationX;
    push.translation[1] = translationY;
    return push;
  }
