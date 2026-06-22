
  void save() override { stateStack_.push_back(state_); }
  void restore() override {
    if (!stateStack_.empty()) {
      state_ = stateStack_.back();
      stateStack_.pop_back();
    }
  }
  void setTransform(Mat3 const &m) override { state_.transform = m; }
  void transform(Mat3 const &m) override { state_.transform = state_.transform * m; }
  void translate(Point p) override { transform(Mat3::translate(p)); }
  void translate(float x, float y) override { translate({x, y}); }
  void scale(float sx, float sy) override { transform(Mat3::scale(sx, sy)); }
  void scale(float s) override { scale(s, s); }
  void rotate(float r) override { transform(Mat3::rotate(r)); }
  void rotate(float r, Point p) override { transform(Mat3::rotate(r, p)); }
  Mat3 currentTransform() const override { return state_.transform; }

  void clipRect(Rect rect, CornerRadius const &cornerRadius, bool) override {
    Rect r = transformedBounds(rect);
    Rect const effective = lambda::intersectRects(state_.clip, r);
    state_.clip = effective;
    if (!cornerRadius.isZero() && r.width > 0.f && r.height > 0.f) {
      float const sx = std::hypot(state_.transform.m[0], state_.transform.m[1]);
      float const sy = std::hypot(state_.transform.m[3], state_.transform.m[4]);
      float const radiusScale = std::max(0.f, std::min(sx, sy));
      CornerRadius radii{
          cornerRadius.topLeft * radiusScale,
          cornerRadius.topRight * radiusScale,
          cornerRadius.bottomRight * radiusScale,
          cornerRadius.bottomLeft * radiusScale,
      };
      constexpr float eps = 1e-3f;
      bool const clipped =
          std::abs(effective.x - r.x) > eps || std::abs(effective.y - r.y) > eps ||
          std::abs(effective.width - r.width) > eps || std::abs(effective.height - r.height) > eps;
      if (clipped) {
        radii = cornerRadiiAfterAxisAlignedClip(r, effective, radii);
      }
      CornerRadius const adjusted = clampRadii(radii, effective.width, effective.height);
      if (!adjusted.isZero() && effective.width > 0.f && effective.height > 0.f &&
          state_.roundedClipCount < kVulkanRoundedClipMaskCapacity) {
        state_.roundedClips[state_.roundedClipCount++] = RoundedClipState{
            .rect = effective,
            .radii = adjusted,
        };
      }
    }
  }
  Rect clipBounds() const override { return state_.clip; }
  bool quickReject(Rect rect) const override { return !state_.clip.intersects(transformedBounds(rect)); }
  void setOpacity(float opacity) override { state_.opacity = std::clamp(opacity, 0.f, 1.f); }
  float opacity() const override { return state_.opacity; }
  void setBlendMode(BlendMode mode) override { state_.blendMode = mode; }
  BlendMode blendMode() const override { return state_.blendMode; }

  void pushRectInstance(Rect const &rect, CornerRadius const &cornerRadius, FillStyle const &fill,
                        StrokeStyle const &stroke, float opacity) {
    if (rect.width <= 0.f || rect.height <= 0.f)
      return;
    Point const p0 = state_.transform.apply({rect.x, rect.y});
    Point const p1 = state_.transform.apply({rect.x + rect.width, rect.y});
    Point const p3 = state_.transform.apply({rect.x, rect.y + rect.height});
    RectInstance inst{};
    inst.rect[0] = 0.f;
    inst.rect[1] = 0.f;
    inst.rect[2] = rect.width;
    inst.rect[3] = rect.height;
    inst.axisX[0] = p0.x;
    inst.axisX[1] = p0.y;
    inst.axisX[2] = p1.x - p0.x;
    inst.axisX[3] = p1.y - p0.y;
    inst.axisY[0] = p3.x - p0.x;
    inst.axisY[1] = p3.y - p0.y;
    CornerRadius cr = clampRadii(cornerRadius, rect.width, rect.height);
    inst.radii[0] = cr.topLeft;
    inst.radii[1] = cr.topRight;
    inst.radii[2] = cr.bottomRight;
    inst.radii[3] = cr.bottomLeft;
    encodeFill(fill, inst);
    Color sc{};
    if (stroke.solidColor(&sc) && stroke.width > 0.f) {
      putColor(inst.stroke, sc, 1.f);
      inst.params[2] = stroke.width;
    }
    inst.params[3] = opacity;
    applyCurrentRoundedClip(inst);
    RecordingTarget target = recordingTarget();
    std::uint32_t first = static_cast<std::uint32_t>(target.rects.size());
    target.rects.push_back(inst);
    appendSignedDrawOp(target, makeDrawOp(DrawOp::Kind::Rect, nullptr, first, 1));
    debug::perf::recordDrawCall(debug::perf::RenderCounterKind::Rect);
  }

  void drawRect(Rect const &rect, CornerRadius const &cornerRadius, FillStyle const &fill,
                StrokeStyle const &stroke, ShadowStyle const &shadow) override {
    if (rect.width <= 0.f || rect.height <= 0.f)
      return;
    Rect rejectBounds = transformedBounds(rect);
    if (!shadow.isNone()) {
      float const pad = shadow.radius;
      Rect const shadowRect = Rect::sharp(rect.x + shadow.offset.x - pad, rect.y + shadow.offset.y - pad,
                                          rect.width + pad * 2.f, rect.height + pad * 2.f);
      rejectBounds = unionRects(rejectBounds, transformedBounds(shadowRect));
    }
    if (rejectBounds.width <= 0.f || rejectBounds.height <= 0.f || !state_.clip.intersects(rejectBounds))
      return;

    if (!shadow.isNone()) {
      if (shadow.radius <= 0.f) {
        Rect const layer = Rect::sharp(rect.x + shadow.offset.x, rect.y + shadow.offset.y,
                                       rect.width, rect.height);
        pushRectInstance(layer, cornerRadius, FillStyle::solid(shadow.color), StrokeStyle::none(), state_.opacity);
      } else {
        int const steps = std::clamp(static_cast<int>(std::ceil(shadow.radius / 3.f)), 3, 8);
        for (int i = steps; i >= 1; --i) {
          float const t = static_cast<float>(i) / static_cast<float>(steps);
          float const spread = shadow.radius * t;
          float const alpha = shadow.color.a * state_.opacity * (1.f - t * 0.72f) / static_cast<float>(steps);
          Color c = shadow.color;
          c.a = alpha;
          Rect const layer = Rect::sharp(rect.x + shadow.offset.x - spread,
                                         rect.y + shadow.offset.y - spread,
                                         rect.width + spread * 2.f,
                                         rect.height + spread * 2.f);
          CornerRadius cr{cornerRadius.topLeft + spread, cornerRadius.topRight + spread,
                          cornerRadius.bottomRight + spread, cornerRadius.bottomLeft + spread};
          pushRectInstance(layer, cr, FillStyle::solid(c), StrokeStyle::none(), 1.f);
        }
      }
    }

    pushRectInstance(rect, cornerRadius, fill, stroke, state_.opacity);
  }

  void drawLine(Point from, Point to, StrokeStyle const &stroke) override {
    if (stroke.isNone())
      return;
    Point a = state_.transform.apply(from);
    Point b = state_.transform.apply(to);
    if (!clipLineToCurrentClip(a, b))
      return;
    StrokeStyle scaled = stroke;
    float const sx = std::hypot(state_.transform.m[0], state_.transform.m[1]);
    float const sy = std::hypot(state_.transform.m[3], state_.transform.m[4]);
    float const s = (sx > 0.f || sy > 0.f) ? (sx + sy) * 0.5f : 1.f;
    scaled.width *= s;
    Path path;
    path.moveTo(a);
    path.lineTo(b);
    DrawState saved = state_;
    state_.transform = Mat3::identity();
    appendPath(path, FillStyle::none(), scaled);
    state_ = saved;
  }
  void drawPath(Path const &path, FillStyle const &fill, StrokeStyle const &stroke, ShadowStyle const &shadow) override {
    if (!shadow.isNone()) {
      DrawState saved = state_;
      state_.transform = state_.transform * Mat3::translate(shadow.offset);
      appendPath(path, FillStyle::solid(shadow.color), StrokeStyle::none());
      state_ = saved;
    }
    appendPath(path, fill, stroke);
  }
  void drawCircle(Point center, float radius, FillStyle const &fill, StrokeStyle const &stroke) override {
    Rect r{center.x - radius, center.y - radius, radius * 2.f, radius * 2.f};
    drawRect(r, CornerRadius::pill(r), fill, stroke, ShadowStyle::none());
  }

  void drawTextLayout(TextLayout const &layout, Point origin) override {
    try {
      ensureAtlasDescriptor();
    } catch (std::exception const &e) {
      std::fprintf(stderr, "Lambda Vulkan: glyph atlas descriptor setup failed: %s\n", e.what());
      return;
    }
    RecordingTarget target = recordingTarget();
    std::uint32_t first = static_cast<std::uint32_t>(target.quads.size());
    for (TextLayout::PlacedRun const &placed : layout.runs) {
      std::size_t const glyphCount = std::min(placed.run.glyphIds.size(), placed.run.positions.size());
      for (std::size_t i = 0; i < glyphCount; ++i) {
        GlyphSlot const *slot = nullptr;
        try {
          slot = glyphSlot(placed.run.fontId, placed.run.glyphIds[i], placed.run.fontSize);
        } catch (std::exception const &e) {
          std::fprintf(stderr, "Lambda Vulkan: glyph atlas update failed: %s\n", e.what());
          continue;
        }
        if (!slot || slot->w == 0 || slot->h == 0)
          continue;
        Point pos = origin + placed.origin + placed.run.positions[i];
        Rect glyphRect = Rect::sharp(pos.x + slot->bearing.x / dpiScaleX_,
                                     pos.y - slot->bearing.y / dpiScaleY_,
                                     static_cast<float>(slot->w) / dpiScaleX_,
                                     static_cast<float>(slot->h) / dpiScaleY_);
        Point p00 = state_.transform.apply({glyphRect.x, glyphRect.y});
        Point p10 = state_.transform.apply({glyphRect.x + glyphRect.width, glyphRect.y});
        Point p01 = state_.transform.apply({glyphRect.x, glyphRect.y + glyphRect.height});
        Point const axisX = {p10.x - p00.x, p10.y - p00.y};
        Point const axisY = {p01.x - p00.x, p01.y - p00.y};
        p00.x = std::round(p00.x * dpiScaleX_) / dpiScaleX_;
        p00.y = std::round(p00.y * dpiScaleY_) / dpiScaleY_;
        p10 = {p00.x + axisX.x, p00.y + axisX.y};
        p01 = {p00.x + axisY.x, p00.y + axisY.y};
        QuadInstance q{};
        q.rect[0] = 0.f;
        q.rect[1] = 0.f;
        q.rect[2] = glyphRect.width;
        q.rect[3] = glyphRect.height;
        q.axisX[0] = p00.x;
        q.axisX[1] = p00.y;
        q.axisX[2] = p10.x - p00.x;
        q.axisX[3] = p10.y - p00.y;
        q.axisY[0] = p01.x - p00.x;
        q.axisY[1] = p01.y - p00.y;
        q.uv[0] = slot->u0;
        q.uv[1] = slot->v0;
        q.uv[2] = slot->u1;
        q.uv[3] = slot->v1;
        putColor(q.color, placed.run.color, state_.opacity);
        applyCurrentRoundedClip(q);
        target.quads.push_back(q);
      }
    }
    std::uint32_t count = static_cast<std::uint32_t>(target.quads.size()) - first;
    if (count > 0) {
      Texture *atlas = &resources().atlas;
      if (captureTarget_) {
        captureTarget_->glyphAtlasGeneration = resources().atlasGeneration;
      } else {
        batches_.push_back(ImageBatch{atlas, first, count});
      }
      appendSignedDrawOp(target, makeDrawOp(DrawOp::Kind::Image, atlas, first, count));
      debug::perf::recordDrawCall(debug::perf::RenderCounterKind::Glyph);
    }
  }

  void drawImage(Image const &image, Rect const &src, Rect const &dst, CornerRadius const &corners, float opacity) override {
    auto const *vi = dynamic_cast<VulkanImage const *>(&image);
    if (!vi || src.width <= 0.f || src.height <= 0.f || dst.width <= 0.f || dst.height <= 0.f)
      return;
    Texture *texture = nullptr;
    try {
      texture = ensureImageTexture(*vi);
    } catch (std::exception const &e) {
      std::fprintf(stderr, "Lambda Vulkan: image texture upload failed: %s\n", e.what());
      return;
    }
    if (!texture)
      return;
    Point p00 = state_.transform.apply({dst.x, dst.y});
    Point p10 = state_.transform.apply({dst.x + dst.width, dst.y});
    Point p01 = state_.transform.apply({dst.x, dst.y + dst.height});
    Size sz = image.size();
    float const u0 = src.x / sz.width;
    float const v0 = src.y / sz.height;
    float const u1 = (src.x + src.width) / sz.width;
    float const v1 = (src.y + src.height) / sz.height;
    QuadInstance q{};
    q.rect[0] = 0.f;
    q.rect[1] = 0.f;
    q.rect[2] = dst.width;
    q.rect[3] = dst.height;
    q.axisX[0] = p00.x;
    q.axisX[1] = p00.y;
    q.axisX[2] = p10.x - p00.x;
    q.axisX[3] = p10.y - p00.y;
    q.axisY[0] = p01.x - p00.x;
    q.axisY[1] = p01.y - p00.y;
    q.uv[0] = u0;
    q.uv[1] = v0;
    q.uv[2] = u1;
    q.uv[3] = v1;
    q.color[0] = q.color[1] = q.color[2] = 1.f;
    q.color[3] = opacity * state_.opacity;
    CornerRadius cr = clampRadii(corners, dst.width, dst.height);
    q.radii[0] = cr.topLeft;
    q.radii[1] = cr.topRight;
    q.radii[2] = cr.bottomRight;
    q.radii[3] = cr.bottomLeft;
    applyCurrentRoundedClip(q);
    RecordingTarget target = recordingTarget();
    std::uint32_t first = static_cast<std::uint32_t>(target.quads.size());
    target.quads.push_back(q);
    if (!captureTarget_) {
      batches_.push_back(ImageBatch{texture, first, 1});
    }
    DrawOp op = makeDrawOp(DrawOp::Kind::Image, texture, first, 1);
    op.sourceImage = vi;
    try {
      op.sourceImageRef = image.shared_from_this();
    } catch (std::bad_weak_ptr const&) {
      op.sourceImageRef.reset();
    }
    op.premultipliedAlpha = imagePremultipliedAlpha_;
    appendSignedDrawOp(target, op);
    debug::perf::recordDrawCall(debug::perf::RenderCounterKind::Image);
  }

  void drawImageTiled(Image const &image, Rect const &dst, CornerRadius const &corners, float opacity) override {
    Size sz = image.size();
    if (sz.width <= 0.f || sz.height <= 0.f || dst.width <= 0.f || dst.height <= 0.f)
      return;
    int const cols = static_cast<int>(std::ceil(dst.width / sz.width));
    int const rows = static_cast<int>(std::ceil(dst.height / sz.height));
    for (int row = 0; row < rows; ++row) {
      for (int col = 0; col < cols; ++col) {
        Rect tile = Rect::sharp(dst.x + static_cast<float>(col) * sz.width,
                                dst.y + static_cast<float>(row) * sz.height,
                                std::min(sz.width, dst.x + dst.width - (dst.x + static_cast<float>(col) * sz.width)),
                                std::min(sz.height, dst.y + dst.height - (dst.y + static_cast<float>(row) * sz.height)));
        Rect src = Rect::sharp(0.f, 0.f, tile.width, tile.height);
        drawImage(image, src, tile, corners, opacity);
      }
    }
  }

  void drawBackdropBlur(Rect const &rect, float radius, Color tint, CornerRadius const &corners) override {
    drawBackdropBlurCached(rect, rect, radius, tint, corners);
  }

  void drawBackdropBlurCached(Rect const &rect,
                              Rect const &cacheRect,
                              float radius,
                              Color tint,
                              CornerRadius const &corners) override {
    if (radius <= 0.f || rect.width <= 0.f || rect.height <= 0.f)
      return;
    Rect const bounds = transformedBounds(rect);
    if (!state_.clip.intersects(bounds))
      return;

    RecordingTarget target = recordingTarget();
    std::uint32_t first = static_cast<std::uint32_t>(target.quads.size());
    appendBackdropBlurQuadInstance(target, rect, tint, corners);
    DrawOp op = makeDrawOp(DrawOp::Kind::BackdropBlur, nullptr, first, 1);
    op.blurRadius = radius * std::max(dpiScaleX_, dpiScaleY_);
    Rect const cacheBounds = transformedBounds(cacheRect);
    if (cacheBounds.width > 0.f && cacheBounds.height > 0.f) {
      op.blurCacheClip = cacheBounds;
      op.hasBlurCacheClip = true;
    }
    appendSignedDrawOp(target, op);
  }

  bool drawBackdropBlurFrame(Rect const& frame,
                             CornerRadius const& frameRadius,
                             Rect const& cutout,
                             float radius,
                             Color tint) {
    if (radius <= 0.f || frame.width <= 0.f || frame.height <= 0.f) {
      return false;
    }
    if (!state_.clip.intersects(transformedBounds(frame))) {
      return true;
    }

    float const left = std::clamp(cutout.x - frame.x, 0.f, frame.width);
    float const top = std::clamp(cutout.y - frame.y, 0.f, frame.height);
    float const right = std::clamp(frame.x + frame.width - (cutout.x + cutout.width), 0.f, frame.width);
    float const bottom = std::clamp(frame.y + frame.height - (cutout.y + cutout.height), 0.f, frame.height);

    RecordingTarget target = recordingTarget();
    std::uint32_t const first = static_cast<std::uint32_t>(target.quads.size());
    if (top > 0.f) {
      appendBackdropBlurQuadInstance(target,
                                     Rect::sharp(frame.x, frame.y, frame.width, top),
                                     tint,
                                     CornerRadius{frameRadius.topLeft, frameRadius.topRight, 0.f, 0.f});
    }
    if (left > 0.f && cutout.height > 0.f) {
      appendBackdropBlurQuadInstance(target,
                                     Rect::sharp(frame.x, cutout.y, left, cutout.height),
                                     tint,
                                     CornerRadius{});
    }
    if (right > 0.f && cutout.height > 0.f) {
      appendBackdropBlurQuadInstance(target,
                                     Rect::sharp(cutout.x + cutout.width, cutout.y, right, cutout.height),
                                     tint,
                                     CornerRadius{});
    }
    if (bottom > 0.f) {
      appendBackdropBlurQuadInstance(target,
                                     Rect::sharp(frame.x, cutout.y + cutout.height, frame.width, bottom),
                                     tint,
                                     CornerRadius{0.f, 0.f, frameRadius.bottomRight, frameRadius.bottomLeft});
    }

    std::uint32_t const count = static_cast<std::uint32_t>(target.quads.size()) - first;
    if (count == 0) {
      return true;
    }
    DrawOp op = makeDrawOp(DrawOp::Kind::BackdropBlur, nullptr, first, count);
    op.blurRadius = radius * std::max(dpiScaleX_, dpiScaleY_);
    Rect const cacheBounds = transformedBounds(frame);
    if (cacheBounds.width > 0.f && cacheBounds.height > 0.f) {
      op.blurCacheClip = cacheBounds;
      op.hasBlurCacheClip = true;
    }
    appendSignedDrawOp(target, op);
    return true;
  }

  bool drawCalloutMaterial(Rect const &bounds,
                           Rect const &card,
                           CornerRadius const &corners,
                           Color baseColor,
                           Color tintColor,
                           Color borderColor,
                           float borderWidth,
                           VulkanCalloutPlacement placement,
                           float arrowWidth,
                           float) {
    if (captureTarget_ || bounds.width <= 0.f || bounds.height <= 0.f || card.width <= 0.f || card.height <= 0.f)
      return false;
    Rect const transformed = transformedBounds(bounds);
    if (!state_.clip.intersects(transformed))
      return true;

    Point p00 = state_.transform.apply({bounds.x, bounds.y});
    Point p10 = state_.transform.apply({bounds.x + bounds.width, bounds.y});
    Point p01 = state_.transform.apply({bounds.x, bounds.y + bounds.height});
    CalloutInstance inst{};
    inst.rect[0] = 0.f;
    inst.rect[1] = 0.f;
    inst.rect[2] = bounds.width;
    inst.rect[3] = bounds.height;
    inst.axisX[0] = p00.x;
    inst.axisX[1] = p00.y;
    inst.axisX[2] = p10.x - p00.x;
    inst.axisX[3] = p10.y - p00.y;
    inst.axisY[0] = p01.x - p00.x;
    inst.axisY[1] = p01.y - p00.y;
    inst.card[0] = card.x - bounds.x;
    inst.card[1] = card.y - bounds.y;
    inst.card[2] = card.width;
    inst.card[3] = card.height;
    CornerRadius cr = clampRadii(corners, card.width, card.height);
    inst.radii[0] = cr.topLeft;
    inst.radii[1] = cr.topRight;
    inst.radii[2] = cr.bottomRight;
    inst.radii[3] = cr.bottomLeft;
    putColor(inst.base, baseColor, state_.opacity);
    putColor(inst.tint, tintColor, state_.opacity);
    putColor(inst.stroke, borderColor, state_.opacity);
    inst.params[0] = std::max(0.f, borderWidth);
    inst.params[1] = static_cast<float>(placement);
    inst.params[2] = std::max(0.f, arrowWidth);
    applyCurrentRoundedClip(inst);

    std::uint32_t const first = static_cast<std::uint32_t>(callouts_.size());
    callouts_.push_back(inst);
    DrawOp op = makeDrawOp(DrawOp::Kind::Callout, nullptr, first, 1);
    std::uint64_t signature = 14695981039346656037ULL;
    hashValue(signature, inst);
    op.geometrySignature = nonZeroSignature(signature);
    appendDrawOp(ops_, op);
    return true;
  }

  void *gpuDevice() const override { return device_; }

  void evictImageTexture(VulkanImage const *image) {
    auto it = imageTextures_.find(image);
    if (it == imageTextures_.end())
      return;
    pendingTextureDestroys_.push_back(PendingTextureDestroy{std::move(it->second), kMaxFramesInFlight + 1u});
    imageTextures_.erase(it);
  }

  bool deferOwnedImageDestroy(PendingOwnedVulkanImageDestroy destroy) {
    if (!destroy.device || destroy.device != device_) {
      return false;
    }
    if (destroy.allocator && destroy.allocator != allocator_) {
      return false;
    }
    destroy.framesRemaining = kMaxFramesInFlight + 1u;
    pendingOwnedImageDestroys_.push_back(destroy);
    return true;
  }

  void updateImageTexture(VulkanImage const* image) {
    if (!image || image->external || image->ownsGpuResource) {
      return;
    }
    auto it = imageTextures_.find(image);
    if (it == imageTextures_.end() || !it->second) {
      return;
    }
    Texture& texture = *it->second;
    if (texture.width != image->width || texture.height != image->height) {
      evictImageTexture(image);
      return;
    }
    renderTargetFrameCacheValid_ = false;
    texture.contentGeneration = image->contentGeneration;
    queueTextureUpload(texture, image->pixels.data());
  }

  void updateImageTextureRegion(VulkanImage const* image,
                                std::uint32_t x,
                                std::uint32_t y,
                                std::uint32_t width,
                                std::uint32_t height,
                                void const* pixels,
                                std::uint32_t sourceBytesPerRow = 0) {
    if (!image || image->external || image->ownsGpuResource || !pixels || width == 0 || height == 0) {
      return;
    }
    auto it = imageTextures_.find(image);
    if (it == imageTextures_.end() || !it->second) {
      return;
    }
    Texture& texture = *it->second;
    if (texture.width != image->width || texture.height != image->height) {
      evictImageTexture(image);
      return;
    }
    if (x > static_cast<std::uint32_t>(texture.width) || y > static_cast<std::uint32_t>(texture.height) ||
        width > static_cast<std::uint32_t>(texture.width) - x ||
        height > static_cast<std::uint32_t>(texture.height) - y) {
      return;
    }
    renderTargetFrameCacheValid_ = false;
    texture.contentGeneration = image->contentGeneration;
    queueTextureRegionUpload(texture, x, y, width, height, pixels, sourceBytesPerRow);
  }

  void markImageTextureContentsChanged(VulkanImage const* image) {
    if (!image) {
      return;
    }
    auto it = imageTextures_.find(image);
    if (it == imageTextures_.end() || !it->second) {
      return;
    }
    it->second->contentGeneration = image->contentGeneration;
    renderTargetFrameCacheValid_ = false;
  }

private:
  struct PendingTextureDestroy {
    std::unique_ptr<Texture> texture;
    std::uint32_t framesRemaining = 0;
    std::uint32_t successfulSubmits = 0;
  };

  struct PendingTextureUpload {
    Texture* texture = nullptr;
    Buffer staging;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
  };

  struct PendingTextureTransition {
    Texture* texture = nullptr;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
  };

  struct PendingBufferDestroy {
    Buffer buffer;
    std::uint32_t framesRemaining = 0;
    std::uint32_t successfulSubmits = 0;
    bool recycleUploadStaging = false;
  };

  struct PendingRecorderResourceDestroy {
    VulkanFrameRecorderResources resources;
    std::uint32_t framesRemaining = 0;
    std::uint32_t successfulSubmits = 0;
  };

  struct FrameGeometryResources {
    VkDescriptorSet rectDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet calloutDescriptorSet = VK_NULL_HANDLE;
    VkDescriptorSet quadDescriptorSet = VK_NULL_HANDLE;
    Buffer rectBuffer;
    Buffer calloutBuffer;
    Buffer quadBuffer;
    Buffer pathBuffer;
  };

  struct RetiredSwapchain {
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    std::vector<VkImageView> views;
    std::vector<VkSemaphore> renderFinished;
    std::vector<VkFence> presentFences;
    std::array<std::uint64_t, kMaxFramesInFlight> frameFenceGenerations{};
  };

  struct CachedBackdropBlur {
    std::uint64_t signature = 0;
    Texture texture;
    bool valid = false;
  };

  struct RoundedClipState {
    Rect rect{};
    CornerRadius radii{};
  };

  struct DrawState {
    Mat3 transform = Mat3::identity();
    Rect clip{};
    std::array<RoundedClipState, kVulkanRoundedClipMaskCapacity> roundedClips{};
    std::uint32_t roundedClipCount = 0;
    float opacity = 1.f;
    BlendMode blendMode = BlendMode::Normal;
  };

  struct RecordingTarget {
    std::vector<DrawOp> &ops;
    std::vector<QuadInstance> &quads;
    std::vector<RectInstance> &rects;
    std::vector<VulkanPathVertex> &pathVerts;
  };

  RecordingTarget recordingTarget() {
    if (captureTarget_) {
      return RecordingTarget{captureTarget_->ops, captureTarget_->quads, captureTarget_->rects,
                             captureTarget_->pathVerts};
    }
    return RecordingTarget{ops_, quads_, rects_, pathVerts_};
  }

  void appendBackdropBlurQuadInstance(RecordingTarget& target,
                                      Rect const& rect,
                                      Color tint,
                                      CornerRadius const& corners) const {
    Point p00 = state_.transform.apply({rect.x, rect.y});
    Point p10 = state_.transform.apply({rect.x + rect.width, rect.y});
    Point p01 = state_.transform.apply({rect.x, rect.y + rect.height});
    QuadInstance q{};
    q.rect[0] = 0.f;
    q.rect[1] = 0.f;
    q.rect[2] = rect.width;
    q.rect[3] = rect.height;
    q.axisX[0] = p00.x;
    q.axisX[1] = p00.y;
    q.axisX[2] = p10.x - p00.x;
    q.axisX[3] = p10.y - p00.y;
    q.axisY[0] = p01.x - p00.x;
    q.axisY[1] = p01.y - p00.y;
    q.uv[0] = p00.x / std::max(1.f, static_cast<float>(width_));
    q.uv[1] = p00.y / std::max(1.f, static_cast<float>(height_));
    q.uv[2] = p10.x / std::max(1.f, static_cast<float>(width_));
    q.uv[3] = p01.y / std::max(1.f, static_cast<float>(height_));
    putColor(q.color, tint, state_.opacity);
    CornerRadius cr = clampRadii(corners, rect.width, rect.height);
    q.radii[0] = cr.topLeft;
    q.radii[1] = cr.topRight;
    q.radii[2] = cr.bottomRight;
    q.radii[3] = cr.bottomLeft;
    applyCurrentRoundedClip(q);
    target.quads.push_back(q);
  }

  void registerCanvas() {
    std::lock_guard lock(gCanvasRegistryMutex);
    gCanvases.push_back(this);
  }

  void unregisterCanvas() {
    std::lock_guard lock(gCanvasRegistryMutex);
    gCanvases.erase(std::remove(gCanvases.begin(), gCanvases.end(), this), gCanvases.end());
  }

  Rect transformedBounds(Rect rect) const {
    return boundsOfTransformedRect(rect, state_.transform);
  }

  DrawOp makeDrawOp(DrawOp::Kind kind, Texture *texture, std::uint32_t first, std::uint32_t count) const {
    DrawOp op{};
    op.kind = kind;
    op.texture = texture;
    op.first = first;
    op.count = count;
    op.clip = state_.clip;
    return op;
  }

  void appendSignedDrawOp(RecordingTarget& target, DrawOp op) const {
    appendDrawOp(target.ops, op);
  }

  static bool sameRect(Rect a, Rect b) {
    constexpr float eps = 1e-4f;
    return std::abs(a.x - b.x) <= eps &&
           std::abs(a.y - b.y) <= eps &&
           std::abs(a.width - b.width) <= eps &&
           std::abs(a.height - b.height) <= eps;
  }

  static bool canMergeDrawOps(DrawOp const &a, DrawOp const &b) {
    return a.kind == b.kind &&
           a.texture == b.texture &&
           a.first + a.count == b.first &&
           sameRect(a.clip, b.clip) &&
           a.hasBlurCacheClip == b.hasBlurCacheClip &&
           (!a.hasBlurCacheClip || sameRect(a.blurCacheClip, b.blurCacheClip)) &&
           a.sourceImage == b.sourceImage &&
           a.externalStorageDescriptor == b.externalStorageDescriptor &&
           a.externalVertexBuffer == b.externalVertexBuffer &&
           a.premultipliedAlpha == b.premultipliedAlpha &&
           std::abs(a.externalTranslationX - b.externalTranslationX) <= 1e-4f &&
           std::abs(a.externalTranslationY - b.externalTranslationY) <= 1e-4f;
  }

  static void appendDrawOp(std::vector<DrawOp> &ops, DrawOp op) {
    if (!ops.empty() && canMergeDrawOps(ops.back(), op)) {
      DrawOp &prev = ops.back();
      std::uint64_t const previousSignature = prev.geometrySignature;
      std::uint64_t const nextSignature = op.geometrySignature;
      prev.count += op.count;
      prev.blurRadius = std::max(prev.blurRadius, op.blurRadius);
      if (previousSignature != 0 && nextSignature != 0) {
        std::uint64_t h = 14695981039346656037ULL;
        hashValue(h, previousSignature);
        hashValue(h, nextSignature);
        hashValue(h, prev.count);
        hashValue(h, prev.blurRadius);
        prev.geometrySignature = nonZeroSignature(h);
      } else {
        prev.geometrySignature = 0;
      }
      return;
    }
    ops.push_back(op);
  }

  static Rect translatedRect(Rect rect, float dx, float dy) {
    rect.x += dx;
    rect.y += dy;
    return rect;
  }

  Rect localReplayClip(VulkanFrameRecorder const& recorded, Rect opClip, float dx, float dy) const {
    if (sameRect(opClip, recorded.rootClip)) {
      return state_.clip;
    }
    return intersectRects(translatedRect(opClip, dx, dy), state_.clip);
  }

  static std::uint32_t roundedClipCount(float const (&header)[4]) {
    return std::min<std::uint32_t>(
        kVulkanRoundedClipMaskCapacity,
        static_cast<std::uint32_t>(std::max(0.f, header[0]) + 0.5f));
  }

  template <typename Instance>
  static void writeRoundedClip(Instance &inst, std::uint32_t index, RoundedClipState const &clip) {
    inst.clipEntries[index * 2u][0] = clip.rect.x;
    inst.clipEntries[index * 2u][1] = clip.rect.y;
    inst.clipEntries[index * 2u][2] = clip.rect.width;
    inst.clipEntries[index * 2u][3] = clip.rect.height;
    inst.clipEntries[index * 2u + 1u][0] = clip.radii.topLeft;
    inst.clipEntries[index * 2u + 1u][1] = clip.radii.topRight;
    inst.clipEntries[index * 2u + 1u][2] = clip.radii.bottomRight;
    inst.clipEntries[index * 2u + 1u][3] = clip.radii.bottomLeft;
  }

  template <typename Instance>
  static void translateRoundedClipStack(Instance &inst, float dx, float dy) {
    std::uint32_t const count = roundedClipCount(inst.clipHeader);
    for (std::uint32_t i = 0; i < count; ++i) {
      inst.clipEntries[i * 2u][0] += dx;
      inst.clipEntries[i * 2u][1] += dy;
    }
  }

  template <typename Instance>
  void applyCurrentRoundedClip(Instance &inst) const {
    if (state_.roundedClipCount == 0) {
      return;
    }
    std::array<float, 4> oldHeader{};
    std::copy(std::begin(inst.clipHeader), std::end(inst.clipHeader), oldHeader.begin());
    std::array<std::array<float, 4>, kVulkanRoundedClipEntryCount> oldEntries{};
    for (std::size_t i = 0; i < kVulkanRoundedClipEntryCount; ++i) {
      std::copy(std::begin(inst.clipEntries[i]), std::end(inst.clipEntries[i]), oldEntries[i].begin());
    }
    std::uint32_t const oldCount = std::min<std::uint32_t>(
        kVulkanRoundedClipMaskCapacity,
        static_cast<std::uint32_t>(std::max(0.f, oldHeader[0]) + 0.5f));

    inst.clipHeader[0] = 0.f;
    for (auto &entry : inst.clipEntries) {
      entry[0] = 0.f;
      entry[1] = 0.f;
      entry[2] = 0.f;
      entry[3] = 0.f;
    }

    std::uint32_t writeCount = 0;
    for (std::uint32_t i = 0; i < state_.roundedClipCount && writeCount < kVulkanRoundedClipMaskCapacity; ++i) {
      writeRoundedClip(inst, writeCount++, state_.roundedClips[i]);
    }
    for (std::uint32_t i = 0; i < oldCount && writeCount < kVulkanRoundedClipMaskCapacity; ++i) {
      inst.clipEntries[writeCount * 2u][0] = oldEntries[i * 2u][0];
      inst.clipEntries[writeCount * 2u][1] = oldEntries[i * 2u][1];
      inst.clipEntries[writeCount * 2u][2] = oldEntries[i * 2u][2];
      inst.clipEntries[writeCount * 2u][3] = oldEntries[i * 2u][3];
      inst.clipEntries[writeCount * 2u + 1u][0] = oldEntries[i * 2u + 1u][0];
      inst.clipEntries[writeCount * 2u + 1u][1] = oldEntries[i * 2u + 1u][1];
      inst.clipEntries[writeCount * 2u + 1u][2] = oldEntries[i * 2u + 1u][2];
      inst.clipEntries[writeCount * 2u + 1u][3] = oldEntries[i * 2u + 1u][3];
      ++writeCount;
    }
    inst.clipHeader[0] = static_cast<float>(writeCount);
  }

  std::uint64_t currentRoundedClipHash() const {
    std::uint64_t h = 14695981039346656037ULL;
    hashValue(h, state_.roundedClipCount);
    for (std::uint32_t i = 0; i < state_.roundedClipCount; ++i) {
      hashValue(h, state_.roundedClips[i].rect);
      hashValue(h, state_.roundedClips[i].radii);
    }
    return h;
  }

  static std::uint64_t nonZeroSignature(std::uint64_t value) {
    return value != 0 ? value : 1;
  }

  static std::uint64_t translatedGeometrySignature(std::uint64_t baseSignature,
                                                   float dx,
                                                   float dy,
                                                   float opacityScale) {
    if (baseSignature == 0) {
      return 0;
    }
    std::uint64_t h = 14695981039346656037ULL;
    hashValue(h, baseSignature);
    hashValue(h, dx);
    hashValue(h, dy);
    hashValue(h, opacityScale);
    return nonZeroSignature(h);
  }

  bool recordedGlyphAtlasCurrent(VulkanFrameRecorder const &recorded) const {
    if (!recorded.replayable) {
      return false;
    }
    return recorded.glyphAtlasGeneration == 0 ||
           recorded.glyphAtlasGeneration == resources().atlasGeneration;
  }

  void uploadRecorderBuffer(VmaAllocation allocation, void const *data, VkDeviceSize size) {
    if (!data || size == 0) {
      return;
    }
    void *mapped = nullptr;
    vkCheck(vmaMapMemory(allocator_, allocation, &mapped), "vmaMapMemory");
    std::memcpy(mapped, data, static_cast<std::size_t>(size));
    vkCheck(vmaFlushAllocation(allocator_, allocation, 0, size), "vmaFlushAllocation");
    vmaUnmapMemory(allocator_, allocation);
  }

  static std::uint64_t recorderBufferUploadSignature(void const *data,
                                                     VkDeviceSize size,
                                                     VkBufferUsageFlags usage) {
    if (!data || size == 0) {
      return 0;
    }
    std::uint64_t h = 14695981039346656037ULL;
    hashValue(h, size);
    hashValue(h, usage);
    hashBytes(h, data, static_cast<std::size_t>(size));
    return nonZeroSignature(h);
  }

  bool prepareRecorderOwnership(VulkanFrameRecorder const &recorded) {
    if ((recorded.allocator && recorded.allocator != allocator_) ||
        (recorded.device && recorded.device != device_) ||
        (recorded.descriptorPool && recorded.descriptorPool != resources().descriptorPool)) {
      return false;
    }
    if (!recorded.allocator) {
      recorded.allocator = allocator_;
    }
    if (!recorded.device) {
      recorded.device = device_;
    }
    if (!recorded.descriptorPool) {
      recorded.descriptorPool = resources().descriptorPool;
    }
    return true;
  }

  bool deferFrameRecorderResourcesDestroy(VulkanFrameRecorderResources retired) {
    if (!retired.device || retired.device != device_) {
      return false;
    }
    if (retired.allocator && retired.allocator != allocator_) {
      return false;
    }
    if (retired.descriptorPool && retired.descriptorPool != resources().descriptorPool) {
      return false;
    }
    pendingRecorderResourceDestroys_.push_back(PendingRecorderResourceDestroy{
        .resources = retired,
        .framesRemaining = kMaxFramesInFlight + 1u,
        .successfulSubmits = 0u,
    });
    return true;
  }

  bool ensureRecorderBuffer(VulkanFrameRecorder const &recorded, VkBuffer &buffer,
                            VmaAllocation &allocation, VkDeviceSize &capacity,
                            std::uint64_t &uploadSignature, VkDescriptorSet* descriptor,
                            void const *data,
                            VkDeviceSize size, VkBufferUsageFlags usage) {
    if (!data || size == 0) {
      return true;
    }
    if (!prepareRecorderOwnership(recorded))
      return false;
    std::uint64_t const nextSignature = recorderBufferUploadSignature(data, size, usage);
    if (buffer && capacity >= size) {
      if (uploadSignature == nextSignature) {
        return true;
      }
      uploadRecorderBuffer(allocation, data, size);
      uploadSignature = nextSignature;
      return true;
    }
    if (buffer) {
      VkDescriptorSet retiredDescriptor = VK_NULL_HANDLE;
      if (descriptor) {
        retiredDescriptor = *descriptor;
        *descriptor = VK_NULL_HANDLE;
      }
      retireVulkanFrameRecorderResources(VulkanFrameRecorderResources{
          .allocator = recorded.allocator,
          .device = recorded.device,
          .descriptorPool = recorded.descriptorPool,
          .preparedQuadBuffer = buffer,
          .preparedQuadAllocation = allocation,
          .preparedQuadDescriptor = retiredDescriptor,
      });
      buffer = VK_NULL_HANDLE;
      allocation = VK_NULL_HANDLE;
      capacity = 0;
      uploadSignature = 0;
    }
    capacity = std::max<VkDeviceSize>(size, 1024);
    auto info = vkStructure<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    info.size = capacity;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
    vkCheck(vmaCreateBuffer(allocator_, &info, &allocationInfo, &buffer, &allocation, nullptr),
            "vmaCreateBuffer");
    uploadRecorderBuffer(allocation, data, size);
    uploadSignature = nextSignature;
    return true;
  }

  bool recorderPreparedGeometryFastPathEnabled() const {
    int const overrideValue = vulkanPreparedGeometryOverride();
    if (overrideValue < 0) {
      return false;
    }
    if (overrideValue > 0) {
      return true;
    }
    return shared_ && shared_->driverId != VK_DRIVER_ID_MESA_RADV;
  }

  bool ensureRecorderStorageDescriptor(VulkanFrameRecorder const &recorded, VkDescriptorSet &set,
                                       VkDescriptorSetLayout layout, VkBuffer buffer,
                                       VkDeviceSize capacity) {
    if (!buffer || capacity == 0) {
      return true;
    }
    if (!prepareRecorderOwnership(recorded))
      return false;
    if (!set) {
      auto alloc = vkStructure<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
      alloc.descriptorPool = recorded.descriptorPool;
      alloc.descriptorSetCount = 1;
      alloc.pSetLayouts = &layout;
      vkCheck(vkAllocateDescriptorSets(device_, &alloc, &set), "vkAllocateDescriptorSets");
    }
    VkDescriptorBufferInfo bi{buffer, 0, capacity};
    auto write = vkStructure<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &bi;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    return true;
  }

  bool prepareRecorderBuffers(VulkanFrameRecorder const &recorded) {
    if (!recorderPreparedGeometryFastPathEnabled()) {
      return true;
    }
    if (!prepareRecorderOwnership(recorded))
      return false;
    if (!ensureRecorderBuffer(recorded, recorded.preparedRectBuffer, recorded.preparedRectAllocation,
                              recorded.preparedRectCapacity,
                              recorded.preparedRectUploadSignature,
                              &recorded.preparedRectDescriptor, recorded.rects.data(),
                              static_cast<VkDeviceSize>(recorded.rects.size() * sizeof(RectInstance)),
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
      return false;
    }
    if (!ensureRecorderBuffer(recorded, recorded.preparedQuadBuffer, recorded.preparedQuadAllocation,
                              recorded.preparedQuadCapacity,
                              recorded.preparedQuadUploadSignature,
                              &recorded.preparedQuadDescriptor, recorded.quads.data(),
                              static_cast<VkDeviceSize>(recorded.quads.size() * sizeof(QuadInstance)),
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
      return false;
    }
    if (!ensureRecorderBuffer(recorded, recorded.preparedPathVertexBuffer,
                              recorded.preparedPathVertexAllocation,
                              recorded.preparedPathVertexCapacity,
                              recorded.preparedPathVertexUploadSignature, nullptr, recorded.pathVerts.data(),
                              static_cast<VkDeviceSize>(recorded.pathVerts.size() * sizeof(VulkanPathVertex)),
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) {
      return false;
    }
    if (!ensureRecorderStorageDescriptor(recorded, recorded.preparedRectDescriptor,
                                         resources().rectDescriptorLayout,
                                         recorded.preparedRectBuffer,
                                         recorded.preparedRectCapacity)) {
      return false;
    }
    if (!ensureRecorderStorageDescriptor(recorded, recorded.preparedQuadDescriptor,
                                         resources().quadDescriptorLayout,
                                         recorded.preparedQuadBuffer,
                                         recorded.preparedQuadCapacity)) {
      return false;
    }
    return true;
  }

  static void translateRectInstance(RectInstance &inst, float dx, float dy, float opacityScale) {
    inst.axisX[0] += dx;
    inst.axisX[1] += dy;
    inst.stroke[3] *= opacityScale;
    inst.params[3] *= opacityScale;
    translateRoundedClipStack(inst, dx, dy);
  }

  bool resolveRecordedImageTexture(DrawOp &op) {
    if (op.kind != DrawOp::Kind::Image || !op.sourceImage) {
      return true;
    }
    try {
      op.texture = ensureImageTexture(*static_cast<VulkanImage const *>(op.sourceImage));
    } catch (std::exception const &e) {
      std::fprintf(stderr, "Lambda Vulkan: recorded image texture upload failed: %s\n", e.what());
      return false;
    }
    return op.texture != nullptr;
  }

  static void translateQuadInstance(QuadInstance &inst, float dx, float dy, float opacityScale) {
    inst.axisX[0] += dx;
    inst.axisX[1] += dy;
    inst.color[3] *= opacityScale;
    translateRoundedClipStack(inst, dx, dy);
  }

  static void translatePathVertex(VulkanPathVertex &vertex, float dx, float dy, float opacityScale) {
    vertex.x += dx;
    vertex.y += dy;
    vertex.color[3] *= opacityScale;
    vertex.params[3] *= opacityScale;
    translateRoundedClipStack(vertex, dx, dy);
  }

  bool appendRecordedOps(VulkanFrameRecorder const &recorded, bool localReplay) {
    float const dx = localReplay ? state_.transform.m[6] : 0.f;
    float const dy = localReplay ? state_.transform.m[7] : 0.f;
    float const opacityScale = localReplay ? state_.opacity : 1.f;
    constexpr float eps = 1e-4f;
    bool const hasTranslatedBackdropBlur =
        localReplay && (std::abs(dx) > eps || std::abs(dy) > eps) &&
        std::any_of(recorded.ops.begin(), recorded.ops.end(), [](DrawOp const &op) {
          return op.kind == DrawOp::Kind::BackdropBlur;
        });
    bool const canUsePreparedGeometry =
        // RADV can crash while recording draws that bind prepared replay descriptors for cached composite
        // content. Keep that driver on the CPU-copy path while allowing unaffected drivers to bind replay buffers.
        recorderPreparedGeometryFastPathEnabled() &&
        (!localReplay || std::abs(opacityScale - 1.f) <= eps) &&
        (!localReplay || state_.roundedClipCount == 0) &&
        !hasTranslatedBackdropBlur &&
        (recorded.rects.empty() ||
         (recorded.preparedRectBuffer && recorded.preparedRectDescriptor)) &&
        (recorded.quads.empty() ||
         (recorded.preparedQuadBuffer && recorded.preparedQuadDescriptor)) &&
        (recorded.pathVerts.empty() || recorded.preparedPathVertexBuffer);

    std::vector<DrawOp> resolvedOps = recorded.ops;
    for (DrawOp &op : resolvedOps) {
      if (!resolveRecordedImageTexture(op)) {
        return false;
      }
    }

    if (canUsePreparedGeometry) {
      ops_.reserve(ops_.size() + resolvedOps.size());
      for (DrawOp op : resolvedOps) {
        switch (op.kind) {
        case DrawOp::Kind::Rect:
          op.externalStorageDescriptor = recorded.preparedRectDescriptor;
          break;
        case DrawOp::Kind::Callout:
          break;
        case DrawOp::Kind::Path:
          op.externalVertexBuffer = recorded.preparedPathVertexBuffer;
          break;
        case DrawOp::Kind::Image:
        case DrawOp::Kind::BackdropBlur:
          op.externalStorageDescriptor = recorded.preparedQuadDescriptor;
          break;
        }
        if (localReplay) {
          op.geometrySignature =
              translatedGeometrySignature(op.geometrySignature, dx, dy, opacityScale);
          op.clip = localReplayClip(recorded, op.clip, dx, dy);
          if (op.hasBlurCacheClip) {
            op.blurCacheClip = translatedRect(op.blurCacheClip, dx, dy);
          }
          op.externalTranslationX = dx;
          op.externalTranslationY = dy;
        }
        ops_.push_back(op);
      }
      return true;
    }

    std::uint32_t const rectBase = static_cast<std::uint32_t>(rects_.size());
    std::uint32_t const quadBase = static_cast<std::uint32_t>(quads_.size());
    std::uint32_t const pathBase = static_cast<std::uint32_t>(pathVerts_.size());

    rects_.reserve(rects_.size() + recorded.rects.size());
    for (RectInstance inst : recorded.rects) {
      if (localReplay) {
        translateRectInstance(inst, dx, dy, opacityScale);
        applyCurrentRoundedClip(inst);
      }
      rects_.push_back(inst);
    }

    quads_.reserve(quads_.size() + recorded.quads.size());
    for (QuadInstance inst : recorded.quads) {
      if (localReplay) {
        translateQuadInstance(inst, dx, dy, opacityScale);
        applyCurrentRoundedClip(inst);
      }
      quads_.push_back(inst);
    }

    pathVerts_.reserve(pathVerts_.size() + recorded.pathVerts.size());
    for (VulkanPathVertex vertex : recorded.pathVerts) {
      if (localReplay) {
        translatePathVertex(vertex, dx, dy, opacityScale);
        applyCurrentRoundedClip(vertex);
      }
      pathVerts_.push_back(vertex);
    }

    ops_.reserve(ops_.size() + resolvedOps.size());
    float const uvDx = localReplay ? dx / std::max(1.f, static_cast<float>(width_)) : 0.f;
    float const uvDy = localReplay ? dy / std::max(1.f, static_cast<float>(height_)) : 0.f;
    for (DrawOp op : resolvedOps) {
      std::uint32_t const originalFirst = op.first;
      switch (op.kind) {
      case DrawOp::Kind::Rect:
        op.first += rectBase;
        break;
      case DrawOp::Kind::Callout:
        break;
      case DrawOp::Kind::Path:
        op.first += pathBase;
        break;
      case DrawOp::Kind::Image:
        op.first += quadBase;
        break;
      case DrawOp::Kind::BackdropBlur:
        op.first += quadBase;
        if (localReplay) {
          for (std::uint32_t i = 0; i < op.count; ++i) {
            std::size_t const index = static_cast<std::size_t>(quadBase + originalFirst + i);
            if (index >= quads_.size()) {
              break;
            }
            QuadInstance &quad = quads_[index];
            quad.uv[0] += uvDx;
            quad.uv[1] += uvDy;
            quad.uv[2] += uvDx;
            quad.uv[3] += uvDy;
          }
        }
        break;
      }
      if (localReplay) {
        op.geometrySignature =
            translatedGeometrySignature(op.geometrySignature, dx, dy, opacityScale);
        op.clip = localReplayClip(recorded, op.clip, dx, dy);
        if (op.hasBlurCacheClip) {
          op.blurCacheClip = translatedRect(op.blurCacheClip, dx, dy);
        }
      }
      ops_.push_back(op);
    }
    return true;
  }

  bool clipLineToCurrentClip(Point &a, Point &b) const {
    float t0 = 0.f;
    float t1 = 1.f;
    float const dx = b.x - a.x;
    float const dy = b.y - a.y;
    float const xMin = state_.clip.x;
    float const yMin = state_.clip.y;
    float const xMax = state_.clip.x + state_.clip.width;
    float const yMax = state_.clip.y + state_.clip.height;
    auto edge = [&](float p, float q) {
      if (std::abs(p) < 1e-6f)
        return q >= 0.f;
      float const r = q / p;
      if (p < 0.f) {
        if (r > t1)
          return false;
        if (r > t0)
          t0 = r;
      } else {
        if (r < t0)
          return false;
        if (r < t1)
          t1 = r;
      }
      return true;
    };
    if (!edge(-dx, a.x - xMin) || !edge(dx, xMax - a.x) ||
        !edge(-dy, a.y - yMin) || !edge(dy, yMax - a.y)) {
      return false;
    }
    Point const original = a;
    a = {original.x + dx * t0, original.y + dy * t0};
    b = {original.x + dx * t1, original.y + dy * t1};
    return state_.clip.width > 0.f && state_.clip.height > 0.f;
  }

  void encodeFill(FillStyle const &fill, RectInstance &inst) {
    Color c{};
    if (fill.solidColor(&c)) {
      putColor(inst.fill0, c, 1.f);
      inst.stops[0] = 0.f;
      inst.params[1] = 1.f;
      return;
    }
    auto writeStops = [&](auto const &g) {
      inst.params[1] = static_cast<float>(g.stopCount);
      for (std::uint8_t i = 0; i < g.stopCount && i < 4; ++i) {
        float *colors[] = {inst.fill0, inst.fill1, inst.fill2, inst.fill3};
        putColor(colors[i], g.stops[i].color, 1.f);
        inst.stops[i] = g.stops[i].position;
      }
    };
    LinearGradient lg{};
    if (fill.linearGradient(&lg) && lg.stopCount > 0) {
      inst.params[0] = 1.f;
      inst.gradient[0] = lg.start.x;
      inst.gradient[1] = lg.start.y;
      inst.gradient[2] = lg.end.x;
      inst.gradient[3] = lg.end.y;
      writeStops(lg);
      return;
    }
    RadialGradient rg{};
    if (fill.radialGradient(&rg) && rg.stopCount > 0) {
      inst.params[0] = 2.f;
      inst.gradient[0] = rg.center.x;
      inst.gradient[1] = rg.center.y;
      inst.gradient[2] = rg.radius;
      writeStops(rg);
      return;
    }
    ConicalGradient cg{};
    if (fill.conicalGradient(&cg) && cg.stopCount > 0) {
      inst.params[0] = 3.f;
      inst.gradient[0] = cg.center.x;
      inst.gradient[1] = cg.center.y;
      inst.gradient[2] = cg.startAngleRadians;
      writeStops(cg);
      return;
    }
    putColor(inst.fill0, Colors::transparent);
    inst.params[1] = 1.f;
  }
