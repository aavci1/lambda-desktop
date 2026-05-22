#include <Flux/UI/Application.hpp>
#include <Flux/UI/EventQueue.hpp>
#include <Flux/UI/Events.hpp>
#include <Flux/Core/Geometry.hpp>
#include <Flux/Core/Color.hpp>
#include <Flux/UI/Cursor.hpp>
#include <Flux/UI/Window.hpp>
#include <Flux/UI/Detail/RootHolder.hpp>
#include <Flux/UI/Detail/Runtime.hpp>
#include <Flux/Graphics/Canvas.hpp>
#include <Flux/Reactive/Signal.hpp>
#include <Flux/SceneGraph/SceneGraph.hpp>
#include <Flux/SceneGraph/SceneRenderer.hpp>
#include <Flux/UI/Overlay.hpp>
#include <Flux/UI/EnvironmentBinding.hpp>
#include <Flux/UI/EnvironmentKeys.hpp>
#include <Flux/UI/Theme.hpp>

#include <memory>
#include <utility>

#include "UI/Platform/Window.hpp"
#include "UI/Platform/WindowFactory.hpp"
#include "UI/WindowRender.hpp"
#include "Detail/ResizeTrace.hpp"
#include <chrono>
#include <optional>

namespace flux {

struct Window::Impl {
  std::unique_ptr<platform::Window> platform_;
  std::unique_ptr<Canvas> canvas_;
  std::unique_ptr<scenegraph::SceneRenderer> sceneRenderer_;
  std::optional<scenegraph::SceneGraph> sceneGraph_;
  Color clearColor_ {Theme::light().windowBackgroundColor};
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
  std::string restoreId_;
  bool shutdown_ = false;

  explicit Impl(Window&, WindowConfig const& config)
      : restoreId_(config.restoreId) {
    windowEnvironmentBinding_ = EnvironmentBinding{}
                                    .withSignal<ThemeKey>(themeSignal_)
                                    .withSignal<WindowChromeMetricsKey>(chromeMetricsSignal_);
  }
  ~Impl();

  platform::Window* platformWindow() const { return platform_.get(); }
  void refreshChromeMetrics();
  void setViewRoot(Window& window, std::unique_ptr<RootHolder> holder);
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

Window::Impl::~Impl() {
  shutdown();
}

void Window::Impl::shutdown() {
  if (shutdown_) {
    return;
  }
  shutdown_ = true;
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
  if (d->runtime_ && d->overlayMgr_.hasTrackedAnchors()) {
    d->overlayMgr_.rebuild(windowSize, *d->runtime_);
  }
  renderWindowFrame(*d->sceneRenderer_, canvas, d->sceneGraph_, windowSize, d->overlayMgr_, d->runtime_.get(),
                    d->clearColor_, d->textCacheRing_);
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
