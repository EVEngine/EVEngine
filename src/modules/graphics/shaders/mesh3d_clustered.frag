#version 450

#extension GL_GOOGLE_include_directive : enable
#include "tex_cell_bomb.glsl"
#include "parallax_map.glsl"
#include "tonemap.glsl"

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vTint;
layout(location = 3) in vec3 vWorldPos;
layout(location = 4) in vec3 vCameraPos;
layout(location = 5) in vec3 vViewPos;

layout(set = 0, binding = 0, std140) uniform Frame {
    mat4 mvp;
    mat4 model;
    mat4 view;
    vec4 lightDir;    // xyz = primary dir; w = 1 if enabled
    vec4 lightColor;
    vec4 tint;
    vec4 cameraPos;   // xyz = eye; w = roughness
    vec4 ambient;     // rgb; w = metallic
    vec4 gridInfo;    // tilesX, tilesY, slices, pointCount
    vec4 clipInfo;    // near, far, screenW, screenH
    vec4 texBomb;     // xyz = cell bombing, w = SurfaceMode (0/1/2)
    vec4 parallax;    // xyz = parallax, w = alphaCutoff
} ubo;

struct Light3D {
    vec4 posRadius;
    vec4 color;
};

layout(set = 0, binding = 1) uniform sampler2D albedoSampler;
layout(set = 0, binding = 2) uniform sampler2D normalSampler;
layout(set = 0, binding = 3) uniform samplerCube envSampler;
layout(set = 0, binding = 9) uniform sampler2D heightSampler;

layout(std430, set = 0, binding = 4) readonly buffer LightBuffer {
    Light3D lights[];
};

layout(std430, set = 0, binding = 5) readonly buffer ClusterTable {
    uvec2 clusters[];  // x=offset y=count
};

layout(std430, set = 0, binding = 6) readonly buffer LightIndexBuffer {
    uint lightIndices[];
};

layout(set = 0, binding = 7, std140) uniform ShadowFrame {
    mat4 lightVP[3];
    vec4 splits;
    vec4 bias;
    vec4 cascadeBias;
    vec4 cascadeTexel;
} shadow;

layout(set = 0, binding = 8) uniform sampler2DArrayShadow shadowMap;

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
    if (length(mapN.xy) < 0.04 && mapN.z > 0.85)
        return normalize(N);
    vec3 dp1 = dFdx(worldPos);
    vec3 dp2 = dFdy(worldPos);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    float det = duv1.x * duv2.y - duv2.x * duv1.y;
    if (abs(det) < 1e-6)
        return normalize(N);
    float invDet = 1.0 / det;
    vec3 T = (dp1 * duv2.y - dp2 * duv1.y) * invDet;
    vec3 B = (dp2 * duv1.x - dp1 * duv2.x) * invDet;
    N = normalize(N);
    T = T - N * dot(N, T);
    float tLen = length(T);
    float bLen = length(B);
    if (tLen < 1e-4 || bLen < 1e-4)
        return N;
    T /= tLen;
    B = normalize(B - N * dot(N, B) - T * dot(T, B));
    if (abs(dot(T, B)) > 0.35)
        return N;
    return normalize(mat3(T, B, N) * mapN);
}

vec3 shadeLight(vec3 N, vec3 V, vec3 albedo, float metallic, float roughness, vec3 L, vec3 radiance) {
    float NdotL = max(dot(N, L), 0.0);
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

/** Hardware-PCF shadow lookup for a single cascade. Returns lit fraction in [0,1]. */
float sampleShadowCascade(vec3 worldPos, int cascade, float bias) {
    vec4 lightClip = shadow.lightVP[cascade] * vec4(worldPos, 1.0);
    vec3 ndc = lightClip.xyz / max(lightClip.w, 1e-6);
    vec2 uv = ndc.xy * 0.5 + 0.5;
    float depth = ndc.z;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || depth < 0.0 || depth > 1.0)
        return 1.0;

    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0).xy);
    // Tight rotated 3x3. Wider taps (1–1.2 texels) plus hardware 2x2 PCF ate
    // indoor creases: empty / unoccluded taps read as lit and drew a bright
    // rim on the left and top of Cornell umbrae.
    const vec2 kRotOffsets[9] = vec2[9](
        vec2(-0.5, -0.5), vec2(0.0, -0.6), vec2(0.5, -0.5),
        vec2(-0.6, 0.0), vec2(0.0, 0.0),  vec2(0.6, 0.0),
        vec2(-0.5, 0.5), vec2(0.0, 0.6),  vec2(0.5, 0.5)
    );
    float sum = 0.0;
    float zref = depth - bias;
    for (int i = 0; i < 9; ++i) {
        vec2 s = uv + kRotOffsets[i] * texel;
        // Clamp-to-edge on a cleared border texel is depth=1 → fully lit.
        // Count out-of-cascade taps as shadowed so frustum-edge walls
        // (Cornell left/ceiling) do not leak.
        if (s.x < 0.0 || s.x > 1.0 || s.y < 0.0 || s.y > 1.0)
            continue;
        sum += texture(shadowMap, vec4(s, float(cascade), zref));
    }
    return sum / 9.0;
}

float cascadeNdcBias(int cascade) {
    float b = cascade == 0 ? shadow.cascadeBias.x
                           : (cascade == 1 ? shadow.cascadeBias.y : shadow.cascadeBias.z);
    return b > 1e-8 ? b : shadow.bias.x;
}

float slopeScaledBias(int cascade, float nDotL) {
    // Caster acne is handled by rasterizer slope bias. Inflating compare bias
    // when the *receiver* grazes the light opens a bright rim along Cornell
    // walls/ceiling (nDotL ~ 0.05 → ~50× the gap along the surface).
    return cascadeNdcBias(cascade) * mix(0.75, 1.0, clamp(nDotL, 0.0, 1.0));
}

float sampleShadowPCF(vec3 worldPos, vec3 N, float viewDepth, float nDotL) {
    if (shadow.bias.y < 0.5 || shadow.bias.z < 0.5 || shadow.splits.w < 1e-4)
        return 1.0;
    int cascade = 2;
    if (viewDepth < shadow.splits.x) cascade = 0;
    else if (viewDepth < shadow.splits.y) cascade = 1;

    float tw = cascade == 0 ? shadow.cascadeTexel.x
                            : (cascade == 1 ? shadow.cascadeTexel.y : shadow.cascadeTexel.z);
    vec3 p = worldPos + N * ((2.0 * max(tw, 1e-6)) / max(nDotL, 0.2));
    float vis = sampleShadowCascade(p, cascade, slopeScaledBias(cascade, nDotL));

    // Cross-fade only inside a band at each split. Mix toward the *adjacent*
    // cascade when approaching that split (weight 1 at the split, 0 after
    // `band` meters). The previous factors were inverted, so indoor views
    // that sit entirely inside cascade 0 sampled cascade 2 — huge texels and
    // swimming stairs as the camera moved.
    float hi = cascade == 0 ? shadow.splits.x : (cascade == 1 ? shadow.splits.y : shadow.splits.z);
    float lo = cascade == 0 ? 0.0 : (cascade == 1 ? shadow.splits.x : shadow.splits.y);
    float band = max(0.5, (hi - lo) * 0.1); // ~10% of the cascade's own span
    float toPrev = 1.0 - clamp((viewDepth - lo) / band, 0.0, 1.0);
    float toNext = 1.0 - clamp((hi - viewDepth) / band, 0.0, 1.0);
    if (toPrev > 0.0 && cascade > 0) {
        float visPrev = sampleShadowCascade(p, cascade - 1, slopeScaledBias(cascade - 1, nDotL));
        vis = mix(vis, visPrev, toPrev);
    }
    if (toNext > 0.0 && cascade < 2) {
        float visNext = sampleShadowCascade(p, cascade + 1, slopeScaledBias(cascade + 1, nDotL));
        vis = mix(vis, visNext, toNext);
    }

    vis = mix(1.0, vis, clamp(shadow.splits.w, 0.0, 1.0));
    // Keep a small unshadowed fill so umbra doesn't crush to black.
    return mix(0.04, 1.0, vis);
}

uint clusterIndex() {
    int tilesX = int(ubo.gridInfo.x + 0.5);
    int tilesY = int(ubo.gridInfo.y + 0.5);
    int slices = int(ubo.gridInfo.z + 0.5);
    float nearZ = ubo.clipInfo.x;
    float farZ = max(ubo.clipInfo.y, nearZ + 1e-3);
    float screenW = max(ubo.clipInfo.z, 1.0);
    float screenH = max(ubo.clipInfo.w, 1.0);

    int tx = clamp(int(floor(gl_FragCoord.x / screenW * float(tilesX))), 0, tilesX - 1);
    int ty = clamp(int(floor(gl_FragCoord.y / screenH * float(tilesY))), 0, tilesY - 1);

    float depth = max(-vViewPos.z, nearZ);
    int sz = clamp(int(floor((depth - nearZ) / (farZ - nearZ) * float(slices))), 0, slices - 1);
    return uint((sz * tilesY + ty) * tilesX + tx);
}

void main() {
    float bombScale = ubo.texBomb.x;
    float bombStrength = ubo.texBomb.y;
    float bombRot = ubo.texBomb.z;
    vec3 Ngeom = normalize(vNormal);
    vec3 V = normalize(vCameraPos - vWorldPos);
    if (dot(Ngeom, V) < 0.0)
        Ngeom = -Ngeom;
    vec2 uv = parallaxMappedUV(heightSampler, vUV, Ngeom, vWorldPos, V, ubo.parallax.x,
                               ubo.parallax.y, ubo.parallax.z);

    vec4 base = textureCellBomb(albedoSampler, uv, bombScale, bombStrength, bombRot) * vTint;
    if (ubo.texBomb.w > 0.5 && ubo.texBomb.w < 1.5 && base.a < ubo.parallax.w)
        discard;
    float alphaHash = fract(dot(floor(gl_FragCoord.xy), vec2(0.06711056, 0.00583715)));
    if (ubo.texBomb.w > 2.5 && base.a < alphaHash)
        discard;
    vec3 albedo = base.rgb;
    float metallic = clamp(ubo.ambient.w, 0.0, 1.0);
    float roughness = clamp(ubo.cameraPos.w, 0.04, 1.0);

    vec3 N = Ngeom;
    vec3 nSample = textureCellBomb(normalSampler, uv, bombScale, bombStrength, bombRot).xyz;
    if (length(nSample - vec3(0.5, 0.5, 1.0)) > 0.04)
        N = applyNormalMap(N, nSample, vWorldPos, uv);
    vec3 Lo = vec3(0.0);
    vec3 primaryL = normalize(ubo.lightDir.xyz);
    float shadowVis = sampleShadowPCF(vWorldPos, N, max(-vViewPos.z, 0.0), max(dot(N, primaryL), 0.0));

    if (ubo.lightDir.w > 0.5 && length(ubo.lightColor.rgb) > 1e-6) {
        Lo += shadeLight(N, V, albedo, metallic, roughness, primaryL, ubo.lightColor.rgb) * shadowVis;
    }

    uint cid = clusterIndex();
    uvec2 entry = clusters[cid];
    uint offset = entry.x;
    uint count = min(entry.y, 32u);
    for (uint i = 0u; i < count; ++i) {
        uint li = lightIndices[offset + i];
        Light3D Lgt = lights[li];
        vec3 radiance = Lgt.color.rgb;
        vec3 toL = Lgt.posRadius.xyz - vWorldPos;
        float dist = length(toL);
        vec3 L = toL / max(dist, 1e-4);
        float atten = clamp(1.0 - dist / max(Lgt.posRadius.w, 1e-3), 0.0, 1.0);
        atten *= atten;
        radiance *= atten;
        Lo += shadeLight(N, V, albedo, metallic, roughness, L, radiance);
    }

    // Hemispheric GI: sky vs ground bounce, plus a cheap wrap fill into umbra.
    float hemi = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 skyIrr = ubo.ambient.rgb * 1.1 + ubo.lightColor.rgb * 0.12;
    vec3 gndIrr = ubo.ambient.rgb * vec3(0.72, 0.62, 0.52);
    vec3 irr = mix(gndIrr, skyIrr, hemi);
    vec3 gi = albedo * irr * (1.0 - metallic);
    float wrap = max(dot(N, primaryL) * 0.5 + 0.5, 0.0);
    gi += albedo * ubo.lightColor.rgb * (wrap * wrap) * 0.06 * (1.0 - metallic);
    vec3 color = gi + Lo;

    float envIntensity = ubo.lightColor.w;
    if (envIntensity > 1e-4) {
        vec3 R = reflect(-V, N);
        float lod = roughness * 5.0;
        vec3 envSpec = textureLod(envSampler, R, lod).rgb * envIntensity;
        vec3 F0 = mix(vec3(0.04), albedo, metallic);
        vec3 F = fresnelSchlick(max(dot(N, V), 0.0), F0);
        color += envSpec * F;
        vec3 irr = textureLod(envSampler, N, 5.0).rgb * envIntensity;
        color += albedo * irr * (1.0 - metallic) * (1.0 - F) * 0.45;
    }

    color = tonemapPeak(color);

    float nearZ = max(ubo.clipInfo.x, 1e-4);
    float farZ = max(ubo.clipInfo.y, nearZ + 1e-3);
    float viewZ = max(-vViewPos.z, 0.0);
    float linear01 = clamp((viewZ - nearZ) / (farZ - nearZ), 0.0, 1.0);
    outColor = vec4(color, linear01);
}
