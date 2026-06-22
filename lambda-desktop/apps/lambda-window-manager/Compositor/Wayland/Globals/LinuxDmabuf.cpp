#include "Compositor/Wayland/Globals/LinuxDmabuf.hpp"

#include "Compositor/Diagnostics/CrashLog.hpp"
#include "Compositor/Wayland/DmabufFeedback.hpp"
#include "Compositor/Wayland/DmabufValidation.hpp"
#include "Compositor/Wayland/ResourceTemplates.hpp"
#include "Compositor/Wayland/WaylandServerImpl.hpp"
#include <Lambda/Graphics/VulkanContext.hpp>
#include "Graphics/Vulkan/VulkanCheck.hpp"
#include "linux-dmabuf-unstable-v1-server-protocol.h"

#include <drm_fourcc.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wayland-server-protocol.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>

namespace lambda::compositor {
namespace {

void bufferDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

struct wl_buffer_interface const bufferImpl{bufferDestroy};

struct DmabufFormatModifier {
  std::uint32_t format = 0;
  std::uint32_t padding = 0;
  std::uint64_t modifier = 0;
};

std::optional<DmabufPlane> findPlane(WaylandServer::Impl::DmabufParams const* params, std::uint32_t index) {
  auto found = std::find_if(params->planes.begin(), params->planes.end(),
                            [index](DmabufPlane const& plane) { return plane.index == index; });
  if (found == params->planes.end()) return std::nullopt;
  return *found;
}

std::vector<DmabufPlaneLayout> planeLayoutsFor(WaylandServer::Impl::DmabufParams const* params) {
  std::vector<DmabufPlaneLayout> layouts;
  layouts.reserve(params->planes.size());
  for (DmabufPlane const& plane : params->planes) {
    layouts.push_back({
        .index = plane.index,
        .offset = plane.offset,
        .stride = plane.stride,
        .modifier = plane.modifier,
    });
  }
  return layouts;
}

std::optional<std::uint64_t> planeByteSize(int fd) {
  if (fd < 0) return 0u;
  off_t const current = lseek(fd, 0, SEEK_CUR);
  off_t const size = lseek(fd, 0, SEEK_END);
  if (current >= 0) {
    (void)lseek(fd, current, SEEK_SET);
  }
  if (size < 0) return std::nullopt;
  return static_cast<std::uint64_t>(size);
}

VkFormat vkFormatForDmabufFormat(std::uint32_t drmFormat) {
  switch (drmFormat) {
  case DRM_FORMAT_ARGB8888:
  case DRM_FORMAT_XRGB8888:
    return VK_FORMAT_B8G8R8A8_UNORM;
  case DRM_FORMAT_ABGR8888:
  case DRM_FORMAT_XBGR8888:
    return VK_FORMAT_R8G8B8A8_UNORM;
  default:
    return VK_FORMAT_UNDEFINED;
  }
}

bool containsModifier(std::vector<std::uint64_t> const& modifiers, std::uint64_t modifier) {
  return std::find(modifiers.begin(), modifiers.end(), modifier) != modifiers.end();
}

void appendUniqueModifier(std::vector<std::uint64_t>& modifiers, std::uint64_t modifier) {
  if (!containsModifier(modifiers, modifier)) modifiers.push_back(modifier);
}

std::vector<std::uint64_t> sampledDmabufModifiersForFormat(std::uint32_t drmFormat) {
  std::vector<std::uint64_t> modifiers{DRM_FORMAT_MOD_INVALID, DRM_FORMAT_MOD_LINEAR};
  VkPhysicalDevice physical = lambda::VulkanContext::instance().physicalDevice();
  VkFormat const vkFormat = vkFormatForDmabufFormat(drmFormat);
  if (!physical || vkFormat == VK_FORMAT_UNDEFINED) return modifiers;

  auto modifierList =
      vkStructure<VkDrmFormatModifierPropertiesList2EXT>(VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_2_EXT);
  auto formatProperties = vkStructure<VkFormatProperties2>(VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2);
  formatProperties.pNext = &modifierList;
  vkGetPhysicalDeviceFormatProperties2(physical, vkFormat, &formatProperties);
  if (modifierList.drmFormatModifierCount == 0) return modifiers;

  std::vector<VkDrmFormatModifierProperties2EXT> properties(modifierList.drmFormatModifierCount);
  modifierList.pDrmFormatModifierProperties = properties.data();
  vkGetPhysicalDeviceFormatProperties2(physical, vkFormat, &formatProperties);

  for (VkDrmFormatModifierProperties2EXT const& property : properties) {
    if (property.drmFormatModifierPlaneCount != 1) continue;
    if ((property.drmFormatModifierTilingFeatures & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT) == 0) continue;
    appendUniqueModifier(modifiers, property.drmFormatModifier);
  }
  return modifiers;
}

bool isSupportedDmabufModifier(std::uint32_t format, std::uint64_t modifier) {
  return containsModifier(sampledDmabufModifiersForFormat(format), modifier);
}

bool validateDmabufParams(WaylandServer::Impl::DmabufParams* params,
                          std::int32_t width,
                          std::int32_t height,
                          std::uint32_t format,
                          std::uint32_t flags) {
  if (params->used) {
    wl_resource_post_error(params->resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED,
                           "zwp_linux_buffer_params_v1 was already used");
    return false;
  }
  if (!areDmabufBufferFlagsSupported(flags)) {
    wl_resource_post_error(params->resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT,
                           "unsupported dmabuf flags 0x%08x", flags);
    return false;
  }

  std::optional<std::uint64_t> byteSize;
  if (params->planes.size() == 1 && params->planes.front().index == 0) {
    byteSize = planeByteSize(params->planes.front().fd);
  }
  DmabufLayoutValidationResult const layout =
      validateSinglePlaneDmabufLayout(width, height, format, planeLayoutsFor(params), byteSize);
  switch (layout.error) {
  case DmabufLayoutValidationError::None:
    break;
  case DmabufLayoutValidationError::InvalidDimensions:
    wl_resource_post_error(params->resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_DIMENSIONS,
                           "dmabuf dimensions must be positive");
    return false;
  case DmabufLayoutValidationError::UnsupportedFormat:
    wl_resource_post_error(params->resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT,
                           "unsupported dmabuf format 0x%08x", format);
    return false;
  case DmabufLayoutValidationError::MissingPlane0:
    wl_resource_post_error(params->resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE,
                           "dmabuf plane 0 is required");
    return false;
  case DmabufLayoutValidationError::UnsupportedPlaneCount:
    wl_resource_post_error(params->resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE,
                           "only single-plane RGB dmabufs are currently supported");
    return false;
  case DmabufLayoutValidationError::StrideTooSmall:
    wl_resource_post_error(params->resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS,
                           "dmabuf plane stride is too small");
    return false;
  case DmabufLayoutValidationError::LayoutOverflow:
    wl_resource_post_error(params->resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS,
                           "dmabuf plane layout overflows addressable byte range");
    return false;
  case DmabufLayoutValidationError::OutOfBounds:
    wl_resource_post_error(params->resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS,
                           "dmabuf plane requires %llu bytes but fd has %llu bytes",
                           static_cast<unsigned long long>(layout.requiredBytes),
                           static_cast<unsigned long long>(*byteSize));
    return false;
  }

  DmabufPlane const& plane = params->planes.front();
  if (!isSupportedDmabufModifier(format, plane.modifier)) {
    wl_resource_post_error(params->resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT,
                           "unsupported dmabuf modifier 0x%016llx",
                           static_cast<unsigned long long>(plane.modifier));
    return false;
  }
  return true;
}

wl_resource* createDmabufBuffer(wl_client* client, WaylandServer::Impl::DmabufParams* params, std::uint32_t id,
                                std::int32_t width, std::int32_t height, std::uint32_t format,
                                std::uint32_t flags) {
  auto buffer = std::make_unique<WaylandServer::Impl::DmabufBuffer>();
  buffer->server = params->server;
  buffer->id = params->server->nextDmabufBufferId_++;
  buffer->width = width;
  buffer->height = height;
  buffer->format = format;
  buffer->flags = flags;
  buffer->planes = std::move(params->planes);
  wl_resource* bufferResource = wl_resource_create(client, &wl_buffer_interface, 1, id);
  if (!bufferResource) {
    for (auto& plane : buffer->planes) {
      if (plane.fd >= 0) close(plane.fd);
      plane.fd = -1;
    }
    wl_client_post_no_memory(client);
    return nullptr;
  }
  buffer->resource = bufferResource;
  auto* raw = buffer.get();
  params->server->dmabufBuffers_.push_back(std::move(buffer));
  wl_resource_set_implementation(bufferResource,
                                 &bufferImpl,
                                 raw,
                                 destroyResourceCallback<WaylandServer::Impl::DmabufBuffer,
                                                         WaylandServer::Impl,
                                                         &WaylandServer::Impl::destroyDmabufBuffer>);
  static bool loggedNonLinear = false;
  if (!loggedNonLinear && !raw->planes.empty() && raw->planes.front().modifier != DRM_FORMAT_MOD_LINEAR &&
      raw->planes.front().modifier != DRM_FORMAT_MOD_INVALID) {
    loggedNonLinear = true;
    std::fprintf(stderr,
                 "lambda-window-manager: first non-linear dmabuf size=%dx%d format=0x%08x stride=%u modifier=0x%016llx\n",
                 width,
                 height,
                 format,
                 raw->planes.front().stride,
                 static_cast<unsigned long long>(raw->planes.front().modifier));
  }
  diagnostics::crashLog("dmabuf-create id=%llu size=%dx%d format=0x%08x flags=0x%08x stride=%u modifier=0x%016llx",
                        static_cast<unsigned long long>(raw->id),
                        width,
                        height,
                        format,
                        flags,
                        raw->planes.empty() ? 0u : raw->planes.front().stride,
                        static_cast<unsigned long long>(raw->planes.empty() ? 0ull : raw->planes.front().modifier));
  return bufferResource;
}

void linuxBufferParamsDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void linuxBufferParamsAdd(wl_client*, wl_resource* resource, int fd, std::uint32_t planeIndex,
                          std::uint32_t offset, std::uint32_t stride, std::uint32_t modifierHi,
                          std::uint32_t modifierLo) {
  auto* params = resourceData<WaylandServer::Impl::DmabufParams>(resource);
  if (params->used) {
    close(fd);
    wl_resource_post_error(resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED,
                           "zwp_linux_buffer_params_v1 was already used");
    return;
  }
  if (planeIndex >= 4) {
    close(fd);
    wl_resource_post_error(resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_IDX,
                           "dmabuf plane index %u is out of bounds", planeIndex);
    return;
  }
  if (findPlane(params, planeIndex).has_value()) {
    close(fd);
    wl_resource_post_error(resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_SET,
                           "dmabuf plane index %u was already set", planeIndex);
    return;
  }
  if (stride == 0) {
    close(fd);
    wl_resource_post_error(resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS,
                           "dmabuf plane stride must be positive");
    return;
  }

  DmabufPlane plane;
  plane.fd = fd;
  plane.index = planeIndex;
  plane.offset = offset;
  plane.stride = stride;
  plane.modifier = (static_cast<std::uint64_t>(modifierHi) << 32u) | modifierLo;
  params->planes.push_back(plane);
}

void linuxBufferParamsCreate(wl_client* client, wl_resource* resource, std::int32_t width,
                             std::int32_t height, std::uint32_t format, std::uint32_t flags) {
  auto* params = resourceData<WaylandServer::Impl::DmabufParams>(resource);
  if (!validateDmabufParams(params, width, height, format, flags)) return;
  params->used = true;
  wl_resource* buffer = createDmabufBuffer(client, params, 0, width, height, format, flags);
  if (buffer) zwp_linux_buffer_params_v1_send_created(resource, buffer);
}

void linuxBufferParamsCreateImmed(wl_client* client, wl_resource* resource, std::uint32_t bufferId,
                                  std::int32_t width, std::int32_t height, std::uint32_t format,
                                  std::uint32_t flags) {
  auto* params = resourceData<WaylandServer::Impl::DmabufParams>(resource);
  if (!validateDmabufParams(params, width, height, format, flags)) return;
  params->used = true;
  createDmabufBuffer(client, params, bufferId, width, height, format, flags);
}

struct zwp_linux_buffer_params_v1_interface const linuxBufferParamsImpl{
    .destroy = linuxBufferParamsDestroy,
    .add = linuxBufferParamsAdd,
    .create = linuxBufferParamsCreate,
    .create_immed = linuxBufferParamsCreateImmed,
};

void linuxDmabufDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

void linuxDmabufCreateParams(wl_client* client, wl_resource* resource, std::uint32_t id) {
  auto* server = serverFrom(resource);
  auto params = std::make_unique<WaylandServer::Impl::DmabufParams>();
  params->server = server;
  auto const version =
      linuxDmabufResourceVersion(static_cast<std::uint32_t>(wl_resource_get_version(resource)));
  wl_resource* paramsResource = wl_resource_create(client, &zwp_linux_buffer_params_v1_interface,
                                                   version, id);
  if (!paramsResource) {
    wl_client_post_no_memory(client);
    return;
  }
  params->resource = paramsResource;
  auto* raw = params.get();
  server->dmabufParams_.push_back(std::move(params));
  wl_resource_set_implementation(paramsResource,
                                 &linuxBufferParamsImpl,
                                 raw,
                                 destroyResourceCallback<WaylandServer::Impl::DmabufParams,
                                                         WaylandServer::Impl,
                                                         &WaylandServer::Impl::destroyDmabufParams>);
}

void linuxDmabufFeedbackDestroy(wl_client*, wl_resource* resource) {
  wl_resource_destroy(resource);
}

struct zwp_linux_dmabuf_feedback_v1_interface const linuxDmabufFeedbackImpl{
    .destroy = linuxDmabufFeedbackDestroy,
};

std::vector<DmabufFeedbackPair> rendererDmabufFeedbackPairs() {
  std::vector<DmabufFeedbackPair> pairs;
  for (std::uint32_t format : kSupportedSinglePlaneDmabufFormats) {
    for (std::uint64_t modifier : sampledDmabufModifiersForFormat(format)) {
      pairs.push_back({
          .format = format,
          .modifier = modifier,
      });
    }
  }
  return pairs;
}

std::vector<DmabufFeedbackPair> scanoutPreferredDmabufFeedbackPairs(WaylandServer::Impl const* server) {
  std::vector<DmabufFeedbackPair> pairs;
  if (!server) return pairs;
  for (DmabufFormatModifierPreference const& preference : server->dmabufFormatModifierPreferences_) {
    pairs.push_back({
        .format = preference.format,
        .modifier = preference.modifier,
    });
  }
  return pairs;
}

DmabufFeedbackPlan feedbackPlanForServer(WaylandServer::Impl const* server) {
  std::vector<DmabufFeedbackPair> rendererPairs = rendererDmabufFeedbackPairs();
  std::vector<DmabufFeedbackPair> scanoutPairs = scanoutPreferredDmabufFeedbackPairs(server);
  return buildDmabufFeedbackPlan(rendererPairs, scanoutPairs);
}

std::vector<DmabufFormatModifier> formatTableForPlan(DmabufFeedbackPlan const& plan) {
  std::vector<DmabufFormatModifier> table;
  table.reserve(plan.table.size());
  for (DmabufFeedbackPair const& pair : plan.table) {
    table.push_back({
        .format = pair.format,
        .padding = 0,
        .modifier = pair.modifier,
    });
  }
  return table;
}

int createDmabufFormatTableFd(std::vector<DmabufFormatModifier> const& table) {
  int fd = memfd_create("lambda-dmabuf-formats", MFD_CLOEXEC);
  if (fd < 0) return -1;

  std::size_t const byteSize = table.size() * sizeof(DmabufFormatModifier);
  auto const* data = reinterpret_cast<char const*>(table.data());
  std::size_t written = 0;
  while (written < byteSize) {
    ssize_t const rc = write(fd, data + written, byteSize - written);
    if (rc < 0) {
      if (errno == EINTR) continue;
      close(fd);
      return -1;
    }
    if (rc == 0) {
      close(fd);
      return -1;
    }
    written += static_cast<std::size_t>(rc);
  }
  return fd;
}

void appendArrayBytes(wl_array& array, void const* data, std::size_t size) {
  void* target = wl_array_add(&array, size);
  if (!target) return;
  std::memcpy(target, data, size);
}

void sendDeviceArray(wl_resource* resource, bool trancheTarget, std::uint64_t rawDevice) {
  wl_array deviceArray;
  wl_array_init(&deviceArray);
  dev_t const device = static_cast<dev_t>(rawDevice);
  appendArrayBytes(deviceArray, &device, sizeof(device));
  if (deviceArray.size == sizeof(device)) {
    if (trancheTarget) {
      zwp_linux_dmabuf_feedback_v1_send_tranche_target_device(resource, &deviceArray);
    } else {
      zwp_linux_dmabuf_feedback_v1_send_main_device(resource, &deviceArray);
    }
  }
  wl_array_release(&deviceArray);
}

void sendTrancheFormatIndices(wl_resource* resource, std::vector<std::uint16_t> const& tableIndices) {
  wl_array indices;
  wl_array_init(&indices);
  for (std::uint16_t const index : tableIndices) {
    appendArrayBytes(indices, &index, sizeof(index));
  }
  if (indices.size == tableIndices.size() * sizeof(std::uint16_t)) {
    zwp_linux_dmabuf_feedback_v1_send_tranche_formats(resource, &indices);
  }
  wl_array_release(&indices);
}

void sendDmabufFeedback(wl_resource* resource, WaylandServer::Impl* server) {
  DmabufFeedbackPlan const plan = feedbackPlanForServer(server);
  auto const table = formatTableForPlan(plan);
  static bool logged = false;
  if (!logged) {
    logged = true;
    std::fprintf(stderr,
                 "lambda-window-manager: linux-dmabuf feedback advertises %zu format/modifier pairs in %zu tranches (%zu scanout preferred)\n",
                 table.size(),
                 plan.tranches.size(),
                 server ? server->dmabufFormatModifierPreferences_.size() : 0u);
  }
  int tableFd = createDmabufFormatTableFd(table);
  if (tableFd < 0) {
    wl_resource_post_no_memory(resource);
    return;
  }

  zwp_linux_dmabuf_feedback_v1_send_format_table(
      resource, tableFd, static_cast<std::uint32_t>(table.size() * sizeof(DmabufFormatModifier)));
  close(tableFd);

  std::uint64_t const device = server && server->output_.drmDevice != 0 ? server->output_.drmDevice : 1;
  sendDeviceArray(resource, false, device);

  for (DmabufFeedbackTranchePlan const& tranche : plan.tranches) {
    sendDeviceArray(resource, true, device);
    zwp_linux_dmabuf_feedback_v1_send_tranche_flags(resource, tranche.flags);
    sendTrancheFormatIndices(resource, tranche.indices);
    zwp_linux_dmabuf_feedback_v1_send_tranche_done(resource);
  }
  zwp_linux_dmabuf_feedback_v1_send_done(resource);
}

wl_resource* createDmabufFeedbackResource(wl_client* client, wl_resource* dmabufResource, std::uint32_t id) {
  auto const version =
      linuxDmabufResourceVersion(static_cast<std::uint32_t>(wl_resource_get_version(dmabufResource)));
  wl_resource* resource = wl_resource_create(client,
                                             &zwp_linux_dmabuf_feedback_v1_interface,
                                             version,
                                             id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return nullptr;
  }
  wl_resource_set_implementation(resource, &linuxDmabufFeedbackImpl, nullptr, nullptr);
  return resource;
}

void linuxDmabufGetDefaultFeedback(wl_client* client, wl_resource* resource, std::uint32_t id) {
  wl_resource* feedback = createDmabufFeedbackResource(client, resource, id);
  if (!feedback) return;
  sendDmabufFeedback(feedback, serverFrom(resource));
}

void linuxDmabufGetSurfaceFeedback(wl_client* client, wl_resource* resource, std::uint32_t id,
                                   wl_resource* surfaceResource) {
  (void)surfaceResource;
  wl_resource* feedback = createDmabufFeedbackResource(client, resource, id);
  if (!feedback) return;
  sendDmabufFeedback(feedback, serverFrom(resource));
}

struct zwp_linux_dmabuf_v1_interface const linuxDmabufImpl{
    .destroy = linuxDmabufDestroy,
    .create_params = linuxDmabufCreateParams,
    .get_default_feedback = linuxDmabufGetDefaultFeedback,
    .get_surface_feedback = linuxDmabufGetSurfaceFeedback,
};

void sendDmabufFormat(wl_resource* resource, WaylandServer::Impl const* server, std::uint32_t format) {
  (void)server;
  if (wl_resource_get_version(resource) >= ZWP_LINUX_DMABUF_V1_MODIFIER_SINCE_VERSION) {
    for (std::uint64_t modifier : sampledDmabufModifiersForFormat(format)) {
      zwp_linux_dmabuf_v1_send_modifier(resource,
                                        format,
                                        static_cast<std::uint32_t>(modifier >> 32u),
                                        static_cast<std::uint32_t>(modifier & 0xffffffffu));
    }
    return;
  }
  zwp_linux_dmabuf_v1_send_format(resource, format);
}

} // namespace

bool isSupportedDmabufFormat(std::uint32_t format) {
  return isSupportedSinglePlaneDmabufFormat(format);
}

void bindLinuxDmabuf(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
  std::uint32_t const resourceVersion = linuxDmabufResourceVersion(version);
  wl_resource* resource = wl_resource_create(client, &zwp_linux_dmabuf_v1_interface, resourceVersion, id);
  if (!resource) {
    wl_client_post_no_memory(client);
    return;
  }
  wl_resource_set_implementation(resource, &linuxDmabufImpl, data, nullptr);
  if (resourceVersion < 4u) {
    auto* server = static_cast<WaylandServer::Impl const*>(data);
    for (std::uint32_t format : kSupportedSinglePlaneDmabufFormats) {
      sendDmabufFormat(resource, server, format);
    }
  }
}

} // namespace lambda::compositor
