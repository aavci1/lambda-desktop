#include "Shell/ShellAudioControl.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>

#if defined(LAMBDA_SHELL_HAVE_LIBPULSE)
#include <pulse/pulseaudio.h>
#endif

namespace lambda_shell {
namespace {

inline constexpr char const* kDefaultWpctlSink = "@DEFAULT_AUDIO_SINK@";
inline constexpr char const* kDefaultPulseSink = "@DEFAULT_SINK@";
inline constexpr char const* kAlsaMasterControl = "Master";
inline constexpr char const* kVolumeStep = "5%";

std::string lowerAscii(std::string_view value) {
  std::string output(value);
  std::transform(output.begin(), output.end(), output.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return output;
}

bool containsAscii(std::string_view value, std::string_view needle) {
  return lowerAscii(value).find(needle) != std::string::npos;
}

int clampedPercent(double percent) {
  if (!std::isfinite(percent)) return 0;
  return std::clamp(static_cast<int>(std::lround(percent)), 0, 999);
}

std::optional<int> parseFirstPercent(std::string_view text) {
  std::size_t const percent = text.find('%');
  if (percent == std::string_view::npos) return std::nullopt;

  std::size_t first = percent;
  while (first > 0 && std::isdigit(static_cast<unsigned char>(text[first - 1])) != 0) {
    --first;
  }
  if (first == percent) return std::nullopt;

  int value = 0;
  for (std::size_t index = first; index < percent; ++index) {
    value = value * 10 + (text[index] - '0');
  }
  return std::clamp(value, 0, 999);
}

std::optional<AudioVolumeState> parseWpctlVolume(std::string_view output) {
  std::size_t const marker = output.find("Volume:");
  if (marker == std::string_view::npos) return std::nullopt;

  std::string const tail(output.substr(marker + std::string_view("Volume:").size()));
  char* end = nullptr;
  errno = 0;
  double const volume = std::strtod(tail.c_str(), &end);
  if (tail.c_str() == end || errno == ERANGE || !std::isfinite(volume)) return std::nullopt;

  return AudioVolumeState{
      .available = true,
      .muted = containsAscii(output, "muted"),
      .percent = clampedPercent(volume * 100.0),
      .backend = AudioBackend::WirePlumber,
  };
}

std::optional<AudioVolumeState> parsePactlVolume(std::string_view volumeOutput,
                                                 std::string_view muteOutput) {
  auto percent = parseFirstPercent(volumeOutput);
  if (!percent) return std::nullopt;

  return AudioVolumeState{
      .available = true,
      .muted = containsAscii(muteOutput, "yes"),
      .percent = *percent,
      .backend = AudioBackend::PulseAudio,
  };
}

std::optional<AudioVolumeState> parseAlsaVolume(std::string_view output) {
  auto percent = parseFirstPercent(output);
  if (!percent) return std::nullopt;

  return AudioVolumeState{
      .available = true,
      .muted = containsAscii(output, "[off]"),
      .percent = *percent,
      .backend = AudioBackend::Alsa,
  };
}

#if defined(LAMBDA_SHELL_HAVE_LIBPULSE)
class PulseAudioLibraryClient {
public:
  ~PulseAudioLibraryClient() {
    std::lock_guard lock(mutex_);
    if (context_) {
      pa_context_disconnect(context_);
      pa_context_unref(context_);
      context_ = nullptr;
    }
    if (loop_) {
      pa_threaded_mainloop_stop(loop_);
      pa_threaded_mainloop_free(loop_);
      loop_ = nullptr;
    }
  }

  std::optional<AudioVolumeState> readState() {
    std::lock_guard lock(mutex_);
    if (!ensureConnected()) return std::nullopt;

    pa_threaded_mainloop_lock(loop_);
    bool const ready = refreshDefaultSinkLocked();
    auto info = ready ? readSinkInfoLocked() : std::optional<SinkInfo>{};
    pa_threaded_mainloop_unlock(loop_);
    if (!info) return std::nullopt;

    return AudioVolumeState{
        .available = true,
        .muted = info->muted,
        .percent = info->percent,
        .backend = AudioBackend::PulseAudioNative,
    };
  }

  bool adjustVolume(int deltaPercent) {
    if (deltaPercent == 0) return false;
    std::lock_guard lock(mutex_);
    if (!ensureConnected()) return false;

    pa_threaded_mainloop_lock(loop_);
    bool const ready = refreshDefaultSinkLocked();
    auto info = ready ? readSinkInfoLocked() : std::optional<SinkInfo>{};
    bool changed = false;
    if (info) {
      pa_cvolume volume = info->volume;
      pa_volume_t const step = static_cast<pa_volume_t>(
          std::lround(static_cast<double>(PA_VOLUME_NORM) * std::abs(deltaPercent) / 100.0));
      for (std::uint8_t index = 0; index < volume.channels; ++index) {
        if (deltaPercent > 0) {
          pa_volume_t const room = PA_VOLUME_NORM > volume.values[index]
                                       ? PA_VOLUME_NORM - volume.values[index]
                                       : 0;
          volume.values[index] += std::min(room, step);
        } else {
          volume.values[index] = volume.values[index] > step ? volume.values[index] - step : PA_VOLUME_MUTED;
        }
      }
      changed = setSinkVolumeLocked(volume);
    }
    pa_threaded_mainloop_unlock(loop_);
    return changed;
  }

  bool toggleMute() {
    std::lock_guard lock(mutex_);
    if (!ensureConnected()) return false;

    pa_threaded_mainloop_lock(loop_);
    bool const ready = refreshDefaultSinkLocked();
    auto info = ready ? readSinkInfoLocked() : std::optional<SinkInfo>{};
    bool changed = info ? setSinkMuteLocked(!info->muted) : false;
    pa_threaded_mainloop_unlock(loop_);
    return changed;
  }

private:
  struct SinkInfo {
    bool muted = false;
    int percent = 0;
    pa_cvolume volume{};
  };

  struct ServerInfoRequest {
    PulseAudioLibraryClient* client = nullptr;
    std::string defaultSink;
    bool done = false;
  };

  struct SinkInfoRequest {
    PulseAudioLibraryClient* client = nullptr;
    SinkInfo info;
    bool found = false;
    bool done = false;
  };

  struct SuccessRequest {
    PulseAudioLibraryClient* client = nullptr;
    bool success = false;
    bool done = false;
  };

  static void contextStateCallback(pa_context*, void* userdata) {
    auto* client = static_cast<PulseAudioLibraryClient*>(userdata);
    if (client && client->loop_) pa_threaded_mainloop_signal(client->loop_, 0);
  }

  static void serverInfoCallback(pa_context*, pa_server_info const* info, void* userdata) {
    auto* request = static_cast<ServerInfoRequest*>(userdata);
    if (!request || !request->client) return;
    if (info && info->default_sink_name) {
      request->defaultSink = info->default_sink_name;
    }
    request->done = true;
    pa_threaded_mainloop_signal(request->client->loop_, 0);
  }

  static void sinkInfoCallback(pa_context*, pa_sink_info const* info, int eol, void* userdata) {
    auto* request = static_cast<SinkInfoRequest*>(userdata);
    if (!request || !request->client) return;
    if (eol != 0) {
      request->done = true;
      pa_threaded_mainloop_signal(request->client->loop_, 0);
      return;
    }
    if (info) {
      request->info.muted = info->mute != 0;
      request->info.volume = info->volume;
      pa_volume_t const average = pa_cvolume_avg(&info->volume);
      request->info.percent = clampedPercent(
          static_cast<double>(average) * 100.0 / static_cast<double>(PA_VOLUME_NORM));
      request->found = true;
    }
  }

  static void successCallback(pa_context*, int success, void* userdata) {
    auto* request = static_cast<SuccessRequest*>(userdata);
    if (!request || !request->client) return;
    request->success = success != 0;
    request->done = true;
    pa_threaded_mainloop_signal(request->client->loop_, 0);
  }

  bool ensureConnected() {
    if (context_ && loop_) {
      pa_threaded_mainloop_lock(loop_);
      bool const ready = pa_context_get_state(context_) == PA_CONTEXT_READY;
      pa_threaded_mainloop_unlock(loop_);
      if (ready) return true;
    }

    cleanup();
    loop_ = pa_threaded_mainloop_new();
    if (!loop_) return false;
    context_ = pa_context_new(pa_threaded_mainloop_get_api(loop_), "lambda-shell");
    if (!context_) {
      cleanup();
      return false;
    }
    pa_context_set_state_callback(context_, contextStateCallback, this);
    if (pa_context_connect(context_, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
      cleanup();
      return false;
    }
    if (pa_threaded_mainloop_start(loop_) < 0) {
      cleanup();
      return false;
    }

    pa_threaded_mainloop_lock(loop_);
    while (true) {
      pa_context_state_t const state = pa_context_get_state(context_);
      if (state == PA_CONTEXT_READY) {
        pa_threaded_mainloop_unlock(loop_);
        return true;
      }
      if (!PA_CONTEXT_IS_GOOD(state)) {
        pa_threaded_mainloop_unlock(loop_);
        cleanup();
        return false;
      }
      pa_threaded_mainloop_wait(loop_);
    }
  }

  void cleanup() {
    if (context_) {
      pa_context_disconnect(context_);
      pa_context_unref(context_);
      context_ = nullptr;
    }
    if (loop_) {
      pa_threaded_mainloop_stop(loop_);
      pa_threaded_mainloop_free(loop_);
      loop_ = nullptr;
    }
    defaultSink_.clear();
  }

  bool waitForOperationLocked(pa_operation* operation) {
    if (!operation) return false;
    while (pa_operation_get_state(operation) == PA_OPERATION_RUNNING) {
      pa_threaded_mainloop_wait(loop_);
    }
    bool const done = pa_operation_get_state(operation) == PA_OPERATION_DONE;
    pa_operation_unref(operation);
    return done;
  }

  bool refreshDefaultSinkLocked() {
    ServerInfoRequest request{.client = this};
    pa_operation* operation = pa_context_get_server_info(context_, serverInfoCallback, &request);
    bool const done = waitForOperationLocked(operation);
    if (!done || !request.done || request.defaultSink.empty()) return false;
    defaultSink_ = std::move(request.defaultSink);
    return true;
  }

  std::optional<SinkInfo> readSinkInfoLocked() {
    if (defaultSink_.empty()) return std::nullopt;
    SinkInfoRequest request{.client = this};
    pa_operation* operation =
        pa_context_get_sink_info_by_name(context_, defaultSink_.c_str(), sinkInfoCallback, &request);
    bool const done = waitForOperationLocked(operation);
    if (!done || !request.done || !request.found) return std::nullopt;
    return request.info;
  }

  bool setSinkVolumeLocked(pa_cvolume const& volume) {
    if (defaultSink_.empty()) return false;
    SuccessRequest request{.client = this};
    pa_operation* operation =
        pa_context_set_sink_volume_by_name(context_, defaultSink_.c_str(), &volume, successCallback, &request);
    bool const done = waitForOperationLocked(operation);
    return done && request.done && request.success;
  }

  bool setSinkMuteLocked(bool muted) {
    if (defaultSink_.empty()) return false;
    SuccessRequest request{.client = this};
    pa_operation* operation =
        pa_context_set_sink_mute_by_name(context_, defaultSink_.c_str(), muted ? 1 : 0, successCallback, &request);
    bool const done = waitForOperationLocked(operation);
    return done && request.done && request.success;
  }

  std::mutex mutex_;
  pa_threaded_mainloop* loop_ = nullptr;
  pa_context* context_ = nullptr;
  std::string defaultSink_;
};

PulseAudioLibraryClient& pulseAudioLibraryClient() {
  static PulseAudioLibraryClient client;
  return client;
}
#endif

AudioCommandResult runProcess(std::vector<std::string> const& command) {
  if (command.empty()) return {};

  int pipeFd[2] = {-1, -1};
  if (pipe(pipeFd) != 0) return {};

  pid_t const pid = fork();
  if (pid < 0) {
    close(pipeFd[0]);
    close(pipeFd[1]);
    return {};
  }

  if (pid == 0) {
    close(pipeFd[0]);
    dup2(pipeFd[1], STDOUT_FILENO);
    dup2(pipeFd[1], STDERR_FILENO);
    close(pipeFd[1]);

    std::vector<char*> argv;
    argv.reserve(command.size() + 1);
    for (std::string const& arg : command) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);
  }

  close(pipeFd[1]);

  std::string output;
  char buffer[4096];
  while (true) {
    ssize_t const count = read(pipeFd[0], buffer, sizeof(buffer));
    if (count > 0) {
      output.append(buffer, static_cast<std::size_t>(count));
      if (output.size() > 65536) break;
      continue;
    }
    if (count == 0) break;
    if (errno == EINTR) continue;
    break;
  }
  close(pipeFd[0]);

  int status = 0;
  while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}

  int exitCode = -1;
  if (WIFEXITED(status)) {
    exitCode = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    exitCode = 128 + WTERMSIG(status);
  }
  return {.exitCode = exitCode, .output = std::move(output)};
}

std::optional<AudioVolumeState> readWirePlumber(AudioControlContext const& context) {
  AudioCommandResult const result = context.run({"wpctl", "get-volume", kDefaultWpctlSink});
  if (result.exitCode != 0) return std::nullopt;
  return parseWpctlVolume(result.output);
}

std::optional<AudioVolumeState> readPulseAudio(AudioControlContext const& context) {
  AudioCommandResult const volume = context.run({"pactl", "get-sink-volume", kDefaultPulseSink});
  if (volume.exitCode != 0) return std::nullopt;
  AudioCommandResult const mute = context.run({"pactl", "get-sink-mute", kDefaultPulseSink});
  std::string_view const muteText = mute.exitCode == 0 ? std::string_view{mute.output} : std::string_view{};
  return parsePactlVolume(volume.output, muteText);
}

std::optional<AudioVolumeState> readAlsa(AudioControlContext const& context) {
  AudioCommandResult const result = context.run({"amixer", "sget", kAlsaMasterControl});
  if (result.exitCode != 0) return std::nullopt;
  return parseAlsaVolume(result.output);
}

AudioVolumeState unavailableState() {
  return AudioVolumeState{};
}

std::vector<std::string> wirePlumberCommand(AudioControlAction action) {
  switch (action) {
  case AudioControlAction::DecreaseVolume:
    return {"wpctl", "set-volume", kDefaultWpctlSink, std::string(kVolumeStep) + "-", "--limit", "1.0"};
  case AudioControlAction::IncreaseVolume:
    return {"wpctl", "set-volume", kDefaultWpctlSink, std::string(kVolumeStep) + "+", "--limit", "1.0"};
  case AudioControlAction::ToggleMute:
    return {"wpctl", "set-mute", kDefaultWpctlSink, "toggle"};
  }
  return {};
}

std::vector<std::string> wirePlumberAdjustCommand(int deltaPercent) {
  int const magnitude = std::abs(deltaPercent);
  if (magnitude == 0) return {};
  return {"wpctl",
          "set-volume",
          kDefaultWpctlSink,
          std::to_string(magnitude) + "%" + (deltaPercent > 0 ? "+" : "-"),
          "--limit",
          "1.0"};
}

std::vector<std::string> pulseAudioCommand(AudioControlAction action) {
  switch (action) {
  case AudioControlAction::DecreaseVolume:
    return {"pactl", "set-sink-volume", kDefaultPulseSink, std::string("-") + kVolumeStep};
  case AudioControlAction::IncreaseVolume:
    return {"pactl", "set-sink-volume", kDefaultPulseSink, std::string("+") + kVolumeStep};
  case AudioControlAction::ToggleMute:
    return {"pactl", "set-sink-mute", kDefaultPulseSink, "toggle"};
  }
  return {};
}

std::vector<std::string> pulseAudioAdjustCommand(int deltaPercent) {
  int const magnitude = std::abs(deltaPercent);
  if (magnitude == 0) return {};
  return {"pactl",
          "set-sink-volume",
          kDefaultPulseSink,
          std::string(deltaPercent > 0 ? "+" : "-") + std::to_string(magnitude) + "%"};
}

std::vector<std::string> alsaCommand(AudioControlAction action) {
  switch (action) {
  case AudioControlAction::DecreaseVolume:
    return {"amixer", "sset", kAlsaMasterControl, std::string(kVolumeStep) + "-"};
  case AudioControlAction::IncreaseVolume:
    return {"amixer", "sset", kAlsaMasterControl, std::string(kVolumeStep) + "+"};
  case AudioControlAction::ToggleMute:
    return {"amixer", "sset", kAlsaMasterControl, "toggle"};
  }
  return {};
}

std::vector<std::string> alsaAdjustCommand(int deltaPercent) {
  int const magnitude = std::abs(deltaPercent);
  if (magnitude == 0) return {};
  return {"amixer",
          "sset",
          kAlsaMasterControl,
          std::to_string(magnitude) + "%" + (deltaPercent > 0 ? "+" : "-")};
}

std::vector<std::string> commandFor(AudioBackend backend, AudioControlAction action) {
  switch (backend) {
  case AudioBackend::PulseAudioNative:
    return {};
  case AudioBackend::WirePlumber:
    return wirePlumberCommand(action);
  case AudioBackend::PulseAudio:
    return pulseAudioCommand(action);
  case AudioBackend::Alsa:
    return alsaCommand(action);
  case AudioBackend::None:
    return {};
  }
  return {};
}

std::vector<std::string> adjustCommandFor(AudioBackend backend, int deltaPercent) {
  switch (backend) {
  case AudioBackend::PulseAudioNative:
    return {};
  case AudioBackend::WirePlumber:
    return wirePlumberAdjustCommand(deltaPercent);
  case AudioBackend::PulseAudio:
    return pulseAudioAdjustCommand(deltaPercent);
  case AudioBackend::Alsa:
    return alsaAdjustCommand(deltaPercent);
  case AudioBackend::None:
    return {};
  }
  return {};
}

} // namespace

char const* audioBackendName(AudioBackend backend) {
  switch (backend) {
  case AudioBackend::None:
    return "none";
  case AudioBackend::PulseAudioNative:
    return "pulseaudio-native";
  case AudioBackend::WirePlumber:
    return "wireplumber";
  case AudioBackend::PulseAudio:
    return "pulseaudio";
  case AudioBackend::Alsa:
    return "alsa";
  }
  return "none";
}

AudioControlContext defaultAudioControlContext() {
  return AudioControlContext{.run = runProcess, .useNativeClients = true};
}

AudioVolumeState readAudioVolumeState() {
  return readAudioVolumeState(defaultAudioControlContext());
}

AudioVolumeState readAudioVolumeState(AudioControlContext const& context) {
  if (!context.run) return unavailableState();

#if defined(LAMBDA_SHELL_HAVE_LIBPULSE)
  if (context.useNativeClients) {
    if (auto state = pulseAudioLibraryClient().readState()) return *state;
  }
#endif

  if (auto state = readWirePlumber(context)) return *state;
  if (auto state = readPulseAudio(context)) return *state;
  if (auto state = readAlsa(context)) return *state;
  return unavailableState();
}

bool controlAudioVolume(AudioControlAction action) {
  return controlAudioVolume(action, defaultAudioControlContext());
}

bool controlAudioVolume(AudioControlAction action, AudioControlContext const& context) {
  if (!context.run) return false;
  if (action == AudioControlAction::IncreaseVolume) {
    return adjustAudioVolumeByPercent(5, context);
  }
  if (action == AudioControlAction::DecreaseVolume) {
    return adjustAudioVolumeByPercent(-5, context);
  }

#if defined(LAMBDA_SHELL_HAVE_LIBPULSE)
  if (context.useNativeClients && action == AudioControlAction::ToggleMute) {
    if (pulseAudioLibraryClient().toggleMute()) return true;
  }
#endif

  AudioVolumeState const state = readAudioVolumeState(context);
  if (!state.available) return false;

  std::vector<std::string> const command = commandFor(state.backend, action);
  if (command.empty()) return false;
  AudioCommandResult const result = context.run(command);
  return result.exitCode == 0;
}

bool adjustAudioVolumeByPercent(int deltaPercent) {
  return adjustAudioVolumeByPercent(deltaPercent, defaultAudioControlContext());
}

bool adjustAudioVolumeByPercent(int deltaPercent, AudioControlContext const& context) {
  if (!context.run || deltaPercent == 0) return false;

#if defined(LAMBDA_SHELL_HAVE_LIBPULSE)
  if (context.useNativeClients) {
    if (pulseAudioLibraryClient().adjustVolume(deltaPercent)) return true;
  }
#endif

  AudioVolumeState const state = readAudioVolumeState(context);
  if (!state.available) return false;

  std::vector<std::string> const command = adjustCommandFor(state.backend, deltaPercent);
  if (command.empty()) return false;
  AudioCommandResult const result = context.run(command);
  return result.exitCode == 0;
}

} // namespace lambda_shell
