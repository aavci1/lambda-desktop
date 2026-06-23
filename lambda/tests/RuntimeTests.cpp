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
  lambdaui::CommandRegistry registry;
  int fired = 0;
  lambdaui::CommandId const id = registry.registerWindowHandler("demo.save", [&] {
    ++fired;
  });

  std::unordered_map<std::string, lambdaui::CommandDescriptor> descriptors;
  descriptors.emplace("demo.save", lambdaui::CommandDescriptor{
      .title = "Save",
      .shortcut = lambdaui::shortcuts::Save,
  });

  CHECK(registry.dispatchShortcut({}, lambdaui::keys::S, lambdaui::Modifiers::Meta, descriptors));
  CHECK(fired == 1);

  registry.unregister(id);

  CHECK_FALSE(registry.dispatchShortcut({}, lambdaui::keys::S, lambdaui::Modifiers::Meta, descriptors));
  CHECK(fired == 1);
}

TEST_CASE("command registry unregisters view handlers by id") {
  lambdaui::CommandRegistry registry;
  int fired = 0;
  lambdaui::CommandId const id = registry.registerViewHandler({}, "demo.save", [&] {
    ++fired;
  });

  std::unordered_map<std::string, lambdaui::CommandDescriptor> descriptors;
  descriptors.emplace("demo.save", lambdaui::CommandDescriptor{
      .title = "Save",
      .shortcut = lambdaui::shortcuts::Save,
  });

  CHECK(registry.dispatchShortcut({}, lambdaui::keys::S, lambdaui::Modifiers::Meta, descriptors));
  CHECK(fired == 1);

  registry.unregister(id);

  CHECK_FALSE(registry.dispatchShortcut({}, lambdaui::keys::S, lambdaui::Modifiers::Meta, descriptors));
  CHECK(fired == 1);
}

TEST_CASE("linux text input emission ignores command modifiers") {
  CHECK(lambdaui::linux_platform::shouldEmitTextInputForModifiers(lambdaui::Modifiers::None));
  CHECK(lambdaui::linux_platform::shouldEmitTextInputForModifiers(lambdaui::Modifiers::Shift));

  CHECK_FALSE(lambdaui::linux_platform::shouldEmitTextInputForModifiers(lambdaui::Modifiers::Ctrl));
  CHECK_FALSE(lambdaui::linux_platform::shouldEmitTextInputForModifiers(lambdaui::Modifiers::Alt));
  CHECK_FALSE(lambdaui::linux_platform::shouldEmitTextInputForModifiers(lambdaui::Modifiers::Meta));
  CHECK_FALSE(lambdaui::linux_platform::shouldEmitTextInputForModifiers(
      lambdaui::Modifiers::Ctrl | lambdaui::Modifiers::Shift));
}

TEST_CASE("unknown key code does not match Ctrl+A shortcut") {
  lambdaui::Shortcut selectAll{lambdaui::keys::A, lambdaui::Modifiers::Ctrl};

  CHECK(selectAll.matches(lambdaui::keys::A, lambdaui::Modifiers::Ctrl));
  CHECK_FALSE(selectAll.matches(lambdaui::keys::Unknown, lambdaui::Modifiers::Ctrl));
}

TEST_CASE("component keys minted from scopes are non-empty and stable") {
  int firstScope = 0;
  int secondScope = 0;

  lambdaui::ComponentKey const firstKey = lambdaui::ComponentKey::fromScope(&firstScope);
  lambdaui::ComponentKey const sameFirstKey = lambdaui::ComponentKey::fromScope(&firstScope);
  lambdaui::ComponentKey const secondKey = lambdaui::ComponentKey::fromScope(&secondScope);

  CHECK_FALSE(firstKey.empty());
  CHECK(firstKey == sameFirstKey);
  CHECK(firstKey != secondKey);
}

TEST_CASE("view command handlers registered with scope keys only fire for the focused scope") {
  lambdaui::CommandRegistry registry;
  int firstFired = 0;
  int secondFired = 0;
  int otherScope = 0;
  int firstScope = 0;
  int secondScope = 0;
  lambdaui::ComponentKey const firstKey = lambdaui::ComponentKey::fromScope(&firstScope);
  lambdaui::ComponentKey const secondKey = lambdaui::ComponentKey::fromScope(&secondScope);
  lambdaui::ComponentKey const otherKey = lambdaui::ComponentKey::fromScope(&otherScope);

  registry.registerViewHandler(firstKey, "demo.save", [&] {
    ++firstFired;
  });
  registry.registerViewHandler(secondKey, "demo.save", [&] {
    ++secondFired;
  });

  std::unordered_map<std::string, lambdaui::CommandDescriptor> descriptors;
  descriptors.emplace("demo.save", lambdaui::CommandDescriptor{
      .title = "Save",
      .shortcut = lambdaui::shortcuts::Save,
  });

  CHECK(registry.dispatchShortcut(firstKey, lambdaui::keys::S, lambdaui::Modifiers::Meta, descriptors));
  CHECK(firstFired == 1);
  CHECK(secondFired == 0);

  CHECK(registry.dispatchShortcut(secondKey, lambdaui::keys::S, lambdaui::Modifiers::Meta, descriptors));
  CHECK(firstFired == 1);
  CHECK(secondFired == 1);

  CHECK_FALSE(registry.dispatchShortcut(otherKey, lambdaui::keys::S, lambdaui::Modifiers::Meta, descriptors));
  CHECK(firstFired == 1);
  CHECK(secondFired == 1);
}

TEST_CASE("view command handlers registered with scope keys still match focused descendants") {
  lambdaui::CommandRegistry registry;
  int fired = 0;
  int scope = 0;
  lambdaui::ComponentKey const scopeKey = lambdaui::ComponentKey::fromScope(&scope);
  lambdaui::ComponentKey const focusedLeaf{scopeKey, lambdaui::LocalId::fromString("leaf")};

  registry.registerViewHandler(scopeKey, "demo.save", [&] {
    ++fired;
  });

  std::unordered_map<std::string, lambdaui::CommandDescriptor> descriptors;
  descriptors.emplace("demo.save", lambdaui::CommandDescriptor{
      .title = "Save",
      .shortcut = lambdaui::shortcuts::Save,
  });

  CHECK(registry.dispatchShortcut(focusedLeaf, lambdaui::keys::S, lambdaui::Modifiers::Meta, descriptors));
  CHECK(fired == 1);
}

TEST_CASE("same-scope view command handlers use the last registration") {
  lambdaui::CommandRegistry registry;
  int firstFired = 0;
  int secondFired = 0;
  int scope = 0;
  lambdaui::ComponentKey const scopeKey = lambdaui::ComponentKey::fromScope(&scope);

  registry.registerViewHandler(scopeKey, "demo.save", [&] {
    ++firstFired;
  });
  registry.registerViewHandler(scopeKey, "demo.save", [&] {
    ++secondFired;
  });

  std::unordered_map<std::string, lambdaui::CommandDescriptor> descriptors;
  descriptors.emplace("demo.save", lambdaui::CommandDescriptor{
      .title = "Save",
      .shortcut = lambdaui::shortcuts::Save,
  });

  CHECK(registry.dispatchShortcut(scopeKey, lambdaui::keys::S, lambdaui::Modifiers::Meta, descriptors));
  CHECK(firstFired == 0);
  CHECK(secondFired == 1);
}

TEST_CASE("disabled focused command handler consumes shortcut before bubbling") {
  lambdaui::CommandRegistry registry;
  int viewFired = 0;
  int windowFired = 0;
  int scope = 0;
  lambdaui::ComponentKey const scopeKey = lambdaui::ComponentKey::fromScope(&scope);

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

  std::unordered_map<std::string, lambdaui::CommandDescriptor> descriptors;
  descriptors.emplace("demo.save", lambdaui::CommandDescriptor{
      .title = "Save",
      .shortcut = lambdaui::shortcuts::Save,
  });

  CHECK(registry.dispatchShortcut(scopeKey, lambdaui::keys::S, lambdaui::Modifiers::Meta, descriptors));
  CHECK(viewFired == 0);
  CHECK(windowFired == 0);
}
