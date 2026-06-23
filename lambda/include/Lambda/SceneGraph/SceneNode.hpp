#pragma once

/// \file Lambda/SceneGraph/SceneNode.hpp
///
/// Concrete scene-graph base node type. Each node stores parent-space bounds and renders itself in
/// local coordinates. A bare SceneNode is a pure container: it holds children but has no draw
/// payload of its own.

#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Reactive/SmallFn.hpp>
#include <Lambda/SceneGraph/LayoutConstraints.hpp>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace lambdaui {

namespace scenegraph {

class Renderer;
class PreparedRenderOps;
class Interaction;
class SceneNode;

namespace detail {
struct SceneNodeAccess;
bool isTransientRelayout() noexcept;
Rect subtreeLocalVisualBounds(SceneNode const& node) noexcept;
}

using RelayoutFn = Reactive::SmallFn<void(LayoutConstraints const&), 64>;

enum class SceneNodeKind : std::uint8_t {
  Group,
  RasterCache,
  Rect,
  Text,
  Image,
  Path,
  Render,
};

enum class LayoutFlow : std::uint8_t {
  None,
  VerticalStack,
  HorizontalStack,
};

std::string_view sceneNodeKindName(SceneNodeKind kind) noexcept;

class SceneNode {
  public:
    explicit SceneNode(Rect bounds = {});
    virtual ~SceneNode();

    SceneNode(SceneNode const &) = delete;
    SceneNode &operator=(SceneNode const &) = delete;
    SceneNode(SceneNode &&) = delete;
    SceneNode &operator=(SceneNode &&) = delete;

    SceneNodeKind kind() const noexcept;
    Rect bounds() const noexcept;
    Point position() const noexcept;
    Size size() const noexcept;
    Mat3 const& transform() const noexcept;
    bool isDirty() const noexcept;
    bool isSubtreeDirty() const noexcept;
    LayoutFlow layoutFlow() const noexcept;
    float layoutSpacing() const noexcept;

    void setBounds(Rect bounds);
    void setPosition(Point position);
    void setSize(Size size);
    void setTransform(Mat3 const& transform);
    void setLayoutFlow(LayoutFlow flow) noexcept;
    void setLayoutSpacing(float spacing) noexcept;
    void setLayoutConstraints(LayoutConstraints constraints) noexcept;
    bool hasLayoutConstraints() const noexcept;
    LayoutConstraints layoutConstraints() const noexcept;
    void setRelayout(RelayoutFn relayout);
    bool relayoutStoredConstraints();
    bool relayout(LayoutConstraints const& constraints, bool storeConstraints = true);

    /// Marks this node and ancestors dirty so the next stored relayout cannot be skipped.
    void invalidateSubtreeLayout() noexcept;

    SceneNode *parent() const noexcept;
    std::span<std::unique_ptr<SceneNode> const> children() const noexcept;
    std::span<std::unique_ptr<SceneNode>> children() noexcept;
    Interaction *interaction() noexcept;
    Interaction const *interaction() const noexcept;
    void setInteraction(std::unique_ptr<Interaction> interaction);

    void appendChild(std::unique_ptr<SceneNode> child);
    void insertChild(std::size_t index, std::unique_ptr<SceneNode> child);
    std::unique_ptr<SceneNode> removeChild(SceneNode &child);
    std::vector<std::unique_ptr<SceneNode>> releaseChildren();
    void replaceChildren(std::vector<std::unique_ptr<SceneNode>> children);

    template <std::derived_from<SceneNode> T, typename... Args>
    T &emplaceChild(Args &&...args) {
        auto child = std::make_unique<T>(std::forward<Args>(args)...);
        T &ref = *child;
        appendChild(std::move(child));
        return ref;
    }

    virtual Rect localBounds() const noexcept;
    virtual void render(Renderer &renderer) const;
    virtual bool canPrepareRenderOps() const noexcept;
    virtual std::uint64_t preparedRenderOpsKey(float dpiScale) const noexcept;

  protected:
    SceneNode(SceneNodeKind kind, Rect bounds);

    void markDirty() noexcept;
    void markSubtreeDirty() noexcept;

  private:
    SceneNodeKind kind_;
    Rect bounds_{};
    Mat3 transform_ = Mat3::identity();
    LayoutFlow layoutFlow_ = LayoutFlow::None;
    float layoutSpacing_ = 0.f;
    LayoutConstraints layoutConstraints_{};
    bool hasLayoutConstraints_ = false;
    SceneNode* parent_ = nullptr;
    std::vector<std::unique_ptr<SceneNode>> children_{};
    std::unique_ptr<Interaction> interaction_{};
    RelayoutFn relayout_{};
    mutable bool ownPaintingDirty_ = true;
    mutable bool subtreeDirty_ = true;
    mutable std::optional<Rect> cachedSubtreeVisualBounds_{};
    mutable std::uint8_t preparedGroupCacheCooldown_ = 0;
    mutable std::uint64_t preparedRenderOpsKey_ = 0;
    mutable std::unique_ptr<PreparedRenderOps> preparedRenderOps_{};

    friend struct detail::SceneNodeAccess;
    friend Rect detail::subtreeLocalVisualBounds(SceneNode const& node) noexcept;
};

} // namespace scenegraph
} // namespace lambdaui
