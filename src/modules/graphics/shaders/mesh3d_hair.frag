#version 450
// Anisotropic hair / fur card fragment (Kajiya-Kay + alpha cutout).
// Push: data[0]=specExp, [1]=specStrength, [2]=primaryShift, [3]=secondaryShift,
//       [4]=alphaCutoff, [5]=rimStrength, [6..8]=strandDir (unused in frag).

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vTint;
layout(location = 3) in vec3 vLightDir;
layout(location = 4) in vec3 vLightColor;
layout(location = 5) in vec3 vWorldPos;
layout(location = 6) in vec3 vCameraPos;
layout(location = 7) in vec3 vTangent;

layout(set = 0, binding = 1) uniform sampler2D albedo;

layout(push_constant) uniform Externals {
    float data[32];
} u;

layout(location = 0) out vec4 outColor;

float kajiyaKay(vec3 T, vec3 L, vec3 V, float exp) {
    float tDotL = dot(T, L);
    float tDotV = dot(T, V);
    float sinTL = sqrt(max(1.0 - tDotL * tDotL, 0.0));
    float sinTV = sqrt(max(1.0 - tDotV * tDotV, 0.0));
    return pow(max(sinTL * sinTV + tDotL * tDotV, 0.0), exp);
}

void main() {
    vec4 base = texture(albedo, vUV) * vTint;
    if (base.a < max(u.data[4], 0.01)) discard;

    vec3 N = normalize(vNormal);
    vec3 T = normalize(vTangent);
    vec3 L = normalize(vLightDir);
    vec3 V = normalize(vCameraPos - vWorldPos);

    float specExp = max(u.data[0], 4.0);
    float specStr = max(u.data[1], 0.0);
    float shift1 = u.data[2];
    float shift2 = u.data[3];
    float rimStr = max(u.data[5], 0.0);

    // Shift tangents for primary / secondary highlights (Marschner-style approximation).
    vec3 T1 = normalize(T + shift1 * N);
    vec3 T2 = normalize(T + shift2 * N);

    float spec1 = kajiyaKay(T1, L, V, specExp);
    float spec2 = kajiyaKay(T2, L, V, specExp * 0.65) * 0.45;

    float ndotl = max(dot(N, L), 0.0);
    vec3 diffuse = base.rgb * (0.22 + 0.78 * ndotl);

    vec3 specCol = vec3(spec1 + spec2) * specStr * min(vLightColor, vec3(1.5));
    vec3 rim = pow(clamp(1.0 - max(dot(N, V), 0.0), 0.0, 1.0), 3.0) * rimStr * base.rgb;

    vec3 lit = diffuse * min(vLightColor, vec3(1.2)) + specCol + rim;
    outColor = vec4(clamp(lit, 0.0, 1.0), base.a);
}
