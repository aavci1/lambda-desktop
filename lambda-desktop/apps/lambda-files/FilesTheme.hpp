#pragma once

#include <Lambda/Core/Color.hpp>

namespace lambda_files {

/// Visual tokens aligned to the Lambda desktop mock (`SADE Desktop (standalone).html`).
struct FilesTheme {
  static constexpr lambdaui::Color accent = lambdaui::Color::hex(0x2A7FFF);
  static constexpr lambdaui::Color accent2 = lambdaui::Color::hex(0x5AA2FF);
  static constexpr lambdaui::Color text = lambdaui::Color::hex(0x16203A);
  static constexpr lambdaui::Color text2 = lambdaui::Color::hex(0x5B6781);
  static constexpr lambdaui::Color text3 = lambdaui::Color::hex(0x8893AD);
  static constexpr lambdaui::Color line = lambdaui::Color{0.08f, 0.12f, 0.24f, 0.08f};
  static constexpr lambdaui::Color line2 = lambdaui::Color{0.08f, 0.12f, 0.24f, 0.05f};
  static constexpr lambdaui::Color glassSoft = lambdaui::Color{1.f, 1.f, 1.f, 0.34f};
  static constexpr lambdaui::Color glassPanel = lambdaui::Color{1.f, 1.f, 1.f, 0.24f};
  static constexpr lambdaui::Color selectFill = lambdaui::Color{0.16f, 0.50f, 1.f, 0.14f};
  static constexpr lambdaui::Color selectBorder = lambdaui::Color{0.16f, 0.50f, 1.f, 0.35f};
  static constexpr lambdaui::Color hoverFill = lambdaui::Color{0.f, 0.f, 0.f, 0.04f};
  static constexpr lambdaui::Color chromeBg = lambdaui::Color{1.f, 1.f, 1.f, 0.f};
  static constexpr lambdaui::Color sideBg = lambdaui::Color{1.f, 1.f, 1.f, 0.28f};
  static constexpr lambdaui::Color folderA = lambdaui::Color::hex(0x4D94FF);
  static constexpr lambdaui::Color folderB = lambdaui::Color::hex(0x2A7FFF);
  static constexpr lambdaui::Color filePaperTop = lambdaui::Color::hex(0xFFFFFF);
  static constexpr lambdaui::Color filePaperBottom = lambdaui::Color::hex(0xEEF2F8);
  static constexpr lambdaui::Color fileLine = lambdaui::Color::hex(0xDDE5F0);
  static constexpr lambdaui::Color pathAccent = lambdaui::Color::hex(0x16203A);
  static constexpr lambdaui::Color viewToggleFill = lambdaui::Color{0.08f, 0.12f, 0.24f, 0.08f};
  static constexpr lambdaui::Color storageTrack = lambdaui::Color{0.08f, 0.12f, 0.24f, 0.06f};
  static constexpr lambdaui::Color storageFill = lambdaui::Color::hex(0x2A7FFF);

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
