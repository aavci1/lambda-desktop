#include <Lambda/SceneGraph/RasterCacheNode.hpp>

#include <Lambda/SceneGraph/Renderer.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cmath>
#include <memory>

namespace lambdaui::scenegraph {

RasterCacheNode::RasterCacheNode(Rect bounds)
    : SceneNode(SceneNodeKind::RasterCache, bounds) {}

RasterCacheNode::~RasterCacheNode() = default;

void RasterCacheNode::setSubtree(std::unique_ptr<SceneNode> subtreeNode) {
  if (!subtreeNode) {
    return;
  }
  subtreeNode->setPosition(Point{});
  replaceChildren({});
  appendChild(std::move(subtreeNode));
  setRelayout([this](LayoutConstraints const& constraints) {
    SceneNode* child = subtree();
    if (!child) {
      setSize(Size{});
      return;
    }
    child->relayout(constraints);
    child->setPosition(Point{});
    setSize(child->size());
  });
}

SceneNode* RasterCacheNode::subtree() noexcept {
  auto children = this->children();
  return children.empty() ? nullptr : children.front().get();
}

SceneNode const* RasterCacheNode::subtree() const noexcept {
  auto children = this->children();
  return children.empty() ? nullptr : children.front().get();
}

void RasterCacheNode::invalidateCache() {
  cachedImage_.reset();
  cachedLogicalSize_ = {};
  cachedDpiScale_ = 0.f;
  markDirty();
}

bool RasterCacheNode::hasValidCache(Size logicalSize, float dpiScale) const noexcept {
  constexpr float eps = 1e-3f;
  return static_cast<bool>(cachedImage_) &&
         std::abs(cachedLogicalSize_.width - logicalSize.width) <= eps &&
         std::abs(cachedLogicalSize_.height - logicalSize.height) <= eps &&
         std::abs(cachedDpiScale_ - dpiScale) <= eps;
}

std::shared_ptr<Image> RasterCacheNode::cachedImage() const noexcept {
  return cachedImage_;
}

void RasterCacheNode::setCachedImage(std::shared_ptr<Image> image, Size logicalSize, float dpiScale) const {
  cachedImage_ = std::move(image);
  cachedLogicalSize_ = logicalSize;
  cachedDpiScale_ = dpiScale;
}

void RasterCacheNode::noteRasterized() const {
#ifndef NDEBUG
  using Clock = std::chrono::steady_clock;
  double const now = std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
  constexpr double kWindowSeconds = 0.5;
  constexpr std::uint32_t kWarningThreshold = 8;
  if (rasterizeBurstStartSeconds_ <= 0.0 || now - rasterizeBurstStartSeconds_ > kWindowSeconds) {
    rasterizeBurstStartSeconds_ = now;
    rasterizeBurstCount_ = 1;
    return;
  }
  ++rasterizeBurstCount_;
  if (!rasterizeWarningLogged_ && rasterizeBurstCount_ > kWarningThreshold) {
    rasterizeWarningLogged_ = true;
    std::fprintf(stderr,
                 "[lambda:render] RasterCacheNode re-rasterized %u times in %.0fms; "
                 "remove .rasterize() from animated content\n",
                 rasterizeBurstCount_, kWindowSeconds * 1000.0);
  }
#endif
}

void RasterCacheNode::render(Renderer&) const {
  // The renderer owns raster-cache traversal. The node itself has no direct paint op.
}

bool RasterCacheNode::canPrepareRenderOps() const noexcept {
  return false;
}

} // namespace lambdaui::scenegraph
