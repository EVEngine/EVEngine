#version 450

#extension GL_GOOGLE_include_directive : enable
#include "tex_cell_bomb.glsl"
#include "parallax_map.glsl"

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vTint;
layout(location = 3) in vec3 vWorldPos;
layout(location = 4) in vec3 vCameraPos;

struct Light3D {
    vec4 posRadius; // xyz = point OR dir; w = radius (0 => directional)
    vec4 color;
};

layout(set = 0, binding = 0, std140) uniform Frame {
    mat4 mvp;
    mat4 model;
    vec4 lightDirIntensity; // xyz = primary dir (toward surface); w = lightCount
    vec4 lightColor;        // rgb = primary radiance
    vec4 tint;
    vec4 cameraPos;         // xyz = eye; w = roughness
    vec4 ambient;           // rgb = ambient; w = metallic
    Light3D lights[8];
    vec4 texBomb;           // x = cellScale, y = strength (0=off), z = rotAmount, w unused
    vec4 parallax;          // x = scale (0=off), y = minLayers, z = maxLayers, w unused
} ubo;

layout(set = 0, binding = 1) uniform sampler2D albedoSampler;
layout(set = 0, binding = 2) uniform sampler2D normalSampler;
layout(set = 0, binding = 3) uniform samplerCube envSampler;
layout(set = 0, binding = 6) uniform sampler2D heightSampler;

layout(set = 0, binding = 4, std140) uniform ShadowFrame {
    mat4 lightVP[3];
    vec4 splits; // xyz = cascade ends (view-space +Z depth), w = strength
    vec4 bias;   // x = depth bias, y = enabled, z = receive, w unused
} shadow;

layout(set = 0, binding = 5) uniform sampler2DArray shadowMap;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = max(roughness * roughness, 0.002);
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float denom = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    return a2 / max(PI * denom * denom, 1e-4);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / max(NdotV * (1.0 - k) + k, 1e-4);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness) *
           GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 applyNormalMap(vec3 N, vec3 mapSample, vec3 worldPos, vec2 uv) {
    vec3 mapN = mapSample * 2.0 - 1.0;
    if (abs(mapN.x) < 0.1 && abs(mapN.y) < 0.1 && mapN.z > 0.85)
        return normalize(N);
    vec3 dp1 = dFdx(worldPos);
    vec3 dp2 = dFdy(worldPos);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    if (abs(duv1.x * duv2.y - duv2.x * duv1.y) < 1e-8)
        return normalize(N);
    vec3 T = normalize(cross(dp2, N) * duv1.x + cross(N, dp1) * duv2.x);
    vec3 B = normalize(cross(dp2, N) * duv1.y + cross(N, dp1) * duv2.y);
    if (length(T) < 1e-4 || length(B) < 1e-4)
        return normalize(N);
    return normalize(mat3(T, B, normalize(N)) * mapN);
}

vec3 shadeLight(vec3 N, vec3 V, vec3 albedo, float metallic, float roughness, vec3 L, vec3 radiance) {
    float NdotL = max(dot(N, L), 0.0);
    // Lambert wrap (legacy mesh3d) for dielectrics; metals rely on specular.
    float diffuse = mix(NdotL, NdotL * 0.5 + 0.5, 0.25);

    vec3 H = normalize(V + L);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    vec3 specular = (NDF * G * F) / max(4.0 * max(dot(N, V), 0.0) * max(NdotL, 0.001), 1e-3);

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    return (kD * albedo * diffuse + specular * NdotL) * radiance;
}

float sampleShadowPCF(vec3 worldPos, float viewDepth) {
    if (shadow.bias.y < 0.5 || shadow.bias.z < 0.5 || shadow.splits.w < 1e-4)
        return 1.0;

    int cascade = 2;
    if (viewDepth < shadow.splits.x) cascade = 0;
    else if (viewDepth < shadow.splits.y) cascade = 1;

    vec4 lightClip = shadow.lightVP[cascade] * vec4(worldPos, 1.0);
    vec3 ndc = lightClip.xyz / max(lightClip.w, 1e-6);
    vec2 uv = ndc.xy * 0.5 + 0.5;
    float depth = ndc.z;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || depth < 0.0 || depth > 1.0)
        return 1.0;

    float bias = shadow.bias.x;
    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0).xy);
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            float closest = texture(shadowMap, vec3(uv + vec2(float(x), float(y)) * texel, float(cascade))).r;
            sum += (depth - bias > closest) ? 0.0 : 1.0;
        }
    }
    float vis = sum / 9.0;
    return mix(1.0, vis, clamp(shadow.splits.w, 0.0, 1.0));
}

void main() {
    float bombScale = ubo.texBomb.x;
    float bombStrength = ubo.texBomb.y;
    float bombRot = ubo.texBomb.z;
    vec3 Ngeom = normalize(vNormal);
    vec3 V = normalize(vCameraPos - vWorldPos);
    vec2 uv = parallaxMappedUV(heightSampler, vUV, Ngeom, vWorldPos, V, ubo.parallax.x,
                               ubo.parallax.y, ubo.parallax.z);

    vec4 base = textureCellBomb(albedoSampler, uv, bombScale, bombStrength, bombRot) * vTint;
    vec3 albedo = base.rgb;
    float metallic = clamp(ubo.ambient.w, 0.0, 1.0);
    float roughness = clamp(ubo.cameraPos.w, 0.04, 1.0);
    int count = int(ubo.lightDirIntensity.w + 0.5);

    vec3 N = Ngeom;
    vec3 nSample = textureCellBomb(normalSampler, uv, bombScale, bombStrength, bombRot).xyz;
    if (length(nSample - vec3(0.5, 0.5, 1.0)) > 0.04)
        N = applyNormalMap(N, nSample, vWorldPos, uv);
    vec3 Lo = vec3(0.0);
    float viewDepth = length(vCameraPos - vWorldPos);
    float shadowVis = sampleShadowPCF(vWorldPos, viewDepth);

    // Primary directional light (legacy slot) — receives CSM.
    Lo += shadeLight(N, V, albedo, metallic, roughness,
                     normalize(ubo.lightDirIntensity.xyz), ubo.lightColor.rgb) * shadowVis;

    for (int i = 0; i < 8; ++i) {
        if (i >= count) break;
        Light3D Lgt = ubo.lights[i];
        vec3 radiance = Lgt.color.rgb;
        vec3 L;
        if (Lgt.posRadius.w <= 0.0) {
            L = normalize(Lgt.posRadius.xyz);
            if (length(L - normalize(ubo.lightDirIntensity.xyz)) < 1e-3)
                continue;
            // Additional dir lights are unshadowed.
        } else {
            vec3 toL = Lgt.posRadius.xyz - vWorldPos;
            float dist = length(toL);
            L = toL / max(dist, 1e-4);
            float atten = clamp(1.0 - dist / max(Lgt.posRadius.w, 1e-3), 0.0, 1.0);
            atten *= atten;
            radiance *= atten;
        }
        Lo += shadeLight(N, V, albedo, metallic, roughness, L, radiance);
    }

    // Ambient is taken at face value — callers control fill brightness.
    vec3 amb = ubo.ambient.rgb;
    vec3 color = albedo * amb * (1.0 - metallic) + Lo;

    // Specular IBL (envIntensity packed in lightColor.w). No BRDF LUT — Fresnel * env.
    float envIntensity = ubo.lightColor.w;
    if (envIntensity > 1e-4) {
        vec3 R = reflect(-V, N);
        float lod = roughness * 5.0;
        vec3 envSpec = textureLod(envSampler, R, lod).rgb * envIntensity;
        vec3 F0 = mix(vec3(0.04), albedo, metallic);
        vec3 F = fresnelSchlick(max(dot(N, V), 0.0), F0);
        color += envSpec * F;
        // Cheap diffuse IBL for dielectrics (sample along N at a blurry lod).
        vec3 irr = textureLod(envSampler, N, 5.0).rgb * envIntensity;
        color += albedo * irr * (1.0 - metallic) * (1.0 - F) * 0.35;
    }

    outColor = vec4(color, base.a);
}
