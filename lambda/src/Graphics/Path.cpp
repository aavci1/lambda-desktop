#include <Lambda/Graphics/Path.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace lambdaui {

Path::Path() = default;

Path::~Path() = default;

Path::Path(const Path& other)
    : commands_(other.commands_)
    , data_(other.data_)
    , cachedBounds_(other.cachedBounds_)
    , boundsDirty_(other.boundsDirty_) {
}

Path& Path::operator=(const Path& other) {
  if (this != &other) {
    commands_ = other.commands_;
    data_ = other.data_;
    cachedBounds_ = other.cachedBounds_;
    boundsDirty_ = other.boundsDirty_;
  }
  return *this;
}

Path::Path(Path&& other) noexcept
    : commands_(std::move(other.commands_))
    , data_(std::move(other.data_))
    , cachedBounds_(other.cachedBounds_)
    , boundsDirty_(other.boundsDirty_) {
}

Path& Path::operator=(Path&& other) noexcept {
  if (this != &other) {
    commands_ = std::move(other.commands_);
    data_ = std::move(other.data_);
    cachedBounds_ = other.cachedBounds_;
    boundsDirty_ = other.boundsDirty_;
  }
  return *this;
}

bool Path::operator==(Path const& other) const {
  return commands_ == other.commands_ && data_ == other.data_;
}

void Path::pushCommand(CommandType type, PathWinding w, std::initializer_list<float> params) {
  Command cmd;
  cmd.type = type;
  cmd.winding = w;
  cmd.dataOffset = static_cast<uint32_t>(data_.size());
  cmd.dataCount = static_cast<uint8_t>(params.size());
  data_.insert(data_.end(), params);
  commands_.push_back(cmd);
}

void Path::setWinding(PathWinding winding) {
  pushCommand(CommandType::SetWinding, winding, {});
  invalidateBounds();
}

void Path::moveTo(const Point& point) {
  pushCommand(CommandType::MoveTo, PathWinding::CounterClockwise, {point.x, point.y});
  invalidateBounds();
}

void Path::lineTo(const Point& point) {
  pushCommand(CommandType::LineTo, PathWinding::CounterClockwise, {point.x, point.y});
  invalidateBounds();
}

void Path::quadTo(const Point& control, const Point& end) {
  pushCommand(CommandType::QuadTo, PathWinding::CounterClockwise,
              {control.x, control.y, end.x, end.y});
  invalidateBounds();
}

void Path::bezierTo(const Point& c1, const Point& c2, const Point& end) {
  pushCommand(CommandType::BezierTo, PathWinding::CounterClockwise,
              {c1.x, c1.y, c2.x, c2.y, end.x, end.y});
  invalidateBounds();
}

void Path::arcTo(const Point& p1, const Point& p2, float radius) {
  pushCommand(CommandType::ArcTo, PathWinding::CounterClockwise,
              {p1.x, p1.y, p2.x, p2.y, radius});
  invalidateBounds();
}

void Path::arc(const Point& center, float radius, float startAngle, float endAngle, bool clockwise) {
  pushCommand(CommandType::Arc, PathWinding::CounterClockwise,
              {center.x, center.y, radius, startAngle, endAngle, clockwise ? 1.0f : 0.0f});
  invalidateBounds();
}

namespace {

static void clampCornerRadii(float w, float h, CornerRadius& r) {
  if (w <= 0.0f || h <= 0.0f) {
    return;
  }
  const float maxR = std::min(w, h) * 0.5f;
  r.topLeft = std::min(r.topLeft, maxR);
  r.topRight = std::min(r.topRight, maxR);
  r.bottomRight = std::min(r.bottomRight, maxR);
  r.bottomLeft = std::min(r.bottomLeft, maxR);
  auto fixEdge = [](float& a, float& b, float len) {
    if (a + b > len && len > 0.0f) {
      const float s = len / (a + b);
      a *= s;
      b *= s;
    }
  };
  fixEdge(r.topLeft, r.topRight, w);
  fixEdge(r.bottomLeft, r.bottomRight, w);
  fixEdge(r.topLeft, r.bottomLeft, h);
  fixEdge(r.topRight, r.bottomRight, h);
}

} // namespace

void Path::rect(const Rect& rect, const CornerRadius& cornerRadius) {
  const float x = rect.x;
  const float y = rect.y;
  const float w = rect.width;
  const float h = rect.height;
  if (w <= 0.0f || h <= 0.0f) {
    return;
  }

  CornerRadius cr = cornerRadius;
  clampCornerRadii(w, h, cr);

  // Single Rect command so Metal (and future backends) can use the same SDF path as drawRect.
  pushCommand(CommandType::Rect, PathWinding::CounterClockwise,
              {x, y, w, h, cr.topLeft, cr.topRight, cr.bottomRight, cr.bottomLeft});
  invalidateBounds();
}

void Path::circle(const Point& center, float radius) {
  pushCommand(CommandType::Circle, PathWinding::CounterClockwise, {center.x, center.y, radius});
  invalidateBounds();
}

void Path::ellipse(const Point& center, float radiusX, float radiusY) {
  pushCommand(CommandType::Ellipse, PathWinding::CounterClockwise, {center.x, center.y, radiusX, radiusY});
  invalidateBounds();
}

void Path::close() {
  pushCommand(CommandType::Close, PathWinding::CounterClockwise, {});
}

void Path::reset() {
  commands_.clear();
  data_.clear();
  invalidateBounds();
}

bool Path::isEmpty() const {
  return commands_.empty();
}

Rect Path::getBounds() const {
  if (boundsDirty_) {
    updateBounds();
  }
  return cachedBounds_;
}

void Path::invalidateBounds() {
  boundsDirty_ = true;
}

void Path::updateBounds() const {
  if (commands_.empty()) {
    cachedBounds_ = Rect{0, 0, 0, 0};
    boundsDirty_ = false;
    return;
  }

  float minX = std::numeric_limits<float>::max();
  float minY = std::numeric_limits<float>::max();
  float maxX = std::numeric_limits<float>::lowest();
  float maxY = std::numeric_limits<float>::lowest();

  auto updateMinMax = [&](float x, float y) {
    minX = std::min(minX, x);
    minY = std::min(minY, y);
    maxX = std::max(maxX, x);
    maxY = std::max(maxY, y);
  };

  for (const auto& cmd : commands_) {
    const float* d = data_.data() + cmd.dataOffset;
    switch (cmd.type) {
    case CommandType::MoveTo:
    case CommandType::LineTo:
      if (cmd.dataCount >= 2) {
        updateMinMax(d[0], d[1]);
      }
      break;
    case CommandType::QuadTo:
      if (cmd.dataCount >= 4) {
        updateMinMax(d[0], d[1]);
        updateMinMax(d[2], d[3]);
      }
      break;
    case CommandType::BezierTo:
      if (cmd.dataCount >= 6) {
        updateMinMax(d[0], d[1]);
        updateMinMax(d[2], d[3]);
        updateMinMax(d[4], d[5]);
      }
      break;
    case CommandType::ArcTo:
      if (cmd.dataCount >= 5) {
        updateMinMax(d[0], d[1]);
        updateMinMax(d[2], d[3]);
      }
      break;
    case CommandType::Arc:
      if (cmd.dataCount >= 3) {
        updateMinMax(d[0] - d[2], d[1] - d[2]);
        updateMinMax(d[0] + d[2], d[1] + d[2]);
      }
      break;
    case CommandType::Rect:
      if (cmd.dataCount >= 8) {
        updateMinMax(d[0], d[1]);
        updateMinMax(d[0] + d[2], d[1] + d[3]);
      } else if (cmd.dataCount >= 4) {
        updateMinMax(d[0], d[1]);
        updateMinMax(d[0] + d[2], d[1] + d[3]);
      }
      break;
    case CommandType::Circle:
      if (cmd.dataCount >= 3) {
        updateMinMax(d[0] - d[2], d[1] - d[2]);
        updateMinMax(d[0] + d[2], d[1] + d[2]);
      }
      break;
    case CommandType::Ellipse:
      if (cmd.dataCount >= 4) {
        updateMinMax(d[0] - d[2], d[1] - d[3]);
        updateMinMax(d[0] + d[2], d[1] + d[3]);
      }
      break;
    default:
      break;
    }
  }

  cachedBounds_ = Rect{minX, minY, maxX - minX, maxY - minY};
  boundsDirty_ = false;
}

static void hashCombineU64(uint64_t& h, uint64_t v) {
  h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
}

uint64_t Path::contentHash() const {
  uint64_t h = 14695981039346656037ULL;
  for (const auto& cmd : commands_) {
    hashCombineU64(h, static_cast<uint64_t>(static_cast<uint8_t>(cmd.type)));
    hashCombineU64(h, static_cast<uint64_t>(static_cast<uint8_t>(cmd.winding)));
    const float* d = data_.data() + cmd.dataOffset;
    for (uint8_t i = 0; i < cmd.dataCount; ++i) {
      uint32_t bits = 0;
      std::memcpy(&bits, &d[i], sizeof(bits));
      hashCombineU64(h, bits);
    }
  }
  return h;
}

size_t Path::commandCount() const {
  return commands_.size();
}

Path::CommandView Path::command(size_t idx) const {
  const auto& c = commands_[idx];
  return {c.type, c.winding, data_.data() + c.dataOffset, c.dataCount};
}

} // namespace lambdaui
