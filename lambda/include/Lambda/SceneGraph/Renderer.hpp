#pragma once

/// \file Lambda/SceneGraph/Renderer.hpp
///
/// Pure scene-graph rendering interface used by `SceneNode` and `SceneRenderer`.

#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Graphics/ImageFillMode.hpp>
#include <Lambda/Graphics/Path.hpp>
#include <Lambda/Graphics/Styles.hpp>
#include <Lambda/Graphics/TextLayout.hpp>

#include <memory>

namespace lambda {

class Canvas;
class Image;

namespace scenegraph {

class SceneNode;
class Renderer;

class PreparedRenderOps {
  public:
    virtual ~PreparedRenderOps() = default;
    virtual bool replay(Renderer &renderer) const = 0;
    virtual bool reusableAfterReplayFailure() const { return true; }
};

class Renderer {
  public:
    virtual ~Renderer() = default;

    virtual void save() = 0;
    virtual void restore() = 0;

    virtual void translate(Point offset) = 0;
    virtual void transform(Mat3 const &matrix) = 0;

    virtual void clipRect(Rect rect, CornerRadius const &cornerRadius = CornerRadius {}, bool antiAlias = false) = 0;
    virtual bool quickReject(Rect rect) const = 0;

    virtual void setOpacity(float opacity) = 0;
    virtual void setBlendMode(BlendMode mode) = 0;

    virtual void drawRect(Rect const &rect, CornerRadius const &cornerRadius, FillStyle const &fill, StrokeStyle const &stroke, ShadowStyle const &shadow) = 0;
    virtual void drawPath(Path const &path, FillStyle const &fill, StrokeStyle const &stroke, ShadowStyle const &shadow) = 0;
    virtual void drawTextLayout(TextLayout const &layout) = 0;
    virtual void drawImage(Image const &image, Rect const &bounds,
                           ImageFillMode fillMode = ImageFillMode::Cover) = 0;

    virtual std::unique_ptr<PreparedRenderOps> prepare(SceneNode const &node) {
        (void)node;
        return nullptr;
    }
    virtual Canvas *canvas() noexcept { return nullptr; }
};

} // namespace scenegraph
} // namespace lambda
