#include "Shell/UI/LambdaShellTypes.hpp"

#include <algorithm>
#include <cctype>
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

TopBarStatusItem unavailableItem(std::string id, flux::IconName icon) {
  TopBarStatusItem item;
  item.id = std::move(id);
  item.icon = icon;
  item.availability = StatusAvailability::Unavailable;
  return item;
}

} // namespace

int dockItemWidth(DockItem const& item) {
  return item.kind == "separator" ? kDockSeparatorWidth : kDockCell;
}

int dockWidth(std::vector<DockItem> const& items) {
  int width = static_cast<int>(kDockPaddingX * 2.f);
  for (std::size_t i = 0; i < items.size(); ++i) {
    width += dockItemWidth(items[i]);
    if (i + 1 < items.size()) width += kDockGap;
  }
  return width;
}

int dockHeight() {
  return static_cast<int>(kDockPaddingTop + static_cast<float>(kDockSlotHeight) + kDockPaddingBottom);
}

std::optional<std::size_t> dockItemIndexAt(std::vector<DockItem> const& items, double x, double y) {
  int cursor = static_cast<int>(kDockPaddingX);
  for (std::size_t i = 0; i < items.size(); ++i) {
    int const width = dockItemWidth(items[i]);
    if (items[i].kind != "separator" &&
        x >= cursor && x < cursor + width &&
        y >= kDockPaddingTop && y < kDockPaddingTop + static_cast<float>(kDockIconSize)) {
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

std::vector<TopBarStatusItem> topBarStatusItems(SystemStatus const& status) {
  std::vector<TopBarStatusItem> items;
  items.reserve(4);

  if (!unavailableStatus(status.wifi)) {
    TopBarStatusItem item;
    item.id = "network";
    item.icon = offStatus(status.wifi) ? flux::IconName::WifiOff : flux::IconName::Wifi;
    item.label = offStatus(status.wifi) ? std::string{} : status.wifi;
    item.availability = StatusAvailability::Available;
    item.active = !offStatus(status.wifi);
    items.push_back(std::move(item));
  } else if (!unavailableStatus(status.network)) {
    TopBarStatusItem item;
    item.id = "network";
    item.icon = offStatus(status.network) ? flux::IconName::WifiOff : flux::IconName::NetworkWifi;
    item.availability = StatusAvailability::Available;
    item.active = !offStatus(status.network);
    items.push_back(std::move(item));
  } else {
    items.push_back(unavailableItem("network", flux::IconName::WifiOff));
  }

  if (unavailableStatus(status.bluetooth)) {
    items.push_back(unavailableItem("bluetooth", flux::IconName::BluetoothDisabled));
  } else {
    TopBarStatusItem item;
    item.id = "bluetooth";
    item.icon = offStatus(status.bluetooth) ? flux::IconName::BluetoothDisabled : flux::IconName::BluetoothConnected;
    item.availability = StatusAvailability::Available;
    item.active = !offStatus(status.bluetooth);
    items.push_back(std::move(item));
  }

  if (unavailableStatus(status.volume)) {
    items.push_back(unavailableItem("volume", flux::IconName::VolumeOff));
  } else {
    TopBarStatusItem item;
    item.id = "volume";
    item.icon = offStatus(status.volume) ? flux::IconName::VolumeOff : flux::IconName::VolumeUp;
    item.label = offStatus(status.volume) ? std::string{} : status.volume;
    item.availability = StatusAvailability::Available;
    item.active = !offStatus(status.volume);
    items.push_back(std::move(item));
  }

  if (unavailableStatus(status.battery)) {
    items.push_back(unavailableItem("battery", flux::IconName::BatteryUnknown));
  } else {
    TopBarStatusItem item;
    item.id = "battery";
    item.icon = offStatus(status.battery) ? flux::IconName::BatteryAlert : flux::IconName::BatteryAndroid4;
    item.label = offStatus(status.battery) ? std::string{} : status.battery;
    item.availability = StatusAvailability::Available;
    item.active = !offStatus(status.battery);
    items.push_back(std::move(item));
  }

  return items;
}

} // namespace lambda_shell
