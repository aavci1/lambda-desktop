#pragma once

#include <Lambda/Core/Color.hpp>

namespace lambda_files {

/// Visual tokens aligned to the Lambda desktop mock (`SADE Desktop (standalone).html`).
struct FilesTheme {
  static constexpr lambda::Color accent = lambda::Color::hex(0x2A7FFF);
  static constexpr lambda::Color accent2 = lambda::Color::hex(0x5AA2FF);
  static constexpr lambda::Color text = lambda::Color::hex(0x16203A);
  static constexpr lambda::Color text2 = lambda::Color::hex(0x5B6781);
  static constexpr lambda::Color text3 = lambda::Color::hex(0x8893AD);
  static constexpr lambda::Color line = lambda::Color{0.08f, 0.12f, 0.24f, 0.08f};
  static constexpr lambda::Color line2 = lambda::Color{0.08f, 0.12f, 0.24f, 0.05f};
  static constexpr lambda::Color glassSoft = lambda::Color{1.f, 1.f, 1.f, 0.34f};
  static constexpr lambda::Color glassPanel = lambda::Color{1.f, 1.f, 1.f, 0.24f};
  static constexpr lambda::Color selectFill = lambda::Color{0.16f, 0.50f, 1.f, 0.14f};
  static constexpr lambda::Color selectBorder = lambda::Color{0.16f, 0.50f, 1.f, 0.35f};
  static constexpr lambda::Color hoverFill = lambda::Color{0.f, 0.f, 0.f, 0.04f};
  static constexpr lambda::Color chromeBg = lambda::Color{1.f, 1.f, 1.f, 0.f};
  static constexpr lambda::Color sideBg = lambda::Color{1.f, 1.f, 1.f, 0.28f};
  static constexpr lambda::Color folderA = lambda::Color::hex(0x4D94FF);
  static constexpr lambda::Color folderB = lambda::Color::hex(0x2A7FFF);
  static constexpr lambda::Color filePaperTop = lambda::Color::hex(0xFFFFFF);
  static constexpr lambda::Color filePaperBottom = lambda::Color::hex(0xEEF2F8);
  static constexpr lambda::Color fileLine = lambda::Color::hex(0xDDE5F0);
  static constexpr lambda::Color pathAccent = lambda::Color::hex(0x16203A);
  static constexpr lambda::Color viewToggleFill = lambda::Color{0.08f, 0.12f, 0.24f, 0.08f};
  static constexpr lambda::Color storageTrack = lambda::Color{0.08f, 0.12f, 0.24f, 0.06f};
  static constexpr lambda::Color storageFill = lambda::Color::hex(0x2A7FFF);

  static constexpr float kSidebarWidth = 220.f;
  static constexpr float kTitlebarHeight = 48.f;
  static constexpr float kTitlebarPadH = 16.f;
  static constexpr float kTitlebarPadV = 0.f;
  static constexpr float kToolbarBtn = 28.f;
  static constexpr float kBreadcrumbHeight = kToolbarBtn;
  static constexpr float kBreadcrumbPadH = 10.f;
  static constexpr float kBreadcrumbPadV = 4.f;
  static constexpr float kBreadcrumbRadius = 8.f;
  static constexpr float kGridMinCell = 110.f;
  static constexpr float kGridGapH = 16.f;
  static constexpr float kGridGapV = 14.f;
  static constexpr float kGridTileH = 118.f;
  static constexpr float kListHeaderHeight = 28.f;
  static constexpr float kListRowHeight = 46.f;
  static constexpr float kListRowGap = 2.f;
  static constexpr float kListRowRadius = 7.f;
  static constexpr float kContentPadH = 20.f;
  static constexpr float kContentPadV = 16.f;
  static constexpr float kSidePad = 10.f;
  static constexpr float kSideItemRadius = 7.f;
};

} // namespace lambda_files
