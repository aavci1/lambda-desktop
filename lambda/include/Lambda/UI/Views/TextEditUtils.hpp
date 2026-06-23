#pragma once

/// \file Lambda/UI/Views/TextEditUtils.hpp
///
/// Pure, stateless UTF-8 and text-layout helpers for text editing.

#include <Lambda/Core/Geometry.hpp>
#include <Lambda/Graphics/TextLayout.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lambdaui {

namespace detail {

/// Default caret stroke width (logical px) for single-line and multiline text fields.
inline constexpr float kTextCaretStrokeWidthPx = 2.f;

// UTF-8 navigation
int utf8NextChar(std::string const &s, int pos) noexcept;
int utf8PrevChar(std::string const &s, int pos) noexcept;
int utf8Clamp(std::string const &s, int pos) noexcept;
int utf8PrevWord(std::string const &s, int pos) noexcept;
int utf8NextWord(std::string const &s, int pos) noexcept;
int utf8CountChars(std::string const &s) noexcept;
std::string utf8TruncateToChars(std::string const &s, int maxChars);

/// Returns true if inserting `inserted` at `pos` into `prev` should coalesce with the previous typing
/// group for undo purposes.
bool shouldCoalesceInsert(std::string const &prev, int pos, std::string_view inserted) noexcept;

struct LineMetrics {
    float top {};
    float bottom {};
    float baseline {};
    float lineMinX {};
    int byteStart = 0;
    int byteEnd = 0;
    std::uint32_t ctLineIndex = 0;
};

struct TextEditSelection {
    int caretByte = 0;
    int anchorByte = 0;

    [[nodiscard]] bool hasSelection() const noexcept { return caretByte != anchorByte; }
    [[nodiscard]] std::pair<int, int> ordered() const noexcept;
    bool operator==(TextEditSelection const&) const = default;
};

struct TextEditState {
    TextEditSelection selection {};
    float preferredColumnX = 0.f;
    bool draggingSelection = false;
    bool focused = false;
    bool disabled = false;
    bool multiline = false;
};

struct TextEditLayoutResult {
    std::shared_ptr<TextLayout const> layout;
    std::vector<LineMetrics> lines;
    int textByteCount = 0;
    float contentWidth = 0.f;

    [[nodiscard]] bool empty() const noexcept { return !layout; }
};

struct TextEditLineHit {
    int lineIndex = 0;
    bool clamped = false;
};

struct TextEditMutation {
    std::string text;
    TextEditSelection selection {};
    bool valueChanged = false;
    bool coalescableTyping = false;
};

std::vector<LineMetrics> buildLineMetrics(TextLayout const &layout);
void normalizeLineMetricsForEditing(std::vector<LineMetrics> &lines, int textByteCount) noexcept;
TextEditLayoutResult makeTextEditLayoutResult(std::shared_ptr<TextLayout const> layout, int textByteCount,
                                              float contentWidth);
TextEditSelection clampSelection(std::string const &text, TextEditSelection selection) noexcept;
TextEditSelection moveSelectionToByte(std::string const &text, TextEditSelection selection, int byte,
                                      bool extendSelection) noexcept;
TextEditSelection selectAllSelection(std::string const &text) noexcept;
TextEditSelection clearSelection(TextEditSelection selection) noexcept;
TextEditSelection moveSelectionByChar(std::string const &text, TextEditSelection selection, int direction,
                                      bool extendSelection) noexcept;
TextEditSelection moveSelectionByWord(std::string const &text, TextEditSelection selection, int direction,
                                      bool extendSelection) noexcept;
TextEditSelection moveSelectionToLineBoundary(std::string const &text, TextEditSelection selection, bool end,
                                              bool extendSelection) noexcept;
TextEditSelection moveSelectionToDocumentBoundary(std::string const &text, TextEditSelection selection, bool end,
                                                  bool extendSelection) noexcept;
TextEditSelection wordSelectionAtByte(std::string const &text, int byteOffset) noexcept;
TextEditMutation insertText(std::string const &text, TextEditSelection const &selection, std::string_view insert,
                            int maxLength = 0);
TextEditMutation eraseSelectionOrChar(std::string const &text, TextEditSelection const &selection,
                                      bool forward) noexcept;
TextEditMutation eraseWord(std::string const &text, TextEditSelection const &selection, bool forward) noexcept;
TextEditMutation eraseToLineBoundary(std::string const &text, TextEditSelection const &selection,
                                     bool forward) noexcept;
TextEditSelection selectCurrentLine(std::string const &text, TextEditSelection const &selection) noexcept;
TextEditMutation eraseCurrentLine(std::string const &text, TextEditSelection const &selection) noexcept;
TextEditMutation insertLineAdjacent(std::string const &text, TextEditSelection const &selection,
                                    bool above, int maxLength = 0);
TextEditMutation moveCurrentLine(std::string const &text, TextEditSelection const &selection,
                                 int direction) noexcept;
TextEditMutation copyCurrentLine(std::string const &text, TextEditSelection const &selection,
                                 int direction, int maxLength = 0);

/// Binary search over sorted `byteStart`. Returns index of the line containing `byteOffset`, clamped.
int lineIndexForByte(std::vector<LineMetrics> const &lines, int byteOffset) noexcept;
int lineIndexForByte(TextEditLayoutResult const &result, int byteOffset) noexcept;
TextEditLineHit lineHitAtY(TextEditLayoutResult const &result, float layoutY) noexcept;
int caretByteAtPoint(TextEditLayoutResult const &result, Point layoutPoint, std::string const &buf) noexcept;
int caretByteAtViewportPoint(TextEditLayoutResult const &result, Point viewportPoint, Point contentOrigin,
                             Point scrollOffset, std::string const &buf) noexcept;
int moveCaretVertically(TextEditLayoutResult const &result, std::string const &buf, int currentByte,
                        int direction) noexcept;
float scrollOffsetXForByte(TextEditLayoutResult const &result, int byteOffset) noexcept;
int scrollByteToKeepCaretVisible(TextEditLayoutResult const &result, std::string const &buf, int scrollByte,
                                 int caretByte, float viewportWidth, float marginPx) noexcept;
float scrollOffsetYToKeepCaretVisible(TextEditLayoutResult const &result, float scrollY, float viewportHeight,
                                      int caretByte, float marginPx) noexcept;

float caretXForByte(TextLayout const &layout, LineMetrics const &line, int byteOffset) noexcept;
float caretXForByte(TextEditLayoutResult const &result, int byteOffset) noexcept;

/// Vertical extent for drawing a caret on \p line (layout Y, same as `LineMetrics::top/bottom`).
/// Computed from runs on that CT line (`min(origin.y - ascent)`, `max(origin.y + descent)`), matching
/// `drawTextLayout` / Core Text line boxes. When the line has no runs (empty line), falls back to
/// `LineMetrics` (extending the box to the layout’s max typographic line height when needed) or
/// baseline ± max ascent/descent from any run in the layout.
std::pair<float, float> lineCaretYRangeInLayout(TextLayout const &layout, LineMetrics const &line) noexcept;
std::pair<float, float> lineCaretYRangeInLayout(TextEditLayoutResult const &result, int byteOffset) noexcept;
Rect caretRect(TextEditLayoutResult const &result, int byteOffset, float originX = 0.f, float originY = 0.f,
               float strokeWidth = kTextCaretStrokeWidthPx) noexcept;

int caretByteAtX(TextLayout const &layout, LineMetrics const &line, float layoutX, std::string const &buf) noexcept;

std::pair<int, int> orderedSelection(int caret, int anchor) noexcept;
std::vector<Rect> selectionRects(TextEditLayoutResult const &result, TextEditSelection const &selection,
                                 std::string const *text = nullptr, float originX = 0.f, float originY = 0.f,
                                 float extraBottomPx = 0.f) noexcept;

} // namespace detail

} // namespace lambdaui
