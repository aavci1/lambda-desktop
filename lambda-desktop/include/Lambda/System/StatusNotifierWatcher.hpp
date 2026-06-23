#pragma once

/// \file Lambda/System/StatusNotifierWatcher.hpp
///
/// Minimal StatusNotifierWatcher service support for tray/status items.

#include <Lambda/System/DBus.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace lambdaui::system {

struct StatusNotifierItemRegistration {
  std::string serviceName;
  std::string objectPath;
  std::string ownerName;

  bool operator==(StatusNotifierItemRegistration const&) const = default;
};

struct StatusNotifierItemAddress {
  std::string id;
  std::string serviceName;
  std::string objectPath;

  bool operator==(StatusNotifierItemAddress const&) const = default;
};

struct StatusNotifierItemPixmap {
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::vector<std::uint8_t> data;

  bool operator==(StatusNotifierItemPixmap const&) const = default;
};

struct StatusNotifierItemTooltip {
  std::string iconName;
  std::vector<StatusNotifierItemPixmap> iconPixmaps;
  std::string title;
  std::string description;

  bool operator==(StatusNotifierItemTooltip const&) const = default;
};

struct StatusNotifierItemProperties {
  StatusNotifierItemAddress address;
  bool propertiesAvailable = false;
  std::string category;
  std::string itemId;
  std::string title;
  std::string status;
  std::string iconName;
  std::string overlayIconName;
  std::string attentionIconName;
  std::vector<StatusNotifierItemPixmap> iconPixmaps;
  std::vector<StatusNotifierItemPixmap> overlayIconPixmaps;
  std::vector<StatusNotifierItemPixmap> attentionIconPixmaps;
  StatusNotifierItemTooltip tooltip;
  bool tooltipAvailable = false;
  dbus::ObjectPath menu;
  bool itemIsMenu = false;
};

class StatusNotifierWatcherService {
public:
  static constexpr char const* serviceName = "org.kde.StatusNotifierWatcher";
  static constexpr char const* objectPath = "/StatusNotifierWatcher";
  static constexpr char const* interfaceName = "org.kde.StatusNotifierWatcher";
  static constexpr char const* defaultItemObjectPath = "/StatusNotifierItem";
  static constexpr std::int32_t protocolVersion = 0;

  explicit StatusNotifierWatcherService(dbus::Bus& bus);

  [[nodiscard]] dbus::Slot exportObject();
  [[nodiscard]] dbus::Slot watchNameOwners();

  [[nodiscard]] bool registerStatusNotifierItem(std::string serviceOrPath,
                                                std::string sender = {});
  [[nodiscard]] bool registerStatusNotifierHost(std::string serviceOrPath,
                                                std::string sender = {});
  [[nodiscard]] bool removeName(std::string const& name);

  [[nodiscard]] std::vector<StatusNotifierItemRegistration> const& items() const noexcept {
    return items_;
  }
  [[nodiscard]] std::vector<std::string> registeredItemServices() const;
  [[nodiscard]] std::vector<std::string> registeredHosts() const { return hosts_; }
  [[nodiscard]] bool isHostRegistered() const noexcept { return !hosts_.empty(); }

private:
  struct NormalizedRegistration {
    std::string serviceName;
    std::string objectPath;
    std::string ownerName;
  };

  [[nodiscard]] std::optional<NormalizedRegistration> normalizeItem(std::string serviceOrPath,
                                                                    std::string sender) const;
  [[nodiscard]] std::optional<std::string> normalizeService(std::string serviceOrPath,
                                                           std::string sender) const;
  void emitItemRegistered(std::string const& service) const;
  void emitItemUnregistered(std::string const& service) const;
  void emitHostRegistered() const;

  dbus::Bus* bus_ = nullptr;
  std::vector<StatusNotifierItemRegistration> items_;
  std::vector<std::string> hosts_;
};

struct StatusNotifierItemsWatch {
  dbus::Slot itemRegistered;
  dbus::Slot itemUnregistered;
};

[[nodiscard]] StatusNotifierItemAddress parseStatusNotifierItemAddress(std::string id);

class StatusNotifierWatcherClient {
public:
  explicit StatusNotifierWatcherClient(dbus::Bus bus);

  [[nodiscard]] static StatusNotifierWatcherClient connectSession();

  [[nodiscard]] dbus::Bus& bus() noexcept { return bus_; }
  [[nodiscard]] dbus::Bus const& bus() const noexcept { return bus_; }

  void registerHost(std::string serviceName);
  [[nodiscard]] std::vector<std::string> registeredItems();
  [[nodiscard]] std::vector<StatusNotifierItemAddress> registeredItemAddresses();
  [[nodiscard]] StatusNotifierItemProperties readItemProperties(StatusNotifierItemAddress const& address);
  [[nodiscard]] std::vector<StatusNotifierItemProperties> registeredItemProperties();
  [[nodiscard]] StatusNotifierItemsWatch watchItems(std::function<void()> handler);
  [[nodiscard]] dbus::Slot watchItemProperties(StatusNotifierItemAddress const& address,
                                               std::function<void()> handler);

private:
  dbus::Bus bus_;
};

} // namespace lambdaui::system
