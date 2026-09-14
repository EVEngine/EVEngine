// Screen-Space Displacement Mapping (SSDM) helpers — educational approximation.
// Classic SSDM (Gallagher / 2008) warps already-rendered color+depth in screen
// space. For a planar comparison card we ray-march in view space against a
// height field defined on the plane, then write FragDepth so the silhouette
// can change without UV-bound silhouette clipping (contrast with SilPOM).
//
// Include after parallax_map.glsl when both are needed.

#ifndef SSDM_GLSL
#define SSDM_GLSL

/**
 * View-space heightfield intersection for a tangent-space planar card.
 *
 * planePosWS / planeNWS — a point and normal of the undeformed mesh plane.
 * viewPosWS / viewDirWS — ray origin (camera) and normalized direction (toward scene).
 * heightMap / scale     — same convention as POM (R: 1 = raised toward viewer).
 * uvFromWorld           — maps a world hit on the plane back to mesh UV [0,1].
 *
 * Returns: xy = hit UV, z = coverage (0 = miss / outside card), w = hit distance.
 */
vec4 ssdmRaymarchPlanar(sampler2D heightMap, vec3 planePosWS, vec3 planeNWS,
                        vec3 viewPosWS, vec3 viewDirWS, float scale,
                        float minLayers, float maxLayers, mat3 worldFromUV) {
    // Extrusion slab thickness in world units ≈ UV-scale (demo cards are ~1 UV = 1 world).
    float thickness = max(scale, 1e-4);
    vec3 n = normalize(planeNWS);
    // Ray vs infinite plane (base).
    float denom = dot(n, viewDirWS);
    if (abs(denom) < 1e-5)
        return vec4(0.0);

    // March from the extruded front face toward the base plane.
    vec3 frontPos = planePosWS + n * thickness;
    float tFront = dot(n, frontPos - viewPosWS) / denom;
    float tBack = dot(n, planePosWS - viewPosWS) / denom;
    if (tFront < 0.0 && tBack < 0.0)
        return vec4(0.0);

    float t0 = min(tFront, tBack);
    float t1 = max(tFront, tBack);
    t0 = max(t0, 0.0);
    if (t1 <= t0)
        return vec4(0.0);

    float layers = clamp(mix(max(maxLayers, 1.0), max(minLayers, 1.0),
                             clamp(abs(dot(n, -viewDirWS)), 0.0, 1.0)),
                         1.0, 64.0);
    float dt = (t1 - t0) / layers;
    float t = t0;
    vec2 hitUV = vec2(0.0);
    float coverage = 0.0;
    float hitT = t1;

    // Cap iterations for lavapipe / mobile.
    for (int i = 0; i < 64; ++i) {
        if (float(i) >= layers)
            break;
        vec3 p = viewPosWS + viewDirWS * t;
        // Project to base plane for UV.
        float hPlane = dot(n, p - planePosWS);
        vec3 onPlane = p - n * hPlane;
        vec3 local = transpose(worldFromUV) * (onPlane - planePosWS);
        vec2 uv = local.xy + vec2(0.5); // card centered on planePos with size 1
        float height = texture(heightMap, clamp(uv, 0.0, 1.0)).r;
        float surfaceH = height * thickness;
        // Ray is "inside" relief when its height above the plane is below the heightfield.
        if (hPlane <= surfaceH + 1e-4) {
            hitUV = uv;
            coverage = (uv.x >= 0.0 && uv.y >= 0.0 && uv.x <= 1.0 && uv.y <= 1.0) ? 1.0 : 0.0;
            hitT = t;
            break;
        }
        t += dt;
    }
    return vec4(hitUV, coverage, hitT);
}

/**
 * Cheap SSDM screen-space color warp (post-style). offset in screen UV.
 * coverage = 1 when the source UV stays inside [0,1].
 */
vec3 ssdmScreenWarpUV(vec2 screenUV, float height01, vec2 viewScreenDir, float scale) {
    float len = length(viewScreenDir);
    vec2 dir = len > 1e-5 ? viewScreenDir / len : vec2(0.0);
    vec2 offset = dir * clamp(height01, 0.0, 1.0) * scale;
    vec2 src = screenUV + offset;
    float coverage = (src.x >= 0.0 && src.y >= 0.0 && src.x <= 1.0 && src.y <= 1.0) ? 1.0 : 0.0;
    return vec3(src, coverage);
}

#endif
