#pragma once

/**
 * @brief GLSL compute kernels for the GPU shallow-water (Saint-Venant) solver.
 *
 * Conserved state per cell is packed as four floats: h, hu, hv, pad. Bed height
 * lives in a separate float buffer. Collider impulse sources are packed as
 * four floats: dh, dhu, dhv, pad — applied before each integrate pass.
 *
 * Push-constant layout (float[32]):
 *   [0]  resX
 *   [1]  resZ
 *   [2]  cellSize
 *   [3]  dt
 *   [4]  gravity
 *   [5]  viscosity
 *   [6]  friction
 *   [7]  minDepth
 *   [8]  maxDepth
 *   [9]  colliderCount
 *   [10] originX
 *   [11] originY
 *   [12] originZ
 *   [13] mode (0=integrate, 1=applySources, 2=clearSources)
 *
 * Buffer bindings:
 *   0 stateSrc  1 stateDst  2 bed  3 sources  4 colliders (16 floats each)
 */

namespace eve::fluids {

/**
 * @brief Zero the per-cell impulse source buffer.
 * @lifetime The returned pointer remains valid for the process lifetime.
 * @return Process-lifetime static read-only GLSL source; the caller must not free it.
 */
inline const char* kShallowWaterClearSources = R"GLSL(
#version 450
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 3) buffer Sources { vec4 s[]; } sources;
layout(push_constant) uniform PC { float d[32]; } pc;
void main() {
    uint x = gl_GlobalInvocationID.x;
    uint z = gl_GlobalInvocationID.y;
    uint resX = uint(pc.d[0]);
    uint resZ = uint(pc.d[1]);
    if (x >= resX || z >= resZ) return;
    sources.s[z * resX + x] = vec4(0.0);
}
)GLSL";

/**
 * @brief Rasterize sphere/capsule/box colliders into height/momentum sources.
 * @lifetime The returned pointer remains valid for the process lifetime.
 * @return Process-lifetime static read-only GLSL source; the caller must not free it.
 */
inline const char* kShallowWaterRasterizeColliders = R"GLSL(
#version 450
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) readonly buffer StateSrc { vec4 q[]; } stateSrc;
layout(set = 0, binding = 2) readonly buffer Bed { float b[]; } bed;
layout(set = 0, binding = 3) buffer Sources { vec4 s[]; } sources;
layout(set = 0, binding = 4) readonly buffer Colliders { float c[]; } colliders;
layout(push_constant) uniform PC { float d[32]; } pc;

float capsuleDistanceXZ(vec2 p, vec3 center, float halfHeight, float radius) {
    // Vertical capsule: segment along local Y, footprint is a disk of `radius`
    // around the capsule axis projected onto XZ.
    vec2 d = p - center.xz;
    return length(d) - radius;
}

void main() {
    uint x = gl_GlobalInvocationID.x;
    uint z = gl_GlobalInvocationID.y;
    uint resX = uint(pc.d[0]);
    uint resZ = uint(pc.d[1]);
    if (x >= resX || z >= resZ) return;

    float cell = pc.d[2];
    float dt = max(pc.d[3], 0.0);
    vec3 origin = vec3(pc.d[10], pc.d[11], pc.d[12]);
    uint idx = z * resX + x;
    vec2 world = origin.xz + vec2((float(x) + 0.5) * cell, (float(z) + 0.5) * cell);
    float surfaceY = bed.b[idx] + stateSrc.q[idx].x;
    vec4 acc = sources.s[idx];
    int count = int(pc.d[9]);

    for (int i = 0; i < count; ++i) {
        int base = i * 16;
        vec3 center = vec3(colliders.c[base + 0], colliders.c[base + 1], colliders.c[base + 2]);
        float radius = colliders.c[base + 3];
        vec3 halfExtent = vec3(colliders.c[base + 4], colliders.c[base + 5], colliders.c[base + 6]);
        int shape = int(colliders.c[base + 7] + 0.5);
        vec3 velocity = vec3(colliders.c[base + 8], colliders.c[base + 9], colliders.c[base + 10]);
        float displacementScale = colliders.c[base + 11];
        float dragScale = colliders.c[base + 12];

        float dist = 1e9;
        float immersion = 0.0;
        if (shape == 0) {
            dist = length(world - center.xz) - radius;
            immersion = clamp(center.y + radius - surfaceY, 0.0, 2.0 * radius);
        } else if (shape == 1) {
            dist = capsuleDistanceXZ(world, center, halfExtent.y, radius);
            float top = center.y + halfExtent.y + radius;
            float bottom = center.y - halfExtent.y - radius;
            immersion = clamp(min(top, surfaceY + 2.0 * radius) - max(bottom, surfaceY - 2.0 * radius), 0.0,
                              top - bottom);
            immersion = max(0.0, min(immersion, top - surfaceY + radius));
        } else {
            vec2 d = abs(world - center.xz) - halfExtent.xz;
            dist = length(max(d, vec2(0.0))) + min(max(d.x, d.y), 0.0);
            immersion = clamp(center.y + halfExtent.y - surfaceY, 0.0, 2.0 * halfExtent.y);
        }

        if (dist >= cell * 0.75 || immersion <= 1e-4) continue;

        float cover = clamp(1.0 - dist / max(cell * 0.75, 1e-4), 0.0, 1.0);
        // displacementScale / dragScale are rates; multiply by substep dt so
        // CFL substepping cannot stack absolute impulses into height spikes.
        float push = immersion * cover * displacementScale * dt;
        vec2 outward = world - center.xz;
        float olen = length(outward);
        vec2 dir = olen > 1e-5 ? outward / olen : vec2(0.0, 1.0);
        float radial = push * 1.0;
        float h = max(stateSrc.q[idx].x, pc.d[7]);
        acc.x -= push * 0.35;
        acc.y += (dir.x * radial + velocity.x * dragScale * dt) * h;
        acc.z += (dir.y * radial + velocity.z * dragScale * dt) * h;
        // Vertical motion injects a local swell that neighbors redistribute via fluxes.
        acc.x += max(0.0, -velocity.y) * cover * 0.15 * displacementScale * dt;
    }
    // Keep one substep's source within a fraction of the hard depth clamp.
    float maxSrcH = pc.d[8] * 0.15;
    acc.x = clamp(acc.x, -maxSrcH, maxSrcH);
    sources.s[idx] = acc;
}
)GLSL";

/**
 * @brief Apply source impulses then advance one Lax-Friedrichs shallow-water step.
 * @lifetime The returned pointer remains valid for the process lifetime.
 * @return Process-lifetime static read-only GLSL source; the caller must not free it.
 */
inline const char* kShallowWaterIntegrate = R"GLSL(
#version 450
layout(local_size_x = 8, local_size_y = 8) in;
layout(set = 0, binding = 0) readonly buffer StateSrc { vec4 q[]; } stateSrc;
layout(set = 0, binding = 1) writeonly buffer StateDst { vec4 q[]; } stateDst;
layout(set = 0, binding = 2) readonly buffer Bed { float b[]; } bed;
layout(set = 0, binding = 3) readonly buffer Sources { vec4 s[]; } sources;
layout(push_constant) uniform PC { float d[32]; } pc;

vec3 fluxX(vec3 q, float g, float minH) {
    float h = q.x;
    if (h <= minH) return vec3(0.0);
    float u = q.y / h;
    float v = q.z / h;
    return vec3(q.y, q.y * u + 0.5 * g * h * h, q.y * v);
}

vec3 fluxZ(vec3 q, float g, float minH) {
    float h = q.x;
    if (h <= minH) return vec3(0.0);
    float u = q.y / h;
    float v = q.z / h;
    return vec3(q.z, q.z * u, q.z * v + 0.5 * g * h * h);
}

vec3 sampleQ(int x, int z, int resX, int resZ) {
    x = clamp(x, 0, resX - 1);
    z = clamp(z, 0, resZ - 1);
    vec4 q = stateSrc.q[z * resX + x];
    vec4 src = sources.s[z * resX + x];
    return vec3(max(q.x + src.x, 0.0), q.y + src.y, q.z + src.z);
}

float sampleBed(int x, int z, int resX, int resZ) {
    x = clamp(x, 0, resX - 1);
    z = clamp(z, 0, resZ - 1);
    return bed.b[z * resX + x];
}

void main() {
    int x = int(gl_GlobalInvocationID.x);
    int z = int(gl_GlobalInvocationID.y);
    int resX = int(pc.d[0]);
    int resZ = int(pc.d[1]);
    if (x >= resX || z >= resZ) return;

    float cell = pc.d[2];
    float dt = pc.d[3];
    float g = pc.d[4];
    float viscosity = pc.d[5];
    float friction = pc.d[6];
    float minH = pc.d[7];
    float maxH = pc.d[8];

    vec3 q = sampleQ(x, z, resX, resZ);
    vec3 qL = sampleQ(x - 1, z, resX, resZ);
    vec3 qR = sampleQ(x + 1, z, resX, resZ);
    vec3 qD = sampleQ(x, z - 1, resX, resZ);
    vec3 qU = sampleQ(x, z + 1, resX, resZ);

    float hSafe = max(q.x, minH);
    float u = q.y / hSafe;
    float v = q.z / hSafe;
    float alpha = abs(u) + abs(v) + sqrt(g * hSafe) + 1.0;
    vec3 fR = 0.5 * (fluxX(q, g, minH) + fluxX(qR, g, minH)) - 0.5 * alpha * (qR - q);
    vec3 fL = 0.5 * (fluxX(qL, g, minH) + fluxX(q, g, minH)) - 0.5 * alpha * (q - qL);
    vec3 fluxUp = 0.5 * (fluxZ(q, g, minH) + fluxZ(qU, g, minH)) - 0.5 * alpha * (qU - q);
    vec3 fluxDown = 0.5 * (fluxZ(qD, g, minH) + fluxZ(q, g, minH)) - 0.5 * alpha * (q - qD);

    float bL = sampleBed(x - 1, z, resX, resZ);
    float bR = sampleBed(x + 1, z, resX, resZ);
    float bD = sampleBed(x, z - 1, resX, resZ);
    float bU = sampleBed(x, z + 1, resX, resZ);
    vec3 source = vec3(0.0, -g * hSafe * (bR - bL) / (2.0 * cell), -g * hSafe * (bU - bD) / (2.0 * cell));

    vec3 qn = q - (dt / cell) * (fR - fL + fluxUp - fluxDown) + dt * source;
    qn.y += viscosity * dt * (qL.y + qR.y + qD.y + qU.y - 4.0 * q.y);
    qn.z += viscosity * dt * (qL.z + qR.z + qD.z + qU.z - 4.0 * q.z);
    qn.y *= max(0.0, 1.0 - friction * dt);
    qn.z *= max(0.0, 1.0 - friction * dt);

    qn.x = clamp(qn.x, 0.0, maxH);
    if (qn.x <= minH) {
        qn.y = 0.0;
        qn.z = 0.0;
    } else {
        float speed = length(vec2(qn.y, qn.z)) / max(qn.x, minH);
        if (speed > 8.0) {
            float s = 8.0 / speed;
            qn.y *= s;
            qn.z *= s;
        }
    }
    // Reflecting walls: kill normal momentum on domain edges.
    if (x == 0 || x == resX - 1) qn.y = 0.0;
    if (z == 0 || z == resZ - 1) qn.z = 0.0;

    stateDst.q[z * resX + x] = vec4(qn, 0.0);
}
)GLSL";

}  // namespace eve::fluids
