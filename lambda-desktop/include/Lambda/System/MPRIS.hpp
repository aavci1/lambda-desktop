#pragma once

/// \file Lambda/System/MPRIS.hpp
///
/// Minimal MPRIS client used by Shell now-playing and media-key providers.

#include <Lambda/System/DBus.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lambda::system {

struct MPRISMetadata {
  std::string trackId;
  std::string title;
  std::string album;
  std::vector<std::string> artists;
  std::vector<std::string> albumArtists;
  std::vector<std::string> genres;
  std::string artUrl;
  std::string artCacheKey;
  std::string url;
  std::string contentCreated;
  std::int32_t discNumber = 0;
  std::int32_t trackNumber = 0;
  std::int64_t lengthUsec = 0;

  bool operator==(MPRISMetadata const&) const = default;
};

struct MPRISTrackListSnapshot {
  bool available = false;
  bool canEditTracks = false;
  std::vector<std::string> trackIds;
  std::vector<MPRISMetadata> tracks;

  bool operator==(MPRISTrackListSnapshot const&) const = default;
};

struct MPRISPlayerSnapshot {
  std::string serviceName;
  std::string identity;
  std::string desktopEntry;
  std::string desktopIconName;
  std::string playbackStatus;
  std::string loopStatus;
  MPRISMetadata metadata;
  double volume = 0.0;
  std::int64_t positionUsec = 0;
  double rate = 1.0;
  double minimumRate = 1.0;
  double maximumRate = 1.0;
  bool shuffle = false;
  bool canGoNext = false;
  bool canGoPrevious = false;
  bool canPlay = false;
  bool canPause = false;
  bool canSeek = false;
  bool canControl = false;
  std::optional<double> progress;
  MPRISTrackListSnapshot trackList;

  bool operator==(MPRISPlayerSnapshot const&) const = default;
};

enum class MPRISPlayerAction : std::uint8_t {
  PlayPause,
  Stop,
  Next,
  Previous,
  Seek,
  SetVolume,
};

struct MPRISChangeWatch {
  dbus::Slot propertiesChanged;
  dbus::Slot seeked;
  dbus::Slot trackListReplaced;
  dbus::Slot trackAdded;
  dbus::Slot trackRemoved;
  dbus::Slot trackMetadataChanged;
  dbus::Slot nameOwnerChanged;
};

class MPRISClient {
public:
  static constexpr char const* servicePrefix = "org.mpris.MediaPlayer2.";
  static constexpr char const* dbusServiceName = "org.freedesktop.DBus";
  static constexpr char const* dbusObjectPath = "/org/freedesktop/DBus";
  static constexpr char const* dbusInterfaceName = "org.freedesktop.DBus";
  static constexpr char const* objectPath = "/org/mpris/MediaPlayer2";
  static constexpr char const* rootInterfaceName = "org.mpris.MediaPlayer2";
  static constexpr char const* playerInterfaceName = "org.mpris.MediaPlayer2.Player";
  static constexpr char const* trackListInterfaceName = "org.mpris.MediaPlayer2.TrackList";

  explicit MPRISClient(dbus::Bus bus);

  [[nodiscard]] static MPRISClient connectSession();

  [[nodiscard]] dbus::Bus& bus() noexcept { return bus_; }
  [[nodiscard]] dbus::Bus const& bus() const noexcept { return bus_; }

  [[nodiscard]] std::vector<std::string> playerServiceNames();
  [[nodiscard]] std::vector<MPRISPlayerSnapshot> readPlayers();
  [[nodiscard]] MPRISPlayerSnapshot readPlayer(std::string const& serviceName);
  [[nodiscard]] MPRISChangeWatch watchPlayerChanges(std::function<void()> handler);

  void playPause(std::string const& serviceName);
  void play(std::string const& serviceName);
  void pause(std::string const& serviceName);
  void stop(std::string const& serviceName);
  void next(std::string const& serviceName);
  void previous(std::string const& serviceName);
  void setVolume(std::string const& serviceName, double volume);

private:
  [[nodiscard]] dbus::BasicValue getRootProperty(std::string const& serviceName,
                                                 std::string const& name,
                                                 std::string_view signature);
  [[nodiscard]] dbus::BasicValue getPlayerProperty(std::string const& serviceName,
                                                   std::string const& name,
                                                   std::string_view signature);
  [[nodiscard]] dbus::BasicValue getTrackListProperty(std::string const& serviceName,
                                                      std::string const& name,
                                                      std::string_view signature);
  [[nodiscard]] MPRISTrackListSnapshot readTrackList(std::string const& serviceName);
  void callPlayerMethod(std::string const& serviceName, std::string const& member);

  dbus::Bus bus_;
};

[[nodiscard]] std::string mprisArtworkCacheKey(std::string_view artUrl);
[[nodiscard]] std::string mprisDesktopIconName(std::string_view desktopEntry,
                                               std::string_view serviceName);
[[nodiscard]] std::optional<double> mprisTrackProgress(MPRISPlayerSnapshot const& player);
[[nodiscard]] bool isStaleMPRISPlayer(MPRISPlayerSnapshot const& player);
[[nodiscard]] bool mprisPlayerSupportsAction(MPRISPlayerSnapshot const& player,
                                             MPRISPlayerAction action);
[[nodiscard]] std::optional<MPRISPlayerSnapshot> activeMPRISPlayer(
    std::vector<MPRISPlayerSnapshot> const& players);
[[nodiscard]] std::optional<std::string> activeMPRISPlayerService(
    std::vector<MPRISPlayerSnapshot> const& players);
[[nodiscard]] std::string formatMPRISStatus(std::vector<MPRISPlayerSnapshot> const& players);

} // namespace lambda::system
