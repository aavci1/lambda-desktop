#pragma once

#include "FilesFlowGrid.hpp"
#include "FilesGlyphs.hpp"
#include "FilesListView.hpp"
#include "FilesStore.hpp"
#include "FilesTheme.hpp"
#include "FilesTrace.hpp"

#include <Lambda.hpp>
#include <Lambda/System/DBus.hpp>
#include <Lambda/UI/EnvironmentKeys.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/KeyCodes.hpp>
#include <Lambda/UI/Views/For.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Icon.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/ScrollView.hpp>
#include <Lambda/UI/Views/Show.hpp>
#include <Lambda/UI/Views/Spacer.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/VStack.hpp>
#include <Lambda/UI/Views/ZStack.hpp>
#include <Lambda/UI/Window.hpp>

#include <algorithm>
#include <chrono>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace lambda_files {

namespace {

using namespace lambda;

struct ChromeInsets {
  float leading = 0.f;
  float trailing = 0.f;
};

ChromeInsets chromeInsets(WindowChromeMetrics const& chrome) {
  ChromeInsets insets;
  for (Rect const& rect : chrome.reservedRegions) {
    if (rect.x < 120.f) {
      insets.leading = std::max(insets.leading, rect.x + rect.width + 8.f);
    } else {
      insets.trailing = std::max(insets.trailing, rect.width + 8.f);
    }
  }
  return insets;
}

ShadowStyle softControlShadow() {
  return ShadowStyle{
      .radius = 24.f,
      .offset = {0.f, 2.f},
      .color = Color{0.05f, 0.10f, 0.18f, 0.045f},
  };
}

StrokeStyle softSurfaceStroke() {
  return StrokeStyle::solid(Color{1.f, 1.f, 1.f, 0.34f}, 1.f);
}

struct IconToolButton {
  IconName icon = IconName::Apps;
  float iconSize = 18.f;
  Reactive::Bindable<bool> enabledState{true};
  Reactive::Bindable<bool> activeState{false};
  bool raised = false;
  std::function<void()> onTap;

  Element body() const {
    auto hover = useHover();
    Reactive::Bindable<bool> const enabled = enabledState;
    Reactive::Bindable<bool> const active = activeState;
    Reactive::Bindable<Color> const iconColor{[hover, enabled, active] {
      if (!enabled.evaluate()) {
        return FilesTheme::text3;
      }
      if (active.evaluate()) {
        return FilesTheme::text;
      }
      return hover() ? FilesTheme::text : FilesTheme::text2;
    }};
    bool const raisedSurface = raised;
    Reactive::Bindable<FillStyle> const fill{[hover, enabled, active, raisedSurface] {
      if (active.evaluate()) {
        return FillStyle::solid(FilesTheme::viewToggleFill);
      }
      if (raisedSurface) {
        if (enabled.evaluate() && hover()) {
          return FillStyle::solid(Color{1.f, 1.f, 1.f, 0.46f});
        }
        return FillStyle::solid(FilesTheme::glassSoft);
      }
      return !enabled.evaluate() || !hover()
                 ? FillStyle::solid(Colors::transparent)
                 : FillStyle::solid(FilesTheme::hoverFill);
    }};

    auto button = ZStack{
                      .horizontalAlignment = Alignment::Center,
                      .verticalAlignment = Alignment::Center,
                      .children = children(Icon{.name = icon, .size = iconSize, .color = iconColor})}
                      .size(FilesTheme::kToolbarBtn, FilesTheme::kToolbarBtn)
                      .fill(fill)
                      .cornerRadius(8.f)
                      .stroke(raised ? softSurfaceStroke() : StrokeStyle::none())
                      .shadow(raised ? softControlShadow() : ShadowStyle::none());
    if (onTap) {
      auto handler = onTap;
      button = std::move(button).onTap([enabled, handler] {
        if (enabled.evaluate()) {
          handler();
        }
      });
    }
    return button;
  }
};

struct NavSegmentButton {
  IconName icon = IconName::ChevronLeft;
  Reactive::Bindable<bool> enabledState{true};
  CornerRadius radius{};
  std::function<void()> onTap;

  Element body() const {
    auto hover = useHover();
    Reactive::Bindable<bool> const enabled = enabledState;
    Reactive::Bindable<Color> const iconColor{[hover, enabled] {
      if (!enabled.evaluate()) {
        return FilesTheme::text3;
      }
      return hover() ? FilesTheme::text : FilesTheme::text2;
    }};
    Reactive::Bindable<FillStyle> const fill{[hover, enabled] {
      return enabled.evaluate() && hover()
                 ? FillStyle::solid(Color{1.f, 1.f, 1.f, 0.38f})
                 : FillStyle::solid(Colors::transparent);
    }};

    auto button = ZStack{
                      .horizontalAlignment = Alignment::Center,
                      .verticalAlignment = Alignment::Center,
                      .children = children(Icon{.name = icon, .size = 20.f, .color = iconColor})}
                      .size(30.f, FilesTheme::kToolbarBtn)
                      .fill(fill)
                      .cornerRadius(radius);
    if (onTap) {
      auto handler = onTap;
      button = std::move(button).onTap([enabled, handler] {
        if (enabled.evaluate()) {
          handler();
        }
      });
    }
    return button;
  }
};

struct NavSegmentedControl {
  Reactive::Bindable<bool> canGoBack{false};
  Reactive::Bindable<bool> canGoForward{false};
  std::function<void()> goBack;
  std::function<void()> goForward;

  Element body() const {
    return HStack{
               .spacing = 0.f,
               .alignment = Alignment::Center,
               .children = children(
                   NavSegmentButton{
                       .icon = IconName::ChevronLeft,
                       .enabledState = canGoBack,
                       .radius = CornerRadius{8.f, 0.f, 0.f, 8.f},
                       .onTap = goBack,
                   },
                   Rectangle{}.width(1.f).height(16.f).fill(FilesTheme::line),
                   NavSegmentButton{
                       .icon = IconName::ChevronRight,
                       .enabledState = canGoForward,
                       .radius = CornerRadius{0.f, 8.f, 8.f, 0.f},
                       .onTap = goForward,
                   })}
        .height(FilesTheme::kToolbarBtn)
        .fill(FilesTheme::glassSoft)
        .stroke(softSurfaceStroke())
        .shadow(softControlShadow())
        .cornerRadius(9.f);
  }
};

struct SideItemRow {
  SidebarPlace place;
  Reactive::Bindable<bool> isActive{false};
  std::function<void()> onTap;
  std::function<void()> onContextMenu;

  Element body() const {
    auto hover = useHover();
    Reactive::Bindable<bool> const activeBinding = isActive;
    Reactive::Bindable<FillStyle> const fill{[hover, activeBinding] {
      if (activeBinding.evaluate()) {
        return FillStyle::solid(FilesTheme::selectFill);
      }
      if (hover()) {
        return FillStyle::solid(FilesTheme::hoverFill);
      }
      return FillStyle::solid(Colors::transparent);
    }};
    Reactive::Bindable<Color> const labelColor{[activeBinding] {
      return activeBinding.evaluate() ? FilesTheme::accent : FilesTheme::text2;
    }};
    Reactive::Bindable<Color> const iconColor{[activeBinding] {
      return activeBinding.evaluate() ? FilesTheme::accent : FilesTheme::text3;
    }};

    std::vector<Element> labelChildren;
    labelChildren.push_back(Text {
        .text = place.label,
        .font = Font {.size = 14.f, .weight = 400.f},
        .color = labelColor,
        .horizontalAlignment = HorizontalAlignment::Leading,
    });
    if (!place.subtitle.empty()) {
      labelChildren.push_back(Text {
          .text = place.subtitle,
          .font = Font {.size = 11.f, .weight = 400.f},
          .color = FilesTheme::text3,
          .horizontalAlignment = HorizontalAlignment::Leading,
      });
    }

    auto row = HStack {
        .spacing = 8.f,
        .alignment = Alignment::Center,
        .children = children(
            Icon {.name = place.icon, .size = 18.f, .weight = 400.f, .color = iconColor},
            VStack {
                .spacing = 0.f,
                .alignment = Alignment::Stretch,
                .children = std::move(labelChildren),
            }
        )
    }
                   .padding(6.f, 10.f, 6.f, 10.f)
                   .fill(fill)
                   .cornerRadius(FilesTheme::kSideItemRadius);
    if (onTap || onContextMenu) {
      auto handler = onTap;
      auto contextMenu = onContextMenu;
      row = std::move(row).onTap([handler, contextMenu](MouseButton button) {
        if (button == MouseButton::Left && handler) {
          handler();
        } else if (button == MouseButton::Right && contextMenu) {
          contextMenu();
        }
      });
    }
    return row;
  }
};

struct FilesVolumeWatcher {
  FilesVolumeWatcher(lambda::Application& app,
                     FilesPreferences preferences,
                     std::function<bool()> refreshPlaces)
      : refreshPlaces(std::move(refreshPlaces)),
        preferences(std::move(preferences)),
        udisks(lambda::system::UDisks2Client::connectSystem()),
        pump(app, udisks.bus()),
        changed(udisks.watchStatusChanges([this] { refresh(); })) {}

  void refresh() {
    applyAutoMountPolicy();
    if (refreshPlaces && refreshPlaces() && lambda::Application::hasInstance()) {
      lambda::Application::instance().requestRedraw();
    }
  }

  void applyAutoMountPolicy() {
    if (!preferences.autoMountRemovable) {
      return;
    }
    try {
      for (auto const& request : autoMountRequestsForVolumes(udisks.readSnapshot(), preferences)) {
        (void)udisks.tryMountFilesystem(request.volumePath, request.options);
      }
    } catch (...) {
    }
  }

  std::function<bool()> refreshPlaces;
  FilesPreferences preferences;
  lambda::system::UDisks2Client udisks;
  lambda::dbus::BusEventPump pump;
  lambda::system::UDisks2StatusWatch changed;
};

struct BreadcrumbCrumbView {
  BreadcrumbCrumb crumb;
  bool showHomeIcon = false;
  Reactive::Bindable<bool> isLast{false};
  std::function<void()> onTap;
  std::function<void()> onContextMenu;

  Element body() const {
    Reactive::Bindable<bool> const isLastBinding = isLast;
    Reactive::Bindable<Color> const labelColor{[isLastBinding] {
      return isLastBinding.evaluate() ? FilesTheme::text
                                    : FilesTheme::text2;
    }};

    std::vector<Element> parts;
    if (showHomeIcon) {
      parts.push_back(Icon{.name = IconName::Home, .size = 14.f, .color = FilesTheme::text2});
    }
    std::string const crumbText = crumb.label;
    parts.push_back(Element{Show(
        [isLastBinding] { return isLastBinding.evaluate(); },
        [labelColor, crumbText] {
          return Text{
              .text = crumbText,
              .font = Font{.size = 12.f, .weight = 400.f},
              .color = labelColor,
          };
        },
        [labelColor, crumbText] {
          return Text{
              .text = crumbText,
              .font = Font{.size = 12.f, .weight = 400.f},
              .color = labelColor,
          };
        })});

    auto row = HStack{
                     .spacing = 5.f,
                     .alignment = Alignment::Center,
                     .children = std::move(parts)}
                     .padding(0.f, showHomeIcon ? 0.f : 2.f, 0.f, 2.f);
    if (onTap || onContextMenu) {
      auto handler = onTap;
      auto contextMenu = onContextMenu;
      row = std::move(row).onTap([handler, contextMenu](MouseButton button) {
        if (button == MouseButton::Left && handler) {
          handler();
        } else if (button == MouseButton::Right && contextMenu) {
          contextMenu();
        }
      });
    }
    return row;
  }
};

struct BreadcrumbSeparatorView {
  Element body() const {
    return Icon{.name = IconName::ChevronRight, .size = 14.f, .color = FilesTheme::text3};
  }
};

struct BreadcrumbBar {
  Reactive::Signal<std::vector<BreadcrumbCrumb>> crumbs;
  std::function<void(std::filesystem::path)> navigateToPath;
  std::function<void(std::filesystem::path)> showPathContextMenu;

  Element body() const {
    Reactive::Signal<std::vector<BreadcrumbCrumb>> const crumbsSignal = crumbs;
    auto const navigate = navigateToPath;
    auto const contextMenu = showPathContextMenu;

    Reactive::Bindable<float> const barWidth{[] {
      Rect const bounds = useBounds();
      return bounds.width > 0.f ? bounds.width : 0.f;
    }};

    Element trail = Element{For(
        crumbsSignal,
        [](BreadcrumbCrumb const& crumb) { return crumb.path.string(); },
        [navigate, contextMenu, crumbsSignal](BreadcrumbCrumb const& crumb,
                                              Signal<std::size_t> const& indexSignal) {
          Reactive::Bindable<bool> const isLast{[indexSignal, crumbsSignal] {
            return indexSignal() + 1 >= crumbsSignal().size();
          }};
          return HStack{
              .spacing = 4.f,
              .alignment = Alignment::Center,
              .children = children(
                  BreadcrumbCrumbView{
                      .crumb = crumb,
                      .showHomeIcon = indexSignal() == 0,
                      .isLast = isLast,
                      .onTap = [navigate, target = crumb.path] { navigate(target); },
                      .onContextMenu = [contextMenu, target = crumb.path] {
                        if (contextMenu) {
                          contextMenu(target);
                        }
                      },
                  },
                  Show(
                      [isLast] { return !isLast.evaluate(); },
                      [] { return Element{BreadcrumbSeparatorView{}}; },
                      [] { return Rectangle{}.size(0.f, 0.f); })),
          };
        },
        4.f,
        Alignment::Center,
        ForLayout::HorizontalStack)};

    return HStack{
               .alignment = Alignment::Center,
               .children = children(
                   Element{ScrollView{
                               .axis = ScrollAxis::Horizontal,
                               .dragScrollEnabled = true,
                               .children = children(std::move(trail)),
                           }}
                       .flex(1.f, 1.f, 0.f),
                   Spacer{}.flex(1.f, 1.f, 0.f)),
           }
               .width(barWidth)
               .height(FilesTheme::kBreadcrumbHeight)
               .padding(0.f, FilesTheme::kBreadcrumbPadH, 0.f, FilesTheme::kBreadcrumbPadH)
               .fill(FilesTheme::glassSoft)
               .stroke(softSurfaceStroke())
               .shadow(softControlShadow())
               .cornerRadius(FilesTheme::kBreadcrumbRadius)
               .clipContent(true);
  }
};

struct FilesOptionsMenu {
  Reactive::Signal<bool> showHiddenFiles;
  Reactive::Signal<std::string> viewMode;
  std::function<void()> onToggleHiddenFiles;
  std::function<void(std::string)> onSetViewMode;

  Element body() const {
    auto showMenu = usePopupMenu();
    Reactive::Signal<bool> const hiddenSignal = showHiddenFiles;
    Reactive::Signal<std::string> const viewModeSignal = viewMode;
    auto const toggle = onToggleHiddenFiles;
    auto const setViewMode = onSetViewMode;

    auto openMenu = [showMenu, hiddenSignal, viewModeSignal, toggle, setViewMode] {
      MenuItem gridView;
      gridView.label = "Grid View";
      gridView.checked = viewModeSignal() != "list";
      gridView.handler = [setViewMode] {
        if (setViewMode) {
          setViewMode("grid");
        }
      };

      MenuItem listView;
      listView.label = "List View";
      listView.checked = viewModeSignal() == "list";
      listView.handler = [setViewMode] {
        if (setViewMode) {
          setViewMode("list");
        }
      };

      MenuItem showHidden;
      showHidden.label = "Show Hidden Files";
      showHidden.checked = hiddenSignal();
      showHidden.handler = [toggle] {
        if (toggle) {
          toggle();
        }
      };
      showMenu(PopupMenu{.items = {std::move(gridView),
                                   std::move(listView),
                                   MenuItem::separator(),
                                   std::move(showHidden)}});
    };

    return IconToolButton{
        .icon = IconName::MoreHoriz,
        .iconSize = 20.f,
        .raised = true,
        .onTap = openMenu,
    };
  }
};

Element windowChromeDot(Color color, std::function<void()> onTap) {
  return Rectangle{}
      .size(12.f, 12.f)
      .fill(color)
      .cornerRadius(6.f)
      .onTap(std::move(onTap));
}

Element filesTitlebar(Window* window,
                      WindowChromeMetrics const& chrome,
                      Reactive::Bindable<bool> canGoBack,
                      Reactive::Bindable<bool> canGoForward,
                      std::function<void()> goBackNav,
                      std::function<void()> goForwardNav,
                      Reactive::Signal<std::vector<BreadcrumbCrumb>> crumbs,
                      std::function<void(std::filesystem::path)> navigateToPath,
                      std::function<void(std::filesystem::path)> showPathContextMenu,
                      Reactive::Signal<bool> showHiddenFiles,
                      std::function<void()> toggleHiddenFiles,
                      Reactive::Signal<std::string> viewMode,
                      std::function<void(std::string)> setViewMode) {
  ChromeInsets const reserved = chromeInsets(chrome);
  std::vector<Element> row;

  if (reserved.leading > 0.f) {
    row.push_back(Rectangle{}.width(reserved.leading));
  }

  row.push_back(HStack{
      .spacing = 8.f,
      .alignment = Alignment::Center,
      .children = children(
          Element{NavSegmentedControl{
              .canGoBack = canGoBack,
              .canGoForward = canGoForward,
              .goBack = goBackNav,
              .goForward = goForwardNav,
          }},
          Element{BreadcrumbBar{
              .crumbs = crumbs,
              .navigateToPath = navigateToPath,
              .showPathContextMenu = showPathContextMenu,
          }}
              .flex(1.f, 1.f, 0.f),
          Element{FilesOptionsMenu{
              .showHiddenFiles = showHiddenFiles,
              .viewMode = viewMode,
              .onToggleHiddenFiles = toggleHiddenFiles,
              .onSetViewMode = setViewMode,
          }})}
      .flex(1.f, 1.f, 0.f));

  if (!chrome.systemControlsVisible) {
    row.push_back(HStack{
        .spacing = 8.f,
        .alignment = Alignment::Center,
        .children = children(
            windowChromeDot(Color::hex(0xEB5757), [window] {
              if (window) {
                window->requestClose();
              }
            }),
            windowChromeDot(Color::hex(0xF2C94C), [] {}),
            windowChromeDot(Color::hex(0x36C275), [] {}))});
  }

  if (reserved.trailing > 0.f) {
    row.push_back(Rectangle{}.width(reserved.trailing));
  }

  return HStack{
             .spacing = 6.f,
             .alignment = Alignment::Center,
             .children = std::move(row)}
      .height(FilesTheme::kTitlebarHeight)
      .padding(FilesTheme::kTitlebarPadV, FilesTheme::kTitlebarPadH, FilesTheme::kTitlebarPadV,
               FilesTheme::kTitlebarPadH)
      .windowDragRegion();
}

} // namespace

struct FilesAppRoot {
  Window* window = nullptr;

  lambda::Element body() const {
    using namespace lambda;
    double const bodyStartMs = trace::nowMs();

    auto history = useState(NavigationHistory{.current = normalizeDirectoryPath(homeDirectory())});
    auto entries = useState(std::vector<FileEntry>{});
    auto listingKey = useState(std::string{});
    auto crumbs = useState(std::vector<BreadcrumbCrumb>{});
    auto listError = useState(std::string{});
    auto activePlaceId = useState(std::string{"home"});
    auto selectedPath = useState(std::string{});
    auto selection = useState(FileSelectionState{});
    auto clipboard = useState(FileClipboardState{});
    auto scrollOffset = useState(Point{});
    auto preferences = useState(loadFilesPreferences().preferences);
    auto showHiddenFiles = useState(preferences().showHidden);
    auto viewMode = useState(preferences().viewMode);
    auto iconThemeRoots = useState(lambda_shell::defaultIconThemeRoots(""));

    auto places = useState(sidebarPlacesWithMountedVolumes());

    auto syncListing = [history, entries, listingKey, crumbs, listError, showHiddenFiles,
                        selection, selectedPath](bool force = true) {
      double const startMs = trace::nowMs();
      std::filesystem::path const current{history().current};
      ListDirectoryResult const result = listDirectory(current, showHiddenFiles());
      if (!force && !directoryListingChanged(entries(), listError(), result)) {
        LAMBDA_FILES_TRACE_EVENT("app sync-listing force=%d changed=0 entries=%zu error=%d elapsed=%.3fms path=\"%s\"\n",
                     force ? 1 : 0,
                     result.entries.size(),
                     result.error.empty() ? 0 : 1,
                     trace::nowMs() - startMs,
                     current.string().c_str());
        return false;
      }
      FileSelectionState const currentSelection = selection();
      std::vector<FileEntry> const currentEntries = entries();
      std::optional<std::filesystem::path> anchorPath;
      if (currentSelection.anchorIndex >= 0 &&
          currentSelection.anchorIndex < static_cast<int>(currentEntries.size())) {
        anchorPath = currentEntries[static_cast<std::size_t>(currentSelection.anchorIndex)].path;
      }
      listingKey.set(current.string());
      crumbs.set(breadcrumbCrumbs(current));
      entries.set(result.entries);
      listError.set(result.error);
      if (result.error.empty()) {
        FileSelectionState kept;
        for (auto const& path : currentSelection.selected) {
          auto found = std::find_if(result.entries.begin(), result.entries.end(), [&](FileEntry const& entry) {
            return entry.path == path;
          });
          if (found != result.entries.end()) {
            kept.selected.push_back(path);
          }
        }
        if (anchorPath) {
          for (std::size_t index = 0; index < result.entries.size(); ++index) {
            if (result.entries[index].path == *anchorPath && kept.contains(*anchorPath)) {
              kept.anchorIndex = static_cast<int>(index);
              break;
            }
          }
        }
        if (kept.anchorIndex < 0 && !kept.selected.empty()) {
          for (std::size_t index = 0; index < result.entries.size(); ++index) {
            if (result.entries[index].path == kept.selected.front()) {
              kept.anchorIndex = static_cast<int>(index);
              break;
            }
          }
        }
        selection.set(kept);
        int const focused = focusedSelectionIndex(kept, result.entries);
        selectedPath.set(focused >= 0 ? result.entries[static_cast<std::size_t>(focused)].path.string()
                                      : std::string{});
      }
      LAMBDA_FILES_TRACE_EVENT("app sync-listing force=%d changed=1 entries=%zu error=%d elapsed=%.3fms path=\"%s\"\n",
                   force ? 1 : 0,
                   result.entries.size(),
                   result.error.empty() ? 0 : 1,
                   trace::nowMs() - startMs,
                   current.string().c_str());
      return true;
    };

    syncListing();

    auto syncPlaces = [places] {
      auto nextPlaces = sidebarPlacesWithMountedVolumes();
      if (nextPlaces == places()) {
        return false;
      }
      places.set(std::move(nextPlaces));
      return true;
    };

    if (Application::hasInstance()) {
      auto refreshTimerId = std::make_shared<std::uint64_t>(
          Application::instance().scheduleRepeatingTimer(std::chrono::seconds{2},
                                                         window ? window->handle() : 0u));
      Application::instance().eventQueue().on<TimerEvent>(
          [refreshTimerId, syncListing](TimerEvent const& event) {
            if (!refreshTimerId || *refreshTimerId == 0 || event.timerId != *refreshTimerId) {
              return;
            }
            if (syncListing(false) && Application::hasInstance()) {
              Application::instance().requestRedraw();
            }
          });
      onCleanup([refreshTimerId] {
        if (refreshTimerId && *refreshTimerId != 0 && Application::hasInstance()) {
          Application::instance().cancelTimer(*refreshTimerId);
          *refreshTimerId = 0;
        }
      });
    }

    useEffect([syncPlaces, preferences] {
      if (!Application::hasInstance()) {
        return;
      }
      try {
        auto watcher = std::make_shared<FilesVolumeWatcher>(Application::instance(), preferences(), syncPlaces);
        watcher->refresh();
        std::shared_ptr<void> keepAlive = watcher;
        Reactive::onCleanup([keepAlive] {});
      } catch (...) {
      }
    });

    auto applyHistory = [history, activePlaceId, selectedPath, selection, scrollOffset, syncListing](
                            NavigationHistory next) {
      history.set(std::move(next));
      activePlaceId.set(std::string{});
      selectedPath.set(std::string{});
      selection.set(FileSelectionState{});
      syncListing();
      scrollOffset.set(Point{});
    };

    auto navigateToPath = [history, applyHistory, listError](std::filesystem::path path) {
      NavigationResult result = navigateToDirectory(history(), std::move(path));
      if (!result.ok) {
        listError.set(std::move(result.error));
        return;
      }
      applyHistory(std::move(result.history));
    };

    auto performVolumeSidebarCommand =
        [navigateToPath, syncPlaces, listError](SidebarPlace place, SidebarVolumeCommandKind command) {
          try {
            auto client = lambda::system::UDisks2Client::connectSystem();
            SidebarVolumeActionResult const result =
                performSidebarVolumeAction(place, command, udisksSidebarVolumeActionBackend(client));
            if (!result.ok) {
              listError.set(result.error.empty() ? "Storage operation failed." : result.error);
              return;
            }
            listError.set(std::string{});
            if (result.refreshPlaces) {
              (void)syncPlaces();
            }
            if (result.navigateToPath) {
              navigateToPath(result.path);
            }
            if (Application::hasInstance()) {
              Application::instance().requestRedraw();
            }
          } catch (std::exception const& error) {
            listError.set(error.what());
          } catch (...) {
            listError.set("Storage operation failed.");
          }
        };

    auto goBackNav = [history, applyHistory] {
      if (!history().canGoBack()) {
        return;
      }
      applyHistory(goBack(history()));
    };

    auto goForwardNav = [history, applyHistory] {
      if (!history().canGoForward()) {
        return;
      }
      applyHistory(goForward(history()));
    };

    auto goUpNav = [history, applyHistory] {
      applyHistory(goUp(history()));
    };

    auto toggleHiddenFiles = [preferences, showHiddenFiles, selectedPath, selection, scrollOffset, syncListing] {
      FilesPreferences next = preferences();
      next.showHidden = !showHiddenFiles();
      preferences.set(next);
      showHiddenFiles.set(next.showHidden);
      (void)saveFilesPreferences(next);
      selectedPath.set(std::string{});
      selection.set(FileSelectionState{});
      syncListing();
      scrollOffset.set(Point{});
    };

    auto setViewMode = [preferences, viewMode, scrollOffset](std::string nextMode) {
      if (nextMode != "list") {
        nextMode = "grid";
      }
      if (viewMode() == nextMode) {
        return;
      }
      FilesPreferences next = preferences();
      next.viewMode = nextMode;
      preferences.set(next);
      viewMode.set(next.viewMode);
      (void)saveFilesPreferences(next);
      scrollOffset.set(Point{});
    };

    auto applySelection = [entries, selectedPath, selection](FileSelectionState next) {
      int const focused = focusedSelectionIndex(next, entries());
      if (focused >= 0) {
        selectedPath.set(entries()[static_cast<std::size_t>(focused)].path.string());
      } else if (!next.selected.empty()) {
        selectedPath.set(next.selected.back().string());
      } else {
        selectedPath.set(std::string{});
      }
      selection.set(std::move(next));
    };

    auto activateEntry = [entries, navigateToPath, applySelection](FileEntry const& entry) {
      for (std::size_t i = 0; i < entries().size(); ++i) {
        if (entries()[i].path == entry.path) {
          applySelection(selectOnly(entries(), static_cast<int>(i)));
          break;
        }
      }
      if (entry.isDirectory) {
        navigateToPath(entry.path);
        return;
      }
      std::string error;
      (void)openEntry(entry, error);
    };

    auto tapEntry = [entries, selection, applySelection, activateEntry](FileEntry const& entry,
                                                                        Modifiers modifiers) {
      int index = -1;
      for (std::size_t i = 0; i < entries().size(); ++i) {
        if (entries()[i].path == entry.path) {
          index = static_cast<int>(i);
          break;
        }
      }
      FilePointerSelectionResult const result =
          selectionForPointerTap(selection(), entries(), index, modifiers);
      applySelection(result.selection);
      if (result.activate) {
        activateEntry(entry);
      }
    };

    auto showMenu = usePopupMenu();
    auto pathsForSelection = [](std::vector<FileEntry> const& selected) {
      std::vector<std::filesystem::path> paths;
      paths.reserve(selected.size());
      for (auto const& entry : selected) paths.push_back(entry.path);
      return paths;
    };
    auto publishClipboardText = [](FileClipboardState const& state) {
      if (state.empty() || !Application::hasInstance()) {
        return;
      }
      Application::instance().clipboard().writeText(serializeFileClipboardText(state));
    };
    auto clipboardForPaste = [clipboard] {
      FileClipboardState state = clipboard();
      if (!state.empty() || !Application::hasInstance()) {
        return state;
      }
      if (std::optional<std::string> text = Application::instance().clipboard().readText()) {
        return fileClipboardFromUriListText(*text);
      }
      return state;
    };
    auto executeContextCommand =
        [history, entries, clipboard, clipboardForPaste, publishClipboardText, listError,
         activateEntry, applySelection, syncListing, pathsForSelection](
            FileContextCommandKind kind,
            FileSelectionState targetSelection) {
          std::vector<FileEntry> const selected = selectedEntries(entries(), targetSelection);
          auto const selectedPaths = pathsForSelection(selected);
          std::filesystem::path const current{history().current};
          auto recordResult = [listError](FileOperationResult const& result) {
            if (!result.ok) {
              listError.set(result.error.empty() ? "File operation failed." : result.error);
            }
          };

          switch (kind) {
          case FileContextCommandKind::Open:
            if (!selected.empty()) activateEntry(selected.front());
            return;
          case FileContextCommandKind::Reveal: {
            if (selected.empty()) return;
            std::string error;
            if (!revealEntryInSystem(selected.front(), error)) listError.set(error);
            return;
          }
          case FileContextCommandKind::Copy:
          {
            FileClipboardState next = makeFileClipboard(selectedPaths, FileClipboardIntent::Copy);
            clipboard.set(next);
            publishClipboardText(next);
            return;
          }
          case FileContextCommandKind::Cut:
          {
            FileClipboardState next = makeFileClipboard(selectedPaths, FileClipboardIntent::Cut);
            clipboard.set(next);
            publishClipboardText(next);
            return;
          }
          case FileContextCommandKind::Paste: {
            auto results = pasteFileClipboard(clipboardForPaste(), current);
            for (auto const& result : results) recordResult(result);
            syncListing();
            return;
          }
          case FileContextCommandKind::Duplicate:
            for (auto const& path : selectedPaths) recordResult(duplicatePath(path));
            syncListing();
            return;
          case FileContextCommandKind::Trash:
            for (auto const& path : selectedPaths) recordResult(trashPath(path));
            applySelection(FileSelectionState{});
            syncListing();
            return;
          case FileContextCommandKind::NewFolder:
            recordResult(createFolder(current));
            syncListing();
            return;
          case FileContextCommandKind::NewFile:
            recordResult(createFile(current));
            syncListing();
            return;
          case FileContextCommandKind::SelectAll:
            applySelection(selectAllEntries(entries()));
            return;
          }
        };

    auto showCommandMenu = [showMenu, executeContextCommand](std::vector<FileContextCommand> commands,
                                                             FileSelectionState targetSelection) {
      std::vector<MenuItem> items;
      items.reserve(commands.size());
      for (auto const& command : commands) {
        MenuItem item;
        item.label = command.label;
        item.actionName = command.label;
        item.isEnabled = [enabled = command.enabled] { return enabled; };
        if (command.enabled) {
          item.handler = [executeContextCommand, kind = command.kind, targetSelection] {
            executeContextCommand(kind, targetSelection);
          };
        }
        items.push_back(std::move(item));
      }
      showMenu(PopupMenu{.items = std::move(items)});
    };

    auto showEntryContextMenu =
        [entries, selection, clipboard, applySelection, showCommandMenu](FileEntry const& entry) {
          FileSelectionState targetSelection = selection();
          if (!targetSelection.contains(entry.path)) {
            for (std::size_t i = 0; i < entries().size(); ++i) {
              if (entries()[i].path == entry.path) {
                targetSelection = selectOnly(entries(), static_cast<int>(i));
                break;
              }
            }
          }
          applySelection(targetSelection);

          showCommandMenu(contextMenuCommands(entries(), targetSelection, clipboard(), false),
                          targetSelection);
        };

    auto showBackgroundContextMenu =
        [entries, clipboardForPaste, showCommandMenu](MouseButton button, Modifiers) {
          if (button != MouseButton::Right) {
            return;
          }
          FileSelectionState const targetSelection{};
          showCommandMenu(contextMenuCommands(entries(), targetSelection, clipboardForPaste(), true),
                          targetSelection);
        };

    auto showPathContextMenu = [navigateToPath, showMenu, listError](std::filesystem::path path) {
      std::error_code ec;
      bool const exists = std::filesystem::exists(path, ec) && !ec;
      bool const isDirectory = exists && std::filesystem::is_directory(path, ec) && !ec;
      auto makePathEntry = [path, isDirectory] {
        return FileEntry{
            .name = path.filename().empty() ? path.string() : path.filename().string(),
            .path = path,
            .isDirectory = isDirectory,
            .visualKind = isDirectory ? FileVisualKind::Folder : visualKindForEntry(path, false),
        };
      };

      MenuItem open;
      open.label = "Open";
      open.actionName = "Open";
      open.isEnabled = [isDirectory] { return isDirectory; };
      if (isDirectory) {
        open.handler = [navigateToPath, path] { navigateToPath(path); };
      }

      MenuItem reveal;
      reveal.label = "Reveal in Folder";
      reveal.actionName = "Reveal in Folder";
      reveal.isEnabled = [exists] { return exists; };
      if (exists) {
        reveal.handler = [makePathEntry, listError] {
          std::string error;
          if (!revealEntryInSystem(makePathEntry(), error)) {
            listError.set(error);
          }
        };
      }

      showMenu(PopupMenu{.items = {std::move(open), std::move(reveal)}});
    };

    auto showVolumeContextMenu =
        [showMenu, performVolumeSidebarCommand](SidebarPlace place) {
          std::vector<SidebarVolumeCommand> commands = sidebarVolumeContextCommands(place);
          std::vector<MenuItem> items;
          items.reserve(commands.size());
          for (auto const& command : commands) {
            MenuItem item;
            item.label = command.label;
            item.actionName = command.label;
            item.isEnabled = [enabled = command.enabled] { return enabled; };
            if (command.enabled) {
              item.handler = [performVolumeSidebarCommand, place, kind = command.kind] {
                performVolumeSidebarCommand(place, kind);
              };
            }
            items.push_back(std::move(item));
          }
          showMenu(PopupMenu{.items = std::move(items)});
        };

    Reactive::Bindable<bool> const canGoBack{[history] { return history().canGoBack(); }};
    Reactive::Bindable<bool> const canGoForward{[history] { return history().canGoForward(); }};

    auto chrome = useEnvironment<WindowChromeMetricsKey>();
    WindowChromeMetrics const metrics = chrome();
    Rect const bounds = useBounds();
    float const gridWidth = std::max(0.f,
                                     bounds.width - FilesTheme::kSidebarWidth - 1.f -
                                         2.f * FilesTheme::kContentPadH);
    int const gridColumns = std::max(1, FilesFlowGridLayout{}.columnCountForWidth(gridWidth));

    FilesFlowGrid filesGrid{
        .entries = entries,
        .listingKey = listingKey,
        .selectedPath = selectedPath,
        .selection = selection,
        .iconThemeRoots = iconThemeRoots(),
        .iconSize = preferences().iconSize,
        .activateEntry = activateEntry,
        .tapEntry = tapEntry,
        .showEntryContextMenu = showEntryContextMenu,
    };
    FilesListView filesList{
        .entries = entries,
        .selectedPath = selectedPath,
        .selection = selection,
        .iconThemeRoots = iconThemeRoots(),
        .iconSize = preferences().iconSize,
        .activateEntry = activateEntry,
        .tapEntry = tapEntry,
        .showEntryContextMenu = showEntryContextMenu,
    };
    Element root = VStack{
        .spacing = 0.f,
        .alignment = Alignment::Stretch,
        .children = children(
            filesTitlebar(window, metrics, canGoBack, canGoForward, goBackNav, goForwardNav, crumbs,
                          navigateToPath, showPathContextMenu, showHiddenFiles, toggleHiddenFiles,
                          viewMode, setViewMode),
            Rectangle{}.height(1.f).fill(FilesTheme::line),
            HStack{
                .spacing = 0.f,
                .alignment = Alignment::Stretch,
                .children = children(
                    VStack{
                        .spacing = 2.f,
                        .alignment = Alignment::Stretch,
                        .children = children(
                            Element{For(
                                places,
                                [](SidebarPlace const& place) { return place.id; },
                                [activePlaceId, navigateToPath, performVolumeSidebarCommand,
                                 showPathContextMenu, showVolumeContextMenu](
                                    SidebarPlace const& place, Signal<std::size_t> const&) {
                                  Reactive::Bindable<bool> active{
                                      [activePlaceId, id = place.id] { return activePlaceId() == id; }};
                                  return SideItemRow{
                                      .place = place,
                                      .isActive = active,
                                      .onTap = [navigateToPath, performVolumeSidebarCommand, place,
                                                activePlaceId] {
                                        activePlaceId.set(place.id);
                                        if (place.kind == SidebarPlaceKind::Volume) {
                                          performVolumeSidebarCommand(place, SidebarVolumeCommandKind::Open);
                                          return;
                                        }
                                        navigateToPath(place.path);
                                      },
                                      .onContextMenu = [showPathContextMenu, showVolumeContextMenu, place] {
                                        if (place.kind == SidebarPlaceKind::Volume) {
                                          showVolumeContextMenu(place);
                                          return;
                                        }
                                        showPathContextMenu(place.path);
                                      },
                                  };
                                })},
                            Spacer{}.flex(1.f, 1.f))}
                        .width(FilesTheme::kSidebarWidth)
                        .padding(FilesTheme::kSidePad, FilesTheme::kSidePad, FilesTheme::kSidePad,
                                 FilesTheme::kSidePad),
                    Rectangle{}.width(1.f).fill(FilesTheme::line),
                    ScrollView{
                        .axis = ScrollAxis::Vertical,
                        .scrollOffset = scrollOffset,
                        .dragScrollEnabled = true,
                        .onTap = showBackgroundContextMenu,
                        .children = children(
                            Show(
                                [listError] { return !listError().empty(); },
                                [listError] {
                                  return Text{
                                      .text = listError,
                                      .font = Font::body(),
                                      .color = Color::warning(),
                                      .wrapping = TextWrapping::Wrap,
                                  }
                                      .padding(FilesTheme::kContentPadV, FilesTheme::kContentPadH,
                                               FilesTheme::kContentPadV, FilesTheme::kContentPadH);
                                },
                                [filesGrid, filesList, viewMode] {
                                  return Show(
                                      [viewMode] { return viewMode() == "list"; },
                                      [filesList] {
                                        return Element{filesList}
                                            .padding(FilesTheme::kContentPadV, FilesTheme::kContentPadH,
                                                     FilesTheme::kContentPadV, FilesTheme::kContentPadH);
                                      },
                                      [filesGrid] {
                                        return Element{filesGrid}
                                            .padding(FilesTheme::kContentPadV, FilesTheme::kContentPadH,
                                                     FilesTheme::kContentPadV, FilesTheme::kContentPadH);
                                      });
                                }))}
                        .flex(1.f, 1.f, 0.f))}
                .flex(1.f, 1.f, 0.f)),
    };

    root = std::move(root).onKeyDown(
        [goBackNav, goForwardNav, goUpNav, selectedPath, selection, entries, activateEntry, applySelection,
         gridColumns, viewMode](
            KeyCode key, Modifiers modifiers) {
          using namespace lambda::keys;
          bool const extend = any(modifiers & Modifiers::Shift);
          bool const command = any(modifiers & Modifiers::Meta) || any(modifiers & Modifiers::Ctrl);
          int const verticalStep = viewMode() == "list" ? 1 : gridColumns;
          if (key == Delete && any(modifiers & Modifiers::Meta)) {
            goUpNav();
            return;
          }
          if (key == LeftArrow && any(modifiers & Modifiers::Meta)) {
            goBackNav();
            return;
          }
          if (key == RightArrow && any(modifiers & Modifiers::Meta)) {
            goForwardNav();
            return;
          }
          if (key == A && command) {
            applySelection(selectAllEntries(entries()));
            return;
          }
          if (key == Escape) {
            applySelection(clearSelection(selection()));
            return;
          }
          if (key == LeftArrow) {
            applySelection(moveSelectionByOffset(selection(), entries(), -1, extend));
            return;
          }
          if (key == RightArrow) {
            applySelection(moveSelectionByOffset(selection(), entries(), 1, extend));
            return;
          }
          if (key == UpArrow) {
            applySelection(moveSelectionByOffset(selection(), entries(), -verticalStep, extend));
            return;
          }
          if (key == DownArrow) {
            applySelection(moveSelectionByOffset(selection(), entries(), verticalStep, extend));
            return;
          }
          if (key == Home) {
            applySelection(moveSelectionToIndex(selection(), entries(), 0, extend));
            return;
          }
          if (key == End) {
            applySelection(moveSelectionToIndex(selection(), entries(), static_cast<int>(entries().size()) - 1, extend));
            return;
          }
          if (key == Return) {
            std::string const selected = selectedPath();
            if (selected.empty()) {
              return;
            }
            for (FileEntry const& entry : entries()) {
              if (entry.path.string() == selected) {
                activateEntry(entry);
                return;
              }
            }
          }
        });

    LAMBDA_FILES_TRACE_EVENT("app body bounds=%.1fx%.1f entries=%zu view=%s gridWidth=%.1f columns=%d elapsed=%.3fms\n",
                 bounds.width,
                 bounds.height,
                 entries().size(),
                 viewMode().c_str(),
                 gridWidth,
                 gridColumns,
                 trace::nowMs() - bodyStartMs);
    return root;
  }
};

} // namespace lambda_files
