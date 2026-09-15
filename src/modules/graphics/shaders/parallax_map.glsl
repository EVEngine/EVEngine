// Parallax Occlusion Mapping (POM) + Silhouette POM helpers.
// Include with: #extension GL_GOOGLE_include_directive : enable
//               #include "parallax_map.glsl"
//
// Height.r convention: 1 = raised toward the viewer. March depth = 1 - height.
// SilPOM (full): steep POM + soft chart coverage + optional horizon trim +
// view-space hit depth for gl_FragDepth. Silhouette growth beyond a thin quad
// still requires an extruded proxy mesh (fins / slab) — FragDepth alone cannot
// expand the rasterized footprint.

#ifndef PARALLAX_MAP_GLSL
#define PARALLAX_MAP_GLSL

mat3 parallaxTBN(vec3 N, vec3 worldPos, vec2 uv) {
    vec3 n = normalize(N);
    vec3 dp1 = dFdx(worldPos);
    vec3 dp2 = dFdy(worldPos);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    float det = duv1.x * duv2.y - duv2.x * duv1.y;
    if (abs(det) < 1e-8)
        return mat3(vec3(0.0), vec3(0.0), n);
    vec3 T = normalize(cross(dp2, n) * duv1.x + cross(n, dp1) * duv2.x);
    vec3 B = normalize(cross(dp2, n) * duv1.y + cross(n, dp1) * duv2.y);
    if (length(T) < 1e-4 || length(B) < 1e-4)
        return mat3(vec3(0.0), vec3(0.0), n);
    return mat3(T, B, n);
}

/**
 * Steep POM + binary refinement.
 * Returns: xy = hit UV, z = hit depth in [0,1] (0 = front / raised, 1 = base),
 *          w = 1 if a surface was found inside the layer budget.
 */
vec4 parallaxOcclusionHit(sampler2D heightMap, vec2 uv, vec3 viewDirTS, float scale,
                          float minLayers, float maxLayers, bool wrapU) {
    if (scale < 1e-5)
        return vec4(uv, 0.0, 1.0);

    float layers = mix(max(maxLayers, 1.0), max(minLayers, 1.0),
                       clamp(abs(viewDirTS.z), 0.0, 1.0));
    layers = clamp(layers, 1.0, 64.0);
    float layerDepth = 1.0 / layers;
    float vz = max(abs(viewDirTS.z), 0.08);
    vec2 deltaUV = ((viewDirTS.xy / vz) * scale) / layers;

    vec2 curUV = uv;
    float curDepth = 0.0;
    float sampleH = wrapU ? texture(heightMap, vec2(fract(curUV.x), clamp(curUV.y, 0.0, 1.0))).r
                          : texture(heightMap, clamp(curUV, 0.0, 1.0)).r;
    float curMapDepth = 1.0 - sampleH;
    bool hit = false;

    for (int i = 0; i < 64; ++i) {
        if (float(i) >= layers)
            break;
        if (curDepth >= curMapDepth) {
            hit = true;
            break;
        }
        curUV -= deltaUV;
        sampleH = wrapU ? texture(heightMap, vec2(fract(curUV.x), clamp(curUV.y, 0.0, 1.0))).r
                        : texture(heightMap, clamp(curUV, 0.0, 1.0)).r;
        curMapDepth = 1.0 - sampleH;
        curDepth += layerDepth;
    }

    vec2 prevUV = curUV + deltaUV;
    float after = curMapDepth - curDepth;
    float prevH = wrapU ? texture(heightMap, vec2(fract(prevUV.x), clamp(prevUV.y, 0.0, 1.0))).r
                        : texture(heightMap, clamp(prevUV, 0.0, 1.0)).r;
    float before = (1.0 - prevH) - (curDepth - layerDepth);
    float denom = after - before;
    float weight = (abs(denom) < 1e-5) ? 0.5 : clamp(after / denom, 0.0, 1.0);
    vec2 hitUV = mix(curUV, prevUV, weight);
    float hitDepth = mix(curDepth, curDepth - layerDepth, weight);
    return vec4(hitUV, clamp(hitDepth, 0.0, 1.0), hit ? 1.0 : 0.0);
}

vec2 parallaxOcclusionUV(sampler2D heightMap, vec2 uv, vec3 viewDirTS, float scale,
                         float minLayers, float maxLayers) {
    return parallaxOcclusionHit(heightMap, uv, viewDirTS, scale, minLayers, maxLayers, false).xy;
}

vec2 parallaxMappedUV(sampler2D heightMap, vec2 uv, vec3 N, vec3 worldPos, vec3 V,
                      float scale, float minLayers, float maxLayers) {
    if (scale < 1e-5)
        return uv;
    mat3 TBN = parallaxTBN(N, worldPos, uv);
    if (length(TBN[0]) < 1e-4)
        return uv;
    vec3 viewTS = normalize(transpose(TBN) * V);
    return parallaxOcclusionUV(heightMap, uv, viewTS, scale, minLayers, maxLayers);
}

/** Hard chart coverage (legacy SilPOM clip). */
float silPomCoverage(vec2 displacedUV, float padding) {
    // padding expands the keep region to [-pad, 1+pad].
    float pad = max(padding, 0.0);
    return (displacedUV.x >= -pad && displacedUV.y >= -pad &&
            displacedUV.x <= 1.0 + pad && displacedUV.y <= 1.0 + pad)
               ? 1.0
               : 0.0;
}

/** Soft chart coverage in [0,1] — feather near the UV border. */
float silPomCoverageSoft(vec2 displacedUV, float feather) {
    float f = max(feather, 1e-4);
    float cx = min(smoothstep(0.0, f, displacedUV.x),
                   smoothstep(0.0, f, 1.0 - displacedUV.x));
    float cy = min(smoothstep(0.0, f, displacedUV.y),
                   smoothstep(0.0, f, 1.0 - displacedUV.y));
    return cx * cy;
}

/**
 * Curved-surface horizon trim (SPOM-style): near-grazing fragments whose height
 * is below a view-dependent threshold are clipped so low relief does not smear
 * past the geometric limb.
 */
float silPomHorizonTrim(float height01, float ndotv, float strength, float bias) {
    float t = clamp(1.0 - abs(ndotv) / 0.35, 0.0, 1.0);
    float threshold = clamp(pow(t, 1.5) * strength, 0.0, 1.0);
    return (height01 - bias >= threshold) ? 1.0 : 0.0;
}

/** Central-difference tangent-space normal from the height map. */
vec3 parallaxHeightNormalTS(sampler2D heightMap, vec2 uv, float scale, bool wrapU) {
    vec2 texSize = vec2(textureSize(heightMap, 0));
    vec2 texel = 1.0 / max(texSize, vec2(1.0));
    vec2 uL = uv + vec2(-texel.x, 0.0);
    vec2 uR = uv + vec2( texel.x, 0.0);
    vec2 uD = uv + vec2(0.0, -texel.y);
    vec2 uU = uv + vec2(0.0,  texel.y);
    float hL = wrapU ? texture(heightMap, vec2(fract(uL.x), clamp(uL.y, 0.0, 1.0))).r
                     : texture(heightMap, clamp(uL, 0.0, 1.0)).r;
    float hR = wrapU ? texture(heightMap, vec2(fract(uR.x), clamp(uR.y, 0.0, 1.0))).r
                     : texture(heightMap, clamp(uR, 0.0, 1.0)).r;
    float hD = wrapU ? texture(heightMap, vec2(fract(uD.x), clamp(uD.y, 0.0, 1.0))).r
                     : texture(heightMap, clamp(uD, 0.0, 1.0)).r;
    float hU = wrapU ? texture(heightMap, vec2(fract(uU.x), clamp(uU.y, 0.0, 1.0))).r
                     : texture(heightMap, clamp(uU, 0.0, 1.0)).r;
    // scale converts height delta into a plausible slope in tangent units.
    return normalize(vec3((hL - hR) * scale * 8.0,
                          (hD - hU) * scale * 8.0,
                          1.0));
}

/**
 * POM self-shadow: march from the hit toward a tangent-space light and see if
 * a taller height occludes it. Returns 1 = lit, 0 = fully shadowed.
 */
float parallaxSelfShadow(sampler2D heightMap, vec2 hitUV, float hitDepth,
                         vec3 lightDirTS, float scale, float minLayers, float maxLayers,
                         bool wrapU) {
    if (scale < 1e-5 || lightDirTS.z <= 0.0)
        return 1.0;
    float layers = clamp(mix(max(maxLayers, 1.0), max(minLayers, 1.0),
                             clamp(lightDirTS.z, 0.0, 1.0)),
                         1.0, 32.0);
    float layerDepth = 1.0 / layers;
    float vz = max(lightDirTS.z, 0.08);
    vec2 deltaUV = ((lightDirTS.xy / vz) * scale) / layers;
    vec2 curUV = hitUV;
    float curDepth = hitDepth;
    for (int i = 0; i < 32; ++i) {
        if (float(i) >= layers || curDepth <= 0.0)
            break;
        curUV += deltaUV;
        curDepth -= layerDepth;
        float h = wrapU ? texture(heightMap, vec2(fract(curUV.x), clamp(curUV.y, 0.0, 1.0))).r
                        : texture(heightMap, clamp(curUV, 0.0, 1.0)).r;
        float mapDepth = 1.0 - h;
        if (mapDepth < curDepth - 0.01)
            return 0.15; // occluded
    }
    return 1.0;
}

/**
 * Full SilPOM sample: POM hit + hard/soft coverage + optional horizon trim.
 * Returns: xy = UV, z = coverage, w = hit depth [0,1].
 */
vec4 silPomSample(sampler2D heightMap, vec2 uv, vec3 N, vec3 worldPos, vec3 V,
                  float scale, float minLayers, float maxLayers,
                  float padding, float softFeather, float horizonStrength) {
    mat3 TBN = parallaxTBN(N, worldPos, uv);
    if (length(TBN[0]) < 1e-4)
        return vec4(uv, 1.0, 0.0);
    vec3 viewTS = normalize(transpose(TBN) * V);
    vec4 hit = parallaxOcclusionHit(heightMap, uv, viewTS, scale, minLayers, maxLayers, false);
    float coverage = silPomCoverage(hit.xy, padding);
    if (softFeather > 1e-5)
        coverage = min(coverage, silPomCoverageSoft(hit.xy, softFeather));
    if (horizonStrength > 1e-5) {
        float h = texture(heightMap, clamp(hit.xy, 0.0, 1.0)).r;
        coverage *= silPomHorizonTrim(h, abs(dot(normalize(N), normalize(V))),
                                      horizonStrength, 0.02);
    }
    return vec4(hit.xy, coverage, hit.z);
}

vec3 parallaxMappedUVSilhouette(sampler2D heightMap, vec2 uv, vec3 N, vec3 worldPos, vec3 V,
                                float scale, float minLayers, float maxLayers,
                                float silhouetteEnabled, float padding) {
    vec2 mapped = parallaxMappedUV(heightMap, uv, N, worldPos, V, scale, minLayers, maxLayers);
    float coverage = 1.0;
    if (silhouetteEnabled > 0.5)
        coverage = silPomCoverage(mapped, padding);
    return vec3(mapped, coverage);
}

/**
 * Convert a world-space hit to Vulkan RH_ZO FragDepth via the draw's MVP.
 * hitLocal must be in the same space as the mesh positions feeding `mvp`.
 */
float parallaxFragDepthFromLocal(mat4 mvp, vec3 hitLocal) {
    vec4 clip = mvp * vec4(hitLocal, 1.0);
    return clamp(clip.z / max(clip.w, 1e-6), 0.0, 1.0);
}

#endif
