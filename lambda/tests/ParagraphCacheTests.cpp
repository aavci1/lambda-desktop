#include <doctest/doctest.h>

#if defined(__APPLE__)

#include "Graphics/CoreTextSystem.hpp"
#include "Graphics/TextSystemPrivate.hpp"

#include <Lambda/Graphics/AttributedString.hpp>
#include <Lambda/Graphics/TextCacheStats.hpp>
#include <Lambda/Graphics/TextLayout.hpp>
#include <Lambda/Graphics/TextLayoutOptions.hpp>

#include <cmath>
#include <cstdlib>
#include <limits>
#include <optional>
#include <string>

using namespace lambda;

namespace {

TextLayout::LineRange const* findLine(TextLayout const& layout, std::uint32_t ctLineIndex) {
    for (auto const& line : layout.lines) {
        if (line.ctLineIndex == ctLineIndex) {
            return &line;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("Paragraph cache: fast path matches slow path (env toggle)") {
    std::string body;
    for (int i = 0; i < 6; ++i) {
        body += std::string(100, static_cast<char>('a' + (i % 26))) + "\n";
    }
    Font f {};
    f.family = ".AppleSystemUIFont";
    f.size = 14.f;
    f.weight = 400.f;
    AttributedString const as = AttributedString::plain(body, f, Colors::black);
    TextLayoutOptions opt {};
    opt.wrapping = TextWrapping::Wrap;

    setenv("LAMBDA_DISABLE_PARAGRAPH_CACHE", "1", 1);
    CoreTextSystem sysSlow;
    auto const slow = sysSlow.layout(as, 420.f, opt);
    unsetenv("LAMBDA_DISABLE_PARAGRAPH_CACHE");

    CoreTextSystem sysFast;
    auto const fast = sysFast.layout(as, 420.f, opt);
    REQUIRE(slow != nullptr);
    REQUIRE(fast != nullptr);
    // Structural parity: same topology and near-identical bounds (per-paragraph frames may differ sub-pixel).
    CHECK(slow->runs.size() == fast->runs.size());
    CHECK(slow->lines.size() == fast->lines.size());
    CHECK(std::fabs(slow->measuredSize.width - fast->measuredSize.width) < 8.f);
    CHECK(std::fabs(slow->measuredSize.height - fast->measuredSize.height) < 8.f);
}

TEST_CASE("Paragraph cache: fast path matches slow path with run backgrounds") {
    std::string body;
    for (int i = 0; i < 6; ++i) {
        body += std::string(80, static_cast<char>('a' + (i % 26))) + "\n";
    }
    Font f {};
    f.family = ".AppleSystemUIFont";
    f.size = 14.f;
    f.weight = 400.f;

    AttributedString as;
    as.utf8 = body;
    std::uint32_t const split = static_cast<std::uint32_t>(body.size() / 2);
    as.runs.push_back({0, split, f, Colors::black, Color::hex(0xFFF59D)});
    as.runs.push_back({split, static_cast<std::uint32_t>(body.size()), f, Colors::black, std::nullopt});

    TextLayoutOptions opt {};
    opt.wrapping = TextWrapping::Wrap;

    setenv("LAMBDA_DISABLE_PARAGRAPH_CACHE", "1", 1);
    CoreTextSystem sysSlow;
    auto const slow = sysSlow.layout(as, 420.f, opt);
    unsetenv("LAMBDA_DISABLE_PARAGRAPH_CACHE");

    CoreTextSystem sysFast;
    auto const fast = sysFast.layout(as, 420.f, opt);
    REQUIRE(slow != nullptr);
    REQUIRE(fast != nullptr);
    CHECK(detail::paragraphCacheLayoutsStructurallyEqual(*slow, *fast));
}

TEST_CASE("Paragraph cache: stats layers exist after layout") {
    CoreTextSystem sys;
    std::string body;
    for (int i = 0; i < 6; ++i) {
        body += std::string(100, 'x') + "\n";
    }
    Font f {};
    f.family = ".AppleSystemUIFont";
    f.size = 14.f;
    AttributedString const as = AttributedString::plain(body, f, Colors::black);
    (void)sys.layout(as, 300.f, TextLayoutOptions {});
    TextCacheStats const st = sys.stats();
    CHECK(st.l2_5_paragraph.misses + st.l2_5_paragraph.hits >= 1);
}

TEST_CASE("Paragraph cache: variant refs survive per-paragraph LRU eviction") {
    std::string body;
    for (int i = 0; i < 6; ++i) {
        body += std::string(100, static_cast<char>('a' + (i % 26))) + "\n";
    }
    Font f {};
    f.family = ".AppleSystemUIFont";
    f.size = 14.f;
    f.weight = 400.f;
    AttributedString const as = AttributedString::plain(body, f, Colors::black);
    TextLayoutOptions opt {};
    opt.wrapping = TextWrapping::Wrap;

    float const wA = 170.f;
    CoreTextSystem sys;
    auto const held = sys.layout(as, wA, opt);
    REQUIRE(held != nullptr);
    auto const snapshot = cloneTextLayout(*held);

    (void)sys.layout(as, 410.f, opt);
    (void)sys.layout(as, 540.f, opt);

    CHECK(detail::paragraphCacheLayoutsStructurallyEqual(*held, *snapshot));
}

TEST_CASE("Paragraph cache: notdefGlyphFilteringMatchesCoreText") {
    Font f {};
    f.family = ".AppleSystemUIFont";
    f.size = 15.f;
    // Emoji + ASCII: Core Text may emit leading `.notdef` (gid 0) in CTRun glyph arrays; storage must filter.
    std::string const txt = "Hello \xF0\x9F\x98\x80 world\n";
    AttributedString const as = AttributedString::plain(txt, f, Colors::black);
    TextLayoutOptions opt {};
    opt.wrapping = TextWrapping::Wrap;
    CoreTextSystem sys;
    auto const ly = sys.layout(as, 400.f, opt);
    REQUIRE(ly != nullptr);
    REQUIRE(ly->measuredSize.width > 0.f);
    for (auto const &pr : ly->runs) {
        for (std::uint16_t g : pr.run.glyphIds) {
            CHECK(g != 0);
        }
    }
}

TEST_CASE("CoreText layout preserves attributed run backgrounds") {
    Font f {};
    f.family = ".AppleSystemUIFont";
    f.size = 15.f;

    AttributedString as;
    as.utf8 = "hello world";
    as.runs.push_back({0, 5, f, Colors::black, Color::hex(0xFFE082)});
    as.runs.push_back({5, 11, f, Colors::black, std::nullopt});

    CoreTextSystem sys;
    auto const ly = sys.layout(as, 400.f, TextLayoutOptions {});
    REQUIRE(ly != nullptr);
    REQUIRE(!ly->runs.empty());

    bool sawBackground = false;
    bool sawPlain = false;
    for (auto const &pr : ly->runs) {
        if (pr.utf8Begin < 5) {
            CHECK(pr.run.backgroundColor.has_value());
            if (pr.run.backgroundColor.has_value()) {
                CHECK(*pr.run.backgroundColor == Color::hex(0xFFE082));
            }
            sawBackground = true;
        } else {
            CHECK(!pr.run.backgroundColor.has_value());
            sawPlain = true;
        }
    }
    CHECK(sawBackground);
    CHECK(sawPlain);
}

TEST_CASE("CoreText boxed layout centers each line independently") {
    Font f {};
    f.family = ".AppleSystemUIFont";
    f.size = 15.f;

    AttributedString as;
    as.utf8 = "tiny\nslightly longer line\nmid\nwidest line of all\n";
    std::uint32_t const split = static_cast<std::uint32_t>(as.utf8.size() / 2);
    as.runs.push_back({0, split, f, Colors::black, std::nullopt});
    as.runs.push_back({split, static_cast<std::uint32_t>(as.utf8.size()), f, Color::hex(0x336699), std::nullopt});

    TextLayoutOptions leadingOpt {};
    leadingOpt.wrapping = TextWrapping::Wrap;

    TextLayoutOptions centeredOpt = leadingOpt;
    centeredOpt.horizontalAlignment = HorizontalAlignment::Center;

    Rect const box {0.f, 0.f, 320.f, 240.f};

    CoreTextSystem sys;
    auto const leading = sys.layout(as, box.width, leadingOpt);
    auto const centered = static_cast<TextSystem&>(sys).layout(as, box, centeredOpt);
    REQUIRE(leading != nullptr);
    REQUIRE(centered != nullptr);
    REQUIRE(leading->lines.size() == centered->lines.size());
    REQUIRE(leading->lines.size() >= 4);

    for (auto const& baseLine : leading->lines) {
        TextLayout::LineRange const* centeredLine = findLine(*centered, baseLine.ctLineIndex);
        REQUIRE(centeredLine != nullptr);

        bool sawRun = false;
        float minL = std::numeric_limits<float>::infinity();
        float maxR = -std::numeric_limits<float>::infinity();
        for (auto const& pr : leading->runs) {
            if (pr.ctLineIndex != baseLine.ctLineIndex) {
                continue;
            }
            sawRun = true;
            minL = std::min(minL, pr.origin.x);
            maxR = std::max(maxR, pr.origin.x + pr.run.width);
        }
        REQUIRE(sawRun);

        float const dx = (box.width - (maxR - minL)) * 0.5f - minL;
        CHECK(centeredLine->lineMinX == doctest::Approx(baseLine.lineMinX + dx).epsilon(0.001f));

        for (auto const& pr : leading->runs) {
            if (pr.ctLineIndex != baseLine.ctLineIndex) {
                continue;
            }

            bool foundMatch = false;
            for (auto const& shifted : centered->runs) {
                if (shifted.ctLineIndex != pr.ctLineIndex || shifted.utf8Begin != pr.utf8Begin ||
                    shifted.utf8End != pr.utf8End) {
                    continue;
                }
                CHECK(shifted.origin.x == doctest::Approx(pr.origin.x + dx).epsilon(0.001f));
                CHECK(shifted.origin.y == doctest::Approx(pr.origin.y).epsilon(0.001f));
                foundMatch = true;
                break;
            }
            CHECK(foundMatch);
        }
    }
}

TEST_CASE("CoreText rejects stale runs on empty attributed strings") {
    Font f {};
    f.family = ".AppleSystemUIFont";
    f.size = 15.f;

    AttributedString as;
    as.utf8 = "";
    as.runs.push_back({0, 5, f, Colors::black, std::nullopt});

    CoreTextSystem sys;
    CHECK_THROWS_AS((void)sys.layout(as, 400.f, TextLayoutOptions {}), std::invalid_argument);
}

TEST_CASE("CoreText tolerates invalid UTF-8 in plain strings") {
    Font f {};
    f.family = ".AppleSystemUIFont";
    f.size = 15.f;

    std::string text = "download ";
    text.push_back(static_cast<char>(0xC3));
    text += " item";

    TextLayoutOptions opt {};
    opt.maxLines = 2;

    CoreTextSystem sys;
    CHECK_NOTHROW((void)sys.measure(text, f, Colors::black, 160.f, opt));
    auto const layout = sys.layout(text, f, Colors::black, 160.f, opt);
    REQUIRE(layout != nullptr);
    CHECK(layout->measuredSize.width >= 0.f);
}

#else

TEST_CASE("Paragraph cache tests skipped on non-Apple builds") { CHECK(true); }

#endif
