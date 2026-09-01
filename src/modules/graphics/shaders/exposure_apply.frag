#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(binding = 1) uniform sampler2D ExposureTex;
layout(push_constant) uniform Externals { float data[32]; } u;

void main() {
  vec4 hdr = texture(MainTex, fragUV);
  float automatic = u.data[1] > 0.5 ? texture(ExposureTex, vec2(0.5)).r : 1.0;
  outColor = vec4(hdr.rgb * max(u.data[0], 0.0) * automatic, hdr.a);
}
