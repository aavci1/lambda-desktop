#pragma once

/// \file Flux/Platform/Linux/KmsOutput.hpp
///
/// Linux-only KMS output access for embedders that need to own a display without
/// creating a Flux Window.

#if FLUX_VULKAN

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

namespace flux {

class Canvas;
class TextSystem;

namespace platform {

class KmsOutput;

class KmsAtomicPresenter {
public:
  struct PageFlipTiming {
    bool hardware = false;
    std::uint32_t presentId = 0;
    std::uint64_t sequence = 0;
    std::uint64_t monotonicNsec = 0;
  };

  ~KmsAtomicPresenter();

  KmsAtomicPresenter(KmsAtomicPresenter const&) = delete;
  KmsAtomicPresenter& operator=(KmsAtomicPresenter const&) = delete;
  KmsAtomicPresenter(KmsAtomicPresenter&&) noexcept;
  KmsAtomicPresenter& operator=(KmsAtomicPresenter&&) noexcept;

  Canvas& canvas();
  void prepareFrame();
  std::uint32_t schedulePresent();
  PageFlipTiming present();
  [[nodiscard]] std::optional<PageFlipTiming> dispatchPageFlipEvents();
  [[nodiscard]] bool hasPendingPageFlip() const noexcept;
  [[nodiscard]] int eventFd() const noexcept;

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
  void acknowledgeVtAcquire();

  /// Services signal, VT-switch, input, wake, and hotplug events owned by the KMS device.
  bool pollEvents(int timeoutMs = 0, std::span<int const> extraFds = {});

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
  void hideCursor() const;

  [[nodiscard]] std::unique_ptr<KmsAtomicPresenter> createAtomicPresenter(TextSystem& textSystem) const;

private:
  class Impl;

  explicit KmsOutput(std::shared_ptr<Impl> impl);

  std::shared_ptr<Impl> impl_;

  friend class KmsDevice::Impl;
};

} // namespace platform
} // namespace flux

#endif // FLUX_VULKAN
