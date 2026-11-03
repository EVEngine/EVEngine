#include "procgen/texture/CloudField.h"
#include <algorithm>
#include <cmath>
#include "procgen/texture/CloudShadow.h"
#include "procgen/texture/NoiseField.h"

// CloudField.cpp
namespace eve::procgen {
namespace {

inline float smoothstepClamp(float e0, float e1, float x) {
    if (e0 == e1) return x < e0 ? 0.f : 1.f;
    const float t = std::clamp((x - e0) / (e1 - e0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

}  // namespace

void CloudField::setParams(const Params &params) { params_ = params; }

void CloudField::setSeed(uint32_t seed) { params_.seed = seed; }
void CloudField::setWorldScale(float v) { params_.worldScale = std::max(1.f, v); }
void CloudField::setCoverage(float v) { params_.coverage = std::clamp(v, 0.f, 1.f); }
void CloudField::setSoftness(float v) { params_.softness = std::clamp(v, 0.f, 1.f); }
void CloudField::setDetail(float v) { params_.detail = std::clamp(v, 0.f, 1.f); }
void CloudField::setWind(float speed, float angleRad) {
    params_.windSpeed = std::max(0.f, speed);
    params_.windAngle = angleRad;
}
void CloudField::setOctaves(int v) { params_.octaves = std::max(1, v); }
void CloudField::setWarp(float v) { params_.warp = std::max(0.f, v); }
void CloudField::setSeamless(bool v) { params_.seamless = v; }

void CloudField::windVelocity(float &vx, float &vz) const {
    vx = std::cos(params_.windAngle) * params_.windSpeed;
    vz = std::sin(params_.windAngle) * params_.windSpeed;
}

float CloudField::coverageAt(float x, float z, float time) const {
    float vx = 0.f, vz = 0.f;
    windVelocity(vx, vz);

    const float cell = params_.worldScale / float(kLattice);
    // Drift with the wind; sample in lattice units (seamless wrap at kLattice).
    const float px = (x - vx * time) / cell;
    const float pz = (z - vz * time) / cell;

    NoiseField noise;
    noise.seed = params_.seed;
    if (params_.seamless) {
        noise.periodX = kLattice;
        noise.periodY = kLattice;
    }

    // iq / noisecreater nested domain warp on sample coordinates, then
    // Perlin–Worley coverage (NoiseCreator Cloud / Perlin-Worley hybrid).
    const int   warpOct = std::max(2, params_.octaves - 1);
    const float amp     = params_.warp;
    const float qx      = noise.fbm(px, pz, warpOct, params_.lacunarity, params_.gain) - 0.5f;
    const float qz      = noise.fbm(px + 5.2f, pz + 1.3f, warpOct, params_.lacunarity, params_.gain) - 0.5f;
    const float rx =
        noise.fbm(px + amp * qx + 1.7f, pz + amp * qz + 9.2f, warpOct, params_.lacunarity, params_.gain) - 0.5f;
    const float rz =
        noise.fbm(px + amp * qx + 8.3f, pz + amp * qz + 2.8f, warpOct, params_.lacunarity, params_.gain) - 0.5f;
    const float sx = px + amp * rx;
    const float sz = pz + amp * rz;

    const float base = noise.perlinWorley(sx, sz, params_.octaves);
    float       c    = smoothstepClamp(params_.coverage - params_.softness, params_.coverage + params_.softness, base);

    // High-frequency wisps erode dense regions for a textured top.
    if (params_.detail > 0.f) {
        const float d = noise.fbmRotated(sx * 3.f + 11.7f, sz * 3.f + 5.3f, 2, params_.lacunarity, params_.gain);
        c             = c * (1.f - params_.detail) + (c * d) * params_.detail;
    }
    return std::clamp(c, 0.f, 1.f);
}

void CloudField::sample(float *out, int width, int height, float time, float x0, float z0, float extent) const {
    if (!out || width <= 0 || height <= 0) return;
    for (int y = 0; y < height; ++y) {
        const float z = z0 + extent * (float(y) / float(std::max(1, height - 1)));
        for (int x = 0; x < width; ++x) {
            const float px             = x0 + extent * (float(x) / float(std::max(1, width - 1)));
            out[size_t(y * width + x)] = coverageAt(px, z, time);
        }
    }
}

}  // namespace eve::procgen

// CloudShadow.cpp
namespace eve::procgen {

void CloudShadow::setParams(const Params &params) { params_ = params; }

void CloudShadow::setSunDirection(float x, float y, float z) {
    const float len = std::sqrt(x * x + y * y + z * z);
    if (len < 1e-6f) return;
    params_.sunDirX = x / len;
    params_.sunDirY = y / len;
    params_.sunDirZ = z / len;
}
void CloudShadow::setCloudAltitude(float v) { params_.cloudAltitude = std::max(0.f, v); }
void CloudShadow::setStrength(float v) { params_.strength = std::clamp(v, 0.f, 1.f); }

void CloudShadow::cloudOffset(float &ox, float &oz) const {
    const float ly = std::max(params_.sunDirY, 1e-4f);
    const float s  = params_.cloudAltitude / ly;
    ox             = params_.sunDirX * s;
    oz             = params_.sunDirZ * s;
}

float CloudShadow::coverageAt(float x, float z, float time) const {
    if (params_.sunDirY <= 0.f) return 0.f;  // sun below horizon → no cloud shadows
    float ox = 0.f, oz = 0.f;
    cloudOffset(ox, oz);
    return params_.field.coverageAt(x + ox, z + oz, time);
}

float CloudShadow::shadowFactorAt(float x, float z, float time) const {
    const float c = coverageAt(x, z, time);
    return 1.f - c * std::clamp(params_.strength, 0.f, 1.f);
}

void CloudShadow::sampleCoverage(float *out, int width, int height, float time, float x0, float z0,
                                 float extent) const {
    if (!out || width <= 0 || height <= 0) return;
    for (int y = 0; y < height; ++y) {
        const float z = z0 + extent * (float(y) / float(std::max(1, height - 1)));
        for (int x = 0; x < width; ++x) {
            const float px             = x0 + extent * (float(x) / float(std::max(1, width - 1)));
            out[size_t(y * width + x)] = coverageAt(px, z, time);
        }
    }
}

void CloudShadow::sampleFactor(float *out, int width, int height, float time, float x0, float z0, float extent) const {
    if (!out || width <= 0 || height <= 0) return;
    for (int y = 0; y < height; ++y) {
        const float z = z0 + extent * (float(y) / float(std::max(1, height - 1)));
        for (int x = 0; x < width; ++x) {
            const float px             = x0 + extent * (float(x) / float(std::max(1, width - 1)));
            out[size_t(y * width + x)] = shadowFactorAt(px, z, time);
        }
    }
}

}  // namespace eve::procgen
