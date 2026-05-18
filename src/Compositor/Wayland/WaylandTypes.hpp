#pragma once

#include <cstdint>
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

enum class ShortcutAction : std::uint8_t {
  CloseFocused,
  CycleFocus,
  SnapLeft,
  SnapRight,
  Maximize,
  Restore,
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

} // namespace flux::compositor
