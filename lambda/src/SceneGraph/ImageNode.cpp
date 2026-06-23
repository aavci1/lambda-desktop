#include <Lambda/SceneGraph/ImageNode.hpp>
#include <Lambda/SceneGraph/Renderer.hpp>

#include <utility>

namespace lambdaui::scenegraph {

ImageNode::ImageNode(Rect bounds, std::shared_ptr<Image const> image, ImageFillMode fillMode)
    : SceneNode(SceneNodeKind::Image, bounds), image_(std::move(image)), fillMode_(fillMode) {}

ImageNode::~ImageNode() = default;

std::shared_ptr<Image const> const &ImageNode::image() const noexcept {
    return image_;
}

ImageFillMode ImageNode::fillMode() const noexcept {
    return fillMode_;
}

void ImageNode::setImage(std::shared_ptr<Image const> image) {
    if (image_ == image) {
        return;
    }
    image_ = std::move(image);
    markDirty();
}

void ImageNode::setFillMode(ImageFillMode fillMode) {
    if (fillMode_ == fillMode) {
        return;
    }
    fillMode_ = fillMode;
    markDirty();
}

void ImageNode::render(Renderer &renderer) const {
    if (!image_) {
        return;
    }
    renderer.drawImage(*image_, localBounds(), fillMode_);
}

} // namespace lambdaui::scenegraph
