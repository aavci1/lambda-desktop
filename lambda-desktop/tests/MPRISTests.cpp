#include <Lambda/System/MPRIS.hpp>

#include "DBusTestHelpers.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

using lambdaui::testing::dbus::pollBus;
using lambdaui::testing::dbus::pumpUntil;
using lambdaui::testing::dbus::startPrivateBus;

template <typename Callback>
class ScopeExit {
public:
  explicit ScopeExit(Callback callback) : callback_(std::move(callback)) {}
  ~ScopeExit() { callback_(); }

private:
  Callback callback_;
};

std::shared_ptr<lambdaui::dbus::VariantDictionary>
metadata(std::string title = "Test Song",
         std::string trackId = "/org/mpris/MediaPlayer2/Track/1") {
  auto values = std::make_shared<lambdaui::dbus::VariantDictionary>();
  values->values["mpris:trackid"] = lambdaui::dbus::ObjectPath{std::move(trackId)};
  values->values["xesam:title"] = std::move(title);
  values->values["xesam:album"] = std::string("Test Album");
  values->values["xesam:artist"] =
      lambdaui::dbus::StringArray{.values = {"Lambda Artist", "Second Artist"}};
  values->values["xesam:albumArtist"] =
      lambdaui::dbus::StringArray{.values = {"Lambda Album Artist"}};
  values->values["xesam:genre"] = lambdaui::dbus::StringArray{.values = {"Electronic"}};
  values->values["mpris:artUrl"] = std::string("file:///tmp/test-song.png");
  values->values["xesam:url"] = std::string("file:///music/test-song.flac");
  values->values["xesam:contentCreated"] = std::string("2026-06-17T12:00:00Z");
  values->values["xesam:discNumber"] = std::int32_t(1);
  values->values["xesam:trackNumber"] = std::int32_t(7);
  values->values["mpris:length"] = std::int64_t(123456789);
  return values;
}

std::shared_ptr<lambdaui::dbus::ArrayValue>
trackMetadataArray(std::vector<lambdaui::dbus::BasicValue> values) {
  return std::make_shared<lambdaui::dbus::ArrayValue>(
      lambdaui::dbus::ArrayValue{.elementSignature = "a{sv}", .values = std::move(values)});
}

} // namespace

TEST_CASE("MPRIS support is compile-time declared") {
  CHECK((LAMBDA_HAS_DBUS == 0 || LAMBDA_HAS_DBUS == 1));
}

TEST_CASE("MPRIS active player policy prunes stale players and gates capabilities") {
  lambdaui::system::MPRISPlayerSnapshot stale{
      .serviceName = "org.mpris.MediaPlayer2.stale",
      .playbackStatus = "",
      .canPlay = true,
      .canControl = true,
  };
  lambdaui::system::MPRISPlayerSnapshot paused{
      .serviceName = "org.mpris.MediaPlayer2.paused",
      .playbackStatus = "Paused",
      .canPlay = true,
      .canControl = true,
  };
  lambdaui::system::MPRISPlayerSnapshot uncontrolledPlaying{
      .serviceName = "org.mpris.MediaPlayer2.uncontrolled",
      .playbackStatus = "Playing",
      .canPause = true,
      .canControl = false,
  };
  lambdaui::system::MPRISPlayerSnapshot playingWithoutToggle{
      .serviceName = "org.mpris.MediaPlayer2.playing-next-only",
      .playbackStatus = "Playing",
      .canGoNext = true,
      .canControl = true,
  };
  lambdaui::system::MPRISPlayerSnapshot playing{
      .serviceName = "org.mpris.MediaPlayer2.playing",
      .playbackStatus = "Playing",
      .canGoNext = true,
      .canPause = true,
      .canControl = true,
  };

  CHECK(lambdaui::system::isStaleMPRISPlayer(stale));
  CHECK_FALSE(lambdaui::system::isStaleMPRISPlayer(playing));
  CHECK(lambdaui::system::mprisPlayerSupportsAction(
      playing, lambdaui::system::MPRISPlayerAction::PlayPause));
  CHECK(lambdaui::system::mprisPlayerSupportsAction(
      playing, lambdaui::system::MPRISPlayerAction::Next));
  CHECK_FALSE(lambdaui::system::mprisPlayerSupportsAction(
      playing, lambdaui::system::MPRISPlayerAction::Previous));
  CHECK_FALSE(lambdaui::system::mprisPlayerSupportsAction(
      uncontrolledPlaying, lambdaui::system::MPRISPlayerAction::PlayPause));
  CHECK_FALSE(lambdaui::system::mprisPlayerSupportsAction(
      stale, lambdaui::system::MPRISPlayerAction::PlayPause));

  auto active =
      lambdaui::system::activeMPRISPlayer({stale, paused, uncontrolledPlaying, playing});
  REQUIRE(active);
  CHECK(active->serviceName == "org.mpris.MediaPlayer2.playing");
  CHECK(lambdaui::system::activeMPRISPlayerService({stale, paused, uncontrolledPlaying, playing}) ==
        "org.mpris.MediaPlayer2.playing");
  CHECK(lambdaui::system::activeMPRISPlayerService({stale, paused, uncontrolledPlaying}) ==
        "org.mpris.MediaPlayer2.paused");
  CHECK(lambdaui::system::activeMPRISPlayerService({playingWithoutToggle, paused}) ==
        "org.mpris.MediaPlayer2.playing-next-only");
  CHECK_FALSE(lambdaui::system::activeMPRISPlayerService({uncontrolledPlaying}).has_value());
  CHECK(lambdaui::system::formatMPRISStatus({stale}) == "unavailable");
}

#if LAMBDA_HAS_DBUS

TEST_CASE("MPRISClient discovers players reads metadata and sends controls") {
  auto privateBus = startPrivateBus();
  if (!privateBus) {
    MESSAGE("Skipping MPRIS integration test because a private bus could not be started");
    return;
  }

  auto service = lambdaui::dbus::Bus::openAddress(privateBus->address);
  lambdaui::system::MPRISClient client(lambdaui::dbus::Bus::openAddress(privateBus->address));
  std::string const serviceName = std::string(lambdaui::system::MPRISClient::servicePrefix) +
                                  "lambdaTest" +
                                  std::to_string(static_cast<unsigned long long>(getpid()));
  service.requestName(serviceName);

  std::string playbackStatus = "Playing";
  double volume = 0.8;
  std::int64_t position = 4000000;
  lambdaui::dbus::ObjectPathArray trackIds{
      .values = {
          lambdaui::dbus::ObjectPath{"/org/mpris/MediaPlayer2/Track/1"},
          lambdaui::dbus::ObjectPath{"/org/mpris/MediaPlayer2/Track/2"},
      },
  };
  int playPauseCalls = 0;
  int nextCalls = 0;

  auto objectSlot = service.exportObject(
      lambdaui::system::MPRISClient::objectPath,
      lambdaui::dbus::ObjectDefinition{
          .methods = {
              lambdaui::dbus::ExportedMethod{
                  .interface = lambdaui::system::MPRISClient::playerInterfaceName,
                  .member = "PlayPause",
                  .handler = [&](lambdaui::dbus::Message&) {
                    ++playPauseCalls;
                    playbackStatus = playbackStatus == "Playing" ? "Paused" : "Playing";
                    return lambdaui::dbus::MethodReply{};
                  },
              },
              lambdaui::dbus::ExportedMethod{
                  .interface = lambdaui::system::MPRISClient::playerInterfaceName,
                  .member = "Next",
                  .handler = [&](lambdaui::dbus::Message&) {
                    ++nextCalls;
                    position = 0;
                    return lambdaui::dbus::MethodReply{};
                  },
              },
              lambdaui::dbus::ExportedMethod{
                  .interface = lambdaui::system::MPRISClient::trackListInterfaceName,
                  .member = "GetTracksMetadata",
                  .handler = [&](lambdaui::dbus::Message& message) {
                    auto requested = message.readObjectPathArray();
                    std::vector<lambdaui::dbus::BasicValue> values;
                    for (auto const& track : requested.values) {
                      if (track.value.ends_with("/2")) {
                        values.push_back(lambdaui::dbus::BasicValue(
                            metadata("Second Song", "/org/mpris/MediaPlayer2/Track/2")));
                      } else {
                        values.push_back(lambdaui::dbus::BasicValue(metadata()));
                      }
                    }
                    return lambdaui::dbus::MethodReply{.values = {trackMetadataArray(std::move(values))}};
                  },
              },
          },
          .properties = {
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::MPRISClient::rootInterfaceName,
                  .name = "Identity",
                  .value = std::string("Lambda Player"),
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::MPRISClient::rootInterfaceName,
                  .name = "DesktopEntry",
                  .value = std::string("lambda-player"),
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::MPRISClient::playerInterfaceName,
                  .name = "PlaybackStatus",
                  .value = std::string("Playing"),
                  .getter = [&] { return lambdaui::dbus::BasicValue(playbackStatus); },
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::MPRISClient::playerInterfaceName,
                  .name = "Metadata",
                  .value = metadata(),
                  .getter = [] { return lambdaui::dbus::BasicValue(metadata()); },
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::MPRISClient::playerInterfaceName,
                  .name = "Volume",
                  .value = 0.8,
                  .writable = true,
                  .getter = [&] { return lambdaui::dbus::BasicValue(volume); },
                  .setter = [&](lambdaui::dbus::BasicValue const& value) {
                    volume = std::get<double>(value);
                  },
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::MPRISClient::playerInterfaceName,
                  .name = "Position",
                  .value = std::int64_t(0),
                  .getter = [&] { return lambdaui::dbus::BasicValue(position); },
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::MPRISClient::playerInterfaceName,
                  .name = "LoopStatus",
                  .value = std::string("None"),
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::MPRISClient::playerInterfaceName,
                  .name = "Rate",
                  .value = 1.25,
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::MPRISClient::playerInterfaceName,
                  .name = "MinimumRate",
                  .value = 0.5,
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::MPRISClient::playerInterfaceName,
                  .name = "MaximumRate",
                  .value = 2.0,
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::MPRISClient::playerInterfaceName,
                  .name = "Shuffle",
                  .value = true,
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::MPRISClient::playerInterfaceName,
                  .name = "CanGoNext",
                  .value = true,
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::MPRISClient::playerInterfaceName,
                  .name = "CanGoPrevious",
                  .value = true,
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::MPRISClient::playerInterfaceName,
                  .name = "CanPlay",
                  .value = true,
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::MPRISClient::playerInterfaceName,
                  .name = "CanPause",
                  .value = true,
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::MPRISClient::playerInterfaceName,
                  .name = "CanSeek",
                  .value = true,
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::MPRISClient::playerInterfaceName,
                  .name = "CanControl",
                  .value = true,
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::MPRISClient::trackListInterfaceName,
                  .name = "Tracks",
                  .value = trackIds,
              },
              lambdaui::dbus::ExportedProperty{
                  .interface = lambdaui::system::MPRISClient::trackListInterfaceName,
                  .name = "CanEditTracks",
                  .value = false,
              },
          },
      });

  std::atomic<bool> running = true;
  std::thread serviceThread([&] {
    while (running.load()) {
      pollBus(service, 25);
    }
  });
  ScopeExit stopServiceThread([&] {
    running = false;
    if (serviceThread.joinable()) {
      serviceThread.join();
    }
  });

  auto services = client.playerServiceNames();
  CHECK(std::find(services.begin(), services.end(), serviceName) != services.end());

  auto player = client.readPlayer(serviceName);
  CHECK(player.identity == "Lambda Player");
  CHECK(player.desktopEntry == "lambda-player");
  CHECK(player.desktopIconName == "lambda-player");
  CHECK(player.playbackStatus == "Playing");
  CHECK(player.loopStatus == "None");
  CHECK(player.metadata.title == "Test Song");
  CHECK(player.metadata.album == "Test Album");
  CHECK(player.metadata.artists == std::vector<std::string>{"Lambda Artist", "Second Artist"});
  CHECK(player.metadata.albumArtists == std::vector<std::string>{"Lambda Album Artist"});
  CHECK(player.metadata.genres == std::vector<std::string>{"Electronic"});
  CHECK(player.metadata.trackId == "/org/mpris/MediaPlayer2/Track/1");
  CHECK(player.metadata.artUrl == "file:///tmp/test-song.png");
  CHECK(player.metadata.artCacheKey.starts_with("mpris-art-"));
  CHECK(player.metadata.artCacheKey.ends_with(".png"));
  CHECK(player.metadata.url == "file:///music/test-song.flac");
  CHECK(player.metadata.contentCreated == "2026-06-17T12:00:00Z");
  CHECK(player.metadata.discNumber == 1);
  CHECK(player.metadata.trackNumber == 7);
  CHECK(player.metadata.lengthUsec == 123456789);
  CHECK(player.positionUsec == 4000000);
  REQUIRE(player.progress);
  CHECK(*player.progress == doctest::Approx(4000000.0 / 123456789.0));
  CHECK(player.rate == doctest::Approx(1.25));
  CHECK(player.minimumRate == doctest::Approx(0.5));
  CHECK(player.maximumRate == doctest::Approx(2.0));
  CHECK(player.shuffle);
  CHECK(player.volume == doctest::Approx(0.8));
  CHECK(player.canControl);
  CHECK(player.trackList.available);
  CHECK_FALSE(player.trackList.canEditTracks);
  CHECK(player.trackList.trackIds == std::vector<std::string>{"/org/mpris/MediaPlayer2/Track/1",
                                                               "/org/mpris/MediaPlayer2/Track/2"});
  REQUIRE(player.trackList.tracks.size() == 2);
  CHECK(player.trackList.tracks[1].title == "Second Song");
  CHECK(lambdaui::system::mprisDesktopIconName("lambda-player.desktop", serviceName) ==
        "lambda-player");
  CHECK(lambdaui::system::mprisDesktopIconName("", serviceName).starts_with("lambdaTest"));
  CHECK(lambdaui::system::mprisTrackProgress(player) == player.progress);
  CHECK(lambdaui::system::activeMPRISPlayerService({player}) == serviceName);
  CHECK(lambdaui::system::formatMPRISStatus({player}) == "Lambda Artist - Test Song");

  int playerChanges = 0;
  auto playerChangedWatch = client.watchPlayerChanges([&] {
    ++playerChanges;
  });
  service.emitPropertiesChanged(
      lambdaui::system::MPRISClient::objectPath,
      lambdaui::system::MPRISClient::playerInterfaceName,
      lambdaui::dbus::VariantDictionary{
          .values = {{"PlaybackStatus", lambdaui::dbus::BasicValue(playbackStatus)}},
      });
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return playerChanges == 1; },
                  std::chrono::milliseconds(500)));

  service.emitSignal(lambdaui::system::MPRISClient::objectPath,
                     lambdaui::system::MPRISClient::playerInterfaceName,
                     "Seeked",
                     {std::int64_t(12000000)});
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return playerChanges == 2; },
                  std::chrono::milliseconds(500)));

  service.emitPropertiesChanged(
      lambdaui::system::MPRISClient::objectPath,
      lambdaui::system::MPRISClient::trackListInterfaceName,
      lambdaui::dbus::VariantDictionary{
          .values = {{"Tracks", lambdaui::dbus::BasicValue(trackIds)}},
      });
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return playerChanges == 3; },
                  std::chrono::milliseconds(500)));

  service.emitSignal(lambdaui::system::MPRISClient::objectPath,
                     lambdaui::system::MPRISClient::trackListInterfaceName,
                     "TrackMetadataChanged",
                     {lambdaui::dbus::ObjectPath{"/org/mpris/MediaPlayer2/Track/2"},
                      metadata("Second Song", "/org/mpris/MediaPlayer2/Track/2")});
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return playerChanges == 4; },
                  std::chrono::milliseconds(500)));

  auto transientPlayer = lambdaui::dbus::Bus::openAddress(privateBus->address);
  transientPlayer.requestName(serviceName + ".transient");
  CHECK(pumpUntil(client.bus(),
                  [&] { return playerChanges == 5; },
                  std::chrono::milliseconds(500)));

  client.playPause(serviceName);
  CHECK(playPauseCalls == 1);
  player = client.readPlayer(serviceName);
  CHECK(player.playbackStatus == "Paused");
  CHECK(lambdaui::system::formatMPRISStatus({player}) == "paused: Lambda Artist - Test Song");
  CHECK(lambdaui::system::activeMPRISPlayerService({player}) == serviceName);

  auto uncontrolled = player;
  uncontrolled.canControl = false;
  CHECK_FALSE(lambdaui::system::activeMPRISPlayerService({uncontrolled}).has_value());

  client.next(serviceName);
  CHECK(nextCalls == 1);
  CHECK(position == 0);

  client.setVolume(serviceName, 0.42);
  CHECK(volume == doctest::Approx(0.42));
  CHECK(lambdaui::system::formatMPRISStatus({}) == "unavailable");
}

#endif
