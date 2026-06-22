#pragma once

#include "FilesTheme.hpp"

#include <Lambda/Core/Geometry.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace lambda_files {

/// Pure layout math for the files grid (no scene graph). Shared by `FilesFlowGrid` and tests.
struct FilesFlowGridLayout {
  float cellWidth = FilesTheme::kGridMinCell;
  float cellHeight = FilesTheme::kGridTileH;
  float horizontalSpacing = FilesTheme::kGridGapH;
  float verticalSpacing = FilesTheme::kGridGapV;

  static int columnCountForWidth(float width, float cellW, float gapH) {
    if (!std::isfinite(width) || width <= 0.f || !std::isfinite(cellW) || cellW <= 0.f) {
      return 0;
    }
    float const stride = cellW + gapH;
    if (!std::isfinite(stride) || stride <= 0.f) {
      return 0;
    }
    return std::max(1, static_cast<int>((width + gapH) / stride));
  }

  int columnCountForWidth(float width) const {
    return columnCountForWidth(width, cellWidth, horizontalSpacing);
  }

  static int rowCountForEntries(std::size_t entryCount, int columns) {
    if (entryCount == 0) {
      return 0;
    }
    if (columns <= 0) {
      return 0;
    }
    int const cols = std::max(1, columns);
    return static_cast<int>((entryCount + static_cast<std::size_t>(cols) - 1) /
                           static_cast<std::size_t>(cols));
  }

  int rowCountForEntries(std::size_t entryCount, float width) const {
    return rowCountForEntries(entryCount, columnCountForWidth(width));
  }

  lambda::Size contentSizeFor(float width, std::size_t entryCount) const {
    if (!std::isfinite(width) || width <= 0.f) {
      return lambda::Size{};
    }
    int const columns = columnCountForWidth(width);
    int const rows = rowCountForEntries(entryCount, columns);
    if (rows <= 0) {
      return lambda::Size{width > 0.f ? width : 0.f, 0.f};
    }
    float const height =
        static_cast<float>(rows) * cellHeight + static_cast<float>(rows - 1) * verticalSpacing;
    return lambda::Size{width > 0.f ? width : 0.f, height};
  }

  std::vector<std::size_t> rowIndicesFor(std::size_t entryCount, float width) const {
    int const rowCount = rowCountForEntries(entryCount, columnCountForWidth(width));
    std::vector<std::size_t> rows;
    rows.reserve(static_cast<std::size_t>(rowCount));
    for (int row = 0; row < rowCount; ++row) {
      rows.push_back(static_cast<std::size_t>(row));
    }
    return rows;
  }
};

} // namespace lambda_files
