#pragma once

#include <Flux/Core/Color.hpp>

namespace lambda_files {

/// Visual tokens aligned to the Lambda desktop mock (`SADE Desktop (standalone).html`).
struct FilesTheme {
  static constexpr flux::Color accent = flux::Color::hex(0x2A7FFF);
  static constexpr flux::Color accent2 = flux::Color::hex(0x5AA2FF);
  static constexpr flux::Color text = flux::Color::hex(0x16203A);
  static constexpr flux::Color text2 = flux::Color::hex(0x5B6781);
  static constexpr flux::Color text3 = flux::Color::hex(0x8893AD);
  static constexpr flux::Color line = flux::Color{0.08f, 0.12f, 0.24f, 0.08f};
  static constexpr flux::Color line2 = flux::Color{0.08f, 0.12f, 0.24f, 0.05f};
  static constexpr flux::Color glassSoft = flux::Color{1.f, 1.f, 1.f, 0.34f};
  static constexpr flux::Color glassPanel = flux::Color{1.f, 1.f, 1.f, 0.24f};
  static constexpr flux::Color selectFill = flux::Color{0.16f, 0.50f, 1.f, 0.14f};
  static constexpr flux::Color selectBorder = flux::Color{0.16f, 0.50f, 1.f, 0.35f};
  static constexpr flux::Color hoverFill = flux::Color{0.f, 0.f, 0.f, 0.04f};
  static constexpr flux::Color chromeBg = flux::Color{1.f, 1.f, 1.f, 0.f};
  static constexpr flux::Color sideBg = flux::Color{1.f, 1.f, 1.f, 0.28f};
  static constexpr flux::Color folderA = flux::Color::hex(0x4D94FF);
  static constexpr flux::Color folderB = flux::Color::hex(0x2A7FFF);
  static constexpr flux::Color filePaperTop = flux::Color::hex(0xFFFFFF);
  static constexpr flux::Color filePaperBottom = flux::Color::hex(0xEEF2F8);
  static constexpr flux::Color fileLine = flux::Color::hex(0xDDE5F0);
  static constexpr flux::Color pathAccent = flux::Color::hex(0x16203A);
  static constexpr flux::Color viewToggleFill = flux::Color{0.08f, 0.12f, 0.24f, 0.08f};
  static constexpr flux::Color storageTrack = flux::Color{0.08f, 0.12f, 0.24f, 0.06f};
  static constexpr flux::Color storageFill = flux::Color::hex(0x2A7FFF);

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
