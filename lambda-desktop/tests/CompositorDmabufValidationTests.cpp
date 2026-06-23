#include "Compositor/Wayland/DmabufValidation.hpp"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <limits>

TEST_CASE("dmabuf layout validation accepts supported single-plane rgb buffers") {
  using namespace lambdaui::compositor;

  std::array planes{
      DmabufPlaneLayout{
          .index = 0,
          .offset = 128,
          .stride = 512,
      },
  };

  DmabufLayoutValidationResult const result =
      validateSinglePlaneDmabufLayout(100, 20, DRM_FORMAT_ARGB8888, planes, 128u + 512u * 20u);

  CHECK(result.valid());
  CHECK(result.minStride == 400);
  CHECK(result.requiredBytes == 10368);
}

TEST_CASE("dmabuf layout validation rejects incomplete and unsupported plane sets") {
  using namespace lambdaui::compositor;

  std::array missingPlane0{
      DmabufPlaneLayout{
          .index = 1,
          .offset = 0,
          .stride = 64,
      },
  };
  CHECK(validateSinglePlaneDmabufLayout(8, 8, DRM_FORMAT_XRGB8888, missingPlane0).error ==
        DmabufLayoutValidationError::MissingPlane0);

  std::array multiPlane{
      DmabufPlaneLayout{.index = 0, .offset = 0, .stride = 64},
      DmabufPlaneLayout{.index = 1, .offset = 0, .stride = 64},
  };
  CHECK(validateSinglePlaneDmabufLayout(8, 8, DRM_FORMAT_XRGB8888, multiPlane).error ==
        DmabufLayoutValidationError::UnsupportedPlaneCount);
}

TEST_CASE("dmabuf layout validation rejects invalid dimensions formats and strides") {
  using namespace lambdaui::compositor;

  std::array planes{
      DmabufPlaneLayout{.index = 0, .offset = 0, .stride = 32},
  };

  CHECK(validateSinglePlaneDmabufLayout(0, 8, DRM_FORMAT_XRGB8888, planes).error ==
        DmabufLayoutValidationError::InvalidDimensions);
  CHECK(validateSinglePlaneDmabufLayout(8, 8, DRM_FORMAT_NV12, planes).error ==
        DmabufLayoutValidationError::UnsupportedFormat);
  CHECK(validateSinglePlaneDmabufLayout(9, 8, DRM_FORMAT_XRGB8888, planes).error ==
        DmabufLayoutValidationError::StrideTooSmall);
}

TEST_CASE("dmabuf layout validation checks required byte span against fd size") {
  using namespace lambdaui::compositor;

  std::array planes{
      DmabufPlaneLayout{
          .index = 0,
          .offset = 64,
          .stride = 256,
      },
  };

  DmabufLayoutValidationResult const valid =
      validateSinglePlaneDmabufLayout(64, 16, DRM_FORMAT_XRGB8888, planes, 4160);
  CHECK(valid.valid());
  CHECK(valid.requiredBytes == 4160);

  CHECK(validateSinglePlaneDmabufLayout(64, 16, DRM_FORMAT_XRGB8888, planes, 4159).error ==
        DmabufLayoutValidationError::OutOfBounds);

  std::array impossibleStride{
      DmabufPlaneLayout{.index = 0, .offset = 0, .stride = std::numeric_limits<std::uint32_t>::max()},
  };
  CHECK(validateSinglePlaneDmabufLayout(1'073'741'824, 1, DRM_FORMAT_XRGB8888, impossibleStride).error ==
        DmabufLayoutValidationError::StrideTooSmall);
}

TEST_CASE("dmabuf validation rejects flags the renderer does not implement") {
  using namespace lambdaui::compositor;

  CHECK(areDmabufBufferFlagsSupported(0));
  CHECK_FALSE(areDmabufBufferFlagsSupported(1));
  CHECK_FALSE(areDmabufBufferFlagsSupported(2));
  CHECK_FALSE(areDmabufBufferFlagsSupported(4));
}
