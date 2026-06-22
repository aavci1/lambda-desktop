#include <Lambda/UI/Views/BackdropBlur.hpp>

#include <Lambda/UI/Views/Render.hpp>

namespace lambda {

Element BackdropBlur::body() const {
  return Render{
      .draw = [radius = radius, tint = tint, corners = corners](Canvas& canvas, Rect frame) {
        canvas.drawBackdropBlur(Rect{0.f, 0.f, frame.width, frame.height}, radius, tint, corners);
      },
  };
}

} // namespace lambda
