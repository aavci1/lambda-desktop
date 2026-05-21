#pragma once

#include "Compositor/Chrome/ChromeConfig.hpp"
#include "Compositor/Wayland/WaylandTypes.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace flux::compositor {

class WaylandServer {
public:
  using ShortcutAction = flux::compositor::ShortcutAction;
  using ShortcutBinding = flux::compositor::ShortcutBinding;

  explicit WaylandServer(WaylandOutputInfo output);
  ~WaylandServer();

  WaylandServer(WaylandServer const&) = delete;
  WaylandServer& operator=(WaylandServer const&) = delete;

  [[nodiscard]] char const* socketName() const noexcept;
  [[nodiscard]] int eventFd() const noexcept;
  [[nodiscard]] float preferredScale() const noexcept;
  [[nodiscard]] std::int32_t logicalOutputWidth() const noexcept;
  [[nodiscard]] std::int32_t logicalOutputHeight() const noexcept;
  [[nodiscard]] std::size_t toplevelCount() const noexcept;
  [[nodiscard]] std::vector<CommittedSurfaceSnapshot> committedSurfaces() const;
  [[nodiscard]] std::optional<CommittedSurfaceSnapshot> cursorSurface() const;
  [[nodiscard]] std::optional<SnapPreviewSnapshot> snapPreview() const;
  [[nodiscard]] CommandLauncherSnapshot commandLauncher() const;
  [[nodiscard]] std::vector<int> duplicateDmabufFds(std::uint64_t surfaceId) const;
  [[nodiscard]] bool copyDmabufToRgba(std::uint64_t surfaceId, std::vector<std::uint8_t>& out) const;

  void dispatch();
  void flushClients();
  void setShortcutBindings(std::vector<ShortcutBinding> bindings);
  void setChromeConfig(ChromeConfig config);
  void setPreferredScale(float scale);
  void updateAnimations(std::uint32_t timeMs, bool animationsEnabled);
  [[nodiscard]] bool hasActiveAnimations() const noexcept;
  void sendFrameCallbacks(std::uint32_t timeMs, PresentationTiming timing);
  void handlePointerMotion(double dx, double dy, std::uint32_t timeMs);
  void handlePointerPosition(double x, double y, std::uint32_t timeMs);
  void handlePointerButton(std::uint32_t button, bool pressed, std::uint32_t timeMs);
  void handlePointerAxis(double dx, double dy, std::uint32_t timeMs);
  void handleKeyboardKey(std::uint32_t key, bool pressed, std::uint32_t timeMs);
  [[nodiscard]] float pointerX() const noexcept;
  [[nodiscard]] float pointerY() const noexcept;
  [[nodiscard]] CursorShape cursorShape() const noexcept;

  struct Impl;

private:
  std::unique_ptr<Impl> impl_;
};

} // namespace flux::compositor
