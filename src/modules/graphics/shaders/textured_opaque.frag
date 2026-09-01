#version 450
layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D texSampler;
void main() {
  vec4 sampled = texture(texSampler, fragUV) * fragColor;
  outColor = vec4(sampled.rgb, 1.0);
}
