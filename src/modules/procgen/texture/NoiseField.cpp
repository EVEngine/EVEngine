#include "procgen/texture/NoiseField.h"

#include <algorithm>
#include <cmath>

namespace eve::procgen {
namespace {

float fade(float t) { return t * t * (3.f - 2.f * t); }
float fade5(float t) { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); }

int wrapIndex(int i, int period) {
    if (period <= 0) return i;
    int m = i % period;
    if (m < 0) m += period;
    return m;
}

}  // namespace

float NoiseField::hash01(int ix, int iy) const {
    ix = wrapIndex(ix, periodX);
    iy = wrapIndex(iy, periodY);
    uint32_t h = seed;
    h ^= uint32_t(ix) * 374761393u;
    h ^= uint32_t(iy) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return float(h & 0x00FFFFFFu) / float(0x00FFFFFFu);
}

float NoiseField::valueNoise(float x, float y) const {
    const int   x0 = int(std::floor(x));
    const int   y0 = int(std::floor(y));
    const float fx = fade(x - float(x0));
    const float fy = fade(y - float(y0));
    const float n00 = hash01(x0, y0);
    const float n10 = hash01(x0 + 1, y0);
    const float n01 = hash01(x0, y0 + 1);
    const float n11 = hash01(x0 + 1, y0 + 1);
    const float nx0 = n00 + (n10 - n00) * fx;
    const float nx1 = n01 + (n11 - n01) * fx;
    return nx0 + (nx1 - nx0) * fy;
}

float NoiseField::perlinNoise(float x, float y) const {
    const int   x0 = int(std::floor(x));
    const int   y0 = int(std::floor(y));
    const float dx = x - float(x0);
    const float dy = y - float(y0);
    const float fx = fade5(dx);
    const float fy = fade5(dy);

    auto grad = [&](int ix, int iy, float px, float py) -> float {
        // 8 compass gradients (Perlin 2002 style).
        static const float gx[8] = {1.f, -1.f, 1.f, -1.f, 1.f, -1.f, 0.f, 0.f};
        static const float gy[8] = {1.f, 1.f, -1.f, -1.f, 0.f, 0.f, 1.f, -1.f};
        const int h = int(hash01(ix, iy) * 7.999f) & 7;
        return gx[h] * (px - float(ix)) + gy[h] * (py - float(iy));
    };

    const float n00 = grad(x0, y0, x, y);
    const float n10 = grad(x0 + 1, y0, x, y);
    const float n01 = grad(x0, y0 + 1, x, y);
    const float n11 = grad(x0 + 1, y0 + 1, x, y);
    const float nx0 = n00 + (n10 - n00) * fx;
    const float nx1 = n01 + (n11 - n01) * fx;
    const float n   = nx0 + (nx1 - nx0) * fy;
    return std::clamp(n * 0.5f + 0.5f, 0.f, 1.f);
}

float NoiseField::fbm(float x, float y, int octaves, float lacunarity, float gain) const {
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

float NoiseField::fbmPerlin(float x, float y, int octaves, float lacunarity, float gain) const {
    octaves = std::max(1, octaves);
    float sum = 0.f, amp = 1.f, norm = 0.f, freq = 1.f;
    for (int i = 0; i < octaves; ++i) {
        // Offset each octave so they are uncorrelated (Red Blob Games).
        const float ox = float(i) * 17.8f;
        const float oy = float(i) * 23.5f;
        sum += perlinNoise(x * freq + ox, y * freq + oy) * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return norm > 0.f ? sum / norm : 0.f;
}

float NoiseField::ridged(float x, float y, int octaves, float lacunarity, float gain) const {
    octaves = std::max(1, octaves);
    float sum = 0.f, amp = 1.f, norm = 0.f, freq = 1.f;
    for (int i = 0; i < octaves; ++i) {
        float n = valueNoise(x * freq, y * freq);
        n       = 1.f - std::fabs(n * 2.f - 1.f);
        sum += n * n * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return norm > 0.f ? sum / norm : 0.f;
}

float NoiseField::ridgedPerlin(float x, float y, int octaves, float lacunarity, float gain) const {
    octaves = std::max(1, octaves);
    float sum = 0.f, amp = 1.f, norm = 0.f, freq = 1.f;
    for (int i = 0; i < octaves; ++i) {
        const float ox = float(i) * 11.3f;
        const float oy = float(i) * 19.7f;
        float n = perlinNoise(x * freq + ox, y * freq + oy);
        n       = 1.f - std::fabs(n * 2.f - 1.f);
        sum += n * n * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return norm > 0.f ? sum / norm : 0.f;
}

float NoiseField::warp(float x, float y, float amp, int octaves) const {
    const float wx = fbm(x + 19.1f, y + 7.3f, octaves) - 0.5f;
    const float wy = fbm(x + 5.2f, y + 31.7f, octaves) - 0.5f;
    return fbm(x + wx * amp, y + wy * amp, octaves);
}

}  // namespace eve::procgen
