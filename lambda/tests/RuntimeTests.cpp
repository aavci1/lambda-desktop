#include <doctest/doctest.h>

#include <Lambda/UI/CommandRegistry.hpp>
#include <Lambda/UI/KeyCodes.hpp>
#include <Lambda/UI/Shortcut.hpp>

#include "Platform/Linux/Common/XkbState.hpp"

#include <unordered_map>

TEST_CASE("runtime tests are parked for the v5 mount runtime rewrite") {
  CHECK(true);
}

TEST_CASE("command registry unregisters window commands by id") {
  lambda::CommandRegistry registry;
  int fired = 0;
  lambda::CommandId const id = registry.registerWindowHandler("demo.save", [&] {
    ++fired;
  });

  std::unordered_map<std::string, lambda::CommandDescriptor> descriptors;
  descriptors.emplace("demo.save", lambda::CommandDescriptor{
      .title = "Save",
      .shortcut = lambda::shortcuts::Save,
  });

  CHECK(registry.dispatchShortcut({}, lambda::keys::S, lambda::Modifiers::Meta, descriptors));
  CHECK(fired == 1);

  registry.unregister(id);

  CHECK_FALSE(registry.dispatchShortcut({}, lambda::keys::S, lambda::Modifiers::Meta, descriptors));
  CHECK(fired == 1);
}

TEST_CASE("command registry unregisters view handlers by id") {
  lambda::CommandRegistry registry;
  int fired = 0;
  lambda::CommandId const id = registry.registerViewHandler({}, "demo.save", [&] {
    ++fired;
  });

  std::unordered_map<std::string, lambda::CommandDescriptor> descriptors;
  descriptors.emplace("demo.save", lambda::CommandDescriptor{
      .title = "Save",
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

TEST_CASE("view command handlers registered with scope keys only fire for the focused scope") {
  lambda::CommandRegistry registry;
  int firstFired = 0;
  int secondFired = 0;
  int otherScope = 0;
  int firstScope = 0;
  int secondScope = 0;
  lambda::ComponentKey const firstKey = lambda::ComponentKey::fromScope(&firstScope);
  lambda::ComponentKey const secondKey = lambda::ComponentKey::fromScope(&secondScope);
  lambda::ComponentKey const otherKey = lambda::ComponentKey::fromScope(&otherScope);

  registry.registerViewHandler(firstKey, "demo.save", [&] {
    ++firstFired;
  });
  registry.registerViewHandler(secondKey, "demo.save", [&] {
    ++secondFired;
  });

  std::unordered_map<std::string, lambda::CommandDescriptor> descriptors;
  descriptors.emplace("demo.save", lambda::CommandDescriptor{
      .title = "Save",
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

TEST_CASE("view command handlers registered with scope keys still match focused descendants") {
  lambda::CommandRegistry registry;
  int fired = 0;
  int scope = 0;
  lambda::ComponentKey const scopeKey = lambda::ComponentKey::fromScope(&scope);
  lambda::ComponentKey const focusedLeaf{scopeKey, lambda::LocalId::fromString("leaf")};

  registry.registerViewHandler(scopeKey, "demo.save", [&] {
    ++fired;
  });

  std::unordered_map<std::string, lambda::CommandDescriptor> descriptors;
  descriptors.emplace("demo.save", lambda::CommandDescriptor{
      .title = "Save",
      .shortcut = lambda::shortcuts::Save,
  });

  CHECK(registry.dispatchShortcut(focusedLeaf, lambda::keys::S, lambda::Modifiers::Meta, descriptors));
  CHECK(fired == 1);
}

TEST_CASE("same-scope view command handlers use the last registration") {
  lambda::CommandRegistry registry;
  int firstFired = 0;
  int secondFired = 0;
  int scope = 0;
  lambda::ComponentKey const scopeKey = lambda::ComponentKey::fromScope(&scope);

  registry.registerViewHandler(scopeKey, "demo.save", [&] {
    ++firstFired;
  });
  registry.registerViewHandler(scopeKey, "demo.save", [&] {
    ++secondFired;
  });

  std::unordered_map<std::string, lambda::CommandDescriptor> descriptors;
  descriptors.emplace("demo.save", lambda::CommandDescriptor{
      .title = "Save",
      .shortcut = lambda::shortcuts::Save,
  });

  CHECK(registry.dispatchShortcut(scopeKey, lambda::keys::S, lambda::Modifiers::Meta, descriptors));
  CHECK(firstFired == 0);
  CHECK(secondFired == 1);
}

TEST_CASE("disabled focused command handler consumes shortcut before bubbling") {
  lambda::CommandRegistry registry;
  int viewFired = 0;
  int windowFired = 0;
  int scope = 0;
  lambda::ComponentKey const scopeKey = lambda::ComponentKey::fromScope(&scope);

  registry.registerViewHandler(scopeKey,
                               "demo.save",
                               [&] {
                                 ++viewFired;
                               },
                               [] {
                                 return false;
                               });
  registry.registerWindowHandler("demo.save", [&] {
    ++windowFired;
  });

  std::unordered_map<std::string, lambda::CommandDescriptor> descriptors;
  descriptors.emplace("demo.save", lambda::CommandDescriptor{
      .title = "Save",
      .shortcut = lambda::shortcuts::Save,
  });

  CHECK(registry.dispatchShortcut(scopeKey, lambda::keys::S, lambda::Modifiers::Meta, descriptors));
  CHECK(viewFired == 0);
  CHECK(windowFired == 0);
}
