#include <Flux/UI/Application.hpp>
#include <Flux/UI/EventQueue.hpp>
#include <Flux/UI/Events.hpp>
#include <Flux/Core/Geometry.hpp>
#include <Flux/Core/Color.hpp>
#include <Flux/UI/Cursor.hpp>
#include <Flux/UI/Window.hpp>
#include <Flux/UI/MenuItem.hpp>
#include <Flux/UI/Views/Popover.hpp>
#include <Flux/UI/Detail/RootHolder.hpp>
#include <Flux/UI/Detail/Runtime.hpp>
#include <Flux/Graphics/Canvas.hpp>
#include <Flux/SceneGraph/SceneInteraction.hpp>
#include <Flux/Reactive/Signal.hpp>
#include <Flux/SceneGraph/SceneGraph.hpp>
#include <Flux/SceneGraph/SceneNode.hpp>
#include <Flux/SceneGraph/SceneRenderer.hpp>
#include <Flux/UI/Overlay.hpp>
#include <Flux/UI/EnvironmentBinding.hpp>
#include <Flux/UI/EnvironmentKeys.hpp>
#include <Flux/UI/Theme.hpp>

#include <memory>
#include <utility>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>

#include "UI/Platform/Window.hpp"
#include "UI/Platform/WindowFactory.hpp"
#include "UI/WindowRender.hpp"
#include "Detail/ResizeTrace.hpp"
#include <chrono>
#include <optional>

namespace flux {

namespace {

void logUnsupportedWindowConfigOnce(char const* feature) {
  static bool loggedGlass = false;
  static bool loggedLayerShell = false;
  static bool loggedBackgroundBlur = false;
  static bool loggedOutputName = false;
  static bool loggedDisplayMode = false;
  bool* slot = nullptr;
  if (std::strcmp(feature, "glass") == 0) slot = &loggedGlass;
  else if (std::strcmp(feature, "layerShell") == 0) slot = &loggedLayerShell;
  else if (std::strcmp(feature, "backgroundBlur") == 0) slot = &loggedBackgroundBlur;
  else if (std::strcmp(feature, "outputName") == 0) slot = &loggedOutputName;
  else if (std::strcmp(feature, "displayMode") == 0) slot = &loggedDisplayMode;
  if (!slot || *slot) return;
  *slot = true;
  std::fprintf(stderr, "flux: WindowConfig.%s is not supported on this platform backend\n", feature);
}

void validateWindowConfig(WindowConfig const& config, PlatformWindowCapabilities const& capabilities) {
  char const* env = std::getenv("FLUX_LOG_WINDOW_CONFIG");
  if (!env || !*env || *env == '0') return;

  if (config.glass.enabled && !capabilities.supportsWindowGlass) {
    logUnsupportedWindowConfigOnce("glass");
  }
  if (config.layerShell.enabled && !capabilities.supportsLayerShell) {
    logUnsupportedWindowConfigOnce("layerShell");
  }
  if (config.layerShell.backgroundBlur && !capabilities.supportsBackgroundBlur) {
    logUnsupportedWindowConfigOnce("backgroundBlur");
  }
  if (!config.outputName.empty() && !capabilities.supportsOutputSelection) {
    logUnsupportedWindowConfigOnce("outputName");
  }
  if ((config.displayMode.width > 0 || config.displayMode.height > 0 || config.displayMode.refreshHz > 0) &&
      !capabilities.supportsDisplayMode) {
    logUnsupportedWindowConfigOnce("displayMode");
  }
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

Rect adjustedPopoverAnchor(Popover const& popover, Rect anchor) {
  if (popover.anchorMaxHeight && anchor.height > *popover.anchorMaxHeight) {
    anchor.height = *popover.anchorMaxHeight;
  }
  EdgeInsets const outsets = popover.anchorOutsets;
  anchor.x -= outsets.left;
  anchor.y -= outsets.top;
  anchor.width += outsets.left + outsets.right;
  anchor.height += outsets.top + outsets.bottom;
  anchor.width = std::max(0.f, anchor.width);
  anchor.height = std::max(0.f, anchor.height);
  return anchor;
}

} // namespace

struct Window::Impl {
  struct NativePopoverEntry {
    PopoverSurfaceId id{};
    Popover popover;
    Rect anchor{};
    std::optional<ComponentKey> anchorTrackComponentKey;
    std::optional<ComponentKey> anchorTrackLeafKey;
  };

  std::unique_ptr<platform::Window> platform_;
  std::unique_ptr<Canvas> canvas_;
  std::unique_ptr<scenegraph::SceneRenderer> sceneRenderer_;
  std::optional<scenegraph::SceneGraph> sceneGraph_;
  Color clearColor_ {Theme::light().windowBackgroundColor};
  WindowGlassOptions glassConfig_{};
  bool hasCustomClearColor_ = false;
  /// Declared before `runtime_` so `~Runtime` (and `OverlayHookSlot` teardown calling `removeOverlay`)
  /// runs while `OverlayManager` is still alive. Reverse member destruction order would destroy
  /// `overlayMgr_` first and use-after-free on window close with an open overlay.
  OverlayManager overlayMgr_;
  /// Declared before `runtime_` so the ring outlives `~Runtime` if teardown ever touches the overlay buffer.
  TextCacheRingBuffer textCacheRing_{};
  std::unique_ptr<Runtime> runtime_;
  std::unordered_map<std::string, ActionDescriptor> actions_;
  Reactive::Signal<Theme> themeSignal_{Theme::light()};
  Reactive::Signal<WindowChromeMetrics> chromeMetricsSignal_{WindowChromeMetrics{}};
  EnvironmentBinding windowEnvironmentBinding_{};
  std::vector<NativePopoverEntry> nativePopovers_;
  std::string restoreId_;
  bool shutdown_ = false;

  explicit Impl(Window&, WindowConfig const& config)
      : glassConfig_(config.glass)
      , restoreId_(config.restoreId) {
    windowEnvironmentBinding_ = EnvironmentBinding{}
                                    .withSignal<ThemeKey>(themeSignal_)
                                    .withSignal<WindowChromeMetricsKey>(chromeMetricsSignal_);
  }
  ~Impl();

  platform::Window* platformWindow() const { return platform_.get(); }
  void refreshChromeMetrics();
  void setViewRoot(Window& window, std::unique_ptr<RootHolder> holder);
  std::optional<Rect> trackedPopoverAnchor(Window& window, NativePopoverEntry const& entry) const;
  void updateNativePopoverAnchors(Window& window);
  void shutdown();
};

void Window::Impl::refreshChromeMetrics() {
  chromeMetricsSignal_.set(platform_ ? platform_->chromeMetrics() : WindowChromeMetrics{});
}

void Window::Impl::setViewRoot(Window& window, std::unique_ptr<RootHolder> holder) {
  if (!runtime_) {
    runtime_ = std::make_unique<Runtime>(window);
  }
  runtime_->setRoot(std::move(holder));
}

std::optional<Rect> Window::Impl::trackedPopoverAnchor(Window& window,
                                                       NativePopoverEntry const& entry) const {
  if (!window.hasSceneGraph()) {
    return std::nullopt;
  }
  if (entry.anchorTrackComponentKey && !entry.anchorTrackComponentKey->empty()) {
    auto const [node, interaction] =
        scenegraph::findInteractionByKey(window.sceneGraph(), *entry.anchorTrackComponentKey);
    (void)interaction;
    if (node) {
      return windowRectForNode(*node);
    }
  }
  if (entry.anchorTrackLeafKey && !entry.anchorTrackLeafKey->empty()) {
    auto const [node, interaction] =
        scenegraph::findInteractionByKey(window.sceneGraph(), *entry.anchorTrackLeafKey);
    (void)interaction;
    if (node) {
      return windowRectForNode(*node);
    }
    if (std::optional<Rect> rect = window.sceneGraph().rectForLeafKeyPrefix(*entry.anchorTrackLeafKey)) {
      return rect;
    }
  }
  return std::nullopt;
}

void Window::Impl::updateNativePopoverAnchors(Window& window) {
  if (!platform_ || nativePopovers_.empty()) {
    return;
  }
  for (NativePopoverEntry& entry : nativePopovers_) {
    std::optional<Rect> tracked = trackedPopoverAnchor(window, entry);
    if (!tracked) {
      continue;
    }
    Rect const adjusted = adjustedPopoverAnchor(entry.popover, *tracked);
    if (adjusted == entry.anchor) {
      continue;
    }
    entry.anchor = adjusted;
    platform_->repositionPopover(entry.id, entry.popover, adjusted);
  }
}

Window::Impl::~Impl() {
  shutdown();
}

void Window::Impl::shutdown() {
  if (shutdown_) {
    return;
  }
  shutdown_ = true;
  nativePopovers_.clear();
  if (runtime_) {
    runtime_->beginShutdown(sceneGraph_ ? &*sceneGraph_ : nullptr);
    overlayMgr_.clear(nullptr, false);
    runtime_.reset();
  } else {
    overlayMgr_.clear(nullptr, false);
  }
}

Window::Window(const WindowConfig& config) {
  d = std::make_unique<Impl>(*this, config);
  d->platform_ = platform::createWindow(config);
  validateWindowConfig(config, d->platform_->capabilities());
  d->platform_->setFluxWindow(this);
  d->refreshChromeMetrics();
  Application::instance().eventQueue().post(WindowLifecycleEvent{
      .kind = WindowLifecycleEvent::Kind::Registered,
      .handle = handle(),
      .window = this,
      .outputName = {},
  });
}

Window::~Window() {
  if (d) {
    d->shutdown();
  }
  const unsigned int id = handle();
  if (Application::hasInstance()) {
    Application::instance().unregisterWindowHandle(id);
    Application::instance().eventQueue().post(WindowLifecycleEvent{
        .kind = WindowLifecycleEvent::Kind::Unregistered,
        .handle = id,
        .window = nullptr,
        .outputName = {},
    });
  }
}

Size Window::getSize() const {
  return d->platform_->currentSize();
}

void Window::resize(Size const& size) {
  d->platform_->resize(size);
  requestRedraw();
}

void Window::setTitle(std::string title) {
  d->platform_->setTitle(std::move(title));
}

void Window::setDecorationMode(WindowDecorationMode mode) {
  d->platform_->setDecorationMode(mode);
  refreshChromeMetrics();
  requestRedraw();
}

WindowDecorationMode Window::decorationMode() const {
  return d->platform_->decorationMode();
}

WindowChromeMetrics Window::chromeMetrics() const {
  return d->platform_->chromeMetrics();
}

void Window::beginWindowDrag() {
  d->platform_->beginWindowDrag();
}

void Window::beginWindowResize(WindowResizeEdge edge) {
  d->platform_->beginWindowResize(edge);
}

void Window::beginWindowDrag(InputEvent const& event) {
  d->platform_->beginWindowDrag(event.platformSerial);
}

void Window::beginWindowResize(WindowResizeEdge edge, InputEvent const& event) {
  d->platform_->beginWindowResize(edge, event.platformSerial);
}

void Window::requestClose() {
  if (Application::hasInstance()) {
    Application::instance().eventQueue().post(WindowEvent{WindowEvent::Kind::CloseRequest, handle()});
  }
}

void Window::setFullscreen(bool fullscreen) {
  d->platform_->setFullscreen(fullscreen);
}

bool Window::showPopupMenu(PopupMenu menu, Rect anchor, std::uint32_t platformSerial) {
  return d->platform_->showPopupMenu(std::move(menu), anchor, platformSerial);
}

PopoverSurfaceId Window::showPopover(Popover popover, Rect anchor, std::uint32_t platformSerial,
                                     std::optional<ComponentKey> anchorTrackComponentKey,
                                     std::optional<ComponentKey> anchorTrackLeafKey) {
  Rect const adjustedAnchor = adjustedPopoverAnchor(popover, anchor);
  PopoverSurfaceId const id = d->platform_->showPopover(popover, adjustedAnchor, platformSerial);
  if (id.isValid() && (anchorTrackComponentKey.has_value() || anchorTrackLeafKey.has_value())) {
    d->nativePopovers_.push_back(Window::Impl::NativePopoverEntry{
        .id = id,
        .popover = std::move(popover),
        .anchor = adjustedAnchor,
        .anchorTrackComponentKey = std::move(anchorTrackComponentKey),
        .anchorTrackLeafKey = std::move(anchorTrackLeafKey),
    });
  }
  return id;
}

void Window::dismissPopover(PopoverSurfaceId id) {
  std::erase_if(d->nativePopovers_, [&](Window::Impl::NativePopoverEntry const& entry) {
    return entry.id == id;
  });
  d->platform_->dismissPopover(id);
}

void Window::setLayerShellKeyboardInteractive(bool enabled) {
  d->platform_->setLayerShellKeyboardInteractive(enabled);
}

unsigned int Window::handle() const {
  return d->platform_->handle();
}

Canvas& Window::canvas() {
  if (!d->canvas_) {
    d->canvas_ = d->platform_->createCanvas(*this);
  }
  return *d->canvas_;
}

void Window::updateCanvasDpiScale(float scaleX, float scaleY) {
  if (d->canvas_) {
    d->canvas_->updateDpiScale(scaleX, scaleY);
  }
}

bool Window::hasSceneGraph() const { return d->sceneGraph_.has_value(); }

scenegraph::SceneGraph& Window::sceneGraph() {
  if (!d->sceneGraph_) {
    d->sceneGraph_.emplace();
  }
  return *d->sceneGraph_;
}

scenegraph::SceneGraph const& Window::sceneGraph() const {
  return const_cast<Window*>(this)->sceneGraph();
}

void Window::requestRedraw() { postRedraw(handle()); }

void Window::setCursor(Cursor kind) {
  d->platform_->setCursor(kind);
}

platform::Window* Window::platformWindow() const {
  return d->platformWindow();
}

void Window::refreshChromeMetrics() {
  d->refreshChromeMetrics();
}

void Window::postRedraw(unsigned int handle) {
  if (!Application::hasInstance()) {
    return;
  }
  Application::instance().requestWindowRedraw(handle);
}

void Window::setClearColor(Color color) {
  d->clearColor_ = color;
  d->hasCustomClearColor_ = true;
}

Color Window::clearColor() const { return d->clearColor_; }

void Window::setTheme(Theme theme) {
  Color const clearColor = theme.windowBackgroundColor;
  d->themeSignal_.set(std::move(theme));
  d->windowEnvironmentBinding_ = EnvironmentBinding{}.withSignal<ThemeKey>(d->themeSignal_);
  if (!d->hasCustomClearColor_) {
    d->clearColor_ = clearColor;
  }
  requestRedraw();
}

Theme const& Window::theme() const {
  return d->themeSignal_.peek();
}

bool Window::wantsTextInput() const {
  return d->runtime_ && d->runtime_->wantsTextInput();
}

OverlayId Window::pushOverlay(Element content, OverlayConfig config) {
  if (!d) {
    return kInvalidOverlayId;
  }
  Runtime* rt = d->runtime_.get();
  return d->overlayMgr_.push(std::move(content), std::move(config), rt);
}

void Window::removeOverlay(OverlayId id) {
  if (!d) {
    return;
  }
  Runtime* rt = d->runtime_.get();
  d->overlayMgr_.remove(id, rt);
}

void Window::clearOverlays() {
  if (!d) {
    return;
  }
  Runtime* rt = d->runtime_.get();
  d->overlayMgr_.clear(rt);
}

OverlayManager& Window::overlayManager() { return d->overlayMgr_; }

OverlayManager const& Window::overlayManager() const { return d->overlayMgr_; }

void Window::registerAction(std::string name, ActionDescriptor descriptor) {
  d->actions_[std::move(name)] = std::move(descriptor);
}

bool Window::isActionEnabled(std::string const& name) const {
  auto it = d->actions_.find(name);
  if (it == d->actions_.end()) {
    return false;
  }
  if (it->second.isEnabled && !it->second.isEnabled()) {
    return false;
  }
  if (!d->runtime_) {
    return true;
  }
  return d->runtime_->isActionCurrentlyEnabled(name);
}

bool Window::dispatchAction(std::string const& name) {
  return d->runtime_ && d->runtime_->dispatchAction(name);
}

std::unordered_map<std::string, ActionDescriptor> const& Window::actionDescriptors() const {
  return d->actions_;
}

std::string const& Window::restoreId() const {
  return d->restoreId_;
}

WindowState Window::currentWindowState() const {
  WindowState state;
  if (auto frame = d->platform_->currentFrame()) {
    state.frame = *frame;
  }
  state.fullscreen = d->platform_->isFullscreen();
  state.contentSize = d->platform_->currentSize();
  return state;
}

void Window::applyRestoredWindowState(WindowState const& state) {
  if (state.frame.width > 0.f && state.frame.height > 0.f) {
    d->platform_->setFrame(state.frame);
  }
}

void Window::setViewRoot(std::unique_ptr<RootHolder> holder) {
  d->setViewRoot(*this, std::move(holder));
}

EnvironmentBinding const& Window::environmentBinding() const {
  return d->windowEnvironmentBinding_;
}

PlatformWindowCapabilities Window::platformCapabilities() const {
  return d->platform_ ? d->platform_->capabilities() : PlatformWindowCapabilities{};
}

EnvironmentBinding& Window::environmentBindingMut() {
  return d->windowEnvironmentBinding_;
}

void Window::render(Canvas& canvas) {
  bool const traceResize = detail::resizeTraceEnabled();
  auto const renderStart = traceResize ? std::chrono::steady_clock::now()
                                       : std::chrono::steady_clock::time_point{};
  if (!d->sceneRenderer_) {
    d->sceneRenderer_ = std::make_unique<scenegraph::SceneRenderer>(canvas);
  }
  Size const windowSize = getSize();
  if (d->runtime_) {
    d->updateNativePopoverAnchors(*this);
  }
  if (d->runtime_ && d->overlayMgr_.hasTrackedAnchors()) {
    d->overlayMgr_.rebuild(windowSize, *d->runtime_);
  }
  std::optional<Color> glassTint;
  if (d->glassConfig_.enabled) {
    Color tint = d->glassConfig_.tint;
    tint.a *= std::clamp(d->glassConfig_.tintOpacity, 0.f, 1.f);
    if (tint.a > 0.f) {
      glassTint = tint;
    }
  }
  renderWindowFrame(*d->sceneRenderer_, canvas, d->sceneGraph_, windowSize, d->overlayMgr_, d->runtime_.get(),
                    d->clearColor_, glassTint, d->textCacheRing_);
  if (traceResize) {
    auto const elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - renderStart).count();
    detail::resizeTrace("window-render",
                        "window=%u size=%.0fx%.0f elapsed=%.3fms\n",
                        handle(),
                        windowSize.width,
                        windowSize.height,
                        static_cast<double>(elapsed) / 1000.0);
  }
}

} // namespace flux
