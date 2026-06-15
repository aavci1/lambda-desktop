#pragma once

/// \file Lambda/System/Notifications.hpp
///
/// Minimal Freedesktop notifications service support.

#include <Lambda/System/DBus.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace lambda::system {

enum class NotificationCloseReason : std::uint32_t {
  Expired = 1,
  DismissedByUser = 2,
  ClosedByCall = 3,
  Undefined = 4,
};

struct NotificationAction {
  std::string key;
  std::string label;

  bool operator==(NotificationAction const&) const = default;
};

struct NotificationRecord {
  std::uint32_t id = 0;
  std::string appName;
  std::string appIcon;
  std::string summary;
  std::string body;
  std::int32_t expireTimeoutMs = -1;
  std::vector<NotificationAction> actions;
  bool closed = false;
  NotificationCloseReason closeReason = NotificationCloseReason::Undefined;

  bool operator==(NotificationRecord const&) const = default;
};

struct NotificationPosted {
  std::uint32_t id = 0;
  std::string appName;
  std::string appIcon;
  std::string summary;
  std::string body;
  std::vector<NotificationAction> actions;
  std::int32_t expireTimeoutMs = -1;

  bool operator==(NotificationPosted const&) const = default;
};

class NotificationsService {
public:
  static constexpr char const* serviceName = "org.freedesktop.Notifications";
  static constexpr char const* objectPath = "/org/freedesktop/Notifications";
  static constexpr char const* interfaceName = "org.freedesktop.Notifications";
  static constexpr char const* monitorInterfaceName = "org.lambda.Notifications";
  static constexpr char const* postedSignalName = "NotificationPosted";
  static constexpr char const* invokeActionMethodName = "InvokeAction";

  explicit NotificationsService(dbus::Bus& bus, std::size_t historyLimit = 100);

  [[nodiscard]] dbus::Slot exportObject();

  [[nodiscard]] std::uint32_t notify(std::string appName,
                                     std::uint32_t replacesId,
                                     std::string appIcon,
                                     std::string summary,
                                     std::string body,
                                     dbus::StringArray actions,
                                     std::int32_t expireTimeoutMs);
  bool closeNotification(std::uint32_t id,
                         NotificationCloseReason reason = NotificationCloseReason::ClosedByCall);
  bool invokeAction(std::uint32_t id, std::string actionKey);

  [[nodiscard]] std::vector<std::string> capabilities() const;
  [[nodiscard]] std::vector<NotificationRecord> const& history() const noexcept { return notifications_; }
  [[nodiscard]] std::optional<NotificationRecord> notification(std::uint32_t id) const;
  [[nodiscard]] bool doNotDisturb() const noexcept { return doNotDisturb_; }
  void setDoNotDisturb(bool enabled) noexcept { doNotDisturb_ = enabled; }

private:
  [[nodiscard]] NotificationRecord* find(std::uint32_t id);
  [[nodiscard]] NotificationRecord const* find(std::uint32_t id) const;
  void emitPosted(NotificationRecord const& record) const;
  void emitClosed(std::uint32_t id, NotificationCloseReason reason) const;
  void emitActionInvoked(std::uint32_t id, std::string const& actionKey) const;
  void trimHistory();

  dbus::Bus* bus_ = nullptr;
  std::size_t historyLimit_ = 100;
  std::uint32_t nextId_ = 1;
  bool doNotDisturb_ = false;
  std::vector<NotificationRecord> notifications_;
};

class NotificationsClient {
public:
  explicit NotificationsClient(dbus::Bus bus);

  [[nodiscard]] static NotificationsClient connectSession();

  [[nodiscard]] dbus::Bus& bus() noexcept { return bus_; }
  [[nodiscard]] dbus::Bus const& bus() const noexcept { return bus_; }

  [[nodiscard]] dbus::Slot watchPosted(std::function<void(NotificationPosted)> handler);
  [[nodiscard]] dbus::Slot watchClosed(std::function<void(std::uint32_t, NotificationCloseReason)> handler);
  void closeNotification(std::uint32_t id);
  void invokeAction(std::uint32_t id, std::string actionKey);

private:
  dbus::Bus bus_;
};

} // namespace lambda::system
