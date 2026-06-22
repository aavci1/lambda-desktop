#version 450

layout(set = 1, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;
layout(location = 2) in vec2 vLocal;
layout(location = 3) in vec2 vSize;
layout(location = 4) in vec4 vRadii;
layout(location = 0) out vec4 outColor;

void main() {
  float radius = max(vRadii.x, 0.0);
  vec2 axis = vRadii.yz;
  if (radius <= 0.01) {
    outColor = texture(tex, vUv);
    return;
  }

  const int sampleRadius = 16;
  vec2 texel = axis / vec2(textureSize(tex, 0));
  float sigma = max(radius * 0.45, 0.5);
  vec4 color = vec4(0.0);
  float totalWeight = 0.0;

  for (int i = -sampleRadius; i <= sampleRadius; ++i) {
    float x = (float(i) / float(sampleRadius)) * radius;
    float weight = exp(-0.5 * (x * x) / (sigma * sigma));
    color += texture(tex, vUv + texel * x) * weight;
    totalWeight += weight;
  }

  outColor = color / max(totalWeight, 0.0001);
}
