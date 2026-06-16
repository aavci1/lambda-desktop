#include <Lambda/Platform/Linux/KmsOutput.hpp>

#include <Lambda/Debug/DebugFlags.hpp>
#include <Lambda/Graphics/RenderTarget.hpp>
#include <Lambda/Graphics/VulkanContext.hpp>

#include "Graphics/Vulkan/VulkanCanvas.hpp"
#include "Graphics/Vulkan/VulkanCheck.hpp"
#include "Platform/Linux/KmsPlatform.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <optional>
#include <span>
#include <ctime>
#include <cmath>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <drm_fourcc.h>
#include <gbm.h>
#include <drm.h>
#include <drm_mode.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

namespace lambda::platform {
namespace {

std::uint64_t monotonicNanoseconds() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<std::uint64_t>(now.tv_sec) * 1'000'000'000ull +
         static_cast<std::uint64_t>(now.tv_nsec);
}

enum class KmsTraceBucket : std::size_t {
  Prepare,
  Reap,
  ResetSemaphore,
  BeginCommand,
  SetTarget,
  MarkRendered,
  FinishCommand,
  QueueSubmit,
  ExportFence,
  UpdateReady,
  SchedulePresent,
  ScheduleOverlay,
  ScheduleDirect,
  ScheduleDirectRepeat,
  AtomicAlloc,
  PopulateRequest,
  AtomicCommit,
  AtomicFree,
  DispatchFlip,
  FlipPoll,
  HandleEvent,
  CursorProps,
  CursorCommit,
  TestAtomic,
  OverlayImport,
  Count,
};

enum class KmsCopyTraceKind : std::size_t {
  PrimaryPreserve,
  Scanout,
  Count,
};

struct KmsCopyTraceCounters {
  std::uint64_t fullCopies = 0;
  std::uint64_t regionCopies = 0;
  std::uint64_t rects = 0;
  std::uint64_t pixels = 0;
  std::uint64_t fullEquivalentPixels = 0;
  std::uint64_t fullPixels = 0;
  std::uint64_t regionPixels = 0;
  std::uint64_t regionFullEquivalentPixels = 0;
};

char const* kmsTraceBucketName(KmsTraceBucket bucket) {
  switch (bucket) {
  case KmsTraceBucket::Prepare: return "prepare";
  case KmsTraceBucket::Reap: return "reap";
  case KmsTraceBucket::ResetSemaphore: return "reset_sem";
  case KmsTraceBucket::BeginCommand: return "begin_cmd";
  case KmsTraceBucket::SetTarget: return "set_target";
  case KmsTraceBucket::MarkRendered: return "mark";
  case KmsTraceBucket::FinishCommand: return "finish_cmd";
  case KmsTraceBucket::QueueSubmit: return "queue_submit";
  case KmsTraceBucket::ExportFence: return "export_fd";
  case KmsTraceBucket::UpdateReady: return "update_ready";
  case KmsTraceBucket::SchedulePresent: return "schedule_present";
  case KmsTraceBucket::ScheduleOverlay: return "schedule_overlay";
  case KmsTraceBucket::ScheduleDirect: return "schedule_direct";
  case KmsTraceBucket::ScheduleDirectRepeat: return "schedule_repeat";
  case KmsTraceBucket::AtomicAlloc: return "atomic_alloc";
  case KmsTraceBucket::PopulateRequest: return "populate";
  case KmsTraceBucket::AtomicCommit: return "atomic_commit";
  case KmsTraceBucket::AtomicFree: return "atomic_free";
  case KmsTraceBucket::DispatchFlip: return "dispatch_flip";
  case KmsTraceBucket::FlipPoll: return "flip_poll";
  case KmsTraceBucket::HandleEvent: return "handle_event";
  case KmsTraceBucket::CursorProps: return "cursor_props";
  case KmsTraceBucket::CursorCommit: return "cursor_commit";
  case KmsTraceBucket::TestAtomic: return "test_atomic";
  case KmsTraceBucket::OverlayImport: return "overlay_import";
  case KmsTraceBucket::Count: return "unknown";
  }
  return "unknown";
}

bool kmsPresentTraceEnabled() {
  static bool const enabled = debug::envNonZero(std::getenv("LAMBDA_KMS_PRESENT_TRACE"));
  return enabled;
}

struct KmsTraceState {
  std::mutex mutex;
  std::uint64_t windowStartNsec = monotonicNanoseconds();
  std::array<std::uint64_t, static_cast<std::size_t>(KmsTraceBucket::Count)> counts{};
  std::array<std::uint64_t, static_cast<std::size_t>(KmsTraceBucket::Count)> nanos{};
  std::array<KmsCopyTraceCounters, static_cast<std::size_t>(KmsCopyTraceKind::Count)> copyCounters{};
};

KmsTraceState& kmsTraceState() {
  static KmsTraceState state;
  return state;
}

std::uint64_t kmsTraceStart() {
  return kmsPresentTraceEnabled() ? monotonicNanoseconds() : 0;
}

void recordKmsCopyTrace(KmsCopyTraceKind kind,
                        bool fullCopy,
                        std::uint64_t rects,
                        std::uint64_t pixels,
                        std::uint64_t fullEquivalentPixels) noexcept {
  if (!kmsPresentTraceEnabled()) return;
  try {
    auto& state = kmsTraceState();
    std::scoped_lock lock(state.mutex);
    std::size_t const index = static_cast<std::size_t>(kind);
    if (index >= state.copyCounters.size()) return;
    KmsCopyTraceCounters& counters = state.copyCounters[index];
    if (fullCopy) {
      ++counters.fullCopies;
      counters.fullPixels += pixels;
    } else {
      ++counters.regionCopies;
      counters.regionPixels += pixels;
      counters.regionFullEquivalentPixels += fullEquivalentPixels;
    }
    counters.rects += rects;
    counters.pixels += pixels;
    counters.fullEquivalentPixels += fullEquivalentPixels;
  } catch (...) {
  }
}

void printKmsCopyCounters(char const* prefix, KmsCopyTraceCounters const& counters) noexcept {
  if (counters.fullCopies == 0 && counters.regionCopies == 0) return;
  std::fprintf(stderr,
               " %s_full=%llu %s_region=%llu %s_rects=%llu %s_pixels=%llu %s_full_equiv_pixels=%llu"
               " %s_full_pixels=%llu %s_region_pixels=%llu %s_region_full_equiv_pixels=%llu",
               prefix,
               static_cast<unsigned long long>(counters.fullCopies),
               prefix,
               static_cast<unsigned long long>(counters.regionCopies),
               prefix,
               static_cast<unsigned long long>(counters.rects),
               prefix,
               static_cast<unsigned long long>(counters.pixels),
               prefix,
               static_cast<unsigned long long>(counters.fullEquivalentPixels),
               prefix,
               static_cast<unsigned long long>(counters.fullPixels),
               prefix,
               static_cast<unsigned long long>(counters.regionPixels),
               prefix,
               static_cast<unsigned long long>(counters.regionFullEquivalentPixels));
}

void recordKmsTraceElapsed(KmsTraceBucket bucket, std::uint64_t elapsedNsec) noexcept {
  if (!kmsPresentTraceEnabled()) return;
  try {
    auto& state = kmsTraceState();
    std::uint64_t const now = monotonicNanoseconds();
    std::scoped_lock lock(state.mutex);
    std::size_t const index = static_cast<std::size_t>(bucket);
    if (index >= state.counts.size()) return;
    ++state.counts[index];
    state.nanos[index] += elapsedNsec;
    if (now - state.windowStartNsec < 1'000'000'000ull) return;

    double const windowSeconds = static_cast<double>(now - state.windowStartNsec) / 1'000'000'000.0;
    std::fprintf(stderr, "kms-trace: window=%.2fs", windowSeconds);
    for (std::size_t i = 0; i < state.counts.size(); ++i) {
      if (state.counts[i] == 0) continue;
      std::fprintf(stderr,
                   " %s=%llu/%.3fms",
                   kmsTraceBucketName(static_cast<KmsTraceBucket>(i)),
                   static_cast<unsigned long long>(state.counts[i]),
                   static_cast<double>(state.nanos[i]) / 1'000'000.0);
    }
    printKmsCopyCounters("primary_preserve_copy",
                         state.copyCounters[static_cast<std::size_t>(KmsCopyTraceKind::PrimaryPreserve)]);
    printKmsCopyCounters("scanout_copy",
                         state.copyCounters[static_cast<std::size_t>(KmsCopyTraceKind::Scanout)]);
    std::fprintf(stderr, "\n");
    state.counts.fill(0);
    state.nanos.fill(0);
    state.copyCounters.fill(KmsCopyTraceCounters{});
    state.windowStartNsec = now;
  } catch (...) {
  }
}

void recordKmsTraceSince(KmsTraceBucket bucket, std::uint64_t startNsec) noexcept {
  if (startNsec == 0) return;
  recordKmsTraceElapsed(bucket, monotonicNanoseconds() - startNsec);
}

class KmsTraceScope {
public:
  explicit KmsTraceScope(KmsTraceBucket bucket) : bucket_(bucket), startNsec_(kmsTraceStart()) {}
  ~KmsTraceScope() noexcept { recordKmsTraceSince(bucket_, startNsec_); }

  KmsTraceScope(KmsTraceScope const&) = delete;
  KmsTraceScope& operator=(KmsTraceScope const&) = delete;

private:
  KmsTraceBucket bucket_;
  std::uint64_t startNsec_ = 0;
};

bool fenceFdReadableNow(int fd) noexcept {
  if (fd < 0) return true;
  pollfd pfd{
      .fd = fd,
      .events = POLLIN,
      .revents = 0,
  };
  return poll(&pfd, 1, 0) > 0 && (pfd.revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) != 0;
}

std::uint32_t refreshRateMilliHz(drmModeModeInfo const& mode) {
  if (mode.vrefresh > 0) return static_cast<std::uint32_t>(mode.vrefresh) * 1000u;
  if (mode.clock > 0 && mode.htotal > 0 && mode.vtotal > 0) {
    return static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(mode.clock) * 1'000'000ull) /
        (static_cast<std::uint64_t>(mode.htotal) * static_cast<std::uint64_t>(mode.vtotal)));
  }
  return 60'000u;
}

bool modesHaveSameTiming(drmModeModeInfo const& lhs, drmModeModeInfo const& rhs) noexcept {
  return lhs.clock == rhs.clock &&
         lhs.hdisplay == rhs.hdisplay &&
         lhs.hsync_start == rhs.hsync_start &&
         lhs.hsync_end == rhs.hsync_end &&
         lhs.htotal == rhs.htotal &&
         lhs.hskew == rhs.hskew &&
         lhs.vdisplay == rhs.vdisplay &&
         lhs.vsync_start == rhs.vsync_start &&
         lhs.vsync_end == rhs.vsync_end &&
         lhs.vtotal == rhs.vtotal &&
         lhs.vscan == rhs.vscan &&
         lhs.flags == rhs.flags;
}

std::chrono::nanoseconds frameInterval(std::uint32_t refreshMilliHz) {
  if (refreshMilliHz == 0) refreshMilliHz = 60'000u;
  return std::chrono::nanoseconds(1'000'000'000'000ll / refreshMilliHz);
}

std::uint64_t monotonicNowNsec() {
  timespec now{};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<std::uint64_t>(now.tv_sec) * 1'000'000'000ull +
         static_cast<std::uint64_t>(now.tv_nsec);
}

std::uint32_t cursorDimension(int fd, std::uint64_t cap) {
  std::uint64_t value = 0;
  if (drmGetCap(fd, cap, &value) == 0 && value >= 16 && value <= 256) {
    return static_cast<std::uint32_t>(value);
  }
  return 64;
}

std::uint32_t findVulkanMemoryType(VkPhysicalDevice physical,
                                   std::uint32_t typeBits,
                                   VkMemoryPropertyFlags requiredProperties) {
  VkPhysicalDeviceMemoryProperties props{};
  vkGetPhysicalDeviceMemoryProperties(physical, &props);
  for (std::uint32_t i = 0; i < props.memoryTypeCount; ++i) {
    if ((typeBits & (1u << i)) &&
        (props.memoryTypes[i].propertyFlags & requiredProperties) == requiredProperties) {
      return i;
    }
  }
  throw std::runtime_error("No suitable Vulkan memory type for KMS scanout buffer");
}

std::uint64_t gbmModifier(gbm_bo* bo) {
  std::uint64_t const modifier = gbm_bo_get_modifier(bo);
  return modifier == DRM_FORMAT_MOD_INVALID ? DRM_FORMAT_MOD_LINEAR : modifier;
}

bool containsModifier(std::vector<std::uint64_t> const& modifiers, std::uint64_t modifier) {
  return std::find(modifiers.begin(), modifiers.end(), modifier) != modifiers.end();
}

void appendUniqueModifier(std::vector<std::uint64_t>& modifiers, std::uint64_t modifier) {
  if (!containsModifier(modifiers, modifier)) modifiers.push_back(modifier);
}

bool forceLinearScanout() {
  char const* value = std::getenv("LAMBDA_COMPOSITOR_FORCE_LINEAR_SCANOUT");
  return debug::envNonZero(value);
}

bool disableKmsRenderInFence() {
  char const* value = std::getenv("LAMBDA_COMPOSITOR_USE_KMS_IN_FENCE");
  return value && !debug::envNonZero(value);
}

bool useDirectScanoutRender() {
  return debug::envNonZero(std::getenv("LAMBDA_COMPOSITOR_DIRECT_SCANOUT_RENDER"));
}

bool disableAutomaticDirectScanoutRender() {
  return debug::envNonZero(std::getenv("LAMBDA_COMPOSITOR_DISABLE_DIRECT_SCANOUT_RENDER"));
}

bool enableKmsOverlayPlanes() {
  if (debug::envNonZero(std::getenv("LAMBDA_COMPOSITOR_DISABLE_KMS_OVERLAYS"))) return false;
  return debug::envNonZero(std::getenv("LAMBDA_COMPOSITOR_ENABLE_KMS_OVERLAYS"));
}

std::uint32_t propertyId(int fd, std::uint32_t objectId, std::uint32_t objectType, char const* name) {
  drmModeObjectProperties* props = drmModeObjectGetProperties(fd, objectId, objectType);
  if (!props) return 0;
  std::uint32_t found = 0;
  for (std::uint32_t i = 0; i < props->count_props && found == 0; ++i) {
    drmModePropertyRes* prop = drmModeGetProperty(fd, props->props[i]);
    if (prop) {
      if (std::strcmp(prop->name, name) == 0) found = prop->prop_id;
      drmModeFreeProperty(prop);
    }
  }
  drmModeFreeObjectProperties(props);
  return found;
}

std::uint64_t propertyValue(int fd, std::uint32_t objectId, std::uint32_t objectType, char const* name) {
  drmModeObjectProperties* props = drmModeObjectGetProperties(fd, objectId, objectType);
  if (!props) return 0;
  std::uint64_t value = 0;
  for (std::uint32_t i = 0; i < props->count_props; ++i) {
    drmModePropertyRes* prop = drmModeGetProperty(fd, props->props[i]);
    if (prop) {
      bool const matches = std::strcmp(prop->name, name) == 0;
      drmModeFreeProperty(prop);
      if (matches) {
        value = props->prop_values[i];
        break;
      }
    }
  }
  drmModeFreeObjectProperties(props);
  return value;
}

std::optional<std::pair<std::uint64_t, std::uint64_t>> propertyRange(int fd,
                                                                     std::uint32_t objectId,
                                                                     std::uint32_t objectType,
                                                                     char const* name) {
  drmModeObjectProperties* props = drmModeObjectGetProperties(fd, objectId, objectType);
  if (!props) return std::nullopt;
  std::optional<std::pair<std::uint64_t, std::uint64_t>> range;
  for (std::uint32_t i = 0; i < props->count_props; ++i) {
    drmModePropertyRes* prop = drmModeGetProperty(fd, props->props[i]);
    if (prop) {
      bool const matches = std::strcmp(prop->name, name) == 0;
      if (matches && (prop->flags & DRM_MODE_PROP_RANGE) != 0 && prop->count_values >= 2) {
        range = std::make_pair(prop->values[0], prop->values[1]);
      }
      drmModeFreeProperty(prop);
      if (matches) break;
    }
  }
  drmModeFreeObjectProperties(props);
  return range;
}

std::uint32_t propertyFlags(int fd, std::uint32_t objectId, std::uint32_t objectType, char const* name) {
  drmModeObjectProperties* props = drmModeObjectGetProperties(fd, objectId, objectType);
  if (!props) return 0;
  std::uint32_t flags = 0;
  for (std::uint32_t i = 0; i < props->count_props; ++i) {
    drmModePropertyRes* prop = drmModeGetProperty(fd, props->props[i]);
    if (prop) {
      bool const matches = std::strcmp(prop->name, name) == 0;
      if (matches) flags = prop->flags;
      drmModeFreeProperty(prop);
      if (matches) break;
    }
  }
  drmModeFreeObjectProperties(props);
  return flags;
}

std::vector<std::uint64_t> planeModifiersForFormat(int fd, std::uint32_t planeId, std::uint32_t drmFormat) {
  std::vector<std::uint64_t> modifiers;
  drmModeObjectProperties* props = drmModeObjectGetProperties(fd, planeId, DRM_MODE_OBJECT_PLANE);
  if (!props) return modifiers;
  std::uint64_t blobId = 0;
  for (std::uint32_t i = 0; i < props->count_props; ++i) {
    drmModePropertyRes* prop = drmModeGetProperty(fd, props->props[i]);
    if (prop) {
      bool const matches = std::strcmp(prop->name, "IN_FORMATS") == 0;
      drmModeFreeProperty(prop);
      if (matches) {
        blobId = props->prop_values[i];
        break;
      }
    }
  }
  drmModeFreeObjectProperties(props);
  if (blobId == 0) return modifiers;

  drmModePropertyBlobRes* blob = drmModeGetPropertyBlob(fd, blobId);
  if (!blob) return modifiers;
  drmModeFormatModifierIterator iter{};
  while (drmModeFormatModifierBlobIterNext(blob, &iter)) {
    if (iter.fmt == drmFormat) appendUniqueModifier(modifiers, iter.mod);
  }
  drmModeFreePropertyBlob(blob);
  return modifiers;
}

VkFormatFeatureFlags2 formatFeaturesForUsage(VkImageUsageFlags usage) {
  VkFormatFeatureFlags2 features = 0;
  if ((usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0) features |= VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT;
  if ((usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0) features |= VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT;
  if ((usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0) features |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT;
  if ((usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0) features |= VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT;
  return features;
}

std::vector<std::uint64_t> vulkanModifiersForFormat(VkPhysicalDevice physical,
                                                    VkFormat format,
                                                    VkImageUsageFlags usage) {
  std::vector<std::uint64_t> modifiers;
  auto modifierList =
      vkStructure<VkDrmFormatModifierPropertiesList2EXT>(VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_2_EXT);
  auto formatProperties = vkStructure<VkFormatProperties2>(VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2);
  formatProperties.pNext = &modifierList;
  vkGetPhysicalDeviceFormatProperties2(physical, format, &formatProperties);
  if (modifierList.drmFormatModifierCount == 0) return modifiers;

  std::vector<VkDrmFormatModifierProperties2EXT> properties(modifierList.drmFormatModifierCount);
  modifierList.pDrmFormatModifierProperties = properties.data();
  vkGetPhysicalDeviceFormatProperties2(physical, format, &formatProperties);

  VkFormatFeatureFlags2 const requiredFeatures = formatFeaturesForUsage(usage);
  for (VkDrmFormatModifierProperties2EXT const& property : properties) {
    if (property.drmFormatModifierPlaneCount != 1) continue;
    if ((property.drmFormatModifierTilingFeatures & requiredFeatures) != requiredFeatures) continue;
    appendUniqueModifier(modifiers, property.drmFormatModifier);
  }
  return modifiers;
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

std::vector<std::uint64_t> scanoutModifiersForUsage(int fd,
                                                    std::uint32_t planeId,
                                                    std::uint32_t drmFormat,
                                                    VkFormat vkFormat,
                                                    VkPhysicalDevice physical,
                                                    VkImageUsageFlags usage,
                                                    bool includeLinear) {
  if (vkFormat == VK_FORMAT_UNDEFINED) return {};
  std::vector<std::uint64_t> const planeModifiers = planeModifiersForFormat(fd, planeId, drmFormat);
  std::vector<std::uint64_t> const vulkanModifiers = vulkanModifiersForFormat(physical, vkFormat, usage);
  std::vector<std::uint64_t> modifiers;
  for (std::uint64_t modifier : planeModifiers) {
    if (modifier == DRM_FORMAT_MOD_INVALID || modifier == DRM_FORMAT_MOD_LINEAR) continue;
    if (containsModifier(vulkanModifiers, modifier)) appendUniqueModifier(modifiers, modifier);
  }
  if (includeLinear && containsModifier(planeModifiers, DRM_FORMAT_MOD_LINEAR) &&
      containsModifier(vulkanModifiers, DRM_FORMAT_MOD_LINEAR)) {
    appendUniqueModifier(modifiers, DRM_FORMAT_MOD_LINEAR);
  }
  return modifiers;
}

void addAtomicProperty(drmModeAtomicReq* request,
                       std::uint32_t objectId,
                       std::uint32_t property,
                       std::uint64_t value,
                       char const* name) {
  if (property == 0) {
    throw std::runtime_error(std::string("KMS atomic property missing: ") + name);
  }
  if (drmModeAtomicAddProperty(request, objectId, property, value) < 0) {
    throw std::runtime_error(std::string("drmModeAtomicAddProperty failed for ") + name);
  }
}

void addOptionalAtomicProperty(drmModeAtomicReq* request,
                               std::uint32_t objectId,
                               std::uint32_t property,
                               std::uint64_t value) {
  if (property == 0) return;
  if (drmModeAtomicAddProperty(request, objectId, property, value) < 0) {
    throw std::runtime_error("drmModeAtomicAddProperty failed for optional property");
  }
}

std::uint32_t crtcIndexForId(int fd, std::uint32_t crtcId) {
  drmModeRes* resources = drmModeGetResources(fd);
  if (!resources) throw std::runtime_error("drmModeGetResources failed while selecting atomic plane");
  std::uint32_t index = UINT32_MAX;
  for (int i = 0; i < resources->count_crtcs; ++i) {
    if (resources->crtcs[i] == crtcId) {
      index = static_cast<std::uint32_t>(i);
      break;
    }
  }
  drmModeFreeResources(resources);
  if (index == UINT32_MAX) throw std::runtime_error("KMS CRTC not found in DRM resources");
  return index;
}

std::uint32_t primaryPlaneForCrtc(int fd, std::uint32_t crtcId) {
  std::uint32_t const crtcIndex = crtcIndexForId(fd, crtcId);
  drmModePlaneRes* planes = drmModeGetPlaneResources(fd);
  if (!planes) throw std::runtime_error("drmModeGetPlaneResources failed");
  std::uint32_t fallback = 0;
  std::uint32_t selected = 0;
  for (std::uint32_t i = 0; i < planes->count_planes; ++i) {
    drmModePlane* plane = drmModeGetPlane(fd, planes->planes[i]);
    if (!plane) continue;
    bool const canUseCrtc = (plane->possible_crtcs & (1u << crtcIndex)) != 0;
    if (canUseCrtc && fallback == 0) fallback = plane->plane_id;
    std::uint64_t const type = propertyValue(fd, plane->plane_id, DRM_MODE_OBJECT_PLANE, "type");
    if (canUseCrtc && type == DRM_PLANE_TYPE_PRIMARY) selected = plane->plane_id;
    drmModeFreePlane(plane);
    if (selected != 0) break;
  }
  drmModeFreePlaneResources(planes);
  selected = selected != 0 ? selected : fallback;
  if (selected == 0) throw std::runtime_error("No usable KMS primary plane found");
  return selected;
}

std::vector<std::uint32_t> planesForCrtcWithType(int fd, std::uint32_t crtcId, std::uint64_t planeType) {
  std::uint32_t const crtcIndex = crtcIndexForId(fd, crtcId);
  drmModePlaneRes* planes = drmModeGetPlaneResources(fd);
  if (!planes) throw std::runtime_error("drmModeGetPlaneResources failed");
  std::vector<std::uint32_t> selected;
  for (std::uint32_t i = 0; i < planes->count_planes; ++i) {
    drmModePlane* plane = drmModeGetPlane(fd, planes->planes[i]);
    if (!plane) continue;
    bool const canUseCrtc = (plane->possible_crtcs & (1u << crtcIndex)) != 0;
    std::uint64_t const type = propertyValue(fd, plane->plane_id, DRM_MODE_OBJECT_PLANE, "type");
    if (canUseCrtc && type == planeType) selected.push_back(plane->plane_id);
    drmModeFreePlane(plane);
  }
  drmModeFreePlaneResources(planes);
  return selected;
}

std::uint64_t normalizedModifier(std::uint64_t modifier) {
  return modifier == DRM_FORMAT_MOD_INVALID ? DRM_FORMAT_MOD_LINEAR : modifier;
}

bool containsFormatModifier(std::vector<KmsDmabufFormatModifier> const& pairs,
                            std::uint32_t format,
                            std::uint64_t modifier) {
  return std::find_if(pairs.begin(), pairs.end(), [format, modifier](KmsDmabufFormatModifier const& pair) {
           return pair.format == format && pair.modifier == modifier;
         }) != pairs.end();
}

void appendUniqueFormatModifier(std::vector<KmsDmabufFormatModifier>& pairs,
                                std::uint32_t format,
                                std::uint64_t modifier) {
  if (!containsFormatModifier(pairs, format, modifier)) {
    pairs.push_back(KmsDmabufFormatModifier{.format = format, .modifier = modifier});
  }
}

void pageFlipHandler(int,
                     unsigned int sequence,
                     unsigned int tvSec,
                     unsigned int tvUsec,
                     void* data) {
  auto* timing = static_cast<KmsAtomicPresenter::PageFlipTiming*>(data);
  timing->hardware = true;
  timing->sequence = sequence;
  timing->monotonicNsec = static_cast<std::uint64_t>(tvSec) * 1'000'000'000ull +
                          static_cast<std::uint64_t>(tvUsec) * 1'000ull;
}

} // namespace

class KmsDevice::Impl {
public:
  explicit Impl(char const* devicePath);

  std::vector<KmsOutput> outputs(std::shared_ptr<Impl> self) const;

  std::unique_ptr<lambda::KmsApplication> app_;
};

class KmsOutput::Impl {
public:
  Impl(std::shared_ptr<KmsDevice::Impl> device, KmsConnector connector)
      : device_(std::move(device)), connector_(std::move(connector)) {}
  ~Impl();

  [[nodiscard]] int drmFd() const noexcept {
    return device_ && device_->app_ ? device_->app_->drmFd() : -1;
  }

  void destroyCursorBuffer();

  struct CursorBuffer {
    std::uint32_t handle = 0;
    std::uint64_t size = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t pitch = 0;
    std::uint32_t fbId = 0;
  };

  struct CursorPlaneProperties {
    std::uint32_t fbId = 0;
    std::uint32_t crtcId = 0;
    std::uint32_t srcX = 0;
    std::uint32_t srcY = 0;
    std::uint32_t srcW = 0;
    std::uint32_t srcH = 0;
    std::uint32_t crtcX = 0;
    std::uint32_t crtcY = 0;
    std::uint32_t crtcW = 0;
    std::uint32_t crtcH = 0;
    std::uint32_t zpos = 0;
  };

  bool ensureAtomicCursorPlane() const;
  bool commitAtomicCursor(std::int32_t x, std::int32_t y, bool visible) const noexcept;
  bool hasDeferredCursorCommit() const noexcept;
  bool retryDeferredCursorCommit() const noexcept;
  drmModeAtomicReq* cursorAtomicRequest() const noexcept;
  bool addAtomicCursorProperties(drmModeAtomicReq* request) const;
  void markAtomicCursorScheduled() const noexcept;
  void setAtomicPageFlipPending(bool pending) const noexcept;

  std::shared_ptr<KmsDevice::Impl> device_;
  KmsConnector connector_{};
  mutable CursorBuffer cursorBuffer_{};
  mutable bool cursorVisible_ = false;
  mutable bool atomicCursorInitialized_ = false;
  mutable bool atomicCursorAvailable_ = false;
  mutable bool atomicCursorActive_ = false;
  mutable bool atomicCursorFailureLogged_ = false;
  mutable std::uint32_t atomicCursorPlaneId_ = 0;
  mutable CursorPlaneProperties atomicCursorPlaneProps_{};
  mutable std::optional<std::uint64_t> atomicCursorZpos_;
  mutable bool atomicCursorZposMutable_ = false;
  mutable bool atomicCursorLogged_ = false;
  mutable bool cursorUploadLogged_ = false;
  mutable bool atomicPageFlipPending_ = false;
  mutable bool atomicCursorMoveDeferred_ = false;
  mutable drmModeAtomicReq* atomicCursorRequest_ = nullptr;
  mutable std::int32_t cursorX_ = 0;
  mutable std::int32_t cursorY_ = 0;
  mutable std::int32_t cursorHotspotX_ = 0;
  mutable std::int32_t cursorHotspotY_ = 0;
  mutable bool vblankWaitFailureLogged_ = false;
  mutable std::uint64_t softwareVblankSequence_ = 0;
  mutable std::uint64_t softwareVblankPhaseNsec_ = 0;
};

class KmsAtomicPresenter::Impl {
public:
  static constexpr std::size_t kMaxTrackedDamageRects = 32;

  Impl(std::shared_ptr<KmsOutput::Impl> output, TextSystem& textSystem)
      : output_(std::move(output)),
        fd_(output_ ? output_->drmFd() : -1),
        connector_(output_ ? output_->connector_ : KmsConnector{}),
        textSystem_(textSystem) {
    try {
      if (fd_ < 0 || connector_.connectorId == 0 || connector_.crtcId == 0) {
        throw std::runtime_error("Invalid KMS output for atomic presenter");
      }
      if (drmSetClientCap(fd_, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1) != 0) {
        throw std::system_error(errno, std::generic_category(), "drmSetClientCap(UNIVERSAL_PLANES)");
      }
      if (drmSetClientCap(fd_, DRM_CLIENT_CAP_ATOMIC, 1) != 0) {
        throw std::system_error(errno, std::generic_category(), "drmSetClientCap(ATOMIC)");
      }
      if (output_) (void)output_->ensureAtomicCursorPlane();
      planeId_ = primaryPlaneForCrtc(fd_, connector_.crtcId);
      loadProperties();
      useRenderInFence_ = !disableKmsRenderInFence() && planeInFenceFd_ != 0;
      directScanoutRenderForced_ = useDirectScanoutRender();
      useAsyncRenderFence_ = true;
      if (planeInFenceFd_ != 0) {
        std::fprintf(stderr,
                     "lambda-window-manager: atomic KMS render fence mode: %s\n",
                     useRenderInFence_ ? "kms-in-fence" : "async wait-before-commit");
      } else {
        std::fprintf(stderr,
                     "lambda-window-manager: atomic KMS render fence mode: async CPU wait-before-commit\n");
      }
      createModeBlob();
      syncModeStateFromKernel();
      gbm_ = gbm_create_device(fd_);
      if (!gbm_) throw std::runtime_error("gbm_create_device failed for KMS atomic presenter");
      lambda::VulkanContext::instance().ensureInitialized();
      directScanoutRender_ = directScanoutRenderForced_ || shouldUseAutomaticDirectScanoutRender();
      createCommandPool();
      try {
        createBuffers();
      } catch (...) {
        if (!directScanoutRender_ || directScanoutRenderForced_) throw;
        std::fprintf(stderr,
                     "lambda-window-manager: automatic direct scanout setup failed; falling back to offscreen copy\n");
        destroyBuffers();
        directScanoutRender_ = false;
        createBuffers();
      }
      canvas_ = lambda::createVulkanRenderTargetCanvas(buffers_[0].spec, textSystem_);
      if (!canvas_) throw std::runtime_error("Failed to create atomic KMS render-target canvas");
      if (directScanoutRender_) {
        std::fprintf(stderr,
                     "lambda-window-manager: atomic KMS presenter renders directly to scanout buffers\n");
      } else {
        std::fprintf(stderr,
                     "lambda-window-manager: atomic KMS presenter uses optimal offscreen render targets with copy to scanout\n");
      }
    } catch (...) {
      cleanup();
      throw;
    }
  }

  ~Impl() { cleanup(); }

  void cleanup() {
    if (canvas_) canvas_.reset();
    VkDevice device = lambda::VulkanContext::instance().device();
    // Intentional presenter teardown wait before destroying KMS buffers.
    if (device) vkDeviceWaitIdle(device);
    if (atomicRequest_) {
      drmModeAtomicFree(atomicRequest_);
      atomicRequest_ = nullptr;
    }
    destroyOverlayFramebuffers();
    for (auto& buffer : buffers_) {
      closeRenderFence(buffer);
      if (buffer.renderFinished) vkDestroySemaphore(device, buffer.renderFinished, nullptr);
      if (buffer.renderFence) vkDestroyFence(device, buffer.renderFence, nullptr);
      if (buffer.offscreenView) vkDestroyImageView(device, buffer.offscreenView, nullptr);
      if (buffer.offscreenImage) vkDestroyImage(device, buffer.offscreenImage, nullptr);
      if (buffer.offscreenMemory) vkFreeMemory(device, buffer.offscreenMemory, nullptr);
      if (buffer.view) vkDestroyImageView(device, buffer.view, nullptr);
      if (buffer.image) vkDestroyImage(device, buffer.image, nullptr);
      if (buffer.memory) vkFreeMemory(device, buffer.memory, nullptr);
      if (buffer.fbId) drmModeRmFB(fd_, buffer.fbId);
      if (buffer.bo) gbm_bo_destroy(buffer.bo);
    }
    buffers_.clear();
    if (commandPool_) vkDestroyCommandPool(device, commandPool_, nullptr);
    commandPool_ = VK_NULL_HANDLE;
    if (modeBlob_ != 0) drmModeDestroyPropertyBlob(fd_, modeBlob_);
    modeBlob_ = 0;
    if (gbm_) gbm_device_destroy(gbm_);
    gbm_ = nullptr;
  }

  Canvas& canvas() {
    if (!canvas_) throw std::runtime_error("Atomic KMS presenter has no canvas");
    return *canvas_;
  }

  [[nodiscard]] bool transientAtomicCommitFailure(int error, char const* operation) noexcept {
    if (error != EBUSY && error != EAGAIN) return false;
    if (!atomicCommitBusyLogged_) {
      std::fprintf(stderr,
                   "lambda-window-manager: %s temporarily busy; retrying on a later loop\n",
                   operation);
      atomicCommitBusyLogged_ = true;
    }
    pendingTiming_ = {};
    return true;
  }

  drmModeAtomicReq* reusableAtomicRequest() {
    if (!atomicRequest_) {
      std::uint64_t const allocStart = kmsTraceStart();
      atomicRequest_ = drmModeAtomicAlloc();
      recordKmsTraceSince(KmsTraceBucket::AtomicAlloc, allocStart);
    } else {
      drmModeAtomicSetCursor(atomicRequest_, 0);
    }
    return atomicRequest_;
  }

  void resetReusableAtomicRequest() noexcept {
    if (atomicRequest_) drmModeAtomicSetCursor(atomicRequest_, 0);
  }

  bool canPrepareFrame() {
    reapDiscardedPreparedFrames();
    if (buffers_.empty()) return false;
    for (std::size_t i = 0; i < buffers_.size(); ++i) {
      Buffer& buffer = buffers_[i];
      updateRenderFenceReadiness(buffer);
      if (static_cast<int>(i) != displayedBuffer_ && static_cast<int>(i) != pendingBuffer_ &&
          static_cast<int>(i) != renderBuffer_ && !buffer.prepared && buffer.renderComplete) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] VkImage primaryRenderImage(int bufferIndex) const noexcept {
    if (bufferIndex < 0 || bufferIndex >= static_cast<int>(buffers_.size())) return VK_NULL_HANDLE;
    auto const& buffer = buffers_[static_cast<std::size_t>(bufferIndex)];
    return directScanoutRender_ ? buffer.image : buffer.offscreenImage;
  }

  [[nodiscard]] VkImageLayout primaryRenderFinalLayout() const noexcept {
    return directScanoutRender_ ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  }

  [[nodiscard]] KmsAtomicPresenter::DamageRect fullDamageRect() const noexcept {
    return KmsAtomicPresenter::DamageRect{
        .x = 0,
        .y = 0,
        .width = connector_.mode.hdisplay,
        .height = connector_.mode.vdisplay,
    };
  }

  [[nodiscard]] std::optional<KmsAtomicPresenter::DamageRect>
  clippedDamageRect(KmsAtomicPresenter::DamageRect const& rect) const noexcept {
    std::int32_t const outputWidth = static_cast<std::int32_t>(connector_.mode.hdisplay);
    std::int32_t const outputHeight = static_cast<std::int32_t>(connector_.mode.vdisplay);
    if (outputWidth <= 0 || outputHeight <= 0 || rect.width == 0 || rect.height == 0) return std::nullopt;
    std::int32_t const x1 = std::clamp(rect.x, 0, outputWidth);
    std::int32_t const y1 = std::clamp(rect.y, 0, outputHeight);
    std::int64_t const rectRight = static_cast<std::int64_t>(rect.x) + static_cast<std::int64_t>(rect.width);
    std::int64_t const rectBottom = static_cast<std::int64_t>(rect.y) + static_cast<std::int64_t>(rect.height);
    std::int32_t const x2 = static_cast<std::int32_t>(std::clamp<std::int64_t>(rectRight, 0, outputWidth));
    std::int32_t const y2 = static_cast<std::int32_t>(std::clamp<std::int64_t>(rectBottom, 0, outputHeight));
    if (x2 <= x1 || y2 <= y1) return std::nullopt;
    return KmsAtomicPresenter::DamageRect{
        .x = x1,
        .y = y1,
        .width = static_cast<std::uint32_t>(x2 - x1),
        .height = static_cast<std::uint32_t>(y2 - y1),
    };
  }

  [[nodiscard]] bool isFullDamageRect(KmsAtomicPresenter::DamageRect const& rect) const noexcept {
    return rect.x <= 0 &&
           rect.y <= 0 &&
           static_cast<std::int64_t>(rect.x) + rect.width >= connector_.mode.hdisplay &&
           static_cast<std::int64_t>(rect.y) + rect.height >= connector_.mode.vdisplay;
  }

  [[nodiscard]] std::uint64_t fullScanoutPixels() const noexcept {
    return static_cast<std::uint64_t>(connector_.mode.hdisplay) *
           static_cast<std::uint64_t>(connector_.mode.vdisplay);
  }

  [[nodiscard]] std::uint64_t copyPixels(VkImageCopy const& copy) const noexcept {
    return static_cast<std::uint64_t>(copy.extent.width) *
           static_cast<std::uint64_t>(copy.extent.height);
  }

  [[nodiscard]] std::uint64_t copyPixels(std::span<VkImageCopy const> copies) const noexcept {
    std::uint64_t pixels = 0;
    for (VkImageCopy const& copy : copies) {
      pixels += copyPixels(copy);
    }
    return pixels;
  }

  [[nodiscard]] bool damageRectsOverlapOrTouch(KmsAtomicPresenter::DamageRect const& a,
                                               KmsAtomicPresenter::DamageRect const& b) const noexcept {
    std::int64_t const ax2 = static_cast<std::int64_t>(a.x) + a.width;
    std::int64_t const ay2 = static_cast<std::int64_t>(a.y) + a.height;
    std::int64_t const bx2 = static_cast<std::int64_t>(b.x) + b.width;
    std::int64_t const by2 = static_cast<std::int64_t>(b.y) + b.height;
    return a.x <= bx2 && b.x <= ax2 && a.y <= by2 && b.y <= ay2;
  }

  [[nodiscard]] KmsAtomicPresenter::DamageRect mergeDamageRects(KmsAtomicPresenter::DamageRect const& a,
                                                                KmsAtomicPresenter::DamageRect const& b) const noexcept {
    std::int32_t const left = std::min(a.x, b.x);
    std::int32_t const top = std::min(a.y, b.y);
    std::int64_t const right = std::max(static_cast<std::int64_t>(a.x) + a.width,
                                        static_cast<std::int64_t>(b.x) + b.width);
    std::int64_t const bottom = std::max(static_cast<std::int64_t>(a.y) + a.height,
                                         static_cast<std::int64_t>(b.y) + b.height);
    return KmsAtomicPresenter::DamageRect{
        .x = left,
        .y = top,
        .width = static_cast<std::uint32_t>(right - left),
        .height = static_cast<std::uint32_t>(bottom - top),
    };
  }

  void appendTrackedDamageRect(std::vector<KmsAtomicPresenter::DamageRect>& rects,
                               KmsAtomicPresenter::DamageRect rect) {
    std::optional<KmsAtomicPresenter::DamageRect> clipped = clippedDamageRect(rect);
    if (!clipped) return;
    rect = *clipped;
    if (isFullDamageRect(rect)) {
      rects.assign(1, fullDamageRect());
      return;
    }
    bool merged = true;
    while (merged) {
      merged = false;
      for (auto it = rects.begin(); it != rects.end(); ++it) {
        if (damageRectsOverlapOrTouch(*it, rect)) {
          rect = mergeDamageRects(*it, rect);
          rects.erase(it);
          merged = true;
          break;
        }
      }
    }
    rects.push_back(rect);
    if (rects.size() > kMaxTrackedDamageRects) {
      rects.assign(1, fullDamageRect());
    }
  }

  [[nodiscard]] std::vector<KmsAtomicPresenter::DamageRect>
  mergedDamageRects(std::span<KmsAtomicPresenter::DamageRect const> first,
                    std::span<KmsAtomicPresenter::DamageRect const> second = {}) {
    std::vector<KmsAtomicPresenter::DamageRect> merged;
    merged.reserve(first.size() + second.size());
    for (KmsAtomicPresenter::DamageRect const& rect : first) {
      appendTrackedDamageRect(merged, rect);
    }
    for (KmsAtomicPresenter::DamageRect const& rect : second) {
      appendTrackedDamageRect(merged, rect);
    }
    return merged;
  }

  [[nodiscard]] bool canPreparePartialFrame(std::span<KmsAtomicPresenter::DamageRect const> damage) const noexcept {
    if (damage.empty() || !modesetDone_ || pageFlipPending_ || renderBuffer_ < 0 || displayedBuffer_ < 0) {
      return false;
    }
    if (activeDirectScanout_ || pendingDirectScanout_ || activeOverlayPlaneId_ != 0 || pendingOverlayPlaneId_ != 0) {
      return false;
    }
    if (displayedBuffer_ == pendingBuffer_) return false;
    if (displayedBuffer_ >= static_cast<int>(buffers_.size()) ||
        renderBuffer_ >= static_cast<int>(buffers_.size())) {
      return false;
    }
    Buffer const& source = buffers_[static_cast<std::size_t>(displayedBuffer_)];
    return source.primaryContentsValid &&
           primaryRenderImage(displayedBuffer_) != VK_NULL_HANDLE &&
           primaryRenderImage(renderBuffer_) != VK_NULL_HANDLE;
  }

  [[nodiscard]] bool copyDisplayedPrimaryToRenderTarget(int destinationBuffer,
                                                        std::span<KmsAtomicPresenter::DamageRect const> damage,
                                                        bool forceFullCopy) {
    if (displayedBuffer_ < 0 || displayedBuffer_ >= static_cast<int>(buffers_.size())) return false;
    if (destinationBuffer < 0 || destinationBuffer >= static_cast<int>(buffers_.size())) return false;
    VkImage const sourceImage = primaryRenderImage(displayedBuffer_);
    VkImage const destinationImage = primaryRenderImage(destinationBuffer);
    if (sourceImage == VK_NULL_HANDLE || destinationImage == VK_NULL_HANDLE || sourceImage == destinationImage) return false;
    bool const fullCopy = forceFullCopy || damage.empty();
    if (!fullCopy && directScanoutRender_) return false;
    VkCommandBuffer commandBuffer = buffers_[static_cast<std::size_t>(destinationBuffer)].commandBuffer;
    if (!commandBuffer) return false;

    VkImageLayout const sourceFinalLayout = primaryRenderFinalLayout();
    if (sourceFinalLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
      transitionImage(commandBuffer,
                      sourceImage,
                      sourceFinalLayout,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                      VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                      VK_ACCESS_2_MEMORY_READ_BIT,
                      VK_ACCESS_2_TRANSFER_READ_BIT);
    }
    VkImageLayout const destinationOldLayout =
        fullCopy || !buffers_[static_cast<std::size_t>(destinationBuffer)].primaryContentsValid
            ? VK_IMAGE_LAYOUT_UNDEFINED
            : primaryRenderFinalLayout();
    transitionImage(commandBuffer,
                    destinationImage,
                    destinationOldLayout,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    destinationOldLayout == VK_IMAGE_LAYOUT_UNDEFINED
                        ? VK_PIPELINE_STAGE_2_NONE
                        : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                    VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                    destinationOldLayout == VK_IMAGE_LAYOUT_UNDEFINED
                        ? VK_ACCESS_2_NONE
                        : VK_ACCESS_2_MEMORY_READ_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT);

    if (fullCopy) {
      VkImageCopy copy{};
      copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      copy.srcSubresource.layerCount = 1;
      copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      copy.dstSubresource.layerCount = 1;
      copy.extent = {connector_.mode.hdisplay, connector_.mode.vdisplay, 1};
      vkCmdCopyImage(commandBuffer,
                     sourceImage,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     destinationImage,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     1,
                     &copy);
      if (kmsPresentTraceEnabled()) {
        std::uint64_t const pixels = fullScanoutPixels();
        recordKmsCopyTrace(KmsCopyTraceKind::PrimaryPreserve, true, 1, pixels, pixels);
      }
    } else {
      std::vector<VkImageCopy> copies;
      copies.reserve(damage.size());
      for (KmsAtomicPresenter::DamageRect const& rect : damage) {
        std::optional<KmsAtomicPresenter::DamageRect> clipped = clippedDamageRect(rect);
        if (!clipped) continue;
        VkImageCopy copy{};
        copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.srcSubresource.layerCount = 1;
        copy.srcOffset = {clipped->x, clipped->y, 0};
        copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.dstSubresource.layerCount = 1;
        copy.dstOffset = {clipped->x, clipped->y, 0};
        copy.extent = {clipped->width, clipped->height, 1};
        copies.push_back(copy);
      }
      if (!copies.empty()) {
        vkCmdCopyImage(commandBuffer,
                       sourceImage,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       destinationImage,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       static_cast<std::uint32_t>(copies.size()),
                       copies.data());
        if (kmsPresentTraceEnabled()) {
          recordKmsCopyTrace(KmsCopyTraceKind::PrimaryPreserve,
                             false,
                             copies.size(),
                             copyPixels(copies),
                             fullScanoutPixels());
        }
      }
    }

    if (sourceFinalLayout != VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
      transitionImage(commandBuffer,
                      sourceImage,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      sourceFinalLayout,
                      VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                      VK_ACCESS_2_TRANSFER_READ_BIT,
                      VK_ACCESS_2_MEMORY_READ_BIT);
    }
    return true;
  }

  bool prepareFrame(std::span<KmsAtomicPresenter::DamageRect const> damage = {}) {
    KmsTraceScope trace(KmsTraceBucket::Prepare);
    reapDiscardedPreparedFrames();
    if (buffers_.empty()) throw std::runtime_error("Atomic KMS presenter has no scanout buffers");
    clearPreparedOverlayCandidate();
    clearPreparedDirectScanout();
    std::optional<std::size_t> next;
    std::size_t candidate = displayedBuffer_ >= 0 ? static_cast<std::size_t>(displayedBuffer_) : 0u;
    for (std::size_t i = 0; i < buffers_.size(); ++i) {
      candidate = (candidate + 1u) % buffers_.size();
      updateRenderFenceReadiness(buffers_[candidate]);
      if (static_cast<int>(candidate) != displayedBuffer_ && static_cast<int>(candidate) != pendingBuffer_ &&
          static_cast<int>(candidate) != renderBuffer_ && !buffers_[candidate].prepared &&
          buffers_[candidate].renderComplete) {
        next = candidate;
        break;
      }
    }
    if (!next) throw std::runtime_error("No reusable KMS atomic render buffers");
    renderBuffer_ = static_cast<int>(*next);
    Buffer& buffer = buffers_[*next];
    bool const partialFrame = canPreparePartialFrame(damage);
    closeRenderFence(buffer);
    if (useAsyncRenderFence_) {
      resetRenderSemaphore(buffer);
    }
    buffer.renderComplete = bufferHasNoAsyncRenderFence(buffer);
    buffer.renderSubmittedNsec = 0;
    buffer.renderReadyNsec = 0;
    buffer.prepared = false;
    buffer.discardWhenReady = false;
    buffer.damageRects.clear();
    buffer.scanoutCopyRects.clear();
    buffer.partialFrame = false;
    beginRenderCommandBuffer(buffer);
    VkImageLayout partialInitialLayout = primaryRenderFinalLayout();
    if (partialFrame) {
      buffer.damageRects = mergedDamageRects(damage);
      bool const staleFullCopy = directScanoutRender_ ||
                                 !buffer.primaryContentsValid ||
                                 std::any_of(buffer.staleRects.begin(),
                                             buffer.staleRects.end(),
                                             [&](KmsAtomicPresenter::DamageRect const& rect) {
                                               return isFullDamageRect(rect);
                                             });
      std::span<KmsAtomicPresenter::DamageRect const> staleDamage =
          staleFullCopy ? std::span<KmsAtomicPresenter::DamageRect const>{}
                        : std::span<KmsAtomicPresenter::DamageRect const>{buffer.staleRects};
      bool const copiedPreservedContent =
          staleFullCopy || !staleDamage.empty()
              ? copyDisplayedPrimaryToRenderTarget(renderBuffer_, staleDamage, staleFullCopy)
              : false;
      if (copiedPreservedContent) {
        partialInitialLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      }
      if (!directScanoutRender_ && !staleFullCopy) {
        buffer.scanoutCopyRects = mergedDamageRects(staleDamage, buffer.damageRects);
      }
      buffer.partialFrame = true;
    }
    std::uint64_t const targetStart = kmsTraceStart();
    buffer.spec.initialLayout = partialFrame ? partialInitialLayout : VK_IMAGE_LAYOUT_UNDEFINED;
    buffer.spec.preserveContents = partialFrame;
    if (!lambda::setVulkanRenderTargetSpecForCanvas(canvas_.get(), buffer.spec)) {
      throw std::runtime_error("Failed to switch atomic KMS render target");
    }
    recordKmsTraceSince(KmsTraceBucket::SetTarget, targetStart);
    return partialFrame;
  }

  std::uint32_t markFrameRendered() {
    KmsTraceScope trace(KmsTraceBucket::MarkRendered);
    if (renderBuffer_ < 0) return 0;
    Buffer& buffer = buffers_[static_cast<std::size_t>(renderBuffer_)];
    finishRenderCommandBuffer(buffer);
    buffer.renderSubmittedNsec = monotonicNanoseconds();
    buffer.renderReadyNsec = 0;
    closeRenderFence(buffer);
    if (buffer.renderFinished == VK_NULL_HANDLE) {
      buffer.renderComplete = true;
      buffer.renderReadyNsec = buffer.renderSubmittedNsec;
      static bool loggedMissingRenderFence = false;
      if (useAsyncRenderFence_ && !loggedMissingRenderFence) {
        std::fprintf(stderr,
                     "lambda-window-manager: KMS render semaphore missing; treating frame as synchronously complete\n");
        loggedMissingRenderFence = true;
      }
    } else {
      buffer.renderFenceFd = exportRenderSemaphoreFd(buffer.renderFinished);
      buffer.renderComplete = false;
      if (!canUseRenderFence(buffer)) {
        updateRenderFenceReadiness(buffer);
      }
    }
    buffer.prepared = true;
    buffer.discardWhenReady = false;
    activePreparedBuffer_ = renderBuffer_;
    std::uint32_t const token = tokenForBuffer(renderBuffer_);
    renderBuffer_ = -1;
    return token;
  }

  bool updateRenderReady(std::uint32_t token = 0) {
    int const index = preparedBufferForToken(token);
    if (index < 0) return true;
    Buffer& buffer = buffers_[static_cast<std::size_t>(index)];
    updateRenderFenceReadiness(buffer);
    releaseDiscardedPreparedBuffer(index);
    return buffer.renderComplete;
  }

  bool canSchedulePresent(std::uint32_t token = 0) {
    int const index = preparedBufferForToken(token);
    if (index < 0) return false;
    bool const renderReady = updateRenderReady(token);
    if (pageFlipPending_) return false;
    Buffer const& buffer = buffers_[static_cast<std::size_t>(index)];
    return renderReady || canUseRenderFence(buffer);
  }

  int renderReadyFd(std::uint32_t token = 0) const noexcept {
    int const index = preparedBufferForToken(token);
    if (index < 0) return -1;
    Buffer const& buffer = buffers_[static_cast<std::size_t>(index)];
    return buffer.renderComplete ? -1 : buffer.renderFenceFd;
  }

  void discardPreparedFrame(std::uint32_t token) {
    int const index = preparedBufferForToken(token);
    if (index < 0) return;
    Buffer& buffer = buffers_[static_cast<std::size_t>(index)];
    if (!buffer.prepared) return;
    if (index == activePreparedBuffer_) activePreparedBuffer_ = -1;
    if (buffer.renderComplete) {
      releasePreparedBuffer(index);
    } else {
      buffer.discardWhenReady = true;
      updateRenderFenceReadiness(buffer);
      releaseDiscardedPreparedBuffer(index);
    }
  }

  bool prepareOverlayCandidate(std::uint32_t token, KmsAtomicPresenter::OverlayCandidate candidate) {
    clearPreparedOverlayCandidate();
    int const overlayBuffer = token == 0 ? renderBuffer_ : preparedBufferForToken(token);
    return prepareOverlayCandidateForBuffer(overlayBuffer, std::move(candidate));
  }

  bool canPrepareOverlayOnly() const noexcept {
    return modesetDone_ && (pageFlipPending_ ? pendingBuffer_ >= 0 : displayedBuffer_ >= 0);
  }

  bool prepareOverlayCandidateForDisplayedFrame(KmsAtomicPresenter::OverlayCandidate candidate) {
    clearPreparedOverlayCandidate();
    if (!canPrepareOverlayOnly()) {
      closeOverlayCandidateFds(candidate);
      return false;
    }
    int const primaryBuffer = pageFlipPending_ ? pendingBuffer_ : displayedBuffer_;
    return prepareOverlayCandidateForBuffer(primaryBuffer, std::move(candidate));
  }

  bool canScheduleOverlayOnly() const noexcept {
    return modesetDone_ && !pageFlipPending_ && preparedOverlay_.has_value() &&
           preparedOverlayPrimaryBuffer_ >= 0 && preparedOverlayPrimaryBuffer_ == displayedBuffer_;
  }

  int preparedOverlayAcquireFenceFd() const noexcept {
    return preparedOverlay_ ? preparedOverlay_->acquireFenceFd : -1;
  }

  std::uint32_t scheduleOverlayOnly() {
    KmsTraceScope trace(KmsTraceBucket::ScheduleOverlay);
    if (!canScheduleOverlayOnly()) throw std::runtime_error("KMS atomic presenter has no prepared overlay-only frame");
    Buffer& buffer = buffers_[static_cast<std::size_t>(preparedOverlayPrimaryBuffer_)];
    if (preparedOverlay_ && fenceFdReadableNow(preparedOverlay_->acquireFenceFd)) {
      closePreparedOverlayFence(*preparedOverlay_);
    }
    drmModeAtomicReq* request = reusableAtomicRequest();
    if (!request) throw std::runtime_error("drmModeAtomicAlloc failed");
    try {
      std::uint64_t const populateStart = kmsTraceStart();
      populateAtomicRequest(request, buffer, false, preparedOverlay_ ? &*preparedOverlay_ : nullptr, 0);
      recordKmsTraceSince(KmsTraceBucket::PopulateRequest, populateStart);
      pendingTiming_ = KmsAtomicPresenter::PageFlipTiming{
          .presentId = nextPresentId_++,
          .scheduledMonotonicNsec = monotonicNanoseconds(),
          .renderSubmittedMonotonicNsec = 0,
          .renderReadyMonotonicNsec = 0,
          .usedRenderFence = false,
      };
      std::uint64_t const commitStartNsec = monotonicNanoseconds();
      pendingTiming_.commitStartMonotonicNsec = commitStartNsec;
      int const rc = drmModeAtomicCommit(fd_, request, DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT, &pendingTiming_);
      pendingTiming_.commitReturnMonotonicNsec = monotonicNanoseconds();
      pendingTiming_.commitDurationNsec = pendingTiming_.commitReturnMonotonicNsec - commitStartNsec;
      recordKmsTraceElapsed(KmsTraceBucket::AtomicCommit, pendingTiming_.commitDurationNsec);
      if (rc != 0) {
        int const error = errno;
        if (transientAtomicCommitFailure(error, "drmModeAtomicCommit overlay-only")) {
          return 0;
        }
        throw std::system_error(error, std::generic_category(), "drmModeAtomicCommit overlay-only");
      }
      pendingBuffer_ = displayedBuffer_;
      pendingOverlayPlaneId_ = preparedOverlay_ ? preparedOverlay_->planeId : 0;
      pendingOverlayFbId_ = preparedOverlay_ ? preparedOverlay_->fbId : 0;
      pendingOverlayBufferId_ = preparedOverlay_ ? preparedOverlay_->bufferId : 0;
      pendingDirectScanout_ = false;
      pendingDirectScanoutFbId_ = 0;
      pendingDirectScanoutBufferId_ = 0;
      pendingDirectScanoutState_.reset();
      clearPreparedOverlayCandidate();
      pageFlipPending_ = true;
      if (output_) {
        output_->setAtomicPageFlipPending(true);
        output_->markAtomicCursorScheduled();
      }
      return pendingTiming_.presentId;
    } catch (...) {
      resetReusableAtomicRequest();
      throw;
    }
  }

  bool prepareOverlayCandidateForBuffer(int overlayBuffer, KmsAtomicPresenter::OverlayCandidate candidate) {
    if (overlayBuffer < 0 || candidate.surfaceId == 0 || candidate.bufferId == 0 ||
        candidate.drmFormat == 0 || candidate.bufferWidth == 0 || candidate.bufferHeight == 0 ||
        candidate.sourceWidth <= 0.0 || candidate.sourceHeight <= 0.0 ||
        candidate.crtcWidth == 0 || candidate.crtcHeight == 0 || candidate.planes.empty() ||
        candidate.planes.size() > 4 || overlayPlanes_.empty()) {
      closeOverlayCandidateFds(candidate);
      return false;
    }

    if (!std::isfinite(candidate.sourceX) || !std::isfinite(candidate.sourceY) ||
        !std::isfinite(candidate.sourceWidth) || !std::isfinite(candidate.sourceHeight)) {
      closeOverlayCandidateFds(candidate);
      return false;
    }

    std::uint64_t const modifier = normalizedModifier(candidate.planes.front().modifier);
    if (char const* reason = overlayStaticRejectReason(candidate.drmFormat, modifier)) {
      if (!overlayStaticRejectLogged_) {
        std::fprintf(stderr,
                     "lambda-window-manager: KMS hardware overlay skipped reason=%s surface=%llu buffer=%llu "
                     "format=0x%08x modifier=0x%016llx\n",
                     reason,
                     static_cast<unsigned long long>(candidate.surfaceId),
                     static_cast<unsigned long long>(candidate.bufferId),
                     candidate.drmFormat,
                     static_cast<unsigned long long>(modifier));
        overlayStaticRejectLogged_ = true;
      }
      closeOverlayCandidateFds(candidate);
      return false;
    }
    OverlayValidationKey const validationKey{
        .format = candidate.drmFormat,
        .modifier = modifier,
        .bufferWidth = candidate.bufferWidth,
        .bufferHeight = candidate.bufferHeight,
        .planeCount = candidate.planes.size(),
        .srcX = toFixed16(candidate.sourceX),
        .srcY = toFixed16(candidate.sourceY),
        .srcW = toFixed16(candidate.sourceWidth),
        .srcH = toFixed16(candidate.sourceHeight),
        .crtcX = candidate.crtcX,
        .crtcY = candidate.crtcY,
        .crtcW = candidate.crtcWidth,
        .crtcH = candidate.crtcHeight,
    };
    if (overlayValidationRejected(validationKey)) {
      closeOverlayCandidateFds(candidate);
      return false;
    }

    Buffer const& buffer = buffers_[static_cast<std::size_t>(overlayBuffer)];
    for (OverlayPlane const& plane : overlayPlanes_) {
      if (!planeCapabilitySupportsFormatModifier(plane.caps, candidate.drmFormat, modifier)) continue;
      OverlayFramebuffer* framebuffer = overlayFramebufferFor(candidate);
      if (!framebuffer) {
        closeOverlayCandidateFds(candidate);
        return false;
      }
      PreparedOverlay overlay{
          .surfaceId = candidate.surfaceId,
          .bufferId = candidate.bufferId,
          .planeId = plane.id,
          .props = plane.props,
          .fbId = framebuffer->fbId,
          .srcX = toFixed16(candidate.sourceX),
          .srcY = toFixed16(candidate.sourceY),
          .srcW = toFixed16(candidate.sourceWidth),
          .srcH = toFixed16(candidate.sourceHeight),
          .crtcX = candidate.crtcX,
          .crtcY = candidate.crtcY,
          .crtcW = candidate.crtcWidth,
          .crtcH = candidate.crtcHeight,
          .acquireFenceFd = -1,
          .zpos = [&]() -> std::optional<std::uint64_t> {
            std::optional<std::uint64_t> const target = cursorSafeOverlayZpos(plane.caps);
            return target && *target != plane.caps.zpos ? target : std::nullopt;
          }(),
      };
      if (testAtomicOverlay(buffer, overlay)) {
        clearPreparedOverlayCandidate();
        overlay.acquireFenceFd = candidate.acquireFenceFd;
        candidate.acquireFenceFd = -1;
        preparedOverlay_ = overlay;
        preparedOverlayPrimaryBuffer_ = overlayBuffer;
        if (!overlayPreparedLogged_) {
          std::fprintf(stderr,
                       "lambda-window-manager: KMS hardware overlay active plane=%u surface=%llu buffer=%llu "
                       "fb=%u format=0x%08x modifier=0x%016llx crtc=%d,%d %ux%u zpos=%s%llu\n",
                       overlay.planeId,
                       static_cast<unsigned long long>(overlay.surfaceId),
                       static_cast<unsigned long long>(overlay.bufferId),
                       overlay.fbId,
                       candidate.drmFormat,
                       static_cast<unsigned long long>(modifier),
                       overlay.crtcX,
                       overlay.crtcY,
                       overlay.crtcW,
                       overlay.crtcH,
                       overlay.zpos ? "" : "default:",
                       static_cast<unsigned long long>(overlay.zpos.value_or(plane.caps.zpos)));
          overlayPreparedLogged_ = true;
        }
        return true;
      }
    }

    rememberRejectedOverlayValidation(validationKey);
    if (!overlayTestFailureLogged_) {
      std::fprintf(stderr,
                   "lambda-window-manager: KMS hardware overlay test rejected surface=%llu buffer=%llu "
                   "format=0x%08x modifier=0x%016llx\n",
                   static_cast<unsigned long long>(candidate.surfaceId),
                   static_cast<unsigned long long>(candidate.bufferId),
                   candidate.drmFormat,
                   static_cast<unsigned long long>(modifier));
      overlayTestFailureLogged_ = true;
    }
    closeOverlayCandidateFds(candidate);
    return false;
  }

  bool prepareDirectScanoutCandidate(KmsAtomicPresenter::OverlayCandidate candidate) {
    clearPreparedDirectScanout();
    if (candidate.surfaceId == 0 || candidate.bufferId == 0 || candidate.drmFormat == 0 ||
        candidate.bufferWidth == 0 || candidate.bufferHeight == 0 || candidate.sourceWidth <= 0.0 ||
        candidate.sourceHeight <= 0.0 || candidate.crtcWidth == 0 || candidate.crtcHeight == 0 ||
        candidate.planes.empty() || candidate.planes.size() > 4) {
      closeOverlayCandidateFds(candidate);
      return false;
    }
    if (!std::isfinite(candidate.sourceX) || !std::isfinite(candidate.sourceY) ||
        !std::isfinite(candidate.sourceWidth) || !std::isfinite(candidate.sourceHeight)) {
      closeOverlayCandidateFds(candidate);
      return false;
    }

    std::uint64_t const modifier = normalizedModifier(candidate.planes.front().modifier);
    if (!planeCapabilitySupportsFormatModifier(primaryPlaneCaps_, candidate.drmFormat, modifier)) {
      if (!directScanoutTestFailureLogged_) {
        std::fprintf(stderr,
                     "lambda-window-manager: KMS direct scanout primary plane rejected format=0x%08x "
                     "modifier=0x%016llx surface=%llu buffer=%llu\n",
                     candidate.drmFormat,
                     static_cast<unsigned long long>(modifier),
                     static_cast<unsigned long long>(candidate.surfaceId),
                     static_cast<unsigned long long>(candidate.bufferId));
        directScanoutTestFailureLogged_ = true;
      }
      closeOverlayCandidateFds(candidate);
      return false;
    }

    OverlayFramebuffer* framebuffer = overlayFramebufferFor(candidate);
    if (!framebuffer) {
      closeOverlayCandidateFds(candidate);
      return false;
    }
    PreparedDirectScanout direct{
        .surfaceId = candidate.surfaceId,
        .bufferId = candidate.bufferId,
        .fbId = framebuffer->fbId,
        .srcX = toFixed16(candidate.sourceX),
        .srcY = toFixed16(candidate.sourceY),
        .srcW = toFixed16(candidate.sourceWidth),
        .srcH = toFixed16(candidate.sourceHeight),
        .crtcX = candidate.crtcX,
        .crtcY = candidate.crtcY,
        .crtcW = candidate.crtcWidth,
        .crtcH = candidate.crtcHeight,
        .acquireFenceFd = -1,
    };
    DirectScanoutValidationKey validationKey{
        .format = candidate.drmFormat,
        .modifier = modifier,
        .bufferWidth = candidate.bufferWidth,
        .bufferHeight = candidate.bufferHeight,
        .planeCount = candidate.planes.size(),
        .srcX = direct.srcX,
        .srcY = direct.srcY,
        .srcW = direct.srcW,
        .srcH = direct.srcH,
        .crtcX = direct.crtcX,
        .crtcY = direct.crtcY,
        .crtcW = direct.crtcW,
        .crtcH = direct.crtcH,
    };
    if (!directScanoutValidationKey_ || *directScanoutValidationKey_ != validationKey) {
      if (!testAtomicDirectScanout(direct)) {
        closeOverlayCandidateFds(candidate);
        return false;
      }
      directScanoutValidationKey_ = validationKey;
    }
    clearPreparedDirectScanout();
    direct.acquireFenceFd = candidate.acquireFenceFd;
    candidate.acquireFenceFd = -1;
    preparedDirectScanout_ = direct;
    if (!directScanoutPreparedLogged_) {
      std::fprintf(stderr,
                   "lambda-window-manager: KMS direct scanout active surface=%llu buffer=%llu "
                   "fb=%u format=0x%08x modifier=0x%016llx crtc=%d,%d %ux%u\n",
                   static_cast<unsigned long long>(direct.surfaceId),
                   static_cast<unsigned long long>(direct.bufferId),
                   direct.fbId,
                   framebuffer->format,
                   static_cast<unsigned long long>(modifier),
                   direct.crtcX,
                   direct.crtcY,
                   direct.crtcW,
                   direct.crtcH);
      directScanoutPreparedLogged_ = true;
    }
    return true;
  }

  bool primeDirectScanoutCandidate(KmsAtomicPresenter::OverlayCandidate& candidate) {
    if (candidate.surfaceId == 0 || candidate.bufferId == 0 || candidate.drmFormat == 0 ||
        candidate.bufferWidth == 0 || candidate.bufferHeight == 0 || candidate.sourceWidth <= 0.0 ||
        candidate.sourceHeight <= 0.0 || candidate.crtcWidth == 0 || candidate.crtcHeight == 0 ||
        candidate.planes.empty() || candidate.planes.size() > 4) {
      return false;
    }
    if (!std::isfinite(candidate.sourceX) || !std::isfinite(candidate.sourceY) ||
        !std::isfinite(candidate.sourceWidth) || !std::isfinite(candidate.sourceHeight)) {
      return false;
    }

    std::uint64_t const modifier = normalizedModifier(candidate.planes.front().modifier);
    if (!planeCapabilitySupportsFormatModifier(primaryPlaneCaps_, candidate.drmFormat, modifier)) return false;

    OverlayFramebuffer* framebuffer = overlayFramebufferFor(candidate);
    if (!framebuffer) return false;

    PreparedDirectScanout direct{
        .surfaceId = candidate.surfaceId,
        .bufferId = candidate.bufferId,
        .fbId = framebuffer->fbId,
        .srcX = toFixed16(candidate.sourceX),
        .srcY = toFixed16(candidate.sourceY),
        .srcW = toFixed16(candidate.sourceWidth),
        .srcH = toFixed16(candidate.sourceHeight),
        .crtcX = candidate.crtcX,
        .crtcY = candidate.crtcY,
        .crtcW = candidate.crtcWidth,
        .crtcH = candidate.crtcHeight,
        .acquireFenceFd = -1,
    };
    DirectScanoutValidationKey validationKey{
        .format = candidate.drmFormat,
        .modifier = modifier,
        .bufferWidth = candidate.bufferWidth,
        .bufferHeight = candidate.bufferHeight,
        .planeCount = candidate.planes.size(),
        .srcX = direct.srcX,
        .srcY = direct.srcY,
        .srcW = direct.srcW,
        .srcH = direct.srcH,
        .crtcX = direct.crtcX,
        .crtcY = direct.crtcY,
        .crtcW = direct.crtcW,
        .crtcH = direct.crtcH,
    };
    if (!directScanoutValidationKey_ || *directScanoutValidationKey_ != validationKey) {
      if (!testAtomicDirectScanout(direct)) return false;
      directScanoutValidationKey_ = validationKey;
    }
    return true;
  }

  bool canScheduleDirectScanout() const noexcept {
    return modesetDone_ && !pageFlipPending_ && preparedDirectScanout_.has_value();
  }

  int preparedDirectScanoutAcquireFenceFd() const noexcept {
    return preparedDirectScanout_ ? preparedDirectScanout_->acquireFenceFd : -1;
  }

  std::uint32_t scheduleDirectScanout() {
    KmsTraceScope trace(KmsTraceBucket::ScheduleDirect);
    if (!canScheduleDirectScanout()) throw std::runtime_error("KMS atomic presenter has no prepared direct scanout frame");
    if (preparedDirectScanout_ && fenceFdReadableNow(preparedDirectScanout_->acquireFenceFd)) {
      closePreparedDirectScanoutFence(*preparedDirectScanout_);
    }
    drmModeAtomicReq* request = reusableAtomicRequest();
    if (!request) throw std::runtime_error("drmModeAtomicAlloc failed");
    try {
      std::uint64_t const populateStart = kmsTraceStart();
      populateDirectScanoutRequest(request, *preparedDirectScanout_);
      recordKmsTraceSince(KmsTraceBucket::PopulateRequest, populateStart);
      pendingTiming_ = KmsAtomicPresenter::PageFlipTiming{
          .presentId = nextPresentId_++,
          .scheduledMonotonicNsec = monotonicNanoseconds(),
          .renderSubmittedMonotonicNsec = 0,
          .renderReadyMonotonicNsec = 0,
          .usedRenderFence = false,
      };
      std::uint64_t const commitStartNsec = monotonicNanoseconds();
      pendingTiming_.commitStartMonotonicNsec = commitStartNsec;
      int const rc = drmModeAtomicCommit(fd_, request, DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT, &pendingTiming_);
      pendingTiming_.commitReturnMonotonicNsec = monotonicNanoseconds();
      pendingTiming_.commitDurationNsec = pendingTiming_.commitReturnMonotonicNsec - commitStartNsec;
      recordKmsTraceElapsed(KmsTraceBucket::AtomicCommit, pendingTiming_.commitDurationNsec);
      if (rc != 0) {
        int const error = errno;
        if (transientAtomicCommitFailure(error, "drmModeAtomicCommit direct scanout")) {
          return 0;
        }
        throw std::system_error(error, std::generic_category(), "drmModeAtomicCommit direct scanout");
      }
      pendingBuffer_ = displayedBuffer_;
      pendingOverlayPlaneId_ = 0;
      pendingOverlayFbId_ = 0;
      pendingOverlayBufferId_ = 0;
      pendingDirectScanout_ = true;
      pendingDirectScanoutFbId_ = preparedDirectScanout_->fbId;
      pendingDirectScanoutBufferId_ = preparedDirectScanout_->bufferId;
      pendingDirectScanoutState_ = *preparedDirectScanout_;
      pendingDirectScanoutState_->acquireFenceFd = -1;
      clearPreparedDirectScanout();
      pageFlipPending_ = true;
      if (output_) {
        output_->setAtomicPageFlipPending(true);
        output_->markAtomicCursorScheduled();
      }
      return pendingTiming_.presentId;
    } catch (...) {
      resetReusableAtomicRequest();
      throw;
    }
  }

  bool canScheduleDirectScanoutRepeat() const noexcept {
    return modesetDone_ && !pageFlipPending_ && activeDirectScanoutState_.has_value();
  }

  std::uint32_t scheduleDirectScanoutRepeat() {
    KmsTraceScope trace(KmsTraceBucket::ScheduleDirectRepeat);
    if (!canScheduleDirectScanoutRepeat()) {
      throw std::runtime_error("KMS atomic presenter has no active direct scanout frame to repeat");
    }
    PreparedDirectScanout repeat = *activeDirectScanoutState_;
    repeat.acquireFenceFd = -1;
    drmModeAtomicReq* request = reusableAtomicRequest();
    if (!request) throw std::runtime_error("drmModeAtomicAlloc failed");
    try {
      std::uint64_t const populateStart = kmsTraceStart();
      populateDirectScanoutRequest(request, repeat);
      recordKmsTraceSince(KmsTraceBucket::PopulateRequest, populateStart);
      pendingTiming_ = KmsAtomicPresenter::PageFlipTiming{
          .presentId = nextPresentId_++,
          .scheduledMonotonicNsec = monotonicNanoseconds(),
          .renderSubmittedMonotonicNsec = 0,
          .renderReadyMonotonicNsec = 0,
          .usedRenderFence = false,
      };
      std::uint64_t const commitStartNsec = monotonicNanoseconds();
      pendingTiming_.commitStartMonotonicNsec = commitStartNsec;
      int const rc = drmModeAtomicCommit(fd_, request, DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT, &pendingTiming_);
      pendingTiming_.commitReturnMonotonicNsec = monotonicNanoseconds();
      pendingTiming_.commitDurationNsec = pendingTiming_.commitReturnMonotonicNsec - commitStartNsec;
      recordKmsTraceElapsed(KmsTraceBucket::AtomicCommit, pendingTiming_.commitDurationNsec);
      if (rc != 0) {
        int const error = errno;
        if (transientAtomicCommitFailure(error, "drmModeAtomicCommit direct scanout repeat")) {
          return 0;
        }
        throw std::system_error(error, std::generic_category(), "drmModeAtomicCommit direct scanout repeat");
      }
      pendingBuffer_ = displayedBuffer_;
      pendingOverlayPlaneId_ = 0;
      pendingOverlayFbId_ = 0;
      pendingOverlayBufferId_ = 0;
      pendingDirectScanout_ = true;
      pendingDirectScanoutFbId_ = repeat.fbId;
      pendingDirectScanoutBufferId_ = repeat.bufferId;
      pendingDirectScanoutState_ = repeat;
      pageFlipPending_ = true;
      if (output_) {
        output_->setAtomicPageFlipPending(true);
        output_->markAtomicCursorScheduled();
      }
      return pendingTiming_.presentId;
    } catch (...) {
      resetReusableAtomicRequest();
      throw;
    }
  }

  void clearPreparedOverlayCandidate() noexcept {
    if (preparedOverlay_) closePreparedOverlayFence(*preparedOverlay_);
    preparedOverlay_.reset();
    preparedOverlayPrimaryBuffer_ = -1;
  }

  void clearPreparedDirectScanout() noexcept {
    if (preparedDirectScanout_) closePreparedDirectScanoutFence(*preparedDirectScanout_);
    preparedDirectScanout_.reset();
  }

  std::uint64_t preparedOverlaySurfaceId() const noexcept {
    return preparedOverlay_ ? preparedOverlay_->surfaceId : 0;
  }

  std::vector<std::uint64_t> overlayBufferIdsInUse() const {
    std::vector<std::uint64_t> ids;
    auto append = [&ids](std::uint64_t id) {
      if (id != 0 && std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
    };
    if (preparedOverlay_) append(preparedOverlay_->bufferId);
    append(pendingOverlayBufferId_);
    append(activeOverlayBufferId_);
    if (preparedDirectScanout_) append(preparedDirectScanout_->bufferId);
    append(pendingDirectScanoutBufferId_);
    append(activeDirectScanoutBufferId_);
    return ids;
  }

  bool canUseOverlayFormatModifier(std::uint32_t format, std::uint64_t modifier) const noexcept {
    return overlayStaticRejectReason(format, normalizedModifier(modifier)) == nullptr;
  }

  std::vector<KmsDmabufFormatModifier> overlayDmabufFormatModifierPreferences() const {
    constexpr std::array<std::uint32_t, 4> formats{
        DRM_FORMAT_ARGB8888,
        DRM_FORMAT_XRGB8888,
        DRM_FORMAT_ABGR8888,
        DRM_FORMAT_XBGR8888,
    };
    std::vector<KmsDmabufFormatModifier> pairs;
    VkPhysicalDevice physical = lambda::VulkanContext::instance().physicalDevice();
    if (!physical) return pairs;

    for (std::uint32_t format : formats) {
      VkFormat const vkFormat = vkFormatForDmabufFormat(format);
      std::vector<std::uint64_t> const vulkanModifiers =
          vulkanModifiersForFormat(physical, vkFormat, VK_IMAGE_USAGE_SAMPLED_BIT);
      if (vulkanModifiers.empty()) continue;

      for (OverlayPlane const& plane : overlayPlanes_) {
        auto const formatIt = std::find_if(plane.caps.formats.begin(),
                                           plane.caps.formats.end(),
                                           [format](PlaneFormatSupport const& support) {
                                             return support.format == format;
                                           });
        if (formatIt == plane.caps.formats.end()) continue;
        std::vector<std::uint64_t> const& planeModifiers = formatIt->modifiers;
        if (containsModifier(planeModifiers, DRM_FORMAT_MOD_LINEAR) &&
            containsModifier(vulkanModifiers, DRM_FORMAT_MOD_LINEAR)) {
          appendUniqueFormatModifier(pairs, format, DRM_FORMAT_MOD_LINEAR);
        }
      }
    }

    if (!pairs.empty()) {
      std::fprintf(stderr,
                   "lambda-window-manager: KMS hardware overlay dmabuf preferences=%zu first=format 0x%08x modifier 0x%016llx\n",
                   pairs.size(),
                   pairs.front().format,
                   static_cast<unsigned long long>(pairs.front().modifier));
    }
    return pairs;
  }

  std::uint32_t schedulePresent(std::uint32_t token = 0) {
    KmsTraceScope trace(KmsTraceBucket::SchedulePresent);
    if (pageFlipPending_) throw std::runtime_error("KMS atomic page flip is already pending");
    int const index = preparedBufferForToken(token);
    if (index < 0) throw std::runtime_error("KMS atomic presenter has no prepared render buffer");
    if (!canSchedulePresent(token)) throw std::runtime_error("KMS atomic render buffer is not ready");
    Buffer& buffer = buffers_[static_cast<std::size_t>(index)];
    std::uint32_t damageBlob = createDamageClipBlob(buffer);
    drmModeAtomicReq* request = reusableAtomicRequest();
    if (!request) throw std::runtime_error("drmModeAtomicAlloc failed");
    try {
      bool const useRenderFence = !buffer.renderComplete && canUseRenderFence(buffer);
      std::uint64_t const populateStart = kmsTraceStart();
      populateAtomicRequest(request,
                            buffer,
                            useRenderFence,
                            preparedOverlay_ ? &*preparedOverlay_ : nullptr,
                            damageBlob);
      recordKmsTraceSince(KmsTraceBucket::PopulateRequest, populateStart);
      std::uint32_t flags = DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT;
      bool const modesetCommit = !modesetDone_;
      if (modesetCommit) flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
      pendingTiming_ = KmsAtomicPresenter::PageFlipTiming{
          .presentId = nextPresentId_++,
          .scheduledMonotonicNsec = monotonicNanoseconds(),
          .renderSubmittedMonotonicNsec = buffer.renderSubmittedNsec,
          .renderReadyMonotonicNsec = buffer.renderReadyNsec,
          .usedRenderFence = useRenderFence,
          .usedModeset = modesetCommit,
      };
      std::uint64_t const commitStartNsec = monotonicNanoseconds();
      pendingTiming_.commitStartMonotonicNsec = commitStartNsec;
      int const rc = drmModeAtomicCommit(fd_, request, flags, &pendingTiming_);
      pendingTiming_.commitReturnMonotonicNsec = monotonicNanoseconds();
      pendingTiming_.commitDurationNsec = pendingTiming_.commitReturnMonotonicNsec - commitStartNsec;
      recordKmsTraceElapsed(KmsTraceBucket::AtomicCommit, pendingTiming_.commitDurationNsec);
      if (rc != 0) {
        int const error = errno;
        if (transientAtomicCommitFailure(error, "drmModeAtomicCommit")) {
          if (damageBlob != 0) {
            drmModeDestroyPropertyBlob(fd_, damageBlob);
            damageBlob = 0;
          }
          return 0;
        }
        throw std::system_error(error, std::generic_category(), "drmModeAtomicCommit");
      }
      if (damageBlob != 0) {
        drmModeDestroyPropertyBlob(fd_, damageBlob);
        damageBlob = 0;
      }
      modesetDone_ = true;
      pendingBuffer_ = index;
      if (buffer.partialFrame) {
        markPrimaryDamagePresented(index, buffer.damageRects);
      } else {
        markPrimaryDamagePresented(index, {});
      }
      buffer.prepared = false;
      buffer.discardWhenReady = false;
      buffer.damageRects.clear();
      buffer.scanoutCopyRects.clear();
      buffer.partialFrame = false;
      buffer.primaryContentsValid = true;
      if (activePreparedBuffer_ == index) activePreparedBuffer_ = -1;
      pendingOverlayPlaneId_ = preparedOverlay_ ? preparedOverlay_->planeId : 0;
      pendingOverlayFbId_ = preparedOverlay_ ? preparedOverlay_->fbId : 0;
      pendingOverlayBufferId_ = preparedOverlay_ ? preparedOverlay_->bufferId : 0;
      pendingDirectScanout_ = false;
      pendingDirectScanoutFbId_ = 0;
      pendingDirectScanoutBufferId_ = 0;
      pendingDirectScanoutState_.reset();
      clearPreparedOverlayCandidate();
      clearPreparedDirectScanout();
      pageFlipPending_ = true;
      if (output_) {
        output_->setAtomicPageFlipPending(true);
        output_->markAtomicCursorScheduled();
      }
      return pendingTiming_.presentId;
    } catch (...) {
      resetReusableAtomicRequest();
      if (damageBlob != 0) drmModeDestroyPropertyBlob(fd_, damageBlob);
      throw;
    }
  }

  KmsAtomicPresenter::PageFlipTiming present() {
    if (schedulePresent() == 0) {
      throw std::system_error(EBUSY, std::generic_category(), "drmModeAtomicCommit");
    }
    waitForPageFlip();
    return pendingTiming_;
  }

  std::optional<KmsAtomicPresenter::PageFlipTiming> dispatchPageFlipEvents() {
    KmsTraceScope trace(KmsTraceBucket::DispatchFlip);
    if (!pageFlipPending_) return std::nullopt;
    pollfd pfd{.fd = fd_, .events = POLLIN, .revents = 0};
    std::uint64_t const pollStart = kmsTraceStart();
    int const pollResult = poll(&pfd, 1, 0);
    recordKmsTraceSince(KmsTraceBucket::FlipPoll, pollStart);
    if (pollResult < 0) {
      if (errno == EINTR) return std::nullopt;
      throw std::system_error(errno, std::generic_category(), "poll KMS page flip");
    }
    if (pollResult == 0) return std::nullopt;
    drmEventContext eventContext{};
    eventContext.version = DRM_EVENT_CONTEXT_VERSION;
    eventContext.page_flip_handler = pageFlipHandler;
    pendingTiming_.eventDispatchStartMonotonicNsec = monotonicNanoseconds();
    std::uint64_t const handleStart = kmsTraceStart();
    if (drmHandleEvent(fd_, &eventContext) != 0) {
      throw std::system_error(errno, std::generic_category(), "drmHandleEvent");
    }
    recordKmsTraceSince(KmsTraceBucket::HandleEvent, handleStart);
    pendingTiming_.eventDispatchEndMonotonicNsec = monotonicNanoseconds();
    if (!pendingTiming_.hardware) return std::nullopt;
    if (pendingBuffer_ >= 0 && pendingBuffer_ < static_cast<int>(buffers_.size())) {
      updateRenderFenceReadiness(buffers_[static_cast<std::size_t>(pendingBuffer_)]);
    }
    displayedBuffer_ = pendingBuffer_;
    pendingBuffer_ = -1;
    activeOverlayPlaneId_ = pendingOverlayPlaneId_;
    activeOverlayFbId_ = pendingOverlayFbId_;
    activeOverlayBufferId_ = pendingOverlayBufferId_;
    activeDirectScanout_ = pendingDirectScanout_;
    activeDirectScanoutFbId_ = pendingDirectScanoutFbId_;
    activeDirectScanoutBufferId_ = pendingDirectScanoutBufferId_;
    activeDirectScanoutState_ = pendingDirectScanoutState_;
    if (activeDirectScanout_) {
      activeOverlayPlaneId_ = 0;
      activeOverlayFbId_ = 0;
      activeOverlayBufferId_ = 0;
      if (displayedBuffer_ >= 0 && displayedBuffer_ < static_cast<int>(buffers_.size())) {
        buffers_[static_cast<std::size_t>(displayedBuffer_)].primaryContentsValid = false;
      }
    } else {
      activeDirectScanoutState_.reset();
      if (displayedBuffer_ >= 0 && displayedBuffer_ < static_cast<int>(buffers_.size())) {
        buffers_[static_cast<std::size_t>(displayedBuffer_)].primaryContentsValid = true;
      }
    }
    pendingOverlayPlaneId_ = 0;
    pendingOverlayFbId_ = 0;
    pendingOverlayBufferId_ = 0;
    pendingDirectScanout_ = false;
    pendingDirectScanoutFbId_ = 0;
    pendingDirectScanoutBufferId_ = 0;
    pendingDirectScanoutState_.reset();
    pruneOverlayFramebufferCache();
    pageFlipPending_ = false;
    if (output_) output_->setAtomicPageFlipPending(false);
    return pendingTiming_;
  }

  bool hasPendingPageFlip() const noexcept { return pageFlipPending_; }

  int eventFd() const noexcept { return fd_; }

  void syncModeStateFromKernel() noexcept {
    if (pageFlipPending_) {
      std::fprintf(stderr,
                   "lambda-window-manager: clearing stale KMS page flip after VT transition "
                   "present=%u pendingBuffer=%d\n",
                   pendingTiming_.presentId,
                   pendingBuffer_);
    }
    pageFlipPending_ = false;
    pendingBuffer_ = -1;
    pendingOverlayPlaneId_ = 0;
    pendingOverlayFbId_ = 0;
    pendingOverlayBufferId_ = 0;
    pendingDirectScanout_ = false;
    pendingDirectScanoutFbId_ = 0;
    pendingDirectScanoutBufferId_ = 0;
    pendingDirectScanoutState_.reset();
    pendingTiming_ = {};
    activeOverlayPlaneId_ = 0;
    activeOverlayFbId_ = 0;
    activeOverlayBufferId_ = 0;
    activeDirectScanout_ = false;
    activeDirectScanoutFbId_ = 0;
    activeDirectScanoutBufferId_ = 0;
    activeDirectScanoutState_.reset();
    modesetDone_ = false;
    displayedBuffer_ = -1;
    for (Buffer& buffer : buffers_) {
      updateRenderFenceReadiness(buffer);
      buffer.primaryContentsValid = false;
      buffer.staleRects.assign(1, fullDamageRect());
    }
    if (output_) output_->setAtomicPageFlipPending(false);
    clearPreparedOverlayCandidate();
    clearPreparedDirectScanout();
  }

private:
  struct PlaneFormatSupport {
    std::uint32_t format = 0;
    std::vector<std::uint64_t> modifiers;
  };

  bool currentKernelModeMatchesTarget() const noexcept {
    if (fd_ < 0 || connector_.crtcId == 0 || connector_.connectorId == 0) return false;
    if (connectorCrtcId_ != 0 &&
        propertyValue(fd_, connector_.connectorId, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID") != connector_.crtcId) {
      return false;
    }
    drmModeCrtc* crtc = drmModeGetCrtc(fd_, connector_.crtcId);
    if (!crtc) return false;
    bool const matches = crtc->mode_valid && modesHaveSameTiming(crtc->mode, connector_.mode);
    drmModeFreeCrtc(crtc);
    return matches;
  }

  struct PlaneCapabilities {
    std::uint32_t possibleCrtcs = 0;
    std::uint64_t type = 0;
    bool hasZpos = false;
    std::uint64_t zpos = 0;
    std::vector<PlaneFormatSupport> formats;
  };

  struct PlaneProperties {
    std::uint32_t fbId = 0;
    std::uint32_t crtcId = 0;
    std::uint32_t srcX = 0;
    std::uint32_t srcY = 0;
    std::uint32_t srcW = 0;
    std::uint32_t srcH = 0;
    std::uint32_t crtcX = 0;
    std::uint32_t crtcY = 0;
    std::uint32_t crtcW = 0;
    std::uint32_t crtcH = 0;
    std::uint32_t inFenceFd = 0;
    std::uint32_t fbDamageClips = 0;
    std::uint32_t alpha = 0;
    std::uint32_t pixelBlendMode = 0;
    std::uint32_t rotation = 0;
    std::uint32_t zpos = 0;
  };

  struct OverlayPlane {
    std::uint32_t id = 0;
    PlaneProperties props{};
    PlaneCapabilities caps{};
  };

  std::optional<std::uint64_t> cursorSafeOverlayZpos(PlaneCapabilities const& caps) const noexcept {
    if (!cursorPlaneZpos_ || !caps.hasZpos || *cursorPlaneZpos_ == 0) return std::nullopt;
    if (caps.zpos < *cursorPlaneZpos_) return caps.zpos;
    if (!primaryPlaneCaps_.hasZpos || primaryPlaneCaps_.zpos + 1 >= *cursorPlaneZpos_) return std::nullopt;
    return *cursorPlaneZpos_ - 1;
  }

  struct OverlayFramebuffer {
    std::uint64_t bufferId = 0;
    std::uint32_t format = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint32_t> offsets;
    std::vector<std::uint32_t> strides;
    std::vector<std::uint64_t> modifiers;
    std::uint32_t fbId = 0;
    std::uint64_t lastUsedSerial = 0;
  };

  struct PreparedOverlay {
    std::uint64_t surfaceId = 0;
    std::uint64_t bufferId = 0;
    std::uint32_t planeId = 0;
    PlaneProperties props{};
    std::uint32_t fbId = 0;
    std::uint64_t srcX = 0;
    std::uint64_t srcY = 0;
    std::uint64_t srcW = 0;
    std::uint64_t srcH = 0;
    std::int32_t crtcX = 0;
    std::int32_t crtcY = 0;
    std::uint32_t crtcW = 0;
    std::uint32_t crtcH = 0;
    int acquireFenceFd = -1;
    std::optional<std::uint64_t> zpos;
  };

  struct PreparedDirectScanout {
    std::uint64_t surfaceId = 0;
    std::uint64_t bufferId = 0;
    std::uint32_t fbId = 0;
    std::uint64_t srcX = 0;
    std::uint64_t srcY = 0;
    std::uint64_t srcW = 0;
    std::uint64_t srcH = 0;
    std::int32_t crtcX = 0;
    std::int32_t crtcY = 0;
    std::uint32_t crtcW = 0;
    std::uint32_t crtcH = 0;
    int acquireFenceFd = -1;
  };

  struct DirectScanoutValidationKey {
    std::uint32_t format = 0;
    std::uint64_t modifier = 0;
    std::uint32_t bufferWidth = 0;
    std::uint32_t bufferHeight = 0;
    std::size_t planeCount = 0;
    std::uint64_t srcX = 0;
    std::uint64_t srcY = 0;
    std::uint64_t srcW = 0;
    std::uint64_t srcH = 0;
    std::int32_t crtcX = 0;
    std::int32_t crtcY = 0;
    std::uint32_t crtcW = 0;
    std::uint32_t crtcH = 0;

    bool operator==(DirectScanoutValidationKey const&) const = default;
  };

  struct OverlayValidationKey {
    std::uint32_t format = 0;
    std::uint64_t modifier = 0;
    std::uint32_t bufferWidth = 0;
    std::uint32_t bufferHeight = 0;
    std::size_t planeCount = 0;
    std::uint64_t srcX = 0;
    std::uint64_t srcY = 0;
    std::uint64_t srcW = 0;
    std::uint64_t srcH = 0;
    std::int32_t crtcX = 0;
    std::int32_t crtcY = 0;
    std::uint32_t crtcW = 0;
    std::uint32_t crtcH = 0;

    bool operator==(OverlayValidationKey const&) const = default;
  };

  struct Buffer {
    gbm_bo* bo = nullptr;
    std::uint32_t fbId = 0;
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkImage offscreenImage = VK_NULL_HANDLE;
    VkDeviceMemory offscreenMemory = VK_NULL_HANDLE;
    VkImageView offscreenView = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    bool commandBufferRecording = false;
    VkFence renderFence = VK_NULL_HANDLE;
    VkSemaphore renderFinished = VK_NULL_HANDLE;
    int renderFenceFd = -1;
    bool renderComplete = true;
    bool prepared = false;
    bool discardWhenReady = false;
    bool partialFrame = false;
    bool primaryContentsValid = false;
    std::uint64_t renderSubmittedNsec = 0;
    std::uint64_t renderReadyNsec = 0;
    std::vector<KmsAtomicPresenter::DamageRect> damageRects;
    std::vector<KmsAtomicPresenter::DamageRect> staleRects;
    std::vector<KmsAtomicPresenter::DamageRect> scanoutCopyRects;
    VulkanRenderTargetSpec spec{};
  };

  std::uint32_t tokenForBuffer(int index) const noexcept {
    return index >= 0 ? static_cast<std::uint32_t>(index + 1) : 0u;
  }

  int bufferForToken(std::uint32_t token) const noexcept {
    if (token == 0) return activePreparedBuffer_;
    int const index = static_cast<int>(token - 1u);
    if (index < 0 || index >= static_cast<int>(buffers_.size())) return -1;
    return index;
  }

  int preparedBufferForToken(std::uint32_t token) const noexcept {
    int const index = bufferForToken(token);
    if (index < 0) return -1;
    Buffer const& buffer = buffers_[static_cast<std::size_t>(index)];
    return buffer.prepared ? index : -1;
  }

  void releasePreparedBuffer(int index) noexcept {
    if (index < 0 || index >= static_cast<int>(buffers_.size())) return;
    Buffer& buffer = buffers_[static_cast<std::size_t>(index)];
    closeRenderFence(buffer);
    buffer.prepared = false;
    buffer.discardWhenReady = false;
    buffer.partialFrame = false;
    buffer.primaryContentsValid = false;
    buffer.damageRects.clear();
    buffer.staleRects.assign(1, fullDamageRect());
    buffer.scanoutCopyRects.clear();
    buffer.renderComplete = true;
    buffer.renderSubmittedNsec = 0;
    buffer.renderReadyNsec = 0;
    if (activePreparedBuffer_ == index) activePreparedBuffer_ = -1;
  }

  void markPrimaryDamagePresented(int presentedIndex,
                                  std::span<KmsAtomicPresenter::DamageRect const> changedDamage) {
    if (presentedIndex < 0 || presentedIndex >= static_cast<int>(buffers_.size())) return;
    for (std::size_t i = 0; i < buffers_.size(); ++i) {
      Buffer& buffer = buffers_[i];
      if (static_cast<int>(i) == presentedIndex) {
        buffer.staleRects.clear();
        continue;
      }
      if (!buffer.primaryContentsValid) continue;
      if (changedDamage.empty()) {
        buffer.staleRects.assign(1, fullDamageRect());
      } else {
        for (KmsAtomicPresenter::DamageRect const& rect : changedDamage) {
          appendTrackedDamageRect(buffer.staleRects, rect);
        }
      }
    }
  }

  void releaseDiscardedPreparedBuffer(int index) {
    if (index < 0 || index >= static_cast<int>(buffers_.size())) return;
    Buffer& buffer = buffers_[static_cast<std::size_t>(index)];
    if (buffer.prepared && buffer.discardWhenReady && buffer.renderComplete) {
      releasePreparedBuffer(index);
    }
  }

  void reapDiscardedPreparedFrames() {
    KmsTraceScope trace(KmsTraceBucket::Reap);
    for (std::size_t i = 0; i < buffers_.size(); ++i) {
      Buffer& buffer = buffers_[i];
      if (!buffer.prepared || !buffer.discardWhenReady) continue;
      updateRenderFenceReadiness(buffer);
      releaseDiscardedPreparedBuffer(static_cast<int>(i));
    }
  }

  static bool bufferHasNoAsyncRenderFence(Buffer const& buffer) noexcept {
    return buffer.renderFinished == VK_NULL_HANDLE;
  }

  bool canUseRenderFence(Buffer const& buffer) const noexcept {
    return useRenderInFence_ && buffer.renderFenceFd >= 0;
  }

  void resetRenderSemaphore(Buffer& buffer) {
    KmsTraceScope trace(KmsTraceBucket::ResetSemaphore);
    updateRenderFenceReadiness(buffer);
    if (!buffer.renderComplete) {
      throw std::runtime_error("KMS render buffer is still in use");
    }
    VkDevice device = lambda::VulkanContext::instance().device();
    if (buffer.renderFinished) {
      vkDestroySemaphore(device, buffer.renderFinished, nullptr);
      buffer.renderFinished = VK_NULL_HANDLE;
    }
    buffer.renderFinished = createExportableSemaphore();
  }

  void closeRenderFence(Buffer& buffer) noexcept {
    if (buffer.renderFenceFd >= 0) {
      close(buffer.renderFenceFd);
      buffer.renderFenceFd = -1;
    }
  }

  void markRenderWorkComplete(Buffer& buffer) noexcept {
    closeRenderFence(buffer);
    buffer.renderComplete = true;
    if (buffer.renderSubmittedNsec != 0 && buffer.renderReadyNsec == 0) {
      buffer.renderReadyNsec = monotonicNanoseconds();
    }
  }

  void updateRenderFenceReadiness(Buffer& buffer) {
    KmsTraceScope trace(KmsTraceBucket::UpdateReady);
    if (buffer.renderComplete) return;
    if (buffer.renderFence) {
      VkResult const fenceStatus =
          vkGetFenceStatus(lambda::VulkanContext::instance().device(), buffer.renderFence);
      if (fenceStatus == VK_SUCCESS) {
        markRenderWorkComplete(buffer);
        return;
      }
      if (fenceStatus != VK_NOT_READY) {
        vkCheck(fenceStatus, "vkGetFenceStatus atomic KMS render fence");
      }
    }
    if (buffer.renderFenceFd < 0) return;
    pollfd pfd{.fd = buffer.renderFenceFd, .events = POLLIN, .revents = 0};
    int const pollResult = poll(&pfd, 1, 0);
    if (pollResult < 0) {
      if (errno == EINTR) return;
      throw std::system_error(errno, std::generic_category(), "poll KMS render sync fd");
    }
    if (pollResult > 0 && (pfd.revents & (POLLIN | POLLERR | POLLHUP)) != 0) {
      if (!buffer.renderFence) {
        markRenderWorkComplete(buffer);
      }
    }
  }

  static std::uint64_t toFixed16(double value) noexcept {
    if (value <= 0.0) return 0;
    return static_cast<std::uint64_t>(std::llround(value * 65536.0));
  }

  static void closeOverlayCandidatePlaneFds(KmsAtomicPresenter::OverlayCandidate& candidate) noexcept {
    for (auto& plane : candidate.planes) {
      if (plane.fd >= 0) {
        close(plane.fd);
        plane.fd = -1;
      }
    }
  }

  static void closeOverlayCandidateFds(KmsAtomicPresenter::OverlayCandidate& candidate) noexcept {
    closeOverlayCandidatePlaneFds(candidate);
    if (candidate.acquireFenceFd >= 0) {
      close(candidate.acquireFenceFd);
      candidate.acquireFenceFd = -1;
    }
  }

  static void closePreparedOverlayFence(PreparedOverlay& overlay) noexcept {
    if (overlay.acquireFenceFd >= 0) {
      close(overlay.acquireFenceFd);
      overlay.acquireFenceFd = -1;
    }
  }

  static void closePreparedDirectScanoutFence(PreparedDirectScanout& direct) noexcept {
    if (direct.acquireFenceFd >= 0) {
      close(direct.acquireFenceFd);
      direct.acquireFenceFd = -1;
    }
  }

  void closeGemHandle(std::uint32_t handle) noexcept {
    if (handle == 0) return;
    struct drm_gem_close closeRequest {};
    closeRequest.handle = handle;
    drmIoctl(fd_, DRM_IOCTL_GEM_CLOSE, &closeRequest);
  }

  static bool overlayFramebufferMatches(OverlayFramebuffer const& fb,
                                        KmsAtomicPresenter::OverlayCandidate const& candidate) {
    if (fb.bufferId != candidate.bufferId || fb.format != candidate.drmFormat ||
        fb.width != candidate.bufferWidth || fb.height != candidate.bufferHeight ||
        fb.offsets.size() != candidate.planes.size() || fb.strides.size() != candidate.planes.size() ||
        fb.modifiers.size() != candidate.planes.size()) {
      return false;
    }
    for (std::size_t i = 0; i < candidate.planes.size(); ++i) {
      if (fb.offsets[i] != candidate.planes[i].offset ||
          fb.strides[i] != candidate.planes[i].stride ||
          fb.modifiers[i] != normalizedModifier(candidate.planes[i].modifier)) {
        return false;
      }
    }
    return true;
  }

  OverlayFramebuffer* overlayFramebufferFor(KmsAtomicPresenter::OverlayCandidate& candidate) {
    for (auto& fb : overlayFramebuffers_) {
      if (overlayFramebufferMatches(fb, candidate)) {
        fb.lastUsedSerial = ++overlayUseSerial_;
        closeOverlayCandidatePlaneFds(candidate);
        return &fb;
      }
    }
    return importOverlayFramebuffer(candidate);
  }

  OverlayFramebuffer* importOverlayFramebuffer(KmsAtomicPresenter::OverlayCandidate& candidate) {
    KmsTraceScope trace(KmsTraceBucket::OverlayImport);
    std::array<std::uint32_t, 4> handles{};
    std::array<std::uint32_t, 4> strides{};
    std::array<std::uint32_t, 4> offsets{};
    std::array<std::uint64_t, 4> modifiers{};
    std::size_t const planeCount = candidate.planes.size();
    try {
      for (std::size_t i = 0; i < planeCount; ++i) {
        if (candidate.planes[i].fd < 0) throw std::runtime_error("overlay dmabuf fd is invalid");
        std::uint32_t handle = 0;
        if (drmPrimeFDToHandle(fd_, candidate.planes[i].fd, &handle) != 0) {
          throw std::system_error(errno, std::generic_category(), "drmPrimeFDToHandle overlay dmabuf");
        }
        handles[i] = handle;
        strides[i] = candidate.planes[i].stride;
        offsets[i] = candidate.planes[i].offset;
        modifiers[i] = normalizedModifier(candidate.planes[i].modifier);
      }

      std::uint32_t fbId = 0;
      int rc = drmModeAddFB2WithModifiers(fd_,
                                          candidate.bufferWidth,
                                          candidate.bufferHeight,
                                          candidate.drmFormat,
                                          handles.data(),
                                          strides.data(),
                                          offsets.data(),
                                          modifiers.data(),
                                          &fbId,
                                          DRM_MODE_FB_MODIFIERS);
      if (rc != 0) {
        bool const allLinear = std::all_of(modifiers.begin(), modifiers.begin() + static_cast<std::ptrdiff_t>(planeCount),
                                           [](std::uint64_t modifier) {
                                             return modifier == DRM_FORMAT_MOD_LINEAR;
                                           });
        if (allLinear) {
          rc = drmModeAddFB2(fd_,
                             candidate.bufferWidth,
                             candidate.bufferHeight,
                             candidate.drmFormat,
                             handles.data(),
                             strides.data(),
                             offsets.data(),
                             &fbId,
                             0);
        }
      }
      if (rc != 0) throw std::system_error(errno, std::generic_category(), "drmModeAddFB2 overlay dmabuf");

      OverlayFramebuffer fb{
          .bufferId = candidate.bufferId,
          .format = candidate.drmFormat,
          .width = candidate.bufferWidth,
          .height = candidate.bufferHeight,
          .offsets = {},
          .strides = {},
          .modifiers = {},
          .fbId = fbId,
          .lastUsedSerial = ++overlayUseSerial_,
      };
      fb.offsets.reserve(planeCount);
      fb.strides.reserve(planeCount);
      fb.modifiers.reserve(planeCount);
      for (std::size_t i = 0; i < planeCount; ++i) {
        fb.offsets.push_back(offsets[i]);
        fb.strides.push_back(strides[i]);
        fb.modifiers.push_back(modifiers[i]);
      }
      overlayFramebuffers_.push_back(std::move(fb));
      for (std::uint32_t handle : handles) closeGemHandle(handle);
      closeOverlayCandidatePlaneFds(candidate);
      pruneOverlayFramebufferCache();
      return &overlayFramebuffers_.back();
    } catch (std::exception const& error) {
      for (std::uint32_t handle : handles) closeGemHandle(handle);
      closeOverlayCandidatePlaneFds(candidate);
      if (!overlayImportFailureLogged_) {
        std::fprintf(stderr,
                     "lambda-window-manager: KMS hardware overlay import failed for surface=%llu buffer=%llu: %s\n",
                     static_cast<unsigned long long>(candidate.surfaceId),
                     static_cast<unsigned long long>(candidate.bufferId),
                     error.what());
        overlayImportFailureLogged_ = true;
      }
      return nullptr;
    } catch (...) {
      for (std::uint32_t handle : handles) closeGemHandle(handle);
      closeOverlayCandidatePlaneFds(candidate);
      if (!overlayImportFailureLogged_) {
        std::fprintf(stderr,
                     "lambda-window-manager: KMS hardware overlay import failed for surface=%llu buffer=%llu\n",
                     static_cast<unsigned long long>(candidate.surfaceId),
                     static_cast<unsigned long long>(candidate.bufferId));
        overlayImportFailureLogged_ = true;
      }
      return nullptr;
    }
  }

  void destroyOverlayFramebuffer(OverlayFramebuffer& fb) noexcept {
    if (fb.fbId != 0) {
      drmModeRmFB(fd_, fb.fbId);
      fb.fbId = 0;
    }
  }

  void destroyOverlayFramebuffers() noexcept {
    for (auto& fb : overlayFramebuffers_) destroyOverlayFramebuffer(fb);
    overlayFramebuffers_.clear();
    activeOverlayFbId_ = 0;
    pendingOverlayFbId_ = 0;
    activeOverlayBufferId_ = 0;
    pendingOverlayBufferId_ = 0;
    activeDirectScanoutFbId_ = 0;
    pendingDirectScanoutFbId_ = 0;
    activeDirectScanoutBufferId_ = 0;
    pendingDirectScanoutBufferId_ = 0;
    activeDirectScanoutState_.reset();
    pendingDirectScanoutState_.reset();
    clearPreparedOverlayCandidate();
    clearPreparedDirectScanout();
  }

  void pruneOverlayFramebufferCache() noexcept {
    if (overlayFramebuffers_.size() <= kOverlayFramebufferCacheLimit) return;
    std::sort(overlayFramebuffers_.begin(),
              overlayFramebuffers_.end(),
              [](OverlayFramebuffer const& a, OverlayFramebuffer const& b) {
                return a.lastUsedSerial > b.lastUsedSerial;
              });
    for (std::size_t i = overlayFramebuffers_.size(); i > kOverlayFramebufferCacheLimit; --i) {
      OverlayFramebuffer& fb = overlayFramebuffers_[i - 1];
      if (fb.fbId == activeOverlayFbId_ || fb.fbId == pendingOverlayFbId_ ||
          fb.fbId == activeDirectScanoutFbId_ || fb.fbId == pendingDirectScanoutFbId_ ||
          (preparedDirectScanout_ && fb.fbId == preparedDirectScanout_->fbId) ||
          (preparedOverlay_ && fb.fbId == preparedOverlay_->fbId)) {
        continue;
      }
      destroyOverlayFramebuffer(fb);
      overlayFramebuffers_.erase(overlayFramebuffers_.begin() + static_cast<std::ptrdiff_t>(i - 1));
    }
  }

  bool overlayValidationRejected(OverlayValidationKey const& key) const noexcept {
    return std::find(rejectedOverlayValidationKeys_.begin(), rejectedOverlayValidationKeys_.end(), key) !=
           rejectedOverlayValidationKeys_.end();
  }

  void rememberRejectedOverlayValidation(OverlayValidationKey const& key) {
    if (overlayValidationRejected(key)) return;
    rejectedOverlayValidationKeys_.push_back(key);
    if (rejectedOverlayValidationKeys_.size() > kOverlayValidationRejectCacheLimit) {
      rejectedOverlayValidationKeys_.erase(rejectedOverlayValidationKeys_.begin());
    }
  }

  void addPlaneProperties(drmModeAtomicReq* request,
                          std::uint32_t planeId,
                          PlaneProperties const& props,
                          std::uint32_t fbId,
                          std::uint32_t crtcId,
                          std::uint64_t srcX,
                          std::uint64_t srcY,
                          std::uint64_t srcW,
                          std::uint64_t srcH,
                          std::int32_t crtcX,
                          std::int32_t crtcY,
                          std::uint32_t crtcW,
                          std::uint32_t crtcH,
                          int inFenceFd,
                          char const* label,
                          std::optional<std::uint64_t> zpos = std::nullopt) {
    addAtomicProperty(request, planeId, props.fbId, fbId, label);
    addAtomicProperty(request, planeId, props.crtcId, crtcId, label);
    addAtomicProperty(request, planeId, props.srcX, srcX, label);
    addAtomicProperty(request, planeId, props.srcY, srcY, label);
    addAtomicProperty(request, planeId, props.srcW, srcW, label);
    addAtomicProperty(request, planeId, props.srcH, srcH, label);
    addAtomicProperty(request, planeId, props.crtcX, static_cast<std::uint64_t>(static_cast<std::int64_t>(crtcX)), label);
    addAtomicProperty(request, planeId, props.crtcY, static_cast<std::uint64_t>(static_cast<std::int64_t>(crtcY)), label);
    addAtomicProperty(request, planeId, props.crtcW, crtcW, label);
    addAtomicProperty(request, planeId, props.crtcH, crtcH, label);
    if (inFenceFd >= 0) {
      addAtomicProperty(request, planeId, props.inFenceFd, static_cast<std::uint64_t>(inFenceFd), label);
    }
    if (fbId != 0) {
      addOptionalAtomicProperty(request, planeId, props.alpha, 65535);
      addOptionalAtomicProperty(request, planeId, props.pixelBlendMode, 2);
      addOptionalAtomicProperty(request, planeId, props.rotation, 1);
      if (zpos) addOptionalAtomicProperty(request, planeId, props.zpos, *zpos);
    }
  }

  void disableOverlayPlane(drmModeAtomicReq* request, std::uint32_t planeId, PlaneProperties const& props) {
    addPlaneProperties(request, planeId, props, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, "overlay.disable");
  }

  std::uint32_t createDamageClipBlob(Buffer const& buffer) {
    if (primaryPlaneProps_.fbDamageClips == 0 || buffer.damageRects.empty()) return 0;
    std::vector<drm_mode_rect> clips;
    clips.reserve(buffer.damageRects.size());
    for (KmsAtomicPresenter::DamageRect const& rect : buffer.damageRects) {
      std::int32_t const x1 = std::clamp(rect.x, 0, static_cast<std::int32_t>(connector_.mode.hdisplay));
      std::int32_t const y1 = std::clamp(rect.y, 0, static_cast<std::int32_t>(connector_.mode.vdisplay));
      std::int64_t const rectRight = static_cast<std::int64_t>(rect.x) + static_cast<std::int64_t>(rect.width);
      std::int64_t const rectBottom = static_cast<std::int64_t>(rect.y) + static_cast<std::int64_t>(rect.height);
      std::int32_t const x2 = static_cast<std::int32_t>(
          std::clamp<std::int64_t>(rectRight, 0, connector_.mode.hdisplay));
      std::int32_t const y2 = static_cast<std::int32_t>(
          std::clamp<std::int64_t>(rectBottom, 0, connector_.mode.vdisplay));
      if (x2 <= x1 || y2 <= y1) continue;
      clips.push_back(drm_mode_rect{.x1 = x1, .y1 = y1, .x2 = x2, .y2 = y2});
    }
    if (clips.empty()) return 0;
    std::uint32_t blob = 0;
    if (drmModeCreatePropertyBlob(fd_,
                                  clips.data(),
                                  static_cast<std::uint32_t>(clips.size() * sizeof(drm_mode_rect)),
                                  &blob) != 0) {
      if (!damageClipFailureLogged_) {
        std::fprintf(stderr,
                     "lambda-window-manager: failed to create FB_DAMAGE_CLIPS blob: %s\n",
                     std::strerror(errno));
        damageClipFailureLogged_ = true;
      }
      return 0;
    }
    return blob;
  }

  void populateAtomicRequest(drmModeAtomicReq* request,
                             Buffer const& buffer,
                             bool useRenderFence,
                             PreparedOverlay const* overlay,
                             std::uint32_t primaryDamageBlob) {
    if (!modesetDone_) {
      addAtomicProperty(request, connector_.connectorId, connectorCrtcId_, connector_.crtcId, "connector.CRTC_ID");
      addAtomicProperty(request, connector_.crtcId, crtcModeId_, modeBlob_, "crtc.MODE_ID");
      addAtomicProperty(request, connector_.crtcId, crtcActive_, 1, "crtc.ACTIVE");
    }
    addPlaneProperties(request,
                       planeId_,
                       primaryPlaneProps_,
                       buffer.fbId,
                       connector_.crtcId,
                       0,
                       0,
                       static_cast<std::uint64_t>(connector_.mode.hdisplay) << 16u,
                       static_cast<std::uint64_t>(connector_.mode.vdisplay) << 16u,
                       0,
                       0,
                       connector_.mode.hdisplay,
                       connector_.mode.vdisplay,
                       useRenderFence ? buffer.renderFenceFd : -1,
                       "primary");
    if (primaryPlaneProps_.fbDamageClips != 0) {
      addAtomicProperty(request,
                        planeId_,
                        primaryPlaneProps_.fbDamageClips,
                        primaryDamageBlob,
                        "primary.FB_DAMAGE_CLIPS");
    }
    if (overlay) {
      addPlaneProperties(request,
                         overlay->planeId,
                         overlay->props,
                         overlay->fbId,
                         connector_.crtcId,
                         overlay->srcX,
                         overlay->srcY,
                         overlay->srcW,
                         overlay->srcH,
                         overlay->crtcX,
                         overlay->crtcY,
                         overlay->crtcW,
                         overlay->crtcH,
                         overlay->acquireFenceFd,
                         "overlay",
                         overlay->zpos);
      if (activeOverlayPlaneId_ != 0 && activeOverlayPlaneId_ != overlay->planeId) {
        if (auto props = overlayPlaneProperties(activeOverlayPlaneId_)) {
          disableOverlayPlane(request, activeOverlayPlaneId_, *props);
        }
      }
    } else if (activeOverlayPlaneId_ != 0) {
      if (auto props = overlayPlaneProperties(activeOverlayPlaneId_)) {
        disableOverlayPlane(request, activeOverlayPlaneId_, *props);
      }
    }
    if (output_) {
      (void)output_->addAtomicCursorProperties(request);
    }
  }

  void populateDirectScanoutRequest(drmModeAtomicReq* request, PreparedDirectScanout const& direct) {
    if (!modesetDone_) {
      addAtomicProperty(request, connector_.connectorId, connectorCrtcId_, connector_.crtcId, "connector.CRTC_ID");
      addAtomicProperty(request, connector_.crtcId, crtcModeId_, modeBlob_, "crtc.MODE_ID");
      addAtomicProperty(request, connector_.crtcId, crtcActive_, 1, "crtc.ACTIVE");
    }
    addPlaneProperties(request,
                       planeId_,
                       primaryPlaneProps_,
                       direct.fbId,
                       connector_.crtcId,
                       direct.srcX,
                       direct.srcY,
                       direct.srcW,
                       direct.srcH,
                       direct.crtcX,
                       direct.crtcY,
                       direct.crtcW,
                       direct.crtcH,
                       direct.acquireFenceFd,
                       "primary.direct-scanout");
    if (activeOverlayPlaneId_ != 0) {
      if (auto props = overlayPlaneProperties(activeOverlayPlaneId_)) {
        disableOverlayPlane(request, activeOverlayPlaneId_, *props);
      }
    }
    if (output_) {
      (void)output_->addAtomicCursorProperties(request);
    }
  }

  bool testAtomicOverlay(Buffer const& buffer, PreparedOverlay const& overlay) {
    KmsTraceScope trace(KmsTraceBucket::TestAtomic);
    drmModeAtomicReq* request = reusableAtomicRequest();
    if (!request) return false;
    bool accepted = false;
    try {
      populateAtomicRequest(request, buffer, false, &overlay, 0);
      std::uint32_t flags = DRM_MODE_ATOMIC_TEST_ONLY;
      if (!modesetDone_) flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
      int const rc = drmModeAtomicCommit(fd_, request, flags, nullptr);
      if (rc == 0) {
        accepted = true;
      } else if (!overlayTestFailureLogged_) {
        std::fprintf(stderr,
                     "lambda-window-manager: KMS hardware overlay atomic test failed plane=%u fb=%u: %s\n",
                     overlay.planeId,
                     overlay.fbId,
                     std::strerror(errno));
      }
    } catch (std::exception const& error) {
      if (!overlayTestFailureLogged_) {
        std::fprintf(stderr,
                     "lambda-window-manager: KMS hardware overlay atomic test exception plane=%u fb=%u: %s\n",
                     overlay.planeId,
                     overlay.fbId,
                     error.what());
      }
      accepted = false;
    } catch (...) {
      if (!overlayTestFailureLogged_) {
        std::fprintf(stderr,
                     "lambda-window-manager: KMS hardware overlay atomic test exception plane=%u fb=%u\n",
                     overlay.planeId,
                     overlay.fbId);
      }
      accepted = false;
    }
    return accepted;
  }

  bool testAtomicDirectScanout(PreparedDirectScanout const& direct) {
    KmsTraceScope trace(KmsTraceBucket::TestAtomic);
    drmModeAtomicReq* request = reusableAtomicRequest();
    if (!request) return false;
    bool accepted = false;
    try {
      populateDirectScanoutRequest(request, direct);
      std::uint32_t flags = DRM_MODE_ATOMIC_TEST_ONLY;
      if (!modesetDone_) flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
      int const rc = drmModeAtomicCommit(fd_, request, flags, nullptr);
      if (rc == 0) {
        accepted = true;
      } else if (!directScanoutTestFailureLogged_) {
        std::fprintf(stderr,
                     "lambda-window-manager: KMS direct scanout atomic test failed fb=%u: %s\n",
                     direct.fbId,
                     std::strerror(errno));
        directScanoutTestFailureLogged_ = true;
      }
    } catch (std::exception const& error) {
      if (!directScanoutTestFailureLogged_) {
        std::fprintf(stderr,
                     "lambda-window-manager: KMS direct scanout atomic test exception fb=%u: %s\n",
                     direct.fbId,
                     error.what());
        directScanoutTestFailureLogged_ = true;
      }
      accepted = false;
    } catch (...) {
      if (!directScanoutTestFailureLogged_) {
        std::fprintf(stderr,
                     "lambda-window-manager: KMS direct scanout atomic test exception fb=%u\n",
                     direct.fbId);
        directScanoutTestFailureLogged_ = true;
      }
      accepted = false;
    }
    return accepted;
  }

  std::optional<PlaneProperties> overlayPlaneProperties(std::uint32_t planeId) const noexcept {
    auto it = std::find_if(overlayPlanes_.begin(), overlayPlanes_.end(), [planeId](OverlayPlane const& plane) {
      return plane.id == planeId;
    });
    if (it == overlayPlanes_.end()) return std::nullopt;
    return it->props;
  }

  PlaneCapabilities loadPlaneCapabilities(std::uint32_t planeId) const {
    std::uint32_t const zposProperty = propertyId(fd_, planeId, DRM_MODE_OBJECT_PLANE, "zpos");
    PlaneCapabilities caps{
        .possibleCrtcs = 0,
        .type = propertyValue(fd_, planeId, DRM_MODE_OBJECT_PLANE, "type"),
        .hasZpos = zposProperty != 0,
        .zpos = zposProperty != 0 ? propertyValue(fd_, planeId, DRM_MODE_OBJECT_PLANE, "zpos") : 0,
        .formats = {},
    };
    drmModePlane* plane = drmModeGetPlane(fd_, planeId);
    if (!plane) return caps;
    caps.possibleCrtcs = plane->possible_crtcs;
    caps.formats.reserve(plane->count_formats);
    for (std::uint32_t i = 0; i < plane->count_formats; ++i) {
      std::uint32_t const format = plane->formats[i];
      if (std::find_if(caps.formats.begin(), caps.formats.end(), [format](PlaneFormatSupport const& support) {
            return support.format == format;
          }) != caps.formats.end()) {
        continue;
      }
      caps.formats.push_back(PlaneFormatSupport{
          .format = format,
          .modifiers = planeModifiersForFormat(fd_, planeId, format),
      });
    }
    drmModeFreePlane(plane);
    return caps;
  }

  static bool planeCapabilityListsFormat(PlaneCapabilities const& caps, std::uint32_t drmFormat) noexcept {
    return std::find_if(caps.formats.begin(), caps.formats.end(), [drmFormat](PlaneFormatSupport const& support) {
             return support.format == drmFormat;
           }) != caps.formats.end();
  }

  static bool planeCapabilitySupportsFormatModifier(PlaneCapabilities const& caps,
                                                    std::uint32_t drmFormat,
                                                    std::uint64_t modifier) noexcept {
    auto const formatIt = std::find_if(caps.formats.begin(),
                                       caps.formats.end(),
                                       [drmFormat](PlaneFormatSupport const& support) {
                                         return support.format == drmFormat;
                                       });
    if (formatIt == caps.formats.end()) return false;
    if (formatIt->modifiers.empty()) return true;
    return containsModifier(formatIt->modifiers, normalizedModifier(modifier));
  }

  char const* overlayStaticRejectReason(std::uint32_t drmFormat, std::uint64_t modifier) const noexcept {
    if (overlayPlanes_.empty()) return "no-overlay-plane";
    bool anyFormat = false;
    for (OverlayPlane const& plane : overlayPlanes_) {
      anyFormat = anyFormat || planeCapabilityListsFormat(plane.caps, drmFormat);
      if (planeCapabilitySupportsFormatModifier(plane.caps, drmFormat, modifier)) return nullptr;
    }
    return anyFormat ? "modifier-unsupported" : "format-unsupported";
  }

  PlaneProperties loadPlaneProperties(std::uint32_t planeId) const {
    return PlaneProperties{
        .fbId = propertyId(fd_, planeId, DRM_MODE_OBJECT_PLANE, "FB_ID"),
        .crtcId = propertyId(fd_, planeId, DRM_MODE_OBJECT_PLANE, "CRTC_ID"),
        .srcX = propertyId(fd_, planeId, DRM_MODE_OBJECT_PLANE, "SRC_X"),
        .srcY = propertyId(fd_, planeId, DRM_MODE_OBJECT_PLANE, "SRC_Y"),
        .srcW = propertyId(fd_, planeId, DRM_MODE_OBJECT_PLANE, "SRC_W"),
        .srcH = propertyId(fd_, planeId, DRM_MODE_OBJECT_PLANE, "SRC_H"),
        .crtcX = propertyId(fd_, planeId, DRM_MODE_OBJECT_PLANE, "CRTC_X"),
        .crtcY = propertyId(fd_, planeId, DRM_MODE_OBJECT_PLANE, "CRTC_Y"),
        .crtcW = propertyId(fd_, planeId, DRM_MODE_OBJECT_PLANE, "CRTC_W"),
        .crtcH = propertyId(fd_, planeId, DRM_MODE_OBJECT_PLANE, "CRTC_H"),
        .inFenceFd = propertyId(fd_, planeId, DRM_MODE_OBJECT_PLANE, "IN_FENCE_FD"),
        .fbDamageClips = propertyId(fd_, planeId, DRM_MODE_OBJECT_PLANE, "FB_DAMAGE_CLIPS"),
        .alpha = propertyId(fd_, planeId, DRM_MODE_OBJECT_PLANE, "alpha"),
        .pixelBlendMode = propertyId(fd_, planeId, DRM_MODE_OBJECT_PLANE, "pixel blend mode"),
        .rotation = propertyId(fd_, planeId, DRM_MODE_OBJECT_PLANE, "rotation"),
        .zpos = propertyId(fd_, planeId, DRM_MODE_OBJECT_PLANE, "zpos"),
    };
  }

  void loadProperties() {
    connectorCrtcId_ = propertyId(fd_, connector_.connectorId, DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID");
    crtcModeId_ = propertyId(fd_, connector_.crtcId, DRM_MODE_OBJECT_CRTC, "MODE_ID");
    crtcActive_ = propertyId(fd_, connector_.crtcId, DRM_MODE_OBJECT_CRTC, "ACTIVE");
    primaryPlaneProps_ = loadPlaneProperties(planeId_);
    primaryPlaneCaps_ = loadPlaneCapabilities(planeId_);
    planeInFenceFd_ = primaryPlaneProps_.inFenceFd;
    if (planeInFenceFd_ == 0) {
      std::fprintf(stderr,
                   "lambda-window-manager: KMS primary plane has no IN_FENCE_FD; atomic presenter will use CPU GPU waits\n");
    }
    bool const overlayPlanesEnabled = enableKmsOverlayPlanes();
    if (!overlayPlanesEnabled) {
      std::fprintf(stderr,
                   "lambda-window-manager: KMS hardware overlay planes disabled by default; set "
                   "LAMBDA_COMPOSITOR_ENABLE_KMS_OVERLAYS=1 to opt in\n");
    }
    std::vector<std::uint32_t> const overlayIds =
        overlayPlanesEnabled ? planesForCrtcWithType(fd_, connector_.crtcId, DRM_PLANE_TYPE_OVERLAY)
                             : std::vector<std::uint32_t>{};
    overlayPlanes_.reserve(overlayIds.size());
    std::optional<std::uint64_t> cursorPlaneZpos;
    for (std::uint32_t cursorId : planesForCrtcWithType(fd_, connector_.crtcId, DRM_PLANE_TYPE_CURSOR)) {
      PlaneCapabilities const caps = loadPlaneCapabilities(cursorId);
      if (!caps.hasZpos) continue;
      cursorPlaneZpos = cursorPlaneZpos ? std::max(*cursorPlaneZpos, caps.zpos) : caps.zpos;
    }
    cursorPlaneZpos_ = output_ && output_->atomicCursorZpos_ ? output_->atomicCursorZpos_ : cursorPlaneZpos;
    for (std::uint32_t overlayId : overlayIds) {
      OverlayPlane plane{
          .id = overlayId,
          .props = loadPlaneProperties(overlayId),
          .caps = loadPlaneCapabilities(overlayId),
      };
      if (cursorPlaneZpos_ && !cursorSafeOverlayZpos(plane.caps)) {
        continue;
      }
      overlayPlanes_.push_back(std::move(plane));
    }
    std::sort(overlayPlanes_.begin(), overlayPlanes_.end(), [](OverlayPlane const& a, OverlayPlane const& b) {
      if (a.caps.hasZpos != b.caps.hasZpos) return a.caps.hasZpos;
      if (a.caps.hasZpos && b.caps.hasZpos && a.caps.zpos != b.caps.zpos) return a.caps.zpos < b.caps.zpos;
      return a.id < b.id;
    });
    std::fprintf(stderr,
                 "lambda-window-manager: KMS hardware overlay planes available=%zu primaryFormats=%zu cursorZpos=%s%llu\n",
                 overlayPlanes_.size(),
                 primaryPlaneCaps_.formats.size(),
                 cursorPlaneZpos_ ? "" : "unknown:",
                 static_cast<unsigned long long>(cursorPlaneZpos_.value_or(0)));
    for (OverlayPlane const& plane : overlayPlanes_) {
      std::fprintf(stderr,
                   "lambda-window-manager: KMS overlay plane=%u formats=%zu zpos=%s%llu\n",
                   plane.id,
                   plane.caps.formats.size(),
                   plane.caps.hasZpos ? "" : "unknown:",
                   static_cast<unsigned long long>(plane.caps.zpos));
    }
  }

  void createCommandPool() {
    auto poolInfo = vkStructure<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = lambda::VulkanContext::instance().queueFamily();
    vkCheck(vkCreateCommandPool(lambda::VulkanContext::instance().device(), &poolInfo, nullptr, &commandPool_),
            "vkCreateCommandPool atomic KMS presenter");
  }

  void createModeBlob() {
    if (drmModeCreatePropertyBlob(fd_, &connector_.mode, sizeof(connector_.mode), &modeBlob_) != 0) {
      throw std::system_error(errno, std::generic_category(), "drmModeCreatePropertyBlob");
    }
  }

  void createBuffers() {
    buffers_.reserve(kBufferCount);
    for (std::size_t i = 0; i < kBufferCount; ++i) {
      buffers_.push_back(createBuffer());
    }
  }

  void destroyBuffers() {
    VkDevice device = lambda::VulkanContext::instance().device();
    // Intentional output-buffer teardown wait before freeing scanout images.
    if (device) vkDeviceWaitIdle(device);
    for (auto& buffer : buffers_) {
      closeRenderFence(buffer);
      if (buffer.renderFinished) vkDestroySemaphore(device, buffer.renderFinished, nullptr);
      if (buffer.renderFence) vkDestroyFence(device, buffer.renderFence, nullptr);
      if (buffer.offscreenView) vkDestroyImageView(device, buffer.offscreenView, nullptr);
      if (buffer.offscreenImage) vkDestroyImage(device, buffer.offscreenImage, nullptr);
      if (buffer.offscreenMemory) vkFreeMemory(device, buffer.offscreenMemory, nullptr);
      if (buffer.view) vkDestroyImageView(device, buffer.view, nullptr);
      if (buffer.image) vkDestroyImage(device, buffer.image, nullptr);
      if (buffer.memory) vkFreeMemory(device, buffer.memory, nullptr);
      if (buffer.fbId) drmModeRmFB(fd_, buffer.fbId);
      if (buffer.bo) gbm_bo_destroy(buffer.bo);
    }
    buffers_.clear();
    displayedBuffer_ = -1;
    pendingBuffer_ = -1;
    renderBuffer_ = -1;
  }

  Buffer createBuffer() {
    Buffer buffer{};
    std::vector<std::uint64_t> const modifiers = preferredScanoutModifiers();
    if (!modifiers.empty()) {
      buffer.bo = gbm_bo_create_with_modifiers2(gbm_,
                                                connector_.mode.hdisplay,
                                                connector_.mode.vdisplay,
                                                GBM_FORMAT_ARGB8888,
                                                modifiers.data(),
                                                static_cast<unsigned int>(modifiers.size()),
                                                GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT);
    }
    if (!buffer.bo && !forceLinearScanout()) {
      buffer.bo = gbm_bo_create(gbm_,
                                connector_.mode.hdisplay,
                                connector_.mode.vdisplay,
                                GBM_FORMAT_ARGB8888,
                                GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT);
    }
    if (!buffer.bo) {
      constexpr std::uint64_t modifiers[] = {DRM_FORMAT_MOD_LINEAR};
      buffer.bo = gbm_bo_create_with_modifiers2(gbm_,
                                                connector_.mode.hdisplay,
                                                connector_.mode.vdisplay,
                                                GBM_FORMAT_ARGB8888,
                                                modifiers,
                                                1,
                                                GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT);
    }
    if (!buffer.bo) {
      buffer.bo = gbm_bo_create(gbm_,
                                connector_.mode.hdisplay,
                                connector_.mode.vdisplay,
                                GBM_FORMAT_ARGB8888,
                                GBM_BO_USE_RENDERING | GBM_BO_USE_SCANOUT | GBM_BO_USE_LINEAR);
    }
    if (!buffer.bo) throw std::runtime_error("gbm_bo_create failed for KMS atomic scanout buffer");
    if (directScanoutRender_ && !directScanoutRenderForced_ && gbmModifier(buffer.bo) == DRM_FORMAT_MOD_LINEAR) {
      destroyPartialBuffer(buffer);
      throw std::runtime_error("automatic direct scanout selected a linear buffer");
    }
    try {
      buffer.fbId = createFramebuffer(buffer.bo);
      importBufferToVulkan(buffer);
      if (!directScanoutRender_) {
        createOffscreenTarget(buffer);
      }
      allocateCommandBuffer(buffer);
      createRenderFence(buffer);
      if (useAsyncRenderFence_) {
        buffer.renderFinished = createExportableSemaphore();
      }
      VkImage renderImage = directScanoutRender_ ? buffer.image : buffer.offscreenImage;
      VkImageView renderView = directScanoutRender_ ? buffer.view : buffer.offscreenView;
      buffer.spec = VulkanRenderTargetSpec{
          .image = renderImage,
          .view = renderView,
          .format = VK_FORMAT_B8G8R8A8_UNORM,
          .width = connector_.mode.hdisplay,
          .height = connector_.mode.vdisplay,
          .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
          .finalLayout = directScanoutRender_ ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          .preserveContents = false,
          .commandBuffer = buffer.commandBuffer,
          .completionFence = buffer.renderFence,
      };
      std::fprintf(stderr,
                   "lambda-window-manager: atomic scanout buffer %ux%u modifier=0x%016llx stride=%u\n",
                   gbm_bo_get_width(buffer.bo),
                   gbm_bo_get_height(buffer.bo),
                   static_cast<unsigned long long>(gbmModifier(buffer.bo)),
                   gbm_bo_get_stride_for_plane(buffer.bo, 0));
    } catch (...) {
      destroyPartialBuffer(buffer);
      throw;
    }
    return buffer;
  }

  static VkImageUsageFlags directScanoutImageUsage() noexcept {
    return VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
           VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  }

  static VkImageUsageFlags copyScanoutImageUsage() noexcept {
    return VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  }

  VkImageUsageFlags scanoutImageUsage() const noexcept {
    return directScanoutRender_ ? directScanoutImageUsage() : copyScanoutImageUsage();
  }

  bool shouldUseAutomaticDirectScanoutRender() const {
    if (disableAutomaticDirectScanoutRender()) return false;
    auto const modifiers = scanoutModifiersForUsage(fd_,
                                                    planeId_,
                                                    DRM_FORMAT_ARGB8888,
                                                    VK_FORMAT_B8G8R8A8_UNORM,
                                                    lambda::VulkanContext::instance().physicalDevice(),
                                                    directScanoutImageUsage(),
                                                    false);
    if (modifiers.empty()) return false;
    std::fprintf(stderr,
                 "lambda-window-manager: automatic direct scanout candidate modifier=0x%016llx count=%zu\n",
                 static_cast<unsigned long long>(modifiers.front()),
                 modifiers.size());
    return true;
  }

  std::vector<std::uint64_t> preferredScanoutModifiers() const {
    if (forceLinearScanout()) return {DRM_FORMAT_MOD_LINEAR};
    std::vector<std::uint64_t> const planeModifiers = planeModifiersForFormat(fd_, planeId_, DRM_FORMAT_ARGB8888);
    std::vector<std::uint64_t> const vulkanModifiers = vulkanModifiersForFormat(
        lambda::VulkanContext::instance().physicalDevice(), VK_FORMAT_B8G8R8A8_UNORM, scanoutImageUsage());
    std::vector<std::uint64_t> const modifiers =
        scanoutModifiersForUsage(fd_,
                                 planeId_,
                                 DRM_FORMAT_ARGB8888,
                                 VK_FORMAT_B8G8R8A8_UNORM,
                                 lambda::VulkanContext::instance().physicalDevice(),
                                 scanoutImageUsage(),
                                 true);
    if (!modifiers.empty()) {
      std::fprintf(stderr,
                   "lambda-window-manager: atomic scanout modifiers: plane=%zu vulkan=%zu selected=%zu first=0x%016llx%s\n",
                   planeModifiers.size(),
                   vulkanModifiers.size(),
                   modifiers.size(),
                   static_cast<unsigned long long>(modifiers.front()),
                   modifiers.front() == DRM_FORMAT_MOD_LINEAR ? " linear" : "");
    }
    return modifiers;
  }

  void createOffscreenTarget(Buffer& buffer) {
    VkDevice device = lambda::VulkanContext::instance().device();
    VkPhysicalDevice physical = lambda::VulkanContext::instance().physicalDevice();
    auto imageInfo = vkStructure<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
    imageInfo.extent = {connector_.mode.hdisplay, connector_.mode.vdisplay, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCheck(vkCreateImage(device, &imageInfo, nullptr, &buffer.offscreenImage),
            "vkCreateImage atomic KMS offscreen target");

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device, buffer.offscreenImage, &requirements);
    auto allocateInfo = vkStructure<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex =
        findVulkanMemoryType(physical, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkCheck(vkAllocateMemory(device, &allocateInfo, nullptr, &buffer.offscreenMemory),
            "vkAllocateMemory atomic KMS offscreen target");
    vkCheck(vkBindImageMemory(device, buffer.offscreenImage, buffer.offscreenMemory, 0),
            "vkBindImageMemory atomic KMS offscreen target");

    auto viewInfo = vkStructure<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    viewInfo.image = buffer.offscreenImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    vkCheck(vkCreateImageView(device, &viewInfo, nullptr, &buffer.offscreenView),
            "vkCreateImageView atomic KMS offscreen target");
  }

  void allocateCommandBuffer(Buffer& buffer) {
    auto allocateInfo = vkStructure<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
    allocateInfo.commandPool = commandPool_;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    vkCheck(vkAllocateCommandBuffers(lambda::VulkanContext::instance().device(), &allocateInfo, &buffer.commandBuffer),
            "vkAllocateCommandBuffers atomic KMS presenter");
  }

  void createRenderFence(Buffer& buffer) {
    auto fenceInfo = vkStructure<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCheck(vkCreateFence(lambda::VulkanContext::instance().device(), &fenceInfo, nullptr, &buffer.renderFence),
            "vkCreateFence atomic KMS render fence");
  }

  static void transitionImage(VkCommandBuffer commandBuffer,
                              VkImage image,
                              VkImageLayout oldLayout,
                              VkImageLayout newLayout,
                              VkPipelineStageFlags2 srcStage,
                              VkPipelineStageFlags2 dstStage,
                              VkAccessFlags2 srcAccess,
                              VkAccessFlags2 dstAccess) {
    auto barrier = vkStructure<VkImageMemoryBarrier2>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2);
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    auto dependency = vkStructure<VkDependencyInfo>(VK_STRUCTURE_TYPE_DEPENDENCY_INFO);
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
  }

  void beginRenderCommandBuffer(Buffer& buffer) {
    KmsTraceScope trace(KmsTraceBucket::BeginCommand);
    if (!buffer.commandBuffer) return;
    vkCheck(vkResetCommandBuffer(buffer.commandBuffer, 0), "vkResetCommandBuffer atomic KMS presenter");
    auto beginInfo = vkStructure<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(buffer.commandBuffer, &beginInfo), "vkBeginCommandBuffer atomic KMS presenter");
    buffer.commandBufferRecording = true;
  }

  void finishRenderCommandBuffer(Buffer& buffer) {
    KmsTraceScope trace(KmsTraceBucket::FinishCommand);
    if (!buffer.commandBuffer || !buffer.commandBufferRecording) return;
    if (!directScanoutRender_) {
      bool const regionCopy = buffer.partialFrame &&
                              buffer.primaryContentsValid &&
                              !buffer.scanoutCopyRects.empty();
      transitionImage(buffer.commandBuffer,
                      buffer.image,
                      regionCopy ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      regionCopy ? VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_2_NONE,
                      VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                      regionCopy ? VK_ACCESS_2_MEMORY_READ_BIT : VK_ACCESS_2_NONE,
                      VK_ACCESS_2_TRANSFER_WRITE_BIT);
      std::vector<VkImageCopy> copies;
      if (regionCopy) {
        copies.reserve(buffer.scanoutCopyRects.size());
        for (KmsAtomicPresenter::DamageRect const& rect : buffer.scanoutCopyRects) {
          std::optional<KmsAtomicPresenter::DamageRect> clipped = clippedDamageRect(rect);
          if (!clipped) continue;
          VkImageCopy copy{};
          copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
          copy.srcSubresource.layerCount = 1;
          copy.srcOffset = {clipped->x, clipped->y, 0};
          copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
          copy.dstSubresource.layerCount = 1;
          copy.dstOffset = {clipped->x, clipped->y, 0};
          copy.extent = {clipped->width, clipped->height, 1};
          copies.push_back(copy);
        }
      }
      if (copies.empty()) {
        VkImageCopy copy{};
        copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.srcSubresource.layerCount = 1;
        copy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.dstSubresource.layerCount = 1;
        copy.extent = {connector_.mode.hdisplay, connector_.mode.vdisplay, 1};
        copies.push_back(copy);
      }
      if (kmsPresentTraceEnabled()) {
        bool const fullCopy = !regionCopy ||
                              (copies.size() == 1u && copyPixels(copies.front()) == fullScanoutPixels());
        recordKmsCopyTrace(KmsCopyTraceKind::Scanout,
                           fullCopy,
                           copies.size(),
                           copyPixels(copies),
                           fullScanoutPixels());
      }
      vkCmdCopyImage(buffer.commandBuffer,
                     buffer.offscreenImage,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     buffer.image,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     static_cast<std::uint32_t>(copies.size()),
                     copies.data());
      transitionImage(buffer.commandBuffer,
                      buffer.image,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_IMAGE_LAYOUT_GENERAL,
                      VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                      VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                      VK_ACCESS_2_TRANSFER_WRITE_BIT,
                      VK_ACCESS_2_MEMORY_READ_BIT);
    }
    vkCheck(vkEndCommandBuffer(buffer.commandBuffer), "vkEndCommandBuffer atomic KMS presenter");
    buffer.commandBufferRecording = false;

    auto commandBufferInfo = vkStructure<VkCommandBufferSubmitInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO);
    commandBufferInfo.commandBuffer = buffer.commandBuffer;
    auto signalSemaphore = vkStructure<VkSemaphoreSubmitInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO);
    signalSemaphore.semaphore = buffer.renderFinished;
    signalSemaphore.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    auto submit = vkStructure<VkSubmitInfo2>(VK_STRUCTURE_TYPE_SUBMIT_INFO_2);
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &commandBufferInfo;
    submit.signalSemaphoreInfoCount = buffer.renderFinished ? 1u : 0u;
    submit.pSignalSemaphoreInfos = buffer.renderFinished ? &signalSemaphore : nullptr;
    if (!buffer.renderFence) createRenderFence(buffer);
    vkCheck(vkResetFences(lambda::VulkanContext::instance().device(), 1, &buffer.renderFence),
            "vkResetFences atomic KMS render fence");
    buffer.renderComplete = false;
    std::uint64_t const submitStart = kmsTraceStart();
    try {
      vkCheck(vkQueueSubmit2(lambda::VulkanContext::instance().queue(), 1, &submit, buffer.renderFence),
              "vkQueueSubmit2 atomic KMS presenter");
    } catch (...) {
      throw;
    }
    lambda::markVulkanRenderTargetCanvasSubmitted(canvas_.get());
    recordKmsTraceSince(KmsTraceBucket::QueueSubmit, submitStart);
    if (!buffer.renderFinished) {
      if (!renderFenceFallbackLogged_) {
        std::fprintf(stderr,
                     "lambda-window-manager: KMS render semaphore missing; waiting on per-frame fence\n");
        renderFenceFallbackLogged_ = true;
      }
      VkResult const waitResult =
          vkWaitForFences(lambda::VulkanContext::instance().device(), 1, &buffer.renderFence, VK_TRUE, UINT64_MAX);
      vkCheck(waitResult, "vkWaitForFences atomic KMS render fallback");
      markRenderWorkComplete(buffer);
    }
  }

  VkSemaphore createExportableSemaphore() {
    VkDevice device = lambda::VulkanContext::instance().device();
    auto exportInfo = vkStructure<VkExportSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO);
    exportInfo.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
    auto semaphoreInfo = vkStructure<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
    semaphoreInfo.pNext = &exportInfo;
    VkSemaphore semaphore = VK_NULL_HANDLE;
    vkCheck(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore),
            "vkCreateSemaphore atomic KMS render fence");
    return semaphore;
  }

  int exportRenderSemaphoreFd(VkSemaphore semaphore) {
    KmsTraceScope trace(KmsTraceBucket::ExportFence);
    auto getSemaphoreFd =
        reinterpret_cast<PFN_vkGetSemaphoreFdKHR>(vkGetDeviceProcAddr(lambda::VulkanContext::instance().device(),
                                                                       "vkGetSemaphoreFdKHR"));
    if (!getSemaphoreFd) throw std::runtime_error("vkGetSemaphoreFdKHR is unavailable");
    auto fdInfo = vkStructure<VkSemaphoreGetFdInfoKHR>(VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR);
    fdInfo.semaphore = semaphore;
    fdInfo.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
    int fd = -1;
    vkCheck(getSemaphoreFd(lambda::VulkanContext::instance().device(), &fdInfo, &fd),
            "vkGetSemaphoreFdKHR atomic KMS render fence");
    return fd;
  }

  std::uint32_t createFramebuffer(gbm_bo* bo) {
    std::array<std::uint32_t, 4> handles{};
    std::array<std::uint32_t, 4> strides{};
    std::array<std::uint32_t, 4> offsets{};
    std::array<std::uint64_t, 4> modifiers{};
    handles[0] = gbm_bo_get_handle_for_plane(bo, 0).u32;
    strides[0] = gbm_bo_get_stride_for_plane(bo, 0);
    offsets[0] = gbm_bo_get_offset(bo, 0);
    modifiers[0] = gbmModifier(bo);
    std::uint32_t fb = 0;
    int rc = drmModeAddFB2WithModifiers(fd_,
                                        gbm_bo_get_width(bo),
                                        gbm_bo_get_height(bo),
                                        DRM_FORMAT_ARGB8888,
                                        handles.data(),
                                        strides.data(),
                                        offsets.data(),
                                        modifiers.data(),
                                        &fb,
                                        DRM_MODE_FB_MODIFIERS);
    if (rc != 0) {
      rc = drmModeAddFB2(fd_,
                         gbm_bo_get_width(bo),
                         gbm_bo_get_height(bo),
                         DRM_FORMAT_ARGB8888,
                         handles.data(),
                         strides.data(),
                         offsets.data(),
                         &fb,
                         0);
    }
    if (rc != 0) throw std::system_error(errno, std::generic_category(), "drmModeAddFB2");
    return fb;
  }

  void importBufferToVulkan(Buffer& buffer) {
    int fd = gbm_bo_get_fd(buffer.bo);
    if (fd < 0) throw std::system_error(errno, std::generic_category(), "gbm_bo_get_fd");
    VkDevice device = lambda::VulkanContext::instance().device();
    VkPhysicalDevice physical = lambda::VulkanContext::instance().physicalDevice();
    try {
      std::uint64_t const modifier = gbmModifier(buffer.bo);
      auto externalInfo =
          vkStructure<VkExternalMemoryImageCreateInfo>(VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO);
      externalInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
      VkSubresourceLayout planeLayout{};
      planeLayout.offset = gbm_bo_get_offset(buffer.bo, 0);
      planeLayout.rowPitch = gbm_bo_get_stride_for_plane(buffer.bo, 0);
      auto modifierInfo = vkStructure<VkImageDrmFormatModifierExplicitCreateInfoEXT>(
          VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT);
      modifierInfo.pNext = &externalInfo;
      modifierInfo.drmFormatModifier = modifier;
      modifierInfo.drmFormatModifierPlaneCount = 1;
      modifierInfo.pPlaneLayouts = &planeLayout;

      auto imageInfo = vkStructure<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
      imageInfo.pNext = &modifierInfo;
      imageInfo.imageType = VK_IMAGE_TYPE_2D;
      imageInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
      imageInfo.extent = {gbm_bo_get_width(buffer.bo), gbm_bo_get_height(buffer.bo), 1};
      imageInfo.mipLevels = 1;
      imageInfo.arrayLayers = 1;
      imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
      imageInfo.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
      imageInfo.usage = scanoutImageUsage();
      imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      vkCheck(vkCreateImage(device, &imageInfo, nullptr, &buffer.image), "vkCreateImage atomic KMS buffer");

      auto getMemoryFdProperties = reinterpret_cast<PFN_vkGetMemoryFdPropertiesKHR>(
          vkGetDeviceProcAddr(device, "vkGetMemoryFdPropertiesKHR"));
      if (!getMemoryFdProperties) throw std::runtime_error("vkGetMemoryFdPropertiesKHR is unavailable");
      auto fdProps = vkStructure<VkMemoryFdPropertiesKHR>(VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR);
      vkCheck(getMemoryFdProperties(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, fd, &fdProps),
              "vkGetMemoryFdPropertiesKHR atomic KMS buffer");
      VkMemoryRequirements requirements{};
      vkGetImageMemoryRequirements(device, buffer.image, &requirements);
      auto dedicated = vkStructure<VkMemoryDedicatedAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO);
      dedicated.image = buffer.image;
      auto importInfo = vkStructure<VkImportMemoryFdInfoKHR>(VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR);
      importInfo.pNext = &dedicated;
      importInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
      importInfo.fd = fd;
      auto allocateInfo = vkStructure<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
      allocateInfo.pNext = &importInfo;
      allocateInfo.allocationSize = requirements.size;
      allocateInfo.memoryTypeIndex =
          findVulkanMemoryType(physical, requirements.memoryTypeBits & fdProps.memoryTypeBits, 0);
      vkCheck(vkAllocateMemory(device, &allocateInfo, nullptr, &buffer.memory),
              "vkAllocateMemory atomic KMS buffer");
      fd = -1;
      vkCheck(vkBindImageMemory(device, buffer.image, buffer.memory, 0), "vkBindImageMemory atomic KMS buffer");
      if (directScanoutRender_) {
        auto viewInfo = vkStructure<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
        viewInfo.image = buffer.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_B8G8R8A8_UNORM;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        vkCheck(vkCreateImageView(device, &viewInfo, nullptr, &buffer.view),
                "vkCreateImageView atomic KMS buffer");
      }
    } catch (...) {
      if (fd >= 0) close(fd);
      throw;
    }
  }

  void waitForPageFlip() {
    while (pageFlipPending_) {
      pollfd pfd{.fd = fd_, .events = POLLIN, .revents = 0};
      int const rc = poll(&pfd, 1, 1000);
      if (rc < 0) {
        if (errno == EINTR) continue;
        throw std::system_error(errno, std::generic_category(), "poll KMS page flip");
      }
      if (rc == 0) throw std::runtime_error("KMS atomic page flip timed out");
      drmEventContext eventContext{};
      eventContext.version = DRM_EVENT_CONTEXT_VERSION;
      eventContext.page_flip_handler = pageFlipHandler;
      pendingTiming_.eventDispatchStartMonotonicNsec = monotonicNanoseconds();
      if (drmHandleEvent(fd_, &eventContext) != 0) {
        throw std::system_error(errno, std::generic_category(), "drmHandleEvent");
      }
      pendingTiming_.eventDispatchEndMonotonicNsec = monotonicNanoseconds();
      if (pendingTiming_.hardware) {
        if (pendingBuffer_ >= 0 && pendingBuffer_ < static_cast<int>(buffers_.size())) {
          updateRenderFenceReadiness(buffers_[static_cast<std::size_t>(pendingBuffer_)]);
        }
        displayedBuffer_ = pendingBuffer_;
        pendingBuffer_ = -1;
        activeOverlayPlaneId_ = pendingOverlayPlaneId_;
        activeOverlayFbId_ = pendingOverlayFbId_;
        activeOverlayBufferId_ = pendingOverlayBufferId_;
        activeDirectScanout_ = pendingDirectScanout_;
        activeDirectScanoutFbId_ = pendingDirectScanoutFbId_;
        activeDirectScanoutBufferId_ = pendingDirectScanoutBufferId_;
        activeDirectScanoutState_ = pendingDirectScanoutState_;
        if (activeDirectScanout_) {
          activeOverlayPlaneId_ = 0;
          activeOverlayFbId_ = 0;
          activeOverlayBufferId_ = 0;
          if (displayedBuffer_ >= 0 && displayedBuffer_ < static_cast<int>(buffers_.size())) {
            buffers_[static_cast<std::size_t>(displayedBuffer_)].primaryContentsValid = false;
          }
        } else {
          activeDirectScanoutState_.reset();
          if (displayedBuffer_ >= 0 && displayedBuffer_ < static_cast<int>(buffers_.size())) {
            buffers_[static_cast<std::size_t>(displayedBuffer_)].primaryContentsValid = true;
          }
        }
        pendingOverlayPlaneId_ = 0;
        pendingOverlayFbId_ = 0;
        pendingOverlayBufferId_ = 0;
        pendingDirectScanout_ = false;
        pendingDirectScanoutFbId_ = 0;
        pendingDirectScanoutBufferId_ = 0;
        pendingDirectScanoutState_.reset();
        pruneOverlayFramebufferCache();
        pageFlipPending_ = false;
        if (output_) output_->setAtomicPageFlipPending(false);
      }
    }
  }

  void destroyPartialBuffer(Buffer& buffer) {
    VkDevice device = lambda::VulkanContext::instance().device();
    closeRenderFence(buffer);
    if (buffer.view) vkDestroyImageView(device, buffer.view, nullptr);
    if (buffer.renderFinished) vkDestroySemaphore(device, buffer.renderFinished, nullptr);
    if (buffer.renderFence) vkDestroyFence(device, buffer.renderFence, nullptr);
    if (buffer.offscreenView) vkDestroyImageView(device, buffer.offscreenView, nullptr);
    if (buffer.offscreenImage) vkDestroyImage(device, buffer.offscreenImage, nullptr);
    if (buffer.offscreenMemory) vkFreeMemory(device, buffer.offscreenMemory, nullptr);
    if (buffer.image) vkDestroyImage(device, buffer.image, nullptr);
    if (buffer.memory) vkFreeMemory(device, buffer.memory, nullptr);
    if (buffer.fbId) drmModeRmFB(fd_, buffer.fbId);
    if (buffer.bo) gbm_bo_destroy(buffer.bo);
    buffer = {};
  }

  static constexpr std::size_t kBufferCount = 4;

  std::shared_ptr<KmsOutput::Impl> output_;
  int fd_ = -1;
  KmsConnector connector_{};
  TextSystem& textSystem_;
  gbm_device* gbm_ = nullptr;
  VkCommandPool commandPool_ = VK_NULL_HANDLE;
  std::uint32_t planeId_ = 0;
  std::uint32_t modeBlob_ = 0;
  std::uint32_t connectorCrtcId_ = 0;
  std::uint32_t crtcModeId_ = 0;
  std::uint32_t crtcActive_ = 0;
  std::uint32_t planeInFenceFd_ = 0;
  PlaneProperties primaryPlaneProps_{};
  PlaneCapabilities primaryPlaneCaps_{};
  std::optional<std::uint64_t> cursorPlaneZpos_;
  std::vector<OverlayPlane> overlayPlanes_;
  std::vector<OverlayFramebuffer> overlayFramebuffers_;
  std::optional<PreparedOverlay> preparedOverlay_;
  std::optional<PreparedDirectScanout> preparedDirectScanout_;
  std::optional<DirectScanoutValidationKey> directScanoutValidationKey_;
  std::vector<OverlayValidationKey> rejectedOverlayValidationKeys_;
  int preparedOverlayPrimaryBuffer_ = -1;
  std::uint64_t overlayUseSerial_ = 0;
  std::uint32_t activeOverlayPlaneId_ = 0;
  std::uint32_t activeOverlayFbId_ = 0;
  std::uint64_t activeOverlayBufferId_ = 0;
  std::uint32_t pendingOverlayPlaneId_ = 0;
  std::uint32_t pendingOverlayFbId_ = 0;
  std::uint64_t pendingOverlayBufferId_ = 0;
  bool activeDirectScanout_ = false;
  bool pendingDirectScanout_ = false;
  std::uint32_t activeDirectScanoutFbId_ = 0;
  std::uint32_t pendingDirectScanoutFbId_ = 0;
  std::uint64_t activeDirectScanoutBufferId_ = 0;
  std::uint64_t pendingDirectScanoutBufferId_ = 0;
  std::optional<PreparedDirectScanout> activeDirectScanoutState_;
  std::optional<PreparedDirectScanout> pendingDirectScanoutState_;
  bool overlayPreparedLogged_ = false;
  bool overlayStaticRejectLogged_ = false;
  bool overlayImportFailureLogged_ = false;
  bool overlayTestFailureLogged_ = false;
  bool directScanoutPreparedLogged_ = false;
  bool directScanoutTestFailureLogged_ = false;
  bool damageClipFailureLogged_ = false;
  bool atomicCommitBusyLogged_ = false;
  bool renderFenceFallbackLogged_ = false;
  bool useRenderInFence_ = false;
  bool useAsyncRenderFence_ = false;
  bool directScanoutRenderForced_ = false;
  bool directScanoutRender_ = false;
  bool modesetDone_ = false;
  bool pageFlipPending_ = false;
  int displayedBuffer_ = -1;
  int pendingBuffer_ = -1;
  int renderBuffer_ = -1;
  int activePreparedBuffer_ = -1;
  std::uint32_t nextPresentId_ = 1;
  KmsAtomicPresenter::PageFlipTiming pendingTiming_{};
  drmModeAtomicReq* atomicRequest_ = nullptr;
  std::vector<Buffer> buffers_;
  std::unique_ptr<Canvas> canvas_;
  static constexpr std::size_t kOverlayFramebufferCacheLimit = 12;
  static constexpr std::size_t kOverlayValidationRejectCacheLimit = 32;
};

KmsOutput::Impl::~Impl() {
  if (atomicCursorRequest_) {
    drmModeAtomicFree(atomicCursorRequest_);
    atomicCursorRequest_ = nullptr;
  }
  destroyCursorBuffer();
}

void KmsOutput::Impl::destroyCursorBuffer() {
  if (!cursorBuffer_.handle) return;
  if (cursorBuffer_.fbId != 0) {
    drmModeRmFB(drmFd(), cursorBuffer_.fbId);
  }
  drm_mode_destroy_dumb destroy{};
  destroy.handle = cursorBuffer_.handle;
  drmIoctl(drmFd(), DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
  cursorBuffer_ = {};
  cursorVisible_ = false;
  atomicCursorActive_ = false;
}

bool KmsOutput::Impl::ensureAtomicCursorPlane() const {
  if (atomicCursorInitialized_) return atomicCursorAvailable_;
  atomicCursorInitialized_ = true;

  int const fd = drmFd();
  if (fd < 0) return false;
  std::vector<std::uint32_t> const cursorPlanes = planesForCrtcWithType(fd, connector_.crtcId, DRM_PLANE_TYPE_CURSOR);
  std::optional<std::uint64_t> highestContentZpos;
  std::uint32_t const primaryPlaneId = primaryPlaneForCrtc(fd, connector_.crtcId);
  if (primaryPlaneId != 0 && propertyId(fd, primaryPlaneId, DRM_MODE_OBJECT_PLANE, "zpos") != 0) {
    highestContentZpos = propertyValue(fd, primaryPlaneId, DRM_MODE_OBJECT_PLANE, "zpos");
  }
  for (std::uint32_t overlayId : planesForCrtcWithType(fd, connector_.crtcId, DRM_PLANE_TYPE_OVERLAY)) {
    if (propertyId(fd, overlayId, DRM_MODE_OBJECT_PLANE, "zpos") == 0) continue;
    std::uint64_t const zpos = propertyValue(fd, overlayId, DRM_MODE_OBJECT_PLANE, "zpos");
    highestContentZpos = highestContentZpos ? std::max(*highestContentZpos, zpos) : zpos;
  }

  struct CursorPlaneCandidate {
    std::uint32_t planeId = 0;
    CursorPlaneProperties props{};
    std::optional<std::uint64_t> zpos;
    bool zposMutable = false;
  };
  std::optional<CursorPlaneCandidate> selected;
  auto betterCursorPlane = [](CursorPlaneCandidate const& a, CursorPlaneCandidate const& b) {
    if (a.zpos.has_value() != b.zpos.has_value()) return a.zpos.has_value();
    if (a.zpos && b.zpos && *a.zpos != *b.zpos) return *a.zpos > *b.zpos;
    return a.planeId > b.planeId;
  };

  for (std::uint32_t planeId : cursorPlanes) {
    CursorPlaneProperties const props{
        .fbId = propertyId(fd, planeId, DRM_MODE_OBJECT_PLANE, "FB_ID"),
        .crtcId = propertyId(fd, planeId, DRM_MODE_OBJECT_PLANE, "CRTC_ID"),
        .srcX = propertyId(fd, planeId, DRM_MODE_OBJECT_PLANE, "SRC_X"),
        .srcY = propertyId(fd, planeId, DRM_MODE_OBJECT_PLANE, "SRC_Y"),
        .srcW = propertyId(fd, planeId, DRM_MODE_OBJECT_PLANE, "SRC_W"),
        .srcH = propertyId(fd, planeId, DRM_MODE_OBJECT_PLANE, "SRC_H"),
        .crtcX = propertyId(fd, planeId, DRM_MODE_OBJECT_PLANE, "CRTC_X"),
        .crtcY = propertyId(fd, planeId, DRM_MODE_OBJECT_PLANE, "CRTC_Y"),
        .crtcW = propertyId(fd, planeId, DRM_MODE_OBJECT_PLANE, "CRTC_W"),
        .crtcH = propertyId(fd, planeId, DRM_MODE_OBJECT_PLANE, "CRTC_H"),
        .zpos = propertyId(fd, planeId, DRM_MODE_OBJECT_PLANE, "zpos"),
    };
    if (props.fbId == 0 || props.crtcId == 0 || props.srcX == 0 || props.srcY == 0 ||
        props.srcW == 0 || props.srcH == 0 || props.crtcX == 0 || props.crtcY == 0 ||
        props.crtcW == 0 || props.crtcH == 0) {
      continue;
    }
    CursorPlaneCandidate candidate{
        .planeId = planeId,
        .props = props,
        .zpos = std::nullopt,
        .zposMutable = false,
    };
    if (props.zpos != 0) {
      std::uint32_t const zposFlags = propertyFlags(fd, planeId, DRM_MODE_OBJECT_PLANE, "zpos");
      candidate.zposMutable = (zposFlags & DRM_MODE_PROP_IMMUTABLE) == 0;
      std::uint64_t const currentZpos = propertyValue(fd, planeId, DRM_MODE_OBJECT_PLANE, "zpos");
      std::uint64_t targetZpos = highestContentZpos ? std::max(currentZpos, *highestContentZpos + 1u) : currentZpos;
      if (auto range = propertyRange(fd, planeId, DRM_MODE_OBJECT_PLANE, "zpos")) {
        targetZpos = std::clamp(targetZpos, range->first, range->second);
      }
      candidate.zpos = targetZpos;
    }
    if (!selected || betterCursorPlane(candidate, *selected)) {
      selected = candidate;
    }
  }

  if (!selected) return false;

  atomicCursorPlaneId_ = selected->planeId;
  atomicCursorPlaneProps_ = selected->props;
  atomicCursorZpos_ = selected->zpos;
  atomicCursorZposMutable_ = selected->zposMutable;
  atomicCursorAvailable_ = true;
  if (!atomicCursorLogged_) {
    std::fprintf(stderr,
                 "lambda-window-manager: KMS atomic cursor plane=%u candidates=%zu zpos=%s%llu mutable=%d highestContentZpos=%s%llu\n",
                 atomicCursorPlaneId_,
                 cursorPlanes.size(),
                 atomicCursorZpos_ ? "" : "unknown:",
                 static_cast<unsigned long long>(atomicCursorZpos_.value_or(0)),
                 atomicCursorZposMutable_ ? 1 : 0,
                 highestContentZpos ? "" : "unknown:",
                 static_cast<unsigned long long>(highestContentZpos.value_or(0)));
    atomicCursorLogged_ = true;
  }
  return true;
}

bool KmsOutput::Impl::addAtomicCursorProperties(drmModeAtomicReq* request) const {
  KmsTraceScope trace(KmsTraceBucket::CursorProps);
  if (!cursorVisible_) return false;
  if (!ensureAtomicCursorPlane()) return false;
  if (cursorBuffer_.fbId == 0 || cursorBuffer_.width == 0 || cursorBuffer_.height == 0) return false;

  CursorPlaneProperties const& props = atomicCursorPlaneProps_;
  std::int32_t const crtcX = cursorX_ - cursorHotspotX_;
  std::int32_t const crtcY = cursorY_ - cursorHotspotY_;
  addAtomicProperty(request, atomicCursorPlaneId_, props.fbId, cursorBuffer_.fbId, "cursor.FB_ID");
  addAtomicProperty(request, atomicCursorPlaneId_, props.crtcId, connector_.crtcId, "cursor.CRTC_ID");
  addAtomicProperty(request, atomicCursorPlaneId_, props.srcX, 0, "cursor.SRC_X");
  addAtomicProperty(request, atomicCursorPlaneId_, props.srcY, 0, "cursor.SRC_Y");
  addAtomicProperty(request, atomicCursorPlaneId_, props.srcW, static_cast<std::uint64_t>(cursorBuffer_.width) << 16u, "cursor.SRC_W");
  addAtomicProperty(request, atomicCursorPlaneId_, props.srcH, static_cast<std::uint64_t>(cursorBuffer_.height) << 16u, "cursor.SRC_H");
  addAtomicProperty(request,
                    atomicCursorPlaneId_,
                    props.crtcX,
                    static_cast<std::uint64_t>(static_cast<std::int64_t>(crtcX)),
                    "cursor.CRTC_X");
  addAtomicProperty(request,
                    atomicCursorPlaneId_,
                    props.crtcY,
                    static_cast<std::uint64_t>(static_cast<std::int64_t>(crtcY)),
                    "cursor.CRTC_Y");
  addAtomicProperty(request, atomicCursorPlaneId_, props.crtcW, cursorBuffer_.width, "cursor.CRTC_W");
  addAtomicProperty(request, atomicCursorPlaneId_, props.crtcH, cursorBuffer_.height, "cursor.CRTC_H");
  if (atomicCursorZpos_ && atomicCursorZposMutable_) {
    addOptionalAtomicProperty(request, atomicCursorPlaneId_, props.zpos, *atomicCursorZpos_);
  }
  return true;
}

void KmsOutput::Impl::markAtomicCursorScheduled() const noexcept {
  atomicCursorActive_ = cursorVisible_ && atomicCursorPlaneId_ != 0 && cursorBuffer_.fbId != 0;
  atomicCursorMoveDeferred_ = false;
}

void KmsOutput::Impl::setAtomicPageFlipPending(bool pending) const noexcept {
  atomicPageFlipPending_ = pending;
  if (pending || !atomicCursorMoveDeferred_) return;
  atomicCursorMoveDeferred_ = false;
  if (!commitAtomicCursor(cursorX_, cursorY_, cursorVisible_)) {
    atomicCursorMoveDeferred_ = true;
  }
}

bool KmsOutput::Impl::hasDeferredCursorCommit() const noexcept {
  return atomicCursorMoveDeferred_;
}

bool KmsOutput::Impl::retryDeferredCursorCommit() const noexcept {
  if (!atomicCursorMoveDeferred_ || atomicPageFlipPending_) {
    return false;
  }
  atomicCursorMoveDeferred_ = false;
  if (commitAtomicCursor(cursorX_, cursorY_, cursorVisible_)) {
    return true;
  }
  atomicCursorMoveDeferred_ = true;
  return false;
}

drmModeAtomicReq* KmsOutput::Impl::cursorAtomicRequest() const noexcept {
  if (!atomicCursorRequest_) {
    atomicCursorRequest_ = drmModeAtomicAlloc();
  } else {
    drmModeAtomicSetCursor(atomicCursorRequest_, 0);
  }
  return atomicCursorRequest_;
}

bool KmsOutput::Impl::commitAtomicCursor(std::int32_t x, std::int32_t y, bool visible) const noexcept {
  KmsTraceScope trace(KmsTraceBucket::CursorCommit);
  if (!ensureAtomicCursorPlane()) return false;
  if (visible && (cursorBuffer_.fbId == 0 || cursorBuffer_.width == 0 || cursorBuffer_.height == 0)) return false;

  cursorX_ = x;
  cursorY_ = y;

  int const fd = drmFd();
  if (fd < 0) return false;
  drmModeAtomicReq* request = cursorAtomicRequest();
  if (!request) return false;
  bool committed = false;
  int commitErrno = 0;
  try {
    if (visible) {
      bool const propsAdded = addAtomicCursorProperties(request);
      if (!propsAdded) {
        commitErrno = EINVAL;
      } else {
        errno = 0;
        committed = drmModeAtomicCommit(fd, request, DRM_MODE_ATOMIC_NONBLOCK, nullptr) == 0;
        if (!committed) commitErrno = errno != 0 ? errno : EINVAL;
      }
    } else {
      CursorPlaneProperties const& props = atomicCursorPlaneProps_;
      addAtomicProperty(request, atomicCursorPlaneId_, props.fbId, 0, "cursor.FB_ID");
      addAtomicProperty(request, atomicCursorPlaneId_, props.crtcId, 0, "cursor.CRTC_ID");
      addAtomicProperty(request, atomicCursorPlaneId_, props.srcX, 0, "cursor.SRC_X");
      addAtomicProperty(request, atomicCursorPlaneId_, props.srcY, 0, "cursor.SRC_Y");
      addAtomicProperty(request, atomicCursorPlaneId_, props.srcW, 0, "cursor.SRC_W");
      addAtomicProperty(request, atomicCursorPlaneId_, props.srcH, 0, "cursor.SRC_H");
      addAtomicProperty(request, atomicCursorPlaneId_, props.crtcX, 0, "cursor.CRTC_X");
      addAtomicProperty(request, atomicCursorPlaneId_, props.crtcY, 0, "cursor.CRTC_Y");
      addAtomicProperty(request, atomicCursorPlaneId_, props.crtcW, 0, "cursor.CRTC_W");
      addAtomicProperty(request, atomicCursorPlaneId_, props.crtcH, 0, "cursor.CRTC_H");
      errno = 0;
      committed = drmModeAtomicCommit(fd, request, DRM_MODE_ATOMIC_NONBLOCK, nullptr) == 0;
      if (!committed) commitErrno = errno != 0 ? errno : EINVAL;
    }
  } catch (...) {
    committed = false;
    commitErrno = errno != 0 ? errno : EINVAL;
  }

  bool const retryDeferred = commitErrno == EBUSY || commitErrno == EAGAIN;
  if (!committed && retryDeferred && visible) {
    errno = 0;
    committed = drmModeMoveCursor(fd, connector_.crtcId, x, y) == 0;
    if (committed) {
      atomicCursorActive_ = true;
      atomicCursorMoveDeferred_ = false;
      return true;
    }
  }
  if (!committed && retryDeferred) {
    atomicCursorMoveDeferred_ = true;
    return true;
  }
  if (!committed && !retryDeferred && !atomicCursorFailureLogged_) {
    std::fprintf(stderr,
                 "lambda-window-manager: KMS atomic cursor commit failed plane=%u: %s\n",
                 atomicCursorPlaneId_,
                 std::strerror(commitErrno));
    atomicCursorFailureLogged_ = true;
  }
  atomicCursorActive_ = committed && visible;
  return committed;
}

KmsOutput::KmsOutput() = default;
KmsOutput::~KmsOutput() = default;
KmsOutput::KmsOutput(KmsOutput const&) = default;
KmsOutput& KmsOutput::operator=(KmsOutput const&) = default;
KmsOutput::KmsOutput(KmsOutput&&) noexcept = default;
KmsOutput& KmsOutput::operator=(KmsOutput&&) noexcept = default;

KmsOutput::KmsOutput(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}

KmsAtomicPresenter::KmsAtomicPresenter(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
KmsAtomicPresenter::~KmsAtomicPresenter() = default;
KmsAtomicPresenter::KmsAtomicPresenter(KmsAtomicPresenter&&) noexcept = default;
KmsAtomicPresenter& KmsAtomicPresenter::operator=(KmsAtomicPresenter&&) noexcept = default;

Canvas& KmsAtomicPresenter::canvas() {
  if (!impl_) throw std::runtime_error("Invalid KMS atomic presenter");
  return impl_->canvas();
}

bool KmsAtomicPresenter::canPrepareFrame() {
  return impl_ && impl_->canPrepareFrame();
}

bool KmsAtomicPresenter::prepareFrame(std::span<DamageRect const> damage) {
  return impl_ && impl_->prepareFrame(damage);
}

std::uint32_t KmsAtomicPresenter::markFrameRendered() {
  return impl_ ? impl_->markFrameRendered() : 0u;
}

bool KmsAtomicPresenter::updateRenderReady(std::uint32_t token) {
  return impl_ ? impl_->updateRenderReady(token) : true;
}

bool KmsAtomicPresenter::canSchedulePresent(std::uint32_t token) {
  return impl_ && impl_->canSchedulePresent(token);
}

int KmsAtomicPresenter::renderReadyFd(std::uint32_t token) const noexcept {
  return impl_ ? impl_->renderReadyFd(token) : -1;
}

void KmsAtomicPresenter::discardPreparedFrame(std::uint32_t token) {
  if (impl_) impl_->discardPreparedFrame(token);
}

bool KmsAtomicPresenter::prepareOverlayCandidate(OverlayCandidate candidate) {
  return prepareOverlayCandidate(0, std::move(candidate));
}

bool KmsAtomicPresenter::prepareOverlayCandidate(std::uint32_t token, OverlayCandidate candidate) {
  if (!impl_) {
    for (auto& plane : candidate.planes) {
      if (plane.fd >= 0) close(plane.fd);
      plane.fd = -1;
    }
    if (candidate.acquireFenceFd >= 0) close(candidate.acquireFenceFd);
    candidate.acquireFenceFd = -1;
    return false;
  }
  return impl_->prepareOverlayCandidate(token, std::move(candidate));
}

bool KmsAtomicPresenter::canPrepareOverlayOnly() const noexcept {
  return impl_ && impl_->canPrepareOverlayOnly();
}

bool KmsAtomicPresenter::prepareOverlayCandidateForDisplayedFrame(OverlayCandidate candidate) {
  if (!impl_) {
    for (auto& plane : candidate.planes) {
      if (plane.fd >= 0) close(plane.fd);
      plane.fd = -1;
    }
    if (candidate.acquireFenceFd >= 0) close(candidate.acquireFenceFd);
    candidate.acquireFenceFd = -1;
    return false;
  }
  return impl_->prepareOverlayCandidateForDisplayedFrame(std::move(candidate));
}

bool KmsAtomicPresenter::canScheduleOverlayOnly() const noexcept {
  return impl_ && impl_->canScheduleOverlayOnly();
}

int KmsAtomicPresenter::preparedOverlayAcquireFenceFd() const noexcept {
  return impl_ ? impl_->preparedOverlayAcquireFenceFd() : -1;
}

std::uint32_t KmsAtomicPresenter::scheduleOverlayOnly() {
  return impl_ ? impl_->scheduleOverlayOnly() : 0u;
}

bool KmsAtomicPresenter::primeDirectScanoutCandidate(OverlayCandidate& candidate) {
  return impl_ && impl_->primeDirectScanoutCandidate(candidate);
}

bool KmsAtomicPresenter::prepareDirectScanoutCandidate(OverlayCandidate candidate) {
  if (!impl_) {
    for (auto& plane : candidate.planes) {
      if (plane.fd >= 0) close(plane.fd);
      plane.fd = -1;
    }
    if (candidate.acquireFenceFd >= 0) close(candidate.acquireFenceFd);
    candidate.acquireFenceFd = -1;
    return false;
  }
  return impl_->prepareDirectScanoutCandidate(std::move(candidate));
}

bool KmsAtomicPresenter::canScheduleDirectScanout() const noexcept {
  return impl_ && impl_->canScheduleDirectScanout();
}

int KmsAtomicPresenter::preparedDirectScanoutAcquireFenceFd() const noexcept {
  return impl_ ? impl_->preparedDirectScanoutAcquireFenceFd() : -1;
}

std::uint32_t KmsAtomicPresenter::scheduleDirectScanout() {
  return impl_ ? impl_->scheduleDirectScanout() : 0u;
}

bool KmsAtomicPresenter::canScheduleDirectScanoutRepeat() const noexcept {
  return impl_ && impl_->canScheduleDirectScanoutRepeat();
}

std::uint32_t KmsAtomicPresenter::scheduleDirectScanoutRepeat() {
  return impl_ ? impl_->scheduleDirectScanoutRepeat() : 0u;
}

void KmsAtomicPresenter::clearPreparedDirectScanout() {
  if (impl_) impl_->clearPreparedDirectScanout();
}

void KmsAtomicPresenter::clearPreparedOverlayCandidate() {
  if (impl_) impl_->clearPreparedOverlayCandidate();
}

std::uint64_t KmsAtomicPresenter::preparedOverlaySurfaceId() const noexcept {
  return impl_ ? impl_->preparedOverlaySurfaceId() : 0;
}

std::vector<std::uint64_t> KmsAtomicPresenter::overlayBufferIdsInUse() const {
  return impl_ ? impl_->overlayBufferIdsInUse() : std::vector<std::uint64_t>{};
}

bool KmsAtomicPresenter::canUseOverlayFormatModifier(std::uint32_t format, std::uint64_t modifier) const noexcept {
  return impl_ && impl_->canUseOverlayFormatModifier(format, modifier);
}

std::vector<KmsDmabufFormatModifier> KmsAtomicPresenter::overlayDmabufFormatModifierPreferences() const {
  return impl_ ? impl_->overlayDmabufFormatModifierPreferences() : std::vector<KmsDmabufFormatModifier>{};
}

std::uint32_t KmsAtomicPresenter::schedulePresent(std::uint32_t token) {
  return impl_ ? impl_->schedulePresent(token) : 0u;
}

KmsAtomicPresenter::PageFlipTiming KmsAtomicPresenter::present() {
  if (!impl_) return {};
  return impl_->present();
}

std::optional<KmsAtomicPresenter::PageFlipTiming> KmsAtomicPresenter::dispatchPageFlipEvents() {
  return impl_ ? impl_->dispatchPageFlipEvents() : std::nullopt;
}

bool KmsAtomicPresenter::hasPendingPageFlip() const noexcept {
  return impl_ && impl_->hasPendingPageFlip();
}

int KmsAtomicPresenter::eventFd() const noexcept {
  return impl_ ? impl_->eventFd() : -1;
}

void KmsAtomicPresenter::syncModeStateFromKernel() noexcept {
  if (impl_) impl_->syncModeStateFromKernel();
}

std::string const& KmsOutput::name() const noexcept {
  static std::string const empty;
  return impl_ ? impl_->connector_.name : empty;
}

std::uint32_t KmsOutput::width() const noexcept {
  return impl_ ? impl_->connector_.mode.hdisplay : 0u;
}

std::uint32_t KmsOutput::height() const noexcept {
  return impl_ ? impl_->connector_.mode.vdisplay : 0u;
}

std::uint32_t KmsOutput::refreshRateMilliHz() const noexcept {
  return impl_ ? lambda::platform::refreshRateMilliHz(impl_->connector_.mode) : 0u;
}

std::uint32_t KmsOutput::cursorWidth() const noexcept {
  return impl_ ? cursorDimension(impl_->drmFd(), DRM_CAP_CURSOR_WIDTH) : 0u;
}

std::uint32_t KmsOutput::cursorHeight() const noexcept {
  return impl_ ? cursorDimension(impl_->drmFd(), DRM_CAP_CURSOR_HEIGHT) : 0u;
}

VkSurfaceKHR KmsOutput::createVulkanSurface(VkInstance instance) const {
  if (!impl_ || !impl_->device_ || !impl_->device_->app_) throw std::runtime_error("Invalid KMS output");
  auto* provider = impl_->device_->app_->gpuSurfaceProvider();
  if (!provider) throw std::runtime_error("KMS application does not provide Vulkan surfaces");
  return provider->createSurface(instance, &impl_->connector_);
}

KmsOutput::VblankTiming KmsOutput::waitForVblank() const {
  if (impl_) {
    drmVBlank vblank{};
    vblank.request.type = DRM_VBLANK_RELATIVE;
    vblank.request.sequence = 1;
    if (drmWaitVBlank(impl_->drmFd(), &vblank) == 0) {
      std::uint64_t const sec = static_cast<std::uint64_t>(vblank.reply.tval_sec);
      std::uint64_t const usec = static_cast<std::uint64_t>(vblank.reply.tval_usec);
      impl_->softwareVblankPhaseNsec_ = sec * 1'000'000'000ull + usec * 1'000ull;
      impl_->softwareVblankSequence_ = static_cast<std::uint64_t>(vblank.reply.sequence);
      return VblankTiming{
          .hardware = true,
          .sequence = static_cast<std::uint64_t>(vblank.reply.sequence),
          .monotonicNsec = impl_->softwareVblankPhaseNsec_,
      };
    }
    if (!impl_->vblankWaitFailureLogged_) {
      std::fprintf(stderr,
                   "[lambda:kms] drmWaitVBlank failed for connector %s: %s; retrying each frame with timer fallback.\n",
                   impl_->connector_.name.c_str(),
                   std::strerror(errno));
      impl_->vblankWaitFailureLogged_ = true;
    }
  }
  std::uint64_t const intervalNsec = static_cast<std::uint64_t>(frameInterval(refreshRateMilliHz()).count());
  std::uint64_t const now = monotonicNowNsec();
  std::uint64_t target = impl_ ? impl_->softwareVblankPhaseNsec_ : 0;
  if (target == 0 || target + intervalNsec <= now) {
    target = now;
  }
  do {
    target += intervalNsec;
  } while (target <= now);
  if (target > now) {
    std::this_thread::sleep_for(std::chrono::nanoseconds(target - now));
  }
  if (impl_) {
    impl_->softwareVblankPhaseNsec_ = target;
    ++impl_->softwareVblankSequence_;
  }
  return VblankTiming{
      .hardware = false,
      .sequence = impl_ ? impl_->softwareVblankSequence_ : 0,
      .monotonicNsec = target,
  };
}

bool KmsOutput::setCursorImage(std::span<std::uint32_t const> premultipliedArgbPixels,
                               std::uint32_t width,
                               std::uint32_t height,
                               std::int32_t hotspotX,
                               std::int32_t hotspotY) const {
  if (!impl_ || width == 0 || height == 0 ||
      premultipliedArgbPixels.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
    return false;
  }

  int const fd = impl_->drmFd();
  if (fd < 0) return false;
  std::uint32_t const bufferWidth = std::max(width, cursorDimension(fd, DRM_CAP_CURSOR_WIDTH));
  std::uint32_t const bufferHeight = std::max(height, cursorDimension(fd, DRM_CAP_CURSOR_HEIGHT));
  if (impl_->cursorBuffer_.handle &&
      (impl_->cursorBuffer_.width != bufferWidth || impl_->cursorBuffer_.height != bufferHeight)) {
    hideCursor();
    impl_->destroyCursorBuffer();
  }

  if (!impl_->cursorBuffer_.handle) {
    drm_mode_create_dumb create{};
    create.width = bufferWidth;
    create.height = bufferHeight;
    create.bpp = 32;
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) return false;
    std::uint32_t fbId = 0;
    std::uint32_t handles[4] = {create.handle, 0, 0, 0};
    std::uint32_t pitches[4] = {create.pitch, 0, 0, 0};
    std::uint32_t offsets[4] = {0, 0, 0, 0};
    if (drmModeAddFB2(fd, bufferWidth, bufferHeight, DRM_FORMAT_ARGB8888, handles, pitches, offsets, &fbId, 0) != 0) {
      drm_mode_destroy_dumb destroy{};
      destroy.handle = create.handle;
      drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
      return false;
    }
    impl_->cursorBuffer_ = KmsOutput::Impl::CursorBuffer{
        .handle = create.handle,
        .size = create.size,
        .width = bufferWidth,
        .height = bufferHeight,
        .pitch = create.pitch,
        .fbId = fbId,
    };
  }

  drm_mode_map_dumb map{};
  map.handle = impl_->cursorBuffer_.handle;
  if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0) return false;
  void* mapped = mmap(nullptr, impl_->cursorBuffer_.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map.offset);
  if (mapped == MAP_FAILED) return false;
  auto* dst = static_cast<std::uint8_t*>(mapped);
  std::size_t const srcRowBytes = static_cast<std::size_t>(width) * sizeof(std::uint32_t);
  std::size_t const dstRowBytes = impl_->cursorBuffer_.pitch;
  std::memset(mapped, 0, impl_->cursorBuffer_.size);
  for (std::uint32_t y = 0; y < height; ++y) {
    std::memcpy(dst + static_cast<std::size_t>(y) * dstRowBytes,
                premultipliedArgbPixels.data() + static_cast<std::size_t>(y) * width,
                srcRowBytes);
  }
  munmap(mapped, impl_->cursorBuffer_.size);

  impl_->cursorHotspotX_ = hotspotX;
  impl_->cursorHotspotY_ = hotspotY;
  if (!impl_->cursorUploadLogged_) {
    std::fprintf(stderr,
                 "lambda-window-manager: KMS cursor image uploaded size=%ux%u buffer=%ux%u hotspot=%d,%d fb=%u\n",
                 width,
                 height,
                 impl_->cursorBuffer_.width,
                 impl_->cursorBuffer_.height,
                 hotspotX,
                 hotspotY,
                 impl_->cursorBuffer_.fbId);
    impl_->cursorUploadLogged_ = true;
  }
  impl_->cursorVisible_ = true;
  if (impl_->ensureAtomicCursorPlane()) {
    return true;
  }

  int rc = drmModeSetCursor2(fd,
                             impl_->connector_.crtcId,
                             impl_->cursorBuffer_.handle,
                             impl_->cursorBuffer_.width,
                             impl_->cursorBuffer_.height,
                             hotspotX,
                             hotspotY);
  if (rc != 0) {
    rc = drmModeSetCursor(fd,
                          impl_->connector_.crtcId,
                          impl_->cursorBuffer_.handle,
                          impl_->cursorBuffer_.width,
                          impl_->cursorBuffer_.height);
  }
  impl_->cursorVisible_ = rc == 0;
  impl_->atomicCursorActive_ = false;
  if (!impl_->cursorVisible_) {
    drmModeSetCursor(fd, impl_->connector_.crtcId, 0, 0, 0);
  }
  return impl_->cursorVisible_;
}

bool KmsOutput::moveCursor(std::int32_t x, std::int32_t y) const {
  if (!impl_ || !impl_->cursorVisible_) return false;
  if (impl_->atomicCursorAvailable_ || !impl_->atomicCursorInitialized_) {
    if (impl_->ensureAtomicCursorPlane()) {
      if (impl_->atomicCursorActive_ && !impl_->atomicCursorMoveDeferred_ &&
          impl_->cursorX_ == x && impl_->cursorY_ == y) {
        return true;
      }
      return impl_->commitAtomicCursor(x, y, true);
    }
  }
  return drmModeMoveCursor(impl_->drmFd(), impl_->connector_.crtcId, x, y) == 0;
}

bool KmsOutput::hasDeferredCursorCommit() const noexcept {
  return impl_ && impl_->hasDeferredCursorCommit();
}

bool KmsOutput::retryDeferredCursorCommit() const noexcept {
  return impl_ && impl_->retryDeferredCursorCommit();
}

void KmsOutput::hideCursor() const {
  if (!impl_ || !impl_->cursorVisible_) return;
  if (impl_->atomicCursorActive_) {
    (void)impl_->commitAtomicCursor(0, 0, false);
  }
  drmModeSetCursor(impl_->drmFd(), impl_->connector_.crtcId, 0, 0, 0);
  impl_->cursorVisible_ = false;
  impl_->atomicCursorActive_ = false;
}

std::unique_ptr<KmsAtomicPresenter> KmsOutput::createAtomicPresenter(TextSystem& textSystem) const {
  if (!impl_) return nullptr;
  return std::unique_ptr<KmsAtomicPresenter>(
      new KmsAtomicPresenter(std::make_unique<KmsAtomicPresenter::Impl>(impl_, textSystem)));
}

KmsDevice::Impl::Impl(char const* devicePath) {
  if (devicePath && *devicePath) {
    throw std::runtime_error("KmsDevice::open(devicePath) is not implemented yet; pass nullptr");
  }
  app_ = std::make_unique<lambda::KmsApplication>();
  app_->setApplicationName("lambda-window-manager");
  app_->initialize();
}

std::vector<KmsOutput> KmsDevice::Impl::outputs(std::shared_ptr<Impl> self) const {
  std::vector<KmsOutput> result;
  result.reserve(app_->connectors_.size());
  for (KmsConnector const& connector : app_->connectors_) {
    result.push_back(KmsOutput(std::shared_ptr<KmsOutput::Impl>(new KmsOutput::Impl(self, connector))));
  }
  return result;
}

std::unique_ptr<KmsDevice> KmsDevice::open(char const* devicePath) {
  return std::unique_ptr<KmsDevice>(new KmsDevice(std::make_shared<Impl>(devicePath)));
}

KmsDevice::KmsDevice(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
KmsDevice::~KmsDevice() = default;
KmsDevice::KmsDevice(KmsDevice&&) noexcept = default;
KmsDevice& KmsDevice::operator=(KmsDevice&&) noexcept = default;

std::vector<KmsOutput> KmsDevice::outputs() const {
  return impl_ ? impl_->outputs(impl_) : std::vector<KmsOutput>{};
}

int KmsDevice::fd() const noexcept {
  return impl_ && impl_->app_ ? impl_->app_->drmFd() : -1;
}

std::span<char const* const> KmsDevice::requiredVulkanInstanceExtensions() const {
  static std::vector<char const*> empty;
  if (!impl_ || !impl_->app_) {
    return std::span<char const* const>(empty.data(), empty.size());
  }
  auto* provider = impl_->app_->gpuSurfaceProvider();
  return provider ? provider->requiredInstanceExtensions()
                  : std::span<char const* const>(empty.data(), empty.size());
}

std::filesystem::path KmsDevice::cacheDir() const {
  return impl_ && impl_->app_ ? std::filesystem::path(impl_->app_->cacheDir()) : std::filesystem::path{};
}

bool KmsDevice::isVtForeground() const noexcept {
  return impl_ && impl_->app_ ? impl_->app_->isVtForeground() : false;
}

bool KmsDevice::shouldTerminate() const noexcept {
  return impl_ && impl_->app_ ? impl_->app_->terminateRequested_.load(std::memory_order_relaxed) : true;
}

void KmsDevice::setInputHandler(std::function<void(KmsInputEvent const&)> handler) {
  if (impl_ && impl_->app_) impl_->app_->rawInputHandler_ = std::move(handler);
}

void KmsDevice::emitInputEventForDiagnostics(KmsInputEvent const& event) {
  if (impl_ && impl_->app_) impl_->app_->emitRawInput(event);
}

void KmsDevice::acknowledgeVtAcquire() {
  if (impl_ && impl_->app_) impl_->app_->acknowledgePendingVtAcquire();
}

bool KmsDevice::switchSession(int session) {
  if (impl_ && impl_->app_) return impl_->app_->switchSession(session);
  errno = ENODEV;
  return false;
}

bool KmsDevice::switchAdjacentSession(int direction) {
  if (impl_ && impl_->app_) return impl_->app_->switchAdjacentSession(direction);
  errno = ENODEV;
  return false;
}

bool KmsDevice::pollEvents(int timeoutMs, std::span<int const> extraFds) {
  return impl_ && impl_->app_ ? impl_->app_->pollInputAndWake(timeoutMs, extraFds) : false;
}

KmsPollResult KmsDevice::pollEventDetails(int timeoutMs, std::span<int const> extraFds) {
  return impl_ && impl_->app_ ? impl_->app_->pollInputAndWakeDetailed(timeoutMs, extraFds) : KmsPollResult{};
}

} // namespace lambda::platform
