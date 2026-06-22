#include <doctest/doctest.h>

#if defined(__APPLE__)

#include "Graphics/CoreTextSystem.hpp"
#include "Graphics/TextSystemPrivate.hpp"

#include <Lambda/Graphics/AttributedString.hpp>
#include <Lambda/Graphics/TextLayout.hpp>
#include <Lambda/Graphics/TextLayoutOptions.hpp>

#include <string>

using namespace lambda;

namespace {

Font defaultFont() {
  Font f{};
  f.family = ".AppleSystemUIFont";
  f.size = 14.f;
  f.weight = 400.f;
  return f;
}

/// Enough UTF-8 bytes and hard line breaks for `paragraphCachePredicate` (≥512 B) after typical edits.
std::string sixParagraphDoc(std::size_t lineLen = 110) {
  std::string body;
  for (int i = 0; i < 6; ++i) {
    body += std::string(lineLen, static_cast<char>('a' + (i % 26))) + "\n";
  }
  return body;
}

TextLayoutOptions wrapOpts() {
  TextLayoutOptions opt{};
  opt.wrapping = TextWrapping::Wrap;
  return opt;
}

void checkIncrementalVsFull(CoreTextSystem& sys, AttributedString const& as, float w) {
  auto const incremental = sys.layout(as, w, wrapOpts());
  REQUIRE(incremental != nullptr);
  auto const full = detail::paragraphCacheFullAssemblyForTest(sys, as, w, wrapOpts());
  REQUIRE(full != nullptr);
  CHECK(detail::paragraphCacheLayoutsStructurallyEqual(*incremental, *full));
}

} // namespace

TEST_CASE("Paragraph cache incremental: single-character insert mid-paragraph") {
  CoreTextSystem sys;
  Font const f = defaultFont();
  std::string doc = sixParagraphDoc();
  AttributedString as = AttributedString::plain(doc, f, Colors::black);
  float const w = 420.f;
  (void)sys.layout(as, w, wrapOpts());
  doc.insert(350, 1, 'Z');
  as = AttributedString::plain(doc, f, Colors::black);
  checkIncrementalVsFull(sys, as, w);
}

TEST_CASE("Paragraph cache incremental: single-character delete") {
  CoreTextSystem sys;
  Font const f = defaultFont();
  std::string doc = sixParagraphDoc();
  AttributedString as = AttributedString::plain(doc, f, Colors::black);
  float const w = 420.f;
  (void)sys.layout(as, w, wrapOpts());
  doc.erase(350, 1);
  as = AttributedString::plain(doc, f, Colors::black);
  checkIncrementalVsFull(sys, as, w);
}

// Topology-changing edits (new hard breaks, leading insert) can diverge between `tryIncrementalSplit`'s
// rebuilt paragraph list and `splitIntoParagraphs` — reference layout uses the latter. Covered by
// `LAMBDA_ENABLE_PARAGRAPH_CACHE_PARALLEL_ASSERT` once lists match.

TEST_CASE("Paragraph cache incremental: delete newline merges paragraphs") {
  CoreTextSystem sys;
  Font const f = defaultFont();
  std::string doc = sixParagraphDoc();
  AttributedString as = AttributedString::plain(doc, f, Colors::black);
  float const w = 420.f;
  (void)sys.layout(as, w, wrapOpts());
  doc.erase(100, 1); // first newline between para 0 and 1
  as = AttributedString::plain(doc, f, Colors::black);
  checkIncrementalVsFull(sys, as, w);
}

TEST_CASE("Paragraph cache incremental: multi-character paste mid-paragraph") {
  CoreTextSystem sys;
  Font const f = defaultFont();
  std::string doc = sixParagraphDoc();
  AttributedString as = AttributedString::plain(doc, f, Colors::black);
  float const w = 420.f;
  (void)sys.layout(as, w, wrapOpts());
  doc.insert(200, "PASTED");
  as = AttributedString::plain(doc, f, Colors::black);
  checkIncrementalVsFull(sys, as, w);
}

TEST_CASE("Paragraph cache incremental: delete whole paragraph line") {
  CoreTextSystem sys;
  Font const f = defaultFont();
  std::string doc = sixParagraphDoc();
  AttributedString as = AttributedString::plain(doc, f, Colors::black);
  float const w = 420.f;
  (void)sys.layout(as, w, wrapOpts());
  doc.erase(101, 101); // second line including trailing \n
  as = AttributedString::plain(doc, f, Colors::black);
  checkIncrementalVsFull(sys, as, w);
}

TEST_CASE("Paragraph cache incremental: delete spanning two paragraphs") {
  CoreTextSystem sys;
  Font const f = defaultFont();
  std::string doc = sixParagraphDoc();
  AttributedString as = AttributedString::plain(doc, f, Colors::black);
  float const w = 420.f;
  (void)sys.layout(as, w, wrapOpts());
  doc.erase(80, 50);
  as = AttributedString::plain(doc, f, Colors::black);
  checkIncrementalVsFull(sys, as, w);
}

TEST_CASE("Paragraph cache incremental: insert at document end") {
  CoreTextSystem sys;
  Font const f = defaultFont();
  std::string doc = sixParagraphDoc();
  AttributedString as = AttributedString::plain(doc, f, Colors::black);
  float const w = 420.f;
  (void)sys.layout(as, w, wrapOpts());
  doc.append("END");
  as = AttributedString::plain(doc, f, Colors::black);
  checkIncrementalVsFull(sys, as, w);
}

TEST_CASE("Paragraph cache incremental: edit immediately before newline") {
  CoreTextSystem sys;
  Font const f = defaultFont();
  std::string doc = sixParagraphDoc();
  AttributedString as = AttributedString::plain(doc, f, Colors::black);
  float const w = 420.f;
  (void)sys.layout(as, w, wrapOpts());
  doc.insert(99, "|");
  as = AttributedString::plain(doc, f, Colors::black);
  checkIncrementalVsFull(sys, as, w);
}

TEST_CASE("Paragraph cache incremental: edit immediately after newline") {
  CoreTextSystem sys;
  Font const f = defaultFont();
  std::string doc = sixParagraphDoc();
  AttributedString as = AttributedString::plain(doc, f, Colors::black);
  float const w = 420.f;
  (void)sys.layout(as, w, wrapOpts());
  doc.insert(101, "|");
  as = AttributedString::plain(doc, f, Colors::black);
  checkIncrementalVsFull(sys, as, w);
}

TEST_CASE("Paragraph cache incremental: no-op second layout") {
  CoreTextSystem sys;
  Font const f = defaultFont();
  std::string const doc = sixParagraphDoc();
  AttributedString as = AttributedString::plain(doc, f, Colors::black);
  float const w = 420.f;
  (void)sys.layout(as, w, wrapOpts());
  checkIncrementalVsFull(sys, as, w);
}

TEST_CASE("Paragraph cache incremental: replace entire document content") {
  CoreTextSystem sys;
  Font const f = defaultFont();
  std::string doc = sixParagraphDoc();
  AttributedString as = AttributedString::plain(doc, f, Colors::black);
  float const w = 420.f;
  (void)sys.layout(as, w, wrapOpts());
  std::string other = sixParagraphDoc(100);
  for (char& c : other) {
    if (c >= 'a' && c <= 'z') {
      c = static_cast<char>('z' - (c - 'a'));
    }
  }
  as = AttributedString::plain(other, f, Colors::black);
  checkIncrementalVsFull(sys, as, w);
}

#else

TEST_CASE("Paragraph cache incremental tests skipped on non-Apple builds") { CHECK(true); }

#endif
