#pragma once

#include <Lambda/UI/Views/TextEditUtils.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lambda_editor {

struct EditorSnapshot {
  std::string text;
  lambda::detail::TextEditSelection selection{};

  bool operator==(EditorSnapshot const&) const = default;
};

struct FindOptions {
  bool caseSensitive = true;
  bool wholeWord = false;
  bool regex = false;

  bool operator==(FindOptions const&) const = default;
};

class EditorEditHistory {
public:
  void reset(EditorSnapshot snapshot);
  void record(EditorSnapshot snapshot);

  [[nodiscard]] EditorSnapshot const& current() const noexcept;
  [[nodiscard]] bool canUndo() const noexcept;
  [[nodiscard]] bool canRedo() const noexcept;

  std::optional<EditorSnapshot> undo();
  std::optional<EditorSnapshot> redo();

private:
  EditorSnapshot current_{};
  std::vector<EditorSnapshot> undoStack_;
  std::vector<EditorSnapshot> redoStack_;
};

[[nodiscard]] std::string selectedText(std::string const& text,
                                       lambda::detail::TextEditSelection selection);
[[nodiscard]] EditorSnapshot insertAtSelection(std::string const& text,
                                               lambda::detail::TextEditSelection selection,
                                               std::string_view inserted);
[[nodiscard]] EditorSnapshot deleteSelectionOrForwardChar(std::string const& text,
                                                          lambda::detail::TextEditSelection selection);
[[nodiscard]] std::optional<lambda::detail::TextEditSelection>
findNextMatch(std::string const& text,
              std::string const& query,
              lambda::detail::TextEditSelection selection,
              bool wrap,
              FindOptions options = {});
[[nodiscard]] std::optional<lambda::detail::TextEditSelection>
findPreviousMatch(std::string const& text,
                  std::string const& query,
                  lambda::detail::TextEditSelection selection,
                  bool wrap,
                  FindOptions options = {});
[[nodiscard]] bool selectionMatches(std::string const& text,
                                    lambda::detail::TextEditSelection selection,
                                    std::string const& query,
                                    FindOptions options = {});
[[nodiscard]] EditorSnapshot replaceSelection(std::string const& text,
                                              lambda::detail::TextEditSelection selection,
                                              std::string_view replacement);
[[nodiscard]] EditorSnapshot replaceAllMatches(std::string const& text,
                                               std::string const& query,
                                               std::string_view replacement,
                                               FindOptions options = {});
[[nodiscard]] lambda::detail::TextEditSelection lineSelection(std::string const& text,
                                                              int oneBasedLine);
[[nodiscard]] int lineNumberForSelection(std::string const& text,
                                         lambda::detail::TextEditSelection selection) noexcept;

} // namespace lambda_editor
