#include "EditorCommands.hpp"

#include <algorithm>

namespace lambda_editor {

namespace {

lambda::detail::TextEditSelection collapsedAt(int byte) noexcept {
  return lambda::detail::TextEditSelection{.caretByte = byte, .anchorByte = byte};
}

} // namespace

void EditorEditHistory::reset(EditorSnapshot snapshot) {
  current_ = std::move(snapshot);
  undoStack_.clear();
  redoStack_.clear();
}

void EditorEditHistory::record(EditorSnapshot snapshot) {
  if (snapshot == current_) {
    return;
  }
  undoStack_.push_back(current_);
  current_ = std::move(snapshot);
  redoStack_.clear();
}

EditorSnapshot const& EditorEditHistory::current() const noexcept {
  return current_;
}

bool EditorEditHistory::canUndo() const noexcept {
  return !undoStack_.empty();
}

bool EditorEditHistory::canRedo() const noexcept {
  return !redoStack_.empty();
}

std::optional<EditorSnapshot> EditorEditHistory::undo() {
  if (undoStack_.empty()) {
    return std::nullopt;
  }
  redoStack_.push_back(current_);
  current_ = undoStack_.back();
  undoStack_.pop_back();
  return current_;
}

std::optional<EditorSnapshot> EditorEditHistory::redo() {
  if (redoStack_.empty()) {
    return std::nullopt;
  }
  undoStack_.push_back(current_);
  current_ = redoStack_.back();
  redoStack_.pop_back();
  return current_;
}

std::string selectedText(std::string const& text, lambda::detail::TextEditSelection selection) {
  auto const [start, end] = lambda::detail::clampSelection(text, selection).ordered();
  if (start >= end) {
    return {};
  }
  return text.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));
}

EditorSnapshot insertAtSelection(std::string const& text,
                                 lambda::detail::TextEditSelection selection,
                                 std::string_view inserted) {
  lambda::detail::TextEditMutation mutation =
      lambda::detail::insertText(text, selection, inserted);
  return EditorSnapshot{.text = std::move(mutation.text), .selection = mutation.selection};
}

EditorSnapshot deleteSelectionOrForwardChar(std::string const& text,
                                            lambda::detail::TextEditSelection selection) {
  lambda::detail::TextEditMutation mutation =
      lambda::detail::eraseSelectionOrChar(text, selection, true);
  return EditorSnapshot{.text = std::move(mutation.text), .selection = mutation.selection};
}

std::optional<lambda::detail::TextEditSelection>
findNextMatch(std::string const& text,
              std::string const& query,
              lambda::detail::TextEditSelection selection,
              bool wrap) {
  if (text.empty() || query.empty()) {
    return std::nullopt;
  }

  lambda::detail::TextEditSelection clamped =
      lambda::detail::clampSelection(text, selection);
  int const startByte = std::max(clamped.caretByte, clamped.anchorByte);
  std::size_t pos = text.find(query, static_cast<std::size_t>(startByte));
  if (pos == std::string::npos && wrap) {
    pos = text.find(query);
  }
  if (pos == std::string::npos) {
    return std::nullopt;
  }

  int const start = static_cast<int>(pos);
  int const end = start + static_cast<int>(query.size());
  return lambda::detail::TextEditSelection{.caretByte = end, .anchorByte = start};
}

EditorSnapshot replaceSelection(std::string const& text,
                                lambda::detail::TextEditSelection selection,
                                std::string_view replacement) {
  return insertAtSelection(text, selection, replacement);
}

EditorSnapshot replaceAllMatches(std::string const& text,
                                 std::string const& query,
                                 std::string_view replacement) {
  if (text.empty() || query.empty()) {
    return EditorSnapshot{
        .text = text,
        .selection = collapsedAt(static_cast<int>(text.size())),
    };
  }

  std::string next;
  next.reserve(text.size());
  std::size_t cursor = 0;
  std::size_t replacements = 0;
  while (cursor < text.size()) {
    std::size_t const pos = text.find(query, cursor);
    if (pos == std::string::npos) {
      next.append(text.substr(cursor));
      break;
    }
    next.append(text.substr(cursor, pos - cursor));
    next.append(replacement);
    cursor = pos + query.size();
    ++replacements;
  }
  if (replacements == 0) {
    next = text;
  }
  int const caret = static_cast<int>(next.size());
  return EditorSnapshot{
      .text = std::move(next),
      .selection = collapsedAt(caret),
  };
}

lambda::detail::TextEditSelection lineSelection(std::string const& text, int oneBasedLine) {
  int targetLine = std::max(1, oneBasedLine);
  int line = 1;
  int byte = 0;
  while (byte < static_cast<int>(text.size()) && line < targetLine) {
    if (text[static_cast<std::size_t>(byte)] == '\n') {
      ++line;
    }
    ++byte;
  }
  byte = lambda::detail::utf8Clamp(text, byte);
  return collapsedAt(byte);
}

int lineNumberForSelection(std::string const& text,
                           lambda::detail::TextEditSelection selection) noexcept {
  int const caret = lambda::detail::utf8Clamp(text, selection.caretByte);
  int line = 1;
  for (int i = 0; i < caret && i < static_cast<int>(text.size()); ++i) {
    if (text[static_cast<std::size_t>(i)] == '\n') {
      ++line;
    }
  }
  return line;
}

} // namespace lambda_editor
