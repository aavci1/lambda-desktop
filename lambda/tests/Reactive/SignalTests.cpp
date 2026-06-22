#include <Lambda/Reactive/Effect.hpp>
#include <Lambda/Reactive/Signal.hpp>
#include <Lambda/Reactive/SmallFn.hpp>
#include <Lambda/Reactive/Untrack.hpp>

#include <array>
#include <doctest/doctest.h>
#include <functional>

using namespace lambda::Reactive;

TEST_CASE("Reactive Signal reads writes and skips equal writes") {
  Signal<int> count(1);
  CHECK(count.get() == 1);
  count.set(2);
  CHECK(count.get() == 2);

  int effectRuns = 0;
  int observed = 0;
  Effect effect([&] {
    ++effectRuns;
    observed = count.get();
  });

  CHECK(effectRuns == 1);
  CHECK(observed == 2);

  count.set(2);
  CHECK(effectRuns == 1);

  count.set(3);
  CHECK(effectRuns == 2);
  CHECK(observed == 3);
}

TEST_CASE("Reactive untrack reads without subscribing") {
  Signal<int> count(3);
  int untracked = 0;
  Effect effect([&] {
    untracked = untrack([&] {
      return count.get();
    });
  });

  CHECK(untracked == 3);
  count.set(4);
  CHECK(untracked == 3);
}

TEST_CASE("Signal operator== compares handle identity") {
  Signal<int> a{42};
  Signal<int> b{42};
  Signal<int> c = a;

  CHECK(a == c);
  CHECK_FALSE(a == b);
}

TEST_CASE("Reactive SmallFn uses inline storage for small closures") {
  int captured = 7;
  SmallFn<int(int)> small([captured](int value) {
    return captured + value;
  });
  CHECK(small(5) == 12);
  CHECK_FALSE(small.usesHeapStorage());

  auto largeCapture = [payload = std::array<int, 16>{}](int value) {
    return payload[0] + value;
  };
  SmallFn<int(int)> large(largeCapture);
  CHECK(large(9) == 9);
  CHECK(large.usesHeapStorage());
}

TEST_CASE("Reactive SmallFn treats empty std::function as empty") {
  std::function<void(int)> empty;
  SmallFn<void(int)> fn(empty);
  CHECK_FALSE(fn);

  std::function<void(int)> movedEmpty;
  SmallFn<void(int)> moved(std::move(movedEmpty));
  CHECK_FALSE(moved);
}
