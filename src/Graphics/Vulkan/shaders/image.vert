#version 450

layout(push_constant) uniform Push {
  vec2 viewport;
  vec2 translation;
} pc;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;
layout(location = 2) out vec2 vLocal;
layout(location = 3) out vec2 vSize;
layout(location = 4) out vec4 vRadii;
layout(location = 5) out vec2 vWorld;
layout(location = 6) flat out vec4 vClipHeader;
layout(location = 7) flat out vec4 vClipEntries[8];

struct QuadInstance {
  vec4 rect;
  vec4 axisX;
  vec4 axisY;
  vec4 uv;
  vec4 color;
  vec4 radii;
  vec4 clipHeader;
  vec4 clipEntries[8];
};

layout(std430, set = 0, binding = 0) readonly buffer Quads {
  QuadInstance instances[];
} quads;

vec2 unitVertex(uint i) {
  vec2 p[6] = vec2[](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
  );
  return p[i];
}

void main() {
  QuadInstance q = quads.instances[gl_InstanceIndex];
  vec2 unit = unitVertex(gl_VertexIndex);
  vec2 size = max(q.rect.zw, vec2(0.000001));
  float maxRadius = max(max(q.radii.x, q.radii.y), max(q.radii.z, q.radii.w));
  float pad = step(0.0001, maxRadius);
  vec2 local = unit * (size + vec2(pad * 2.0)) - vec2(pad);
  vec2 axisUnit = local / size;
  vec2 world = q.axisX.xy + axisUnit.x * q.axisX.zw + axisUnit.y * q.axisY.xy;
  vec2 pos = world + pc.translation;
  vec2 ndc = vec2(pos.x / pc.viewport.x * 2.0 - 1.0,
                  pos.y / pc.viewport.y * 2.0 - 1.0);
  gl_Position = vec4(ndc, 0.0, 1.0);
  vUv = mix(q.uv.xy, q.uv.zw, axisUnit);
  vColor = q.color;
  vLocal = local;
  vSize = size;
  vRadii = q.radii;
  vWorld = world;
  vClipHeader = q.clipHeader;
  for (int i = 0; i < 8; ++i) {
    vClipEntries[i] = q.clipEntries[i];
  }
}
