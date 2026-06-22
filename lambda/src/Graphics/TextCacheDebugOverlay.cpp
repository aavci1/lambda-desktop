#include <Lambda/Graphics/TextCacheDebugOverlay.hpp>

#include <Lambda/Graphics/Canvas.hpp>
#include <Lambda/Graphics/Font.hpp>
#include <Lambda/Graphics/Styles.hpp>
#include <Lambda/Graphics/TextLayoutOptions.hpp>
#include <Lambda/Graphics/TextSystem.hpp>

#include <cstdint>
#include <sstream>
#include <string>

namespace lambda {

namespace {

float hitRateWindow(TextCacheStats::LayerStats const& older, TextCacheStats::LayerStats const& newer) {
  std::uint64_t const dh = newer.hits > older.hits ? newer.hits - older.hits : 0u;
  std::uint64_t const dm = newer.misses > older.misses ? newer.misses - older.misses : 0u;
  std::uint64_t const tot = dh + dm;
  if (tot == 0u) {
    return 100.f;
  }
  return 100.f * static_cast<float>(dh) / static_cast<float>(tot);
}

std::string formatLayer(char const* label, float pct, TextCacheStats::LayerStats const& snap) {
  std::ostringstream o;
  o.setf(std::ios::fixed);
  o.precision(1);
  o << label << ": " << pct << "%  (ev " << snap.evictions << "  ~" << (snap.currentBytes / 1024u)
    << " KiB)";
  return o.str();
}

std::string buildOverlayText(TextCacheStats const& oldest, TextCacheStats const& newest,
                             std::size_t windowFrames) {
  std::ostringstream o;
  o << "TEXT CACHE (rolling " << windowFrames << " frames)\n";
  o << formatLayer("L0 sizedFont", hitRateWindow(oldest.l0_sizedFont, newest.l0_sizedFont),
                   newest.l0_sizedFont)
    << "\n";
  o << formatLayer("L1 color", hitRateWindow(oldest.l1_color, newest.l1_color), newest.l1_color)
    << "\n";
  o << formatLayer("L1 runAttr", hitRateWindow(oldest.l1_runAttr, newest.l1_runAttr),
                   newest.l1_runAttr)
    << "\n";
  o << formatLayer("L1 para", hitRateWindow(oldest.l1_paraStyle, newest.l1_paraStyle),
                   newest.l1_paraStyle)
    << "\n";
  o << formatLayer("L2 framesetter", hitRateWindow(oldest.l2_framesetter, newest.l2_framesetter),
                   newest.l2_framesetter)
    << "\n";
  o << formatLayer("L2.5 para", hitRateWindow(oldest.l2_5_paragraph, newest.l2_5_paragraph),
                   newest.l2_5_paragraph)
    << "\n";
  o << formatLayer("L2.5 variant", hitRateWindow(oldest.l2_5_variant, newest.l2_5_variant),
                   newest.l2_5_variant)
    << "\n";
  o << formatLayer("L2.5 memo", hitRateWindow(oldest.l2_5_memo, newest.l2_5_memo),
                   newest.l2_5_memo)
    << "\n";
  o << formatLayer("L3 layout", hitRateWindow(oldest.l3_layout, newest.l3_layout), newest.l3_layout)
    << "\n";
  o << formatLayer("L4 box", hitRateWindow(oldest.l4_boxLayout, newest.l4_boxLayout),
                   newest.l4_boxLayout)
    << "\n";
  o << "collisions: " << newest.contentHashCollisions;
  return o.str();
}

} // namespace

void renderTextCacheDebugOverlay(Canvas& canvas, Rect viewport, TextCacheRingBuffer& ring,
                                 TextSystem& textSystem) {
  TextCacheStats const cur = textSystem.stats();
  ring.samples[ring.writeIdx] = cur;
  ring.writeIdx = (ring.writeIdx + 1) % 60;
  ring.count = std::min(ring.count + 1, std::size_t{60});

  std::size_t const newestIdx = (ring.writeIdx + 60 - 1) % 60;
  std::size_t const oldestIdx = (ring.writeIdx + 60 - ring.count) % 60;
  TextCacheStats const& newest = ring.samples[newestIdx];
  TextCacheStats const& oldest = ring.samples[oldestIdx];
  std::size_t const windowFrames = ring.count < 2 ? 1 : ring.count;

  std::string const text = buildOverlayText(oldest, newest, windowFrames);

  float const panelW = std::min(420.f, std::max(120.f, viewport.width - 16.f));
  float const panelH = std::min(280.f, std::max(80.f, viewport.height - 16.f));

  Font font{};
  font.family = ".AppleSystemUIFont";
  font.size = 11.f;
  font.weight = 400.f;

  TextLayoutOptions opts{};
  opts.wrapping = TextWrapping::Wrap;
  opts.suppressCacheStats = true;

  auto layout = textSystem.layout(
      text, font, Color{0.95f, 0.95f, 0.98f, 1.f}, panelW, opts);

  canvas.save();
  canvas.translate(8.f, 8.f);
  canvas.drawRect(Rect{0, 0, panelW, panelH}, CornerRadius{6.f},
                  FillStyle::solid(Color{0.05f, 0.05f, 0.08f, 0.78f}),
                  StrokeStyle::solid(Color{0.4f, 0.4f, 0.55f, 0.9f}, 1.f));
  if (layout) {
    canvas.drawTextLayout(*layout, Point{10.f, 10.f});
  }
  canvas.restore();
}

} // namespace lambda
