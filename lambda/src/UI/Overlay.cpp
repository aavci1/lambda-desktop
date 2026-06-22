#include <Lambda/UI/Overlay.hpp>

#include <Lambda/UI/Application.hpp>
#include <Lambda/UI/Window.hpp>
#include <Lambda/UI/Detail/Runtime.hpp>
#include <Lambda/UI/Element.hpp>
#include <Lambda/UI/MeasureContext.hpp>
#include <Lambda/UI/MountContext.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/SceneGraph/SceneNode.hpp>
#include <Lambda/UI/InteractionData.hpp>
#include <Lambda/SceneGraph/RectNode.hpp>
#include <Lambda/SceneGraph/SceneInteraction.hpp>

#include "UI/ViewLayout/OverlayLayout.hpp"

#include <algorithm>
#include <cassert>
#include <tuple>
#include <utility>

namespace lambda {

namespace {

LayoutConstraints overlayConstraints(Size windowSize, OverlayConfig const& config) {
  LayoutConstraints constraints{};
  constraints.minWidth = 0.f;
  constraints.minHeight = 0.f;
  constraints.maxWidth = windowSize.width;
  constraints.maxHeight = windowSize.height;
  if (config.maxSize) {
    if (config.maxSize->width > 0.f) {
      constraints.maxWidth = std::min(constraints.maxWidth, config.maxSize->width);
    }
    if (config.maxSize->height > 0.f) {
      constraints.maxHeight = std::min(constraints.maxHeight, config.maxSize->height);
    }
  }
  return constraints;
}

void resolveOverlayBackdropDefaults(OverlayConfig& config, Theme const& theme) {
  if (config.backdropBlurRadius != kFloatFromTheme) {
    return;
  }
  config.backdropBlurRadius = config.modal ? theme.modalBackdropBlurRadius
                                           : theme.popoverBackdropBlurRadius;
}

Rect contentBoundsFor(scenegraph::SceneNode const* contentNode) {
  if (!contentNode) {
    return Rect{0.f, 0.f, 1.f, 1.f};
  }
  Rect bounds = contentNode->bounds();
  if (bounds.width <= 0.f || bounds.height <= 0.f) {
    Size const size = contentNode->size();
    bounds = Rect{0.f, 0.f, std::max(1.f, size.width), std::max(1.f, size.height)};
  }
  if (bounds.width <= 0.f) {
    bounds.width = 1.f;
  }
  if (bounds.height <= 0.f) {
    bounds.height = 1.f;
  }
  return bounds;
}

Rect windowRectForNode(scenegraph::SceneNode const& node) {
  Point origin{};
  scenegraph::SceneNode const* current = &node;
  while (current) {
    origin.x += current->position().x;
    origin.y += current->position().y;
    current = current->parent();
  }
  Size const size = node.size();
  return Rect{origin.x, origin.y, std::max(0.f, size.width), std::max(0.f, size.height)};
}

Rect adjustedAnchor(OverlayConfig const& config) {
  Rect anchor = *config.anchor;
  if (config.anchorMaxHeight && anchor.height > *config.anchorMaxHeight) {
    anchor.height = *config.anchorMaxHeight;
  }
  EdgeInsets const outsets = config.anchorOutsets;
  anchor.x -= outsets.left;
  anchor.y -= outsets.top;
  anchor.width += outsets.left + outsets.right;
  anchor.height += outsets.top + outsets.bottom;
  anchor.width = std::max(0.f, anchor.width);
  anchor.height = std::max(0.f, anchor.height);
  return anchor;
}

std::optional<Rect> trackedAnchorRect(Window& window, OverlayConfig const& config) {
  if (!window.hasSceneGraph()) {
    return std::nullopt;
  }
  if (config.anchorTrackComponentKey && !config.anchorTrackComponentKey->empty()) {
    auto const [node, interaction] =
        scenegraph::findInteractionByKey(window.sceneGraph(), *config.anchorTrackComponentKey);
    (void)interaction;
    if (node) {
      return windowRectForNode(*node);
    }
  }
  if (config.anchorTrackLeafKey && !config.anchorTrackLeafKey->empty()) {
    auto const [node, interaction] =
        scenegraph::findInteractionByKey(window.sceneGraph(), *config.anchorTrackLeafKey);
    (void)interaction;
    if (node) {
      return windowRectForNode(*node);
    }
    if (std::optional<Rect> rect = window.sceneGraph().rectForLeafKeyPrefix(*config.anchorTrackLeafKey)) {
      return rect;
    }
  }
  return std::nullopt;
}

void updateTrackedAnchor(OverlayEntry& entry, Window& window) {
  if (std::optional<Rect> tracked = trackedAnchorRect(window, entry.config)) {
    entry.config.anchor = *tracked;
  }
}

float availableAbove(Rect const& anchor) {
  return std::max(0.f, anchor.y);
}

float availableBelow(Rect const& anchor, Size windowSize) {
  return std::max(0.f, windowSize.height - (anchor.y + anchor.height));
}

float availableStart(Rect const& anchor) {
  return std::max(0.f, anchor.x);
}

float availableEnd(Rect const& anchor, Size windowSize) {
  return std::max(0.f, windowSize.width - (anchor.x + anchor.width));
}

Vec2 offsetForPlacement(OverlayConfig::Placement placement, float gap) {
  switch (placement) {
  case OverlayConfig::Placement::Below:
    return Vec2{0.f, gap};
  case OverlayConfig::Placement::Above:
    return Vec2{0.f, -gap};
  case OverlayConfig::Placement::End:
    return Vec2{gap, 0.f};
  case OverlayConfig::Placement::Start:
    return Vec2{-gap, 0.f};
  }
  return Vec2{};
}

OverlayConfig::Placement resolveAutoFlipPlacement(OverlayConfig const& config,
                                                  Size windowSize,
                                                  Rect contentBounds) {
  if (!config.autoFlipPreferredPlacement || !config.anchor) {
    return config.placement;
  }

  OverlayConfig::Placement const preferred = *config.autoFlipPreferredPlacement;
  Rect const anchor = adjustedAnchor(config);
  float const desiredWidth = contentBounds.width + config.autoFlipGap;
  float const desiredHeight = contentBounds.height + config.autoFlipGap;

  switch (preferred) {
  case OverlayConfig::Placement::Below:
    if (desiredHeight > 0.f && availableBelow(anchor, windowSize) < desiredHeight &&
        availableAbove(anchor) > availableBelow(anchor, windowSize)) {
      return OverlayConfig::Placement::Above;
    }
    return OverlayConfig::Placement::Below;
  case OverlayConfig::Placement::Above:
    if (desiredHeight > 0.f && availableAbove(anchor) < desiredHeight &&
        availableBelow(anchor, windowSize) > availableAbove(anchor)) {
      return OverlayConfig::Placement::Below;
    }
    return OverlayConfig::Placement::Above;
  case OverlayConfig::Placement::End:
    if (desiredWidth > 0.f && availableEnd(anchor, windowSize) < desiredWidth &&
        availableStart(anchor) > availableEnd(anchor, windowSize)) {
      return OverlayConfig::Placement::Start;
    }
    return OverlayConfig::Placement::End;
  case OverlayConfig::Placement::Start:
    if (desiredWidth > 0.f && availableStart(anchor) < desiredWidth &&
        availableEnd(anchor, windowSize) > availableStart(anchor)) {
      return OverlayConfig::Placement::End;
    }
    return OverlayConfig::Placement::Start;
  }
  return preferred;
}

bool updateAutoFlipPlacement(OverlayEntry& entry, Size windowSize, Rect contentBounds) {
  OverlayConfig::Placement const resolved =
      resolveAutoFlipPlacement(entry.config, windowSize, contentBounds);
  if (resolved == entry.config.placement) {
    entry.resolvedPlacement = std::optional<OverlayConfig::Placement>{entry.config.placement};
    return false;
  }
  entry.config.placement = resolved;
  entry.config.offset = offsetForPlacement(resolved, entry.config.autoFlipGap);
  entry.resolvedPlacement = std::optional<OverlayConfig::Placement>{resolved};
  return true;
}

std::unique_ptr<InteractionData> makeBackdropInteraction(Window& window,
                                                                     OverlayEntry& entry,
                                                                     bool dismissOnTap,
                                                                     bool captureScroll) {
  auto interaction = std::make_unique<InteractionData>();
  interaction->cursor = Cursor::Arrow;
  if (captureScroll) {
    interaction->onScroll = [](Vec2) {};
  }
  if (dismissOnTap) {
    OverlayId const id = entry.id;
    interaction->onTap = [&window, id](MouseButton button) {
      if (button == MouseButton::Left) {
        window.removeOverlay(id);
      }
    };
  } else {
    interaction->onTap = [](MouseButton) {};
  }
  return interaction;
}

void insertBackdrop(scenegraph::SceneNode& root, OverlayEntry& entry, Size windowSize,
                    Window& window, bool dismissOnTap, bool captureScroll) {
  float const ox = -entry.resolvedFrame.x;
  float const oy = -entry.resolvedFrame.y;
  if (entry.config.backdropBlurRadius <= 0.f) {
    auto backdrop = std::make_unique<scenegraph::RectNode>(
        Rect{ox, oy, windowSize.width, windowSize.height},
        FillStyle::solid(entry.config.backdropColor));
    root.appendChild(std::move(backdrop));
  }

  auto capture = std::make_unique<scenegraph::RectNode>(Rect{ox, oy, windowSize.width, windowSize.height});
  capture->setInteraction(makeBackdropInteraction(window, entry, dismissOnTap, captureScroll));
  root.appendChild(std::move(capture));
}

std::unique_ptr<scenegraph::SceneNode> takeMountedContentNode(OverlayEntry& entry) {
  if (!entry.content) {
    return nullptr;
  }
  std::vector<std::unique_ptr<scenegraph::SceneNode>> children =
      entry.sceneGraph.root().releaseChildren();
  if (children.empty()) {
    return nullptr;
  }
  std::unique_ptr<scenegraph::SceneNode> contentNode = std::move(children.back());
  children.pop_back();
  return contentNode;
}

void rebuildOverlayRoot(OverlayEntry& entry, Size windowSize, Window& window,
                        std::unique_ptr<scenegraph::SceneNode> contentNode) {
  updateTrackedAnchor(entry, window);
  Rect const contentBounds = contentBoundsFor(contentNode.get());
  entry.resolvedFrame = layout::resolveOverlayFrame(windowSize, entry.config, contentBounds);

  auto root = std::make_unique<scenegraph::SceneNode>(
      Rect{0.f, 0.f, std::max(windowSize.width, entry.resolvedFrame.width),
           std::max(windowSize.height, entry.resolvedFrame.height)});
  bool capturesBackdrop = false;
  bool dismissBackdropTap = false;
  bool captureBackdropScroll = false;
  if (entry.config.modal) {
    insertBackdrop(*root, entry, windowSize, window, false, true);
    capturesBackdrop = true;
    captureBackdropScroll = true;
  } else if (entry.config.backdropColor.a > 0.001f || entry.config.dismissOnOutsideTap) {
    bool const visibleBackdrop = entry.config.backdropColor.a > 0.001f;
    insertBackdrop(*root, entry, windowSize, window, entry.config.dismissOnOutsideTap,
                   visibleBackdrop);
    capturesBackdrop = true;
    dismissBackdropTap = entry.config.dismissOnOutsideTap;
    captureBackdropScroll = visibleBackdrop;
  }
  if (capturesBackdrop) {
    root->setInteraction(makeBackdropInteraction(window, entry, dismissBackdropTap,
                                                 captureBackdropScroll));
  }
  if (contentNode) {
    root->appendChild(std::move(contentNode));
  }
  entry.sceneGraph.setRoot(std::move(root));
}

void mountOverlay(OverlayEntry& entry, Size windowSize, Runtime& runtime,
                  LayoutConstraints const& constraints) {
  entry.scope.dispose();
  entry.scope = Reactive::Scope{};
  entry.sceneGraph.releaseRoot();

  std::unique_ptr<scenegraph::SceneNode> contentNode;
  if (entry.content) {
    entry.resolvedPlacement = std::optional<OverlayConfig::Placement>{entry.config.placement};
    EnvironmentBinding const overlayEnvironment =
        runtime.window().environmentBinding().withSignal<ResolvedOverlayPlacementKey>(
            entry.resolvedPlacement);
    MeasureContext measureContext{Application::instance().textSystem(), overlayEnvironment};
    MountContext context{entry.scope, Application::instance().textSystem(),
                         measureContext, constraints, LayoutHints{}, [handle = runtime.window().handle()] {
                           Window::postRedraw(handle);
                         }, overlayEnvironment};

    contentNode = Reactive::withOwner(entry.scope, [&] {
      return entry.content->mount(context);
    });
  }
  updateTrackedAnchor(entry, runtime.window());
  if (contentNode && updateAutoFlipPlacement(entry, windowSize, contentBoundsFor(contentNode.get()))) {
    contentNode->relayout(constraints);
  }
  rebuildOverlayRoot(entry, windowSize, runtime.window(), std::move(contentNode));
}

} // namespace

std::tuple<std::function<void(Element, OverlayConfig)>, std::function<void()>, bool> useOverlay() {
  Runtime* runtime = Runtime::current();
  assert(runtime && "useOverlay must be called while mounting a Lambda view");

  auto id = std::make_shared<OverlayId>(kInvalidOverlayId);
  Window* window = &runtime->window();

  Reactive::onCleanup([id, window] {
    if (id->isValid()) {
      OverlayId const removeId = *id;
      *id = kInvalidOverlayId;
      window->removeOverlay(removeId);
    }
  });

  auto hide = [id, window] {
    if (!id->isValid()) {
      return;
    }
    OverlayId const removeId = *id;
    *id = kInvalidOverlayId;
    window->removeOverlay(removeId);
  };

  auto show = [id, window](Element element, OverlayConfig config) {
    if (id->isValid()) {
      OverlayId const removeId = *id;
      *id = kInvalidOverlayId;
      window->removeOverlay(removeId);
    }
    *id = window->pushOverlay(std::move(element), std::move(config));
  };

  return {
      std::move(show),
      std::move(hide),
      id->isValid(),
  };
}

void OverlayManager::rebuild(Size windowSize, Runtime& runtime) {
  for (auto& entryPtr : overlays_) {
    OverlayEntry& entry = *entryPtr;
    LayoutConstraints const constraints = overlayConstraints(windowSize, entry.config);
    std::unique_ptr<scenegraph::SceneNode> contentNode = takeMountedContentNode(entry);
    if (contentNode) {
      if (!contentNode->relayout(constraints)) {
        contentNode->setSize(Size{constraints.maxWidth, constraints.maxHeight});
      }
      updateTrackedAnchor(entry, runtime.window());
      if (updateAutoFlipPlacement(entry, windowSize, contentBoundsFor(contentNode.get()))) {
        if (!contentNode->relayout(constraints)) {
          contentNode->setSize(Size{constraints.maxWidth, constraints.maxHeight});
        }
      }
      rebuildOverlayRoot(entry, windowSize, runtime.window(), std::move(contentNode));
    } else {
      mountOverlay(entry, windowSize, runtime, constraints);
    }
  }
}

void OverlayManager::remountEntry(OverlayId id, Runtime& runtime) {
  OverlayEntry* entry = find(id);
  if (!entry) {
    return;
  }
  mountOverlay(*entry, runtime.window().getSize(), runtime,
               overlayConstraints(runtime.window().getSize(), entry->config));
  runtime.window().requestRedraw();
}

OverlayId OverlayManager::push(Element content, OverlayConfig config, Runtime* runtime) {
  resolveOverlayBackdropDefaults(config, runtime ? runtime->window().theme() : Theme::light());
  auto entry = std::make_unique<OverlayEntry>();
  entry->id = OverlayId{nextId_++};
  entry->content.emplace(std::move(content));
  entry->config = std::move(config);
  entry->resolvedPlacement = std::optional<OverlayConfig::Placement>{entry->config.placement};
  OverlayId id = entry->id;
  overlays_.push_back(std::move(entry));
  if (runtime) {
    rebuild(runtime->window().getSize(), *runtime);
    runtime->window().requestRedraw();
  } else if (Application::hasInstance()) {
    Application::instance().requestRedraw();
  }
  return id;
}

bool OverlayManager::hasTrackedAnchors() const noexcept {
  return std::any_of(overlays_.begin(), overlays_.end(), [](std::unique_ptr<OverlayEntry> const& entry) {
    return entry && (entry->config.anchorTrackComponentKey.has_value() ||
                     entry->config.anchorTrackLeafKey.has_value());
  });
}

void OverlayManager::remove(OverlayId id, Runtime* runtime) {
  std::function<void()> onDismiss;
  std::erase_if(overlays_, [&](std::unique_ptr<OverlayEntry> const& entry) {
    if (entry && entry->id == id) {
      onDismiss = entry->config.onDismiss;
      return true;
    }
    return false;
  });
  if (runtime) {
    runtime->window().requestRedraw();
  } else if (Application::hasInstance()) {
    Application::instance().requestRedraw();
  }
  if (onDismiss) {
    onDismiss();
  }
}

void OverlayManager::clear(Runtime* runtime, bool invokeDismissCallbacks) {
  if (invokeDismissCallbacks) {
    for (auto const& entry : overlays_) {
      if (entry && entry->config.onDismiss) {
        entry->config.onDismiss();
      }
    }
  }
  overlays_.clear();
  if (!runtime && !invokeDismissCallbacks) {
    return;
  }
  if (runtime) {
    runtime->window().requestRedraw();
  } else if (Application::hasInstance()) {
    Application::instance().requestRedraw();
  }
}

OverlayEntry const* OverlayManager::top() const {
  return overlays_.empty() ? nullptr : overlays_.back().get();
}

OverlayEntry* OverlayManager::find(OverlayId id) {
  for (auto& entry : overlays_) {
    if (entry && entry->id == id) {
      return entry.get();
    }
  }
  return nullptr;
}

OverlayEntry const* OverlayManager::find(OverlayId id) const {
  for (auto const& entry : overlays_) {
    if (entry && entry->id == id) {
      return entry.get();
    }
  }
  return nullptr;
}

} // namespace lambda
