// SDF shaders adapted from aavci1/lambda (GLSL → Metal).

#include <metal_stdlib>
using namespace metal;

// -----------------------------------------------------------------------------
// Rounded rect (sdf_quad.vert + rect.frag)
// -----------------------------------------------------------------------------

struct RectInstance {
  float4 rect;
  float4 corners;
  float4 fillColor;
  float4 strokeColor;
  float2 strokeWidthOpacity;
  float2 viewport;
  float4 rotationPad;
  float4 shadowColor;
  float4 shadowGeom;
  float4 gradientColor1;
  float4 gradientColor2;
  float4 gradientColor3;
  float4 gradientStops;
  float4 gradientPoints;
};

constant uint kMaxRoundedClipMasks = 4;

struct RoundedClipStack {
  float4 header;
  float4 entries[kMaxRoundedClipMasks * 2];
};

struct DrawUniforms {
  float2 viewport;
  float2 translation;
};

struct RectVertexOut {
  /// Clip-space output (NDC in xy).
  float4 clip [[position]];
  float2 fragLocalPos;
  float2 fragHalfSize;
  float4 fragCorners;
  float4 fragFillColor;
  float4 fragStrokeColor;
  float fragStrokeWidth;
  float fragOpacity;
  float2 fragCenter;
  float fragAngle;
  float2 fragViewport;
  float4 fragShadowColor;
  float4 fragShadowGeom;
  float4 fragGradientColor1;
  float4 fragGradientColor2;
  float4 fragGradientColor3;
  float4 fragGradientStops;
  float4 fragGradientPoints;
  float fragGradientStopCount;
  float fragGradientType;
};

/// Same layout as `RectVertexOut`, but `clip` has no `[[position]]` so the fragment can also take `float4 [[position]]` for pixel coords (Metal allows only one `[[position]]` per fragment signature when using `[[stage_in]]`).
struct RectFragmentIn {
  float4 clip;
  float2 fragLocalPos;
  float2 fragHalfSize;
  float4 fragCorners;
  float4 fragFillColor;
  float4 fragStrokeColor;
  float fragStrokeWidth;
  float fragOpacity;
  float2 fragCenter;
  float fragAngle;
  float2 fragViewport;
  float4 fragShadowColor;
  float4 fragShadowGeom;
  float4 fragGradientColor1;
  float4 fragGradientColor2;
  float4 fragGradientColor3;
  float4 fragGradientStops;
  float4 fragGradientPoints;
  float fragGradientStopCount;
  float fragGradientType;
};

vertex RectVertexOut rect_sdf_vert(uint vid [[vertex_id]], uint iid [[instance_id]],
                                   constant float2* quad [[buffer(0)]],
                                   constant RectInstance* instances [[buffer(1)]],
                                   constant DrawUniforms* uniforms [[buffer(2)]]) {
  RectInstance inst = instances[iid];
  float2 halfSize = inst.rect.zw * 0.5f;
  float2 center = inst.rect.xy + halfSize + uniforms[0].translation;
  float shadowPad = 0.0f;
  if (inst.shadowColor.a > 0.0001f && inst.shadowGeom.z > 0.0f) {
    shadowPad = inst.shadowGeom.z + length(inst.shadowGeom.xy);
  }
  float pad = max(max(inst.strokeWidthOpacity.x, 1.0f), shadowPad);
  float2 paddedHalf = halfSize + pad;
  float2 localOffset = quad[vid] * paddedHalf;
  float cr = cos(inst.rotationPad.x);
  float sr = sin(inst.rotationPad.x);
  float2 worldOffset =
      float2(localOffset.x * cr - localOffset.y * sr, localOffset.x * sr + localOffset.y * cr);
  float2 screenPos = center + worldOffset;
  float2 ndc = (screenPos / uniforms[0].viewport) * 2.0f - 1.0f;
  ndc.y = -ndc.y;

  RectVertexOut out;
  out.clip = float4(ndc, 0.0f, 1.0f);
  out.fragLocalPos = localOffset;
  out.fragHalfSize = halfSize;
  out.fragCorners = inst.corners;
  out.fragFillColor = inst.fillColor;
  out.fragStrokeColor = inst.strokeColor;
  out.fragStrokeWidth = inst.strokeWidthOpacity.x;
  out.fragOpacity = inst.strokeWidthOpacity.y;
  out.fragCenter = center;
  out.fragAngle = inst.rotationPad.x;
  out.fragViewport = uniforms[0].viewport;
  out.fragShadowColor = inst.shadowColor;
  out.fragShadowGeom = inst.shadowGeom;
  out.fragGradientColor1 = inst.gradientColor1;
  out.fragGradientColor2 = inst.gradientColor2;
  out.fragGradientColor3 = inst.gradientColor3;
  out.fragGradientStops = inst.gradientStops;
  out.fragGradientPoints = inst.gradientPoints;
  out.fragGradientStopCount = inst.rotationPad.y;
  out.fragGradientType = inst.rotationPad.z;
  return out;
}

static float roundedRectSDF(float2 p, float2 halfSize, float4 corners) {
  float r = (p.x > 0.0f) ? ((p.y > 0.0f) ? corners.z : corners.y) : ((p.y > 0.0f) ? corners.w : corners.x);
  float2 q = abs(p) - halfSize + r;
  return min(max(q.x, q.y), 0.0f) + length(max(q, float2(0.0f))) - r;
}

static float roundedClipCoverage(float2 pixel, constant RoundedClipStack& clips) {
  float coverage = 1.0f;
  uint count = min((uint)clips.header.x, kMaxRoundedClipMasks);
  for (uint i = 0; i < count; ++i) {
    float4 clipRect = clips.entries[i * 2];
    float4 clipCorners = clips.entries[i * 2 + 1];
    float2 halfSize = clipRect.zw * 0.5f;
    float2 center = clipRect.xy + halfSize;
    float d = roundedRectSDF(pixel - center, halfSize, clipCorners);
    coverage *= 1.0f - smoothstep(-0.75f, 0.75f, d);
    if (coverage <= 0.001f) {
      return 0.0f;
    }
  }
  return coverage;
}

static float4 gradientColor(RectFragmentIn in, float2 localPos) {
  if (in.fragGradientStopCount < 2.0f || in.fragGradientType < 0.5f) {
    return in.fragFillColor;
  }
  float2 denom = max(2.0f * in.fragHalfSize, float2(1e-4f));
  float2 unit = (localPos + in.fragHalfSize) / denom;
  float t = 0.0f;
  if (in.fragGradientType < 1.5f) {
    float2 start = in.fragGradientPoints.xy;
    float2 end = in.fragGradientPoints.zw;
    float2 axis = end - start;
    float axisLenSq = dot(axis, axis);
    t = axisLenSq > 1e-8f ? clamp(dot(unit - start, axis) / axisLenSq, 0.0f, 1.0f) : 0.0f;
  } else if (in.fragGradientType < 2.5f) {
    float radius = max(in.fragGradientPoints.z, 1e-4f);
    t = clamp(distance(unit, in.fragGradientPoints.xy) / radius, 0.0f, 1.0f);
  } else {
    const float twoPi = 6.28318530718f;
    float2 delta = unit - in.fragGradientPoints.xy;
    float angle = atan2(delta.y, delta.x) - in.fragGradientPoints.z;
    t = fract(angle / twoPi);
  }

  float4 colors[4] = {in.fragFillColor, in.fragGradientColor1, in.fragGradientColor2, in.fragGradientColor3};
  float stops[4] = {in.fragGradientStops.x, in.fragGradientStops.y, in.fragGradientStops.z, in.fragGradientStops.w};
  int stopCount = min(max((int)(in.fragGradientStopCount + 0.5f), 2), 4);
  if (t <= stops[0]) {
    return colors[0];
  }
  for (int i = 0; i < stopCount - 1; ++i) {
    float a = stops[i];
    float b = stops[i + 1];
    if (t <= b || i == stopCount - 2) {
      float span = max(b - a, 1e-5f);
      float u = clamp((t - a) / span, 0.0f, 1.0f);
      return mix(colors[i], colors[i + 1], u);
    }
  }
  return colors[stopCount - 1];
}

fragment float4 rect_sdf_frag(RectFragmentIn in [[stage_in]], float4 fragCoord [[position]],
                              constant RoundedClipStack* clips [[buffer(0)]]) {
  // Built-in fragment position: pixel coordinates in the render target (same space as vertex `screenPos` / inst.rect).
  float2 pixel = fragCoord.xy;
  float clipCoverage = roundedClipCoverage(pixel, clips[0]);
  if (clipCoverage <= 0.001f)
    discard_fragment();
  float2 delta = pixel - in.fragCenter;
  float ca = cos(-in.fragAngle);
  float sa = sin(-in.fragAngle);
  float2 p = float2(delta.x * ca - delta.y * sa, delta.x * sa + delta.y * ca);
  float d = roundedRectSDF(p, in.fragHalfSize, in.fragCorners);
  float fillCoverage = 1.0f - smoothstep(-0.75f, 0.75f, d);
  float strokeCoverage = 0.0f;
  if (in.fragStrokeWidth > 0.0f) {
    float sd = abs(d) - in.fragStrokeWidth * 0.5f;
    strokeCoverage = 1.0f - smoothstep(-0.75f, 0.75f, sd);
  }
  float4 fillColor = gradientColor(in, p);
  float fillA = fillColor.a * fillCoverage;
  float strokeA = in.fragStrokeColor.a * strokeCoverage;
  float4 fillP = float4(fillColor.rgb * fillA, fillA);
  float4 strokeP = float4(in.fragStrokeColor.rgb * strokeA, strokeA);
  float4 blended = strokeP + fillP * (1.0f - strokeP.a);

  float4 shadowP = float4(0.0f);
  if (in.fragShadowColor.a > 0.0001f && in.fragShadowGeom.z > 0.0f) {
    float2 shadowOff = in.fragShadowGeom.xy;
    float2 localShadow = float2(shadowOff.x * ca - shadowOff.y * sa, shadowOff.x * sa + shadowOff.y * ca);
    float2 ps = p - localShadow;
    float ds = roundedRectSDF(ps, in.fragHalfSize, in.fragCorners);
    float blur = max(in.fragShadowGeom.z, 0.5f);
    float soft = 1.0f - smoothstep(-blur, blur, ds);
    float shA = in.fragShadowColor.a * soft;
    shadowP = float4(in.fragShadowColor.rgb * shA, shA);
  }
  // Block shadow under the shape's ink using geometric coverage (not premultiplied alpha), so
  // semi-transparent fills (e.g. secondary buttons) do not show shadow through the interior.
  float shapeMask = max(fillCoverage, strokeCoverage);
  shadowP *= (1.0f - shapeMask);
  // Source-over: shape on top of shadow (premultiplied).
  blended = blended + shadowP * (1.0f - blended.a);

  float outA = blended.a * in.fragOpacity;
  if (outA < 0.001f)
    discard_fragment();
  // Premultiplied RGBA; blend state uses (One, OneMinusSourceAlpha) like glyph pipeline.
  return float4(blended.rgb * (in.fragOpacity * clipCoverage), outA * clipCoverage);
}

// -----------------------------------------------------------------------------
// Textured rounded rect (same SDF mask as rect; UV from sub-rect or tile)
// -----------------------------------------------------------------------------

struct ImageInstance {
  float4 rect;
  float4 corners;
  float4 fillColor;
  float4 strokeColor;
  float2 strokeWidthOpacity;
  float2 viewport;
  float4 rotationPad;
  float4 shadowColor;
  float4 shadowGeom;
  float4 gradientColor1;
  float4 gradientColor2;
  float4 gradientColor3;
  float4 gradientStops;
  float4 gradientPoints;
  float4 uvBounds;
  float2 texSizeInv;
  float2 imageModePad;
};

struct ImageVertexOut {
  float4 clip [[position]];
  float2 fragLocalPos;
  float2 fragHalfSize;
  float4 fragCorners;
  float4 fragFillColor;
  float4 fragStrokeColor;
  float fragStrokeWidth;
  float fragOpacity;
  float2 fragCenter;
  float fragAngle;
  float2 fragViewport;
  float4 fragUvBounds;
  float2 fragTexSizeInv;
  float fragImageMode [[flat]];
};

struct ImageFragmentIn {
  float4 clip;
  float2 fragLocalPos;
  float2 fragHalfSize;
  float4 fragCorners;
  float4 fragFillColor;
  float4 fragStrokeColor;
  float fragStrokeWidth;
  float fragOpacity;
  float2 fragCenter;
  float fragAngle;
  float2 fragViewport;
  float4 fragUvBounds;
  float2 fragTexSizeInv;
  float fragImageMode;
};

vertex ImageVertexOut image_sdf_vert(uint vid [[vertex_id]], uint iid [[instance_id]], constant float2* quad [[buffer(0)]],
                                     constant ImageInstance* instances [[buffer(1)]],
                                     constant DrawUniforms* uniforms [[buffer(2)]]) {
  ImageInstance inst = instances[iid];
  float2 halfSize = inst.rect.zw * 0.5f;
  float2 center = inst.rect.xy + halfSize + uniforms[0].translation;
  float shadowPad = 0.0f;
  if (inst.shadowColor.a > 0.0001f && inst.shadowGeom.z > 0.0f) {
    shadowPad = inst.shadowGeom.z + length(inst.shadowGeom.xy);
  }
  float pad = max(max(inst.strokeWidthOpacity.x, 1.0f), shadowPad);
  float2 paddedHalf = halfSize + pad;
  float2 localOffset = quad[vid] * paddedHalf;
  float cr = cos(inst.rotationPad.x);
  float sr = sin(inst.rotationPad.x);
  float2 worldOffset =
      float2(localOffset.x * cr - localOffset.y * sr, localOffset.x * sr + localOffset.y * cr);
  float2 screenPos = center + worldOffset;
  float2 ndc = (screenPos / uniforms[0].viewport) * 2.0f - 1.0f;
  ndc.y = -ndc.y;

  ImageVertexOut out;
  out.clip = float4(ndc, 0.0f, 1.0f);
  out.fragLocalPos = localOffset;
  out.fragHalfSize = halfSize;
  out.fragCorners = inst.corners;
  out.fragFillColor = inst.fillColor;
  out.fragStrokeColor = inst.strokeColor;
  out.fragStrokeWidth = inst.strokeWidthOpacity.x;
  out.fragOpacity = inst.strokeWidthOpacity.y;
  out.fragCenter = center;
  out.fragAngle = inst.rotationPad.x;
  out.fragViewport = uniforms[0].viewport;
  out.fragUvBounds = inst.uvBounds;
  out.fragTexSizeInv = inst.texSizeInv;
  out.fragImageMode = inst.imageModePad.x;
  return out;
}

fragment float4 image_sdf_frag(ImageFragmentIn in [[stage_in]], float4 fragCoord [[position]],
                               constant RoundedClipStack* clips [[buffer(0)]], texture2d<float> tex [[texture(0)]],
                               sampler smpl [[sampler(0)]]) {
  float2 pixel = fragCoord.xy;
  float clipCoverage = roundedClipCoverage(pixel, clips[0]);
  if (clipCoverage <= 0.001f)
    discard_fragment();
  float2 delta = pixel - in.fragCenter;
  float ca = cos(-in.fragAngle);
  float sa = sin(-in.fragAngle);
  float2 p = float2(delta.x * ca - delta.y * sa, delta.x * sa + delta.y * ca);
  float d = roundedRectSDF(p, in.fragHalfSize, in.fragCorners);
  float coverage = 1.0f - smoothstep(-0.75f, 0.75f, d);
  float2 local = p + in.fragHalfSize;
  float2 denom = max(2.0f * in.fragHalfSize, float2(1e-4f));
  float2 t = float2(local.x / denom.x, local.y / denom.y);
  float2 uv;
  if (in.fragImageMode < 0.5f) {
    uv = float2(mix(in.fragUvBounds.x, in.fragUvBounds.z, t.x), mix(in.fragUvBounds.y, in.fragUvBounds.w, t.y));
  } else {
    uv = float2(local.x * in.fragTexSizeInv.x, local.y * in.fragTexSizeInv.y);
  }
  float4 c = tex.sample(smpl, uv);
  float4 premul = float4(c.rgb * c.a, c.a);
  premul *= coverage * in.fragOpacity * clipCoverage;
  if (premul.a < 0.001f)
    discard_fragment();
  return premul;
}

// -----------------------------------------------------------------------------
// Backdrop blur (samples a previously rendered scene texture)
// -----------------------------------------------------------------------------

struct BackdropUniforms {
  float4 rect;       // x, y, width, height in render-target pixels
  float4 tint;       // straight RGBA tint
  float4 corners;    // top-left, top-right, bottom-right, bottom-left in render-target pixels
  float4 params;     // viewport.xy, texelSize.xy
  float4 blurParams; // radiusPx, axis.xy, unused
};

struct BackdropVertexOut {
  float4 clip [[position]];
  float2 uv;
  float2 localPos;
  float2 halfSize;
};

vertex BackdropVertexOut backdrop_vert(uint vid [[vertex_id]], constant float2* quad [[buffer(0)]],
                                       constant BackdropUniforms* uniforms [[buffer(1)]]) {
  float2 unit = quad[vid] * 0.5f + 0.5f;
  float2 pixel = uniforms[0].rect.xy + unit * uniforms[0].rect.zw;
  float2 ndc = (pixel / uniforms[0].params.xy) * 2.0f - 1.0f;
  ndc.y = -ndc.y;

  BackdropVertexOut out;
  out.clip = float4(ndc, 0.0f, 1.0f);
  out.uv = pixel * uniforms[0].params.zw;
  out.halfSize = uniforms[0].rect.zw * 0.5f;
  out.localPos = unit * uniforms[0].rect.zw - out.halfSize;
  return out;
}

fragment float4 backdrop_gaussian_frag(BackdropVertexOut in [[stage_in]],
                                       constant BackdropUniforms* uniforms [[buffer(0)]],
                                       texture2d<float> scene [[texture(0)]],
                                       sampler smpl [[sampler(0)]]) {
  float radius = max(uniforms[0].blurParams.x, 0.0f);
  float2 axis = uniforms[0].blurParams.yz;
  if (radius <= 0.01f) {
    return scene.sample(smpl, in.uv);
  }

  float2 unitOffset = axis * uniforms[0].params.zw * (radius / 8.0f);
  float4 c = scene.sample(smpl, in.uv) * 0.11283128f;
  c += (scene.sample(smpl, in.uv + unitOffset * 1.0f) +
        scene.sample(smpl, in.uv - unitOffset * 1.0f)) * 0.10856112f;
  c += (scene.sample(smpl, in.uv + unitOffset * 2.0f) +
        scene.sample(smpl, in.uv - unitOffset * 2.0f)) * 0.09669606f;
  c += (scene.sample(smpl, in.uv + unitOffset * 3.0f) +
        scene.sample(smpl, in.uv - unitOffset * 3.0f)) * 0.07973203f;
  c += (scene.sample(smpl, in.uv + unitOffset * 4.0f) +
        scene.sample(smpl, in.uv - unitOffset * 4.0f)) * 0.06086204f;
  c += (scene.sample(smpl, in.uv + unitOffset * 5.0f) +
        scene.sample(smpl, in.uv - unitOffset * 5.0f)) * 0.04300806f;
  c += (scene.sample(smpl, in.uv + unitOffset * 6.0f) +
        scene.sample(smpl, in.uv - unitOffset * 6.0f)) * 0.02813473f;
  c += (scene.sample(smpl, in.uv + unitOffset * 7.0f) +
        scene.sample(smpl, in.uv - unitOffset * 7.0f)) * 0.01703826f;
  c += (scene.sample(smpl, in.uv + unitOffset * 8.0f) +
        scene.sample(smpl, in.uv - unitOffset * 8.0f)) * 0.00955207f;
  return c;
}

fragment float4 backdrop_frag(BackdropVertexOut in [[stage_in]],
                              constant BackdropUniforms* uniforms [[buffer(0)]],
                              texture2d<float> blurredScene [[texture(0)]],
                              sampler smpl [[sampler(0)]]) {
  float coverage = 1.0f;
  if (any(uniforms[0].corners > float4(0.0f))) {
    float d = roundedRectSDF(in.localPos, in.halfSize, uniforms[0].corners);
    coverage = 1.0f - smoothstep(-0.75f, 0.75f, d);
    if (coverage <= 0.001f) {
      discard_fragment();
    }
  }

  float4 c = blurredScene.sample(smpl, in.uv);
  float luma = dot(c.rgb, float3(0.299, 0.587, 0.114));
  float3 vibrant = mix(float3(luma), c.rgb, 1.34);
  vibrant = min(vibrant * 1.08, float3(1.0));
  c.rgb = vibrant;
  float4 tint = uniforms[0].tint;
  float4 tintP = float4(tint.rgb * tint.a, tint.a);
  return (tintP + c * (1.0f - tintP.a)) * coverage;
}

// -----------------------------------------------------------------------------
// Line capsule (sdf_line.vert + line.frag)
// -----------------------------------------------------------------------------

vertex RectVertexOut line_sdf_vert(uint vid [[vertex_id]], uint iid [[instance_id]],
                                   constant float2* quad [[buffer(0)]],
                                   constant RectInstance* instances [[buffer(1)]],
                                   constant DrawUniforms* uniforms [[buffer(2)]]) {
  RectInstance inst = instances[iid];
  float2 halfSize = inst.rect.zw * 0.5f;
  float2 center = inst.rect.xy + halfSize + uniforms[0].translation;
  float pad = max(inst.strokeWidthOpacity.x, 1.0f);
  float2 paddedHalf = halfSize + pad;
  float2 localOffset = quad[vid] * paddedHalf;
  float cosA = inst.corners.x;
  float sinA = inst.corners.y;
  float2 lineRotated =
      float2(localOffset.x * cosA - localOffset.y * sinA, localOffset.x * sinA + localOffset.y * cosA);
  float cr = cos(inst.rotationPad.x);
  float sr = sin(inst.rotationPad.x);
  float2 rotatedOffset =
      float2(lineRotated.x * cr - lineRotated.y * sr, lineRotated.x * sr + lineRotated.y * cr);
  float2 screenPos = center + rotatedOffset;
  float2 ndc = (screenPos / uniforms[0].viewport) * 2.0f - 1.0f;
  ndc.y = -ndc.y;

  RectVertexOut out;
  out.clip = float4(ndc, 0.0f, 1.0f);
  out.fragLocalPos = localOffset;
  out.fragHalfSize = halfSize;
  out.fragCorners = inst.corners;
  out.fragFillColor = inst.fillColor;
  out.fragStrokeColor = inst.strokeColor;
  out.fragStrokeWidth = inst.strokeWidthOpacity.x;
  out.fragOpacity = inst.strokeWidthOpacity.y;
  out.fragCenter = center;
  out.fragAngle = inst.rotationPad.x;
  out.fragViewport = uniforms[0].viewport;
  out.fragShadowColor = float4(0.0f);
  out.fragShadowGeom = float4(0.0f);
  out.fragGradientColor1 = float4(0.0f);
  out.fragGradientColor2 = float4(0.0f);
  out.fragGradientColor3 = float4(0.0f);
  out.fragGradientStops = float4(0.0f);
  out.fragGradientPoints = float4(0.0f);
  out.fragGradientStopCount = 0.0f;
  out.fragGradientType = 0.0f;
  return out;
}

fragment float4 line_sdf_frag(RectFragmentIn in [[stage_in]], float4 fragCoord [[position]],
                              constant RoundedClipStack* clips [[buffer(0)]]) {
  // Capsule stroke in line space: +u along segment, v perpendicular.
  // Real half-length is `fragCorners.z` (device px). `fragHalfSize.x` includes quad padding and must
  // not be used for the SDF or ink extends past the true endpoints.
  float2 pixel = fragCoord.xy;
  float clipCoverage = roundedClipCoverage(pixel, clips[0]);
  if (clipCoverage <= 0.001f)
    discard_fragment();
  float2 delta = pixel - in.fragCenter;
  float cosA = in.fragCorners.x;
  float sinA = in.fragCorners.y;
  float cr = cos(-in.fragAngle);
  float sr = sin(-in.fragAngle);
  float2 dRot = float2(delta.x * cr - delta.y * sr, delta.x * sr + delta.y * cr);
  float u = dRot.x * cosA + dRot.y * sinA;
  float v = -dRot.x * sinA + dRot.y * cosA;
  float halfLen = in.fragCorners.z;
  float halfW = in.fragStrokeWidth * 0.5f;
  float2 p = float2(u, v);
  float2 pa = p - float2(-halfLen, 0.0f);
  float2 ba = float2(2.0f * halfLen, 0.0f);
  float denom = dot(ba, ba);
  float h = denom > 1e-8f ? clamp(dot(pa, ba) / denom, 0.0f, 1.0f) : 0.0f;
  float d = length(pa - ba * h) - halfW;
  float alpha = (1.0f - smoothstep(-0.75f, 0.75f, d)) * in.fragStrokeColor.a * in.fragOpacity * clipCoverage;
  if (alpha < 0.001f)
    discard_fragment();
  return float4(in.fragStrokeColor.rgb * alpha, alpha);
}

// -----------------------------------------------------------------------------
// Path mesh (triangulated fill / expanded stroke — same convention as upstream lambda path shaders)
// -----------------------------------------------------------------------------

struct PathVertexIn {
  float2 pos [[attribute(0)]];
  float4 color [[attribute(1)]];
  float2 viewport [[attribute(2)]];
};

struct PathVertexOut {
  float4 clip [[position]];
  float2 fragPixelPos;
  float4 color;
};

vertex PathVertexOut path_vert(PathVertexIn in [[stage_in]], constant DrawUniforms* uniforms [[buffer(1)]]) {
  PathVertexOut out;
  float2 pixelPos = in.pos + uniforms[0].translation;
  float2 ndc = (pixelPos / uniforms[0].viewport) * 2.0f - 1.0f;
  ndc.y = -ndc.y;
  out.clip = float4(ndc, 0.0f, 1.0f);
  out.fragPixelPos = pixelPos;
  out.color = in.color;
  return out;
}

fragment float4 path_frag(PathVertexOut in [[stage_in]], constant RoundedClipStack* clips [[buffer(0)]]) {
  float clipCoverage = roundedClipCoverage(in.fragPixelPos, clips[0]);
  if (clipCoverage <= 0.001f)
    discard_fragment();
  float4 c = in.color;
  return float4(c.rgb * c.a * clipCoverage, c.a * clipCoverage);
}

// -----------------------------------------------------------------------------
// Glyph atlas (R8 coverage × premultiplied run color)
// -----------------------------------------------------------------------------

struct GlyphVertexIn {
  float2 pos [[attribute(0)]];
  float2 uv [[attribute(1)]];
  float4 color [[attribute(2)]];
};

struct GlyphVertexOut {
  float4 clip [[position]];
  float2 fragPixelPos;
  float2 uv;
  float4 color;
};

vertex GlyphVertexOut glyph_vert(GlyphVertexIn in [[stage_in]], constant DrawUniforms* uniforms [[buffer(1)]]) {
  float2 pixelPos = in.pos + uniforms[0].translation;
  float2 ndc = (pixelPos / uniforms[0].viewport) * 2.0f - 1.0f;
  ndc.y = -ndc.y;
  GlyphVertexOut out;
  out.clip = float4(ndc, 0.0f, 1.0f);
  out.fragPixelPos = pixelPos;
  out.uv = in.uv;
  out.color = in.color;
  return out;
}

fragment float4 glyph_frag(GlyphVertexOut in [[stage_in]], constant RoundedClipStack* clips [[buffer(0)]],
                           texture2d<float> atlas [[texture(0)]], sampler atlasSmpl [[sampler(0)]]) {
  // in.color is premultiplied (rgb already multiplied by alpha * opacity).
  // Scale by R8 coverage and return; the pipeline uses (One, OneMinusSrcAlpha) premul blending.
  float clipCoverage = roundedClipCoverage(in.fragPixelPos, clips[0]);
  if (clipCoverage <= 0.001f)
    discard_fragment();
  float cov = atlas.sample(atlasSmpl, in.uv).r;
  if (cov < 0.001f)
    discard_fragment();
  return float4(in.color.rgb * cov * clipCoverage, in.color.a * cov * clipCoverage);
}
