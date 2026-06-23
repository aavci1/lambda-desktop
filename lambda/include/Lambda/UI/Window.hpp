#pragma once

/// \file Lambda/UI/Window.hpp
///
/// Part of the Lambda public API.


#include <Lambda/UI/Command.hpp>
#include <Lambda/UI/EnvironmentBinding.hpp>
#include <Lambda/Core/Identity.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include <Lambda/UI/Cursor.hpp>
#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Core/Color.hpp>
#include <Lambda/Graphics/Styles.hpp>
#include <Lambda/UI/WindowChrome.hpp>

namespace lambdaui {

struct RootHolder;
class Element;
struct OverlayConfig;
struct OverlayId;
struct InputEvent;
struct PopupMenu;
struct Popover;

struct PopoverSurfaceId {
  std::uint64_t value = 0;
  bool isValid() const noexcept { return value != 0; }
  bool operator==(PopoverSurfaceId const&) const = default;
};

inline constexpr PopoverSurfaceId kInvalidPopoverSurfaceId{};

class Application;
class Canvas;
namespace platform {
class Window;
}
namespace scenegraph {
class SceneGraph;
}

class OverlayManager;

struct DisplayMode {
  int width = 0;
  int height = 0;
  /// Refresh rate in Hz. A value of 0 means any refresh rate at the requested resolution.
  int refreshHz = 0;
};

/// Per-backend window feature support. Query with `Window::platformCapabilities()`.
///
/// Backend matrix (config field → capability):
/// - `background.kind == Glass` → native/compositor-backed window material where available
/// - `layerShell` / `backgroundBlur` → Wayland compositor client only
/// - `outputName` / `displayMode` → KMS only
struct PlatformWindowCapabilities {
  bool supportsWindowGlass = false;
  bool supportsLayerShell = false;
  bool supportsBackgroundBlur = false;
  bool supportsOutputSelection = false;
  bool supportsDisplayMode = false;
};

enum class LayerShellLayer {
  Background,
  Bottom,
  Top,
  Overlay,
};

enum class LayerShellChromeStyle : std::uint8_t {
  None,
  BlurPanel,
  BlurPanelBorder,
};

enum class LayerShellBackgroundEffectShape : std::uint8_t {
  RoundedRect,
  Callout,
};

enum class LayerShellCalloutPlacement : std::uint8_t {
  Below,
  Above,
  End,
  Start,
};

struct GlassEffectOptions {
  /// Preferred blur radius for platforms that expose a tunable backdrop blur.
  /// Some backends map this to the nearest native material instead.
  float blurRadius = 46.f;
  /// Base material wash applied over the blurred backdrop before the tint.
  Color baseColor{1.f, 1.f, 1.f, 0.5f};
  /// Color tint applied over the base glass material.
  Color tintColor{0.86f, 0.96f, 1.f, 0.56f};
  Color borderColor{1.f, 1.f, 1.f, 0.62f};
  float opacity = 1.f;
};

struct LayerShellChromeOptions {
  LayerShellChromeStyle style = LayerShellChromeStyle::None;
  GlassEffectOptions glass{};
  CornerRadius cornerRadius{16.f};
};

struct LayerShellBackgroundEffectRegion {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  LayerShellBackgroundEffectShape shape = LayerShellBackgroundEffectShape::RoundedRect;
  LayerShellCalloutPlacement calloutPlacement = LayerShellCalloutPlacement::Below;
  float arrowWidth = 16.f;
  float arrowHeight = 8.f;
};

struct LayerShellInputRegion {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

struct LayerShellOptions {
  bool enabled = false;
  LayerShellLayer layer = LayerShellLayer::Top;
  std::string nameSpace;
  bool anchorTop = false;
  bool anchorBottom = false;
  bool anchorLeft = false;
  bool anchorRight = false;
  int marginTop = 0;
  int marginRight = 0;
  int marginBottom = 0;
  int marginLeft = 0;
  int exclusiveZone = 0;
  bool keyboardInteractive = false;
  /// When supported by the compositor, apply a blurred background effect to the full surface region.
  bool backgroundBlur = false;
  /// Optional surface-local region for the background effect. When omitted, the full surface is used.
  std::optional<LayerShellBackgroundEffectRegion> backgroundEffectRegion{};
  /// Optional surface-local input region. An empty region makes the surface pointer-transparent.
  std::optional<LayerShellInputRegion> inputRegion{};
  LayerShellChromeOptions chrome{};
};

enum class WindowBackgroundKind : std::uint8_t {
  Transparent,
  Fill,
  Glass,
};

struct WindowBackground {
  WindowBackgroundKind kind = WindowBackgroundKind::Fill;
  FillStyle fill = FillStyle::solid(Color::windowBackground());
  GlassEffectOptions glass{};

  static WindowBackground transparent();
  static WindowBackground solid(Color color);
  static WindowBackground gradient(FillStyle fill);
  static WindowBackground glassEffect();
  static WindowBackground glassEffect(GlassEffectOptions options);
};

struct WindowConfig {
  Size size = {1280, 720};
  std::string title = "Lambda Application";
  WindowTitlebarMode titlebar = WindowTitlebarMode::System;
  bool fullscreen = false;
  bool resizable = true;
  Size minSize{};
  Size maxSize{};
  std::string restoreId;
  /// On KMS, bind this window to a named output connector (for example "HDMI-A-1" or "DP-1").
  /// Empty means the platform default output. Other backends currently ignore this value.
  std::string outputName;
  /// On KMS, request a specific connector mode. Zero values use the output's preferred mode.
  /// Other backends currently ignore this value.
  DisplayMode displayMode{};
  /// On Wayland, create this window as a layer-shell surface instead of an xdg_toplevel.
  /// Other backends currently ignore this value.
  LayerShellOptions layerShell{};
};

struct WindowState {
  Rect frame{};
  bool fullscreen = false;
  Size contentSize{};
};

class Window {
public:
  virtual ~Window();

  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;
  Window(Window&&) = delete;
  Window& operator=(Window&&) = delete;

  Size getSize() const;
  void resize(Size const& size);
  void setTitle(std::string title);
  void setTitlebarMode(WindowTitlebarMode mode);
  WindowTitlebarMode titlebarMode() const;
  WindowChromeMetrics chromeMetrics() const;
  void beginWindowDrag();
  void beginWindowResize(WindowResizeEdge edge);
  void requestClose();
  void setFullscreen(bool fullscreen);
  bool showPopupMenu(PopupMenu menu, Rect anchor, std::uint32_t platformSerial = 0);
  PopoverSurfaceId showPopover(Popover popover, Rect anchor, std::uint32_t platformSerial = 0,
                               std::optional<ComponentKey> anchorTrackComponentKey = std::nullopt,
                               std::optional<ComponentKey> anchorTrackLeafKey = std::nullopt);
  void dismissPopover(PopoverSurfaceId id);
  /// Layer-shell windows only. Updates keyboard focus routing on the compositor.
  void setLayerShellKeyboardInteractive(bool enabled);
  /// Layer-shell windows only. Updates layer, anchors, margins, exclusive zone and chrome.
  void setLayerShellOptions(LayerShellOptions const& options);
  unsigned int handle() const;

  /// Lazily creates the backing canvas on first use.
  Canvas& canvas();
  void updateCanvasDpiScale(float scaleX, float scaleY);

  /// True after the retained scene tree has been created (first `sceneTree()` call).
  bool hasSceneGraph() const;

  /// Lazily creates the retained scene graph on first access. Does not create the canvas.
  scenegraph::SceneGraph& sceneGraph();
  scenegraph::SceneGraph const& sceneGraph() const;

  /// Request a frame; `Application::exec()` renders all windows when the event pump runs.
  void requestRedraw();

  /// Sets the platform mouse cursor shape. Called by Runtime; safe to call
  /// from any code that has a Window reference.
  void setCursor(Cursor kind);

  /// Like `requestRedraw()` but addressed to a specific window handle.
  static void postRedraw(unsigned int handle);

  /// Drawing only; `Application` wraps each call with `beginFrame` and `present` when handling redraw.
  /// Default implementation draws the configured window background then the retained scene tree (if any).
  virtual void render(Canvas& canvas);

  void setBackground(WindowBackground background);
  WindowBackground const& background() const;
  void setTheme(Theme theme);
  Theme const& theme() const;
  bool wantsTextInput() const;

  /// Pushes content onto the overlay stack. Safe from event handlers and outside build passes.
  /// Returns a handle for `removeOverlay`.
  OverlayId pushOverlay(Element content, OverlayConfig config);

  /// Removes the overlay with the given id; no-op if invalid or already removed. Calls `onDismiss`.
  void removeOverlay(OverlayId id);

  /// Removes all overlays; calls `onDismiss` for each.
  void clearOverlays();

  OverlayManager& overlayManager();
  OverlayManager const& overlayManager() const;

  /// Registers a command descriptor with the application. Must be called before the first build or
  /// during setup; calling again for the same name replaces it.
  void registerCommand(std::string name, CommandDescriptor descriptor);
  void registerAction(std::string name, ActionDescriptor descriptor) {
    registerCommand(std::move(name), std::move(descriptor));
  }

  /// True if \p name is registered and descriptor + handler enabled checks pass (for menus/toolbars).
  ///
  /// During an active `body()` pass, handler state is read from the **committed** command registry (the
  /// previous rebuild). The in-flight build buffer is not swapped until rebuild completes, so enabled
  /// UI can lag by one frame (e.g. clipboard or selection); the next reactive pass corrects it.
  bool isCommandEnabled(std::string const& name) const;
  bool isActionEnabled(std::string const& name) const { return isCommandEnabled(name); }

  /// Dispatches a named command through the same focused-view first, then window-handler ordering used
  /// for shortcuts. Returns true if an enabled handler fired.
  bool dispatchCommand(std::string const& name);
  bool dispatchAction(std::string const& name) { return dispatchCommand(name); }

  /// Sets the root view component (declarative UI). Creates internal state on first call.
  /// Definition in `<Lambda/UI/WindowUI.hpp>` (include that header in TUs that call `setView`).
  ///
  /// Pass a component with `setView(std::move(c))` when `C` is movable/copyable.
  /// For a default-constructible root whose subcomponents own non-movable state (e.g. `Signal`),
  /// use `setView<C>()` so the root is built in place on the heap (no move of inner state).
  template<typename C>
  void setView(C&& component);

  template<typename C>
  void setView();

  EnvironmentBinding const& environmentBinding() const;

  /// Reports which optional `WindowConfig` fields the current platform backend honors.
  [[nodiscard]] PlatformWindowCapabilities platformCapabilities() const;

  template<typename T>
  void setEnvironmentValue(typename EnvironmentKey<T>::Value value);

  template<typename T>
  typename EnvironmentKey<T>::Value environmentValue() const;

protected:
  friend class Application;

  explicit Window(const WindowConfig& config);

private:
  friend class Runtime;
  friend class InputDispatcher;

  EnvironmentBinding& environmentBindingMut();

  std::unordered_map<std::string, CommandDescriptor> const& commandDescriptors() const;
  std::unordered_map<std::string, ActionDescriptor> const& actionDescriptors() const { return commandDescriptors(); }

  std::string const& restoreId() const;
  WindowState currentWindowState() const;
  void applyRestoredWindowState(WindowState const& state);
  void setTransientParent(unsigned int parentHandle, bool modal);
  void refreshChromeMetrics();
  void beginWindowDrag(InputEvent const& event);
  void beginWindowResize(WindowResizeEdge edge, InputEvent const& event);

  /// Used by `Application` (friend); implementation on `Impl`.
  platform::Window* platformWindow() const;
  /// Used by `Window::setView` in `<Lambda/UI/WindowUI.hpp>`; implementation on `Impl`.
  void setViewRoot(std::unique_ptr<RootHolder> holder);

  struct Impl;
  std::unique_ptr<Impl> d;
};

template<typename T>
void Window::setEnvironmentValue(typename EnvironmentKey<T>::Value value) {
  environmentBindingMut() = environmentBinding().withValue<T>(std::move(value));
}

template<typename T>
typename EnvironmentKey<T>::Value Window::environmentValue() const {
  return environmentBinding().value<T>();
}

} // namespace lambdaui
