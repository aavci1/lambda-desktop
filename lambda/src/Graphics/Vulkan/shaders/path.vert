#version 450

layout(push_constant) uniform Push {
  vec2 viewport;
  vec2 translation;
} pc;

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inViewport;
layout(location = 3) in vec2 inLocal;
layout(location = 4) in vec4 inFill0;
layout(location = 5) in vec4 inFill1;
layout(location = 6) in vec4 inFill2;
layout(location = 7) in vec4 inFill3;
layout(location = 8) in vec4 inStops;
layout(location = 9) in vec4 inGradient;
layout(location = 10) in vec4 inParams;
layout(location = 11) in vec4 inClipHeader;
layout(location = 12) in vec4 inClipEntries[8];

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vLocal;
layout(location = 2) out vec4 vFill0;
layout(location = 3) out vec4 vFill1;
layout(location = 4) out vec4 vFill2;
layout(location = 5) out vec4 vFill3;
layout(location = 6) out vec4 vStops;
layout(location = 7) out vec4 vGradient;
layout(location = 8) out vec4 vParams;
layout(location = 9) out vec2 vWorld;
layout(location = 10) flat out vec4 vClipHeader;
layout(location = 11) flat out vec4 vClipEntries[8];

void main() {
  vec2 pos = inPos + pc.translation;
  vec2 clip = vec2((pos.x / inViewport.x) * 2.0 - 1.0, (pos.y / inViewport.y) * 2.0 - 1.0);
  gl_Position = vec4(clip, 0.0, 1.0);
  vColor = inColor;
  vLocal = inLocal;
  vFill0 = inFill0;
  vFill1 = inFill1;
  vFill2 = inFill2;
  vFill3 = inFill3;
  vStops = inStops;
  vGradient = inGradient;
  vParams = inParams;
  vWorld = inPos;
  vClipHeader = inClipHeader;
  for (int i = 0; i < 8; ++i) {
    vClipEntries[i] = inClipEntries[i];
  }
}
