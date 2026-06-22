#include <Lambda/UI/CommandRegistry.hpp>

#include <algorithm>

namespace lambda {

void CommandRegistry::beginRebuild() {
  viewHandlers_.clear();
  windowHandlers_.clear();
}

CommandId CommandRegistry::registerViewHandler(ComponentKey const& key, std::string const& commandId,
                                               std::function<void()> handler,
                                               std::function<bool()> isEnabled) {
  CommandHandler h;
  h.id = nextId_++;
  h.commandId = commandId;
  h.trigger = std::move(handler);
  h.isEnabled = std::move(isEnabled);
  CommandId const id = h.id;
  viewHandlers_[key].push_back(std::move(h));
  return id;
}

CommandId CommandRegistry::registerWindowHandler(std::string const& commandId,
                                                 std::function<void()> handler,
                                                 std::function<bool()> isEnabled) {
  CommandHandler h;
  h.id = nextId_++;
  h.commandId = commandId;
  h.trigger = std::move(handler);
  h.isEnabled = std::move(isEnabled);
  CommandId const id = h.id;
  windowHandlers_.push_back(std::move(h));
  return id;
}

void CommandRegistry::unregister(CommandId id) {
  if (id == 0) {
    return;
  }
  for (auto it = viewHandlers_.begin(); it != viewHandlers_.end();) {
    auto& handlers = it->second;
    handlers.erase(std::remove_if(handlers.begin(), handlers.end(),
                                  [id](CommandHandler const& handler) {
                                    return handler.id == id;
                                  }),
                   handlers.end());
    if (handlers.empty()) {
      it = viewHandlers_.erase(it);
    } else {
      ++it;
    }
  }
  windowHandlers_.erase(std::remove_if(windowHandlers_.begin(), windowHandlers_.end(),
                                      [id](CommandHandler const& handler) {
                                        return handler.id == id;
                                      }),
                       windowHandlers_.end());
}

CommandHandler const* CommandRegistry::findViewHandler(ComponentKey const& focusedKey,
                                                       std::string const& commandId) const {
  ComponentKey probe = focusedKey;
  for (;;) {
    auto cit = viewHandlers_.find(probe);
    if (cit != viewHandlers_.end()) {
      for (auto it = cit->second.rbegin(); it != cit->second.rend(); ++it) {
        if (it->commandId == commandId) {
          return &*it;
        }
      }
    }
    if (probe.empty()) {
      break;
    }
    probe.pop_back();
  }
  return nullptr;
}

CommandHandler const* CommandRegistry::findWindowHandler(std::string const& commandId) const {
  for (auto it = windowHandlers_.rbegin(); it != windowHandlers_.rend(); ++it) {
    if (it->commandId == commandId) {
      return &*it;
    }
  }
  return nullptr;
}

bool CommandRegistry::dispatchShortcut(ComponentKey const& focusedKey, KeyCode key, Modifiers modifiers,
                                       std::unordered_map<std::string, CommandDescriptor> const& descriptors) const {
  // Step 1: view handlers on the focused leaf or an ancestor composite. `useViewCommand` registers on the
  // composite key while `focusedKey_` is the leaf `stableTargetKey`, so we walk key prefixes.
  ComponentKey probe = focusedKey;
  for (;;) {
    auto cit = viewHandlers_.find(probe);
    if (cit != viewHandlers_.end()) {
      for (auto it = cit->second.rbegin(); it != cit->second.rend(); ++it) {
        CommandHandler const& claim = *it;
        auto dit = descriptors.find(claim.commandId);
        if (dit == descriptors.end()) {
          continue;
        }
        if (!dit->second.shortcut.matches(key, modifiers)) {
          continue;
        }
        if (dit->second.isEnabled && !dit->second.isEnabled()) {
          return true;
        }
        if (claim.isEnabled && !claim.isEnabled()) {
          return true;
        }
        claim.trigger();
        return true;
      }
    }
    if (probe.empty()) {
      break;
    }
    probe.pop_back();
  }

  // Step 2: window handlers — last registration wins (scan from end).
  for (auto it = windowHandlers_.rbegin(); it != windowHandlers_.rend(); ++it) {
    CommandHandler const& handler = *it;
    auto dit = descriptors.find(handler.commandId);
    if (dit == descriptors.end()) {
      continue;
    }
    if (!dit->second.shortcut.matches(key, modifiers)) {
      continue;
    }
    if (dit->second.isEnabled && !dit->second.isEnabled()) {
      return true;
    }
    if (handler.isEnabled && !handler.isEnabled()) {
      return true;
    }
    handler.trigger();
    return true;
  }

  return false;
}

bool CommandRegistry::dispatchCommand(ComponentKey const& focusedKey, std::string const& commandId,
                                      std::unordered_map<std::string, CommandDescriptor> const& descriptors) const {
  auto enabled = [&](CommandHandler const& handler) {
    auto dit = descriptors.find(handler.commandId);
    if (dit != descriptors.end() && dit->second.isEnabled && !dit->second.isEnabled()) {
      return false;
    }
    return !handler.isEnabled || handler.isEnabled();
  };

  if (CommandHandler const* claim = findViewHandler(focusedKey, commandId)) {
    if (enabled(*claim)) {
      claim->trigger();
      return true;
    }
    return false;
  }

  if (CommandHandler const* win = findWindowHandler(commandId)) {
    if (enabled(*win)) {
      win->trigger();
      return true;
    }
  }

  return false;
}

bool CommandRegistry::isHandlerEnabled(ComponentKey const& focusedKey, std::string const& commandId,
                                       std::unordered_map<std::string, CommandDescriptor> const& descriptors) const {
  auto dit = descriptors.find(commandId);
  if (dit == descriptors.end()) {
    return false;
  }
  if (dit->second.isEnabled && !dit->second.isEnabled()) {
    return false;
  }

  if (CommandHandler const* claim = findViewHandler(focusedKey, commandId)) {
    if (claim->isEnabled && !claim->isEnabled()) {
      return false;
    }
    return true;
  }

  if (CommandHandler const* win = findWindowHandler(commandId)) {
    if (win->isEnabled && !win->isEnabled()) {
      return false;
    }
    return true;
  }

  return false;
}

} // namespace lambda
