#version 450

layout(location = 2) in vec4 vTint;
layout(location = 3) in vec3 vWorldPos;
layout(location = 5) in vec3 vViewPos;

layout(set = 0, binding = 1) uniform sampler2D albedoSampler;

layout(push_constant) uniform Externals { float data[32]; } u;

layout(location = 0) out vec4 outColor;

void main() {
    // Push-constant slots: 7=fogR 8=fogG 9=fogB 10=fogDensity 11=flash.
    float flash = clamp(u.data[11], 0.0, 1.0);
    if (flash <= 0.01) discard;
    vec3 core = vec3(0.75, 0.85, 1.0) * flash;
    float viewDist = length(vViewPos);
    float fogAmt = clamp(1.0 - exp(-viewDist * u.data[10]), 0.0, 1.0);
    vec3 fogCol = vec3(u.data[7], u.data[8], u.data[9]);
    vec3 rgb = mix(core, fogCol, fogAmt * 0.6);
    outColor = vec4(rgb, 1.0);
}
