#include "Compositor/SceneGraph/CompositorSceneGraph.hpp"

#include "Compositor/Chrome/ChromeMetrics.hpp"
#include "Compositor/Chrome/WindowFrameGeometry.hpp"
#include "Compositor/Surface/CommittedSurfaceSnapshotState.hpp"

#include <algorithm>
#include <cmath>
#include <drm_fourcc.h>
#include <string>
#include <unistd.h>
#include <unordered_map>

namespace lambdaui::compositor {
namespace {

using RegionRect = CommittedSurfaceSnapshot::RegionRect;

constexpr std::size_t kMaxDamageRects = 96;
constexpr std::uint64_t kDamageMergeAreaSlackDivisor = 5;
constexpr std::uint64_t kBackdropDamageFullOutputAreaPercent = 80;

std::uint64_t kindBits(CompositorSceneNodeKind kind) {
  return static_cast<std::uint64_t>(kind);
}

std::uint64_t nodeId(std::uint64_t surfaceId, CompositorSceneNodeKind kind) {
  return (surfaceId << 8u) ^ kindBits(kind);
}

void hashCombine(std::uint64_t& seed, std::uint64_t value) {
  seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
}

template <typename T>
void hashValue(std::uint64_t& seed, T value) {
  hashCombine(seed, static_cast<std::uint64_t>(value));
}

void hashValue(std::uint64_t& seed, float value) {
  hashCombine(seed, static_cast<std::uint64_t>(std::llround(static_cast<double>(value) * 1024.0)));
}

void hashValue(std::uint64_t& seed, double value) {
  hashCombine(seed, static_cast<std::uint64_t>(std::llround(value * 1024.0)));
}

void hashValue(std::uint64_t& seed, bool value) {
  hashCombine(seed, value ? 1u : 0u);
}

void hashValue(std::uint64_t& seed, std::string const& value) {
  hashCombine(seed, static_cast<std::uint64_t>(std::hash<std::string>{}(value)));
}

void hashColor(std::uint64_t& seed, Color color) {
  hashValue(seed, color.r);
  hashValue(seed, color.g);
  hashValue(seed, color.b);
  hashValue(seed, color.a);
}

void hashCornerRadius(std::uint64_t& seed, CornerRadius radius) {
  hashValue(seed, radius.topLeft);
  hashValue(seed, radius.topRight);
  hashValue(seed, radius.bottomRight);
  hashValue(seed, radius.bottomLeft);
}

struct SignatureVisitor {
  std::uint64_t& seed;

  template <typename T>
  void operator()(T value) const {
    hashValue(seed, value);
  }

  void operator()(std::string const& value) const {
    hashValue(seed, value);
  }
};

RegionRect rectFromRect(Rect const& rect) {
  std::int32_t const left = static_cast<std::int32_t>(std::floor(rect.x));
  std::int32_t const top = static_cast<std::int32_t>(std::floor(rect.y));
  std::int32_t const right = static_cast<std::int32_t>(std::ceil(rect.x + rect.width));
  std::int32_t const bottom = static_cast<std::int32_t>(std::ceil(rect.y + rect.height));
  return RegionRect{.x = left, .y = top, .width = right - left, .height = bottom - top};
}

RegionRect clippedRect(RegionRect rect, std::int32_t outputWidth, std::int32_t outputHeight) {
  std::int32_t const left = std::clamp(rect.x, 0, outputWidth);
  std::int32_t const top = std::clamp(rect.y, 0, outputHeight);
  std::int32_t const right = std::clamp(rect.x + rect.width, 0, outputWidth);
  std::int32_t const bottom = std::clamp(rect.y + rect.height, 0, outputHeight);
  return RegionRect{.x = left, .y = top, .width = right - left, .height = bottom - top};
}

RegionRect inflatedRect(RegionRect rect, std::int32_t amount) {
  if (amount <= 0) return rect;
  return RegionRect{
      .x = rect.x - amount,
      .y = rect.y - amount,
      .width = rect.width + amount * 2,
      .height = rect.height + amount * 2,
  };
}

bool rectEmpty(RegionRect const& rect) {
  return rect.width <= 0 || rect.height <= 0;
}

bool rectsOverlap(RegionRect const& a, RegionRect const& b) {
  std::int32_t const ax2 = a.x + std::max(0, a.width);
  std::int32_t const ay2 = a.y + std::max(0, a.height);
  std::int32_t const bx2 = b.x + std::max(0, b.width);
  std::int32_t const by2 = b.y + std::max(0, b.height);
  return a.x < bx2 && ax2 > b.x && a.y < by2 && ay2 > b.y;
}

bool rectContains(RegionRect const& outer, RegionRect const& inner) {
  std::int64_t const outerRight = static_cast<std::int64_t>(outer.x) + std::max(0, outer.width);
  std::int64_t const outerBottom = static_cast<std::int64_t>(outer.y) + std::max(0, outer.height);
  std::int64_t const innerRight = static_cast<std::int64_t>(inner.x) + std::max(0, inner.width);
  std::int64_t const innerBottom = static_cast<std::int64_t>(inner.y) + std::max(0, inner.height);
  return outer.x <= inner.x &&
         outer.y <= inner.y &&
         outerRight >= innerRight &&
         outerBottom >= innerBottom;
}

RegionRect unionRect(RegionRect const& a, RegionRect const& b) {
  std::int32_t const left = std::min(a.x, b.x);
  std::int32_t const top = std::min(a.y, b.y);
  std::int32_t const right = std::max(a.x + std::max(0, a.width), b.x + std::max(0, b.width));
  std::int32_t const bottom = std::max(a.y + std::max(0, a.height), b.y + std::max(0, b.height));
  return RegionRect{.x = left, .y = top, .width = right - left, .height = bottom - top};
}

std::uint64_t rectArea(RegionRect const& rect) {
  if (rect.width <= 0 || rect.height <= 0) return 0;
  return static_cast<std::uint64_t>(rect.width) * static_cast<std::uint64_t>(rect.height);
}

std::uint64_t damageArea(SceneDamageResult const& damage) {
  std::uint64_t area = 0;
  for (RegionRect const& rect : damage.rects) {
    area += rectArea(rect);
  }
  return area;
}

bool damageRectsMergeable(RegionRect const& a, RegionRect const& b) {
  RegionRect const merged = unionRect(a, b);
  std::uint64_t const combinedArea = rectArea(a) + rectArea(b);
  std::uint64_t const mergedArea = rectArea(merged);
  if (combinedArea == 0) return false;
  if (mergedArea <= combinedArea) return true;
  return mergedArea - combinedArea <= combinedArea / kDamageMergeAreaSlackDivisor;
}

void makeFullDamage(SceneDamageResult& damage, std::int32_t outputWidth, std::int32_t outputHeight) {
  damage.fullOutput = outputWidth > 0 && outputHeight > 0;
  damage.backgroundFillRequired = damage.fullOutput;
  damage.rects.clear();
  if (damage.fullOutput) {
    damage.rects.push_back(RegionRect{.x = 0, .y = 0, .width = outputWidth, .height = outputHeight});
  }
}

void appendDamageRect(SceneDamageResult& damage,
                      RegionRect rect,
                      std::int32_t outputWidth,
                      std::int32_t outputHeight,
                      bool backgroundFillRequired = true) {
  if (damage.fullOutput || outputWidth <= 0 || outputHeight <= 0) {
    if (damage.fullOutput && backgroundFillRequired) damage.backgroundFillRequired = true;
    return;
  }
  rect = clippedRect(rect, outputWidth, outputHeight);
  if (rectEmpty(rect)) return;
  damage.backgroundFillRequired = damage.backgroundFillRequired || backgroundFillRequired;
  if (rect.x <= 0 && rect.y <= 0 && rect.width >= outputWidth && rect.height >= outputHeight) {
    makeFullDamage(damage, outputWidth, outputHeight);
    return;
  }
  bool merged = true;
  while (merged) {
    merged = false;
    for (auto it = damage.rects.begin(); it != damage.rects.end(); ++it) {
      if (rectContains(*it, rect)) return;
      if (rectContains(rect, *it) || damageRectsMergeable(*it, rect)) {
        rect = clippedRect(unionRect(rect, *it), outputWidth, outputHeight);
        damage.rects.erase(it);
        if (rect.x <= 0 && rect.y <= 0 && rect.width >= outputWidth && rect.height >= outputHeight) {
          makeFullDamage(damage, outputWidth, outputHeight);
          return;
        }
        merged = true;
        break;
      }
    }
  }
  if (damage.rects.size() >= kMaxDamageRects) {
    makeFullDamage(damage, outputWidth, outputHeight);
    return;
  }
  damage.rects.push_back(rect);
}

std::int32_t backdropDamageInflation(std::vector<CommittedSurfaceSnapshot> const& surfaces,
                                     ChromeConfig const& chrome) {
  float maxBlurRadius = std::max(0.f, chrome.glass.blurRadius);
  for (CommittedSurfaceSnapshot const& surface : surfaces) {
    if (surface.backgroundBlurRects.empty()) continue;
    maxBlurRadius = std::max(maxBlurRadius, surface.backgroundEffect.blurRadius);
  }
  if (maxBlurRadius <= 0.f) return 0;
  return static_cast<std::int32_t>(std::ceil(maxBlurRadius * 1.5f + 2.f));
}

void inflateDamageForBackdropSampling(SceneDamageResult& damage,
                                      std::vector<CommittedSurfaceSnapshot> const& surfaces,
                                      ChromeConfig const& chrome,
                                      std::int32_t outputWidth,
                                      std::int32_t outputHeight) {
  if (damage.fullOutput || damage.rects.empty()) return;
  std::int32_t const inflation = backdropDamageInflation(surfaces, chrome);
  if (inflation <= 0) return;
  SceneDamageResult normalized;
  for (RegionRect const& rect : damage.rects) {
    RegionRect clipped = clippedRect(inflatedRect(rect, inflation), outputWidth, outputHeight);
    if (!rectEmpty(clipped)) {
      appendDamageRect(normalized, clipped, outputWidth, outputHeight, damage.backgroundFillRequired);
      if (normalized.fullOutput) {
        damage = std::move(normalized);
        return;
      }
    }
  }
  std::uint64_t const outputArea = static_cast<std::uint64_t>(outputWidth) *
                                  static_cast<std::uint64_t>(outputHeight);
  if (outputArea > 0 &&
      damageArea(normalized) * 100u >= outputArea * kBackdropDamageFullOutputAreaPercent) {
    makeFullDamage(damage, outputWidth, outputHeight);
    return;
  }
  damage.rects = std::move(normalized.rects);
}

RegionRect surfaceVisibleContentRect(CommittedSurfaceSnapshot const& surface,
                                     ChromeConfig const& chrome,
                                     float dpiScale) {
  return rectFromRect(windowVisibleContentRect(surface, chrome.contentInsetWidth, dpiScale));
}

RegionRect surfaceShadowBounds(CommittedSurfaceSnapshot const& surface, ChromeConfig const& chrome) {
  if (!surface.serverSideDecorated) return {};
  ShadowStyle const shadow{
      .radius = surface.focused ? chrome.focusedShadowRadius : chrome.unfocusedShadowRadius,
      .offset = surface.focused ? chrome.focusedShadowOffset : chrome.unfocusedShadowOffset,
      .color = surface.focused ? chrome.focusedShadowColor : chrome.unfocusedShadowColor,
  };
  if (shadow.isNone()) return {};
  WindowShadowLayerGeometry const layer =
      windowShadowLayerGeometry(windowFrameRect(surface, chrome.contentInsetWidth),
                                chrome.windowCornerRadius,
                                shadow,
                                std::max(0.f, shadow.radius + 1.f));
  return rectFromRect(layer.rect);
}

std::uint64_t chromeConfigSignature(ChromeConfig const& chrome) {
  std::uint64_t seed = 0x654df31cb8632a8bull;
  hashValue(seed, chrome.titleBarHeight);
  hashValue(seed, chrome.contentInsetWidth);
  hashValue(seed, chrome.controlsWidth);
  hashValue(seed, chrome.controlsInsetRight);
  hashValue(seed, chrome.controlsInsetTop);
  hashValue(seed, chrome.buttonSize);
  hashValue(seed, chrome.buttonRadius);
  hashValue(seed, chrome.buttonGap);
  hashValue(seed, chrome.glass.blurRadius);
  hashValue(seed, chrome.glass.opacity);
  hashColor(seed, chrome.glass.baseColor);
  hashColor(seed, chrome.glass.tintColor);
  hashColor(seed, chrome.glass.borderColor);
  hashColor(seed, chrome.glass.contrastColor);
  hashValue(seed, chrome.glass.focusedContrastOpacity);
  hashValue(seed, chrome.glass.unfocusedContrastOpacity);
  hashColor(seed, chrome.titleTextColor);
  hashValue(seed, chrome.titleTextFontSize);
  hashValue(seed, chrome.titleTextFontWeight);
  hashColor(seed, chrome.windowBorderColor);
  hashValue(seed, chrome.windowBorderWidth);
  hashColor(seed, chrome.borderLineColor);
  hashColor(seed, chrome.insetHighlightColor);
  hashColor(seed, chrome.focusedShadowColor);
  hashColor(seed, chrome.unfocusedShadowColor);
  hashCornerRadius(seed, chrome.windowCornerRadius);
  return seed;
}

std::uint64_t contentNodeSignature(CommittedSurfaceSnapshot const& surface) {
  std::uint64_t seed = 0xb7159e3779b97f4aull;
  SignatureVisitor const visit{seed};
  hashValue(seed, surface.id);
  hashValue(seed, surface.serial);
  hashValue(seed, surface.dmabufBufferId);
  hashValue(seed, surface.dmabufFormat);
  hashValue(seed, surface.pixelFormat);
  hashValue(seed, reinterpret_cast<std::uintptr_t>(surface.shmPixels));
  hashValue(seed, surface.shmPixelBytes);
  hashValue(seed, static_cast<std::uint64_t>(surface.dmabufPlanes.size()));
  for (auto const& plane : surface.dmabufPlanes) {
    hashValue(seed, plane.offset);
    hashValue(seed, plane.stride);
    hashValue(seed, plane.modifier);
  }
  visitCommittedSurfaceContentShape(surface, visit);
  return seed;
}

std::uint64_t chromeNodeSignature(CommittedSurfaceSnapshot const& surface, ChromeConfig const& chrome) {
  CommittedSurfaceSnapshot stable = surface;
  stable.closeButtonHovered = false;
  stable.closeButtonPressed = false;
  stable.maximizeButtonHovered = false;
  stable.maximizeButtonPressed = false;
  stable.minimizeButtonHovered = false;
  stable.minimizeButtonPressed = false;
  std::uint64_t seed = 0xb492b66fbe98f273ull;
  SignatureVisitor const visit{seed};
  visitCommittedSurfaceFrameVisualState(stable, visit);
  hashValue(seed, chromeConfigSignature(chrome));
  return seed;
}

std::uint64_t chromeControlsNodeSignature(CommittedSurfaceSnapshot const& surface, ChromeConfig const& chrome) {
  std::uint64_t seed = 0x5a06e3ad0e4f9b5dull;
  hashValue(seed, surface.closeButtonHovered);
  hashValue(seed, surface.closeButtonPressed);
  hashValue(seed, surface.maximizeButtonHovered);
  hashValue(seed, surface.maximizeButtonPressed);
  hashValue(seed, surface.minimizeButtonHovered);
  hashValue(seed, surface.minimizeButtonPressed);
  hashValue(seed, surface.focused);
  hashValue(seed, surface.fullscreen);
  hashValue(seed, chromeConfigSignature(chrome));
  return seed;
}

std::uint64_t shadowNodeSignature(CommittedSurfaceSnapshot const& surface, ChromeConfig const& chrome) {
  std::uint64_t seed = 0x9ae16a3b2f90404full;
  hashValue(seed, surface.id);
  hashValue(seed, surface.focused);
  hashValue(seed, surface.shadowClipTop);
  hashValue(seed, surface.shadowClipBottom);
  hashValue(seed, chromeConfigSignature(chrome));
  return seed;
}

RegionRect chromeControlsBounds(CommittedSurfaceSnapshot const& surface, ChromeConfig const& chrome) {
  if (!surface.serverSideDecorated) return {};
  Rect titleRect = windowUsesCutoutChrome(surface)
                       ? Rect::sharp(static_cast<float>(surface.x),
                                     static_cast<float>(surface.y),
                                     static_cast<float>(surface.width),
                                     static_cast<float>(chrome.titleBarHeight))
                       : windowTitleBarRect(surface, chrome.contentInsetWidth);
  if (titleRect.width <= 0.f || titleRect.height <= 0.f) return {};
  ChromeControlRects const rects =
      chromeControlRects(chrome, titleRect.x, titleRect.y, titleRect.width, titleRect.height);
  float const left = std::min({rects.minimizeButton.x, rects.maximizeButton.x, rects.closeButton.x});
  float const top = std::min({rects.minimizeButton.y, rects.maximizeButton.y, rects.closeButton.y});
  float const right = std::max({rects.minimizeButton.x + rects.minimizeButton.width,
                                rects.maximizeButton.x + rects.maximizeButton.width,
                                rects.closeButton.x + rects.closeButton.width});
  float const bottom = std::max({rects.minimizeButton.y + rects.minimizeButton.height,
                                 rects.maximizeButton.y + rects.maximizeButton.height,
                                 rects.closeButton.y + rects.closeButton.height});
  return rectFromRect(Rect::sharp(left, top, right - left, bottom - top));
}

std::uint64_t cursorNodeSignature(CommittedSurfaceSnapshot const& cursor) {
  std::uint64_t seed = 0x243f6a8885a308d3ull;
  SignatureVisitor const visit{seed};
  hashValue(seed, cursor.id);
  hashValue(seed, cursor.serial);
  visitCommittedSurfaceContentMapping(cursor, visit);
  hashValue(seed, cursor.dmabufBufferId);
  hashValue(seed, reinterpret_cast<std::uintptr_t>(cursor.shmPixels));
  hashValue(seed, cursor.shmPixelBytes);
  return seed;
}

void appendSurfaceNodes(std::vector<CompositorSceneNodeSnapshot>& nodes,
                        CommittedSurfaceSnapshot const& surface,
                        ChromeConfig const& chrome,
                        float dpiScale) {
  RegionRect const shadow = surfaceShadowBounds(surface, chrome);
  if (!rectEmpty(shadow)) {
    nodes.push_back({
        .id = nodeId(surface.id, CompositorSceneNodeKind::WindowShadow),
        .surfaceId = surface.id,
        .kind = CompositorSceneNodeKind::WindowShadow,
        .bounds = shadow,
        .signature = shadowNodeSignature(surface, chrome),
        .primaryPlane = true,
    });
  }

  if (surface.serverSideDecorated) {
    nodes.push_back({
        .id = nodeId(surface.id, CompositorSceneNodeKind::WindowChrome),
        .surfaceId = surface.id,
        .kind = CompositorSceneNodeKind::WindowChrome,
        .bounds = rectFromRect(windowFrameRect(surface, chrome.contentInsetWidth)),
        .signature = chromeNodeSignature(surface, chrome),
        .primaryPlane = true,
    });
    RegionRect const controls = chromeControlsBounds(surface, chrome);
    if (!rectEmpty(controls)) {
      nodes.push_back({
          .id = nodeId(surface.id, CompositorSceneNodeKind::WindowChromeControls),
          .surfaceId = surface.id,
          .kind = CompositorSceneNodeKind::WindowChromeControls,
          .bounds = controls,
          .signature = chromeControlsNodeSignature(surface, chrome),
          .primaryPlane = true,
      });
    }
  }

  nodes.push_back({
      .id = nodeId(surface.id, CompositorSceneNodeKind::WindowContent),
      .surfaceId = surface.id,
      .kind = CompositorSceneNodeKind::WindowContent,
      .bounds = surfaceVisibleContentRect(surface, chrome, dpiScale),
      .signature = contentNodeSignature(surface),
      .primaryPlane = true,
  });
}

std::vector<CompositorSceneNodeSnapshot>
buildSceneNodes(std::vector<CommittedSurfaceSnapshot> const& surfaces,
                std::optional<CommittedSurfaceSnapshot> const& cursor,
                ChromeConfig const& chrome,
                float dpiScale,
                std::int32_t outputWidth,
                std::int32_t outputHeight) {
  std::vector<CompositorSceneNodeSnapshot> nodes;
  nodes.reserve(surfaces.size() * 3u + (cursor ? 2u : 1u));
  nodes.push_back({
      .id = nodeId(0, CompositorSceneNodeKind::Background),
      .surfaceId = 0,
      .kind = CompositorSceneNodeKind::Background,
      .bounds = RegionRect{.x = 0, .y = 0, .width = outputWidth, .height = outputHeight},
      .signature = static_cast<std::uint64_t>(outputWidth) << 32u | static_cast<std::uint32_t>(outputHeight),
      .primaryPlane = true,
  });
  for (auto const& surface : surfaces) {
    appendSurfaceNodes(nodes, surface, chrome, dpiScale);
  }
  if (cursor) {
    nodes.push_back({
        .id = nodeId(cursor->id == 0 ? 1 : cursor->id, CompositorSceneNodeKind::SoftwareCursor),
        .surfaceId = cursor->id,
        .kind = CompositorSceneNodeKind::SoftwareCursor,
        .bounds = sceneSurfaceContentRect(*cursor),
        .signature = cursorNodeSignature(*cursor),
        .primaryPlane = true,
    });
  }
  return nodes;
}

bool nodeOrderChanged(std::vector<CompositorSceneNodeSnapshot> const& previous,
                      std::vector<CompositorSceneNodeSnapshot> const& current) {
  std::vector<std::uint64_t> previousIds;
  previousIds.reserve(previous.size());
  for (auto const& node : previous) {
    previousIds.push_back(node.id);
  }
  std::vector<std::uint64_t> currentIds;
  currentIds.reserve(current.size());
  for (auto const& node : current) {
    currentIds.push_back(node.id);
  }
  return previousIds != currentIds;
}

RegionRect mapBufferDamageToSurface(CommittedSurfaceSnapshot const& surface,
                                    RegionRect const& damage,
                                    ChromeConfig const& chrome,
                                    float dpiScale) {
  double const sourceX = surface.sourceX;
  double const sourceY = surface.sourceY;
  double const sourceWidth = surface.sourceWidth > 0.f
                                 ? static_cast<double>(surface.sourceWidth)
                                 : static_cast<double>(surface.bufferWidth);
  double const sourceHeight = surface.sourceHeight > 0.f
                                  ? static_cast<double>(surface.sourceHeight)
                                  : static_cast<double>(surface.bufferHeight);
  if (sourceWidth <= 0.0 || sourceHeight <= 0.0) {
    return surfaceVisibleContentRect(surface, chrome, dpiScale);
  }

  double const left = std::max(static_cast<double>(damage.x), sourceX);
  double const top = std::max(static_cast<double>(damage.y), sourceY);
  double const right = std::min(static_cast<double>(damage.x + damage.width), sourceX + sourceWidth);
  double const bottom = std::min(static_cast<double>(damage.y + damage.height), sourceY + sourceHeight);
  if (right <= left || bottom <= top) return {};

  Rect const visible = windowVisibleContentRect(surface, chrome.contentInsetWidth, dpiScale);
  double const scaleX = static_cast<double>(visible.width) / sourceWidth;
  double const scaleY = static_cast<double>(visible.height) / sourceHeight;
  double const mappedLeft = static_cast<double>(visible.x) + (left - sourceX) * scaleX;
  double const mappedTop = static_cast<double>(visible.y) + (top - sourceY) * scaleY;
  double const mappedRight = static_cast<double>(visible.x) + (right - sourceX) * scaleX;
  double const mappedBottom = static_cast<double>(visible.y) + (bottom - sourceY) * scaleY;

  std::int32_t const x0 = static_cast<std::int32_t>(std::floor(mappedLeft));
  std::int32_t const y0 = static_cast<std::int32_t>(std::floor(mappedTop));
  std::int32_t const x1 = static_cast<std::int32_t>(std::ceil(mappedRight));
  std::int32_t const y1 = static_cast<std::int32_t>(std::ceil(mappedBottom));
  return RegionRect{.x = x0, .y = y0, .width = x1 - x0, .height = y1 - y0};
}

bool opaqueRegionCoversOutputDamage(CommittedSurfaceSnapshot const& surface, RegionRect const& damage) {
  if (rectEmpty(damage)) return true;
  for (RegionRect const& opaque : surface.opaqueRegionRects) {
    RegionRect const outputOpaque{
        .x = surface.x + opaque.x,
        .y = surface.y + opaque.y,
        .width = opaque.width,
        .height = opaque.height,
    };
    if (rectContains(outputOpaque, damage)) return true;
  }
  return false;
}

bool contentDamageKnownOpaque(CommittedSurfaceSnapshot const& surface,
                              RegionRect const& damage,
                              ChromeConfig const& chrome,
                              float dpiScale) {
  if (rectEmpty(damage)) return true;
  RegionRect const visibleContent = surfaceVisibleContentRect(surface, chrome, dpiScale);
  if (!rectContains(visibleContent, damage)) return false;
  return surface.contentFullyOpaque || opaqueRegionCoversOutputDamage(surface, damage);
}

RetainedSceneSurfaceSnapshot retainedSceneSnapshot(CommittedSurfaceSnapshot const& snapshot,
                                                   ChromeConfig const& chrome) {
  return RetainedSceneSurfaceSnapshot{
      .id = snapshot.id,
      .serial = snapshot.serial,
      .x = snapshot.x,
      .y = snapshot.y,
      .width = snapshot.width,
      .height = snapshot.height,
      .committedWidth = snapshot.committedWidth,
      .committedHeight = snapshot.committedHeight,
      .bufferWidth = snapshot.bufferWidth,
      .bufferHeight = snapshot.bufferHeight,
      .bufferTransform = snapshot.bufferTransform,
      .sourceX = snapshot.sourceX,
      .sourceY = snapshot.sourceY,
      .sourceWidth = snapshot.sourceWidth,
      .sourceHeight = snapshot.sourceHeight,
      .destinationWidth = snapshot.destinationWidth,
      .destinationHeight = snapshot.destinationHeight,
      .chromeSignature = chromeNodeSignature(snapshot, chrome),
  };
}

std::vector<RetainedSceneSurfaceSnapshot>
retainedSceneSnapshots(std::vector<CommittedSurfaceSnapshot> const& surfaces,
                       ChromeConfig const& chrome) {
  std::vector<RetainedSceneSurfaceSnapshot> retained;
  retained.reserve(surfaces.size());
  for (auto const& surface : surfaces) {
    retained.push_back(retainedSceneSnapshot(surface, chrome));
  }
  return retained;
}

std::optional<RetainedSceneSurfaceSnapshot>
retainedSceneSnapshot(std::optional<CommittedSurfaceSnapshot> const& cursor,
                      ChromeConfig const& chrome) {
  if (!cursor) return std::nullopt;
  return retainedSceneSnapshot(*cursor, chrome);
}

std::unordered_map<std::uint64_t, RetainedSceneSurfaceSnapshot const*>
retainedSurfaceMap(std::vector<RetainedSceneSurfaceSnapshot> const& surfaces) {
  std::unordered_map<std::uint64_t, RetainedSceneSurfaceSnapshot const*> map;
  map.reserve(surfaces.size());
  for (auto const& surface : surfaces) {
    if (surface.id != 0) map[surface.id] = &surface;
  }
  return map;
}

std::unordered_map<std::uint64_t, CommittedSurfaceSnapshot const*>
surfaceMap(std::vector<CommittedSurfaceSnapshot> const& surfaces) {
  std::unordered_map<std::uint64_t, CommittedSurfaceSnapshot const*> map;
  map.reserve(surfaces.size());
  for (auto const& surface : surfaces) {
    if (surface.id != 0) map[surface.id] = &surface;
  }
  return map;
}

bool canMapBufferDamage(RetainedSceneSurfaceSnapshot const& previous,
                        CommittedSurfaceSnapshot const& current) {
  return previous.x == current.x &&
         previous.y == current.y &&
         previous.width == current.width &&
         previous.height == current.height &&
         previous.bufferWidth == current.bufferWidth &&
         previous.bufferHeight == current.bufferHeight &&
         previous.bufferTransform == current.bufferTransform &&
         previous.sourceX == current.sourceX &&
         previous.sourceY == current.sourceY &&
         previous.sourceWidth == current.sourceWidth &&
         previous.sourceHeight == current.sourceHeight &&
         previous.destinationWidth == current.destinationWidth &&
         previous.destinationHeight == current.destinationHeight &&
         current.bufferTransform == 0 &&
         current.bufferWidth > 0 &&
         current.bufferHeight > 0 &&
         current.width > 0 &&
         current.height > 0;
}

RegionRect sceneSurfaceContentRect(RetainedSceneSurfaceSnapshot const& surface) {
  return RegionRect{
      .x = surface.x,
      .y = surface.y,
      .width = std::max(0, surface.width),
      .height = std::max(0, surface.height),
  };
}

SceneDamageResult computeSceneDamage(CompositorSceneGraphState const& previous,
                                     std::vector<CompositorSceneNodeSnapshot> const& currentNodes,
                                     std::vector<CommittedSurfaceSnapshot> const& currentSurfaces,
                                     std::optional<CommittedSurfaceSnapshot> const& currentCursor,
                                     ChromeConfig const& chrome,
                                     float dpiScale,
                                     std::int32_t outputWidth,
                                     std::int32_t outputHeight,
                                     bool forceFullDamage) {
  SceneDamageResult damage;
  if (forceFullDamage ||
      !previous.initialized ||
      previous.outputWidth != outputWidth ||
      previous.outputHeight != outputHeight ||
      nodeOrderChanged(previous.nodes, currentNodes)) {
    makeFullDamage(damage, outputWidth, outputHeight);
    return damage;
  }

  auto const previousSurfacesById = retainedSurfaceMap(previous.surfaces);
  auto const currentSurfacesById = surfaceMap(currentSurfaces);

  std::unordered_map<std::uint64_t, CompositorSceneNodeSnapshot const*> previousNodesById;
  previousNodesById.reserve(previous.nodes.size());
  for (auto const& node : previous.nodes) {
    previousNodesById[node.id] = &node;
  }

  std::unordered_map<std::uint64_t, CompositorSceneNodeSnapshot const*> currentNodesById;
  currentNodesById.reserve(currentNodes.size());
  for (auto const& node : currentNodes) {
    currentNodesById[node.id] = &node;
  }

  for (auto const& oldNode : previous.nodes) {
    if (!currentNodesById.contains(oldNode.id)) {
      appendDamageRect(damage, oldNode.bounds, outputWidth, outputHeight);
    }
  }

  for (auto const& node : currentNodes) {
    auto const oldNodeIt = previousNodesById.find(node.id);
    if (oldNodeIt == previousNodesById.end()) {
      appendDamageRect(damage, node.bounds, outputWidth, outputHeight);
      continue;
    }

    CompositorSceneNodeSnapshot const& oldNode = *oldNodeIt->second;
    if (oldNode.bounds.x == node.bounds.x &&
        oldNode.bounds.y == node.bounds.y &&
        oldNode.bounds.width == node.bounds.width &&
        oldNode.bounds.height == node.bounds.height &&
        oldNode.signature == node.signature) {
      continue;
    }

    if (node.kind == CompositorSceneNodeKind::WindowContent &&
        oldNode.bounds.x == node.bounds.x &&
        oldNode.bounds.y == node.bounds.y &&
        oldNode.bounds.width == node.bounds.width &&
        oldNode.bounds.height == node.bounds.height) {
      auto const oldSurfaceIt = previousSurfacesById.find(node.surfaceId);
      auto const currentSurfaceIt = currentSurfacesById.find(node.surfaceId);
      if (oldSurfaceIt != previousSurfacesById.end() &&
          currentSurfaceIt != currentSurfacesById.end() &&
          canMapBufferDamage(*oldSurfaceIt->second, *currentSurfaceIt->second)) {
        CommittedSurfaceSnapshot const& current = *currentSurfaceIt->second;
        if (current.bufferDamageRects.empty()) {
          appendDamageRect(damage,
                           node.bounds,
                           outputWidth,
                           outputHeight,
                           !contentDamageKnownOpaque(current, node.bounds, chrome, dpiScale));
        } else {
          for (RegionRect const& rect : current.bufferDamageRects) {
            RegionRect const mappedDamage = mapBufferDamageToSurface(current, rect, chrome, dpiScale);
            appendDamageRect(damage,
                             mappedDamage,
                             outputWidth,
                             outputHeight,
                             !contentDamageKnownOpaque(current, mappedDamage, chrome, dpiScale));
          }
        }
        continue;
      }
    }

    appendDamageRect(damage, oldNode.bounds, outputWidth, outputHeight);
    appendDamageRect(damage, node.bounds, outputWidth, outputHeight);
  }

  if (previous.cursor.has_value() != currentCursor.has_value()) {
    if (previous.cursor) appendDamageRect(damage, sceneSurfaceContentRect(*previous.cursor), outputWidth, outputHeight);
    if (currentCursor) appendDamageRect(damage, sceneSurfaceContentRect(*currentCursor), outputWidth, outputHeight);
  }

  inflateDamageForBackdropSampling(damage, currentSurfaces, chrome, outputWidth, outputHeight);
  return damage;
}

std::vector<std::uint64_t> frameSurfaceIds(std::vector<CommittedSurfaceSnapshot> const& surfaces) {
  std::vector<std::uint64_t> ids;
  ids.reserve(surfaces.size());
  for (CommittedSurfaceSnapshot const& surface : surfaces) {
    if (surface.id != 0) ids.push_back(surface.id);
  }
  return ids;
}

bool surfaceOpenAnimationComplete(std::unordered_map<std::uint64_t, SurfaceVisualState> const& visuals,
                                  CommittedSurfaceSnapshot const& surface,
                                  std::chrono::steady_clock::time_point frameTime,
                                  bool animationsEnabled) {
  if (!animationsEnabled) return true;
  auto visual = visuals.find(surface.id);
  if (visual == visuals.end() || visual->second.firstSeen.time_since_epoch().count() == 0) return false;
  return frameTime - visual->second.firstSeen >= kSurfaceOpenAnimationDuration;
}

bool validScanoutSource(CommittedSurfaceSnapshot const& surface) {
  double const sourceWidth = surface.sourceWidth > 0.f ? surface.sourceWidth : static_cast<double>(surface.bufferWidth);
  double const sourceHeight = surface.sourceHeight > 0.f ? surface.sourceHeight : static_cast<double>(surface.bufferHeight);
  return std::isfinite(sourceWidth) &&
         std::isfinite(sourceHeight) &&
         surface.sourceX >= 0.f &&
         surface.sourceY >= 0.f &&
         sourceWidth > 0.0 &&
         sourceHeight > 0.0 &&
         surface.sourceX + sourceWidth <= static_cast<double>(surface.bufferWidth) + 0.5 &&
         surface.sourceY + sourceHeight <= static_cast<double>(surface.bufferHeight) + 0.5;
}

bool surfaceEligibleForHardwareScanoutPlane(CommittedSurfaceSnapshot const& surface,
                                            CompositorSceneFrameInput const& input,
                                            RegionRect visibleContent) {
  if (surface.id == 0 || surface.dmabufBufferId == 0 || surface.dmabufFormat == 0 ||
      surface.dmabufPlanes.empty() || surface.dmabufPlanes.size() > 4) {
    return false;
  }
  bool const hasExternalGlassChrome =
      windowExternalTitleBarHeight(surface) > 0.f && input.chrome.glass.blurRadius > 0.f;
  if (hasExternalGlassChrome || windowUsesCutoutChrome(surface) || surface.activeSizing || surface.pacingSizing ||
      surface.geometryAnimationGrowing || surface.windowClipTop > 0 || surface.windowClipBottom > 0 ||
      !surface.backgroundBlurRects.empty()) {
    return false;
  }
  if (surface.bufferTransform != 0 || surface.bufferWidth <= 0 || surface.bufferHeight <= 0 ||
      visibleContent.width <= 0 || visibleContent.height <= 0) {
    return false;
  }
  if (visibleContent.x < 0 || visibleContent.y < 0 ||
      visibleContent.x + visibleContent.width > input.logicalOutputWidth ||
      visibleContent.y + visibleContent.height > input.logicalOutputHeight) {
    return false;
  }
  if (!validScanoutSource(surface)) return false;
  return surfaceOpenAnimationComplete(input.surfaceVisuals,
                                      surface,
                                      input.frameTime,
                                      input.animationsEnabled);
}

std::uint64_t scanoutCandidateSignature(CommittedSurfaceSnapshot const& surface,
                                        RegionRect visibleContent,
                                        double outputScaleX,
                                        double outputScaleY) {
  std::uint64_t seed = 0x84222325cbf29ce4ull;
  double const sourceWidth = surface.sourceWidth > 0.f ? surface.sourceWidth : static_cast<double>(surface.bufferWidth);
  double const sourceHeight = surface.sourceHeight > 0.f ? surface.sourceHeight : static_cast<double>(surface.bufferHeight);
  std::int32_t const crtcX = static_cast<std::int32_t>(std::llround(static_cast<double>(visibleContent.x) * outputScaleX));
  std::int32_t const crtcY = static_cast<std::int32_t>(std::llround(static_cast<double>(visibleContent.y) * outputScaleY));
  std::uint32_t const crtcWidth =
      static_cast<std::uint32_t>(std::max(1ll, std::llround(static_cast<double>(visibleContent.width) * outputScaleX)));
  std::uint32_t const crtcHeight =
      static_cast<std::uint32_t>(std::max(1ll, std::llround(static_cast<double>(visibleContent.height) * outputScaleY)));

  hashValue(seed, surface.dmabufFormat);
  hashValue(seed, surface.bufferWidth);
  hashValue(seed, surface.bufferHeight);
  hashValue(seed, surface.sourceX);
  hashValue(seed, surface.sourceY);
  hashValue(seed, sourceWidth);
  hashValue(seed, sourceHeight);
  hashValue(seed, crtcX);
  hashValue(seed, crtcY);
  hashValue(seed, crtcWidth);
  hashValue(seed, crtcHeight);
  hashValue(seed, static_cast<std::uint64_t>(surface.dmabufPlanes.size()));
  for (auto const& plane : surface.dmabufPlanes) {
    hashValue(seed, plane.offset);
    hashValue(seed, plane.stride);
    hashValue(seed, plane.modifier);
  }
  return seed;
}

bool coveredByLaterSurface(std::vector<CommittedSurfaceSnapshot> const& surfaces,
                           std::size_t index,
                           RegionRect content,
                           ChromeConfig const& chrome) {
  for (std::size_t j = index + 1; j < surfaces.size(); ++j) {
    if (rectsOverlap(content, rectFromRect(windowFrameRect(surfaces[j], chrome.contentInsetWidth)))) return true;
  }
  return false;
}

std::optional<CompositorHardwareScanoutSelection>
buildHardwareScanoutSelection(CompositorSceneFrameInput const& input,
                              std::size_t surfaceIndex,
                              RegionRect visibleContent,
                              double outputScaleX,
                              double outputScaleY) {
  if (surfaceIndex >= input.surfaces.size()) return std::nullopt;
  CommittedSurfaceSnapshot const& surface = input.surfaces[surfaceIndex];
  std::vector<int> fds = input.duplicateDmabufFds(surface.id);
  if (fds.size() != surface.dmabufPlanes.size()) {
    for (int fd : fds) {
      if (fd >= 0) close(fd);
    }
    return std::nullopt;
  }

  platform::KmsAtomicPresenter::OverlayCandidate candidate{
      .surfaceId = surface.id,
      .bufferId = surface.dmabufBufferId,
      .drmFormat = surface.dmabufFormat,
      .bufferWidth = static_cast<std::uint32_t>(surface.bufferWidth),
      .bufferHeight = static_cast<std::uint32_t>(surface.bufferHeight),
      .sourceX = surface.sourceX,
      .sourceY = surface.sourceY,
      .sourceWidth = surface.sourceWidth > 0.f ? surface.sourceWidth : static_cast<double>(surface.bufferWidth),
      .sourceHeight = surface.sourceHeight > 0.f ? surface.sourceHeight : static_cast<double>(surface.bufferHeight),
      .crtcX = static_cast<std::int32_t>(std::llround(static_cast<double>(visibleContent.x) * outputScaleX)),
      .crtcY = static_cast<std::int32_t>(std::llround(static_cast<double>(visibleContent.y) * outputScaleY)),
      .crtcWidth = static_cast<std::uint32_t>(std::max(1ll, std::llround(static_cast<double>(visibleContent.width) * outputScaleX))),
      .crtcHeight = static_cast<std::uint32_t>(std::max(1ll, std::llround(static_cast<double>(visibleContent.height) * outputScaleY))),
      .acquireFenceFd = -1,
      .planes = {},
  };
  candidate.planes.reserve(surface.dmabufPlanes.size());
  for (std::size_t planeIndex = 0; planeIndex < surface.dmabufPlanes.size(); ++planeIndex) {
    candidate.planes.push_back({
        .fd = fds[planeIndex],
        .offset = surface.dmabufPlanes[planeIndex].offset,
        .stride = surface.dmabufPlanes[planeIndex].stride,
        .modifier = surface.dmabufPlanes[planeIndex].modifier,
    });
    fds[planeIndex] = -1;
  }
  return CompositorHardwareScanoutSelection{
      .surfaceIndex = surfaceIndex,
      .signature = scanoutCandidateSignature(surface, visibleContent, outputScaleX, outputScaleY),
      .candidate = std::move(candidate),
  };
}

std::optional<CompositorHardwareScanoutSelection>
selectHardwareScanoutSurface(CompositorSceneGraphState const& previous,
                             CompositorSceneFrameInput const& input) {
  if (!input.selectScanout || !input.output || !input.atomicPresenter || !input.duplicateDmabufFds ||
      input.forceFullDamage) {
    return std::nullopt;
  }
  if (input.logicalOutputWidth <= 0 || input.logicalOutputHeight <= 0) return std::nullopt;
  double const outputScaleX = static_cast<double>(input.output->width()) / static_cast<double>(input.logicalOutputWidth);
  double const outputScaleY = static_cast<double>(input.output->height()) / static_cast<double>(input.logicalOutputHeight);

  std::optional<std::size_t> selectedIndex;
  RegionRect selectedVisible{};
  std::int64_t selectedArea = 0;
  for (std::size_t i = 0; i < input.surfaces.size(); ++i) {
    CommittedSurfaceSnapshot const& surface = input.surfaces[i];
    RegionRect const visible = surfaceVisibleContentRect(surface, input.chrome, input.dpiScale);
    if (!surfaceEligibleForHardwareScanoutPlane(surface, input, visible)) continue;
    if (coveredByLaterSurface(input.surfaces, i, visible, input.chrome)) continue;

    std::uint64_t const modifier = surface.dmabufPlanes.empty() ||
                                           surface.dmabufPlanes.front().modifier == DRM_FORMAT_MOD_INVALID
                                       ? DRM_FORMAT_MOD_LINEAR
                                       : surface.dmabufPlanes.front().modifier;
    if (!input.atomicPresenter->canUseOverlayFormatModifier(surface.dmabufFormat, modifier)) continue;

    std::uint64_t const signature = scanoutCandidateSignature(surface, visible, outputScaleX, outputScaleY);
    auto const rejected = previous.rejectedScanoutSignaturesBySurface.find(surface.id);
    if (rejected != previous.rejectedScanoutSignaturesBySurface.end() && rejected->second == signature) continue;

    std::int64_t const area = static_cast<std::int64_t>(visible.width) * static_cast<std::int64_t>(visible.height);
    if (!selectedIndex || area > selectedArea) {
      selectedIndex = i;
      selectedVisible = visible;
      selectedArea = area;
    }
  }

  if (!selectedIndex) return std::nullopt;
  return buildHardwareScanoutSelection(input, *selectedIndex, selectedVisible, outputScaleX, outputScaleY);
}

std::uint64_t primaryReuseSignature(std::vector<CompositorSceneNodeSnapshot> const& nodes,
                                    std::uint64_t overlaySurfaceId) {
  std::uint64_t seed = 0x0ddc0ffeec001d00ull;
  for (auto const& node : nodes) {
    if (!node.primaryPlane) continue;
    bool const overlayContent = node.kind == CompositorSceneNodeKind::WindowContent &&
                                node.surfaceId == overlaySurfaceId;
    if (overlayContent) continue;
    hashValue(seed, node.id);
    hashValue(seed, kindBits(node.kind));
    hashValue(seed, node.surfaceId);
    hashValue(seed, node.bounds.x);
    hashValue(seed, node.bounds.y);
    hashValue(seed, node.bounds.width);
    hashValue(seed, node.bounds.height);
    hashValue(seed, node.signature);
  }
  return seed;
}

void pruneRejectedScanoutState(CompositorSceneGraphState& state,
                               std::vector<CommittedSurfaceSnapshot> const& surfaces) {
  for (auto it = state.rejectedScanoutSignaturesBySurface.begin();
       it != state.rejectedScanoutSignaturesBySurface.end();) {
    bool const live = std::any_of(surfaces.begin(), surfaces.end(), [&](auto const& surface) {
      return surface.id == it->first;
    });
    if (live) {
      ++it;
    } else {
      it = state.rejectedScanoutSignaturesBySurface.erase(it);
    }
  }
}

} // namespace

CompositorSceneFramePlan buildCompositorSceneFrame(CompositorSceneGraphState const& previous,
                                                   CompositorSceneFrameInput const& input) {
  CompositorSceneFramePlan plan;
  std::vector<CompositorSceneNodeSnapshot> const nodes =
      buildSceneNodes(input.surfaces,
                      input.softwareCursor,
                      input.chrome,
                      input.dpiScale,
                      input.logicalOutputWidth,
                      input.logicalOutputHeight);
  plan.damage = computeSceneDamage(previous,
                                   nodes,
                                   input.surfaces,
                                   input.softwareCursor,
                                   input.chrome,
                                   input.dpiScale,
                                   input.logicalOutputWidth,
                                   input.logicalOutputHeight,
                                   input.forceFullDamage);
  plan.frameCallbackSurfaceIds = frameSurfaceIds(input.surfaces);
  plan.nextState = previous;
  plan.nextState.initialized = true;
  plan.nextState.outputWidth = input.logicalOutputWidth;
  plan.nextState.outputHeight = input.logicalOutputHeight;
  plan.nextState.nodes = nodes;
  plan.nextState.surfaces = retainedSceneSnapshots(input.surfaces, input.chrome);
  plan.nextState.cursor = retainedSceneSnapshot(input.softwareCursor, input.chrome);
  pruneRejectedScanoutState(plan.nextState, input.surfaces);

  plan.scanout = selectHardwareScanoutSurface(previous, input);
  if (plan.scanout) {
    std::uint64_t const surfaceId = plan.scanout->candidate.surfaceId;
    plan.primaryReuseSignatureForScanout = primaryReuseSignature(nodes, surfaceId);
    plan.primaryReuseMatchesScanout =
        previous.primaryReuseSignatureValid &&
        previous.primaryReuseOverlaySurfaceId == surfaceId &&
        previous.primaryReuseSignature == plan.primaryReuseSignatureForScanout;
  }
  return plan;
}

void rejectCompositorSceneScanout(CompositorSceneGraphState& state,
                                  CompositorHardwareScanoutSelection const& selection) {
  if (selection.candidate.surfaceId == 0 || selection.signature == 0) return;
  state.rejectedScanoutSignaturesBySurface[selection.candidate.surfaceId] = selection.signature;
}

void rememberCompositorScenePrimaryReuse(CompositorSceneGraphState& state,
                                         std::uint64_t overlaySurfaceId,
                                         std::uint64_t primaryReuseSignature) {
  state.primaryReuseSignature = primaryReuseSignature;
  state.primaryReuseOverlaySurfaceId = overlaySurfaceId;
  state.primaryReuseSignatureValid = overlaySurfaceId != 0 && primaryReuseSignature != 0;
}

void clearCompositorScenePrimaryReuse(CompositorSceneGraphState& state) {
  state.primaryReuseSignature = 0;
  state.primaryReuseOverlaySurfaceId = 0;
  state.primaryReuseSignatureValid = false;
}

bool compositorSceneScanoutCoversOutput(CompositorHardwareScanoutSelection const& selection,
                                        platform::KmsOutput const& output) {
  return selection.candidate.crtcX == 0 &&
         selection.candidate.crtcY == 0 &&
         selection.candidate.crtcWidth == output.width() &&
         selection.candidate.crtcHeight == output.height();
}

platform::KmsAtomicPresenter::OverlayCandidate
duplicateCompositorSceneOverlayCandidate(platform::KmsAtomicPresenter::OverlayCandidate const& candidate) {
  platform::KmsAtomicPresenter::OverlayCandidate copy = candidate;
  copy.acquireFenceFd = candidate.acquireFenceFd >= 0 ? dup(candidate.acquireFenceFd) : -1;
  if (candidate.acquireFenceFd >= 0 && copy.acquireFenceFd < 0) {
    return {};
  }
  copy.planes.clear();
  copy.planes.reserve(candidate.planes.size());
  for (auto const& plane : candidate.planes) {
    int fd = plane.fd >= 0 ? dup(plane.fd) : -1;
    if (fd < 0) {
      closeCompositorSceneOverlayCandidate(copy);
      return {};
    }
    copy.planes.push_back({
        .fd = fd,
        .offset = plane.offset,
        .stride = plane.stride,
        .modifier = plane.modifier,
    });
  }
  return copy;
}

void closeCompositorSceneOverlayCandidate(platform::KmsAtomicPresenter::OverlayCandidate& candidate) noexcept {
  if (candidate.acquireFenceFd >= 0) {
    close(candidate.acquireFenceFd);
    candidate.acquireFenceFd = -1;
  }
  for (auto& plane : candidate.planes) {
    if (plane.fd >= 0) {
      close(plane.fd);
      plane.fd = -1;
    }
  }
}

std::shared_ptr<platform::KmsAtomicPresenter::OverlayCandidate>
ownCompositorSceneOverlayCandidate(platform::KmsAtomicPresenter::OverlayCandidate candidate) {
  return std::shared_ptr<platform::KmsAtomicPresenter::OverlayCandidate>(
      new platform::KmsAtomicPresenter::OverlayCandidate(std::move(candidate)),
      [](platform::KmsAtomicPresenter::OverlayCandidate* owned) {
        if (owned) {
          closeCompositorSceneOverlayCandidate(*owned);
          delete owned;
        }
      });
}

} // namespace lambdaui::compositor
