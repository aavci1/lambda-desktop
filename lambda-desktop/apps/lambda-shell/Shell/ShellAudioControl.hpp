#pragma once

#include <functional>
#include <string>
#include <vector>

namespace lambda_shell {

enum class AudioBackend {
  None,
  PulseAudioNative,
  WirePlumber,
  PulseAudio,
  Alsa,
};

struct AudioVolumeState {
  bool available = false;
  bool muted = false;
  int percent = 0;
  AudioBackend backend = AudioBackend::None;

  bool operator==(AudioVolumeState const&) const = default;
};

struct AudioCommandResult {
  int exitCode = -1;
  std::string output;
};

using AudioCommandRunner = std::function<AudioCommandResult(std::vector<std::string> const&)>;

struct AudioControlContext {
  AudioCommandRunner run;
  bool useNativeClients = false;
};

enum class AudioControlAction {
  DecreaseVolume,
  IncreaseVolume,
  ToggleMute,
};

[[nodiscard]] char const* audioBackendName(AudioBackend backend);
[[nodiscard]] AudioControlContext defaultAudioControlContext();
[[nodiscard]] AudioVolumeState readAudioVolumeState();
[[nodiscard]] AudioVolumeState readAudioVolumeState(AudioControlContext const& context);
[[nodiscard]] bool controlAudioVolume(AudioControlAction action);
[[nodiscard]] bool controlAudioVolume(AudioControlAction action, AudioControlContext const& context);
[[nodiscard]] bool adjustAudioVolumeByPercent(int deltaPercent);
[[nodiscard]] bool adjustAudioVolumeByPercent(int deltaPercent, AudioControlContext const& context);

} // namespace lambda_shell
