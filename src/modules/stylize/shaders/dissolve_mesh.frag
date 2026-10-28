#version 450
// Dissolve / burn-away mesh fragment shader (Unity "Dissolve" style).
//
// Reuses graphics/mesh3d_toon.vert for the 7 vertex inputs. A value-noise
// field (hash + 3-octave fbm in world space) controls coverage: fragments
// below `amount` are discarded, and a thin emissive band glows along the
// dissolve front. Two-sided normals keep the band consistent on open shells.
//
// The dissolve is driven by the `amount` param (0 = opaque, 1 = gone) rather
// than time, so a game can script it from 0->1 for a clean keyframed effect.
//
// Push constants (declareFloat order in bindEffectMeshUniforms("dissolve")):
//   0 amount  1 edgeWidth  2 edgeColorR  3 edgeColorG  4 edgeColorB
//   5 edgeGlow  6 noiseScale  7 hardness
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
    float amount = clamp(u.data[0], 0.0, 1.0);
    float edgeWidth = max(u.data[1], 1e-3);
    vec3 edgeColor = vec3(u.data[2], u.data[3], u.data[4]);
    float edgeGlow = max(u.data[5], 0.0);
    float noiseScale = max(u.data[6], 1e-3);
    float hardness = clamp(u.data[7], 0.0, 1.0);

    vec3 N = normalize(vNormal);
    vec3 V = normalize(vCameraPos - vWorldPos);
    if (dot(N, V) < 0.0)
        N = -N;

    // World-space noise field; mix in the UV so thin shells read differently.
    float n = fbm(vWorldPos.xy * noiseScale + vUV * 0.5);

    // Hardness pushes the noise toward a binary cutoff (crisper front).
    n = n * (1.0 - hardness) + n * n * hardness;
    if (n < amount)
        discard;

    // 1.0 at the dissolve front, falling to 0 across `edgeWidth`.
    float band = clamp((n - amount) / edgeWidth, 0.0, 1.0);
    float glow = 1.0 - band;

    vec3 albedo = texture(albedoSampler, vUV).rgb * vTint.rgb;
    vec3 L = normalize(vLightDir);
    float diff = max(dot(N, L), 0.0);
    vec3 base = albedo * (vLightColor * diff + vec3(0.12));

    vec3 color = base + edgeColor * glow * glow * edgeGlow;
    outColor = vec4(color, vTint.a);
}