#include <Lambda/UI/Views/FileDialog.hpp>

#include <Lambda/Graphics/Styles.hpp>
#include <Lambda/UI/IconName.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/Views/Button.hpp>
#include <Lambda/UI/Views/Checkbox.hpp>
#include <Lambda/UI/Views/For.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Icon.hpp>
#include <Lambda/UI/Views/ListView.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/ScrollView.hpp>
#include <Lambda/UI/Views/Show.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/TextInput.hpp>
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
  bool parent = false;
  std::uintmax_t size = 0;

  bool operator==(FileDialogEntry const&) const = default;
};

struct FileDialogPlace {
  std::filesystem::path path;
  std::string label;
  IconName icon = IconName::Folder;

  bool operator==(FileDialogPlace const&) const = default;
};

struct DirectoryListing {
  std::filesystem::path directory;
  std::vector<FileDialogEntry> entries;
  std::string status;
  std::string summary;
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

std::vector<FileDialogPlace> commonPlaces(std::filesystem::path const& currentDirectory) {
  std::vector<FileDialogPlace> places;
  std::filesystem::path const home = homeDirectory();
  addPlaceIfDirectory(places, "Home", home, IconName::Home);
  addPlaceIfDirectory(places, "Desktop", home / "Desktop", IconName::DesktopMac);
  addPlaceIfDirectory(places, "Documents", home / "Documents", IconName::Article);
  addPlaceIfDirectory(places, "Downloads", home / "Downloads", IconName::FolderSpecial);
  addPlaceIfDirectory(places, "Computer", std::filesystem::path{"/"}, IconName::Computer);
  addPlaceIfDirectory(places, "Current Folder", currentDirectory, IconName::FolderOpen);
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

std::string listingSummary(std::size_t folders, std::size_t files, std::size_t hiddenSkipped) {
  std::string summary = std::to_string(folders) + " folders, " + std::to_string(files) + " files";
  if (hiddenSkipped > 0) {
    summary += ", " + std::to_string(hiddenSkipped) + " hidden";
  }
  return summary;
}

std::filesystem::path newFolderCandidate(std::filesystem::path const& directory) {
  std::filesystem::path candidate = directory / "New Folder";
  std::error_code ec;
  if (!std::filesystem::exists(candidate, ec) || ec) {
    return candidate;
  }
  for (int index = 2; index < 1000; ++index) {
    candidate = directory / ("New Folder " + std::to_string(index));
    ec.clear();
    if (!std::filesystem::exists(candidate, ec) || ec) {
      return candidate;
    }
  }
  return directory / "New Folder";
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
    listing.summary = listingSummary(0, 0, 0);
    return listing;
  }

  std::vector<FileDialogEntry> children;
  std::size_t folderCount = 0;
  std::size_t fileCount = 0;
  std::size_t hiddenSkipped = 0;
  std::filesystem::directory_options const options =
      std::filesystem::directory_options::skip_permission_denied;
  std::filesystem::directory_iterator it(listing.directory, options, ec);
  std::filesystem::directory_iterator const end;
  for (; !ec && it != end; it.increment(ec)) {
    std::filesystem::directory_entry const entry = *it;
    if (!includeHidden && isHiddenPath(entry.path())) {
      ++hiddenSkipped;
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
    if (directoryEntry) {
      ++folderCount;
    } else {
      ++fileCount;
    }

    children.push_back(FileDialogEntry{
        .path = entry.path(),
        .name = displayName(entry.path()),
        .directory = directoryEntry,
        .parent = false,
        .size = fileSize,
    });
  }

  if (ec) {
    listing.status = "Some items could not be read: " + ec.message();
  }

  std::sort(children.begin(), children.end(), caseInsensitiveLess);

  std::filesystem::path const parent = listing.directory.parent_path();
  if (!parent.empty() && parent != listing.directory) {
    listing.entries.push_back(FileDialogEntry{
        .path = parent,
        .name = "..",
        .directory = true,
        .parent = true,
    });
  }
  listing.entries.insert(listing.entries.end(), children.begin(), children.end());
  listing.summary = listingSummary(folderCount, fileCount, hiddenSkipped);
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

TextInput::Style compactInputStyle() {
  TextInput::Style style;
  style.font = Font{.size = 13.f, .weight = 430.f};
  style.paddingH = 8.f;
  style.paddingV = 6.f;
  style.cornerRadius = 6.f;
  return style;
}

struct FileDialogToolButton : ViewModifiers<FileDialogToolButton> {
  IconName icon = IconName::Folder;
  std::string tooltip;
  std::function<void()> onTap;
  bool disabled = false;

  Element body() const {
    auto theme = useEnvironment<ThemeKey>();
    if (!tooltip.empty()) {
      useTooltip(TooltipConfig{.text = tooltip, .placement = PopoverPlacement::Below});
    }
    Reactive::Signal<bool> hovered = useHover();
    auto handleTap = [onTap = onTap, disabled = disabled] {
      if (!disabled && onTap) {
        onTap();
      }
    };
    auto handleKey = [handleTap](KeyCode key, Modifiers) {
      if (key == keys::Return || key == keys::Space) {
        handleTap();
      }
    };

    return ZStack{
        .horizontalAlignment = Alignment::Center,
        .verticalAlignment = Alignment::Center,
        .children = children(
            Rectangle{}
                .size(30.f, 30.f)
                .fill([hovered, disabled = disabled, theme] {
                  if (disabled) {
                    return FillStyle::solid(Color{0.f, 0.f, 0.f, 0.02f});
                  }
                  return hovered() ? FillStyle::solid(theme().hoveredControlBackgroundColor)
                                   : FillStyle::solid(Color::controlBackground());
                })
                .stroke(StrokeStyle::solid(Color::separator(), 1.f))
                .cornerRadius(CornerRadius{6.f}),
            Icon{
                .name = icon,
                .size = 18.f,
                .weight = 450.f,
                .color = disabled ? Color::secondary() : Color::primary(),
            })}
        .size(30.f, 30.f)
        .cursor(disabled ? Cursor::Inherit : Cursor::Hand)
        .focusable(!disabled)
        .onKeyDown(std::function<void(KeyCode, Modifiers)>{handleKey})
        .onTap(std::function<void()>{handleTap});
  }
};

Element fileDialogRow(FileDialogEntry entry,
                      Reactive::Signal<std::string> name,
                      Reactive::Signal<std::filesystem::path> selectedPath,
                      Reactive::Signal<std::filesystem::path> overwriteConfirmPath,
                      Reactive::Signal<std::string> status,
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
  std::string const detail = entry.parent ? "Parent folder"
                                          : entry.directory ? "Folder" : formatByteSize(entry.size);

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
                  .font = Font{.size = 13.f, .weight = entry.directory ? 480.f : 420.f},
                  .color = entry.parent ? Color::secondary() : Color::primary(),
                  .verticalAlignment = VerticalAlignment::Center,
              }.flex(1.f, 1.f, 0.f),
              Text{
                  .text = detail,
                  .font = Font{.size = 12.f, .weight = 410.f},
                  .color = Color::secondary(),
                  .horizontalAlignment = HorizontalAlignment::Trailing,
                  .verticalAlignment = VerticalAlignment::Center,
              }.width(92.f))},
      .selected = [entry, selectedPath] {
        return !selectedPath().empty() && selectedPath() == entry.path;
      },
      .style = ListRow::Style{.paddingH = 9.f, .paddingV = 7.f},
      .onTap = entry.directory ? std::function<void()>{navigate} : std::function<void()>{select},
      .onKeyDown = std::move(onRowKeyDown),
  };
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
                  .font = Font{.size = 13.f, .weight = 450.f},
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
  };
}

} // namespace

Element FileDialog::body() const {
  DirectoryListing initial = listDirectory(initialDirectory, false);
  auto directory = useState<std::filesystem::path>(initial.directory);
  auto directoryText = useState<std::string>(initial.directory.string());
  auto entries = useState<std::vector<FileDialogEntry>>(initial.entries);
  auto places = useState<std::vector<FileDialogPlace>>(commonPlaces(initial.directory));
  auto name = useState<std::string>(initialName);
  auto selectedPath = useState<std::filesystem::path>({});
  auto overwriteConfirmPath = useState<std::filesystem::path>({});
  auto scrollOffset = useState<Point>({});
  auto showHidden = useState<bool>(false);
  auto status = useState<std::string>(initial.status);
  auto summary = useState<std::string>(initial.summary);

  auto loadDirectory = [directory, directoryText, entries, places, selectedPath,
                        overwriteConfirmPath, scrollOffset, status, summary](
                           std::filesystem::path path, bool includeHidden) {
    DirectoryListing next = listDirectory(std::move(path), includeHidden);
    directory.set(next.directory);
    directoryText.set(next.directory.string());
    places.set(commonPlaces(next.directory));
    entries.set(std::move(next.entries));
    selectedPath.set({});
    overwriteConfirmPath.set({});
    scrollOffset.set({});
    status.set(next.status);
    summary.set(next.summary);
  };

  auto refreshDirectory = [loadDirectory, showHidden](std::filesystem::path path) {
    loadDirectory(std::move(path), showHidden.peek());
  };

  auto nameChanged = [selectedPath, overwriteConfirmPath, status](std::string const&) {
    selectedPath.set({});
    overwriteConfirmPath.set({});
    status.set(std::string{});
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

  auto accept = [mode = mode, directory, name, status, acceptPath] {
    std::string const nameText = name.peek();
    if (nameText.empty()) {
      status.set(mode == FileDialogMode::Open ? "Choose a file to open." : "Enter a file name.");
      return;
    }
    acceptPath(pathFromDialogInput(directory.peek(), nameText));
  };

  auto cancel = [onCancel = onCancel] {
    if (onCancel) {
      onCancel();
    }
  };
  auto toggleHidden = [directory, showHidden, loadDirectory](bool visible) {
    showHidden.set(visible);
    loadDirectory(directory.peek(), visible);
  };
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

  auto goToDirectory = [directoryText, refreshDirectory] {
    refreshDirectory(std::filesystem::path{directoryText.peek()});
  };
  auto goUp = [directory, refreshDirectory] {
    std::filesystem::path const current = directory.peek();
    std::filesystem::path const parent = current.parent_path();
    if (!parent.empty() && parent != current) {
      refreshDirectory(parent);
    }
  };
  auto refreshCurrent = [directory, refreshDirectory] {
    refreshDirectory(directory.peek());
  };
  auto createFolder = [directory, refreshDirectory, name, selectedPath,
                       overwriteConfirmPath, status] {
    std::filesystem::path const target = newFolderCandidate(directory.peek());
    std::error_code ec;
    std::filesystem::create_directory(target, ec);
    if (ec) {
      status.set("Could not create folder: " + ec.message());
      return;
    }
    refreshDirectory(directory.peek());
    name.set(target.filename().string());
    selectedPath.set(target);
    overwriteConfirmPath.set({});
    status.set("Created \"" + target.filename().string() + "\".");
  };

  auto activateEntry = [name, refreshDirectory, acceptPath](FileDialogEntry const& entry) {
    if (entry.directory) {
      refreshDirectory(entry.path);
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

  std::string const title = mode == FileDialogMode::Open ? "Open File" : "Save File";
  std::string const action = mode == FileDialogMode::Open ? "Open" : "Save";
  std::filesystem::path const parent = directory.peek().parent_path();
  bool const canGoUp = !parent.empty() && parent != directory.peek();

  return VStack{
      .spacing = 0.f,
      .alignment = Alignment::Stretch,
      .children = children(
          HStack{
              .spacing = 8.f,
              .alignment = Alignment::Center,
              .children = children(
                  FileDialogToolButton{
                      .icon = IconName::ArrowUpward,
                      .tooltip = "Parent Folder",
                      .onTap = goUp,
                      .disabled = !canGoUp,
                  },
                  FileDialogToolButton{
                      .icon = IconName::Refresh,
                      .tooltip = "Refresh",
                      .onTap = refreshCurrent,
                  },
                  FileDialogToolButton{
                      .icon = IconName::CreateNewFolder,
                      .tooltip = "New Folder",
                      .onTap = createFolder,
                  },
                  TextInput{
                      .value = directoryText,
                      .placeholder = "Folder",
                      .style = compactInputStyle(),
                      .onSubmit = [goToDirectory](std::string const&) { goToDirectory(); },
                      .onEscape = [cancel](std::string const&) { cancel(); },
                  }.flex(1.f, 1.f, 0.f),
                  Button{
                      .label = "Go",
                      .variant = ButtonVariant::Secondary,
                      .onTap = goToDirectory,
                  },
                  HStack{
                      .spacing = 6.f,
                      .alignment = Alignment::Center,
                      .children = children(
                          Checkbox{
                              .value = showHidden,
                              .onChange = toggleHidden,
                          },
                          Text{
                              .text = "Show hidden",
                              .font = Font{.size = 12.f, .weight = 430.f},
                              .color = Color::secondary(),
                              .verticalAlignment = VerticalAlignment::Center,
                          })})}
              .height(52.f)
              .padding(10.f, 12.f, 10.f, 12.f)
              .fill(FillStyle::solid(Color::windowBackground()))
              .stroke(StrokeStyle::solid(Color::separator(), 1.f)),
          HStack{
              .spacing = 0.f,
              .alignment = Alignment::Stretch,
              .children = children(
                  VStack{
                      .spacing = 8.f,
                      .alignment = Alignment::Stretch,
                      .children = children(
                          Text{
                              .text = "Locations",
                              .font = Font{.size = 12.f, .weight = 620.f},
                              .color = Color::secondary(),
                              .verticalAlignment = VerticalAlignment::Center,
                          }.height(20.f),
                          VStack{
                              .spacing = 2.f,
                              .alignment = Alignment::Stretch,
                              .children = children(For<FileDialogPlace>(
                                  places,
                                  [](FileDialogPlace const& place) {
                                    return place.label + ":" + place.path.string();
                                  },
                                  [directory, refreshDirectory](FileDialogPlace const& place) {
                                    return fileDialogPlaceRow(place, directory, refreshDirectory);
                                  },
                                  0.f,
                                  Alignment::Stretch)),
                          }.flex(1.f, 1.f, 0.f))}.width(170.f)
                                                   .padding(12.f, 10.f, 12.f, 10.f)
                                                   .fill(FillStyle::solid(Color::windowBackground())),
                  Rectangle{}.width(1.f).fill(FillStyle::solid(Color::separator())),
                  VStack{
                      .spacing = 10.f,
                      .alignment = Alignment::Stretch,
                      .children = children(
                          Rectangle{}
                              .fill(FillStyle::solid(Color::controlBackground()))
                              .stroke(StrokeStyle::solid(Color::separator(), 1.f))
                              .cornerRadius(CornerRadius{6.f})
                              .overlay(ScrollView{
                                  .axis = ScrollAxis::Vertical,
                                  .scrollOffset = scrollOffset,
                                  .children = children(For<FileDialogEntry>(
                                      entries,
                                      [](FileDialogEntry const& entry) {
                                        return entry.path.string() + (entry.directory ? "/" : "");
                                      },
                                      [name, selectedPath, overwriteConfirmPath, status,
                                       refreshDirectory, activateFileFromClick,
                                       handleListKeyAtIndex, entries](
                                          FileDialogEntry const& entry) {
                                        auto rowKey = [handleListKeyAtIndex, entries,
                                                       path = entry.path](
                                                          KeyCode key, Modifiers modifiers) {
                                          handleListKeyAtIndex(entryIndex(entries.peek(), path),
                                                               key, modifiers);
                                        };
                                        return fileDialogRow(entry,
                                                             name,
                                                             selectedPath,
                                                             overwriteConfirmPath,
                                                             status,
                                                             refreshDirectory,
                                                             activateFileFromClick,
                                                             std::function<void(KeyCode, Modifiers)>{
                                                                 std::move(rowKey)});
                                      },
                                      0.f,
                                      Alignment::Stretch)),
                              })
                              .focusable(true)
                              .onKeyDown(std::function<void(KeyCode, Modifiers)>{handleListKey})
                              .flex(1.f, 1.f, 0.f),
                          HStack{
                              .spacing = 8.f,
                              .alignment = Alignment::Center,
                              .children = children(
                                  Text{
                                      .text = "Name",
                                      .font = Font{.size = 13.f, .weight = 500.f},
                                      .color = Color::secondary(),
                                      .verticalAlignment = VerticalAlignment::Center,
                                  }.width(52.f),
                                  TextInput{
                                      .value = name,
                                      .placeholder =
                                          mode == FileDialogMode::Open ? "Select a file" : "File name",
                                      .style = compactInputStyle(),
                                      .onChange = nameChanged,
                                      .onSubmit = [accept](std::string const&) { accept(); },
                                      .onEscape = [cancel](std::string const&) { cancel(); },
                                  }.flex(1.f, 1.f, 0.f))},
                          HStack{
                              .spacing = 8.f,
                              .alignment = Alignment::Center,
                              .children = children(
                                  Text{
                                      .text = [status, summary] {
                                        return status().empty() ? summary() : status();
                                      },
                                      .font = Font{.size = 12.f, .weight = 430.f},
                                      .color = Color::secondary(),
                                      .verticalAlignment = VerticalAlignment::Center,
                                  }.flex(1.f, 1.f, 0.f))},
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
                                              .font = Font{.size = 12.f, .weight = 500.f},
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
                                      .cornerRadius(CornerRadius{6.f});
                              }))}.padding(12.f)
                                  .flex(1.f, 1.f, 0.f))}
              .flex(1.f, 1.f, 0.f),
          HStack{
              .spacing = 8.f,
              .alignment = Alignment::Center,
              .children = children(
                  Text{
                      .text = title,
                      .font = Font{.size = 12.f, .weight = 520.f},
                      .color = Color::secondary(),
                      .verticalAlignment = VerticalAlignment::Center,
                  }.flex(1.f, 1.f, 0.f),
                  Button{
                      .label = "Cancel",
                      .variant = ButtonVariant::Secondary,
                      .onTap = cancel,
                  },
                  Button{
                      .label = action,
                      .variant = ButtonVariant::Primary,
                      .onTap = accept,
                  })}
              .height(58.f)
              .padding(10.f, 12.f, 10.f, 12.f)
              .fill(FillStyle::solid(Color::windowBackground()))
              .stroke(StrokeStyle::solid(Color::separator(), 1.f)))}
      .fill(FillStyle::solid(Color::windowBackground()));
}

} // namespace lambda
