#pragma once

/// \file Flux/UI/Window.hpp
///
/// Part of the Flux public API.


#include <Flux/UI/Action.hpp>
#include <Flux/UI/EnvironmentBinding.hpp>
#include <Flux/Core/Identity.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include <Flux/UI/Cursor.hpp>
#include <Flux/Core/Geometry.hpp>
#include <Flux/Core/Color.hpp>
#include <Flux/UI/WindowChrome.hpp>

namespace flux {

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
/// - `glass` → native/compositor-backed window material where available
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

struct LayerShellChromeOptions {
  LayerShellChromeStyle style = LayerShellChromeStyle::None;
  float blurRadius = 46.f;
  Color tint{0.86f, 0.96f, 1.f, 0.56f};
  Color borderColor{1.f, 1.f, 1.f, 0.62f};
  float tintOpacity = 1.f;
  bool squareBottomCorners = false;
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
  LayerShellChromeOptions chrome{};
};

struct WindowGlassOptions {
  bool enabled = false;
  /// Preferred blur radius for platforms that expose a tunable backdrop blur.
  /// Some backends map this to the nearest native material instead.
  float blurRadius = 46.f;
  /// Preferred tint for app chrome drawn over the material or compositor chrome
  /// that supports explicit tint metadata.
  Color tint{0.86f, 0.96f, 1.f, 0.56f};
  Color borderColor{1.f, 1.f, 1.f, 0.62f};
  float tintOpacity = 1.f;
};

struct WindowConfig {
  Size size = {1280, 720};
  std::string title = "Flux Application";
  WindowDecorationMode decorationMode = WindowDecorationMode::System;
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
  /// Request native/compositor-backed background glass for this window.
  /// Unsupported backends ignore this; apps should still draw a readable fallback background.
  WindowGlassOptions glass{};
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
  void setDecorationMode(WindowDecorationMode mode);
  WindowDecorationMode decorationMode() const;
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
  /// Default implementation clears with `clearColor()` then draws the retained scene tree (if any).
  virtual void render(Canvas& canvas);

  /// Color passed to the retained scene-tree render for the initial canvas clear. Default is transparent;
  /// use an opaque color if the scene has no full-window background rect.
  void setClearColor(Color color);
  Color clearColor() const;
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

  /// Registers an action descriptor. Must be called before the first build or during window setup —
  /// descriptors are static for the window lifetime. Calling again for the same name replaces it.
  void registerAction(std::string name, ActionDescriptor descriptor);

  /// True if \p name is registered and descriptor + handler enabled checks pass (for menus/toolbars).
  ///
  /// During an active `body()` pass, handler state is read from the **committed** action registry (the
  /// previous rebuild). The in-flight build buffer is not swapped until rebuild completes, so enabled
  /// UI can lag by one frame (e.g. clipboard or selection); the next reactive pass corrects it.
  bool isActionEnabled(std::string const& name) const;

  /// Dispatches a named action through the same focused-view first, then window-action ordering used
  /// for shortcuts. Returns true if an enabled handler fired.
  bool dispatchAction(std::string const& name);

  /// Sets the root view component (declarative UI). Creates internal state on first call.
  /// Definition in `<Flux/UI/WindowUI.hpp>` (include that header in TUs that call `setView`).
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

  std::unordered_map<std::string, ActionDescriptor> const& actionDescriptors() const;

  std::string const& restoreId() const;
  WindowState currentWindowState() const;
  void applyRestoredWindowState(WindowState const& state);
  void refreshChromeMetrics();
  void beginWindowDrag(InputEvent const& event);
  void beginWindowResize(WindowResizeEdge edge, InputEvent const& event);

  /// Used by `Application` (friend); implementation on `Impl`.
  platform::Window* platformWindow() const;
  /// Used by `Window::setView` in `<Flux/UI/WindowUI.hpp>`; implementation on `Impl`.
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

} // namespace flux
