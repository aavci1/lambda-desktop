#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include <drm_fourcc.h>

namespace lambda::compositor {

inline constexpr std::array<std::uint32_t, 4> kSupportedSinglePlaneDmabufFormats{
    DRM_FORMAT_ARGB8888,
    DRM_FORMAT_XRGB8888,
    DRM_FORMAT_ABGR8888,
    DRM_FORMAT_XBGR8888,
};

struct DmabufPlaneLayout {
  std::uint32_t index = 0;
  std::uint32_t offset = 0;
  std::uint32_t stride = 0;
  std::uint64_t modifier = DRM_FORMAT_MOD_INVALID;
};

enum class DmabufLayoutValidationError : std::uint8_t {
  None,
  InvalidDimensions,
  UnsupportedFormat,
  MissingPlane0,
  UnsupportedPlaneCount,
  StrideTooSmall,
  LayoutOverflow,
  OutOfBounds,
};

struct DmabufLayoutValidationResult {
  DmabufLayoutValidationError error = DmabufLayoutValidationError::None;
  std::uint32_t minStride = 0;
  std::uint64_t requiredBytes = 0;

  [[nodiscard]] bool valid() const noexcept {
    return error == DmabufLayoutValidationError::None;
  }
};

[[nodiscard]] bool isSupportedSinglePlaneDmabufFormat(std::uint32_t format);
[[nodiscard]] std::optional<std::uint32_t> bytesPerPixelForDmabufFormat(std::uint32_t format);

[[nodiscard]] DmabufLayoutValidationResult validateSinglePlaneDmabufLayout(
    std::int32_t width,
    std::int32_t height,
    std::uint32_t format,
    std::span<DmabufPlaneLayout const> planes,
    std::optional<std::uint64_t> planeByteSize = std::nullopt);

} // namespace lambda::compositor
