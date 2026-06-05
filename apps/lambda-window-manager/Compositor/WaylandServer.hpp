#pragma once

#include "Compositor/Chrome/ChromeConfig.hpp"
#include "Compositor/Wayland/WaylandTypes.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace lambda::compositor {

struct CompositorKeyboardConfig {
  std::string rules;
  std::string model;
  std::string layout;
  std::string variant;
  std::string options;
  int repeatRate = 25;
  int repeatDelayMs = 600;

  bool operator==(CompositorKeyboardConfig const&) const = default;
};

struct CompositorInputConfig {
  bool popupGrabs = true;
  CompositorKeyboardConfig keyboard;
};

class WaylandServer {
public:
  using ShortcutAction = lambda::compositor::ShortcutAction;
  using ShortcutBinding = lambda::compositor::ShortcutBinding;

  explicit WaylandServer(WaylandOutputInfo output);
  ~WaylandServer();

  WaylandServer(WaylandServer const&) = delete;
  WaylandServer& operator=(WaylandServer const&) = delete;

  [[nodiscard]] char const* socketName() const noexcept;
  [[nodiscard]] int eventFd() const noexcept;
  [[nodiscard]] int shellIpcFd() const noexcept;
  [[nodiscard]] float preferredScale() const noexcept;
  [[nodiscard]] std::int32_t logicalOutputWidth() const noexcept;
  [[nodiscard]] std::int32_t logicalOutputHeight() const noexcept;
  [[nodiscard]] std::size_t toplevelCount() const noexcept;
  [[nodiscard]] std::vector<CommittedSurfaceSnapshot> committedSurfaces() const;
  [[nodiscard]] std::optional<CommittedSurfaceSnapshot> cursorSurface() const;
  [[nodiscard]] std::optional<SnapPreviewSnapshot> snapPreview() const;
  [[nodiscard]] std::optional<int> snapPreviewWakeDelayMs() const;
  [[nodiscard]] bool hasPendingFrameCallbacks() const noexcept;
  [[nodiscard]] std::uint64_t contentSerial() const noexcept;
  [[nodiscard]] std::vector<int> duplicateDmabufFds(std::uint64_t surfaceId) const;
  [[nodiscard]] bool copyDmabufToRgba(std::uint64_t surfaceId, std::vector<std::uint8_t>& out) const;
  void consumeSurfaceDamage(std::uint64_t surfaceId, std::uint64_t serial);

  void dispatch();
  void dispatchShellIpc();
  [[nodiscard]] std::optional<ScreenshotRequest> consumeScreenshotRequest();
  [[nodiscard]] std::optional<ScreenshotSelectionOverlay> screenshotSelectionOverlay() const;
  void notifyShellStateChanged();
  void flushClients();
  void setShortcutBindings(std::vector<ShortcutBinding> bindings);
  void setChromeConfig(ChromeConfig config);
  void setChromeThemeConfig(ChromeConfig base, std::optional<ChromeConfig> dark);
  void setShellThemeDark(bool dark);
  void setInputConfig(CompositorInputConfig config);
  void setPreferredScale(float scale);
  void setDmabufFormatModifierPreferences(std::vector<DmabufFormatModifierPreference> preferences);
  void setRetainedDmabufBufferIds(std::vector<std::uint64_t> bufferIds);
  void updateAnimations(std::uint32_t timeMs, bool animationsEnabled);
  [[nodiscard]] bool hasActiveAnimations() const noexcept;
  [[nodiscard]] bool hasActiveResizePacing() const noexcept;
  [[nodiscard]] bool hasIdleInhibitors() const noexcept;
  void sendFrameCallbacksOnly(std::uint32_t timeMs);
  void sendFrameCallbacksOnly(std::uint32_t timeMs, std::span<std::uint64_t const> frameSurfaceIds);
  void sendPresentationFeedbacks(std::uint32_t timeMs, PresentationTiming timing);
  void sendPresentationFeedbacks(std::uint32_t timeMs,
                                 PresentationTiming timing,
                                 std::span<std::uint64_t const> frameSurfaceIds);
  void sendFrameCallbacks(std::uint32_t timeMs, PresentationTiming timing);
  void sendFrameCallbacks(std::uint32_t timeMs,
                          PresentationTiming timing,
                          std::span<std::uint64_t const> frameSurfaceIds);
  void completePresentationFeedbacks(std::vector<PresentationCompletion> const& completions, std::uint32_t timeMs);
  void handlePointerMotion(double dx, double dy, std::uint32_t timeMs);
  void handlePointerPosition(double x, double y, std::uint32_t timeMs);
  void handlePointerButton(std::uint32_t button, bool pressed, std::uint32_t timeMs);
  void handlePointerAxis(double dx, double dy, std::uint32_t timeMs);
  void handleKeyboardKey(std::uint32_t key, bool pressed, std::uint32_t timeMs);
  void resetKeyboardState(std::uint32_t timeMs);
  [[nodiscard]] float pointerX() const noexcept;
  [[nodiscard]] float pointerY() const noexcept;
  [[nodiscard]] CursorShape cursorShape() const noexcept;
  bool diagnosticExerciseTopToplevel(std::uint32_t step, bool resize);

  struct Impl;

private:
  std::unique_ptr<Impl> impl_;
};

} // namespace lambda::compositor
