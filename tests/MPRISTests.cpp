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

namespace {

using lambda::testing::dbus::pollBus;
using lambda::testing::dbus::pumpUntil;
using lambda::testing::dbus::startPrivateBus;

template <typename Callback>
class ScopeExit {
public:
  explicit ScopeExit(Callback callback) : callback_(std::move(callback)) {}
  ~ScopeExit() { callback_(); }

private:
  Callback callback_;
};

std::shared_ptr<lambda::dbus::VariantDictionary> metadata() {
  auto values = std::make_shared<lambda::dbus::VariantDictionary>();
  values->values["mpris:trackid"] =
      lambda::dbus::ObjectPath{"/org/mpris/MediaPlayer2/Track/1"};
  values->values["xesam:title"] = std::string("Test Song");
  values->values["xesam:album"] = std::string("Test Album");
  values->values["xesam:artist"] =
      lambda::dbus::StringArray{.values = {"Lambda Artist", "Second Artist"}};
  values->values["mpris:artUrl"] = std::string("file:///tmp/test-song.png");
  values->values["mpris:length"] = std::int64_t(123456789);
  return values;
}

} // namespace

TEST_CASE("MPRIS support is compile-time declared") {
  CHECK((LAMBDA_HAS_DBUS == 0 || LAMBDA_HAS_DBUS == 1));
}

TEST_CASE("MPRIS active player policy prunes stale players and gates capabilities") {
  lambda::system::MPRISPlayerSnapshot stale{
      .serviceName = "org.mpris.MediaPlayer2.stale",
      .playbackStatus = "",
      .canPlay = true,
      .canControl = true,
  };
  lambda::system::MPRISPlayerSnapshot paused{
      .serviceName = "org.mpris.MediaPlayer2.paused",
      .playbackStatus = "Paused",
      .canPlay = true,
      .canControl = true,
  };
  lambda::system::MPRISPlayerSnapshot uncontrolledPlaying{
      .serviceName = "org.mpris.MediaPlayer2.uncontrolled",
      .playbackStatus = "Playing",
      .canPause = true,
      .canControl = false,
  };
  lambda::system::MPRISPlayerSnapshot playingWithoutToggle{
      .serviceName = "org.mpris.MediaPlayer2.playing-next-only",
      .playbackStatus = "Playing",
      .canGoNext = true,
      .canControl = true,
  };
  lambda::system::MPRISPlayerSnapshot playing{
      .serviceName = "org.mpris.MediaPlayer2.playing",
      .playbackStatus = "Playing",
      .canGoNext = true,
      .canPause = true,
      .canControl = true,
  };

  CHECK(lambda::system::isStaleMPRISPlayer(stale));
  CHECK_FALSE(lambda::system::isStaleMPRISPlayer(playing));
  CHECK(lambda::system::mprisPlayerSupportsAction(
      playing, lambda::system::MPRISPlayerAction::PlayPause));
  CHECK(lambda::system::mprisPlayerSupportsAction(
      playing, lambda::system::MPRISPlayerAction::Next));
  CHECK_FALSE(lambda::system::mprisPlayerSupportsAction(
      playing, lambda::system::MPRISPlayerAction::Previous));
  CHECK_FALSE(lambda::system::mprisPlayerSupportsAction(
      uncontrolledPlaying, lambda::system::MPRISPlayerAction::PlayPause));
  CHECK_FALSE(lambda::system::mprisPlayerSupportsAction(
      stale, lambda::system::MPRISPlayerAction::PlayPause));

  auto active =
      lambda::system::activeMPRISPlayer({stale, paused, uncontrolledPlaying, playing});
  REQUIRE(active);
  CHECK(active->serviceName == "org.mpris.MediaPlayer2.playing");
  CHECK(lambda::system::activeMPRISPlayerService({stale, paused, uncontrolledPlaying, playing}) ==
        "org.mpris.MediaPlayer2.playing");
  CHECK(lambda::system::activeMPRISPlayerService({stale, paused, uncontrolledPlaying}) ==
        "org.mpris.MediaPlayer2.paused");
  CHECK(lambda::system::activeMPRISPlayerService({playingWithoutToggle, paused}) ==
        "org.mpris.MediaPlayer2.playing-next-only");
  CHECK_FALSE(lambda::system::activeMPRISPlayerService({uncontrolledPlaying}).has_value());
  CHECK(lambda::system::formatMPRISStatus({stale}) == "unavailable");
}

#if LAMBDA_HAS_DBUS

TEST_CASE("MPRISClient discovers players reads metadata and sends controls") {
  auto privateBus = startPrivateBus();
  if (!privateBus) {
    MESSAGE("Skipping MPRIS integration test because a private bus could not be started");
    return;
  }

  auto service = lambda::dbus::Bus::openAddress(privateBus->address);
  lambda::system::MPRISClient client(lambda::dbus::Bus::openAddress(privateBus->address));
  std::string const serviceName = std::string(lambda::system::MPRISClient::servicePrefix) +
                                  "lambdaTest" +
                                  std::to_string(static_cast<unsigned long long>(getpid()));
  service.requestName(serviceName);

  std::string playbackStatus = "Playing";
  double volume = 0.8;
  std::int64_t position = 4000000;
  int playPauseCalls = 0;
  int nextCalls = 0;

  auto objectSlot = service.exportObject(
      lambda::system::MPRISClient::objectPath,
      lambda::dbus::ObjectDefinition{
          .methods = {
              lambda::dbus::ExportedMethod{
                  .interface = lambda::system::MPRISClient::playerInterfaceName,
                  .member = "PlayPause",
                  .handler = [&](lambda::dbus::Message&) {
                    ++playPauseCalls;
                    playbackStatus = playbackStatus == "Playing" ? "Paused" : "Playing";
                    return lambda::dbus::MethodReply{};
                  },
              },
              lambda::dbus::ExportedMethod{
                  .interface = lambda::system::MPRISClient::playerInterfaceName,
                  .member = "Next",
                  .handler = [&](lambda::dbus::Message&) {
                    ++nextCalls;
                    position = 0;
                    return lambda::dbus::MethodReply{};
                  },
              },
          },
          .properties = {
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::MPRISClient::rootInterfaceName,
                  .name = "Identity",
                  .value = std::string("Lambda Player"),
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::MPRISClient::rootInterfaceName,
                  .name = "DesktopEntry",
                  .value = std::string("lambda-player"),
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::MPRISClient::playerInterfaceName,
                  .name = "PlaybackStatus",
                  .value = std::string("Playing"),
                  .getter = [&] { return lambda::dbus::BasicValue(playbackStatus); },
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::MPRISClient::playerInterfaceName,
                  .name = "Metadata",
                  .value = metadata(),
                  .getter = [] { return lambda::dbus::BasicValue(metadata()); },
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::MPRISClient::playerInterfaceName,
                  .name = "Volume",
                  .value = 0.8,
                  .writable = true,
                  .getter = [&] { return lambda::dbus::BasicValue(volume); },
                  .setter = [&](lambda::dbus::BasicValue const& value) {
                    volume = std::get<double>(value);
                  },
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::MPRISClient::playerInterfaceName,
                  .name = "Position",
                  .value = std::int64_t(0),
                  .getter = [&] { return lambda::dbus::BasicValue(position); },
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::MPRISClient::playerInterfaceName,
                  .name = "CanGoNext",
                  .value = true,
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::MPRISClient::playerInterfaceName,
                  .name = "CanGoPrevious",
                  .value = true,
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::MPRISClient::playerInterfaceName,
                  .name = "CanPlay",
                  .value = true,
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::MPRISClient::playerInterfaceName,
                  .name = "CanPause",
                  .value = true,
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::MPRISClient::playerInterfaceName,
                  .name = "CanSeek",
                  .value = true,
              },
              lambda::dbus::ExportedProperty{
                  .interface = lambda::system::MPRISClient::playerInterfaceName,
                  .name = "CanControl",
                  .value = true,
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
  CHECK(player.playbackStatus == "Playing");
  CHECK(player.metadata.title == "Test Song");
  CHECK(player.metadata.album == "Test Album");
  CHECK(player.metadata.artists == std::vector<std::string>{"Lambda Artist", "Second Artist"});
  CHECK(player.metadata.trackId == "/org/mpris/MediaPlayer2/Track/1");
  CHECK(player.metadata.lengthUsec == 123456789);
  CHECK(player.positionUsec == 4000000);
  CHECK(player.volume == doctest::Approx(0.8));
  CHECK(player.canControl);
  CHECK(lambda::system::activeMPRISPlayerService({player}) == serviceName);
  CHECK(lambda::system::formatMPRISStatus({player}) == "Lambda Artist - Test Song");

  int playerChanges = 0;
  auto playerChangedWatch = client.watchPlayerChanges([&] {
    ++playerChanges;
  });
  service.emitPropertiesChanged(
      lambda::system::MPRISClient::objectPath,
      lambda::system::MPRISClient::playerInterfaceName,
      lambda::dbus::VariantDictionary{
          .values = {{"PlaybackStatus", lambda::dbus::BasicValue(playbackStatus)}},
      });
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return playerChanges == 1; },
                  std::chrono::milliseconds(500)));

  service.emitSignal(lambda::system::MPRISClient::objectPath,
                     lambda::system::MPRISClient::playerInterfaceName,
                     "Seeked",
                     {std::int64_t(12000000)});
  service.flush();
  CHECK(pumpUntil(client.bus(),
                  [&] { return playerChanges == 2; },
                  std::chrono::milliseconds(500)));

  auto transientPlayer = lambda::dbus::Bus::openAddress(privateBus->address);
  transientPlayer.requestName(serviceName + ".transient");
  CHECK(pumpUntil(client.bus(),
                  [&] { return playerChanges == 3; },
                  std::chrono::milliseconds(500)));

  client.playPause(serviceName);
  CHECK(playPauseCalls == 1);
  player = client.readPlayer(serviceName);
  CHECK(player.playbackStatus == "Paused");
  CHECK(lambda::system::formatMPRISStatus({player}) == "paused: Lambda Artist - Test Song");
  CHECK(lambda::system::activeMPRISPlayerService({player}) == serviceName);

  auto uncontrolled = player;
  uncontrolled.canControl = false;
  CHECK_FALSE(lambda::system::activeMPRISPlayerService({uncontrolled}).has_value());

  client.next(serviceName);
  CHECK(nextCalls == 1);
  CHECK(position == 0);

  client.setVolume(serviceName, 0.42);
  CHECK(volume == doctest::Approx(0.42));
  CHECK(lambda::system::formatMPRISStatus({}) == "unavailable");
}

#endif
