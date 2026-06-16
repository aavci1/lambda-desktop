#include <Lambda/System/MPRIS.hpp>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <utility>

namespace lambda::system {

namespace {

constexpr char kPropertiesInterface[] = "org.freedesktop.DBus.Properties";

bool startsWith(std::string const& value, std::string_view prefix) {
  return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

void notify(std::shared_ptr<std::function<void()>> const& handler) {
  if (handler && *handler) {
    (*handler)();
  }
}

std::string stringValue(dbus::VariantDictionary const& metadata, std::string const& key) {
  auto const it = metadata.values.find(key);
  if (it == metadata.values.end()) {
    return {};
  }
  if (auto value = std::get_if<std::string>(&it->second)) {
    return *value;
  }
  if (auto value = std::get_if<dbus::ObjectPath>(&it->second)) {
    return value->value;
  }
  return {};
}

std::int64_t int64Value(dbus::VariantDictionary const& metadata, std::string const& key) {
  auto const it = metadata.values.find(key);
  if (it == metadata.values.end()) {
    return 0;
  }
  if (auto value = std::get_if<std::int64_t>(&it->second)) {
    return *value;
  }
  return 0;
}

std::int32_t int32Value(dbus::VariantDictionary const& metadata, std::string const& key) {
  auto const it = metadata.values.find(key);
  if (it == metadata.values.end()) {
    return 0;
  }
  if (auto value = std::get_if<std::int32_t>(&it->second)) {
    return *value;
  }
  if (auto value = std::get_if<std::uint32_t>(&it->second)) {
    return static_cast<std::int32_t>(*value);
  }
  if (auto value = std::get_if<std::int64_t>(&it->second)) {
    return static_cast<std::int32_t>(*value);
  }
  return 0;
}

std::vector<std::string> stringArrayValue(dbus::VariantDictionary const& metadata,
                                          std::string const& key) {
  auto const it = metadata.values.find(key);
  if (it == metadata.values.end()) {
    return {};
  }
  if (auto value = std::get_if<dbus::StringArray>(&it->second)) {
    return value->values;
  }
  if (auto value = std::get_if<std::string>(&it->second)) {
    return {*value};
  }
  return {};
}

MPRISMetadata metadataFromDictionary(dbus::VariantDictionary const& metadata) {
  std::string artUrl = stringValue(metadata, "mpris:artUrl");
  return MPRISMetadata{
      .trackId = stringValue(metadata, "mpris:trackid"),
      .title = stringValue(metadata, "xesam:title"),
      .album = stringValue(metadata, "xesam:album"),
      .artists = stringArrayValue(metadata, "xesam:artist"),
      .albumArtists = stringArrayValue(metadata, "xesam:albumArtist"),
      .genres = stringArrayValue(metadata, "xesam:genre"),
      .artUrl = artUrl,
      .artCacheKey = mprisArtworkCacheKey(artUrl),
      .url = stringValue(metadata, "xesam:url"),
      .contentCreated = stringValue(metadata, "xesam:contentCreated"),
      .discNumber = int32Value(metadata, "xesam:discNumber"),
      .trackNumber = int32Value(metadata, "xesam:trackNumber"),
      .lengthUsec = int64Value(metadata, "mpris:length"),
  };
}

MPRISMetadata metadataFromValue(dbus::BasicValue const& value) {
  if (auto metadata = std::get_if<std::shared_ptr<dbus::VariantDictionary>>(&value)) {
    if (*metadata) {
      return metadataFromDictionary(**metadata);
    }
  }
  return {};
}

std::string primaryArtist(MPRISMetadata const& metadata) {
  return metadata.artists.empty() ? std::string{} : metadata.artists.front();
}

std::string displayTitle(MPRISPlayerSnapshot const& player) {
  if (!player.metadata.title.empty()) {
    std::string const artist = primaryArtist(player.metadata);
    return artist.empty() ? player.metadata.title : artist + " - " + player.metadata.title;
  }
  return player.identity.empty() ? player.serviceName : player.identity;
}

bool knownPlaybackStatus(std::string_view status) {
  return status == "Playing" || status == "Paused" || status == "Stopped";
}

std::string serviceSuffix(std::string_view serviceName) {
  if (startsWith(std::string(serviceName), MPRISClient::servicePrefix)) {
    return std::string(serviceName.substr(std::string_view(MPRISClient::servicePrefix).size()));
  }
  return std::string(serviceName);
}

bool safeExtensionChar(char ch) {
  return std::isalnum(static_cast<unsigned char>(ch)) != 0;
}

bool hasAnyControls(MPRISPlayerSnapshot const& player) {
  return mprisPlayerSupportsAction(player, MPRISPlayerAction::PlayPause) ||
         mprisPlayerSupportsAction(player, MPRISPlayerAction::Stop) ||
         mprisPlayerSupportsAction(player, MPRISPlayerAction::Next) ||
         mprisPlayerSupportsAction(player, MPRISPlayerAction::Previous) ||
         mprisPlayerSupportsAction(player, MPRISPlayerAction::Seek) ||
         mprisPlayerSupportsAction(player, MPRISPlayerAction::SetVolume);
}

std::vector<std::string> pathStrings(dbus::ObjectPathArray const& paths) {
  std::vector<std::string> output;
  output.reserve(paths.values.size());
  for (auto const& path : paths.values) {
    output.push_back(path.value);
  }
  return output;
}

} // namespace

MPRISClient::MPRISClient(dbus::Bus bus) : bus_(std::move(bus)) {}

MPRISClient MPRISClient::connectSession() {
  return MPRISClient(dbus::Bus::open(dbus::BusType::Session));
}

std::vector<std::string> MPRISClient::playerServiceNames() {
  dbus::Message reply = bus_.call(dbus::MethodCall{
      .destination = dbusServiceName,
      .path = dbusObjectPath,
      .interface = dbusInterfaceName,
      .member = "ListNames",
      .arguments = {},
  });

  std::vector<std::string> players;
  for (auto const& name : reply.readStringArray().values) {
    if (startsWith(name, servicePrefix)) {
      players.push_back(name);
    }
  }
  std::sort(players.begin(), players.end());
  players.erase(std::unique(players.begin(), players.end()), players.end());
  return players;
}

std::vector<MPRISPlayerSnapshot> MPRISClient::readPlayers() {
  std::vector<MPRISPlayerSnapshot> players;
  for (auto const& name : playerServiceNames()) {
    try {
      players.push_back(readPlayer(name));
    } catch (...) {
    }
  }
  return players;
}

MPRISPlayerSnapshot MPRISClient::readPlayer(std::string const& serviceName) {
  MPRISPlayerSnapshot player;
  player.serviceName = serviceName;
  try {
    player.identity = std::get<std::string>(getRootProperty(serviceName, "Identity", "s"));
  } catch (...) {
  }
  try {
    player.desktopEntry = std::get<std::string>(getRootProperty(serviceName, "DesktopEntry", "s"));
  } catch (...) {
  }
  player.desktopIconName = mprisDesktopIconName(player.desktopEntry, player.serviceName);

  player.playbackStatus =
      std::get<std::string>(getPlayerProperty(serviceName, "PlaybackStatus", "s"));
  player.metadata = metadataFromValue(getPlayerProperty(serviceName, "Metadata", "a{sv}"));
  player.volume = std::get<double>(getPlayerProperty(serviceName, "Volume", "d"));
  player.positionUsec = std::get<std::int64_t>(getPlayerProperty(serviceName, "Position", "x"));
  try {
    player.loopStatus = std::get<std::string>(getPlayerProperty(serviceName, "LoopStatus", "s"));
  } catch (...) {
  }
  try {
    player.rate = std::get<double>(getPlayerProperty(serviceName, "Rate", "d"));
  } catch (...) {
  }
  try {
    player.minimumRate = std::get<double>(getPlayerProperty(serviceName, "MinimumRate", "d"));
  } catch (...) {
  }
  try {
    player.maximumRate = std::get<double>(getPlayerProperty(serviceName, "MaximumRate", "d"));
  } catch (...) {
  }
  try {
    player.shuffle = std::get<bool>(getPlayerProperty(serviceName, "Shuffle", "b"));
  } catch (...) {
  }
  player.canGoNext = std::get<bool>(getPlayerProperty(serviceName, "CanGoNext", "b"));
  player.canGoPrevious = std::get<bool>(getPlayerProperty(serviceName, "CanGoPrevious", "b"));
  player.canPlay = std::get<bool>(getPlayerProperty(serviceName, "CanPlay", "b"));
  player.canPause = std::get<bool>(getPlayerProperty(serviceName, "CanPause", "b"));
  player.canSeek = std::get<bool>(getPlayerProperty(serviceName, "CanSeek", "b"));
  player.canControl = std::get<bool>(getPlayerProperty(serviceName, "CanControl", "b"));
  player.progress = mprisTrackProgress(player);
  player.trackList = readTrackList(serviceName);
  return player;
}

MPRISChangeWatch MPRISClient::watchPlayerChanges(std::function<void()> handler) {
  auto sharedHandler = std::make_shared<std::function<void()>>(std::move(handler));
  return MPRISChangeWatch{
      .propertiesChanged =
          bus_.matchSignal(
              dbus::SignalMatch{
                  .sender = {},
                  .path = objectPath,
                  .interface = kPropertiesInterface,
                  .member = "PropertiesChanged",
              },
              [sharedHandler](dbus::Message& message) {
                auto const changed = dbus::readPropertiesChanged(message);
                if (changed.interface != MPRISClient::rootInterfaceName &&
                    changed.interface != MPRISClient::playerInterfaceName &&
                    changed.interface != MPRISClient::trackListInterfaceName) {
                  return;
                }
                notify(sharedHandler);
              }),
      .seeked =
          bus_.matchSignal(
              dbus::SignalMatch{
                  .sender = {},
                  .path = objectPath,
                  .interface = playerInterfaceName,
                  .member = "Seeked",
              },
              [sharedHandler](dbus::Message&) {
                notify(sharedHandler);
              }),
      .trackListReplaced =
          bus_.matchSignal(
              dbus::SignalMatch{
                  .sender = {},
                  .path = objectPath,
                  .interface = trackListInterfaceName,
                  .member = "TrackListReplaced",
              },
              [sharedHandler](dbus::Message&) {
                notify(sharedHandler);
              }),
      .trackAdded =
          bus_.matchSignal(
              dbus::SignalMatch{
                  .sender = {},
                  .path = objectPath,
                  .interface = trackListInterfaceName,
                  .member = "TrackAdded",
              },
              [sharedHandler](dbus::Message&) {
                notify(sharedHandler);
              }),
      .trackRemoved =
          bus_.matchSignal(
              dbus::SignalMatch{
                  .sender = {},
                  .path = objectPath,
                  .interface = trackListInterfaceName,
                  .member = "TrackRemoved",
              },
              [sharedHandler](dbus::Message&) {
                notify(sharedHandler);
              }),
      .trackMetadataChanged =
          bus_.matchSignal(
              dbus::SignalMatch{
                  .sender = {},
                  .path = objectPath,
                  .interface = trackListInterfaceName,
                  .member = "TrackMetadataChanged",
              },
              [sharedHandler](dbus::Message&) {
                notify(sharedHandler);
              }),
      .nameOwnerChanged =
          bus_.matchSignal(
              dbus::SignalMatch{
                  .sender = dbusServiceName,
                  .path = dbusObjectPath,
                  .interface = dbusInterfaceName,
                  .member = "NameOwnerChanged",
              },
              [sharedHandler](dbus::Message& message) {
                std::string name = message.readString();
                (void)message.readString();
                (void)message.readString();
                if (startsWith(name, MPRISClient::servicePrefix)) {
                  notify(sharedHandler);
                }
              }),
  };
}

void MPRISClient::playPause(std::string const& serviceName) {
  callPlayerMethod(serviceName, "PlayPause");
}

void MPRISClient::play(std::string const& serviceName) {
  callPlayerMethod(serviceName, "Play");
}

void MPRISClient::pause(std::string const& serviceName) {
  callPlayerMethod(serviceName, "Pause");
}

void MPRISClient::stop(std::string const& serviceName) {
  callPlayerMethod(serviceName, "Stop");
}

void MPRISClient::next(std::string const& serviceName) {
  callPlayerMethod(serviceName, "Next");
}

void MPRISClient::previous(std::string const& serviceName) {
  callPlayerMethod(serviceName, "Previous");
}

void MPRISClient::setVolume(std::string const& serviceName, double volume) {
  bus_.setProperty(dbus::PropertyAddress{
                       .destination = serviceName,
                       .path = objectPath,
                       .interface = playerInterfaceName,
                       .name = "Volume",
                   },
                   volume);
}

dbus::BasicValue MPRISClient::getRootProperty(std::string const& serviceName,
                                              std::string const& name,
                                              std::string_view signature) {
  return bus_.getProperty(dbus::PropertyAddress{
                              .destination = serviceName,
                              .path = objectPath,
                              .interface = rootInterfaceName,
                              .name = name,
                          },
                          signature);
}

dbus::BasicValue MPRISClient::getPlayerProperty(std::string const& serviceName,
                                                std::string const& name,
                                                std::string_view signature) {
  return bus_.getProperty(dbus::PropertyAddress{
                              .destination = serviceName,
                              .path = objectPath,
                              .interface = playerInterfaceName,
                              .name = name,
                          },
                          signature);
}

dbus::BasicValue MPRISClient::getTrackListProperty(std::string const& serviceName,
                                                   std::string const& name,
                                                   std::string_view signature) {
  return bus_.getProperty(dbus::PropertyAddress{
                              .destination = serviceName,
                              .path = objectPath,
                              .interface = trackListInterfaceName,
                              .name = name,
                          },
                          signature);
}

MPRISTrackListSnapshot MPRISClient::readTrackList(std::string const& serviceName) {
  MPRISTrackListSnapshot snapshot;
  dbus::ObjectPathArray tracks;
  try {
    tracks = std::get<dbus::ObjectPathArray>(getTrackListProperty(serviceName, "Tracks", "ao"));
  } catch (...) {
    return snapshot;
  }

  snapshot.available = true;
  snapshot.trackIds = pathStrings(tracks);
  try {
    snapshot.canEditTracks =
        std::get<bool>(getTrackListProperty(serviceName, "CanEditTracks", "b"));
  } catch (...) {
  }

  if (tracks.values.empty()) {
    return snapshot;
  }

  try {
    dbus::Message reply = bus_.call(dbus::MethodCall{
        .destination = serviceName,
        .path = objectPath,
        .interface = trackListInterfaceName,
        .member = "GetTracksMetadata",
        .arguments = {tracks},
    });
    auto rawTracks = std::get<std::shared_ptr<dbus::ArrayValue>>(reply.readBasic("aa{sv}"));
    if (!rawTracks) {
      return snapshot;
    }
    for (auto const& rawTrack : rawTracks->values) {
      auto const* metadata = std::get_if<std::shared_ptr<dbus::VariantDictionary>>(&rawTrack);
      if (!metadata || !*metadata) {
        continue;
      }
      snapshot.tracks.push_back(metadataFromDictionary(**metadata));
    }
  } catch (...) {
  }
  return snapshot;
}

void MPRISClient::callPlayerMethod(std::string const& serviceName, std::string const& member) {
  (void)bus_.call(dbus::MethodCall{
      .destination = serviceName,
      .path = objectPath,
      .interface = playerInterfaceName,
      .member = member,
      .arguments = {},
  });
}

std::string mprisArtworkCacheKey(std::string_view artUrl) {
  if (artUrl.empty()) {
    return {};
  }

  std::uint64_t hash = 1469598103934665603ull;
  for (unsigned char ch : artUrl) {
    hash ^= ch;
    hash *= 1099511628211ull;
  }

  std::string_view path = artUrl.substr(0, artUrl.find_first_of("?#"));
  std::string extension;
  std::size_t const slash = path.find_last_of('/');
  std::size_t const dot = path.find_last_of('.');
  if (dot != std::string_view::npos && (slash == std::string_view::npos || dot > slash)) {
    std::string_view rawExtension = path.substr(dot);
    if (rawExtension.size() >= 2 && rawExtension.size() <= 8 &&
        std::all_of(rawExtension.begin() + 1, rawExtension.end(), safeExtensionChar)) {
      extension.reserve(rawExtension.size());
      for (char ch : rawExtension) {
        extension.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
      }
    }
  }

  std::ostringstream output;
  output << "mpris-art-" << std::hex << std::setw(16) << std::setfill('0') << hash << extension;
  return output.str();
}

std::string mprisDesktopIconName(std::string_view desktopEntry, std::string_view serviceName) {
  std::string name = desktopEntry.empty() ? serviceSuffix(serviceName) : std::string(desktopEntry);
  if (name.ends_with(".desktop")) {
    name.resize(name.size() - 8u);
  }
  return name;
}

std::optional<double> mprisTrackProgress(MPRISPlayerSnapshot const& player) {
  if (player.metadata.lengthUsec <= 0 || player.positionUsec < 0) {
    return std::nullopt;
  }
  double const progress =
      static_cast<double>(player.positionUsec) / static_cast<double>(player.metadata.lengthUsec);
  return std::clamp(progress, 0.0, 1.0);
}

bool isStaleMPRISPlayer(MPRISPlayerSnapshot const& player) {
  return player.serviceName.empty() || !knownPlaybackStatus(player.playbackStatus);
}

bool mprisPlayerSupportsAction(MPRISPlayerSnapshot const& player, MPRISPlayerAction action) {
  if (isStaleMPRISPlayer(player) || !player.canControl) {
    return false;
  }

  switch (action) {
  case MPRISPlayerAction::PlayPause:
    if (player.playbackStatus == "Playing") {
      return player.canPause || player.canPlay;
    }
    return player.canPlay || player.canPause;
  case MPRISPlayerAction::Stop:
    return player.playbackStatus != "Stopped";
  case MPRISPlayerAction::Next:
    return player.canGoNext;
  case MPRISPlayerAction::Previous:
    return player.canGoPrevious;
  case MPRISPlayerAction::Seek:
    return player.canSeek;
  case MPRISPlayerAction::SetVolume:
    return true;
  }
  return false;
}

std::optional<MPRISPlayerSnapshot> activeMPRISPlayer(
    std::vector<MPRISPlayerSnapshot> const& players) {
  auto const select = [&](std::string_view status,
                          bool requirePlaybackToggle) -> std::optional<MPRISPlayerSnapshot> {
    auto const found = std::find_if(players.begin(), players.end(), [&](auto const& player) {
      if (isStaleMPRISPlayer(player) || !player.canControl ||
          player.playbackStatus != status) {
        return false;
      }
      return !requirePlaybackToggle ||
             mprisPlayerSupportsAction(player, MPRISPlayerAction::PlayPause);
    });
    if (found == players.end()) return std::nullopt;
    return *found;
  };

  if (auto playing = select("Playing", true)) return playing;
  if (auto playing = select("Playing", false)) return playing;
  if (auto paused = select("Paused", true)) return paused;
  if (auto paused = select("Paused", false)) return paused;
  if (auto stopped = select("Stopped", true)) return stopped;

  auto const controllable = std::find_if(players.begin(), players.end(), [](auto const& player) {
    return !isStaleMPRISPlayer(player) && hasAnyControls(player);
  });
  if (controllable != players.end()) return *controllable;
  return std::nullopt;
}

std::optional<std::string> activeMPRISPlayerService(
    std::vector<MPRISPlayerSnapshot> const& players) {
  if (auto player = activeMPRISPlayer(players)) {
    return player->serviceName;
  }
  return std::nullopt;
}

std::string formatMPRISStatus(std::vector<MPRISPlayerSnapshot> const& players) {
  auto const playing = std::find_if(players.begin(), players.end(), [](auto const& player) {
    return !isStaleMPRISPlayer(player) && player.playbackStatus == "Playing";
  });
  if (playing != players.end()) {
    return displayTitle(*playing);
  }

  auto const paused = std::find_if(players.begin(), players.end(), [](auto const& player) {
    return !isStaleMPRISPlayer(player) && player.playbackStatus == "Paused";
  });
  if (paused != players.end()) {
    return "paused: " + displayTitle(*paused);
  }

  auto const stopped = std::find_if(players.begin(), players.end(), [](auto const& player) {
    return !isStaleMPRISPlayer(player) && player.playbackStatus == "Stopped";
  });
  if (stopped == players.end()) {
    return "unavailable";
  }
  return "stopped";
}

} // namespace lambda::system
