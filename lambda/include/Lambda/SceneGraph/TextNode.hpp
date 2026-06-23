#pragma once

/// \file Lambda/SceneGraph/TextNode.hpp
///
/// Scene-graph text node.

#include <Lambda/Graphics/TextLayout.hpp>
#include <Lambda/SceneGraph/SceneNode.hpp>

#include <memory>

namespace lambdaui::scenegraph {

class TextNode final : public SceneNode {
  public:
    explicit TextNode(Rect bounds = {}, std::shared_ptr<TextLayout const> layout = {});
    ~TextNode() override;

    std::shared_ptr<TextLayout const> const &layout() const noexcept;

    void setLayout(std::shared_ptr<TextLayout const> layout);

    void render(Renderer &renderer) const override;
    bool canPrepareRenderOps() const noexcept override;
    std::uint64_t preparedRenderOpsKey(float dpiScale) const noexcept override;

  private:
    std::shared_ptr<TextLayout const> layout_{};
};

} // namespace lambdaui::scenegraph
