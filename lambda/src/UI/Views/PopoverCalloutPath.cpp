#include <Lambda/UI/Views/PopoverCalloutPath.hpp>

#include <Lambda/Graphics/Path.hpp>

#include <algorithm>
#include <cmath>

namespace lambdaui {

namespace {

constexpr float kPi = 3.14159265358979323846f;

void clampCornerRadii(float w, float h, CornerRadius& r) {
  if (w <= 0.f || h <= 0.f) {
    return;
  }
  float const maxR = std::min(w, h) * 0.5f;
  r.topLeft = std::min(r.topLeft, maxR);
  r.topRight = std::min(r.topRight, maxR);
  r.bottomRight = std::min(r.bottomRight, maxR);
  r.bottomLeft = std::min(r.bottomLeft, maxR);
  auto fixEdge = [](float& a, float& b, float len) {
    if (a + b > len && len > 0.f) {
      float const s = len / (a + b);
      a *= s;
      b *= s;
    }
  };
  fixEdge(r.topLeft, r.topRight, w);
  fixEdge(r.bottomLeft, r.bottomRight, w);
  fixEdge(r.topLeft, r.bottomLeft, h);
  fixEdge(r.topRight, r.bottomRight, h);
}

void appendArc(Path& p, Point c, float rad, float a0, float a1) {
  int const n = std::clamp(static_cast<int>(std::ceil(std::abs(a1 - a0) * std::max(rad, 1.f) * 0.9f)),
                           12,
                           48);
  for (int i = 1; i <= n; ++i) {
    float const t = static_cast<float>(i) / static_cast<float>(n);
    float const a = a0 + (a1 - a0) * t;
    p.lineTo({c.x + std::cos(a) * rad, c.y + std::sin(a) * rad});
  }
}

Path flipPathX(Path const& src, float W) {
  Path out{};
  for (std::size_t i = 0; i < src.commandCount(); ++i) {
    Path::CommandView const cv = src.command(i);
    switch (cv.type) {
    case Path::CommandType::MoveTo:
      if (cv.dataCount >= 2) {
        out.moveTo({W - cv.data[0], cv.data[1]});
      }
      break;
    case Path::CommandType::LineTo:
      if (cv.dataCount >= 2) {
        out.lineTo({W - cv.data[0], cv.data[1]});
      }
      break;
    case Path::CommandType::Close:
      out.close();
      break;
    default:
      break;
    }
  }
  return out;
}

/// Vertical mirror: y' = H - y (screen coords, y down). Used to derive Above from Below.
Path flipPathY(Path const& src, float H) {
  Path out{};
  for (std::size_t i = 0; i < src.commandCount(); ++i) {
    Path::CommandView const cv = src.command(i);
    switch (cv.type) {
    case Path::CommandType::MoveTo:
      if (cv.dataCount >= 2) {
        out.moveTo({cv.data[0], H - cv.data[1]});
      }
      break;
    case Path::CommandType::LineTo:
      if (cv.dataCount >= 2) {
        out.lineTo({cv.data[0], H - cv.data[1]});
      }
      break;
    case Path::CommandType::Close:
      out.close();
      break;
    default:
      break;
    }
  }
  return out;
}

Path pathBelow(Rect const& card, CornerRadius cr, float aw) {
  float const W = card.width;
  float const topY = card.y;
  float const cardH = card.height;
  CornerRadius c = cr;
  clampCornerRadii(W, cardH, c);
  float const rtl = c.topLeft;
  float const rtr = c.topRight;
  float const rbr = c.bottomRight;
  float const rbl = c.bottomLeft;

  float x0 = (W - aw) * 0.5f;
  x0 = std::clamp(x0, rtl, std::max(rtl, W - rtr - aw));

  Path p{};
  p.moveTo({W * 0.5f, 0.f});
  p.lineTo({x0 + aw, topY});
  p.lineTo({W - rtr, topY});
  appendArc(p, {W - rtr, topY + rtr}, rtr, -kPi * 0.5f, 0.f);
  p.lineTo({W, topY + cardH - rbr});
  appendArc(p, {W - rbr, topY + cardH - rbr}, rbr, 0.f, kPi * 0.5f);
  p.lineTo({rbl, topY + cardH});
  appendArc(p, {rbl, topY + cardH - rbl}, rbl, kPi * 0.5f, kPi);
  p.lineTo({0.f, topY + rtl});
  appendArc(p, {rtl, topY + rtl}, rtl, kPi, kPi * 1.5f);
  p.lineTo({x0, topY});
  p.close();
  return p;
}

Path pathAbove(Rect const& card, CornerRadius cr, float aw, float totalH) {
  float const W = card.width;
  float const cardH = card.height;
  if (W <= 0.f || cardH <= 0.f || totalH <= cardH) {
    Path p{};
    p.rect(card, cr);
    return p;
  }
  float const ah = totalH - cardH;
  // Same outline as Below with arrow on top, then flip Y so the arrow sits under the card.
  Rect const belowCard{0.f, ah, W, cardH};
  Path const below = pathBelow(belowCard, cr, aw);
  return flipPathY(below, totalH);
}

Path pathEnd(Rect const& card, CornerRadius cr, float awVert, float totalH) {
  float const ax = card.x;
  float const cy = card.y;
  float const cw = card.width;
  float const ch = card.height;
  CornerRadius c = cr;
  clampCornerRadii(cw, ch, c);
  float const rtl = c.topLeft;
  float const rtr = c.topRight;
  float const rbr = c.bottomRight;
  float const rbl = c.bottomLeft;

  float const ty = totalH * 0.5f;
  float yTop = ty - awVert * 0.5f;
  float yBot = ty + awVert * 0.5f;
  float const edgeTop = cy + rtl;
  float const edgeBot = cy + ch - rbl;
  yTop = std::clamp(yTop, edgeTop, std::max(edgeTop, edgeBot - awVert));
  yBot = yTop + awVert;
  if (yBot > edgeBot) {
    yBot = edgeBot;
    yTop = yBot - awVert;
    yTop = std::max(yTop, edgeTop);
  }

  Path p{};
  p.moveTo({0.f, ty});
  p.lineTo({ax, yTop});
  if (std::abs(yTop - edgeTop) > 1e-4f) {
    p.lineTo({ax, edgeTop});
  }
  appendArc(p, {ax + rtl, cy + rtl}, rtl, kPi, kPi * 1.5f);
  p.lineTo({ax + cw - rtr, cy});
  appendArc(p, {ax + cw - rtr, cy + rtr}, rtr, -kPi * 0.5f, 0.f);
  p.lineTo({ax + cw, cy + ch - rbr});
  appendArc(p, {ax + cw - rbr, cy + ch - rbr}, rbr, 0.f, kPi * 0.5f);
  p.lineTo({ax + rbl, cy + ch});
  appendArc(p, {ax + rbl, cy + ch - rbl}, rbl, kPi * 0.5f, kPi);
  if (std::abs(yBot - edgeBot) > 1e-4f) {
    p.lineTo({ax, yBot});
  }
  p.close();
  return p;
}

Path pathStart(Rect const& card, CornerRadius cr, float awVert, float totalW, float totalH) {
  Rect const endCard{totalW - card.x - card.width, card.y, card.width, card.height};
  return flipPathX(pathEnd(endCard, cr, awVert, totalH), totalW);
}

} // namespace

Path buildPopoverCalloutPath(PopoverPlacement placement, CornerRadius cornerRadius, bool arrow,
                             float aw, float ah, Rect cardRect, Size total) {
  if (!arrow) {
    Path p{};
    p.rect(cardRect, cornerRadius);
    return p;
  }

  switch (placement) {
  case PopoverPlacement::Below:
    return pathBelow(cardRect, cornerRadius, aw);
  case PopoverPlacement::Above:
    return pathAbove(cardRect, cornerRadius, aw, cardRect.height + ah);
  case PopoverPlacement::End:
    return pathEnd(cardRect, cornerRadius, aw, total.height);
  case PopoverPlacement::Start:
    return pathStart(cardRect, cornerRadius, aw, total.width, total.height);
  }
  Path p{};
  p.rect(cardRect, cornerRadius);
  return p;
}

} // namespace lambdaui
