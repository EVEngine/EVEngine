#pragma once

namespace eve::particles {

/**
 * @brief GLSL compute kernel for GPU-accelerated particle integration.
 *
 * Particle layout: 16 floats per particle.
 *   [0] x, [1] y, [2] vx, [3] vy,
 *   [4] life, [5] lifetime, [6] size, [7] rot,
 *   [8] spin, [9] frame, [10] radial, [11] tangential,
 *   [12] ax0, [13] ay0, [14] noisePhase, [15] pad
 *
 * Push constants (float[32]):
 *   [0] aliveCount, [1] dt, [2] emitterX, [3] emitterY,
 *   [4] gravityX, [5] gravityY, [6] damping, [7] limitVelocity,
 *   [8] noiseStrength, [9] noiseFrequency, [10] noiseSpeed, [11] time,
 *   [12] frameRate
 *
 * Compile with glslc (Vulkan SDK) — the engine also compiles this at runtime
 * via Gpgpu::newShader when the first GPU emitter initializes.
 */
inline const char *kParticleGpuKernel = R"GLSL(
#version 450
layout(local_size_x = 64) in;

layout(set = 0, binding = 0) buffer Particles {
    float data[];
} p;

layout(push_constant) uniform PC {
    float d[32];
} pc;

float hashNoise(int ix, int iy) {
    uint h = uint(ix) * 374761393u + uint(iy) * 668265263u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    return (float((h ^ (h >> 16u)) & 0xFFFFFFu) / float(0x1000000)) * 2.0 - 1.0;
}

float valueNoise(float x, float y) {
    int ix = int(floor(x));
    int iy = int(floor(y));
    float fx = x - float(ix);
    float fy = y - float(iy);
    float sx = fx * fx * (3.0 - 2.0 * fx);
    float sy = fy * fy * (3.0 - 2.0 * fy);
    float n00 = hashNoise(ix, iy);
    float n10 = hashNoise(ix + 1, iy);
    float n01 = hashNoise(ix, iy + 1);
    float n11 = hashNoise(ix + 1, iy + 1);
    float a = mix(n00, n10, sx);
    float b = mix(n01, n11, sx);
    return mix(a, b, sy);
}

void main() {
    uint i = gl_GlobalInvocationID.x;
    uint alive = uint(pc.d[0]);
    if (i >= alive) return;

    float dt = pc.d[1];
    float ex = pc.d[2];
    float ey = pc.d[3];
    float gx = pc.d[4];
    float gy = pc.d[5];
    float damp = pc.d[6];
    float limit = pc.d[7];
    float nstr = pc.d[8];
    float nfreq = pc.d[9];
    float nspeed = pc.d[10];
    float time = pc.d[11];
    float frameRate = pc.d[12];

    uint b = i * 16u;
    float x = p.data[b + 0u];
    float y = p.data[b + 1u];
    float vx = p.data[b + 2u];
    float vy = p.data[b + 3u];
    float life = p.data[b + 4u];
    float rot = p.data[b + 7u];
    float spin = p.data[b + 8u];
    float frame = p.data[b + 9u];
    float radial = p.data[b + 10u];
    float tangential = p.data[b + 11u];
    float ax0 = p.data[b + 12u];
    float ay0 = p.data[b + 13u];
    float nphase = p.data[b + 14u];

    life -= dt;
    if (life <= 0.0) {
        p.data[b + 4u] = life;
        return;
    }

    float ax = ax0 + gx;
    float ay = ay0 + gy;

    // Radial / tangential acceleration relative to the emitter.
    float dx = x - ex;
    float dy = y - ey;
    float len2 = dx * dx + dy * dy;
    if (len2 > 1e-6) {
        float inv = inversesqrt(len2);
        ax += dx * inv * radial - dy * inv * tangential;
        ay += dy * inv * radial + dx * inv * tangential;
    }

    // Turbulence noise.
    if (nstr != 0.0) {
        float t = time * nspeed;
        ax += valueNoise(x * nfreq + t, y * nfreq + nphase) * nstr;
        ay += valueNoise(y * nfreq + t, x * nfreq - nphase) * nstr;
    }

    vx += ax * dt;
    vy += ay * dt;

    // Damping.
    if (damp > 0.0) {
        float f = max(0.0, 1.0 - damp * dt);
        vx *= f;
        vy *= f;
    }
    // Limit velocity.
    if (limit > 0.0) {
        float sp2 = vx * vx + vy * vy;
        float m2 = limit * limit;
        if (sp2 > m2) {
            float s = sqrt(m2 / sp2);
            vx *= s;
            vy *= s;
        }
    }

    x += vx * dt;
    y += vy * dt;
    rot += spin * dt;
    frame += frameRate * dt;

    p.data[b + 0u] = x;
    p.data[b + 1u] = y;
    p.data[b + 2u] = vx;
    p.data[b + 3u] = vy;
    p.data[b + 4u] = life;
    p.data[b + 7u] = rot;
    p.data[b + 9u] = frame;
}
)GLSL";

}  // namespace eve::particles
