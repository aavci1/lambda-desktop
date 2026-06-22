#include <doctest/doctest.h>
#include <Lambda/Reactive/Animation.hpp>

#include <Lambda/UI/Detail/RootHolder.hpp>
#include <Lambda/Graphics/TextSystem.hpp>
#include <Lambda/SceneGraph/RectNode.hpp>
#include <Lambda/SceneGraph/SceneGraph.hpp>
#include <Lambda/UI/MountRoot.hpp>
#include <Lambda/UI/Theme.hpp>
#include <Lambda/UI/Views/Toggle.hpp>

#include <memory>
#include <string_view>
#include <vector>

using namespace lambda;

namespace {

class FakeTextSystem final : public TextSystem {
public:
  std::shared_ptr<TextLayout const>
  layout(AttributedString const&, float, TextLayoutOptions const&) override {
    return std::make_shared<TextLayout>();
  }

  std::shared_ptr<TextLayout const>
  layout(std::string_view, Font const&, Color const&, float, TextLayoutOptions const&) override {
    return std::make_shared<TextLayout>();
  }

  Size measure(AttributedString const&, float, TextLayoutOptions const&) override {
    return {0.f, 0.f};
  }

  Size measure(std::string_view, Font const&, Color const&, float,
               TextLayoutOptions const&) override {
    return {0.f, 0.f};
  }

  std::uint32_t resolveFontId(std::string_view, float, bool) override { return 0; }

  std::vector<std::uint8_t> rasterizeGlyph(std::uint32_t, std::uint32_t, float,
                                           std::uint32_t& outWidth,
                                           std::uint32_t& outHeight,
                                           Point& outBearing) override {
    outWidth = 0;
    outHeight = 0;
    outBearing = {};
    return {};
  }
};

EnvironmentBinding testEnvironment() {
  return EnvironmentBinding{}.withValue<ThemeKey>(Theme::light());
}

[[maybe_unused]] void compileFrameActionReturningUseFrame() {
  useFrame([](AnimationTick const&) {
    return FrameAction::StopAndRedraw;
  });
}

scenegraph::RectNode const* findMovedThumb(scenegraph::SceneNode const& node) {
  if (node.kind() == scenegraph::SceneNodeKind::Rect) {
    auto const& rect = static_cast<scenegraph::RectNode const&>(node);
    Size const size = rect.size();
    Point const position = rect.position();
    if (size.width == doctest::Approx(18.f) &&
        size.height == doctest::Approx(18.f) &&
        position.x > 0.f) {
      return &rect;
    }
  }
  for (auto const& child : node.children()) {
    if (child) {
      if (auto const* found = findMovedThumb(*child)) {
        return found;
      }
    }
  }
  return nullptr;
}

} // namespace

TEST_CASE("Animated repeats across finite iterations") {
  Animated<float> value{0.f};
  value.play(10.f, AnimationOptions {
      .transition = Transition::linear(1.f),
      .repeat = 3,
      .autoreverse = false,
  });

  value.testSetStartTime(100.0);

  CHECK(value.testTick(100.50));
  CHECK(value.get() == doctest::Approx(5.f));

  CHECK(value.testTick(101.00));
  CHECK(value.get() == doctest::Approx(0.f));

  CHECK(value.testTick(101.50));
  CHECK(value.get() == doctest::Approx(5.f));

  CHECK_FALSE(value.testTick(103.00));
  CHECK(value.get() == doctest::Approx(10.f));
  CHECK_FALSE(value.isRunning());
}

TEST_CASE("Animated autoreverse returns to its start on even iteration counts") {
  Animated<float> value{0.f};
  value.play(10.f, AnimationOptions {
      .transition = Transition::linear(1.f),
      .repeat = 2,
      .autoreverse = true,
  });

  value.testSetStartTime(10.0);

  CHECK(value.testTick(10.50));
  CHECK(value.get() == doctest::Approx(5.f));

  CHECK(value.testTick(11.50));
  CHECK(value.get() == doctest::Approx(5.f));

  CHECK_FALSE(value.testTick(12.00));
  CHECK(value.get() == doctest::Approx(0.f));
  CHECK_FALSE(value.isRunning());
}

TEST_CASE("Animation options preserve transition and playback configuration") {
  AnimationOptions const options {
      .transition = Transition::ease(0.4f).delayed(0.2f),
      .repeat = AnimationOptions::kRepeatForever,
      .autoreverse = true,
  };

  CHECK(options.transition.duration == doctest::Approx(0.4f));
  CHECK(options.transition.delay == doctest::Approx(0.2f));
  CHECK(options.repeat == AnimationOptions::kRepeatForever);
  CHECK(options.autoreverse);

  Transition const custom = Transition::custom(Easing::easeOut, 0.3f);
  CHECK(custom.duration == doctest::Approx(0.3f));
  CHECK(custom.easing == Easing::easeOut);
}

TEST_CASE("Animation clip samples from the timeline on read") {
  auto clip = addAnimation<float>(AnimationParams<float>{
      .from = 0.f,
      .to = 10.f,
      .duration = 2.0,
      .delay = 0.5,
      .startedAt = 100.0,
      .transition = Transition::linear(1.f),
  });

  CHECK(clip.testValueAt(100.25) == doctest::Approx(0.f));
  CHECK(clip.testValueAt(101.50) == doctest::Approx(5.f));
  CHECK(clip.testValueAt(102.50) == doctest::Approx(10.f));
  CHECK_FALSE(clip.testTick(102.50));
}

TEST_CASE("Animation clip completes exactly once on natural finish") {
  int completionCount = 0;
  auto clip = addAnimation<float>(AnimationParams<float>{
      .from = 0.f,
      .to = 1.f,
      .duration = 1.0,
      .startedAt = 10.0,
      .transition = Transition::linear(1.f),
      .onComplete = [&] {
        ++completionCount;
      },
  });

  CHECK(clip.testTick(10.50));
  CHECK(completionCount == 0);

  CHECK_FALSE(clip.testTick(11.00));
  CHECK(completionCount == 1);

  CHECK_FALSE(clip.testTick(12.00));
  CHECK(completionCount == 1);
}

TEST_CASE("Animation clip continues to completion after handle is dropped") {
  int completionCount = 0;
  {
    auto clip = addAnimation<float>(AnimationParams<float>{
        .from = 0.f,
        .to = 1.f,
        .duration = 1.0,
        .startedAt = 20.0,
        .transition = Transition::linear(1.f),
        .onComplete = [&] {
          ++completionCount;
        },
    });
    (void)clip;
  }

  CHECK(AnimationClock::instance().testOwnedAnimationCount() == 1u);
  AnimationClock::instance().testTick(21.0);
  CHECK(completionCount == 1);
  CHECK(AnimationClock::instance().testOwnedAnimationCount() == 0u);
}

TEST_CASE("AnimationClock delegates frame pump and redraw to installed driver") {
  AnimationClock& clock = AnimationClock::instance();
  clock.shutdown();

  int frameRequests = 0;
  int redrawRequests = 0;
  clock.setFrameDriver([&] {
    ++frameRequests;
  }, [&] {
    ++redrawRequests;
  });

  auto clip = addAnimation<float>(AnimationParams<float>{
      .from = 0.f,
      .to = 1.f,
      .duration = 1.0,
      .startedAt = 30.0,
      .transition = Transition::linear(1.f),
  });
  (void)clip;

  CHECK(frameRequests == 1);
  clock.notifyFrame(30'500'000'000LL);
  CHECK(redrawRequests == 1);
  CHECK(frameRequests == 2);

  clock.notifyFrame(31'000'000'000LL);
  CHECK(redrawRequests == 2);
  CHECK_FALSE(clock.needsFramePump());

  clock.shutdown();
}

TEST_CASE("Animation clip cancel freezes the sampled value without completion") {
  int completionCount = 0;
  double const now = AnimationClock::nowSeconds();
  auto clip = addAnimation<float>(AnimationParams<float>{
      .from = 0.f,
      .to = 10.f,
      .duration = 2.0,
      .startedAt = now - 1.0,
      .transition = Transition::linear(1.f),
      .onComplete = [&] {
        ++completionCount;
      },
  });

  clip.cancel();

  CHECK(clip.isFinished());
  CHECK(clip.value() == doctest::Approx(5.f).epsilon(0.02));
  CHECK(completionCount == 0);
}

TEST_CASE("Timeline animations reject invalid timing") {
  CHECK_THROWS_AS(
      (void)addAnimation<float>(AnimationParams<float>{
          .from = 0.f,
          .to = 1.f,
          .duration = -1.0,
      }),
      std::invalid_argument);
}

TEST_CASE("Animated copies share playback state") {
  Animated<float> original{0.f};
  Animated<float> copy = original;

  copy.play(10.f, Transition::linear(1.f));

  CHECK(original.isRunning());
  CHECK(copy.isRunning());

  original.testSetStartTime(20.0);
  REQUIRE(copy.testTick(20.5));
  CHECK(original.get() == doctest::Approx(5.f));

  original.stop();
  CHECK_FALSE(copy.isRunning());
}

TEST_CASE("Toggle state changes drive thumb through animation instead of jumping immediately") {
  struct Root {
    Signal<bool> value;

    Element body() const {
      return Toggle{.value = value};
    }
  };

  Signal<bool> value{false};
  FakeTextSystem textSystem;
  scenegraph::SceneGraph sceneGraph;
  MountRoot root{
      std::make_unique<TypedRootHolder<Root>>(std::in_place, Root{value}),
      textSystem,
      testEnvironment(),
      Size{120.f, 80.f},
  };

  root.mount(sceneGraph);

  scenegraph::RectNode const* thumb = findMovedThumb(sceneGraph.root());
  REQUIRE(thumb != nullptr);
  float const initialX = thumb->position().x;
  CHECK(initialX == doctest::Approx(4.f));

  value = true;

  thumb = findMovedThumb(sceneGraph.root());
  REQUIRE(thumb != nullptr);
  CHECK(thumb->position().x == doctest::Approx(initialX));
}
