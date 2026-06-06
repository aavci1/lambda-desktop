#include "Shell/ShellAudioControl.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

std::string commandKey(std::vector<std::string> const& command) {
  std::string key;
  for (std::string const& part : command) {
    if (!key.empty()) key.push_back('\n');
    key += part;
  }
  return key;
}

struct FakeAudioCommands {
  std::map<std::string, lambda_shell::AudioCommandResult> replies;
  std::vector<std::vector<std::string>> calls;

  void reply(std::vector<std::string> command,
             int exitCode,
             std::string output = {}) {
    replies[commandKey(command)] = lambda_shell::AudioCommandResult{
        .exitCode = exitCode,
        .output = std::move(output),
    };
  }

  lambda_shell::AudioCommandResult operator()(std::vector<std::string> const& command) {
    calls.push_back(command);
    auto found = replies.find(commandKey(command));
    if (found == replies.end()) {
      return {.exitCode = 127, .output = {}};
    }
    return found->second;
  }
};

lambda_shell::AudioControlContext contextFor(FakeAudioCommands& fake) {
  return lambda_shell::AudioControlContext{
      .run = [&fake](std::vector<std::string> const& command) {
        return fake(command);
      },
  };
}

bool called(FakeAudioCommands const& fake, std::vector<std::string> command) {
  return std::find(fake.calls.begin(), fake.calls.end(), command) != fake.calls.end();
}

} // namespace

TEST_CASE("Shell audio control prefers wpctl and parses default sink volume") {
  FakeAudioCommands fake;
  fake.reply({"wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@"}, 0, "Volume: 0.40\n");

  auto state = lambda_shell::readAudioVolumeState(contextFor(fake));

  CHECK(state.available);
  CHECK_FALSE(state.muted);
  CHECK(state.percent == 40);
  CHECK(state.backend == lambda_shell::AudioBackend::WirePlumber);
  CHECK(lambda_shell::audioBackendName(state.backend) == std::string{"wireplumber"});
}

TEST_CASE("Shell audio control parses muted wpctl state") {
  FakeAudioCommands fake;
  fake.reply({"wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@"}, 0, "Volume: 0.31 [MUTED]\n");

  auto state = lambda_shell::readAudioVolumeState(contextFor(fake));

  CHECK(state.available);
  CHECK(state.muted);
  CHECK(state.percent == 31);
  CHECK(state.backend == lambda_shell::AudioBackend::WirePlumber);
}

TEST_CASE("Shell audio control falls back to pactl when wpctl is unavailable") {
  FakeAudioCommands fake;
  fake.reply({"pactl", "get-sink-volume", "@DEFAULT_SINK@"},
             0,
             "Volume: front-left: 32768 /  50% / -18.06 dB,   front-right: 32768 /  50% / -18.06 dB\n");
  fake.reply({"pactl", "get-sink-mute", "@DEFAULT_SINK@"}, 0, "Mute: yes\n");

  auto state = lambda_shell::readAudioVolumeState(contextFor(fake));

  CHECK(state.available);
  CHECK(state.muted);
  CHECK(state.percent == 50);
  CHECK(state.backend == lambda_shell::AudioBackend::PulseAudio);
}

TEST_CASE("Shell audio control falls back to ALSA Master when server controls are unavailable") {
  FakeAudioCommands fake;
  fake.reply({"amixer", "sget", "Master"},
             0,
             "Simple mixer control 'Master',0\n"
             "  Front Left: Playback 26214 [40%] [on]\n"
             "  Front Right: Playback 26214 [40%] [on]\n");

  auto state = lambda_shell::readAudioVolumeState(contextFor(fake));

  CHECK(state.available);
  CHECK_FALSE(state.muted);
  CHECK(state.percent == 40);
  CHECK(state.backend == lambda_shell::AudioBackend::Alsa);
}

TEST_CASE("Shell audio control issues backend-specific volume commands") {
  FakeAudioCommands fake;
  fake.reply({"wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@"}, 0, "Volume: 0.40\n");
  fake.reply({"wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "5%+", "--limit", "1.0"}, 0);
  fake.reply({"wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "5%-", "--limit", "1.0"}, 0);
  fake.reply({"wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "toggle"}, 0);
  auto context = contextFor(fake);

  CHECK(lambda_shell::controlAudioVolume(lambda_shell::AudioControlAction::IncreaseVolume, context));
  CHECK(lambda_shell::controlAudioVolume(lambda_shell::AudioControlAction::DecreaseVolume, context));
  CHECK(lambda_shell::controlAudioVolume(lambda_shell::AudioControlAction::ToggleMute, context));

  CHECK(called(fake, {"wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "5%+", "--limit", "1.0"}));
  CHECK(called(fake, {"wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "5%-", "--limit", "1.0"}));
  CHECK(called(fake, {"wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "toggle"}));
}

TEST_CASE("Shell audio control coalesces relative volume adjustments into one backend command") {
  FakeAudioCommands fake;
  fake.reply({"wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@"}, 0, "Volume: 0.40\n");
  fake.reply({"wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "15%+", "--limit", "1.0"}, 0);

  CHECK(lambda_shell::adjustAudioVolumeByPercent(15, contextFor(fake)));

  CHECK(called(fake, {"wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", "15%+", "--limit", "1.0"}));
}
