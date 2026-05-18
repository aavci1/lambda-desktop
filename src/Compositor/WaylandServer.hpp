#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace flux::compositor {

enum class CursorShape : std::uint8_t {
  Arrow,
  IBeam,
  Hand,
  Crosshair,
  ResizeEW,
  ResizeNS,
  ResizeNESW,
  ResizeNWSE,
  ResizeAll,
  NotAllowed,
};

struct WaylandOutputInfo {
  std::string name;
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::int32_t refreshMilliHz = 60'000;
  std::int32_t physicalWidthMm = 0;
  std::int32_t physicalHeightMm = 0;
};

struct CommittedSurfaceSnapshot {
  struct DmabufPlane {
    std::uint32_t offset = 0;
    std::uint32_t stride = 0;
    std::uint64_t modifier = 0;
  };

  std::uint64_t id = 0;
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::int32_t bufferWidth = 0;
  std::int32_t bufferHeight = 0;
  float sourceX = 0.f;
  float sourceY = 0.f;
  float sourceWidth = 0.f;
  float sourceHeight = 0.f;
  std::int32_t destinationWidth = 0;
  std::int32_t destinationHeight = 0;
  std::int32_t titleBarHeight = 0;
  std::string title;
  bool focused = false;
  bool activeSizing = false;
  std::uint64_t serial = 0;
  std::vector<std::uint8_t> rgbaPixels;
  std::uint32_t dmabufFormat = 0;
  std::vector<DmabufPlane> dmabufPlanes;
};

struct SnapPreviewSnapshot {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
};

class WaylandServer {
public:
  enum class ShortcutAction : std::uint8_t {
    CloseFocused,
    CycleFocus,
    SnapLeft,
    SnapRight,
    Terminate,
  };

  struct ShortcutBinding {
    ShortcutAction action = ShortcutAction::CloseFocused;
    std::uint32_t key = 0;
    bool meta = false;
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
  };

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
  [[nodiscard]] std::vector<int> duplicateDmabufFds(std::uint64_t surfaceId) const;
  [[nodiscard]] bool copyDmabufToRgba(std::uint64_t surfaceId, std::vector<std::uint8_t>& out) const;

  void dispatch();
  void flushClients();
  void setShortcutBindings(std::vector<ShortcutBinding> bindings);
  void setPreferredScale(float scale);
  void updateAnimations(std::uint32_t timeMs, bool animationsEnabled);
  void sendFrameCallbacks(std::uint32_t timeMs);
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
