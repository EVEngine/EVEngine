// Screen-Space Displacement Mapping (SSDM) — full planar + screen-warp helpers.
// Include after parallax_map.glsl when both are needed.
//
// Classic SSDM (screen-space image warping) displaces already-rendered color and
// depth along the projected relief. For a planar proxy we equivalently ray-march
// the heightfield in world/view space and write FragDepth from the hit — that is
// the geometrically correct planar form of SSDM / relief mapping.
//
// Height.r: 1 = raised toward the plane normal (viewer-facing for the demo cards).

#ifndef SSDM_GLSL
#define SSDM_GLSL

/**
 * World-space heightfield intersection for a tangent-plane card.
 *
 * planePosWS / planeNWS — point + normal of the undeformed mesh plane.
 * camPosWS / viewDirWS  — ray origin (camera) and normalized direction (into scene).
 * uvBasisWS             — columns = (T, B, N) mapping plane UV offsets to world.
 *                         UV (0,0) sits at planePosWS - 0.5*T - 0.5*B for a unit card,
 *                         or use planeOriginUV for a custom origin.
 *
 * Returns: xy = hit UV, z = coverage (1 inside chart), w = hit distance along ray.
 */
vec4 ssdmRaymarchPlanar(sampler2D heightMap, vec3 planePosWS, vec3 planeNWS,
                        vec3 camPosWS, vec3 viewDirWS, float thickness,
                        float minLayers, float maxLayers, mat3 uvBasisWS) {
    float thick = max(thickness, 1e-4);
    vec3 n = normalize(planeNWS);
    float denom = dot(n, viewDirWS);
    if (abs(denom) < 1e-5)
        return vec4(0.0);

    // Slab: front = plane + n*thick, back = plane.
    vec3 frontPos = planePosWS + n * thick;
    float tFront = dot(n, frontPos - camPosWS) / denom;
    float tBack = dot(n, planePosWS - camPosWS) / denom;
    float t0 = max(min(tFront, tBack), 0.0);
    float t1 = max(tFront, tBack);
    if (t1 <= t0)
        return vec4(0.0);

    float layers = clamp(mix(max(maxLayers, 1.0), max(minLayers, 1.0),
                             clamp(abs(dot(n, -viewDirWS)), 0.0, 1.0)),
                         1.0, 64.0);
    float dt = (t1 - t0) / layers;
    float t = t0;
    vec3 T = uvBasisWS[0];
    vec3 B = uvBasisWS[1];
    // Unit card centered on planePos: UV = localXY + 0.5
    for (int i = 0; i < 64; ++i) {
        if (float(i) >= layers)
            break;
        vec3 p = camPosWS + viewDirWS * t;
        float hPlane = dot(n, p - planePosWS);
        vec3 onPlane = p - n * hPlane;
        vec3 delta = onPlane - planePosWS;
        vec2 uv = vec2(dot(delta, T), dot(delta, B)) + vec2(0.5);
        float height = texture(heightMap, clamp(uv, 0.0, 1.0)).r;
        float surfaceH = height * thick;
        if (hPlane <= surfaceH + 1e-4) {
            float cov = (uv.x >= 0.0 && uv.y >= 0.0 && uv.x <= 1.0 && uv.y <= 1.0) ? 1.0 : 0.0;
            return vec4(uv, cov, t);
        }
        t += dt;
    }
    return vec4(0.0);
}

/**
 * Refine a planar SSDM hit with a few binary steps between the last free and
 * occupied samples (caller passes the bracketing distances).
 */
vec4 ssdmRefinePlanar(sampler2D heightMap, vec3 planePosWS, vec3 planeNWS,
                      vec3 camPosWS, vec3 viewDirWS, float thickness,
                      float tFree, float tHit, mat3 uvBasisWS) {
    float thick = max(thickness, 1e-4);
    vec3 n = normalize(planeNWS);
    vec3 T = uvBasisWS[0];
    vec3 B = uvBasisWS[1];
    float t0 = tFree;
    float t1 = tHit;
    vec2 uv = vec2(0.5);
    for (int i = 0; i < 5; ++i) {
        float tm = 0.5 * (t0 + t1);
        vec3 p = camPosWS + viewDirWS * tm;
        float hPlane = dot(n, p - planePosWS);
        vec3 onPlane = p - n * hPlane;
        vec3 delta = onPlane - planePosWS;
        uv = vec2(dot(delta, T), dot(delta, B)) + vec2(0.5);
        float height = texture(heightMap, clamp(uv, 0.0, 1.0)).r;
        if (hPlane <= height * thick)
            t1 = tm;
        else
            t0 = tm;
    }
    float cov = (uv.x >= 0.0 && uv.y >= 0.0 && uv.x <= 1.0 && uv.y <= 1.0) ? 1.0 : 0.0;
    return vec4(uv, cov, t1);
}

/**
 * Screen-space SSDM color warp (post-style).
 * March along the projected displacement direction, using a height/mask texture
 * sampled at the candidate screen UV. Returns xy = source UV to resample,
 * z = coverage, w = estimated displaced linear depth (or input depth on miss).
 *
 * sceneDepth  — hardware or linear depth at the destination pixel (starting depth).
 * heightMask  — .r = height01, .a = material mask (0 = skip warp).
 * viewDirSS   — displacement direction in screen UV (typically projected view-XY).
 */
vec4 ssdmScreenWarp(sampler2D sceneColor, sampler2D sceneDepth, sampler2D heightMask,
                    vec2 screenUV, vec2 viewDirSS, float scale, float minLayers, float maxLayers,
                    float startDepth) {
    float mask = texture(heightMask, screenUV).a;
    if (mask < 0.01 || scale < 1e-5)
        return vec4(screenUV, 0.0, startDepth);

    float len = length(viewDirSS);
    vec2 dir = len > 1e-5 ? viewDirSS / len : vec2(0.0, 0.0);
    float layers = clamp(mix(max(maxLayers, 1.0), max(minLayers, 1.0), 0.5), 1.0, 32.0);
    float stepUV = scale / layers;
    vec2 cur = screenUV;
    float bestDepth = startDepth;
    vec2 bestUV = screenUV;
    bool hit = false;

    // Inverse-search style: walk from the pixel toward the relief source.
    for (int i = 0; i < 32; ++i) {
        if (float(i) >= layers)
            break;
        cur -= dir * stepUV;
        if (cur.x < 0.0 || cur.y < 0.0 || cur.x > 1.0 || cur.y > 1.0)
            break;
        vec4 hm = texture(heightMask, cur);
        if (hm.a < 0.01)
            continue;
        float d = texture(sceneDepth, cur).r;
        // Raised height pulls the surface toward the camera (smaller ZO depth).
        float displaced = clamp(d - hm.r * scale * 0.15, 0.0, 1.0);
        if (displaced <= startDepth + 1e-4) {
            bestUV = cur;
            bestDepth = displaced;
            hit = true;
            break;
        }
    }
    return vec4(bestUV, hit ? 1.0 : 0.0, bestDepth);
}

/** Cheap single-tap SSDM screen offset (CPU/GLSL reference parity). */
vec3 ssdmScreenWarpUV(vec2 screenUV, float height01, vec2 viewScreenDir, float scale) {
    float len = length(viewScreenDir);
    vec2 dir = len > 1e-5 ? viewScreenDir / len : vec2(0.0);
    vec2 offset = dir * clamp(height01, 0.0, 1.0) * scale;
    vec2 src = screenUV + offset;
    float coverage = (src.x >= 0.0 && src.y >= 0.0 && src.x <= 1.0 && src.y <= 1.0) ? 1.0 : 0.0;
    return vec3(src, coverage);
}

/** FragDepth from a world-space hit using the draw MVP (hit in model space). */
float ssdmFragDepthFromLocal(mat4 mvp, vec3 hitLocal) {
    vec4 clip = mvp * vec4(hitLocal, 1.0);
    return clamp(clip.z / max(clip.w, 1e-6), 0.0, 1.0);
}

#endif
