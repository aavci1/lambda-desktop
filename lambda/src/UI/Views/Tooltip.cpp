#include <Lambda/UI/Views/Tooltip.hpp>

#include <Lambda/UI/Application.hpp>
#include <Lambda/UI/EventQueue.hpp>
#include <Lambda/UI/Events.hpp>
#include <Lambda/UI/Hooks.hpp>
#include <Lambda/UI/Views/Popover.hpp>
#include <Lambda/UI/Views/Text.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <utility>

namespace lambdaui {

namespace {

constexpr int kDefaultDelayMs = 600;

void cancelTooltipTimer(Signal<std::uint64_t> timerId) {
  std::uint64_t const id = timerId.peek();
  if (id == 0) {
    return;
  }
  timerId.set(0);
  if (Application::hasInstance()) {
    Application::instance().cancelTimer(id);
  }
}

Popover tooltipPopover(TooltipConfig const& config) {
  return Popover{
      .content = Element{Text{
          .text = config.text,
          .font = Font::footnote(),
          .color = Color::primary(),
          .wrapping = TextWrapping::Wrap,
      }},
      .placement = config.placement,
      .gap = 6.f,
      .arrow = false,
      .backgroundColor = Color::elevatedBackground(),
      .borderColor = Color::separator(),
      .borderWidth = 1.f,
      .cornerRadius = 8.f,
      .contentPadding = 8.f,
      .maxSize = Size{240.f, 0.f},
      .backdropColor = Colors::transparent,
      .dismissOnEscape = false,
      .dismissOnOutsideTap = false,
      .useTapAnchor = false,
      .useHoverLeafAnchor = true,
      .debugName = "tooltip",
  };
}

} // namespace

void useTooltip(TooltipConfig const& config) {
  Reactive::Signal<bool> hovered = useHover();
  Reactive::Signal<bool> pressed = usePress();
  Reactive::Signal<bool> ready = useState(false);
  Reactive::Signal<std::uint64_t> timerId = useState<std::uint64_t>(0);
  auto [showPopover, hidePopover, isPresented] = usePopover();
  (void)isPresented;

  auto alive = std::make_shared<bool>(true);
  if (Application::hasInstance()) {
    Application::instance().eventQueue().on<TimerEvent>(
        [alive, timerId, ready](TimerEvent const& event) {
          if (!*alive || event.timerId == 0 || event.timerId != timerId.peek()) {
            return;
          }
          timerId.set(0);
          ready.set(true);
          Application::instance().cancelTimer(event.timerId);
          Application::instance().requestRedraw();
        });
  }
  Reactive::onCleanup([alive, timerId] {
    *alive = false;
    cancelTooltipTimer(timerId);
  });

  useEffect([config,
             hovered,
             pressed,
             ready,
             timerId,
             showPopover = std::move(showPopover),
             hidePopover = std::move(hidePopover)] mutable {
    if (config.text.empty() || pressed() || !hovered()) {
      cancelTooltipTimer(timerId);
      ready.set(false);
      hidePopover();
      return;
    }

    if (ready()) {
      showPopover(tooltipPopover(config));
      return;
    }

    if (timerId.peek() == 0 && Application::hasInstance()) {
      int const delayMs = config.delayMs > 0 ? config.delayMs : kDefaultDelayMs;
      auto const interval = std::chrono::milliseconds{std::max(1, delayMs)};
      timerId.set(Application::instance().scheduleRepeatingTimer(interval));
    }
  });
}

void useTooltip(std::string text) {
  useTooltip(TooltipConfig {.text = std::move(text)});
}

} // namespace lambdaui
