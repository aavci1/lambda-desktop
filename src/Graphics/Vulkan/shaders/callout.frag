#version 450

layout(location = 0) in vec2 vLocal;
layout(location = 1) in flat uint vInstance;
layout(location = 2) in vec2 vWorld;
layout(location = 0) out vec4 outColor;

struct CalloutInstance {
  vec4 rect;
  vec4 axisX;
  vec4 axisY;
  vec4 card;
  vec4 radii;
  vec4 base;
  vec4 tint;
  vec4 stroke;
  vec4 params;
  vec4 clipHeader;
  vec4 clipEntries[8];
};

layout(std430, set = 0, binding = 0) readonly buffer Callouts {
  CalloutInstance instances[];
} callouts;

float roundedRectSDF(vec2 p, vec2 halfSize, vec4 radii) {
  float r = (p.x > 0.0)
              ? ((p.y > 0.0) ? radii.z : radii.y)
              : ((p.y > 0.0) ? radii.w : radii.x);
  vec2 q = abs(p) - halfSize + vec2(r);
  return min(max(q.x, q.y), 0.0) + length(max(q, vec2(0.0))) - r;
}

float triangleSDF(vec2 p, vec2 a, vec2 b, vec2 c) {
  vec2 e0 = b - a;
  vec2 e1 = c - b;
  vec2 e2 = a - c;
  vec2 v0 = p - a;
  vec2 v1 = p - b;
  vec2 v2 = p - c;
  vec2 pq0 = v0 - e0 * clamp(dot(v0, e0) / max(dot(e0, e0), 0.000001), 0.0, 1.0);
  vec2 pq1 = v1 - e1 * clamp(dot(v1, e1) / max(dot(e1, e1), 0.000001), 0.0, 1.0);
  vec2 pq2 = v2 - e2 * clamp(dot(v2, e2) / max(dot(e2, e2), 0.000001), 0.0, 1.0);
  float s = sign(e0.x * e2.y - e0.y * e2.x);
  vec2 d = min(min(vec2(dot(pq0, pq0), s * (v0.x * e0.y - v0.y * e0.x)),
                   vec2(dot(pq1, pq1), s * (v1.x * e1.y - v1.y * e1.x))),
                   vec2(dot(pq2, pq2), s * (v2.x * e2.y - v2.y * e2.x)));
  return -sqrt(d.x) * sign(d.y);
}

float antialiasWidth(float d) {
  return max(0.75 * length(vec2(dFdx(d), dFdy(d))), 0.0001);
}

float roundedClipCoverage(CalloutInstance c, vec2 world) {
  int count = clamp(int(c.clipHeader.x + 0.5), 0, 4);
  float coverage = 1.0;
  for (int i = 0; i < count; ++i) {
    vec4 clipRect = c.clipEntries[i * 2];
    vec4 clipRadii = c.clipEntries[i * 2 + 1];
    if (clipRect.z <= 0.0 || clipRect.w <= 0.0 ||
        max(max(clipRadii.x, clipRadii.y), max(clipRadii.z, clipRadii.w)) <= 0.0) {
      continue;
    }
    vec2 halfSize = clipRect.zw * 0.5;
    vec2 local = world - clipRect.xy - halfSize;
    float d = roundedRectSDF(local, halfSize, clipRadii);
    float aa = antialiasWidth(d);
    coverage *= 1.0 - smoothstep(-aa, aa, d);
  }
  return coverage;
}

float arrowSDF(CalloutInstance c, vec2 p) {
  vec2 size = c.rect.zw;
  vec4 card = c.card;
  float aw = max(c.params.z, 0.0);
  int placement = int(c.params.y + 0.5);
  if (aw <= 0.0) {
    return 100000.0;
  }

  if (placement == 0) {
    float x0 = clamp((size.x - aw) * 0.5, c.radii.x, max(c.radii.x, size.x - c.radii.y - aw));
    return triangleSDF(p, vec2(size.x * 0.5, 0.0), vec2(x0 + aw, card.y), vec2(x0, card.y));
  }
  if (placement == 1) {
    float baseY = card.y + card.w;
    float x0 = clamp((size.x - aw) * 0.5, c.radii.w, max(c.radii.w, size.x - c.radii.z - aw));
    return triangleSDF(p, vec2(size.x * 0.5, size.y), vec2(x0, baseY), vec2(x0 + aw, baseY));
  }
  if (placement == 2) {
    float edgeTop = card.y + c.radii.x;
    float edgeBottom = card.y + card.w - c.radii.w;
    float y0 = clamp(size.y * 0.5 - aw * 0.5, edgeTop, max(edgeTop, edgeBottom - aw));
    return triangleSDF(p, vec2(0.0, size.y * 0.5), vec2(card.x, y0), vec2(card.x, y0 + aw));
  }

  float baseX = card.x + card.z;
  float edgeTop = card.y + c.radii.y;
  float edgeBottom = card.y + card.w - c.radii.z;
  float y0 = clamp(size.y * 0.5 - aw * 0.5, edgeTop, max(edgeTop, edgeBottom - aw));
  return triangleSDF(p, vec2(size.x, size.y * 0.5), vec2(baseX, y0 + aw), vec2(baseX, y0));
}

float rangeCoverage(float v, float a, float b, float aa) {
  return smoothstep(a - aa, a + aa, v) * (1.0 - smoothstep(b - aa, b + aa, v));
}

float attachmentCoverage(CalloutInstance c, vec2 p, float aa) {
  vec2 size = c.rect.zw;
  vec4 card = c.card;
  float aw = max(c.params.z, 0.0);
  float borderWidth = max(c.params.x, 0.0);
  int placement = int(c.params.y + 0.5);
  if (aw <= 0.0) {
    return 0.0;
  }

  float inset = min(aw * 0.25, max(borderWidth, aa * 2.0));
  if (inset * 2.0 >= aw) {
    inset = aw * 0.25;
  }
  float band = max(borderWidth * 0.5, aa * 1.5);

  if (placement == 0) {
    float x0 = clamp((size.x - aw) * 0.5, c.radii.x, max(c.radii.x, size.x - c.radii.y - aw));
    float along = rangeCoverage(p.x, x0 + inset, x0 + aw - inset, aa);
    float across = 1.0 - smoothstep(band, band + aa, abs(p.y - card.y));
    return along * across;
  }
  if (placement == 1) {
    float baseY = card.y + card.w;
    float x0 = clamp((size.x - aw) * 0.5, c.radii.w, max(c.radii.w, size.x - c.radii.z - aw));
    float along = rangeCoverage(p.x, x0 + inset, x0 + aw - inset, aa);
    float across = 1.0 - smoothstep(band, band + aa, abs(p.y - baseY));
    return along * across;
  }
  if (placement == 2) {
    float edgeTop = card.y + c.radii.x;
    float edgeBottom = card.y + card.w - c.radii.w;
    float y0 = clamp(size.y * 0.5 - aw * 0.5, edgeTop, max(edgeTop, edgeBottom - aw));
    float along = rangeCoverage(p.y, y0 + inset, y0 + aw - inset, aa);
    float across = 1.0 - smoothstep(band, band + aa, abs(p.x - card.x));
    return along * across;
  }

  float baseX = card.x + card.z;
  float edgeTop = card.y + c.radii.y;
  float edgeBottom = card.y + card.w - c.radii.z;
  float y0 = clamp(size.y * 0.5 - aw * 0.5, edgeTop, max(edgeTop, edgeBottom - aw));
  float along = rangeCoverage(p.y, y0 + inset, y0 + aw - inset, aa);
  float across = 1.0 - smoothstep(band, band + aa, abs(p.x - baseX));
  return along * across;
}

vec4 sourceOver(vec4 top, vec4 bottom) {
  float outA = top.a + bottom.a * (1.0 - top.a);
  if (outA <= 0.0001) {
    return vec4(0.0);
  }
  vec3 outRgb = (top.rgb * top.a + bottom.rgb * bottom.a * (1.0 - top.a)) / outA;
  return vec4(outRgb, outA);
}

void main() {
  CalloutInstance c = callouts.instances[vInstance];
  vec2 cardCenter = c.card.xy + c.card.zw * 0.5;
  float rectD = roundedRectSDF(vLocal - cardCenter, c.card.zw * 0.5, c.radii);
  float d = min(rectD, arrowSDF(c, vLocal));
  float aa = antialiasWidth(d);
  float fillCoverage = 1.0 - smoothstep(-aa, aa, d);
  float attachCoverage = attachmentCoverage(c, vLocal, aa);
  fillCoverage = max(fillCoverage, attachCoverage);
  float borderWidth = c.params.x;
  float strokeCoverage = 0.0;
  if (borderWidth > 0.0) {
    float strokeDistance = abs(d) - borderWidth * 0.5;
    strokeCoverage = 1.0 - smoothstep(-aa, aa, strokeDistance);
    strokeCoverage *= 1.0 - attachCoverage;
  }
  float clipCoverage = roundedClipCoverage(c, vWorld);
  if (clipCoverage <= 0.001) {
    discard;
  }
  float shapeCoverage = max(fillCoverage, strokeCoverage);
  if (shapeCoverage <= 0.001) {
    discard;
  }

  vec4 fill = sourceOver(c.tint, c.base);
  float fillAlpha = fill.a * fillCoverage;
  float strokeAlpha = c.stroke.a * strokeCoverage;
  float outAlpha = strokeAlpha + fillAlpha * (1.0 - strokeAlpha);
  if (outAlpha <= 0.001) {
    discard;
  }
  vec3 outRgb = (c.stroke.rgb * strokeAlpha + fill.rgb * fillAlpha * (1.0 - strokeAlpha)) / outAlpha;
  outColor = vec4(outRgb, outAlpha * clipCoverage);
}
