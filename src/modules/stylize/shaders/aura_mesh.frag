#version 450
// 3D aura/status mesh fragment shader (buff/debuff环形光晕).
//
// Reuses graphics/mesh3d_toon.vert for standard inputs. Builds a rim-driven
// animated aura with pulse modulation and noise breakup, intended for skill buff/
// debuff visual tagging on character meshes.
//
// Push constants (declareFloat order in bindEffectMeshUniforms("aura")):
//   0 auraR  1 auraG  2 auraB
//   3 pulse  4 edge   5 radius
//   6 noiseScale 7 time  8 intensity
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

void main() {
    vec3 aura = vec3(u.data[0], u.data[1], u.data[2]);
    float pulse = max(u.data[3], 0.0);
    float edge = max(u.data[4], 0.1);
    float radius = clamp(u.data[5], 0.1, 2.0);
    float noiseScale = max(u.data[6], 0.1);
    float time = max(u.data[7], 0.0);
    float intensity = max(u.data[8], 0.0);

    vec3 N = normalize(vNormal);
    vec3 V = normalize(vCameraPos - vWorldPos);
    if (dot(N, V) < 0.0)
        N = -N;

    vec3 albedo = texture(albedoSampler, vUV).rgb * vTint.rgb;
    vec3 L = normalize(vLightDir);
    float diff = max(dot(N, L), 0.0);

    // Rim basis + animated modulation.
    float rim = pow(1.0 - max(dot(N, V), 0.0), edge);
    float pulsePhase = 0.5 + 0.5 * sin(time * 2.0 + vWorldPos.x * 0.7 + vWorldPos.z * 0.5);
    float n = noise(vWorldPos.xz * noiseScale + time * 0.15);

    // Radius-lifted shell: push glow outward by rim + noise.
    float spread = smoothstep(radius * 0.2, radius, rim + n * 0.15 + pulse * 0.08 * pulsePhase);
    float shell = pow(rim, 0.8) * spread;
    shell = clamp(shell * (0.6 + 0.4 * pulsePhase), 0.0, 1.0);

    vec3 base = albedo * (vLightColor * diff + vec3(0.12));
    vec3 color = mix(base, base + aura * shell * intensity, 0.55 + 0.4 * shell);

    float alpha = clamp(vTint.a * (0.25 + shell * 0.75), 0.0, 1.0);
    outColor = vec4(color, alpha);
}
