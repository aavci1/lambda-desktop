#include <Lambda/Reactive/Animation.hpp>
#include <Lambda/Reactive/Computed.hpp>
#include <Lambda/Reactive/Effect.hpp>
#include <Lambda/Reactive/Signal.hpp>

#include <doctest/doctest.h>

#include <vector>

using namespace lambdaui::Reactive;

TEST_CASE("Reactive Effect runs once per source write") {
  Signal<int> source(1);
  Computed<int> doubled([&] {
    return source.get() * 2;
  });

  int runs = 0;
  int observed = 0;
  Effect effect([&] {
    ++runs;
    observed = doubled.get();
  });

  CHECK(runs == 1);
  CHECK(observed == 2);

  source.set(5);
  CHECK(runs == 2);
  CHECK(observed == 10);

  effect.dispose();
  source.set(6);
  CHECK(runs == 2);
}

TEST_CASE("Reactive Effect updates dynamic subscriptions") {
  Signal<bool> chooseLeft(true);
  Signal<int> left(1);
  Signal<int> right(10);
  int observed = 0;
  int runs = 0;
  Effect effect([&] {
    ++runs;
    observed = chooseLeft.get() ? left.get() : right.get();
  });

  CHECK(observed == 1);
  right.set(11);
  CHECK(runs == 1);

  chooseLeft.set(false);
  CHECK(runs == 2);
  CHECK(observed == 11);

  left.set(2);
  CHECK(runs == 2);
}

TEST_CASE("Reactive Effect sweeps sources dropped between runs") {
  Signal<bool> useLeft(true);
  Signal<int> left(1);
  Signal<int> right(10);
  int observed = 0;
  int runs = 0;

  Effect effect([&] {
    ++runs;
    observed = useLeft() ? left() : right();
  });

  CHECK(runs == 1);
  CHECK(observed == 1);

  right.set(11);
  CHECK(runs == 1);

  left.set(2);
  CHECK(runs == 2);
  CHECK(observed == 2);

  useLeft = false;
  CHECK(runs == 3);
  CHECK(observed == 11);

  left.set(3);
  CHECK(runs == 3);

  right.set(12);
  CHECK(runs == 4);
  CHECK(observed == 12);

  useLeft = true;
  CHECK(runs == 5);
  CHECK(observed == 3);

  right.set(13);
  CHECK(runs == 5);

  left.set(4);
  CHECK(runs == 6);
  CHECK(observed == 4);
}

TEST_CASE("Reactive Effect ignores stale source writes during a rerun") {
  Signal<bool> useLeft(true);
  Signal<int> left(1);
  Signal<int> right(10);
  int runs = 0;
  int observed = 0;

  Effect effect([&] {
    ++runs;
    if (useLeft()) {
      observed = left();
      return;
    }
    left = left.peek() + 1;
    observed = right();
  });

  CHECK(runs == 1);
  CHECK(observed == 1);

  useLeft = false;

  CHECK(runs == 2);
  CHECK(observed == 10);
  CHECK(left.peek() == 2);
}

TEST_CASE("Reactive Effect flushes shallower graph dependencies first") {
  Signal<int> source(1);
  Computed<int> first([&] {
    return source() + 1;
  });
  Computed<int> second([&] {
    return first() + 1;
  });
  std::vector<int> order;

  Effect deeper([&] {
    (void)second();
    order.push_back(2);
  });
  Effect shallower([&] {
    (void)first();
    order.push_back(1);
  });

  order.clear();
  source = 2;

  CHECK(order == std::vector<int>{1, 2});
}

TEST_CASE("Reactive Effect flush survives signal writes from a running effect") {
  // Regression: an effect writing a signal mid-flush re-entered flushEffects
  // through the nested BatchGuard and invalidated the flush queue iteration.
  Signal<int> source(0);
  Signal<int> derived(0);

  Effect writer([&] {
    derived.set(source.get() * 10);
  });

  std::vector<int> watcherRuns(8, 0);
  std::vector<Effect> watchers;
  watchers.reserve(watcherRuns.size());
  for (std::size_t i = 0; i < watcherRuns.size(); ++i) {
    watchers.emplace_back([&watcherRuns, &source, i] {
      (void)source.get();
      ++watcherRuns[i];
    });
  }

  std::vector<int> observed;
  Effect reader([&] {
    observed.push_back(derived.get());
  });

  source.set(1);
  source.set(2);

  CHECK(observed == std::vector<int>{0, 10, 20});
  for (int runs : watcherRuns) {
    CHECK(runs == 3);
  }
}

TEST_CASE("Reactive Effect flush caps runaway self scheduling in test builds") {
  Signal<int> source(0);
  int runs = 0;

  Effect effect([&] {
    int const value = source.get();
    ++runs;
    if (value > 0) {
      source.set(value + 1);
    }
  });

  source.set(1);

  CHECK(runs <= 10002);
  CHECK(source.peek() <= 10001);
}

TEST_CASE("Reactive Effect reruns do not inherit ambient WithTransition") {
  Signal<int> trigger(0);
  lambdaui::Animated<float> readAnimation{0.f};
  lambdaui::Animated<float> writeAnimation{0.f};
  int runs = 0;
  float observed = -1.f;

  Effect effect([&] {
    ++runs;
    observed = readAnimation.get();
    if (trigger.get() > 0) {
      writeAnimation = 5.f;
    }
  });

  CHECK(runs == 1);
  CHECK(observed == doctest::Approx(0.f));
  CHECK_FALSE(writeAnimation.isRunning());

  {
    lambdaui::WithTransition transition{lambdaui::Transition::linear(10.f)};
    readAnimation = 1.f;
    CHECK(readAnimation.isRunning());
    CHECK(readAnimation.get() == doctest::Approx(0.f));

    trigger.set(1);
  }

  CHECK(runs == 2);
  CHECK(observed == doctest::Approx(0.f));
  CHECK_FALSE(writeAnimation.isRunning());
  CHECK(writeAnimation.get() == doctest::Approx(5.f));

  Signal<int> localTrigger(0);
  lambdaui::Animated<float> scopedWrite{0.f};
  Effect scopedEffect([&] {
    if (localTrigger.get() > 0) {
      lambdaui::WithTransition transition{lambdaui::Transition::linear(10.f)};
      scopedWrite = 8.f;
    }
  });

  localTrigger.set(1);

  CHECK(scopedWrite.isRunning());
  CHECK(scopedWrite.get() == doctest::Approx(0.f));
}
