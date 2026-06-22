#pragma once

#include <Lambda/Core/Geometry.hpp>

#include <optional>

namespace lambda {

class WaylandScrollAccumulator {
  public:
    void addAxis(bool horizontal, float value) noexcept {
      if (horizontal) {
        delta_.x += value;
      } else {
        delta_.y += value;
      }
      pending_ = true;
    }

    [[nodiscard]] bool pending() const noexcept { return pending_; }

    std::optional<Vec2> take() noexcept {
      if (!pending_) {
        return std::nullopt;
      }
      Vec2 const delta = delta_;
      pending_ = false;
      delta_ = {};
      if (delta.x == 0.f && delta.y == 0.f) {
        return std::nullopt;
      }
      return delta;
    }

  private:
    Vec2 delta_{};
    bool pending_ = false;
};

} // namespace lambda
