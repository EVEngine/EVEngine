#version 450
// Rim-light mesh fragment shader (Unity "Rim Lighting" style).
//
// Reuses graphics/mesh3d_toon.vert for the 7 vertex inputs. Lighting is a
// simple two-band lambert (directional lightColor + a flat ambient fill); a
// fresnel term adds a rim glow that is strongest on grazing silhouettes.
// Faces are two-sided (flip N toward the view) so planar surfaces still rim.
//
// Push constants (declareFloat order in bindEffectMeshUniforms("rim")):
//   0 rimColorR  1 rimColorG  2 rimColorB
//   3 rimPower   4 rimIntensity   5 rimMix
//   6 ambientStrength   7 baseLight
// Binding 1 = albedo (multiplied by vTint from the mesh Frame UBO).

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

void main() {
    vec3 rimColor = vec3(u.data[0], u.data[1], u.data[2]);
    float rimPower = max(u.data[3], 0.1);
    float rimIntensity = max(u.data[4], 0.0);
    float rimMix = clamp(u.data[5], 0.0, 1.0);
    float ambientStrength = max(u.data[6], 0.0);
    float baseLight = max(u.data[7], 0.0);

    vec3 N = normalize(vNormal);
    vec3 V = normalize(vCameraPos - vWorldPos);
    if (dot(N, V) < 0.0)
        N = -N;

    vec3 albedo = texture(albedoSampler, vUV).rgb * vTint.rgb;
    vec3 L = normalize(vLightDir);
    float diff = max(dot(N, L), 0.0);

    // Two-band base: key + flat ambient, plus a manual baseLight boost.
    vec3 base = albedo * (vLightColor * (diff * baseLight) + vec3(ambientStrength));

    // Fresnel rim: strongest where the normal faces away from the view.
    float rim = pow(clamp(1.0 - max(dot(N, V), 0.0), 0.0, 1.0), rimPower);
    vec3 rimTerm = rimColor * rim * rimIntensity;

    outColor = vec4(mix(base, base + rimTerm, rimMix), vTint.a);
}