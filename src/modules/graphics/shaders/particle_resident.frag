#version 450

layout(set = 0, binding = 0) uniform sampler2D particleTexture;

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUv;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(particleTexture, fragUv) * fragColor;
    if (outColor.a <= 0.001) discard;
}
