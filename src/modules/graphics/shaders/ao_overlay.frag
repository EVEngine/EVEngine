#version 450
// Darken scene with AO: output black with alpha = (1 - ao) * intensity.
// Designed for engine SrcAlpha / OneMinusSrcAlpha blend over an existing scene.
// MainTex.rgb = AO factor (1 = unoccluded).
//
// Push:
//  0 intensity  1 power

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;

void main() {
  float intensity = max(u.data[0], 0.0);
  float power = max(u.data[1], 0.01);
  float ao = texture(MainTex, fragUV).r;
  ao = clamp(pow(max(ao, 0.0), power), 0.0, 1.0);
  float dark = clamp((1.0 - ao) * intensity, 0.0, 1.0);
  outColor = vec4(0.0, 0.0, 0.0, dark) * fragColor;
}
