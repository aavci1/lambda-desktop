#include <Lambda/UI/Views/FileDialog.hpp>

#include <Lambda/Graphics/Styles.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/IconName.hpp>
#include <Lambda/UI/Shortcut.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/Views/Button.hpp>
#include <Lambda/UI/Views/For.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Icon.hpp>
#include <Lambda/UI/Views/ListView.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/ScrollView.hpp>
#include <Lambda/UI/Views/Show.hpp>
#include <Lambda/UI/Views/Spacer.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/Tooltip.hpp>
#include <Lambda/UI/Views/VStack.hpp>
#include <Lambda/UI/Views/ZStack.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace lambda {

namespace {

constexpr float kFileDialogListHeight = 300.f;
constexpr float kFileDialogRowHeight = 38.f;

struct FileDialogEntry {
  std::filesystem::path path;
  std::string name;
  bool directory = false;
  std::uintmax_t size = 0;

  bool operator==(FileDialogEntry const&) const = default;
};

struct FileDialogPlace {
  std::filesystem::path path;
  std::string label;
  IconName icon = IconName::Folder;

  bool operator==(FileDialogPlace const&) const = default;
};

struct FileDialogCrumb {
  std::filesystem::path path;
  std::string label;

  bool operator==(FileDialogCrumb const&) const = default;
};

struct DirectoryListing {
  std::filesystem::path directory;
  std::vector<FileDialogEntry> entries;
  std::string status;
};

std::string displayName(std::filesystem::path const& path) {
  std::string name = path.filename().string();
  return name.empty() ? path.string() : name;
}

bool isHiddenPath(std::filesystem::path const& path) {
  std::string const name = path.filename().string();
  return !name.empty() && name.front() == '.';
}

std::filesystem::path currentDirectoryFallback() {
  std::error_code ec;
  std::filesystem::path path = std::filesystem::current_path(ec);
  return ec ? std::filesystem::path{"."} : path;
}

std::filesystem::path normalizeDirectory(std::filesystem::path path) {
  if (path.empty()) {
    path = currentDirectoryFallback();
  }

  std::error_code ec;
  if (!std::filesystem::is_directory(path, ec) || ec) {
    if (std::filesystem::is_regular_file(path, ec) && !ec) {
      path = path.parent_path();
    } else {
      path = currentDirectoryFallback();
    }
  }

  std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
  if (!ec && !canonical.empty()) {
    return canonical;
  }

  std::filesystem::path absolute = std::filesystem::absolute(path, ec);
  return ec ? path.lexically_normal() : absolute.lexically_normal();
}

bool sameDirectory(std::filesystem::path const& lhs, std::filesystem::path const& rhs) {
  if (lhs.empty() || rhs.empty()) {
    return false;
  }
  std::error_code ec;
  bool const equivalent = std::filesystem::equivalent(lhs, rhs, ec);
  if (!ec) {
    return equivalent;
  }
  return normalizeDirectory(lhs) == normalizeDirectory(rhs);
}

std::filesystem::path homeDirectory() {
  if (char const* home = std::getenv("HOME"); home && *home) {
    return normalizeDirectory(std::filesystem::path{home});
  }
  return currentDirectoryFallback();
}

bool isInsideOrEqual(std::filesystem::path const& path, std::filesystem::path const& parent) {
  std::error_code ec;
  std::filesystem::path current = std::filesystem::weakly_canonical(path, ec);
  if (ec || current.empty()) {
    current = path.lexically_normal();
  }
  std::filesystem::path root = std::filesystem::weakly_canonical(parent, ec);
  if (ec || root.empty()) {
    root = parent.lexically_normal();
  }
  if (current == root) {
    return true;
  }
  auto currentIt = current.begin();
  auto rootIt = root.begin();
  for (; rootIt != root.end(); ++rootIt, ++currentIt) {
    if (currentIt == current.end() || *currentIt != *rootIt) {
      return false;
    }
  }
  return true;
}

std::vector<FileDialogCrumb> breadcrumbCrumbs(std::filesystem::path const& path) {
  std::vector<FileDialogCrumb> crumbs;
  std::error_code ec;
  std::filesystem::path current = std::filesystem::weakly_canonical(path, ec);
  if (ec || current.empty()) {
    current = path.lexically_normal();
  }

  std::filesystem::path const home = homeDirectory();
  if (isInsideOrEqual(current, home)) {
    crumbs.push_back(FileDialogCrumb{.path = home, .label = "Home"});
    if (current == home) {
      return crumbs;
    }
    std::filesystem::path accumulated = home;
    std::filesystem::path relative = std::filesystem::relative(current, home, ec);
    if (ec) {
      relative = current.lexically_relative(home);
    }
    for (std::filesystem::path const& part : relative) {
      if (part.empty() || part == ".") {
        continue;
      }
      accumulated /= part;
      crumbs.push_back(FileDialogCrumb{.path = accumulated, .label = part.string()});
    }
    return crumbs;
  }

  std::filesystem::path accumulated = current.root_path();
  crumbs.push_back(FileDialogCrumb{
      .path = accumulated.empty() ? std::filesystem::path{"/"} : accumulated,
      .label = accumulated.empty() ? "/" : accumulated.string(),
  });
  if (current == accumulated) {
    return crumbs;
  }
  for (std::filesystem::path const& part : current.relative_path()) {
    if (part.empty() || part == ".") {
      continue;
    }
    accumulated /= part;
    crumbs.push_back(FileDialogCrumb{.path = accumulated, .label = part.string()});
  }
  return crumbs;
}

void addPlaceIfDirectory(std::vector<FileDialogPlace>& places,
                         std::string label,
                         std::filesystem::path path,
                         IconName icon) {
  std::error_code ec;
  if (path.empty() || !std::filesystem::is_directory(path, ec) || ec) {
    return;
  }
  path = normalizeDirectory(std::move(path));
  auto const exists = std::find_if(places.begin(), places.end(), [&](FileDialogPlace const& place) {
    return sameDirectory(place.path, path);
  });
  if (exists == places.end()) {
    places.push_back(FileDialogPlace{.path = std::move(path), .label = std::move(label), .icon = icon});
  }
}

std::vector<FileDialogPlace> commonPlaces() {
  std::vector<FileDialogPlace> places;
  std::filesystem::path const home = homeDirectory();
  addPlaceIfDirectory(places, "Home", home, IconName::Home);
  addPlaceIfDirectory(places, "Desktop", home / "Desktop", IconName::DesktopMac);
  addPlaceIfDirectory(places, "Documents", home / "Documents", IconName::Article);
  addPlaceIfDirectory(places, "Downloads", home / "Downloads", IconName::FolderSpecial);
  addPlaceIfDirectory(places, "Computer", std::filesystem::path{"/"}, IconName::Computer);
  return places;
}

std::string formatByteSize(std::uintmax_t bytes) {
  constexpr double kUnits = 1024.0;
  double value = static_cast<double>(bytes);
  char const* suffix = " B";
  if (value >= kUnits) {
    value /= kUnits;
    suffix = " KB";
  }
  if (value >= kUnits) {
    value /= kUnits;
    suffix = " MB";
  }
  if (value >= kUnits) {
    value /= kUnits;
    suffix = " GB";
  }
  if (std::string{suffix} == " B") {
    return std::to_string(bytes) + suffix;
  }
  int const rounded = static_cast<int>(std::round(value * 10.0));
  return std::to_string(rounded / 10) + "." + std::to_string(rounded % 10) + suffix;
}

bool caseInsensitiveLess(FileDialogEntry const& lhs, FileDialogEntry const& rhs) {
  if (lhs.directory != rhs.directory) {
    return lhs.directory && !rhs.directory;
  }

  std::string left = lhs.name;
  std::string right = rhs.name;
  std::transform(left.begin(), left.end(), left.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  std::transform(right.begin(), right.end(), right.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  if (left == right) {
    return lhs.name < rhs.name;
  }
  return left < right;
}

DirectoryListing listDirectory(std::filesystem::path directory, bool includeHidden) {
  DirectoryListing listing;
  listing.directory = normalizeDirectory(std::move(directory));

  std::error_code ec;
  if (!std::filesystem::is_directory(listing.directory, ec) || ec) {
    listing.status = "Folder is not available.";
    return listing;
  }

  std::vector<FileDialogEntry> children;
  std::filesystem::directory_options const options =
      std::filesystem::directory_options::skip_permission_denied;
  std::filesystem::directory_iterator it(listing.directory, options, ec);
  std::filesystem::directory_iterator const end;
  for (; !ec && it != end; it.increment(ec)) {
    std::filesystem::directory_entry const entry = *it;
    if (!includeHidden && isHiddenPath(entry.path())) {
      continue;
    }

    std::error_code typeEc;
    bool const directoryEntry = entry.is_directory(typeEc) && !typeEc;
    typeEc.clear();
    bool const fileEntry = entry.is_regular_file(typeEc) && !typeEc;
    if (!directoryEntry && !fileEntry) {
      continue;
    }

    std::uintmax_t fileSize = 0;
    if (fileEntry) {
      std::error_code sizeEc;
      fileSize = entry.file_size(sizeEc);
      if (sizeEc) {
        fileSize = 0;
      }
    }

    children.push_back(FileDialogEntry{
        .path = entry.path(),
        .name = displayName(entry.path()),
        .directory = directoryEntry,
        .size = fileSize,
    });
  }

  if (ec) {
    listing.status = "Some items could not be read: " + ec.message();
  }

  std::sort(children.begin(), children.end(), caseInsensitiveLess);
  listing.entries = std::move(children);
  return listing;
}

std::filesystem::path pathFromDialogInput(std::filesystem::path const& directory,
                                          std::string const& name) {
  std::filesystem::path path{name};
  if (path.is_absolute()) {
    return path.lexically_normal();
  }
  return (directory / path).lexically_normal();
}

std::optional<std::size_t> entryIndex(std::vector<FileDialogEntry> const& entries,
                                      std::filesystem::path const& path) {
  if (path.empty()) {
    return std::nullopt;
  }
  for (std::size_t index = 0; index < entries.size(); ++index) {
    if (entries[index].path == path) {
      return index;
    }
  }
  return std::nullopt;
}

std::optional<FileDialogEntry> selectedEntry(std::vector<FileDialogEntry> const& entries,
                                             std::filesystem::path const& path) {
  if (auto const index = entryIndex(entries, path)) {
    return entries[*index];
  }
  return std::nullopt;
}

Font dialogFont(float size, float weight) {
  Font font;
  font.size = size;
  font.weight = weight;
  return font;
}

struct FileDialogNavSegmentButton : ViewModifiers<FileDialogNavSegmentButton> {
  IconName icon = IconName::ChevronLeft;
  std::string tooltip;
  Reactive::Bindable<bool> enabled{true};
  CornerRadius radius{};
  std::function<void()> onTap;

  Element body() const {
    auto theme = useEnvironment<ThemeKey>();
    if (!tooltip.empty()) {
      useTooltip(TooltipConfig{.text = tooltip, .placement = PopoverPlacement::Below});
    }
    Reactive::Signal<bool> hovered = useHover();
    Reactive::Bindable<bool> const enabledBinding = enabled;
    auto handleTap = [onTap = onTap, enabledBinding] {
      if (enabledBinding.evaluate() && onTap) {
        onTap();
      }
    };
    auto handleKey = [handleTap](KeyCode key, Modifiers) {
      if (key == keys::Return || key == keys::Space) {
        handleTap();
      }
    };
    Reactive::Bindable<FillStyle> const fill{[hovered, enabledBinding, theme] {
      if (!enabledBinding.evaluate()) {
        return FillStyle::solid(Color{0.f, 0.f, 0.f, 0.02f});
      }
      return hovered() ? FillStyle::solid(theme().hoveredControlBackgroundColor)
                       : FillStyle::solid(Colors::transparent);
    }};
    Reactive::Bindable<Color> const iconColor{[enabledBinding] {
      return enabledBinding.evaluate() ? Color::primary() : Color::secondary();
    }};

    return ZStack{
        .horizontalAlignment = Alignment::Center,
        .verticalAlignment = Alignment::Center,
        .children = children(
            Rectangle{}.size(30.f, 30.f).fill(fill).cornerRadius(radius),
            Icon{
                .name = icon,
                .size = 18.f,
                .weight = 450.f,
                .color = iconColor,
            })}
        .size(30.f, 30.f)
        .cursor([enabledBinding] {
          return enabledBinding.evaluate() ? Cursor::Hand : Cursor::Inherit;
        })
        .focusable([enabledBinding] {
          return enabledBinding.evaluate();
        })
        .onKeyDown(std::function<void(KeyCode, Modifiers)>{handleKey})
        .onTap(std::function<void()>{handleTap});
  }
};

struct FileDialogNavSegmentedControl : ViewModifiers<FileDialogNavSegmentedControl> {
  Reactive::Bindable<bool> canGoBack{false};
  Reactive::Bindable<bool> canGoForward{false};
  std::function<void()> goBack;
  std::function<void()> goForward;

  Element body() const {
    return HStack{
               .spacing = 0.f,
               .alignment = Alignment::Center,
               .children = children(
                   FileDialogNavSegmentButton{
                       .icon = IconName::ChevronLeft,
                       .tooltip = "Back",
                       .enabled = canGoBack,
                       .radius = CornerRadius{6.f, 0.f, 0.f, 6.f},
                       .onTap = goBack,
                   },
                   Rectangle{}.width(1.f).height(16.f).fill(FillStyle::solid(Color::separator())),
                   FileDialogNavSegmentButton{
                       .icon = IconName::ChevronRight,
                       .tooltip = "Forward",
                       .enabled = canGoForward,
                       .radius = CornerRadius{0.f, 6.f, 6.f, 0.f},
                       .onTap = goForward,
                   })}
        .height(30.f)
        .fill(FillStyle::solid(Color::controlBackground()))
        .stroke(StrokeStyle::solid(Color::separator(), 1.f))
        .cornerRadius(CornerRadius{6.f});
  }
};

struct FileDialogBreadcrumbCrumbView : ViewModifiers<FileDialogBreadcrumbCrumbView> {
  FileDialogCrumb crumb;
  bool showHomeIcon = false;
  Reactive::Bindable<bool> isLast{false};
  std::function<void()> onTap;

  Element body() const {
    auto theme = useEnvironment<ThemeKey>();
    Reactive::Signal<bool> hovered = useHover();
    Reactive::Bindable<bool> const isLastBinding = isLast;
    Reactive::Bindable<FillStyle> const fill{[hovered, isLastBinding, theme] {
      if (!isLastBinding.evaluate() && hovered()) {
        return FillStyle::solid(theme().hoveredControlBackgroundColor);
      }
      return FillStyle::solid(Colors::transparent);
    }};
    Reactive::Bindable<Color> const labelColor{[isLastBinding] {
      return isLastBinding.evaluate() ? Color::primary() : Color::secondary();
    }};
    auto handleTap = [isLastBinding, onTap = onTap] {
      if (!isLastBinding.evaluate() && onTap) {
        onTap();
      }
    };

    std::vector<Element> parts;
    if (showHomeIcon) {
      parts.push_back(Icon{.name = IconName::Home, .size = 14.f, .color = Color::secondary()});
    }
    parts.push_back(Text{
        .text = crumb.label,
        .font = dialogFont(12.f, 430.f),
        .color = labelColor,
        .verticalAlignment = VerticalAlignment::Center,
        .maxLines = 1,
    });

    return HStack{
               .spacing = 5.f,
               .alignment = Alignment::Center,
               .children = std::move(parts)}
        .height(26.f)
        .padding(0.f, showHomeIcon ? 6.f : 5.f, 0.f, 5.f)
        .fill(fill)
        .cornerRadius(CornerRadius{5.f})
        .cursor([isLastBinding] {
          return isLastBinding.evaluate() ? Cursor::Inherit : Cursor::Hand;
        })
        .onTap(std::function<void()>{handleTap});
  }
};

struct FileDialogBreadcrumbSeparatorView {
  Element body() const {
    return Icon{.name = IconName::ChevronRight, .size = 14.f, .color = Color::secondary()};
  }
};

struct FileDialogBreadcrumbBar : ViewModifiers<FileDialogBreadcrumbBar> {
  Reactive::Signal<std::vector<FileDialogCrumb>> crumbs;
  std::function<void(std::filesystem::path)> navigateToPath;

  Element body() const {
    Reactive::Signal<std::vector<FileDialogCrumb>> const crumbsSignal = crumbs;
    auto const navigate = navigateToPath;
    Reactive::Bindable<float> const barWidth{[] {
      Rect const bounds = useBounds();
      return bounds.width > 0.f ? bounds.width : 0.f;
    }};

    Element trail = Element{For<FileDialogCrumb>(
        crumbsSignal,
        [](FileDialogCrumb const& crumb) {
          return crumb.path.string();
        },
        [navigate, crumbsSignal](FileDialogCrumb const& crumb,
                                 Reactive::Signal<std::size_t> const& indexSignal) {
          Reactive::Bindable<bool> const isLast{[indexSignal, crumbsSignal] {
            return indexSignal() + 1 >= crumbsSignal().size();
          }};
          return HStack{
              .spacing = 4.f,
              .alignment = Alignment::Center,
              .children = children(
                  FileDialogBreadcrumbCrumbView{
                      .crumb = crumb,
                      .showHomeIcon = indexSignal() == 0,
                      .isLast = isLast,
                      .onTap = [navigate, target = crumb.path] {
                        if (navigate) {
                          navigate(target);
                        }
                      },
                  },
                  Show(
                      [isLast] {
                        return !isLast.evaluate();
                      },
                      [] {
                        return Element{FileDialogBreadcrumbSeparatorView{}};
                      },
                      [] {
                        return Rectangle{}.size(0.f, 0.f);
                      })),
          };
        },
        4.f,
        Alignment::Center,
        ForLayout::HorizontalStack)};

    return HStack{
               .spacing = 0.f,
               .alignment = Alignment::Center,
               .children = children(
                   Element{ScrollView{
                               .axis = ScrollAxis::Horizontal,
                               .dragScrollEnabled = true,
                               .onTap = {},
                               .children = children(std::move(trail)),
                           }}
                       .flex(1.f, 1.f, 0.f),
                   Spacer{}.flex(1.f, 1.f, 0.f))}
        .width(barWidth)
        .height(30.f)
        .padding(0.f, 8.f, 0.f, 8.f)
        .fill(FillStyle::solid(Color::controlBackground()))
        .stroke(StrokeStyle::solid(Color::separator(), 1.f))
        .cornerRadius(CornerRadius{6.f})
        .clipContent(true);
  }
};

Element fileDialogRow(FileDialogEntry entry,
                      Reactive::Signal<std::string> name,
                      Reactive::Signal<std::filesystem::path> selectedPath,
                      Reactive::Signal<std::filesystem::path> overwriteConfirmPath,
                      Reactive::Signal<std::string> status,
                      Reactive::Bindable<float> rowWidth,
                      std::function<void(std::filesystem::path)> refreshDirectory,
                      std::function<void(FileDialogEntry)> onFileActivate,
                      std::function<void(KeyCode, Modifiers)> onRowKeyDown) {
  auto navigate = [entry = entry, refreshDirectory = std::move(refreshDirectory)] {
    if (refreshDirectory) {
      refreshDirectory(entry.path);
    }
  };
  auto select = [entry = entry, name, selectedPath, overwriteConfirmPath, status,
                 onFileActivate = std::move(onFileActivate)] {
    bool const alreadySelected = selectedPath.peek() == entry.path;
    name.set(entry.name);
    selectedPath.set(entry.path);
    overwriteConfirmPath.set({});
    status.set(std::string{});
    if (alreadySelected && onFileActivate) {
      onFileActivate(entry);
    }
  };
  std::string const detail = entry.directory ? "Folder" : formatByteSize(entry.size);

  return ListRow{
             .content = HStack{
                 .spacing = 8.f,
                 .alignment = Alignment::Center,
                 .children = children(
                     Icon{
                         .name = entry.directory ? IconName::Folder : IconName::TextSnippet,
                         .size = 18.f,
                         .weight = 430.f,
                         .color = entry.directory ? Color{0.11f, 0.36f, 0.74f, 1.f}
                                                  : Color::secondary(),
                     },
                     Text{
                         .text = entry.name,
                         .font = dialogFont(13.f, entry.directory ? 480.f : 420.f),
                         .color = Color::primary(),
                         .verticalAlignment = VerticalAlignment::Center,
                         .maxLines = 1,
                     }.flex(1.f, 1.f, 0.f),
                     Text{
                         .text = detail,
                         .font = dialogFont(12.f, 410.f),
                         .color = Color::secondary(),
                         .horizontalAlignment = HorizontalAlignment::Trailing,
                         .verticalAlignment = VerticalAlignment::Center,
                         .maxLines = 1,
                     }.width(92.f))},
             .selected = [entry, selectedPath] {
               return !selectedPath().empty() && selectedPath() == entry.path;
             },
             .style = ListRow::Style{.paddingH = 9.f, .paddingV = 7.f},
             .onTap = entry.directory ? std::function<void()>{navigate} : std::function<void()>{select},
             .onKeyDown = std::move(onRowKeyDown),
         }
      .width(rowWidth);
}

Element fileDialogPlaceRow(FileDialogPlace place,
                           Reactive::Signal<std::filesystem::path> directory,
                           std::function<void(std::filesystem::path)> refreshDirectory) {
  return ListRow{
      .content = HStack{
          .spacing = 8.f,
          .alignment = Alignment::Center,
          .children = children(
              Icon{
                  .name = place.icon,
                  .size = 17.f,
                  .weight = 430.f,
                  .color = Color::secondary(),
              },
              Text{
                  .text = place.label,
                  .font = dialogFont(13.f, 450.f),
                  .color = Color::primary(),
                  .verticalAlignment = VerticalAlignment::Center,
              }.flex(1.f, 1.f, 0.f))},
      .selected = [place, directory] {
        return sameDirectory(directory(), place.path);
      },
      .style = ListRow::Style{.paddingH = 10.f, .paddingV = 7.f},
      .onTap = [place = std::move(place), refreshDirectory = std::move(refreshDirectory)] {
        if (refreshDirectory) {
          refreshDirectory(place.path);
        }
      },
      .onKeyDown = {},
  };
}

} // namespace

Element FileDialog::body() const {
  DirectoryListing initial = listDirectory(initialDirectory, false);
  auto directory = useState<std::filesystem::path>(initial.directory);
  auto entries = useState<std::vector<FileDialogEntry>>(initial.entries);
  auto places = useState<std::vector<FileDialogPlace>>(commonPlaces());
  auto crumbs = useState<std::vector<FileDialogCrumb>>(breadcrumbCrumbs(initial.directory));
  auto name = useState<std::string>(initialName);
  auto selectedPath = useState<std::filesystem::path>({});
  auto overwriteConfirmPath = useState<std::filesystem::path>({});
  auto scrollOffset = useState<Point>({});
  auto listViewportSize = useState<Size>({});
  auto listGeneration = useState<bool>(false);
  auto backStack = useState<std::vector<std::filesystem::path>>({});
  auto forwardStack = useState<std::vector<std::filesystem::path>>({});
  auto status = useState<std::string>(initial.status);

  auto applyListing = [mode = mode, directory, entries, places, crumbs, name, selectedPath,
                       overwriteConfirmPath, scrollOffset, listGeneration, status](
                          DirectoryListing next) {
    directory.set(next.directory);
    places.set(commonPlaces());
    crumbs.set(breadcrumbCrumbs(next.directory));
    entries.set(std::move(next.entries));
    selectedPath.set({});
    if (mode == FileDialogMode::Open) {
      name.set(std::string{});
    }
    overwriteConfirmPath.set({});
    scrollOffset.set({});
    status.set(next.status);
    listGeneration.set(!listGeneration.peek());
  };

  auto loadDirectory = [directory, backStack, forwardStack, applyListing](
                           std::filesystem::path path, bool recordHistory) {
    DirectoryListing next = listDirectory(std::move(path), false);
    std::filesystem::path const previous = directory.peek();
    bool const changed = !previous.empty() && !sameDirectory(previous, next.directory);
    if (recordHistory && changed) {
      std::vector<std::filesystem::path> back = backStack.peek();
      back.push_back(previous);
      backStack.set(std::move(back));
      forwardStack.set({});
    }
    applyListing(std::move(next));
  };

  auto acceptPath = [mode = mode, overwriteConfirmPath, status, onAccept = onAccept](
                        std::filesystem::path const& target) {
    if (mode == FileDialogMode::Open) {
      std::error_code ec;
      if (!std::filesystem::is_regular_file(target, ec) || ec) {
        status.set("Choose an existing file.");
        return;
      }
    } else {
      std::error_code ec;
      bool const exists = std::filesystem::exists(target, ec) && !ec;
      if (exists) {
        overwriteConfirmPath.set(target);
        status.set("File exists.");
        return;
      }
    }
    if (onAccept && !onAccept(target)) {
      status.set(mode == FileDialogMode::Open ? "Could not open file." : "Could not save file.");
    }
  };

  auto accept = [mode = mode, directory, name, selectedPath, status, acceptPath] {
    if (mode == FileDialogMode::Open) {
      std::filesystem::path const target = selectedPath.peek();
      if (target.empty()) {
        status.set("Choose a file to open.");
        return;
      }
      acceptPath(target);
      return;
    }

    std::string const nameText = name.peek();
    if (nameText.empty()) {
      status.set("Enter a file name.");
      return;
    }
    acceptPath(pathFromDialogInput(directory.peek(), nameText));
  };

  auto cancel = [onCancel = onCancel] {
    if (onCancel) {
      onCancel();
    }
  };
  useWindowAction("dialog.cancel",
                  cancel,
                  ActionDescriptor{
                      .label = "Cancel",
                      .shortcut = Shortcut{keys::Escape, Modifiers::None},
                      .paletteVisible = false,
                      .isEnabled = [] { return true; },
                  });
  auto cancelReplace = [overwriteConfirmPath, status] {
    overwriteConfirmPath.set({});
    status.set(std::string{});
  };
  auto replaceConfirmed = [mode = mode, overwriteConfirmPath, status, onAccept = onAccept] {
    std::filesystem::path const target = overwriteConfirmPath.peek();
    if (!target.empty() && onAccept && !onAccept(target)) {
      status.set(mode == FileDialogMode::Open ? "Could not open file." : "Could not save file.");
    }
  };

  auto navigateToDirectory = [loadDirectory](std::filesystem::path path) {
    loadDirectory(std::move(path), true);
  };
  auto goBack = [directory, backStack, forwardStack, applyListing] {
    std::vector<std::filesystem::path> back = backStack.peek();
    if (back.empty()) {
      return;
    }
    std::filesystem::path const target = back.back();
    back.pop_back();
    std::vector<std::filesystem::path> forward = forwardStack.peek();
    if (!directory.peek().empty()) {
      forward.push_back(directory.peek());
    }
    backStack.set(std::move(back));
    forwardStack.set(std::move(forward));
    applyListing(listDirectory(target, false));
  };
  auto goForward = [directory, backStack, forwardStack, applyListing] {
    std::vector<std::filesystem::path> forward = forwardStack.peek();
    if (forward.empty()) {
      return;
    }
    std::filesystem::path const target = forward.back();
    forward.pop_back();
    std::vector<std::filesystem::path> back = backStack.peek();
    if (!directory.peek().empty()) {
      back.push_back(directory.peek());
    }
    backStack.set(std::move(back));
    forwardStack.set(std::move(forward));
    applyListing(listDirectory(target, false));
  };

  auto activateEntry = [name, navigateToDirectory, acceptPath](FileDialogEntry const& entry) {
    if (entry.directory) {
      navigateToDirectory(entry.path);
      return;
    }
    name.set(entry.name);
    acceptPath(entry.path);
  };
  auto activateFileFromClick = [mode = mode, activateEntry](FileDialogEntry entry) {
    if (mode == FileDialogMode::Open && !entry.directory) {
      activateEntry(entry);
    }
  };

  auto ensureIndexVisible = [scrollOffset](std::size_t index) {
    float const rowTop = static_cast<float>(index) * kFileDialogRowHeight;
    float const rowBottom = rowTop + kFileDialogRowHeight;
    float nextOffset = scrollOffset.peek().y;
    if (rowTop < nextOffset) {
      nextOffset = rowTop;
    } else if (rowBottom > nextOffset + kFileDialogListHeight) {
      nextOffset = rowBottom - kFileDialogListHeight;
    }
    scrollOffset.set(Point{0.f, std::max(0.f, nextOffset)});
  };

  auto selectIndex = [entries, name, selectedPath, overwriteConfirmPath, status,
                      ensureIndexVisible](std::size_t index) {
    std::vector<FileDialogEntry> const currentEntries = entries.peek();
    if (index >= currentEntries.size()) {
      return;
    }
    FileDialogEntry const entry = currentEntries[index];
    selectedPath.set(entry.path);
    overwriteConfirmPath.set({});
    if (!entry.directory) {
      name.set(entry.name);
    }
    ensureIndexVisible(index);
    status.set(std::string{});
  };

  auto moveSelection = [entries, selectedPath, selectIndex](
                           int delta, std::optional<std::size_t> focusedIndex = std::nullopt) {
    std::vector<FileDialogEntry> const currentEntries = entries.peek();
    if (currentEntries.empty()) {
      return;
    }
    std::size_t index = 0;
    if (auto const currentIndex = entryIndex(currentEntries, selectedPath.peek())) {
      int const next = static_cast<int>(*currentIndex) + delta;
      index = static_cast<std::size_t>(
          std::clamp(next, 0, static_cast<int>(currentEntries.size() - 1)));
    } else if (focusedIndex && *focusedIndex < currentEntries.size()) {
      int const next = static_cast<int>(*focusedIndex) + delta;
      index = static_cast<std::size_t>(
          std::clamp(next, 0, static_cast<int>(currentEntries.size() - 1)));
    } else if (delta < 0) {
      index = currentEntries.size() - 1;
    }
    selectIndex(index);
  };

  auto handleListKeyAtIndex = [entries, selectedPath, selectIndex, moveSelection, accept, cancel,
                               activateEntry](std::optional<std::size_t> focusedIndex,
                                               KeyCode key, Modifiers) {
    std::vector<FileDialogEntry> const currentEntries = entries.peek();
    if (key == keys::Escape) {
      cancel();
      return;
    }
    if (key == keys::DownArrow) {
      moveSelection(1, focusedIndex);
      return;
    }
    if (key == keys::UpArrow) {
      moveSelection(-1, focusedIndex);
      return;
    }
    if (key == keys::Home && !currentEntries.empty()) {
      selectIndex(0);
      return;
    }
    if (key == keys::End && !currentEntries.empty()) {
      selectIndex(currentEntries.size() - 1);
      return;
    }
    if (key == keys::Return) {
      if (auto const entry = selectedEntry(currentEntries, selectedPath.peek())) {
        activateEntry(*entry);
      } else {
        accept();
      }
    }
  };
  auto handleListKey = [handleListKeyAtIndex](KeyCode key, Modifiers modifiers) {
    handleListKeyAtIndex(std::nullopt, key, modifiers);
  };

  std::string const action = mode == FileDialogMode::Open ? "Open" : "Save";
  Reactive::Bindable<bool> const canGoBack{[backStack] {
    return !backStack().empty();
  }};
  Reactive::Bindable<bool> const canGoForward{[forwardStack] {
    return !forwardStack().empty();
  }};
  Reactive::Bindable<bool> const actionDisabled{[mode = mode, selectedPath, name] {
    return mode == FileDialogMode::Open ? selectedPath().empty() : name().empty();
  }};
  Reactive::Bindable<float> const fileListWidth{[listViewportSize] {
    return std::max(1.f, listViewportSize().width - 2.f);
  }};
  auto fileListScrollView = [scrollOffset,
                             listViewportSize,
                             entries,
                             name,
                             selectedPath,
                             overwriteConfirmPath,
                             status,
                             navigateToDirectory,
                             activateFileFromClick,
                             handleListKeyAtIndex,
                             fileListWidth] {
    return ScrollView{
        .axis = ScrollAxis::Vertical,
        .scrollOffset = scrollOffset,
        .viewportSize = listViewportSize,
        .onTap = {},
        .children = children(
            VStack{
                .spacing = 0.f,
                .alignment = Alignment::Stretch,
                .children = children(For<FileDialogEntry>(
                    entries,
                    [](FileDialogEntry const& entry) {
                      return entry.path.string() + (entry.directory ? "/" : "");
                    },
                    [name, selectedPath, overwriteConfirmPath, status,
                     navigateToDirectory, activateFileFromClick,
                     handleListKeyAtIndex, entries, fileListWidth](
                        FileDialogEntry const& entry) {
                      auto rowKey = [handleListKeyAtIndex, entries, path = entry.path](
                                        KeyCode key, Modifiers modifiers) {
                        handleListKeyAtIndex(entryIndex(entries.peek(), path), key, modifiers);
                      };
                      return fileDialogRow(entry,
                                           name,
                                           selectedPath,
                                           overwriteConfirmPath,
                                           status,
                                           fileListWidth,
                                           navigateToDirectory,
                                           activateFileFromClick,
                                           std::function<void(KeyCode, Modifiers)>{
                                               std::move(rowKey)});
                    },
                    0.f,
                    Alignment::Stretch)),
            }.width(fileListWidth)
             .padding(0.f, 1.f, 0.f, 1.f)),
    };
  };

  return VStack{
      .spacing = 0.f,
      .alignment = Alignment::Stretch,
      .children = children(
          HStack{
              .spacing = 8.f,
              .alignment = Alignment::Center,
              .children = children(
                  FileDialogNavSegmentedControl{
                      .canGoBack = canGoBack,
                      .canGoForward = canGoForward,
                      .goBack = goBack,
                      .goForward = goForward,
                  },
                  FileDialogBreadcrumbBar{
                      .crumbs = crumbs,
                      .navigateToPath = navigateToDirectory,
                  }.flex(1.f, 1.f, 0.f))}
              .height(52.f)
              .padding(10.f, 12.f, 10.f, 12.f)
              .fill(FillStyle::solid(Color::windowBackground()))
              .stroke(StrokeStyle::solid(Color::separator(), 1.f)),
          HStack{
              .spacing = 0.f,
              .alignment = Alignment::Stretch,
              .children = children(
                  VStack{
                      .spacing = 0.f,
                      .alignment = Alignment::Stretch,
                      .children = children(
                          VStack{
                              .spacing = 2.f,
                              .alignment = Alignment::Stretch,
                              .children = children(For<FileDialogPlace>(
                                  places,
                                  [](FileDialogPlace const& place) {
                                    return place.label + ":" + place.path.string();
                                  },
                                  [directory, navigateToDirectory](FileDialogPlace const& place) {
                                    return fileDialogPlaceRow(place, directory, navigateToDirectory);
                                  },
                                  0.f,
                                  Alignment::Stretch)),
                          }.flex(1.f, 1.f, 0.f))}.width(170.f)
                                                   .padding(12.f, 10.f, 12.f, 10.f)
                                                   .fill(FillStyle::solid(Color::windowBackground())),
                  Rectangle{}.width(1.f).fill(FillStyle::solid(Color::separator())),
                  VStack{
                      .spacing = 0.f,
                      .alignment = Alignment::Stretch,
                      .children = children(
                          Rectangle{}
                              .fill(FillStyle::solid(Color::controlBackground()))
                              .stroke(StrokeStyle::solid(Color::separator(), 1.f))
                              .cornerRadius(CornerRadius{6.f})
                              .overlay(Show(
                                  [listGeneration] {
                                    return listGeneration();
                                  },
                                  fileListScrollView,
                                  fileListScrollView))
                              .focusable(true)
                              .onKeyDown(std::function<void(KeyCode, Modifiers)>{handleListKey})
                              .flex(1.f, 1.f, 0.f),
                          Show(
                              [status] {
                                return !status().empty();
                              },
                              [status] {
                                return Text{
                                    .text = status,
                                    .font = dialogFont(12.f, 430.f),
                                    .color = Color::secondary(),
                                    .verticalAlignment = VerticalAlignment::Center,
                                }.height(20.f).padding(6.f, 0.f, 0.f, 0.f);
                              }),
                          Show(
                              [overwriteConfirmPath] {
                                return !overwriteConfirmPath().empty();
                              },
                              [overwriteConfirmPath, cancelReplace, replaceConfirmed] {
                                return HStack{
                                      .spacing = 8.f,
                                      .alignment = Alignment::Center,
                                      .children = children(
                                          Text{
                                              .text = [overwriteConfirmPath] {
                                                return "Replace \"" +
                                                       overwriteConfirmPath().filename().string() +
                                                       "\"?";
                                              },
                                              .font = dialogFont(12.f, 500.f),
                                              .color = Color::primary(),
                                              .verticalAlignment = VerticalAlignment::Center,
                                          }.flex(1.f, 1.f, 0.f),
                                          Button{
                                              .label = "Cancel",
                                              .variant = ButtonVariant::Secondary,
                                              .onTap = cancelReplace,
                                          },
                                          Button{
                                              .label = "Replace",
                                              .variant = ButtonVariant::Destructive,
                                              .onTap = replaceConfirmed,
                                          })}
                                      .padding(8.f)
                                      .fill(FillStyle::solid(Color::controlBackground()))
                                      .stroke(StrokeStyle::solid(Color::separator(), 1.f))
                                      .cornerRadius(CornerRadius{6.f})
                                      .padding(6.f, 0.f, 0.f, 0.f);
                              }))}.padding(12.f, 12.f, 12.f, 12.f)
                                  .flex(1.f, 1.f, 0.f))}
              .flex(1.f, 1.f, 0.f),
          HStack{
              .spacing = 8.f,
              .alignment = Alignment::Center,
              .children = children(
                  Spacer{}.flex(1.f, 1.f, 0.f),
                  Button{
                      .label = "Cancel",
                      .variant = ButtonVariant::Secondary,
                      .onTap = cancel,
                  },
                  Button{
                      .label = action,
                      .variant = ButtonVariant::Primary,
                      .disabled = actionDisabled,
                      .onTap = accept,
                  })}
              .height(58.f)
              .padding(10.f, 12.f, 10.f, 12.f)
              .fill(FillStyle::solid(Color::windowBackground()))
              .stroke(StrokeStyle::solid(Color::separator(), 1.f)))}
      .fill(FillStyle::solid(Color::windowBackground()));
}

} // namespace lambda
