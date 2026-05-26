#pragma once

#include "FilesGlyphs.hpp"
#include "FilesStore.hpp"
#include "FilesTheme.hpp"

#include <Flux/Graphics/ImageFillMode.hpp>
#include <Flux/Reactive/Bindable.hpp>
#include <Flux/Reactive/Signal.hpp>
#include <Flux/SceneGraph/SceneNode.hpp>
#include <Flux/UI/Hooks.hpp>
#include <Flux/UI/IconName.hpp>
#include <Flux/UI/Layout.hpp>
#include <Flux/UI/MountContext.hpp>
#include <Flux/UI/Views/For.hpp>
#include <Flux/UI/Views/HStack.hpp>
#include <Flux/UI/Views/Icon.hpp>
#include <Flux/UI/Views/Image.hpp>
#include <Flux/UI/Views/Rectangle.hpp>
#include <Flux/UI/Views/Text.hpp>
#include <Flux/UI/Views/VStack.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lambda_files {

namespace detail {

inline float resolvedFilesListWidth(flux::LayoutConstraints const& constraints) {
  if (std::isfinite(constraints.maxWidth) && constraints.maxWidth > 0.f) {
    return constraints.maxWidth;
  }
  if (std::isfinite(constraints.minWidth) && constraints.minWidth > 0.f) {
    return constraints.minWidth;
  }
  return 0.f;
}

inline std::string listRowKey(FileEntry const& entry) {
  std::string key = entry.path.string();
  key += entry.isDirectory ? ":d:" : ":f:";
  key += std::to_string(static_cast<int>(entry.visualKind));
  key += ":";
  key += std::to_string(entry.size);
  return key;
}

inline std::string listKindLabel(FileEntry const& entry) {
  if (entry.isDirectory) {
    return "Folder";
  }
  switch (entry.visualKind) {
  case FileVisualKind::Folder:
    return "Folder";
  case FileVisualKind::Pdf:
    return "PDF";
  case FileVisualKind::Image:
    return "Image";
  case FileVisualKind::Presentation:
    return "Presentation";
  case FileVisualKind::Sketch:
    return "Sketch";
  case FileVisualKind::Generic:
    return "File";
  }
  return "File";
}

inline std::string listSizeLabel(FileEntry const& entry) {
  return entry.isDirectory ? std::string{"--"} : formatEntrySubtitle(entry);
}

struct FilesListState {
  flux::Reactive::Signal<float> layoutWidth{0.f};
};

struct FilesListRelayoutBridge {
  std::shared_ptr<FilesListState> state;
  flux::Element content;

  flux::Size measure(flux::MeasureContext& ctx, flux::LayoutConstraints const& constraints,
                     flux::LayoutHints const& hints, flux::TextSystem& textSystem) const {
    float const width = resolvedFilesListWidth(constraints);
    if (width > 0.f) {
      state->layoutWidth.set(width);
    }
    return content.measure(ctx, constraints, hints, textSystem);
  }

  std::unique_ptr<flux::scenegraph::SceneNode> mount(flux::MountContext& ctx) const {
    auto wrapper = std::make_unique<flux::scenegraph::SceneNode>(flux::Rect{});
    std::unique_ptr<flux::scenegraph::SceneNode> child = content.mount(ctx);
    if (!child) {
      return wrapper;
    }
    flux::scenegraph::SceneNode* rawChild = child.get();
    flux::scenegraph::SceneNode* rawWrapper = wrapper.get();
    wrapper->appendChild(std::move(child));
    wrapper->setRelayout([state = state, rawChild, rawWrapper](flux::LayoutConstraints const& constraints) {
      float const width = resolvedFilesListWidth(constraints);
      if (width > 0.f) {
        state->layoutWidth.set(width);
      }
      (void)rawChild->relayout(constraints);
      rawWrapper->setSize(rawChild->size());
    });
    return wrapper;
  }
};

} // namespace detail

struct FileListIcon {
  FileEntry entry;
  std::string iconPath;

  flux::Element body() const {
    using namespace flux;
    if (auto image = detail::themedFileIconImage(iconPath)) {
      return Element{views::Image{
                 .source = std::move(image),
                 .fillMode = ImageFillMode::Fit,
             }}
          .size(30.f, 30.f);
    }
    return Rectangle{}
        .size(30.f, 30.f)
        .fill(entry.isDirectory ? FilesTheme::selectFill : FilesTheme::glassSoft)
        .stroke(StrokeStyle::solid(FilesTheme::line, 0.5f))
        .cornerRadius(7.f)
        .overlay(Icon{
            .name = entry.isDirectory ? IconName::FolderOpen : IconName::Description,
            .size = 18.f,
            .color = entry.isDirectory ? FilesTheme::accent : FilesTheme::text2,
        });
  }
};

struct FilesListHeaderRow {
  flux::Reactive::Bindable<float> width{0.f};

  flux::Element body() const {
    using namespace flux;
    return HStack{
               .spacing = 12.f,
               .alignment = Alignment::Center,
               .children = children(
                   Rectangle{}.width(42.f),
                   Text{
                       .text = "Name",
                       .font = Font{.size = 11.f, .weight = 650.f},
                       .color = FilesTheme::text3,
                       .maxLines = 1,
                   }
                       .flex(1.f, 1.f, 0.f),
                   Text{
                       .text = "Kind",
                       .font = Font{.size = 11.f, .weight = 650.f},
                       .color = FilesTheme::text3,
                       .maxLines = 1,
                   }
                       .width(116.f),
                   Text{
                       .text = "Size",
                       .font = Font{.size = 11.f, .weight = 650.f},
                       .color = FilesTheme::text3,
                       .horizontalAlignment = HorizontalAlignment::Trailing,
                       .maxLines = 1,
                   }
                       .width(86.f))}
        .height(FilesTheme::kListHeaderHeight)
        .padding(0.f, 10.f, 0.f, 10.f)
        .width(width);
  }
};

struct FileListRowView {
  FileEntry entry;
  std::string iconPath;
  flux::Reactive::Bindable<float> width{0.f};
  flux::Reactive::Bindable<bool> selected{false};
  std::function<void(flux::Modifiers)> onActivate;
  std::function<void()> onContextMenu;

  flux::Element body() const {
    using namespace flux;
    auto hover = useHover();
    Reactive::Bindable<bool> const selectedBinding = selected;

    Reactive::Bindable<FillStyle> const rowFill{[hover, selectedBinding] {
      if (selectedBinding.evaluate()) {
        return FillStyle::solid(FilesTheme::selectFill);
      }
      if (hover()) {
        return FillStyle::solid(FilesTheme::hoverFill);
      }
      return FillStyle::solid(Colors::transparent);
    }};
    Reactive::Bindable<StrokeStyle> const rowStroke{[selectedBinding] {
      if (selectedBinding.evaluate()) {
        return StrokeStyle::solid(FilesTheme::selectBorder, 1.f);
      }
      return StrokeStyle::solid(Colors::transparent, 0.f);
    }};

    Element row = HStack{
                      .spacing = 12.f,
                      .alignment = Alignment::Center,
                      .children = children(
                          Element{FileListIcon{.entry = entry, .iconPath = iconPath}}.width(42.f),
                          Text{
                              .text = entry.name,
                              .font = Font{.size = 13.f, .weight = 520.f},
                              .color = FilesTheme::text,
                              .maxLines = 1,
                          }
                              .flex(1.f, 1.f, 0.f),
                          Text{
                              .text = detail::listKindLabel(entry),
                              .font = Font{.size = 12.f},
                              .color = FilesTheme::text2,
                              .maxLines = 1,
                          }
                              .width(116.f),
                          Text{
                              .text = detail::listSizeLabel(entry),
                              .font = Font{.size = 12.f},
                              .color = FilesTheme::text2,
                              .horizontalAlignment = HorizontalAlignment::Trailing,
                              .maxLines = 1,
                          }
                              .width(86.f))}
        .height(FilesTheme::kListRowHeight)
        .padding(0.f, 10.f, 0.f, 10.f)
        .width(width)
        .fill(rowFill)
        .stroke(rowStroke)
        .cornerRadius(FilesTheme::kListRowRadius)
        .clipContent(true);

    if (onActivate || onContextMenu) {
      auto activate = onActivate;
      auto contextMenu = onContextMenu;
      row = std::move(row).onTap([activate, contextMenu](MouseButton button, Modifiers modifiers) {
        if (button == MouseButton::Left && activate) {
          activate(modifiers);
        } else if (button == MouseButton::Right && contextMenu) {
          contextMenu();
        }
      });
    }
    return row;
  }
};

struct FilesListView {
  flux::Reactive::Signal<std::vector<FileEntry>> entries;
  flux::Reactive::Signal<std::string> selectedPath;
  flux::Reactive::Signal<FileSelectionState> selection;
  std::vector<std::filesystem::path> iconThemeRoots;
  int iconSize = 48;
  std::function<void(FileEntry const&)> activateEntry;
  std::function<void(FileEntry const&, flux::Modifiers)> tapEntry;
  std::function<void(FileEntry const&)> showEntryContextMenu;

  mutable std::shared_ptr<detail::FilesListState> state = std::make_shared<detail::FilesListState>();

  flux::Size measure(flux::MeasureContext&, flux::LayoutConstraints const& constraints,
                     flux::LayoutHints const&, flux::TextSystem&) const {
    float const width = detail::resolvedFilesListWidth(constraints);
    if (width > 0.f) {
      state->layoutWidth.set(width);
    }
    std::size_t const count = entries.peek().size();
    float const rowsHeight = static_cast<float>(count) * FilesTheme::kListRowHeight;
    float const gaps = count > 0 ? static_cast<float>(count) * FilesTheme::kListRowGap : 0.f;
    return flux::Size{width, FilesTheme::kListHeaderHeight + rowsHeight + gaps};
  }

  flux::Element body() const {
    using namespace flux;

    Reactive::Signal<std::vector<FileEntry>> const entriesSignal = entries;
    Reactive::Signal<std::string> const selectedPathSignal = selectedPath;
    Reactive::Signal<FileSelectionState> const selectionSignal = selection;
    std::vector<std::filesystem::path> const roots = iconThemeRoots;
    int const preferredIconSize = iconSize;
    auto const activate = activateEntry;
    auto const tap = tapEntry;
    auto const contextMenu = showEntryContextMenu;
    std::shared_ptr<detail::FilesListState> const listState = state;

    LayoutConstraints const* constraints = useLayoutConstraints();
    if (constraints != nullptr) {
      float const width = detail::resolvedFilesListWidth(*constraints);
      if (width > 0.f) {
        listState->layoutWidth.set(width);
      }
    }

    Reactive::Bindable<float> const listWidth{[listState] {
      return std::max(0.f, listState->layoutWidth());
    }};

    return Element{detail::FilesListRelayoutBridge{
        .state = listState,
        .content = VStack{
            .spacing = FilesTheme::kListRowGap,
            .alignment = Alignment::Stretch,
            .children = children(
                Element{FilesListHeaderRow{.width = listWidth}},
                Element{For(
                    entriesSignal,
                    [](FileEntry const& entry) {
                      return detail::listRowKey(entry);
                    },
                    [selectedPathSignal, selectionSignal, roots, preferredIconSize, activate, tap, contextMenu,
                     listWidth](FileEntry const& entry, Signal<std::size_t> const&) {
                      Reactive::Bindable<bool> selected{
                          [selectedPathSignal, selectionSignal, entry] {
                            FileSelectionState const current = selectionSignal();
                            if (!current.selected.empty()) return current.contains(entry.path);
                            return selectedPathSignal() == entry.path.string();
                          }};
                      return FileListRowView{
                          .entry = entry,
                          .iconPath =
                              resolveFileIcon(roots, entry.path, entry.isDirectory, preferredIconSize)
                                  .themePath.string(),
                          .width = listWidth,
                          .selected = selected,
                          .onActivate = [activate, tap, entry](Modifiers modifiers) {
                            if (tap) {
                              tap(entry, modifiers);
                            } else if (activate) {
                              activate(entry);
                            }
                          },
                          .onContextMenu = [contextMenu, entry] {
                            if (contextMenu) {
                              contextMenu(entry);
                            }
                          },
                      };
                    },
                    FilesTheme::kListRowGap,
                    Alignment::Stretch,
                    ForLayout::VerticalStack)})}}};
  }
};

inline flux::Size measureFilesListView(FilesListView const& list,
                                       flux::LayoutConstraints const& constraints) {
  float const width = detail::resolvedFilesListWidth(constraints);
  if (width > 0.f) {
    list.state->layoutWidth.set(width);
  }
  std::size_t const count = list.entries.peek().size();
  float const rowsHeight = static_cast<float>(count) * FilesTheme::kListRowHeight;
  float const gaps = count > 0 ? static_cast<float>(count) * FilesTheme::kListRowGap : 0.f;
  return flux::Size{width, FilesTheme::kListHeaderHeight + rowsHeight + gaps};
}

} // namespace lambda_files
