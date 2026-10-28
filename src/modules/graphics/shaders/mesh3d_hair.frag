#version 450
// Anisotropic hair / fur card fragment (Kajiya-Kay + Marschner-ish lobes + cheap self-shadow).
// Push: data[0]=specExp, [1]=specStrength, [2]=primaryShift, [3]=secondaryShift,
//       [4]=alphaCutoff, [5]=rimStrength, [6..8]=strandDir (unused in frag),
//       [9]=marschnerR, [10]=marschnerTT, [11]=marschnerTRT,
//       [12]=selfShadowStrength, [13]=selfShadowBias, [14]=rootAoStrength.
//
// Self-shadow is an analytical fiber term + along-strand root AO — not a deep
// shadow map / transmittance volume (see design doc vs UE groom shadows).

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
    float lobeR = max(u.data[9], 0.0);
    float lobeTT = max(u.data[10], 0.0);
    float lobeTRT = max(u.data[11], 0.0);
    float selfStr = clamp(u.data[12], 0.0, 1.0);
    float selfBias = clamp(u.data[13], 0.0, 1.0);
    float rootAoStr = clamp(u.data[14], 0.0, 1.0);

    // Shifted tangents approximate Marschner R / TT longitudinal lobes.
    vec3 T1 = normalize(T + shift1 * N);
    vec3 T2 = normalize(T + shift2 * N);
    // TRT: softer third lobe biased toward the back-light direction.
    vec3 T3 = normalize(T - 0.5 * shift1 * N);

    float r = kajiyaKay(T1, L, V, specExp) * lobeR;
    float tt = kajiyaKay(T2, L, V, specExp * 0.65) * 0.45 * lobeTT;
    float trt = kajiyaKay(T3, -L, V, specExp * 0.35) * 0.35 * lobeTRT;

    float ndotl = max(dot(N, L), 0.0);
    vec3 diffuse = base.rgb * (0.22 + 0.78 * ndotl);

    vec3 specCol = vec3(r + tt + trt) * specStr * min(vLightColor, vec3(1.5));
    vec3 rim = pow(clamp(1.0 - max(dot(N, V), 0.0), 0.0, 1.0), 3.0) * rimStr * base.rgb;

    // Analytical fiber self-shadow (depth-bias style wrap); UV.y is strand u.
    float fiberShadow = mix(1.0, smoothstep(-selfBias, 1.0 - selfBias, dot(N, L)), selfStr);
    float rootAo = mix(1.0, pow(clamp(vUV.y, 0.0, 1.0), 1.25), rootAoStr);
    float shadow = clamp(fiberShadow * rootAo, 0.0, 1.0);

    vec3 lit = (diffuse * min(vLightColor, vec3(1.2)) + specCol + rim) * shadow;
    outColor = vec4(clamp(lit, 0.0, 1.0), base.a);
}
