#version 450
// Sprite-stack slice fragment shader: unlit textured alpha cutout.
// Push constants: data[0] = alphaCutoff.

layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vTint;

layout(set = 0, binding = 1) uniform sampler2D albedo;

layout(push_constant) uniform Externals {
    vec4 uvRect;
    float data[28];
} u;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 base = texture(albedo, vUV) * vTint;
    if (base.a < u.data[0]) discard;
    outColor = base;
}
