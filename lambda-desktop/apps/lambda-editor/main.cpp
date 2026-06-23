#include "EditorCommands.hpp"
#include "EditorDocument.hpp"
#include "EditorFileWatcher.hpp"

#include <Lambda.hpp>
#include <Lambda/UI/Events.hpp>
#include <Lambda/UI/Shortcut.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/UI.hpp>
#include <Lambda/UI/WindowUI.hpp>
#include <Lambda/UI/Views/Views.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace lambdaui;
using namespace lambda_editor;

namespace {

enum class EditorPanel {
  None,
  Find,
  Replace,
  GoToLine,
};

std::string editorWindowTitle(EditorDocument const& document) {
  return document.displayName() + (document.isDirty() ? " *" : "") + " - Lambda Editor";
}

std::filesystem::path documentDirectory(EditorDocument const& document) {
  if (document.hasPath()) {
    return document.path().parent_path();
  }
  std::error_code ec;
  std::filesystem::path current = std::filesystem::current_path(ec);
  return ec ? std::filesystem::path{"."} : current;
}

std::string saveDialogInitialName(EditorDocument const& document) {
  if (!document.hasPath()) {
    return "Untitled.txt";
  }
  return document.displayName();
}

void requestWindowClose(unsigned int handle) {
  if (handle != 0 && Application::hasInstance()) {
    Application::instance().eventQueue().post(WindowEvent{WindowEvent::Kind::CloseRequest, handle});
  }
}

std::optional<std::string> readWholeFile(std::string const& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return std::nullopt;
  }
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

int parsePositiveInt(std::string const& text) {
  if (text.empty()) {
    return 0;
  }
  int value = 0;
  for (char ch : text) {
    if (ch < '0' || ch > '9') {
      return 0;
    }
    value = value * 10 + (ch - '0');
  }
  return value;
}

int envPositiveInt(char const* name, int fallback) {
  char const* value = std::getenv(name);
  if (!value || !*value) {
    return fallback;
  }
  int parsed = parsePositiveInt(value);
  return parsed > 0 ? parsed : fallback;
}

std::string envString(char const* name, std::string fallback = {}) {
  char const* value = std::getenv(name);
  return value && *value ? std::string{value} : std::move(fallback);
}

bool writeWholeFile(std::filesystem::path const& path, std::string const& contents) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) {
    return false;
  }
  out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  if (!out) {
    return false;
  }
  std::error_code ec;
  std::filesystem::last_write_time(
      path,
      std::filesystem::file_time_type::clock::now() + std::chrono::seconds{5},
      ec);
  return !ec;
}

std::string generatedFileWatchReplacement() {
  std::string text;
  for (int i = 1; i <= 120; ++i) {
    text += "externally replaced line " + std::to_string(i) +
            " with enough text to exercise scrolling and reload preservation\n";
  }
  return text;
}

char const* watchKindLabel(EditorFileWatchEventKind kind) {
  switch (kind) {
  case EditorFileWatchEventKind::Modified:
    return "modified";
  case EditorFileWatchEventKind::Missing:
    return "missing";
  }
  return "unknown";
}

TextInput::Style compactInputStyle() {
  TextInput::Style style;
  style.font = Font{.size = 12.f, .weight = 430.f};
  style.paddingH = 8.f;
  style.paddingV = 5.f;
  style.cornerRadius = 6.f;
  return style;
}

constexpr Shortcut ctrlShortcut(KeyCode key, Modifiers extra = Modifiers::None) {
  return Shortcut{key, Modifiers::Ctrl | extra};
}

constexpr float kCommandPaletteRowHeight = 52.f;
constexpr float kCommandPaletteRowSpacing = 4.f;
constexpr float kCommandPaletteListHeight = 360.f;

void registerCommandDescriptor(Window* window, std::string name, CommandDescriptor descriptor) {
  if (!window) {
    return;
  }
  window->registerCommand(std::move(name), std::move(descriptor));
}

void registerStandardTextCommandDescriptors(Window* window) {
  auto textCommand = [window](std::string name,
                             std::string title,
                             std::string category,
                             Shortcut shortcut,
                             std::string description = {}) {
    registerCommandDescriptor(window, std::move(name), CommandDescriptor{
        .title = std::move(title),
        .description = std::move(description),
        .category = std::move(category),
        .icon = IconName::Keyboard,
        .shortcut = shortcut,
    });
  };

  textCommand("edit.deleteBackward", "Delete Backward", "Edit",
              Shortcut{keys::Delete, Modifiers::None});
  textCommand("edit.deleteForward", "Delete Forward", "Edit",
              Shortcut{keys::ForwardDelete, Modifiers::None});
  textCommand("edit.deleteWordBackward", "Delete Word Backward", "Edit",
              ctrlShortcut(keys::Delete));
  textCommand("edit.deleteWordForward", "Delete Word Forward", "Edit",
              ctrlShortcut(keys::ForwardDelete));
  textCommand("edit.pastePlainText", "Paste as Plain Text", "Edit",
              ctrlShortcut(keys::V, Modifiers::Shift));
  textCommand("edit.selectLine", "Select Line", "Selection",
              ctrlShortcut(keys::L));
  textCommand("edit.deleteLine", "Delete Line", "Edit",
              ctrlShortcut(keys::K, Modifiers::Shift));
  textCommand("edit.insertLineBelow", "Insert Line Below", "Edit",
              ctrlShortcut(keys::Return));
  textCommand("edit.insertLineAbove", "Insert Line Above", "Edit",
              ctrlShortcut(keys::Return, Modifiers::Shift));
  textCommand("edit.moveLineUp", "Move Line Up", "Edit",
              Shortcut{keys::UpArrow, Modifiers::Alt});
  textCommand("edit.moveLineDown", "Move Line Down", "Edit",
              Shortcut{keys::DownArrow, Modifiers::Alt});
  textCommand("edit.copyLineUp", "Copy Line Up", "Edit",
              Shortcut{keys::UpArrow, Modifiers::Alt | Modifiers::Shift});
  textCommand("edit.copyLineDown", "Copy Line Down", "Edit",
              Shortcut{keys::DownArrow, Modifiers::Alt | Modifiers::Shift});

  textCommand("cursor.left", "Move Cursor Left", "Cursor",
              Shortcut{keys::LeftArrow, Modifiers::None});
  textCommand("cursor.right", "Move Cursor Right", "Cursor",
              Shortcut{keys::RightArrow, Modifiers::None});
  textCommand("cursor.up", "Move Cursor Up", "Cursor",
              Shortcut{keys::UpArrow, Modifiers::None});
  textCommand("cursor.down", "Move Cursor Down", "Cursor",
              Shortcut{keys::DownArrow, Modifiers::None});
  textCommand("cursor.wordLeft", "Move Cursor Word Left", "Cursor",
              ctrlShortcut(keys::LeftArrow));
  textCommand("cursor.wordRight", "Move Cursor Word Right", "Cursor",
              ctrlShortcut(keys::RightArrow));
  textCommand("cursor.lineStart", "Move Cursor to Line Start", "Cursor",
              Shortcut{keys::Home, Modifiers::None});
  textCommand("cursor.lineEnd", "Move Cursor to Line End", "Cursor",
              Shortcut{keys::End, Modifiers::None});
  textCommand("cursor.documentStart", "Move Cursor to Document Start", "Cursor",
              ctrlShortcut(keys::Home));
  textCommand("cursor.documentEnd", "Move Cursor to Document End", "Cursor",
              ctrlShortcut(keys::End));

  textCommand("selection.left", "Select Left", "Selection",
              Shortcut{keys::LeftArrow, Modifiers::Shift});
  textCommand("selection.right", "Select Right", "Selection",
              Shortcut{keys::RightArrow, Modifiers::Shift});
  textCommand("selection.up", "Select Up", "Selection",
              Shortcut{keys::UpArrow, Modifiers::Shift});
  textCommand("selection.down", "Select Down", "Selection",
              Shortcut{keys::DownArrow, Modifiers::Shift});
  textCommand("selection.wordLeft", "Select Word Left", "Selection",
              ctrlShortcut(keys::LeftArrow, Modifiers::Shift));
  textCommand("selection.wordRight", "Select Word Right", "Selection",
              ctrlShortcut(keys::RightArrow, Modifiers::Shift));
  textCommand("selection.lineStart", "Select to Line Start", "Selection",
              Shortcut{keys::Home, Modifiers::Shift});
  textCommand("selection.lineEnd", "Select to Line End", "Selection",
              Shortcut{keys::End, Modifiers::Shift});
  textCommand("selection.documentStart", "Select to Document Start", "Selection",
              ctrlShortcut(keys::Home, Modifiers::Shift));
  textCommand("selection.documentEnd", "Select to Document End", "Selection",
              ctrlShortcut(keys::End, Modifiers::Shift));
}

struct ToolbarButton {
  std::string commandId;
  IconName icon;

  Element body() const {
    Runtime* runtime = Runtime::current();
    Window* window = runtime ? &runtime->window() : nullptr;
    CommandDescriptor const* descriptor = nullptr;
    if (Application::hasInstance()) {
      auto const& descriptors = Application::instance().commandDescriptors();
      auto it = descriptors.find(commandId);
      if (it != descriptors.end()) {
        descriptor = &it->second;
      }
    }
    std::string const title = descriptor ? descriptor->displayTitle() : commandId;
    IconName const resolvedIcon = descriptor && descriptor->icon ? *descriptor->icon : icon;
    useTooltip(TooltipConfig{.text = title, .placement = PopoverPlacement::Below});
    Reactive::Signal<bool> hovered = useHover();
    auto theme = useEnvironment<ThemeKey>();
    auto disabledBinding = [window, commandId = commandId] {
      return !window || !window->isCommandEnabled(commandId);
    };
    auto handleTap = [window, commandId = commandId] {
      if (window && window->isCommandEnabled(commandId)) {
        window->dispatchCommand(commandId);
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
                .size(42.f, 42.f)
                .cornerRadius(CornerRadius{7.f})
                .fill([hovered, theme, disabledBinding] {
                  if (disabledBinding()) {
                    return FillStyle::solid(Colors::transparent);
                  }
                  return FillStyle::solid(hovered() ? theme().hoveredControlBackgroundColor
                                                    : Colors::transparent);
                }),
            Icon{
                .name = resolvedIcon,
                .size = 26.f,
                .weight = 430.f,
                .color = [disabledBinding] {
                  return disabledBinding() ? Color::secondary() : Color::primary();
                },
            })}
        .size(42.f, 42.f)
        .cursor([disabledBinding] {
          return disabledBinding() ? Cursor::Inherit : Cursor::Hand;
        })
        .focusable(false)
        .onKeyDown(std::function<void(KeyCode, Modifiers)>{handleKey})
        .onTap(std::function<void()>{handleTap});
  }
};

struct AutoFocusTextInput {
  TextInput input;
  Reactive::Signal<int> focusGeneration;

  Element body() const {
    useAutoFocus(focusGeneration);
    return input;
  }
};

Element toolbarDivider() {
  return Spacer{}.flex(0.f, 0.f, 12.f);
}

Element toolbarGroup(std::vector<Element> items) {
  return HStack{
      .spacing = 3.f,
      .alignment = Alignment::Center,
      .children = std::move(items),
  };
}

std::string lowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

std::string keyLabel(KeyCode key) {
  switch (key) {
  case keys::A: return "A";
  case keys::B: return "B";
  case keys::C: return "C";
  case keys::D: return "D";
  case keys::E: return "E";
  case keys::F: return "F";
  case keys::G: return "G";
  case keys::H: return "H";
  case keys::I: return "I";
  case keys::J: return "J";
  case keys::K: return "K";
  case keys::L: return "L";
  case keys::M: return "M";
  case keys::N: return "N";
  case keys::O: return "O";
  case keys::P: return "P";
  case keys::Q: return "Q";
  case keys::R: return "R";
  case keys::S: return "S";
  case keys::T: return "T";
  case keys::U: return "U";
  case keys::V: return "V";
  case keys::W: return "W";
  case keys::X: return "X";
  case keys::Y: return "Y";
  case keys::Z: return "Z";
  case keys::Digit0: return "0";
  case keys::Minus: return "-";
  case keys::Equal: return "=";
  case keys::Return: return "Enter";
  case keys::Escape: return "Esc";
  case keys::Tab: return "Tab";
  case keys::Delete: return "Backspace";
  case keys::ForwardDelete: return "Delete";
  case keys::LeftArrow: return "Left";
  case keys::RightArrow: return "Right";
  case keys::UpArrow: return "Up";
  case keys::DownArrow: return "Down";
  case keys::Home: return "Home";
  case keys::End: return "End";
  case keys::F3: return "F3";
  default: return {};
  }
}

std::string shortcutLabel(std::string const& commandId, Shortcut shortcut) {
  if (commandId == "app.commandPalette") {
    return {};
  }
  if (!shortcut.matches(shortcut.key, shortcut.modifiers)) {
    return {};
  }
  std::vector<std::string> parts;
  if (any(shortcut.modifiers & Modifiers::Ctrl)) parts.push_back("Ctrl");
  if (any(shortcut.modifiers & Modifiers::Shift)) parts.push_back("Shift");
  if (any(shortcut.modifiers & Modifiers::Alt)) parts.push_back("Alt");
  if (any(shortcut.modifiers & Modifiers::Meta)) parts.push_back("Super");
  std::string key = keyLabel(shortcut.key);
  if (key.empty()) {
    return {};
  }
  parts.push_back(std::move(key));
  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) out += "+";
    out += parts[i];
  }
  return out;
}

struct CommandPaletteItem {
  std::string id;
  std::string title;
  std::string category;
  std::string description;
  std::string shortcut;
  std::optional<IconName> icon;
  bool enabled = false;

  bool operator==(CommandPaletteItem const&) const = default;
};

bool commandMatchesQuery(CommandPaletteItem const& item, std::string const& query) {
  if (query.empty()) {
    return true;
  }
  std::string const q = lowerAscii(query);
  return lowerAscii(item.title).find(q) != std::string::npos ||
         lowerAscii(item.category).find(q) != std::string::npos ||
         lowerAscii(item.description).find(q) != std::string::npos ||
         lowerAscii(item.id).find(q) != std::string::npos;
}

std::vector<CommandPaletteItem> buildCommandPaletteItems(Runtime* runtime,
                                                         std::optional<ComponentKey> const& targetKey,
                                                         std::string const& query) {
  std::vector<CommandPaletteItem> items;
  if (!Application::hasInstance()) {
    return items;
  }
  auto const& descriptors = Application::instance().commandDescriptors();
  items.reserve(descriptors.size());
  for (auto const& [id, descriptor] : descriptors) {
    if (!descriptor.paletteVisible) {
      continue;
    }
    std::string title = descriptor.displayTitle();
    if (title.empty()) {
      title = id;
    }
    ComponentKey const effectiveTarget = targetKey.value_or(ComponentKey{});
    CommandPaletteItem item{
        .id = id,
        .title = std::move(title),
        .category = descriptor.category,
        .description = descriptor.description,
        .shortcut = shortcutLabel(id, descriptor.shortcut),
        .icon = descriptor.icon,
        .enabled = runtime &&
                   runtime->isCommandCurrentlyEnabledFrom(effectiveTarget, id),
    };
    if (commandMatchesQuery(item, query)) {
      items.push_back(std::move(item));
    }
  }
  std::sort(items.begin(), items.end(), [](CommandPaletteItem const& lhs,
                                           CommandPaletteItem const& rhs) {
    if (lhs.category != rhs.category) {
      return lhs.category < rhs.category;
    }
    if (lhs.title != rhs.title) {
      return lhs.title < rhs.title;
    }
    return lhs.id < rhs.id;
  });
  return items;
}

int firstCommandPaletteIndex(std::vector<CommandPaletteItem> const& items) {
  return items.empty() ? -1 : 0;
}

int clampedCommandPaletteIndex(std::vector<CommandPaletteItem> const& items, int index) {
  if (items.empty()) {
    return -1;
  }
  return std::clamp(index, 0, static_cast<int>(items.size()) - 1);
}

void ensureCommandPaletteIndexVisible(Signal<Point> const& scrollOffset, int index) {
  if (index < 0) {
    scrollOffset.set(Point{});
    return;
  }
  float const stride = kCommandPaletteRowHeight + kCommandPaletteRowSpacing;
  float const rowTop = static_cast<float>(index) * stride;
  float const rowBottom = rowTop + kCommandPaletteRowHeight;
  float nextY = scrollOffset.peek().y;
  if (rowTop < nextY) {
    nextY = rowTop;
  } else if (rowBottom > nextY + kCommandPaletteListHeight) {
    nextY = rowBottom - kCommandPaletteListHeight;
  }
  nextY = std::max(0.f, nextY);
  if (std::abs(nextY - scrollOffset.peek().y) > 0.1f) {
    scrollOffset.set(Point{0.f, nextY});
  }
}

bool runCommandPaletteItem(std::vector<CommandPaletteItem> const& items,
                           int index,
                           Signal<bool> const& open,
                           Signal<std::optional<ComponentKey>> const& targetKey) {
  Runtime* runtime = Runtime::current();
  if (!runtime || index < 0 || index >= static_cast<int>(items.size())) {
    return false;
  }
  CommandPaletteItem const& item = items[static_cast<std::size_t>(index)];
  if (!item.enabled) {
    return false;
  }
  ComponentKey const effectiveTarget = targetKey.peek().value_or(ComponentKey{});
  if (!runtime->dispatchCommandFrom(effectiveTarget, item.id)) {
    return false;
  }
  open.set(false);
  return true;
}

struct CommandPaletteRow {
  CommandPaletteItem item;
  Reactive::Signal<bool> open;
  Reactive::Signal<std::optional<ComponentKey>> targetKey;
  Reactive::Signal<int> selectedIndex;
  Reactive::Signal<std::size_t> index;

  Element body() const {
    Reactive::Signal<bool> hovered = useHover();
    auto theme = useEnvironment<ThemeKey>();
    auto isSelected = [selectedIndex = selectedIndex, index = index] {
      return selectedIndex.peek() == static_cast<int>(index.peek());
    };
    auto activate = [items = std::vector<CommandPaletteItem>{item},
                     open = open,
                     targetKey = targetKey] {
      runCommandPaletteItem(items, 0, open, targetKey);
    };
    auto selectThis = [selectedIndex = selectedIndex, index = index] {
      selectedIndex.set(static_cast<int>(index.peek()));
    };
    return HStack{
        .spacing = 10.f,
        .alignment = Alignment::Center,
        .children = children(
            Icon{
                .name = item.icon.value_or(IconName::Terminal),
                .size = 22.f,
                .weight = 430.f,
                .color = [item = item] {
                  return item.enabled ? Color::primary() : Color::secondary();
                },
            },
            VStack{
                .spacing = 2.f,
                .alignment = Alignment::Stretch,
                .children = children(
                    Text{
                        .text = item.title,
                        .font = Font{.size = 14.f, .weight = 520.f},
                        .color = [item = item] {
                          return item.enabled ? Color::primary() : Color::secondary();
                        },
                        .verticalAlignment = VerticalAlignment::Center,
                    },
                    Text{
                        .text = item.category.empty() ? item.id : item.category,
                        .font = Font{.size = 11.f, .weight = 430.f},
                        .color = Color::secondary(),
                        .verticalAlignment = VerticalAlignment::Center,
                    })}.flex(1.f, 1.f, 0.f),
            Text{
                .text = item.shortcut,
                .font = Font{.size = 12.f, .weight = 500.f},
                .color = Color::secondary(),
                .verticalAlignment = VerticalAlignment::Center,
            })}
        .height(52.f)
        .padding(6.f, 10.f, 6.f, 10.f)
        .cornerRadius(CornerRadius{7.f})
        .fill([hovered, theme, isSelected, item = item] {
          if (isSelected()) {
            return FillStyle::solid(theme().selectedContentBackgroundColor);
          }
          if (item.enabled && hovered()) {
            return FillStyle::solid(theme().hoveredControlBackgroundColor);
          }
          return FillStyle::solid(Colors::transparent);
        })
        .cursor([item = item] {
          return item.enabled ? Cursor::Hand : Cursor::Inherit;
        })
        .focusable(item.enabled)
        .onKeyDown(std::function<void(KeyCode, Modifiers)>{
            [activate](KeyCode key, Modifiers) {
              if (key == keys::Return || key == keys::Space) {
                activate();
              }
            }})
        .onPointerEnter(std::function<void()>{selectThis})
        .onTap(std::function<void()>{[selectThis, activate] {
          selectThis();
          activate();
        }});
  }
};

struct CommandPalette {
  Reactive::Signal<bool> open;
  Reactive::Signal<std::string> query;
  Reactive::Signal<std::vector<CommandPaletteItem>> items;
  Reactive::Signal<std::optional<ComponentKey>> targetKey;
  Reactive::Signal<int> selectedIndex;
  Reactive::Signal<Point> scrollOffset;
  Reactive::Signal<int> focusGeneration;
  std::function<void()> refresh;

  Element body() const {
    auto theme = useEnvironment<ThemeKey>();
    auto close = [open = open] {
      open.set(false);
    };
    auto runFirst = [items = items, open = open, targetKey = targetKey,
                     selectedIndex = selectedIndex] {
      Runtime* runtime = Runtime::current();
      if (!runtime) {
        return;
      }
      if (runCommandPaletteItem(items.peek(), selectedIndex.peek(), open, targetKey)) {
        return;
      }
      for (int i = 0; i < static_cast<int>(items.peek().size()); ++i) {
        if (runCommandPaletteItem(items.peek(), i, open, targetKey)) {
          return;
        }
      }
    };
    auto moveSelection = [items = items, selectedIndex = selectedIndex,
                          scrollOffset = scrollOffset](int delta) {
      std::vector<CommandPaletteItem> const currentItems = items.peek();
      if (currentItems.empty()) {
        selectedIndex.set(-1);
        scrollOffset.set(Point{});
        return;
      }
      int const current = selectedIndex.peek() < 0 ? 0 : selectedIndex.peek();
      int const next = clampedCommandPaletteIndex(currentItems, current + delta);
      selectedIndex.set(next);
      ensureCommandPaletteIndexVisible(scrollOffset, next);
    };
    TextInput::Style searchStyle = compactInputStyle();
    searchStyle.font = Font{.size = 15.f, .weight = 440.f};
    auto commandList = Element{ScrollView{
        .axis = ScrollAxis::Vertical,
        .scrollOffset = scrollOffset,
        .children = children(Element{For<CommandPaletteItem>(
            items,
            [](CommandPaletteItem const& item) {
              return item.id;
            },
            [open = open, targetKey = targetKey, selectedIndex = selectedIndex](
                CommandPaletteItem const& item,
                Reactive::Signal<std::size_t> index) {
              return Element{CommandPaletteRow{
                  .item = item,
                  .open = open,
                  .targetKey = targetKey,
                  .selectedIndex = selectedIndex,
                  .index = index,
              }};
            },
            4.f,
            Alignment::Stretch)})}}
        .height(kCommandPaletteListHeight);
    auto panel = VStack{
        .spacing = 8.f,
        .alignment = Alignment::Stretch,
        .children = children(
            Element{AutoFocusTextInput{
                        .input = TextInput{
                            .value = query,
                            .placeholder = "Run command",
                            .style = searchStyle,
                            .onChange = [refresh = refresh](std::string const&) {
                              if (refresh) {
                                refresh();
                              }
                            },
                            .onSubmit = [runFirst](std::string const&) {
                              runFirst();
                            },
                            .onEscape = [close](std::string const&) {
                              close();
                            },
                            .onPreviewKeyDown = [moveSelection](KeyCode key, Modifiers modifiers) {
                              if (modifiers != Modifiers::None) {
                                return false;
                              }
                              if (key == keys::DownArrow) {
                                moveSelection(1);
                                return true;
                              }
                              if (key == keys::UpArrow) {
                                moveSelection(-1);
                                return true;
                              }
                              if (key == keys::PageDown) {
                                moveSelection(6);
                                return true;
                              }
                              if (key == keys::PageUp) {
                                moveSelection(-6);
                                return true;
                              }
                              return false;
                            },
                            .onPreviewCommand = [moveSelection](std::string const& commandId) {
                              if (commandId == "cursor.down") {
                                moveSelection(1);
                                return true;
                              }
                              if (commandId == "cursor.up") {
                                moveSelection(-1);
                                return true;
                              }
                              return false;
                            },
                        },
                        .focusGeneration = focusGeneration,
                    }}
                .height(42.f),
            std::move(commandList))}
        .width(560.f)
        .padding(10.f)
        .cornerRadius(CornerRadius{8.f})
        .fill(FillStyle::solid(Color::windowBackground()))
        .stroke(StrokeStyle::solid(Color::separator(), 1.f))
        .shadow(ShadowStyle{
            .radius = 24.f,
            .offset = {0.f, 12.f},
            .color = Color{0.f, 0.f, 0.f, 0.24f},
        })
        .padding(88.f, 0.f, 0.f, 0.f);
    auto backdrop = Rectangle{}
        .fill(FillStyle::solid(Color{0.f, 0.f, 0.f, 0.22f}))
        .onTap(std::function<void()>{close});
    std::vector<Element> overlayChildren = children(std::move(backdrop), std::move(panel));
    return Element{ZStack{
        .horizontalAlignment = Alignment::Center,
        .verticalAlignment = Alignment::Start,
        .children = std::move(overlayChildren),
    }};
  }
};

struct EditorFileWatchAutotestState {
  std::uint64_t timerId = 0;
  int step = 0;
  int ticks = 0;
  std::filesystem::path path;
  std::string mode;
  std::string replacement;
  int expectedCaret = 0;
  Point expectedScroll{};
};

struct LambdaEditor {
  EditorDocument initialDocument = EditorDocument::untitled();
  std::string initialStatus;

  Element body() const {
    auto theme = useEnvironment<ThemeKey>();
    Runtime* runtime = Runtime::current();
    Window* window = runtime ? &runtime->window() : nullptr;
    auto document = useState(initialDocument);
    auto text = useState(initialDocument.text());
    auto selection = useState(detail::TextEditSelection{
        .caretByte = static_cast<int>(initialDocument.text().size()),
        .anchorByte = static_cast<int>(initialDocument.text().size()),
    });
    auto status = useState(initialStatus.empty() ? std::string{"Ready"} : initialStatus);
    auto history = useState<std::shared_ptr<EditorEditHistory>>(std::make_shared<EditorEditHistory>());
    auto historyReady = useState(false);
    auto canUndo = useState(false);
    auto canRedo = useState(false);
    auto clipboardHasText = useState(
        Application::hasInstance() && Application::instance().clipboard().hasText());
    auto activePanel = useState(EditorPanel::None);
    auto panelFocusGeneration = useState(0);
    auto findQuery = useState(std::string{});
    auto replaceValue = useState(std::string{});
    auto gotoLineValue = useState(std::string{});
    auto findCaseSensitive = useState(false);
    auto findWholeWord = useState(false);
    auto findRegex = useState(false);
    auto wordWrap = useState(true);
    auto fontSize = useState(14.f);
    auto commandPaletteOpen = useState(false);
    auto commandPaletteQuery = useState(std::string{});
    auto commandPaletteItems = useState(std::vector<CommandPaletteItem>{});
    auto commandPaletteTargetKey = useState(std::optional<ComponentKey>{});
    auto commandPaletteSelectedIndex = useState(-1);
    auto commandPaletteScrollOffset = useState(Point{});
    auto commandPaletteFocusGeneration = useState(0);
    auto editorScrollOffset = useState(Point{});
    auto fileWatcher = useState<std::shared_ptr<EditorFileWatcher>>(
        std::make_shared<EditorFileWatcher>());
    auto watcherReady = useState(false);
    auto pendingExternalChange = useState(std::optional<EditorFileWatchEvent>{});
    auto activeExternalToastId = useState<std::uint64_t>(0);
    auto fileWatchAutotest =
        useState<std::shared_ptr<EditorFileWatchAutotestState>>(
            std::make_shared<EditorFileWatchAutotestState>());

    auto toastApi = useToast();
    auto showToast = std::get<0>(toastApi);
    auto dismissToast = std::get<1>(toastApi);
    auto clearToasts = std::get<2>(toastApi);
    bool const hasVisibleToasts = std::get<3>(toastApi);
    (void)clearToasts;
    (void)hasVisibleToasts;

    auto refreshHistoryState = [history, canUndo, canRedo] {
      canUndo.set(history.peek()->canUndo());
      canRedo.set(history.peek()->canRedo());
    };

    if (!historyReady.peek()) {
      history.peek()->reset(EditorSnapshot{.text = text.peek(), .selection = selection.peek()});
      historyReady.set(true);
      refreshHistoryState();
    }

    if (!watcherReady.peek()) {
      fileWatcher.peek()->reset(document.peek());
      watcherReady.set(true);
    }

    useEffect([document, window] {
      if (window) {
        window->setTitle(editorWindowTitle(document.get()));
      }
    });

    auto updateDocumentText = [document](std::string const& value) {
      EditorDocument current = document.peek();
      current.setText(value);
      document.set(std::move(current));
    };

    auto applySnapshot = [text, selection, updateDocumentText, history, refreshHistoryState, status](
                             EditorSnapshot snapshot, bool recordHistory, std::string message) {
      snapshot.selection = detail::clampSelection(snapshot.text, snapshot.selection);
      text.set(snapshot.text);
      selection.set(snapshot.selection);
      updateDocumentText(snapshot.text);
      if (recordHistory) {
        history.peek()->record(snapshot);
      }
      refreshHistoryState();
      status.set(std::move(message));
    };

    auto clearExternalPrompt = [pendingExternalChange, activeExternalToastId, dismissToast] {
      pendingExternalChange.set(std::optional<EditorFileWatchEvent>{});
      std::uint64_t const toastId = activeExternalToastId.peek();
      if (toastId != 0) {
        activeExternalToastId.set(0);
        dismissToast(toastId);
      }
    };

    auto resetToDocument = [document, text, selection, history, refreshHistoryState, status,
                            editorScrollOffset, fileWatcher, clearExternalPrompt](
                               EditorDocument next, std::string message) {
      detail::TextEditSelection endSelection{
          .caretByte = static_cast<int>(next.text().size()),
          .anchorByte = static_cast<int>(next.text().size()),
      };
      text.set(next.text());
      selection.set(endSelection);
      history.peek()->reset(EditorSnapshot{.text = next.text(), .selection = endSelection});
      fileWatcher.peek()->reset(next);
      document.set(std::move(next));
      refreshHistoryState();
      editorScrollOffset.set(Point{});
      clearExternalPrompt();
      status.set(std::move(message));
    };

    auto showFileDialog = [window](FileDialog dialog) mutable {
      if (!window || !Application::hasInstance()) {
        return;
      }
      FileDialogMode const dialogMode = dialog.mode;
      Window& dialogWindow = Application::instance().createModalChildWindow<Window>(window->handle(), {
          .size = {760.f, 540.f},
          .title = dialogMode == FileDialogMode::Open ? "Open File" : "Save File",
          .resizable = true,
          .minSize = {640.f, 430.f},
      });
      unsigned int const dialogHandle = dialogWindow.handle();
      auto accept = std::move(dialog.onAccept);
      dialog.onAccept = [accept = std::move(accept), dialogHandle](std::filesystem::path path) mutable {
        bool const accepted = accept ? accept(std::move(path)) : true;
        if (accepted) {
          requestWindowClose(dialogHandle);
        }
        return accepted;
      };
      dialog.onCancel = [dialogHandle] {
        requestWindowClose(dialogHandle);
      };
      dialogWindow.setView(std::move(dialog));
    };

    auto newFile = [resetToDocument] {
      resetToDocument(EditorDocument::untitled(), "New document");
    };
    auto openFile = [document, resetToDocument, status, showFileDialog] mutable {
      showFileDialog(FileDialog{
          .mode = FileDialogMode::Open,
          .initialDirectory = documentDirectory(document.peek()),
          .onAccept = [resetToDocument, status](std::filesystem::path path) {
            EditorDocumentResult result = openDocument(path.string());
            if (result.ok) {
              resetToDocument(result.document, result.status);
            } else {
              status.set(result.status);
            }
            return result.ok;
          },
      });
    };
    auto saveFileAs = [document, text, status, showFileDialog, fileWatcher,
                       clearExternalPrompt] mutable {
      showFileDialog(FileDialog{
          .mode = FileDialogMode::Save,
          .initialDirectory = documentDirectory(document.peek()),
          .initialName = saveDialogInitialName(document.peek()),
          .onAccept = [document, text, status, fileWatcher, clearExternalPrompt](
                          std::filesystem::path path) {
            EditorDocument current = document.peek();
            current.setText(text.peek());
            EditorDocumentResult result = saveDocumentAs(current, path.string());
            status.set(result.status);
            if (result.ok) {
              document.set(result.document);
              text.set(result.document.text());
              fileWatcher.peek()->reset(result.document);
              clearExternalPrompt();
            }
            return result.ok;
          },
      });
    };
    auto saveFile = [document, text, status, saveFileAs, fileWatcher,
                     clearExternalPrompt] mutable {
      EditorDocument current = document.peek();
      current.setText(text.peek());
      EditorDocumentResult result = saveDocument(current);
      status.set(result.status);
      if (result.ok) {
        document.set(result.document);
        text.set(result.document.text());
        fileWatcher.peek()->reset(result.document);
        clearExternalPrompt();
      } else if (result.needsPath) {
        saveFileAs();
      }
    };

    auto reloadExternalChange = [document, text, selection, history, refreshHistoryState,
                                 status, editorScrollOffset, fileWatcher,
                                 clearExternalPrompt, pendingExternalChange] {
      std::optional<EditorFileWatchEvent> event = pendingExternalChange.peek();
      if (!event || event->kind != EditorFileWatchEventKind::Modified) {
        return;
      }

      Point const preservedScroll = editorScrollOffset.peek();
      detail::TextEditSelection const preservedSelection = selection.peek();
      EditorDocumentResult result = openDocument(event->path.string());
      if (!result.ok) {
        status.set(result.status);
        return;
      }

      detail::TextEditSelection const clampedSelection =
          detail::clampSelection(result.document.text(), preservedSelection);
      text.set(result.document.text());
      selection.set(clampedSelection);
      history.peek()->reset(EditorSnapshot{
          .text = result.document.text(),
          .selection = clampedSelection,
      });
      document.set(result.document);
      refreshHistoryState();
      editorScrollOffset.set(preservedScroll);
      fileWatcher.peek()->reset(result.document);
      clearExternalPrompt();
      status.set("Reloaded " + result.document.displayName() + ".");
      std::fprintf(stderr,
                   "lambda-editor-watch: reloaded path=\"%s\" bytes=%zu caret=%d anchor=%d scroll_y=%.3f\n",
                   result.document.pathText().c_str(),
                   result.document.text().size(),
                   clampedSelection.caretByte,
                   clampedSelection.anchorByte,
                   preservedScroll.y);
    };

    auto showExternalChangePrompt = [showToast, dismissToast, saveFileAs,
                                     reloadExternalChange, fileWatcher,
                                     pendingExternalChange, activeExternalToastId,
                                     status](EditorFileWatchEvent event) mutable {
      std::uint64_t const previousToastId = activeExternalToastId.peek();
      if (previousToastId != 0) {
        pendingExternalChange.set(std::optional<EditorFileWatchEvent>{});
        activeExternalToastId.set(0);
        dismissToast(previousToastId);
      }

      pendingExternalChange.set(event);
      std::string displayName = event.path.filename().string();
      if (displayName.empty()) {
        displayName = event.path.string();
      }

      Toast toast;
      toast.placement = ToastPlacement::BottomTrailing;
      toast.autoDismissMs = 0;
      toast.showCloseButton = true;
      toast.minWidth = 360.f;
      toast.maxWidth = 460.f;
      toast.tone = event.kind == EditorFileWatchEventKind::Modified ? ToastTone::Warning
                                                                     : ToastTone::Danger;
      if (event.kind == EditorFileWatchEventKind::Modified) {
        toast.title = "File changed on disk";
        toast.message = event.localDirty
            ? "Reloading " + displayName + " will discard unsaved edits."
            : displayName + " changed outside the editor.";
        toast.action = ToastAction{
            .label = "Reload",
            .variant = ButtonVariant::Secondary,
            .dismissOnTap = false,
            .action = reloadExternalChange,
        };
      } else {
        toast.title = "File no longer exists";
        toast.message = event.localDirty
            ? displayName + " disappeared outside the editor. Save As keeps your unsaved edits."
            : displayName + " disappeared outside the editor.";
        toast.action = ToastAction{
            .label = "Save As",
            .variant = ButtonVariant::Secondary,
            .dismissOnTap = false,
            .action = saveFileAs,
        };
      }
      toast.onDismiss = [fileWatcher, pendingExternalChange, activeExternalToastId, status] {
        if (pendingExternalChange.peek()) {
          fileWatcher.peek()->dismissPending();
          pendingExternalChange.set(std::optional<EditorFileWatchEvent>{});
          status.set("External file change dismissed.");
        }
        activeExternalToastId.set(0);
      };

      std::uint64_t const toastId = showToast(std::move(toast));
      activeExternalToastId.set(toastId);
      status.set(event.kind == EditorFileWatchEventKind::Modified
                     ? "File changed outside the editor."
                     : "File disappeared outside the editor.");
      std::fprintf(stderr,
                   "lambda-editor-watch: prompt kind=%s dirty=%d path=\"%s\"\n",
                   watchKindLabel(event.kind),
                   event.localDirty ? 1 : 0,
                   event.path.string().c_str());
    };

    auto dismissExternalChange = [fileWatcher, pendingExternalChange, activeExternalToastId,
                                  dismissToast, status] {
      if (!pendingExternalChange.peek()) {
        return;
      }
      fileWatcher.peek()->dismissPending();
      pendingExternalChange.set(std::optional<EditorFileWatchEvent>{});
      std::uint64_t const toastId = activeExternalToastId.peek();
      if (toastId != 0) {
        activeExternalToastId.set(0);
        dismissToast(toastId);
      }
      status.set("External file change dismissed.");
    };

    if (Application::hasInstance()) {
      auto watchTimerId = std::make_shared<std::uint64_t>(
          Application::instance().scheduleRepeatingTimer(std::chrono::milliseconds{500},
                                                         window ? window->handle() : 0u));
      Application::instance().eventQueue().on<TimerEvent>(
          [watchTimerId, document, fileWatcher, showExternalChangePrompt](TimerEvent const& event) mutable {
            if (!watchTimerId || *watchTimerId == 0 || event.timerId != *watchTimerId) {
              return;
            }
            if (std::optional<EditorFileWatchEvent> watchEvent =
                    fileWatcher.peek()->poll(document.peek())) {
              showExternalChangePrompt(*watchEvent);
              if (Application::hasInstance()) {
                Application::instance().requestRedraw();
              }
            }
          });
      onCleanup([watchTimerId] {
        if (watchTimerId && *watchTimerId != 0 && Application::hasInstance()) {
          Application::instance().cancelTimer(*watchTimerId);
          *watchTimerId = 0;
        }
      });
    }

    if (!envString("LAMBDA_EDITOR_AUTOTEST_FILE_WATCH").empty() && Application::hasInstance()) {
      std::shared_ptr<EditorFileWatchAutotestState> state = fileWatchAutotest.peek();
      if (state && state->timerId == 0) {
        if (!document.peek().hasPath()) {
          std::fprintf(stderr, "lambda-editor-watch-autotest: initial document has no path\n");
          std::exit(20);
        }
        state->path = document.peek().path();
        state->mode = envString("LAMBDA_EDITOR_AUTOTEST_FILE_WATCH_MODE", "modified");
        if (state->mode != "modified" && state->mode != "missing") {
          std::fprintf(stderr,
                       "lambda-editor-watch-autotest: unsupported mode: %s\n",
                       state->mode.c_str());
          std::exit(21);
        }
        if (state->mode == "modified") {
          std::string replacementPath = envString("LAMBDA_EDITOR_AUTOTEST_FILE_WATCH_TEXT_FILE");
          if (!replacementPath.empty()) {
            std::optional<std::string> replacement = readWholeFile(replacementPath);
            if (!replacement) {
              std::fprintf(stderr,
                           "lambda-editor-watch-autotest: failed to read replacement file: %s\n",
                           replacementPath.c_str());
              std::exit(22);
            }
            state->replacement = std::move(*replacement);
          } else {
            state->replacement = envString("LAMBDA_EDITOR_AUTOTEST_FILE_WATCH_TEXT",
                                           generatedFileWatchReplacement());
          }
        }
        int const requestedCaret = envPositiveInt("LAMBDA_EDITOR_AUTOTEST_FILE_WATCH_CARET", 64);
        state->expectedCaret = std::min(requestedCaret, static_cast<int>(text.peek().size()));
        state->expectedScroll =
            Point{0.f, static_cast<float>(envPositiveInt("LAMBDA_EDITOR_AUTOTEST_FILE_WATCH_SCROLL_Y", 160))};
        state->timerId =
            Application::instance().scheduleRepeatingTimer(std::chrono::milliseconds{100},
                                                           window ? window->handle() : 0u);
        Application::instance().eventQueue().on<TimerEvent>(
            [state, text, selection, editorScrollOffset, pendingExternalChange,
             reloadExternalChange, dismissExternalChange](TimerEvent const& event) {
              if (!state || state->timerId == 0 || event.timerId != state->timerId) {
                return;
              }

              auto fail = [state](int code, char const* message) {
                std::fprintf(stderr,
                             "lambda-editor-watch-autotest: failed code=%d step=%d ticks=%d %s\n",
                             code,
                             state->step,
                             state->ticks,
                             message);
                if (Application::hasInstance()) {
                  Application::instance().cancelTimer(state->timerId);
                }
                state->timerId = 0;
                std::exit(code);
              };
              auto finish = [state] {
                if (Application::hasInstance()) {
                  Application::instance().cancelTimer(state->timerId);
                  Application::instance().quit();
                }
                state->timerId = 0;
              };

              ++state->ticks;
              if (state->ticks > 120) {
                fail(22, "timed out");
                return;
              }

              if (state->step == 0) {
                detail::TextEditSelection const expectedSelection{
                    .caretByte = state->expectedCaret,
                    .anchorByte = state->expectedCaret,
                };
                selection.set(expectedSelection);
                editorScrollOffset.set(state->expectedScroll);
                std::fprintf(stderr,
                             "lambda-editor-watch-autotest: initialized caret=%d scroll_y=%.3f path=\"%s\"\n",
                             state->expectedCaret,
                             state->expectedScroll.y,
                             state->path.string().c_str());
                state->step = 1;
                if (Application::hasInstance()) {
                  Application::instance().requestRedraw();
                }
                return;
              }

              if (state->step == 1) {
                if (state->mode == "missing") {
                  std::error_code ec;
                  bool const removed = std::filesystem::remove(state->path, ec);
                  if (ec || !removed) {
                    fail(23, "could not remove watched file");
                    return;
                  }
                  std::fprintf(stderr,
                               "lambda-editor-watch-autotest: external-remove path=\"%s\"\n",
                               state->path.string().c_str());
                } else {
                  if (!writeWholeFile(state->path, state->replacement)) {
                    fail(24, "could not write replacement");
                    return;
                  }
                  std::fprintf(stderr,
                               "lambda-editor-watch-autotest: external-write bytes=%zu\n",
                               state->replacement.size());
                }
                state->step = 2;
                return;
              }

              if (state->step == 2) {
                std::optional<EditorFileWatchEvent> event = pendingExternalChange.peek();
                if (!event) {
                  return;
                }
                EditorFileWatchEventKind const expectedKind =
                    state->mode == "missing" ? EditorFileWatchEventKind::Missing
                                             : EditorFileWatchEventKind::Modified;
                if (event->kind != expectedKind) {
                  fail(24, "unexpected prompt kind");
                  return;
                }
                if (state->mode == "missing") {
                  std::fprintf(stderr,
                               "lambda-editor-watch-autotest: missing-prompt-observed dirty=%d path=\"%s\"\n",
                               event->localDirty ? 1 : 0,
                               event->path.string().c_str());
                  dismissExternalChange();
                } else {
                  std::fprintf(stderr,
                               "lambda-editor-watch-autotest: prompt-observed dirty=%d path=\"%s\"\n",
                               event->localDirty ? 1 : 0,
                               event->path.string().c_str());
                  reloadExternalChange();
                }
                state->step = 3;
                if (Application::hasInstance()) {
                  Application::instance().requestRedraw();
                }
                return;
              }

              if (state->step == 3) {
                if (state->mode == "missing") {
                  if (pendingExternalChange.peek()) {
                    fail(25, "missing prompt did not dismiss");
                    return;
                  }
                  std::fprintf(stderr,
                               "lambda-editor-watch-autotest: missing-verified path=\"%s\"\n",
                               state->path.string().c_str());
                  finish();
                  return;
                }

                int const expectedCaret =
                    std::min(state->expectedCaret, static_cast<int>(state->replacement.size()));
                detail::TextEditSelection const actualSelection = selection.peek();
                Point const actualScroll = editorScrollOffset.peek();
                if (text.peek() != state->replacement) {
                  fail(26, "reloaded text mismatch");
                  return;
                }
                if (actualSelection.caretByte != expectedCaret ||
                    actualSelection.anchorByte != expectedCaret) {
                  fail(27, "caret was not preserved or clamped");
                  return;
                }
                if (std::abs(actualScroll.y - state->expectedScroll.y) > 0.5f) {
                  fail(28, "scroll offset was not preserved");
                  return;
                }
                std::fprintf(stderr,
                             "lambda-editor-watch-autotest: verified bytes=%zu caret=%d scroll_y=%.3f\n",
                             state->replacement.size(),
                             actualSelection.caretByte,
                             actualScroll.y);
                finish();
              }
            });
        onCleanup([state] {
          if (state && state->timerId != 0 && Application::hasInstance()) {
            Application::instance().cancelTimer(state->timerId);
            state->timerId = 0;
          }
        });
      }
    }

    auto undo = [history, applySnapshot] {
      if (std::optional<EditorSnapshot> snapshot = history.peek()->undo()) {
        applySnapshot(*snapshot, false, "Undo");
      }
    };
    auto redo = [history, applySnapshot] {
      if (std::optional<EditorSnapshot> snapshot = history.peek()->redo()) {
        applySnapshot(*snapshot, false, "Redo");
      }
    };
    auto copySelection = [text, selection, clipboardHasText, status] {
      std::string selected = selectedText(text.peek(), selection.peek());
      if (selected.empty()) {
        status.set("No selection to copy.");
        return;
      }
      if (Application::hasInstance()) {
        Application::instance().clipboard().writeText(selected);
        clipboardHasText.set(true);
      }
      status.set("Copied selection.");
    };
    auto cutSelection = [text, selection, copySelection, applySnapshot, status] {
      if (!selection.peek().hasSelection()) {
        status.set("No selection to cut.");
        return;
      }
      copySelection();
      applySnapshot(replaceSelection(text.peek(), selection.peek(), ""), true, "Cut selection.");
    };
    auto pasteClipboard = [text, selection, clipboardHasText, applySnapshot, status] {
      if (!Application::hasInstance()) {
        clipboardHasText.set(false);
        return;
      }
      std::optional<std::string> clipboard = Application::instance().clipboard().readText();
      if (!clipboard || clipboard->empty()) {
        clipboardHasText.set(false);
        status.set("Clipboard is empty.");
        return;
      }
      clipboardHasText.set(true);
      applySnapshot(insertAtSelection(text.peek(), selection.peek(), *clipboard), true, "Pasted text.");
    };
    auto selectAll = [text, selection, status] {
      selection.set(detail::selectAllSelection(text.peek()));
      status.set("Selected all text.");
    };
    auto currentFindOptions = [findCaseSensitive, findWholeWord, findRegex] {
      return FindOptions{
          .caseSensitive = findCaseSensitive.peek(),
          .wholeWord = findWholeWord.peek(),
          .regex = findRegex.peek(),
      };
    };
    auto requestPanelFocus = [panelFocusGeneration] {
      panelFocusGeneration.set(panelFocusGeneration.peek() + 1);
    };
    auto showFind = [activePanel, requestPanelFocus, status] {
      activePanel.set(EditorPanel::Find);
      requestPanelFocus();
      status.set("Find");
    };
    auto showReplace = [activePanel, requestPanelFocus, status] {
      activePanel.set(EditorPanel::Replace);
      requestPanelFocus();
      status.set("Replace");
    };
    auto showGoToLine = [activePanel, requestPanelFocus, gotoLineValue, text, selection, status] {
      activePanel.set(EditorPanel::GoToLine);
      gotoLineValue.set(std::to_string(lineNumberForSelection(text.peek(), selection.peek())));
      requestPanelFocus();
      status.set("Go to line");
    };
    auto closePanel = [activePanel, status] {
      activePanel.set(EditorPanel::None);
      status.set("Ready");
    };
    auto findNext = [text, selection, findQuery, currentFindOptions, status] {
      if (std::optional<detail::TextEditSelection> match =
              findNextMatch(text.peek(), findQuery.peek(), selection.peek(), true, currentFindOptions())) {
        selection.set(*match);
        status.set("Match found.");
      } else {
        status.set("No match.");
      }
    };
    auto findPrevious = [text, selection, findQuery, currentFindOptions, status] {
      if (std::optional<detail::TextEditSelection> match =
              findPreviousMatch(text.peek(), findQuery.peek(), selection.peek(), true, currentFindOptions())) {
        selection.set(*match);
        status.set("Match found.");
      } else {
        status.set("No match.");
      }
    };
    auto replaceOne = [text, selection, findQuery, replaceValue, currentFindOptions,
                       applySnapshot, findNext, status] {
      if (findQuery.peek().empty()) {
        status.set("Enter text to replace.");
        return;
      }
      if (!selectionMatches(text.peek(), selection.peek(), findQuery.peek(), currentFindOptions())) {
        findNext();
        return;
      }
      applySnapshot(replaceSelection(text.peek(), selection.peek(), replaceValue.peek()),
                    true,
                    "Replaced match.");
    };
    auto replaceAll = [text, findQuery, replaceValue, currentFindOptions, applySnapshot, status] {
      if (findQuery.peek().empty()) {
        status.set("Enter text to replace.");
        return;
      }
      EditorSnapshot snapshot =
          replaceAllMatches(text.peek(), findQuery.peek(), replaceValue.peek(), currentFindOptions());
      if (snapshot.text == text.peek()) {
        status.set("No match.");
        return;
      }
      applySnapshot(std::move(snapshot), true, "Replaced all matches.");
    };
    auto goToLine = [text, selection, gotoLineValue, status] {
      int const line = parsePositiveInt(gotoLineValue.peek());
      if (line <= 0) {
        status.set("Enter a line number.");
        return;
      }
      selection.set(lineSelection(text.peek(), line));
      status.set("Moved to line " + std::to_string(line) + ".");
    };

    auto toggleWordWrap = [wordWrap, status] {
      bool const next = !wordWrap.peek();
      wordWrap.set(next);
      status.set(next ? "Word wrap on." : "Word wrap off.");
    };
    auto toggleFindCaseSensitive = [findCaseSensitive, status] {
      bool const next = !findCaseSensitive.peek();
      findCaseSensitive.set(next);
      status.set(next ? "Find case-sensitive." : "Find case-insensitive.");
    };
    auto toggleFindWholeWord = [findWholeWord, status] {
      bool const next = !findWholeWord.peek();
      findWholeWord.set(next);
      status.set(next ? "Find whole words." : "Find any occurrence.");
    };
    auto toggleFindRegex = [findRegex, status] {
      bool const next = !findRegex.peek();
      findRegex.set(next);
      status.set(next ? "Find regex enabled." : "Find regex disabled.");
    };
    auto zoomOut = [fontSize, status] {
      float const next = std::max(10.f, fontSize.peek() - 1.f);
      fontSize.set(next);
      status.set("Zoom " + std::to_string(static_cast<int>(next)) + " pt.");
    };
    auto zoomIn = [fontSize, status] {
      float const next = std::min(32.f, fontSize.peek() + 1.f);
      fontSize.set(next);
      status.set("Zoom " + std::to_string(static_cast<int>(next)) + " pt.");
    };
    auto zoomReset = [fontSize, status] {
      fontSize.set(14.f);
      status.set("Zoom 14 pt.");
    };
    auto refreshCommandPalette = [runtime, commandPaletteQuery, commandPaletteItems,
                                  commandPaletteTargetKey, commandPaletteSelectedIndex,
                                  commandPaletteScrollOffset] {
      std::vector<CommandPaletteItem> nextItems =
          buildCommandPaletteItems(runtime, commandPaletteTargetKey.peek(), commandPaletteQuery.peek());
      commandPaletteSelectedIndex.set(firstCommandPaletteIndex(nextItems));
      commandPaletteScrollOffset.set(Point{});
      commandPaletteItems.set(std::move(nextItems));
    };
    auto openCommandPalette = [commandPaletteOpen, commandPaletteQuery, commandPaletteItems,
                               commandPaletteTargetKey, commandPaletteSelectedIndex,
                               commandPaletteScrollOffset, commandPaletteFocusGeneration, runtime] {
      std::optional<ComponentKey> target = runtime ? runtime->focusTargetKey() : std::nullopt;
      std::vector<CommandPaletteItem> nextItems = buildCommandPaletteItems(runtime, target, "");
      commandPaletteQuery.set("");
      commandPaletteTargetKey.set(target);
      commandPaletteSelectedIndex.set(firstCommandPaletteIndex(nextItems));
      commandPaletteScrollOffset.set(Point{});
      commandPaletteItems.set(std::move(nextItems));
      commandPaletteOpen.set(true);
      commandPaletteFocusGeneration.set(commandPaletteFocusGeneration.peek() + 1);
    };

    registerStandardTextCommandDescriptors(window);

    useWindowCommand("app.commandPalette", openCommandPalette, CommandDescriptor{
        .title = "Command Palette",
        .description = "Find and run editor commands.",
        .category = "Application",
        .icon = IconName::KeyboardCommandKey,
        .shortcut = ctrlShortcut(keys::P, Modifiers::Shift),
        .paletteVisible = false,
        .isEnabled = [] { return true; },
    });
    useWindowCommand("file.new", newFile, CommandDescriptor{
        .title = "New",
        .category = "File",
        .icon = IconName::NoteAdd,
        .shortcut = ctrlShortcut(keys::N),
        .isEnabled = [] { return true; },
    });
    useWindowCommand("file.open", openFile, CommandDescriptor{
        .title = "Open",
        .category = "File",
        .icon = IconName::FileOpen,
        .shortcut = ctrlShortcut(keys::O),
        .isEnabled = [] { return true; },
    });
    useWindowCommand("file.save", saveFile, CommandDescriptor{
        .title = "Save",
        .category = "File",
        .icon = IconName::Save,
        .shortcut = ctrlShortcut(keys::S),
        .isEnabled = [] { return true; },
    });
    useWindowCommand("file.saveAs", saveFileAs, CommandDescriptor{
        .title = "Save As",
        .category = "File",
        .icon = IconName::SaveAs,
        .shortcut = ctrlShortcut(keys::S, Modifiers::Shift),
        .isEnabled = [] { return true; },
    });
    useWindowCommand("file.reloadExternalChange", reloadExternalChange, CommandDescriptor{
        .title = "Reload Changed File",
        .category = "File",
        .icon = IconName::Refresh,
        .paletteVisible = false,
        .isEnabled = [pendingExternalChange] {
          std::optional<EditorFileWatchEvent> event = pendingExternalChange.peek();
          return event && event->kind == EditorFileWatchEventKind::Modified;
        },
    });
    useWindowCommand("file.dismissExternalChange", dismissExternalChange, CommandDescriptor{
        .title = "Dismiss External File Change",
        .category = "File",
        .icon = IconName::Close,
        .paletteVisible = false,
        .isEnabled = [pendingExternalChange] {
          return pendingExternalChange.peek().has_value();
        },
    });
    useWindowCommand("edit.undo", undo, CommandDescriptor{
        .title = "Undo",
        .category = "Edit",
        .icon = IconName::Undo,
        .shortcut = ctrlShortcut(keys::Z),
        .isEnabled = [canUndo] { return canUndo.peek(); },
    });
    useWindowCommand("edit.redo", redo, CommandDescriptor{
        .title = "Redo",
        .category = "Edit",
        .icon = IconName::Redo,
        .shortcut = ctrlShortcut(keys::Z, Modifiers::Shift),
        .isEnabled = [canRedo] { return canRedo.peek(); },
    });
    useWindowCommand("edit.cut", cutSelection, CommandDescriptor{
        .title = "Cut",
        .category = "Edit",
        .icon = IconName::ContentCut,
        .shortcut = ctrlShortcut(keys::X),
        .isEnabled = [selection] { return selection.peek().hasSelection(); },
    });
    useWindowCommand("edit.copy", copySelection, CommandDescriptor{
        .title = "Copy",
        .category = "Edit",
        .icon = IconName::ContentCopy,
        .shortcut = ctrlShortcut(keys::C),
        .isEnabled = [selection] { return selection.peek().hasSelection(); },
    });
    useWindowCommand("edit.paste", pasteClipboard, CommandDescriptor{
        .title = "Paste",
        .category = "Edit",
        .icon = IconName::ContentPaste,
        .shortcut = ctrlShortcut(keys::V),
        .isEnabled = [clipboardHasText] {
          return clipboardHasText.peek() ||
                 (Application::hasInstance() && Application::instance().clipboard().hasText());
        },
    });
    useWindowCommand("edit.selectAll", selectAll, CommandDescriptor{
        .title = "Select All",
        .category = "Edit",
        .icon = IconName::SelectAll,
        .shortcut = ctrlShortcut(keys::A),
        .isEnabled = [] { return true; },
    });
    useWindowCommand("editor.find", showFind, CommandDescriptor{
        .title = "Find",
        .category = "Navigate",
        .icon = IconName::Search,
        .shortcut = ctrlShortcut(keys::F),
        .isEnabled = [] { return true; },
    });
    useWindowCommand("editor.findNext", findNext, CommandDescriptor{
        .title = "Find Next",
        .category = "Navigate",
        .icon = IconName::Search,
        .shortcut = Shortcut{keys::F3, Modifiers::None},
        .isEnabled = [findQuery] { return !findQuery.peek().empty(); },
    });
    useWindowCommand("editor.findPrevious", findPrevious, CommandDescriptor{
        .title = "Find Previous",
        .category = "Navigate",
        .icon = IconName::Search,
        .shortcut = Shortcut{keys::F3, Modifiers::Shift},
        .isEnabled = [findQuery] { return !findQuery.peek().empty(); },
    });
    useWindowCommand("editor.replace", showReplace, CommandDescriptor{
        .title = "Replace",
        .category = "Navigate",
        .icon = IconName::FindReplace,
        .shortcut = ctrlShortcut(keys::H),
        .isEnabled = [] { return true; },
    });
    useWindowCommand("editor.goToLine", showGoToLine, CommandDescriptor{
        .title = "Go To Line",
        .category = "Navigate",
        .icon = IconName::MoreHoriz,
        .shortcut = ctrlShortcut(keys::G),
        .isEnabled = [] { return true; },
    });
    useWindowCommand("editor.toggleFindCaseSensitive", toggleFindCaseSensitive, CommandDescriptor{
        .title = "Toggle Find Case Sensitive",
        .category = "Navigate",
        .icon = IconName::TextFields,
        .isEnabled = [] { return true; },
    });
    useWindowCommand("editor.toggleFindWholeWord", toggleFindWholeWord, CommandDescriptor{
        .title = "Toggle Find Whole Word",
        .category = "Navigate",
        .icon = IconName::TextFieldsAlt,
        .isEnabled = [] { return true; },
    });
    useWindowCommand("editor.toggleFindRegex", toggleFindRegex, CommandDescriptor{
        .title = "Toggle Find Regex",
        .category = "Navigate",
        .icon = IconName::RegularExpression,
        .isEnabled = [] { return true; },
    });
    useWindowCommand("view.wordWrap", toggleWordWrap, CommandDescriptor{
        .title = "Word Wrap",
        .category = "View",
        .icon = IconName::WrapText,
        .isEnabled = [] { return true; },
    });
    useWindowCommand("view.zoomOut", zoomOut, CommandDescriptor{
        .title = "Zoom Out",
        .category = "View",
        .icon = IconName::ZoomOut,
        .shortcut = ctrlShortcut(keys::Minus),
        .isEnabled = [fontSize] { return fontSize.peek() > 10.f; },
    });
    useWindowCommand("view.zoomIn", zoomIn, CommandDescriptor{
        .title = "Zoom In",
        .category = "View",
        .icon = IconName::ZoomIn,
        .shortcut = ctrlShortcut(keys::Equal),
        .isEnabled = [fontSize] { return fontSize.peek() < 32.f; },
    });
    useWindowCommand("view.zoomReset", zoomReset, CommandDescriptor{
        .title = "Reset Zoom",
        .category = "View",
        .icon = IconName::RestartAlt,
        .shortcut = ctrlShortcut(keys::Digit0),
        .isEnabled = [fontSize] { return fontSize.peek() != 14.f; },
    });

    TextInput::Style editorStyle = TextInput::Style::plain();
    editorStyle.font = [fontSize] {
      return Font{
          .family = "monospace",
          .size = fontSize(),
      };
    };
    editorStyle.textColor = Color::primary();
    editorStyle.placeholderColor = Color::secondary();
    editorStyle.paddingH = 12.f;
    editorStyle.paddingV = 12.f;
    editorStyle.lineHeight = [fontSize] {
      return fontSize() + 6.f;
    };

    TextInput::Style panelInputStyle = compactInputStyle();

    auto panel = Show(
        [activePanel] {
          return activePanel() != EditorPanel::None;
        },
        [activePanel, panelFocusGeneration, findQuery, replaceValue, gotoLineValue, findNext,
         replaceOne, replaceAll, goToLine, closePanel, panelInputStyle, theme] {
          return HStack{
              .spacing = 8.f,
              .alignment = Alignment::Center,
              .children = children(
                  Show(
                      [activePanel] {
                        return activePanel() == EditorPanel::Find ||
                               activePanel() == EditorPanel::Replace;
                      },
                      [findQuery, findNext, panelFocusGeneration, panelInputStyle] {
                        return HStack{
                            .spacing = 6.f,
                            .alignment = Alignment::Center,
                            .children = children(
                                Element{AutoFocusTextInput{
                                            .input = TextInput{
                                                .value = findQuery,
                                                .placeholder = "Find",
                                                .style = panelInputStyle,
                                                .onSubmit = [findNext](std::string const &) { findNext(); },
                                            },
                                            .focusGeneration = panelFocusGeneration,
                                        }}
                                    .width(220.f),
                                Button{
                                    .label = "Next",
                                    .variant = ButtonVariant::Secondary,
                                    .disabled = [findQuery] { return findQuery().empty(); },
                                    .onTap = findNext,
                                })};
                      }),
                  Show(
                      [activePanel] {
                        return activePanel() == EditorPanel::Replace;
                      },
                      [replaceValue, replaceOne, replaceAll, panelInputStyle] {
                        return HStack{
                            .spacing = 6.f,
                            .alignment = Alignment::Center,
                            .children = children(
                                TextInput{
                                    .value = replaceValue,
                                    .placeholder = "Replace",
                                    .style = panelInputStyle,
                                    .onSubmit = [replaceOne](std::string const &) { replaceOne(); },
                                }
                                    .width(220.f),
                                Button{
                                    .label = "Replace",
                                    .variant = ButtonVariant::Secondary,
                                    .onTap = replaceOne,
                                },
                                Button{
                                    .label = "All",
                                    .variant = ButtonVariant::Secondary,
                                    .onTap = replaceAll,
                                })};
                      }),
                  Show(
                      [activePanel] {
                        return activePanel() == EditorPanel::GoToLine;
                      },
                      [gotoLineValue, goToLine, panelFocusGeneration, panelInputStyle] {
                        return HStack{
                            .spacing = 6.f,
                            .alignment = Alignment::Center,
                            .children = children(
                                Element{AutoFocusTextInput{
                                            .input = TextInput{
                                                .value = gotoLineValue,
                                                .placeholder = "Line",
                                                .style = panelInputStyle,
                                                .onSubmit = [goToLine](std::string const &) { goToLine(); },
                                            },
                                            .focusGeneration = panelFocusGeneration,
                                        }}
                                    .width(120.f),
                                Button{
                                    .label = "Go",
                                    .variant = ButtonVariant::Secondary,
                                    .onTap = goToLine,
                                })};
                      }),
                  Spacer{}.flex(1.f, 1.f, 0.f),
                  Button{
                      .label = "Close",
                      .variant = ButtonVariant::Ghost,
                      .onTap = closePanel,
                  })}
              .height(44.f)
              .padding(5.f, theme().space3, 5.f, theme().space3)
              .fill(FillStyle::solid(Color::windowBackground()))
              .stroke(StrokeStyle::solid(Color::separator(), 1.f));
        });

    auto editorArea = [text, selection, document, history, refreshHistoryState,
                       editorStyle, editorScrollOffset](bool wrap) {
      return ScrollView{
          .axis = wrap ? ScrollAxis::Vertical : ScrollAxis::Both,
          .scrollOffset = editorScrollOffset,
          .children = children(
              TextInput{
                  .value = text,
                  .selection = selection,
                  .placeholder = "Start typing...",
                  .style = editorStyle,
                  .multiline = true,
                  .wrapping = wrap ? TextWrapping::Wrap : TextWrapping::NoWrap,
                  .multilineHeight = {.fixed = 0.f, .minIntrinsic = 560.f},
                  .onEdit = [document, history, refreshHistoryState](
                                std::string const& value,
                                detail::TextEditSelection editSelection) {
                    EditorDocument current = document.peek();
                    current.setText(value);
                    document.set(std::move(current));
                    history.peek()->record(EditorSnapshot{
                        .text = value,
                        .selection = editSelection,
                    });
                    refreshHistoryState();
                  },
              }
                  .fill(FillStyle::solid(Color::controlBackground()))),
      }
          .flex(1.f, 1.f, 0.f)
          .fill(FillStyle::solid(Color::controlBackground()));
    };

    auto editorContent = Element{Show(
        [wordWrap] {
          return wordWrap();
        },
        [editorArea] {
          return editorArea(true);
        },
        [editorArea] {
          return editorArea(false);
        })}.flex(1.f, 1.f, 0.f);

    auto mainContent = VStack{
        .spacing = 0.f,
        .alignment = Alignment::Stretch,
        .children = children(
            HStack{
                       .spacing = 8.f,
                       .alignment = Alignment::Center,
                       .children = children(
                           toolbarGroup(children(
                               ToolbarButton{.commandId = "file.new",
                                             .icon = IconName::NoteAdd},
                               ToolbarButton{.commandId = "file.open",
                                             .icon = IconName::FileOpen},
                               ToolbarButton{.commandId = "file.save",
                                             .icon = IconName::Save},
                               ToolbarButton{.commandId = "file.saveAs",
                                             .icon = IconName::SaveAs})),
                           toolbarDivider(),
                           toolbarGroup(children(
                               ToolbarButton{.commandId = "edit.undo",
                                             .icon = IconName::Undo},
                               ToolbarButton{.commandId = "edit.redo",
                                             .icon = IconName::Redo})),
                           toolbarDivider(),
                           toolbarGroup(children(
                               ToolbarButton{.commandId = "edit.cut",
                                             .icon = IconName::ContentCut},
                               ToolbarButton{.commandId = "edit.copy",
                                             .icon = IconName::ContentCopy},
                               ToolbarButton{.commandId = "edit.paste",
                                             .icon = IconName::ContentPaste})),
                           toolbarDivider(),
                           toolbarGroup(children(
                               ToolbarButton{.commandId = "editor.find",
                                             .icon = IconName::Search},
                               ToolbarButton{.commandId = "editor.replace",
                                             .icon = IconName::FindReplace},
                               ToolbarButton{.commandId = "editor.goToLine",
                                             .icon = IconName::MoreHoriz})),
                           toolbarDivider(),
                           toolbarGroup(children(
                               ToolbarButton{.commandId = "view.wordWrap",
                                             .icon = IconName::WrapText})),
                           toolbarDivider(),
                           toolbarGroup(children(
                               ToolbarButton{.commandId = "view.zoomOut",
                                             .icon = IconName::ZoomOut},
                               ToolbarButton{.commandId = "view.zoomReset",
                                             .icon = IconName::RestartAlt},
                               ToolbarButton{.commandId = "view.zoomIn",
                                             .icon = IconName::ZoomIn})),
                           Spacer{})}
                       .height(56.f)
                       .padding(5.f, theme().space3, 5.f, theme().space3)
                       .fill(FillStyle::solid(Color::windowBackground()))
                       .stroke(StrokeStyle::solid(Color::separator(), 1.f)),
                   std::move(panel),
                   std::move(editorContent),
                   HStack{
                       .spacing = theme().space2,
                       .alignment = Alignment::Center,
                       .children = children(
                           Text{
                               .text = status,
                               .font = Font{.size = 12.f, .weight = 450.f},
                               .color = Color::secondary(),
                               .verticalAlignment = VerticalAlignment::Center,
                           }.flex(1.f, 1.f, 0.f),
                           Text{
                               .text = [text, selection] {
                                 return std::to_string(text().size()) + " chars, line " +
                                        std::to_string(lineNumberForSelection(text(), selection()));
                               },
                               .font = Font{.size = 12.f, .weight = 450.f},
                               .color = Color::secondary(),
                               .verticalAlignment = VerticalAlignment::Center,
                           })}
                       .padding(8.f, theme().space3, 8.f, theme().space3)
                       .fill(FillStyle::solid(Color::windowBackground()))
                       .stroke(StrokeStyle::solid(Color::separator(), 1.f)))};

    return ZStack{
        .horizontalAlignment = Alignment::Stretch,
        .verticalAlignment = Alignment::Stretch,
        .children = children(
            std::move(mainContent),
            Show(
                commandPaletteOpen,
                [commandPaletteOpen, commandPaletteQuery, commandPaletteItems,
                 commandPaletteTargetKey, commandPaletteSelectedIndex, commandPaletteScrollOffset,
                 commandPaletteFocusGeneration, refreshCommandPalette] {
                  return CommandPalette{
                      .open = commandPaletteOpen,
                      .query = commandPaletteQuery,
                      .items = commandPaletteItems,
                      .targetKey = commandPaletteTargetKey,
                      .selectedIndex = commandPaletteSelectedIndex,
                      .scrollOffset = commandPaletteScrollOffset,
                      .focusGeneration = commandPaletteFocusGeneration,
                      .refresh = refreshCommandPalette,
                  };
                }))};
  }
};

struct EditorAutotestPasteState {
  std::string path;
  std::optional<std::string> payload;
  std::uint64_t timerId = 0;
  int attempts = 0;
  int ticksAfterPaste = 0;
  int exitAfterTicks = 80;
  bool pasted = false;
};

void installEditorAutotestPaste(Application& app, Window& window) {
  char const* pastePath = std::getenv("LAMBDA_EDITOR_AUTOTEST_PASTE_FILE");
  if (!pastePath || !*pastePath) {
    return;
  }

  auto state = std::make_shared<EditorAutotestPasteState>();
  state->path = pastePath;
  state->exitAfterTicks = std::max(1, envPositiveInt("LAMBDA_EDITOR_AUTOTEST_EXIT_AFTER_PASTE_SECONDS", 8) * 10);
  state->timerId = app.scheduleRepeatingTimer(std::chrono::milliseconds{100}, window.handle());
  app.eventQueue().on<TimerEvent>([state, &app, &window](TimerEvent const& event) {
    if (event.timerId != state->timerId) {
      return;
    }

    if (state->pasted) {
      ++state->ticksAfterPaste;
      window.requestRedraw();
      if (state->ticksAfterPaste >= state->exitAfterTicks) {
        std::fprintf(stderr, "lambda-editor-autotest: complete ticks_after_paste=%d\n",
                     state->ticksAfterPaste);
        app.cancelTimer(state->timerId);
        app.quit();
      }
      return;
    }

    ++state->attempts;
    if (!state->payload) {
      state->payload = readWholeFile(state->path);
      if (!state->payload) {
        std::fprintf(stderr, "lambda-editor-autotest: failed to read paste file: %s\n",
                     state->path.c_str());
        app.cancelTimer(state->timerId);
        std::exit(3);
      }
    }

    if (Application::hasInstance()) {
      Application::instance().clipboard().writeText(*state->payload);
    }
    if (!window.isCommandEnabled("edit.paste")) {
      if (state->attempts > 100) {
        std::fprintf(stderr, "lambda-editor-autotest: edit.paste was not enabled after %d attempts\n",
                     state->attempts);
        app.cancelTimer(state->timerId);
        std::exit(4);
      }
      return;
    }

    (void)window.dispatchCommand("edit.selectAll");
    if (!window.dispatchCommand("edit.paste")) {
      if (state->attempts > 100) {
        std::fprintf(stderr, "lambda-editor-autotest: edit.paste dispatch failed after %d attempts\n",
                     state->attempts);
        app.cancelTimer(state->timerId);
        std::exit(5);
      }
      return;
    }

    state->pasted = true;
    window.requestRedraw();
    std::fprintf(stderr, "lambda-editor-autotest: pasted bytes=%zu attempts=%d exit_after_ticks=%d\n",
                 state->payload->size(),
                 state->attempts,
                 state->exitAfterTicks);
  });
}

} // namespace

int main(int argc, char* argv[]) {
  Application app(argc, argv);

  std::string initialPath = argc > 1 ? std::string(argv[1]) : std::string{};
  EditorDocumentResult initial =
      initialPath.empty() ? EditorDocumentResult{} : openDocument(initialPath);

  auto& window = app.createWindow<Window>({
      .size = {920.f, 720.f},
      .title = "Lambda Editor",
      .resizable = true,
  });
  window.registerCommand("app.quit", {
      .title = "Quit",
      .category = "Application",
      .shortcut = shortcuts::Quit,
      .paletteVisible = false,
      .isEnabled = [] { return true; },
  });
  window.setView<LambdaEditor>({
      .initialDocument = initial.ok ? std::move(initial.document) : EditorDocument::untitled(),
      .initialStatus = initial.status,
  });
  installEditorAutotestPaste(app, window);
  return app.exec();
}
