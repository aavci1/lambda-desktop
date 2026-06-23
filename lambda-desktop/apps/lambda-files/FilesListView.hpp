#pragma once

#include "FilesGlyphs.hpp"
#include "FilesStore.hpp"
#include "FilesTheme.hpp"
#include "FilesTrace.hpp"

#include <Lambda/Graphics/ImageFillMode.hpp>
#include <Lambda/Reactive/Bindable.hpp>
#include <Lambda/Reactive/Signal.hpp>
#include <Lambda/SceneGraph/SceneNode.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/IconName.hpp>
#include <Lambda/UI/Layout.hpp>
#include <Lambda/UI/MountContext.hpp>
#include <Lambda/UI/Views/For.hpp>
#include <Lambda/UI/Views/HStack.hpp>
#include <Lambda/UI/Views/Icon.hpp>
#include <Lambda/UI/Views/Image.hpp>
#include <Lambda/UI/Views/Rectangle.hpp>
#include <Lambda/UI/Views/Text.hpp>
#include <Lambda/UI/Views/VStack.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lambda_files {

namespace detail {

inline float resolvedFilesListWidth(lambdaui::LayoutConstraints const& constraints) {
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
  lambdaui::Reactive::Signal<float> layoutWidth{0.f};
};

struct FilesListRelayoutBridge {
  std::shared_ptr<FilesListState> state;
  lambdaui::Element content;

  lambdaui::Size measure(lambdaui::MeasureContext& ctx, lambdaui::LayoutConstraints const& constraints,
                     lambdaui::LayoutHints const& hints, lambdaui::TextSystem& textSystem) const {
    double const startMs = trace::nowMs();
    float const width = resolvedFilesListWidth(constraints);
    if (width > 0.f) {
      state->layoutWidth.set(width);
    }
    lambdaui::Size const size = content.measure(ctx, constraints, hints, textSystem);
    LAMBDA_FILES_TRACE_EVENT("list bridge-measure width=%.1f size=%.1fx%.1f elapsed=%.3fms\n",
                 width,
                 size.width,
                 size.height,
                 trace::nowMs() - startMs);
    return size;
  }

  std::unique_ptr<lambdaui::scenegraph::SceneNode> mount(lambdaui::MountContext& ctx) const {
    auto wrapper = std::make_unique<lambdaui::scenegraph::SceneNode>(lambdaui::Rect{});
    std::unique_ptr<lambdaui::scenegraph::SceneNode> child = content.mount(ctx);
    if (!child) {
      return wrapper;
    }
    lambdaui::scenegraph::SceneNode* rawChild = child.get();
    lambdaui::scenegraph::SceneNode* rawWrapper = wrapper.get();
    wrapper->appendChild(std::move(child));
    wrapper->setRelayout([state = state, rawChild, rawWrapper](lambdaui::LayoutConstraints const& constraints) {
      double const startMs = trace::nowMs();
      float const width = resolvedFilesListWidth(constraints);
      if (width > 0.f) {
        state->layoutWidth.set(width);
      }
      (void)rawChild->relayout(constraints);
      rawWrapper->setSize(rawChild->size());
      lambdaui::Size const size = rawChild->size();
      LAMBDA_FILES_TRACE_EVENT("list bridge-relayout width=%.1f size=%.1fx%.1f elapsed=%.3fms\n",
                   width,
                   size.width,
                   size.height,
                   trace::nowMs() - startMs);
    });
    return wrapper;
  }
};

} // namespace detail

struct FileListIcon {
  FileEntry entry;
  std::string iconPath;

  lambdaui::Element body() const {
    using namespace lambdaui;
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
  lambdaui::Reactive::Bindable<float> width{0.f};

  lambdaui::Element body() const {
    using namespace lambdaui;
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
  lambdaui::Reactive::Bindable<float> width{0.f};
  lambdaui::Reactive::Bindable<bool> selected{false};
  std::function<void(lambdaui::Modifiers)> onActivate;
  std::function<void()> onContextMenu;

  lambdaui::Element body() const {
    using namespace lambdaui;
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
  lambdaui::Reactive::Signal<std::vector<FileEntry>> entries;
  lambdaui::Reactive::Signal<std::string> selectedPath;
  lambdaui::Reactive::Signal<FileSelectionState> selection;
  std::vector<std::filesystem::path> iconThemeRoots;
  int iconSize = 48;
  std::function<void(FileEntry const&)> activateEntry;
  std::function<void(FileEntry const&, lambdaui::Modifiers)> tapEntry;
  std::function<void(FileEntry const&)> showEntryContextMenu;

  mutable std::shared_ptr<detail::FilesListState> state = std::make_shared<detail::FilesListState>();

  lambdaui::Size measure(lambdaui::MeasureContext&, lambdaui::LayoutConstraints const& constraints,
                     lambdaui::LayoutHints const&, lambdaui::TextSystem&) const {
    double const startMs = trace::nowMs();
    float const width = detail::resolvedFilesListWidth(constraints);
    if (width > 0.f) {
      state->layoutWidth.set(width);
    }
    std::size_t const count = entries.peek().size();
    float const rowsHeight = static_cast<float>(count) * FilesTheme::kListRowHeight;
    float const gaps = count > 0 ? static_cast<float>(count) * FilesTheme::kListRowGap : 0.f;
    lambdaui::Size const size{width, FilesTheme::kListHeaderHeight + rowsHeight + gaps};
    LAMBDA_FILES_TRACE_EVENT("list measure entries=%zu width=%.1f size=%.1fx%.1f elapsed=%.3fms\n",
                 count,
                 width,
                 size.width,
                 size.height,
                 trace::nowMs() - startMs);
    return size;
  }

  lambdaui::Element body() const {
    using namespace lambdaui;

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
                      double const startMs = trace::nowMs();
                      Reactive::Bindable<bool> selected{
                          [selectedPathSignal, selectionSignal, entry] {
                            FileSelectionState const current = selectionSignal();
                            if (!current.selected.empty()) return current.contains(entry.path);
                            return selectedPathSignal() == entry.path.string();
                          }};
                      double const iconStartMs = trace::nowMs();
                      FileIconLookup const icon =
                          resolveFileIcon(roots, entry.path, entry.isDirectory, preferredIconSize);
                      double const iconMs = trace::nowMs() - iconStartMs;
                      LAMBDA_FILES_TRACE_EVENT("list row-body path=\"%s\" iconResolve=%.3fms elapsed=%.3fms\n",
                                   entry.path.string().c_str(),
                                   iconMs,
                                   trace::nowMs() - startMs);
                      return FileListRowView{
                          .entry = entry,
                          .iconPath = icon.themePath.string(),
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

inline lambdaui::Size measureFilesListView(FilesListView const& list,
                                       lambdaui::LayoutConstraints const& constraints) {
  double const startMs = trace::nowMs();
  float const width = detail::resolvedFilesListWidth(constraints);
  if (width > 0.f) {
    list.state->layoutWidth.set(width);
  }
  std::size_t const count = list.entries.peek().size();
  float const rowsHeight = static_cast<float>(count) * FilesTheme::kListRowHeight;
  float const gaps = count > 0 ? static_cast<float>(count) * FilesTheme::kListRowGap : 0.f;
  lambdaui::Size const size{width, FilesTheme::kListHeaderHeight + rowsHeight + gaps};
  LAMBDA_FILES_TRACE_EVENT("list test-measure entries=%zu width=%.1f size=%.1fx%.1f elapsed=%.3fms\n",
               count,
               width,
               size.width,
               size.height,
               trace::nowMs() - startMs);
  return size;
}

} // namespace lambda_files
