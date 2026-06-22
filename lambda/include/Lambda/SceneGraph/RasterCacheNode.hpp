#pragma once

/// \file Lambda/SceneGraph/RasterCacheNode.hpp
///
/// Scene-graph boundary for subtrees opted into texture rasterization.

#include <Lambda/Graphics/Image.hpp>
#include <Lambda/SceneGraph/SceneNode.hpp>

#include <cstdint>
#include <memory>

namespace lambda::scenegraph {

class RasterCacheNode final : public SceneNode {
public:
  explicit RasterCacheNode(Rect bounds = {});
  ~RasterCacheNode() override;

  void setSubtree(std::unique_ptr<SceneNode> subtree);
  SceneNode* subtree() noexcept;
  SceneNode const* subtree() const noexcept;

  void invalidateCache();
  bool hasValidCache(Size logicalSize, float dpiScale) const noexcept;
  std::shared_ptr<Image> cachedImage() const noexcept;
  void setCachedImage(std::shared_ptr<Image> image, Size logicalSize, float dpiScale) const;
  void noteRasterized() const;

  void render(Renderer& renderer) const override;
  bool canPrepareRenderOps() const noexcept override;

private:
  mutable std::shared_ptr<Image> cachedImage_{};
  mutable Size cachedLogicalSize_{};
  mutable float cachedDpiScale_ = 0.f;
#ifndef NDEBUG
  mutable std::uint32_t rasterizeBurstCount_ = 0;
  mutable bool rasterizeWarningLogged_ = false;
  mutable double rasterizeBurstStartSeconds_ = 0.0;
#endif
};

} // namespace lambda::scenegraph
