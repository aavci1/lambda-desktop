#include "Platform/Linux/KmsPlatform.hpp"

#include "Graphics/Vulkan/VulkanCheck.hpp"
#include "Platform/Linux/Common/VtSwitch.hpp"
#include "UI/Platform/WindowFactory.hpp"

#include <Lambda/Debug/DebugFlags.hpp>
#include <Lambda/UI/Application.hpp>
#include <Lambda/UI/EventQueue.hpp>

#include <fcntl.h>
#include <libinput.h>
#include <libudev.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <poll.h>
#include <cstdarg>
#include <signal.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vulkan/vulkan.h>
#include <xf86drm.h>

extern "C" {
#include <libseat.h>
}

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lambdaui {
namespace {

KmsApplication* gKmsApplication = nullptr;

struct TerminationSignalState {
  struct sigaction previousSigInt {};
  struct sigaction previousSigTerm {};
  struct sigaction previousSigUsr1 {};
  struct sigaction previousSigUsr2 {};
  bool sigIntInstalled = false;
  bool sigTermInstalled = false;
  bool sigUsr1Installed = false;
  bool sigUsr2Installed = false;
};

TerminationSignalState gTerminationSignals;
volatile sig_atomic_t gTerminateSignalPending = 0;
volatile sig_atomic_t gVtSignalPending = 0;
volatile sig_atomic_t gSignalWakeFd = -1;

void terminateSignalHandler(int) {
  gTerminateSignalPending = 1;
  int const wakeFd = static_cast<int>(gSignalWakeFd);
  if (wakeFd >= 0) {
    char const c = 1;
    (void)write(wakeFd, &c, 1);
  }
}

void vtSignalHandler(int signal) {
  gVtSignalPending = signal;
  int const wakeFd = static_cast<int>(gSignalWakeFd);
  if (wakeFd >= 0) {
    char const c = 1;
    (void)write(wakeFd, &c, 1);
  }
}

void restoreTerminationSignalHandlers() {
  if (gTerminationSignals.sigIntInstalled) {
    sigaction(SIGINT, &gTerminationSignals.previousSigInt, nullptr);
    gTerminationSignals.sigIntInstalled = false;
  }
  if (gTerminationSignals.sigTermInstalled) {
    sigaction(SIGTERM, &gTerminationSignals.previousSigTerm, nullptr);
    gTerminationSignals.sigTermInstalled = false;
  }
  if (gTerminationSignals.sigUsr1Installed) {
    sigaction(SIGUSR1, &gTerminationSignals.previousSigUsr1, nullptr);
    gTerminationSignals.sigUsr1Installed = false;
  }
  if (gTerminationSignals.sigUsr2Installed) {
    sigaction(SIGUSR2, &gTerminationSignals.previousSigUsr2, nullptr);
    gTerminationSignals.sigUsr2Installed = false;
  }
  gSignalWakeFd = -1;
  gTerminateSignalPending = 0;
  gVtSignalPending = 0;
}

bool debugKms() {
  static bool const enabled = debug::envNonZero(std::getenv("LAMBDA_DEBUG_KMS"));
  return enabled;
}

void debugLog(char const* format, ...) {
  if (!debugKms()) return;
  std::fprintf(stderr, "[lambda:kms] ");
  va_list args;
  va_start(args, format);
  std::vfprintf(stderr, format, args);
  va_end(args);
  std::fprintf(stderr, "\n");
  std::fflush(stderr);
}

int readActiveVt() {
  int fd = open("/sys/class/tty/tty0/active", O_RDONLY | O_CLOEXEC);
  if (fd < 0) return 0;
  char buffer[32]{};
  ssize_t const n = read(fd, buffer, sizeof(buffer) - 1);
  close(fd);
  if (n <= 0) return 0;
  char const* p = buffer;
  while (*p && !std::isdigit(static_cast<unsigned char>(*p))) ++p;
  return *p ? std::atoi(p) : 0;
}

void libinputLog(libinput*, libinput_log_priority priority, char const* format, va_list args) {
  if (!debugKms()) return;
  std::fprintf(stderr, "[lambda:kms:libinput:%d] ", static_cast<int>(priority));
  std::vfprintf(stderr, format, args);
  std::fflush(stderr);
}

void libseatLog(libseat_log_level level, char const* format, va_list args) {
  if (!debugKms()) return;
  std::fprintf(stderr, "[lambda:kms:libseat:%d] ", static_cast<int>(level));
  std::vfprintf(stderr, format, args);
  std::fflush(stderr);
}

std::int64_t monotonicMillis() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

std::string envOr(std::string const& name, std::string fallback) {
  if (char const* value = std::getenv(name.c_str())) {
    if (*value) return value;
  }
  return fallback;
}

std::string sanitizeAppName(std::string name) {
  std::string out;
  out.reserve(name.size());
  for (unsigned char c : name) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.') out.push_back(static_cast<char>(c));
    else if (c == ' ') out.push_back('-');
  }
  return out.empty() ? "lambda" : out;
}

std::string appDir(std::string const& base, std::string const& appName) {
  std::filesystem::path path = std::filesystem::path(base) / "lambda" / sanitizeAppName(appName);
  std::error_code ec;
  std::filesystem::create_directories(path, ec);
  return path.string();
}

int openRestricted(char const* path, int flags, void* userdata) {
  auto* app = static_cast<KmsApplication*>(userdata);
  int fd = app ? app->openRestrictedDevice(path, flags) : open(path, flags | O_CLOEXEC);
  if (fd < 0) debugLog("failed to open input device %s: %s", path, std::strerror(errno));
  return fd;
}

void closeRestricted(int fd, void* userdata) {
  auto* app = static_cast<KmsApplication*>(userdata);
  if (app) app->closeRestrictedDevice(fd);
  else close(fd);
}

libinput_interface const kLibinputInterface{openRestricted, closeRestricted};

void seatEnabled(libseat*, void* userdata) {
  if (auto* app = static_cast<KmsApplication*>(userdata)) app->handleSeatEnabled();
}

void seatDisabled(libseat*, void* userdata) {
  if (auto* app = static_cast<KmsApplication*>(userdata)) app->handleSeatDisabled();
}

libseat_seat_listener const kSeatListener{
    .enable_seat = seatEnabled,
    .disable_seat = seatDisabled,
};

std::string connectorName(drmModeConnector const& connector) {
  char const* type = "UNKNOWN";
  switch (connector.connector_type) {
  case DRM_MODE_CONNECTOR_HDMIA: type = "HDMI-A"; break;
  case DRM_MODE_CONNECTOR_HDMIB: type = "HDMI-B"; break;
  case DRM_MODE_CONNECTOR_eDP: type = "eDP"; break;
  case DRM_MODE_CONNECTOR_DisplayPort: type = "DP"; break;
  case DRM_MODE_CONNECTOR_DVID: type = "DVI-D"; break;
  case DRM_MODE_CONNECTOR_DVII: type = "DVI-I"; break;
  case DRM_MODE_CONNECTOR_VGA: type = "VGA"; break;
  default: break;
  }
  return std::string(type) + "-" + std::to_string(connector.connector_type_id);
}

drmModeModeInfo chooseMode(drmModeConnector const& connector) {
  if (connector.count_modes <= 0) {
    throw std::runtime_error("Connected DRM output has no display modes");
  }
  for (int i = 0; i < connector.count_modes; ++i) {
    if ((connector.modes[i].type & DRM_MODE_TYPE_PREFERRED) != 0) return connector.modes[i];
  }
  return connector.modes[0];
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

bool displayNameMatches(char const* displayName, KmsConnector const& connector) {
  if (!displayName || connector.name.empty()) return false;
  return std::string(displayName).find(connector.name) != std::string::npos;
}

bool displaySizeMatches(VkDisplayPropertiesKHR const& display, KmsConnector const& connector) {
  if (connector.widthMm <= 0 || connector.heightMm <= 0) return false;
  int const widthDelta = std::abs(static_cast<int>(display.physicalDimensions.width) - connector.widthMm);
  int const heightDelta = std::abs(static_cast<int>(display.physicalDimensions.height) - connector.heightMm);
  return widthDelta <= 5 && heightDelta <= 5;
}

bool modeResolutionMatches(VkDisplayModePropertiesKHR const& mode, KmsConnector const& connector) {
  return mode.parameters.visibleRegion.width == connector.mode.hdisplay &&
         mode.parameters.visibleRegion.height == connector.mode.vdisplay;
}

bool modeRefreshMatches(VkDisplayModePropertiesKHR const& mode, KmsConnector const& connector) {
  std::uint32_t const target = refreshRateMilliHz(connector.mode);
  std::uint32_t const actual = mode.parameters.refreshRate;
  std::uint32_t const delta = actual > target ? actual - target : target - actual;
  return delta <= 500u;
}

bool modeMatches(VkDisplayModePropertiesKHR const& mode, KmsConnector const& connector) {
  return modeResolutionMatches(mode, connector) && modeRefreshMatches(mode, connector);
}

bool drmModeInfoMatches(drmModeModeInfo const& a, drmModeModeInfo const& b) {
  return a.hdisplay == b.hdisplay && a.vdisplay == b.vdisplay && a.vrefresh == b.vrefresh &&
         a.clock == b.clock;
}

bool connectorModeChanged(KmsConnector const& a, KmsConnector const& b) {
  if (!drmModeInfoMatches(a.mode, b.mode) || a.modes.size() != b.modes.size()) return true;
  for (std::size_t i = 0; i < a.modes.size(); ++i) {
    if (!drmModeInfoMatches(a.modes[i], b.modes[i])) return true;
  }
  return false;
}

bool connectorDpiChanged(KmsConnector const& a, KmsConnector const& b) {
  return a.widthMm != b.widthMm || a.heightMm != b.heightMm;
}

std::uint32_t chooseAlphaMode(VkDisplayPlaneCapabilitiesKHR const& caps) {
  if ((caps.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR) != 0) {
    return VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
  }
  if ((caps.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_GLOBAL_BIT_KHR) != 0) {
    return VK_DISPLAY_PLANE_ALPHA_GLOBAL_BIT_KHR;
  }
  if ((caps.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_BIT_KHR) != 0) {
    return VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_BIT_KHR;
  }
  return VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_PREMULTIPLIED_BIT_KHR;
}

VkSurfaceTransformFlagBitsKHR chooseTransform(VkDisplayPropertiesKHR const& display) {
  if ((display.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) != 0) {
    return VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  }
  if ((display.supportedTransforms & VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR) != 0) {
    return VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR;
  }
  if ((display.supportedTransforms & VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR) != 0) {
    return VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR;
  }
  return VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR;
}

bool planeSupportsDisplay(VkPhysicalDevice physical, std::uint32_t plane, VkDisplayKHR display) {
  std::uint32_t supportedCount = 0;
  if (vkGetDisplayPlaneSupportedDisplaysKHR(physical, plane, &supportedCount, nullptr) != VK_SUCCESS ||
      supportedCount == 0) {
    return false;
  }
  std::vector<VkDisplayKHR> supported(supportedCount);
  if (vkGetDisplayPlaneSupportedDisplaysKHR(physical, plane, &supportedCount, supported.data()) != VK_SUCCESS) {
    return false;
  }
  return std::find(supported.begin(), supported.end(), display) != supported.end();
}

bool instanceExtensionAvailable(char const* name) {
  std::uint32_t count = 0;
  if (vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS) return false;
  std::vector<VkExtensionProperties> props(count);
  if (vkEnumerateInstanceExtensionProperties(nullptr, &count, props.data()) != VK_SUCCESS) return false;
  return std::any_of(props.begin(), props.end(), [&](VkExtensionProperties const& prop) {
    return std::strcmp(prop.extensionName, name) == 0;
  });
}

VkDisplayPropertiesKHR drmMappedDisplayProperties(VkDisplayKHR display, KmsConnector const& connector) {
  static char const name[] = "DRM connector";
  VkDisplayPropertiesKHR props{};
  props.display = display;
  props.displayName = name;
  props.physicalDimensions = {static_cast<std::uint32_t>(std::max(0, connector.widthMm)),
                              static_cast<std::uint32_t>(std::max(0, connector.heightMm))};
  props.physicalResolution = {connector.mode.hdisplay, connector.mode.vdisplay};
  props.supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  props.planeReorderPossible = VK_FALSE;
  props.persistentContent = VK_FALSE;
  return props;
}

VkSurfaceKHR tryCreateDisplaySurface(VkInstance instance, VkPhysicalDevice physical,
                                     VkDisplayPropertiesKHR const& display,
                                     VkDisplayModeKHR displayMode,
                                     VkExtent2D extent) {
  std::uint32_t planeCount = 0;
  if (vkGetPhysicalDeviceDisplayPlanePropertiesKHR(physical, &planeCount, nullptr) != VK_SUCCESS ||
      planeCount == 0) {
    return VK_NULL_HANDLE;
  }
  std::vector<VkDisplayPlanePropertiesKHR> planes(planeCount);
  vkGetPhysicalDeviceDisplayPlanePropertiesKHR(physical, &planeCount, planes.data());
  for (std::uint32_t plane = 0; plane < planeCount; ++plane) {
    if (!planeSupportsDisplay(physical, plane, display.display)) continue;
    VkDisplayPlaneCapabilitiesKHR caps{};
    VkResult capsResult = vkGetDisplayPlaneCapabilitiesKHR(physical, displayMode, plane, &caps);
    if (capsResult != VK_SUCCESS) continue;
    auto info = vkStructure<VkDisplaySurfaceCreateInfoKHR>(VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR);
    info.displayMode = displayMode;
    info.planeIndex = plane;
    info.planeStackIndex = planes[plane].currentStackIndex;
    info.transform = chooseTransform(display);
    info.globalAlpha = 1.f;
    info.alphaMode = static_cast<VkDisplayPlaneAlphaFlagBitsKHR>(chooseAlphaMode(caps));
    info.imageExtent = extent;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkResult const surfaceResult = vkCreateDisplayPlaneSurfaceKHR(instance, &info, nullptr, &surface);
    if (surfaceResult == VK_SUCCESS) {
      return surface;
    }
  }
  return VK_NULL_HANDLE;
}

std::uint32_t chooseCrtc(int fd, drmModeRes* resources, drmModeConnector const& connector) {
  if (connector.encoder_id != 0) {
    if (drmModeEncoder* encoder = drmModeGetEncoder(fd, connector.encoder_id)) {
      std::uint32_t crtc = encoder->crtc_id;
      drmModeFreeEncoder(encoder);
      if (crtc != 0) return crtc;
    }
  }
  for (int i = 0; i < connector.count_encoders; ++i) {
    drmModeEncoder* encoder = drmModeGetEncoder(fd, connector.encoders[i]);
    if (!encoder) continue;
    for (int c = 0; c < resources->count_crtcs; ++c) {
      if ((encoder->possible_crtcs & (1 << c)) != 0) {
        std::uint32_t crtc = resources->crtcs[c];
        drmModeFreeEncoder(encoder);
        return crtc;
      }
    }
    drmModeFreeEncoder(encoder);
  }
  return 0;
}

bool acquireDrmMaster(int fd, char const* context) {
  if (fd < 0) {
    errno = EBADF;
    return false;
  }

  if (drmSetMaster(fd) == 0) {
    return true;
  }

  int const setMasterErrno = errno;
  int const masterState = drmIsMaster(fd);
  if (masterState > 0) {
    debugLog("drmSetMaster %s failed with %s, but the fd is already DRM master",
             context,
             std::strerror(setMasterErrno));
    errno = 0;
    return true;
  }
  if (masterState < 0) {
    debugLog("drmIsMaster after drmSetMaster %s failed: %s", context, std::strerror(errno));
  }
  errno = setMasterErrno;
  return false;
}

} // namespace

KmsApplication::KmsApplication() {
  if (gKmsApplication) throw std::runtime_error("Only one KMS application can exist");
  gKmsApplication = this;
}

KmsApplication::~KmsApplication() {
  debugLog("destroying KMS application");
  restoreConsole();
  uninstallSignalHandlers();
  debugLog("releasing input devices");
  destroyInput();
  if (udevMonitor_) udev_monitor_unref(udevMonitor_);
  if (udev_) udev_unref(udev_);
  if (drmFd_ >= 0) {
    if (drmMaster_) {
      debugLog("dropping DRM master during shutdown");
      if (drmDropMaster(drmFd_) != 0) debugLog("drmDropMaster during shutdown failed: %s", std::strerror(errno));
      drmMaster_ = false;
    }
    closeRestrictedDevice(drmFd_);
  }
  closeSeat();
  closeActiveVtWatch();
  if (ttyFd_ >= 0) close(ttyFd_);
  if (wakePipe_[0] >= 0) close(wakePipe_[0]);
  if (wakePipe_[1] >= 0) close(wakePipe_[1]);
  if (gKmsApplication == this) gKmsApplication = nullptr;
}

void KmsApplication::initialize() {
  if (pipe(wakePipe_) != 0) throw std::system_error(errno, std::generic_category(), "pipe");
  fcntl(wakePipe_[0], F_SETFL, fcntl(wakePipe_[0], F_GETFL, 0) | O_NONBLOCK);
  fcntl(wakePipe_[1], F_SETFL, fcntl(wakePipe_[1], F_GETFL, 0) | O_NONBLOCK);
  installSignalHandlers();
  initializeSeat();
  initializeConsole();
  if (!openFirstDisplayCard()) {
    throw std::runtime_error("No connected DRM/KMS display found");
  }
  if (!acquireDrmMaster(drmFd_, "during startup")) {
    int const masterErrno = errno;
    throw std::runtime_error(std::string("DRM master unavailable during startup: ") +
                             std::strerror(masterErrno) +
                             ". Run from an active TTY and make sure no other compositor owns the card.");
  }
  drmMaster_ = true;
  enumerateConnectors();
  initializeInput();
  initializeDrmMonitor();
}

void KmsApplication::initializeSeat() {
  libseat_set_log_handler(libseatLog);
  libseat_set_log_level(debugKms() ? LIBSEAT_LOG_LEVEL_DEBUG : LIBSEAT_LOG_LEVEL_ERROR);
  seat_ = libseat_open_seat(&kSeatListener, this);
  if (!seat_) {
    debugLog("libseat_open_seat failed; falling back to direct device opens: %s", std::strerror(errno));
    return;
  }

  seatFd_ = libseat_get_fd(seat_);
  char const* seatName = libseat_seat_name(seat_);
  seatName_ = seatName && *seatName ? seatName : "seat0";
  seatEnabled_ = true;
  debugLog("opened libseat seat=%s fd=%d", seatName_.c_str(), seatFd_);
  dispatchSeatEvents();
}

void KmsApplication::closeSeat() {
  if (!seat_) return;
  for (auto const& [fd, deviceId] : seatDeviceIdsByFd_) {
    debugLog("closing lingering libseat device fd=%d id=%d", fd, deviceId);
    if (libseat_close_device(seat_, deviceId) != 0) {
      close(fd);
    }
  }
  seatDeviceIdsByFd_.clear();
  if (libseat_close_seat(seat_) != 0) {
    debugLog("libseat_close_seat failed: %s", std::strerror(errno));
  }
  seat_ = nullptr;
  seatFd_ = -1;
  seatName_.clear();
  seatEnabled_ = false;
  seatDisablePending_ = false;
}

void KmsApplication::dispatchSeatEvents() {
  if (!seat_) return;
  for (;;) {
    int const result = libseat_dispatch(seat_, 0);
    if (result > 0) continue;
    if (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
      debugLog("libseat_dispatch failed: %s", std::strerror(errno));
    }
    break;
  }
}

int KmsApplication::openRestrictedDevice(char const* path, int flags) {
  if (!path || !*path) {
    errno = EINVAL;
    return -1;
  }

  if (seat_) {
    if (!seatEnabled_) {
      errno = EACCES;
      debugLog("refusing to open %s while libseat seat is disabled", path);
      return -1;
    }
    int fd = -1;
    int const deviceId = libseat_open_device(seat_, path, &fd);
    if (deviceId >= 0 && fd >= 0) {
      int const fdFlags = fcntl(fd, F_GETFD, 0);
      if (fdFlags >= 0) {
        fcntl(fd, F_SETFD, fdFlags | FD_CLOEXEC);
      }
      seatDeviceIdsByFd_[fd] = deviceId;
      debugLog("opened %s through libseat id=%d fd=%d", path, deviceId, fd);
      return fd;
    }
    debugLog("libseat_open_device(%s) failed; falling back to direct open: %s", path, std::strerror(errno));
  }

  int fd = open(path, flags | O_CLOEXEC);
  if (fd >= 0) {
    debugLog("opened %s directly fd=%d", path, fd);
  }
  return fd;
}

void KmsApplication::closeRestrictedDevice(int fd) {
  if (fd < 0) return;
  auto it = seatDeviceIdsByFd_.find(fd);
  if (it != seatDeviceIdsByFd_.end()) {
    int const deviceId = it->second;
    seatDeviceIdsByFd_.erase(it);
    if (seat_ && libseat_close_device(seat_, deviceId) == 0) {
      debugLog("closed libseat device id=%d fd=%d", deviceId, fd);
      return;
    }
    debugLog("libseat_close_device id=%d fd=%d failed; closing fd directly: %s",
             deviceId,
             fd,
             std::strerror(errno));
  }
  close(fd);
}

void KmsApplication::handleSeatEnabled() {
  if (!seat_) return;
  seatEnabled_ = true;
  seatDisablePending_ = false;
  debugLog("libseat enabled seat=%s", seatName_.empty() ? "(unknown)" : seatName_.c_str());
  if (inputInitialized_ && !rebuildInputForSeatEnable()) {
    std::fprintf(stderr, "lambda-window-manager: failed to reopen input devices after seat enable\n");
  }
  acquireDrmMasterForVt(false);
}

void KmsApplication::handleSeatDisabled() {
  if (!seat_) return;
  if (seatDisablePending_) return;
  seatDisablePending_ = true;
  seatEnabled_ = false;
  debugLog("libseat disabled seat=%s", seatName_.empty() ? "(unknown)" : seatName_.c_str());
  releaseDrmMasterForVt(false);
  if (libseat_disable_seat(seat_) != 0) {
    debugLog("libseat_disable_seat failed: %s", std::strerror(errno));
  }
  seatDisablePending_ = false;
}

bool KmsApplication::openFirstDisplayCard() {
  if (!std::filesystem::exists("/dev/dri")) return false;
  for (auto const& entry : std::filesystem::directory_iterator("/dev/dri")) {
    std::string name = entry.path().filename().string();
    if (!name.starts_with("card")) continue;
    int fd = openRestrictedDevice(entry.path().c_str(), O_RDWR);
    if (fd < 0) continue;
    drmModeRes* resources = drmModeGetResources(fd);
    bool hasConnected = false;
    if (resources) {
      for (int i = 0; i < resources->count_connectors && !hasConnected; ++i) {
        drmModeConnector* connector = drmModeGetConnector(fd, resources->connectors[i]);
        hasConnected = connector && connector->connection == DRM_MODE_CONNECTED && connector->count_modes > 0;
        if (connector) drmModeFreeConnector(connector);
      }
      drmModeFreeResources(resources);
    }
    if (hasConnected) {
      drmFd_ = fd;
      return true;
    }
    closeRestrictedDevice(fd);
  }
  return false;
}

void KmsApplication::enumerateConnectors() {
  connectors_ = scanConnectors();
  if (connectors_.empty()) throw std::runtime_error("No usable DRM/KMS connector found");
}

std::vector<KmsConnector> KmsApplication::scanConnectors() const {
  std::vector<KmsConnector> result;
  drmModeRes* resources = drmModeGetResources(drmFd_);
  if (!resources) throw std::runtime_error("drmModeGetResources failed");
  for (int i = 0; i < resources->count_connectors; ++i) {
    drmModeConnector* connector = drmModeGetConnector(drmFd_, resources->connectors[i]);
    if (!connector) continue;
    if (connector->connection == DRM_MODE_CONNECTED && connector->count_modes > 0) {
      KmsConnector out{};
      out.connectorId = connector->connector_id;
      out.encoderId = connector->encoder_id;
      out.crtcId = chooseCrtc(drmFd_, resources, *connector);
      out.modes.assign(connector->modes, connector->modes + connector->count_modes);
      out.mode = chooseMode(*connector);
      out.widthMm = connector->mmWidth;
      out.heightMm = connector->mmHeight;
      out.name = connectorName(*connector);
      if (out.crtcId != 0) result.push_back(out);
    }
    drmModeFreeConnector(connector);
  }
  drmModeFreeResources(resources);
  return result;
}

void KmsApplication::initializeInput() {
  inputInitialized_ = true;
  inputDeviceCount_ = 0;
  inputSuspendedForVt_ = false;
  if (!udev_) udev_ = udev_new();
  if (!udev_) throw std::runtime_error("udev_new failed");
  input_ = libinput_udev_create_context(&kLibinputInterface, this, udev_);
  if (!input_) throw std::runtime_error("libinput_udev_create_context failed");
  libinput_log_set_handler(input_, libinputLog);
  libinput_log_set_priority(input_, debugKms() ? LIBINPUT_LOG_PRIORITY_DEBUG : LIBINPUT_LOG_PRIORITY_ERROR);
  debugLog("created udev libinput context fd=%d", libinput_get_fd(input_));
  if (libinput_udev_assign_seat(input_, "seat0") != 0) {
    throw std::runtime_error("libinput_udev_assign_seat(seat0) failed");
  }
  debugLog("assigned libinput seat0");
  dispatchPendingInput();
  if (inputDeviceCount_ > 0) return;

  debugLog("udev seat0 reported no input devices; falling back to /dev/input/event*");
  discardPendingInputEvents(true);
  libinput_suspend(input_);
  discardPendingInputEvents(true);
  libinput_unref(input_);
  input_ = libinput_path_create_context(&kLibinputInterface, this);
  if (!input_) throw std::runtime_error("libinput_path_create_context failed");
  libinput_log_set_handler(input_, libinputLog);
  libinput_log_set_priority(input_, debugKms() ? LIBINPUT_LOG_PRIORITY_DEBUG : LIBINPUT_LOG_PRIORITY_ERROR);
  if (std::filesystem::exists("/dev/input")) {
    for (auto const& entry : std::filesystem::directory_iterator("/dev/input")) {
      std::string name = entry.path().filename().string();
      if (!name.starts_with("event")) continue;
      libinput_device* device = libinput_path_add_device(input_, entry.path().c_str());
      if (device) {
        debugLog("added input path %s", entry.path().c_str());
        // libinput_path_add_device returns a pointer whose lifetime is only
        // guaranteed until the next dispatch unless we keep our own reference.
        pathInputDevices_.push_back(libinput_device_ref(device));
      } else {
        debugLog("libinput rejected input path %s", entry.path().c_str());
      }
    }
  }
  dispatchPendingInput();
  debugLog("input initialization complete with %d device(s)", inputDeviceCount_);
  if (inputDeviceCount_ == 0) {
    std::fprintf(stderr,
                 "[lambda:kms] warning: no input devices are readable; mouse and keyboard input will not work. "
                 "Grant access to /dev/input/event* or run under a seat manager.\n");
    std::fflush(stderr);
  }
}

void KmsApplication::destroyInput() {
  if (input_) {
    discardPendingInputEvents(true);
    for (libinput_device* device : pathInputDevices_) {
      libinput_path_remove_device(device);
    }
    discardPendingInputEvents(true);
    libinput_suspend(input_);
    discardPendingInputEvents(true);
    for (libinput_device* device : pathInputDevices_) {
      libinput_device_unref(device);
    }
    pathInputDevices_.clear();
    libinput_unref(input_);
    input_ = nullptr;
  } else {
    pathInputDevices_.clear();
  }
  inputDeviceCount_ = 0;
  inputSuspendedForVt_ = false;
  rawPressedButtons_.clear();
  rawPressedKeys_.clear();
  pressedButtons_ = 0;
}

bool KmsApplication::rebuildInputForSeatEnable() {
  if (!seat_ || !seatEnabled_) return false;
  debugLog("reopening libseat-managed input devices after seat enable");
  releaseRawInputState(static_cast<std::uint32_t>(std::max<std::int64_t>(0, monotonicMillis())));
  destroyInput();
  try {
    initializeInput();
    if (inputDeviceCount_ > 0) return true;
  } catch (std::exception const& error) {
    std::fprintf(stderr, "lambda-window-manager: input reopen failed after seat enable: %s\n", error.what());
  } catch (...) {
    std::fprintf(stderr, "lambda-window-manager: input reopen failed after seat enable\n");
  }
  destroyInput();
  return false;
}

void KmsApplication::initializeDrmMonitor() {
  if (!udev_ || udevMonitor_) return;
  udevMonitor_ = udev_monitor_new_from_netlink(udev_, "udev");
  if (!udevMonitor_) {
    debugLog("udev_monitor_new_from_netlink failed; KMS hot-plug disabled");
    return;
  }
  if (udev_monitor_filter_add_match_subsystem_devtype(udevMonitor_, "drm", nullptr) != 0) {
    debugLog("udev_monitor_filter_add_match_subsystem_devtype(drm) failed; KMS hot-plug disabled");
    udev_monitor_unref(udevMonitor_);
    udevMonitor_ = nullptr;
    return;
  }
  if (udev_monitor_enable_receiving(udevMonitor_) != 0) {
    debugLog("udev_monitor_enable_receiving failed; KMS hot-plug disabled");
    udev_monitor_unref(udevMonitor_);
    udevMonitor_ = nullptr;
    return;
  }
  udevMonitorFd_ = udev_monitor_get_fd(udevMonitor_);
  debugLog("watching DRM hot-plug via udev fd=%d", udevMonitorFd_);
}

void KmsApplication::drainDrmMonitor() {
  if (!udevMonitor_) return;
  bool shouldReEnumerate = false;
  for (;;) {
    udev_device* device = udev_monitor_receive_device(udevMonitor_);
    if (!device) break;
    char const* action = udev_device_get_action(device);
    char const* hotplug = udev_device_get_property_value(device, "HOTPLUG");
    char const* devnode = udev_device_get_devnode(device);
    debugLog("DRM udev event action=%s hotplug=%s devnode=%s", action ? action : "(none)",
             hotplug ? hotplug : "(none)", devnode ? devnode : "(none)");
    if (hotplug && std::string_view(hotplug) == "1") {
      shouldReEnumerate = true;
    }
    udev_device_unref(device);
  }
  if (shouldReEnumerate) reEnumerateConnectors();
}

void KmsApplication::reEnumerateConnectors() {
  std::vector<KmsConnector> next;
  try {
    next = scanConnectors();
  } catch (std::exception const& e) {
    debugLog("connector re-enumeration failed: %s", e.what());
    return;
  }

  std::unordered_map<std::uint32_t, KmsConnector> previousById;
  std::unordered_map<std::uint32_t, KmsConnector> nextById;
  for (KmsConnector const& connector : connectors_) previousById.emplace(connector.connectorId, connector);
  for (KmsConnector const& connector : next) nextById.emplace(connector.connectorId, connector);

  bool const hasApplication = ::lambdaui::Application::hasInstance();

  for (KmsConnector const& connector : connectors_) {
    if (nextById.contains(connector.connectorId)) continue;
    debugLog("KMS output removed: %s", connector.name.c_str());
    if (hasApplication) {
      ::lambdaui::Application::instance().eventQueue().post(
          WindowLifecycleEvent{.kind = WindowLifecycleEvent::Kind::OutputRemoved,
                               .handle = 0,
                               .window = nullptr,
                               .outputName = connector.name});
    }
    for (KmsWindow* window : windows_) {
      if (hasApplication && window && window->connectorId() == connector.connectorId) {
        ::lambdaui::Application::instance().eventQueue().post(WindowEvent{.kind = WindowEvent::Kind::CloseRequest,
                                                                      .handle = window->handle(),
                                                                      .size = {},
                                                                      .dpi = 1.f,
                                                                      .dpiX = 1.f,
                                                                      .dpiY = 1.f});
      }
    }
  }

  for (KmsConnector const& connector : next) {
    auto previous = previousById.find(connector.connectorId);
    if (previous == previousById.end()) {
      debugLog("KMS output added: %s", connector.name.c_str());
      if (hasApplication) {
        ::lambdaui::Application::instance().eventQueue().post(
            WindowLifecycleEvent{.kind = WindowLifecycleEvent::Kind::OutputAdded,
                                 .handle = 0,
                                 .window = nullptr,
                                 .outputName = connector.name});
      }
      continue;
    }
    bool const modeChanged = connectorModeChanged(previous->second, connector);
    bool const dpiChanged = connectorDpiChanged(previous->second, connector);
    if (!modeChanged && !dpiChanged && previous->second.name == connector.name) continue;

    debugLog("KMS output changed: %s", connector.name.c_str());
    for (KmsWindow* window : windows_) {
      if (!window || window->connectorId() != connector.connectorId) continue;
      window->updateConnector(connector);
      if (hasApplication && dpiChanged) {
        ::lambdaui::Application::instance().eventQueue().post(WindowEvent{.kind = WindowEvent::Kind::DpiChanged,
                                                                      .handle = window->handle(),
                                                                      .size = {},
                                                                      .dpi = 1.f,
                                                                      .dpiX = 1.f,
                                                                      .dpiY = 1.f});
      }
      if (hasApplication && modeChanged) {
        ::lambdaui::Application::instance().eventQueue().post(WindowEvent{.kind = WindowEvent::Kind::Resize,
                                                                      .handle = window->handle(),
                                                                      .size = window->currentSize(),
                                                                      .dpi = 1.f,
                                                                      .dpiX = 1.f,
                                                                      .dpiY = 1.f});
      }
    }
  }

  connectors_ = std::move(next);
  if (hasApplication) {
    ::lambdaui::Application::instance().eventQueue().dispatch();
  }
}

void KmsApplication::setApplicationName(std::string name) {
  appName_ = sanitizeAppName(std::move(name));
}

std::string KmsApplication::applicationName() const {
  return appName_.empty() ? "lambda" : appName_;
}

void KmsApplication::setMenuBar(MenuBar const& menu, platform::MenuActionDispatcher dispatcher) {
  claimedShortcuts_.clear();
  collectShortcuts(menu);
  dispatcher_ = std::move(dispatcher);
}

void KmsApplication::setTerminateHandler(std::function<void()> handler) {
  terminateHandler_ = std::move(handler);
}

void KmsApplication::requestTerminate() {
  if (!terminateRequested_.exchange(true) && terminateHandler_) {
    terminateHandler_();
  }
  wakeEventLoop();
}

std::unordered_set<platform::ShortcutKey, platform::ShortcutKeyHash> KmsApplication::menuClaimedShortcuts() const {
  return claimedShortcuts_;
}

void KmsApplication::revalidateMenuItems(std::function<bool(std::string const&)>) {}

std::string KmsApplication::userDataDir() const {
  return appDir(envOr("XDG_DATA_HOME", envOr("HOME", ".") + "/.local/share"), applicationName());
}

std::string KmsApplication::cacheDir() const {
  return appDir(envOr("XDG_CACHE_HOME", envOr("HOME", ".") + "/.cache"), applicationName());
}

std::vector<std::string> KmsApplication::availableOutputs() const {
  std::vector<std::string> outputs;
  outputs.reserve(connectors_.size());
  for (KmsConnector const& connector : connectors_) {
    outputs.push_back(connector.name);
  }
  return outputs;
}

std::span<char const* const> KmsApplication::requiredInstanceExtensions() const {
  static std::vector<char const*> exts = [] {
    std::vector<char const*> result{
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_DISPLAY_EXTENSION_NAME,
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
    };
    if (instanceExtensionAvailable(VK_EXT_DIRECT_MODE_DISPLAY_EXTENSION_NAME)) {
      result.push_back(VK_EXT_DIRECT_MODE_DISPLAY_EXTENSION_NAME);
    }
    if (instanceExtensionAvailable(VK_EXT_ACQUIRE_DRM_DISPLAY_EXTENSION_NAME)) {
      result.push_back(VK_EXT_ACQUIRE_DRM_DISPLAY_EXTENSION_NAME);
    }
    return result;
  }();
  return exts;
}

VkSurfaceKHR KmsApplication::createSurface(VkInstance instance, void* nativeHandle) {
  auto* connector = static_cast<KmsConnector*>(nativeHandle);
  if (!connector) throw std::runtime_error("Invalid KMS Vulkan surface handle");

  std::uint32_t deviceCount = 0;
  vkCheck(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr), "vkEnumeratePhysicalDevices");
  std::vector<VkPhysicalDevice> devices(deviceCount);
  vkCheck(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()), "vkEnumeratePhysicalDevices");

  struct DisplayCandidate {
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDisplayPropertiesKHR display{};
    int score = 0;
  };
  std::vector<DisplayCandidate> candidates;
  auto getDrmDisplay = reinterpret_cast<PFN_vkGetDrmDisplayEXT>(
      vkGetInstanceProcAddr(instance, "vkGetDrmDisplayEXT"));
  auto acquireDrmDisplay = reinterpret_cast<PFN_vkAcquireDrmDisplayEXT>(
      vkGetInstanceProcAddr(instance, "vkAcquireDrmDisplayEXT"));
  bool const hasDrmDisplayExtension = getDrmDisplay != nullptr;
  bool drmDisplayMapped = false;
  std::vector<std::string> diagnostics;

  for (VkPhysicalDevice physical : devices) {
    VkPhysicalDeviceProperties physicalProps{};
    vkGetPhysicalDeviceProperties(physical, &physicalProps);
    if (getDrmDisplay) {
      VkDisplayKHR drmDisplay = VK_NULL_HANDLE;
      VkResult const getResult = getDrmDisplay(physical, drmFd_, connector->connectorId, &drmDisplay);
      diagnostics.push_back(std::string("device ") + physicalProps.deviceName + ": vkGetDrmDisplayEXT=" +
                            vkResultName(getResult));
      if (getResult == VK_SUCCESS && drmDisplay != VK_NULL_HANDLE) {
        drmDisplayMapped = true;
        if (acquireDrmDisplay) {
          VkResult const acquireResult = acquireDrmDisplay(physical, drmFd_, drmDisplay);
          if (acquireResult != VK_SUCCESS && acquireResult != VK_ERROR_INITIALIZATION_FAILED) {
            std::fprintf(stderr,
                         "[lambda:kms] vkAcquireDrmDisplayEXT failed for connector %s with %s (%d); trying surface creation anyway.\n",
                         connector->name.c_str(), vkResultName(acquireResult), static_cast<int>(acquireResult));
          }
        }
        candidates.push_back(DisplayCandidate{physical, drmMappedDisplayProperties(drmDisplay, *connector), 20'000});
      }
    }

    std::uint32_t displayCount = 0;
    if (vkGetPhysicalDeviceDisplayPropertiesKHR(physical, &displayCount, nullptr) != VK_SUCCESS || displayCount == 0) {
      diagnostics.push_back(std::string("device ") + physicalProps.deviceName + ": no VK_KHR_display displays");
      continue;
    }
    std::vector<VkDisplayPropertiesKHR> displays(displayCount);
    vkGetPhysicalDeviceDisplayPropertiesKHR(physical, &displayCount, displays.data());
    diagnostics.push_back(std::string("device ") + physicalProps.deviceName + ": " +
                          std::to_string(displayCount) + " VK_KHR_display displays");
    for (auto const& display : displays) {
      int score = 0;
      if (displayNameMatches(display.displayName, *connector)) score += 1000;
      if (displaySizeMatches(display, *connector)) score += 300;
      if (display.physicalResolution.width == connector->mode.hdisplay &&
          display.physicalResolution.height == connector->mode.vdisplay) {
        score += 100;
      }
      candidates.push_back(DisplayCandidate{physical, display, score});
    }
  }
  std::sort(candidates.begin(), candidates.end(),
            [](DisplayCandidate const& a, DisplayCandidate const& b) { return a.score > b.score; });

  for (auto const& candidate : candidates) {
    std::uint32_t modeCount = 0;
    if (vkGetDisplayModePropertiesKHR(candidate.physical, candidate.display.display, &modeCount, nullptr) !=
            VK_SUCCESS ||
        modeCount == 0) {
      continue;
    }
    std::vector<VkDisplayModePropertiesKHR> modes(modeCount);
    vkGetDisplayModePropertiesKHR(candidate.physical, candidate.display.display, &modeCount, modes.data());
    std::sort(modes.begin(), modes.end(), [&](auto const& a, auto const& b) {
      int const aExact = modeMatches(a, *connector) ? 2 : (modeResolutionMatches(a, *connector) ? 1 : 0);
      int const bExact = modeMatches(b, *connector) ? 2 : (modeResolutionMatches(b, *connector) ? 1 : 0);
      return aExact > bExact;
    });
    for (auto const& mode : modes) {
      if (!modeResolutionMatches(mode, *connector)) {
        continue;
      }
      if (VkSurfaceKHR surface = tryCreateDisplaySurface(instance, candidate.physical, candidate.display,
                                                         mode.displayMode, mode.parameters.visibleRegion)) {
        return surface;
      }
    }
  }

  for (auto const& candidate : candidates) {
    auto modeInfo = vkStructure<VkDisplayModeCreateInfoKHR>(VK_STRUCTURE_TYPE_DISPLAY_MODE_CREATE_INFO_KHR);
    modeInfo.parameters.visibleRegion = {connector->mode.hdisplay, connector->mode.vdisplay};
    modeInfo.parameters.refreshRate = refreshRateMilliHz(connector->mode);
    VkDisplayModeKHR createdMode = VK_NULL_HANDLE;
    if (vkCreateDisplayModeKHR(candidate.physical, candidate.display.display, &modeInfo, nullptr, &createdMode) !=
        VK_SUCCESS) {
      continue;
    }
    if (VkSurfaceKHR surface = tryCreateDisplaySurface(instance, candidate.physical, candidate.display, createdMode,
                                                       modeInfo.parameters.visibleRegion)) {
      return surface;
    }
  }

  std::fprintf(stderr,
               "[lambda:kms] no Vulkan display surface for connector %s id=%u (%ux%u @ %u mHz); "
               "VK_EXT_acquire_drm_display=%s mapped=%s candidates=%zu.\n",
               connector->name.c_str(), connector->connectorId, connector->mode.hdisplay,
               connector->mode.vdisplay, refreshRateMilliHz(connector->mode),
               hasDrmDisplayExtension ? "yes" : "no", drmDisplayMapped ? "yes" : "no", candidates.size());
  for (std::string const& line : diagnostics) {
    std::fprintf(stderr, "[lambda:kms] %s\n", line.c_str());
  }
  throw std::runtime_error("No Vulkan display surface matched the selected KMS connector");
}

int KmsApplication::inputFd() const noexcept {
  return input_ ? libinput_get_fd(input_) : -1;
}

void KmsApplication::wakeEventLoop() {
  if (wakePipe_[1] < 0) return;
  char const c = 1;
  (void)write(wakePipe_[1], &c, 1);
}

void KmsApplication::drainWakePipe() {
  char buffer[64];
  while (read(wakePipe_[0], buffer, sizeof(buffer)) > 0) {}
}

void KmsApplication::initializeActiveVtWatch() {
  if (ourVt_ <= 0 || activeVtNotifyFd_ >= 0) return;

  activeVtNotifyFd_ = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
  if (activeVtNotifyFd_ < 0) {
    debugLog("inotify_init1 for active VT failed; using periodic fallback: %s", std::strerror(errno));
    return;
  }

  activeVtWatch_ = inotify_add_watch(activeVtNotifyFd_, "/sys/class/tty/tty0/active",
                                     IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE | IN_DELETE_SELF |
                                         IN_MOVE_SELF | IN_IGNORED);
  if (activeVtWatch_ < 0) {
    debugLog("inotify_add_watch for active VT failed; using periodic fallback: %s", std::strerror(errno));
    closeActiveVtWatch();
    return;
  }

  debugLog("watching active VT via inotify fd=%d", activeVtNotifyFd_);
}

void KmsApplication::closeActiveVtWatch() {
  if (activeVtNotifyFd_ >= 0) {
    close(activeVtNotifyFd_);
    activeVtNotifyFd_ = -1;
  }
  activeVtWatch_ = -1;
}

void KmsApplication::handleActiveVt(int activeVt) {
  if (activeVt <= 0 || ourVt_ <= 0) return;
  if (activeVt != ourVt_ && vtForeground_) {
    releaseDrmMasterForVt(false);
  } else if (activeVt == ourVt_ && !vtForeground_) {
    acquireDrmMasterForVt(false);
  }
}

void KmsApplication::drainActiveVtWatch() {
  if (activeVtNotifyFd_ < 0) return;

  bool sawEvent = false;
  bool watchInvalidated = false;
  char buffer[4096];
  for (;;) {
    ssize_t const n = read(activeVtNotifyFd_, buffer, sizeof(buffer));
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) break;
      debugLog("active VT inotify read failed: %s", std::strerror(errno));
      break;
    }
    if (n == 0) break;
    sawEvent = true;

    ssize_t offset = 0;
    while (offset + static_cast<ssize_t>(sizeof(inotify_event)) <= n) {
      auto const* event = reinterpret_cast<inotify_event const*>(buffer + offset);
      if (event->wd == activeVtWatch_ &&
          (event->mask & (IN_IGNORED | IN_DELETE_SELF | IN_MOVE_SELF)) != 0) {
        watchInvalidated = true;
      }
      offset += static_cast<ssize_t>(sizeof(inotify_event)) + static_cast<ssize_t>(event->len);
    }
  }

  if (watchInvalidated) {
    closeActiveVtWatch();
    initializeActiveVtWatch();
  }
  if (sawEvent) {
    handleActiveVt(readActiveVt());
  }
}

void KmsApplication::initializeConsole() {
  int activeVt = readActiveVt();
  ttyFd_ = open("/dev/tty", O_RDWR | O_CLOEXEC);
  if (ttyFd_ < 0) {
    if (activeVt > 0) {
      std::string path = "/dev/tty" + std::to_string(activeVt);
      ttyFd_ = open(path.c_str(), O_RDWR | O_CLOEXEC);
      if (ttyFd_ >= 0) debugLog("using %s for VT switching", path.c_str());
    }
  }
  if (ttyFd_ < 0) {
    debugLog("failed to open a tty for VT switching: %s", std::strerror(errno));
    ourVt_ = activeVt;
    initializeActiveVtWatch();
    return;
  }
  if (ioctl(ttyFd_, VT_GETMODE, &previousVtMode_) != 0) {
    debugLog("VT_GETMODE failed on /dev/tty: %s", std::strerror(errno));
    close(ttyFd_);
    ttyFd_ = -1;
    activeVt = readActiveVt();
    if (activeVt > 0) {
      std::string path = "/dev/tty" + std::to_string(activeVt);
      ttyFd_ = open(path.c_str(), O_RDWR | O_CLOEXEC);
      if (ttyFd_ >= 0) debugLog("retrying VT switching with %s", path.c_str());
    }
    if (ttyFd_ < 0 || ioctl(ttyFd_, VT_GETMODE, &previousVtMode_) != 0) {
      debugLog("VT_GETMODE failed; VT switching fallback will use active VT notifications: %s",
               std::strerror(errno));
      if (ttyFd_ >= 0) {
        close(ttyFd_);
        ttyFd_ = -1;
      }
      ourVt_ = activeVt;
      initializeActiveVtWatch();
      return;
    }
  }
  if (ioctl(ttyFd_, KDGETMODE, &previousConsoleMode_) != 0) {
    previousConsoleMode_ = KD_TEXT;
  }
  if (tcgetattr(ttyFd_, &previousTermios_) == 0) {
    termios compositorTermios = previousTermios_;
    compositorTermios.c_iflag &= static_cast<tcflag_t>(~(BRKINT | ICRNL | INPCK | ISTRIP | IXON));
    compositorTermios.c_oflag &= static_cast<tcflag_t>(~OPOST);
    compositorTermios.c_cflag |= CS8;
    compositorTermios.c_lflag &= static_cast<tcflag_t>(~(ECHO | ICANON | IEXTEN | ISIG));
    compositorTermios.c_cc[VMIN] = 0;
    compositorTermios.c_cc[VTIME] = 0;
    if (tcsetattr(ttyFd_, TCSAFLUSH, &compositorTermios) == 0) {
      terminalConfigured_ = true;
    } else {
      debugLog("tcsetattr compositor mode failed: %s", std::strerror(errno));
    }
  } else {
    debugLog("tcgetattr failed: %s", std::strerror(errno));
  }
  vt_stat state{};
  if (ioctl(ttyFd_, VT_GETSTATE, &state) == 0) {
    ourVt_ = state.v_active;
  } else {
    ourVt_ = readActiveVt();
  }

  vt_mode mode = previousVtMode_;
  mode.mode = VT_PROCESS;
  mode.relsig = SIGUSR1;
  mode.acqsig = SIGUSR2;
  mode.frsig = 0;
  if (ioctl(ttyFd_, VT_SETMODE, &mode) == 0) {
    vtProcessMode_ = true;
  } else {
    debugLog("VT_SETMODE(VT_PROCESS) failed; active VT notification remains enabled: %s", std::strerror(errno));
  }
  if (ioctl(ttyFd_, KDSETMODE, KD_GRAPHICS) != 0) {
    debugLog("KDSETMODE(KD_GRAPHICS) failed: %s", std::strerror(errno));
  }
  consoleInitialized_ = true;
  vtForeground_ = true;
  initializeActiveVtWatch();
  debugLog("initialized VT switching vt=%d processMode=%d", ourVt_, vtProcessMode_ ? 1 : 0);
}

void KmsApplication::restoreConsole() {
  if (ttyFd_ < 0 || !consoleInitialized_) return;
  debugLog("restoring console vt=%d foreground=%d previousMode=%d", ourVt_, vtForeground_ ? 1 : 0,
           previousConsoleMode_);
  acknowledgePendingVtAcquire();
  if (vtProcessMode_) {
    vt_mode mode = previousVtMode_;
    if (mode.mode == VT_PROCESS) mode.mode = VT_AUTO;
    if (ioctl(ttyFd_, VT_SETMODE, &mode) != 0) {
      debugLog("VT_SETMODE restore failed: %s", std::strerror(errno));
      vt_mode autoMode{};
      autoMode.mode = VT_AUTO;
      if (ioctl(ttyFd_, VT_SETMODE, &autoMode) != 0) {
        debugLog("VT_SETMODE(VT_AUTO) fallback failed: %s", std::strerror(errno));
      }
    }
  }
  if (terminalConfigured_) {
    if (tcsetattr(ttyFd_, TCSAFLUSH, &previousTermios_) != 0) {
      debugLog("tcsetattr restore failed: %s", std::strerror(errno));
    }
    tcflush(ttyFd_, TCIFLUSH);
  }
  if (vtForeground_ && ioctl(ttyFd_, KDSETMODE, KD_TEXT) != 0) {
    debugLog("KDSETMODE(KD_TEXT) during shutdown failed: %s", std::strerror(errno));
  }
  consoleInitialized_ = false;
  terminalConfigured_ = false;
  vtProcessMode_ = false;
  debugLog("console restore complete");
}

void KmsApplication::installSignalHandlers() {
  if (signalHandlersInstalled_) return;
  gSignalWakeFd = wakePipe_[1];
  gTerminateSignalPending = 0;
  gVtSignalPending = 0;

  struct sigaction action {};
  sigemptyset(&action.sa_mask);
  action.sa_handler = terminateSignalHandler;

  if (sigaction(SIGINT, &action, &gTerminationSignals.previousSigInt) != 0) {
    gSignalWakeFd = -1;
    throw std::system_error(errno, std::generic_category(), "sigaction(SIGINT)");
  }
  gTerminationSignals.sigIntInstalled = true;

  if (sigaction(SIGTERM, &action, &gTerminationSignals.previousSigTerm) != 0) {
    int const savedErrno = errno;
    restoreTerminationSignalHandlers();
    throw std::system_error(savedErrno, std::generic_category(), "sigaction(SIGTERM)");
  }
  gTerminationSignals.sigTermInstalled = true;

  struct sigaction vtAction {};
  sigemptyset(&vtAction.sa_mask);
  vtAction.sa_handler = vtSignalHandler;
  if (sigaction(SIGUSR1, &vtAction, &gTerminationSignals.previousSigUsr1) != 0) {
    int const savedErrno = errno;
    restoreTerminationSignalHandlers();
    throw std::system_error(savedErrno, std::generic_category(), "sigaction(SIGUSR1)");
  }
  gTerminationSignals.sigUsr1Installed = true;
  if (sigaction(SIGUSR2, &vtAction, &gTerminationSignals.previousSigUsr2) != 0) {
    int const savedErrno = errno;
    restoreTerminationSignalHandlers();
    throw std::system_error(savedErrno, std::generic_category(), "sigaction(SIGUSR2)");
  }
  gTerminationSignals.sigUsr2Installed = true;
  signalHandlersInstalled_ = true;
}

void KmsApplication::uninstallSignalHandlers() {
  if (!signalHandlersInstalled_) return;
  restoreTerminationSignalHandlers();
  signalHandlersInstalled_ = false;
}

void KmsApplication::handlePendingTerminateSignal() {
  if (!gTerminateSignalPending) return;
  gTerminateSignalPending = 0;
  debugLog("termination signal received");
  requestTerminate();
}

void KmsApplication::releaseDrmMasterForVt(bool acknowledge) {
  auto acknowledgeRelease = [&] {
    if (ttyFd_ >= 0 && acknowledge && vtProcessMode_) {
      if (ioctl(ttyFd_, VT_RELDISP, 1) != 0) {
        debugLog("VT_RELDISP release failed: %s", std::strerror(errno));
      }
    }
  };

  if (!vtForeground_) {
    acknowledgeRelease();
    return;
  }
  vtForeground_ = false;
  debugLog("releasing DRM master for VT switch");
  suspendInputForVtSwitch();
  for (KmsWindow* window : windows_) {
    if (window) window->suspendForVtSwitch();
  }
  if (drmFd_ >= 0 && drmMaster_) {
    if (drmDropMaster(drmFd_) != 0) debugLog("drmDropMaster failed: %s", std::strerror(errno));
    drmMaster_ = false;
  }
  acknowledgeRelease();
}

void KmsApplication::acquireDrmMasterForVt(bool acknowledge) {
  auto noteAcquireAck = [&] {
    if (ttyFd_ >= 0 && acknowledge && vtProcessMode_) vtAcquireAckPending_ = true;
  };

  noteAcquireAck();
  if (seat_ && !seatEnabled_) {
    debugLog("deferring VT acquire until libseat enables seat");
    return;
  }

  if (vtForeground_) {
    acknowledgePendingVtAcquire();
    return;
  }
  debugLog("reacquiring DRM master after VT switch");
  if (drmFd_ >= 0 && !drmMaster_) {
    for (int attempt = 0; attempt < 10 && !drmMaster_; ++attempt) {
      if (acquireDrmMaster(drmFd_, "after VT acquire")) {
        drmMaster_ = true;
      } else {
        debugLog("drmSetMaster attempt %d failed: %s", attempt + 1, std::strerror(errno));
        usleep(100'000);
      }
    }
    if (!drmMaster_) {
      requestTerminate();
      return;
    }
  }
  if (ttyFd_ >= 0 && ioctl(ttyFd_, KDSETMODE, KD_GRAPHICS) != 0) {
    debugLog("KDSETMODE(KD_GRAPHICS) after VT acquire failed: %s", std::strerror(errno));
  }
  if (ttyFd_ >= 0 && !terminalConfigured_) {
    termios compositorTermios = previousTermios_;
    compositorTermios.c_iflag &= static_cast<tcflag_t>(~(BRKINT | ICRNL | INPCK | ISTRIP | IXON));
    compositorTermios.c_oflag &= static_cast<tcflag_t>(~OPOST);
    compositorTermios.c_cflag |= CS8;
    compositorTermios.c_lflag &= static_cast<tcflag_t>(~(ECHO | ICANON | IEXTEN | ISIG));
    compositorTermios.c_cc[VMIN] = 0;
    compositorTermios.c_cc[VTIME] = 0;
    if (tcsetattr(ttyFd_, TCSAFLUSH, &compositorTermios) == 0) {
      terminalConfigured_ = true;
    } else {
      debugLog("tcsetattr compositor mode after VT acquire failed: %s", std::strerror(errno));
    }
  }
  vtForeground_ = true;
  resumeInputAfterVtSwitch();
  for (KmsWindow* window : windows_) {
    if (window) window->resumeFromVtSwitch();
  }
  wakeEventLoop();
}

void KmsApplication::acknowledgePendingVtAcquire() {
  if (!vtAcquireAckPending_) return;
  vtAcquireAckPending_ = false;
  if (ttyFd_ >= 0 && vtProcessMode_ && ioctl(ttyFd_, VT_RELDISP, VT_ACKACQ) != 0) {
    debugLog("VT_RELDISP acquire ack failed: %s", std::strerror(errno));
  }
}

void KmsApplication::handlePendingVtSignal() {
  int const signal = static_cast<int>(gVtSignalPending);
  if (!signal) return;
  gVtSignalPending = 0;
  if (signal == SIGUSR1) {
    releaseDrmMasterForVt(true);
  } else if (signal == SIGUSR2) {
    acquireDrmMasterForVt(true);
  }
}

bool KmsApplication::switchSession(int session) {
  if (session <= 0) {
    errno = EINVAL;
    return false;
  }

  if (ourVt_ > 0 && session == ourVt_) {
    debugLog("ignoring request to switch to current VT%d", session);
    return true;
  }

  if (seat_) {
    if (libseat_switch_session(seat_, session) == 0) {
      debugLog("requested libseat session switch to VT%d", session);
      return true;
    }
    int const seatErrno = errno;
    debugLog("libseat_switch_session(%d) failed: %s", session, std::strerror(seatErrno));
    errno = seatErrno;
    return false;
  }

  if (ttyFd_ >= 0) {
    if (ioctl(ttyFd_, VT_ACTIVATE, session) == 0) {
      debugLog("requested kernel VT switch to VT%d", session);
      return true;
    }
    int const vtErrno = errno;
    debugLog("VT_ACTIVATE(%d) failed: %s", session, std::strerror(vtErrno));
    errno = vtErrno;
    return false;
  }

  errno = ENODEV;
  return false;
}

bool KmsApplication::switchAdjacentSession(int direction) {
  if (direction == 0) {
    errno = EINVAL;
    return false;
  }

  int current = 0;
  std::uint16_t allocatedMask = 0;
  bool haveAllocatedMask = false;
  if (ttyFd_ >= 0) {
    vt_stat state{};
    if (ioctl(ttyFd_, VT_GETSTATE, &state) == 0) {
      current = state.v_active;
      allocatedMask = state.v_state;
      haveAllocatedMask = true;
    }
  }
  if (current <= 0) {
    int const active = readActiveVt();
    current = active > 0 ? active : ourVt_;
  }
  if (current <= 0) {
    errno = ENODEV;
    return false;
  }

  std::optional<int> target;
  if (haveAllocatedMask) {
    target = linux_platform::adjacentAllocatedVtSession(current, allocatedMask, direction);
    if (!target) {
      errno = ENODEV;
      debugLog("no adjacent allocated VT for current=VT%d direction=%d allocated=0x%04x",
               current,
               direction,
               static_cast<unsigned int>(allocatedMask));
      return false;
    }
  } else {
    target = linux_platform::adjacentNumberedVtSession(current, direction);
  }
  if (!target) {
    errno = ENODEV;
    return false;
  }

  debugLog("requested adjacent VT switch current=VT%d target=VT%d direction=%d allocated=0x%04x",
           current,
           *target,
           direction,
           static_cast<unsigned int>(allocatedMask));
  return switchSession(*target);
}

void KmsApplication::pollActiveVt() {
  if (ourVt_ <= 0) return;
  if (activeVtNotifyFd_ >= 0) return;

  std::int64_t const now = monotonicMillis();
  if (now < nextActiveVtPollMs_) return;
  nextActiveVtPollMs_ = now + (vtForeground_ ? 250 : 1000);
  handleActiveVt(readActiveVt());
}

platform::KmsPollResult KmsApplication::pollInputAndWakeDetailed(int timeoutMs, std::span<int const> extraFds) {
  handlePendingVtSignal();
  pollActiveVt();
  std::vector<pollfd> fds;
  if (seatFd_ >= 0) fds.push_back({seatFd_, POLLIN, 0});
  if (isVtForeground() && inputFd() >= 0) fds.push_back({inputFd(), POLLIN, 0});
  if (isVtForeground() && udevMonitorFd_ >= 0) fds.push_back({udevMonitorFd_, POLLIN, 0});
  if (activeVtNotifyFd_ >= 0) fds.push_back({activeVtNotifyFd_, POLLIN, 0});
  if (wakePipe_[0] >= 0) fds.push_back({wakePipe_[0], POLLIN, 0});
  std::size_t const firstExtraFd = fds.size();
  for (int fd : extraFds) {
    if (fd >= 0) fds.push_back({fd, POLLIN, 0});
  }
  std::size_t const onePastExtraFd = fds.size();
  for (KmsWindow const* window : windows_) {
    if (!isVtForeground()) break;
    int const timerFd = window ? window->frameTimerFd() : -1;
    if (timerFd >= 0) {
      bool const alreadyPolled = std::any_of(fds.begin(), fds.end(), [&](pollfd const& pollFd) {
        return pollFd.fd == timerFd;
      });
      if (!alreadyPolled) fds.push_back({timerFd, POLLIN, 0});
    }
  }
  int const activeVtFallbackTimeout = vtForeground_ ? 250 : 1000;
  int const effectiveTimeout = ourVt_ > 0 && activeVtNotifyFd_ < 0
                                   ? (timeoutMs < 0 ? activeVtFallbackTimeout
                                                    : std::min(timeoutMs, activeVtFallbackTimeout))
                                   : timeoutMs;
  int rc = poll(fds.data(), fds.size(), effectiveTimeout < 0 ? -1 : effectiveTimeout);
  if (rc < 0) {
    if (errno == EINTR) {
      handlePendingVtSignal();
      pollActiveVt();
      handlePendingTerminateSignal();
      return platform::KmsPollResult{.woke = true, .inputOrSystem = true};
    }
    return {};
  }
  if (rc == 0) {
    handlePendingVtSignal();
    pollActiveVt();
    handlePendingTerminateSignal();
    return {};
  }
  platform::KmsPollResult result{.woke = true};
  for (std::size_t index = 0; index < fds.size(); ++index) {
    auto const& fd = fds[index];
    bool const readable = (fd.revents & (POLLIN | POLLERR | POLLHUP)) != 0;
    if (readable) {
      if (index >= firstExtraFd && index < onePastExtraFd) {
        std::size_t const extraIndex = index - firstExtraFd;
        if (extraIndex < 64) result.extraReadableMask |= (std::uint64_t{1} << extraIndex);
      } else {
        result.inputOrSystem = true;
      }
    }
    if (fd.fd == wakePipe_[0] && (fd.revents & POLLIN)) drainWakePipe();
    if (fd.fd == seatFd_ && (fd.revents & (POLLIN | POLLERR | POLLHUP))) {
      dispatchSeatEvents();
    }
    if (fd.fd == activeVtNotifyFd_ && (fd.revents & (POLLIN | POLLERR | POLLHUP))) {
      drainActiveVtWatch();
    }
    if (fd.fd == udevMonitorFd_ && (fd.revents & (POLLIN | POLLERR | POLLHUP))) {
      drainDrmMonitor();
    }
  }
  handlePendingVtSignal();
  pollActiveVt();
  handlePendingTerminateSignal();
  if (isVtForeground()) dispatchPendingInput();
  return result;
}

bool KmsApplication::pollInputAndWake(int timeoutMs, std::span<int const> extraFds) {
  return pollInputAndWakeDetailed(timeoutMs, extraFds).woke;
}

void KmsApplication::registerWindow(KmsWindow* window) {
  windows_.push_back(window);
  if (!pointerFocus_) pointerFocus_ = window;
}

void KmsApplication::unregisterWindow(KmsWindow* window) {
  windows_.erase(std::remove(windows_.begin(), windows_.end(), window), windows_.end());
  if (pointerFocus_ == window) pointerFocus_ = windows_.empty() ? nullptr : windows_.front();
}

KmsWindow* KmsApplication::focusedWindow() const {
  return pointerFocus_ ? pointerFocus_ : (windows_.empty() ? nullptr : windows_.front());
}

Point KmsApplication::windowOrigin(KmsWindow const* window) const {
  Point origin{};
  for (KmsWindow const* candidate : windows_) {
    if (!candidate || candidate == window) break;
    origin.x += candidate->currentSize().width;
  }
  return origin;
}

Point KmsApplication::clampGlobalPointer(Point position) const {
  if (windows_.empty()) return position;
  float width = 0.f;
  float height = 0.f;
  for (KmsWindow const* window : windows_) {
    if (!window) continue;
    Size const size = window->currentSize();
    width += std::max(1.f, size.width);
    height = std::max(height, std::max(1.f, size.height));
  }
  position.x = std::clamp(position.x, 0.f, std::max(0.f, width - 1.f));
  position.y = std::clamp(position.y, 0.f, std::max(0.f, height - 1.f));
  return position;
}

KmsWindow* KmsApplication::windowAtGlobalPoint(Point position, Point& localPosition) const {
  if (windows_.empty()) return nullptr;
  float x = position.x;
  KmsWindow* fallback = nullptr;
  for (KmsWindow* window : windows_) {
    if (!window) continue;
    fallback = window;
    Size const size = window->currentSize();
    float const width = std::max(1.f, size.width);
    if (x < width) {
      localPosition = window->clampPointer(Point{x, position.y});
      return window;
    }
    x -= width;
  }
  if (!fallback) return nullptr;
  Size const size = fallback->currentSize();
  localPosition = fallback->clampPointer(Point{std::max(0.f, size.width - 1.f), position.y});
  return fallback;
}

void KmsApplication::focusPointerWindow(KmsWindow* window) {
  if (!window) return;
  for (KmsWindow* candidate : windows_) {
    if (candidate && candidate != window) candidate->hideCursor();
  }
  pointerFocus_ = window;
}

KmsWindow* KmsApplication::windowForConnector(std::uint32_t connectorId) const {
  auto it = std::find_if(windows_.begin(), windows_.end(), [&](KmsWindow const* window) {
    return window && window->connectorId() == connectorId;
  });
  return it == windows_.end() ? nullptr : *it;
}

void KmsApplication::collectShortcuts(MenuItem const& item) {
  if (!item.actionName.empty() && (item.shortcut.key != 0 || item.shortcut.modifiers != Modifiers::None)) {
    claimedShortcuts_.insert(platform::ShortcutKey{.key = item.shortcut.key, .modifiers = item.shortcut.modifiers});
  }
  for (MenuItem const& child : item.children) collectShortcuts(child);
}

void KmsApplication::collectShortcuts(MenuBar const& menu) {
  for (MenuItem const& item : menu.menus) collectShortcuts(item);
}

KmsApplication& kmsApplication() {
  if (!gKmsApplication) throw std::runtime_error("KMS application is not initialized");
  return *gKmsApplication;
}

namespace platform {

std::unique_ptr<Application> createApplication() {
  return std::make_unique<KmsApplication>();
}

} // namespace platform
} // namespace lambdaui
