// Parallax Occlusion Mapping (POM) — displace UV along the tangent-space view
// ray using a height map (R channel: white = raised toward viewer).
// Include with: #extension GL_GOOGLE_include_directive : enable
//               #include "parallax_map.glsl"

#ifndef PARALLAX_MAP_GLSL
#define PARALLAX_MAP_GLSL

/**
 * Build an approximate TBN (columns = T, B, N) from screen-space derivatives.
 * Returns false-ish (zero T/B) when UV derivatives are degenerate.
 */
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
 * Steep parallax + linear POM refinement.
 * scale     — UV displacement strength (0 = identity). Typical 0.02..0.08.
 * minLayers / maxLayers — adaptive ray-march steps (more when glancing).
 * height.r  — 1 = raised (toward viewer); depth for marching = 1 - height.
 */
vec2 parallaxOcclusionUV(sampler2D heightMap, vec2 uv, vec3 viewDirTS, float scale,
                         float minLayers, float maxLayers) {
    if (scale < 1e-5)
        return uv;

    float layers = mix(max(maxLayers, 1.0), max(minLayers, 1.0),
                       clamp(abs(viewDirTS.z), 0.0, 1.0));
    layers = clamp(layers, 1.0, 64.0);
    float layerDepth = 1.0 / layers;
    // Avoid explode at grazing angles.
    float vz = max(abs(viewDirTS.z), 0.08);
    vec2 P = (viewDirTS.xy / vz) * scale;
    vec2 deltaUV = P / layers;

    vec2 curUV = uv;
    float curDepth = 0.0;
    float height = texture(heightMap, curUV).r;
    float curMapDepth = 1.0 - height;

    // Cap iterations for mobile / lavapipe friendliness.
    for (int i = 0; i < 64; ++i) {
        if (curDepth >= curMapDepth || float(i) >= layers)
            break;
        curUV -= deltaUV;
        height = texture(heightMap, curUV).r;
        curMapDepth = 1.0 - height;
        curDepth += layerDepth;
    }

    // Linear interpolate between the last two samples (classic POM).
    vec2 prevUV = curUV + deltaUV;
    float after = curMapDepth - curDepth;
    float before = (1.0 - texture(heightMap, prevUV).r) - (curDepth - layerDepth);
    float denom = after - before;
    float weight = (abs(denom) < 1e-5) ? 0.5 : clamp(after / denom, 0.0, 1.0);
    return mix(curUV, prevUV, weight);
}

/**
 * Convenience: world-space N / V / pos → displaced UV.
 * Returns original uv when scale≈0 or TBN is degenerate.
 */
vec2 parallaxMappedUV(sampler2D heightMap, vec2 uv, vec3 N, vec3 worldPos, vec3 V,
                      float scale, float minLayers, float maxLayers) {
    if (scale < 1e-5)
        return uv;
    mat3 TBN = parallaxTBN(N, worldPos, uv);
    if (length(TBN[0]) < 1e-4)
        return uv;
    // View direction in tangent space (from surface toward camera).
    vec3 viewTS = normalize(transpose(TBN) * V);
    return parallaxOcclusionUV(heightMap, uv, viewTS, scale, minLayers, maxLayers);
}

#endif
