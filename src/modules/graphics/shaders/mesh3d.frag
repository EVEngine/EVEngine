#version 450

#extension GL_GOOGLE_include_directive : enable
#include "tex_cell_bomb.glsl"
#include "parallax_map.glsl"
#include "virtual_texture.glsl"

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec4 vTint;
layout(location = 3) in vec3 vWorldPos;
layout(location = 4) in vec3 vCameraPos;
layout(location = 5) in vec3 vViewPos;

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
    vec4 texBomb;           // xyz = cell bombing, w = SurfaceMode (0/1/2)
    vec4 parallax;          // xyz = parallax, w = alphaCutoff
    mat4 view;
    vec4 clipInfo;          // x = near, y = far
    vec4 cloud;             // x = strength (0=off), y = world cell size, z = time, w unused
    vec4 cloudWind;         // xy = wind velocity (world/s), z = coverage, w = detail
    vec4 virtualTexture;    // enabled, page counts xy, border / stored extent
    vec4 virtualAtlas;      // physical slot counts xy
    vec4 bindlessEnv;       // reserved by the shared Mesh3D UBO layout
    vec4 envProbeCenter;
    vec4 envProbeExtent;
    vec4 skinInfo;
    mat4 skinBones[128];
    vec4 reflectionProbeCenter[2];
    vec4 reflectionProbeExtent[2];
} ubo;

layout(set = 0, binding = 1) uniform sampler2D albedoSampler;
layout(set = 0, binding = 2) uniform sampler2D normalSampler;
layout(set = 0, binding = 3) uniform samplerCube envSampler;
layout(set = 0, binding = 6) uniform sampler2D heightSampler;
layout(set = 0, binding = 16) uniform samplerCube reflectionProbe0;
layout(set = 0, binding = 17) uniform samplerCube reflectionProbe1;

layout(set = 0, binding = 4, std140) uniform ShadowFrame {
    mat4 lightVP[3];
    vec4 splits; // xyz = cascade ends (view-space +Z depth), w = strength
    vec4 bias;   // x = c0 fallback, y = enabled, z = receive, w unused
    vec4 cascadeBias; // xyz = per-cascade NDC compare bias
    vec4 cascadeTexel; // xyz = world units per shadow texel
} shadow;

layout(set = 0, binding = 5) uniform sampler2DArrayShadow shadowMap;
layout(set = 0, binding = 20) uniform sampler2DArray shadowMapRaw;
layout(set = 0, binding = 8) uniform sampler2D decalAlbedoSampler;
layout(set = 0, binding = 9) uniform sampler2D decalNormalSampler;
layout(set = 0, binding = 10) uniform sampler2D decalParamsSampler;

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

vec3 boxProjectedEnv(vec3 direction, vec3 worldPos) {
    vec3 extent = ubo.envProbeExtent.xyz;
    vec3 local = worldPos - ubo.envProbeCenter.xyz;
    if (min(extent.x, min(extent.y, extent.z)) <= 1e-4 ||
        any(greaterThan(abs(local), extent)))
        return direction;
    vec3 safeDir = mix(vec3(1e-5), direction, greaterThan(abs(direction), vec3(1e-5)));
    vec3 t0 = (ubo.envProbeCenter.xyz - extent - worldPos) / safeDir;
    vec3 t1 = (ubo.envProbeCenter.xyz + extent - worldPos) / safeDir;
    vec3 exitT = max(t0, t1);
    float distanceToBox = min(exitT.x, min(exitT.y, exitT.z));
    if (distanceToBox <= 0.0) return direction;
    vec3 projected = worldPos + direction * distanceToBox - ubo.envProbeCenter.xyz;
    float minExtent = min(extent.x, min(extent.y, extent.z));
    vec3 edgeDistance = extent - abs(local);
    float influence = smoothstep(0.0, max(minExtent * 0.1, 0.01),
                                 min(edgeDistance.x, min(edgeDistance.y, edgeDistance.z)));
    return normalize(mix(normalize(direction), normalize(projected), influence));
}

float reflectionProbeWeight(int index, vec3 worldPos) {
    vec3 extent = ubo.reflectionProbeExtent[index].xyz;
    vec3 edge = extent - abs(worldPos - ubo.reflectionProbeCenter[index].xyz);
    float inside = min(edge.x, min(edge.y, edge.z));
    if (inside <= 0.0 || ubo.reflectionProbeCenter[index].w <= 0.0) return 0.0;
    float blend = max(ubo.reflectionProbeExtent[index].w, 1e-4);
    return clamp(inside / blend, 0.0, 1.0);
}

vec3 reflectionProbeDirection(int index, vec3 direction, vec3 worldPos) {
    vec3 center = ubo.reflectionProbeCenter[index].xyz;
    vec3 extent = ubo.reflectionProbeExtent[index].xyz;
    vec3 safeDir = mix(vec3(1e-5), direction, greaterThan(abs(direction), vec3(1e-5)));
    vec3 exitT = max((center - extent - worldPos) / safeDir,
                     (center + extent - worldPos) / safeDir);
    float distanceToBox = min(exitT.x, min(exitT.y, exitT.z));
    return distanceToBox > 0.0
        ? normalize(worldPos + direction * distanceToBox - center)
        : normalize(direction);
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

/** Hardware-PCF shadow lookup for a single cascade. Returns lit fraction in [0,1]. */
float cloudHash(vec2 p) {
    ivec2 ip = ivec2(floor(mod(p, 64.0)));
    uint h = uint(ip.x) * 374761393u ^ uint(ip.y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return float(h & 0x00FFFFFFu) / float(0x00FFFFFFu);
}
float cloudNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = cloudHash(i);
    float b = cloudHash(i + vec2(1.0, 0.0));
    float c = cloudHash(i + vec2(0.0, 1.0));
    float d = cloudHash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
float cloudFbm(vec2 p) {
    float sum = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    for (int i = 0; i < 4; ++i) {
        sum += amp * cloudNoise(p * freq);
        amp *= 0.5;
        freq *= 2.0;
    }
    return sum * 2.0;
}
float cloudShadowFactor(vec3 worldPos) {
    if (ubo.cloud.x < 1e-4)
        return 1.0;
    float cell = 1.0 / max(ubo.cloud.y, 1e-4);
    vec2 drift = (ubo.cloudWind.xy * cell) * ubo.cloud.z;
    vec2 p = worldPos.xz * cell - drift;
    float f = cloudFbm(p);
    float c = smoothstep(ubo.cloudWind.z - 0.1, ubo.cloudWind.z + 0.1, f);
    float d = cloudNoise(p * 3.0 + vec2(11.7, 5.3));
    float covered = mix(c, c * d, ubo.cloudWind.w);
    return 1.0 - clamp(covered, 0.0, 1.0) * clamp(ubo.cloud.x, 0.0, 1.0);
}

float blockerPenumbra(vec3 worldPos, int cascade, float bias) {
    vec4 lightClip = shadow.lightVP[cascade] * vec4(worldPos, 1.0);
    vec3 ndc = lightClip.xyz / max(lightClip.w, 1e-6);
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
        return 0.5;
    vec2 texel = 1.0 / vec2(textureSize(shadowMapRaw, 0).xy);
    const vec2 searchOffsets[8] = vec2[8](
        vec2(-1.0, -1.0), vec2(0.0, -1.5), vec2(1.0, -1.0), vec2(-1.5, 0.0),
        vec2(1.5, 0.0), vec2(-1.0, 1.0), vec2(0.0, 1.5), vec2(1.0, 1.0));
    float blockerSum = 0.0;
    float blockerCount = 0.0;
    float receiver = ndc.z - bias;
    for (int i = 0; i < 8; ++i) {
        float blocker = texture(shadowMapRaw,
                                vec3(clamp(uv + searchOffsets[i] * texel * 1.5,
                                           vec2(0.0), vec2(1.0)), float(cascade))).r;
        if (blocker < receiver) {
            blockerSum += blocker;
            blockerCount += 1.0;
        }
    }
    if (blockerCount < 0.5) return 0.5;
    float averageBlocker = blockerSum / blockerCount;
    float separation = max(receiver - averageBlocker, 0.0);
    float mapSize = float(textureSize(shadowMapRaw, 0).x);
    return clamp(0.5 + separation * mapSize * max(shadow.bias.w, 0.0), 0.5, 4.0);
}


float sampleShadowCascade(vec3 worldPos, int cascade, float bias, float filterRadius) {
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
    const vec2 kPoisson[9] = vec2[9](
        vec2(0.0), vec2(-0.326, -0.406), vec2(-0.840, -0.074),
        vec2(-0.696, 0.457), vec2(-0.203, 0.621), vec2(0.473, -0.480),
        vec2(0.519, 0.767), vec2(0.185, -0.893), vec2(0.896, 0.262)
    );
    float worldTexel = cascade == 0 ? shadow.cascadeTexel.x
                                     : (cascade == 1 ? shadow.cascadeTexel.y : shadow.cascadeTexel.z);
    vec2 cell = floor(worldPos.xz / max(worldTexel * 4.0, 1e-4));
    float angle = fract(sin(dot(cell, vec2(12.9898, 78.233))) * 43758.5453) * 6.2831853;
    mat2 rotation = mat2(cos(angle), -sin(angle), sin(angle), cos(angle));
    float sum = 0.0;
    float zref = depth - bias;
    for (int i = 0; i < 9; ++i) {
        vec2 s = uv + rotation * kPoisson[i] * texel * filterRadius;
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

    // Slide the receiver a couple of texels along the geometric normal so
    // concave creases (Cornell box/wall, ceiling/wall) still hit the occluder
    // instead of a 1–6px fully-lit rim on the left/top of the umbra.
    float tw = cascade == 0 ? shadow.cascadeTexel.x
                            : (cascade == 1 ? shadow.cascadeTexel.y : shadow.cascadeTexel.z);
    // Slope-scaled normal offset: grazing walls (Cornell) need a longer
    // push into the room or the umbra detaches as a fully-lit rim.
    vec3 p = worldPos + N * ((2.0 * max(tw, 1e-6)) / max(nDotL, 0.2));
    // Cross-fade only inside a band at each split. Mix toward the *adjacent*
    // cascade when approaching that split (weight 1 at the split, 0 after
    // `band` meters). The previous factors were inverted, so indoor views
    // that sit entirely inside cascade 0 sampled cascade 2 — huge texels and
    // swimming stairs as the camera moved.
    float hi = cascade == 0 ? shadow.splits.x : (cascade == 1 ? shadow.splits.y : shadow.splits.z);
    float lo = cascade == 0 ? 0.0 : (cascade == 1 ? shadow.splits.x : shadow.splits.y);
    float cascadeT = clamp((viewDepth - lo) / max(hi - lo, 1e-3), 0.0, 1.0);
    float filterRadius = max(mix(0.5, 2.0, cascadeT),
                             blockerPenumbra(p, cascade, slopeScaledBias(cascade, nDotL)));
    float vis = sampleShadowCascade(p, cascade, slopeScaledBias(cascade, nDotL), filterRadius);
    float band = max(0.5, (hi - lo) * 0.1); // ~10% of the cascade's own span
    float toPrev = 1.0 - clamp((viewDepth - lo) / band, 0.0, 1.0);
    float toNext = 1.0 - clamp((hi - viewDepth) / band, 0.0, 1.0);
    if (toPrev > 0.0 && cascade > 0) {
        float visPrev = sampleShadowCascade(p, cascade - 1, slopeScaledBias(cascade - 1, nDotL), filterRadius);
        vis = mix(vis, visPrev, toPrev);
    }
    if (toNext > 0.0 && cascade < 2) {
        float visNext = sampleShadowCascade(p, cascade + 1, slopeScaledBias(cascade + 1, nDotL), filterRadius);
        vis = mix(vis, visNext, toNext);
    }

    vis = mix(1.0, vis, clamp(shadow.splits.w, 0.0, 1.0));
    // Keep a small unshadowed fill so umbra doesn't crush to black.
    return mix(0.04, 1.0, vis);
}

void main() {
    float bombScale = ubo.texBomb.x;
    float bombStrength = ubo.texBomb.y;
    float bombRot = ubo.texBomb.z;
    vec3 Ngeom = normalize(vNormal);
    vec3 V = normalize(vCameraPos - vWorldPos);
    // Two-sided: orient toward the camera. gl_FrontFacing follows pipeline
    // frontFace/winding and flips floors dark when Assimp winding disagrees.
    if (dot(Ngeom, V) < 0.0)
        Ngeom = -Ngeom;
    vec2 uv = ubo.virtualTexture.x > 0.5
                  ? vUV
                  : parallaxMappedUV(heightSampler, vUV, Ngeom, vWorldPos, V,
                                     ubo.parallax.x, ubo.parallax.y, ubo.parallax.z);

    vec4 base = (ubo.virtualTexture.x > 0.5
                     ? sampleVirtualTexture(albedoSampler, heightSampler, uv,
                                            ubo.virtualTexture, ubo.virtualAtlas)
                     : textureCellBomb(albedoSampler, uv, bombScale, bombStrength, bombRot)) *
                vTint;
    if (ubo.texBomb.w > 0.5 && ubo.texBomb.w < 1.5 && base.a < ubo.parallax.w)
        discard;
    // Alpha hash is stable in screen space and avoids object-order artifacts.
    // "coverage" uses the same fallback when MSAA alpha-to-coverage is unavailable.
    float alphaHash = fract(dot(floor(gl_FragCoord.xy), vec2(0.06711056, 0.00583715)));
    if (ubo.texBomb.w > 2.5 && base.a < alphaHash)
        discard;
    vec3 albedo = base.rgb;
    float metallic = clamp(ubo.ambient.w, 0.0, 1.0);
    float roughness = clamp(ubo.cameraPos.w, 0.04, 1.0);
    int count = int(ubo.lightDirIntensity.w + 0.5);

    vec3 N = Ngeom;
    vec3 nSample =
        (ubo.virtualTexture.x > 0.5
             ? sampleVirtualTexture(normalSampler, heightSampler, uv, ubo.virtualTexture,
                                    ubo.virtualAtlas)
             : textureCellBomb(normalSampler, uv, bombScale, bombStrength, bombRot))
            .xyz;
    if (length(nSample - vec3(0.5, 0.5, 1.0)) > 0.04)
        N = applyNormalMap(N, nSample, vWorldPos, uv);

    vec3 emissive = vec3(0.0);
    // Screen-space decal layer (bindings 8/9/10). When the decal feature is
    // off these samplers are 1x1 placeholders, so every coverage is 0 and
    // nothing changes. Albedo coverage is the master alpha; normal carries its
    // own strength-scaled alpha; params damp the stored values by strength.
    vec2 decalUV = gl_FragCoord.xy / vec2(textureSize(decalAlbedoSampler, 0));
    vec4 decalA = texture(decalAlbedoSampler, decalUV);
    float decalCov = clamp(decalA.a, 0.0, 1.0);
    if (decalCov > 0.001) {
        albedo = mix(albedo, decalA.rgb, decalCov);

        vec4 decalN = texture(decalNormalSampler, decalUV);
        float nWeight = clamp(decalN.a, 0.0, 1.0);
        if (nWeight > 0.001) {
            vec3 decalNormal = normalize(decalN.rgb * 2.0 - 1.0);
            N = normalize(mix(N, decalNormal, nWeight));
        }

        vec4 decalP = texture(decalParamsSampler, decalUV);
        float pWeight = clamp(decalP.a, 0.0, 1.0);
        if (pWeight > 0.001) {
            roughness = mix(roughness, decalP.r, pWeight);
            metallic = mix(metallic, decalP.g, pWeight);
            // Emissive: decal color scaled by the params intensity channel.
            emissive += decalA.rgb * decalP.b;
        }
    }

    vec3 Lo = vec3(0.0);
    // Splits are camera-forward distances (view-space +Z), not euclidean length.
    float viewDepth = max(-vViewPos.z, 0.0);
    vec3 primaryL = normalize(ubo.lightDirIntensity.xyz);
    float shadowVis = sampleShadowPCF(vWorldPos, N, viewDepth, max(dot(N, primaryL), 0.0));

    // Primary directional (legacy slot). Packing zeros lightColor when no dir exists
    // so a point in lights[0] is never treated as a sun direction.
    if (length(ubo.lightColor.rgb) > 1e-6) {
        Lo += shadeLight(N, V, albedo, metallic, roughness, primaryL, ubo.lightColor.rgb) * shadowVis * cloudShadowFactor(vWorldPos);
    }

    for (int i = 0; i < 8; ++i) {
        if (i >= count) break;
        Light3D Lgt = ubo.lights[i];
        vec3 radiance = Lgt.color.rgb;
        vec3 L;
        if (Lgt.posRadius.w <= 0.0) {
            L = normalize(Lgt.posRadius.xyz);
            if (length(L - primaryL) < 1e-3)
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

    // Hemispheric GI: sky vs ground bounce, plus a cheap wrap fill into umbra.
    float hemi = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 skyIrr = ubo.ambient.rgb * 1.1 + ubo.lightColor.rgb * 0.12;
    vec3 gndIrr = ubo.ambient.rgb * vec3(0.72, 0.62, 0.52);
    vec3 irr = mix(gndIrr, skyIrr, hemi);
    vec3 gi = albedo * irr * (1.0 - metallic);
    float wrap = max(dot(N, primaryL) * 0.5 + 0.5, 0.0);
    gi += albedo * ubo.lightColor.rgb * (wrap * wrap) * 0.06 * (1.0 - metallic);
    vec3 color = gi + Lo;

    // Split-sum specular IBL using the UE-style analytic DFG approximation.
    float envIntensity = ubo.lightColor.w;
    float probeWeight0 = reflectionProbeWeight(0, vWorldPos);
    float probeWeight1 = reflectionProbeWeight(1, vWorldPos);
    float probeWeightSum = probeWeight0 + probeWeight1;
    if (probeWeightSum > 1.0) {
        probeWeight0 /= probeWeightSum;
        probeWeight1 /= probeWeightSum;
        probeWeightSum = 1.0;
    }
    float globalWeight = 1.0 - probeWeightSum;
    if (envIntensity * globalWeight + probeWeight0 + probeWeight1 > 1e-4) {
        vec3 R = boxProjectedEnv(reflect(-V, N), vWorldPos);
        float maxLod = float(max(textureQueryLevels(envSampler) - 1, 0));
        float specMaxLod = maxLod >= 2.0 ? maxLod - 1.0 : maxLod;
        float lod = roughness * specMaxLod;
        vec3 envSpec = textureLod(envSampler, R, lod).rgb * envIntensity * globalWeight;
        vec3 envDiffuse = textureLod(envSampler, N, maxLod).rgb * envIntensity * globalWeight;
        if (probeWeight0 > 0.0) {
            float probeLod = float(max(textureQueryLevels(reflectionProbe0) - 1, 0));
            vec3 probeR = reflectionProbeDirection(0, reflect(-V, N), vWorldPos);
            envSpec += textureLod(reflectionProbe0, probeR, roughness * max(probeLod - 1.0, 0.0)).rgb *
                       ubo.reflectionProbeCenter[0].w * probeWeight0;
            envDiffuse += textureLod(reflectionProbe0, N, probeLod).rgb *
                          ubo.reflectionProbeCenter[0].w * probeWeight0;
        }
        if (probeWeight1 > 0.0) {
            float probeLod = float(max(textureQueryLevels(reflectionProbe1) - 1, 0));
            vec3 probeR = reflectionProbeDirection(1, reflect(-V, N), vWorldPos);
            envSpec += textureLod(reflectionProbe1, probeR, roughness * max(probeLod - 1.0, 0.0)).rgb *
                       ubo.reflectionProbeCenter[1].w * probeWeight1;
            envDiffuse += textureLod(reflectionProbe1, N, probeLod).rgb *
                          ubo.reflectionProbeCenter[1].w * probeWeight1;
        }
        vec3 F0 = mix(vec3(0.04), albedo, metallic);
        float NoV = max(dot(N, V), 0.0);
        vec4 brdf = roughness * vec4(-1.0, -0.0275, -0.572, 0.022) +
                    vec4(1.0, 0.0425, 1.04, -0.04);
        float a004 = min(brdf.x * brdf.x, exp2(-9.28 * NoV)) * brdf.x + brdf.y;
        vec2 dfg = vec2(-1.04, 1.04) * a004 + brdf.zw;
        vec3 specWeight = max(F0 * dfg.x + dfg.y, vec3(0.0));
        float directionalAlbedo = max(dfg.x + dfg.y, 1e-3);
        vec3 multiScatter = 1.0 + F0 * (min(1.0 / directionalAlbedo, 8.0) - 1.0);
        float horizon = clamp(1.0 + dot(reflect(-V, N), Ngeom), 0.0, 1.0);
        color += envSpec * specWeight * multiScatter * (horizon * horizon);
        vec3 F = fresnelSchlick(NoV, F0);
        // Cheap diffuse IBL for dielectrics (sample along N at a blurry lod).
        color += albedo * envDiffuse * (1.0 - metallic) * (1.0 - F) * 0.45;
    }

    color += emissive;

    float outputAlpha = (ubo.texBomb.w > 1.5 && ubo.texBomb.w < 2.5) ? base.a : 1.0;
    outColor = vec4(color, outputAlpha);
}
