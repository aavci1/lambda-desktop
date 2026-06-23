#pragma once

#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Reactive/Scope.hpp>
#include <Lambda/Reactive/SmallFn.hpp>
#include <Lambda/UI/EnvironmentBinding.hpp>

#include <memory>

namespace lambdaui {

struct RootHolder;
class TextSystem;

namespace scenegraph {
class SceneGraph;
class SceneNode;
}

class MountRoot {
public:
  MountRoot(std::unique_ptr<RootHolder> root, TextSystem& textSystem,
            EnvironmentBinding environment, Size viewportSize,
            Reactive::SmallFn<void()> requestRedraw = {});
  ~MountRoot();

  MountRoot(MountRoot const&) = delete;
  MountRoot& operator=(MountRoot const&) = delete;

  void mount(scenegraph::SceneGraph& sceneGraph);
  void unmount(scenegraph::SceneGraph& sceneGraph);
  void resize(Size viewportSize, scenegraph::SceneGraph& sceneGraph);

  bool mounted() const noexcept { return mounted_; }

private:
  std::unique_ptr<RootHolder> root_;
  TextSystem& textSystem_;
  EnvironmentBinding environment_;
  Size viewportSize_{};
  Reactive::SmallFn<void()> requestRedraw_;
  Reactive::Scope rootScope_{};
  bool mounted_ = false;
};

} // namespace lambdaui
