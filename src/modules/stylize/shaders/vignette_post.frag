#version 450
// Vignette post-effect (Unity / common engine style).
// Darkens the image edges toward a configurable color.
// Push: 0 strength, 1 smoothness, 2 roundness, 3 colorR, 4 colorG, 5 colorB

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 0) out vec4 outColor;
layout(binding = 0) uniform sampler2D MainTex;
layout(push_constant) uniform Externals { float data[32]; } u;

void main() {
    float strength = clamp(u.data[0], 0.0, 1.0);
    float smoothness = max(u.data[1], 0.0001);
    float roundness = clamp(u.data[2], 0.0, 1.0);
    vec3 vgColor = vec3(u.data[3], u.data[4], u.data[5]);

    vec4 src = texture(MainTex, fragUV) * fragColor;

    // Center-weighted distance; `roundness` mixes a squarer falloff in.
    vec2 dist = fragUV - 0.5;
    float radial = length(dist) * 2.0;
    float rect = length(dist * vec2(1.0 / mix(1.0, 0.5, roundness), 1.0)) * 2.0;
    float d = mix(radial, rect, roundness);

    float vignette = 1.0 - smoothstep(1.0 - smoothness, 1.0, d) * strength;
    outColor = vec4(mix(vgColor, src.rgb, vignette), src.a);
}