#version 450

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vLocal;
layout(location = 2) in vec4 vFill0;
layout(location = 3) in vec4 vFill1;
layout(location = 4) in vec4 vFill2;
layout(location = 5) in vec4 vFill3;
layout(location = 6) in vec4 vStops;
layout(location = 7) in vec4 vGradient;
layout(location = 8) in vec4 vParams;
layout(location = 9) in vec2 vWorld;
layout(location = 10) flat in vec4 vClipHeader;
layout(location = 11) flat in vec4 vClipEntries[8];
layout(location = 0) out vec4 outColor;

float roundedRectSDF(vec2 p, vec2 halfSize, vec4 radii) {
  float r = (p.x > 0.0)
              ? ((p.y > 0.0) ? radii.z : radii.y)
              : ((p.y > 0.0) ? radii.w : radii.x);
  vec2 q = abs(p) - halfSize + vec2(r);
  return min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - r;
}

float roundedClipCoverage(vec2 world) {
  int count = clamp(int(vClipHeader.x + 0.5), 0, 4);
  float coverage = 1.0;
  for (int i = 0; i < count; ++i) {
    vec4 clipRect = vClipEntries[i * 2];
    vec4 clipRadii = vClipEntries[i * 2 + 1];
    if (clipRect.z <= 0.0 || clipRect.w <= 0.0 ||
        max(max(clipRadii.x, clipRadii.y), max(clipRadii.z, clipRadii.w)) <= 0.0) {
      continue;
    }
    vec2 halfSize = clipRect.zw * 0.5;
    vec2 local = world - clipRect.xy - halfSize;
    float d = roundedRectSDF(local, halfSize, clipRadii);
    float aa = max(0.75 * length(vec2(dFdx(d), dFdy(d))), 0.0001);
    coverage *= 1.0 - smoothstep(-aa, aa, d);
  }
  return coverage;
}

vec4 sampleStops(float t) {
  t = clamp(t, 0.0, 1.0);
  vec4 colors[4] = vec4[](vFill0, vFill1, vFill2, vFill3);
  float stops[4] = float[](vStops.x, vStops.y, vStops.z, vStops.w);
  int count = int(vParams.y + 0.5);
  if (count <= 1 || t <= stops[0]) {
    return colors[0];
  }
  for (int i = 1; i < count; ++i) {
    if (t <= stops[i]) {
      float span = max(0.000001, stops[i] - stops[i - 1]);
      return mix(colors[i - 1], colors[i], (t - stops[i - 1]) / span);
    }
  }
  return colors[count - 1];
}

vec4 pathColor() {
  int type = int(vParams.x + 0.5);
  if (type == 1) {
    vec2 a = vGradient.xy;
    vec2 b = vGradient.zw;
    vec2 d = b - a;
    float t = dot(vLocal - a, d) / max(dot(d, d), 0.000001);
    vec4 c = sampleStops(t);
    c.a *= vParams.w;
    return c;
  }
  if (type == 2) {
    vec2 d = vLocal - vGradient.xy;
    vec4 c = sampleStops(length(d) / max(vGradient.z, 0.000001));
    c.a *= vParams.w;
    return c;
  }
  if (type == 3) {
    float angle = atan(vLocal.y - vGradient.y, vLocal.x - vGradient.x) - vGradient.z;
    vec4 c = sampleStops(fract(angle / 6.28318530718));
    c.a *= vParams.w;
    return c;
  }
  return vColor;
}

void main() {
  vec4 color = pathColor();
  color.a *= roundedClipCoverage(vWorld);
  if (color.a <= 0.001) {
    discard;
  }
  outColor = color;
}
