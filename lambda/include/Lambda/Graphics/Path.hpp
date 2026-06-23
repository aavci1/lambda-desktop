#pragma once

/// \file Lambda/Graphics/Path.hpp
///
/// Part of the Lambda public API.


#include <Lambda/Core/Geometry.hpp>
#include <cstdint>
#include <vector>

namespace lambdaui {

enum class PathWinding {
  CounterClockwise,  // Solid shapes
  Clockwise          // Holes
};

/**
 * Path — vector path for drawing and clipping (SkPath / QPainterPath style).
 *
 * Command parameters live in a single flat float buffer (`data_`) to avoid
 * per-command heap allocations.
 */
class Path {
public:
  Path();
  ~Path();

  Path(const Path& other);
  Path& operator=(const Path& other);
  Path(Path&& other) noexcept;
  Path& operator=(Path&& other) noexcept;

  void setWinding(PathWinding winding);
  void moveTo(const Point& point);
  void lineTo(const Point& point);
  void quadTo(const Point& control, const Point& end);
  void bezierTo(const Point& c1, const Point& c2, const Point& end);
  void arcTo(const Point& p1, const Point& p2, float radius);
  void arc(const Point& center, float radius, float startAngle, float endAngle, bool clockwise = false);
  void rect(const Rect& rect, const CornerRadius& cornerRadius = CornerRadius());
  void circle(const Point& center, float radius);
  void ellipse(const Point& center, float radiusX, float radiusY);
  void close();
  void reset();

  bool isEmpty() const;
  Rect getBounds() const;
  uint64_t contentHash() const;

  enum class CommandType : uint8_t {
    SetWinding,
    MoveTo,
    LineTo,
    QuadTo,
    BezierTo,
    ArcTo,
    Arc,
    /// Packed: x, y, width, height, tl, tr, br, bl corner radii (same as `Rect` + `CornerRadius`).
    Rect,
    Circle,
    Ellipse,
    Close
  };

  struct Command {
    CommandType type;
    PathWinding winding = PathWinding::CounterClockwise;
    uint32_t dataOffset = 0;
    uint8_t dataCount = 0;

    bool operator==(Command const& other) const = default;
  };

  struct CommandView {
    CommandType type;
    PathWinding winding;
    const float* data;
    uint8_t dataCount;
  };

  size_t commandCount() const;

  CommandView command(size_t idx) const;

  bool operator==(Path const& other) const;

private:
  std::vector<Command> commands_;
  std::vector<float> data_;
  mutable Rect cachedBounds_{};
  mutable bool boundsDirty_ = true;

  void pushCommand(CommandType type, PathWinding w, std::initializer_list<float> params);
  void invalidateBounds();
  void updateBounds() const;
};

} // namespace lambdaui
