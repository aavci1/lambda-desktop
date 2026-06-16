#include "Shell/UI/LambdaShellTypes.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iterator>
#include <string_view>
#include <utility>

namespace lambda_shell {
namespace {

std::string lowerAscii(std::string_view value) {
  std::string text(value);
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}

bool unavailableStatus(std::string_view value) {
  std::string const text = lowerAscii(value);
  return text.empty() || text == "unknown" || text == "unavailable" || text == "n/a";
}

bool offStatus(std::string_view value) {
  std::string const text = lowerAscii(value);
  return text == "off" || text == "disabled" || text == "muted" || text == "0%" || text == "0";
}

bool stoppedMediaStatus(std::string_view value) {
  std::string const text = lowerAscii(value);
  return offStatus(value) || text == "stopped";
}

bool containsAsciiInsensitive(std::string_view haystack, std::string_view needle) {
  if (needle.empty()) return true;
  return lowerAscii(haystack).find(lowerAscii(needle)) != std::string::npos;
}

struct LauncherShellAction {
  char const* id;
  char const* label;
  char const* icon;
  char const* keywords;
};

std::vector<DockItem> launcherShellActions(std::string_view query) {
  if (query.empty()) return {};

  LauncherShellAction const actions[] = {
      {"shell.lock", "Lock", "lock", "screen secure session"},
      {"shell.logout", "Log Out", "logout", "sign out session exit"},
      {"shell.suspend", "Suspend", "sleep", "sleep standby power"},
      {"shell.hibernate", "Hibernate", "sleep", "sleep suspend power"},
      {"shell.reboot", "Restart", "restart", "reboot power"},
      {"shell.power-off", "Power Off", "power", "shutdown turn off"},
  };

  std::vector<DockItem> results;
  for (auto const& action : actions) {
    std::string const searchable = std::string(action.label) + " " + action.id + " " + action.keywords;
    if (!containsAsciiInsensitive(searchable, query)) continue;
    DockItem item;
    item.id = action.id;
    item.kind = "shell-action";
    item.label = action.label;
    item.icon = action.icon;
    results.push_back(std::move(item));
  }
  return results;
}

DockletStatusItem unavailableItem(std::string id, lambda::IconName icon) {
  DockletStatusItem item;
  item.id = std::move(id);
  item.icon = icon;
  item.availability = StatusAvailability::Unavailable;
  return item;
}

bool batteryAvailable(SystemStatus const& status) {
  if (status.batteryStatus.available || status.batteryStatus.present) {
    return true;
  }
  return !unavailableStatus(status.battery);
}

std::string batteryLabel(SystemStatus const& status) {
  if (!status.batteryStatus.label.empty() && !unavailableStatus(status.batteryStatus.label)) {
    return status.batteryStatus.label;
  }
  return status.battery;
}

lambda::IconName batteryIcon(BatteryStatus const& status) {
  if (status.warningLevel == BatteryWarningLevel::Action ||
      status.warningLevel == BatteryWarningLevel::Critical ||
      status.chargeState == BatteryChargeState::Empty) {
    return lambda::IconName::BatteryAlert;
  }
  if (status.chargeState == BatteryChargeState::Charging ||
      status.chargeState == BatteryChargeState::PendingCharge) {
    return lambda::IconName::BatteryChargingFull;
  }
  if (status.chargeState == BatteryChargeState::Full || status.percentage >= 95) {
    return lambda::IconName::BatteryFull;
  }
  if (status.warningLevel == BatteryWarningLevel::Low ||
      (status.percentage >= 0 && status.percentage <= 15)) {
    return lambda::IconName::BatteryLow;
  }
  return lambda::IconName::BatteryAndroid4;
}

} // namespace

int clampedDockItemSize(int itemSize) {
  return std::clamp(itemSize, kDockMinItemSize, kDockMaxItemSize);
}

bool dockUsesSingleRowDocklets(int itemSize) {
  return clampedDockItemSize(itemSize) < kDockSingleRowThreshold;
}

int dockCell(int itemSize) {
  return clampedDockItemSize(itemSize) + (kDockCell - kDockIconSize);
}

int dockSlotHeight(int itemSize) {
  return kDockSlotMargin + clampedDockItemSize(itemSize) + kDockIconDotGap + kDockDotSize + kDockDotBelowPad;
}

int dockStatusCellSize(int itemSize) {
  if (!dockUsesSingleRowDocklets(itemSize)) {
    return std::max(14, static_cast<int>(std::round(static_cast<float>(clampedDockItemSize(itemSize)) * 0.4f)));
  }
  return std::max(kDockStatusCell, clampedDockItemSize(itemSize));
}

int dockStatusIconSize(int itemSize) {
  if (!dockUsesSingleRowDocklets(itemSize)) return dockStatusCellSize(itemSize);
  int const cell = dockStatusCellSize(itemSize);
  return std::clamp(cell - 8, 14, std::max(14, clampedDockItemSize(itemSize)));
}

int dockStatusGridGap(int itemSize) {
  if (!dockUsesSingleRowDocklets(itemSize)) {
    return std::max(0, static_cast<int>(std::round(static_cast<float>(clampedDockItemSize(itemSize)) * 0.2f)));
  }
  return kDockStatusGridGap;
}

int dockStatusGridColumns(int itemSize) {
  return dockUsesSingleRowDocklets(itemSize) ? kDockStatusItemCount : kDockStatusColumns;
}

int dockStatusGridRows(int itemSize) {
  return dockUsesSingleRowDocklets(itemSize) ? 1 : kDockStatusRows;
}

int dockStatusGridWidth(int itemSize) {
  int const columns = dockStatusGridColumns(itemSize);
  return columns * dockStatusCellSize(itemSize) + std::max(0, columns - 1) * dockStatusGridGap(itemSize);
}

int dockStatusGridHeight(int itemSize) {
  int const rows = dockStatusGridRows(itemSize);
  return rows * dockStatusCellSize(itemSize) + std::max(0, rows - 1) * dockStatusGridGap(itemSize);
}

float dockClockDateRowHeight(int itemSize) {
  if (dockUsesSingleRowDocklets(itemSize)) return 0.f;
  return std::max(14.f, std::round(static_cast<float>(clampedDockItemSize(itemSize)) * 0.4f));
}

float dockClockTimeRowHeight(int itemSize) {
  return dockClockDateRowHeight(itemSize);
}

float dockClockRowGap(int itemSize) {
  if (dockUsesSingleRowDocklets(itemSize)) return 0.f;
  return std::max(0.f, static_cast<float>(clampedDockItemSize(itemSize)) -
                           dockClockDateRowHeight(itemSize) -
                           dockClockTimeRowHeight(itemSize));
}

float dockClockDateFontSize(int itemSize) {
  if (dockUsesSingleRowDocklets(itemSize)) return kDockClockDateFontSize;
  return std::max(10.f, dockClockDateRowHeight(itemSize) * 0.68f);
}

float dockClockTimeFontSize(int itemSize) {
  if (dockUsesSingleRowDocklets(itemSize)) return kDockClockTimeFontSize;
  return std::max(12.f, dockClockTimeRowHeight(itemSize) * 0.9f);
}

float dockClockSingleRowFontSize(int itemSize) {
  return std::max(12.f, static_cast<float>(clampedDockItemSize(itemSize)) * 0.39f);
}

int dockItemWidth(DockItem const& item, int itemSize) {
  return item.kind == "separator" ? kDockSeparatorWidth : dockCell(itemSize);
}

int dockItemsWidth(std::vector<DockItem> const& items, int itemSize) {
  int width = 0;
  for (std::size_t i = 0; i < items.size(); ++i) {
    width += dockItemWidth(items[i], itemSize);
    if (i + 1 < items.size()) width += kDockGap;
  }
  return width;
}

int dockStatusWidth(int clockWidth, int itemSize) {
  return kDockSeparatorWidth + kDockGap + dockStatusGridWidth(itemSize) + kDockGap + kDockSeparatorWidth + kDockGap +
         std::max(kDockClockMinWidth, clockWidth);
}

int dockWidth(std::vector<DockItem> const& items, int clockWidth, int itemSize) {
  int width = static_cast<int>(kDockPaddingX * 2.f) + dockItemsWidth(items, itemSize);
  if (!items.empty()) width += kDockGap;
  width += dockStatusWidth(clockWidth, itemSize);
  return width;
}

int dockHeight(int itemSize) {
  return static_cast<int>(kDockPaddingTop + static_cast<float>(dockSlotHeight(itemSize)) + kDockPaddingBottom);
}

std::optional<std::size_t> dockItemIndexAt(std::vector<DockItem> const& items,
                                           double x,
                                           double y,
                                           int itemSize) {
  int cursor = static_cast<int>(kDockPaddingX);
  int const slotHeight = dockSlotHeight(itemSize);
  for (std::size_t i = 0; i < items.size(); ++i) {
    int const width = dockItemWidth(items[i], itemSize);
    if (items[i].kind != "separator" &&
        x >= cursor && x < cursor + width &&
        y >= kDockPaddingTop && y < kDockPaddingTop + static_cast<float>(slotHeight)) {
      return i;
    }
    cursor += width + kDockGap;
  }
  return std::nullopt;
}

std::vector<DockItem> launcherResults(std::vector<DockItem> const& items, std::string query) {
  std::vector<DockItem> results;
  std::transform(query.begin(), query.end(), query.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  for (auto const& item : items) {
    if (item.kind != "app") continue;
    std::string label = item.label;
    std::transform(label.begin(), label.end(), label.begin(), [](unsigned char c) {
      return static_cast<char>(std::tolower(c));
    });
    if (query.empty() || label.find(query) != std::string::npos || item.appId.find(query) != std::string::npos) {
      results.push_back(item);
    }
  }
  auto actions = launcherShellActions(query);
  results.insert(results.end(),
                 std::make_move_iterator(actions.begin()),
                 std::make_move_iterator(actions.end()));
  return results;
}

LauncherLayout launcherLayout(int width) {
  width = std::max(1, width);
  LauncherLayout layout;
  layout.fieldW = std::max(120.f, std::min(420.f, static_cast<float>(width) - 48.f));
  layout.fieldX = (static_cast<float>(width) - layout.fieldW) * 0.5f;
  layout.columns = std::max(1, std::min(4, (width - 80) / (layout.tileW + layout.gap)));
  layout.gridW = layout.columns * layout.tileW + (layout.columns - 1) * layout.gap;
  layout.startX = std::max(0, (width - layout.gridW) / 2);
  return layout;
}

std::optional<std::size_t> launcherResultAt(int width,
                                            bool launcherOpen,
                                            std::vector<DockItem> const& results,
                                            double x,
                                            double y) {
  if (!launcherOpen || results.empty()) return std::nullopt;
  LauncherLayout const layout = launcherLayout(width);
  double const localY = y - (layout.fieldY + 76.0);
  if (localY < 0.0) return std::nullopt;
  int const row = static_cast<int>(localY) / (layout.tileH + layout.gap);
  int const col = (static_cast<int>(x) - layout.startX) / (layout.tileW + layout.gap);
  if (col < 0 || col >= layout.columns) return std::nullopt;
  int const cellX = static_cast<int>(x) - layout.startX - col * (layout.tileW + layout.gap);
  int const cellY = static_cast<int>(localY) - row * (layout.tileH + layout.gap);
  if (cellX < 0 || cellX >= layout.tileW || cellY < 0 || cellY >= layout.tileH) return std::nullopt;
  std::size_t const index = static_cast<std::size_t>(row * layout.columns + col);
  if (index >= results.size()) return std::nullopt;
  return index;
}

bool launcherPointerInsideContent(int width,
                                  bool launcherOpen,
                                  std::vector<DockItem> const& results,
                                  double x,
                                  double y) {
  LauncherLayout const layout = launcherLayout(width);
  bool const inField = x >= layout.fieldX &&
                       x < layout.fieldX + layout.fieldW &&
                       y >= layout.fieldY &&
                       y < layout.fieldY + 48.f;
  return inField || launcherResultAt(width, launcherOpen, results, x, y).has_value();
}

std::vector<DockletStatusItem> dockletStatusItems(SystemStatus const& status) {
  std::vector<DockletStatusItem> items;
  items.reserve(5);

  if (!unavailableStatus(status.wifi)) {
    DockletStatusItem item;
    item.id = "network";
    item.icon = offStatus(status.wifi) ? lambda::IconName::WifiOff : lambda::IconName::Wifi;
    item.label = offStatus(status.wifi) ? std::string{} : status.wifi;
    item.availability = StatusAvailability::Available;
    item.active = !offStatus(status.wifi);
    items.push_back(std::move(item));
  } else if (!unavailableStatus(status.network)) {
    DockletStatusItem item;
    item.id = "network";
    item.icon = offStatus(status.network) ? lambda::IconName::WifiOff : lambda::IconName::NetworkWifi;
    item.availability = StatusAvailability::Available;
    item.active = !offStatus(status.network);
    items.push_back(std::move(item));
  } else {
    items.push_back(unavailableItem("network", lambda::IconName::WifiOff));
  }

  if (unavailableStatus(status.bluetooth)) {
    items.push_back(unavailableItem("bluetooth", lambda::IconName::BluetoothDisabled));
  } else {
    DockletStatusItem item;
    item.id = "bluetooth";
    item.icon = offStatus(status.bluetooth) ? lambda::IconName::BluetoothDisabled : lambda::IconName::BluetoothConnected;
    item.availability = StatusAvailability::Available;
    item.active = !offStatus(status.bluetooth);
    items.push_back(std::move(item));
  }

  if (unavailableStatus(status.volume)) {
    items.push_back(unavailableItem("volume", lambda::IconName::VolumeOff));
  } else {
    DockletStatusItem item;
    item.id = "volume";
    item.icon = offStatus(status.volume) ? lambda::IconName::VolumeOff : lambda::IconName::VolumeUp;
    item.label = offStatus(status.volume) ? std::string{} : status.volume;
    item.availability = StatusAvailability::Available;
    item.active = !offStatus(status.volume);
    items.push_back(std::move(item));
  }

  if (!batteryAvailable(status)) {
    items.push_back(unavailableItem("battery", lambda::IconName::BatteryUnknown));
  } else {
    DockletStatusItem item;
    item.id = "battery";
    std::string const label = batteryLabel(status);
    item.icon = batteryIcon(status.batteryStatus);
    item.label = offStatus(label) ? std::string{} : label;
    item.availability = StatusAvailability::Available;
    item.active = !offStatus(label);
    items.push_back(std::move(item));
  }

  if (unavailableStatus(status.media)) {
    items.push_back(unavailableItem("media", lambda::IconName::MusicOff));
  } else {
    DockletStatusItem item;
    item.id = "media";
    item.icon = stoppedMediaStatus(status.media) ? lambda::IconName::MusicOff : lambda::IconName::MusicNote;
    item.label = stoppedMediaStatus(status.media) ? std::string{} : status.media;
    item.availability = StatusAvailability::Available;
    item.active = !stoppedMediaStatus(status.media);
    items.push_back(std::move(item));
  }

  DockletStatusItem session;
  session.id = "session";
  session.icon = lambda::IconName::PowerSettingsNew;
  session.availability = StatusAvailability::Available;
  session.active = true;
  items.push_back(std::move(session));

  return items;
}

std::string dockClockDateText(std::string_view timeText) {
  std::size_t const split = timeText.find(", ");
  return split == std::string_view::npos ? std::string{timeText} : std::string{timeText.substr(0, split)};
}

std::string dockClockTimeText(std::string_view timeText) {
  std::size_t const split = timeText.find(", ");
  return split == std::string_view::npos ? std::string{} : std::string{timeText.substr(split + 2)};
}

} // namespace lambda_shell
