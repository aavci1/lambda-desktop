#include <Lambda/Reactive/Easing.hpp>

#include <algorithm>
#include <cmath>

namespace lambda::Easing {

float linear(float t) {
  return t;
}

float easeIn(float t) {
  t = std::clamp(t, 0.f, 1.f);
  return t * t * t;
}

float easeOut(float t) {
  t = std::clamp(t, 0.f, 1.f);
  const float u = 1.f - t;
  return 1.f - u * u * u;
}

float easeInOut(float t) {
  t = std::clamp(t, 0.f, 1.f);
  if (t < 0.5f) {
    return 4.f * t * t * t;
  }
  const float u = -2.f * t + 2.f;
  return 1.f - (u * u * u) / 2.f;
}

std::function<float(float)> spring(float stiffness, float damping) {
  return [stiffness, damping](float t) {
    t = std::clamp(t, 0.f, 1.f);
    const int n = 96;
    float x = 0.f;
    float v = 0.f;
    // Fixed step count with `dt = t/n` — step size scales with `t`; visually fine for UI, not a fixed-timestep physics solve.
    const float dt = t / static_cast<float>(n);
    for (int i = 0; i < n; ++i) {
      const float a = stiffness * (1.f - x) - damping * v;
      v += a * dt;
      x += v * dt;
    }
    return x;
  };
}

} // namespace lambda::Easing
