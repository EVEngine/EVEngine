// Shared PBR / CSM shadow / cloud / env shading for the GPU-driven forward
// path (mesh3d_gpudriven.frag) and the stage-3 visibility-buffer resolve
// (resolve_vis.frag). The includer must declare, in the exact same layout:
//   - struct GpuMaterialRecord (gpudriven_tables.glsl) + kInvalidSlot
//   - `Light3D` struct
//   - `ubo`    : the Mesh3D Frame uniform block (incl. bindlessEnv)
//   - `shadow` : the ShadowFrame uniform block
//   - `shadowMap` : sampler2DArrayShadow at set 0 / binding 5
//   - `textures[]` / `cubemaps[]` : bindless set-1 arrays
//   - tex_cell_bomb.glsl / parallax_map.glsl / tonemap.glsl includes

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

#ifdef EVE_RAW_SHADOW_MAP
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
#else
float blockerPenumbra(vec3 worldPos, int cascade, float bias) {
    return 0.5;
}
#endif


float sampleShadowCascade(vec3 worldPos, int cascade, float bias, float filterRadius) {
    vec4 lightClip = shadow.lightVP[cascade] * vec4(worldPos, 1.0);
    vec3 ndc = lightClip.xyz / max(lightClip.w, 1e-6);
    vec2 uv = ndc.xy * 0.5 + 0.5;
    float depth = ndc.z;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || depth < 0.0 || depth > 1.0)
        return 1.0;
    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0).xy);
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
    float hi = cascade == 0 ? shadow.splits.x : (cascade == 1 ? shadow.splits.y : shadow.splits.z);
    float lo = cascade == 0 ? 0.0 : (cascade == 1 ? shadow.splits.x : shadow.splits.y);
    float cascadeT = clamp((viewDepth - lo) / max(hi - lo, 1e-3), 0.0, 1.0);
    float filterRadius = max(mix(0.5, 2.0, cascadeT),
                             blockerPenumbra(p, cascade, slopeScaledBias(cascade, nDotL)));
    float vis = sampleShadowCascade(p, cascade, slopeScaledBias(cascade, nDotL), filterRadius);
    float band = max(0.5, (hi - lo) * 0.1);
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
    return mix(0.04, 1.0, vis);
}

// Albedo sample with parallax + tex-cell-bomb + tint. Used by both the vis
// pass (alpha discard + GBuffer albedo) and the shading functions.
vec4 fetchAlbedo(GpuMaterialRecord m, vec3 N, vec3 V, vec3 worldPos, vec2 uv) {
    uint albedoSlot = m.textureSlots[0] == kInvalidSlot ? 0u : m.textureSlots[0];
    uint heightSlot = m.textureSlots[2] == kInvalidSlot ? 0u : m.textureSlots[2];
    vec2 muv = parallaxMappedUV(textures[heightSlot], uv, N, worldPos, V, m.parallax.x,
                                m.parallax.y, m.parallax.z);
    return textureCellBomb(textures[albedoSlot], muv, m.texBomb.x, m.texBomb.y, m.texBomb.z) *
           m.tint;
}

// Lighting / GI / env core shared by the textured and flat shading paths.
// `albedo` is the final pre-lighting albedo (textured or flat tint).
vec3 shadePbrCore(GpuMaterialRecord m, GpuInstance gi, vec3 N, vec3 Ngeom, vec3 V,
                  vec3 worldPos, vec3 viewPos, vec3 albedo) {
    float metallic = clamp(m.pbr.x, 0.0, 1.0);
    float roughness = clamp(m.pbr.y, 0.04, 1.0);
    bool receiveLight = m.pbr.w > 0.5;
    int envSlot = ubo.bindlessEnv.x == kInvalidSlot ? 0 : int(ubo.bindlessEnv.x);
    int probeSlot0 = gi.reflectionProbeSlots.x == kInvalidSlot ? 0 : int(gi.reflectionProbeSlots.x);
    int probeSlot1 = gi.reflectionProbeSlots.y == kInvalidSlot ? 0 : int(gi.reflectionProbeSlots.y);

    int count = int(ubo.lightDirIntensity.w + 0.5);
    vec3 ambient = ubo.ambient.rgb;
    if (!receiveLight) {
        count = 0;
        ambient = vec3(1.0);
    }
    vec3 Lo = vec3(0.0);
    float viewDepth = max(-viewPos.z, 0.0);
    vec3 primaryL = normalize(ubo.lightDirIntensity.xyz);
    float shadowVis = receiveLight
                          ? sampleShadowPCF(worldPos, N, viewDepth, max(dot(N, primaryL), 0.0))
                          : 1.0;

    if (receiveLight && length(ubo.lightColor.rgb) > 1e-6) {
        Lo += shadeLight(N, V, albedo, metallic, roughness, primaryL, ubo.lightColor.rgb) *
              shadowVis * cloudShadowFactor(worldPos);
    }
    for (int i = 0; i < 8; ++i) {
        if (i >= count) break;
        Light3D Lgt = ubo.lights[i];
        vec3 radiance = Lgt.color.rgb;
        vec3 L;
        if (Lgt.posRadius.w <= 0.0) {
            L = normalize(Lgt.posRadius.xyz);
            if (receiveLight && length(L - primaryL) < 1e-3)
                continue;
        } else {
            vec3 toL = Lgt.posRadius.xyz - worldPos;
            float dist = length(toL);
            L = toL / max(dist, 1e-4);
            float atten = clamp(1.0 - dist / max(Lgt.posRadius.w, 1e-3), 0.0, 1.0);
            atten *= atten;
            radiance *= atten;
        }
        Lo += shadeLight(N, V, albedo, metallic, roughness, L, radiance);
    }

    float hemi = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 skyIrr = ambient * 1.1 + ubo.lightColor.rgb * 0.12;
    vec3 gndIrr = ambient * vec3(0.72, 0.62, 0.52);
    vec3 irr = mix(gndIrr, skyIrr, hemi);
    vec3 indirectDiffuse = albedo * irr * (1.0 - metallic);
    float wrap = max(dot(N, primaryL) * 0.5 + 0.5, 0.0);
    indirectDiffuse +=
        albedo * ubo.lightColor.rgb * (wrap * wrap) * 0.06 * (1.0 - metallic);
    vec3 color = indirectDiffuse + Lo;

    float envIntensity = ubo.bindlessEnv.y;
    vec3 edge0 = gi.reflectionProbeExtent[0].xyz -
                 abs(worldPos - gi.reflectionProbeCenter[0].xyz);
    vec3 edge1 = gi.reflectionProbeExtent[1].xyz -
                 abs(worldPos - gi.reflectionProbeCenter[1].xyz);
    float probeWeight0 = min(edge0.x, min(edge0.y, edge0.z)) > 0.0
                             ? clamp(min(edge0.x, min(edge0.y, edge0.z)) /
                                         max(gi.reflectionProbeExtent[0].w, 1e-4),
                                     0.0, 1.0)
                             : 0.0;
    float probeWeight1 = min(edge1.x, min(edge1.y, edge1.z)) > 0.0
                             ? clamp(min(edge1.x, min(edge1.y, edge1.z)) /
                                         max(gi.reflectionProbeExtent[1].w, 1e-4),
                                     0.0, 1.0)
                             : 0.0;
    probeWeight0 *= gi.reflectionProbeCenter[0].w > 0.0 ? 1.0 : 0.0;
    probeWeight1 *= gi.reflectionProbeCenter[1].w > 0.0 ? 1.0 : 0.0;
    float probeWeightSum = probeWeight0 + probeWeight1;
    if (probeWeightSum > 1.0) {
        probeWeight0 /= probeWeightSum;
        probeWeight1 /= probeWeightSum;
        probeWeightSum = 1.0;
    }
    float globalWeight = 1.0 - probeWeightSum;
    if (envIntensity * globalWeight + probeWeight0 + probeWeight1 > 1e-4) {
        vec3 R = boxProjectedEnv(reflect(-V, N), worldPos);
        float maxLod = float(max(textureQueryLevels(cubemaps[envSlot]) - 1, 0));
        float specMaxLod = maxLod >= 2.0 ? maxLod - 1.0 : maxLod;
        float lod = roughness * specMaxLod;
        vec3 envSpec = textureLod(cubemaps[envSlot], R, lod).rgb * envIntensity * globalWeight;
        vec3 irrEnv = textureLod(cubemaps[envSlot], N, maxLod).rgb *
                      envIntensity * globalWeight;
        if (probeWeight0 > 0.0) {
            vec3 center = gi.reflectionProbeCenter[0].xyz;
            vec3 extent = gi.reflectionProbeExtent[0].xyz;
            vec3 rawR = reflect(-V, N);
            vec3 safeR = mix(vec3(1e-5), rawR, greaterThan(abs(rawR), vec3(1e-5)));
            vec3 exitT = max((center - extent - worldPos) / safeR,
                             (center + extent - worldPos) / safeR);
            float distanceToBox = min(exitT.x, min(exitT.y, exitT.z));
            vec3 probeR = distanceToBox > 0.0
                              ? normalize(worldPos + rawR * distanceToBox - center)
                              : normalize(rawR);
            float probeLod = float(max(textureQueryLevels(cubemaps[probeSlot0]) - 1, 0));
            envSpec += textureLod(cubemaps[probeSlot0], probeR,
                                  roughness * max(probeLod - 1.0, 0.0)).rgb *
                       gi.reflectionProbeCenter[0].w * probeWeight0;
            irrEnv += textureLod(cubemaps[probeSlot0], N, probeLod).rgb *
                      gi.reflectionProbeCenter[0].w * probeWeight0;
        }
        if (probeWeight1 > 0.0) {
            vec3 center = gi.reflectionProbeCenter[1].xyz;
            vec3 extent = gi.reflectionProbeExtent[1].xyz;
            vec3 rawR = reflect(-V, N);
            vec3 safeR = mix(vec3(1e-5), rawR, greaterThan(abs(rawR), vec3(1e-5)));
            vec3 exitT = max((center - extent - worldPos) / safeR,
                             (center + extent - worldPos) / safeR);
            float distanceToBox = min(exitT.x, min(exitT.y, exitT.z));
            vec3 probeR = distanceToBox > 0.0
                              ? normalize(worldPos + rawR * distanceToBox - center)
                              : normalize(rawR);
            float probeLod = float(max(textureQueryLevels(cubemaps[probeSlot1]) - 1, 0));
            envSpec += textureLod(cubemaps[probeSlot1], probeR,
                                  roughness * max(probeLod - 1.0, 0.0)).rgb *
                       gi.reflectionProbeCenter[1].w * probeWeight1;
            irrEnv += textureLod(cubemaps[probeSlot1], N, probeLod).rgb *
                      gi.reflectionProbeCenter[1].w * probeWeight1;
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
        float horizon = clamp(1.0 + dot(reflect(-V, N), normalize(Ngeom)), 0.0, 1.0);
        color += envSpec * specWeight * multiScatter * (horizon * horizon);
        vec3 F = fresnelSchlick(NoV, F0);
        color += albedo * irrEnv * (1.0 - metallic) * (1.0 - F) * 0.45;
    }

    return max(color, vec3(0.0));
}

// Full GPU-driven opaque shading (forward + resolve share this). Returns the
// tonemapped color; caller appends the linear-depth alpha channel.
vec3 shadeGpuDrivenPixel(GpuMaterialRecord m, GpuInstance gi, vec3 Ngeom, vec3 V, vec3 worldPos, vec3 viewPos,
                         vec2 uv) {
    float bombScale = m.texBomb.x;
    float bombStrength = m.texBomb.y;
    float bombRot = m.texBomb.z;
    uint albedoSlot = m.textureSlots[0] == kInvalidSlot ? 0u : m.textureSlots[0];
    uint normalSlot = m.textureSlots[1] == kInvalidSlot ? 0u : m.textureSlots[1];
    uint heightSlot = m.textureSlots[2] == kInvalidSlot ? 0u : m.textureSlots[2];

    vec3 N = Ngeom;
    vec2 muv = parallaxMappedUV(textures[heightSlot], uv, N, worldPos, V, m.parallax.x,
                                m.parallax.y, m.parallax.z);
    vec4 base = textureCellBomb(textures[albedoSlot], muv, bombScale, bombStrength, bombRot) *
                m.tint;
    if (base.a < 0.5)
        discard;
    vec3 albedo = base.rgb;

    // Only apply a normal map when the material actually provides one.
    if (m.textureSlots[1] != kInvalidSlot) {
        vec3 nSample = textureCellBomb(textures[normalSlot], muv, bombScale, bombStrength,
                                       bombRot).xyz;
        if (length(nSample - vec3(0.5, 0.5, 1.0)) > 0.04)
            N = applyNormalMap(N, nSample, worldPos, muv);
    }
    return shadePbrCore(m, gi, N, Ngeom, V, worldPos, viewPos, albedo);
}

// Flat shading for virtual-geometry clusters (no uv/normal/texture streams):
// same lighting / GI / env, albedo = material tint.
vec3 shadeGpuDrivenPixelFlat(GpuMaterialRecord m, GpuInstance gi, vec3 Ngeom, vec3 V, vec3 worldPos,
                             vec3 viewPos) {
    vec3 N = Ngeom;
    return shadePbrCore(m, gi, N, Ngeom, V, worldPos, viewPos, m.tint.rgb);
}
