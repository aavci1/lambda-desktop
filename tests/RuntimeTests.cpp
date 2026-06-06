#include <doctest/doctest.h>

#include <Lambda/UI/KeyCodes.hpp>
#include <Lambda/UI/Shortcut.hpp>
#include <Lambda/UI/ActionRegistry.hpp>

#include "Platform/Linux/Common/XkbState.hpp"

#include <unordered_map>

TEST_CASE("runtime tests are parked for the v5 mount runtime rewrite") {
  CHECK(true);
}

TEST_CASE("action registry unregisters window actions by id") {
  lambda::ActionRegistry registry;
  int fired = 0;
  lambda::ActionId const id = registry.registerWindowAction("demo.save", [&] {
    ++fired;
  });

  std::unordered_map<std::string, lambda::ActionDescriptor> descriptors;
  descriptors.emplace("demo.save", lambda::ActionDescriptor{
      .label = "Save",
      .shortcut = lambda::shortcuts::Save,
  });

  CHECK(registry.dispatchShortcut({}, lambda::keys::S, lambda::Modifiers::Meta, descriptors));
  CHECK(fired == 1);

  registry.unregister(id);

  CHECK_FALSE(registry.dispatchShortcut({}, lambda::keys::S, lambda::Modifiers::Meta, descriptors));
  CHECK(fired == 1);
}

TEST_CASE("action registry unregisters view claims by id") {
  lambda::ActionRegistry registry;
  int fired = 0;
  lambda::ActionId const id = registry.registerViewClaim({}, "demo.save", [&] {
    ++fired;
  });

  std::unordered_map<std::string, lambda::ActionDescriptor> descriptors;
  descriptors.emplace("demo.save", lambda::ActionDescriptor{
      .label = "Save",
      .shortcut = lambda::shortcuts::Save,
  });

  CHECK(registry.dispatchShortcut({}, lambda::keys::S, lambda::Modifiers::Meta, descriptors));
  CHECK(fired == 1);

  registry.unregister(id);

  CHECK_FALSE(registry.dispatchShortcut({}, lambda::keys::S, lambda::Modifiers::Meta, descriptors));
  CHECK(fired == 1);
}

TEST_CASE("linux text input emission ignores command modifiers") {
  CHECK(lambda::linux_platform::shouldEmitTextInputForModifiers(lambda::Modifiers::None));
  CHECK(lambda::linux_platform::shouldEmitTextInputForModifiers(lambda::Modifiers::Shift));

  CHECK_FALSE(lambda::linux_platform::shouldEmitTextInputForModifiers(lambda::Modifiers::Ctrl));
  CHECK_FALSE(lambda::linux_platform::shouldEmitTextInputForModifiers(lambda::Modifiers::Alt));
  CHECK_FALSE(lambda::linux_platform::shouldEmitTextInputForModifiers(lambda::Modifiers::Meta));
  CHECK_FALSE(lambda::linux_platform::shouldEmitTextInputForModifiers(
      lambda::Modifiers::Ctrl | lambda::Modifiers::Shift));
}

TEST_CASE("unknown key code does not match Ctrl+A shortcut") {
  lambda::Shortcut selectAll{lambda::keys::A, lambda::Modifiers::Ctrl};

  CHECK(selectAll.matches(lambda::keys::A, lambda::Modifiers::Ctrl));
  CHECK_FALSE(selectAll.matches(lambda::keys::Unknown, lambda::Modifiers::Ctrl));
}

TEST_CASE("component keys minted from scopes are non-empty and stable") {
  int firstScope = 0;
  int secondScope = 0;

  lambda::ComponentKey const firstKey = lambda::ComponentKey::fromScope(&firstScope);
  lambda::ComponentKey const sameFirstKey = lambda::ComponentKey::fromScope(&firstScope);
  lambda::ComponentKey const secondKey = lambda::ComponentKey::fromScope(&secondScope);

  CHECK_FALSE(firstKey.empty());
  CHECK(firstKey == sameFirstKey);
  CHECK(firstKey != secondKey);
}

TEST_CASE("view claims registered with scope keys only fire for the focused scope") {
  lambda::ActionRegistry registry;
  int firstFired = 0;
  int secondFired = 0;
  int otherScope = 0;
  int firstScope = 0;
  int secondScope = 0;
  lambda::ComponentKey const firstKey = lambda::ComponentKey::fromScope(&firstScope);
  lambda::ComponentKey const secondKey = lambda::ComponentKey::fromScope(&secondScope);
  lambda::ComponentKey const otherKey = lambda::ComponentKey::fromScope(&otherScope);

  registry.registerViewClaim(firstKey, "demo.save", [&] {
    ++firstFired;
  });
  registry.registerViewClaim(secondKey, "demo.save", [&] {
    ++secondFired;
  });

  std::unordered_map<std::string, lambda::ActionDescriptor> descriptors;
  descriptors.emplace("demo.save", lambda::ActionDescriptor{
      .label = "Save",
      .shortcut = lambda::shortcuts::Save,
  });

  CHECK(registry.dispatchShortcut(firstKey, lambda::keys::S, lambda::Modifiers::Meta, descriptors));
  CHECK(firstFired == 1);
  CHECK(secondFired == 0);

  CHECK(registry.dispatchShortcut(secondKey, lambda::keys::S, lambda::Modifiers::Meta, descriptors));
  CHECK(firstFired == 1);
  CHECK(secondFired == 1);

  CHECK_FALSE(registry.dispatchShortcut(otherKey, lambda::keys::S, lambda::Modifiers::Meta, descriptors));
  CHECK(firstFired == 1);
  CHECK(secondFired == 1);
}

TEST_CASE("view claims registered with scope keys still match focused descendants") {
  lambda::ActionRegistry registry;
  int fired = 0;
  int scope = 0;
  lambda::ComponentKey const scopeKey = lambda::ComponentKey::fromScope(&scope);
  lambda::ComponentKey const focusedLeaf{scopeKey, lambda::LocalId::fromString("leaf")};

  registry.registerViewClaim(scopeKey, "demo.save", [&] {
    ++fired;
  });

  std::unordered_map<std::string, lambda::ActionDescriptor> descriptors;
  descriptors.emplace("demo.save", lambda::ActionDescriptor{
      .label = "Save",
      .shortcut = lambda::shortcuts::Save,
  });

  CHECK(registry.dispatchShortcut(focusedLeaf, lambda::keys::S, lambda::Modifiers::Meta, descriptors));
  CHECK(fired == 1);
}
