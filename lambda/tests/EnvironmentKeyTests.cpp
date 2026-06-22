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

namespace lambda {

LAMBDA_DEFINE_ENVIRONMENT_KEY(FirstEnvironmentTestKey, int, 10);
LAMBDA_DEFINE_ENVIRONMENT_KEY(SecondEnvironmentTestKey, int, 20);
LAMBDA_DEFINE_ENVIRONMENT_KEY(StringEnvironmentTestKey, std::string, std::string{"fallback"});

template<std::size_t>
struct ManyEnvironmentSlotTag {};

} // namespace lambda

namespace lambda::tests {

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

} // namespace lambda::tests

namespace {

template<std::size_t... I>
std::array<std::uint16_t, sizeof...(I)> allocateManyEnvironmentSlots(std::index_sequence<I...>) {
  return {lambda::detail::allocateEnvironmentSlot(typeid(lambda::ManyEnvironmentSlotTag<I>))...};
}

} // namespace

TEST_CASE("environment keys allocate distinct stable slots") {
  std::uint16_t const first = lambda::EnvironmentKey<lambda::FirstEnvironmentTestKey>::slot().index();
  std::uint16_t const second = lambda::EnvironmentKey<lambda::SecondEnvironmentTestKey>::slot().index();
  std::uint16_t const shared = lambda::EnvironmentKey<lambda::SharedEnvironmentTestKey>::slot().index();

  CHECK(first != second);
  CHECK(shared == lambda::tests::sharedEnvironmentTestKeyIndexFromOtherTranslationUnit());
}

TEST_CASE("environment slot registry reuses existing assignments") {
  struct LocalSlotTag {};

  std::uint16_t const first = lambda::detail::allocateEnvironmentSlot(typeid(LocalSlotTag));
  std::uint16_t const second = lambda::detail::allocateEnvironmentSlot(typeid(LocalSlotTag));
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
  lambda::detail::EnvironmentEntry entry;
  entry.setValue<int>(42);

  REQUIRE(entry.kind() == lambda::detail::EnvironmentEntryKind::Value);
  REQUIRE(entry.asValue<int>() != nullptr);
  CHECK(*entry.asValue<int>() == 42);
  CHECK(entry.asValue<float>() == nullptr);
  CHECK(entry.asSignal<int>() == nullptr);
}

TEST_CASE("environment entries store signal handles by identity") {
  lambda::detail::EnvironmentEntry lhs;
  lambda::detail::EnvironmentEntry rhs;
  lambda::detail::EnvironmentEntry different;
  lambda::Reactive::Signal<int> signal{3};

  lhs.setSignal<int>(signal);
  rhs.setSignal<int>(signal);
  different.setSignal<int>(lambda::Reactive::Signal<int>{3});

  REQUIRE(lhs.asSignal<int>() != nullptr);
  CHECK(lhs.asSignal<int>()->peek() == 3);
  CHECK(lhs.equals(rhs));
  CHECK_FALSE(lhs.equals(different));
}

TEST_CASE("environment entries copy, move, and destroy stored values") {
  int destroyed = 0;
  {
    lambda::detail::EnvironmentEntry entry;
    entry.setValue(lambda::tests::DestructionCounter{&destroyed});
    lambda::detail::EnvironmentEntry copy = entry;
    CHECK(copy.asValue<lambda::tests::DestructionCounter>() != nullptr);

    lambda::detail::EnvironmentEntry moved = std::move(copy);
    CHECK(moved.asValue<lambda::tests::DestructionCounter>() != nullptr);
    CHECK(copy.kind() == lambda::detail::EnvironmentEntryKind::None);
  }
  CHECK(destroyed >= 3);
}

TEST_CASE("environment entry move is noexcept") {
  static_assert(std::is_nothrow_move_constructible_v<lambda::detail::EnvironmentEntry>);
  static_assert(std::is_nothrow_move_assignable_v<lambda::detail::EnvironmentEntry>);
}

TEST_CASE("environment binding resolves defaults, values, and signals") {
  using namespace lambda::tests;

  lambda::EnvironmentBinding binding;
  CHECK(binding.value<lambda::FirstEnvironmentTestKey>() == 10);

  auto darkBinding = binding.withValue<lambda::ThemeKey>(lambda::Theme::dark());
  CHECK(darkBinding.value<lambda::ThemeKey>() == lambda::Theme::dark());
  CHECK(binding.value<lambda::ThemeKey>() == lambda::Theme::light());

  lambda::Reactive::Signal<lambda::Theme> theme{lambda::Theme::light()};
  auto signalBinding = binding.withSignal<lambda::ThemeKey>(theme);
  auto signal = signalBinding.signal<lambda::ThemeKey>();
  REQUIRE(signal.has_value());
  CHECK(signal->peek() == lambda::Theme::light());
  CHECK(signalBinding.value<lambda::ThemeKey>() == lambda::Theme::light());

  theme = lambda::Theme::dark();
  CHECK(signalBinding.value<lambda::ThemeKey>() == lambda::Theme::dark());
}

TEST_CASE("environment binding reuses entries when rebinding matching values") {
  lambda::EnvironmentBinding original =
      lambda::EnvironmentBinding{}.withValue<lambda::ThemeKey>(lambda::Theme::light());

  lambda::EnvironmentBinding rebound =
      original.withValue<lambda::ThemeKey>(lambda::Theme::light());

  CHECK(rebound.internalEntriesPointer() == original.internalEntriesPointer());
}

TEST_CASE("environment binding reuses entries when rebinding matching signals") {
  lambda::Reactive::Signal<lambda::Theme> themeSignal{lambda::Theme::light()};
  lambda::EnvironmentBinding original =
      lambda::EnvironmentBinding{}.withSignal<lambda::ThemeKey>(themeSignal);

  lambda::EnvironmentBinding rebound =
      original.withSignal<lambda::ThemeKey>(themeSignal);

  CHECK(rebound.internalEntriesPointer() == original.internalEntriesPointer());
}

TEST_CASE("signal-backed environment binding participates in reactive tracking") {
  lambda::Reactive::Signal<lambda::Theme> theme{lambda::Theme::light()};
  lambda::EnvironmentBinding binding =
      lambda::EnvironmentBinding{}.withSignal<lambda::ThemeKey>(theme);

  int runs = 0;
  lambda::Color observed{};
  lambda::Reactive::Effect effect{[&] {
    ++runs;
    observed = binding.value<lambda::ThemeKey>().labelColor;
  }};

  CHECK(runs == 1);
  CHECK(observed == lambda::Theme::light().labelColor);

  theme = lambda::Theme::dark();

  CHECK(runs == 2);
  CHECK(observed == lambda::Theme::dark().labelColor);
}
