#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;

vec2 depthRange(vec2 uv) {
  vec4 value = texture(MainTex, clamp(uv, vec2(0.0), vec2(1.0)));
  return u.data[2] > 0.5 ? vec2(value.r) : value.rg;
}

void main() {
  vec2 texel = vec2(u.data[0], u.data[1]);
  vec2 a = depthRange(fragUV + texel * vec2(-0.5, -0.5));
  vec2 b = depthRange(fragUV + texel * vec2(0.5, -0.5));
  vec2 c = depthRange(fragUV + texel * vec2(-0.5, 0.5));
  vec2 d = depthRange(fragUV + texel * vec2(0.5, 0.5));
  outColor = vec4(min(min(a.x, b.x), min(c.x, d.x)),
                  max(max(a.y, b.y), max(c.y, d.y)), 0.0, 1.0);
}
