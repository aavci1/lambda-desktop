#pragma once

/// \file Lambda/SceneGraph/ImageNode.hpp
///
/// Scene-graph image node.

#include <Lambda/SceneGraph/SceneNode.hpp>
#include <Lambda/Graphics/ImageFillMode.hpp>

#include <memory>

namespace lambda {
class Image;
}

namespace lambda::scenegraph {

class ImageNode final : public SceneNode {
  public:
    explicit ImageNode(Rect bounds = {}, std::shared_ptr<Image const> image = {},
                       ImageFillMode fillMode = ImageFillMode::Cover);
    ~ImageNode() override;

    std::shared_ptr<Image const> const &image() const noexcept;
    ImageFillMode fillMode() const noexcept;

    void setImage(std::shared_ptr<Image const> image);
    void setFillMode(ImageFillMode fillMode);

    void render(Renderer &renderer) const override;

  private:
    std::shared_ptr<Image const> image_{};
    ImageFillMode fillMode_ = ImageFillMode::Cover;
};

} // namespace lambda::scenegraph
