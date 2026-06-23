#pragma once

#include <Lambda/Core/Identity.hpp>
#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Core/Color.hpp>
#include <Lambda/Reactive/Scope.hpp>
#include <Lambda/Reactive/Signal.hpp>
#include <Lambda/SceneGraph/SceneGraph.hpp>
#include <Lambda/UI/Element.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace lambdaui {

class Runtime;

struct OverlayId {
  std::uint64_t value = 0;
  bool isValid() const noexcept { return value != 0; }
  bool operator==(OverlayId const&) const = default;
};

inline constexpr OverlayId kInvalidOverlayId{};

struct OverlayConfig {
  enum class CrossAlignment {
    Center,
    Start,
    End,
    PreferStart,
    PreferEnd,
  };

  enum class Placement {
    Below,
    Above,
    End,
    Start,
  };

  std::optional<Rect> anchor{};
  std::optional<ComponentKey> anchorTrackLeafKey{};
  std::optional<ComponentKey> anchorTrackComponentKey{};
  std::optional<float> anchorMaxHeight{};
  EdgeInsets anchorOutsets{};
  Placement placement = Placement::Below;
  /// When set, placement is re-resolved from this preference when the anchor moves or the
  /// overlay is rebuilt. The current resolved placement remains in `placement`.
  std::optional<Placement> autoFlipPreferredPlacement{};
  /// Gap included in auto-flip fit checks; usually matches the directional `offset`.
  float autoFlipGap = 0.f;
  CrossAlignment crossAlignment = CrossAlignment::Center;
  Vec2 offset{};
  std::optional<Size> maxSize{};
  bool modal = false;
  Color backdropColor = Colors::transparent;
  /// Transparent backdrop blur radius in logical pixels. `kFloatFromTheme` resolves to the
  /// modal or popover backdrop blur token depending on `modal`.
  float backdropBlurRadius = kFloatFromTheme;
  bool dismissOnOutsideTap = true;
  bool dismissOnEscape = true;
  std::function<void()> onDismiss{};
  std::string debugName{};
};

LAMBDA_DEFINE_ENVIRONMENT_KEY(ResolvedOverlayPlacementKey,
                            std::optional<OverlayConfig::Placement>,
                            std::optional<OverlayConfig::Placement>{});

std::tuple<std::function<void(Element, OverlayConfig)>, std::function<void()>, bool> useOverlay();

struct OverlayEntry {
  OverlayId id{};
  std::optional<Element> content{};
  OverlayConfig config{};
  Reactive::Signal<std::optional<OverlayConfig::Placement>> resolvedPlacement{};
  Reactive::Scope scope{};
  scenegraph::SceneGraph sceneGraph{};
  Rect resolvedFrame{};
};

class OverlayManager {
public:
  OverlayManager() = default;

  void rebuild(Size windowSize, Runtime& runtime);
  void remountEntry(OverlayId id, Runtime& runtime);
  OverlayId push(Element content, OverlayConfig config, Runtime* runtime);
  void remove(OverlayId id, Runtime* runtime);
  void clear(Runtime* runtime, bool invokeDismissCallbacks = true);

  bool hasOverlays() const noexcept { return !overlays_.empty(); }
  bool hasTrackedAnchors() const noexcept;
  OverlayEntry const* top() const;
  OverlayEntry* find(OverlayId id);
  OverlayEntry const* find(OverlayId id) const;
  std::vector<std::unique_ptr<OverlayEntry>> const& entries() const { return overlays_; }

private:
  std::vector<std::unique_ptr<OverlayEntry>> overlays_{};
  std::uint64_t nextId_ = 1;
};

} // namespace lambdaui
