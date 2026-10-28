#version 450
// 3D ember burn mesh fragment shader (hit-impact/DoT skill effect).
//
// Reuses graphics/mesh3d_toon.vert for standard inputs. Uses world-space
// noise to create a moving ember pattern and highlights a flickering flame band.
//
// Push constants (declareFloat order in bindEffectMeshUniforms("ember")):
//   0 coreR  1 coreG  2 coreB
//   3 flareR 4 flareG 5 flareB
//   6 burnAmount 7 flicker 8 noiseScale
//   9 hardness 10 time
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
    vec3 core = vec3(u.data[0], u.data[1], u.data[2]);
    vec3 flare = vec3(u.data[3], u.data[4], u.data[5]);
    float burnAmount = clamp(u.data[6], 0.0, 1.0);
    float flicker = clamp(u.data[7], 0.0, 1.0);
    float noiseScale = max(u.data[8], 0.1);
    float hardness = clamp(u.data[9], 0.0, 1.0);
    float time = max(u.data[10], 0.0);

    vec3 N = normalize(vNormal);
    vec3 V = normalize(vCameraPos - vWorldPos);
    if (dot(N, V) < 0.0)
        N = -N;

    vec3 albedo = texture(albedoSampler, vUV).rgb * vTint.rgb;
    vec3 L = normalize(vLightDir);
    float diff = max(dot(N, L), 0.0);

    float n = fbm(vWorldPos.xz * noiseScale + vUV * 8.0 + time * 0.45);
    float crack = n * n * (0.7 + 0.3 * flicker * sin(time * 4.0 + vUV.x * 20.0 + vUV.y * 17.0));

    float threshold = mix(0.35, 0.85, burnAmount);
    float edge = smoothstep(threshold, 1.0, crack);
    edge = mix(edge * edge, pow(edge, 2.0 + 4.0 * hardness), 0.5);

    float rim = pow(clamp(1.0 - max(dot(N, V), 0.0), 0.0, 1.0), 2.0);
    vec3 base = albedo * (vLightColor * diff + vec3(0.2));
    vec3 flame = mix(core, flare, edge);
    vec3 color = mix(base, base + flame * (0.5 + 1.2 * edge), rim + edge * 0.8);

    float alpha = clamp(vTint.a * (0.2 + edge * (1.0 + 0.6 * rim) + 0.25 * burnAmount), 0.0, 1.0);
    outColor = vec4(color, alpha);
}
