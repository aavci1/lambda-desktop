#include "presentation-time-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <wayland-client.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <string>

namespace {

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

constexpr int kWidth = 160;
constexpr int kHeight = 96;
constexpr int kStride = kWidth * 4;
constexpr int kBufferSize = kStride * kHeight;
constexpr int kDefaultTimeoutMs = 5000;

struct ShmBuffer {
  wl_buffer* buffer = nullptr;
  void* pixels = MAP_FAILED;
  int fd = -1;

  ~ShmBuffer() {
    if (buffer) wl_buffer_destroy(buffer);
    if (pixels != MAP_FAILED) munmap(pixels, kBufferSize);
    if (fd >= 0) close(fd);
  }
};

struct Client {
  wl_display* display = nullptr;
  wl_registry* registry = nullptr;
  wl_compositor* compositor = nullptr;
  wl_shm* shm = nullptr;
  xdg_wm_base* wmBase = nullptr;
  zxdg_decoration_manager_v1* decorationManager = nullptr;
  wp_presentation* presentation = nullptr;
  wl_surface* surface = nullptr;
  xdg_surface* xdgSurface = nullptr;
  xdg_toplevel* toplevel = nullptr;
  zxdg_toplevel_decoration_v1* decoration = nullptr;
  struct wp_presentation_feedback* feedback = nullptr;

  bool hasArgb8888 = false;
  bool configured = false;
  bool closed = false;
  bool feedbackDone = false;
  bool feedbackPresented = false;
  bool feedbackDiscarded = false;
  std::uint32_t clockId = UINT32_MAX;
  std::uint32_t tvSecHi = 0;
  std::uint32_t tvSecLo = 0;
  std::uint32_t tvNsec = 0;
  std::uint32_t refresh = 0;
  std::uint32_t seqHi = 0;
  std::uint32_t seqLo = 0;
  std::uint32_t flags = 0;

  ~Client() {
    if (feedback && !feedbackDone) wl_proxy_destroy(reinterpret_cast<wl_proxy*>(feedback));
    if (decoration) zxdg_toplevel_decoration_v1_destroy(decoration);
    if (toplevel) xdg_toplevel_destroy(toplevel);
    if (xdgSurface) xdg_surface_destroy(xdgSurface);
    if (surface) wl_surface_destroy(surface);
    if (presentation) wp_presentation_destroy(presentation);
    if (decorationManager) zxdg_decoration_manager_v1_destroy(decorationManager);
    if (wmBase) xdg_wm_base_destroy(wmBase);
    if (shm) wl_shm_destroy(shm);
    if (compositor) wl_compositor_destroy(compositor);
    if (registry) wl_registry_destroy(registry);
    if (display) wl_display_disconnect(display);
  }
};

[[noreturn]] void fail(std::string const& message) {
  std::fprintf(stderr, "lambda-presentation-feedback-check: %s\n", message.c_str());
  std::exit(1);
}

int createMemfd(char const* name) {
#ifdef SYS_memfd_create
  if (int fd = static_cast<int>(syscall(SYS_memfd_create, name, MFD_CLOEXEC)); fd >= 0) return fd;
#endif
  char path[] = "/tmp/lambda-presentation-feedback-XXXXXX";
  int fd = mkstemp(path);
  if (fd >= 0) unlink(path);
  return fd;
}

void dispatchUntil(Client& client, bool Client::*condition, int timeoutMs, char const* what) {
  auto const deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
  while (!(client.*condition)) {
    while (wl_display_prepare_read(client.display) != 0) {
      if (wl_display_dispatch_pending(client.display) < 0) {
        fail(std::string("Wayland dispatch failed while waiting for ") + what);
      }
      if (client.*condition) return;
    }

    if (wl_display_flush(client.display) < 0 && errno != EAGAIN) {
      wl_display_cancel_read(client.display);
      fail(std::string("Wayland flush failed while waiting for ") + what + ": " + std::strerror(errno));
    }

    auto const now = std::chrono::steady_clock::now();
    int remainingMs =
        static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
    if (remainingMs <= 0) {
      wl_display_cancel_read(client.display);
      fail(std::string("timed out waiting for ") + what);
    }

    pollfd pfd{.fd = wl_display_get_fd(client.display), .events = POLLIN, .revents = 0};
    int const rc = poll(&pfd, 1, remainingMs);
    if (rc < 0) {
      if (errno == EINTR) {
        wl_display_cancel_read(client.display);
        continue;
      }
      wl_display_cancel_read(client.display);
      fail(std::string("poll failed while waiting for ") + what + ": " + std::strerror(errno));
    }
    if (rc == 0) {
      wl_display_cancel_read(client.display);
      fail(std::string("timed out waiting for ") + what);
    }

    if (wl_display_read_events(client.display) < 0) {
      fail(std::string("Wayland read failed while waiting for ") + what);
    }
    if (wl_display_dispatch_pending(client.display) < 0) {
      fail(std::string("Wayland dispatch failed while waiting for ") + what);
    }
    if (client.closed) fail("compositor closed the verifier window");
  }
}

std::string flagsString(std::uint32_t flags) {
  std::string out;
  auto add = [&](std::uint32_t bit, char const* name) {
    if ((flags & bit) == 0) return;
    if (!out.empty()) out += "|";
    out += name;
  };
  add(WP_PRESENTATION_FEEDBACK_KIND_VSYNC, "VSYNC");
  add(WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK, "HW_CLOCK");
  add(WP_PRESENTATION_FEEDBACK_KIND_HW_COMPLETION, "HW_COMPLETION");
  add(WP_PRESENTATION_FEEDBACK_KIND_ZERO_COPY, "ZERO_COPY");
  if (out.empty()) out = "0";
  return out;
}

void registryGlobal(void* data,
                    wl_registry* registry,
                    std::uint32_t name,
                    char const* interface,
                    std::uint32_t version) {
  auto& client = *static_cast<Client*>(data);
  if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
    client.compositor = static_cast<wl_compositor*>(
        wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 4u)));
  } else if (std::strcmp(interface, wl_shm_interface.name) == 0) {
    client.shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
  } else if (std::strcmp(interface, xdg_wm_base_interface.name) == 0) {
    client.wmBase =
        static_cast<xdg_wm_base*>(wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
  } else if (std::strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
    client.decorationManager = static_cast<zxdg_decoration_manager_v1*>(
        wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, std::min(version, 1u)));
  } else if (std::strcmp(interface, wp_presentation_interface.name) == 0) {
    client.presentation = static_cast<wp_presentation*>(
        wl_registry_bind(registry, name, &wp_presentation_interface, std::min(version, 2u)));
  }
}

void registryRemove(void*, wl_registry*, std::uint32_t) {}

wl_registry_listener const kRegistryListener{
    .global = registryGlobal,
    .global_remove = registryRemove,
};

void shmFormat(void* data, wl_shm*, std::uint32_t format) {
  auto& client = *static_cast<Client*>(data);
  if (format == WL_SHM_FORMAT_ARGB8888) client.hasArgb8888 = true;
}

wl_shm_listener const kShmListener{
    .format = shmFormat,
};

void wmBasePing(void*, xdg_wm_base* wmBase, std::uint32_t serial) {
  xdg_wm_base_pong(wmBase, serial);
}

xdg_wm_base_listener const kWmBaseListener{
    .ping = wmBasePing,
};

void xdgSurfaceConfigure(void* data, xdg_surface* surface, std::uint32_t serial) {
  auto& client = *static_cast<Client*>(data);
  xdg_surface_ack_configure(surface, serial);
  client.configured = true;
}

xdg_surface_listener const kXdgSurfaceListener{
    .configure = xdgSurfaceConfigure,
};

void toplevelConfigure(void*, xdg_toplevel*, std::int32_t, std::int32_t, wl_array*) {}

void toplevelClose(void* data, xdg_toplevel*) {
  static_cast<Client*>(data)->closed = true;
}

void toplevelConfigureBounds(void*, xdg_toplevel*, std::int32_t, std::int32_t) {}

void toplevelWmCapabilities(void*, xdg_toplevel*, wl_array*) {}

xdg_toplevel_listener const kToplevelListener{
    .configure = toplevelConfigure,
    .close = toplevelClose,
    .configure_bounds = toplevelConfigureBounds,
    .wm_capabilities = toplevelWmCapabilities,
};

void decorationConfigure(void*, zxdg_toplevel_decoration_v1*, std::uint32_t) {}

zxdg_toplevel_decoration_v1_listener const kDecorationListener{
    .configure = decorationConfigure,
};

void presentationClockId(void* data, wp_presentation*, std::uint32_t clockId) {
  static_cast<Client*>(data)->clockId = clockId;
}

wp_presentation_listener const kPresentationListener{
    .clock_id = presentationClockId,
};

void feedbackSyncOutput(void*, struct wp_presentation_feedback*, wl_output*) {}

void feedbackPresented(void* data,
                       struct wp_presentation_feedback*,
                       std::uint32_t tvSecHi,
                       std::uint32_t tvSecLo,
                       std::uint32_t tvNsec,
                       std::uint32_t refresh,
                       std::uint32_t seqHi,
                       std::uint32_t seqLo,
                       std::uint32_t flags) {
  auto& client = *static_cast<Client*>(data);
  client.tvSecHi = tvSecHi;
  client.tvSecLo = tvSecLo;
  client.tvNsec = tvNsec;
  client.refresh = refresh;
  client.seqHi = seqHi;
  client.seqLo = seqLo;
  client.flags = flags;
  client.feedbackPresented = true;
  client.feedbackDone = true;
}

void feedbackDiscarded(void* data, struct wp_presentation_feedback*) {
  auto& client = *static_cast<Client*>(data);
  client.feedbackDiscarded = true;
  client.feedbackDone = true;
}

wp_presentation_feedback_listener const kFeedbackListener{
    .sync_output = feedbackSyncOutput,
    .presented = feedbackPresented,
    .discarded = feedbackDiscarded,
};

ShmBuffer createBuffer(wl_shm* shm) {
  ShmBuffer result;
  result.fd = createMemfd("lambda-presentation-feedback");
  if (result.fd < 0) fail(std::string("memfd/tmpfile creation failed: ") + std::strerror(errno));
  if (ftruncate(result.fd, kBufferSize) != 0) fail(std::string("ftruncate failed: ") + std::strerror(errno));
  result.pixels = mmap(nullptr, kBufferSize, PROT_READ | PROT_WRITE, MAP_SHARED, result.fd, 0);
  if (result.pixels == MAP_FAILED) fail(std::string("mmap failed: ") + std::strerror(errno));

  auto* pixels = static_cast<std::uint32_t*>(result.pixels);
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      std::uint8_t const r = static_cast<std::uint8_t>((x * 255) / std::max(1, kWidth - 1));
      std::uint8_t const g = static_cast<std::uint8_t>((y * 255) / std::max(1, kHeight - 1));
      std::uint8_t const b = 0x90;
      pixels[y * kWidth + x] = (0xffu << 24u) | (static_cast<std::uint32_t>(r) << 16u) |
                               (static_cast<std::uint32_t>(g) << 8u) | b;
    }
  }

  wl_shm_pool* pool = wl_shm_create_pool(shm, result.fd, kBufferSize);
  if (!pool) fail("wl_shm_create_pool failed");
  result.buffer = wl_shm_pool_create_buffer(pool, 0, kWidth, kHeight, kStride, WL_SHM_FORMAT_ARGB8888);
  wl_shm_pool_destroy(pool);
  if (!result.buffer) fail("wl_shm_pool_create_buffer failed");
  return result;
}

int timeoutMsFromEnv() {
  if (char const* value = std::getenv("LAMBDA_PRESENTATION_FEEDBACK_TIMEOUT_MS"); value && *value) {
    char* end = nullptr;
    long parsed = std::strtol(value, &end, 10);
    if (end != value && parsed > 0 && parsed < 60000) return static_cast<int>(parsed);
  }
  return kDefaultTimeoutMs;
}

int holdMsFromEnv() {
  if (char const* value = std::getenv("LAMBDA_PRESENTATION_FEEDBACK_HOLD_MS"); value && *value) {
    char* end = nullptr;
    long parsed = std::strtol(value, &end, 10);
    if (end != value && parsed > 0 && parsed < 60000) return static_cast<int>(parsed);
  }
  return 0;
}

bool envNonZero(char const* name) {
  char const* value = std::getenv(name);
  return value && *value && std::strcmp(value, "0") != 0 && std::strcmp(value, "false") != 0;
}

bool requestServerDecoration() {
  return envNonZero("LAMBDA_PRESENTATION_FEEDBACK_REQUEST_SERVER_DECORATION");
}

void holdMapped(Client& client, int holdMs) {
  if (holdMs <= 0) return;
  auto const deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(holdMs);
  while (!client.closed) {
    if (wl_display_dispatch_pending(client.display) < 0) {
      fail("Wayland dispatch failed while holding mapped surface");
    }
    if (wl_display_flush(client.display) < 0 && errno != EAGAIN) {
      fail(std::string("Wayland flush failed while holding mapped surface: ") + std::strerror(errno));
    }
    auto const now = std::chrono::steady_clock::now();
    if (now >= deadline) return;
    int const remainingMs =
        static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
    pollfd pfd{.fd = wl_display_get_fd(client.display), .events = POLLIN, .revents = 0};
    int const rc = poll(&pfd, 1, std::min(remainingMs, 250));
    if (rc < 0 && errno != EINTR) {
      fail(std::string("poll failed while holding mapped surface: ") + std::strerror(errno));
    }
    if (rc > 0 && wl_display_dispatch(client.display) < 0) {
      fail("Wayland dispatch failed while holding mapped surface");
    }
  }
}

} // namespace

int main() {
  Client client;
  int const timeoutMs = timeoutMsFromEnv();
  int const holdMs = holdMsFromEnv();
  bool const requireHardwareFlags = envNonZero("LAMBDA_PRESENTATION_FEEDBACK_REQUIRE_HARDWARE_FLAGS");

  client.display = wl_display_connect(nullptr);
  if (!client.display) fail("wl_display_connect failed; set WAYLAND_DISPLAY to an active compositor");

  client.registry = wl_display_get_registry(client.display);
  wl_registry_add_listener(client.registry, &kRegistryListener, &client);
  if (wl_display_roundtrip(client.display) < 0) fail("initial registry roundtrip failed");

  if (!client.compositor) fail("wl_compositor global not found");
  if (!client.shm) fail("wl_shm global not found");
  if (!client.wmBase) fail("xdg_wm_base global not found");
  if (!client.presentation) fail("wp_presentation global not found");

  wl_shm_add_listener(client.shm, &kShmListener, &client);
  xdg_wm_base_add_listener(client.wmBase, &kWmBaseListener, &client);
  wp_presentation_add_listener(client.presentation, &kPresentationListener, &client);
  if (wl_display_roundtrip(client.display) < 0) fail("global setup roundtrip failed");
  if (!client.hasArgb8888) fail("wl_shm did not advertise ARGB8888");
  if (client.clockId != CLOCK_MONOTONIC) fail("wp_presentation did not advertise CLOCK_MONOTONIC");

  client.surface = wl_compositor_create_surface(client.compositor);
  if (!client.surface) fail("wl_compositor_create_surface failed");
  client.xdgSurface = xdg_wm_base_get_xdg_surface(client.wmBase, client.surface);
  if (!client.xdgSurface) fail("xdg_wm_base_get_xdg_surface failed");
  xdg_surface_add_listener(client.xdgSurface, &kXdgSurfaceListener, &client);
  client.toplevel = xdg_surface_get_toplevel(client.xdgSurface);
  if (!client.toplevel) fail("xdg_surface_get_toplevel failed");
  xdg_toplevel_add_listener(client.toplevel, &kToplevelListener, &client);
  xdg_toplevel_set_title(client.toplevel, "Lambda Presentation Feedback Check");
  xdg_toplevel_set_app_id(client.toplevel, "lambda-presentation-feedback-check");
  if (requestServerDecoration()) {
    if (!client.decorationManager) fail("xdg-decoration manager not found");
    client.decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(client.decorationManager, client.toplevel);
    if (!client.decoration) fail("xdg toplevel decoration creation failed");
    zxdg_toplevel_decoration_v1_add_listener(client.decoration, &kDecorationListener, &client);
    zxdg_toplevel_decoration_v1_set_mode(client.decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
  }
  wl_surface_commit(client.surface);
  dispatchUntil(client, &Client::configured, timeoutMs, "initial xdg configure");

  ShmBuffer buffer = createBuffer(client.shm);
  client.feedback = wp_presentation_feedback(client.presentation, client.surface);
  wp_presentation_feedback_add_listener(client.feedback, &kFeedbackListener, &client);
  wl_surface_attach(client.surface, buffer.buffer, 0, 0);
  wl_surface_damage_buffer(client.surface, 0, 0, kWidth, kHeight);
  wl_surface_commit(client.surface);
  dispatchUntil(client, &Client::feedbackDone, timeoutMs, "presentation feedback");

  if (client.feedbackDiscarded || !client.feedbackPresented) fail("presentation feedback was discarded");
  if (client.tvNsec >= 1'000'000'000u) fail("presentation tv_nsec is outside [0, 999999999]");
  if ((static_cast<std::uint64_t>(client.tvSecHi) << 32u | client.tvSecLo) == 0) {
    fail("presentation timestamp seconds are zero");
  }
  if (client.refresh == 0) fail("presentation refresh is zero");
  std::uint64_t const seq = (static_cast<std::uint64_t>(client.seqHi) << 32u) | client.seqLo;
  if (seq == 0) fail("presentation sequence is zero");
  constexpr std::uint32_t requiredFlags = WP_PRESENTATION_FEEDBACK_KIND_VSYNC |
                                          WP_PRESENTATION_FEEDBACK_KIND_HW_CLOCK |
                                          WP_PRESENTATION_FEEDBACK_KIND_HW_COMPLETION;
  if (requireHardwareFlags && (client.flags & requiredFlags) != requiredFlags) {
    fail("presentation flags missing required hardware/vsync bits: " + flagsString(client.flags));
  }

  std::printf("lambda-presentation-feedback-check: presented clock=CLOCK_MONOTONIC tv_sec=%llu "
              "tv_nsec=%u refresh_nsec=%u seq=%llu flags=%s hardware_flags_required=%d hold_ms=%d\n",
              static_cast<unsigned long long>((static_cast<std::uint64_t>(client.tvSecHi) << 32u) |
                                              client.tvSecLo),
              client.tvNsec,
              client.refresh,
              static_cast<unsigned long long>(seq),
              flagsString(client.flags).c_str(),
              requireHardwareFlags ? 1 : 0,
              holdMs);
  std::fflush(stdout);
  holdMapped(client, holdMs);
  return 0;
}
