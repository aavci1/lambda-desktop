#include <doctest/doctest.h>

#include "EnvironmentKeyTestSupport.hpp"

#include <Lambda/Reactive/Effect.hpp>
#include <Lambda/UI/Detail/EnvironmentSlot.hpp>
#include <Lambda/UI/EnvironmentBinding.hpp>
#include <Lambda/UI/EnvironmentKeys.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace lambdaui {

LAMBDA_DEFINE_ENVIRONMENT_KEY(FirstEnvironmentTestKey, int, 10);
LAMBDA_DEFINE_ENVIRONMENT_KEY(SecondEnvironmentTestKey, int, 20);
LAMBDA_DEFINE_ENVIRONMENT_KEY(StringEnvironmentTestKey, std::string, std::string{"fallback"});

template<std::size_t>
struct ManyEnvironmentSlotTag {};

} // namespace lambdaui

namespace lambdaui::tests {

struct DestructionCounter {
  int* destroyed = nullptr;

  bool operator==(DestructionCounter const& other) const {
    return destroyed == other.destroyed;
  }

  ~DestructionCounter() {
    if (destroyed) {
      ++*destroyed;
    }
  }
};

} // namespace lambdaui::tests

namespace {

template<std::size_t... I>
std::array<std::uint16_t, sizeof...(I)> allocateManyEnvironmentSlots(std::index_sequence<I...>) {
  return {lambdaui::detail::allocateEnvironmentSlot(typeid(lambdaui::ManyEnvironmentSlotTag<I>))...};
}

} // namespace

TEST_CASE("environment keys allocate distinct stable slots") {
  std::uint16_t const first = lambdaui::EnvironmentKey<lambdaui::FirstEnvironmentTestKey>::slot().index();
  std::uint16_t const second = lambdaui::EnvironmentKey<lambdaui::SecondEnvironmentTestKey>::slot().index();
  std::uint16_t const shared = lambdaui::EnvironmentKey<lambdaui::SharedEnvironmentTestKey>::slot().index();

  CHECK(first != second);
  CHECK(shared == lambdaui::tests::sharedEnvironmentTestKeyIndexFromOtherTranslationUnit());
}

TEST_CASE("environment slot registry reuses existing assignments") {
  struct LocalSlotTag {};

  std::uint16_t const first = lambdaui::detail::allocateEnvironmentSlot(typeid(LocalSlotTag));
  std::uint16_t const second = lambdaui::detail::allocateEnvironmentSlot(typeid(LocalSlotTag));
  CHECK(first == second);
}

TEST_CASE("environment slot registry assigns many distinct indices") {
  auto const indices = allocateManyEnvironmentSlots(std::make_index_sequence<100>{});
  for (std::size_t i = 0; i < indices.size(); ++i) {
    for (std::size_t j = i + 1; j < indices.size(); ++j) {
      CHECK(indices[i] != indices[j]);
    }
  }
}

TEST_CASE("environment entries store values and reject mismatched types") {
  lambdaui::detail::EnvironmentEntry entry;
  entry.setValue<int>(42);

  REQUIRE(entry.kind() == lambdaui::detail::EnvironmentEntryKind::Value);
  REQUIRE(entry.asValue<int>() != nullptr);
  CHECK(*entry.asValue<int>() == 42);
  CHECK(entry.asValue<float>() == nullptr);
  CHECK(entry.asSignal<int>() == nullptr);
}

TEST_CASE("environment entries store signal handles by identity") {
  lambdaui::detail::EnvironmentEntry lhs;
  lambdaui::detail::EnvironmentEntry rhs;
  lambdaui::detail::EnvironmentEntry different;
  lambdaui::Reactive::Signal<int> signal{3};

  lhs.setSignal<int>(signal);
  rhs.setSignal<int>(signal);
  different.setSignal<int>(lambdaui::Reactive::Signal<int>{3});

  REQUIRE(lhs.asSignal<int>() != nullptr);
  CHECK(lhs.asSignal<int>()->peek() == 3);
  CHECK(lhs.equals(rhs));
  CHECK_FALSE(lhs.equals(different));
}

TEST_CASE("environment entries copy, move, and destroy stored values") {
  int destroyed = 0;
  {
    lambdaui::detail::EnvironmentEntry entry;
    entry.setValue(lambdaui::tests::DestructionCounter{&destroyed});
    lambdaui::detail::EnvironmentEntry copy = entry;
    CHECK(copy.asValue<lambdaui::tests::DestructionCounter>() != nullptr);

    lambdaui::detail::EnvironmentEntry moved = std::move(copy);
    CHECK(moved.asValue<lambdaui::tests::DestructionCounter>() != nullptr);
    CHECK(copy.kind() == lambdaui::detail::EnvironmentEntryKind::None);
  }
  CHECK(destroyed >= 3);
}

TEST_CASE("environment entry move is noexcept") {
  static_assert(std::is_nothrow_move_constructible_v<lambdaui::detail::EnvironmentEntry>);
  static_assert(std::is_nothrow_move_assignable_v<lambdaui::detail::EnvironmentEntry>);
}

TEST_CASE("environment binding resolves defaults, values, and signals") {
  using namespace lambdaui::tests;

  lambdaui::EnvironmentBinding binding;
  CHECK(binding.value<lambdaui::FirstEnvironmentTestKey>() == 10);

  auto darkBinding = binding.withValue<lambdaui::ThemeKey>(lambdaui::Theme::dark());
  CHECK(darkBinding.value<lambdaui::ThemeKey>() == lambdaui::Theme::dark());
  CHECK(binding.value<lambdaui::ThemeKey>() == lambdaui::Theme::light());

  lambdaui::Reactive::Signal<lambdaui::Theme> theme{lambdaui::Theme::light()};
  auto signalBinding = binding.withSignal<lambdaui::ThemeKey>(theme);
  auto signal = signalBinding.signal<lambdaui::ThemeKey>();
  REQUIRE(signal.has_value());
  CHECK(signal->peek() == lambdaui::Theme::light());
  CHECK(signalBinding.value<lambdaui::ThemeKey>() == lambdaui::Theme::light());

  theme = lambdaui::Theme::dark();
  CHECK(signalBinding.value<lambdaui::ThemeKey>() == lambdaui::Theme::dark());
}

TEST_CASE("environment binding reuses entries when rebinding matching values") {
  lambdaui::EnvironmentBinding original =
      lambdaui::EnvironmentBinding{}.withValue<lambdaui::ThemeKey>(lambdaui::Theme::light());

  lambdaui::EnvironmentBinding rebound =
      original.withValue<lambdaui::ThemeKey>(lambdaui::Theme::light());

  CHECK(rebound.internalEntriesPointer() == original.internalEntriesPointer());
}

TEST_CASE("environment binding reuses entries when rebinding matching signals") {
  lambdaui::Reactive::Signal<lambdaui::Theme> themeSignal{lambdaui::Theme::light()};
  lambdaui::EnvironmentBinding original =
      lambdaui::EnvironmentBinding{}.withSignal<lambdaui::ThemeKey>(themeSignal);

  lambdaui::EnvironmentBinding rebound =
      original.withSignal<lambdaui::ThemeKey>(themeSignal);

  CHECK(rebound.internalEntriesPointer() == original.internalEntriesPointer());
}

TEST_CASE("signal-backed environment binding participates in reactive tracking") {
  lambdaui::Reactive::Signal<lambdaui::Theme> theme{lambdaui::Theme::light()};
  lambdaui::EnvironmentBinding binding =
      lambdaui::EnvironmentBinding{}.withSignal<lambdaui::ThemeKey>(theme);

  int runs = 0;
  lambdaui::Color observed{};
  lambdaui::Reactive::Effect effect{[&] {
    ++runs;
    observed = binding.value<lambdaui::ThemeKey>().labelColor;
  }};

  CHECK(runs == 1);
  CHECK(observed == lambdaui::Theme::light().labelColor);

  theme = lambdaui::Theme::dark();

  CHECK(runs == 2);
  CHECK(observed == lambdaui::Theme::dark().labelColor);
}
