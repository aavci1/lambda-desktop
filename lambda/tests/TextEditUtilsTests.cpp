#include <doctest/doctest.h>

#include <Lambda/UI/Views/TextEditUtils.hpp>

#include <Lambda/Graphics/TextSystem.hpp>

#include <string>

using namespace lambda;
using namespace lambda::detail;

TEST_CASE("TextEditUtils: utf8 navigation") {
    std::string s = "a";
    s += std::string {"\xc3\xa9"};
    s += "b";
    CHECK(utf8NextChar(s, 0) == 1);
    CHECK(utf8NextChar(s, 1) == 3);
    CHECK(utf8PrevChar(s, 3) == 1);
    CHECK(utf8Clamp(s, 100) == static_cast<int>(s.size()));
}

TEST_CASE("TextEditUtils: orderedSelection") {
    auto p = orderedSelection(3, 1);
    CHECK(p.first == 1);
    CHECK(p.second == 3);
}

TEST_CASE("TextEditUtils: TextEditSelection ordered and selection state") {
    TextEditSelection sel {.caretByte = 8, .anchorByte = 3};
    CHECK(sel.hasSelection());
    auto const [a, b] = sel.ordered();
    CHECK(a == 3);
    CHECK(b == 8);
}

TEST_CASE("TextEditUtils: selection movement helpers") {
    std::string const text = "hello";

    TextEditSelection moved = moveSelectionToByte(text, TextEditSelection {.caretByte = 1, .anchorByte = 1}, 4, false);
    CHECK(moved.caretByte == 4);
    CHECK(moved.anchorByte == 4);

    TextEditSelection extended =
        moveSelectionToByte(text, TextEditSelection {.caretByte = 1, .anchorByte = 1}, 4, true);
    CHECK(extended.caretByte == 4);
    CHECK(extended.anchorByte == 1);

    TextEditSelection all = selectAllSelection(text);
    CHECK(all.anchorByte == 0);
    CHECK(all.caretByte == 5);

    TextEditSelection cleared = clearSelection(TextEditSelection {.caretByte = 3, .anchorByte = 1});
    CHECK(cleared.caretByte == 3);
    CHECK(cleared.anchorByte == 3);
}

TEST_CASE("TextEditUtils: caret movement helpers") {
    std::string const text = "ab cd\nef";

    TextEditSelection charMove =
        moveSelectionByChar(text, TextEditSelection {.caretByte = 2, .anchorByte = 2}, 1, false);
    CHECK(charMove.caretByte == 3);
    CHECK(charMove.anchorByte == 3);

    TextEditSelection wordMove =
        moveSelectionByWord(text, TextEditSelection {.caretByte = 0, .anchorByte = 0}, 1, false);
    CHECK(wordMove.caretByte == 2);
    CHECK(wordMove.anchorByte == 2);

    TextEditSelection lineEnd =
        moveSelectionToLineBoundary(text, TextEditSelection {.caretByte = 1, .anchorByte = 1}, true, false);
    CHECK(lineEnd.caretByte == 5);
    CHECK(lineEnd.anchorByte == 5);

    TextEditSelection lineStart =
        moveSelectionToLineBoundary(text, TextEditSelection {.caretByte = 7, .anchorByte = 7}, false, false);
    CHECK(lineStart.caretByte == 6);
    CHECK(lineStart.anchorByte == 6);

    TextEditSelection docEnd =
        moveSelectionToDocumentBoundary(text, TextEditSelection {.caretByte = 1, .anchorByte = 1}, true, true);
    CHECK(docEnd.caretByte == static_cast<int>(text.size()));
    CHECK(docEnd.anchorByte == 1);
}

TEST_CASE("TextEditUtils: shouldCoalesceInsert") {
    std::string const prev = "hello";
    CHECK(shouldCoalesceInsert(prev, 5, "x") == true);
    CHECK(shouldCoalesceInsert(prev, 5, " ") == false);
    CHECK(shouldCoalesceInsert("hello ", 6, "w") == false);
    CHECK(shouldCoalesceInsert(prev, 5, "xy") == false);
}

TEST_CASE("TextEditUtils: insertText replaces selection and respects max length") {
    TextEditMutation const inserted = insertText("hello", TextEditSelection {.caretByte = 5, .anchorByte = 2}, "yy", 0);
    CHECK(inserted.text == "heyy");
    CHECK(inserted.selection.caretByte == 4);
    CHECK(inserted.selection.anchorByte == 4);
    CHECK(inserted.valueChanged);

    TextEditMutation const limited = insertText("hello", TextEditSelection {.caretByte = 5, .anchorByte = 5}, " world", 6);
    CHECK(limited.text == "hello ");
    CHECK(limited.selection.caretByte == 6);
    CHECK(limited.valueChanged);
}

TEST_CASE("TextEditUtils: eraseSelectionOrChar handles selection and single char deletes") {
    TextEditMutation const eraseSelection =
        eraseSelectionOrChar("hello", TextEditSelection {.caretByte = 4, .anchorByte = 1}, false);
    CHECK(eraseSelection.text == "ho");
    CHECK(eraseSelection.selection.caretByte == 1);
    CHECK(eraseSelection.selection.anchorByte == 1);
    CHECK(eraseSelection.valueChanged);

    TextEditMutation const backspace =
        eraseSelectionOrChar("hello", TextEditSelection {.caretByte = 3, .anchorByte = 3}, false);
    CHECK(backspace.text == "helo");
    CHECK(backspace.selection.caretByte == 2);

    TextEditMutation const forwardDelete =
        eraseSelectionOrChar("hello", TextEditSelection {.caretByte = 1, .anchorByte = 1}, true);
    CHECK(forwardDelete.text == "hllo");
    CHECK(forwardDelete.selection.caretByte == 1);
}

TEST_CASE("TextEditUtils: eraseWord handles both directions") {
    TextEditMutation const backward =
        eraseWord("hello world", TextEditSelection {.caretByte = 11, .anchorByte = 11}, false);
    CHECK(backward.text == "hello ");
    CHECK(backward.selection.caretByte == 6);

    TextEditMutation const forward =
        eraseWord("hello world", TextEditSelection {.caretByte = 0, .anchorByte = 0}, true);
    CHECK(forward.text == " world");
    CHECK(forward.selection.caretByte == 0);
}

TEST_CASE("TextEditUtils: word navigation crosses punctuation groups") {
    std::string const text = "foo.bar, baz";
    CHECK(utf8NextWord(text, 3) == 4);
    CHECK(utf8NextWord(text, 7) == 8);
    CHECK(utf8PrevWord(text, 4) == 3);
    CHECK(utf8PrevWord(text, 8) == 7);
}

TEST_CASE("TextEditUtils: eraseToLineBoundary deletes to start and end of line") {
    TextEditMutation backward =
        eraseToLineBoundary("abc\ndef", TextEditSelection {.caretByte = 6, .anchorByte = 6}, false);
    CHECK(backward.text == "abc\nf");
    CHECK(backward.selection.caretByte == 4);

    TextEditMutation forward =
        eraseToLineBoundary("abc\ndef", TextEditSelection {.caretByte = 1, .anchorByte = 1}, true);
    CHECK(forward.text == "a\ndef");
    CHECK(forward.selection.caretByte == 1);
}

TEST_CASE("TextEditUtils: line commands select delete insert move and copy lines") {
    TextEditSelection selected = selectCurrentLine("one\ntwo\nthree", TextEditSelection {.caretByte = 5, .anchorByte = 5});
    CHECK(selected.anchorByte == 4);
    CHECK(selected.caretByte == 8);

    TextEditMutation deleted = eraseCurrentLine("one\ntwo\nthree", TextEditSelection {.caretByte = 5, .anchorByte = 5});
    CHECK(deleted.text == "one\nthree");
    CHECK(deleted.selection.caretByte == 4);

    TextEditMutation insertedAbove =
        insertLineAdjacent("one\ntwo", TextEditSelection {.caretByte = 5, .anchorByte = 5}, true);
    CHECK(insertedAbove.text == "one\n\ntwo");
    CHECK(insertedAbove.selection.caretByte == 4);

    TextEditMutation insertedBelow =
        insertLineAdjacent("one\ntwo", TextEditSelection {.caretByte = 5, .anchorByte = 5}, false);
    CHECK(insertedBelow.text == "one\ntwo\n");
    CHECK(insertedBelow.selection.caretByte == 8);

    TextEditMutation movedUp =
        moveCurrentLine("one\ntwo\nthree", TextEditSelection {.caretByte = 5, .anchorByte = 5}, -1);
    CHECK(movedUp.text == "two\none\nthree");
    CHECK(movedUp.selection.caretByte == 1);

    TextEditMutation movedDown =
        moveCurrentLine("one\ntwo\nthree", TextEditSelection {.caretByte = 1, .anchorByte = 1}, 1);
    CHECK(movedDown.text == "two\none\nthree");
    CHECK(movedDown.selection.caretByte == 5);

    TextEditMutation copiedDown =
        copyCurrentLine("one\ntwo", TextEditSelection {.caretByte = 1, .anchorByte = 1}, 1);
    CHECK(copiedDown.text == "one\none\ntwo");
    CHECK(copiedDown.selection.caretByte == 5);
}

TEST_CASE("TextEditUtils: lineIndexForByte") {
    std::vector<LineMetrics> lines;
    LineMetrics a {};
    a.byteStart = 0;
    a.byteEnd = 5;
    lines.push_back(a);
    LineMetrics b {};
    b.byteStart = 5;
    b.byteEnd = 10;
    lines.push_back(b);
    CHECK(lineIndexForByte(lines, 0) == 0);
    CHECK(lineIndexForByte(lines, 4) == 0);
    CHECK(lineIndexForByte(lines, 5) == 1);
    CHECK(lineIndexForByte(lines, 9) == 1);
}

TEST_CASE("TextEditUtils: lineIndexForByte prefers exact visual line starts at empty-line boundaries") {
    std::vector<LineMetrics> lines = {
        LineMetrics {.byteStart = 0, .byteEnd = 5},
        LineMetrics {.byteStart = 5, .byteEnd = 6},
        LineMetrics {.byteStart = 6, .byteEnd = 10},
    };
    CHECK(lineIndexForByte(lines, 5) == 1);
    CHECK(lineIndexForByte(lines, 6) == 2);
}

TEST_CASE("TextEditUtils: lineIndexForByte keeps caret on the current visual line at half-open line ends") {
    std::vector<LineMetrics> lines = {
        LineMetrics {.byteStart = 0, .byteEnd = 20},
        LineMetrics {.byteStart = 21, .byteEnd = 22},
        LineMetrics {.byteStart = 22, .byteEnd = 46},
        LineMetrics {.byteStart = 47, .byteEnd = 48},
        LineMetrics {.byteStart = 48, .byteEnd = 64},
    };

    CHECK(lineIndexForByte(lines, 20) == 0);
    CHECK(lineIndexForByte(lines, 21) == 1);
    CHECK(lineIndexForByte(lines, 46) == 2);
    CHECK(lineIndexForByte(lines, 47) == 3);
    CHECK(lineIndexForByte(lines, 48) == 4);
}

TEST_CASE("TextEditUtils: makeTextEditLayoutResult builds line metrics") {
    auto layout = std::make_shared<TextLayout>();
    layout->lines.push_back(TextLayout::LineRange {
        .ctLineIndex = 4,
        .byteStart = 0,
        .byteEnd = 5,
        .lineMinX = 2.f,
        .top = 1.f,
        .bottom = 13.f,
        .baseline = 10.f,
    });

    TextEditLayoutResult const result = makeTextEditLayoutResult(layout, 5, 120.f);
    REQUIRE(result.layout != nullptr);
    REQUIRE(result.lines.size() == 1);
    CHECK(result.textByteCount == 5);
    CHECK(result.contentWidth == doctest::Approx(120.f));
    CHECK(result.lines[0].ctLineIndex == 4);
    CHECK(result.lines[0].byteStart == 0);
    CHECK(result.lines[0].byteEnd == 5);
}

TEST_CASE("TextEditUtils: normalizeLineMetricsForEditing repairs zero-length empty lines from neighboring spans") {
    std::vector<LineMetrics> lines = {
        LineMetrics {.byteStart = 0, .byteEnd = 20, .ctLineIndex = 0},
        LineMetrics {.byteStart = 42, .byteEnd = 42, .ctLineIndex = 1},
        LineMetrics {.byteStart = 22, .byteEnd = 46, .ctLineIndex = 2},
    };

    normalizeLineMetricsForEditing(lines, 64);

    CHECK(lines[0].byteStart == 0);
    CHECK(lines[0].byteEnd == 20);
    CHECK(lines[1].byteStart == 21);
    CHECK(lines[1].byteEnd == 22);
    CHECK(lines[2].byteStart == 22);
    CHECK(lines[2].byteEnd == 46);
    CHECK(lineIndexForByte(lines, 21) == 1);
    CHECK(lineIndexForByte(lines, 22) == 2);
}

TEST_CASE("TextEditUtils: moveCaretVertically preserves empty visual lines") {
    auto layout = std::make_shared<TextLayout>();
    layout->lines = {
        TextLayout::LineRange {.ctLineIndex = 0, .byteStart = 0, .byteEnd = 2, .lineMinX = 0.f, .top = 0.f, .bottom = 10.f, .baseline = 8.f},
        TextLayout::LineRange {.ctLineIndex = 1, .byteStart = 2, .byteEnd = 2, .lineMinX = 0.f, .top = 10.f, .bottom = 20.f, .baseline = 18.f},
        TextLayout::LineRange {.ctLineIndex = 2, .byteStart = 3, .byteEnd = 5, .lineMinX = 0.f, .top = 20.f, .bottom = 30.f, .baseline = 28.f},
    };
    TextEditLayoutResult const result = makeTextEditLayoutResult(layout, 5, 120.f);

    CHECK(moveCaretVertically(result, "ab\ncd", 1, 1) == 2);
    CHECK(moveCaretVertically(result, "ab\ncd", 3, -1) == 2);
}

TEST_CASE("TextEditUtils: scrollOffsetYToKeepCaretVisible handles empty visual lines") {
    auto layout = std::make_shared<TextLayout>();
    layout->lines = {
        TextLayout::LineRange {.ctLineIndex = 0, .byteStart = 0, .byteEnd = 2, .lineMinX = 0.f, .top = 0.f, .bottom = 10.f, .baseline = 8.f},
        TextLayout::LineRange {.ctLineIndex = 1, .byteStart = 2, .byteEnd = 2, .lineMinX = 0.f, .top = 10.f, .bottom = 20.f, .baseline = 18.f},
        TextLayout::LineRange {.ctLineIndex = 2, .byteStart = 3, .byteEnd = 5, .lineMinX = 0.f, .top = 20.f, .bottom = 30.f, .baseline = 28.f},
    };
    TextEditLayoutResult const result = makeTextEditLayoutResult(layout, 5, 120.f);

    CHECK(scrollOffsetYToKeepCaretVisible(result, 0.f, 8.f, 2, 2.f) == doctest::Approx(14.f));
    CHECK(scrollOffsetYToKeepCaretVisible(result, 12.f, 8.f, 2, 2.f) == doctest::Approx(8.f));
}

TEST_CASE("TextEditUtils: selectionRects spans wrapped lines") {
    auto layout = std::make_shared<TextLayout>();
    auto storage = std::make_unique<TextLayoutStorage>();
    storage->glyphArena = {1, 2, 3, 4};
    storage->positionArena = {{0.f, 0.f}, {10.f, 0.f}, {0.f, 0.f}, {10.f, 0.f}};

    TextLayout::PlacedRun run0 {};
    run0.run.glyphIds = std::span<std::uint32_t const>(storage->glyphArena.data(), 2);
    run0.run.positions = std::span<Point const>(storage->positionArena.data(), 2);
    run0.run.ascent = 8.f;
    run0.run.descent = 2.f;
    run0.run.width = 20.f;
    run0.origin = {0.f, 8.f};
    run0.utf8Begin = 0;
    run0.utf8End = 2;
    run0.ctLineIndex = 0;

    TextLayout::PlacedRun run1 = run0;
    run1.run.glyphIds = std::span<std::uint32_t const>(storage->glyphArena.data() + 2, 2);
    run1.run.positions = std::span<Point const>(storage->positionArena.data() + 2, 2);
    run1.origin = {0.f, 24.f};
    run1.utf8Begin = 2;
    run1.utf8End = 4;
    run1.ctLineIndex = 1;

    layout->runs = {run0, run1};
    layout->lines = {
        TextLayout::LineRange {.ctLineIndex = 0, .byteStart = 0, .byteEnd = 2, .lineMinX = 0.f, .top = 0.f, .bottom = 10.f, .baseline = 8.f},
        TextLayout::LineRange {.ctLineIndex = 1, .byteStart = 2, .byteEnd = 4, .lineMinX = 0.f, .top = 16.f, .bottom = 26.f, .baseline = 24.f},
    };
    layout->ownedStorage = std::move(storage);

    TextEditLayoutResult const result = makeTextEditLayoutResult(layout, 4, 100.f);
    std::vector<Rect> const rects =
        selectionRects(result, TextEditSelection {.caretByte = 4, .anchorByte = 1}, nullptr, 5.f, 7.f, 3.f);

    REQUIRE(rects.size() == 2);
    CHECK(rects[0].x == doctest::Approx(15.f));
    CHECK(rects[0].y == doctest::Approx(7.f));
    CHECK(rects[0].width == doctest::Approx(10.f));
    CHECK(rects[0].height == doctest::Approx(13.f));
    CHECK(rects[1].x == doctest::Approx(5.f));
    CHECK(rects[1].y == doctest::Approx(23.f));
    CHECK(rects[1].width == doctest::Approx(20.f));
    CHECK(rects[1].height == doctest::Approx(13.f));
}

TEST_CASE("TextEditUtils: selectionRects uses caret-width highlight for empty visual line") {
    auto layout = std::make_shared<TextLayout>();
    layout->lines = {
        TextLayout::LineRange {.ctLineIndex = 0, .byteStart = 0, .byteEnd = 1, .lineMinX = 0.f, .top = 0.f, .bottom = 10.f, .baseline = 8.f},
    };

    TextEditLayoutResult const result = makeTextEditLayoutResult(layout, 1, 100.f);
    std::string const text = "\n";
    std::vector<Rect> const rects =
        selectionRects(result, TextEditSelection {.caretByte = 1, .anchorByte = 0}, &text, 0.f, 0.f, 0.f);

    REQUIRE(rects.size() == 1);
    CHECK(rects[0].width == doctest::Approx(kTextCaretStrokeWidthPx));
}

TEST_CASE("TextEditUtils: lineHitAtY clamps and locates lines") {
    TextEditLayoutResult result;
    result.lines = {
        LineMetrics {.top = 0.f, .bottom = 10.f, .baseline = 8.f, .byteStart = 0, .byteEnd = 2},
        LineMetrics {.top = 16.f, .bottom = 28.f, .baseline = 24.f, .byteStart = 2, .byteEnd = 4},
    };

    auto const above = lineHitAtY(result, -5.f);
    CHECK(above.lineIndex == 0);
    CHECK(above.clamped);

    auto const inside = lineHitAtY(result, 20.f);
    CHECK(inside.lineIndex == 1);
    CHECK(!inside.clamped);

    auto const below = lineHitAtY(result, 40.f);
    CHECK(below.lineIndex == 1);
    CHECK(below.clamped);
}

TEST_CASE("TextEditUtils: lineHitAtY uses nearest-line partition for empty lines") {
    auto layout = std::make_shared<TextLayout>();
    auto storage = std::make_unique<TextLayoutStorage>();
    storage->glyphArena = {1};
    storage->positionArena = {{0.f, 0.f}};

    TextLayout::PlacedRun run {};
    run.run.glyphIds = std::span<std::uint32_t const>(storage->glyphArena.data(), 1);
    run.run.positions = std::span<Point const>(storage->positionArena.data(), 1);
    run.run.ascent = 8.f;
    run.run.descent = 2.f;
    run.run.width = 10.f;
    run.origin = {0.f, 8.f};
    run.utf8Begin = 0;
    run.utf8End = 1;
    run.ctLineIndex = 0;

    layout->runs = {run};
    layout->lines = {
        TextLayout::LineRange {.ctLineIndex = 0, .byteStart = 0, .byteEnd = 2, .lineMinX = 0.f, .top = 0.f, .bottom = 10.f, .baseline = 8.f},
        TextLayout::LineRange {.ctLineIndex = 1, .byteStart = 2, .byteEnd = 3, .lineMinX = 0.f, .top = 11.f, .bottom = 11.5f, .baseline = 18.f},
        TextLayout::LineRange {.ctLineIndex = 2, .byteStart = 3, .byteEnd = 4, .lineMinX = 0.f, .top = 24.f, .bottom = 34.f, .baseline = 32.f},
    };
    layout->ownedStorage = std::move(storage);

    TextEditLayoutResult const result = makeTextEditLayoutResult(layout, 4, 100.f);
    auto const hit = lineHitAtY(result, 17.f);
    CHECK(hit.lineIndex == 1);
}

TEST_CASE("TextEditUtils: caretByteAtPoint and moveCaretVertically use layout result") {
    auto layout = std::make_shared<TextLayout>();
    auto storage = std::make_unique<TextLayoutStorage>();
    storage->glyphArena = {1, 2, 3, 4};
    storage->positionArena = {{0.f, 0.f}, {10.f, 0.f}, {0.f, 0.f}, {10.f, 0.f}};

    TextLayout::PlacedRun run0 {};
    run0.run.glyphIds = std::span<std::uint32_t const>(storage->glyphArena.data(), 2);
    run0.run.positions = std::span<Point const>(storage->positionArena.data(), 2);
    run0.run.ascent = 8.f;
    run0.run.descent = 2.f;
    run0.run.width = 20.f;
    run0.origin = {0.f, 8.f};
    run0.utf8Begin = 0;
    run0.utf8End = 2;
    run0.ctLineIndex = 0;

    TextLayout::PlacedRun run1 = run0;
    run1.run.glyphIds = std::span<std::uint32_t const>(storage->glyphArena.data() + 2, 2);
    run1.run.positions = std::span<Point const>(storage->positionArena.data() + 2, 2);
    run1.origin = {0.f, 24.f};
    run1.utf8Begin = 2;
    run1.utf8End = 4;
    run1.ctLineIndex = 1;

    layout->runs = {run0, run1};
    layout->lines = {
        TextLayout::LineRange {.ctLineIndex = 0, .byteStart = 0, .byteEnd = 2, .lineMinX = 0.f, .top = 0.f, .bottom = 10.f, .baseline = 8.f},
        TextLayout::LineRange {.ctLineIndex = 1, .byteStart = 2, .byteEnd = 4, .lineMinX = 0.f, .top = 16.f, .bottom = 26.f, .baseline = 24.f},
    };
    layout->ownedStorage = std::move(storage);

    TextEditLayoutResult const result = makeTextEditLayoutResult(layout, 4, 100.f);
    CHECK(caretByteAtPoint(result, Point {15.f, 5.f}, "abcd") == 1);
    CHECK(caretByteAtPoint(result, Point {15.f, 20.f}, "abcd") == 3);
    CHECK(moveCaretVertically(result, "abcd", 1, 1) == 3);
    CHECK(moveCaretVertically(result, "abcd", 3, -1) == 1);
}

TEST_CASE("TextEditUtils: caretByteAtPoint clamps to visual line end before newline") {
    auto layout = std::make_shared<TextLayout>();
    auto storage = std::make_unique<TextLayoutStorage>();
    storage->glyphArena = {1, 2, 3};
    storage->positionArena = {{0.f, 0.f}, {10.f, 0.f}, {20.f, 0.f}};

    TextLayout::PlacedRun run {};
    run.run.glyphIds = std::span<std::uint32_t const>(storage->glyphArena.data(), 3);
    run.run.positions = std::span<Point const>(storage->positionArena.data(), 3);
    run.run.ascent = 8.f;
    run.run.descent = 2.f;
    run.run.width = 30.f;
    run.origin = {0.f, 8.f};
    run.utf8Begin = 0;
    run.utf8End = 3;
    run.ctLineIndex = 0;

    layout->runs = {run};
    layout->lines = {
        TextLayout::LineRange {.ctLineIndex = 0, .byteStart = 0, .byteEnd = 4, .lineMinX = 0.f, .top = 0.f, .bottom = 10.f, .baseline = 8.f},
        TextLayout::LineRange {.ctLineIndex = 1, .byteStart = 4, .byteEnd = 5, .lineMinX = 0.f, .top = 16.f, .bottom = 26.f, .baseline = 24.f},
    };
    layout->ownedStorage = std::move(storage);

    TextEditLayoutResult const result = makeTextEditLayoutResult(layout, 5, 100.f);
    CHECK(caretByteAtPoint(result, Point {200.f, 5.f}, "abc\nx") == 3);
}

TEST_CASE("TextEditUtils: caretRect returns caret geometry for a byte") {
    auto layout = std::make_shared<TextLayout>();
    auto storage = std::make_unique<TextLayoutStorage>();
    storage->glyphArena = {1, 2};
    storage->positionArena = {{0.f, 0.f}, {10.f, 0.f}};

    TextLayout::PlacedRun run {};
    run.run.glyphIds = std::span<std::uint32_t const>(storage->glyphArena.data(), 2);
    run.run.positions = std::span<Point const>(storage->positionArena.data(), 2);
    run.run.ascent = 8.f;
    run.run.descent = 2.f;
    run.run.width = 20.f;
    run.origin = {0.f, 8.f};
    run.utf8Begin = 0;
    run.utf8End = 2;
    run.ctLineIndex = 0;

    layout->runs = {run};
    layout->lines = {
        TextLayout::LineRange {.ctLineIndex = 0, .byteStart = 0, .byteEnd = 2, .lineMinX = 0.f, .top = 0.f, .bottom = 10.f, .baseline = 8.f},
    };
    layout->ownedStorage = std::move(storage);

    TextEditLayoutResult const result = makeTextEditLayoutResult(layout, 2, 20.f);
    Rect const rect = caretRect(result, 1, 5.f, 7.f, 2.f);
    CHECK(rect.x == doctest::Approx(14.f));
    CHECK(rect.y == doctest::Approx(7.f));
    CHECK(rect.width == doctest::Approx(2.f));
    CHECK(rect.height == doctest::Approx(10.f));
}

TEST_CASE("TextEditUtils: scrollOffsetXForByte and scrollByteToKeepCaretVisible") {
    auto layout = std::make_shared<TextLayout>();
    auto storage = std::make_unique<TextLayoutStorage>();
    storage->glyphArena = {1, 2, 3, 4};
    storage->positionArena = {{0.f, 0.f}, {10.f, 0.f}, {20.f, 0.f}, {30.f, 0.f}};

    TextLayout::PlacedRun run {};
    run.run.glyphIds = std::span<std::uint32_t const>(storage->glyphArena.data(), 4);
    run.run.positions = std::span<Point const>(storage->positionArena.data(), 4);
    run.run.ascent = 8.f;
    run.run.descent = 2.f;
    run.run.width = 40.f;
    run.origin = {0.f, 8.f};
    run.utf8Begin = 0;
    run.utf8End = 4;
    run.ctLineIndex = 0;

    layout->runs = {run};
    layout->lines = {
        TextLayout::LineRange {.ctLineIndex = 0, .byteStart = 0, .byteEnd = 4, .lineMinX = 0.f, .top = 0.f, .bottom = 10.f, .baseline = 8.f},
    };
    layout->ownedStorage = std::move(storage);

    TextEditLayoutResult const result = makeTextEditLayoutResult(layout, 4, 40.f);
    CHECK(scrollOffsetXForByte(result, 2) == doctest::Approx(20.f));
    CHECK(scrollByteToKeepCaretVisible(result, "abcd", 0, 3, 16.f, 4.f) == 2);
}

TEST_CASE("TextEditUtils: scrollOffsetYToKeepCaretVisible adjusts to line bounds") {
    TextEditLayoutResult result;
    result.lines = {
        LineMetrics {.top = 0.f, .bottom = 10.f, .byteStart = 0, .byteEnd = 2},
        LineMetrics {.top = 16.f, .bottom = 26.f, .byteStart = 2, .byteEnd = 4},
        LineMetrics {.top = 32.f, .bottom = 42.f, .byteStart = 4, .byteEnd = 6},
    };
    result.layout = std::make_shared<TextLayout>();

    CHECK(scrollOffsetYToKeepCaretVisible(result, 0.f, 20.f, 0, 4.f) == doctest::Approx(-4.f));
    CHECK(scrollOffsetYToKeepCaretVisible(result, 0.f, 20.f, 3, 4.f) == doctest::Approx(10.f));
    CHECK(scrollOffsetYToKeepCaretVisible(result, 12.f, 20.f, 3, 4.f) == doctest::Approx(12.f));
}
