#pragma once

/// \file Lambda/SceneGraph/PathNode.hpp
///
/// Scene-graph path node.

#include <Lambda/Graphics/Path.hpp>
#include <Lambda/Graphics/Styles.hpp>
#include <Lambda/SceneGraph/SceneNode.hpp>

namespace lambda::scenegraph {

class PathNode final : public SceneNode {
  public:
    explicit PathNode(Rect bounds = {}, Path path = {}, FillStyle fill = FillStyle::none(),
                      StrokeStyle stroke = StrokeStyle::none(),
                      ShadowStyle shadow = ShadowStyle::none());
    ~PathNode() override;

    Path const& path() const noexcept;
    FillStyle const& fill() const noexcept;
    StrokeStyle const& stroke() const noexcept;
    ShadowStyle const& shadow() const noexcept;

    void setPath(Path path);
    void setFill(FillStyle fill);
    void setStroke(StrokeStyle stroke);
    void setShadow(ShadowStyle shadow);

    Rect localBounds() const noexcept override;
    void render(Renderer& renderer) const override;

  private:
    Path path_{};
    FillStyle fill_ = FillStyle::none();
    StrokeStyle stroke_ = StrokeStyle::none();
    ShadowStyle shadow_ = ShadowStyle::none();
};

} // namespace lambda::scenegraph
