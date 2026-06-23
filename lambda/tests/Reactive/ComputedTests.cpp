#include <Lambda/Reactive/Computed.hpp>
#include <Lambda/Reactive/Effect.hpp>
#include <Lambda/Reactive/Signal.hpp>

#include <doctest/doctest.h>

using namespace lambdaui::Reactive;

TEST_CASE("Reactive Computed recomputes lazily") {
  Signal<int> source(2);
  int runs = 0;
  Computed<int> doubled([&] {
    ++runs;
    return source.get() * 2;
  });

  CHECK(doubled.get() == 4);
  CHECK(runs == 1);

  source.set(3);
  CHECK(runs == 1);
  CHECK(doubled.get() == 6);
  CHECK(runs == 2);
}

TEST_CASE("Reactive Computed supports transitive and dynamic dependencies") {
  Signal<int> source(3);
  Computed<int> doubled([&] {
    return source.get() * 2;
  });
  Computed<int> plusOne([&] {
    return doubled.get() + 1;
  });

  CHECK(plusOne.get() == 7);
  source.set(4);
  CHECK(plusOne.get() == 9);

  Signal<bool> useLeft(true);
  Signal<int> left(10);
  Signal<int> right(20);
  int runs = 0;
  Computed<int> selected([&] {
    ++runs;
    return useLeft.get() ? left.get() : right.get();
  });

  CHECK(selected.get() == 10);
  right.set(21);
  CHECK(selected.get() == 10);
  CHECK(runs == 1);

  useLeft.set(false);
  CHECK(selected.get() == 21);
  CHECK(runs == 2);

  left.set(11);
  CHECK(selected.get() == 21);
  CHECK(runs == 2);
}

TEST_CASE("Reactive Computed skips downstream recompute when intermediate value is unchanged") {
  Signal<int> source(1);
  int bucketRuns = 0;
  int derivedRuns = 0;
  int effectRuns = 0;
  int observed = 0;

  Computed<int> bucket([&] {
    ++bucketRuns;
    return source.get() % 2;
  });
  Computed<int> derived([&] {
    ++derivedRuns;
    return bucket.get() * 10;
  });
  Effect effect([&] {
    ++effectRuns;
    observed = derived.get();
  });

  CHECK(observed == 10);
  CHECK(bucketRuns == 1);
  CHECK(derivedRuns == 1);
  CHECK(effectRuns == 1);

  source.set(3);
  CHECK(observed == 10);
  CHECK(bucketRuns == 2);
  CHECK(derivedRuns == 1);
  CHECK(effectRuns == 1);

  source.set(4);
  CHECK(observed == 0);
  CHECK(bucketRuns == 3);
  CHECK(derivedRuns == 2);
  CHECK(effectRuns == 2);
}
