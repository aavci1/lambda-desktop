#pragma once

#include <Lambda/Reactive/Scope.hpp>
#include <Lambda/Reactive/SmallFn.hpp>
#include <Lambda/UI/Environment.hpp>
#include <Lambda/UI/EnvironmentBinding.hpp>
#include <Lambda/Layout/LayoutEngine.hpp>

#include <memory>

namespace lambdaui {

class MeasureContext;
class TextSystem;
struct Rectangle;
struct Text;
struct VStack;
struct HStack;
struct ZStack;

namespace scenegraph {
class SceneNode;
}

class MountContext {
public:
  MountContext(Reactive::Scope& owner, TextSystem& textSystem,
               MeasureContext& measureContext, LayoutConstraints constraints,
               LayoutHints hints = {}, Reactive::SmallFn<void()> requestRedraw = {},
               EnvironmentBinding environmentBinding = {});

  Reactive::Scope& owner() const noexcept { return *owner_; }
  EnvironmentBinding const& environmentBinding() const noexcept { return environmentBinding_; }
  TextSystem& textSystem() const noexcept { return textSystem_; }
  MeasureContext& measureContext() const noexcept { return measureContext_; }
  LayoutConstraints const& constraints() const noexcept { return constraints_; }
  LayoutHints const& hints() const noexcept { return hints_; }
  Reactive::SmallFn<void()> const& redrawCallback() const noexcept { return requestRedraw_; }

  MountContext childWithSharedScope(LayoutConstraints constraints, LayoutHints hints = {}) const;
  MountContext childWithOwnScope(LayoutConstraints constraints, LayoutHints hints = {}) const;
  MountContext childWithEnvironment(EnvironmentBinding environment, LayoutConstraints constraints,
                                    LayoutHints hints = {}) const;
  MountContext child(LayoutConstraints constraints, LayoutHints hints = {}) const = delete;
  void requestRedraw() const;

private:
  MountContext(std::shared_ptr<Reactive::Scope> owner,
               TextSystem& textSystem, MeasureContext& measureContext,
               LayoutConstraints constraints, LayoutHints hints,
               Reactive::SmallFn<void()> requestRedraw,
               EnvironmentBinding environmentBinding);

  std::shared_ptr<Reactive::Scope> ownedOwner_;
  Reactive::Scope* owner_;
  EnvironmentBinding environmentBinding_;
  TextSystem& textSystem_;
  MeasureContext& measureContext_;
  LayoutConstraints constraints_;
  LayoutHints hints_;
  Reactive::SmallFn<void()> requestRedraw_;
};

namespace detail {

MountContext* currentMountContext() noexcept;

class CurrentMountContextScope {
public:
  explicit CurrentMountContextScope(MountContext& ctx) noexcept;
  CurrentMountContextScope(CurrentMountContextScope const&) = delete;
  CurrentMountContextScope& operator=(CurrentMountContextScope const&) = delete;
  ~CurrentMountContextScope();

private:
  MountContext* previous_ = nullptr;
};

std::unique_ptr<scenegraph::SceneNode> mountRectangle(Rectangle const& rectangle, MountContext& ctx);
std::unique_ptr<scenegraph::SceneNode> mountText(Text const& text, MountContext& ctx);
std::unique_ptr<scenegraph::SceneNode> mountVStack(VStack const& stack, MountContext& ctx);
std::unique_ptr<scenegraph::SceneNode> mountHStack(HStack const& stack, MountContext& ctx);
std::unique_ptr<scenegraph::SceneNode> mountZStack(ZStack const& stack, MountContext& ctx);

} // namespace detail
} // namespace lambdaui
