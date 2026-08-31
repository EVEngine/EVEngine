#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(binding = 1) uniform sampler2D HistoryTex;
layout(push_constant) uniform Externals { float data[32]; } u;

void main() {
  float target = max(texture(MainTex, vec2(0.5)).r, 1e-5);
  float previous = max(texture(HistoryTex, vec2(0.5)).r, 1e-5);
  float speed = target > previous ? max(u.data[1], 0.0) : max(u.data[2], 0.0);
  float blend = u.data[3] > 0.5 ? 1.0 : 1.0 - exp(-speed * max(u.data[0], 0.0));
  float exposure = mix(previous, target, clamp(blend, 0.0, 1.0));
  outColor = vec4(exposure, log2(exposure), 0.0, 1.0);
}
