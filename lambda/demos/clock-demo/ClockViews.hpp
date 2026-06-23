#pragma once

#include <Lambda.hpp>
#include <Lambda/UI/WindowUI.hpp>
#include <Lambda/Graphics/TextSystem.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/UI.hpp>
#include <Lambda/UI/ViewModifiers.hpp>
#include <Lambda/UI/Views/Render.hpp>
#include <Lambda/UI/Views/ZStack.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <string>
#include <utility>

namespace clock_demo {

constexpr float kPi = 3.14159265358979323846f;

struct ClockSnapshot {
  int rawSecondOfDay = 0;
  double seconds = 0.0;
  std::string label = "00:00:00";

  bool operator==(ClockSnapshot const&) const = default;
};

inline lambdaui::Color withAlpha(lambdaui::Color color, float alpha) {
  color.a = alpha;
  return color;
}

inline ClockSnapshot readClock(double dayOffset = 0.0) {
  using namespace std::chrono;

  auto const now = system_clock::now();
  std::time_t const nowTime = system_clock::to_time_t(now);
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &nowTime);
#else
  localtime_r(&nowTime, &local);
#endif

  char buffer[16]{};
  std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local);

  int const raw = local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
  return ClockSnapshot{
      .rawSecondOfDay = raw,
      .seconds = static_cast<double>(raw) + dayOffset,
      .label = buffer,
  };
}

inline lambdaui::Signal<ClockSnapshot> useClock() {
  auto clock = lambdaui::useState(readClock());

  double dayOffset = 0.0;
  int lastRawSecond = clock.peek().rawSecondOfDay;
  lambdaui::useFrame([clock, dayOffset, lastRawSecond](lambdaui::AnimationTick const&) mutable {
    ClockSnapshot next = readClock(dayOffset);
    if (next.rawSecondOfDay < lastRawSecond) {
      dayOffset += 24.0 * 60.0 * 60.0;
      next = readClock(dayOffset);
    }
    if (next.rawSecondOfDay != lastRawSecond) {
      lastRawSecond = next.rawSecondOfDay;
      clock = std::move(next);
    }
  });

  return clock;
}

inline lambdaui::Point polar(lambdaui::Point center, float radius, float degrees) {
  float const radians = degrees * kPi / 180.f;
  return lambdaui::Point{
      center.x + std::sin(radians) * radius,
      center.y - std::cos(radians) * radius,
  };
}

inline lambdaui::Size measureClock(lambdaui::LayoutConstraints const& constraints) {
  float const width = std::isfinite(constraints.maxWidth) ? constraints.maxWidth : 640.f;
  float const height = std::isfinite(constraints.maxHeight) ? constraints.maxHeight : width;
  return lambdaui::Size{std::max(1.f, width), std::max(1.f, height)};
}

struct ClockGeometry {
  float side = 1.f;
  lambdaui::Point center{};
  float radius = 1.f;
};

inline ClockGeometry clockGeometry(lambdaui::Rect frame) {
  float const side = std::max(1.f, std::min(frame.width, frame.height));
  return ClockGeometry{
      .side = side,
      .center = frame.center(),
      .radius = side * 0.45f,
  };
}

inline void drawClockFace(lambdaui::Canvas& canvas, lambdaui::Rect frame, lambdaui::Theme const& theme) {
  ClockGeometry const geometry = clockGeometry(frame);
  lambdaui::Point const center = geometry.center;
  float const radius = geometry.radius;

  lambdaui::Color const face = theme.elevatedBackgroundColor;
  lambdaui::Color const ring = theme.separatorColor;
  lambdaui::Color const major = theme.labelColor;
  lambdaui::Color const minor = theme.tertiaryLabelColor;

  canvas.drawCircle(center, radius + 14.f, lambdaui::FillStyle::solid(withAlpha(theme.accentColor, 0.07f)),
                    lambdaui::StrokeStyle::none());
  canvas.drawCircle(center, radius, lambdaui::FillStyle::solid(face),
                    lambdaui::StrokeStyle::solid(withAlpha(ring, 0.95f), 2.f));
  canvas.drawCircle(center, radius * 0.76f, lambdaui::FillStyle::none(),
                    lambdaui::StrokeStyle::solid(withAlpha(ring, 0.45f), 1.f));

  for (int i = 0; i < 60; ++i) {
    bool const isHour = (i % 5) == 0;
    float const degrees = static_cast<float>(i) * 6.f;
    float const outer = radius - 16.f;
    float const inner = radius - (isHour ? 42.f : 28.f);
    lambdaui::StrokeStyle stroke = lambdaui::StrokeStyle::solid(isHour ? major : minor, isHour ? 3.f : 1.25f);
    stroke.cap = lambdaui::StrokeCap::Round;
    canvas.drawLine(polar(center, inner, degrees), polar(center, outer, degrees), stroke);
  }

  lambdaui::Font const numberFont{.size = 24.f, .weight = 650.f};
  struct NumberMark {
    char const* text;
    float degrees;
  };
  NumberMark const numbers[]{{"12", 0.f}, {"3", 90.f}, {"6", 180.f}, {"9", 270.f}};
  for (NumberMark const& mark : numbers) {
    auto layout = lambdaui::Application::instance().textSystem().layout(
        mark.text, numberFont, theme.secondaryLabelColor, 0.f);
    lambdaui::Size const measured = layout->measuredSize;
    lambdaui::Point const p = polar(center, radius * 0.62f, mark.degrees);
    canvas.drawTextLayout(*layout, {p.x - measured.width * 0.5f, p.y - measured.height * 0.5f});
  }
}

struct ClockFace : lambdaui::ViewModifiers<ClockFace> {
  auto body() const {
    auto theme = lambdaui::useEnvironment<lambdaui::ThemeKey>();
    return lambdaui::Render{
        .measureFn = [](lambdaui::LayoutConstraints const& constraints, lambdaui::LayoutHints const&) {
          return measureClock(constraints);
        },
        .draw = [theme](lambdaui::Canvas& canvas, lambdaui::Rect frame) {
          drawClockFace(canvas, frame, theme.evaluate());
        },
    }
        .rasterize();
  }
};

struct ClockHands : lambdaui::ViewModifiers<ClockHands> {
  lambdaui::Reactive::Bindable<ClockSnapshot> clock{readClock()};

  auto body() const {
    auto theme = lambdaui::useEnvironment<lambdaui::ThemeKey>();
    auto clockBinding = clock;

    auto secondTarget = [clockBinding] {
      return static_cast<float>(clockBinding.evaluate().seconds * 6.0);
    };
    auto minuteTarget = [clockBinding] {
      return static_cast<float>(clockBinding.evaluate().seconds * 0.1);
    };
    auto hourTarget = [clockBinding] {
      return static_cast<float>(clockBinding.evaluate().seconds / 120.0);
    };
    auto secondSpring = [] {
      return lambdaui::Transition::spring(980.f, 16.f, 0.46f);
    };
    auto slowHandSpring = [] {
      return lambdaui::Transition::spring(360.f, 28.f, 0.36f);
    };

    auto second = lambdaui::useAnimated(secondTarget, secondSpring);
    auto minute = lambdaui::useAnimated(minuteTarget, slowHandSpring);
    auto hour = lambdaui::useAnimated(hourTarget, slowHandSpring);

    return lambdaui::Render{
        .measureFn = [](lambdaui::LayoutConstraints const& constraints, lambdaui::LayoutHints const&) {
          return measureClock(constraints);
        },
        .draw = [theme, hour, minute, second](lambdaui::Canvas& canvas, lambdaui::Rect frame) {
          lambdaui::Theme const currentTheme = theme.evaluate();
          ClockGeometry const geometry = clockGeometry(frame);
          float const hourWidth = std::max(5.f, geometry.side * 0.018f);
          float const minuteWidth = std::max(4.f, geometry.side * 0.013f);
          float const secondWidth = std::max(2.f, geometry.side * 0.006f);

          auto drawHand = [&](float degrees, lambdaui::Color color, float width, float length, float tail) {
            float const radians = std::fmod(degrees, 360.f) * kPi / 180.f;
            canvas.save();
            canvas.translate(geometry.center);
            canvas.rotate(radians);
            canvas.drawRect(lambdaui::Rect{-width * 0.5f, -length, width, length + tail},
                            lambdaui::CornerRadius{width * 0.5f},
                            lambdaui::FillStyle::solid(color),
                            lambdaui::StrokeStyle::none(),
                            lambdaui::ShadowStyle{.radius = 5.f,
                                              .offset = {1.5f, 2.5f},
                                              .color = lambdaui::Color{0.f, 0.f, 0.f, 0.18f}});
            canvas.restore();
          };

          drawHand(hour.evaluate(), currentTheme.labelColor, hourWidth,
                   geometry.radius * 0.42f, geometry.radius * 0.08f);
          drawHand(minute.evaluate(), currentTheme.labelColor, minuteWidth,
                   geometry.radius * 0.64f, geometry.radius * 0.10f);
          drawHand(second.evaluate(), currentTheme.dangerColor, secondWidth,
                   geometry.radius * 0.72f, geometry.radius * 0.16f);

          float const dotRadius = geometry.radius * 0.043f;
          canvas.drawCircle(geometry.center, dotRadius,
                            lambdaui::FillStyle::solid(currentTheme.dangerColor),
                            lambdaui::StrokeStyle::none());
        },
    }
        .flex(1.f);
  }

  bool operator==(ClockHands const& other) const {
    return clock == other.clock;
  }
};

struct Clock : lambdaui::ViewModifiers<Clock> {
  lambdaui::Reactive::Bindable<ClockSnapshot> clock{readClock()};

  auto body() const {
    auto clockBinding = clock;
    return lambdaui::ZStack{
        .children = lambdaui::children(
            ClockFace{},
            ClockHands{.clock = clockBinding}.flex(1.f)
        ),
    }
        .flex(1.f);
  }

  bool operator==(Clock const& other) const {
    return clock == other.clock;
  }
};

} // namespace clock_demo
