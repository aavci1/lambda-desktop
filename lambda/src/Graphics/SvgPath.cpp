#include <Lambda/Graphics/SvgPath.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <numbers>
#include <string>

namespace lambdaui {

namespace {

constexpr float kEpsilon = 1e-6f;

bool isCommand(char c) {
  switch (c) {
  case 'M': case 'm': case 'L': case 'l': case 'H': case 'h': case 'V': case 'v':
  case 'C': case 'c': case 'S': case 's': case 'Q': case 'q': case 'T': case 't':
  case 'A': case 'a': case 'Z': case 'z':
    return true;
  default:
    return false;
  }
}

bool isSeparator(char c) {
  return c == ',' || c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

Point reflected(Point current, Point control) {
  return Point{2.f * current.x - control.x, 2.f * current.y - control.y};
}

float vectorAngle(float ux, float uy, float vx, float vy) {
  float const dot = ux * vx + uy * vy;
  float const len = std::sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
  if (len <= kEpsilon) {
    return 0.f;
  }
  float const clamped = std::clamp(dot / len, -1.f, 1.f);
  float angle = std::acos(clamped);
  if (ux * vy - uy * vx < 0.f) {
    angle = -angle;
  }
  return angle;
}

void appendSvgArc(Path& path, Point start, float rxIn, float ryIn, float xAxisRotationDegrees,
                  bool largeArc, bool sweep, Point end) {
  if (std::abs(start.x - end.x) <= kEpsilon && std::abs(start.y - end.y) <= kEpsilon) {
    return;
  }

  float rx = std::abs(rxIn);
  float ry = std::abs(ryIn);
  if (rx <= kEpsilon || ry <= kEpsilon) {
    path.lineTo(end);
    return;
  }

  float const phi = xAxisRotationDegrees * std::numbers::pi_v<float> / 180.f;
  float const cosPhi = std::cos(phi);
  float const sinPhi = std::sin(phi);
  float const dx = (start.x - end.x) * 0.5f;
  float const dy = (start.y - end.y) * 0.5f;
  float const x1p = cosPhi * dx + sinPhi * dy;
  float const y1p = -sinPhi * dx + cosPhi * dy;

  float const lambda = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
  if (lambda > 1.f) {
    float const scale = std::sqrt(lambda);
    rx *= scale;
    ry *= scale;
  }

  float const rx2 = rx * rx;
  float const ry2 = ry * ry;
  float const x1p2 = x1p * x1p;
  float const y1p2 = y1p * y1p;
  float const denom = rx2 * y1p2 + ry2 * x1p2;
  if (denom <= kEpsilon) {
    path.lineTo(end);
    return;
  }

  float factor = std::max(0.f, (rx2 * ry2 - rx2 * y1p2 - ry2 * x1p2) / denom);
  factor = std::sqrt(factor);
  if (largeArc == sweep) {
    factor = -factor;
  }

  float const cxp = factor * rx * y1p / ry;
  float const cyp = factor * -ry * x1p / rx;
  float const cx = cosPhi * cxp - sinPhi * cyp + (start.x + end.x) * 0.5f;
  float const cy = sinPhi * cxp + cosPhi * cyp + (start.y + end.y) * 0.5f;

  float const ux = (x1p - cxp) / rx;
  float const uy = (y1p - cyp) / ry;
  float const vx = (-x1p - cxp) / rx;
  float const vy = (-y1p - cyp) / ry;
  float theta1 = vectorAngle(1.f, 0.f, ux, uy);
  float deltaTheta = vectorAngle(ux, uy, vx, vy);
  if (!sweep && deltaTheta > 0.f) {
    deltaTheta -= 2.f * std::numbers::pi_v<float>;
  } else if (sweep && deltaTheta < 0.f) {
    deltaTheta += 2.f * std::numbers::pi_v<float>;
  }

  int const segments = std::max(1, static_cast<int>(std::ceil(std::abs(deltaTheta) / (std::numbers::pi_v<float> * 0.5f))));
  float const step = deltaTheta / static_cast<float>(segments);

  auto mapPoint = [&](float x, float y) {
    return Point{
      cx + rx * cosPhi * x - ry * sinPhi * y,
      cy + rx * sinPhi * x + ry * cosPhi * y,
    };
  };

  for (int i = 0; i < segments; ++i) {
    float const t1 = theta1 + step * static_cast<float>(i);
    float const t2 = t1 + step;
    float const alpha = (4.f / 3.f) * std::tan((t2 - t1) * 0.25f);
    float const cos1 = std::cos(t1);
    float const sin1 = std::sin(t1);
    float const cos2 = std::cos(t2);
    float const sin2 = std::sin(t2);

    Point const c1 = mapPoint(cos1 - alpha * sin1, sin1 + alpha * cos1);
    Point const c2 = mapPoint(cos2 + alpha * sin2, sin2 - alpha * cos2);
    Point const p = (i + 1 == segments) ? end : mapPoint(cos2, sin2);
    path.bezierTo(c1, c2, p);
  }
}

class Parser {
public:
  explicit Parser(std::string_view input) : input_(input) {}

  Path parse(SvgPathParseError* error) {
    while (true) {
      skipSeparators();
      if (eof()) {
        break;
      }
      if (isCommand(peek())) {
        command_ = get();
      } else if (command_ == 0) {
        fail(error, "expected path command");
        break;
      } else if (!startsNumber(peek())) {
        fail(error, "expected path command or number");
        break;
      }

      if (!parseCommand(error)) {
        break;
      }
    }
    return path_;
  }

private:
  bool parseCommand(SvgPathParseError* error) {
    switch (command_) {
    case 'M': case 'm': return parseMove(error, command_ == 'm');
    case 'L': case 'l': return parseLine(error, command_ == 'l');
    case 'H': case 'h': return parseHorizontal(error, command_ == 'h');
    case 'V': case 'v': return parseVertical(error, command_ == 'v');
    case 'C': case 'c': return parseCubic(error, command_ == 'c');
    case 'S': case 's': return parseSmoothCubic(error, command_ == 's');
    case 'Q': case 'q': return parseQuad(error, command_ == 'q');
    case 'T': case 't': return parseSmoothQuad(error, command_ == 't');
    case 'A': case 'a': return parseArc(error, command_ == 'a');
    case 'Z': case 'z':
      path_.close();
      current_ = subpathStart_;
      lastCurve_ = LastCurve::None;
      command_ = 0;
      return true;
    default:
      fail(error, "unsupported path command");
      return false;
    }
  }

  bool parseMove(SvgPathParseError* error, bool relative) {
    float x = 0.f;
    float y = 0.f;
    if (!numberPair(error, x, y, "expected coordinate pair after 'M'")) {
      return false;
    }
    Point p = point(x, y, relative);
    path_.moveTo(p);
    current_ = p;
    subpathStart_ = p;
    lastCurve_ = LastCurve::None;
    while (hasNumber()) {
      if (!numberPair(error, x, y, "expected coordinate pair after implicit 'L'")) {
        return false;
      }
      p = point(x, y, relative);
      path_.lineTo(p);
      current_ = p;
    }
    command_ = relative ? 'l' : 'L';
    return true;
  }

  bool parseLine(SvgPathParseError* error, bool relative) {
    float x = 0.f;
    float y = 0.f;
    if (!hasNumber()) {
      fail(error, "expected coordinate pair after 'L'");
      return false;
    }
    while (hasNumber()) {
      if (!numberPair(error, x, y, "expected coordinate pair after 'L'")) {
        return false;
      }
      current_ = point(x, y, relative);
      path_.lineTo(current_);
      lastCurve_ = LastCurve::None;
    }
    return true;
  }

  bool parseHorizontal(SvgPathParseError* error, bool relative) {
    float x = 0.f;
    if (!hasNumber()) {
      fail(error, "expected number after 'H'");
      return false;
    }
    while (hasNumber()) {
      if (!number(error, x, "expected number after 'H'")) {
        return false;
      }
      current_.x = relative ? current_.x + x : x;
      path_.lineTo(current_);
      lastCurve_ = LastCurve::None;
    }
    return true;
  }

  bool parseVertical(SvgPathParseError* error, bool relative) {
    float y = 0.f;
    if (!hasNumber()) {
      fail(error, "expected number after 'V'");
      return false;
    }
    while (hasNumber()) {
      if (!number(error, y, "expected number after 'V'")) {
        return false;
      }
      current_.y = relative ? current_.y + y : y;
      path_.lineTo(current_);
      lastCurve_ = LastCurve::None;
    }
    return true;
  }

  bool parseCubic(SvgPathParseError* error, bool relative) {
    if (!hasNumber()) {
      fail(error, "expected cubic parameters after 'C'");
      return false;
    }
    while (hasNumber()) {
      float x1, y1, x2, y2, x, y;
      if (!number(error, x1, "expected x1 after 'C'") || !number(error, y1, "expected y1 after 'C'") ||
          !number(error, x2, "expected x2 after 'C'") || !number(error, y2, "expected y2 after 'C'") ||
          !number(error, x, "expected x after 'C'") || !number(error, y, "expected y after 'C'")) {
        return false;
      }
      Point const c1 = point(x1, y1, relative);
      Point const c2 = point(x2, y2, relative);
      Point const end = point(x, y, relative);
      path_.bezierTo(c1, c2, end);
      current_ = end;
      lastCubicControl_ = c2;
      lastCurve_ = LastCurve::Cubic;
    }
    return true;
  }

  bool parseSmoothCubic(SvgPathParseError* error, bool relative) {
    if (!hasNumber()) {
      fail(error, "expected cubic parameters after 'S'");
      return false;
    }
    while (hasNumber()) {
      float x2, y2, x, y;
      if (!number(error, x2, "expected x2 after 'S'") || !number(error, y2, "expected y2 after 'S'") ||
          !number(error, x, "expected x after 'S'") || !number(error, y, "expected y after 'S'")) {
        return false;
      }
      Point const c1 = lastCurve_ == LastCurve::Cubic ? reflected(current_, lastCubicControl_) : current_;
      Point const c2 = point(x2, y2, relative);
      Point const end = point(x, y, relative);
      path_.bezierTo(c1, c2, end);
      current_ = end;
      lastCubicControl_ = c2;
      lastCurve_ = LastCurve::Cubic;
    }
    return true;
  }

  bool parseQuad(SvgPathParseError* error, bool relative) {
    if (!hasNumber()) {
      fail(error, "expected quadratic parameters after 'Q'");
      return false;
    }
    while (hasNumber()) {
      float x1, y1, x, y;
      if (!number(error, x1, "expected x1 after 'Q'") || !number(error, y1, "expected y1 after 'Q'") ||
          !number(error, x, "expected x after 'Q'") || !number(error, y, "expected y after 'Q'")) {
        return false;
      }
      Point const c = point(x1, y1, relative);
      Point const end = point(x, y, relative);
      path_.quadTo(c, end);
      current_ = end;
      lastQuadControl_ = c;
      lastCurve_ = LastCurve::Quad;
    }
    return true;
  }

  bool parseSmoothQuad(SvgPathParseError* error, bool relative) {
    if (!hasNumber()) {
      fail(error, "expected quadratic parameters after 'T'");
      return false;
    }
    while (hasNumber()) {
      float x, y;
      if (!number(error, x, "expected x after 'T'") || !number(error, y, "expected y after 'T'")) {
        return false;
      }
      Point const c = lastCurve_ == LastCurve::Quad ? reflected(current_, lastQuadControl_) : current_;
      Point const end = point(x, y, relative);
      path_.quadTo(c, end);
      current_ = end;
      lastQuadControl_ = c;
      lastCurve_ = LastCurve::Quad;
    }
    return true;
  }

  bool parseArc(SvgPathParseError* error, bool relative) {
    if (!hasNumber()) {
      fail(error, "expected arc parameters after 'A'");
      return false;
    }
    while (hasNumber()) {
      float rx, ry, rot, large, sweep, x, y;
      if (!number(error, rx, "expected rx after 'A'") || !number(error, ry, "expected ry after 'A'") ||
          !number(error, rot, "expected x-axis-rotation after 'A'") ||
          !number(error, large, "expected large-arc flag after 'A'") ||
          !number(error, sweep, "expected sweep flag after 'A'") ||
          !number(error, x, "expected x after 'A'") || !number(error, y, "expected y after 'A'")) {
        return false;
      }
      Point const end = point(x, y, relative);
      appendSvgArc(path_, current_, rx, ry, rot, large != 0.f, sweep != 0.f, end);
      current_ = end;
      lastCurve_ = LastCurve::None;
    }
    return true;
  }

  bool numberPair(SvgPathParseError* error, float& x, float& y, char const* message) {
    return number(error, x, message) && number(error, y, message);
  }

  bool number(SvgPathParseError* error, float& out, char const* message) {
    skipSeparators();
    if (eof() || !startsNumber(peek())) {
      fail(error, message);
      return false;
    }
    char* end = nullptr;
    char const* begin = input_.c_str() + pos_;
    out = std::strtof(begin, &end);
    if (end == begin) {
      fail(error, message);
      return false;
    }
    pos_ = static_cast<std::size_t>(end - input_.c_str());
    skipSeparators();
    return true;
  }

  bool hasNumber() {
    skipSeparators();
    return !eof() && startsNumber(peek());
  }

  static bool startsNumber(char c) {
    return c == '+' || c == '-' || c == '.' || (c >= '0' && c <= '9');
  }

  Point point(float x, float y, bool relative) const {
    return relative ? Point{current_.x + x, current_.y + y} : Point{x, y};
  }

  void skipSeparators() {
    while (!eof() && isSeparator(input_[pos_])) {
      ++pos_;
    }
  }

  bool eof() const { return pos_ >= input_.size(); }
  char peek() const { return input_[pos_]; }
  char get() { return input_[pos_++]; }

  void fail(SvgPathParseError* error, char const* message) const {
    if (error) {
      error->position = std::min(pos_, input_.size());
      error->message = message;
    }
  }

  enum class LastCurve { None, Cubic, Quad };

  std::string input_;
  std::size_t pos_ = 0;
  char command_ = 0;
  Path path_;
  Point current_{};
  Point subpathStart_{};
  Point lastCubicControl_{};
  Point lastQuadControl_{};
  LastCurve lastCurve_ = LastCurve::None;
};

} // namespace

Path parseSvgPath(std::string_view d, SvgPathParseError* error) {
  if (error) {
    *error = {};
  }
  if (d.empty()) {
    return {};
  }
  return Parser{d}.parse(error);
}

} // namespace lambdaui
