#include <Lambda/UI/Views/TextEditUtils.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct ByteRange {
    int start = 0;
    int end = 0;
};

bool utf8DecodeAt(std::string const &s, int i, char32_t &outCp, int &outLen) {
    int const n = static_cast<int>(s.size());
    if (i < 0 || i >= n) {
        return false;
    }
    auto const u = static_cast<unsigned char>(s[static_cast<std::size_t>(i)]);
    if ((u & 0x80) == 0) {
        outCp = u;
        outLen = 1;
        return true;
    }
    if ((u & 0xE0) == 0xC0 && i + 1 < n) {
        auto const u1 = static_cast<unsigned char>(s[static_cast<std::size_t>(i + 1)]);
        if ((u1 & 0xC0) != 0x80) {
            return false;
        }
        outCp = (u & 0x1F) << 6 | (u1 & 0x3F);
        outLen = 2;
        return true;
    }
    if ((u & 0xF0) == 0xE0 && i + 2 < n) {
        auto const u1 = static_cast<unsigned char>(s[static_cast<std::size_t>(i + 1)]);
        auto const u2 = static_cast<unsigned char>(s[static_cast<std::size_t>(i + 2)]);
        if ((u1 & 0xC0) != 0x80 || (u2 & 0xC0) != 0x80) {
            return false;
        }
        outCp = (u & 0x0F) << 12 | (u1 & 0x3F) << 6 | (u2 & 0x3F);
        outLen = 3;
        return true;
    }
    if ((u & 0xF8) == 0xF0 && i + 3 < n) {
        auto const u1 = static_cast<unsigned char>(s[static_cast<std::size_t>(i + 1)]);
        auto const u2 = static_cast<unsigned char>(s[static_cast<std::size_t>(i + 2)]);
        auto const u3 = static_cast<unsigned char>(s[static_cast<std::size_t>(i + 3)]);
        if ((u1 & 0xC0) != 0x80 || (u2 & 0xC0) != 0x80 || (u3 & 0xC0) != 0x80) {
            return false;
        }
        outCp = (u & 0x07) << 18 | (u1 & 0x3F) << 12 | (u2 & 0x3F) << 6 | (u3 & 0x3F);
        outLen = 4;
        return true;
    }
    return false;
}

bool isSpaceChar(char32_t cp) {
    return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == 0x3000;
}

bool isWordChar(char32_t cp) {
    if (cp <= 0x7F) {
        return (cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') || cp == '_';
    }
    if (isSpaceChar(cp)) {
        return false;
    }
    return true;
}

bool isPunctuationChar(char32_t cp) {
    return !isSpaceChar(cp) && !isWordChar(cp);
}

int utf8NextCharImpl(std::string const &s, int pos) {
    int const n = static_cast<int>(s.size());
    if (pos >= n) {
        return pos;
    }
    int len = 1;
    char32_t cp {};
    if (utf8DecodeAt(s, pos, cp, len)) {
        return pos + len;
    }
    return pos + 1;
}

int utf8PrevCharImpl(std::string const &s, int pos) {
    if (pos <= 0) {
        return 0;
    }
    int p = pos - 1;
    while (p > 0 && (static_cast<unsigned char>(s[static_cast<std::size_t>(p)]) & 0xC0) == 0x80) {
        --p;
    }
    return p;
}

int utf8ClampImpl(std::string const &s, int pos) {
    int const n = static_cast<int>(s.size());
    if (pos <= 0) {
        return 0;
    }
    if (pos >= n) {
        return n;
    }
    int p = pos;
    while (p > 0 && (static_cast<unsigned char>(s[static_cast<std::size_t>(p)]) & 0xC0) == 0x80) {
        --p;
    }
    char32_t cp {};
    int len = 1;
    if (!utf8DecodeAt(s, p, cp, len)) {
        return pos;
    }
    if (p + len < pos) {
        return p + len;
    }
    return p;
}

int lineStartForByte(std::string const &text, int byte) {
    int const size = static_cast<int>(text.size());
    byte = std::clamp(byte, 0, size);
    for (int i = byte - 1; i >= 0; --i) {
        if (text[static_cast<std::size_t>(i)] == '\n') {
            return i + 1;
        }
    }
    return 0;
}

int lineEndIncludingNewline(std::string const &text, int byte) {
    int const size = static_cast<int>(text.size());
    byte = std::clamp(byte, 0, size);
    for (int i = byte; i < size; ++i) {
        if (text[static_cast<std::size_t>(i)] == '\n') {
            return i + 1;
        }
    }
    return size;
}

ByteRange lineRangeForSelection(std::string const &text,
                                lambda::detail::TextEditSelection selection) {
    int const size = static_cast<int>(text.size());
    lambda::detail::TextEditSelection const clamped =
        lambda::detail::clampSelection(text, selection);
    auto const [orderedStart, orderedEnd] = clamped.ordered();
    int const endProbe = orderedEnd > orderedStart ? std::max(0, orderedEnd - 1) : orderedEnd;
    return ByteRange{
        .start = lineStartForByte(text, orderedStart),
        .end = size == 0 ? 0 : lineEndIncludingNewline(text, endProbe),
    };
}

lambda::detail::TextEditSelection shiftSelection(std::string const &text,
                                                 lambda::detail::TextEditSelection selection,
                                                 int delta) {
    return lambda::detail::clampSelection(text, lambda::detail::TextEditSelection{
        .caretByte = selection.caretByte + delta,
        .anchorByte = selection.anchorByte + delta,
    });
}

int utf8PrevWordImpl(std::string const &s, int pos) {
    pos = utf8ClampImpl(s, pos);
    if (pos <= 0) {
        return 0;
    }
    int p = pos;
    while (p > 0) {
        int const prevStart = utf8PrevCharImpl(s, p);
        char32_t cp = 0;
        int len = 1;
        if (!utf8DecodeAt(s, prevStart, cp, len)) {
            p = prevStart;
            continue;
        }
        if (!isSpaceChar(cp)) {
            break;
        }
        p = prevStart;
    }
    if (p <= 0) {
        return 0;
    }
    int const anchorStart = utf8PrevCharImpl(s, p);
    char32_t anchorCp = 0;
    int anchorLen = 1;
    if (!utf8DecodeAt(s, anchorStart, anchorCp, anchorLen)) {
        return anchorStart;
    }
    bool const wantWord = isWordChar(anchorCp);
    bool const wantPunct = isPunctuationChar(anchorCp);
    while (p > 0) {
        int const prevStart = utf8PrevCharImpl(s, p);
        char32_t cp = 0;
        int len = 1;
        if (!utf8DecodeAt(s, prevStart, cp, len)) {
            p = prevStart;
            continue;
        }
        if ((wantWord && !isWordChar(cp)) || (wantPunct && !isPunctuationChar(cp))) {
            break;
        }
        p = prevStart;
    }
    return p;
}

int utf8NextWordImpl(std::string const &s, int pos) {
    int p = utf8ClampImpl(s, pos);
    int const n = static_cast<int>(s.size());
    while (p < n) {
        char32_t cp = 0;
        int len = 1;
        if (!utf8DecodeAt(s, p, cp, len)) {
            p = utf8NextCharImpl(s, p);
            continue;
        }
        if (!isSpaceChar(cp)) {
            break;
        }
        p += len;
    }
    if (p >= n) {
        return n;
    }
    char32_t anchorCp = 0;
    int anchorLen = 1;
    if (!utf8DecodeAt(s, p, anchorCp, anchorLen)) {
        return utf8NextCharImpl(s, p);
    }
    bool const wantWord = isWordChar(anchorCp);
    bool const wantPunct = isPunctuationChar(anchorCp);
    while (p < n) {
        char32_t cp = 0;
        int len = 1;
        if (!utf8DecodeAt(s, p, cp, len)) {
            p = utf8NextCharImpl(s, p);
            continue;
        }
        if ((wantWord && !isWordChar(cp)) || (wantPunct && !isPunctuationChar(cp))) {
            break;
        }
        p += len;
    }
    return p;
}

float caretXInRun(lambda::TextLayout::PlacedRun const &pr, int byteOffset) {
    int const b0 = static_cast<int>(pr.utf8Begin);
    int const b1 = static_cast<int>(pr.utf8End);
    int const nB = b1 - b0;
    if (nB <= 0) {
        return pr.origin.x;
    }
    if (byteOffset <= b0) {
        return pr.origin.x;
    }
    if (byteOffset >= b1) {
        return pr.origin.x + pr.run.width;
    }
    int const nG = static_cast<int>(pr.run.positions.size());
    if (nG > 0 && nG == nB) {
        int rel = byteOffset - b0;
        rel = std::clamp(rel, 0, nG);
        if (rel >= nG) {
            return pr.origin.x + pr.run.width;
        }
        return pr.origin.x + pr.run.positions[static_cast<std::size_t>(rel)].x;
    }
    float const t = static_cast<float>(byteOffset - b0) / static_cast<float>(nB);
    return pr.origin.x + t * pr.run.width;
}

int glyphIndexForLocalX(lambda::TextRun const &run, float localX) {
    int const nG = static_cast<int>(run.positions.size());
    if (nG == 0) {
        return 0;
    }
    localX = std::max(0.f, std::min(localX, run.width));
    for (int i = 0; i < nG; ++i) {
        float const gx = run.positions[static_cast<std::size_t>(i)].x;
        float const nextX = (i + 1 < nG) ? run.positions[static_cast<std::size_t>(i + 1)].x : run.width;
        float const mid = (gx + nextX) * 0.5f;
        if (localX < mid) {
            return i;
        }
    }
    return nG - 1;
}

int byteAtLocalXInRun(lambda::TextLayout::PlacedRun const &pr, float localX, std::string const &buf) {
    int const b0 = static_cast<int>(pr.utf8Begin);
    int const b1 = static_cast<int>(pr.utf8End);
    int const nBytes = b1 - b0;
    if (nBytes <= 0) {
        return b0;
    }

    int const nG = static_cast<int>(pr.run.positions.size());
    localX = std::max(0.f, std::min(localX, pr.run.width));

    if (nG == 0 || nG != nBytes) {
        float const t = localX / std::max(1e-6f, pr.run.width);
        int const idx = static_cast<int>(std::lround(t * static_cast<float>(nBytes)));
        int p = b0;
        int remaining = std::clamp(idx, 0, nBytes);
        while (remaining-- > 0 && p < b1) {
            p = lambda::detail::utf8NextChar(buf, p);
            if (p > b1) {
                p = b1;
                break;
            }
        }
        return lambda::detail::utf8Clamp(buf, std::min(p, b1));
    }

    int const g = glyphIndexForLocalX(pr.run, localX);
    int p = b0;
    for (int i = 0; i < g && p < b1; ++i) {
        p = lambda::detail::utf8NextChar(buf, p);
    }
    return lambda::detail::utf8Clamp(buf, std::min(p, b1));
}

int visualLineEndByte(lambda::detail::LineMetrics const &line, std::string const &buf) {
    int end = lambda::detail::utf8Clamp(buf, line.byteEnd);
    while (end > line.byteStart) {
        int const prev = lambda::detail::utf8PrevChar(buf, end);
        char32_t cp = 0;
        int len = 1;
        if (!utf8DecodeAt(buf, prev, cp, len)) {
            break;
        }
        if (cp != '\n' && cp != '\r') {
            break;
        }
        end = prev;
    }
    return std::max(line.byteStart, end);
}

} // namespace

namespace lambda::detail {

int utf8NextChar(std::string const &s, int pos) noexcept {
    return utf8NextCharImpl(s, pos);
}

int utf8PrevChar(std::string const &s, int pos) noexcept {
    return utf8PrevCharImpl(s, pos);
}

int utf8Clamp(std::string const &s, int pos) noexcept {
    return utf8ClampImpl(s, pos);
}

int utf8PrevWord(std::string const &s, int pos) noexcept {
    return utf8PrevWordImpl(s, pos);
}

int utf8NextWord(std::string const &s, int pos) noexcept {
    return utf8NextWordImpl(s, pos);
}

bool shouldCoalesceInsert(std::string const &prev, int pos, std::string_view inserted) noexcept {
    if (inserted.empty()) {
        return false;
    }
    std::string const chunk(inserted);
    if (utf8NextChar(chunk, 0) != static_cast<int>(chunk.size())) {
        return false;
    }
    char32_t insCp = 0;
    int insLen = 1;
    if (!utf8DecodeAt(chunk, 0, insCp, insLen)) {
        return false;
    }
    if (isSpaceChar(insCp)) {
        return false;
    }
    if (!isWordChar(insCp)) {
        return false;
    }
    if (pos <= 0) {
        return true;
    }
    char32_t prevCp = 0;
    int prevLen = 1;
    int const prevStart = utf8PrevCharImpl(prev, pos);
    if (!utf8DecodeAt(prev, prevStart, prevCp, prevLen)) {
        return false;
    }
    return isWordChar(prevCp);
}

int utf8CountChars(std::string const &s) noexcept {
    int n = 0;
    int i = 0;
    int const len = static_cast<int>(s.size());
    while (i < len) {
        char32_t cp {};
        int L = 1;
        if (!utf8DecodeAt(s, i, cp, L)) {
            ++i;
            ++n;
            continue;
        }
        i += L;
        ++n;
    }
    return n;
}

std::string utf8TruncateToChars(std::string const &s, int maxChars) {
    if (maxChars <= 0) {
        return {};
    }
    int n = 0;
    int i = 0;
    int const len = static_cast<int>(s.size());
    while (i < len && n < maxChars) {
        char32_t cp {};
        int L = 1;
        if (!utf8DecodeAt(s, i, cp, L)) {
            ++i;
            ++n;
            continue;
        }
        i += L;
        ++n;
    }
    return s.substr(0, static_cast<std::size_t>(i));
}

std::pair<int, int> orderedSelection(int caret, int anchor) noexcept {
    return {std::min(caret, anchor), std::max(caret, anchor)};
}

std::pair<int, int> TextEditSelection::ordered() const noexcept {
    return orderedSelection(caretByte, anchorByte);
}

namespace {

float lineMinXForCtLine(TextLayout const &layout, std::uint32_t ctLine) {
    float m = std::numeric_limits<float>::infinity();
    for (auto const &pr : layout.runs) {
        if (pr.ctLineIndex == ctLine) {
            m = std::min(m, pr.origin.x);
        }
    }
    return std::isfinite(m) ? m : 0.f;
}

std::vector<TextLayout::PlacedRun const *> runsForLineSorted(TextLayout const &layout,
                                                             std::uint32_t ctLine) {
    std::vector<TextLayout::PlacedRun const *> out;
    for (auto const &pr : layout.runs) {
        if (pr.ctLineIndex == ctLine) {
            out.push_back(&pr);
        }
    }
    std::sort(out.begin(), out.end(), [](auto const *a, auto const *b) { return a->origin.x < b->origin.x; });
    return out;
}

} // namespace

std::vector<LineMetrics> buildLineMetrics(TextLayout const &layout) {
    std::vector<LineMetrics> out;

    if (!layout.lines.empty()) {
        out.reserve(layout.lines.size());
        for (auto const &lr : layout.lines) {
            LineMetrics lm {};
            lm.top = lr.top;
            lm.bottom = lr.bottom;
            lm.baseline = lr.baseline;
            lm.byteStart = lr.byteStart;
            lm.byteEnd = lr.byteEnd;
            lm.ctLineIndex = lr.ctLineIndex;
            float const scanned = lineMinXForCtLine(layout, lr.ctLineIndex);
            lm.lineMinX = std::isfinite(scanned) ? scanned : lr.lineMinX;
            out.push_back(lm);
        }
        return out;
    }

    if (layout.runs.empty()) {
        LineMetrics lm {};
        lm.lineMinX = 0.f;
        out.push_back(lm);
        return out;
    }

    std::uint32_t curLine = layout.runs[0].ctLineIndex;
    std::vector<TextLayout::PlacedRun const *> grp;
    auto flush = [&]() {
        if (grp.empty()) {
            return;
        }
        float baselineY = -std::numeric_limits<float>::infinity();
        float minTop = std::numeric_limits<float>::infinity();
        float maxBot = -std::numeric_limits<float>::infinity();
        float minOriginX = std::numeric_limits<float>::infinity();
        int b0 = std::numeric_limits<int>::max();
        int b1 = 0;
        for (auto const *pr : grp) {
            baselineY = std::max(baselineY, pr->origin.y);
            minTop = std::min(minTop, pr->origin.y - pr->run.ascent);
            maxBot = std::max(maxBot, pr->origin.y + pr->run.descent);
            minOriginX = std::min(minOriginX, pr->origin.x);
            b0 = std::min(b0, static_cast<int>(pr->utf8Begin));
            b1 = std::max(b1, static_cast<int>(pr->utf8End));
        }
        LineMetrics lm {};
        lm.baseline = baselineY;
        lm.top = minTop;
        lm.bottom = maxBot;
        lm.lineMinX = std::isfinite(minOriginX) ? minOriginX : 0.f;
        lm.byteStart = b0;
        lm.byteEnd = b1;
        lm.ctLineIndex = grp.front()->ctLineIndex;
        out.push_back(lm);
        grp.clear();
    };

    for (auto const &pr : layout.runs) {
        if (!grp.empty() && pr.ctLineIndex != curLine) {
            flush();
            curLine = pr.ctLineIndex;
        }
        if (grp.empty()) {
            curLine = pr.ctLineIndex;
        }
        grp.push_back(&pr);
    }
    flush();
    return out;
}

void normalizeLineMetricsForEditing(std::vector<LineMetrics> &lines, int textByteCount) noexcept {
    if (lines.empty()) {
        return;
    }

    int const textEnd = std::max(0, textByteCount);

    for (std::size_t i = 0; i < lines.size();) {
        if (lines[i].byteStart != lines[i].byteEnd) {
            ++i;
            continue;
        }

        std::size_t const runStart = i;
        while (i < lines.size() && lines[i].byteStart == lines[i].byteEnd) {
            ++i;
        }
        std::size_t const runEnd = i;
        int const emptyCount = static_cast<int>(runEnd - runStart);

        int prevEnd = -1;
        for (std::size_t j = runStart; j-- > 0;) {
            if (lines[j].byteStart != lines[j].byteEnd) {
                prevEnd = lines[j].byteEnd;
                break;
            }
        }

        int nextStart = textEnd;
        bool foundNext = false;
        for (std::size_t j = runEnd; j < lines.size(); ++j) {
            if (lines[j].byteStart != lines[j].byteEnd) {
                nextStart = lines[j].byteStart;
                foundNext = true;
                break;
            }
        }

        int const firstAssignable = std::max(0, prevEnd + 1);
        int const lastExclusive = foundNext ? nextStart : textEnd;
        int const capacity = std::max(0, lastExclusive - firstAssignable);

        if (capacity <= 0) {
            continue;
        }

        int const assignCount = std::min(emptyCount, capacity);
        for (int k = 0; k < assignCount; ++k) {
            LineMetrics &line = lines[runStart + static_cast<std::size_t>(k)];
            line.byteStart = firstAssignable + k;
            line.byteEnd = line.byteStart + 1;
        }
    }
}

TextEditLayoutResult makeTextEditLayoutResult(std::shared_ptr<TextLayout const> layout, int textByteCount,
                                              float contentWidth) {
    TextEditLayoutResult result {};
    result.textByteCount = std::max(0, textByteCount);
    result.contentWidth = std::max(0.f, contentWidth);
    result.layout = std::move(layout);
    if (result.layout) {
        result.lines = buildLineMetrics(*result.layout);
        normalizeLineMetricsForEditing(result.lines, result.textByteCount);
    }
    return result;
}

TextEditSelection clampSelection(std::string const &text, TextEditSelection selection) noexcept {
    selection.caretByte = utf8Clamp(text, selection.caretByte);
    selection.anchorByte = utf8Clamp(text, selection.anchorByte);
    return selection;
}

TextEditSelection moveSelectionToByte(std::string const &text, TextEditSelection selection, int byte,
                                      bool extendSelection) noexcept {
    selection.caretByte = utf8Clamp(text, byte);
    if (!extendSelection) {
        selection.anchorByte = selection.caretByte;
    } else {
        selection.anchorByte = utf8Clamp(text, selection.anchorByte);
    }
    return selection;
}

TextEditSelection selectAllSelection(std::string const &text) noexcept {
    return TextEditSelection {
        .caretByte = static_cast<int>(text.size()),
        .anchorByte = 0,
    };
}

TextEditSelection clearSelection(TextEditSelection selection) noexcept {
    selection.anchorByte = selection.caretByte;
    return selection;
}

TextEditSelection moveSelectionByChar(std::string const &text, TextEditSelection selection, int direction,
                                      bool extendSelection) noexcept {
    int const caret = utf8Clamp(text, selection.caretByte);
    int const next = direction > 0 ? utf8NextChar(text, caret) : utf8PrevChar(text, caret);
    return moveSelectionToByte(text, selection, next, extendSelection);
}

TextEditSelection moveSelectionByWord(std::string const &text, TextEditSelection selection, int direction,
                                      bool extendSelection) noexcept {
    int const caret = utf8Clamp(text, selection.caretByte);
    int const next = direction > 0 ? utf8NextWord(text, caret) : utf8PrevWord(text, caret);
    return moveSelectionToByte(text, selection, next, extendSelection);
}

TextEditSelection moveSelectionToLineBoundary(std::string const &text, TextEditSelection selection, bool end,
                                              bool extendSelection) noexcept {
    int const caret = utf8Clamp(text, selection.caretByte);
    int target = end ? static_cast<int>(text.size()) : 0;
    if (end) {
        for (int i = caret; i < static_cast<int>(text.size()); ++i) {
            if (text[static_cast<std::size_t>(i)] == '\n') {
                target = i;
                break;
            }
        }
    } else {
        for (int i = caret - 1; i >= 0; --i) {
            if (text[static_cast<std::size_t>(i)] == '\n') {
                target = utf8Clamp(text, i + 1);
                break;
            }
        }
    }
    return moveSelectionToByte(text, selection, target, extendSelection);
}

TextEditSelection moveSelectionToDocumentBoundary(std::string const &text, TextEditSelection selection, bool end,
                                                  bool extendSelection) noexcept {
    return moveSelectionToByte(text, selection, end ? static_cast<int>(text.size()) : 0, extendSelection);
}

TextEditSelection wordSelectionAtByte(std::string const &text, int byteOffset) noexcept {
    int const n = static_cast<int>(text.size());
    if (n <= 0) {
        return {};
    }

    int byte = utf8Clamp(text, byteOffset);
    if (byte >= n) {
        byte = utf8PrevChar(text, n);
    }

    char32_t cp = 0;
    int len = 1;
    if (!utf8DecodeAt(text, byte, cp, len)) {
        return {.caretByte = byte, .anchorByte = byte};
    }

    auto sameClass = [&](char32_t other) {
        if (isSpaceChar(cp)) {
            return isSpaceChar(other);
        }
        if (isWordChar(cp)) {
            return isWordChar(other);
        }
        return isPunctuationChar(other);
    };

    int start = byte;
    while (start > 0) {
        int const prev = utf8PrevChar(text, start);
        char32_t prevCp = 0;
        int prevLen = 1;
        if (!utf8DecodeAt(text, prev, prevCp, prevLen) || !sameClass(prevCp)) {
            break;
        }
        start = prev;
    }

    int end = utf8NextChar(text, byte);
    while (end < n) {
        char32_t nextCp = 0;
        int nextLen = 1;
        if (!utf8DecodeAt(text, end, nextCp, nextLen) || !sameClass(nextCp)) {
            break;
        }
        end = utf8NextChar(text, end);
    }

    return {.caretByte = end, .anchorByte = start};
}

TextEditMutation insertText(std::string const &text, TextEditSelection const &selection, std::string_view insert,
                            int maxLength) {
    TextEditMutation mutation {};
    mutation.text = text;
    auto const [orderedStart, orderedEnd] = clampSelection(text, selection).ordered();

    std::string inserted(insert);
    if (maxLength > 0) {
        int const before = utf8CountChars(text.substr(0, static_cast<std::size_t>(orderedStart)));
        int const after = utf8CountChars(text.substr(static_cast<std::size_t>(orderedEnd)));
        int remaining = maxLength - (before + after);
        if (remaining < 0) {
            remaining = 0;
        }
        inserted = utf8TruncateToChars(inserted, remaining);
    }

    mutation.coalescableTyping =
        inserted.size() == 1 && shouldCoalesceInsert(text, orderedStart, inserted);
    mutation.text.erase(static_cast<std::size_t>(orderedStart), static_cast<std::size_t>(orderedEnd - orderedStart));
    mutation.text.insert(static_cast<std::size_t>(orderedStart), inserted);
    int const newPos = orderedStart + static_cast<int>(inserted.size());
    mutation.selection = TextEditSelection {
        .caretByte = newPos,
        .anchorByte = newPos,
    };
    mutation.valueChanged = mutation.text != text;
    return mutation;
}

TextEditMutation eraseSelectionOrChar(std::string const &text, TextEditSelection const &selection,
                                      bool forward) noexcept {
    TextEditMutation mutation {};
    mutation.text = text;

    auto const [orderedStart, orderedEnd] = clampSelection(text, selection).ordered();
    if (orderedStart < orderedEnd) {
        mutation.text.erase(static_cast<std::size_t>(orderedStart),
                            static_cast<std::size_t>(orderedEnd - orderedStart));
        mutation.selection = TextEditSelection {
            .caretByte = orderedStart,
            .anchorByte = orderedStart,
        };
        mutation.valueChanged = true;
        return mutation;
    }

    int const caret = utf8Clamp(text, selection.caretByte);
    if (forward) {
        int const size = static_cast<int>(text.size());
        if (caret >= size) {
            mutation.selection = TextEditSelection {.caretByte = caret, .anchorByte = caret};
            return mutation;
        }
        int const next = utf8NextChar(text, caret);
        mutation.text.erase(static_cast<std::size_t>(caret), static_cast<std::size_t>(next - caret));
        mutation.selection = TextEditSelection {.caretByte = caret, .anchorByte = caret};
        mutation.valueChanged = true;
        return mutation;
    }

    if (caret <= 0) {
        mutation.selection = TextEditSelection {.caretByte = 0, .anchorByte = 0};
        return mutation;
    }
    int const prev = utf8PrevChar(text, caret);
    mutation.text.erase(static_cast<std::size_t>(prev), static_cast<std::size_t>(caret - prev));
    mutation.selection = TextEditSelection {.caretByte = prev, .anchorByte = prev};
    mutation.valueChanged = true;
    return mutation;
}

TextEditMutation eraseWord(std::string const &text, TextEditSelection const &selection, bool forward) noexcept {
    TextEditMutation mutation {};
    mutation.text = text;

    auto const [orderedStart, orderedEnd] = clampSelection(text, selection).ordered();
    if (orderedStart < orderedEnd) {
        mutation.text.erase(static_cast<std::size_t>(orderedStart),
                            static_cast<std::size_t>(orderedEnd - orderedStart));
        mutation.selection = TextEditSelection {
            .caretByte = orderedStart,
            .anchorByte = orderedStart,
        };
        mutation.valueChanged = true;
        return mutation;
    }

    int const caret = utf8Clamp(text, selection.caretByte);
    int const edge = forward ? utf8NextWord(text, caret) : utf8PrevWord(text, caret);
    if (edge == caret) {
        mutation.selection = TextEditSelection {.caretByte = caret, .anchorByte = caret};
        return mutation;
    }

    int const start = std::min(caret, edge);
    int const end = std::max(caret, edge);
    mutation.text.erase(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));
    mutation.selection = TextEditSelection {
        .caretByte = start,
        .anchorByte = start,
    };
    mutation.valueChanged = true;
    return mutation;
}

TextEditMutation eraseToLineBoundary(std::string const &text, TextEditSelection const &selection,
                                     bool forward) noexcept {
    TextEditMutation mutation {};
    mutation.text = text;

    auto const [orderedStart, orderedEnd] = clampSelection(text, selection).ordered();
    if (orderedStart < orderedEnd) {
        mutation.text.erase(static_cast<std::size_t>(orderedStart),
                            static_cast<std::size_t>(orderedEnd - orderedStart));
        mutation.selection = TextEditSelection {.caretByte = orderedStart, .anchorByte = orderedStart};
        mutation.valueChanged = true;
        return mutation;
    }

    int const caret = utf8Clamp(text, selection.caretByte);
    int const edge = moveSelectionToLineBoundary(text, TextEditSelection {.caretByte = caret, .anchorByte = caret},
                                                 forward, false)
                         .caretByte;
    if (edge == caret) {
        mutation.selection = TextEditSelection {.caretByte = caret, .anchorByte = caret};
        return mutation;
    }

    int const start = std::min(caret, edge);
    int const end = std::max(caret, edge);
    mutation.text.erase(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start));
    mutation.selection = TextEditSelection {.caretByte = start, .anchorByte = start};
    mutation.valueChanged = true;
    return mutation;
}

TextEditSelection selectCurrentLine(std::string const &text,
                                    TextEditSelection const &selection) noexcept {
    ByteRange const range = lineRangeForSelection(text, selection);
    return TextEditSelection{
        .caretByte = range.end,
        .anchorByte = range.start,
    };
}

TextEditMutation eraseCurrentLine(std::string const &text,
                                  TextEditSelection const &selection) noexcept {
    TextEditMutation mutation {};
    mutation.text = text;
    ByteRange const range = lineRangeForSelection(text, selection);
    if (range.start >= range.end) {
        mutation.selection = TextEditSelection{.caretByte = range.start, .anchorByte = range.start};
        return mutation;
    }

    mutation.text.erase(static_cast<std::size_t>(range.start),
                        static_cast<std::size_t>(range.end - range.start));
    int const caret = std::min(range.start, static_cast<int>(mutation.text.size()));
    mutation.selection = TextEditSelection{.caretByte = caret, .anchorByte = caret};
    mutation.valueChanged = true;
    return mutation;
}

TextEditMutation insertLineAdjacent(std::string const &text,
                                    TextEditSelection const &selection,
                                    bool above,
                                    int maxLength) {
    ByteRange const range = lineRangeForSelection(text, selection);
    int const insertAt = above ? range.start : range.end;
    TextEditSelection insertionPoint{.caretByte = insertAt, .anchorByte = insertAt};
    TextEditMutation mutation = insertText(text, insertionPoint, "\n", maxLength);
    if (!mutation.valueChanged) {
        return mutation;
    }

    int const caret = above ? insertAt : insertAt + 1;
    mutation.selection = TextEditSelection{.caretByte = caret, .anchorByte = caret};
    return mutation;
}

TextEditMutation moveCurrentLine(std::string const &text,
                                 TextEditSelection const &selection,
                                 int direction) noexcept {
    TextEditMutation mutation {};
    mutation.text = text;

    if (text.empty() || direction == 0) {
        mutation.selection = clampSelection(text, selection);
        return mutation;
    }

    ByteRange const range = lineRangeForSelection(text, selection);
    if (range.start >= range.end) {
        mutation.selection = clampSelection(text, selection);
        return mutation;
    }

    if (direction < 0) {
        if (range.start <= 0) {
            mutation.selection = clampSelection(text, selection);
            return mutation;
        }
        int const previousStart = lineStartForByte(text, range.start - 1);
        std::string const moving = text.substr(static_cast<std::size_t>(range.start),
                                               static_cast<std::size_t>(range.end - range.start));
        std::string const previous = text.substr(static_cast<std::size_t>(previousStart),
                                                 static_cast<std::size_t>(range.start - previousStart));
        mutation.text.replace(static_cast<std::size_t>(previousStart),
                              static_cast<std::size_t>(range.end - previousStart),
                              moving + previous);
        mutation.selection = shiftSelection(mutation.text, selection, previousStart - range.start);
        mutation.valueChanged = true;
        return mutation;
    }

    if (range.end >= static_cast<int>(text.size())) {
        mutation.selection = clampSelection(text, selection);
        return mutation;
    }
    int const nextEnd = lineEndIncludingNewline(text, range.end);
    if (nextEnd <= range.end) {
        mutation.selection = clampSelection(text, selection);
        return mutation;
    }
    std::string const moving = text.substr(static_cast<std::size_t>(range.start),
                                           static_cast<std::size_t>(range.end - range.start));
    std::string const next = text.substr(static_cast<std::size_t>(range.end),
                                         static_cast<std::size_t>(nextEnd - range.end));
    mutation.text.replace(static_cast<std::size_t>(range.start),
                          static_cast<std::size_t>(nextEnd - range.start),
                          next + moving);
    mutation.selection = shiftSelection(mutation.text, selection, nextEnd - range.end);
    mutation.valueChanged = true;
    return mutation;
}

TextEditMutation copyCurrentLine(std::string const &text,
                                 TextEditSelection const &selection,
                                 int direction,
                                 int maxLength) {
    TextEditMutation mutation {};
    mutation.text = text;

    ByteRange const range = lineRangeForSelection(text, selection);
    if (range.start >= range.end) {
        mutation.selection = clampSelection(text, selection);
        return mutation;
    }

    std::string const copied = text.substr(static_cast<std::size_t>(range.start),
                                           static_cast<std::size_t>(range.end - range.start));
    int const insertAt = direction < 0 ? range.start : range.end;
    TextEditMutation inserted =
        insertText(text, TextEditSelection{.caretByte = insertAt, .anchorByte = insertAt},
                   copied, maxLength);
    if (!inserted.valueChanged) {
        return inserted;
    }

    if (direction < 0) {
        inserted.selection = clampSelection(inserted.text, selection);
    } else {
        inserted.selection = shiftSelection(inserted.text, selection,
                                            static_cast<int>(copied.size()));
    }
    return inserted;
}

int lineIndexForByte(std::vector<LineMetrics> const &lines, int byteOffset) noexcept {
    if (lines.empty()) {
        return 0;
    }
    int maxB = 0;
    for (auto const &L : lines) {
        maxB = std::max(maxB, L.byteEnd);
    }
    int k = byteOffset;
    if (k < 0) {
        k = 0;
    }
    if (k > maxB) {
        k = maxB;
    }

    // Caret positions that land exactly on a visual line start belong to that line, even if the
    // preceding line's UTF-8 span also "contains" the same byte (for example around newline-only lines).
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        auto const &L = lines[static_cast<std::size_t>(i)];
        if (L.byteStart == k) {
            return i;
        }
    }

    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        auto const &L = lines[static_cast<std::size_t>(i)];
        if (k >= L.byteStart && k < L.byteEnd) {
            return i;
        }
    }
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        auto const &L = lines[static_cast<std::size_t>(i)];
        if (L.byteStart == L.byteEnd && L.byteStart == k) {
            return i;
        }
    }

    // When raw visual line ranges come from shaped text, the caret position at a visual line end can sit
    // exactly on a boundary that is not included in any half-open [byteStart, byteEnd) span. In that case
    // the caret still belongs to the most recent visual line whose start is <= k.
    int best = 0;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        auto const &L = lines[static_cast<std::size_t>(i)];
        if (L.byteStart <= k) {
            best = i;
        } else {
            break;
        }
    }
    return best;
}

int lineIndexForByte(TextEditLayoutResult const &result, int byteOffset) noexcept {
    return lineIndexForByte(result.lines, byteOffset);
}

TextEditLineHit lineHitAtY(TextEditLayoutResult const &result, float layoutY) noexcept {
    if (result.lines.empty()) {
        return {};
    }
    if (result.lines.size() == 1) {
        return {.lineIndex = 0, .clamped = true};
    }
    std::vector<float> baselines;
    baselines.reserve(result.lines.size());
    for (auto const &line : result.lines) {
        baselines.push_back(line.baseline);
    }
    if (result.lines.size() == 2) {
        float const boundary = (baselines[0] + baselines[1]) * 0.5f;
        bool const clamped = layoutY < baselines[0] || layoutY >= baselines[1];
        return {.lineIndex = layoutY < boundary ? 0 : 1, .clamped = clamped};
    }
    float const firstBoundary = (baselines[0] + baselines[1]) * 0.5f;
    if (layoutY < firstBoundary) {
        return {.lineIndex = 0, .clamped = true};
    }
    float const lastBoundary =
        (baselines[baselines.size() - 2] + baselines[baselines.size() - 1]) * 0.5f;
    if (layoutY >= lastBoundary) {
        return {.lineIndex = static_cast<int>(result.lines.size()) - 1, .clamped = true};
    }
    for (int i = 1; i < static_cast<int>(baselines.size()) - 1; ++i) {
        float const prevBoundary =
            (baselines[static_cast<std::size_t>(i - 1)] + baselines[static_cast<std::size_t>(i)]) * 0.5f;
        float const nextBoundary =
            (baselines[static_cast<std::size_t>(i)] + baselines[static_cast<std::size_t>(i + 1)]) * 0.5f;
        if (layoutY >= prevBoundary && layoutY < nextBoundary) {
            return {.lineIndex = i, .clamped = false};
        }
    }
    return {.lineIndex = static_cast<int>(result.lines.size()) - 1, .clamped = false};
}

float caretXForByte(TextLayout const &layout, LineMetrics const &line, int byteOffset) noexcept {
    auto sorted = runsForLineSorted(layout, line.ctLineIndex);
    if (sorted.empty()) {
        return 0.f;
    }

    int b = byteOffset;
    if (b < line.byteStart) {
        return sorted.front()->origin.x;
    }
    if (b > line.byteEnd) {
        auto const *r = sorted.back();
        return r->origin.x + r->run.width;
    }

    for (std::size_t i = 0; i < sorted.size(); ++i) {
        auto const *pr = sorted[i];
        int const rb0 = static_cast<int>(pr->utf8Begin);
        int const rb1 = static_cast<int>(pr->utf8End);
        if (b < rb0) {
            return pr->origin.x;
        }
        if (b >= rb0 && b < rb1) {
            return caretXInRun(*pr, b);
        }
        if (b == rb1) {
            float const endX = pr->origin.x + pr->run.width;
            if (i + 1 < sorted.size() && b == static_cast<int>(sorted[i + 1]->utf8Begin)) {
                return endX;
            }
            return endX;
        }
    }

    auto const *last = sorted.back();
    return last->origin.x + last->run.width;
}

float caretXForByte(TextEditLayoutResult const &result, int byteOffset) noexcept {
    if (!result.layout || result.lines.empty()) {
        return 0.f;
    }
    int const lineIndex = lineIndexForByte(result, byteOffset);
    auto const &line = result.lines[static_cast<std::size_t>(lineIndex)];
    return caretXForByte(*result.layout, line, byteOffset);
}

std::pair<float, float> lineCaretYRangeInLayout(TextLayout const &layout, LineMetrics const &line) noexcept {
    auto maxTypographicLineHeight = [](TextLayout const &L) noexcept -> float {
        float h = 0.f;
        for (auto const &pr : L.runs) {
            h = std::max(h, pr.run.ascent + pr.run.descent);
        }
        return h;
    };
    auto maxAscDescInLayout = [](TextLayout const &L, float &outA, float &outD) noexcept {
        outA = 0.f;
        outD = 0.f;
        for (auto const &pr : L.runs) {
            outA = std::max(outA, pr.run.ascent);
            outD = std::max(outD, pr.run.descent);
        }
    };
    float const fontLineH = maxTypographicLineHeight(layout);
    float maxA = 0.f;
    float maxD = 0.f;
    maxAscDescInLayout(layout, maxA, maxD);

    // Match `CoreTextSystem` line boxes and `drawTextLayout`: for each run on this CT line, extend
    // [min(origin.y - ascent), max(origin.y + descent)]. Do not use a midpoint fallback (that drifted
    // vertically when top≈bottom) and do not rely on LineMetrics top/bottom alone (can disagree with
    // runs after shaping).
    float minTop = std::numeric_limits<float>::infinity();
    float maxBot = -std::numeric_limits<float>::infinity();
    for (auto const &pr : layout.runs) {
        if (pr.ctLineIndex == line.ctLineIndex) {
            minTop = std::min(minTop, pr.origin.y - pr.run.ascent);
            maxBot = std::max(maxBot, pr.origin.y + pr.run.descent);
        }
    }
    if (std::isfinite(minTop) && std::isfinite(maxBot) && maxBot > minTop + 1e-4f) {
        return {minTop, maxBot};
    }
    // No drawable runs on this line (empty line): use aggregated LineRange box when present.
    if (line.bottom > line.top + 1e-3f) {
        float t = line.top;
        float b = line.bottom;
        // Core Text / line boxes can be shorter than the field font's typographic height when the line is
        // empty; extend downward so the caret matches the height used when glyphs are present.
        if (fontLineH > 1e-3f && b - t + 1e-4f < fontLineH) {
            b = t + fontLineH;
        }
        return {t, b};
    }
    // Still need a box (e.g. empty last line): anchor on baseline with max ascent/descent from the layout.
    float const bl = line.baseline;
    if (std::isfinite(bl) && (maxA > 1e-6f || maxD > 1e-6f)) {
        return {bl - maxA, bl + maxD};
    }
    float const y0 = std::isfinite(line.top) ? line.top : 0.f;
    float const fallbackH = fontLineH > 1e-3f ? fontLineH : 16.f;
    return {y0, y0 + fallbackH};
}

std::pair<float, float> lineCaretYRangeInLayout(TextEditLayoutResult const &result, int byteOffset) noexcept {
    if (!result.layout || result.lines.empty()) {
        return {0.f, 0.f};
    }
    int const lineIndex = lineIndexForByte(result, byteOffset);
    auto const &line = result.lines[static_cast<std::size_t>(lineIndex)];
    return lineCaretYRangeInLayout(*result.layout, line);
}

int caretByteAtX(TextLayout const &layout, LineMetrics const &line, float layoutX, std::string const &buf) noexcept {
    auto sorted = runsForLineSorted(layout, line.ctLineIndex);
    if (sorted.empty()) {
        return utf8Clamp(buf, visualLineEndByte(line, buf));
    }

    auto const *rightmost = sorted.back();
    float const rightEdge = rightmost->origin.x + rightmost->run.width;
    if (layoutX < line.lineMinX) {
        return utf8Clamp(buf, line.byteStart);
    }
    if (layoutX >= rightEdge) {
        return utf8Clamp(buf, visualLineEndByte(line, buf));
    }

    for (auto const *pr : sorted) {
        float const x0 = pr->origin.x;
        float const x1 = pr->origin.x + pr->run.width;
        if (layoutX < x0) {
            break;
        }
        if (layoutX >= x0 && layoutX < x1) {
            float const localX = layoutX - x0;
            int raw = byteAtLocalXInRun(*pr, localX, buf);
            raw = utf8Clamp(buf, raw);
            return raw;
        }
        if (layoutX == x1 && pr == rightmost) {
            return utf8Clamp(buf, static_cast<int>(pr->utf8End));
        }
    }

    int const b0 = static_cast<int>(sorted.front()->utf8Begin);
    int const b1 = static_cast<int>(sorted.back()->utf8End);
    return utf8Clamp(buf, std::clamp(line.byteStart, b0, b1));
}

int caretByteAtPoint(TextEditLayoutResult const &result, Point layoutPoint, std::string const &buf) noexcept {
    if (!result.layout || result.lines.empty()) {
        return 0;
    }
    TextEditLineHit const hit = lineHitAtY(result, layoutPoint.y);
    auto const &line = result.lines[static_cast<std::size_t>(hit.lineIndex)];
    return caretByteAtX(*result.layout, line, layoutPoint.x, buf);
}

int caretByteAtViewportPoint(TextEditLayoutResult const &result, Point viewportPoint, Point contentOrigin,
                             Point scrollOffset, std::string const &buf) noexcept {
    return caretByteAtPoint(
        result,
        Point {
            viewportPoint.x - contentOrigin.x + scrollOffset.x,
            viewportPoint.y - contentOrigin.y + scrollOffset.y,
        },
        buf
    );
}

int moveCaretVertically(TextEditLayoutResult const &result, std::string const &buf, int currentByte,
                        int direction) noexcept {
    if (!result.layout || result.lines.empty()) {
        return currentByte;
    }
    int const srcIndex = lineIndexForByte(result, currentByte);
    int const dstIndex = std::clamp(srcIndex + direction, 0, static_cast<int>(result.lines.size()) - 1);
    if (srcIndex == dstIndex) {
        return currentByte;
    }
    auto const &srcLine = result.lines[static_cast<std::size_t>(srcIndex)];
    auto const &dstLine = result.lines[static_cast<std::size_t>(dstIndex)];
    float const x = caretXForByte(*result.layout, srcLine, currentByte);
    return caretByteAtX(*result.layout, dstLine, x, buf);
}

float scrollOffsetXForByte(TextEditLayoutResult const &result, int byteOffset) noexcept {
    return caretXForByte(result, byteOffset);
}

int scrollByteToKeepCaretVisible(TextEditLayoutResult const &result, std::string const &buf, int scrollByte,
                                 int caretByte, float viewportWidth, float marginPx) noexcept {
    if (!result.layout || result.lines.empty()) {
        return scrollByte;
    }
    int cur = utf8Clamp(buf, scrollByte);
    int const caret = utf8Clamp(buf, caretByte);
    for (int iter = 0; iter < 128; ++iter) {
        float const caretX = caretXForByte(result, caret);
        float const relativeX = caretX - scrollOffsetXForByte(result, cur);
        if (relativeX >= marginPx && relativeX <= viewportWidth - marginPx) {
            break;
        }
        if (relativeX < marginPx) {
            if (cur <= 0) {
                break;
            }
            cur = utf8PrevChar(buf, cur);
        } else {
            if (cur >= static_cast<int>(buf.size())) {
                break;
            }
            cur = utf8NextChar(buf, cur);
        }
    }
    return cur;
}

float scrollOffsetYToKeepCaretVisible(TextEditLayoutResult const &result, float scrollY, float viewportHeight,
                                      int caretByte, float marginPx) noexcept {
    if (!result.layout || result.lines.empty()) {
        return scrollY;
    }
    int const lineIndex = lineIndexForByte(result, caretByte);
    auto const &line = result.lines[static_cast<std::size_t>(lineIndex)];
    if (line.top < scrollY + marginPx) {
        return line.top - marginPx;
    }
    if (line.bottom > scrollY + viewportHeight - marginPx) {
        return line.bottom - viewportHeight + marginPx;
    }
    return scrollY;
}

Rect caretRect(TextEditLayoutResult const &result, int byteOffset, float originX, float originY,
               float strokeWidth) noexcept {
    if (!result.layout || result.lines.empty()) {
        return {originX, originY, strokeWidth, 0.f};
    }
    float const x = originX + caretXForByte(result, byteOffset);
    auto const [y0, y1] = lineCaretYRangeInLayout(result, byteOffset);
    return {x - strokeWidth * 0.5f, originY + y0, strokeWidth, std::max(0.f, y1 - y0)};
}

std::vector<Rect> selectionRects(TextEditLayoutResult const &result, TextEditSelection const &selection,
                                 std::string const *text, float originX, float originY, float extraBottomPx) noexcept {
    std::vector<Rect> rects;
    if (!result.layout || result.lines.empty() || !selection.hasSelection()) {
        return rects;
    }

    auto const [s0, s1] = selection.ordered();
    for (auto const &line : result.lines) {
        int const a = std::max(s0, line.byteStart);
        int const b = std::min(s1, line.byteEnd);
        if (a >= b) {
            continue;
        }

        float x0 = caretXForByte(*result.layout, line, a) + originX;
        float x1 = caretXForByte(*result.layout, line, b) + originX;
        if (x0 > x1) {
            std::swap(x0, x1);
        }
        if (text && std::abs(x1 - x0) < 1e-3f && a < b) {
            int const visualEnd = visualLineEndByte(line, *text);
            if (b > visualEnd) {
                x1 = x0 + kTextCaretStrokeWidthPx;
            }
        }
        rects.push_back(Rect {x0, originY + line.top, x1 - x0, (line.bottom - line.top) + extraBottomPx});
    }
    return rects;
}

} // namespace lambda::detail
