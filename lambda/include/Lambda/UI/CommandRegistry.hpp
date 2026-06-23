#pragma once

/// \file Lambda/UI/CommandRegistry.hpp
///
/// Part of the Lambda public API.


#include <Lambda/UI/Command.hpp>
#include <Lambda/Core/Identity.hpp>
#include <Lambda/UI/Input.hpp>

#include <functional>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace lambdaui {

using CommandId = std::uint64_t;

struct CommandHandler {
  CommandId id = 0;
  std::string commandId;
  std::function<void()> trigger;
  std::function<bool()> isEnabled; // empty = always enabled
};

class CommandRegistry {
public:
  /// Called at the start of each rebuild. Clears both tables.
  void beginRebuild();

  /// Registers a view command handler for the given component key.
  CommandId registerViewHandler(ComponentKey const& key, std::string const& commandId,
                                std::function<void()> handler,
                                std::function<bool()> isEnabled = {});

  /// Registers a window command handler. Last call for a given command id in build order wins.
  CommandId registerWindowHandler(std::string const& commandId, std::function<void()> handler,
                                  std::function<bool()> isEnabled = {});

  void unregister(CommandId id);

  /// Returns the view handler for (focusedKey, commandId), or nullptr.
  CommandHandler const* findViewHandler(ComponentKey const& focusedKey, std::string const& commandId) const;

  /// Returns the window handler for commandId, or nullptr.
  CommandHandler const* findWindowHandler(std::string const& commandId) const;

  /// Dispatch by shortcut: tries view handlers first, then window handlers.
  /// Returns true if an enabled handler fired.
  bool dispatchShortcut(ComponentKey const& focusedKey, KeyCode key, Modifiers modifiers,
                        std::unordered_map<std::string, CommandDescriptor> const& descriptors) const;

  /// Dispatch by command id: tries view handlers first, then window handlers.
  bool dispatchCommand(ComponentKey const& focusedKey, std::string const& commandId,
                       std::unordered_map<std::string, CommandDescriptor> const& descriptors) const;

  /// True if a handler exists for \p commandId and descriptor + handler enabled checks pass.
  bool isHandlerEnabled(ComponentKey const& focusedKey, std::string const& commandId,
                        std::unordered_map<std::string, CommandDescriptor> const& descriptors) const;

  // Compatibility names for older action-based call sites.
  CommandId registerViewClaim(ComponentKey const& key, std::string const& actionName,
                              std::function<void()> handler,
                              std::function<bool()> isEnabled = {}) {
    return registerViewHandler(key, actionName, std::move(handler), std::move(isEnabled));
  }

  CommandId registerWindowAction(std::string const& actionName, std::function<void()> handler,
                                 std::function<bool()> isEnabled = {}) {
    return registerWindowHandler(actionName, std::move(handler), std::move(isEnabled));
  }

  CommandHandler const* findViewClaim(ComponentKey const& focusedKey, std::string const& actionName) const {
    return findViewHandler(focusedKey, actionName);
  }

  CommandHandler const* findWindowAction(std::string const& actionName) const {
    return findWindowHandler(actionName);
  }

  bool dispatchAction(ComponentKey const& focusedKey, std::string const& name,
                      std::unordered_map<std::string, CommandDescriptor> const& descriptors) const {
    return dispatchCommand(focusedKey, name, descriptors);
  }

private:
  // View handler table: componentKey -> list of handlers (multiple commands per component).
  std::unordered_map<ComponentKey, std::vector<CommandHandler>, ComponentKeyHash> viewHandlers_;

  // Registration order — last matching name wins (scan from end).
  std::vector<CommandHandler> windowHandlers_;
  CommandId nextId_ = 1;
};

using ActionId = CommandId;
using ActionHandler = CommandHandler;
using ActionRegistry = CommandRegistry;

} // namespace lambdaui
