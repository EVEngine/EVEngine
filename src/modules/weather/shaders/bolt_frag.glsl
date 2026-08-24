#version 450

layout(location = 2) in vec4 vTint;
layout(location = 1) in vec2 vUV;
layout(location = 3) in vec3 vWorldPos;
layout(location = 5) in vec3 vViewPos;

layout(set = 0, binding = 1) uniform sampler2D albedoSampler;

layout(push_constant) uniform Externals { float data[32]; } u;

layout(location = 0) out vec4 outColor;

void main() {
    // Push-constant slots: 7=fogR 8=fogG 9=fogB 10=fogDensity 11=flash.
    float flash = clamp(u.data[11], 0.0, 1.0);
    if (flash <= 0.01) discard;
    float across = abs(vUV.x);
    float coreMask = 1.0 - smoothstep(0.08, 0.34, across);
    float halo = pow(max(0.0, 1.0 - across), 2.2) * flash;
    ivec2 pixel = ivec2(gl_FragCoord.xy) & 3;
    const float bayer[16] = float[16](
        0.0, 0.5, 0.125, 0.625,
        0.75, 0.25, 0.875, 0.375,
        0.1875, 0.6875, 0.0625, 0.5625,
        0.9375, 0.4375, 0.8125, 0.3125);
    if (max(coreMask, halo * 0.72) <= bayer[pixel.y * 4 + pixel.x]) discard;
    vec3 core = mix(vec3(0.20, 0.38, 0.95), vec3(0.92, 0.97, 1.0), coreMask) *
                (0.55 + 0.85 * flash);
    float viewDist = length(vViewPos);
    float fogAmt = clamp(1.0 - exp(-viewDist * u.data[10]), 0.0, 1.0);
    vec3 fogCol = vec3(u.data[7], u.data[8], u.data[9]);
    vec3 rgb = mix(core, fogCol, fogAmt * 0.6);
    outColor = vec4(rgb, 1.0);
}
