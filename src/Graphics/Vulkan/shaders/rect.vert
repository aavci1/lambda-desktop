#version 450

layout(push_constant) uniform Push {
  vec2 viewport;
  vec2 translation;
} pc;

layout(location = 0) out vec2 vLocal;
layout(location = 1) out flat uint vInstance;

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
};

layout(std430, set = 0, binding = 0) readonly buffer Rects {
  RectInstance instances[];
} rects;

vec2 unitVertex(uint i) {
  vec2 p[6] = vec2[](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(1.0, 1.0),
    vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
  );
  return p[i];
}

void main() {
  RectInstance r = rects.instances[gl_InstanceIndex];
  vec2 unit = unitVertex(gl_VertexIndex);
  vec2 size = max(r.rect.zw, vec2(0.000001));
  float pad = max(r.params.z * 0.5, 1.0);
  vec2 local = unit * (size + vec2(pad * 2.0)) - vec2(pad);
  vec2 axisUnit = local / size;
  vec2 pos = r.axisX.xy + axisUnit.x * r.axisX.zw + axisUnit.y * r.axisY.xy + pc.translation;
  vec2 ndc = vec2(pos.x / pc.viewport.x * 2.0 - 1.0,
                  pos.y / pc.viewport.y * 2.0 - 1.0);
  gl_Position = vec4(ndc, 0.0, 1.0);
  vLocal = local;
  vInstance = gl_InstanceIndex;
}
