#include "EditorCommands.hpp"

#include <algorithm>
#include <cctype>
#include <regex>

namespace lambda_editor {

namespace {

lambdaui::detail::TextEditSelection collapsedAt(int byte) noexcept {
  return lambdaui::detail::TextEditSelection{.caretByte = byte, .anchorByte = byte};
}

std::string lowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

bool isWordByte(std::string const& text, int byte) noexcept {
  if (byte < 0 || byte >= static_cast<int>(text.size())) {
    return false;
  }
  unsigned char const ch = static_cast<unsigned char>(text[static_cast<std::size_t>(byte)]);
  return ch >= 0x80 || std::isalnum(ch) || ch == '_';
}

bool wholeWordMatch(std::string const& text, int start, int end) noexcept {
  return !isWordByte(text, start - 1) && !isWordByte(text, end);
}

std::optional<lambdaui::detail::TextEditSelection>
matchSelectionAt(std::string const& text, int start, int length, FindOptions options) {
  if (length <= 0) {
    return std::nullopt;
  }
  int const end = start + length;
  if (start < 0 || end > static_cast<int>(text.size())) {
    return std::nullopt;
  }
  if (options.wholeWord && !wholeWordMatch(text, start, end)) {
    return std::nullopt;
  }
  return lambdaui::detail::TextEditSelection{.caretByte = end, .anchorByte = start};
}

std::optional<lambdaui::detail::TextEditSelection>
findPlainMatch(std::string const& text,
               std::string const& query,
               int startByte,
               FindOptions options) {
  std::string const haystack = options.caseSensitive ? text : lowerAscii(text);
  std::string const needle = options.caseSensitive ? query : lowerAscii(query);
  std::size_t pos = haystack.find(needle, static_cast<std::size_t>(std::max(0, startByte)));
  while (pos != std::string::npos) {
    int const start = static_cast<int>(pos);
    if (auto match = matchSelectionAt(text, start, static_cast<int>(query.size()), options)) {
      return match;
    }
    pos = haystack.find(needle, pos + std::max<std::size_t>(1, needle.size()));
  }
  return std::nullopt;
}

std::optional<lambdaui::detail::TextEditSelection>
findPlainPreviousMatch(std::string const& text,
                       std::string const& query,
                       int beforeByte,
                       FindOptions options) {
  std::string const haystack = options.caseSensitive ? text : lowerAscii(text);
  std::string const needle = options.caseSensitive ? query : lowerAscii(query);
  std::optional<lambdaui::detail::TextEditSelection> last;
  std::size_t pos = haystack.find(needle);
  while (pos != std::string::npos) {
    int const start = static_cast<int>(pos);
    int const end = start + static_cast<int>(query.size());
    if (end <= beforeByte) {
      if (auto match = matchSelectionAt(text, start, static_cast<int>(query.size()), options)) {
        last = match;
      }
    }
    if (start >= beforeByte) {
      break;
    }
    pos = haystack.find(needle, pos + std::max<std::size_t>(1, needle.size()));
  }
  return last;
}

std::optional<std::regex> makeRegex(std::string const& query, FindOptions options) {
  try {
    std::regex::flag_type flags = std::regex::ECMAScript;
    if (!options.caseSensitive) {
      flags |= std::regex::icase;
    }
    return std::regex(query, flags);
  } catch (std::regex_error const&) {
    return std::nullopt;
  }
}

std::optional<lambdaui::detail::TextEditSelection>
findRegexMatch(std::string const& text,
               std::regex const& pattern,
               int startByte,
               FindOptions options) {
  int cursor = std::clamp(startByte, 0, static_cast<int>(text.size()));
  while (cursor <= static_cast<int>(text.size())) {
    std::smatch match;
    std::string const tail = text.substr(static_cast<std::size_t>(cursor));
    if (!std::regex_search(tail, match, pattern)) {
      return std::nullopt;
    }
    int const start = cursor + static_cast<int>(match.position());
    int const length = static_cast<int>(match.length());
    if (auto selection = matchSelectionAt(text, start, length, options)) {
      return selection;
    }
    cursor = start + std::max(1, length);
  }
  return std::nullopt;
}

std::optional<lambdaui::detail::TextEditSelection>
findRegexPreviousMatch(std::string const& text,
                       std::regex const& pattern,
                       int beforeByte,
                       FindOptions options) {
  std::optional<lambdaui::detail::TextEditSelection> last;
  int cursor = 0;
  int const limit = std::clamp(beforeByte, 0, static_cast<int>(text.size()));
  while (cursor <= limit) {
    std::smatch match;
    std::string const tail = text.substr(static_cast<std::size_t>(cursor));
    if (!std::regex_search(tail, match, pattern)) {
      break;
    }
    int const start = cursor + static_cast<int>(match.position());
    int const length = static_cast<int>(match.length());
    int const end = start + length;
    if (end <= limit) {
      if (auto selection = matchSelectionAt(text, start, length, options)) {
        last = selection;
      }
    }
    if (start >= limit) {
      break;
    }
    cursor = start + std::max(1, length);
  }
  return last;
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

std::string selectedText(std::string const& text, lambdaui::detail::TextEditSelection selection) {
  auto const [start, end] = lambdaui::detail::clampSelection(text, selection).ordered();
  if (start >= end) {
    return {};
  }
  return text.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));
}

EditorSnapshot insertAtSelection(std::string const& text,
                                 lambdaui::detail::TextEditSelection selection,
                                 std::string_view inserted) {
  lambdaui::detail::TextEditMutation mutation =
      lambdaui::detail::insertText(text, selection, inserted);
  return EditorSnapshot{.text = std::move(mutation.text), .selection = mutation.selection};
}

EditorSnapshot deleteSelectionOrForwardChar(std::string const& text,
                                            lambdaui::detail::TextEditSelection selection) {
  lambdaui::detail::TextEditMutation mutation =
      lambdaui::detail::eraseSelectionOrChar(text, selection, true);
  return EditorSnapshot{.text = std::move(mutation.text), .selection = mutation.selection};
}

std::optional<lambdaui::detail::TextEditSelection>
findNextMatch(std::string const& text,
              std::string const& query,
              lambdaui::detail::TextEditSelection selection,
              bool wrap,
              FindOptions options) {
  if (text.empty() || query.empty()) {
    return std::nullopt;
  }

  lambdaui::detail::TextEditSelection clamped =
      lambdaui::detail::clampSelection(text, selection);
  int const startByte = std::max(clamped.caretByte, clamped.anchorByte);
  if (options.regex) {
    std::optional<std::regex> pattern = makeRegex(query, options);
    if (!pattern) {
      return std::nullopt;
    }
    if (auto match = findRegexMatch(text, *pattern, startByte, options)) {
      return match;
    }
    return wrap ? findRegexMatch(text, *pattern, 0, options) : std::nullopt;
  }

  if (auto match = findPlainMatch(text, query, startByte, options)) {
    return match;
  }
  return wrap ? findPlainMatch(text, query, 0, options) : std::nullopt;
}

std::optional<lambdaui::detail::TextEditSelection>
findPreviousMatch(std::string const& text,
                  std::string const& query,
                  lambdaui::detail::TextEditSelection selection,
                  bool wrap,
                  FindOptions options) {
  if (text.empty() || query.empty()) {
    return std::nullopt;
  }

  lambdaui::detail::TextEditSelection clamped =
      lambdaui::detail::clampSelection(text, selection);
  int const beforeByte = std::min(clamped.caretByte, clamped.anchorByte);
  if (options.regex) {
    std::optional<std::regex> pattern = makeRegex(query, options);
    if (!pattern) {
      return std::nullopt;
    }
    if (auto match = findRegexPreviousMatch(text, *pattern, beforeByte, options)) {
      return match;
    }
    return wrap ? findRegexPreviousMatch(text, *pattern, static_cast<int>(text.size()), options)
                : std::nullopt;
  }

  if (auto match = findPlainPreviousMatch(text, query, beforeByte, options)) {
    return match;
  }
  return wrap ? findPlainPreviousMatch(text, query, static_cast<int>(text.size()), options)
              : std::nullopt;
}

bool selectionMatches(std::string const& text,
                      lambdaui::detail::TextEditSelection selection,
                      std::string const& query,
                      FindOptions options) {
  auto const [start, end] = lambdaui::detail::clampSelection(text, selection).ordered();
  if (start >= end || query.empty()) {
    return false;
  }
  std::string const selected =
      text.substr(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));
  if (options.regex) {
    std::optional<std::regex> pattern = makeRegex(query, options);
    return pattern && std::regex_match(selected, *pattern) &&
           (!options.wholeWord || wholeWordMatch(text, start, end));
  }
  if (options.wholeWord && !wholeWordMatch(text, start, end)) {
    return false;
  }
  if (options.caseSensitive) {
    return selected == query;
  }
  return lowerAscii(selected) == lowerAscii(query);
}

EditorSnapshot replaceSelection(std::string const& text,
                                lambdaui::detail::TextEditSelection selection,
                                std::string_view replacement) {
  return insertAtSelection(text, selection, replacement);
}

EditorSnapshot replaceAllMatches(std::string const& text,
                                 std::string const& query,
                                 std::string_view replacement,
                                 FindOptions options) {
  if (text.empty() || query.empty()) {
    return EditorSnapshot{
        .text = text,
        .selection = collapsedAt(static_cast<int>(text.size())),
    };
  }

  std::string next;
  next.reserve(text.size());
  int cursor = 0;
  std::size_t replacements = 0;

  if (options.regex) {
    std::optional<std::regex> pattern = makeRegex(query, options);
    if (!pattern) {
      return EditorSnapshot{.text = text, .selection = collapsedAt(static_cast<int>(text.size()))};
    }
    while (cursor < static_cast<int>(text.size())) {
      std::smatch match;
      std::string const tail = text.substr(static_cast<std::size_t>(cursor));
      if (!std::regex_search(tail, match, *pattern)) {
        next.append(text.substr(static_cast<std::size_t>(cursor)));
        break;
      }
      int const start = cursor + static_cast<int>(match.position());
      int const length = static_cast<int>(match.length());
      int const end = start + length;
      if (length > 0 && (!options.wholeWord || wholeWordMatch(text, start, end))) {
        next.append(text.substr(static_cast<std::size_t>(cursor),
                                static_cast<std::size_t>(start - cursor)));
        next.append(replacement);
        cursor = end;
        ++replacements;
      } else {
        next.append(text.substr(static_cast<std::size_t>(cursor),
                                static_cast<std::size_t>(std::max(1, start + 1 - cursor))));
        cursor = start + 1;
      }
    }
  } else {
    std::string const haystack = options.caseSensitive ? text : lowerAscii(text);
    std::string const needle = options.caseSensitive ? query : lowerAscii(query);
    while (cursor < static_cast<int>(text.size())) {
      std::size_t const pos = haystack.find(needle, static_cast<std::size_t>(cursor));
      if (pos == std::string::npos) {
        next.append(text.substr(static_cast<std::size_t>(cursor)));
        break;
      }
      int const start = static_cast<int>(pos);
      int const end = start + static_cast<int>(query.size());
      if (options.wholeWord && !wholeWordMatch(text, start, end)) {
        next.append(text.substr(static_cast<std::size_t>(cursor),
                                static_cast<std::size_t>(start + 1 - cursor)));
        cursor = start + 1;
        continue;
      }
      next.append(text.substr(static_cast<std::size_t>(cursor),
                              static_cast<std::size_t>(start - cursor)));
      next.append(replacement);
      cursor = end;
      ++replacements;
    }
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

lambdaui::detail::TextEditSelection lineSelection(std::string const& text, int oneBasedLine) {
  int targetLine = std::max(1, oneBasedLine);
  int line = 1;
  int byte = 0;
  while (byte < static_cast<int>(text.size()) && line < targetLine) {
    if (text[static_cast<std::size_t>(byte)] == '\n') {
      ++line;
    }
    ++byte;
  }
  byte = lambdaui::detail::utf8Clamp(text, byte);
  return collapsedAt(byte);
}

int lineNumberForSelection(std::string const& text,
                           lambdaui::detail::TextEditSelection selection) noexcept {
  int const caret = lambdaui::detail::utf8Clamp(text, selection.caretByte);
  int line = 1;
  for (int i = 0; i < caret && i < static_cast<int>(text.size()); ++i) {
    if (text[static_cast<std::size_t>(i)] == '\n') {
      ++line;
    }
  }
  return line;
}

} // namespace lambda_editor
