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
 *   [17] orthographic (0 perspective, 1 orthographic)
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
 *   [29] blurRadiusWorld (-1 keeps the established fixed radius)
 *   [30..31] pad
 *
 * Buffer bindings:
 *   0 particles(vec4 xyz+radius)  1 depthA(uint)  2 depthB(uint)
 *   3 thickness(uint)             4 normal(vec4)  5 color(packed RGBA8 uint)
 *   6 particle colors(vec4)       7 multicolor accum(5 uint/pixel)
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
layout(set = 0, binding = 5) buffer Color { uint c[]; } color;
layout(push_constant) uniform PC { float d[32]; } pc;
void main() {
    uint i = gl_GlobalInvocationID.x;
    uint total = uint(pc.d[23]) * uint(pc.d[24]);
    if (i >= total) return;
    depthA.d[i] = 0xFFFFFFFFu;
    depthB.d[i] = 0xFFFFFFFFu;
    thick.t[i] = 0u;
    normal.n[i] = vec4(0.0);
    color.c[i] = 0u;
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
    bool orthographic = pc.d[17] > 0.5;
    if (!orthographic && clip.w <= 1e-4) return;
    vec3 ndc = clip.xyz / clip.w;
    if (any(lessThan(ndc, vec3(-1.0))) || any(greaterThan(ndc, vec3(1.0)))) return;
    float W = pc.d[23];
    float H = pc.d[24];
    float sx = (ndc.x * 0.5 + 0.5) * W;
    float sy = (0.5 - ndc.y * 0.5) * H;
    float nearZ = max(pc.d[19], 1e-4);
    float farZ = max(pc.d[20], nearZ + 1e-3);
    float tanHalf = max(pc.d[21], 1e-4);
    float depth = orthographic ? nearZ + (ndc.z * 0.5 + 0.5) * (farZ - nearZ) : clip.w;
    float radiusPx = (pc.d[25] * (H * 0.5) / tanHalf) /
                     (orthographic ? 1.0 : max(depth, 1e-4));
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
            // Spherical cap in the projected particle disc, not a flat billboard.
            float cap = pc.d[25] * sqrt(1.0 - q);
            float t = clamp((depth - cap - nearZ) / (farZ - nearZ), 0.0, 1.0);
            uint key = uint(t * 16777215.0);
            atomicMin(depthA.d[idx], key);
            uint contribution = uint(clamp(2.0 * cap * scale * 256.0, 0.0, 16777215.0));
            atomicAdd(thick.t[idx], contribution);
        }
    }
}
)GLSL";

/**
 * @brief Clear lazily-created fixed-point multicolor accumulation buffers.
 * @lifetime The returned pointer remains valid for the process lifetime.
 * @return Process-lifetime static read-only GLSL source; the caller must not free it.
 */
inline const char* kSsfColorClear = R"GLSL(
#version 450
layout(local_size_x = 64) in;
layout(set = 0, binding = 7) buffer Accum { uint v[]; } accum;
layout(push_constant) uniform PC { float d[32]; } pc;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(pc.d[23]) * uint(pc.d[24])) return;
    uint at = i * 5u;
    for (uint channel = 0u; channel < 5u; ++channel) accum.v[at + channel] = 0u;
}
)GLSL";

/**
 * @brief Accumulate isotropic particle colors using the CPU tint pass' disc weights.
 * @lifetime The returned pointer remains valid for the process lifetime.
 * @return Process-lifetime static read-only GLSL source; the caller must not free it.
 */
inline const char* kSsfColorSplat = R"GLSL(
#version 450
layout(local_size_x = 64) in;
layout(set = 0, binding = 0) buffer Particles { vec4 p[]; } parts;
layout(set = 0, binding = 6) buffer ParticleColors { vec4 c[]; } particleColors;
layout(set = 0, binding = 7) buffer Accum { uint v[]; } accum;
layout(push_constant) uniform PC { float d[32]; } pc;
mat4 loadVP() {
    return mat4(pc.d[0], pc.d[1], pc.d[2], pc.d[3], pc.d[4], pc.d[5], pc.d[6], pc.d[7],
                pc.d[8], pc.d[9], pc.d[10], pc.d[11], pc.d[12], pc.d[13], pc.d[14], pc.d[15]);
}
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(pc.d[16])) return;
    vec4 clip = loadVP() * vec4(parts.p[i].xyz, 1.0);
    bool orthographic = pc.d[17] > 0.5;
    if (!orthographic && clip.w <= 1e-4) return;
    vec3 ndc = clip.xyz / clip.w;
    if (any(lessThan(ndc, vec3(-1.0))) || any(greaterThan(ndc, vec3(1.0)))) return;
    float W = pc.d[23], H = pc.d[24];
    float sx = (ndc.x * 0.5 + 0.5) * W;
    float sy = (0.5 - ndc.y * 0.5) * H;
    float depth = orthographic ? pc.d[19] + (ndc.z * 0.5 + 0.5) * (pc.d[20] - pc.d[19]) : clip.w;
    float radiusPx = (pc.d[25] * (H * 0.5) / max(pc.d[21], 1e-4)) /
                     (orthographic ? 1.0 : max(depth, 1e-4));
    if (radiusPx < 0.5) return;
    int x0 = int(max(floor(sx - radiusPx), 0.0)), x1 = int(min(ceil(sx + radiusPx), W - 1.0));
    int y0 = int(max(floor(sy - radiusPx), 0.0)), y1 = int(min(ceil(sy + radiusPx), H - 1.0));
    vec4 tint = clamp(particleColors.c[i], 0.0, 1.0);
    float radiusSquared = radiusPx * radiusPx;
    for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
        vec2 delta = vec2(float(x) + 0.5 - sx, float(y) + 0.5 - sy);
        float q = 1.0 - dot(delta, delta) / radiusSquared;
        if (q <= 0.0) continue;
        uint scaled = max(1u, uint(q * 4095.0));
        uint at = (uint(y) * uint(W) + uint(x)) * 5u;
        atomicAdd(accum.v[at + 0u], scaled);
        atomicAdd(accum.v[at + 1u], uint(float(scaled) * tint.r));
        atomicAdd(accum.v[at + 2u], uint(float(scaled) * tint.g));
        atomicAdd(accum.v[at + 3u], uint(float(scaled) * tint.b));
        atomicAdd(accum.v[at + 4u], uint(float(scaled) * tint.a));
    }
}
)GLSL";

// Splat preprojected oriented ellipsoids without expanding push constants.
inline constexpr auto kSsfAnisotropicSplat = R"GLSL(
#version 450
layout(local_size_x = 64) in;
layout(set = 0, binding = 0) buffer Ellipsoids { vec4 e[]; } ellipsoids;
layout(set = 0, binding = 1) buffer DepthA { uint d[]; } depthA;
layout(set = 0, binding = 3) buffer Thick { uint t[]; } thick;
layout(push_constant) uniform PC { float d[32]; } pc;

void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(pc.d[16])) return;
    vec4 projected = ellipsoids.e[i * 3u + 0u];
    vec4 inverseAndRadius = ellipsoids.e[i * 3u + 1u];
    vec4 depthSlope = ellipsoids.e[i * 3u + 2u];
    if (projected.w <= 0.0) return;
    float W = pc.d[23], H = pc.d[24];
    int x0 = int(max(floor(projected.x - projected.w), 0.0));
    int x1 = int(min(ceil(projected.x + projected.w), W - 1.0));
    int y0 = int(max(floor(projected.y - projected.w), 0.0));
    int y1 = int(min(ceil(projected.y + projected.w), H - 1.0));
    float nearZ = max(pc.d[19], 1e-4);
    float farZ = max(pc.d[20], nearZ + 1e-3);
    for (int yy = y0; yy <= y1; ++yy) for (int xx = x0; xx <= x1; ++xx) {
        vec2 delta = vec2(float(xx) + 0.5 - projected.x, float(yy) + 0.5 - projected.y);
        float q = inverseAndRadius.x * delta.x * delta.x +
                  2.0 * inverseAndRadius.y * delta.x * delta.y +
                  inverseAndRadius.z * delta.y * delta.y;
        if (q >= 1.0) continue;
        float cap = inverseAndRadius.w * sqrt(max(0.0, 1.0 - q));
        float centerDepth = projected.z + dot(depthSlope.xy, delta);
        float t = clamp((centerDepth - cap - nearZ) / (farZ - nearZ), 0.0, 1.0);
        uint idx = uint(yy) * uint(W) + uint(xx);
        atomicMin(depthA.d[idx], uint(t * 16777215.0));
        uint contribution = uint(clamp(2.0 * cap * pc.d[27] * 256.0, 0.0, 16777215.0));
        atomicAdd(thick.t[idx], contribution);
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
    float projectedBlur = pc.d[29] < 0.0 ? 2.0 :
        pc.d[29] * float(H) / (2.0 * max(pc.d[21], 1e-4) * (pc.d[17] > 0.5 ? 1.0 : center));
    int kernelRadius = clamp(int(ceil(projectedBlur)), 0, 4);
    float sigma = max(0.5, projectedBlur * 0.5);
    float sum = center;
    float wsum = 1.0;
    for (int oy = -kernelRadius; oy <= kernelRadius; ++oy) {
        for (int ox = -kernelRadius; ox <= kernelRadius; ++ox) {
            if (ox == 0 && oy == 0) continue;
            int xx = int(cx) + ox;
            int yy = int(cy) + oy;
            if (xx < 0 || yy < 0 || xx >= int(W) || yy >= int(H)) continue;
            uint idx = uint(yy) * W + uint(xx);
            uint nk = depthA.d[idx];
            if (nk == 0xFFFFFFFFu) continue;
            float nd = decode(nk);
            float spatial = exp(-float(ox * ox + oy * oy) / (2.0 * sigma * sigma));
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

float sampleDepth(ivec2 off, uint W, uint H, uint cx, uint cy) {
    int xx = int(cx) + off.x;
    int yy = int(cy) + off.y;
    if (xx < 0 || yy < 0 || xx >= int(W) || yy >= int(H)) return 1e30;
    uint k = depthA.d[uint(yy) * W + uint(xx)];
    return k == 0xFFFFFFFFu ? 1e30 : decode(k);
}

vec3 surfaceDerivative(vec3 forward, vec3 backward, bool forwardValid, bool backwardValid) {
    if (!forwardValid && backwardValid) return backward;
    if (!backwardValid && forwardValid) return forward;
    // Preserve symmetry when differences are indistinguishable at depth-key precision.
    float tolerance = 2.0 * (pc.d[20] - pc.d[19]) / 16777215.0;
    if ((!forwardValid && !backwardValid) || abs(abs(forward.z) - abs(backward.z)) <= tolerance)
        return (forward + backward) * 0.5;
    return abs(forward.z) < abs(backward.z) ? forward : backward;
}

vec3 viewPos(vec2 uv, float depth) {
    float tanHalf = max(pc.d[21], 1e-4);
    float aspect = max(pc.d[22], 1e-4);
    float scale = pc.d[17] > 0.5 ? 1.0 : depth;
    float x = (uv.x * 2.0 - 1.0) * aspect * tanHalf * scale;
    float y = (1.0 - uv.y * 2.0) * tanHalf * scale;
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
    float dl = sampleDepth(ivec2(-1, 0), W, H, cx, cy);
    float dr = sampleDepth(ivec2(1, 0), W, H, cx, cy);
    float dt = sampleDepth(ivec2(0, -1), W, H, cx, cy);
    float db = sampleDepth(ivec2(0, 1), W, H, cx, cy);
    vec3 center = viewPos(uv, cd);
    vec3 pL = viewPos(uv - vec2(texelW, 0.0), dl < 1e29 ? dl : cd);
    vec3 pR = viewPos(uv + vec2(texelW, 0.0), dr < 1e29 ? dr : cd);
    vec3 pT = viewPos(uv - vec2(0.0, texelH), dt < 1e29 ? dt : cd);
    vec3 pB = viewPos(uv + vec2(0.0, texelH), db < 1e29 ? db : cd);
    vec3 dpx = surfaceDerivative(pR - center, center - pL, dr < 1e29, dl < 1e29);
    vec3 dpy = surfaceDerivative(pB - center, center - pT, db < 1e29, dt < 1e29);
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
layout(set = 0, binding = 5) buffer Color { uint c[]; } color;
layout(set = 0, binding = 7) buffer Accum { uint v[]; } accum;
layout(push_constant) uniform PC { float d[32]; } pc;

void main() {
    uint i = gl_GlobalInvocationID.x;
    uint W = uint(pc.d[23]);
    uint H = uint(pc.d[24]);
    if (i >= W * H) return;
    if (depthA.d[i] == 0xFFFFFFFFu) {
        color.c[i] = 0u;
        return;
    }
    vec3 n = normal.n[i].xyz;
    float thickness = float(thick.t[i]) / 256.0;
    vec3 L = normalize(vec3(0.35, 0.65, 0.55));
    vec3 V = vec3(0.0, 0.0, 1.0);
    float diff = max(dot(n, L), 0.0);
    int mode = int(pc.d[26]);
    bool particleTint = mode >= 20;
    if (particleTint) mode -= 20;
    bool uniformTint = !particleTint && mode >= 10;
    if (uniformTint) mode -= 10;
    vec3 outC;
    float alpha;
    if (uniformTint || particleTint) {
        uint accumulatedAt = i * 5u;
        uint accumulatedWeight = particleTint ? accum.v[accumulatedAt] : 1u;
        if (accumulatedWeight == 0u) {
            color.c[i] = 0u;
            return;
        }
        vec4 tint = particleTint
            ? vec4(accum.v[accumulatedAt + 1u], accum.v[accumulatedAt + 2u],
                   accum.v[accumulatedAt + 3u], accum.v[accumulatedAt + 4u]) / float(accumulatedWeight)
            : vec4(pc.d[0], pc.d[1], pc.d[2], pc.d[3]);
        vec3 reflectionColor = vec3(pc.d[4], pc.d[5], pc.d[6]);
        bool lighting = pc.d[7] > 0.5;
        float smoothness = pc.d[8];
        float metalness = pc.d[9];
        float ambient = pc.d[10];
        float reflection = pc.d[11];
        float opacity = pc.d[12];
        float cutoff = pc.d[13];
        if (thickness * 10.0 < cutoff) {
            color.c[i] = 0u;
            return;
        }
        float light = lighting ?
            clamp(ambient + (1.0 - min(ambient, 1.0)) * diff, 0.0, 6.0) : 1.0;
        float exponent = 4.0 + 124.0 * smoothness;
        float spec = lighting ? pow(max(n.z, 0.0), exponent) * smoothness * 0.56 : 0.0;
        float fresnel = 0.04 + 0.96 * pow(1.0 - max(n.z, 0.0), 5.0);
        vec3 reflected = mix(reflectionColor, tint.rgb, metalness);
        outC = tint.rgb * light + reflected * fresnel * reflection + vec3(spec);
        alpha = clamp(thickness * opacity, 0.0, 1.0) * tint.a;
    } else if (mode == 1) {
        // Mud: diffuse brown, darker with thickness, rough specular.
        vec3 base = vec3(0.36, 0.23, 0.12) * (0.45 + 0.55 * diff);
        float attenuation = exp(-thickness * 1.8);
        vec3 Hv = normalize(L + V);
        float spec = pow(max(dot(n, Hv), 0.0), 8.0) * 0.12;
        outC = base * attenuation + vec3(spec);
        alpha = clamp(thickness * 0.6, 0.0, 1.0);
    } else {
        vec3 base = vec3(0.05, 0.32, 0.72) * (0.55 + 0.45 * diff);
        float fresnel = 0.04 + 0.96 * pow(1.0 - max(dot(n, V), 0.0), 5.0);
        vec3 Hv = normalize(L + V);
        float spec = pow(max(dot(n, Hv), 0.0), 64.0) * 0.45;
        outC = base + vec3(0.55, 0.72, 1.0) * fresnel * 0.75 + vec3(spec);
        alpha = clamp(thickness * 0.35, 0.0, 1.0);
    }
    // Match the CPU output's clamp and truncation (packUnorm4x8 rounds instead).
    uvec4 rgba = uvec4(clamp(vec4(outC, alpha), 0.0, 1.0) * 255.0);
    color.c[i] = rgba.x | (rgba.y << 8u) | (rgba.z << 16u) | (rgba.w << 24u);
}
)GLSL";

}  // namespace eve::fluids
