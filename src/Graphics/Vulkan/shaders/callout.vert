#version 450

layout(push_constant) uniform Push {
  vec2 viewport;
  vec2 translation;
} pc;

layout(location = 0) out vec2 vLocal;
layout(location = 1) out flat uint vInstance;
layout(location = 2) out vec2 vWorld;

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

vec2 unitVertex(uint i) {
  vec2 p[6] = vec2[](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
  );
  return p[i];
}

void main() {
  CalloutInstance c = callouts.instances[gl_InstanceIndex];
  vec2 unit = unitVertex(gl_VertexIndex);
  vec2 size = max(c.rect.zw, vec2(0.000001));
  float pad = max(c.params.x * 0.5 + 1.0, 1.0);
  vec2 local = unit * (size + vec2(pad * 2.0)) - vec2(pad);
  vec2 axisUnit = local / size;
  vec2 world = c.axisX.xy + axisUnit.x * c.axisX.zw + axisUnit.y * c.axisY.xy;
  vec2 pos = world + pc.translation;
  vec2 ndc = vec2(pos.x / pc.viewport.x * 2.0 - 1.0,
                  pos.y / pc.viewport.y * 2.0 - 1.0);
  gl_Position = vec4(ndc, 0.0, 1.0);
  vLocal = local;
  vInstance = gl_InstanceIndex;
  vWorld = world;
}
