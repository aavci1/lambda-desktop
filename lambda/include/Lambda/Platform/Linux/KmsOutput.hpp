#pragma once

/// \file Lambda/Platform/Linux/KmsOutput.hpp
///
/// Linux-only KMS output access for embedders that need to own a display without
/// creating a Lambda Window.

#if LAMBDAUI_VULKAN

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace lambdaui {

class Canvas;
class TextSystem;

namespace platform {

class KmsOutput;

struct KmsDmabufFormatModifier {
  std::uint32_t format = 0;
  std::uint64_t modifier = 0;
};

struct KmsPollResult {
  bool woke = false;
  bool inputOrSystem = false;
  std::uint64_t extraReadableMask = 0;
};

class KmsAtomicPresenter {
public:
  struct DmabufPlane {
    int fd = -1;
    std::uint32_t offset = 0;
    std::uint32_t stride = 0;
    std::uint64_t modifier = 0;
  };

  struct OverlayCandidate {
    std::uint64_t surfaceId = 0;
    std::uint64_t bufferId = 0;
    std::uint32_t drmFormat = 0;
    std::uint32_t bufferWidth = 0;
    std::uint32_t bufferHeight = 0;
    double sourceX = 0.0;
    double sourceY = 0.0;
    double sourceWidth = 0.0;
    double sourceHeight = 0.0;
    std::int32_t crtcX = 0;
    std::int32_t crtcY = 0;
    std::uint32_t crtcWidth = 0;
    std::uint32_t crtcHeight = 0;
    int acquireFenceFd = -1;
    std::vector<DmabufPlane> planes;
  };

  struct DamageRect {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
  };

  struct PageFlipTiming {
    bool hardware = false;
    std::uint32_t presentId = 0;
    std::uint64_t sequence = 0;
    std::uint64_t monotonicNsec = 0;
    std::uint64_t scheduledMonotonicNsec = 0;
    std::uint64_t commitStartMonotonicNsec = 0;
    std::uint64_t commitReturnMonotonicNsec = 0;
    std::uint64_t renderSubmittedMonotonicNsec = 0;
    std::uint64_t renderReadyMonotonicNsec = 0;
    std::uint64_t eventDispatchStartMonotonicNsec = 0;
    std::uint64_t eventDispatchEndMonotonicNsec = 0;
    std::uint64_t commitDurationNsec = 0;
    bool usedRenderFence = false;
    bool usedModeset = false;
  };

  ~KmsAtomicPresenter();

  KmsAtomicPresenter(KmsAtomicPresenter const&) = delete;
  KmsAtomicPresenter& operator=(KmsAtomicPresenter const&) = delete;
  KmsAtomicPresenter(KmsAtomicPresenter&&) noexcept;
  KmsAtomicPresenter& operator=(KmsAtomicPresenter&&) noexcept;

  Canvas& canvas();
  [[nodiscard]] bool canPrepareFrame();
  [[nodiscard]] bool prepareFrame(std::span<DamageRect const> damage = {});
  [[nodiscard]] std::uint32_t markFrameRendered();
  [[nodiscard]] bool updateRenderReady(std::uint32_t token = 0);
  [[nodiscard]] bool canSchedulePresent(std::uint32_t token = 0);
  [[nodiscard]] int renderReadyFd(std::uint32_t token = 0) const noexcept;
  void discardPreparedFrame(std::uint32_t token);
  [[nodiscard]] bool prepareOverlayCandidate(OverlayCandidate candidate);
  [[nodiscard]] bool prepareOverlayCandidate(std::uint32_t token, OverlayCandidate candidate);
  [[nodiscard]] bool canPrepareOverlayOnly() const noexcept;
  [[nodiscard]] bool prepareOverlayCandidateForDisplayedFrame(OverlayCandidate candidate);
  [[nodiscard]] bool canScheduleOverlayOnly() const noexcept;
  [[nodiscard]] int preparedOverlayAcquireFenceFd() const noexcept;
  std::uint32_t scheduleOverlayOnly();
  [[nodiscard]] bool primeDirectScanoutCandidate(OverlayCandidate& candidate);
  [[nodiscard]] bool prepareDirectScanoutCandidate(OverlayCandidate candidate);
  [[nodiscard]] bool canScheduleDirectScanout() const noexcept;
  [[nodiscard]] int preparedDirectScanoutAcquireFenceFd() const noexcept;
  std::uint32_t scheduleDirectScanout();
  [[nodiscard]] bool canScheduleDirectScanoutRepeat() const noexcept;
  std::uint32_t scheduleDirectScanoutRepeat();
  void clearPreparedDirectScanout();
  void clearPreparedOverlayCandidate();
  [[nodiscard]] std::uint64_t preparedOverlaySurfaceId() const noexcept;
  [[nodiscard]] std::vector<std::uint64_t> overlayBufferIdsInUse() const;
  [[nodiscard]] bool canUseOverlayFormatModifier(std::uint32_t format, std::uint64_t modifier) const noexcept;
  [[nodiscard]] std::vector<KmsDmabufFormatModifier> overlayDmabufFormatModifierPreferences() const;
  std::uint32_t schedulePresent(std::uint32_t token = 0);
  PageFlipTiming present();
  [[nodiscard]] std::optional<PageFlipTiming> dispatchPageFlipEvents();
  [[nodiscard]] bool hasPendingPageFlip() const noexcept;
  [[nodiscard]] int eventFd() const noexcept;
  void syncModeStateFromKernel() noexcept;

private:
  class Impl;

  explicit KmsAtomicPresenter(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> impl_;

  friend class KmsOutput;
};

struct KmsInputEvent {
  enum class Kind {
    PointerMotion,
    PointerPosition,
    PointerButton,
    PointerAxis,
    Key,
    KeyboardReset,
  };

  Kind kind = Kind::PointerMotion;
  double dx = 0.0;
  double dy = 0.0;
  double x = 0.0;
  double y = 0.0;
  std::uint32_t button = 0;
  bool pressed = false;
  std::uint32_t key = 0;
  std::uint32_t timeMs = 0;
};

class KmsDevice {
public:
  class Impl;

  static std::unique_ptr<KmsDevice> open(char const* devicePath = nullptr);

  ~KmsDevice();

  KmsDevice(KmsDevice const&) = delete;
  KmsDevice& operator=(KmsDevice const&) = delete;
  KmsDevice(KmsDevice&&) noexcept;
  KmsDevice& operator=(KmsDevice&&) noexcept;

  [[nodiscard]] std::vector<KmsOutput> outputs() const;
  [[nodiscard]] int fd() const noexcept;
  [[nodiscard]] std::span<char const* const> requiredVulkanInstanceExtensions() const;
  [[nodiscard]] std::filesystem::path cacheDir() const;
  [[nodiscard]] bool isVtForeground() const noexcept;
  [[nodiscard]] bool shouldTerminate() const noexcept;

  void setInputHandler(std::function<void(KmsInputEvent const&)> handler);
  void emitInputEventForDiagnostics(KmsInputEvent const& event);
  void acknowledgeVtAcquire();
  bool switchSession(int session);
  bool switchAdjacentSession(int direction);

  /// Services signal, VT-switch, input, wake, and hotplug events owned by the KMS device.
  bool pollEvents(int timeoutMs = 0, std::span<int const> extraFds = {});
  KmsPollResult pollEventDetails(int timeoutMs = 0, std::span<int const> extraFds = {});

private:
  explicit KmsDevice(std::shared_ptr<Impl> impl);

  std::shared_ptr<Impl> impl_;
};

class KmsOutput {
public:
  struct VblankTiming {
    bool hardware = false;
    std::uint64_t sequence = 0;
    std::uint64_t monotonicNsec = 0;
  };

  KmsOutput();
  ~KmsOutput();

  KmsOutput(KmsOutput const&);
  KmsOutput& operator=(KmsOutput const&);
  KmsOutput(KmsOutput&&) noexcept;
  KmsOutput& operator=(KmsOutput&&) noexcept;

  [[nodiscard]] std::string const& name() const noexcept;
  [[nodiscard]] std::uint32_t width() const noexcept;
  [[nodiscard]] std::uint32_t height() const noexcept;
  [[nodiscard]] std::uint32_t refreshRateMilliHz() const noexcept;
  [[nodiscard]] std::uint32_t cursorWidth() const noexcept;
  [[nodiscard]] std::uint32_t cursorHeight() const noexcept;

  [[nodiscard]] VkSurfaceKHR createVulkanSurface(VkInstance instance) const;

  /// Lightweight vblank pacing approximation used by phase-1 compositor code.
  /// The KMS Window path still uses its existing frame scheduling.
  VblankTiming waitForVblank() const;
  [[nodiscard]] bool setCursorImage(std::span<std::uint32_t const> premultipliedArgbPixels,
                                    std::uint32_t width,
                                    std::uint32_t height,
                                    std::int32_t hotspotX = 0,
                                    std::int32_t hotspotY = 0) const;
  [[nodiscard]] bool moveCursor(std::int32_t x, std::int32_t y) const;
  [[nodiscard]] bool hasDeferredCursorCommit() const noexcept;
  [[nodiscard]] bool retryDeferredCursorCommit() const noexcept;
  void hideCursor() const;

  [[nodiscard]] std::unique_ptr<KmsAtomicPresenter> createAtomicPresenter(TextSystem& textSystem) const;

 private:
  class Impl;

  explicit KmsOutput(std::shared_ptr<Impl> impl);

  std::shared_ptr<Impl> impl_;

  friend class KmsDevice::Impl;
  friend class KmsAtomicPresenter::Impl;
};

} // namespace platform
} // namespace lambdaui

#endif // LAMBDAUI_VULKAN
