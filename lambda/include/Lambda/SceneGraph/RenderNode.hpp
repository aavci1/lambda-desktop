#pragma once

/// \file Lambda/SceneGraph/RenderNode.hpp
///
/// Scene-graph custom draw node backed by a Canvas callback.

#include <Lambda/Graphics/Canvas.hpp>
#include <Lambda/SceneGraph/SceneNode.hpp>

#include <cstdint>
#include <functional>
#include <memory>

namespace lambdaui::scenegraph {

class RenderNode final : public SceneNode {
  public:
    using DrawFunction = std::function<void(Canvas&, Rect)>;
    enum class Purity : std::uint8_t {
        Unknown,
        Pure,
        Live,
    };

    explicit RenderNode(Rect bounds = {}, DrawFunction draw = {});
    ~RenderNode() override;

    DrawFunction const& draw() const noexcept;
    Purity purity() const noexcept;

    void setDraw(DrawFunction draw);
    void setPurity(Purity purity);
    void invalidate();

    void render(Renderer& renderer) const override;
    bool canPrepareRenderOps() const noexcept override;

  private:
    DrawFunction draw_{};
    Purity purity_ = Purity::Unknown;
};

} // namespace lambdaui::scenegraph
