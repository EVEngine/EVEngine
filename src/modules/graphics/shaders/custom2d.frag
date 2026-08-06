#version 450
// Custom 2D fragment shader sample: tint RGB by push-constant factor.
// Uniform layout (declare before send):
//   declareFloat("factor") → u.data[0]
layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals {
  float data[32];
} u;
void main() {
  vec4 c = texture(MainTex, fragUV) * fragColor;
  float f = u.data[0];
  outColor = vec4(c.rgb * f, c.a);
}
