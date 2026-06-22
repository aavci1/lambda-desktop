  bool atlasHasSpace(SharedVulkanCore::Resources const &res, std::uint32_t width, std::uint32_t height) const {
    int const pad = 1;
    int x = res.atlasX;
    int y = res.atlasY;
    int rowH = res.atlasRowH;
    if (x + static_cast<int>(width) + pad >= res.atlas.width) {
      x = pad;
      y += rowH + pad;
    }
    return y + static_cast<int>(height) + pad < res.atlas.height;
  }

  void placeGlyphInAtlas(SharedVulkanCore::Resources &res, GlyphSlot &slot) {
    int const pad = 1;
    if (res.atlasX + static_cast<int>(slot.w) + pad >= res.atlas.width) {
      res.atlasX = pad;
      res.atlasY += res.atlasRowH + pad;
      res.atlasRowH = 0;
    }
    slot.x = res.atlasX;
    slot.y = res.atlasY;
    res.atlasX += static_cast<int>(slot.w) + pad;
    res.atlasRowH = std::max(res.atlasRowH, static_cast<int>(slot.h));
    for (std::uint32_t row = 0; row < slot.h; ++row) {
      for (std::uint32_t col = 0; col < slot.w; ++col) {
        Rgba &px = res.atlasPixels[static_cast<std::size_t>(slot.y + row) * res.atlas.width + slot.x + col];
        px = {255, 255, 255, slot.alpha[static_cast<std::size_t>(row) * slot.w + col]};
      }
    }
    slot.u0 = static_cast<float>(slot.x) / res.atlas.width;
    slot.v0 = static_cast<float>(slot.y) / res.atlas.height;
    slot.u1 = static_cast<float>(slot.x + static_cast<int>(slot.w)) / res.atlas.width;
    slot.v1 = static_cast<float>(slot.y + static_cast<int>(slot.h)) / res.atlas.height;
  }

  void rebuildAtlas(SharedVulkanCore::Resources &res) {
    std::vector<std::pair<GlyphKey, GlyphSlot>> slots;
    slots.reserve(res.glyphs.size());
    for (auto const &[key, slot] : res.glyphs) {
      if (slot.w > 0 && slot.h > 0 && !slot.alpha.empty()) {
        slots.push_back({key, slot});
      }
    }
    std::sort(slots.begin(), slots.end(), [](auto const &a, auto const &b) {
      return a.second.lastUsed > b.second.lastUsed;
    });
    res.atlasPixels.assign(static_cast<std::size_t>(res.atlas.width) * res.atlas.height, Rgba{255, 255, 255, 0});
    res.atlasX = 1;
    res.atlasY = 1;
    res.atlasRowH = 0;
    res.glyphs.clear();
    ++res.atlasGeneration;
    for (auto &[key, slot] : slots) {
      if (!atlasHasSpace(res, slot.w, slot.h)) {
        continue;
      }
      placeGlyphInAtlas(res, slot);
      res.glyphs.emplace(key, std::move(slot));
    }
    res.atlasDirty = true;
  }

  bool evictGlyphsForAtlasSpace(SharedVulkanCore::Resources &res, std::uint32_t width, std::uint32_t height) {
    if (width + 2u >= static_cast<std::uint32_t>(res.atlas.width) ||
        height + 2u >= static_cast<std::uint32_t>(res.atlas.height)) {
      return false;
    }
    while (!atlasHasSpace(res, width, height) && !res.glyphs.empty()) {
      std::vector<std::pair<GlyphKey, std::uint64_t>> byAge;
      byAge.reserve(res.glyphs.size());
      for (auto const &[key, slot] : res.glyphs) {
        if (slot.w > 0 && slot.h > 0) {
          byAge.push_back({key, slot.lastUsed});
        }
      }
      if (byAge.empty())
        return false;
      std::sort(byAge.begin(), byAge.end(), [](auto const &a, auto const &b) {
        return a.second < b.second;
      });
      std::size_t const eraseCount = std::max<std::size_t>(1, byAge.size() / 4);
      for (std::size_t i = 0; i < eraseCount && i < byAge.size(); ++i) {
        res.glyphs.erase(byAge[i].first);
      }
      rebuildAtlas(res);
    }
    return atlasHasSpace(res, width, height);
  }

  GlyphSlot const *glyphSlot(std::uint32_t fontId, std::uint32_t glyphId, float fontSize) {
    auto &res = resources();
    std::uint16_t size = static_cast<std::uint16_t>(std::clamp(std::round(fontSize * dpiScaleY_), 1.f, 512.f));
    GlyphKey key{fontId, glyphId, size};
    auto it = res.glyphs.find(key);
    if (it != res.glyphs.end()) {
      it->second.lastUsed = ++res.atlasUseCounter;
      return &it->second;
    }
    std::uint32_t gw = 0, gh = 0;
    Point bearing{};
    std::vector<std::uint8_t> alpha = textSystem_.rasterizeGlyph(fontId, glyphId, static_cast<float>(size), gw, gh,
                                                                 bearing);
    if (gw == 0 || gh == 0 || alpha.empty()) {
      GlyphSlot empty{};
      empty.lastUsed = ++res.atlasUseCounter;
      auto [inserted, ok] = res.glyphs.emplace(key, std::move(empty));
      (void)ok;
      return &inserted->second;
    }
    if (!atlasHasSpace(res, gw, gh) && !evictGlyphsForAtlasSpace(res, gw, gh))
      return nullptr;
    GlyphSlot slot{};
    slot.w = gw;
    slot.h = gh;
    slot.bearing = bearing;
    slot.lastUsed = ++res.atlasUseCounter;
    slot.alpha = std::move(alpha);
    placeGlyphInAtlas(res, slot);
    res.atlasDirty = true;
    auto [inserted, ok] = res.glyphs.emplace(key, slot);
    (void)ok;
    return &inserted->second;
  }

  void ensureAtlasDescriptor() { ensureTextureDescriptor(resources().atlas); }

  void queueAtlasUploadIfNeeded() {
    auto &res = resources();
    if (!res.atlasDirty)
      return;
    queueTextureUpload(res.atlas, res.atlasPixels.data());
    res.atlasDirty = false;
  }
