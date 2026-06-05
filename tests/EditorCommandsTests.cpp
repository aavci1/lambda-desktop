#include "EditorCommands.hpp"

#include <doctest/doctest.h>

namespace {

lambda::detail::TextEditSelection caret(int byte) {
  return {.caretByte = byte, .anchorByte = byte};
}

lambda::detail::TextEditSelection range(int start, int end) {
  return {.caretByte = end, .anchorByte = start};
}

} // namespace

TEST_CASE("EditorEditHistory records undo and redo snapshots") {
  lambda_editor::EditorEditHistory history;
  history.reset({"", caret(0)});

  lambda_editor::EditorSnapshot a = lambda_editor::insertAtSelection(
      history.current().text, history.current().selection, "a");
  history.record(a);
  lambda_editor::EditorSnapshot ab = lambda_editor::insertAtSelection(
      history.current().text, history.current().selection, "b");
  history.record(ab);
  lambda_editor::EditorSnapshot withNewline = lambda_editor::insertAtSelection(
      history.current().text, history.current().selection, "\n");
  history.record(withNewline);

  REQUIRE(history.canUndo());
  CHECK(history.undo()->text == "ab");
  CHECK(history.undo()->text == "a");
  CHECK(history.redo()->text == "ab");
  CHECK(history.redo()->text == "ab\n");
}

TEST_CASE("Editor commands delete cut and paste with selection") {
  std::string text = "hello world";
  lambda::detail::TextEditSelection selection = range(6, 11);

  CHECK(lambda_editor::selectedText(text, selection) == "world");

  lambda_editor::EditorSnapshot cut = lambda_editor::replaceSelection(text, selection, "");
  CHECK(cut.text == "hello ");
  CHECK(cut.selection == caret(6));

  lambda_editor::EditorSnapshot pasted =
      lambda_editor::insertAtSelection(cut.text, cut.selection, "editor");
  CHECK(pasted.text == "hello editor");
  CHECK(pasted.selection == caret(12));

  lambda_editor::EditorSnapshot deleted =
      lambda_editor::deleteSelectionOrForwardChar(pasted.text, range(6, 12));
  CHECK(deleted.text == "hello ");
  CHECK(deleted.selection == caret(6));
}

TEST_CASE("Editor commands find replace and go to line") {
  std::string text = "one\ntwo\none\n";

  auto first = lambda_editor::findNextMatch(text, "one", caret(0), true);
  REQUIRE(first.has_value());
  CHECK(*first == range(0, 3));

  auto second = lambda_editor::findNextMatch(text, "one", *first, true);
  REQUIRE(second.has_value());
  CHECK(*second == range(8, 11));

  lambda_editor::EditorSnapshot replaced =
      lambda_editor::replaceSelection(text, *second, "three");
  CHECK(replaced.text == "one\ntwo\nthree\n");
  CHECK(replaced.selection == caret(13));

  lambda_editor::EditorSnapshot replacedAll =
      lambda_editor::replaceAllMatches(text, "one", "1");
  CHECK(replacedAll.text == "1\ntwo\n1\n");

  CHECK(lambda_editor::lineSelection(text, 2) == caret(4));
  CHECK(lambda_editor::lineSelection(text, 20) == caret(static_cast<int>(text.size())));
  CHECK(lambda_editor::lineNumberForSelection(text, caret(8)) == 3);
}
