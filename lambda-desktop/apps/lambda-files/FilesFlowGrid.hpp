#pragma once

#include "FilesFlowGridLayout.hpp"
#include "FilesGlyphs.hpp"
#include "FilesStore.hpp"
#include "FilesTheme.hpp"
#include "FilesTrace.hpp"

#include <Lambda/Reactive/Effect.hpp>
#include <Lambda/Reactive/Signal.hpp>
#include <Lambda/SceneGraph/SceneNode.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/Layout.hpp>
#include <Lambda/UI/MountContext.hpp>
#include <Lambda/UI/Views/For.hpp>
#include <Lambda/UI/Views/HStack.hpp>

#include <algorithm>
#include <chrono>
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

inline float resolvedLayoutWidth(lambda::LayoutConstraints const& constraints) {
  if (std::isfinite(constraints.maxWidth) && constraints.maxWidth > 0.f) {
    return constraints.maxWidth;
  }
  if (std::isfinite(constraints.minWidth) && constraints.minWidth > 0.f) {
    return constraints.minWidth;
  }
  return 0.f;
}

inline std::vector<RowDescriptor> makeRowsForColumns(std::vector<FileEntry> const& currentEntries,
                                                     std::string const& currentListingKey,
                                                     int columns, float layoutWidth) {
  double const startMs = trace::nowMs();
  if (columns <= 0) {
    LAMBDA_FILES_TRACE_EVENT("flow-grid make-rows entries=%zu width=%.1f columns=0 rows=0 elapsed=%.3fms\n",
                 currentEntries.size(),
                 layoutWidth,
                 trace::nowMs() - startMs);
    return {};
  }
  int const rowCount = FilesFlowGridLayout::rowCountForEntries(currentEntries.size(), columns);
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
  LAMBDA_FILES_TRACE_EVENT("flow-grid make-rows entries=%zu width=%.1f columns=%d rows=%zu elapsed=%.3fms\n",
               currentEntries.size(),
               layoutWidth,
               columns,
               nextRows.size(),
               trace::nowMs() - startMs);
  return nextRows;
}

inline std::vector<RowDescriptor> makeRows(std::vector<FileEntry> const& currentEntries,
                                           std::string const& currentListingKey, float layoutWidth,
                                           FilesFlowGridLayout const& metrics) {
  return makeRowsForColumns(currentEntries,
                            currentListingKey,
                            metrics.columnCountForWidth(layoutWidth),
                            layoutWidth);
}

struct GridState {
  float layoutWidth = 0.f;
  lambda::Reactive::Signal<int> columns{0};
  lambda::Reactive::Signal<std::vector<RowDescriptor>> rows{};
};

inline void updateGridLayout(GridState& state, float width, FilesFlowGridLayout const& metrics) {
  if (!std::isfinite(width) || width <= 0.f) {
    return;
  }
  state.layoutWidth = width;
  int const columns = metrics.columnCountForWidth(width);
  if (columns != state.columns.peek()) {
    state.columns.set(columns);
  }
}

/// Forwards relayout constraints into the grid state so row/column counts track the viewport.
struct GridRelayoutBridge {
  std::shared_ptr<GridState> state;
  FilesFlowGridLayout metrics;
  lambda::Element content;

  lambda::Size measure(lambda::MeasureContext& ctx, lambda::LayoutConstraints const& constraints,
                     lambda::LayoutHints const& hints, lambda::TextSystem& textSystem) const {
    double const startMs = trace::nowMs();
    float const width = resolvedLayoutWidth(constraints);
    updateGridLayout(*state, width, metrics);
    lambda::Size const size = content.measure(ctx, constraints, hints, textSystem);
    LAMBDA_FILES_TRACE_EVENT("flow-grid bridge-measure width=%.1f rows=%zu size=%.1fx%.1f elapsed=%.3fms\n",
                 width,
                 state->rows.peek().size(),
                 size.width,
                 size.height,
                 trace::nowMs() - startMs);
    return size;
  }

  std::unique_ptr<lambda::scenegraph::SceneNode> mount(lambda::MountContext& ctx) const {
    auto wrapper = std::make_unique<lambda::scenegraph::SceneNode>(lambda::Rect{});
    std::unique_ptr<lambda::scenegraph::SceneNode> child = content.mount(ctx);
    if (!child) {
      return wrapper;
    }
    lambda::scenegraph::SceneNode* rawChild = child.get();
    lambda::scenegraph::SceneNode* rawWrapper = wrapper.get();
    wrapper->appendChild(std::move(child));
    wrapper->setRelayout([state = state, metrics = metrics, rawChild, rawWrapper](
                             lambda::LayoutConstraints const& constraints) {
      double const startMs = trace::nowMs();
      float const width = resolvedLayoutWidth(constraints);
      updateGridLayout(*state, width, metrics);
      (void)rawChild->relayout(constraints);
      rawWrapper->setSize(rawChild->size());
      lambda::Size const size = rawChild->size();
      LAMBDA_FILES_TRACE_EVENT("flow-grid bridge-relayout width=%.1f rows=%zu size=%.1fx%.1f elapsed=%.3fms\n",
                   width,
                   state->rows.peek().size(),
                   size.width,
                   size.height,
                   trace::nowMs() - startMs);
    });
    return wrapper;
  }
};

} // namespace detail

/// Composite grid: `For` of `HStack` rows.
struct FilesFlowGrid {
  lambda::Reactive::Signal<std::vector<FileEntry>> entries;
  lambda::Reactive::Signal<std::string> listingKey;
  lambda::Reactive::Signal<std::string> selectedPath;
  lambda::Reactive::Signal<FileSelectionState> selection;
  std::vector<std::filesystem::path> iconThemeRoots;
  int iconSize = 48;
  std::function<void(FileEntry const&)> activateEntry;
  std::function<void(FileEntry const&, lambda::Modifiers)> tapEntry;
  std::function<void(FileEntry const&)> showEntryContextMenu;

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

  lambda::Size measure(lambda::MeasureContext&, lambda::LayoutConstraints const& constraints,
                     lambda::LayoutHints const&, lambda::TextSystem&) const;

  lambda::Element body() const;
};

inline lambda::Size FilesFlowGrid::measure(lambda::MeasureContext&, lambda::LayoutConstraints const& constraints,
                                         lambda::LayoutHints const&, lambda::TextSystem&) const {
  double const startMs = trace::nowMs();
  float const width = detail::resolvedLayoutWidth(constraints);
  detail::updateGridLayout(*state, width, layoutMetrics());
  float const layoutWidth = width > 0.f ? width : std::max(0.f, state->layoutWidth);
  std::size_t const entryCount = entries.peek().size();
  lambda::Size const size = layoutMetrics().contentSizeFor(layoutWidth, entryCount);
  LAMBDA_FILES_TRACE_EVENT("flow-grid measure entries=%zu width=%.1f layoutWidth=%.1f size=%.1fx%.1f elapsed=%.3fms\n",
               entryCount,
               width,
               layoutWidth,
               size.width,
               size.height,
               trace::nowMs() - startMs);
  return size;
}

inline lambda::Size measureFilesFlowGrid(FilesFlowGrid const& grid,
                                     lambda::LayoutConstraints const& constraints) {
  double const startMs = trace::nowMs();
  float const width = detail::resolvedLayoutWidth(constraints);
  detail::updateGridLayout(*grid.state, width, grid.layoutMetrics());
  float const layoutWidth = width > 0.f ? width : std::max(0.f, grid.state->layoutWidth);
  std::size_t const entryCount = grid.entries.peek().size();
  lambda::Size const size = grid.layoutMetrics().contentSizeFor(layoutWidth, entryCount);
  LAMBDA_FILES_TRACE_EVENT("flow-grid test-measure entries=%zu width=%.1f layoutWidth=%.1f size=%.1fx%.1f elapsed=%.3fms\n",
               entryCount,
               width,
               layoutWidth,
               size.width,
               size.height,
               trace::nowMs() - startMs);
  return size;
}

inline lambda::Element FilesFlowGrid::body() const {
  using namespace lambda;

  FilesFlowGridLayout const metrics = layoutMetrics();
  Reactive::Signal<std::vector<FileEntry>> const entriesSignal = entries;
  Reactive::Signal<std::string> const listingKeySignal = listingKey;
  Reactive::Signal<std::string> const selectedPathSignal = selectedPath;
  Reactive::Signal<FileSelectionState> const selectionSignal = selection;
  std::vector<std::filesystem::path> const roots = iconThemeRoots;
  int const preferredIconSize = iconSize;
  auto const activate = activateEntry;
  auto const tap = tapEntry;
  auto const contextMenu = showEntryContextMenu;
  float const tileW = cellWidth;
  float const tileH = cellHeight;
  float const gapH = horizontalSpacing;
  float const rowGap = verticalSpacing;

  std::shared_ptr<detail::GridState> const gridState = state;
  LayoutConstraints const* constraints = useLayoutConstraints();
  if (constraints != nullptr) {
    float const width = detail::resolvedLayoutWidth(*constraints);
    detail::updateGridLayout(*gridState, width, metrics);
  }

  Reactive::Effect([entriesSignal, listingKeySignal, gridState] {
    double const startMs = trace::nowMs();
    (void)entriesSignal();
    (void)listingKeySignal();
    (void)gridState->columns();
    int const columns = gridState->columns.peek();
    float const width = std::max(0.f, gridState->layoutWidth);
    std::vector<detail::RowDescriptor> nextRows =
        detail::makeRowsForColumns(entriesSignal.peek(), listingKeySignal.peek(), columns, width);
    bool const changed = nextRows != gridState->rows.peek();
    if (changed) {
      gridState->rows.set(std::move(nextRows));
    }
    LAMBDA_FILES_TRACE_EVENT("flow-grid rows-effect entries=%zu width=%.1f columns=%d rows=%zu elapsed=%.3fms\n",
                 entriesSignal.peek().size(),
                 width,
                 columns,
                 gridState->rows.peek().size(),
                 trace::nowMs() - startMs);
  });

  return Element{detail::GridRelayoutBridge{
      .state = gridState,
      .metrics = metrics,
      .content = Element{For(
      gridState->rows,
      [](detail::RowDescriptor const& row) {
        return row.key;
      },
      [selectedPathSignal, selectionSignal, roots, preferredIconSize, activate, tap, contextMenu, tileW, tileH, gapH](
          detail::RowDescriptor const& row,
          Signal<std::size_t> const&) {
        int const colCount = std::max(1, row.columns);
        std::vector<Element> cells;
        cells.reserve(static_cast<std::size_t>(colCount));
        double const rowStartMs = trace::nowMs();
        double iconResolveMs = 0.0;
        for (FileEntry const& entry : row.entries) {
          Reactive::Bindable<bool> selected{
              [selectedPathSignal, selectionSignal, entry] {
                FileSelectionState const current = selectionSignal();
                if (!current.selected.empty()) return current.contains(entry.path);
                return selectedPathSignal() == entry.path.string();
              }};
          double const iconStartMs = trace::nowMs();
          FileIconLookup const icon = resolveFileIcon(roots, entry.path, entry.isDirectory, preferredIconSize);
          iconResolveMs += trace::nowMs() - iconStartMs;
          cells.push_back(Element{FileItemTile{
                                      .entry = entry,
                                      .iconPath = icon.themePath.string(),
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
                                  }}
                              .size(tileW, tileH)
                              .clipContent(true));
        }
        LAMBDA_FILES_TRACE_EVENT("flow-grid row-body row=%zu entries=%zu columns=%d iconResolve=%.3fms elapsed=%.3fms\n",
                     row.rowIndex,
                     row.entries.size(),
                     colCount,
                     iconResolveMs,
                     trace::nowMs() - rowStartMs);
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
