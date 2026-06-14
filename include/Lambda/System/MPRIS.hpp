#pragma once

/// \file Lambda/System/MPRIS.hpp
///
/// Minimal MPRIS client used by future Shell now-playing and media-key providers.

#include <Lambda/System/DBus.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lambda::system {

struct MPRISMetadata {
  std::string trackId;
  std::string title;
  std::string album;
  std::vector<std::string> artists;
  std::string artUrl;
  std::int64_t lengthUsec = 0;

  bool operator==(MPRISMetadata const&) const = default;
};

struct MPRISPlayerSnapshot {
  std::string serviceName;
  std::string identity;
  std::string desktopEntry;
  std::string playbackStatus;
  MPRISMetadata metadata;
  double volume = 0.0;
  std::int64_t positionUsec = 0;
  bool canGoNext = false;
  bool canGoPrevious = false;
  bool canPlay = false;
  bool canPause = false;
  bool canSeek = false;
  bool canControl = false;

  bool operator==(MPRISPlayerSnapshot const&) const = default;
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

  explicit MPRISClient(dbus::Bus bus);

  [[nodiscard]] static MPRISClient connectSession();

  [[nodiscard]] dbus::Bus& bus() noexcept { return bus_; }
  [[nodiscard]] dbus::Bus const& bus() const noexcept { return bus_; }

  [[nodiscard]] std::vector<std::string> playerServiceNames();
  [[nodiscard]] std::vector<MPRISPlayerSnapshot> readPlayers();
  [[nodiscard]] MPRISPlayerSnapshot readPlayer(std::string const& serviceName);

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
  void callPlayerMethod(std::string const& serviceName, std::string const& member);

  dbus::Bus bus_;
};

[[nodiscard]] std::string formatMPRISStatus(std::vector<MPRISPlayerSnapshot> const& players);

} // namespace lambda::system
