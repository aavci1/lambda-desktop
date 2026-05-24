#pragma once

#include <memory>
#include <optional>
#include <cstdint>
#include <string>

#include <Flux/UI/Cursor.hpp>
#include <Flux/Core/Geometry.hpp>
#include <Flux/UI/MenuItem.hpp>
#include <Flux/UI/Views/Popover.hpp>
#include <Flux/UI/WindowChrome.hpp>
#include <Flux/UI/Window.hpp>

namespace flux {

class Window;
class Canvas;
struct PopupMenu;
struct Popover;

namespace platform {

/// Internal abstract platform window; implemented in platform translation units. Not part of the public API.
class Window {
public:
  virtual ~Window() = default;

  virtual void setFluxWindow(::flux::Window* window) = 0;

  /// Present the native window after the Flux `Window` is registered and `setFluxWindow` has run.
  /// Implementations should not order the window on screen before this (so lifecycle callbacks see a
  /// valid `Window*`). Default: no-op.
  virtual void show() {}

  virtual std::unique_ptr<::flux::Canvas> createCanvas(::flux::Window& owner) = 0;

  virtual void resize(const Size& newSize) = 0;
  virtual void setMinSize(Size /*size*/) {}
  virtual void setMaxSize(Size /*size*/) {}
  virtual void setFullscreen(bool fullscreen) = 0;
  virtual void setTitle(const std::string& title) = 0;
  virtual void setTitlebarMode(WindowTitlebarMode /*mode*/) {}
  virtual WindowTitlebarMode titlebarMode() const { return WindowTitlebarMode::System; }
  virtual void setBackground(WindowBackground const& /*background*/) {}
  virtual WindowChromeMetrics chromeMetrics() const { return {}; }
  virtual void beginWindowDrag(std::uint32_t /*platformSerial*/ = 0) {}
  virtual void beginWindowResize(WindowResizeEdge /*edge*/, std::uint32_t /*platformSerial*/ = 0) {}
  virtual bool showPopupMenu(PopupMenu /*menu*/, Rect /*anchor*/, std::uint32_t /*platformSerial*/ = 0) {
    return false;
  }
  virtual PopoverSurfaceId showPopover(Popover /*popover*/, Rect /*anchor*/,
                                       std::uint32_t /*platformSerial*/ = 0) {
    return kInvalidPopoverSurfaceId;
  }
  virtual void repositionPopover(PopoverSurfaceId /*id*/, Popover const& /*popover*/, Rect /*anchor*/) {}
  virtual void dismissPopover(PopoverSurfaceId /*id*/) {}

  virtual Size currentSize() const = 0;
  virtual std::optional<Rect> currentFrame() const { return std::nullopt; }
  virtual void setFrame(Rect /*frame*/) {}
  virtual bool isFullscreen() const = 0;
  virtual unsigned int handle() const = 0;

  virtual void* nativeGraphicsSurface() const = 0;

  /// Drain queued AppKit/SDL events without blocking (used when a redraw is already pending).
  virtual void processEvents() {}

  /// Block until the next event or `timeoutMs` elapses; `timeoutMs < 0` waits indefinitely.
  virtual void waitForEvents(int /*timeoutMs*/) {}
  virtual int eventFd() const { return -1; }
  virtual int wakeFd() const { return -1; }

  /// Wake `waitForEvents` (e.g. after `requestRedraw`).
  virtual void wakeEventLoop() {}

  /// Arm the platform frame pump for the next display boundary.
  virtual void requestAnimationFrame() {}

  /// Marks the most recent frame boundary event as handled by the application loop.
  virtual void acknowledgeAnimationFrameTick() {}

  /// Called after a frame has been presented. `needsAnotherFrame` keeps the frame pump running.
  virtual void completeAnimationFrame(bool /*needsAnotherFrame*/) {}

  virtual void setCursor(Cursor /*kind*/) {}

  /// Layer-shell surfaces only. No-op on xdg toplevel windows.
  virtual void setLayerShellKeyboardInteractive(bool /*enabled*/) {}

  [[nodiscard]] virtual PlatformWindowCapabilities capabilities() const { return {}; }
};

} // namespace platform
} // namespace flux
