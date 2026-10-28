#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;

void main() {
  vec2 t = vec2(u.data[0], u.data[1]) * max(u.data[2], 0.5);
  vec3 sum = texture(MainTex, fragUV).rgb * 4.0;
  sum += texture(MainTex, fragUV + vec2(-t.x, 0.0)).rgb * 2.0;
  sum += texture(MainTex, fragUV + vec2(t.x, 0.0)).rgb * 2.0;
  sum += texture(MainTex, fragUV + vec2(0.0, -t.y)).rgb * 2.0;
  sum += texture(MainTex, fragUV + vec2(0.0, t.y)).rgb * 2.0;
  sum += texture(MainTex, fragUV + vec2(-t.x, -t.y)).rgb;
  sum += texture(MainTex, fragUV + vec2(t.x, -t.y)).rgb;
  sum += texture(MainTex, fragUV + vec2(-t.x, t.y)).rgb;
  sum += texture(MainTex, fragUV + vec2(t.x, t.y)).rgb;
  outColor = vec4(sum * (1.0 / 16.0) * fragColor.rgb, 1.0);
}
