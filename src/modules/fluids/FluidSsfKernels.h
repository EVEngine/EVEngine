#pragma once

/**
 * @brief GLSL compute kernels for screen-space fluid surface reconstruction.
 *
 * Pipeline (mirrors FluidSurfaceRenderer.cpp):
 *   clear -> splat (particles to depth+thickness) -> smooth (bilateral,
 *   ping-pong) -> normals (depth gradients) -> shade (water or mud).
 *
 * Push-constant layout (float[32]):
 *   [0..15] viewProj (column-major, glm)
 *   [16] particleCount
 *   [17] pad
 *   [18] pad
 *   [19] nearZ
 *   [20] farZ
 *   [21] tanHalfFov
 *   [22] aspect
 *   [23] screenW
 *   [24] screenH
 *   [25] particleRadius
 *   [26] mode (0 water, 1 mud)
 *   [27] thicknessScale
 *   [28] depthFalloff (bilateral depth edge falloff)
 *   [29..31] pad
 *
 * Buffer bindings:
 *   0 particles(vec4 xyz+radius)  1 depthA(uint)  2 depthB(uint)
 *   3 thickness(uint)             4 normal(vec4)  5 color(vec4)
 */

namespace eve::fluids {

/** @brief Reset depth (0xFFFFFFFF), thickness, normal and color buffers. */
inline const char* kSsfClear = R"GLSL(
#version 450
layout(local_size_x = 64) in;
layout(set = 0, binding = 1) buffer DepthA { uint d[]; } depthA;
layout(set = 0, binding = 2) buffer DepthB { uint d[]; } depthB;
layout(set = 0, binding = 3) buffer Thick { uint t[]; } thick;
layout(set = 0, binding = 4) buffer Normal { vec4 n[]; } normal;
layout(set = 0, binding = 5) buffer Color { vec4 c[]; } color;
layout(push_constant) uniform PC { float d[32]; } pc;
void main() {
    uint i = gl_GlobalInvocationID.x;
    uint total = uint(pc.d[23]) * uint(pc.d[24]);
    if (i >= total) return;
    depthA.d[i] = 0xFFFFFFFFu;
    depthB.d[i] = 0xFFFFFFFFu;
    thick.t[i] = 0u;
    normal.n[i] = vec4(0.0);
    color.c[i] = vec4(0.0);
}
)GLSL";

/** @brief Splat particles into depth + thickness (atomic min / add). */
inline const char* kSsfSplat = R"GLSL(
#version 450
layout(local_size_x = 64) in;
layout(set = 0, binding = 0) buffer Particles { vec4 p[]; } parts;
layout(set = 0, binding = 1) buffer DepthA { uint d[]; } depthA;
layout(set = 0, binding = 3) buffer Thick { uint t[]; } thick;
layout(push_constant) uniform PC { float d[32]; } pc;

mat4 loadVP() {
    return mat4(pc.d[0], pc.d[1], pc.d[2], pc.d[3],
                pc.d[4], pc.d[5], pc.d[6], pc.d[7],
                pc.d[8], pc.d[9], pc.d[10], pc.d[11],
                pc.d[12], pc.d[13], pc.d[14], pc.d[15]);
}

void main() {
    uint i = gl_GlobalInvocationID.x;
    uint n = uint(pc.d[16]);
    if (i >= n) return;
    vec4 clip = loadVP() * vec4(parts.p[i].xyz, 1.0);
    if (clip.w <= 1e-4) return;
    vec3 ndc = clip.xyz / clip.w;
    if (any(lessThan(ndc, vec3(-1.0))) || any(greaterThan(ndc, vec3(1.0)))) return;
    float W = pc.d[23];
    float H = pc.d[24];
    float sx = (ndc.x * 0.5 + 0.5) * W;
    float sy = (0.5 - ndc.y * 0.5) * H;
    float depth = clip.w;  // == -viewZ for a perspective projection
    float nearZ = max(pc.d[19], 1e-4);
    float farZ = max(pc.d[20], nearZ + 1e-3);
    float t = clamp((depth - nearZ) / (farZ - nearZ), 0.0, 1.0);
    uint key = uint(t * 16777215.0);
    float tanHalf = max(pc.d[21], 1e-4);
    float radiusPx = (pc.d[25] * (H * 0.5) / tanHalf) / max(depth, 1e-4);
    if (radiusPx < 0.5) return;
    int x0 = int(max(floor(sx - radiusPx), 0.0));
    int x1 = int(min(ceil(sx + radiusPx), W - 1.0));
    int y0 = int(max(floor(sy - radiusPx), 0.0));
    int y1 = int(min(ceil(sy + radiusPx), H - 1.0));
    float r2 = radiusPx * radiusPx;
    float scale = pc.d[27];
    for (int yy = y0; yy <= y1; ++yy) {
        for (int xx = x0; xx <= x1; ++xx) {
            float ddx = float(xx) + 0.5 - sx;
            float ddy = float(yy) + 0.5 - sy;
            float q = (ddx * ddx + ddy * ddy) / r2;
            if (q >= 1.0) continue;
            uint idx = uint(yy) * uint(W) + uint(xx);
            atomicMin(depthA.d[idx], key);
            float w = 1.0 - smoothstep(0.4, 1.0, sqrt(q));
            uint contribution = uint(clamp(w * 2.0 * pc.d[25] * scale * 256.0, 0.0, 16777215.0));
            atomicAdd(thick.t[idx], contribution);
        }
    }
}
)GLSL";

/** @brief One bilateral smoothing pass (read depthA, write depthB). */
inline const char* kSsfSmooth = R"GLSL(
#version 450
layout(local_size_x = 64) in;
layout(set = 0, binding = 1) buffer DepthA { uint d[]; } depthA;
layout(set = 0, binding = 2) buffer DepthB { uint d[]; } depthB;
layout(push_constant) uniform PC { float d[32]; } pc;

float decode(uint key) {
    float nearZ = max(pc.d[19], 1e-4);
    float farZ = max(pc.d[20], nearZ + 1e-3);
    return nearZ + (float(key) / 16777215.0) * (farZ - nearZ);
}

void main() {
    uint i = gl_GlobalInvocationID.x;
    uint W = uint(pc.d[23]);
    uint H = uint(pc.d[24]);
    if (i >= W * H) return;
    uint cx = i % W;
    uint cy = i / W;
    uint centerKey = depthA.d[i];
    if (centerKey == 0xFFFFFFFFu) {
        depthB.d[i] = centerKey;
        return;
    }
    float center = decode(centerKey);
    float falloff = max(pc.d[28], 1e-4);  // depth edge falloff
    float sum = center;
    float wsum = 1.0;
    for (int oy = -2; oy <= 2; ++oy) {
        for (int ox = -2; ox <= 2; ++ox) {
            if (ox == 0 && oy == 0) continue;
            int xx = int(cx) + ox;
            int yy = int(cy) + oy;
            if (xx < 0 || yy < 0 || xx >= int(W) || yy >= int(H)) continue;
            uint idx = uint(yy) * W + uint(xx);
            uint nk = depthA.d[idx];
            if (nk == 0xFFFFFFFFu) continue;
            float nd = decode(nk);
            float spatial = exp(-(float(ox * ox + oy * oy)) * 0.5);
            float wDepth = exp(-abs(nd - center) / falloff);
            float w = spatial * wDepth;
            sum += nd * w;
            wsum += w;
        }
    }
    float outD = sum / max(wsum, 1e-5);
    float nearZ = max(pc.d[19], 1e-4);
    float farZ = max(pc.d[20], nearZ + 1e-3);
    float t = clamp((outD - nearZ) / (farZ - nearZ), 0.0, 1.0);
    depthB.d[i] = uint(t * 16777215.0);
}
)GLSL";

/** @brief Reconstruct view-space normals from the smoothed depth. */
inline const char* kSsfNormal = R"GLSL(
#version 450
layout(local_size_x = 64) in;
layout(set = 0, binding = 1) buffer DepthA { uint d[]; } depthA;
layout(set = 0, binding = 4) buffer Normal { vec4 n[]; } normal;
layout(push_constant) uniform PC { float d[32]; } pc;

float decode(uint key) {
    float nearZ = max(pc.d[19], 1e-4);
    float farZ = max(pc.d[20], nearZ + 1e-3);
    return nearZ + (float(key) / 16777215.0) * (farZ - nearZ);
}

float sampleDepth(ivec2 off, uint W, uint H, uint cx, uint cy, float fallback) {
    int xx = int(cx) + off.x;
    int yy = int(cy) + off.y;
    if (xx < 0 || yy < 0 || xx >= int(W) || yy >= int(H)) return fallback;
    uint k = depthA.d[uint(yy) * W + uint(xx)];
    return k == 0xFFFFFFFFu ? fallback : decode(k);
}

vec3 viewPos(vec2 uv, float depth) {
    float tanHalf = max(pc.d[21], 1e-4);
    float aspect = max(pc.d[22], 1e-4);
    float x = (uv.x * 2.0 - 1.0) * aspect * tanHalf * depth;
    float y = (uv.y * 2.0 - 1.0) * tanHalf * depth;
    return vec3(x, y, -depth);
}

void main() {
    uint i = gl_GlobalInvocationID.x;
    uint W = uint(pc.d[23]);
    uint H = uint(pc.d[24]);
    if (i >= W * H) return;
    uint cx = i % W;
    uint cy = i / W;
    uint ck = depthA.d[i];
    if (ck == 0xFFFFFFFFu) {
        normal.n[i] = vec4(0.0);
        return;
    }
    float cd = decode(ck);
    vec2 uv = vec2((float(cx) + 0.5) / float(W), (float(cy) + 0.5) / float(H));
    float texelW = 1.0 / float(W);
    float texelH = 1.0 / float(H);
    vec3 pL = viewPos(uv - vec2(texelW, 0.0), cd);
    vec3 pR = viewPos(uv + vec2(texelW, 0.0), cd);
    vec3 pD = viewPos(uv - vec2(0.0, texelH), cd);
    vec3 pU = viewPos(uv + vec2(0.0, texelH), cd);
    vec3 dpx = (pR - pL) * 0.5;
    vec3 dpy = (pU - pD) * 0.5;
    vec3 n = normalize(cross(dpx, dpy));
    if (n.z < 0.0) n = -n;
    normal.n[i] = vec4(n, 0.0);
}
)GLSL";

/** @brief Water/mud shading from depth + normal + thickness. */
inline const char* kSsfShade = R"GLSL(
#version 450
layout(local_size_x = 64) in;
layout(set = 0, binding = 1) buffer DepthA { uint d[]; } depthA;
layout(set = 0, binding = 3) buffer Thick { uint t[]; } thick;
layout(set = 0, binding = 4) buffer Normal { vec4 n[]; } normal;
layout(set = 0, binding = 5) buffer Color { vec4 c[]; } color;
layout(push_constant) uniform PC { float d[32]; } pc;

void main() {
    uint i = gl_GlobalInvocationID.x;
    uint W = uint(pc.d[23]);
    uint H = uint(pc.d[24]);
    if (i >= W * H) return;
    if (depthA.d[i] == 0xFFFFFFFFu) {
        color.c[i] = vec4(0.0);
        return;
    }
    vec3 n = normal.n[i].xyz;
    float thickness = float(thick.t[i]) / 256.0;
    vec3 L = normalize(vec3(0.35, 0.65, 0.55));
    vec3 V = vec3(0.0, 0.0, 1.0);
    float diff = max(dot(n, L), 0.0);
    int mode = int(pc.d[26]);
    vec3 outC;
    float alpha;
    if (mode == 1) {
        // Mud: diffuse brown, darker with thickness, rough specular.
        vec3 base = vec3(0.36, 0.23, 0.12) * (0.45 + 0.55 * diff);
        float attenuation = exp(-thickness * 1.8);
        vec3 Hv = normalize(L + V);
        float spec = pow(max(dot(n, Hv), 0.0), 8.0) * 0.12;
        outC = base * attenuation + vec3(spec);
        alpha = clamp(thickness * 0.6, 0.0, 1.0);
    } else {
        // Water: blue base, Fresnel rim, sharp specular.
        vec3 base = vec3(0.05, 0.32, 0.72) * (0.55 + 0.45 * diff);
        float fresnel = 0.04 + 0.96 * pow(1.0 - max(dot(n, V), 0.0), 5.0);
        vec3 Hv = normalize(L + V);
        float spec = pow(max(dot(n, Hv), 0.0), 64.0) * 0.45;
        outC = base + vec3(0.55, 0.72, 1.0) * fresnel * 0.75 + vec3(spec);
        alpha = clamp(thickness * 0.35, 0.0, 1.0);
    }
    color.c[i] = vec4(outC, alpha);
}
)GLSL";

}  // namespace eve::fluids
