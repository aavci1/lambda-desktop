#include <Lambda/System/MPRIS.hpp>

#include <algorithm>
#include <memory>
#include <utility>

namespace lambda::system {

namespace {

bool startsWith(std::string const& value, std::string_view prefix) {
  return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
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
  return MPRISMetadata{
      .trackId = stringValue(metadata, "mpris:trackid"),
      .title = stringValue(metadata, "xesam:title"),
      .album = stringValue(metadata, "xesam:album"),
      .artists = stringArrayValue(metadata, "xesam:artist"),
      .artUrl = stringValue(metadata, "mpris:artUrl"),
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

  player.playbackStatus =
      std::get<std::string>(getPlayerProperty(serviceName, "PlaybackStatus", "s"));
  player.metadata = metadataFromValue(getPlayerProperty(serviceName, "Metadata", "a{sv}"));
  player.volume = std::get<double>(getPlayerProperty(serviceName, "Volume", "d"));
  player.positionUsec = std::get<std::int64_t>(getPlayerProperty(serviceName, "Position", "x"));
  player.canGoNext = std::get<bool>(getPlayerProperty(serviceName, "CanGoNext", "b"));
  player.canGoPrevious = std::get<bool>(getPlayerProperty(serviceName, "CanGoPrevious", "b"));
  player.canPlay = std::get<bool>(getPlayerProperty(serviceName, "CanPlay", "b"));
  player.canPause = std::get<bool>(getPlayerProperty(serviceName, "CanPause", "b"));
  player.canSeek = std::get<bool>(getPlayerProperty(serviceName, "CanSeek", "b"));
  player.canControl = std::get<bool>(getPlayerProperty(serviceName, "CanControl", "b"));
  return player;
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

void MPRISClient::callPlayerMethod(std::string const& serviceName, std::string const& member) {
  (void)bus_.call(dbus::MethodCall{
      .destination = serviceName,
      .path = objectPath,
      .interface = playerInterfaceName,
      .member = member,
      .arguments = {},
  });
}

std::string formatMPRISStatus(std::vector<MPRISPlayerSnapshot> const& players) {
  if (players.empty()) {
    return "unavailable";
  }

  auto const playing = std::find_if(players.begin(), players.end(), [](auto const& player) {
    return player.playbackStatus == "Playing";
  });
  if (playing != players.end()) {
    return displayTitle(*playing);
  }

  auto const paused = std::find_if(players.begin(), players.end(), [](auto const& player) {
    return player.playbackStatus == "Paused";
  });
  if (paused != players.end()) {
    return "paused: " + displayTitle(*paused);
  }

  return "stopped";
}

} // namespace lambda::system
