#include <Lambda/Reactive/Computed.hpp>
#include <Lambda/Reactive/Effect.hpp>
#include <Lambda/Reactive/Signal.hpp>

#include <doctest/doctest.h>

using namespace lambda::Reactive;

TEST_CASE("Reactive diamond graph is glitch-free") {
  Signal<int> source(1);
  int bRuns = 0;
  int cRuns = 0;
  int dRuns = 0;
  int effectRuns = 0;
  int observed = 0;

  Computed<int> b([&] {
    ++bRuns;
    return source.get() + 1;
  });
  Computed<int> c([&] {
    ++cRuns;
    return source.get() + 2;
  });
  Computed<int> d([&] {
    ++dRuns;
    return b.get() + c.get();
  });
  Effect effect([&] {
    ++effectRuns;
    observed = d.get();
  });

  CHECK(observed == 5);
  CHECK(effectRuns == 1);
  CHECK(dRuns == 1);

  source.set(2);
  CHECK(observed == 7);
  CHECK(effectRuns == 2);
  CHECK(dRuns == 2);
  CHECK(bRuns == 2);
  CHECK(cRuns == 2);
}
