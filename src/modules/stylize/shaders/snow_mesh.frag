#version 450
// Snow accumulation mesh fragment shader (Godot snow shader style).
//
// Reuses graphics/mesh3d_toon.vert. Upward-facing surfaces (N.y ~ 1) get
// covered in snow; flat/slanted surfaces stay on the albedo. Coverage is
// modulated by a world-space value noise so drifts look organic instead of a
// hard normal cut. A world-height fade lets the same material coat a mountain
// from snowline up.
//
// Push constants (declareFloat order in bindEffectMeshUniforms("snow")):
//   0 snowAmount  1 snowHardness
//   2 snowColorR  3 snowColorG  4 snowColorB
//   5 noiseScale  6 snowHeight  7 heightFade
// Binding 1 = albedo (multiplied by vTint).

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vTint;
layout(location = 3) in vec3 vLightDir;
layout(location = 4) in vec3 vLightColor;
layout(location = 5) in vec3 vWorldPos;
layout(location = 6) in vec3 vCameraPos;

layout(set = 0, binding = 1) uniform sampler2D albedoSampler;

layout(push_constant) uniform Externals { float data[32]; } u;

layout(location = 0) out vec4 outColor;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i), hash21(i + vec2(1.0, 0.0)), f.x),
               mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), f.x),
               f.y);
}

float fbm(vec2 p) {
    float sum = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    for (int i = 0; i < 3; ++i) {
        sum += amp * noise(p * freq);
        amp *= 0.5;
        freq *= 2.0;
    }
    return sum;
}

void main() {
    float snowAmount = clamp(u.data[0], 0.0, 1.0);
    float snowHardness = clamp(u.data[1], 0.0, 1.0);
    vec3 snowColor = vec3(u.data[2], u.data[3], u.data[4]);
    float noiseScale = max(u.data[5], 1e-3);
    float snowHeight = u.data[6];
    float heightFade = max(u.data[7], 1e-3);

    vec3 N = normalize(vNormal);
    vec3 V = normalize(vCameraPos - vWorldPos);
    if (dot(N, V) < 0.0)
        N = -N;

    // Upward bias: steep normal -> no snow, up -> snow. `snowHardness` widens
    // the band so walls stay bare while flattish tops fill in.
    float up = smoothstep(1.0 - snowHardness, 1.0, max(N.y, 0.0));

    // Organic drift noise, rescaled so `snowAmount` controls overall coverage.
    float n = fbm(vWorldPos.xz * noiseScale + vWorldPos.y * 0.15);
    float coverage = up * smoothstep(1.0 - snowAmount, 1.0, n);

    // Height fade: below snowline nothing, ramp up across `heightFade`.
    float heightFactor = smoothstep(snowHeight - heightFade, snowHeight + heightFade,
                                    vWorldPos.y);
    coverage *= heightFactor;

    vec3 albedo = texture(albedoSampler, vUV).rgb * vTint.rgb;
    vec3 col = mix(albedo, snowColor, coverage);

    // Simple lambert shading on top so the snow reads as lit geometry.
    vec3 L = normalize(vLightDir);
    float diff = max(dot(N, L), 0.0);
    outColor = vec4(col * (vLightColor * diff + vec3(0.18)), vTint.a);
}