#pragma once

#include <memory>
#include <cstdint>

namespace lambdaui::scenegraph {

class SceneNode;
class PreparedRenderOps;

namespace detail {

struct SceneNodeAccess {
    static void markDirty(SceneNode &node) noexcept;
    static void markSubtreeDirty(SceneNode &node) noexcept;
    static void clearDirty(SceneNode const &node) noexcept;
    static bool ownPaintingDirty(SceneNode const &node) noexcept;
    static bool subtreeDirty(SceneNode const &node) noexcept;
    static void clearSubtreeDirty(SceneNode const &node) noexcept;
    static bool preparedGroupCacheSuppressed(SceneNode const &node) noexcept;
    static void suppressPreparedGroupCache(SceneNode const &node) noexcept;
    static std::uint8_t preparedGroupCacheCooldown(SceneNode const &node) noexcept;
    static void decrementPreparedGroupCacheCooldown(SceneNode const &node) noexcept;
    static std::uint64_t preparedRenderOpsKey(SceneNode const &node) noexcept;
    static void setPreparedRenderOpsKey(SceneNode const &node, std::uint64_t key) noexcept;
    static std::unique_ptr<PreparedRenderOps>& preparedRenderOps(SceneNode const &node) noexcept;
};

} // namespace detail
} // namespace lambdaui::scenegraph
