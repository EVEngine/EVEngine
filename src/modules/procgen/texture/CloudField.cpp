#include "procgen/texture/CloudField.h"

#include <algorithm>
#include <cmath>

namespace eve::procgen {
namespace {

inline float smoothstepClamp(float e0, float e1, float x) {
    if (e0 == e1) return x < e0 ? 0.f : 1.f;
    const float t = std::clamp((x - e0) / (e1 - e0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

inline float fade(float t) { return t * t * (3.f - 2.f * t); }

struct Lat {
    int periodX = CloudField::kLattice;
    int periodY = CloudField::kLattice;
    uint32_t seed = 1;

    int wrap(int i, int period) const {
        if (period <= 0) return i;
        int m = i % period;
        return m < 0 ? m + period : m;
    }

    float hash01(int ix, int iy) const {
        ix = wrap(ix, periodX);
        iy = wrap(iy, periodY);
        uint32_t h = seed;
        h ^= uint32_t(ix) * 374761393u;
        h ^= uint32_t(iy) * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        h ^= h >> 16;
        return float(h & 0x00FFFFFFu) / float(0x00FFFFFFu);
    }

    float valueNoise(float x, float y) const {
        const int   x0 = int(std::floor(x));
        const int   y0 = int(std::floor(y));
        const float fx = fade(x - float(x0));
        const float fy = fade(y - float(y0));
        const float n00 = hash01(x0, y0);
        const float n10 = hash01(x0 + 1, y0);
        const float n01 = hash01(x0, y0 + 1);
        const float n11 = hash01(x0 + 1, y0 + 1);
        return n00 + (n10 - n00) * fx + (n01 - n00) * fy + (n00 - n10 - n01 + n11) * fx * fy;
    }

    float fbm(float x, float y, int octaves, float lacunarity, float gain) const {
        octaves = std::max(1, octaves);
        float sum = 0.f, amp = 1.f, norm = 0.f, freq = 1.f;
        for (int i = 0; i < octaves; ++i) {
            sum += valueNoise(x * freq, y * freq) * amp;
            norm += amp;
            amp *= gain;
            freq *= lacunarity;
        }
        return norm > 0.f ? sum / norm : 0.f;
    }
};

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

    Lat lat;
    lat.seed = params_.seed;
    if (!params_.seamless) {
        lat.periodX = 0;
        lat.periodY = 0;
    }

    // Domain warp → organic, billowy shapes.
    const float w = lat.fbm(px + 19.1f, pz + 7.3f, params_.octaves, params_.lacunarity,
                            params_.gain) - 0.5f;
    const float wx = px + w * params_.warp * 0.5f;
    const float wz = pz + (lat.fbm(px + 5.2f, pz + 31.7f, params_.octaves, params_.lacunarity,
                                   params_.gain) -
                           0.5f) *
                              params_.warp * 0.5f;

    float base = lat.fbm(wx, wz, params_.octaves, params_.lacunarity, params_.gain);
    // Threshold into puffs with soft edges.
    float c = smoothstepClamp(params_.coverage - params_.softness,
                              params_.coverage + params_.softness, base);

    // High-frequency wisps erode dense regions for a textured top.
    if (params_.detail > 0.f) {
        const float d = lat.fbm(wx * 3.f + 11.7f, wz * 3.f + 5.3f, 2, params_.lacunarity,
                                params_.gain);
        c = c * (1.f - params_.detail) + (c * d) * params_.detail;
    }
    return std::clamp(c, 0.f, 1.f);
}

void CloudField::sample(float *out, int width, int height, float time, float x0, float z0,
                        float extent) const {
    if (!out || width <= 0 || height <= 0) return;
    for (int y = 0; y < height; ++y) {
        const float z = z0 + extent * (float(y) / float(height - 1));
        for (int x = 0; x < width; ++x) {
            const float px = x0 + extent * (float(x) / float(width - 1));
            out[size_t(y * width + x)] = coverageAt(px, z, time);
        }
    }
}

}  // namespace eve::procgen
