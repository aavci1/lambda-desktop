#import <Foundation/Foundation.h>
#import <CoreText/CoreText.h>
#import <CoreGraphics/CoreGraphics.h>

/* Stack XXH3_state_t / streaming helpers need full state layouts from xxhash.h */
#define XXH_STATIC_LINKING_ONLY
#include "xxhash.h"

#include "Graphics/CoreTextSystem.hpp"
#include "Graphics/TextSystemPrivate.hpp"
#include "Debug/PerfCounters.hpp"

#include <Lambda/Detail/SmallVector.hpp>
#include <Lambda/Graphics/TextLayout.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <list>
#include <utility>
#include <vector>

namespace lambdaui {

struct ContentHash {
  std::uint64_t hi = 0;
  std::uint64_t lo = 0;
  bool operator==(ContentHash const& o) const noexcept { return hi == o.hi && lo == o.lo; }
};

struct ContentHashHasher {
  std::size_t operator()(ContentHash const& h) const noexcept { return static_cast<std::size_t>(h.lo); }
};

/// 128-bit paragraph identity for `ParagraphShapeCache` (must not be mixed with `ContentHash`).
struct ParagraphHash {
  std::uint64_t hi = 0;
  std::uint64_t lo = 0;
  bool operator==(ParagraphHash const& o) const noexcept { return hi == o.hi && lo == o.lo; }
};

struct ParagraphHashHasher {
  std::size_t operator()(ParagraphHash const& h) const noexcept { return static_cast<std::size_t>(h.lo); }
};

struct LayoutMemoKey {
  std::uint64_t hi = 0;
  std::uint64_t lo = 0;
};

/// One paragraph slice for the paragraph shape cache (UTF-8 byte offsets into the source buffer).
struct Paragraph {
  std::uint32_t byteStart = 0;
  std::uint32_t byteEnd = 0;
  std::uint32_t runStart = 0;
  std::uint32_t runEnd = 0;
  ParagraphHash hash{};
};

struct ParagraphStyleKey {
  std::uint8_t wrap = 0;
  std::uint32_t lhQ8 = 0;
  std::uint32_t lhMulQ8 = 0;
  bool operator==(ParagraphStyleKey const& o) const noexcept = default;
};

struct ParagraphStyleKeyHash {
  std::size_t operator()(ParagraphStyleKey const& k) const noexcept {
    std::size_t h = k.wrap;
    h = h * 31 + k.lhQ8;
    h = h * 31 + k.lhMulQ8;
    return h;
  }
};

struct RunAttrKey {
  std::uint32_t fontId = 0;
  std::uint32_t sizeQ8 = 0;
  std::uint32_t rgba = 0;
  std::uint32_t backgroundRgba = 0;
  bool operator==(RunAttrKey const& o) const noexcept = default;
};

struct RunAttrKeyHash {
  std::size_t operator()(RunAttrKey const& k) const noexcept {
    std::size_t h = k.fontId;
    h = h * 31 + k.sizeQ8;
    h = h * 31 + k.rgba;
    h = h * 31 + k.backgroundRgba;
    return h;
  }
};

namespace {

constexpr char const* kDefaultFontFamily = ".AppleSystemUIFont";
constexpr float kDefaultFontSize = 14.f;
constexpr float kDefaultFontWeight = 400.f;
constexpr float kPadPx = 1.f;
NSString* const kLambdaBackgroundColorAttributeName = @"LambdaBackgroundColorRGBA";

constexpr std::size_t kMinFastPathBytes = 512u;
constexpr std::uint32_t kMinHardLineBreaks = 4u;

template<typename T>
struct CFReleaser {
  void operator()(T ref) const noexcept {
    if (ref) {
      CFRelease(ref);
    }
  }
};

template<>
struct CFReleaser<CGColorRef> {
  void operator()(CGColorRef ref) const noexcept {
    if (ref) {
      CGColorRelease(ref);
    }
  }
};

template<>
struct CFReleaser<CGColorSpaceRef> {
  void operator()(CGColorSpaceRef ref) const noexcept {
    if (ref) {
      CGColorSpaceRelease(ref);
    }
  }
};

template<>
struct CFReleaser<CGContextRef> {
  void operator()(CGContextRef ref) const noexcept {
    if (ref) {
      CGContextRelease(ref);
    }
  }
};

template<typename T>
class CfPtr {
public:
  CfPtr() noexcept = default;
  explicit CfPtr(T ref) noexcept : ref_(ref) {}
  ~CfPtr() noexcept { CFReleaser<T>{}(ref_); }

  CfPtr(CfPtr const&) = delete;
  CfPtr& operator=(CfPtr const&) = delete;

  CfPtr(CfPtr&& o) noexcept : ref_(o.ref_) { o.ref_ = nullptr; }

  CfPtr& operator=(CfPtr&& o) noexcept {
    if (this != &o) {
      CFReleaser<T>{}(ref_);
      ref_ = o.ref_;
      o.ref_ = nullptr;
    }
    return *this;
  }

  T get() const noexcept { return ref_; }

  T release() noexcept {
    T r = ref_;
    ref_ = nullptr;
    return r;
  }

  operator bool() const noexcept { return ref_ != nullptr; }

private:
  T ref_ = nullptr;
};

template<typename Key, typename Value, typename Hash = std::hash<Key>, typename Eq = std::equal_to<Key>>
class LruCache {
public:
  LruCache(std::size_t capacity, void (*release)(Value)) noexcept
      : capacity_(capacity), release_(release) {}

  ~LruCache() { clear(); }

  LruCache(LruCache const&) = delete;
  LruCache& operator=(LruCache const&) = delete;

  Value* find(Key const& k) {
    auto it = map_.find(k);
    if (it == map_.end()) {
      return nullptr;
    }
    order_.splice(order_.begin(), order_, it->second);
    return &it->second->second;
  }

  Value& insert(Key k, Value v) {
    while (map_.size() >= capacity_) {
      auto& last = order_.back();
      release_(last.second);
      map_.erase(last.first);
      order_.pop_back();
      ++evictions_;
    }
    order_.push_front({std::move(k), std::move(v)});
    map_[order_.front().first] = order_.begin();
    return order_.front().second;
  }

  void clear() {
    for (auto& kv : order_) {
      release_(kv.second);
    }
    order_.clear();
    map_.clear();
  }

  std::size_t evictionsConsumed() noexcept {
    std::size_t const e = evictions_;
    evictions_ = 0;
    return e;
  }

private:
  std::list<std::pair<Key, Value>> order_;
  std::unordered_map<Key, typename std::list<std::pair<Key, Value>>::iterator, Hash, Eq> map_;
  std::size_t capacity_ = 0;
  void (*release_)(Value) = nullptr;
  std::size_t evictions_ = 0;
};

template<typename T>
static void releaseRef(T ref) noexcept {
  CFReleaser<T>{}(ref);
}

static bool paragraphCacheDisabledByEnv() noexcept {
  char const* const s = std::getenv("LAMBDA_DISABLE_PARAGRAPH_CACHE");
  return s != nullptr && s[0] == '1' && s[1] == '\0';
}

struct FontKey {
  std::string family;
  float weight = 400.f;
  bool italic = false;
  bool operator==(FontKey const& o) const noexcept {
    return family == o.family && weight == o.weight && italic == o.italic;
  }
};

struct FontKeyView {
  std::string_view family;
  float weight = 400.f;
  bool italic = false;
};

struct FontKeyHash {
  using is_transparent = void;

  static std::size_t hashFields(std::string_view fam, float w, bool it) noexcept {
    std::size_t h = std::hash<std::string_view>{}(fam);
    h ^= std::hash<float>{}(w) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<bool>{}(it) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }

  std::size_t operator()(FontKey const& k) const noexcept {
    return hashFields(k.family, k.weight, k.italic);
  }
  std::size_t operator()(FontKeyView const& k) const noexcept {
    return hashFields(k.family, k.weight, k.italic);
  }
};

struct FontKeyEq {
  using is_transparent = void;
  bool operator()(FontKey const& a, FontKey const& b) const noexcept {
    return a.family == b.family && a.weight == b.weight && a.italic == b.italic;
  }
  bool operator()(FontKey const& a, FontKeyView const& b) const noexcept {
    return std::string_view(a.family) == b.family && a.weight == b.weight && a.italic == b.italic;
  }
  bool operator()(FontKeyView const& a, FontKey const& b) const noexcept {
    return a.family == std::string_view(b.family) && a.weight == b.weight && a.italic == b.italic;
  }
};

// Core Text weight trait is roughly in [-1, 1]; regular ≈ 0, bold ≈ 0.4.
CGFloat ctWeightTraitFromCss(float w) {
  if (w <= 0.f) {
    return 0.0;
  }
  if (w < 450.f) {
    return 0.0;
  }
  if (w < 600.f) {
    return 0.23;
  }
  return 0.4;
}

Color colorFromCGColor(CGColorRef cg) {
  if (!cg) {
    return Colors::black;
  }
  const size_t n = CGColorGetNumberOfComponents(cg);
  const CGFloat* c = CGColorGetComponents(cg);
  if (n >= 4) {
    return Color{static_cast<float>(c[0]), static_cast<float>(c[1]), static_cast<float>(c[2]),
                 static_cast<float>(c[3])};
  }
  if (n >= 2) {
    return Color{static_cast<float>(c[0]), static_cast<float>(c[0]), static_cast<float>(c[0]),
                 static_cast<float>(c[1])};
  }
  return Colors::black;
}

void validateRuns(AttributedString const& text) {
  if (text.utf8.empty()) {
    if (!text.runs.empty()) {
      throw std::invalid_argument("AttributedString: empty utf8 must not carry runs");
    }
    return;
  }
  if (text.runs.empty()) {
    throw std::invalid_argument("AttributedString: runs must cover the string (empty runs)");
  }
  std::uint32_t const n = static_cast<std::uint32_t>(text.utf8.size());
  if (text.runs[0].start != 0 || text.runs.back().end != n) {
    throw std::invalid_argument("AttributedString: runs must cover full utf8 range");
  }
  for (std::size_t i = 0; i < text.runs.size(); ++i) {
    auto const& r = text.runs[i];
    if (r.start >= r.end || r.end > n) {
      throw std::invalid_argument("AttributedString: invalid run range");
    }
    if (i + 1 < text.runs.size() && r.end != text.runs[i + 1].start) {
      throw std::invalid_argument("AttributedString: runs must be contiguous");
    }
  }
}

struct ResolvedStyle {
  Font font;
  Color color;
  std::optional<Color> backgroundColor;
};

Font resolveFont(Font const& base, Font const& run) {
  Font a = run;
  if (a.family.empty()) {
    a.family = base.family;
  }
  if (a.size <= 0.f) {
    a.size = base.size;
  }
  if (a.weight <= 0.f) {
    a.weight = base.weight;
  }
  return a;
}

Font baseDefaultsFont() {
  Font b;
  b.family = kDefaultFontFamily;
  b.size = kDefaultFontSize;
  b.weight = kDefaultFontWeight;
  b.italic = false;
  return b;
}

template<typename Out>
void accumulateInheritance(Out& out, AttributedString const& text) {
  out.clear();
  Font inherited = baseDefaultsFont();
  for (std::size_t i = 0; i < text.runs.size(); ++i) {
    AttributedRun const& run = text.runs[i];
    Font const& rf = run.font;
    ResolvedStyle& rs = out.emplace_back();
    rs.font = resolveFont(inherited, rf);
    rs.color = run.color;
    rs.backgroundColor = run.backgroundColor;
    if (!rf.family.empty()) {
      inherited.family = rf.family;
    }
    if (rf.size > 0.f) {
      inherited.size = rf.size;
    }
    if (rf.weight > 0.f) {
      inherited.weight = rf.weight;
    }
    inherited.italic = rf.italic;
  }
}

CTFontRef createCTFont(Font const& attr) {
  NSString* fam = [NSString stringWithUTF8String:attr.family.c_str()];
  // Empty string is a valid NSString (not nil) but must not be passed as kCTFontFamilyNameAttribute.
  if (!fam || fam.length == 0) {
    fam = @".AppleSystemUIFont";
  }
  CGFloat const wTrait = ctWeightTraitFromCss(attr.weight);
  NSDictionary* traits = @{
    (id)kCTFontSymbolicTrait : @(attr.italic ? kCTFontItalicTrait : 0),
    (id)kCTFontWeightTrait : @(wTrait),
  };
  NSDictionary* descDict = @{
    (id)kCTFontFamilyNameAttribute : fam,
    (id)kCTFontTraitsAttribute : traits,
  };
  CfPtr<CTFontDescriptorRef> fd(CTFontDescriptorCreateWithAttributes((__bridge CFDictionaryRef)descDict));
  CTFontRef font = CTFontCreateWithFontDescriptor(fd.get(), static_cast<CGFloat>(attr.size), nullptr);
  return font;
}

/// `AttributedString` run ranges are UTF-8 byte offsets; `NSMutableAttributedString` uses UTF-16 indices.
static size_t utf8Advance(char const* s, std::size_t len, std::size_t i, std::uint32_t* outCp) {
  if (i >= len) {
    return 0;
  }
  auto const u = static_cast<unsigned char>(s[i]);
  if (u < 0x80) {
    *outCp = u;
    return 1;
  }
  if (i + 1 < len && (u & 0xE0) == 0xC0) {
    auto const u1 = static_cast<unsigned char>(s[i + 1]);
    if ((u1 & 0xC0) != 0x80) {
      return 0;
    }
    *outCp = (static_cast<std::uint32_t>(u & 0x1F) << 6) | (u1 & 0x3F);
    return 2;
  }
  if (i + 2 < len && (u & 0xF0) == 0xE0) {
    auto const u1 = static_cast<unsigned char>(s[i + 1]);
    auto const u2 = static_cast<unsigned char>(s[i + 2]);
    if ((u1 & 0xC0) != 0x80 || (u2 & 0xC0) != 0x80) {
      return 0;
    }
    *outCp = (static_cast<std::uint32_t>(u & 0x0F) << 12) | (static_cast<std::uint32_t>(u1 & 0x3F) << 6) |
             (u2 & 0x3F);
    return 3;
  }
  if (i + 3 < len && (u & 0xF8) == 0xF0) {
    auto const u1 = static_cast<unsigned char>(s[i + 1]);
    auto const u2 = static_cast<unsigned char>(s[i + 2]);
    auto const u3 = static_cast<unsigned char>(s[i + 3]);
    if ((u1 & 0xC0) != 0x80 || (u2 & 0xC0) != 0x80 || (u3 & 0xC0) != 0x80) {
      return 0;
    }
    *outCp = (static_cast<std::uint32_t>(u & 0x07) << 18) | (static_cast<std::uint32_t>(u1 & 0x3F) << 12) |
             (static_cast<std::uint32_t>(u2 & 0x3F) << 6) | (u3 & 0x3F);
    return 4;
  }
  return 0;
}

static NSUInteger utf16UnitsForCodepoint(std::uint32_t cp) {
  return cp > 0xFFFF ? 2u : 1u;
}

static bool validUtf8Scalar(std::uint32_t cp, std::size_t byteLength) {
  if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
    return false;
  }
  if (byteLength == 1) {
    return cp <= 0x7F;
  }
  if (byteLength == 2) {
    return cp >= 0x80 && cp <= 0x7FF;
  }
  if (byteLength == 3) {
    return cp >= 0x800 && cp <= 0xFFFF;
  }
  if (byteLength == 4) {
    return cp >= 0x10000;
  }
  return false;
}

static size_t utf8AdvanceLossy(char const* s, std::size_t len, std::size_t i, std::uint32_t* outCp) {
  std::uint32_t cp = 0;
  std::size_t const adv = utf8Advance(s, len, i, &cp);
  if (adv > 0 && validUtf8Scalar(cp, adv)) {
    *outCp = cp;
    return adv;
  }
  *outCp = 0xFFFD;
  return i < len ? 1 : 0;
}

static void appendUtf8Codepoint(std::string& out, std::uint32_t cp) {
  if (cp <= 0x7F) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

static std::string sanitizeUtf8Lossy(std::string_view utf8) {
  std::string out;
  out.reserve(utf8.size());
  std::size_t i = 0;
  while (i < utf8.size()) {
    std::uint32_t cp = 0;
    std::size_t const adv = utf8AdvanceLossy(utf8.data(), utf8.size(), i, &cp);
    if (adv == 0) {
      break;
    }
    appendUtf8Codepoint(out, cp);
    i += adv;
  }
  return out;
}

/// Maps half-open UTF-8 byte range `[bStart, bEnd)` to an `NSRange` in UTF-16 (Foundation string units).
static NSRange utf8ByteRangeToNSRange(char const* utf8, std::size_t utf8Len, std::uint32_t bStart,
                                      std::uint32_t bEnd) {
  if (bEnd > utf8Len) {
    bEnd = static_cast<std::uint32_t>(utf8Len);
  }
  if (bStart > bEnd) {
    bStart = bEnd;
  }
  NSUInteger u16Start = 0;
  std::size_t i = 0;
  while (i < utf8Len && i < bStart) {
    std::uint32_t cp = 0;
    std::size_t const adv = utf8Advance(utf8, utf8Len, i, &cp);
    if (adv == 0) {
      break;
    }
    u16Start += utf16UnitsForCodepoint(cp);
    i += adv;
  }
  NSUInteger u16End = u16Start;
  while (i < utf8Len && i < bEnd) {
    std::uint32_t cp = 0;
    std::size_t const adv = utf8Advance(utf8, utf8Len, i, &cp);
    if (adv == 0) {
      break;
    }
    u16End += utf16UnitsForCodepoint(cp);
    i += adv;
  }
  return NSMakeRange(u16Start, u16End - u16Start);
}

static NSRange utf8ByteRangeToNSRangeLossy(char const* utf8, std::size_t utf8Len, std::uint32_t bStart,
                                           std::uint32_t bEnd) {
  if (bEnd > utf8Len) {
    bEnd = static_cast<std::uint32_t>(utf8Len);
  }
  if (bStart > bEnd) {
    bStart = bEnd;
  }
  NSUInteger u16Start = 0;
  std::size_t i = 0;
  while (i < utf8Len && i < bStart) {
    std::uint32_t cp = 0;
    std::size_t const adv = utf8AdvanceLossy(utf8, utf8Len, i, &cp);
    if (adv == 0) {
      break;
    }
    u16Start += utf16UnitsForCodepoint(cp);
    i += adv;
  }
  NSUInteger u16End = u16Start;
  while (i < utf8Len && i < bEnd) {
    std::uint32_t cp = 0;
    std::size_t const adv = utf8AdvanceLossy(utf8, utf8Len, i, &cp);
    if (adv == 0) {
      break;
    }
    u16End += utf16UnitsForCodepoint(cp);
    i += adv;
  }
  return NSMakeRange(u16Start, u16End - u16Start);
}

static NSRange clampNSRange(NSRange range, NSUInteger length) {
  if (range.location > length) {
    range.location = length;
  }
  NSUInteger const available = length - range.location;
  if (range.length > available) {
    range.length = available;
  }
  return range;
}

/// Builds a lookup table where entry `i` is the UTF-8 byte length of the UTF-16 prefix `[0, i)`.
static std::vector<std::uint32_t> buildUtf16ToUtf8PrefixMap(std::string_view utf8) {
  std::vector<std::uint32_t> prefixMap;
  prefixMap.reserve(utf8.size() + 1);
  prefixMap.push_back(0);

  std::size_t byteIndex = 0;
  while (byteIndex < utf8.size()) {
    std::uint32_t cp = 0;
    std::size_t const adv = utf8Advance(utf8.data(), utf8.size(), byteIndex, &cp);
    if (adv == 0) {
      break;
    }
    byteIndex += adv;
    std::size_t const u16Units = utf16UnitsForCodepoint(cp);
    for (std::size_t i = 0; i < u16Units; ++i) {
      prefixMap.push_back(static_cast<std::uint32_t>(byteIndex));
    }
  }

  return prefixMap;
}

/// Maps a UTF-16 `NSRange` to half-open UTF-8 byte offsets `[outBegin, outEnd)`.
static void utf16RangeToUtf8ByteRange(std::span<std::uint32_t const> utf16ToUtf8PrefixMap, NSRange r16,
                                      std::uint32_t& outBegin, std::uint32_t& outEnd) {
  if (utf16ToUtf8PrefixMap.empty()) {
    outBegin = 0;
    outEnd = 0;
    return;
  }
  NSUInteger const len = utf16ToUtf8PrefixMap.size() - 1;
  NSUInteger u16Start = r16.location;
  NSUInteger u16End = NSMaxRange(r16);
  if (u16Start > len) {
    u16Start = len;
  }
  if (u16End > len) {
    u16End = len;
  }
  outBegin = utf16ToUtf8PrefixMap[u16Start];
  outEnd = utf16ToUtf8PrefixMap[u16End];
}

// --- ContentHash (XXH3 128-bit) -------------------------------------------------

static std::uint32_t rgba8Pack(Color const& c) {
  auto ch = [](float x) -> std::uint32_t {
    return static_cast<std::uint32_t>(std::clamp(x, 0.f, 1.f) * 255.f + 0.5f);
  };
  return (ch(c.r) << 24) | (ch(c.g) << 16) | (ch(c.b) << 8) | ch(c.a);
}

static Color rgba8Unpack(std::uint32_t rgba) {
  Color color = Color::rgb(static_cast<std::uint8_t>((rgba >> 24) & 0xFFu),
                           static_cast<std::uint8_t>((rgba >> 16) & 0xFFu),
                           static_cast<std::uint8_t>((rgba >> 8) & 0xFFu));
  color.a = static_cast<float>(rgba & 0xFFu) * (1.f / 255.f);
  return color;
}

// computeContentHash / computeContentHashPlain are defined after CoreTextSystem::Impl (needs findFontId).

static std::uint32_t quantizeWidth(float maxWidth) {
  if (maxWidth <= 0.f) {
    return 0;
  }
  return static_cast<std::uint32_t>(std::lround(maxWidth * 2.f));
}

static std::uint16_t quantizeFirstBaseline(float v) {
  return static_cast<std::uint16_t>(std::clamp(std::lround(v * 8.f), 0L, 65535L));
}

static ParagraphStyleKey paragraphKeyFor(TextLayoutOptions const& opt) {
  ParagraphStyleKey k{};
  k.wrap = static_cast<std::uint8_t>(opt.wrapping);
  if (opt.lineHeightMultiple > 0.f) {
    k.lhMulQ8 = static_cast<std::uint32_t>(std::lround(opt.lineHeightMultiple * 256.f));
  } else if (opt.lineHeight > 0.f) {
    k.lhQ8 = static_cast<std::uint32_t>(std::lround(opt.lineHeight * 4.f));
  }
  return k;
}

/// Same paragraph metrics as the previous `applyParagraphStyleToMutable` (creates a new ref each time).
static CTParagraphStyleRef createParagraphStyleRef(TextLayoutOptions const& options) {
  CTLineBreakMode lineBreak = kCTLineBreakByWordWrapping;
  if (options.wrapping == TextWrapping::WrapAnywhere) {
    lineBreak = kCTLineBreakByCharWrapping;
  }

  CTParagraphStyleSetting settings[5];
  std::size_t n = 0;
  settings[n].spec = kCTParagraphStyleSpecifierLineBreakMode;
  settings[n].valueSize = sizeof(lineBreak);
  settings[n].value = &lineBreak;
  ++n;

  CGFloat minLh = 0;
  CGFloat maxLh = 0;
  CGFloat lineMultiple = 0;
  if (options.lineHeightMultiple > 0.f) {
    lineMultiple = static_cast<CGFloat>(options.lineHeightMultiple);
    settings[n].spec = kCTParagraphStyleSpecifierLineHeightMultiple;
    settings[n].valueSize = sizeof(lineMultiple);
    settings[n].value = &lineMultiple;
    ++n;
  } else if (options.lineHeight > 0.f) {
    minLh = static_cast<CGFloat>(options.lineHeight);
    maxLh = static_cast<CGFloat>(options.lineHeight);
    settings[n].spec = kCTParagraphStyleSpecifierMinimumLineHeight;
    settings[n].valueSize = sizeof(minLh);
    settings[n].value = &minLh;
    ++n;
    settings[n].spec = kCTParagraphStyleSpecifierMaximumLineHeight;
    settings[n].valueSize = sizeof(maxLh);
    settings[n].value = &maxLh;
    ++n;
  }

  return CTParagraphStyleCreate(settings, static_cast<CFIndex>(n));
}

/// Resolves `fontId` / style from a Core Text run’s font (same mapping as the previous single-line shaper).
static void styleFromCTFont(CTFontRef ctFont, CGColorRef cgColor, CoreTextSystem& sys, std::uint32_t& outFontId,
                            float& outFontSize, Color& outColor) {
  outColor = colorFromCGColor(cgColor);
  outFontSize = static_cast<float>(CTFontGetSize(ctFont));
  CfPtr<CTFontDescriptorRef> fd(CTFontCopyFontDescriptor(ctFont));
  CfPtr<CFDictionaryRef> traitDict(
      static_cast<CFDictionaryRef>(CTFontDescriptorCopyAttribute(fd.get(), kCTFontTraitsAttribute)));
  bool italic = false;
  CGFloat weightTrait = 0.0;
  if (traitDict) {
    NSDictionary* traits = (__bridge NSDictionary*)traitDict.get();
    NSNumber* sym = traits[(id)kCTFontSymbolicTrait];
    if (sym) {
      italic = ([sym unsignedIntValue] & kCTFontItalicTrait) != 0;
    }
    NSNumber* wt = traits[(id)kCTFontWeightTrait];
    if (wt) {
      weightTrait = [wt floatValue];
    }
  }
  CfPtr<CFStringRef> famRef(CTFontCopyFamilyName(ctFont));
  NSString* famNs = (__bridge NSString*)famRef.get();
  std::string fam = famNs ? [famNs UTF8String] : std::string(kDefaultFontFamily);
  float cssWeight = 400.f;
  if (weightTrait >= 0.35) {
    cssWeight = 700.f;
  } else if (weightTrait >= 0.15) {
    cssWeight = 600.f;
  }
  outFontId = sys.resolveFontId(fam, cssWeight, italic);
}

struct RunEmitInfo {
  std::uint32_t drawableCount = 0;
  std::uint32_t firstKeptIdx = 0;
  bool hasNotdef = false;
};

static std::size_t countDrawableGlyphs(std::span<std::uint32_t const> gids) noexcept {
  std::size_t total = 0;
  for (std::uint32_t g : gids) {
    if (g != 0) {
      ++total;
    }
  }
  return total;
}

static CGGlyph const* runGlyphs(CTRunRef run, CFIndex glyphCount, std::vector<CGGlyph>& scratch) {
  if (glyphCount <= 0) {
    return nullptr;
  }
  if (CGGlyph const* ptr = CTRunGetGlyphsPtr(run)) {
    return ptr;
  }
  scratch.resize(static_cast<std::size_t>(glyphCount));
  CTRunGetGlyphs(run, CFRangeMake(0, 0), scratch.data());
  return scratch.data();
}

static std::span<std::uint32_t const> runGlyphIds(CTRunRef run, CFIndex glyphCount, std::vector<CGGlyph>& glyphScratch,
                                                  std::vector<std::uint32_t>& idScratch) {
  CGGlyph const* gids = runGlyphs(run, glyphCount, glyphScratch);
  if (!gids) {
    return {};
  }
  idScratch.resize(static_cast<std::size_t>(glyphCount));
  for (CFIndex i = 0; i < glyphCount; ++i) {
    idScratch[static_cast<std::size_t>(i)] = static_cast<std::uint32_t>(gids[i]);
  }
  return std::span<std::uint32_t const>(idScratch.data(), idScratch.size());
}

static CGPoint const* runPositions(CTRunRef run, CFIndex glyphCount, std::vector<CGPoint>& scratch) {
  if (glyphCount <= 0) {
    return nullptr;
  }
  if (CGPoint const* ptr = CTRunGetPositionsPtr(run)) {
    return ptr;
  }
  scratch.resize(static_cast<std::size_t>(glyphCount));
  CTRunGetPositions(run, CFRangeMake(0, 0), scratch.data());
  return scratch.data();
}

/// Count drawable (non-`.notdef`) glyphs for arena sizing while caching the first kept index for emit.
static RunEmitInfo analyzeRunGlyphs(CTRunRef run, std::vector<CGGlyph>& glyphScratch,
                                    std::vector<std::uint32_t>& glyphIdScratch) {
  RunEmitInfo info;
  CFIndex const glyphCount = CTRunGetGlyphCount(run);
  if (glyphCount <= 0) {
    return info;
  }
  std::span<std::uint32_t const> const gidSpan = runGlyphIds(run, glyphCount, glyphScratch, glyphIdScratch);
  if (gidSpan.empty()) {
    return info;
  }
  info.hasNotdef = detail::hasNotdefGlyph(gidSpan);
  if (!info.hasNotdef) {
    info.drawableCount = static_cast<std::uint32_t>(glyphCount);
    return info;
  }
  info.drawableCount = static_cast<std::uint32_t>(countDrawableGlyphs(gidSpan));
  while (info.firstKeptIdx < gidSpan.size() && gidSpan[info.firstKeptIdx] == 0) {
    ++info.firstKeptIdx;
  }
  return info;
}

/// One `CTRun` appended to `TextLayoutStorage` arenas. Glyph positions are relative to the run's
/// baseline-left; `origin` is baseline-left in layout space (top-left origin, Y down).
/// `frameHeight` is the CT frame path height (Quartz, Y up).
static void appendPlacedRunToStorage(CTRunRef run, CGPoint lineOrigin, CGFloat frameHeight, CoreTextSystem& sys,
                                     TextLayout& layout, TextLayoutStorage& storage,
                                     RunEmitInfo const& emitInfo, std::vector<CGGlyph>& glyphScratch,
                                     std::vector<std::uint32_t>& glyphIdScratch,
                                     std::vector<CGPoint>& positionScratch, std::vector<std::size_t>& keptScratch,
                                     std::span<std::uint32_t const> utf16ToUtf8PrefixMap,
                                     CFIndex ctLineIndex, std::size_t& arenaWrite) {
  CFIndex const glyphCount = CTRunGetGlyphCount(run);
  if (glyphCount <= 0 || emitInfo.drawableCount == 0) {
    return;
  }
  NSDictionary* attrs = (__bridge NSDictionary*)CTRunGetAttributes(run);
  CTFontRef ctFont = (__bridge CTFontRef)attrs[(id)kCTFontAttributeName];
  CGColorRef cgColor = (__bridge CGColorRef)attrs[(id)kCTForegroundColorAttributeName];
  NSNumber* bgPacked = attrs[kLambdaBackgroundColorAttributeName];

  std::uint32_t fontId = 0;
  float fontSize = 0.f;
  Color color = Colors::black;
  styleFromCTFont(ctFont, cgColor, sys, fontId, fontSize, color);

  CGPoint const* cpos = runPositions(run, glyphCount, positionScratch);
  std::span<std::uint32_t const> const gidSpan = runGlyphIds(run, glyphCount, glyphScratch, glyphIdScratch);
  if (!cpos || gidSpan.empty()) {
    return;
  }

  std::size_t const n = emitInfo.drawableCount;
  std::size_t fi = emitInfo.firstKeptIdx;
  if (emitInfo.hasNotdef) {
    detail::collectDrawableGlyphIndices(gidSpan, keptScratch);
    if (keptScratch.empty()) {
      return;
    }
    fi = keptScratch[0];
  } else {
    keptScratch.clear();
  }

  CGFloat ascent = 0, descent = 0, leading = 0;
  double const runWidth = CTRunGetTypographicBounds(
      run, CFRangeMake(static_cast<CFIndex>(fi), glyphCount - static_cast<CFIndex>(fi)), &ascent, &descent, &leading);

  std::size_t const gOff = arenaWrite;
  assert(gOff + n <= storage.glyphArena.size() && gOff + n <= storage.positionArena.size());
  arenaWrite += n;

  // LTR: anchor to first drawable glyph (`fi`). For RTL, a different anchor would be needed.
  CGPoint const anchor = cpos[fi];
  if (!emitInfo.hasNotdef) {
    for (std::size_t j = 0; j < n; ++j) {
      storage.glyphArena[gOff + j] = gidSpan[j];
      float const dx = static_cast<float>(cpos[j].x - anchor.x);
      float const dy = -static_cast<float>(cpos[j].y - anchor.y); // Quartz Y up -> canvas Y down
      storage.positionArena[gOff + j] = Point{dx, dy};
    }
  } else {
    for (std::size_t j = 0; j < n; ++j) {
      std::size_t const gi = keptScratch[j];
      storage.glyphArena[gOff + j] = gidSpan[gi];
      float const dx = static_cast<float>(cpos[gi].x - anchor.x);
      float const dy = -static_cast<float>(cpos[gi].y - anchor.y); // Quartz Y up -> canvas Y down
      storage.positionArena[gOff + j] = Point{dx, dy};
    }
  }

  TextLayout::PlacedRun placed{};
  placed.run.fontId = fontId;
  placed.run.fontSize = fontSize;
  placed.run.color = color;
  if (bgPacked != nil) {
    placed.run.backgroundColor = rgba8Unpack(static_cast<std::uint32_t>(bgPacked.unsignedIntValue));
  }
  placed.run.ascent = static_cast<float>(ascent);
  placed.run.descent = static_cast<float>(descent);
  placed.run.width = static_cast<float>(runWidth);
  placed.run.glyphIds = std::span<std::uint32_t const>(storage.glyphArena.data() + gOff, n);
  placed.run.positions = std::span<Point const>(storage.positionArena.data() + gOff, n);

  CGFloat const baselineX = lineOrigin.x + cpos[fi].x;
  CGFloat const baselineY = lineOrigin.y + cpos[fi].y;
  placed.origin.x = static_cast<float>(baselineX);
  placed.origin.y = static_cast<float>(frameHeight - baselineY);

  CFRange const strRange = CTRunGetStringRange(run);
  NSRange const nsr = NSMakeRange(static_cast<NSUInteger>(strRange.location), static_cast<NSUInteger>(strRange.length));
  utf16RangeToUtf8ByteRange(utf16ToUtf8PrefixMap, nsr, placed.utf8Begin, placed.utf8End);
  placed.ctLineIndex = static_cast<std::uint32_t>(ctLineIndex);

  layout.runs.push_back(std::move(placed));
}

} // namespace

// --- Cache entry types (file-local) -------------------------------------------

namespace {

struct MeasureSlot {
  std::uint32_t maxWidthQ1 = 0;
  std::int32_t maxLines = 0;
  Size measuredSize{};
};

struct BoxSlot {
  std::uint32_t boxWQ1 = 0;
  std::uint32_t boxHQ1 = 0;
  /// Exact box dimensions (layout uses these for center/trailing alignment, not only quantized keys).
  float boxW = 0.f;
  float boxH = 0.f;
  std::uint8_t hAlign = 0;
  std::uint8_t vAlign = 0;
  std::uint16_t firstBaselineQ8 = 0;
  std::shared_ptr<TextLayout const> layout;
};

struct LayoutSlot {
  std::uint32_t maxWidthQ1 = 0;
  /// Must match layout requests: `quantizeWidth` alone can collide for different float maxWidth values.
  float maxWidthExact = 0.f;
  std::int32_t maxLines = 0;
  std::shared_ptr<TextLayout const> unboxed;
  lambdaui::detail::SmallVector<BoxSlot, 4> boxes;
};

struct FramesetterEntry {
  CFAttributedStringRef attrString = nullptr;
  CTFramesetterRef framesetter = nullptr;
  lambdaui::detail::SmallVector<MeasureSlot, 4> measures;
  lambdaui::detail::SmallVector<LayoutSlot, 4> layouts;
  lambdaui::detail::SmallVector<std::uint32_t, 4> fontIds;
  std::uint64_t lastTouchFrame = 0;
  std::uint32_t approxBytes = 0;
};

static std::size_t countStoredTextLayouts(FramesetterEntry const& e) {
  std::size_t n = 0;
  for (auto const& ls : e.layouts) {
    if (ls.unboxed) {
      ++n;
    }
    n += ls.boxes.size();
  }
  return n;
}

static std::uint32_t estimateEntryBytes(AttributedString const& text, std::size_t layoutPieces) {
  return static_cast<std::uint32_t>(text.utf8.size() + text.runs.size() * 64 + 256 +
                                    text.utf8.size() * 32 + layoutPieces * sizeof(TextLayout));
}

constexpr std::size_t kMaxVariantsPerParagraph = 2;

struct ParagraphLayoutVariant {
  std::uint32_t maxWidthQ1 = 0;
  std::uint8_t wrap = 0;
  std::uint32_t lhQ8 = 0;
  std::uint32_t lhMulQ8 = 0;
  std::vector<TextLayout::PlacedRun> runs;
  std::vector<TextLayout::LineRange> lines;
  std::vector<std::uint32_t> glyphStorage;
  std::vector<Point> positionStorage;
  float height = 0.f;
  float maxLineWidth = 0.f;
  std::uint32_t approxBytes = 0;
};

struct ShapedParagraph {
  CTTypesetterRef typesetter = nullptr;
  lambdaui::detail::SmallVector<std::uint32_t, 4> fontIds;
  std::uint32_t byteLength = 0;
  CFIndex utf16Length = 0;
  std::uint32_t approxBytes = 0;
  std::uint64_t lastTouchFrame = 0;
  lambdaui::detail::SmallVector<std::shared_ptr<ParagraphLayoutVariant>, 2> variants{};
};

static std::shared_ptr<void> typeErasedVariantRef(std::shared_ptr<ParagraphLayoutVariant> const& v) noexcept {
  return std::static_pointer_cast<void>(v);
}

static std::uint32_t computeVariantApproxBytes(ParagraphLayoutVariant const& v) noexcept {
  return static_cast<std::uint32_t>(
      sizeof(ParagraphLayoutVariant) + v.runs.size() * sizeof(TextLayout::PlacedRun) +
      v.lines.size() * sizeof(TextLayout::LineRange) +
      v.glyphStorage.capacity() * sizeof(std::uint16_t) + v.positionStorage.capacity() * sizeof(Point));
}

static std::uint32_t recomputeShapedParagraphApproxBytes(ShapedParagraph const& sp) noexcept {
  std::uint32_t vsum = 0;
  for (auto const& vp : sp.variants) {
    if (vp) {
      vsum += vp->approxBytes;
    }
  }
  return sp.byteLength + sp.byteLength * 8u + 256u + static_cast<std::uint32_t>(sizeof(ShapedParagraph)) + vsum;
}

class ParagraphShapeCache {
public:
  ShapedParagraph const* find(ParagraphHash const& h, bool suppressStats) {
    std::shared_ptr<ShapedParagraph> const s = findShared(h, suppressStats);
    return s ? s.get() : nullptr;
  }

  std::shared_ptr<ShapedParagraph> findShared(ParagraphHash const& h, bool suppressStats) {
    auto it = map_.find(h);
    if (it == map_.end()) {
      return nullptr;
    }
    it->second->lastTouchFrame = currentFrame_;
    if (!suppressStats) {
      ++stats_.hits;
    }
    return it->second;
  }

  ShapedParagraph const& insert(ParagraphHash const& h, ShapedParagraph&& sp, bool suppressStats) {
    if (!suppressStats) {
      ++stats_.misses;
    }
    auto const oldIt = map_.find(h);
    if (oldIt != map_.end()) {
      totalBytes_ -= static_cast<std::size_t>(oldIt->second->approxBytes);
      releaseEntry(*oldIt->second);
      map_.erase(oldIt);
    }
    sp.approxBytes = recomputeShapedParagraphApproxBytes(sp);
    totalBytes_ += static_cast<std::size_t>(sp.approxBytes);
    sp.lastTouchFrame = currentFrame_;
    auto sptr = std::make_shared<ShapedParagraph>(std::move(sp));
    auto const ins = map_.emplace(h, std::move(sptr));
    stats_.currentBytes = totalBytes_;
    if (totalBytes_ > stats_.peakBytes) {
      stats_.peakBytes = totalBytes_;
    }
    return *ins.first->second;
  }

  void onFrameBegin(std::uint64_t frameIndex) { currentFrame_ = frameIndex; }

  /// Evicts entries not touched this frame (oldest `lastTouchFrame` first) until under budget.
  /// Returns hashes that were removed (for memo invalidation).
  std::vector<ParagraphHash> onFrameEnd(std::size_t byteBudget) {
    std::vector<ParagraphHash> evicted;
    while (totalBytes_ > byteBudget) {
      std::vector<std::pair<std::uint64_t, ParagraphHash>> candidates;
      candidates.reserve(map_.size());
      for (auto const& p : map_) {
        if (p.second->lastTouchFrame >= currentFrame_) {
          continue;
        }
        candidates.push_back({p.second->lastTouchFrame, p.first});
      }
      if (candidates.empty()) {
        break;
      }
      std::sort(candidates.begin(), candidates.end(),
                [](auto const& a, auto const& b) { return a.first < b.first; });
      bool progressed = false;
      for (auto const& c : candidates) {
        if (totalBytes_ <= byteBudget) {
          break;
        }
        auto it = map_.find(c.second);
        if (it == map_.end()) {
          continue;
        }
        if (it->second->lastTouchFrame >= currentFrame_) {
          continue;
        }
        totalBytes_ -= static_cast<std::size_t>(it->second->approxBytes);
        evicted.push_back(it->first);
        releaseEntry(*it->second);
        map_.erase(it);
        ++stats_.evictions;
        progressed = true;
      }
      if (!progressed) {
        break;
      }
    }
    stats_.currentBytes = totalBytes_;
    if (totalBytes_ > stats_.peakBytes) {
      stats_.peakBytes = totalBytes_;
    }
    return evicted;
  }

  void invalidateAll() {
    for (auto& p : map_) {
      releaseEntry(*p.second);
    }
    map_.clear();
    totalBytes_ = 0;
    stats_.currentBytes = 0;
  }

  std::vector<ParagraphHash> invalidateForFontChange(std::span<std::uint32_t const> fontIds) {
    std::unordered_set<std::uint32_t> const want(fontIds.begin(), fontIds.end());
    std::vector<ParagraphHash> toErase;
    for (auto const& p : map_) {
      for (std::uint32_t fid : p.second->fontIds) {
        if (want.count(fid)) {
          toErase.push_back(p.first);
          break;
        }
      }
    }
    std::vector<ParagraphHash> removed;
    for (ParagraphHash const& h : toErase) {
      auto it = map_.find(h);
      if (it != map_.end()) {
        totalBytes_ -= static_cast<std::size_t>(it->second->approxBytes);
        releaseEntry(*it->second);
        map_.erase(it);
        removed.push_back(h);
      }
    }
    stats_.currentBytes = totalBytes_;
    return removed;
  }

  [[nodiscard]] TextCacheStats::LayerStats const& stats() const noexcept { return stats_; }
  [[nodiscard]] TextCacheStats::LayerStats const& variantStats() const noexcept { return variantStats_; }

  void setBudget(std::size_t bytes) noexcept { budgetBytes_ = bytes; }
  [[nodiscard]] std::size_t budget() const noexcept { return budgetBytes_; }

  void adjustParagraphBytes(ParagraphHash const& h, std::uint32_t oldApprox, std::uint32_t newApprox) noexcept {
    auto it = map_.find(h);
    if (it == map_.end()) {
      return;
    }
    totalBytes_ -= static_cast<std::size_t>(oldApprox);
    totalBytes_ += static_cast<std::size_t>(newApprox);
    it->second->approxBytes = newApprox;
    stats_.currentBytes = totalBytes_;
    if (totalBytes_ > stats_.peakBytes) {
      stats_.peakBytes = totalBytes_;
    }
    updateVariantStatsBytes();
  }

  void recordVariantHit(bool suppressStats) noexcept {
    if (!suppressStats) {
      ++variantStats_.hits;
      debug::perf::recordTextParagraphVariantHit();
    }
  }

  void recordVariantMiss(bool suppressStats) noexcept {
    if (!suppressStats) {
      ++variantStats_.misses;
      debug::perf::recordTextParagraphVariantMiss();
    }
  }

  void recordVariantEviction(bool suppressStats) noexcept {
    if (!suppressStats) {
      ++variantStats_.evictions;
    }
  }

private:
  void updateVariantStatsBytes() noexcept {
    std::uint64_t vb = 0;
    for (auto const& p : map_) {
      for (auto const& vp : p.second->variants) {
        if (vp) {
          vb += static_cast<std::uint64_t>(vp->approxBytes);
        }
      }
    }
    variantStats_.currentBytes = vb;
    if (vb > variantStats_.peakBytes) {
      variantStats_.peakBytes = vb;
    }
  }

  static void releaseEntry(ShapedParagraph& e) noexcept {
    if (e.typesetter) {
      CFRelease(e.typesetter);
      e.typesetter = nullptr;
    }
    e.variants.clear();
    e.fontIds.clear();
    e.approxBytes = 0;
    e.byteLength = 0;
    e.utf16Length = 0;
  }

  std::unordered_map<ParagraphHash, std::shared_ptr<ShapedParagraph>, ParagraphHashHasher> map_;
  std::size_t totalBytes_ = 0;
  std::uint64_t currentFrame_ = 0;
  std::size_t budgetBytes_ = 64u * 1024u * 1024u;
  TextCacheStats::LayerStats stats_{};
  TextCacheStats::LayerStats variantStats_{};
};

// Deleted: AssemblyCache (replaced by LastLayoutMemo in CoreTextSystem::Impl)
// PLACEHOLDER_FOR_ASSEMBLY_CACHE_END

static LayoutMemoKey computeLayoutMemoKey(lambdaui::detail::SmallVector<ParagraphHash, 32> const& hashes,
                                          std::uint32_t maxWidthQ1, TextLayoutOptions const& opt) {
  XXH3_state_t st{};
  XXH3_128bits_reset(&st);
  for (std::size_t i = 0; i < hashes.size(); ++i) {
    ParagraphHash const& h = hashes[i];
    XXH3_128bits_update(&st, &h.hi, sizeof(h.hi));
    XXH3_128bits_update(&st, &h.lo, sizeof(h.lo));
  }
  XXH3_128bits_update(&st, &maxWidthQ1, sizeof(maxWidthQ1));
  std::int32_t const ml = opt.maxLines;
  XXH3_128bits_update(&st, &ml, sizeof(ml));
  std::uint8_t const w = static_cast<std::uint8_t>(opt.wrapping);
  XXH3_128bits_update(&st, &w, sizeof(w));
  std::uint32_t lhQ8 = 0;
  std::uint32_t lhMulQ8 = 0;
  if (opt.lineHeightMultiple > 0.f) {
    lhMulQ8 = static_cast<std::uint32_t>(std::lround(opt.lineHeightMultiple * 256.f));
  } else if (opt.lineHeight > 0.f) {
    lhQ8 = static_cast<std::uint32_t>(std::lround(opt.lineHeight * 4.f));
  }
  XXH3_128bits_update(&st, &lhQ8, sizeof(lhQ8));
  XXH3_128bits_update(&st, &lhMulQ8, sizeof(lhMulQ8));
  XXH128_hash_t const d = XXH3_128bits_digest(&st);
  return LayoutMemoKey{d.high64, d.low64};
}

} // namespace


struct CoreTextSystem::Impl {
  std::unordered_map<FontKey, std::uint32_t, FontKeyHash, FontKeyEq> fontIds_;
  std::vector<CTFontRef> fontById_;
  CGColorSpaceRef rgbColorSpace_ = nullptr;

  LruCache<std::uint64_t, CTFontRef> sizedFontCache_{256, releaseRef<CTFontRef>};
  LruCache<std::uint32_t, CGColorRef> colorCache_{256, releaseRef<CGColorRef>};
  LruCache<ParagraphStyleKey, CTParagraphStyleRef, ParagraphStyleKeyHash> paraStyleCache_{
      32, releaseRef<CTParagraphStyleRef>};
  LruCache<RunAttrKey, CFDictionaryRef, RunAttrKeyHash> runAttrCache_{1024, releaseRef<CFDictionaryRef>};

  std::unordered_map<ContentHash, std::unique_ptr<FramesetterEntry>, ContentHashHasher> frameMap_;
  std::size_t frameMapBytes_ = 0;
  std::uint64_t currentFrame_ = 0;
  std::size_t budgetBytes_ = 48u * 1024u * 1024u;
  TextCacheStats stats_{};

  ParagraphShapeCache paragraphCache_;
  std::size_t paragraphCacheBudgetBytes_ = 64u * 1024u * 1024u;

  struct LastLayoutMemo {
    std::uint64_t keyHi = 0;
    std::uint64_t keyLo = 0;
    ContentHash contentHash{};
    std::uint32_t contentWidthQ1 = 0;
    /// Pointer identity of the last AttributedString::utf8 data — if the same string object
    /// (same data pointer + same size) is re-used, skip the full content hash recomputation.
    char const* utf8Ptr = nullptr;
    std::size_t utf8Size = 0;
    std::shared_ptr<TextLayout const> layout;

    // --- Incremental split/assembly state ---
    /// Copy of the previous UTF-8 string, used by tryIncrementalSplit to locate the diff region.
    std::string prevUtf8;
    /// Copy of the previous run list (for attribute-change detection outside the dirty region).
    std::vector<AttributedRun> prevRuns;
    /// Paragraph split that produced `layout` (cached to avoid re-scanning on the next keystroke).
    std::vector<Paragraph> prevParagraphs;
    /// paraRunStarts[i]  = index of the first run  in `layout->runs`  belonging to paragraph i.
    /// paraRunStarts[N]  = layout->runs.size().
    std::vector<std::uint32_t> paraRunStarts;
    /// paraLineStarts[i] = index of the first line in `layout->lines` belonging to paragraph i.
    /// paraLineStarts[N] = layout->lines.size().
    std::vector<std::uint32_t> paraLineStarts;
    /// paraYCursors[i]   = yCursor (accumulated paragraph height) at the START of paragraph i.
    /// paraYCursors[N]   = total document height.
    std::vector<float> paraYCursors;
    /// paraCtBases[i]    = ctLineIndex base at the START of paragraph i.
    /// paraCtBases[N]    = total CTLine count.
    std::vector<std::uint32_t> paraCtBases;
    /// Maximum line width (= layout->measuredSize.width) of the last produced layout, stored to
    /// allow the incremental path to skip the O(prefix_runs) maxDocWidth scan.
    float prevMaxDocWidth = 0.f;
    /// firstBaseline of the last produced layout (copy of layout->firstBaseline).
    float prevFirstBaseline = 0.f;
  };
  LastLayoutMemo lastLayout_;
  TextCacheStats::LayerStats memoStats_{};

  ParagraphHash hashParagraph(CoreTextSystem& sys, char const* paraUtf8, std::uint32_t byteLen,
                              std::vector<ResolvedStyle> const& resolved, AttributedString const& text,
                              std::uint32_t runStart, std::uint32_t runEnd, std::uint32_t byteStart,
                              TextLayoutOptions const& opt);

  lambdaui::detail::SmallVector<Paragraph, 32> splitIntoParagraphs(
      CoreTextSystem& sys, AttributedString const& text, std::vector<ResolvedStyle> const& resolved,
      TextLayoutOptions const& opt);

  struct IncrementalSplitResult {
    lambdaui::detail::SmallVector<Paragraph, 32> paragraphs;
    std::size_t firstChanged = 0;        ///< First dirty paragraph index in the NEW list.
    std::size_t lastChangedExcl = 0;     ///< Exclusive end of dirty range in the NEW list.
    std::size_t firstChangedOld = 0;     ///< First dirty paragraph index in the OLD list.
    std::size_t lastChangedExclOld = 0;  ///< Exclusive end of dirty range in the OLD list.
    std::int32_t byteDelta = 0;          ///< newLen - oldLen.
  };

  /// Returns std::nullopt when an incremental split is not possible so the caller should fall back to the
  /// full \c splitIntoParagraphs path. Bail conditions: empty previous memo (\c prevUtf8 / \c prevParagraphs);
  /// identical strings (no-op); attributed run count changed; font/color mismatch on any run entirely
  /// outside the single dirty byte interval (prefix/suffix stable region); no old paragraph intersects the
  /// dirty region (\c firstChangedOld == prevParas.size()). The prefix/suffix diff always yields one
  /// contiguous changed region in the buffer (not multiple disjoint edits).
  std::optional<IncrementalSplitResult> tryIncrementalSplit(
      CoreTextSystem& sys, AttributedString const& text,
      std::vector<ResolvedStyle> const& resolved, TextLayoutOptions const& opt);

  ShapedParagraph shapeParagraphForCache(CoreTextSystem& sys, AttributedString const& text,
                                           std::vector<ResolvedStyle> const& resolved,
                                           Paragraph const& para, TextLayoutOptions const& options);

  std::shared_ptr<TextLayout const> layoutViaParagraphCache(
      CoreTextSystem& sys, AttributedString const& text, float maxWidth,
      TextLayoutOptions const& options, std::vector<ResolvedStyle> const& resolved,
      lambdaui::detail::SmallVector<Paragraph, 32>&& paragraphs, IncrementalSplitResult const* incr = nullptr,
      bool noMemoSideEffects = false);

  std::shared_ptr<ParagraphLayoutVariant> buildParagraphVariant(
      CoreTextSystem& sys, ParagraphHash const& hash, ShapedParagraph& sp, Paragraph const& para,
      AttributedString const& text, std::vector<ResolvedStyle> const& resolved, float maxWidth,
      TextLayoutOptions const& options, bool suppressStats);

  std::optional<std::uint32_t> findFontId(std::string_view family, float weight, bool italic) const noexcept {
    FontKeyView const kv{family, weight > 0.f ? weight : kDefaultFontWeight, italic};
    auto it = fontIds_.find(kv);
    if (it != fontIds_.end()) {
      return it->second;
    }
    return std::nullopt;
  }

  std::uint32_t fontIdForKey(FontKey const& key, CTFontRef font) {
    auto it = fontIds_.find(key);
    if (it != fontIds_.end()) {
      CFRelease(font);
      return it->second;
    }
    std::uint32_t const id = static_cast<std::uint32_t>(fontById_.size());
    fontIds_[key] = id;
    fontById_.push_back(font);
    return id;
  }

  void bumpEntryApproxBytes(FramesetterEntry& e, AttributedString const& text);

  Size const* findMeasureSlot(FramesetterEntry& e, std::uint32_t wq, std::int32_t ml) const {
    for (MeasureSlot& ms : e.measures) {
      if (ms.maxWidthQ1 == wq && ms.maxLines == ml) {
        return &ms.measuredSize;
      }
    }
    return nullptr;
  }

  void insertMeasureSlot(FramesetterEntry& e, std::uint32_t wq, std::int32_t ml, Size measuredSize) {
    e.measures.push_back(MeasureSlot{wq, ml, measuredSize});
  }

  std::shared_ptr<TextLayout const> findLayoutSlot(FramesetterEntry& e, std::uint32_t wq,
                                                   float maxWidth, std::int32_t ml,
                                                   bool suppressStats) {
    for (LayoutSlot& ls : e.layouts) {
      if (ls.maxWidthQ1 != wq || ls.maxLines != ml) {
        continue;
      }
      if (std::abs(ls.maxWidthExact - maxWidth) > 1e-5f) {
        continue;
      }
      if (!suppressStats) {
        ++stats_.l3_layout.hits;
      }
      return ls.unboxed;
    }
    if (!suppressStats) {
      ++stats_.l3_layout.misses;
    }
    return nullptr;
  }

  void insertLayoutSlot(FramesetterEntry& e, AttributedString const& text, std::uint32_t wq,
                        float maxWidth, std::int32_t ml,
                        std::shared_ptr<TextLayout const> layout) {
    LayoutSlot slot;
    slot.maxWidthQ1 = wq;
    slot.maxWidthExact = maxWidth;
    slot.maxLines = ml;
    slot.unboxed = std::move(layout);
    e.layouts.push_back(std::move(slot));
    bumpEntryApproxBytes(e, text);
  }

  FramesetterEntry& insertFramesetterMiss(ContentHash const& h, AttributedString const& text,
                                          std::span<ResolvedStyle const> resolved,
                                          TextLayoutOptions const& options, CoreTextSystem& sys);

  FramesetterEntry& findOrInsertFramesetterEntry(ContentHash const& h, AttributedString const& text,
                                                 std::vector<ResolvedStyle> const& resolved,
                                                 TextLayoutOptions const& options, CoreTextSystem& sys);

  ContentHash computeContentHash(CoreTextSystem& sys, AttributedString const& text,
                                 std::vector<ResolvedStyle> const& resolved, TextLayoutOptions const& opt);

  ContentHash computeContentHashPlain(CoreTextSystem& sys, std::string_view utf8, Font const& font,
                                      Color const& color, TextLayoutOptions const& opt);

  CGColorRef colorRef(std::uint32_t rgba) {
    if (CGColorRef* p = colorCache_.find(rgba)) {
      ++stats_.l1_color.hits;
      return *p;
    }
    ++stats_.l1_color.misses;
    float const r = static_cast<float>((rgba >> 24) & 0xFF) / 255.f;
    float const g = static_cast<float>((rgba >> 16) & 0xFF) / 255.f;
    float const b = static_cast<float>((rgba >> 8) & 0xFF) / 255.f;
    float const a = static_cast<float>(rgba & 0xFF) / 255.f;
    CGFloat comp[4] = {r, g, b, a};
    if (!rgbColorSpace_) {
      rgbColorSpace_ = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    }
    CGColorRef cg = CGColorCreate(rgbColorSpace_, comp);
    CGColorRef const result = colorCache_.insert(rgba, cg);
    stats_.l1_color.evictions += colorCache_.evictionsConsumed();
    return result;
  }

  CTFontRef sizedFont(std::uint32_t fontId, std::uint32_t sizeQ8) {
    std::uint64_t const key = (static_cast<std::uint64_t>(fontId) << 32) |
                              static_cast<std::uint64_t>(sizeQ8);
    if (CTFontRef* p = sizedFontCache_.find(key)) {
      ++stats_.l0_sizedFont.hits;
      return *p;
    }
    ++stats_.l0_sizedFont.misses;
    if (fontId >= fontById_.size()) {
      return nullptr;
    }
    CTFontRef base = fontById_[fontId];
    float const pt = static_cast<float>(sizeQ8) / 4.f;
    CTFontRef newf = CTFontCreateCopyWithAttributes(base, static_cast<CGFloat>(pt), nullptr, nullptr);
    CTFontRef const result = sizedFontCache_.insert(key, newf);
    stats_.l0_sizedFont.evictions += sizedFontCache_.evictionsConsumed();
    return result;
  }

  CTParagraphStyleRef paragraphStyleRef(TextLayoutOptions const& opt) {
    ParagraphStyleKey const pk = paragraphKeyFor(opt);
    if (CTParagraphStyleRef* p = paraStyleCache_.find(pk)) {
      ++stats_.l1_paraStyle.hits;
      return *p;
    }
    ++stats_.l1_paraStyle.misses;
    CTParagraphStyleRef ps = createParagraphStyleRef(opt);
    CTParagraphStyleRef const result = paraStyleCache_.insert(pk, ps);
    stats_.l1_paraStyle.evictions += paraStyleCache_.evictionsConsumed();
    return result;
  }

  CFDictionaryRef runAttrDict(std::uint32_t fontId, std::uint32_t sizeQ8, std::uint32_t rgba,
                              std::uint32_t backgroundRgba) {
    RunAttrKey const key{fontId, sizeQ8, rgba, backgroundRgba};
    if (CFDictionaryRef* p = runAttrCache_.find(key)) {
      ++stats_.l1_runAttr.hits;
      return *p;
    }
    ++stats_.l1_runAttr.misses;
    CTFontRef font = sizedFont(fontId, sizeQ8);
    CGColorRef cg = colorRef(rgba);
    NSDictionary* attrs = nil;
    if (backgroundRgba != 0) {
      attrs = @{
        (id)kCTFontAttributeName : (__bridge id)font,
        (id)kCTForegroundColorAttributeName : (__bridge id)cg,
        kLambdaBackgroundColorAttributeName : @(backgroundRgba),
      };
    } else {
      attrs = @{
        (id)kCTFontAttributeName : (__bridge id)font,
        (id)kCTForegroundColorAttributeName : (__bridge id)cg,
      };
    }
    CFDictionaryRef stored = (__bridge_retained CFDictionaryRef)attrs;
    CFDictionaryRef const result = runAttrCache_.insert(key, stored);
    stats_.l1_runAttr.evictions += runAttrCache_.evictionsConsumed();
    return result;
  }

  CFAttributedStringRef createCFAttributed(CoreTextSystem& sys, AttributedString const& text,
                                           std::span<ResolvedStyle const> resolved,
                                           TextLayoutOptions const& options) {
    NSString* ns = [[NSString alloc] initWithBytes:text.utf8.data()
                                            length:text.utf8.size()
                                          encoding:NSUTF8StringEncoding];
    bool lossyUtf8 = false;
    std::string sanitized;
    if (!ns) {
      sanitized = sanitizeUtf8Lossy(text.utf8);
      ns = [[NSString alloc] initWithBytes:sanitized.data()
                                    length:sanitized.size()
                                  encoding:NSUTF8StringEncoding];
      lossyUtf8 = true;
    }
    if (!ns) {
      ns = @"";
    }
    NSMutableAttributedString* mas =
        [[NSMutableAttributedString alloc] initWithString:ns attributes:@{}];

    char const* const bytes = text.utf8.c_str();
    std::size_t const byteLen = text.utf8.size();

    for (std::size_t ri = 0; ri < text.runs.size(); ++ri) {
      auto const& run = text.runs[ri];
      ResolvedStyle const& a = resolved[ri];
      NSRange range = lossyUtf8
          ? utf8ByteRangeToNSRangeLossy(bytes, byteLen, run.start, run.end)
          : utf8ByteRangeToNSRange(bytes, byteLen, run.start, run.end);
      range = clampNSRange(range, [mas length]);
      std::string_view const fam =
          a.font.family.empty() ? std::string_view(kDefaultFontFamily) : std::string_view(a.font.family);
      std::uint32_t fid = 0;
      if (auto const o = findFontId(fam, a.font.weight, a.font.italic)) {
        fid = *o;
      } else {
        fid = sys.resolveFontId(fam, a.font.weight, a.font.italic);
      }
      std::uint32_t const sizeQ8 = static_cast<std::uint32_t>(std::lround(a.font.size * 4.f));
      std::uint32_t const rgba = rgba8Pack(a.color);
      std::uint32_t const backgroundRgba = a.backgroundColor.has_value() ? rgba8Pack(*a.backgroundColor) : 0;
      CFDictionaryRef attrs = runAttrDict(fid, sizeQ8, rgba, backgroundRgba);
      [mas addAttributes:(__bridge id)attrs range:range];
    }

    CTParagraphStyleRef ps = paragraphStyleRef(options);
    NSDictionary* paraAttrs = @{ (id)kCTParagraphStyleAttributeName : (__bridge id)ps };
    [mas addAttributes:paraAttrs range:NSMakeRange(0, [mas length])];
    return (__bridge_retained CFAttributedStringRef)mas;
  }

  CFAttributedStringRef createCFAttributedPlain(CoreTextSystem& sys, std::string_view utf8, Font const& font,
                                                Color const& color, TextLayoutOptions const& options) {
    AttributedString as;
    as.utf8 = std::string(utf8);
    as.runs.push_back({.start = 0,
                       .end = static_cast<std::uint32_t>(utf8.size()),
                       .font = font,
                       .color = color});
    lambdaui::detail::SmallVector<ResolvedStyle, 4> resolved;
    accumulateInheritance(resolved, as);
    return createCFAttributed(sys, as, {resolved.data(), resolved.size()}, options);
  }

  void releaseFramesetterEntry(FramesetterEntry& e) {
    if (e.attrString) {
      CFRelease(e.attrString);
      e.attrString = nullptr;
    }
    if (e.framesetter) {
      CFRelease(e.framesetter);
      e.framesetter = nullptr;
    }
    e.measures.clear();
    e.layouts.clear();
    e.fontIds.clear();
    e.approxBytes = 0;
  }

  ~Impl() {
    paragraphCache_.invalidateAll();
    lastLayout_ = {};
    for (auto& p : frameMap_) {
      releaseFramesetterEntry(*p.second);
    }
    frameMap_.clear();
    for (CTFontRef f : fontById_) {
      if (f) {
        CFRelease(f);
      }
    }
    sizedFontCache_.clear();
    colorCache_.clear();
    runAttrCache_.clear();
    paraStyleCache_.clear();
    if (rgbColorSpace_) {
      CGColorSpaceRelease(rgbColorSpace_);
      rgbColorSpace_ = nullptr;
    }
  }
};

void CoreTextSystem::Impl::bumpEntryApproxBytes(FramesetterEntry& e, AttributedString const& text) {
  std::size_t const pieces = countStoredTextLayouts(e);
  std::uint32_t const newApprox = estimateEntryBytes(text, pieces);
  std::uint32_t const old = e.approxBytes;
  e.approxBytes = newApprox;
  frameMapBytes_ = frameMapBytes_ - static_cast<std::size_t>(old) + static_cast<std::size_t>(newApprox);
}

FramesetterEntry& CoreTextSystem::Impl::insertFramesetterMiss(ContentHash const& h,
                                                              AttributedString const& text,
                                                              std::span<ResolvedStyle const> resolved,
                                                              TextLayoutOptions const& options,
                                                              CoreTextSystem& sys) {
  if (!options.suppressCacheStats) {
    ++stats_.l2_framesetter.misses;
  }
  auto entry = std::make_unique<FramesetterEntry>();
  CFAttributedStringRef cf = createCFAttributed(sys, text, resolved, options);
  entry->attrString = cf;
  CTFramesetterRef fs = CTFramesetterCreateWithAttributedString(cf);
  entry->framesetter = fs;
  for (auto const& rs : resolved) {
    std::string_view const fam =
        rs.font.family.empty() ? std::string_view(kDefaultFontFamily) : std::string_view(rs.font.family);
    entry->fontIds.push_back(sys.resolveFontId(fam, rs.font.weight, rs.font.italic));
  }
  entry->approxBytes = estimateEntryBytes(text, 0);
  frameMapBytes_ += entry->approxBytes;
  frameMap_[h] = std::move(entry);
  return *frameMap_.find(h)->second;
}

FramesetterEntry& CoreTextSystem::Impl::findOrInsertFramesetterEntry(ContentHash const& h,
                                                                     AttributedString const& text,
                                                                     std::vector<ResolvedStyle> const& resolved,
                                                                     TextLayoutOptions const& options,
                                                                     CoreTextSystem& sys) {
  auto it = frameMap_.find(h);
  if (it != frameMap_.end()) {
    if (!options.suppressCacheStats) {
      ++stats_.l2_framesetter.hits;
    }
    it->second->lastTouchFrame = currentFrame_;
    return *it->second;
  }
  return insertFramesetterMiss(h, text, resolved, options, sys);
}

static void hashLayoutOptions(XXH3_state_t& st, TextLayoutOptions const& opt) {
  std::uint8_t wrap = static_cast<std::uint8_t>(opt.wrapping);
  std::uint32_t lhQ8 = 0;
  std::uint32_t lhMulQ8 = 0;
  if (opt.lineHeightMultiple > 0.f) {
    lhMulQ8 = static_cast<std::uint32_t>(std::lround(opt.lineHeightMultiple * 256.f));
  } else if (opt.lineHeight > 0.f) {
    lhQ8 = static_cast<std::uint32_t>(std::lround(opt.lineHeight * 4.f));
  }
  XXH3_128bits_update(&st, &wrap, sizeof(wrap));
  XXH3_128bits_update(&st, &lhQ8, sizeof(lhQ8));
  XXH3_128bits_update(&st, &lhMulQ8, sizeof(lhMulQ8));
}

ContentHash CoreTextSystem::Impl::computeContentHash(CoreTextSystem& sys, AttributedString const& text,
                                                     std::vector<ResolvedStyle> const& resolved,
                                                     TextLayoutOptions const& opt) {
  XXH3_state_t st{};
  XXH3_128bits_reset(&st);
  if (!text.utf8.empty()) {
    XXH3_128bits_update(&st, text.utf8.data(), text.utf8.size());
  }
  std::uint32_t const runCount = static_cast<std::uint32_t>(resolved.size());
  XXH3_128bits_update(&st, &runCount, sizeof(runCount));
  for (std::size_t i = 0; i < text.runs.size(); ++i) {
    auto const& run = text.runs[i];
    ResolvedStyle const& rs = resolved[i];
    std::string_view const fam =
        rs.font.family.empty() ? std::string_view(kDefaultFontFamily) : std::string_view(rs.font.family);
    std::uint32_t fid = 0;
    if (auto const o = findFontId(fam, rs.font.weight, rs.font.italic)) {
      fid = *o;
    } else {
      fid = sys.resolveFontId(fam, rs.font.weight, rs.font.italic);
    }
    std::uint32_t const sizeQ8 = static_cast<std::uint32_t>(std::lround(rs.font.size * 4.f));
    std::uint32_t const rgba = rgba8Pack(rs.color);
    std::uint32_t const backgroundRgba = rs.backgroundColor.has_value() ? rgba8Pack(*rs.backgroundColor) : 0;
    std::uint32_t b0 = run.start;
    std::uint32_t b1 = run.end;
    XXH3_128bits_update(&st, &b0, sizeof(b0));
    XXH3_128bits_update(&st, &b1, sizeof(b1));
    XXH3_128bits_update(&st, &fid, sizeof(fid));
    XXH3_128bits_update(&st, &sizeQ8, sizeof(sizeQ8));
    XXH3_128bits_update(&st, &rgba, sizeof(rgba));
    XXH3_128bits_update(&st, &backgroundRgba, sizeof(backgroundRgba));
  }
  hashLayoutOptions(st, opt);
  XXH128_hash_t const h = XXH3_128bits_digest(&st);
  return ContentHash{h.high64, h.low64};
}

ContentHash CoreTextSystem::Impl::computeContentHashPlain(CoreTextSystem& sys, std::string_view utf8,
                                                          Font const& font, Color const& color,
                                                          TextLayoutOptions const& opt) {
  XXH3_state_t st{};
  XXH3_128bits_reset(&st);
  if (!utf8.empty()) {
    XXH3_128bits_update(&st, utf8.data(), utf8.size());
  }
  std::uint32_t const runCount = 1;
  XXH3_128bits_update(&st, &runCount, sizeof(runCount));

  Font const resolved = resolveFont(baseDefaultsFont(), font);
  std::string_view const fam =
      resolved.family.empty() ? std::string_view(kDefaultFontFamily) : std::string_view(resolved.family);
  std::uint32_t fid = 0;
  if (auto const o = findFontId(fam, resolved.weight, resolved.italic)) {
    fid = *o;
  } else {
    fid = sys.resolveFontId(fam, resolved.weight, resolved.italic);
  }
  std::uint32_t const sizeQ8 = static_cast<std::uint32_t>(std::lround(resolved.size * 4.f));
  std::uint32_t const rgba = rgba8Pack(color);
  std::uint32_t b0 = 0;
  std::uint32_t b1 = static_cast<std::uint32_t>(utf8.size());
  XXH3_128bits_update(&st, &b0, sizeof(b0));
  XXH3_128bits_update(&st, &b1, sizeof(b1));
  XXH3_128bits_update(&st, &fid, sizeof(fid));
  XXH3_128bits_update(&st, &sizeQ8, sizeof(sizeQ8));
  XXH3_128bits_update(&st, &rgba, sizeof(rgba));

  hashLayoutOptions(st, opt);
  XXH128_hash_t const h = XXH3_128bits_digest(&st);
  return ContentHash{h.high64, h.low64};
}

ParagraphHash CoreTextSystem::Impl::hashParagraph(CoreTextSystem& sys, char const* paraUtf8,
                                                  std::uint32_t byteLen,
                                                  std::vector<ResolvedStyle> const& resolved,
                                                  AttributedString const& text, std::uint32_t runStart,
                                                  std::uint32_t runEnd, std::uint32_t byteStart,
                                                  TextLayoutOptions const& opt) {
  XXH3_state_t st{};
  XXH3_128bits_reset(&st);
  if (byteLen > 0 && paraUtf8) {
    XXH3_128bits_update(&st, paraUtf8, byteLen);
  }
  std::uint32_t clipCount = 0;
  std::uint32_t const byteEnd = byteStart + byteLen;
  for (std::uint32_t ri = runStart; ri < runEnd; ++ri) {
    auto const& run = text.runs[ri];
    std::uint32_t const cs = std::max(run.start, byteStart);
    std::uint32_t const ce = std::min(run.end, byteEnd);
    if (cs < ce) {
      ++clipCount;
    }
  }
  XXH3_128bits_update(&st, &clipCount, sizeof(clipCount));
  for (std::uint32_t ri = runStart; ri < runEnd; ++ri) {
    auto const& run = text.runs[ri];
    std::uint32_t const cs = std::max(run.start, byteStart);
    std::uint32_t const ce = std::min(run.end, byteEnd);
    if (cs >= ce) {
      continue;
    }
    std::uint32_t const len = ce - cs;
    ResolvedStyle const& rs = resolved[ri];
    std::string_view const fam =
        rs.font.family.empty() ? std::string_view(kDefaultFontFamily) : std::string_view(rs.font.family);
    std::uint32_t fid = 0;
    if (auto const o = findFontId(fam, rs.font.weight, rs.font.italic)) {
      fid = *o;
    } else {
      fid = sys.resolveFontId(fam, rs.font.weight, rs.font.italic);
    }
    std::uint32_t const sizeQ8 = static_cast<std::uint32_t>(std::lround(rs.font.size * 4.f));
    std::uint32_t const rgba = rgba8Pack(rs.color);
    std::uint32_t const backgroundRgba = rs.backgroundColor.has_value() ? rgba8Pack(*rs.backgroundColor) : 0;
    XXH3_128bits_update(&st, &len, sizeof(len));
    XXH3_128bits_update(&st, &fid, sizeof(fid));
    XXH3_128bits_update(&st, &sizeQ8, sizeof(sizeQ8));
    XXH3_128bits_update(&st, &rgba, sizeof(rgba));
    XXH3_128bits_update(&st, &backgroundRgba, sizeof(backgroundRgba));
  }
  hashLayoutOptions(st, opt);
  XXH128_hash_t const h = XXH3_128bits_digest(&st);
  return ParagraphHash{h.high64, h.low64};
}

CoreTextSystem::CoreTextSystem() : d(std::make_unique<Impl>()) {
  d->paragraphCache_.setBudget(d->paragraphCacheBudgetBytes_);
}

CoreTextSystem::~CoreTextSystem() = default;

std::uint32_t CoreTextSystem::resolveFontId(std::string_view fontFamily, float weight, bool italic) {
  std::string_view const fam = fontFamily.empty() ? std::string_view(kDefaultFontFamily) : fontFamily;
  if (auto const o = d->findFontId(fam, weight, italic)) {
    return *o;
  }
  FontKey key;
  key.family = std::string(fam);
  key.weight = weight > 0.f ? weight : kDefaultFontWeight;
  key.italic = italic;
  Font a;
  a.family = key.family;
  a.size = 12.f;
  a.weight = key.weight;
  a.italic = key.italic;
  CTFontRef font = createCTFont(a);
  return d->fontIdForKey(key, font);
}

static void adjustSuggestSizeForSingleLineTrailingWhitespace(CTFramesetterRef fs, CGSize* sz) {
  if (!fs || !sz) {
    return;
  }
  CGFloat const fw = std::max(sz->width, static_cast<CGFloat>(1e-6));
  CGFloat const fh = std::max(sz->height, static_cast<CGFloat>(1e-6));
  CfPtr<CGPathRef> path(CGPathCreateWithRect(CGRectMake(0, 0, fw, fh), nullptr));
  CfPtr<CTFrameRef> frame(CTFramesetterCreateFrame(fs, CFRangeMake(0, 0), path.get(), nullptr));
  if (!frame) {
    return;
  }
  CTFrameRef const frameRef = frame.get();
  CFArrayRef lines = CTFrameGetLines(frameRef);
  CFIndex const lineCount = lines ? CFArrayGetCount(lines) : 0;
  if (lineCount == 1) {
    CTLineRef const line = (CTLineRef)CFArrayGetValueAtIndex(lines, 0);
    CGFloat ascent = 0;
    CGFloat descent = 0;
    CGFloat leading = 0;
    double const tw = CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
    if (tw > sz->width) {
      sz->width = static_cast<CGFloat>(tw);
    }
  }
}


static Size measureWithFramesetter(CTFramesetterRef fs, float maxWidth) {
  if (!fs) {
    return {};
  }
  CGSize const constraints =
      CGSizeMake(maxWidth > 0.f ? static_cast<CGFloat>(maxWidth) : CGFLOAT_MAX, CGFLOAT_MAX);
  CFRange fitRange{};
  CGSize sz = CTFramesetterSuggestFrameSizeWithConstraints(fs, CFRangeMake(0, 0), nullptr, constraints,
                                                             &fitRange);
  adjustSuggestSizeForSingleLineTrailingWhitespace(fs, &sz);
  return Size{static_cast<float>(sz.width), static_cast<float>(sz.height)};
}

static void fillTextLayoutFromFramesetter(CoreTextSystem& sys, CTFramesetterRef fs,
                                          AttributedString const& text, float maxWidth,
                                          TextLayoutOptions const& options, TextLayout& out,
                                          TextLayoutStorage& storage) {
  if (!fs) {
    return;
  }
  CGSize const constraints =
      CGSizeMake(maxWidth > 0.f ? static_cast<CGFloat>(maxWidth) : CGFLOAT_MAX, CGFLOAT_MAX);
  CFRange fitRange{};
  CGSize sz = CTFramesetterSuggestFrameSizeWithConstraints(fs, CFRangeMake(0, 0), nullptr, constraints,
                                                           &fitRange);
  adjustSuggestSizeForSingleLineTrailingWhitespace(fs, &sz);

  CGFloat const fw = std::max(static_cast<CGFloat>(sz.width), static_cast<CGFloat>(1e-6));
  CGFloat const fh = std::max(static_cast<CGFloat>(sz.height), static_cast<CGFloat>(1e-6));
  out.measuredSize = Size{static_cast<float>(fw), static_cast<float>(fh)};

  // Path width must match the constraint used by CTFramesetterSuggestFrameSizeWithConstraints above.
  // Using only the tight sz.width can reflow lines into a narrower column than the measurement pass
  // (constraint maxWidth), which produced stacked/overlapping ink while the last line looked fine.
  CGFloat const pathW = maxWidth > 0.f ? static_cast<CGFloat>(maxWidth) : fw;

  CfPtr<CGPathRef> path(CGPathCreateWithRect(CGRectMake(0, 0, pathW, fh), nullptr));
  CfPtr<CTFrameRef> frame(CTFramesetterCreateFrame(fs, CFRangeMake(0, 0), path.get(), nullptr));
  if (!frame) {
    return;
  }

  CTFrameRef const frameRef = frame.get();
  CFArrayRef lines = CTFrameGetLines(frameRef);
  CFIndex const lineCount = lines ? CFArrayGetCount(lines) : 0;
  std::vector<CGPoint> origins(static_cast<std::size_t>(std::max(lineCount, CFIndex{0})));
  if (lineCount > 0) {
    CTFrameGetLineOrigins(frameRef, CFRangeMake(0, lineCount), origins.data());
  }

  std::vector<std::uint32_t> const utf16ToUtf8PrefixMap = buildUtf16ToUtf8PrefixMap(text.utf8);

  // Single allocation: count glyphs, resize arenas once, then fill with a write cursor so we never call
  // `vector::resize` during append (which could reallocate and invalidate earlier `std::span`s).
  std::size_t totalGlyphs = 0;
  std::vector<CGGlyph> glyphScratch;
  std::vector<std::uint32_t> glyphIdScratch;
  std::vector<CGPoint> positionScratch;
  std::vector<std::size_t> keptScratch;
  std::vector<RunEmitInfo> emitInfo;
  emitInfo.reserve(static_cast<std::size_t>(std::max(lineCount, CFIndex{0})));
  for (CFIndex li = 0; li < lineCount; ++li) {
    CTLineRef const line = (CTLineRef)CFArrayGetValueAtIndex(lines, li);
    CFArrayRef const glyphRuns = CTLineGetGlyphRuns(line);
    CFIndex const runCount = CFArrayGetCount(glyphRuns);
    for (CFIndex ri = 0; ri < runCount; ++ri) {
      CTRunRef const run = (CTRunRef)CFArrayGetValueAtIndex(glyphRuns, ri);
      RunEmitInfo const info = analyzeRunGlyphs(run, glyphScratch, glyphIdScratch);
      totalGlyphs += info.drawableCount;
      emitInfo.push_back(info);
    }
  }
  storage.glyphArena.resize(totalGlyphs);
  storage.positionArena.resize(totalGlyphs);
  std::size_t arenaWrite = 0;
  std::vector<std::uint32_t> lineRunEnd;
  lineRunEnd.reserve(static_cast<std::size_t>(std::max(lineCount, CFIndex{0})));
  std::size_t emitInfoIndex = 0;

  for (CFIndex li = 0; li < lineCount; ++li) {
    CTLineRef line = (CTLineRef)CFArrayGetValueAtIndex(lines, li);
    CGPoint const lineOrigin = origins[static_cast<std::size_t>(li)];
    CFArrayRef glyphRuns = CTLineGetGlyphRuns(line);
    CFIndex const runCount = CFArrayGetCount(glyphRuns);
    for (CFIndex ri = 0; ri < runCount; ++ri) {
      CTRunRef run = (CTRunRef)CFArrayGetValueAtIndex(glyphRuns, ri);
      appendPlacedRunToStorage(run, lineOrigin, fh, sys, out, storage, emitInfo[emitInfoIndex],
                               glyphScratch, glyphIdScratch, positionScratch, keptScratch, utf16ToUtf8PrefixMap, li,
                               arenaWrite);
      ++emitInfoIndex;
    }
    lineRunEnd.push_back(static_cast<std::uint32_t>(out.runs.size()));
  }
  assert(emitInfoIndex == emitInfo.size());
  assert(arenaWrite == totalGlyphs);
#ifndef NDEBUG
  for (std::size_t i = 1; i < out.runs.size(); ++i) {
    assert(out.runs[i - 1].ctLineIndex <= out.runs[i].ctLineIndex);
  }
#endif

  out.lines.clear();
  out.lines.reserve(static_cast<std::size_t>(std::max(lineCount, CFIndex{0})));
  std::uint32_t runBegin = 0;
  float docMinX = std::numeric_limits<float>::infinity();
  float docMaxX = -std::numeric_limits<float>::infinity();
  float docMinTop = std::numeric_limits<float>::infinity();
  float docMaxBot = -std::numeric_limits<float>::infinity();
  float docMinBaselineY = std::numeric_limits<float>::infinity();
  float docMaxBaselineY = -std::numeric_limits<float>::infinity();
  for (CFIndex li = 0; li < lineCount; ++li) {
    CTLineRef line = (CTLineRef)CFArrayGetValueAtIndex(lines, li);
    CGPoint const lineOrigin = origins[static_cast<std::size_t>(li)];
    CFRange const rng = CTLineGetStringRange(line);
    NSRange const nsr = NSMakeRange(static_cast<NSUInteger>(rng.location), static_cast<NSUInteger>(rng.length));
    TextLayout::LineRange lr{};
    lr.ctLineIndex = static_cast<std::uint32_t>(li);
    std::uint32_t b0 = 0;
    std::uint32_t b1 = 0;
    utf16RangeToUtf8ByteRange(utf16ToUtf8PrefixMap, nsr, b0, b1);
    lr.byteStart = static_cast<int>(b0);
    lr.byteEnd = static_cast<int>(b1);

    float baselineY = -std::numeric_limits<float>::infinity();
    float minTop = std::numeric_limits<float>::infinity();
    float maxBot = -std::numeric_limits<float>::infinity();
    float minOx = std::numeric_limits<float>::infinity();
    std::uint32_t const runEnd = lineRunEnd[static_cast<std::size_t>(li)];
    for (std::uint32_t i = runBegin; i < runEnd; ++i) {
      auto const& pr = out.runs[i];
      docMinX = std::min(docMinX, pr.origin.x);
      docMaxX = std::max(docMaxX, pr.origin.x + pr.run.width);
      docMinTop = std::min(docMinTop, pr.origin.y - pr.run.ascent);
      docMaxBot = std::max(docMaxBot, pr.origin.y + pr.run.descent);
      docMinBaselineY = std::min(docMinBaselineY, pr.origin.y);
      docMaxBaselineY = std::max(docMaxBaselineY, pr.origin.y);
      baselineY = std::max(baselineY, pr.origin.y);
      minTop = std::min(minTop, pr.origin.y - pr.run.ascent);
      maxBot = std::max(maxBot, pr.origin.y + pr.run.descent);
      minOx = std::min(minOx, pr.origin.x);
    }
    runBegin = runEnd;
    // No drawable runs on this line (e.g. newline-only, filtered glyphs): still need typographic bounds
    // so `LineRange` matches `CTLineGetTypographicBounds` / caret height when text exists on other lines.
    if (!std::isfinite(minTop) || !std::isfinite(maxBot) || !(maxBot > minTop + 1e-4f)) {
      CGFloat ascent = 0;
      CGFloat descent = 0;
      CGFloat leading = 0;
      (void)CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
      float const baselineCanvas = static_cast<float>(fh - lineOrigin.y);
      lr.baseline = baselineCanvas;
      lr.top = baselineCanvas - static_cast<float>(ascent);
      lr.bottom = baselineCanvas + static_cast<float>(descent);
      lr.lineMinX = std::isfinite(minOx) ? minOx : static_cast<float>(lineOrigin.x);
    } else {
      lr.baseline = baselineY;
      lr.top = minTop;
      lr.bottom = maxBot;
      lr.lineMinX = std::isfinite(minOx) ? minOx : 0.f;
    }
    out.lines.push_back(lr);
  }

  if (options.maxLines == 0 && !out.runs.empty() &&
      std::isfinite(docMinX) && std::isfinite(docMaxX) && std::isfinite(docMinTop) &&
      std::isfinite(docMaxBot) && std::isfinite(docMinBaselineY) && std::isfinite(docMaxBaselineY)) {
    out.measuredSize.width = std::max(0.f, docMaxX - docMinX);
    out.measuredSize.height = std::max(0.f, docMaxBot - docMinTop);
    out.firstBaseline = docMinBaselineY - docMinTop;
    out.lastBaseline = docMaxBaselineY - docMinTop;
  } else {
    recomputeTextLayoutMetrics(out);
  }
  if (out.measuredSize.width <= 0.f && out.measuredSize.height <= 0.f && !text.utf8.empty()) {
    out.measuredSize = Size{static_cast<float>(fw), static_cast<float>(fh)};
  }
  if (options.maxLines > 0) {
    trimTextLayoutToMaxLines(out, options.maxLines, true);
  }
}

static std::shared_ptr<ParagraphLayoutVariant> findVariant(ShapedParagraph& sp, std::uint32_t wq,
                                                           TextLayoutOptions const& opt,
                                                           ParagraphShapeCache& pec, bool suppressStats) {
  std::uint32_t lhQ8 = 0;
  std::uint32_t lhMulQ8 = 0;
  if (opt.lineHeightMultiple > 0.f) {
    lhMulQ8 = static_cast<std::uint32_t>(std::lround(opt.lineHeightMultiple * 256.f));
  } else if (opt.lineHeight > 0.f) {
    lhQ8 = static_cast<std::uint32_t>(std::lround(opt.lineHeight * 4.f));
  }
  std::uint8_t const wrap = static_cast<std::uint8_t>(opt.wrapping);
  for (auto const& vp : sp.variants) {
    if (!vp) {
      continue;
    }
    if (vp->maxWidthQ1 == wq && vp->wrap == wrap && vp->lhQ8 == lhQ8 && vp->lhMulQ8 == lhMulQ8) {
      pec.recordVariantHit(suppressStats);
      return vp;
    }
  }
  return nullptr;
}

std::shared_ptr<ParagraphLayoutVariant> CoreTextSystem::Impl::buildParagraphVariant(
    CoreTextSystem& sys, ParagraphHash const& hash, ShapedParagraph& sp, Paragraph const& para,
    AttributedString const& text, std::vector<ResolvedStyle> const& resolved, float maxWidth,
    TextLayoutOptions const& options, bool suppressStats) {
  paragraphCache_.recordVariantMiss(suppressStats);
  std::uint32_t const oldAb = sp.approxBytes;
  if (sp.variants.size() == kMaxVariantsPerParagraph) {
    paragraphCache_.recordVariantEviction(suppressStats);
    sp.variants[0] = std::move(sp.variants[1]);
    sp.variants.pop_back();
  }
  std::uint32_t const wq = quantizeWidth(maxWidth);
  auto v = std::make_shared<ParagraphLayoutVariant>();
  v->maxWidthQ1 = wq;
  v->wrap = static_cast<std::uint8_t>(options.wrapping);
  if (options.lineHeightMultiple > 0.f) {
    v->lhMulQ8 = static_cast<std::uint32_t>(std::lround(options.lineHeightMultiple * 256.f));
  } else if (options.lineHeight > 0.f) {
    v->lhQ8 = static_cast<std::uint32_t>(std::lround(options.lineHeight * 4.f));
  }

  // Use CTFramesetter for paragraph variants so multi-line ink matches `fillTextLayoutFromFramesetter` /
  // `layoutUnboxed`. The previous CTTypesetter-only layout could diverge (wrong per-line geometry / glyphs).
  {
    AttributedString paraText{};
    paraText.utf8 = text.utf8.substr(para.byteStart, para.byteEnd - para.byteStart);
    std::vector<ResolvedStyle> paraResolved;
    for (std::uint32_t ri = para.runStart; ri < para.runEnd; ++ri) {
      auto const& run = text.runs[ri];
      std::uint32_t const cs = std::max(run.start, para.byteStart);
      std::uint32_t const ce = std::min(run.end, para.byteEnd);
      if (cs >= ce) continue;
      AttributedRun nr{};
      nr.start = cs - para.byteStart;
      nr.end = ce - para.byteStart;
      nr.font = run.font;
      nr.color = run.color;
      nr.backgroundColor = run.backgroundColor;
      paraText.runs.push_back(nr);
      paraResolved.push_back(resolved[ri]);
    }
    CfPtr<CFAttributedStringRef> attr(createCFAttributed(sys, paraText, paraResolved, options));
    if (!attr) {
      v->approxBytes = computeVariantApproxBytes(*v);
      sp.variants.push_back(v);
      sp.approxBytes = recomputeShapedParagraphApproxBytes(sp);
      paragraphCache_.adjustParagraphBytes(hash, oldAb, sp.approxBytes);
      return v;
    }
    CfPtr<CTFramesetterRef> fs(CTFramesetterCreateWithAttributedString(attr.get()));
    TextLayout partial{};
    TextLayoutStorage stor{};
    fillTextLayoutFromFramesetter(sys, fs.get(), paraText, maxWidth, options, partial, stor);
    // Empty paragraph (consecutive `\n` segments): CTFramesetter reports 0 height and no lines, so
    // multi-paragraph assembly would not advance `yCursor` and blank lines visually collapse.
    if (para.byteStart == para.byteEnd) {
      Font baseFont{};
      if (!text.runs.empty()) {
        baseFont = text.runs[0].font;
      } else {
        baseFont.family = kDefaultFontFamily;
        baseFont.size = 12.f;
      }
      if (baseFont.size <= 0.f) {
        baseFont.size = 16.f;
      }
      std::string_view const fam =
          baseFont.family.empty() ? std::string_view(kDefaultFontFamily) : std::string_view(baseFont.family);
      std::uint32_t const fid = sys.resolveFontId(fam, baseFont.weight, baseFont.italic);
      std::uint32_t const sizeQ8 = static_cast<std::uint32_t>(std::lround(baseFont.size * 4.f));
      CTFontRef const ct = sizedFont(fid, sizeQ8);
      CGFloat const ascent = CTFontGetAscent(ct);
      CGFloat const descent = CTFontGetDescent(ct);
      CGFloat const leading = CTFontGetLeading(ct);
      float lineH = static_cast<float>(ascent + descent + leading);
      if (options.lineHeight > 0.f) {
        lineH = std::max(lineH, options.lineHeight);
      }
      if (options.lineHeightMultiple > 0.f) {
        lineH *= options.lineHeightMultiple;
      }
      TextLayout::LineRange lr{};
      lr.ctLineIndex = 0;
      lr.top = 0.f;
      lr.baseline = static_cast<float>(ascent);
      lr.bottom = lineH;
      lr.byteStart = static_cast<int>(para.byteStart);
      lr.byteEnd = static_cast<int>(para.byteEnd);
      lr.lineMinX = 0.f;
      partial.runs.clear();
      partial.lines.clear();
      partial.lines.push_back(lr);
      recomputeTextLayoutMetrics(partial);
    }
#ifndef NDEBUG
    std::size_t sumGlyphs = 0;
    for (auto const& pr : partial.runs) {
      sumGlyphs += pr.run.glyphIds.size();
    }
    assert(sumGlyphs == stor.glyphArena.size() && "run glyph counts must match arenas");
#endif
    v->runs = std::move(partial.runs);
    v->lines = std::move(partial.lines);
    v->height = partial.measuredSize.height;
    v->maxLineWidth = partial.measuredSize.width;
    v->glyphStorage = std::move(stor.glyphArena);
    v->positionStorage = std::move(stor.positionArena);
    std::size_t off = 0;
    for (auto& pr : v->runs) {
      std::size_t const n = pr.run.glyphIds.size();
      assert(off + n <= v->glyphStorage.size() && "variant glyph span overflow");
      pr.run.glyphIds = std::span<std::uint32_t const>(v->glyphStorage.data() + off, n);
      pr.run.positions = std::span<Point const>(v->positionStorage.data() + off, n);
      off += n;
    }
    assert(off == v->glyphStorage.size() && off == v->positionStorage.size() && "variant repoint must cover arenas");
  }

  v->approxBytes = computeVariantApproxBytes(*v);
  sp.variants.push_back(v);
  sp.approxBytes = recomputeShapedParagraphApproxBytes(sp);
  paragraphCache_.adjustParagraphBytes(hash, oldAb, sp.approxBytes);
  return v;
}


static std::uint32_t countNewlines(std::string const& utf8) noexcept {
  std::uint32_t n = 0;
  for (char const c : utf8) {
    if (c == '\n') {
      ++n;
    }
  }
  return n;
}

static bool paragraphCachePredicate(AttributedString const& text, TextLayoutOptions const& opt) noexcept {
  if (paragraphCacheDisabledByEnv()) {
    return false;
  }
  if (text.utf8.size() < kMinFastPathBytes) {
    return false;
  }
  if (opt.wrapping == TextWrapping::WrapAnywhere) {
    return false;
  }
  if (opt.maxLines != 0) {
    return false;
  }
  if (countNewlines(text.utf8) < kMinHardLineBreaks) {
    return false;
  }
  return true;
}

lambdaui::detail::SmallVector<Paragraph, 32> CoreTextSystem::Impl::splitIntoParagraphs(
    CoreTextSystem& sys, AttributedString const& text, std::vector<ResolvedStyle> const& resolved,
    TextLayoutOptions const& opt) {
  lambdaui::detail::SmallVector<Paragraph, 32> out;
  char const* const bytes = text.utf8.data();
  std::uint32_t const n = static_cast<std::uint32_t>(text.utf8.size());
  std::uint32_t seg = 0;
  while (seg < n) {
    std::uint32_t const paraStart = seg;
    while (seg < n && bytes[seg] != '\n') {
      ++seg;
    }
    std::uint32_t const paraEnd = seg;
    if (seg < n && bytes[seg] == '\n') {
      ++seg;
    }
    std::uint32_t runStart = 0;
    while (runStart < text.runs.size() && text.runs[runStart].end <= paraStart) {
      ++runStart;
    }
    std::uint32_t runEnd = runStart;
    while (runEnd < text.runs.size() && text.runs[runEnd].start < paraEnd) {
      ++runEnd;
    }
    Paragraph p{};
    p.byteStart = paraStart;
    p.byteEnd = paraEnd;
    p.runStart = runStart;
    p.runEnd = runEnd;
    char const* pUtf8 = bytes + paraStart;
    std::uint32_t const blen = paraEnd - paraStart;
    p.hash = hashParagraph(sys, pUtf8, blen, resolved, text, runStart, runEnd, paraStart, opt);
    out.push_back(p);
  }
  return out;
}

std::optional<CoreTextSystem::Impl::IncrementalSplitResult>
CoreTextSystem::Impl::tryIncrementalSplit(CoreTextSystem& sys, AttributedString const& text,
                                          std::vector<ResolvedStyle> const& resolved,
                                          TextLayoutOptions const& opt) {
  if (lastLayout_.prevUtf8.empty() || lastLayout_.prevParagraphs.empty()) {
    return std::nullopt;
  }

  std::string const& prev = lastLayout_.prevUtf8;
  std::string const& curr = text.utf8;

  // Find common prefix length.
  auto const [prevIt, currIt] = std::mismatch(prev.begin(), prev.end(), curr.begin(), curr.end());
  std::size_t const prefixLen = static_cast<std::size_t>(prevIt - prev.begin());
  if (prevIt == prev.end() && currIt == curr.end()) {
    return std::nullopt; // identical — should not happen here, but safe
  }

  std::int32_t const delta =
      static_cast<std::int32_t>(curr.size()) - static_cast<std::int32_t>(prev.size());

  // Find common suffix length (capped so the two dirty windows can't overlap).
  // Use memcmp in a binary-search pattern so the fast path (all equal) costs one SIMD memcmp,
  // replacing the old scalar byte-by-byte backward loop that was ~3ms for a 202KB suffix.
  std::size_t suffixLen = 0;
  {
    std::size_t const maxOldSuffix = prev.size() - prefixLen;
    std::size_t const maxNewSuffix = curr.size() - prefixLen;
    std::size_t const maxSuffix = std::min(maxOldSuffix, maxNewSuffix);
    if (maxSuffix > 0) {
      char const* const prevEnd = prev.data() + prev.size();
      char const* const currEnd = curr.data() + curr.size();
      if (std::memcmp(prevEnd - maxSuffix, currEnd - maxSuffix, maxSuffix) == 0) {
        // Fast path: the entire trailing region is identical.
        suffixLen = maxSuffix;
      } else {
        // Binary-search for the exact length of the common suffix.
        std::size_t lo = 0, hi = maxSuffix;
        while (lo + 1 < hi) {
          std::size_t const mid = (lo + hi) / 2;
          if (std::memcmp(prevEnd - mid, currEnd - mid, mid) == 0) {
            lo = mid; // suffix length >= mid
          } else {
            hi = mid; // suffix length < mid
          }
        }
        suffixLen = lo;
      }
    }
  }
  std::size_t const dirtyOldEnd = prev.size() - suffixLen;

  // If the run count changed, we can't safely reuse suffix-paragraph hashes.
  auto const& prevRuns = lastLayout_.prevRuns;
  if (text.runs.size() != prevRuns.size()) {
    return std::nullopt;
  }

  // If any run's attributes changed outside the dirty byte region, fall back.
  for (std::size_t i = 0; i < prevRuns.size(); ++i) {
    auto const& pr = prevRuns[i];
    auto const& cr = text.runs[i];
    bool const outsideDirty = (pr.end <= prefixLen) || (pr.start >= dirtyOldEnd);
    if (outsideDirty) {
      if (pr.font.family != cr.font.family || pr.font.size != cr.font.size ||
          pr.font.weight != cr.font.weight || pr.font.italic != cr.font.italic ||
          !(pr.color == cr.color)) {
        return std::nullopt;
      }
    }
  }

  auto const& prevParas = lastLayout_.prevParagraphs;

  // Locate the first old paragraph that touches the dirty region.
  std::size_t firstChangedOld = prevParas.size();
  for (std::size_t i = 0; i < prevParas.size(); ++i) {
    if (prevParas[i].byteEnd > prefixLen) {
      firstChangedOld = i;
      break;
    }
  }
  if (firstChangedOld == prevParas.size()) {
    return std::nullopt;
  }

  // Locate the exclusive end of old paragraphs that touch the dirty region.
  std::size_t lastChangedExclOld = firstChangedOld;
  for (std::size_t i = firstChangedOld; i < prevParas.size(); ++i) {
    if (prevParas[i].byteStart < dirtyOldEnd) {
      lastChangedExclOld = i + 1u;
    } else {
      break;
    }
  }

  // Compute the new string's "dirty end" (in terms of bytes in the new string):
  // the suffix starts at (prev.size() - suffixLen) in old = (curr.size() - suffixLen) in new.
  std::uint32_t const newDirtyEnd = static_cast<std::uint32_t>(curr.size() - suffixLen);

  IncrementalSplitResult res;
  res.firstChangedOld = firstChangedOld;
  res.lastChangedExclOld = lastChangedExclOld;
  res.byteDelta = delta;

  // Prefix paragraphs: identical in old and new.
  for (std::size_t i = 0; i < firstChangedOld; ++i) {
    res.paragraphs.push_back(prevParas[i]);
  }
  res.firstChanged = res.paragraphs.size();

  // Re-scan the dirty window in the NEW string.
  char const* const newBytes = curr.data();
  std::uint32_t const newLen = static_cast<std::uint32_t>(curr.size());
  std::uint32_t seg = prevParas[firstChangedOld].byteStart;
  while (seg < newDirtyEnd && seg < newLen) {
    std::uint32_t const paraStart = seg;
    while (seg < newLen && newBytes[seg] != '\n') ++seg;
    std::uint32_t const paraEnd = seg;
    if (seg < newLen) ++seg; // consume '\n'
    std::uint32_t runStart = 0;
    while (runStart < static_cast<std::uint32_t>(text.runs.size()) &&
           text.runs[runStart].end <= paraStart) {
      ++runStart;
    }
    std::uint32_t runEnd = runStart;
    while (runEnd < static_cast<std::uint32_t>(text.runs.size()) &&
           text.runs[runEnd].start < paraEnd) {
      ++runEnd;
    }
    Paragraph p{};
    p.byteStart = paraStart;
    p.byteEnd = paraEnd;
    p.runStart = runStart;
    p.runEnd = runEnd;
    p.hash = hashParagraph(sys, newBytes + paraStart, paraEnd - paraStart, resolved, text,
                           runStart, runEnd, paraStart, opt);
    res.paragraphs.push_back(p);
    if (paraEnd >= newLen) break;
  }
  res.lastChangedExcl = res.paragraphs.size();

  // Suffix paragraphs: same content, byte positions shifted by delta.
  for (std::size_t i = lastChangedExclOld; i < prevParas.size(); ++i) {
    Paragraph p = prevParas[i];
    p.byteStart = static_cast<std::uint32_t>(static_cast<std::int32_t>(p.byteStart) + delta);
    p.byteEnd = static_cast<std::uint32_t>(static_cast<std::int32_t>(p.byteEnd) + delta);
    // runStart/runEnd indices are valid (same run count and ordering).
    res.paragraphs.push_back(p);
  }

  return res;
}


ShapedParagraph CoreTextSystem::Impl::shapeParagraphForCache(CoreTextSystem& sys,
                                                             AttributedString const& text,
                                                             std::vector<ResolvedStyle> const& resolved,
                                                             Paragraph const& para,
                                                             TextLayoutOptions const& options) {
  ShapedParagraph sp{};
  sp.byteLength = para.byteEnd - para.byteStart;
  AttributedString paraText{};
  std::vector<ResolvedStyle> paraResolved;
  paraText.utf8 = text.utf8.substr(para.byteStart, sp.byteLength);
  for (std::uint32_t ri = para.runStart; ri < para.runEnd; ++ri) {
    auto const& run = text.runs[ri];
    std::uint32_t const cs = std::max(run.start, para.byteStart);
    std::uint32_t const ce = std::min(run.end, para.byteEnd);
    if (cs >= ce) {
      continue;
    }
    AttributedRun nr{};
    nr.start = cs - para.byteStart;
    nr.end = ce - para.byteStart;
    nr.font = run.font;
    nr.color = run.color;
    nr.backgroundColor = run.backgroundColor;
    paraText.runs.push_back(nr);
    paraResolved.push_back(resolved[ri]);
  }
  CfPtr<CFAttributedStringRef> cf(createCFAttributed(sys, paraText, paraResolved, options));
  sp.utf16Length = static_cast<CFIndex>(CFAttributedStringGetLength(cf.get()));
  CfPtr<CTTypesetterRef> ts(CTTypesetterCreateWithAttributedString(cf.get()));
  sp.typesetter = ts.release();
  std::unordered_set<std::uint32_t> fidSeen;
  for (auto const& rs : paraResolved) {
    std::string_view const fam =
        rs.font.family.empty() ? std::string_view(kDefaultFontFamily) : std::string_view(rs.font.family);
    std::uint32_t const fid = sys.resolveFontId(fam, rs.font.weight, rs.font.italic);
    if (fidSeen.insert(fid).second) {
      sp.fontIds.push_back(fid);
    }
  }
  sp.approxBytes = recomputeShapedParagraphApproxBytes(sp);
  return sp;
}

// Helpers used only by layoutViaParagraphCache.

std::shared_ptr<TextLayout const> CoreTextSystem::Impl::layoutViaParagraphCache(
    CoreTextSystem& sys, AttributedString const& text, float maxWidth, TextLayoutOptions const& options,
    std::vector<ResolvedStyle> const& resolved, lambdaui::detail::SmallVector<Paragraph, 32>&& paragraphs,
    IncrementalSplitResult const* incr, bool const noMemoSideEffects) {
#if defined(LAMBDAUI_PARAGRAPH_CACHE_PARALLEL_ASSERT) && !defined(NDEBUG)
  lambdaui::detail::SmallVector<Paragraph, 32> parallelRefParagraphs;
  if (incr != nullptr) {
    parallelRefParagraphs = paragraphs;
  }
#endif
  bool const suppressStats = options.suppressCacheStats;
  std::uint32_t const wq = quantizeWidth(maxWidth);
  std::size_t const paraCount = paragraphs.size();

  // On the incremental path (incr != nullptr) the content provably changed, so the memoKey
  // check would always miss.  Skip the O(N) phashes build and hash computation entirely.
  if (!noMemoSideEffects) {
    if (incr == nullptr) {
      lambdaui::detail::SmallVector<ParagraphHash, 32> phashes;
      phashes.reserve(paraCount);
      for (std::size_t i = 0; i < paraCount; ++i) {
        phashes.push_back(paragraphs[i].hash);
      }
      LayoutMemoKey const memoKey = computeLayoutMemoKey(phashes, wq, options);
      if (lastLayout_.layout && lastLayout_.keyHi == memoKey.hi && lastLayout_.keyLo == memoKey.lo) {
        if (!suppressStats) ++memoStats_.hits;
        return lastLayout_.layout;
      }
      if (!suppressStats) ++memoStats_.misses;
      lastLayout_.keyHi = memoKey.hi;
      lastLayout_.keyLo = memoKey.lo;
    } else {
      if (!suppressStats) ++memoStats_.misses;
      // Invalidate stale memoKey so a non-incremental caller doesn't get a false hit.
      lastLayout_.keyHi = 0;
      lastLayout_.keyLo = 0;
    }
  }

  // Can we do incremental assembly (bulk-copy prefix+suffix, rebuild only dirty paragraphs)?
  bool const canIncrAssemble =
      !noMemoSideEffects && incr != nullptr && lastLayout_.layout != nullptr &&
      lastLayout_.paraRunStarts.size() == lastLayout_.prevParagraphs.size() + 1u &&
      incr->lastChangedExclOld <= lastLayout_.prevParagraphs.size();

  // Per-paragraph tracking tables (filled during assembly, stored in memo afterwards).
  std::vector<std::uint32_t> newRunStarts(paraCount + 1u, 0u);
  std::vector<std::uint32_t> newLineStarts(paraCount + 1u, 0u);
  std::vector<float>         newYCursors(paraCount + 1u, 0.f);
  std::vector<std::uint32_t> newCtBases(paraCount + 1u, 0u);

  auto out = std::make_shared<TextLayout>();
  out->runs.reserve(paraCount);
  out->lines.reserve(paraCount);
  out->variantRefs.reserve(paraCount);
  float yCursor = 0.f;
  std::uint32_t ctBase = 0;
  float maxDocWidth = 0.f;

  // Assemble one paragraph: look up shape cache, find/build variant, append to `out`.
  auto assembleParagraph = [&](std::size_t pi) -> bool {
    newRunStarts[pi]  = static_cast<std::uint32_t>(out->runs.size());
    newLineStarts[pi] = static_cast<std::uint32_t>(out->lines.size());
    newYCursors[pi]   = yCursor;
    newCtBases[pi]    = ctBase;

    Paragraph const& para = paragraphs[pi];
    std::shared_ptr<ShapedParagraph> sp = paragraphCache_.findShared(para.hash, suppressStats);
    if (!sp) {
      ShapedParagraph shaped = shapeParagraphForCache(sys, text, resolved, para, options);
      paragraphCache_.insert(para.hash, std::move(shaped), suppressStats);
      sp = paragraphCache_.findShared(para.hash, true);
    }
    if (!sp) return false;

#if defined(LAMBDA_DISABLE_VARIANT_CACHE)
    std::shared_ptr<ParagraphLayoutVariant> v;
#else
    std::shared_ptr<ParagraphLayoutVariant> v = findVariant(*sp, wq, options, paragraphCache_, suppressStats);
#endif
    if (!v) {
      v = buildParagraphVariant(sys, para.hash, *sp, para, text, resolved, maxWidth, options,
                                suppressStats);
    }

    out->variantRefs.push_back(typeErasedVariantRef(v));
    std::size_t const runBase = out->runs.size();
    out->runs.resize(runBase + v->runs.size());
    for (std::size_t i = 0; i < v->runs.size(); ++i) {
      TextLayout::PlacedRun& dst = out->runs[runBase + i];
      dst = v->runs[i];
      dst.origin.y    += yCursor;
      dst.utf8Begin   += para.byteStart;
      dst.utf8End     += para.byteStart;
      dst.ctLineIndex += ctBase;
    }
    std::size_t const lineBase = out->lines.size();
    out->lines.resize(lineBase + v->lines.size());
    for (std::size_t i = 0; i < v->lines.size(); ++i) {
      TextLayout::LineRange& dst = out->lines[lineBase + i];
      dst = v->lines[i];
      dst.top         += yCursor;
      dst.bottom      += yCursor;
      dst.baseline    += yCursor;
      dst.byteStart   += static_cast<int>(para.byteStart);
      dst.byteEnd     += static_cast<int>(para.byteStart);
      dst.ctLineIndex += ctBase;
    }
    yCursor     += v->height;
    ctBase      += static_cast<std::uint32_t>(v->lines.size());
    maxDocWidth  = std::max(maxDocWidth, v->maxLineWidth);
    return true;
  };

  if (canIncrAssemble) {
    TextLayout const& prev = *lastLayout_.layout;
    std::size_t const fc   = incr->firstChanged;
    std::size_t const lce  = incr->lastChangedExcl;
    std::size_t const fco  = incr->firstChangedOld;
    std::size_t const lceo = incr->lastChangedExclOld;
    std::int32_t const bd  = incr->byteDelta;

    // Prefix: copy index entries from old memo, bulk-copy data from old layout.
    for (std::size_t i = 0; i < fc; ++i) {
      newRunStarts[i]  = lastLayout_.paraRunStarts[i];
      newLineStarts[i] = lastLayout_.paraLineStarts[i];
      newYCursors[i]   = lastLayout_.paraYCursors[i];
      newCtBases[i]    = lastLayout_.paraCtBases[i];
    }
    std::uint32_t const prefixRunEnd  = lastLayout_.paraRunStarts[fco];
    std::uint32_t const prefixLineEnd = lastLayout_.paraLineStarts[fco];
    if (prefixRunEnd > 0) {
      out->runs.insert(out->runs.end(),
          prev.runs.begin(),
          prev.runs.begin() + static_cast<std::ptrdiff_t>(prefixRunEnd));
    }
    if (prefixLineEnd > 0) {
      out->lines.insert(out->lines.end(),
          prev.lines.begin(),
          prev.lines.begin() + static_cast<std::ptrdiff_t>(prefixLineEnd));
    }
    for (std::size_t i = 0; i < fco && i < prev.variantRefs.size(); ++i) {
      out->variantRefs.push_back(prev.variantRefs[i]);
    }
    yCursor = lastLayout_.paraYCursors[fco];
    ctBase  = lastLayout_.paraCtBases[fco];
    // Seed maxDocWidth from memo — avoids re-scanning all prefix runs.
    maxDocWidth = lastLayout_.prevMaxDocWidth;

    // Dirty range: full assembly (updates maxDocWidth for new dirty paragraph).
    for (std::size_t pi = fc; pi < lce; ++pi) {
      if (!assembleParagraph(pi)) return std::make_shared<TextLayout const>();
    }

    // Suffix: bulk-copy from old layout with byte/Y/ctLine adjustments.
    float const deltaY = yCursor - lastLayout_.paraYCursors[lceo];
    std::int32_t const dCt =
        static_cast<std::int32_t>(ctBase) - static_cast<std::int32_t>(lastLayout_.paraCtBases[lceo]);
    std::uint32_t const sufRunStart  = lastLayout_.paraRunStarts[lceo];
    std::uint32_t const sufRunEnd    = lastLayout_.paraRunStarts.back();
    std::uint32_t const sufLineStart = lastLayout_.paraLineStarts[lceo];
    std::uint32_t const sufLineEnd   = lastLayout_.paraLineStarts.back();
    std::size_t   const sufRunBase   = out->runs.size();
    std::size_t   const sufLineBase  = out->lines.size();

    if (sufRunEnd > sufRunStart) {
      out->runs.insert(out->runs.end(),
          prev.runs.begin() + static_cast<std::ptrdiff_t>(sufRunStart),
          prev.runs.begin() + static_cast<std::ptrdiff_t>(sufRunEnd));
    }
    if (sufLineEnd > sufLineStart) {
      out->lines.insert(out->lines.end(),
          prev.lines.begin() + static_cast<std::ptrdiff_t>(sufLineStart),
          prev.lines.begin() + static_cast<std::ptrdiff_t>(sufLineEnd));
    }
    // Adjust suffix runs: apply Y, byte, ctLine deltas.
    // No maxDocWidth scan needed — suffix widths are unchanged from the previous layout.
    for (std::size_t i = sufRunBase; i < out->runs.size(); ++i) {
      out->runs[i].origin.y   += deltaY;
      out->runs[i].utf8Begin   = static_cast<std::uint32_t>(
          static_cast<std::int32_t>(out->runs[i].utf8Begin) + bd);
      out->runs[i].utf8End     = static_cast<std::uint32_t>(
          static_cast<std::int32_t>(out->runs[i].utf8End) + bd);
      out->runs[i].ctLineIndex = static_cast<std::uint32_t>(
          static_cast<std::int32_t>(out->runs[i].ctLineIndex) + dCt);
    }
    for (std::size_t i = sufLineBase; i < out->lines.size(); ++i) {
      out->lines[i].top       += deltaY;
      out->lines[i].bottom    += deltaY;
      out->lines[i].baseline  += deltaY;
      out->lines[i].byteStart += bd;
      out->lines[i].byteEnd   += bd;
      out->lines[i].ctLineIndex = static_cast<std::uint32_t>(
          static_cast<std::int32_t>(out->lines[i].ctLineIndex) + dCt);
    }
    for (std::size_t i = lceo; i < prev.variantRefs.size(); ++i) {
      out->variantRefs.push_back(prev.variantRefs[i]);
    }
    // Suffix per-paragraph index entries.
    for (std::size_t j = 0; lce + j < paraCount; ++j) {
      std::size_t const pi_new = lce + j;
      std::size_t const pi_old = lceo + j;
      newRunStarts[pi_new]  = static_cast<std::uint32_t>(sufRunBase) +
          (lastLayout_.paraRunStarts[pi_old]  - sufRunStart);
      newLineStarts[pi_new] = static_cast<std::uint32_t>(sufLineBase) +
          (lastLayout_.paraLineStarts[pi_old] - sufLineStart);
      newYCursors[pi_new]   = lastLayout_.paraYCursors[pi_old] + deltaY;
      newCtBases[pi_new]    = static_cast<std::uint32_t>(
          static_cast<std::int32_t>(lastLayout_.paraCtBases[pi_old]) + dCt);
    }
    yCursor = lastLayout_.paraYCursors.back() + deltaY;
    ctBase  = static_cast<std::uint32_t>(
        static_cast<std::int32_t>(lastLayout_.paraCtBases.back()) + dCt);

  } else {
    // Full (non-incremental) assembly.
    for (std::size_t pi = 0; pi < paraCount; ++pi) {
      if (!assembleParagraph(pi)) return std::make_shared<TextLayout const>();
    }
  }

  // Terminating sentinel entries.
  newRunStarts[paraCount]  = static_cast<std::uint32_t>(out->runs.size());
  newLineStarts[paraCount] = static_cast<std::uint32_t>(out->lines.size());
  newYCursors[paraCount]   = yCursor;
  newCtBases[paraCount]    = ctBase;

  out->measuredSize = {maxDocWidth, yCursor};

  if (canIncrAssemble && !out->runs.empty()) {
    // Incremental path: derive firstBaseline/lastBaseline from the memo without an O(N) scan.
    //
    // firstBaseline = min_baseline_y - min_top, where min values are from the first line.
    // If the prefix is non-empty (fc > 0), the first paragraph is unchanged → reuse memo.
    // If the prefix is empty (fc == 0), the first line may have new metrics → recompute.
    //
    // lastBaseline = last run's baseline - min_top.
    // If the suffix is non-empty (lce < paraCount), the last line came from the suffix, which was
    // shifted uniformly by heightDelta → lastBaseline_new = lastBaseline_old + heightDelta.
    // If the suffix is empty (lce == paraCount), the last line is from the dirty range → recompute.
    bool const prefixNonEmpty = (incr->firstChanged > 0);
    bool const suffixNonEmpty = (incr->lastChangedExcl < paraCount);
    if (prefixNonEmpty && suffixNonEmpty) {
      float const prevTotalH  = lastLayout_.paraYCursors.back();
      float const heightDelta = yCursor - prevTotalH;
      out->firstBaseline = lastLayout_.prevFirstBaseline;
      out->lastBaseline  = lastLayout_.layout->lastBaseline + heightDelta;
    } else {
      recomputeTextLayoutMetrics(*out);
    }
  } else {
    recomputeTextLayoutMetrics(*out);
  }

#if defined(LAMBDAUI_DISABLE_VARIANT_REFS)
  // Deep-copy glyphs into owned storage (rollback when variant reference-counting is disabled).
  std::shared_ptr<TextLayout const> const result = cloneTextLayout(*out);
#else
  std::shared_ptr<TextLayout const> const result = std::const_pointer_cast<TextLayout const>(out);
#endif

#if defined(LAMBDAUI_PARAGRAPH_CACHE_PARALLEL_ASSERT) && !defined(NDEBUG)
  if (!noMemoSideEffects && canIncrAssemble && incr != nullptr) {
    std::string dump;
    auto const ref =
        layoutViaParagraphCache(sys, text, maxWidth, options, resolved, std::move(parallelRefParagraphs), nullptr,
                                true);
    if (ref && !detail::paragraphCacheLayoutsStructurallyEqual(*result, *ref, &dump)) {
      std::fprintf(stderr, "paragraph cache parallel assert mismatch:\n%s\n", dump.c_str());
      assert(false && "paragraph cache parallel assert");
    }
  }
#endif

  if (!noMemoSideEffects) {
    // keyHi/keyLo already written in the non-incremental branch above (or zeroed for incremental).
    lastLayout_.layout = result;

    // Persist per-paragraph tables for the next incremental assembly.
    lastLayout_.prevParagraphs.assign(paragraphs.begin(), paragraphs.end());
    lastLayout_.paraRunStarts  = std::move(newRunStarts);
    lastLayout_.paraLineStarts = std::move(newLineStarts);
    lastLayout_.paraYCursors   = std::move(newYCursors);
    lastLayout_.paraCtBases    = std::move(newCtBases);
  }

  return result;
}


std::vector<std::uint8_t> CoreTextSystem::rasterizeGlyph(std::uint32_t fontId, std::uint32_t glyphId,
                                                        float size, std::uint32_t& outWidth,
                                                        std::uint32_t& outHeight, Point& outBearing) {
  outWidth = 0;
  outHeight = 0;
  outBearing = {0, 0};
  std::uint32_t const sizeQ8 = static_cast<std::uint32_t>(std::lround(size * 4.f));
  CTFontRef font = d->sizedFont(fontId, sizeQ8);
  if (!font) {
    return {};
  }
  CGGlyph g = static_cast<CGGlyph>(glyphId);
  CGRect bounds = CGRectZero;
  CTFontGetBoundingRectsForGlyphs(font, kCTFontOrientationHorizontal, &g, &bounds, 1);
  if (bounds.size.width <= 0 || bounds.size.height <= 0) {
    return {};
  }

  CGFloat const pad = kPadPx;
  CGFloat const ascent = CTFontGetAscent(font);
  CGFloat const descent = CTFontGetDescent(font);
  CGFloat const glyphTop = bounds.origin.y + bounds.size.height;
  double const minBoxH = static_cast<double>(pad + ascent + descent + pad);
  std::uint32_t const bw =
      static_cast<std::uint32_t>(std::ceil(static_cast<double>(bounds.size.width + pad * 2.f)));
  std::uint32_t const bh = static_cast<std::uint32_t>(
      std::max(std::ceil(static_cast<double>(bounds.size.height + pad * 2.f)), std::ceil(minBoxH)));
  if (bw == 0 || bh == 0) {
    return {};
  }

  std::vector<std::uint8_t> grayBuf(static_cast<std::size_t>(bw) * bh);
  CfPtr<CGColorSpaceRef> grayCs(CGColorSpaceCreateDeviceGray());
  CfPtr<CGContextRef> ctx(
      CGBitmapContextCreate(grayBuf.data(), bw, bh, 8, bw, grayCs.get(), kCGImageAlphaNone));
  if (!ctx) {
    return {};
  }

  CGContextRef const ctxRef = ctx.get();
  CGContextSetShouldAntialias(ctxRef, true);
  CGContextSetShouldSmoothFonts(ctxRef, true);
  CGContextSetGrayFillColor(ctxRef, 1, 1);
  CGContextFillRect(ctxRef, CGRectMake(0, 0, bw, bh));
  CGContextSetGrayFillColor(ctxRef, 0, 1);

  CGContextSetTextMatrix(ctxRef, CGAffineTransformIdentity);
  CGFloat const ox = static_cast<CGFloat>(pad) - bounds.origin.x;
  CGFloat const baselineY = static_cast<CGFloat>(bh) - pad - glyphTop;
  CGContextSetTextPosition(ctxRef, ox, baselineY);
  CGPoint const z = CGPointZero;
  CTFontDrawGlyphs(font, &g, &z, 1, ctxRef);

  std::vector<std::uint8_t> r8(static_cast<std::size_t>(bw) * bh);
  for (std::uint32_t row = 0; row < bh; ++row) {
    std::uint8_t const* src = grayBuf.data() + static_cast<std::size_t>(row) * bw;
    std::uint8_t* dst = r8.data() + static_cast<std::size_t>(row) * bw;
    for (std::uint32_t col = 0; col < bw; ++col) {
      dst[col] = static_cast<std::uint8_t>(255 - src[col]);
    }
  }

  outWidth = bw;
  outHeight = bh;
  outBearing.x = static_cast<float>(ox);
  outBearing.y = static_cast<float>(pad + glyphTop);
  return r8;
}

Size CoreTextSystem::measure(AttributedString const& text, float maxWidth, TextLayoutOptions const& options) {
  if (text.utf8.empty()) {
    return {};
  }
  if (options.maxLines > 0) {
    std::shared_ptr<TextLayout const> const L = layout(text, maxWidth, options);
    return L ? L->measuredSize : Size{};
  }
  validateRuns(text);
  std::vector<ResolvedStyle> resolved;
  accumulateInheritance(resolved, text);
  ContentHash const h = d->computeContentHash(*this, text, resolved, options);
  std::uint32_t const wq = quantizeWidth(maxWidth);
  std::int32_t const ml = options.maxLines;

  FramesetterEntry& e = d->findOrInsertFramesetterEntry(h, text, resolved, options, *this);

  if (Size const* cached = d->findMeasureSlot(e, wq, ml)) {
    return *cached;
  }
  Size const sz = measureWithFramesetter(e.framesetter, maxWidth);
  d->insertMeasureSlot(e, wq, ml, sz);
  return sz;
}

Size CoreTextSystem::measure(std::string_view utf8, Font const& font, Color const& color, float maxWidth,
                             TextLayoutOptions const& options) {
  if (utf8.empty()) {
    return {};
  }
  if (options.maxLines > 0) {
    std::shared_ptr<TextLayout const> const L = layout(utf8, font, color, maxWidth, options);
    return L ? L->measuredSize : Size{};
  }
  ContentHash const h = d->computeContentHashPlain(*this, utf8, font, color, options);
  std::uint32_t const wq = quantizeWidth(maxWidth);
  std::int32_t const ml = options.maxLines;

  FramesetterEntry& e = [&]() -> FramesetterEntry& {
    auto it = d->frameMap_.find(h);
    if (it != d->frameMap_.end()) {
      if (!options.suppressCacheStats) {
        ++d->stats_.l2_framesetter.hits;
      }
      it->second->lastTouchFrame = d->currentFrame_;
      return *it->second;
    }
    AttributedString as;
    as.utf8 = std::string(utf8);
    as.runs.push_back({.start = 0,
                       .end = static_cast<std::uint32_t>(utf8.size()),
                       .font = font,
                       .color = color});
    lambdaui::detail::SmallVector<ResolvedStyle, 4> resolved;
    accumulateInheritance(resolved, as);
    return d->insertFramesetterMiss(h, as, {resolved.data(), resolved.size()}, options, *this);
  }();

  if (Size const* cached = d->findMeasureSlot(e, wq, ml)) {
    return *cached;
  }
  Size const sz = measureWithFramesetter(e.framesetter, maxWidth);
  d->insertMeasureSlot(e, wq, ml, sz);
  return sz;
}

std::shared_ptr<TextLayout const> CoreTextSystem::layoutUnboxed(AttributedString const& text,
                                                                TextLayoutOptions const& options,
                                                                float maxWidth, bool hasPrecomputedHash,
                                                                std::uint64_t preHi, std::uint64_t preLo) {
  if (text.utf8.empty()) {
    validateRuns(text);
    std::vector<ResolvedStyle> resolved;
    accumulateInheritance(resolved, text);
    ContentHash const h =
        hasPrecomputedHash ? ContentHash{preHi, preLo} : d->computeContentHash(*this, text, resolved, options);
    FramesetterEntry& e = d->findOrInsertFramesetterEntry(h, text, resolved, options, *this);
    std::uint32_t const wq = quantizeWidth(maxWidth);
    std::int32_t const ml = options.maxLines;

    if (auto cached = d->findLayoutSlot(e, wq, maxWidth, ml, options.suppressCacheStats)) {
      return cached;
    }

    // Empty UTF-8 yields no CT lines from the framesetter; shape a one-space probe with the same
    // styles/options, then drop runs so nothing is drawn but line metrics match typed text.
    std::shared_ptr<TextLayout> builtProbe = std::make_shared<TextLayout>();
    if (!resolved.empty()) {
      AttributedString probe;
      probe.utf8 = " ";
      probe.runs.push_back({.start = 0, .end = 1, .font = resolved[0].font, .color = resolved[0].color});
      std::vector<ResolvedStyle> probeResolved;
      accumulateInheritance(probeResolved, probe);
      CfPtr<CFAttributedStringRef> cf(d->createCFAttributed(*this, probe, probeResolved, options));
      CfPtr<CTFramesetterRef> fs(CTFramesetterCreateWithAttributedString(cf.get()));
      auto stor = std::make_unique<TextLayoutStorage>();
      fillTextLayoutFromFramesetter(*this, fs.get(), probe, maxWidth, options, *builtProbe, *stor);
      builtProbe->runs.clear();
      builtProbe->variantRefs.clear();
      builtProbe->ownedStorage.reset();
      for (auto& lr : builtProbe->lines) {
        lr.byteStart = 0;
        lr.byteEnd = 0;
      }
      recomputeTextLayoutMetrics(*builtProbe);
    }
    std::shared_ptr<TextLayout const> result = std::const_pointer_cast<TextLayout const>(builtProbe);
    d->insertLayoutSlot(e, text, wq, maxWidth, ml, result);
    return result;
  }
  validateRuns(text);
  std::vector<ResolvedStyle> resolved;
  accumulateInheritance(resolved, text);
  ContentHash const h =
      hasPrecomputedHash ? ContentHash{preHi, preLo} : d->computeContentHash(*this, text, resolved, options);
  FramesetterEntry& e = d->findOrInsertFramesetterEntry(h, text, resolved, options, *this);
  std::uint32_t const wq = quantizeWidth(maxWidth);
  std::int32_t const ml = options.maxLines;

  if (auto cached = d->findLayoutSlot(e, wq, maxWidth, ml, options.suppressCacheStats)) {
    return cached;
  }

  auto built = std::make_shared<TextLayout>();
  auto stor = std::make_unique<TextLayoutStorage>();
  fillTextLayoutFromFramesetter(*this, e.framesetter, text, maxWidth, options, *built, *stor);
  built->ownedStorage = std::move(stor);
  std::shared_ptr<TextLayout const> result = std::const_pointer_cast<TextLayout const>(built);
  d->insertLayoutSlot(e, text, wq, maxWidth, ml, result);
  return result;
}

std::shared_ptr<TextLayout const> CoreTextSystem::layout(std::string_view utf8, Font const& font,
                                                         Color const& color, float maxWidth,
                                                         TextLayoutOptions const& options) {
  debug::perf::recordTextLayoutCall();
  std::uint32_t const wq = quantizeWidth(maxWidth);

  // --- Tier 0: pointer-identity exact-repeat (same backing buffer, same width) ---
  if (d->lastLayout_.layout && wq == d->lastLayout_.contentWidthQ1 &&
      utf8.data() == d->lastLayout_.utf8Ptr &&
      utf8.size() == d->lastLayout_.utf8Size) {
    if (!options.suppressCacheStats) {
      ++d->memoStats_.hits;
    }
    debug::perf::recordTextLayoutCacheHit();
    return d->lastLayout_.layout;
  }

  if (utf8.empty()) {
    AttributedString as;
    as.utf8 = "";
    as.runs.push_back({.start = 0, .end = 0, .font = font, .color = color});
    return layoutUnboxed(as, options, maxWidth, false, 0, 0);
  }
  ContentHash const h = d->computeContentHashPlain(*this, utf8, font, color, options);
  std::int32_t const ml = options.maxLines;

  FramesetterEntry& e = [&]() -> FramesetterEntry& {
    auto it = d->frameMap_.find(h);
    if (it != d->frameMap_.end()) {
      if (!options.suppressCacheStats) {
        ++d->stats_.l2_framesetter.hits;
      }
      it->second->lastTouchFrame = d->currentFrame_;
      return *it->second;
    }
    AttributedString as;
    as.utf8 = std::string(utf8);
    as.runs.push_back({.start = 0,
                       .end = static_cast<std::uint32_t>(utf8.size()),
                       .font = font,
                       .color = color});
    lambdaui::detail::SmallVector<ResolvedStyle, 4> resolved;
    accumulateInheritance(resolved, as);
    return d->insertFramesetterMiss(h, as, {resolved.data(), resolved.size()}, options, *this);
  }();

  if (auto cached = d->findLayoutSlot(e, wq, maxWidth, ml, options.suppressCacheStats)) {
    debug::perf::recordTextLayoutCacheHit();
    return cached;
  }
  debug::perf::recordTextLayoutCacheMiss();

  AttributedString as;
  as.utf8 = std::string(utf8);
  as.runs.push_back({.start = 0,
                     .end = static_cast<std::uint32_t>(utf8.size()),
                     .font = font,
                     .color = color});
  auto built = std::make_shared<TextLayout>();
  auto stor2 = std::make_unique<TextLayoutStorage>();
  fillTextLayoutFromFramesetter(*this, e.framesetter, as, maxWidth, options, *built, *stor2);
  built->ownedStorage = std::move(stor2);
  std::shared_ptr<TextLayout const> result = std::const_pointer_cast<TextLayout const>(built);
  d->insertLayoutSlot(e, as, wq, maxWidth, ml, result);
  return result;
}

std::shared_ptr<TextLayout const> CoreTextSystem::layout(AttributedString const& text, float maxWidth,
                                                         TextLayoutOptions const& options) {
  debug::perf::recordTextLayoutCall();
  std::uint32_t const wq = quantizeWidth(maxWidth);

  // --- Tier 0: pointer-identity exact-repeat (same string object, same width) ---
  if (d->lastLayout_.layout && wq == d->lastLayout_.contentWidthQ1) {
    bool const samePtr = (text.utf8.data() == d->lastLayout_.utf8Ptr &&
                          text.utf8.size() == d->lastLayout_.utf8Size);
    if (samePtr) {
      if (!options.suppressCacheStats) ++d->memoStats_.hits;
      debug::perf::recordTextLayoutCacheHit();
      return d->lastLayout_.layout;
    }
  }

  validateRuns(text);
  std::vector<ResolvedStyle> resolved;
  accumulateInheritance(resolved, text);

  // Helper: update the memo after a successful layout.
  // chComputed=true  → also update contentHash (caller already has it).
  // chComputed=false → caller has no hash (incremental path); leave contentHash stale so Tier-2
  //                    still re-derives it on the next same-width miss — that is correct.
  auto updateMemoIds = [&](std::shared_ptr<TextLayout const> const& result,
                           ContentHash const& ch, bool chComputed) {
    if (!result) return;
    d->lastLayout_.contentWidthQ1 = wq;
    if (chComputed) {
      d->lastLayout_.contentHash = ch;
    }
    d->lastLayout_.utf8Ptr  = text.utf8.data();
    d->lastLayout_.utf8Size = text.utf8.size();
    d->lastLayout_.prevUtf8 = text.utf8;
    d->lastLayout_.prevRuns.assign(text.runs.begin(), text.runs.end());
    if (result) {
      d->lastLayout_.prevMaxDocWidth  = result->measuredSize.width;
      d->lastLayout_.prevFirstBaseline = result->firstBaseline;
    }
  };

  // --- Tier 1: incremental paragraph split + assembly ---
  // Bypass content-hash and full splitIntoParagraphs on single-keystroke changes.
  if (d->lastLayout_.layout &&
      !d->lastLayout_.prevParagraphs.empty() &&
      !d->lastLayout_.prevUtf8.empty() &&
      !paragraphCacheDisabledByEnv() &&
      text.utf8.size() >= kMinFastPathBytes &&
      options.wrapping != TextWrapping::WrapAnywhere &&
      options.maxLines == 0) {
    if (auto incr = d->tryIncrementalSplit(*this, text, resolved, options)) {
      auto parasCopy = incr->paragraphs; // SmallVector copy; incr still valid for the pointer
      debug::perf::recordTextLayoutCacheMiss();
      auto result = d->layoutViaParagraphCache(*this, text, maxWidth, options, resolved,
          std::move(parasCopy), &*incr);
      updateMemoIds(result, ContentHash{}, false);
      return result;
    }
  }

  // --- Tier 2: content-hash exact-repeat check ---
  if (d->lastLayout_.layout && wq == d->lastLayout_.contentWidthQ1) {
    ContentHash const ch = d->computeContentHash(*this, text, resolved, options);
    if (ch == d->lastLayout_.contentHash) {
      if (!options.suppressCacheStats) ++d->memoStats_.hits;
      debug::perf::recordTextLayoutCacheHit();
      // Update pointer identity for future Tier-0 hits.
      d->lastLayout_.utf8Ptr  = text.utf8.data();
      d->lastLayout_.utf8Size = text.utf8.size();
      return d->lastLayout_.layout;
    }
    // Cache miss at same width.
    if (paragraphCachePredicate(text, options)) {
      debug::perf::recordTextLayoutCacheMiss();
      auto paras = d->splitIntoParagraphs(*this, text, resolved, options);
      auto result = d->layoutViaParagraphCache(*this, text, maxWidth, options, resolved,
                                               std::move(paras));
      updateMemoIds(result, ch, true);
      return result;
    }
    debug::perf::recordTextLayoutCacheMiss();
    return layoutUnboxed(text, options, maxWidth, true, ch.hi, ch.lo);
  }

  // --- Tier 3: cold path (different width or no memo) ---
  if (paragraphCachePredicate(text, options)) {
    ContentHash const ch = d->computeContentHash(*this, text, resolved, options);
    debug::perf::recordTextLayoutCacheMiss();
    auto paras = d->splitIntoParagraphs(*this, text, resolved, options);
    auto result = d->layoutViaParagraphCache(*this, text, maxWidth, options, resolved,
                                             std::move(paras));
    updateMemoIds(result, ch, true);
    return result;
  }
  debug::perf::recordTextLayoutCacheMiss();
  return layoutUnboxed(text, options, maxWidth, false, 0, 0);
}

std::shared_ptr<TextLayout const> CoreTextSystem::layoutBoxedImpl(AttributedString const& text, Rect const& box,
                                                                  TextLayoutOptions const& options) {
  debug::perf::recordTextLayoutCall();
  float const maxWidth = options.wrapping == TextWrapping::NoWrap ? 0.f : box.width;
  validateRuns(text);
  std::vector<ResolvedStyle> resolved;
  accumulateInheritance(resolved, text);
  ContentHash const h = d->computeContentHash(*this, text, resolved, options);
  std::shared_ptr<TextLayout const> base = layoutUnboxed(text, options, maxWidth, true, h.hi, h.lo);
  if (!base) {
    return nullptr;
  }
  std::uint32_t const wq = quantizeWidth(maxWidth);
  std::int32_t const ml = options.maxLines;
  std::uint32_t const boxWQ = quantizeWidth(box.width);
  std::uint32_t const boxHQ = quantizeWidth(box.height);
  std::uint8_t const ha = static_cast<std::uint8_t>(options.horizontalAlignment);
  std::uint8_t const va = static_cast<std::uint8_t>(options.verticalAlignment);
  std::uint16_t const fbq = quantizeFirstBaseline(options.firstBaselineOffset);

  auto it = d->frameMap_.find(h);
  assert(it != d->frameMap_.end() && "layoutUnboxed must have inserted the framesetter entry");
  FramesetterEntry& e = *it->second;

  LayoutSlot* layoutSlot = nullptr;
  for (std::size_t li = 0; li < e.layouts.size(); ++li) {
    LayoutSlot& ls = e.layouts[li];
    if (ls.maxWidthQ1 == wq && ls.maxLines == ml && std::abs(ls.maxWidthExact - maxWidth) <= 1e-5f) {
      layoutSlot = &ls;
      break;
    }
  }
  assert(layoutSlot && "layoutUnboxed must have populated a matching LayoutSlot");
  LayoutSlot& ls = *layoutSlot;

  for (std::size_t bi = 0; bi < ls.boxes.size(); ++bi) {
    BoxSlot& bs = ls.boxes[bi];
    if (bs.boxWQ1 == boxWQ && bs.boxHQ1 == boxHQ && bs.hAlign == ha && bs.vAlign == va &&
        bs.firstBaselineQ8 == fbq && bs.layout && bs.boxW == box.width && bs.boxH == box.height) {
      if (!options.suppressCacheStats) {
        ++d->stats_.l4_boxLayout.hits;
      }
      debug::perf::recordTextLayoutCacheHit();
      return bs.layout;
    }
  }
  if (!options.suppressCacheStats) {
    ++d->stats_.l4_boxLayout.misses;
  }
  debug::perf::recordTextLayoutCacheMiss();
  std::shared_ptr<TextLayout> mut = cloneTextLayout(*base);
  detail::applyBoxOptions(*mut, box, options);
  auto boxed = std::shared_ptr<TextLayout const>(mut);
  ls.boxes.push_back(BoxSlot{
      .boxWQ1 = boxWQ,
      .boxHQ1 = boxHQ,
      .boxW = box.width,
      .boxH = box.height,
      .hAlign = ha,
      .vAlign = va,
      .firstBaselineQ8 = fbq,
      .layout = boxed,
  });
  d->bumpEntryApproxBytes(e, text);
  return boxed;
}

void CoreTextSystem::onFrameBegin(std::uint64_t frameIndex) {
  d->currentFrame_ = frameIndex;
  d->paragraphCache_.onFrameBegin(frameIndex);
}

void CoreTextSystem::onFrameEnd() {
  while (d->frameMapBytes_ > d->budgetBytes_) {
    std::vector<std::pair<std::uint64_t, ContentHash>> candidates;
    candidates.reserve(d->frameMap_.size());
    for (auto const& p : d->frameMap_) {
      if (p.second->lastTouchFrame >= d->currentFrame_) {
        continue;
      }
      candidates.push_back({p.second->lastTouchFrame, p.first});
    }
    if (candidates.empty()) {
      break;
    }
    std::sort(candidates.begin(), candidates.end(),
              [](auto const& a, auto const& b) { return a.first < b.first; });
    for (auto const& c : candidates) {
      if (d->frameMapBytes_ <= d->budgetBytes_) {
        break;
      }
      auto it = d->frameMap_.find(c.second);
      if (it == d->frameMap_.end()) {
        continue;
      }
      if (it->second->lastTouchFrame >= d->currentFrame_) {
        continue;
      }
      d->frameMapBytes_ -= it->second->approxBytes;
      d->releaseFramesetterEntry(*it->second);
      d->frameMap_.erase(it);
      ++d->stats_.l2_framesetter.evictions;
    }
  }
  d->stats_.l2_framesetter.currentBytes = d->frameMapBytes_;
  if (d->frameMapBytes_ > d->stats_.l2_framesetter.peakBytes) {
    d->stats_.l2_framesetter.peakBytes = d->frameMapBytes_;
  }

  std::vector<ParagraphHash> const evictedPara =
      d->paragraphCache_.onFrameEnd(d->paragraphCacheBudgetBytes_);
  if (!evictedPara.empty()) {
    d->lastLayout_ = {};
    ++d->memoStats_.evictions;
  }
}

void CoreTextSystem::invalidateAll() {
  d->paragraphCache_.invalidateAll();
  d->lastLayout_ = {};
  for (auto& p : d->frameMap_) {
    d->releaseFramesetterEntry(*p.second);
  }
  d->frameMap_.clear();
  d->frameMapBytes_ = 0;
}

void CoreTextSystem::invalidateForFontChange(std::span<std::uint32_t const> fontIds) {
  std::vector<ParagraphHash> const removedPara = d->paragraphCache_.invalidateForFontChange(fontIds);
  if (!removedPara.empty()) {
    d->lastLayout_ = {};
    ++d->memoStats_.evictions;
  }

  std::unordered_set<std::uint32_t> idset(fontIds.begin(), fontIds.end());
  std::vector<ContentHash> toErase;
  for (auto const& p : d->frameMap_) {
    for (std::uint32_t fid : p.second->fontIds) {
      if (idset.count(fid)) {
        toErase.push_back(p.first);
        break;
      }
    }
  }
  for (ContentHash const& h : toErase) {
    auto it = d->frameMap_.find(h);
    if (it != d->frameMap_.end()) {
      d->frameMapBytes_ -= it->second->approxBytes;
      d->releaseFramesetterEntry(*it->second);
      d->frameMap_.erase(it);
    }
  }
}

TextCacheStats CoreTextSystem::stats() const {
  TextCacheStats s = d->stats_;
  s.l2_5_paragraph = d->paragraphCache_.stats();
  s.l2_5_variant = d->paragraphCache_.variantStats();
  s.l2_5_memo = d->memoStats_;
  return s;
}

void CoreTextSystem::setParagraphCacheBudget(std::size_t bytes) {
  d->paragraphCacheBudgetBytes_ = bytes;
  d->paragraphCache_.setBudget(bytes);
}

namespace detail {

std::shared_ptr<TextLayout const> paragraphCacheFullAssemblyForTest(
    CoreTextSystem& sys, AttributedString const& text, float maxWidth, TextLayoutOptions const& options) {
  validateRuns(text);
  if (!paragraphCachePredicate(text, options)) {
    return nullptr;
  }
  std::vector<ResolvedStyle> resolved;
  accumulateInheritance(resolved, text);
  auto paras = sys.d->splitIntoParagraphs(sys, text, resolved, options);
  return sys.d->layoutViaParagraphCache(sys, text, maxWidth, options, resolved, std::move(paras), nullptr,
                                        true);
}

} // namespace detail

} // namespace lambdaui
