#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;

float luminance(vec3 color) { return max(dot(max(color, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722)), 1e-5); }

void main() {
  float values[16];
  int index = 0;
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      vec2 uv = (vec2(x, y) + 0.5) * 0.25;
      values[index++] = log2(luminance(texture(MainTex, uv).rgb));
    }
  }
  for (int i = 1; i < 16; ++i) {
    float value = values[i];
    int j = i - 1;
    while (j >= 0 && values[j] > value) {
      values[j + 1] = values[j];
      --j;
    }
    values[j + 1] = value;
  }
  float logAverage = 0.0;
  for (int i = 2; i < 14; ++i) logAverage += values[i];
  logAverage *= 1.0 / 12.0;
  float targetEV = clamp(log2(0.18) - logAverage, u.data[0], u.data[1]);
  outColor = vec4(exp2(targetEV), targetEV, 0.0, 1.0);
}
