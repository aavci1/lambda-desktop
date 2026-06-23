#include <Lambda/UI/Views/Grid.hpp>

#include <Lambda/SceneGraph/SceneNode.hpp>
#include <Lambda/UI/MeasureContext.hpp>
#include <Lambda/UI/MountContext.hpp>
#include <Lambda/UI/Detail/MountPosition.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

namespace lambdaui {

namespace {

constexpr float kGridLayoutEpsilon = 1e-4f;

std::size_t clampedColumns(std::size_t columns) {
  return std::max<std::size_t>(1, columns);
}

std::size_t colSpanFor(Grid const& grid, std::size_t index) {
  if (index < grid.children.size()) {
    return std::max<std::size_t>(1, std::min(grid.children[index].colSpan(),
                                             clampedColumns(grid.columns)));
  }
  return 1;
}

std::size_t rowSpanFor(Grid const& grid, std::size_t index) {
  if (index < grid.children.size()) {
    return std::max<std::size_t>(1, grid.children[index].rowSpan());
  }
  return 1;
}

LayoutConstraints fixedConstraints(Size size) {
  return LayoutConstraints{
      .maxWidth = std::max(0.f, size.width),
      .maxHeight = std::max(0.f, size.height),
      .minWidth = std::max(0.f, size.width),
      .minHeight = std::max(0.f, size.height),
  };
}

Size measureChild(Element const& child, MeasureContext& ctx, LayoutConstraints const& constraints,
                  TextSystem& textSystem) {
  ctx.pushConstraints(constraints, LayoutHints{});
  Size size = child.measure(ctx, constraints, LayoutHints{}, textSystem);
  ctx.popConstraints();
  return size;
}

float alignedOffset(float cellExtent, float childExtent, Alignment alignment) {
  switch (alignment) {
  case Alignment::Start:
  case Alignment::Stretch:
    return 0.f;
  case Alignment::Center:
    return std::max(0.f, (cellExtent - childExtent) * 0.5f);
  case Alignment::End:
    return std::max(0.f, cellExtent - childExtent);
  }
  return 0.f;
}

struct GridPlan {
  Size size{};
  std::vector<Rect> slots;
};

struct GridPlacement {
  std::size_t row = 0;
  std::size_t column = 0;
  std::size_t colSpan = 1;
  std::size_t rowSpan = 1;
};

using OccupancyGrid = std::vector<std::vector<bool>>;

bool canOccupy(OccupancyGrid const& occupied, std::size_t row, std::size_t column,
               std::size_t colSpan, std::size_t rowSpan, std::size_t columns) {
  if (column + colSpan > columns) {
    return false;
  }
  for (std::size_t r = row; r < row + rowSpan; ++r) {
    if (r >= occupied.size()) {
      continue;
    }
    for (std::size_t c = column; c < column + colSpan; ++c) {
      if (occupied[r][c]) {
        return false;
      }
    }
  }
  return true;
}

void markOccupied(OccupancyGrid& occupied, GridPlacement const& placement, std::size_t columns) {
  std::size_t const requiredRows = placement.row + placement.rowSpan;
  while (occupied.size() < requiredRows) {
    occupied.push_back(std::vector<bool>(columns, false));
  }
  for (std::size_t r = placement.row; r < placement.row + placement.rowSpan; ++r) {
    for (std::size_t c = placement.column; c < placement.column + placement.colSpan; ++c) {
      occupied[r][c] = true;
    }
  }
}

std::vector<GridPlacement> placeGridItems(Grid const& grid, std::size_t count) {
  std::size_t const columns = clampedColumns(grid.columns);
  std::vector<GridPlacement> placements(count);
  OccupancyGrid occupied;
  std::size_t row = 0;
  std::size_t column = 0;

  for (std::size_t i = 0; i < count; ++i) {
    GridPlacement placement{
        .row = row,
        .column = column,
        .colSpan = colSpanFor(grid, i),
        .rowSpan = rowSpanFor(grid, i),
    };

    while (!canOccupy(occupied, placement.row, placement.column,
                      placement.colSpan, placement.rowSpan, columns)) {
      ++placement.column;
      if (placement.column >= columns) {
        ++placement.row;
        placement.column = 0;
      }
    }

    placements[i] = placement;
    markOccupied(occupied, placement, columns);

    row = placement.row;
    column = placement.column + placement.colSpan;
    if (column >= columns) {
      ++row;
      column = 0;
    }
  }

  return placements;
}

float slotWidth(GridPlacement const& placement, float columnWidth, float horizontalSpacing) {
  return columnWidth * static_cast<float>(placement.colSpan) +
         horizontalSpacing * static_cast<float>(placement.colSpan - 1);
}

float spannedHeight(std::vector<float> const& rowHeights, std::size_t row,
                    std::size_t rowSpan, float verticalSpacing) {
  float height = 0.f;
  for (std::size_t r = row; r < row + rowSpan && r < rowHeights.size(); ++r) {
    height += rowHeights[r];
  }
  if (rowSpan > 1) {
    height += verticalSpacing * static_cast<float>(rowSpan - 1);
  }
  return height;
}

float totalRowSpacing(std::size_t rows, float verticalSpacing) {
  return rows > 1 ? static_cast<float>(rows - 1) * verticalSpacing : 0.f;
}

float rowHeightSum(std::vector<float> const& rowHeights) {
  float total = 0.f;
  for (float height : rowHeights) {
    total += height;
  }
  return total;
}

float assignedGridHeight(LayoutConstraints const& constraints) {
  if (std::isfinite(constraints.maxHeight) && constraints.maxHeight > 0.f &&
      constraints.minHeight >= constraints.maxHeight - kGridLayoutEpsilon) {
    return constraints.maxHeight;
  }
  return constraints.minHeight > 0.f ? constraints.minHeight : 0.f;
}

void stretchRowsToAssignedHeight(std::vector<float>& rowHeights, float verticalSpacing,
                                 LayoutConstraints const& constraints) {
  if (rowHeights.empty()) {
    return;
  }

  float const assignedHeight = assignedGridHeight(constraints);
  if (assignedHeight <= 0.f) {
    return;
  }

  float const targetRowsHeight =
      std::max(0.f, assignedHeight - totalRowSpacing(rowHeights.size(), verticalSpacing));
  float const minimumRowsHeight = rowHeightSum(rowHeights);
  if (targetRowsHeight <= minimumRowsHeight + kGridLayoutEpsilon) {
    return;
  }

  std::vector<bool> locked(rowHeights.size(), false);
  std::size_t flexibleRows = rowHeights.size();
  float remainingHeight = targetRowsHeight;

  while (flexibleRows > 0) {
    float const candidateHeight = remainingHeight / static_cast<float>(flexibleRows);
    bool lockedAny = false;
    for (std::size_t i = 0; i < rowHeights.size(); ++i) {
      if (locked[i] || rowHeights[i] <= candidateHeight + kGridLayoutEpsilon) {
        continue;
      }
      locked[i] = true;
      remainingHeight -= rowHeights[i];
      --flexibleRows;
      lockedAny = true;
    }
    if (lockedAny) {
      continue;
    }
    for (std::size_t i = 0; i < rowHeights.size(); ++i) {
      if (!locked[i]) {
        rowHeights[i] = candidateHeight;
      }
    }
    break;
  }
}

GridPlan planGrid(Grid const& grid, LayoutConstraints const& constraints,
                  std::vector<Size> const& measured) {
  GridPlan plan{};
  std::size_t const columns = clampedColumns(grid.columns);
  float const availableWidth = std::isfinite(constraints.maxWidth) && constraints.maxWidth > 0.f
                                   ? constraints.maxWidth
                                   : 0.f;
  float const totalGap = static_cast<float>(columns - 1) * grid.horizontalSpacing;
  float const columnWidth = columns > 0 ? std::max(0.f, (availableWidth - totalGap) /
                                                            static_cast<float>(columns))
                                        : 0.f;

  plan.slots.resize(measured.size());
  std::vector<GridPlacement> const placements = placeGridItems(grid, measured.size());

  std::size_t rows = 0;
  for (GridPlacement const& placement : placements) {
    rows = std::max(rows, placement.row + placement.rowSpan);
  }

  std::vector<float> rowHeights(rows, 0.f);
  for (std::size_t i = 0; i < measured.size(); ++i) {
    GridPlacement const& placement = placements[i];
    if (placement.rowSpan == 1 && placement.row < rowHeights.size()) {
      rowHeights[placement.row] = std::max(rowHeights[placement.row], measured[i].height);
    }
  }
  for (std::size_t i = 0; i < measured.size(); ++i) {
    GridPlacement const& placement = placements[i];
    if (placement.rowSpan <= 1) {
      continue;
    }
    float const currentHeight = spannedHeight(rowHeights, placement.row, placement.rowSpan,
                                             grid.verticalSpacing);
    float const deficit = measured[i].height - currentHeight;
    if (deficit <= 0.f) {
      continue;
    }
    float const perRow = deficit / static_cast<float>(placement.rowSpan);
    for (std::size_t r = placement.row; r < placement.row + placement.rowSpan && r < rowHeights.size(); ++r) {
      rowHeights[r] += perRow;
    }
  }
  stretchRowsToAssignedHeight(rowHeights, grid.verticalSpacing, constraints);

  std::vector<float> rowY(rows, 0.f);
  float y = 0.f;
  for (std::size_t r = 0; r < rows; ++r) {
    rowY[r] = y;
    y += rowHeights[r];
    if (r + 1 < rows) {
      y += grid.verticalSpacing;
    }
  }

  for (std::size_t i = 0; i < measured.size(); ++i) {
    GridPlacement const& placement = placements[i];
    float const x = static_cast<float>(placement.column) * (columnWidth + grid.horizontalSpacing);
    plan.slots[i] = Rect{
        x,
        placement.row < rowY.size() ? rowY[placement.row] : 0.f,
        slotWidth(placement, columnWidth, grid.horizontalSpacing),
        spannedHeight(rowHeights, placement.row, placement.rowSpan, grid.verticalSpacing),
    };
  }

  plan.size = Size{std::max(availableWidth, constraints.minWidth), std::max(y, constraints.minHeight)};
  return plan;
}

} // namespace

Size Grid::measure(MeasureContext& ctx, LayoutConstraints const& constraints,
                   LayoutHints const&, TextSystem& textSystem) const {
  std::size_t const columnsCount = clampedColumns(columns);
  float const availableWidth = std::isfinite(constraints.maxWidth) && constraints.maxWidth > 0.f
                                   ? constraints.maxWidth
                                   : 0.f;
  float const totalGap = static_cast<float>(columnsCount - 1) * horizontalSpacing;
  float const columnWidth = std::max(0.f, (availableWidth - totalGap) /
                                             static_cast<float>(columnsCount));
  std::vector<Size> measured;
  measured.reserve(children.size());
  std::vector<GridPlacement> const placements = placeGridItems(*this, children.size());
  for (std::size_t i = 0; i < children.size(); ++i) {
    float const width = slotWidth(placements[i], columnWidth, horizontalSpacing);
    LayoutConstraints childConstraints{
        .maxWidth = width,
        .maxHeight = std::numeric_limits<float>::infinity(),
        .minWidth = 0.f,
        .minHeight = 0.f,
    };
    measured.push_back(measureChild(children[i], ctx, childConstraints, textSystem));
  }
  return planGrid(*this, constraints, measured).size;
}

std::unique_ptr<scenegraph::SceneNode> Grid::mount(MountContext& ctx) const {
  std::size_t const columnsCount = clampedColumns(columns);
  float const availableWidth = std::isfinite(ctx.constraints().maxWidth) && ctx.constraints().maxWidth > 0.f
                                   ? ctx.constraints().maxWidth
                                   : 0.f;
  float const totalGap = static_cast<float>(columnsCount - 1) * horizontalSpacing;
  float const columnWidth = std::max(0.f, (availableWidth - totalGap) /
                                             static_cast<float>(columnsCount));

  std::vector<Size> measured;
  measured.reserve(children.size());
  std::vector<GridPlacement> const placements = placeGridItems(*this, children.size());
  for (std::size_t i = 0; i < children.size(); ++i) {
    float const width = slotWidth(placements[i], columnWidth, horizontalSpacing);
    LayoutConstraints childConstraints{
        .maxWidth = width,
        .maxHeight = std::numeric_limits<float>::infinity(),
        .minWidth = 0.f,
        .minHeight = 0.f,
    };
    measured.push_back(measureChild(children[i], ctx.measureContext(), childConstraints, ctx.textSystem()));
  }

  GridPlan const plan = planGrid(*this, ctx.constraints(), measured);
  auto group = std::make_unique<scenegraph::SceneNode>(Rect{0.f, 0.f, plan.size.width, plan.size.height});
  struct MountedGridChild {
    scenegraph::SceneNode* node = nullptr;
    Point layoutOrigin{};
    std::size_t childIndex = 0;
  };
  auto measuredSizes = std::make_shared<std::vector<Size>>(std::move(measured));
  auto mountedChildren = std::make_shared<std::vector<MountedGridChild>>();
  for (std::size_t i = 0; i < children.size(); ++i) {
    Rect const slot = plan.slots[i];
    MountContext childCtx = ctx.childWithSharedScope(fixedConstraints(Size{slot.width, slot.height}), {});
    auto childNode = children[i].mount(childCtx);
    if (childNode) {
      Rect const bounds = childNode->bounds();
      Point const origin{
          slot.x + alignedOffset(slot.width, bounds.width, horizontalAlignment),
          slot.y + alignedOffset(slot.height, bounds.height, verticalAlignment),
      };
      detail::setLayoutPosition(*childNode, origin);
      mountedChildren->push_back(MountedGridChild{childNode.get(), origin, i});
      group->appendChild(std::move(childNode));
    }
  }
  auto* rawGroup = group.get();
  Grid grid = *this;
  rawGroup->setLayoutConstraints(ctx.constraints());
  rawGroup->setRelayout([rawGroup, mountedChildren, measuredSizes, grid = std::move(grid)](
                            LayoutConstraints const& constraints) mutable {
    std::size_t const columnsCount = clampedColumns(grid.columns);
    float const availableWidth = std::isfinite(constraints.maxWidth) && constraints.maxWidth > 0.f
                                     ? constraints.maxWidth
                                     : 0.f;
    float const totalGap = static_cast<float>(columnsCount - 1) * grid.horizontalSpacing;
    float const columnWidth = std::max(0.f, (availableWidth - totalGap) /
                                               static_cast<float>(columnsCount));
    std::vector<GridPlacement> const placements = placeGridItems(grid, grid.children.size());
    std::vector<Size> measured = *measuredSizes;
    measured.resize(grid.children.size());
    for (MountedGridChild const& child : *mountedChildren) {
      if (!child.node || child.childIndex >= placements.size()) {
        continue;
      }
      float const width = slotWidth(placements[child.childIndex], columnWidth, grid.horizontalSpacing);
      LayoutConstraints childConstraints{
          .maxWidth = width,
          .maxHeight = std::numeric_limits<float>::infinity(),
          .minWidth = 0.f,
          .minHeight = 0.f,
      };
      if (child.node && child.node->relayout(childConstraints)) {
        measured[child.childIndex] = child.node->size();
      } else {
        measured[child.childIndex] = child.node ? child.node->size() : Size{};
      }
    }
    *measuredSizes = measured;
    GridPlan const plan = planGrid(grid, constraints, measured);
    for (MountedGridChild& child : *mountedChildren) {
      if (!child.node || child.childIndex >= plan.slots.size()) {
        continue;
      }
      Rect const slot = plan.slots[child.childIndex];
      child.node->relayout(fixedConstraints(Size{slot.width, slot.height}), false);
      Rect const bounds = child.node->bounds();
      Point const current = child.node->position();
      Vec2 const localOffset{current.x - child.layoutOrigin.x,
                             current.y - child.layoutOrigin.y};
      Point const origin{
          slot.x + alignedOffset(slot.width, bounds.width, grid.horizontalAlignment),
          slot.y + alignedOffset(slot.height, bounds.height, grid.verticalAlignment),
      };
      child.node->setPosition(Point{origin.x + localOffset.x, origin.y + localOffset.y});
      child.layoutOrigin = origin;
    }
    rawGroup->setSize(plan.size);
  });
  return group;
}

} // namespace lambdaui
