#include "Compositor/Wayland/DmabufValidation.hpp"

#include <algorithm>
#include <limits>

namespace lambdaui::compositor {
namespace {

std::optional<DmabufPlaneLayout> findPlane(std::span<DmabufPlaneLayout const> planes,
                                           std::uint32_t index) {
  auto const found = std::find_if(planes.begin(), planes.end(), [index](DmabufPlaneLayout const& plane) {
    return plane.index == index;
  });
  if (found == planes.end()) return std::nullopt;
  return *found;
}

bool addWouldOverflow(std::uint64_t left, std::uint64_t right) {
  return left > std::numeric_limits<std::uint64_t>::max() - right;
}

bool multiplyWouldOverflow(std::uint64_t left, std::uint64_t right) {
  return left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left;
}

} // namespace

bool isSupportedSinglePlaneDmabufFormat(std::uint32_t format) {
  return std::find(kSupportedSinglePlaneDmabufFormats.begin(),
                   kSupportedSinglePlaneDmabufFormats.end(),
                   format) != kSupportedSinglePlaneDmabufFormats.end();
}

std::optional<std::uint32_t> bytesPerPixelForDmabufFormat(std::uint32_t format) {
  switch (format) {
  case DRM_FORMAT_ARGB8888:
  case DRM_FORMAT_XRGB8888:
  case DRM_FORMAT_ABGR8888:
  case DRM_FORMAT_XBGR8888:
    return 4u;
  default:
    return std::nullopt;
  }
}

bool areDmabufBufferFlagsSupported(std::uint32_t flags) {
  return flags == 0;
}

DmabufLayoutValidationResult validateSinglePlaneDmabufLayout(
    std::int32_t width,
    std::int32_t height,
    std::uint32_t format,
    std::span<DmabufPlaneLayout const> planes,
    std::optional<std::uint64_t> planeByteSize) {
  DmabufLayoutValidationResult result;
  if (width <= 0 || height <= 0) {
    result.error = DmabufLayoutValidationError::InvalidDimensions;
    return result;
  }

  auto const bpp = bytesPerPixelForDmabufFormat(format);
  if (!bpp) {
    result.error = DmabufLayoutValidationError::UnsupportedFormat;
    return result;
  }

  auto const plane0 = findPlane(planes, 0);
  if (!plane0) {
    result.error = DmabufLayoutValidationError::MissingPlane0;
    return result;
  }
  if (planes.size() != 1) {
    result.error = DmabufLayoutValidationError::UnsupportedPlaneCount;
    return result;
  }

  std::uint64_t const rowBytes = static_cast<std::uint64_t>(width) * *bpp;
  if (rowBytes > std::numeric_limits<std::uint32_t>::max()) {
    result.error = DmabufLayoutValidationError::StrideTooSmall;
    return result;
  }
  result.minStride = static_cast<std::uint32_t>(rowBytes);
  if (plane0->stride < rowBytes) {
    result.error = DmabufLayoutValidationError::StrideTooSmall;
    return result;
  }

  std::uint64_t const height64 = static_cast<std::uint64_t>(height);
  if (multiplyWouldOverflow(static_cast<std::uint64_t>(plane0->stride), height64)) {
    result.error = DmabufLayoutValidationError::LayoutOverflow;
    return result;
  }
  std::uint64_t const planeSpan = static_cast<std::uint64_t>(plane0->stride) * height64;
  if (addWouldOverflow(static_cast<std::uint64_t>(plane0->offset), planeSpan)) {
    result.error = DmabufLayoutValidationError::LayoutOverflow;
    return result;
  }
  result.requiredBytes = static_cast<std::uint64_t>(plane0->offset) + planeSpan;
  if (planeByteSize && result.requiredBytes > *planeByteSize) {
    result.error = DmabufLayoutValidationError::OutOfBounds;
    return result;
  }

  return result;
}

} // namespace lambdaui::compositor
