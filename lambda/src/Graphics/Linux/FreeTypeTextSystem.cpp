#include "Graphics/Linux/FreeTypeTextSystem.hpp"

#include <Lambda/Debug/PerfCounters.hpp>
#include <Lambda/Graphics/AttributedString.hpp>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <fontconfig/fontconfig.h>
#include <hb-ft.h>
#include <hb.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits.h>
#include <list>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lambda {

namespace {

struct Codepoint {
  char32_t value = 0;
  std::uint32_t byteBegin = 0;
  std::uint32_t byteEnd = 0;
};

std::vector<Codepoint> decodeUtf8(std::string_view text) {
  std::vector<Codepoint> out;
  for (std::uint32_t i = 0; i < text.size();) {
    unsigned char c = static_cast<unsigned char>(text[i]);
    char32_t cp = c;
    std::uint32_t len = 1;
    if ((c & 0xE0u) == 0xC0u && i + 1 < text.size()) {
      cp = ((c & 0x1Fu) << 6u) | (static_cast<unsigned char>(text[i + 1]) & 0x3Fu);
      len = 2;
    } else if ((c & 0xF0u) == 0xE0u && i + 2 < text.size()) {
      cp = ((c & 0x0Fu) << 12u) | ((static_cast<unsigned char>(text[i + 1]) & 0x3Fu) << 6u) |
           (static_cast<unsigned char>(text[i + 2]) & 0x3Fu);
      len = 3;
    } else if ((c & 0xF8u) == 0xF0u && i + 3 < text.size()) {
      cp = ((c & 0x07u) << 18u) | ((static_cast<unsigned char>(text[i + 1]) & 0x3Fu) << 12u) |
           ((static_cast<unsigned char>(text[i + 2]) & 0x3Fu) << 6u) |
           (static_cast<unsigned char>(text[i + 3]) & 0x3Fu);
      len = 4;
    }
    out.push_back(Codepoint{cp, i, static_cast<std::uint32_t>(std::min<std::size_t>(text.size(), i + len))});
    i += len;
  }
  return out;
}

Font resolvedFont(Font f) {
  if (f.size <= 0.f) {
    f.size = 16.f;
  }
  if (f.weight <= 0.f) {
    f.weight = 400.f;
  }
  return f;
}

std::filesystem::path executableDir() {
  std::array<char, PATH_MAX> buffer{};
  ssize_t const n = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
  if (n <= 0) {
    return {};
  }
  buffer[static_cast<std::size_t>(n)] = '\0';
  return std::filesystem::path(buffer.data()).parent_path();
}

std::string firstExisting(std::initializer_list<std::filesystem::path> paths) {
  for (std::filesystem::path const& p : paths) {
    if (std::filesystem::exists(p)) {
      return p.string();
    }
  }
  return {};
}

std::string findFontPathFc(std::string_view family, float weight, bool italic, std::optional<char32_t> codepoint) {
  FcPattern* pattern = FcPatternCreate();
  if (!pattern) return {};

  std::string const familyName = family.empty() || family == "System" || family == ".System"
                                     ? "sans-serif"
                                     : std::string(family);
  FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<FcChar8 const*>(familyName.c_str()));
  FcPatternAddInteger(pattern, FC_WEIGHT, weight >= 700.f ? FC_WEIGHT_BOLD
                                                          : weight >= 600.f ? FC_WEIGHT_SEMIBOLD
                                                                            : FC_WEIGHT_REGULAR);
  FcPatternAddInteger(pattern, FC_SLANT, italic ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
  if (codepoint) {
    FcCharSet* charset = FcCharSetCreate();
    if (charset) {
      FcCharSetAddChar(charset, static_cast<FcChar32>(*codepoint));
      FcPatternAddCharSet(pattern, FC_CHARSET, charset);
      FcCharSetDestroy(charset);
    }
  }

  FcConfigSubstitute(nullptr, pattern, FcMatchPattern);
  FcDefaultSubstitute(pattern);
  FcResult result = FcResultNoMatch;
  FcPattern* match = FcFontMatch(nullptr, pattern, &result);
  FcPatternDestroy(pattern);
  if (!match) return {};
  FcChar8* file = nullptr;
  std::string path;
  if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch && file) {
    path = reinterpret_cast<char const*>(file);
  }
  FcPatternDestroy(match);
  return path;
}

std::string findFontPath(std::string_view family, float weight, bool italic,
                         std::string const& appName) {
  std::filesystem::path const exeDir = executableDir();
  if (family == "Material Symbols Rounded") {
    std::string path = firstExisting({
        exeDir / "fonts/MaterialSymbolsRounded.ttf",
        exeDir / "../share" / appName / "fonts/MaterialSymbolsRounded.ttf",
        exeDir / "../share/lambda/fonts/MaterialSymbolsRounded.ttf",
        std::filesystem::current_path() / "lambda/resources/fonts/MaterialSymbolsRounded.ttf",
        std::filesystem::current_path() / "fonts/MaterialSymbolsRounded.ttf",
        std::filesystem::current_path() / "../fonts/MaterialSymbolsRounded.ttf",
    });
    if (!path.empty()) return path;
  }
  return findFontPathFc(family, weight, italic, std::nullopt);
}

std::string findFontPathForChar(std::string_view family, float weight, bool italic, char32_t ch) {
  return findFontPathFc(family, weight, italic, ch);
}

} // namespace

struct FreeTypeTextSystem::Impl {
  struct ShapedGlyph {
    std::uint32_t fontId = 0;
    float fontSize = 0.f;
    Color color{};
    float ascent = 0.f;
    float descent = 0.f;
    float lineHeight = 0.f;
    std::uint32_t glyphId = 0;
    float advance = 0.f;
    float offsetX = 0.f;
    float offsetY = 0.f;
    std::uint32_t byteBegin = 0;
    std::uint32_t byteEnd = 0;
  };

  struct ShapedParagraph {
    std::vector<ShapedGlyph> glyphs;
    float ascent = 0.f;
    float descent = 0.f;
    float lineHeight = 0.f;
  };

  std::function<std::string()> appNameProvider;
  FT_Library library = nullptr;
  std::vector<FT_Face> faces;
  std::vector<hb_font_t*> hbFonts;
  std::unordered_map<std::string, std::uint32_t> ids;
  std::unordered_map<std::string, std::uint32_t> idsByPath;
  struct LayoutCacheEntry {
    std::shared_ptr<TextLayout const> layout;
    std::list<std::string>::iterator order;
  };
  struct ShapedCacheEntry {
    std::shared_ptr<ShapedParagraph const> paragraph;
    std::list<std::string>::iterator order;
  };
  std::unordered_map<std::string, LayoutCacheEntry> layoutCache;
  std::list<std::string> layoutCacheOrder;
  std::unordered_map<std::string, ShapedCacheEntry> shapedCache;
  std::list<std::string> shapedCacheOrder;

  Impl() {
    if (!FcInit()) {
      throw std::runtime_error("Fontconfig initialization failed");
    }
    if (FT_Init_FreeType(&library) != 0) {
      throw std::runtime_error("FreeType initialization failed");
    }
  }

  ~Impl() {
    for (hb_font_t* font : hbFonts) {
      hb_font_destroy(font);
    }
    for (FT_Face face : faces) {
      FT_Done_Face(face);
    }
    if (library) {
      FT_Done_FreeType(library);
    }
    FcFini();
  }

  FT_Face face(std::uint32_t id) const {
    if (id >= faces.size()) {
      return faces.empty() ? nullptr : faces.front();
    }
    return faces[id];
  }

  hb_font_t* hbFont(std::uint32_t id) const {
    if (id >= hbFonts.size()) {
      return hbFonts.empty() ? nullptr : hbFonts.front();
    }
    return hbFonts[id];
  }

  std::uint32_t loadPath(std::string const& path, std::string const& key) {
    if (auto it = idsByPath.find(path); it != idsByPath.end()) {
      ids[key] = it->second;
      return it->second;
    }
    FT_Face face = nullptr;
    if (FT_New_Face(library, path.c_str(), 0, &face) != 0) {
      throw std::runtime_error("Failed to load font: " + path);
    }
    std::uint32_t const id = static_cast<std::uint32_t>(faces.size());
    faces.push_back(face);
    hbFonts.push_back(hb_ft_font_create_referenced(face));
    idsByPath[path] = id;
    ids[key] = id;
    return id;
  }

  void cacheLayout(std::string key, std::shared_ptr<TextLayout const> layout) {
    constexpr std::size_t kMaxLayoutCacheEntries = 1024;
    if (auto it = layoutCache.find(key); it != layoutCache.end()) {
      it->second.layout = std::move(layout);
      promoteLayout(it);
      return;
    }
    layoutCacheOrder.push_back(key);
    auto orderIt = std::prev(layoutCacheOrder.end());
    layoutCache.emplace(std::move(key), LayoutCacheEntry{std::move(layout), orderIt});
    while (layoutCache.size() > kMaxLayoutCacheEntries) {
      layoutCache.erase(layoutCacheOrder.front());
      layoutCacheOrder.pop_front();
    }
  }

  void promoteLayout(std::unordered_map<std::string, LayoutCacheEntry>::iterator it) {
    if (it == layoutCache.end()) return;
    layoutCacheOrder.splice(layoutCacheOrder.end(), layoutCacheOrder, it->second.order);
  }

  void cacheShaped(std::string key, std::shared_ptr<ShapedParagraph const> paragraph) {
    constexpr std::size_t kMaxShapedCacheEntries = 256;
    if (auto it = shapedCache.find(key); it != shapedCache.end()) {
      it->second.paragraph = std::move(paragraph);
      promoteShaped(it);
      return;
    }
    shapedCacheOrder.push_back(key);
    auto orderIt = std::prev(shapedCacheOrder.end());
    shapedCache.emplace(std::move(key), ShapedCacheEntry{std::move(paragraph), orderIt});
    while (shapedCache.size() > kMaxShapedCacheEntries) {
      shapedCache.erase(shapedCacheOrder.front());
      shapedCacheOrder.pop_front();
    }
  }

  void promoteShaped(std::unordered_map<std::string, ShapedCacheEntry>::iterator it) {
    if (it == shapedCache.end()) return;
    shapedCacheOrder.splice(shapedCacheOrder.end(), shapedCacheOrder, it->second.order);
  }
};

FreeTypeTextSystem::FreeTypeTextSystem(std::function<std::string()> appNameProvider)
    : d(std::make_unique<Impl>()) {
  d->appNameProvider = std::move(appNameProvider);
}
FreeTypeTextSystem::~FreeTypeTextSystem() = default;

std::uint32_t FreeTypeTextSystem::resolveFontId(std::string_view fontFamily, float weight, bool italic) {
  std::string const family = fontFamily.empty() ? "sans" : std::string(fontFamily);
  std::string const key = family + ":" + std::to_string(static_cast<int>(std::lround(weight))) +
                          (italic ? ":i" : ":r");
  if (auto it = d->ids.find(key); it != d->ids.end()) {
    return it->second;
  }
  std::string const appName = d->appNameProvider ? d->appNameProvider() : "lambda";
  std::string path = findFontPath(family, weight, italic, appName.empty() ? "lambda" : appName);
  if (path.empty()) {
    throw std::runtime_error("No usable Linux font found");
  }
  return d->loadPath(path, key);
}

std::shared_ptr<TextLayout const> FreeTypeTextSystem::layout(std::string_view utf8, Font const& font,
                                                             Color const& color, float maxWidth,
                                                             TextLayoutOptions const& options) {
  AttributedString as = AttributedString::plain(utf8, font, color);
  return layout(as, maxWidth, options);
}

std::shared_ptr<TextLayout const> FreeTypeTextSystem::layout(AttributedString const& text, float maxWidth,
                                                             TextLayoutOptions const& options) {
  auto cacheKey = [&] {
    std::string key;
    key.reserve(text.utf8.size() + text.runs.size() * 128 + 128);
    key.append(text.utf8);
    key.push_back('\x1f');
    key.append(std::to_string(maxWidth));
    key.push_back(':');
    key.append(std::to_string(static_cast<int>(options.horizontalAlignment)));
    key.push_back(':');
    key.append(std::to_string(static_cast<int>(options.verticalAlignment)));
    key.push_back(':');
    key.append(std::to_string(static_cast<int>(options.wrapping)));
    key.push_back(':');
    key.append(std::to_string(options.lineHeight));
    key.push_back(':');
    key.append(std::to_string(options.lineHeightMultiple));
    key.push_back(':');
    key.append(std::to_string(options.maxLines));
    key.push_back(':');
    key.append(std::to_string(options.firstBaselineOffset));
    for (AttributedRun const& run : text.runs) {
      Font const font = resolvedFont(run.font);
      key.push_back('\x1e');
      key.append(std::to_string(run.start));
      key.push_back(':');
      key.append(std::to_string(run.end));
      key.push_back(':');
      key.append(font.family);
      key.push_back(':');
      key.append(std::to_string(font.size));
      key.push_back(':');
      key.append(std::to_string(font.weight));
      key.push_back(':');
      key.push_back(font.italic ? 'i' : 'r');
      key.push_back(':');
      key.append(std::to_string(run.color.r));
      key.push_back(',');
      key.append(std::to_string(run.color.g));
      key.push_back(',');
      key.append(std::to_string(run.color.b));
      key.push_back(',');
      key.append(std::to_string(run.color.a));
      if (run.backgroundColor) {
        key.push_back(':');
        key.append(std::to_string(run.backgroundColor->r));
        key.push_back(',');
        key.append(std::to_string(run.backgroundColor->g));
        key.push_back(',');
        key.append(std::to_string(run.backgroundColor->b));
        key.push_back(',');
        key.append(std::to_string(run.backgroundColor->a));
      }
    }
    return key;
  }();

  debug::perf::recordTextLayoutCall();
  if (auto it = d->layoutCache.find(cacheKey); it != d->layoutCache.end()) {
    if (!options.suppressCacheStats) {
      debug::perf::recordTextLayoutCacheHit();
    }
    d->promoteLayout(it);
    return it->second.layout;
  }
  if (!options.suppressCacheStats) {
    debug::perf::recordTextLayoutCacheMiss();
  }

  auto result = std::make_shared<TextLayout>();
  result->ownedStorage = std::make_unique<TextLayoutStorage>();
  if (text.utf8.empty()) {
    d->cacheLayout(std::move(cacheKey), result);
    return result;
  }

  auto shapeKey = [&] {
    std::string key;
    key.reserve(text.utf8.size() + text.runs.size() * 128 + 96);
    key.append(text.utf8);
    key.push_back('\x1f');
    key.append(std::to_string(options.lineHeight));
    key.push_back(':');
    key.append(std::to_string(options.lineHeightMultiple));
    for (AttributedRun const& run : text.runs) {
      Font const font = resolvedFont(run.font);
      key.push_back('\x1e');
      key.append(std::to_string(run.start));
      key.push_back(':');
      key.append(std::to_string(run.end));
      key.push_back(':');
      key.append(font.family);
      key.push_back(':');
      key.append(std::to_string(font.size));
      key.push_back(':');
      key.append(std::to_string(font.weight));
      key.push_back(':');
      key.push_back(font.italic ? 'i' : 'r');
      key.push_back(':');
      key.append(std::to_string(run.color.r));
      key.push_back(',');
      key.append(std::to_string(run.color.g));
      key.push_back(',');
      key.append(std::to_string(run.color.b));
      key.push_back(',');
      key.append(std::to_string(run.color.a));
      if (run.backgroundColor) {
        key.push_back(':');
        key.append(std::to_string(run.backgroundColor->r));
        key.push_back(',');
        key.append(std::to_string(run.backgroundColor->g));
        key.push_back(',');
        key.append(std::to_string(run.backgroundColor->b));
        key.push_back(',');
        key.append(std::to_string(run.backgroundColor->a));
      }
    }
    return key;
  }();

  std::shared_ptr<Impl::ShapedParagraph const> paragraph;
  if (auto shapedIt = d->shapedCache.find(shapeKey); shapedIt != d->shapedCache.end()) {
    d->promoteShaped(shapedIt);
    paragraph = shapedIt->second.paragraph;
  } else {
    std::vector<AttributedRun> runs = text.runs;
    if (runs.empty()) {
      runs.push_back(AttributedRun{.start = 0,
                                   .end = static_cast<std::uint32_t>(text.utf8.size()),
                                   .font = Font::body(),
                                   .color = Colors::black,
                                   .backgroundColor = std::nullopt});
    }
    std::sort(runs.begin(), runs.end(), [](AttributedRun const& a, AttributedRun const& b) {
      return a.start < b.start;
    });

    struct ResolvedRun {
      AttributedRun source;
      Font font;
      std::uint32_t fontId = 0;
      FT_Face face = nullptr;
      float ascent = 0.f;
      float descent = 0.f;
      float lineHeight = 0.f;
    };
    std::vector<ResolvedRun> resolved;
    resolved.reserve(runs.size());
    for (AttributedRun const& source : runs) {
      Font font = resolvedFont(source.font);
      std::uint32_t fontId = resolveFontId(font.family, font.weight, font.italic);
      FT_Face face = d->face(fontId);
      FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(std::max(1.f, font.size)));
      if (hb_font_t* hbFont = d->hbFont(fontId)) {
        hb_ft_font_changed(hbFont);
      }
      float const ascent = static_cast<float>(face->size->metrics.ascender >> 6);
      float const descent = static_cast<float>(-(face->size->metrics.descender >> 6));
      float lineHeight = static_cast<float>((face->size->metrics.height >> 6) > 0
                                                ? (face->size->metrics.height >> 6)
                                                : std::lround(font.size * 1.25f));
      if (options.lineHeight > 0.f) {
        lineHeight = std::max(lineHeight, options.lineHeight);
      } else if (options.lineHeightMultiple > 0.f) {
        lineHeight = std::max(lineHeight, static_cast<float>(std::lround(font.size * options.lineHeightMultiple)));
      }
      resolved.push_back(ResolvedRun{source, font, fontId, face, ascent, descent, lineHeight});
    }

    auto shapedParagraph = std::make_shared<Impl::ShapedParagraph>();
    shapedParagraph->ascent = resolved.front().ascent;
    shapedParagraph->descent = resolved.front().descent;
    shapedParagraph->lineHeight = resolved.front().lineHeight;

    std::vector<Impl::ShapedGlyph>& shaped = shapedParagraph->glyphs;

    auto resolvedForFallback = [&](ResolvedRun const& base, char32_t ch) {
      if (FT_Get_Char_Index(base.face, static_cast<FT_ULong>(ch)) != 0) return base;
      std::string const path = findFontPathForChar(base.font.family, base.font.weight, base.font.italic, ch);
      if (path.empty()) return base;
      std::string const key = "fallback:" + path + ":" +
                              std::to_string(static_cast<int>(std::lround(base.font.size)));
      std::uint32_t const fontId = d->loadPath(path, key);
      FT_Face face = d->face(fontId);
      FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(std::max(1.f, base.font.size)));
      if (hb_font_t* hbFont = d->hbFont(fontId)) {
        hb_ft_font_changed(hbFont);
      }
      float const ascent = static_cast<float>(face->size->metrics.ascender >> 6);
      float const descent = static_cast<float>(-(face->size->metrics.descender >> 6));
      float lineHeight = static_cast<float>((face->size->metrics.height >> 6) > 0
                                                ? (face->size->metrics.height >> 6)
                                                : std::lround(base.font.size * 1.25f));
      if (options.lineHeight > 0.f) lineHeight = std::max(lineHeight, options.lineHeight);
      else if (options.lineHeightMultiple > 0.f) {
        lineHeight = std::max(lineHeight,
                              static_cast<float>(std::lround(base.font.size * options.lineHeightMultiple)));
      }
      return ResolvedRun{base.source, base.font, fontId, face, ascent, descent, lineHeight};
    };

    auto shapeSpan = [&](ResolvedRun const& rr, std::uint32_t byteBegin, std::uint32_t byteEnd,
                         std::vector<Impl::ShapedGlyph>& out) {
      if (byteBegin >= byteEnd) return;
      FT_Set_Pixel_Sizes(rr.face, 0, static_cast<FT_UInt>(std::max(1.f, rr.font.size)));
      hb_font_t* hbFont = d->hbFont(rr.fontId);
      if (!hbFont) return;
      hb_ft_font_changed(hbFont);
      hb_buffer_t* buffer = hb_buffer_create();
      hb_buffer_add_utf8(buffer, text.utf8.data(), static_cast<int>(text.utf8.size()),
                         static_cast<unsigned int>(byteBegin),
                         static_cast<int>(byteEnd - byteBegin));
      hb_buffer_guess_segment_properties(buffer);
      hb_shape(hbFont, buffer, nullptr, 0);
      unsigned int glyphCount = 0;
      hb_glyph_info_t const* infos = hb_buffer_get_glyph_infos(buffer, &glyphCount);
      hb_glyph_position_t const* positions = hb_buffer_get_glyph_positions(buffer, &glyphCount);
      for (unsigned int i = 0; i < glyphCount; ++i) {
        std::uint32_t cluster = infos[i].cluster;
        std::uint32_t nextCluster = byteEnd;
        if (i + 1 < glyphCount) nextCluster = std::max(cluster, infos[i + 1].cluster);
        out.push_back(Impl::ShapedGlyph{rr.fontId, rr.font.size, rr.source.color, rr.ascent, rr.descent,
                                        rr.lineHeight, infos[i].codepoint,
                                        static_cast<float>(positions[i].x_advance) / 64.f,
                                        static_cast<float>(positions[i].x_offset) / 64.f,
                                        -static_cast<float>(positions[i].y_offset) / 64.f,
                                        cluster, nextCluster});
      }
      hb_buffer_destroy(buffer);
    };

    shaped.reserve(text.utf8.size());
    for (ResolvedRun const& base : resolved) {
      std::vector<Codepoint> cps = decodeUtf8(std::string_view(text.utf8).substr(
          base.source.start, base.source.end - base.source.start));
      std::uint32_t spanBegin = base.source.start;
      std::uint32_t spanEnd = spanBegin;
      ResolvedRun active = base;
      bool haveActive = false;
      for (Codepoint& cp : cps) {
        cp.byteBegin += base.source.start;
        cp.byteEnd += base.source.start;
        if (cp.value == U'\n') {
          if (haveActive && spanBegin < spanEnd) shapeSpan(active, spanBegin, spanEnd, shaped);
          shaped.push_back(Impl::ShapedGlyph{base.fontId, base.font.size, base.source.color, base.ascent,
                                             base.descent, base.lineHeight, 0, 0.f, 0.f, 0.f,
                                             cp.byteBegin, cp.byteEnd});
          haveActive = false;
          spanBegin = cp.byteEnd;
          spanEnd = cp.byteEnd;
          continue;
        }
        ResolvedRun next = resolvedForFallback(base, cp.value);
        if (!haveActive) {
          active = next;
          spanBegin = cp.byteBegin;
          spanEnd = cp.byteEnd;
          haveActive = true;
        } else if (next.fontId != active.fontId) {
          shapeSpan(active, spanBegin, spanEnd, shaped);
          active = next;
          spanBegin = cp.byteBegin;
          spanEnd = cp.byteEnd;
        } else {
          spanEnd = cp.byteEnd;
        }
      }
      if (haveActive && spanBegin < spanEnd) shapeSpan(active, spanBegin, spanEnd, shaped);
    }
    paragraph = shapedParagraph;
    d->cacheShaped(std::move(shapeKey), paragraph);
  }

  std::vector<Impl::ShapedGlyph> const& shaped = paragraph->glyphs;
  result->ownedStorage->glyphArena.reserve(shaped.size());
  result->ownedStorage->positionArena.reserve(shaped.size());

  struct RunBuilder {
    std::uint32_t fontId = 0;
    float fontSize = 0.f;
    Color color{};
    float ascent = 0.f;
    float descent = 0.f;
    std::uint32_t line = 0;
    std::uint32_t begin = 0;
    std::size_t glyphStart = 0;
    std::size_t positionStart = 0;
    bool active = false;
  } builder;

  bool const allowWrap = options.wrapping != TextWrapping::NoWrap && maxWidth > 0.f;
  float x = 0.f;
  float y = paragraph->ascent;
  float lineTop = 0.f;
  float lineAscent = paragraph->ascent;
  float lineDescent = paragraph->descent;
  float lineHeight = paragraph->lineHeight;
  float maxLineWidth = 0.f;
  std::uint32_t lineStartByte = 0;
  std::uint32_t lineIndex = 0;
  std::map<std::uint32_t, float> lineWidths;

  auto flushRun = [&](std::uint32_t byteEnd) {
    if (!builder.active) return;
    std::size_t const glyphCount = result->ownedStorage->glyphArena.size() - builder.glyphStart;
    if (glyphCount == 0) {
      builder.active = false;
      return;
    }
    TextLayout::PlacedRun placed{};
    placed.run.fontId = builder.fontId;
    placed.run.fontSize = builder.fontSize;
    placed.run.color = builder.color;
    placed.run.glyphIds = std::span<std::uint32_t const>(result->ownedStorage->glyphArena.data() + builder.glyphStart,
                                                         glyphCount);
    placed.run.positions = std::span<Point const>(result->ownedStorage->positionArena.data() + builder.positionStart,
                                                  glyphCount);
    placed.run.ascent = builder.ascent;
    placed.run.descent = builder.descent;
    placed.run.width = x;
    placed.origin = {0.f, y};
    placed.utf8Begin = builder.begin;
    placed.utf8End = byteEnd;
    placed.ctLineIndex = builder.line;
    result->runs.push_back(placed);
    builder.active = false;
  };

  auto flushLine = [&](std::uint32_t byteEnd) {
    flushRun(byteEnd);
    float const leading = std::max(0.f, lineHeight - lineAscent - lineDescent);
    float const finalBaseline = lineTop + leading * 0.5f + lineAscent;
    float const baselineDelta = finalBaseline - y;
    if (std::fabs(baselineDelta) > 1e-6f) {
      for (auto& run : result->runs) {
        if (run.ctLineIndex == lineIndex) {
          run.origin.y += baselineDelta;
        }
      }
    }
    result->lines.push_back(TextLayout::LineRange{lineIndex, static_cast<int>(lineStartByte),
                                                  static_cast<int>(byteEnd), 0.f, lineTop,
                                                  lineTop + lineHeight, finalBaseline});
    lineWidths[lineIndex] = x;
    maxLineWidth = std::max(maxLineWidth, x);
    ++lineIndex;
    x = 0.f;
    lineTop += lineHeight;
    lineStartByte = byteEnd;
    lineAscent = paragraph->ascent;
    lineDescent = paragraph->descent;
    lineHeight = paragraph->lineHeight;
    float const nextLeading = std::max(0.f, lineHeight - lineAscent - lineDescent);
    y = lineTop + nextLeading * 0.5f + lineAscent;
  };

  for (Impl::ShapedGlyph const& glyph : shaped) {
    if (options.maxLines > 0 && static_cast<int>(lineIndex) >= options.maxLines) break;
    if (glyph.glyphId == 0 && glyph.advance == 0.f && glyph.byteEnd > glyph.byteBegin &&
        text.utf8[glyph.byteBegin] == '\n') {
      flushLine(glyph.byteBegin);
      lineStartByte = glyph.byteEnd;
      continue;
    }
    if (allowWrap && x > 0.f && x + glyph.advance > maxWidth) {
      flushLine(glyph.byteBegin);
      lineStartByte = glyph.byteBegin;
    }
    lineAscent = std::max(lineAscent, glyph.ascent);
    lineDescent = std::max(lineDescent, glyph.descent);
    lineHeight = std::max(lineHeight, glyph.lineHeight);
    if (!builder.active || builder.fontId != glyph.fontId || builder.fontSize != glyph.fontSize ||
        !(builder.color == glyph.color) || builder.line != lineIndex) {
      flushRun(glyph.byteBegin);
      builder = RunBuilder{glyph.fontId, glyph.fontSize, glyph.color, glyph.ascent, glyph.descent, lineIndex,
                           glyph.byteBegin, result->ownedStorage->glyphArena.size(),
                           result->ownedStorage->positionArena.size(), true};
    }
    result->ownedStorage->glyphArena.push_back(glyph.glyphId);
    result->ownedStorage->positionArena.push_back(Point{x + glyph.offsetX, glyph.offsetY});
    x += glyph.advance;
  }

  flushLine(static_cast<std::uint32_t>(text.utf8.size()));
  for (auto& run : result->runs) {
    run.run.width = lineWidths[run.ctLineIndex];
  }
  result->measuredSize = {maxLineWidth, result->lines.empty() ? 0.f : result->lines.back().bottom};
  result->firstBaseline = result->lines.empty() ? 0.f : result->lines.front().baseline;
  result->lastBaseline = result->lines.empty() ? 0.f : result->lines.back().baseline;
  d->cacheLayout(std::move(cacheKey), result);
  return result;
}

std::shared_ptr<TextLayout const> FreeTypeTextSystem::layoutBoxedImpl(AttributedString const& text, Rect const& box,
                                                                      TextLayoutOptions const& options) {
  float const maxWidth = options.wrapping == TextWrapping::NoWrap ? 0.f : box.width;
  auto layoutResult = cloneTextLayout(*layout(text, maxWidth, options));
  float const contentHeight = layoutResult->measuredSize.height;
  float dy = 0.f;
  switch (options.verticalAlignment) {
  case VerticalAlignment::Top:
    break;
  case VerticalAlignment::Center:
    dy = (box.height - contentHeight) * 0.5f;
    break;
  case VerticalAlignment::Bottom:
    dy = box.height - contentHeight;
    break;
  case VerticalAlignment::FirstBaseline:
    dy = options.firstBaselineOffset - layoutResult->firstBaseline;
    break;
  }

  for (auto& run : layoutResult->runs) {
    float dx = 0.f;
    if (options.horizontalAlignment == HorizontalAlignment::Center) {
      dx = (box.width - run.run.width) * 0.5f;
    } else if (options.horizontalAlignment == HorizontalAlignment::Trailing) {
      dx = box.width - run.run.width;
    }
    run.origin.x += box.x;
    run.origin.x += dx;
    run.origin.y += box.y + dy;
  }
  for (auto& line : layoutResult->lines) {
    float lineWidth = 0.f;
    for (auto const& run : layoutResult->runs) {
      if (run.ctLineIndex == line.ctLineIndex) {
        lineWidth = std::max(lineWidth, run.run.width);
      }
    }
    float dx = 0.f;
    if (options.horizontalAlignment == HorizontalAlignment::Center) {
      dx = (box.width - lineWidth) * 0.5f;
    } else if (options.horizontalAlignment == HorizontalAlignment::Trailing) {
      dx = box.width - lineWidth;
    }
    line.lineMinX += box.x + dx;
    line.top += box.y + dy;
    line.bottom += box.y + dy;
    line.baseline += box.y + dy;
  }
  layoutResult->measuredSize.width = std::min(layoutResult->measuredSize.width, box.width);
  layoutResult->measuredSize.height = std::min(layoutResult->measuredSize.height, box.height);
  return layoutResult;
}

Size FreeTypeTextSystem::measure(std::string_view utf8, Font const& font, Color const& color,
                                 float maxWidth, TextLayoutOptions const& options) {
  return layout(utf8, font, color, maxWidth, options)->measuredSize;
}

Size FreeTypeTextSystem::measure(AttributedString const& text, float maxWidth, TextLayoutOptions const& options) {
  return layout(text, maxWidth, options)->measuredSize;
}

std::vector<std::uint8_t> FreeTypeTextSystem::rasterizeGlyph(std::uint32_t fontId, std::uint32_t glyphId,
                                                             float size, std::uint32_t& outWidth,
                                                             std::uint32_t& outHeight, Point& outBearing) {
  FT_Face face = d->face(fontId);
  if (!face) {
    outWidth = outHeight = 0;
    return {};
  }
  FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(std::max(1.f, size)));
  if (FT_Load_Glyph(face, static_cast<FT_UInt>(glyphId), FT_LOAD_RENDER) != 0) {
    outWidth = outHeight = 0;
    return {};
  }
  FT_GlyphSlot slot = face->glyph;
  outWidth = slot->bitmap.width;
  outHeight = slot->bitmap.rows;
  outBearing = {static_cast<float>(slot->bitmap_left), static_cast<float>(slot->bitmap_top)};
  std::vector<std::uint8_t> out(outWidth * outHeight);
  for (std::uint32_t y = 0; y < outHeight; ++y) {
    std::memcpy(out.data() + y * outWidth, slot->bitmap.buffer + y * slot->bitmap.pitch, outWidth);
  }
  return out;
}

} // namespace lambda
