#include "Compositor/CompositorRenderFrame.hpp"

#include "Compositor/Chrome/CursorRenderer.hpp"
#include "Compositor/Chrome/WindowFrameGeometry.hpp"
#include "Compositor/Chrome/WindowChromeRenderer.hpp"
#include "Compositor/CompositorPresentation.hpp"
#include "Compositor/Diagnostics/CpuTrace.hpp"
#include "Compositor/SceneGraph/CompositorSceneGraph.hpp"
#include "Compositor/Surface/CommittedSurfacePainter.hpp"
#include "Compositor/Surface/SurfaceRenderer.hpp"
#include "Graphics/Linux/FreeTypeTextSystem.hpp"

#include "presentation-time-server-protocol.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace lambda::compositor {
namespace {

void drawScreenshotSelectionOverlay(Canvas& canvas,
                                    WaylandServer const& wayland,
                                    ScreenshotSelectionOverlay const& overlay) {
  float const outputWidth = static_cast<float>(wayland.logicalOutputWidth());
  float const outputHeight = static_cast<float>(wayland.logicalOutputHeight());
  if (outputWidth <= 0.f || outputHeight <= 0.f) return;

  Color const scrim{0.f, 0.f, 0.f, 0.36f};
  Color const guide{0.38f, 0.68f, 1.f, 0.72f};
  Color const fill{0.38f, 0.68f, 1.f, 0.12f};
  Color const border{0.86f, 0.95f, 1.f, 0.96f};
  Rect const outputRect = Rect::sharp(0.f, 0.f, outputWidth, outputHeight);

  if (!overlay.region) {
    canvas.drawRect(outputRect,
                    CornerRadius{},
                    FillStyle::solid(Color{0.f, 0.f, 0.f, 0.12f}),
                    StrokeStyle::none(),
                    ShadowStyle::none());
    float const x = std::clamp(overlay.currentX, 0.f, outputWidth);
    float const y = std::clamp(overlay.currentY, 0.f, outputHeight);
    canvas.drawLine(Point{x, 0.f}, Point{x, outputHeight}, StrokeStyle::solid(guide, 1.f));
    canvas.drawLine(Point{0.f, y}, Point{outputWidth, y}, StrokeStyle::solid(guide, 1.f));
    return;
  }

  Rect const selected = Rect::sharp(static_cast<float>(overlay.region->x),
                                   static_cast<float>(overlay.region->y),
                                   static_cast<float>(overlay.region->width),
                                   static_cast<float>(overlay.region->height));
  canvas.drawRect(Rect::sharp(0.f, 0.f, outputWidth, selected.y),
                  CornerRadius{},
                  FillStyle::solid(scrim),
                  StrokeStyle::none(),
                  ShadowStyle::none());
  canvas.drawRect(Rect::sharp(0.f, selected.y, selected.x, selected.height),
                  CornerRadius{},
                  FillStyle::solid(scrim),
                  StrokeStyle::none(),
                  ShadowStyle::none());
  canvas.drawRect(Rect::sharp(selected.x + selected.width,
                              selected.y,
                              std::max(0.f, outputWidth - selected.x - selected.width),
                              selected.height),
                  CornerRadius{},
                  FillStyle::solid(scrim),
                  StrokeStyle::none(),
                  ShadowStyle::none());
  canvas.drawRect(Rect::sharp(0.f,
                              selected.y + selected.height,
                              outputWidth,
                              std::max(0.f, outputHeight - selected.y - selected.height)),
                  CornerRadius{},
                  FillStyle::solid(scrim),
                  StrokeStyle::none(),
                  ShadowStyle::none());
  canvas.drawRect(selected,
                  CornerRadius{},
                  FillStyle::solid(fill),
                  StrokeStyle::solid(border, 1.5f),
                  ShadowStyle::none());
}

void drawScreenshotFlash(Canvas& canvas, WaylandServer const& wayland, float opacity) {
  if (opacity <= 0.f) return;
  float const outputWidth = static_cast<float>(wayland.logicalOutputWidth());
  float const outputHeight = static_cast<float>(wayland.logicalOutputHeight());
  if (outputWidth <= 0.f || outputHeight <= 0.f) return;
  canvas.drawRect(Rect::sharp(0.f, 0.f, outputWidth, outputHeight),
                  CornerRadius{},
                  FillStyle::solid(Color{1.f, 1.f, 1.f, std::clamp(opacity, 0.f, 1.f)}),
                  StrokeStyle::none(),
                  ShadowStyle::none());
}

struct WindowCyclerSurfaceList {
  std::vector<CommittedSurfaceSnapshot const*> surfaces;
  std::size_t selectedIndex = 0;
};

struct WindowCyclerVisibleRange {
  std::size_t start = 0;
  std::size_t count = 0;
};

std::string windowCyclerTitle(CommittedSurfaceSnapshot const& surface) {
  if (!surface.title.empty()) return surface.title;
  if (!surface.appId.empty()) return surface.appId;
  return "Window";
}

std::string windowCyclerBadgeText(std::string_view title, std::string_view appId) {
  std::string_view const source = appId.empty() ? title : appId;
  std::string initials;
  bool wordStart = true;
  for (unsigned char ch : source) {
    if (std::isalnum(ch)) {
      if (wordStart || initials.empty()) {
        initials.push_back(static_cast<char>(std::toupper(ch)));
        if (initials.size() == 2u) break;
      }
      wordStart = false;
      continue;
    }
    if (ch == ' ' || ch == '-' || ch == '_' || ch == '.' || ch == '/') wordStart = true;
  }
  if (initials.empty()) initials = "W";
  return initials;
}

Color windowCyclerBadgeColor(std::string_view key) {
  std::uint32_t hash = 2166136261u;
  for (unsigned char ch : key) {
    hash ^= static_cast<std::uint32_t>(ch);
    hash *= 16777619u;
  }
  constexpr std::array<Color, 8> palette{
      Color{0.26f, 0.46f, 0.80f, 1.f},
      Color{0.20f, 0.58f, 0.48f, 1.f},
      Color{0.66f, 0.40f, 0.68f, 1.f},
      Color{0.70f, 0.44f, 0.30f, 1.f},
      Color{0.36f, 0.54f, 0.34f, 1.f},
      Color{0.54f, 0.42f, 0.76f, 1.f},
      Color{0.78f, 0.53f, 0.24f, 1.f},
      Color{0.24f, 0.56f, 0.72f, 1.f},
  };
  return palette[hash % palette.size()];
}

Font windowCyclerFont(float size, float weight) {
  Font font{};
  font.size = size;
  font.weight = weight;
  return font;
}

void drawWindowCyclerText(Canvas& canvas,
                          TextSystem& textSystem,
                          std::string_view text,
                          Font const& font,
                          Color color,
                          Rect const& bounds,
                          HorizontalAlignment alignment = HorizontalAlignment::Center) {
  TextLayoutOptions options{
      .horizontalAlignment = alignment,
      .verticalAlignment = VerticalAlignment::Center,
      .wrapping = TextWrapping::NoWrap,
      .maxLines = 1,
  };
  auto layout = textSystem.layout(text, font, color, bounds, options);
  if (!layout) return;
  canvas.save();
  canvas.clipRect(bounds);
  canvas.drawTextLayout(*layout, {0.f, 0.f});
  canvas.restore();
}

WindowCyclerSurfaceList windowCyclerSurfaces(WindowCyclerOverlaySnapshot const& overlay,
                                             std::vector<CommittedSurfaceSnapshot> const& surfaces) {
  std::unordered_map<std::uint64_t, CommittedSurfaceSnapshot const*> surfaceById;
  surfaceById.reserve(surfaces.size());
  for (CommittedSurfaceSnapshot const& surface : surfaces) {
    surfaceById.emplace(surface.id, &surface);
  }

  WindowCyclerSurfaceList list;
  list.surfaces.reserve(overlay.surfaceIds.size());
  bool selectedFound = false;
  for (std::size_t index = 0; index < overlay.surfaceIds.size(); ++index) {
    auto found = surfaceById.find(overlay.surfaceIds[index]);
    if (found == surfaceById.end()) continue;
    if (index == overlay.selectedIndex) {
      list.selectedIndex = list.surfaces.size();
      selectedFound = true;
    }
    list.surfaces.push_back(found->second);
  }
  if (!selectedFound && !list.surfaces.empty()) {
    list.selectedIndex = std::min(list.selectedIndex, list.surfaces.size() - 1u);
  }
  return list;
}

WindowCyclerVisibleRange windowCyclerVisibleRange(std::size_t total,
                                                  std::size_t selected,
                                                  std::size_t maxVisible) {
  if (total == 0u || maxVisible == 0u) return {};
  std::size_t const count = std::min(total, maxVisible);
  std::size_t const clampedSelected = std::min(selected, total - 1u);
  std::size_t start = clampedSelected > count / 2u ? clampedSelected - count / 2u : 0u;
  if (start + count > total) start = total - count;
  return WindowCyclerVisibleRange{.start = start, .count = count};
}

bool drawWindowCyclerThumbnail(WaylandServer& wayland,
                               Canvas& canvas,
                               TextSystem& textSystem,
                               SurfaceRenderState& renderState,
                               CommittedSurfaceSnapshot const& surface,
                               Rect const& previewRect,
                               std::chrono::steady_clock::time_point frameTime,
                               ChromeConfig const& chrome,
                               bool animationsEnabled) {
  auto& cached = renderState.clientImages[surface.id];
  updateCachedImage(wayland, canvas, surface, cached);
  if (!cached.image) return false;

  auto& visual = renderState.surfaceVisuals[surface.id];
  CommittedSurfaceSnapshot previewSurface = surface;
  previewSurface.closeButtonHovered = false;
  previewSurface.closeButtonPressed = false;
  previewSurface.maximizeButtonHovered = false;
  previewSurface.maximizeButtonPressed = false;
  previewSurface.minimizeButtonHovered = false;
  previewSurface.minimizeButtonPressed = false;

  Rect const frameRect = windowFrameRect(previewSurface, chrome.contentInsetWidth);
  if (frameRect.width <= 0.f || frameRect.height <= 0.f ||
      previewRect.width <= 0.f || previewRect.height <= 0.f) {
    return false;
  }
  float const scale = std::min(previewRect.width / frameRect.width, previewRect.height / frameRect.height);
  if (!std::isfinite(scale) || scale <= 0.f) return false;
  float const scaledWidth = frameRect.width * scale;
  float const scaledHeight = frameRect.height * scale;
  float const targetX = previewRect.x + (previewRect.width - scaledWidth) * 0.5f;
  float const targetY = previewRect.y + (previewRect.height - scaledHeight) * 0.5f;

  canvas.save();
  canvas.clipRect(previewRect, CornerRadius{6.f}, true);
  canvas.translate(targetX - frameRect.x * scale, targetY - frameRect.y * scale);
  canvas.scale(scale);
  drawCommittedSurfaceSnapshot(canvas,
                               textSystem,
                               previewSurface,
                               visual,
                               *cached.image,
                               frameTime,
                               chrome,
                               animationsEnabled);
  canvas.restore();
  return true;
}

void drawWindowCyclerItem(WaylandServer& wayland,
                          Canvas& canvas,
                          TextSystem& textSystem,
                          SurfaceRenderState& renderState,
                          CommittedSurfaceSnapshot const& surface,
                          Rect const& itemRect,
                          bool selected,
                          std::chrono::steady_clock::time_point frameTime,
                          ChromeConfig const& chrome,
                          bool animationsEnabled) {
  CornerRadius const itemRadius{8.f};
  Color const itemFill = selected ? Color{0.13f, 0.16f, 0.20f, 0.90f}
                                  : Color{0.07f, 0.08f, 0.10f, 0.78f};
  Color const itemStroke = selected ? Color{0.45f, 0.76f, 1.f, 0.96f}
                                    : Color{1.f, 1.f, 1.f, 0.16f};
  canvas.drawRect(itemRect,
                  itemRadius,
                  FillStyle::solid(itemFill),
                  StrokeStyle::solid(itemStroke, selected ? 2.f : 1.f),
                  selected ? ShadowStyle{.radius = 16.f, .offset = {0.f, 8.f}, .color = Color{0.f, 0.f, 0.f, 0.30f}}
                           : ShadowStyle::none());

  float const padding = 10.f;
  float const previewHeight = std::max(80.f, itemRect.height - 80.f);
  Rect const previewRect = Rect::sharp(itemRect.x + padding,
                                       itemRect.y + padding,
                                       itemRect.width - padding * 2.f,
                                       previewHeight);
  canvas.drawRect(previewRect,
                  CornerRadius{6.f},
                  FillStyle::solid(Color{0.02f, 0.025f, 0.03f, 0.94f}),
                  StrokeStyle::solid(Color{1.f, 1.f, 1.f, 0.10f}, 1.f),
                  ShadowStyle::none());
  bool const previewDrawn = drawWindowCyclerThumbnail(wayland,
                                                      canvas,
                                                      textSystem,
                                                      renderState,
                                                      surface,
                                                      previewRect,
                                                      frameTime,
                                                      chrome,
                                                      animationsEnabled);
  if (!previewDrawn) {
    drawWindowCyclerText(canvas,
                         textSystem,
                         "No Preview",
                         windowCyclerFont(11.f, 620.f),
                         Color{1.f, 1.f, 1.f, 0.52f},
                         previewRect);
  }

  std::string const title = windowCyclerTitle(surface);
  std::string const badge = windowCyclerBadgeText(title, surface.appId);
  std::string_view const badgeKey = surface.appId.empty() ? std::string_view{title} : std::string_view{surface.appId};
  float const iconSize = 32.f;
  float const iconY = previewRect.y + previewRect.height + 10.f;
  Rect const iconRect = Rect::sharp(itemRect.x + (itemRect.width - iconSize) * 0.5f,
                                    iconY,
                                    iconSize,
                                    iconSize);
  canvas.drawCircle(iconRect.center(),
                    iconSize * 0.5f,
                    FillStyle::solid(windowCyclerBadgeColor(badgeKey)),
                    StrokeStyle::solid(Color{1.f, 1.f, 1.f, 0.22f}, 1.f));
  drawWindowCyclerText(canvas,
                       textSystem,
                       badge,
                       windowCyclerFont(13.f, 760.f),
                       Color{1.f, 1.f, 1.f, 0.96f},
                       iconRect);

  Rect const titleRect = Rect::sharp(itemRect.x + padding,
                                     iconRect.y + iconRect.height + 3.f,
                                     itemRect.width - padding * 2.f,
                                     22.f);
  drawWindowCyclerText(canvas,
                       textSystem,
                       title,
                       windowCyclerFont(12.5f, selected ? 660.f : 560.f),
                       selected ? Color{1.f, 1.f, 1.f, 0.96f} : Color{1.f, 1.f, 1.f, 0.74f},
                       titleRect);
}

void drawWindowCyclerOverlay(WaylandServer& wayland,
                             Canvas& canvas,
                             TextSystem& textSystem,
                             SurfaceRenderState& renderState,
                             WindowCyclerOverlaySnapshot const& overlay,
                             std::vector<CommittedSurfaceSnapshot> const& surfaces,
                             std::chrono::steady_clock::time_point frameTime,
                             ChromeConfig const& chrome,
                             bool animationsEnabled) {
  float const outputWidth = static_cast<float>(wayland.logicalOutputWidth());
  float const outputHeight = static_cast<float>(wayland.logicalOutputHeight());
  if (outputWidth <= 0.f || outputHeight <= 0.f) return;

  WindowCyclerSurfaceList const list = windowCyclerSurfaces(overlay, surfaces);
  if (list.surfaces.size() < 2u) return;

  float const availableWidth = std::max(0.f, outputWidth - 64.f);
  float itemWidth = std::clamp(outputWidth * 0.18f, 184.f, 280.f);
  itemWidth = std::min(itemWidth, availableWidth);
  float const gap = std::clamp(outputWidth * 0.012f, 12.f, 22.f);
  float const previewHeight = std::clamp(itemWidth * 0.58f, 104.f, 162.f);
  float const itemHeight = previewHeight + 80.f;
  std::size_t maxVisible = static_cast<std::size_t>(std::max(1.f, std::floor((availableWidth + gap) / (itemWidth + gap))));
  maxVisible = std::min<std::size_t>(maxVisible, 7u);
  if (maxVisible > 2u && maxVisible % 2u == 0u) --maxVisible;
  WindowCyclerVisibleRange const visible =
      windowCyclerVisibleRange(list.surfaces.size(), list.selectedIndex, maxVisible);
  if (visible.count == 0u) return;

  float const stripWidth = static_cast<float>(visible.count) * itemWidth +
                           static_cast<float>(visible.count - 1u) * gap;
  float const panelPadding = 18.f;
  Rect const panelRect = Rect::sharp((outputWidth - stripWidth) * 0.5f - panelPadding,
                                     std::max(40.f, outputHeight * 0.5f - (itemHeight + panelPadding * 2.f) * 0.5f),
                                     stripWidth + panelPadding * 2.f,
                                     itemHeight + panelPadding * 2.f);

  canvas.drawRect(Rect::sharp(0.f, 0.f, outputWidth, outputHeight),
                  CornerRadius{},
                  FillStyle::solid(Color{0.f, 0.f, 0.f, 0.20f}),
                  StrokeStyle::none(),
                  ShadowStyle::none());
  canvas.drawBackdropBlur(panelRect, 22.f, Color{0.03f, 0.035f, 0.045f, 0.48f}, CornerRadius{8.f});
  canvas.drawRect(panelRect,
                  CornerRadius{8.f},
                  FillStyle::solid(Color{0.04f, 0.045f, 0.055f, 0.46f}),
                  StrokeStyle::solid(Color{1.f, 1.f, 1.f, 0.18f}, 1.f),
                  ShadowStyle{.radius = 24.f, .offset = {0.f, 12.f}, .color = Color{0.f, 0.f, 0.f, 0.34f}});

  float const firstX = (outputWidth - stripWidth) * 0.5f;
  float const itemY = panelRect.y + panelPadding;
  for (std::size_t visibleIndex = 0; visibleIndex < visible.count; ++visibleIndex) {
    std::size_t const surfaceIndex = visible.start + visibleIndex;
    Rect const itemRect = Rect::sharp(firstX + static_cast<float>(visibleIndex) * (itemWidth + gap),
                                      itemY,
                                      itemWidth,
                                      itemHeight);
    drawWindowCyclerItem(wayland,
                         canvas,
                         textSystem,
                         renderState,
                         *list.surfaces[surfaceIndex],
                         itemRect,
                         surfaceIndex == list.selectedIndex,
                         frameTime,
                         chrome,
                         animationsEnabled);
  }
}

bool surfaceEligibleForFullscreenDirectScanout(CommittedSurfaceSnapshot const& surface,
                                               platform::KmsAtomicPresenter::OverlayCandidate const& candidate,
                                               WaylandServer const& wayland,
                                               platform::KmsOutput const& output,
                                               CursorRenderState const& cursorState,
                                               bool hardwareCursorEnabled,
                                               bool hardwareCursorAvailable,
                                               float screenshotFlashOpacity) {
  if (std::getenv("LAMBDA_COMPOSITOR_DISABLE_FULLSCREEN_DIRECT_SCANOUT")) return false;
  if (!surface.fullscreen) return false;
  if (screenshotFlashOpacity > 0.001f) return false;
  if (surface.x != 0 || surface.y != 0 ||
      surface.width != wayland.logicalOutputWidth() ||
      surface.height != wayland.logicalOutputHeight()) {
    return false;
  }
  if (candidate.crtcX != 0 || candidate.crtcY != 0 ||
      candidate.crtcWidth != output.width() || candidate.crtcHeight != output.height()) {
    return false;
  }
  if (!wayland.cursorSurface()) return true;
  return hardwareCursorEnabled && hardwareCursorAvailable && cursorState.hardwareVisible;
}

bool overlayPrimaryReuseCursorReady(WaylandServer& wayland,
                                    platform::KmsOutput const& output,
                                    CursorRenderState const& cursorState,
                                    bool hardwareCursorEnabled) {
  if (!hardwareCursorEnabled || !cursorState.hardwareVisible) return true;
  return moveCurrentHardwareCursor(wayland, output, cursorState, true);
}

double renderMilliseconds(CompositorRenderFrameContext const& ctx,
                          std::chrono::steady_clock::time_point renderStart) {
  return ctx.detailedFrameProfile
             ? presentation::LoopInstrumentation::milliseconds(renderStart, presentation::LoopInstrumentation::Clock::now())
             : 0.0;
}

using RegionRect = CommittedSurfaceSnapshot::RegionRect;

Rect logicalRect(RegionRect const& rect) {
  return Rect::sharp(static_cast<float>(rect.x),
                     static_cast<float>(rect.y),
                     static_cast<float>(rect.width),
                     static_cast<float>(rect.height));
}

std::optional<Rect> logicalDamageBounds(SceneDamageResult const& damage) {
  std::optional<RegionRect> bounds;
  for (RegionRect const& rect : damage.rects) {
    if (rect.width <= 0 || rect.height <= 0) continue;
    if (!bounds) {
      bounds = rect;
      continue;
    }
    std::int32_t const left = std::min(bounds->x, rect.x);
    std::int32_t const top = std::min(bounds->y, rect.y);
    std::int32_t const right = std::max(bounds->x + bounds->width, rect.x + rect.width);
    std::int32_t const bottom = std::max(bounds->y + bounds->height, rect.y + rect.height);
    bounds = RegionRect{
        .x = left,
        .y = top,
        .width = right - left,
        .height = bottom - top,
    };
  }
  if (!bounds) return std::nullopt;
  return logicalRect(*bounds);
}

std::uint64_t damageArea(SceneDamageResult const& damage) {
  std::uint64_t area = 0;
  for (RegionRect const& rect : damage.rects) {
    if (rect.width <= 0 || rect.height <= 0) continue;
    area += static_cast<std::uint64_t>(rect.width) * static_cast<std::uint64_t>(rect.height);
  }
  return area;
}

bool sceneUsesBackdropSampling(std::vector<CommittedSurfaceSnapshot> const& surfaces, ChromeConfig const& chrome) {
  bool const chromeSamplesBackdrop = chrome.glass.blurRadius > 0.f;
  for (CommittedSurfaceSnapshot const& surface : surfaces) {
    if (chromeSamplesBackdrop && windowExternalTitleBarHeight(surface) > 0.f) {
      return true;
    }
    if (!surface.backgroundBlurRects.empty() && surface.backgroundEffect.blurRadius > 0.f) {
      return true;
    }
  }
  return false;
}

std::vector<platform::KmsAtomicPresenter::DamageRect>
physicalDamageRects(SceneDamageResult const& damage,
                    platform::KmsOutput const& output,
                    std::int32_t logicalWidth,
                    std::int32_t logicalHeight) {
  std::vector<platform::KmsAtomicPresenter::DamageRect> rects;
  std::int32_t const outputWidth = static_cast<std::int32_t>(output.width());
  std::int32_t const outputHeight = static_cast<std::int32_t>(output.height());
  if (damage.fullOutput || damage.empty() || logicalWidth <= 0 || logicalHeight <= 0 ||
      outputWidth <= 0 || outputHeight <= 0) {
    return rects;
  }
  double const scaleX = static_cast<double>(outputWidth) / static_cast<double>(logicalWidth);
  double const scaleY = static_cast<double>(outputHeight) / static_cast<double>(logicalHeight);
  rects.reserve(damage.rects.size());
  for (RegionRect const& rect : damage.rects) {
    if (rect.width <= 0 || rect.height <= 0) continue;
    std::int32_t const x1 = std::clamp(static_cast<std::int32_t>(std::floor(rect.x * scaleX)), 0, outputWidth);
    std::int32_t const y1 = std::clamp(static_cast<std::int32_t>(std::floor(rect.y * scaleY)), 0, outputHeight);
    std::int32_t const x2 =
        std::clamp(static_cast<std::int32_t>(std::ceil((rect.x + rect.width) * scaleX)), 0, outputWidth);
    std::int32_t const y2 =
        std::clamp(static_cast<std::int32_t>(std::ceil((rect.y + rect.height) * scaleY)), 0, outputHeight);
    if (x2 <= x1 || y2 <= y1) continue;
    rects.push_back(platform::KmsAtomicPresenter::DamageRect{
        .x = x1,
        .y = y1,
        .width = static_cast<std::uint32_t>(x2 - x1),
        .height = static_cast<std::uint32_t>(y2 - y1),
    });
  }
  return rects;
}

struct AtomicReadyFrameOptions {
  bool overlayOnly = false;
  bool directScanout = false;
  std::shared_ptr<platform::KmsAtomicPresenter::OverlayCandidate> scanoutCandidate;
};

void storeAtomicReadyFrame(
    CompositorRenderFrameContext& ctx,
    std::uint32_t presentToken,
    PresentationTiming presentationTiming,
    std::size_t surfaceCount,
    std::chrono::steady_clock::time_point frameTime,
    std::chrono::steady_clock::time_point renderStart,
    bool renderAheadFrame,
    presentation::AtomicFrameProfile const& profile,
    CompositorSceneGraphState&& sceneGraphState,
    std::vector<std::uint64_t> const& frameCallbackSurfaceIds,
    AtomicReadyFrameOptions options = {}) {
  if (!ctx.atomicReadyFrame || !ctx.atomicFrameDirty || !ctx.lastKnownContentSerial) return;
  *ctx.atomicReadyFrame = AtomicReadyFrame{
      .ready = true,
      .presentToken = presentToken,
      .timing = presentationTiming,
      .surfaceCount = surfaceCount,
      .frameTime = frameTime,
      .renderMs = renderMilliseconds(ctx, renderStart),
      .renderedAhead = renderAheadFrame,
      .overlayOnly = options.overlayOnly,
      .directScanout = options.directScanout,
      .contentSerial = ctx.wayland.contentSerial(),
      .profile = profile,
      .sceneGraphState = std::move(sceneGraphState),
      .sceneGraphStateValid = true,
      .frameCallbackSurfaceIds = frameCallbackSurfaceIds,
      .scanoutCandidate = std::move(options.scanoutCandidate),
  };
  *ctx.atomicFrameDirty = false;
  *ctx.lastKnownContentSerial = ctx.atomicReadyFrame->contentSerial;
}

} // namespace

void renderCompositorFrame(CompositorRenderFrameContext& ctx,
                           std::chrono::steady_clock::time_point frameTime,
                           std::chrono::steady_clock::time_point renderStart,
                           PresentationTiming presentationTiming,
                           bool renderAheadFrame) {
  auto const profileNow = [&] {
    return ctx.detailedFrameProfile ? presentation::CompositorFrameProfile::Clock::now()
                                    : presentation::CompositorFrameProfile::Clock::time_point{};
  };
  auto const profileMs = [&](presentation::CompositorFrameProfile::Clock::time_point start) {
    return ctx.detailedFrameProfile ? presentation::CompositorFrameProfile::milliseconds(start) : 0.0;
  };
  auto const frameProfileStart = profileNow();
  auto phaseStart = frameProfileStart;
  presentation::AtomicFrameProfile atomicFrameProfile{};
  platform::KmsAtomicPresenter* atomicPresenter = ctx.presenter.atomicPresenter();
  std::size_t committedSurfaceCount = 0;
  if (ctx.idleBlanked) {
    std::vector<CommittedSurfaceSnapshot> emptySurfaces;
    std::optional<CommittedSurfaceSnapshot> emptyCursor;
    CompositorSceneFramePlan scenePlan =
        buildCompositorSceneFrame(ctx.surfaceRenderState.sceneGraph,
                                  CompositorSceneFrameInput{
                                      .wayland = ctx.wayland,
                                      .output = ctx.output,
                                      .atomicPresenter = atomicPresenter,
                                      .chrome = ctx.appliedConfig.config.chrome,
                                      .surfaceVisuals = ctx.surfaceRenderState.surfaceVisuals,
                                      .surfaces = emptySurfaces,
                                      .softwareCursor = emptyCursor,
                                      .frameTime = frameTime,
                                      .logicalOutputWidth = ctx.wayland.logicalOutputWidth(),
                                      .logicalOutputHeight = ctx.wayland.logicalOutputHeight(),
                                      .dpiScale = ctx.canvas.dpiScale(),
                                      .animationsEnabled = ctx.appliedConfig.config.animationsEnabled,
                                      .forceFullDamage = true,
                                      .selectScanout = false,
                                  });
    if (atomicPresenter) (void)atomicPresenter->prepareFrame();
    ctx.canvas.beginFrame();
    ctx.canvas.clear(Color{0.f, 0.f, 0.f, 1.f});
    ctx.output.hideCursor();
    atomicFrameProfile.backgroundMs = profileMs(phaseStart);
    ctx.frameProfile.backgroundMs += atomicFrameProfile.backgroundMs;
    phaseStart = profileNow();
    auto const canvasPresentStart = profileNow();
    ctx.canvas.present();
    atomicFrameProfile.canvasPresentMs = profileMs(canvasPresentStart);
    std::uint32_t presentToken = 0;
    if (atomicPresenter) {
      auto const kmsPresentStart = profileNow();
      presentToken = atomicPresenter->markFrameRendered();
      atomicFrameProfile.kmsPresentMs = profileMs(kmsPresentStart);
    }
    atomicFrameProfile.presentMs = profileMs(phaseStart);
    atomicFrameProfile.totalMs = profileMs(frameProfileStart);
    ctx.frameProfile.presentMs += atomicFrameProfile.presentMs;
    ++ctx.frameProfile.frames;
    ctx.frameProfile.totalMs += atomicFrameProfile.totalMs;
    diagnostics::recordCpuFrame({
        .surfaces = committedSurfaceCount,
        .backgroundMs = atomicFrameProfile.backgroundMs,
        .snapshotMs = atomicFrameProfile.snapshotMs,
        .surfaceMs = atomicFrameProfile.surfaceMs,
        .closingMs = atomicFrameProfile.closingMs,
        .cursorMs = atomicFrameProfile.cursorMs,
        .presentMs = atomicFrameProfile.presentMs,
        .canvasPresentMs = atomicFrameProfile.canvasPresentMs,
        .kmsPresentMs = atomicFrameProfile.kmsPresentMs,
        .totalMs = atomicFrameProfile.totalMs,
    });
    ctx.frameProfile.maybeLog();
    ctx.loopStats.recordRender(renderStart);
    if (atomicPresenter) {
      storeAtomicReadyFrame(ctx,
                            presentToken,
                            presentationTiming,
                            committedSurfaceCount,
                            frameTime,
                            renderStart,
                            renderAheadFrame,
                            atomicFrameProfile,
                            std::move(scenePlan.nextState),
                            {});
    }
    if (!atomicPresenter) {
      ctx.surfaceRenderState.sceneGraph = std::move(scenePlan.nextState);
    }
    return;
  }
  auto snapPreview = ctx.wayland.snapPreview();
  bool snapPreviewDrawn = false;
  auto committedSurfaces = ctx.wayland.committedSurfaces();
  committedSurfaceCount = committedSurfaces.size();
  auto screenshotOverlay = ctx.wayland.screenshotSelectionOverlay();
  auto windowCyclerOverlay = ctx.wayland.windowCyclerOverlay();
  std::optional<CommittedSurfaceSnapshot> softwareCursorSnapshot;
  if (!ctx.appliedConfig.config.hardwareCursorEnabled || !ctx.hardwareCursorAvailable) {
    softwareCursorSnapshot = ctx.wayland.cursorSurface();
  }
  bool const forceFullSceneDamage =
      snapPreview.has_value() ||
      screenshotOverlay.has_value() ||
      windowCyclerOverlay.has_value() ||
      ctx.screenshotFlashOpacity > 0.001f;
  CompositorSceneFramePlan scenePlan =
      buildCompositorSceneFrame(ctx.surfaceRenderState.sceneGraph,
                                CompositorSceneFrameInput{
                                    .wayland = ctx.wayland,
                                    .output = ctx.output,
                                    .atomicPresenter = atomicPresenter,
                                    .chrome = ctx.appliedConfig.config.chrome,
                                    .surfaceVisuals = ctx.surfaceRenderState.surfaceVisuals,
                                    .surfaces = committedSurfaces,
                                    .softwareCursor = softwareCursorSnapshot,
                                    .frameTime = frameTime,
                                    .logicalOutputWidth = ctx.wayland.logicalOutputWidth(),
                                    .logicalOutputHeight = ctx.wayland.logicalOutputHeight(),
                                    .dpiScale = ctx.canvas.dpiScale(),
                                    .animationsEnabled = ctx.appliedConfig.config.animationsEnabled,
                                    .forceFullDamage = forceFullSceneDamage,
                                    .selectScanout = atomicPresenter && !snapPreview && !screenshotOverlay &&
                                                     !windowCyclerOverlay,
                                });
  std::vector<std::uint64_t> const& frameCallbackSurfaceIds = scenePlan.frameCallbackSurfaceIds;
  SceneDamageResult const& sceneDamage = scenePlan.damage;
  LAMBDA_WINDOW_MANAGER_TRACE_PACING("scene-damage full=%d rects=%zu area=%llu empty=%d surfaces=%zu\n",
                            sceneDamage.fullOutput ? 1 : 0,
                            sceneDamage.rectCount(),
                            static_cast<unsigned long long>(damageArea(sceneDamage)),
                            sceneDamage.empty() ? 1 : 0,
                            committedSurfaces.size());
  bool const collectAgeProfile = ctx.detailedFrameProfile;
  std::uint64_t const renderSnapshotNsec = collectAgeProfile ? presentation::monotonicNanoseconds() : 0;
  auto ageMs = [renderSnapshotNsec](std::uint64_t thenNsec) {
    return thenNsec > 0 && renderSnapshotNsec >= thenNsec
               ? static_cast<double>(renderSnapshotNsec - thenNsec) / 1'000'000.0
               : 0.0;
  };
  for (auto const& surface : committedSurfaces) {
    if (surface.pacingSizing) ++atomicFrameProfile.activeSizingSurfaces;
    if (surface.bufferWidth * surface.bufferHeight >
        atomicFrameProfile.maxBufferWidth * atomicFrameProfile.maxBufferHeight) {
      atomicFrameProfile.maxBufferWidth = surface.bufferWidth;
      atomicFrameProfile.maxBufferHeight = surface.bufferHeight;
      atomicFrameProfile.maxDmabufFormat = surface.dmabufFormat;
      atomicFrameProfile.maxDmabufModifier =
          surface.dmabufPlanes.empty() ? 0 : surface.dmabufPlanes.front().modifier;
    }
    if (surface.width * surface.height > atomicFrameProfile.maxFrameWidth * atomicFrameProfile.maxFrameHeight) {
      atomicFrameProfile.maxFrameWidth = surface.width;
      atomicFrameProfile.maxFrameHeight = surface.height;
    }
    if (surface.pacingSizing && collectAgeProfile) {
      double const commitToRenderMs = ageMs(surface.lastCommitNsec);
      double const inputToRenderMs = ageMs(surface.lastResizeInputNsec);
      double const configureToRenderMs = ageMs(surface.lastConfigureSentNsec);
      double const ackToRenderMs = ageMs(surface.lastConfigureAckNsec);
      double const configureToCommitMs =
          surface.lastConfigureSentNsec > 0 && surface.lastCommitNsec >= surface.lastConfigureSentNsec
              ? static_cast<double>(surface.lastCommitNsec - surface.lastConfigureSentNsec) / 1'000'000.0
              : 0.0;
      if (commitToRenderMs >= atomicFrameProfile.maxCommitToRenderMs) {
        atomicFrameProfile.maxAgeSurfaceId = surface.id;
        atomicFrameProfile.maxCommitToRenderMs = commitToRenderMs;
      }
      atomicFrameProfile.maxInputToRenderMs =
          std::max(atomicFrameProfile.maxInputToRenderMs, inputToRenderMs);
      atomicFrameProfile.maxConfigureToRenderMs =
          std::max(atomicFrameProfile.maxConfigureToRenderMs, configureToRenderMs);
      atomicFrameProfile.maxAckToRenderMs = std::max(atomicFrameProfile.maxAckToRenderMs, ackToRenderMs);
      atomicFrameProfile.maxConfigureToCommitMs =
          std::max(atomicFrameProfile.maxConfigureToCommitMs, configureToCommitMs);
      LAMBDA_WINDOW_MANAGER_TRACE_PACING("surface-age surface=%llu frame=%dx%d buffer=%dx%d serial=%llu "
                                "configureSerial=%u configure=%dx%d inputToRender=%.3fms "
                                "configureToRender=%.3fms ackToRender=%.3fms commitToRender=%.3fms "
                                "configureToCommit=%.3fms activeSizing=%d pacingSizing=%d\n",
                                static_cast<unsigned long long>(surface.id),
                                surface.width,
                                surface.height,
                                surface.bufferWidth,
                                surface.bufferHeight,
                                static_cast<unsigned long long>(surface.serial),
                                surface.lastConfigureSerial,
                                surface.lastConfigureWidth,
                                surface.lastConfigureHeight,
                                inputToRenderMs,
                                configureToRenderMs,
                                ackToRenderMs,
                                commitToRenderMs,
                                configureToCommitMs,
                                surface.activeSizing ? 1 : 0,
                                surface.pacingSizing ? 1 : 0);
    }
  }
  ctx.loopStats.lastSurfaceCount = committedSurfaces.size();
  atomicFrameProfile.snapshotMs = profileMs(phaseStart);
  ctx.frameProfile.snapshotMs += atomicFrameProfile.snapshotMs;
  ctx.frameProfile.surfaces += committedSurfaces.size();
  std::unordered_set<std::uint64_t> liveSurfaceIds;
  liveSurfaceIds.reserve(committedSurfaces.size());
  std::uint64_t overlaySurfaceId = 0;
  std::optional<CompositorHardwareScanoutSelection> pendingOverlay = std::move(scenePlan.scanout);
  if (atomicPresenter && !snapPreview && !screenshotOverlay && !windowCyclerOverlay) {
    overlaySurfaceId = pendingOverlay ? pendingOverlay->candidate.surfaceId : 0;
    if (!pendingOverlay) {
      atomicPresenter->clearPreparedOverlayCandidate();
      atomicPresenter->clearPreparedDirectScanout();
      ctx.wayland.setRetainedDmabufBufferIds(atomicPresenter->overlayBufferIdsInUse());
    }
  } else if (atomicPresenter) {
    atomicPresenter->clearPreparedOverlayCandidate();
    atomicPresenter->clearPreparedDirectScanout();
    ctx.wayland.setRetainedDmabufBufferIds(atomicPresenter->overlayBufferIdsInUse());
  }
  if (atomicPresenter && pendingOverlay && pendingOverlay->surfaceIndex < committedSurfaces.size() &&
      compositorSceneScanoutCoversOutput(*pendingOverlay, ctx.output) &&
      surfaceEligibleForFullscreenDirectScanout(committedSurfaces[pendingOverlay->surfaceIndex],
                                                pendingOverlay->candidate,
                                                ctx.wayland,
                                                ctx.output,
                                                ctx.cursorState,
                                                ctx.appliedConfig.config.hardwareCursorEnabled,
                                                ctx.hardwareCursorAvailable,
                                                ctx.screenshotFlashOpacity)) {
    std::uint64_t const candidateSurfaceId = pendingOverlay->candidate.surfaceId;
    std::uint64_t const candidateBufferId = pendingOverlay->candidate.bufferId;
    bool const cursorMoved =
        moveCurrentHardwareCursor(ctx.wayland,
                                  ctx.output,
                                  ctx.cursorState,
                                  ctx.appliedConfig.config.hardwareCursorEnabled && ctx.hardwareCursorAvailable);
    platform::KmsAtomicPresenter::OverlayCandidate directCandidate =
        cursorMoved ? duplicateCompositorSceneOverlayCandidate(pendingOverlay->candidate)
                    : platform::KmsAtomicPresenter::OverlayCandidate{};
    auto const directPresentStart = profileNow();
    bool directPrepared = false;
    bool directDeferred = false;
    if (cursorMoved && !directCandidate.planes.empty()) {
      if (renderAheadFrame) {
        directDeferred = true;
        (void)atomicPresenter->primeDirectScanoutCandidate(directCandidate);
      } else {
        directPrepared = atomicPresenter->prepareDirectScanoutCandidate(std::move(directCandidate));
      }
    }
    if (directPrepared || directDeferred) {
      closeCompositorSceneOverlayCandidate(pendingOverlay->candidate);
      ctx.surfaceRenderState.clientImages.erase(candidateSurfaceId);
      atomicFrameProfile.presentMs = profileMs(directPresentStart);
      atomicFrameProfile.totalMs = profileMs(frameProfileStart);
      diagnostics::recordCpuFrame({
          .surfaces = committedSurfaceCount,
          .snapshotMs = atomicFrameProfile.snapshotMs,
          .presentMs = atomicFrameProfile.presentMs,
          .totalMs = atomicFrameProfile.totalMs,
      });
      ctx.loopStats.recordRender(renderStart);
      ctx.wayland.setRetainedDmabufBufferIds(atomicPresenter->overlayBufferIdsInUse());
      clearCompositorScenePrimaryReuse(scenePlan.nextState);
      storeAtomicReadyFrame(ctx,
                            0,
                            presentationTiming,
                            committedSurfaceCount,
                            frameTime,
                            renderStart,
                            renderAheadFrame,
                            atomicFrameProfile,
                            std::move(scenePlan.nextState),
                            frameCallbackSurfaceIds,
                            AtomicReadyFrameOptions{
                                .directScanout = true,
                                .scanoutCandidate = directDeferred
                                                        ? ownCompositorSceneOverlayCandidate(std::move(directCandidate))
                                                        : nullptr,
                            });
      LAMBDA_WINDOW_MANAGER_TRACE_PACING("direct-scanout surface=%llu buffer=%llu prepared=1 fullscreen=1\n",
                                static_cast<unsigned long long>(candidateSurfaceId),
                                static_cast<unsigned long long>(candidateBufferId));
      return;
    }
    LAMBDA_WINDOW_MANAGER_TRACE_PACING("direct-scanout surface=%llu buffer=%llu prepared=0 fullscreen=1\n",
                              static_cast<unsigned long long>(candidateSurfaceId),
                              static_cast<unsigned long long>(candidateBufferId));
  }
  bool overlayPreparedForFrame = false;
  if (atomicPresenter && pendingOverlay && atomicPresenter->canPrepareOverlayOnly() &&
      ctx.screenshotFlashOpacity <= 0.001f &&
      scenePlan.primaryReuseMatchesScanout) {
    bool const cursorReady =
        overlayPrimaryReuseCursorReady(ctx.wayland,
                                       ctx.output,
                                       ctx.cursorState,
                                       ctx.appliedConfig.config.hardwareCursorEnabled && ctx.hardwareCursorAvailable);
    if (cursorReady) {
      std::uint64_t const candidateSurfaceId = pendingOverlay->candidate.surfaceId;
      std::uint64_t const candidateBufferId = pendingOverlay->candidate.bufferId;
      auto const overlayPresentStart = profileNow();
      bool overlayPrepared = false;
      bool overlayDeferred = false;
      if (renderAheadFrame) {
        overlayDeferred = true;
      } else {
        overlayPrepared =
            atomicPresenter->prepareOverlayCandidateForDisplayedFrame(std::move(pendingOverlay->candidate));
      }
      if (overlayPrepared || overlayDeferred) {
        atomicFrameProfile.presentMs = profileMs(overlayPresentStart);
        atomicFrameProfile.totalMs = profileMs(frameProfileStart);
        diagnostics::recordCpuFrame({
            .surfaces = committedSurfaceCount,
            .snapshotMs = atomicFrameProfile.snapshotMs,
            .presentMs = atomicFrameProfile.presentMs,
            .totalMs = atomicFrameProfile.totalMs,
        });
        ctx.loopStats.recordRender(renderStart);
        ctx.wayland.setRetainedDmabufBufferIds(atomicPresenter->overlayBufferIdsInUse());
        storeAtomicReadyFrame(ctx,
                              0,
                              presentationTiming,
                              committedSurfaceCount,
                              frameTime,
                              renderStart,
                              renderAheadFrame,
                              atomicFrameProfile,
                              std::move(scenePlan.nextState),
                              frameCallbackSurfaceIds,
                              AtomicReadyFrameOptions{
                                  .overlayOnly = true,
                                  .scanoutCandidate = overlayDeferred
                                                          ? ownCompositorSceneOverlayCandidate(std::move(pendingOverlay->candidate))
                                                          : nullptr,
                              });
        LAMBDA_WINDOW_MANAGER_TRACE_PACING("hardware-overlay surface=%llu buffer=%llu prepared=1 overlayOnly=1\n",
                                  static_cast<unsigned long long>(candidateSurfaceId),
                                  static_cast<unsigned long long>(candidateBufferId));
        return;
      }
      rejectCompositorSceneScanout(ctx.surfaceRenderState.sceneGraph, *pendingOverlay);
      rejectCompositorSceneScanout(scenePlan.nextState, *pendingOverlay);
      overlaySurfaceId = 0;
      LAMBDA_WINDOW_MANAGER_TRACE_PACING("hardware-overlay surface=%llu buffer=%llu prepared=0 overlayOnly=1\n",
                                static_cast<unsigned long long>(candidateSurfaceId),
                                static_cast<unsigned long long>(candidateBufferId));
    }
  }

  bool const partialDamageCandidate =
      atomicPresenter &&
      !renderAheadFrame &&
      !pendingOverlay &&
      !sceneUsesBackdropSampling(committedSurfaces, ctx.appliedConfig.config.chrome) &&
      !sceneDamage.fullOutput &&
      !sceneDamage.empty();
  std::vector<platform::KmsAtomicPresenter::DamageRect> physicalDamage =
      partialDamageCandidate
          ? physicalDamageRects(sceneDamage,
                                ctx.output,
                                ctx.wayland.logicalOutputWidth(),
                                ctx.wayland.logicalOutputHeight())
          : std::vector<platform::KmsAtomicPresenter::DamageRect>{};
  bool partialDamageFrame = false;
  if (atomicPresenter) {
    if (!physicalDamage.empty()) {
      partialDamageFrame = atomicPresenter->prepareFrame(physicalDamage);
    } else {
      (void)atomicPresenter->prepareFrame();
    }
    if (!partialDamageFrame) {
      physicalDamage.clear();
    }
  }
  LAMBDA_WINDOW_MANAGER_TRACE_PACING("scene-damage-render partial=%d logicalRects=%zu kmsRects=%zu\n",
                            partialDamageFrame ? 1 : 0,
                            partialDamageFrame ? sceneDamage.rects.size() : 0u,
                            physicalDamage.size());
  ctx.canvas.beginFrame();
  auto drawFrameContent = [&] {
    phaseStart = profileNow();
    drawCompositorBackground(ctx.canvas,
                             ctx.appliedConfig,
                             static_cast<std::uint32_t>(ctx.wayland.logicalOutputWidth()),
                             static_cast<std::uint32_t>(ctx.wayland.logicalOutputHeight()));
    double const backgroundMs = profileMs(phaseStart);
    atomicFrameProfile.backgroundMs += backgroundMs;
    ctx.frameProfile.backgroundMs += backgroundMs;
    phaseStart = profileNow();
    for (auto const& clientSurface : committedSurfaces) {
      if (snapPreview && !snapPreviewDrawn && snapPreview->surfaceId == clientSurface.id) {
        drawSnapPreview(ctx.canvas, *snapPreview, ctx.appliedConfig.config.chrome);
        snapPreviewDrawn = true;
      }
      liveSurfaceIds.insert(clientSurface.id);
      if (clientSurface.id == overlaySurfaceId) {
        ctx.surfaceRenderState.clientImages.erase(clientSurface.id);
        drawWindowFrameShadow(ctx.canvas, clientSurface, ctx.appliedConfig.config.chrome);
        drawWindowChrome(ctx.canvas, ctx.textSystem, clientSurface, ctx.appliedConfig.config.chrome);
        drawWindowFrameBorder(ctx.canvas, clientSurface, ctx.appliedConfig.config.chrome);
        continue;
      }
      auto& visual = ctx.surfaceRenderState.surfaceVisuals[clientSurface.id];
      auto& cached = ctx.surfaceRenderState.clientImages[clientSurface.id];
      drawCommittedSurface(ctx.wayland,
                           ctx.canvas,
                           ctx.textSystem,
                           clientSurface,
                           visual,
                           cached,
                           frameTime,
                           ctx.appliedConfig.config.chrome,
                           ctx.appliedConfig.config.animationsEnabled);
    }
    if (snapPreview && !snapPreviewDrawn) {
      drawSnapPreview(ctx.canvas, *snapPreview, ctx.appliedConfig.config.chrome);
    }
    double const surfaceMs = profileMs(phaseStart);
    atomicFrameProfile.surfaceMs += surfaceMs;
    ctx.frameProfile.surfaceMs += surfaceMs;
    phaseStart = profileNow();
    if (screenshotOverlay) {
      drawScreenshotSelectionOverlay(ctx.canvas, ctx.wayland, *screenshotOverlay);
    }
    if (windowCyclerOverlay) {
      drawWindowCyclerOverlay(ctx.wayland,
                              ctx.canvas,
                              ctx.textSystem,
                              ctx.surfaceRenderState,
                              *windowCyclerOverlay,
                              committedSurfaces,
                              frameTime,
                              ctx.appliedConfig.config.chrome,
                              ctx.appliedConfig.config.animationsEnabled);
    }
    double const closingMs = profileMs(phaseStart);
    atomicFrameProfile.closingMs += closingMs;
    ctx.frameProfile.closingMs += closingMs;
    phaseStart = profileNow();
    drawCompositorCursor(ctx.wayland,
                         ctx.canvas,
                         ctx.output,
                         ctx.cursorState,
                         ctx.appliedConfig.config.cursorTheme,
                         ctx.appliedConfig.config.cursorSize,
                         ctx.appliedConfig.config.hardwareCursorEnabled && ctx.hardwareCursorAvailable);
    drawScreenshotFlash(ctx.canvas, ctx.wayland, ctx.screenshotFlashOpacity);
    double const cursorMs = profileMs(phaseStart);
    atomicFrameProfile.cursorMs += cursorMs;
    ctx.frameProfile.cursorMs += cursorMs;
  };
  if (partialDamageFrame) {
    std::optional<Rect> const damageBounds = logicalDamageBounds(sceneDamage);
    if (damageBounds) {
      ctx.canvas.save();
      ctx.canvas.clipRect(*damageBounds);
      drawFrameContent();
      ctx.canvas.restore();
    } else {
      drawFrameContent();
    }
  } else {
    drawFrameContent();
  }
  pruneSurfaceRenderState(ctx.surfaceRenderState, liveSurfaceIds);
  phaseStart = profileNow();
  std::vector<PresentationCompletion> presentationCompletions;
  auto const canvasPresentStart = profileNow();
  ctx.canvas.present();
  atomicFrameProfile.canvasPresentMs = profileMs(canvasPresentStart);
  std::uint32_t presentToken = 0;
  if (atomicPresenter) {
    auto const kmsPresentStart = profileNow();
    presentToken = atomicPresenter->markFrameRendered();
    if (pendingOverlay) {
      std::uint64_t const candidateSurfaceId = pendingOverlay->candidate.surfaceId;
      std::uint64_t const candidateBufferId = pendingOverlay->candidate.bufferId;
      if (atomicPresenter->prepareOverlayCandidate(presentToken, std::move(pendingOverlay->candidate))) {
        overlaySurfaceId = atomicPresenter->preparedOverlaySurfaceId();
        overlayPreparedForFrame = true;
        LAMBDA_WINDOW_MANAGER_TRACE_PACING("hardware-overlay surface=%llu buffer=%llu prepared=1\n",
                                  static_cast<unsigned long long>(overlaySurfaceId),
                                  static_cast<unsigned long long>(candidateBufferId));
      } else {
        rejectCompositorSceneScanout(ctx.surfaceRenderState.sceneGraph, *pendingOverlay);
        rejectCompositorSceneScanout(scenePlan.nextState, *pendingOverlay);
        overlaySurfaceId = 0;
        LAMBDA_WINDOW_MANAGER_TRACE_PACING("hardware-overlay surface=%llu buffer=%llu prepared=0\n",
                                  static_cast<unsigned long long>(candidateSurfaceId),
                                  static_cast<unsigned long long>(candidateBufferId));
      }
      ctx.wayland.setRetainedDmabufBufferIds(atomicPresenter->overlayBufferIdsInUse());
    }
    atomicFrameProfile.kmsPresentMs = profileMs(kmsPresentStart);
  }
  if (overlayPreparedForFrame && overlaySurfaceId != 0) {
    rememberCompositorScenePrimaryReuse(scenePlan.nextState,
                                        overlaySurfaceId,
                                        scenePlan.primaryReuseSignatureForScanout);
  } else {
    clearCompositorScenePrimaryReuse(scenePlan.nextState);
  }
  atomicFrameProfile.presentMs = profileMs(phaseStart);
  atomicFrameProfile.totalMs = profileMs(frameProfileStart);
  if (atomicPresenter) {
    storeAtomicReadyFrame(ctx,
                          presentToken,
                          presentationTiming,
                          committedSurfaceCount,
                          frameTime,
                          renderStart,
                          renderAheadFrame,
                          atomicFrameProfile,
                          std::move(scenePlan.nextState),
                          frameCallbackSurfaceIds);
  } else if (!atomicPresenter) {
    if (!ctx.vulkanDisplayTimingSupportLogged && ctx.presenter.vulkanDisplayTimingAvailable()) {
      std::fprintf(stderr, "lambda-window-manager: Vulkan display timing available\n");
      ctx.vulkanDisplayTimingSupportLogged = true;
    }
    auto pastPresentationTimings = ctx.presenter.pollVulkanPresentationTimings();
    if (!pastPresentationTimings.empty()) {
      ctx.useVulkanPresentationCompletion = true;
      presentationCompletions.reserve(pastPresentationTimings.size());
      for (auto const& timing : pastPresentationTimings) {
        presentationCompletions.push_back(PresentationCompletion{
            .backendPresentId = timing.presentId,
            .monotonicNsec = timing.actualPresentTime,
        });
      }
    }
    if (ctx.useVulkanPresentationCompletion) {
      presentationTiming.backendPresentId = ctx.presenter.lastVulkanPresentId();
    }
  }
  ctx.frameProfile.presentMs += atomicFrameProfile.presentMs;
  ++ctx.frameProfile.frames;
  ctx.frameProfile.totalMs += atomicFrameProfile.totalMs;
  diagnostics::recordCpuFrame({
      .surfaces = committedSurfaceCount,
      .backgroundMs = atomicFrameProfile.backgroundMs,
      .snapshotMs = atomicFrameProfile.snapshotMs,
      .surfaceMs = atomicFrameProfile.surfaceMs,
      .closingMs = atomicFrameProfile.closingMs,
      .cursorMs = atomicFrameProfile.cursorMs,
      .presentMs = atomicFrameProfile.presentMs,
      .canvasPresentMs = atomicFrameProfile.canvasPresentMs,
      .kmsPresentMs = atomicFrameProfile.kmsPresentMs,
      .totalMs = atomicFrameProfile.totalMs,
  });
  ctx.frameProfile.maybeLog();
  ctx.loopStats.recordRender(renderStart);
  if (!atomicPresenter) {
    ctx.surfaceRenderState.sceneGraph = std::move(scenePlan.nextState);
    ctx.wayland.completePresentationFeedbacks(presentationCompletions, presentation::monotonicMilliseconds());
    ctx.wayland.sendFrameCallbacks(presentation::monotonicMilliseconds(),
                                   presentationTiming,
                                   frameCallbackSurfaceIds);
  }
}

} // namespace lambda::compositor
