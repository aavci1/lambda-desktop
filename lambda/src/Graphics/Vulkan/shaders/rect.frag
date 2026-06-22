#version 450

layout(location = 0) in vec2 vLocal;
layout(location = 1) in flat uint vInstance;
layout(location = 2) in vec2 vWorld;
layout(location = 0) out vec4 outColor;

struct RectInstance {
  vec4 rect;
  vec4 axisX;
  vec4 axisY;
  vec4 radii;
  vec4 fill0;
  vec4 fill1;
  vec4 fill2;
  vec4 fill3;
  vec4 stops;
  vec4 gradient;
  vec4 stroke;
  vec4 params;
  vec4 clipHeader;
  vec4 clipEntries[8];
};

layout(std430, set = 0, binding = 0) readonly buffer Rects {
  RectInstance instances[];
} rects;

float roundedRectSDF(vec2 p, vec2 halfSize, vec4 radii) {
  float r = (p.x > 0.0)
              ? ((p.y > 0.0) ? radii.z : radii.y)
              : ((p.y > 0.0) ? radii.w : radii.x);
  vec2 q = abs(p) - halfSize + vec2(r);
  return min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - r;
}

float antialiasWidth(float d) {
  return max(0.75 * length(vec2(dFdx(d), dFdy(d))), 0.0001);
}

float distanceCoverage(float d) {
  float aa = antialiasWidth(d);
  return 1.0 - smoothstep(-aa, aa, d);
}

float roundedClipCoverage(RectInstance r, vec2 world) {
  int count = clamp(int(r.clipHeader.x + 0.5), 0, 4);
  float coverage = 1.0;
  for (int i = 0; i < count; ++i) {
    vec4 clipRect = r.clipEntries[i * 2];
    vec4 clipRadii = r.clipEntries[i * 2 + 1];
    if (clipRect.z <= 0.0 || clipRect.w <= 0.0 ||
        max(max(clipRadii.x, clipRadii.y), max(clipRadii.z, clipRadii.w)) <= 0.0) {
      continue;
    }
    vec2 halfSize = clipRect.zw * 0.5;
    vec2 local = world - clipRect.xy - halfSize;
    float d = roundedRectSDF(local, halfSize, clipRadii);
    coverage *= distanceCoverage(d);
  }
  return coverage;
}

vec4 sampleStops(RectInstance r, float t) {
  t = clamp(t, 0.0, 1.0);
  vec4 colors[4] = vec4[](r.fill0, r.fill1, r.fill2, r.fill3);
  float stops[4] = float[](r.stops.x, r.stops.y, r.stops.z, r.stops.w);
  int count = int(r.params.y + 0.5);
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

vec4 fillColor(RectInstance r, vec2 p) {
  int type = int(r.params.x + 0.5);
  vec2 uv = p / max(r.rect.zw, vec2(0.000001));
  if (type == 1) {
    vec2 a = r.gradient.xy;
    vec2 b = r.gradient.zw;
    vec2 d = b - a;
    float t = dot(uv - a, d) / max(dot(d, d), 0.000001);
    return sampleStops(r, t);
  }
  if (type == 2) {
    vec2 d = uv - r.gradient.xy;
    float t = length(d) / max(r.gradient.z, 0.000001);
    return sampleStops(r, t);
  }
  if (type == 3) {
    float angle = atan(uv.y - r.gradient.y, uv.x - r.gradient.x) - r.gradient.z;
    float t = fract(angle / 6.28318530718);
    return sampleStops(r, t);
  }
  return r.fill0;
}

void main() {
  RectInstance r = rects.instances[vInstance];
  vec2 size = r.rect.zw;
  vec2 halfSize = size * 0.5;
  vec2 local = vLocal - halfSize;
  float d = roundedRectSDF(local, halfSize, r.radii);
  float aa = antialiasWidth(d);
  float fillCoverage = 1.0 - smoothstep(-aa, aa, d);
  float strokeWidth = r.params.z;
  float strokeCoverage = 0.0;
  if (strokeWidth > 0.0) {
    float strokeDistance = abs(d) - strokeWidth * 0.5;
    strokeCoverage = 1.0 - smoothstep(-aa, aa, strokeDistance);
  }
  float clipCoverage = roundedClipCoverage(r, vWorld);
  if (clipCoverage <= 0.001) {
    discard;
  }
  float shapeCoverage = max(fillCoverage, strokeCoverage);
  if (shapeCoverage <= 0.001) {
    discard;
  }
  vec4 fill = fillColor(r, vLocal);
  float fillAlpha = fill.a * fillCoverage;
  float strokeAlpha = r.stroke.a * strokeCoverage;
  float outAlpha = strokeAlpha + fillAlpha * (1.0 - strokeAlpha);
  if (outAlpha <= 0.001) {
    discard;
  }
  vec3 outRgb = (r.stroke.rgb * strokeAlpha + fill.rgb * fillAlpha * (1.0 - strokeAlpha)) / outAlpha;
  vec4 color = vec4(outRgb, outAlpha);
  color.a *= r.params.w * clipCoverage;
  outColor = color;
}
