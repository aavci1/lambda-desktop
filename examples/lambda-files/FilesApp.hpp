#pragma once

#include "FilesFlowGrid.hpp"
#include "FilesGlyphs.hpp"
#include "FilesStore.hpp"
#include "FilesTheme.hpp"

#include <Flux.hpp>
#include <Flux/UI/EnvironmentKeys.hpp>
#include <Flux/UI/Hooks.hpp>
#include <Flux/UI/KeyCodes.hpp>
#include <Flux/UI/Views/For.hpp>
#include <Flux/UI/Views/HStack.hpp>
#include <Flux/UI/Views/Icon.hpp>
#include <Flux/UI/Views/Rectangle.hpp>
#include <Flux/UI/Views/ScrollView.hpp>
#include <Flux/UI/Views/Show.hpp>
#include <Flux/UI/Views/Spacer.hpp>
#include <Flux/UI/Views/Text.hpp>
#include <Flux/UI/Views/VStack.hpp>
#include <Flux/UI/Views/ZStack.hpp>
#include <Flux/UI/Window.hpp>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace lambda_files {

namespace {

using namespace flux;

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

struct IconToolButton {
  IconName icon = IconName::Apps;
  float iconSize = 18.f;
  Reactive::Bindable<bool> enabledState{true};
  Reactive::Bindable<bool> activeState{false};
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
    Reactive::Bindable<FillStyle> const fill{[hover, enabled, active] {
      if (active.evaluate()) {
        return FillStyle::solid(FilesTheme::viewToggleFill);
      }
      if (!enabled.evaluate() || !hover()) {
        return FillStyle::solid(Colors::transparent);
      }
      return FillStyle::solid(FilesTheme::hoverFill);
    }};

    auto button = ZStack{
                      .horizontalAlignment = Alignment::Center,
                      .verticalAlignment = Alignment::Center,
                      .children = children(Icon{.name = icon, .size = iconSize, .color = iconColor})}
                      .size(FilesTheme::kToolbarBtn, FilesTheme::kToolbarBtn)
                      .fill(fill)
                      .cornerRadius(7.f);
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

struct SideItemRow {
  SidebarPlace place;
  Reactive::Bindable<bool> isActive{false};
  std::function<void()> onTap;

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

    auto row = HStack{
                   .spacing = 8.f,
                   .alignment = Alignment::Center,
                   .children = children(
                       Icon{.name = place.icon, .size = 16.f, .color = iconColor},
                       Text{
                           .text = place.label,
                           .font = Font{.size = 12.5f, .weight = 500.f},
                           .color = labelColor,
                           .horizontalAlignment = HorizontalAlignment::Leading,
                       })}
               .padding(6.f, 10.f, 6.f, 10.f)
               .fill(fill)
               .cornerRadius(FilesTheme::kSideItemRadius);
    if (onTap) {
      auto handler = onTap;
      row = std::move(row).onTap([handler] { handler(); });
    }
    return row;
  }
};

struct BreadcrumbCrumbView {
  BreadcrumbCrumb crumb;
  bool showHomeIcon = false;
  Reactive::Bindable<bool> isLast{false};
  std::function<void()> onTap;

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
              .font = Font{.size = 12.f, .weight = 600.f},
              .color = labelColor,
          };
        },
        [labelColor, crumbText] {
          return Text{
              .text = crumbText,
              .font = Font{.size = 12.f, .weight = 500.f},
              .color = labelColor,
          };
        })});

    auto row = HStack{
                     .spacing = 5.f,
                     .alignment = Alignment::Center,
                     .children = std::move(parts)}
                     .padding(0.f, showHomeIcon ? 0.f : 2.f, 0.f, 2.f);
    if (onTap) {
      auto handler = onTap;
      row = std::move(row).onTap([handler] { handler(); });
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

  Element body() const {
    Reactive::Signal<std::vector<BreadcrumbCrumb>> const crumbsSignal = crumbs;
    auto const navigate = navigateToPath;

    Reactive::Bindable<float> const barWidth{[] {
      Rect const bounds = useBounds();
      return bounds.width > 0.f ? bounds.width : 0.f;
    }};

    Element trail = Element{For(
        crumbsSignal,
        [](BreadcrumbCrumb const& crumb) { return crumb.path.string(); },
        [navigate, crumbsSignal](BreadcrumbCrumb const& crumb, Signal<std::size_t> const& indexSignal) {
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
               .cornerRadius(FilesTheme::kBreadcrumbRadius)
               .clipContent(true);
  }
};

struct FilesOptionsMenu {
  Reactive::Signal<bool> showHiddenFiles;
  std::function<void()> onToggleHiddenFiles;

  Element body() const {
    auto showMenu = usePopupMenu();
    Reactive::Signal<bool> const hiddenSignal = showHiddenFiles;
    auto const toggle = onToggleHiddenFiles;

    auto openMenu = [showMenu, hiddenSignal, toggle] {
      MenuItem showHidden;
      showHidden.label = "Show Hidden Files";
      showHidden.checked = hiddenSignal();
      showHidden.handler = [toggle] {
        if (toggle) {
          toggle();
        }
      };
      showMenu(PopupMenu{.items = {std::move(showHidden)}});
    };

    return IconToolButton{
        .icon = IconName::MoreHoriz,
        .iconSize = 20.f,
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
                      Reactive::Signal<bool> showHiddenFiles,
                      std::function<void()> toggleHiddenFiles) {
  ChromeInsets const reserved = chromeInsets(chrome);
  std::vector<Element> row;

  if (reserved.leading > 0.f) {
    row.push_back(Rectangle{}.width(reserved.leading));
  }

  row.push_back(HStack{
      .spacing = 6.f,
      .alignment = Alignment::Center,
      .children = children(
          Element{IconToolButton{
              .icon = IconName::ChevronLeft,
              .iconSize = 20.f,
              .enabledState = canGoBack,
              .onTap = goBackNav,
          }},
          Element{IconToolButton{
              .icon = IconName::ChevronRight,
              .iconSize = 20.f,
              .enabledState = canGoForward,
              .onTap = goForwardNav,
          }},
          Element{BreadcrumbBar{.crumbs = crumbs, .navigateToPath = navigateToPath}}
              .flex(1.f, 1.f, 0.f),
          Element{FilesOptionsMenu{
              .showHiddenFiles = showHiddenFiles,
              .onToggleHiddenFiles = toggleHiddenFiles,
          }})}
      .flex(1.f, 1.f, 0.f));

  if (!chrome.nativeControlsVisible) {
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
      .fill(FilesTheme::chromeBg)
      .windowDragRegion();
}

} // namespace

struct FilesAppRoot {
  Window* window = nullptr;

  flux::Element body() const {
    using namespace flux;

    auto history = useState(NavigationHistory{.current = normalizeDirectoryPath(homeDirectory())});
    auto entries = useState(std::vector<FileEntry>{});
    auto listingKey = useState(std::string{});
    auto crumbs = useState(std::vector<BreadcrumbCrumb>{});
    auto listError = useState(std::string{});
    auto activePlaceId = useState(std::string{"home"});
    auto selectedPath = useState(std::string{});
    auto scrollOffset = useState(Point{});
    auto showHiddenFiles = useState(false);

    auto places = useState(sidebarPlaces());

    auto syncListing = [history, entries, listingKey, crumbs, listError, showHiddenFiles] {
      std::filesystem::path const current{history().current};
      listingKey.set(current.string());
      crumbs.set(breadcrumbCrumbs(current));
      ListDirectoryResult const result = listDirectory(current, showHiddenFiles());
      entries.set(result.entries);
      listError.set(result.error);
    };

    syncListing();

    auto applyHistory = [history, activePlaceId, selectedPath, scrollOffset, syncListing](
                            NavigationHistory next) {
      history.set(std::move(next));
      activePlaceId.set(std::string{});
      selectedPath.set(std::string{});
      syncListing();
      scrollOffset.set(Point{});
    };

    auto navigateToPath = [history, applyHistory](std::filesystem::path path) {
      applyHistory(navigateTo(history(), std::move(path)));
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

    auto toggleHiddenFiles = [showHiddenFiles, selectedPath, scrollOffset, syncListing] {
      showHiddenFiles.set(!showHiddenFiles());
      selectedPath.set(std::string{});
      syncListing();
      scrollOffset.set(Point{});
    };

    auto activateEntry = [navigateToPath, selectedPath](FileEntry const& entry) {
      if (entry.isDirectory) {
        navigateToPath(entry.path);
        return;
      }
      selectedPath.set(entry.path.string());
      std::string error;
      (void)openEntry(entry, error);
    };

    Reactive::Bindable<bool> const canGoBack{[history] { return history().canGoBack(); }};
    Reactive::Bindable<bool> const canGoForward{[history] { return history().canGoForward(); }};

    auto chrome = useEnvironment<WindowChromeMetricsKey>();
    WindowChromeMetrics const metrics = chrome();

    FilesFlowGrid filesGrid{
        .entries = entries,
        .listingKey = listingKey,
        .selectedPath = selectedPath,
        .activateEntry = activateEntry,
    };

    auto root = VStack{
        .spacing = 0.f,
        .alignment = Alignment::Stretch,
        .children = children(
            filesTitlebar(window, metrics, canGoBack, canGoForward, goBackNav, goForwardNav, crumbs,
                          navigateToPath, showHiddenFiles, toggleHiddenFiles),
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
                                [activePlaceId, navigateToPath](SidebarPlace const& place,
                                                                  Signal<std::size_t> const&) {
                                  Reactive::Bindable<bool> active{
                                      [activePlaceId, id = place.id] { return activePlaceId() == id; }};
                                  return SideItemRow{
                                      .place = place,
                                      .isActive = active,
                                      .onTap = [navigateToPath, place, activePlaceId] {
                                        activePlaceId.set(place.id);
                                        navigateToPath(place.path);
                                      },
                                  };
                                })},
                            Spacer{}.flex(1.f, 1.f))}
                        .width(FilesTheme::kSidebarWidth)
                        .padding(FilesTheme::kSidePad, FilesTheme::kSidePad, FilesTheme::kSidePad,
                                 FilesTheme::kSidePad)
                        .fill(FilesTheme::sideBg),
                    Rectangle{}
                        .width(1.f)
                        .fill(FilesTheme::line),
                    ScrollView{
                        .axis = ScrollAxis::Vertical,
                        .scrollOffset = scrollOffset,
                        .dragScrollEnabled = true,
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
                                [filesGrid] {
                                  return Element{filesGrid}
                                      .padding(FilesTheme::kContentPadV, FilesTheme::kContentPadH,
                                               FilesTheme::kContentPadV, FilesTheme::kContentPadH);
                                }))}
                        .flex(1.f, 1.f, 0.f)
                        .fill(FilesTheme::windowBg))}
                .flex(1.f, 1.f, 0.f)),
    }
        .fill(FilesTheme::windowBg);

    root = std::move(root).onKeyDown(
        [goBackNav, goForwardNav, goUpNav, selectedPath, entries, activateEntry](
            KeyCode key, Modifiers modifiers) {
          using namespace flux::keys;
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

    return root;
  }
};

} // namespace lambda_files
