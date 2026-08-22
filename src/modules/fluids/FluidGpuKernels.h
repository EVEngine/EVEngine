#pragma once

/**
 * @brief GLSL compute kernels for the GPU surface-flow solver.
 *
 * The kernels mirror FluidSimulation.cpp 1:1. Push-constant layout (float[32]):
 *
 *   [0]  count
 *   [1]  dt
 *   [2]  particleRadius
 *   [3]  supportRadius h
 *   [4]  restDensity
 *   [5]  gravityX   [6] gravityY   [7] gravityZ
 *   [8]  viscosity
 *   [9]  yieldStress   (Phase 3)
 *   [10] cohesion
 *   [11] adhesion
 *   [12] damping
 *   [13] maxVelocity
 *   [14] gridResX  [15] gridResY  [16] gridResZ
 *   [17] gridOriginX [18] gridOriginY [19] gridOriginZ
 *   [20] gridCellSize
 *   [21] sdfResX [22] sdfResY [23] sdfResZ
 *   [24] sdfOriginX [25] sdfOriginY [26] sdfOriginZ
 *   [27] sdfCellSize
 *   [28] iterations
 *   [29] mode (0 water, 1 mud)
 *   [30] time
 *   [31] pbfIterations
 *
 * Buffer bindings:
 *   0 pos(vec4)   1 vel(vec4)   2 cellHead(int)   3 cellNext(int)
 *   4 densities(float)   5 sdf(float)   6 lambdas(float)   7 gradSums/deltas(vec4)
 */

namespace eve::fluids {

/** @brief Zero the linked-list cell heads. */
inline const char* kFluidClearGrid = R"GLSL(
#version 450
layout(local_size_x = 64) in;
layout(set = 0, binding = 2) buffer CellHead { int h[]; } head;
layout(push_constant) uniform PC { float d[32]; } pc;
void main() {
    uint i = gl_GlobalInvocationID.x;
    uint resX = uint(pc.d[14]);
    uint resY = uint(pc.d[15]);
    uint resZ = uint(pc.d[16]);
    if (i < resX * resY * resZ) head.h[i] = -1;
}
)GLSL";

/** @brief Insert every particle into its grid cell (linked list via atomicExchange). */
inline const char* kFluidBuildGrid = R"GLSL(
#version 450
layout(local_size_x = 64) in;
layout(set = 0, binding = 0) buffer Pos { vec4 p[]; } pos;
layout(set = 0, binding = 2) buffer CellHead { int h[]; } head;
layout(set = 0, binding = 3) buffer CellNext { int nx[]; } next;
layout(push_constant) uniform PC { float d[32]; } pc;
void main() {
    uint i = gl_GlobalInvocationID.x;
    uint n = uint(pc.d[0]);
    if (i >= n) return;
    vec3 origin = vec3(pc.d[17], pc.d[18], pc.d[19]);
    float cs = pc.d[20];
    ivec3 dim = ivec3(int(pc.d[14]), int(pc.d[15]), int(pc.d[16]));
    ivec3 c = ivec3(floor((pos.p[i].xyz - origin) / cs));
    c = clamp(c, ivec3(0), dim - ivec3(1));
    int cell = c.x + dim.x * (c.y + dim.y * c.z);
    int prev = atomicExchange(head.h[cell], int(i));
    next.nx[i] = prev;
}
)GLSL";

/** @brief Density, gradient sum and PBF lambda in one pass (mirror computeDensitiesAndGrads + computeLambdas). */
inline const char* kFluidDensityLambda = R"GLSL(
#version 450
layout(local_size_x = 64) in;
layout(set = 0, binding = 0) buffer Pos { vec4 p[]; } pos;
layout(set = 0, binding = 2) buffer CellHead { int h[]; } head;
layout(set = 0, binding = 3) buffer CellNext { int nx[]; } next;
layout(set = 0, binding = 4) buffer Dens { float d[]; } dens;
layout(set = 0, binding = 6) buffer Lambda { float l[]; } lam;
layout(set = 0, binding = 7) buffer Grad { vec4 g[]; } grad;
layout(push_constant) uniform PC { float d[32]; } pc;

float poly6(float r2, float h) {
    float h2 = h * h;
    if (r2 >= h2 || r2 <= 0.0) return 0.0;
    float q = h2 - r2;
    return 315.0 / (64.0 * 3.14159265358979 * pow(h, 9.0)) * q * q * q;
}

vec3 spikyGrad(vec3 dx, float h) {
    float r2 = dot(dx, dx);
    if (r2 >= h * h || r2 <= 1e-12) return vec3(0.0);
    float r = sqrt(r2);
    float q = h - r;
    float k = -45.0 / (3.14159265358979 * pow(h, 6.0));
    return dx * (k * q * q / r);
}

void main() {
    uint i = gl_GlobalInvocationID.x;
    uint n = uint(pc.d[0]);
    if (i >= n) return;
    float h = pc.d[3];
    float h2 = h * h;
    vec3 pi = pos.p[i].xyz;
    vec3 origin = vec3(pc.d[17], pc.d[18], pc.d[19]);
    float cs = pc.d[20];
    ivec3 dim = ivec3(int(pc.d[14]), int(pc.d[15]), int(pc.d[16]));
    ivec3 c = ivec3(floor((pi - origin) / cs));
    c = clamp(c, ivec3(0), dim - ivec3(1));
    float rho = 0.0;
    vec3 gradSum = vec3(0.0);
    for (int oz = -1; oz <= 1; ++oz) {
        for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
                ivec3 cc = c + ivec3(ox, oy, oz);
                if (any(lessThan(cc, ivec3(0))) || any(greaterThanEqual(cc, dim))) continue;
                int cell = cc.x + dim.x * (cc.y + dim.y * cc.z);
                for (int j = head.h[cell]; j >= 0; j = next.nx[j]) {
                    vec3 dx = pi - pos.p[j].xyz;
                    float r2 = dot(dx, dx);
                    if (r2 < h2) {
                        rho += poly6(r2, h);
                        gradSum += spikyGrad(dx, h);
                    }
                }
            }
        }
    }
    dens.d[i] = rho;
    grad.g[i] = vec4(gradSum, 0.0);
    float rho0 = max(pc.d[4], 1e-6);
    float C = rho / rho0 - 1.0;
    lam.l[i] = -C / (dot(gradSum, gradSum) + 1e-6);
}
)GLSL";

/** @brief Accumulate PBF position deltas into the (reused) grad buffer. */
inline const char* kFluidComputeDelta = R"GLSL(
#version 450
layout(local_size_x = 64) in;
layout(set = 0, binding = 0) buffer Pos { vec4 p[]; } pos;
layout(set = 0, binding = 2) buffer CellHead { int h[]; } head;
layout(set = 0, binding = 3) buffer CellNext { int nx[]; } next;
layout(set = 0, binding = 6) buffer Lambda { float l[]; } lam;
layout(set = 0, binding = 7) buffer Grad { vec4 g[]; } grad;
layout(push_constant) uniform PC { float d[32]; } pc;

vec3 spikyGrad(vec3 dx, float h) {
    float r2 = dot(dx, dx);
    if (r2 >= h * h || r2 <= 1e-12) return vec3(0.0);
    float r = sqrt(r2);
    float q = h - r;
    float k = -45.0 / (3.14159265358979 * pow(h, 6.0));
    return dx * (k * q * q / r);
}

void main() {
    uint i = gl_GlobalInvocationID.x;
    uint n = uint(pc.d[0]);
    if (i >= n) return;
    float h = pc.d[3];
    float h2 = h * h;
    float rho0 = max(pc.d[4], 1e-6);
    vec3 pi = pos.p[i].xyz;
    float li = lam.l[i];
    vec3 origin = vec3(pc.d[17], pc.d[18], pc.d[19]);
    float cs = pc.d[20];
    ivec3 dim = ivec3(int(pc.d[14]), int(pc.d[15]), int(pc.d[16]));
    ivec3 c = ivec3(floor((pi - origin) / cs));
    c = clamp(c, ivec3(0), dim - ivec3(1));
    vec3 delta = vec3(0.0);
    for (int oz = -1; oz <= 1; ++oz) {
        for (int oy = -1; oy <= 1; ++oy) {
            for (int ox = -1; ox <= 1; ++ox) {
                ivec3 cc = c + ivec3(ox, oy, oz);
                if (any(lessThan(cc, ivec3(0))) || any(greaterThanEqual(cc, dim))) continue;
                int cell = cc.x + dim.x * (cc.y + dim.y * cc.z);
                for (int j = head.h[cell]; j >= 0; j = next.nx[j]) {
                    if (j == int(i)) continue;
                    vec3 dx = pi - pos.p[j].xyz;
                    float r2 = dot(dx, dx);
                    if (r2 < h2) delta += (li + lam.l[j]) * spikyGrad(dx, h);
                }
            }
        }
    }
    grad.g[i] = vec4(delta / rho0, 0.0);
}
)GLSL";

/** @brief Apply PBF deltas and re-project onto the SDF surface. */
inline const char* kFluidApplyDelta = R"GLSL(
#version 450
layout(local_size_x = 64) in;
layout(set = 0, binding = 0) buffer Pos { vec4 p[]; } pos;
layout(set = 0, binding = 2) buffer CellHead { int h[]; } head;
layout(set = 0, binding = 3) buffer CellNext { int nx[]; } next;
layout(set = 0, binding = 7) buffer Grad { vec4 g[]; } grad;
layout(set = 0, binding = 5) buffer Sdf { float d[]; } sdf;
layout(push_constant) uniform PC { float d[32]; } pc;

float sdfValue(ivec3 res, int x, int y, int z) {
    int idx = x + res.x * (y + res.y * z);
    return sdf.d[idx];
}

float sdfSample(vec3 p) {
    ivec3 res = ivec3(int(pc.d[21]), int(pc.d[22]), int(pc.d[23]));
    vec3 o = vec3(pc.d[24], pc.d[25], pc.d[26]);
    float cs = pc.d[27];
    vec3 f = (p - o) / cs;
    vec3 clamped = clamp(f, vec3(0.0), vec3(res - 1));
    ivec3 i0 = ivec3(floor(clamped));
    ivec3 i1 = min(i0 + ivec3(1), res - ivec3(1));
    vec3 t = fract(clamped);
    float v000 = sdfValue(res, i0.x, i0.y, i0.z);
    float v100 = sdfValue(res, i1.x, i0.y, i0.z);
    float v010 = sdfValue(res, i0.x, i1.y, i0.z);
    float v110 = sdfValue(res, i1.x, i1.y, i0.z);
    float v001 = sdfValue(res, i0.x, i0.y, i1.z);
    float v101 = sdfValue(res, i1.x, i0.y, i1.z);
    float v011 = sdfValue(res, i0.x, i1.y, i1.z);
    float v111 = sdfValue(res, i1.x, i1.y, i1.z);
    float c00 = v000 + (v100 - v000) * t.x;
    float c10 = v010 + (v110 - v010) * t.x;
    float c01 = v001 + (v101 - v001) * t.x;
    float c11 = v011 + (v111 - v011) * t.x;
    float c0 = c00 + (c10 - c00) * t.y;
    float c1 = c01 + (c11 - c01) * t.y;
    return c0 + (c1 - c0) * t.z;
}

vec3 sdfGrad(vec3 p) {
    float e = pc.d[27];
    return vec3(sdfSample(p + vec3(e, 0.0, 0.0)) - sdfSample(p - vec3(e, 0.0, 0.0)),
                sdfSample(p + vec3(0.0, e, 0.0)) - sdfSample(p - vec3(0.0, e, 0.0)),
                sdfSample(p + vec3(0.0, 0.0, e)) - sdfSample(p - vec3(0.0, 0.0, e))) / (2.0 * e);
}

void main() {
    uint i = gl_GlobalInvocationID.x;
    uint n = uint(pc.d[0]);
    if (i >= n) return;
    vec3 p = pos.p[i].xyz + grad.g[i].xyz;
    float radius = pc.d[2];
    float d = sdfSample(p);
    if (d < radius) {
        vec3 nn = sdfGrad(p);
        float nl = length(nn);
        if (nl > 1e-6) nn /= nl;
        else nn = vec3(0.0, 1.0, 0.0);
        p = p - nn * (d - radius);
    }
    pos.p[i] = vec4(p, 0.0);
}
)GLSL";

/** @brief Viscosity + cohesion + adhesion + gravity + integration + SDF projection. */
inline const char* kFluidIntegrate = R"GLSL(
#version 450
layout(local_size_x = 64) in;
layout(set = 0, binding = 0) buffer Pos { vec4 p[]; } pos;
layout(set = 0, binding = 1) buffer Vel { vec4 v[]; } vel;
layout(set = 0, binding = 2) buffer CellHead { int h[]; } head;
layout(set = 0, binding = 3) buffer CellNext { int nx[]; } next;
layout(set = 0, binding = 5) buffer Sdf { float d[]; } sdf;
layout(push_constant) uniform PC { float d[32]; } pc;

float poly6(float r2, float h) {
    float h2 = h * h;
    if (r2 >= h2 || r2 <= 0.0) return 0.0;
    float q = h2 - r2;
    return 315.0 / (64.0 * 3.14159265358979 * pow(h, 9.0)) * q * q * q;
}

float cohesionKernel(float r, float h) {
    if (r >= h || r <= 1e-9) return 0.0;
    float q = h - r;
    return 32.0 / (3.14159265358979 * pow(h, 9.0)) * q * q * q * r * r * r;
}

float sdfValue(ivec3 res, int x, int y, int z) {
    int idx = x + res.x * (y + res.y * z);
    return sdf.d[idx];
}

float sdfSample(vec3 p) {
    ivec3 res = ivec3(int(pc.d[21]), int(pc.d[22]), int(pc.d[23]));
    vec3 o = vec3(pc.d[24], pc.d[25], pc.d[26]);
    float cs = pc.d[27];
    vec3 f = (p - o) / cs;
    vec3 clamped = clamp(f, vec3(0.0), vec3(res - 1));
    ivec3 i0 = ivec3(floor(clamped));
    ivec3 i1 = min(i0 + ivec3(1), res - ivec3(1));
    vec3 t = fract(clamped);
    float v000 = sdfValue(res, i0.x, i0.y, i0.z);
    float v100 = sdfValue(res, i1.x, i0.y, i0.z);
    float v010 = sdfValue(res, i0.x, i1.y, i0.z);
    float v110 = sdfValue(res, i1.x, i1.y, i0.z);
    float v001 = sdfValue(res, i0.x, i0.y, i1.z);
    float v101 = sdfValue(res, i1.x, i0.y, i1.z);
    float v011 = sdfValue(res, i0.x, i1.y, i1.z);
    float v111 = sdfValue(res, i1.x, i1.y, i1.z);
    float c00 = v000 + (v100 - v000) * t.x;
    float c10 = v010 + (v110 - v010) * t.x;
    float c01 = v001 + (v101 - v001) * t.x;
    float c11 = v011 + (v111 - v011) * t.x;
    float c0 = c00 + (c10 - c00) * t.y;
    float c1 = c01 + (c11 - c01) * t.y;
    return c0 + (c1 - c0) * t.z;
}

vec3 sdfGrad(vec3 p) {
    float e = pc.d[27];
    return vec3(sdfSample(p + vec3(e, 0.0, 0.0)) - sdfSample(p - vec3(e, 0.0, 0.0)),
                sdfSample(p + vec3(0.0, e, 0.0)) - sdfSample(p - vec3(0.0, e, 0.0)),
                sdfSample(p + vec3(0.0, 0.0, e)) - sdfSample(p - vec3(0.0, 0.0, e))) / (2.0 * e);
}

void main() {
    uint i = gl_GlobalInvocationID.x;
    uint n = uint(pc.d[0]);
    if (i >= n) return;
    float dt = pc.d[1];
    float h = pc.d[3];
    float h2 = h * h;
    vec3 p = pos.p[i].xyz;
    vec3 v = vel.v[i].xyz;

    float visc = pc.d[8];
    float coh = pc.d[10];
    if (visc > 0.0 || coh > 0.0) {
        vec3 viscAcc = vec3(0.0);
        vec3 cohAcc = vec3(0.0);
        vec3 origin = vec3(pc.d[17], pc.d[18], pc.d[19]);
        float cs = pc.d[20];
        ivec3 dim = ivec3(int(pc.d[14]), int(pc.d[15]), int(pc.d[16]));
        ivec3 c = ivec3(floor((p - origin) / cs));
        c = clamp(c, ivec3(0), dim - ivec3(1));
        for (int oz = -1; oz <= 1; ++oz) {
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    ivec3 cc = c + ivec3(ox, oy, oz);
                    if (any(lessThan(cc, ivec3(0))) || any(greaterThanEqual(cc, dim))) continue;
                    int cell = cc.x + dim.x * (cc.y + dim.y * cc.z);
                    for (int j = head.h[cell]; j >= 0; j = next.nx[j]) {
                        if (j == int(i)) continue;
                        vec3 dx = p - pos.p[j].xyz;
                        float r2 = dot(dx, dx);
                        if (r2 < h2) {
                            if (visc > 0.0) viscAcc += (vel.v[j].xyz - v) * poly6(r2, h);
                            if (coh > 0.0) {
                                float r = sqrt(r2);
                                cohAcc += -coh * cohesionKernel(r, h) * (dx / r);
                            }
                        }
                    }
                }
            }
        }
        v += viscAcc * (visc * dt);
        v += cohAcc * dt;
    }

    v += vec3(pc.d[5], pc.d[6], pc.d[7]) * dt;
    v *= max(0.0, 1.0 - pc.d[12] * dt);
    float sp = length(v);
    float vmax = pc.d[13];
    if (sp > vmax && sp > 1e-9) v *= vmax / sp;
    p += v * dt;

    float radius = pc.d[2];
    float d = sdfSample(p);
    vec3 nn = sdfGrad(p);
    float nl = length(nn);
    if (nl > 1e-6) nn /= nl;
    else nn = vec3(0.0, 1.0, 0.0);
    if (d < radius) {
        p = p - nn * (d - radius);
        float vn = dot(v, nn);
        if (vn < 0.0) v -= nn * vn;
        d = radius;
    }
    float adh = pc.d[11];
    if (adh > 0.0 && d < h) v += -nn * (adh * cohesionKernel(d, h) * dt);

    pos.p[i] = vec4(p, 0.0);
    vel.v[i] = vec4(v, 0.0);
}
)GLSL";

}  // namespace eve::fluids
