#pragma once

#include "FilesFlowGridLayout.hpp"
#include "FilesGlyphs.hpp"
#include "FilesStore.hpp"
#include "FilesTheme.hpp"

#include <Flux/Reactive/Effect.hpp>
#include <Flux/Reactive/Signal.hpp>
#include <Flux/SceneGraph/SceneNode.hpp>
#include <Flux/UI/Hooks.hpp>
#include <Flux/UI/Layout.hpp>
#include <Flux/UI/MountContext.hpp>
#include <Flux/UI/Views/For.hpp>
#include <Flux/UI/Views/HStack.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lambda_files {

namespace detail {

struct RowDescriptor {
  std::size_t rowIndex = 0;
  int columns = 0;
  std::string key;
  std::vector<FileEntry> entries;

  bool operator==(RowDescriptor const& other) const = default;
};

inline float resolvedLayoutWidth(flux::LayoutConstraints const& constraints) {
  if (std::isfinite(constraints.maxWidth) && constraints.maxWidth > 0.f) {
    return constraints.maxWidth;
  }
  if (std::isfinite(constraints.minWidth) && constraints.minWidth > 0.f) {
    return constraints.minWidth;
  }
  return 0.f;
}

inline std::vector<RowDescriptor> makeRows(std::vector<FileEntry> const& currentEntries,
                                           std::string const& currentListingKey, float layoutWidth,
                                           FilesFlowGridLayout const& metrics) {
  if (layoutWidth <= 0.f) {
    return {};
  }
  int const columns = metrics.columnCountForWidth(layoutWidth);
  int const rowCount = metrics.rowCountForEntries(currentEntries.size(), columns);
  std::vector<RowDescriptor> nextRows;
  nextRows.reserve(static_cast<std::size_t>(rowCount));
  for (int row = 0; row < rowCount; ++row) {
    std::size_t const baseIndex = static_cast<std::size_t>(row) * static_cast<std::size_t>(columns);
    std::size_t const endIndex =
        std::min(baseIndex + static_cast<std::size_t>(columns), currentEntries.size());
    std::vector<FileEntry> rowEntries;
    rowEntries.reserve(endIndex - baseIndex);
    std::string rowKey = currentListingKey + ":" + std::to_string(columns) + ":" + std::to_string(row);
    for (std::size_t index = baseIndex; index < endIndex; ++index) {
      FileEntry const& entry = currentEntries[index];
      rowKey += "\x1f";
      rowKey += entry.path.string();
      rowKey += entry.isDirectory ? ":d:" : ":f:";
      rowKey += std::to_string(static_cast<int>(entry.visualKind));
      rowKey += ":";
      rowKey += std::to_string(entry.size);
      rowEntries.push_back(entry);
    }
    nextRows.push_back(RowDescriptor{
        .rowIndex = static_cast<std::size_t>(row),
        .columns = columns,
        .key = std::move(rowKey),
        .entries = std::move(rowEntries),
    });
  }
  return nextRows;
}

struct GridState {
  flux::Reactive::Signal<float> layoutWidth{0.f};
  flux::Reactive::Signal<std::vector<RowDescriptor>> rows{};
};

/// Forwards relayout constraints into `layoutWidth` so row/column counts track the viewport.
struct GridRelayoutBridge {
  std::shared_ptr<GridState> state;
  flux::Element content;

  flux::Size measure(flux::MeasureContext& ctx, flux::LayoutConstraints const& constraints,
                     flux::LayoutHints const& hints, flux::TextSystem& textSystem) const {
    float const width = resolvedLayoutWidth(constraints);
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
      float const width = resolvedLayoutWidth(constraints);
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

/// Composite grid: `For` of `HStack` rows.
struct FilesFlowGrid {
  flux::Reactive::Signal<std::vector<FileEntry>> entries;
  flux::Reactive::Signal<std::string> listingKey;
  flux::Reactive::Signal<std::string> selectedPath;
  flux::Reactive::Signal<FileSelectionState> selection;
  std::vector<std::filesystem::path> iconThemeRoots;
  int iconSize = 48;
  std::function<void(FileEntry const&)> activateEntry;

  float cellWidth = FilesTheme::kGridMinCell;
  float cellHeight = FilesTheme::kGridTileH;
  float horizontalSpacing = FilesTheme::kGridGapH;
  float verticalSpacing = FilesTheme::kGridGapV;

  mutable std::shared_ptr<detail::GridState> state = std::make_shared<detail::GridState>();

  FilesFlowGridLayout layoutMetrics() const {
    return FilesFlowGridLayout{
        .cellWidth = cellWidth,
        .cellHeight = cellHeight,
        .horizontalSpacing = horizontalSpacing,
        .verticalSpacing = verticalSpacing,
    };
  }

  flux::Size measure(flux::MeasureContext&, flux::LayoutConstraints const& constraints,
                     flux::LayoutHints const&, flux::TextSystem&) const;

  flux::Element body() const;
};

inline flux::Size FilesFlowGrid::measure(flux::MeasureContext&, flux::LayoutConstraints const& constraints,
                                         flux::LayoutHints const&, flux::TextSystem&) const {
  float const width = detail::resolvedLayoutWidth(constraints);
  if (width > 0.f) {
    state->layoutWidth.set(width);
  }
  float const layoutWidth =
      width > 0.f ? width : std::max(0.f, state->layoutWidth.peek());
  return layoutMetrics().contentSizeFor(layoutWidth, entries.peek().size());
}

inline flux::Size measureFilesFlowGrid(FilesFlowGrid const& grid,
                                     flux::LayoutConstraints const& constraints) {
  float const width = detail::resolvedLayoutWidth(constraints);
  if (width > 0.f) {
    grid.state->layoutWidth.set(width);
  }
  float const layoutWidth =
      width > 0.f ? width : std::max(0.f, grid.state->layoutWidth.peek());
  return grid.layoutMetrics().contentSizeFor(layoutWidth, grid.entries.peek().size());
}

inline flux::Element FilesFlowGrid::body() const {
  using namespace flux;

  FilesFlowGridLayout const metrics = layoutMetrics();
  Reactive::Signal<std::vector<FileEntry>> const entriesSignal = entries;
  Reactive::Signal<std::string> const listingKeySignal = listingKey;
  Reactive::Signal<std::string> const selectedPathSignal = selectedPath;
  Reactive::Signal<FileSelectionState> const selectionSignal = selection;
  std::vector<std::filesystem::path> const roots = iconThemeRoots;
  int const preferredIconSize = iconSize;
  auto const activate = activateEntry;
  float const tileW = cellWidth;
  float const tileH = cellHeight;
  float const gapH = horizontalSpacing;
  float const rowGap = verticalSpacing;

  std::shared_ptr<detail::GridState> const gridState = state;
  LayoutConstraints const* constraints = useLayoutConstraints();
  if (constraints != nullptr) {
    float const width = detail::resolvedLayoutWidth(*constraints);
    if (width > 0.f) {
      gridState->layoutWidth.set(width);
    }
  }

  Reactive::Effect([entriesSignal, listingKeySignal, gridState, metrics] {
    (void)entriesSignal();
    (void)listingKeySignal();
    (void)gridState->layoutWidth();
    float const width = std::max(0.f, gridState->layoutWidth.peek());
    gridState->rows.set(
        detail::makeRows(entriesSignal.peek(), listingKeySignal.peek(), width, metrics));
  });

  return Element{detail::GridRelayoutBridge{
      .state = gridState,
      .content = Element{For(
      gridState->rows,
      [](detail::RowDescriptor const& row) {
        return row.key;
      },
      [selectedPathSignal, selectionSignal, roots, preferredIconSize, activate, tileW, tileH, gapH](
          detail::RowDescriptor const& row,
          Signal<std::size_t> const&) {
        int const colCount = std::max(1, row.columns);
        std::vector<Element> cells;
        cells.reserve(static_cast<std::size_t>(colCount));
        for (FileEntry const& entry : row.entries) {
          Reactive::Bindable<bool> selected{
              [selectedPathSignal, selectionSignal, entry] {
                FileSelectionState const current = selectionSignal();
                if (!current.selected.empty()) return current.contains(entry.path);
                return selectedPathSignal() == entry.path.string();
              }};
          cells.push_back(Element{FileItemTile{
                                      .entry = entry,
                                      .iconPath =
                                          resolveFileIcon(roots, entry.path, entry.isDirectory, preferredIconSize)
                                              .themePath.string(),
                                      .selected = selected,
                                      .onActivate = [activate, entry] {
                                        if (activate) {
                                          activate(entry);
                                        }
                                      },
                                  }}
                              .size(tileW, tileH)
                              .clipContent(true));
        }
        return HStack{
            .spacing = gapH,
            .alignment = Alignment::Start,
            .children = std::move(cells),
        };
      },
      rowGap,
      Alignment::Start,
      ForLayout::VerticalStack)},
  }};
}

} // namespace lambda_files
