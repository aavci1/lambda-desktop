#pragma once

/// \file Lambda/SceneGraph/RectNode.hpp
///
/// Scene-graph rectangle node.

#include <Lambda/Graphics/Styles.hpp>
#include <Lambda/SceneGraph/SceneNode.hpp>

namespace lambda::scenegraph {

class RectNode final : public SceneNode {
  public:
    explicit RectNode(Rect bounds = {}, FillStyle fill = FillStyle::none(), StrokeStyle stroke = StrokeStyle::none(), CornerRadius cornerRadius = {}, ShadowStyle shadow = ShadowStyle::none());
    ~RectNode() override;

    FillStyle const &fill() const noexcept;
    StrokeStyle const &stroke() const noexcept;
    CornerRadius cornerRadius() const noexcept;
    ShadowStyle const &shadow() const noexcept;
    bool clipsContents() const noexcept;
    float opacity() const noexcept;

    void setFill(FillStyle fill);
    void setStroke(StrokeStyle stroke);
    void setCornerRadius(CornerRadius cornerRadius);
    void setShadow(ShadowStyle shadow);
    void setClipsContents(bool clipsContents) noexcept;
    void setOpacity(float opacity) noexcept;

    Rect localBounds() const noexcept override;
    void render(Renderer &renderer) const override;

  private:
    FillStyle fill_ = FillStyle::none();
    StrokeStyle stroke_ = StrokeStyle::none();
    CornerRadius cornerRadius_{};
    ShadowStyle shadow_ = ShadowStyle::none();
    bool clipsContents_ = false;
    float opacity_ = 1.f;
};

} // namespace lambda::scenegraph
