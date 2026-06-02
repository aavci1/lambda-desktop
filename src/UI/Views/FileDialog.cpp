#include <Lambda/UI/Views/FileDialog.hpp>

#include <Lambda/Graphics/Styles.hpp>
#include <Lambda/UI/IconName.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/Views/Button.hpp>
#include <Lambda/UI/Views/Dialog.hpp>
#include <Lambda/UI/Views/For.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Icon.hpp>
#include <Lambda/UI/Views/ListView.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/ScrollView.hpp>
#include <Lambda/UI/Views/Spacer.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/TextInput.hpp>
#include <Lambda/UI/Views/VStack.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace lambda {

namespace {

struct FileDialogEntry {
  std::filesystem::path path;
  std::string name;
  bool directory = false;
  bool parent = false;

  bool operator==(FileDialogEntry const&) const = default;
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

DirectoryListing listDirectory(std::filesystem::path directory) {
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
    std::error_code typeEc;
    bool const directoryEntry = entry.is_directory(typeEc) && !typeEc;
    bool const fileEntry = entry.is_regular_file(typeEc) && !typeEc;
    if (!directoryEntry && !fileEntry) {
      continue;
    }

    children.push_back(FileDialogEntry{
        .path = entry.path(),
        .name = displayName(entry.path()),
        .directory = directoryEntry,
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

Element fileDialogRow(FileDialogEntry entry,
                      Reactive::Signal<std::filesystem::path> directory,
                      Reactive::Signal<std::string> directoryText,
                      Reactive::Signal<std::vector<FileDialogEntry>> entries,
                      Reactive::Signal<std::string> name,
                      Reactive::Signal<std::filesystem::path> selectedPath,
                      Reactive::Signal<std::filesystem::path> overwriteConfirmPath,
                      Reactive::Signal<std::string> status,
                      std::function<void(KeyCode, Modifiers)> onRowKeyDown) {
  auto navigate = [entry = entry, directory, directoryText, entries,
                   selectedPath, overwriteConfirmPath, status] {
    DirectoryListing next = listDirectory(entry.path);
    directory.set(next.directory);
    directoryText.set(next.directory.string());
    entries.set(std::move(next.entries));
    selectedPath.set({});
    overwriteConfirmPath.set({});
    status.set(next.status);
  };
  auto select = [entry = entry, name, selectedPath, overwriteConfirmPath, status] {
    name.set(entry.name);
    selectedPath.set(entry.path);
    overwriteConfirmPath.set({});
    status.set(std::string{});
  };

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
              }.flex(1.f, 1.f, 0.f))},
      .selected = [entry, selectedPath] {
        return !selectedPath().empty() && selectedPath() == entry.path;
      },
      .onTap = entry.directory ? std::function<void()>{navigate} : std::function<void()>{select},
      .onKeyDown = std::move(onRowKeyDown),
  };
}

} // namespace

Element FileDialog::body() const {
  DirectoryListing initial = listDirectory(initialDirectory);
  auto directory = useState<std::filesystem::path>(initial.directory);
  auto directoryText = useState<std::string>(initial.directory.string());
  auto entries = useState<std::vector<FileDialogEntry>>(initial.entries);
  auto name = useState<std::string>(initialName);
  auto selectedPath = useState<std::filesystem::path>({});
  auto overwriteConfirmPath = useState<std::filesystem::path>({});
  auto status = useState<std::string>(initial.status);

  auto refreshDirectory = [directory, directoryText, entries, selectedPath,
                           overwriteConfirmPath, status](std::filesystem::path path) {
    DirectoryListing next = listDirectory(std::move(path));
    directory.set(next.directory);
    directoryText.set(next.directory.string());
    entries.set(std::move(next.entries));
    selectedPath.set({});
    overwriteConfirmPath.set({});
    status.set(next.status);
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
      if (exists && overwriteConfirmPath.peek() != target) {
        overwriteConfirmPath.set(target);
        status.set("File exists. Press Save again to replace it.");
        return;
      }
    }
    if (onAccept) {
      onAccept(target);
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

  auto goToDirectory = [directoryText, refreshDirectory] {
    refreshDirectory(std::filesystem::path{directoryText.peek()});
  };

  auto activateEntry = [name, refreshDirectory, acceptPath](FileDialogEntry const& entry) {
    if (entry.directory) {
      refreshDirectory(entry.path);
      return;
    }
    name.set(entry.name);
    acceptPath(entry.path);
  };

  auto selectIndex = [entries, name, selectedPath, overwriteConfirmPath, status](std::size_t index) {
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

  auto handleListKeyAtIndex = [entries, selectedPath, selectIndex, moveSelection, accept,
                               activateEntry](std::optional<std::size_t> focusedIndex,
                                               KeyCode key, Modifiers) {
    std::vector<FileDialogEntry> const currentEntries = entries.peek();
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

  return Dialog{
      .title = title,
      .content = children(
          VStack{
              .spacing = 10.f,
              .alignment = Alignment::Stretch,
              .children = children(
                  HStack{
                      .spacing = 8.f,
                      .alignment = Alignment::Center,
                      .children = children(
                          TextInput{
                              .value = directoryText,
                              .placeholder = "Folder",
                              .style = compactInputStyle(),
                              .onSubmit = [goToDirectory](std::string const&) { goToDirectory(); },
                          }.flex(1.f, 1.f, 0.f),
                          Button{
                              .label = "Go",
                              .variant = ButtonVariant::Secondary,
                              .onTap = goToDirectory,
                          })},
                  Rectangle{}
                      .height(260.f)
                      .fill(FillStyle::solid(Color::controlBackground()))
                      .stroke(StrokeStyle::solid(Color::separator(), 1.f))
                      .cornerRadius(CornerRadius{6.f})
                      .overlay(ScrollView{
                          .axis = ScrollAxis::Vertical,
                          .children = children(For<FileDialogEntry>(
                              entries,
                              [](FileDialogEntry const& entry) {
                                return entry.path.string() + (entry.directory ? "/" : "");
                              },
                              [directory, directoryText, entries, name, selectedPath,
                               overwriteConfirmPath, status, handleListKeyAtIndex](
                                  FileDialogEntry const& entry) {
                                auto rowKey = [handleListKeyAtIndex, entries,
                                               path = entry.path](
                                                  KeyCode key, Modifiers modifiers) {
                                  handleListKeyAtIndex(entryIndex(entries.peek(), path),
                                                       key, modifiers);
                                };
                                return fileDialogRow(entry, directory, directoryText, entries,
                                                     name, selectedPath, overwriteConfirmPath,
                                                     status,
                                                     std::function<void(KeyCode, Modifiers)>{
                                                         std::move(rowKey)});
                              },
                              0.f,
                              Alignment::Stretch)),
                      })
                      .focusable(true)
                      .onKeyDown(std::function<void(KeyCode, Modifiers)>{handleListKey}),
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
                          }.flex(1.f, 1.f, 0.f))},
                  Text{
                      .text = [status] { return status(); },
                      .font = Font{.size = 12.f, .weight = 430.f},
                      .color = Color::secondary(),
                      .verticalAlignment = VerticalAlignment::Center,
                  }.height(18.f))}),
      .footer = children(
          Spacer{},
          Button{
              .label = "Cancel",
              .variant = ButtonVariant::Secondary,
              .onTap = cancel,
          },
          Button{
              .label = action,
              .variant = ButtonVariant::Primary,
              .onTap = accept,
          }),
      .onClose = cancel,
  };
}

} // namespace lambda
