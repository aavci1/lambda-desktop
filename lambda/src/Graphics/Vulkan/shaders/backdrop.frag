#version 450

layout(set = 1, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;
layout(location = 2) in vec2 vLocal;
layout(location = 3) in vec2 vSize;
layout(location = 4) in vec4 vRadii;
layout(location = 5) in vec2 vWorld;
layout(location = 6) flat in vec4 vClipHeader;
layout(location = 7) flat in vec4 vClipEntries[8];
layout(location = 0) out vec4 outColor;

float roundedRectSDF(vec2 p, vec2 halfSize, vec4 radii) {
  float r = (p.x > 0.0)
              ? ((p.y > 0.0) ? radii.z : radii.y)
              : ((p.y > 0.0) ? radii.w : radii.x);
  vec2 q = abs(p) - halfSize + vec2(r);
  return min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - r;
}

float roundedAlpha(vec2 p, vec2 size, vec4 radii) {
  if (max(max(radii.x, radii.y), max(radii.z, radii.w)) <= 0.0) {
    return 1.0;
  }
  vec2 halfSize = size * 0.5;
  float d = roundedRectSDF(p - halfSize, halfSize, radii);
  float aa = max(0.75 * length(vec2(dFdx(d), dFdy(d))), 0.0001);
  return 1.0 - smoothstep(-aa, aa, d);
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

void main() {
  float mask = roundedAlpha(vLocal, vSize, vRadii) * roundedClipCoverage(vWorld);
  if (mask <= 0.0) {
    discard;
  }
  vec4 c = texture(tex, vUv);
  float luma = dot(c.rgb, vec3(0.299, 0.587, 0.114));
  vec3 vibrant = mix(vec3(luma), c.rgb, 1.34);
  vibrant = min(vibrant * 1.08, vec3(1.0));
  float tintAlpha = clamp(vColor.a, 0.0, 1.0);
  vec3 rgb = mix(vibrant, vColor.rgb, tintAlpha);
  float alpha = mix(c.a, 1.0, tintAlpha) * mask;
  outColor = vec4(rgb, alpha);
}
