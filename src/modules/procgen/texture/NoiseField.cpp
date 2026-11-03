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

void rotate2(float& x, float& y) {
    // Fixed rotation ≈ 0.5 rad (cos/sin) — same trick as noisecreater smoke fBm.
    constexpr float c  = 0.87758256189f;  // cos(0.5)
    constexpr float s  = 0.47942553860f;  // sin(0.5)
    const float     nx = c * x - s * y;
    const float     ny = s * x + c * y;
    x                  = nx;
    y                  = ny;
}

}  // namespace

float NoiseField::hash01(int ix, int iy) const {
    ix         = wrapIndex(ix, periodX);
    iy         = wrapIndex(iy, periodY);
    uint32_t h = seed;
    h ^= uint32_t(ix) * 374761393u;
    h ^= uint32_t(iy) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return float(h & 0x00FFFFFFu) / float(0x00FFFFFFu);
}

float NoiseField::whiteNoise(int ix, int iy) const { return hash01(ix, iy); }

float NoiseField::valueNoise(float x, float y) const {
    const int   x0  = int(std::floor(x));
    const int   y0  = int(std::floor(y));
    const float fx  = fade(x - float(x0));
    const float fy  = fade(y - float(y0));
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
        const int          h     = int(hash01(ix, iy) * 7.999f) & 7;
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

float NoiseField::simplexNoise(float x, float y) const {
    // Ashima Arts / noisecreater SampleNoiseA 2D simplex.
    constexpr float K1 = 0.36602540378f;  // (sqrt(3)-1)/2
    constexpr float K2 = 0.2113248654f;   // (3-sqrt(3))/6

    const float s  = (x + y) * K1;
    const int   i  = int(std::floor(x + s));
    const int   j  = int(std::floor(y + s));
    const float t  = float(i + j) * K2;
    const float x0 = x - (float(i) - t);
    const float y0 = y - (float(j) - t);

    const int i1 = x0 > y0 ? 1 : 0;
    const int j1 = x0 > y0 ? 0 : 1;

    const float x1 = x0 - float(i1) + K2;
    const float y1 = y0 - float(j1) + K2;
    const float x2 = x0 - 1.f + 2.f * K2;
    const float y2 = y0 - 1.f + 2.f * K2;

    auto gradDot = [&](int ix, int iy, float px, float py) -> float {
        static const float gx[8] = {1.f, -1.f, 1.f, -1.f, 1.f, -1.f, 0.f, 0.f};
        static const float gy[8] = {1.f, 1.f, -1.f, -1.f, 0.f, 0.f, 1.f, -1.f};
        const int          h     = int(hash01(ix, iy) * 7.999f) & 7;
        return gx[h] * px + gy[h] * py;
    };

    float n0 = 0.f, n1 = 0.f, n2 = 0.f;
    float t0 = 0.5f - x0 * x0 - y0 * y0;
    if (t0 > 0.f) {
        t0 *= t0;
        n0 = t0 * t0 * gradDot(i, j, x0, y0);
    }
    float t1 = 0.5f - x1 * x1 - y1 * y1;
    if (t1 > 0.f) {
        t1 *= t1;
        n1 = t1 * t1 * gradDot(i + i1, j + j1, x1, y1);
    }
    float t2 = 0.5f - x2 * x2 - y2 * y2;
    if (t2 > 0.f) {
        t2 *= t2;
        n2 = t2 * t2 * gradDot(i + 1, j + 1, x2, y2);
    }
    // 70 scales roughly to [-1, 1] for unit gradients.
    const float n = 70.f * (n0 + n1 + n2);
    return std::clamp(n * 0.5f + 0.5f, 0.f, 1.f);
}

float NoiseField::worleyF1(float x, float y, float jitter) const {
    jitter       = std::clamp(jitter, 0.f, 1.f);
    const int ix = int(std::floor(x));
    const int iy = int(std::floor(y));
    float     f1 = 1e9f;
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            const int   cx = ix + i;
            const int   cy = iy + j;
            const float px = float(cx) + 0.5f + (hash01(cx, cy) - 0.5f) * jitter;
            const float py = float(cy) + 0.5f + (hash01(cx * 7 + 3, cy * 13 + 5) - 0.5f) * jitter;
            const float dx = x - px;
            const float dy = y - py;
            f1             = std::min(f1, std::sqrt(dx * dx + dy * dy));
        }
    }
    return f1;
}

float NoiseField::worleyNoise(float x, float y, float jitter) const {
    return std::clamp(1.f - worleyF1(x, y, jitter), 0.f, 1.f);
}

float NoiseField::worleyEdge(float x, float y, float jitter) const {
    jitter       = std::clamp(jitter, 0.f, 1.f);
    const int ix = int(std::floor(x));
    const int iy = int(std::floor(y));
    float     f1 = 1e9f, f2 = 1e9f;
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            const int   cx = ix + i;
            const int   cy = iy + j;
            const float px = float(cx) + 0.5f + (hash01(cx, cy) - 0.5f) * jitter;
            const float py = float(cy) + 0.5f + (hash01(cx * 7 + 3, cy * 13 + 5) - 0.5f) * jitter;
            const float dx = x - px;
            const float dy = y - py;
            const float d  = std::sqrt(dx * dx + dy * dy);
            if (d < f1) {
                f2 = f1;
                f1 = d;
            } else if (d < f2) {
                f2 = d;
            }
        }
    }
    return std::max(0.f, f2 - f1);
}

float NoiseField::fbm(float x, float y, int octaves, float lacunarity, float gain) const {
    octaves   = std::max(1, octaves);
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
    octaves   = std::max(1, octaves);
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

float NoiseField::fbmSimplex(float x, float y, int octaves, float lacunarity, float gain) const {
    octaves   = std::max(1, octaves);
    float sum = 0.f, amp = 1.f, norm = 0.f, freq = 1.f;
    for (int i = 0; i < octaves; ++i) {
        const float ox = float(i) * 13.1f;
        const float oy = float(i) * 29.7f;
        sum += simplexNoise(x * freq + ox, y * freq + oy) * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return norm > 0.f ? sum / norm : 0.f;
}

float NoiseField::fbmRotated(float x, float y, int octaves, float lacunarity, float gain) const {
    octaves   = std::max(1, octaves);
    float sum = 0.f, amp = 1.f, norm = 0.f;
    float px = x, py = y;
    for (int i = 0; i < octaves; ++i) {
        sum += valueNoise(px, py) * amp;
        norm += amp;
        amp *= gain;
        px *= lacunarity;
        py *= lacunarity;
        rotate2(px, py);
        px += 100.f;  // break lattice correlation across octaves
        py += 100.f;
    }
    return norm > 0.f ? sum / norm : 0.f;
}

float NoiseField::ridged(float x, float y, int octaves, float lacunarity, float gain) const {
    octaves   = std::max(1, octaves);
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
    octaves   = std::max(1, octaves);
    float sum = 0.f, amp = 1.f, norm = 0.f, freq = 1.f;
    for (int i = 0; i < octaves; ++i) {
        const float ox = float(i) * 11.3f;
        const float oy = float(i) * 19.7f;
        float       n  = perlinNoise(x * freq + ox, y * freq + oy);
        n              = 1.f - std::fabs(n * 2.f - 1.f);
        sum += n * n * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return norm > 0.f ? sum / norm : 0.f;
}

float NoiseField::billow(float x, float y, int octaves, float lacunarity, float gain) const {
    octaves   = std::max(1, octaves);
    float sum = 0.f, amp = 1.f, norm = 0.f, freq = 1.f;
    for (int i = 0; i < octaves; ++i) {
        sum += std::fabs(valueNoise(x * freq, y * freq) * 2.f - 1.f) * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return norm > 0.f ? sum / norm : 0.f;
}

float NoiseField::billowPerlin(float x, float y, int octaves, float lacunarity, float gain) const {
    octaves   = std::max(1, octaves);
    float sum = 0.f, amp = 1.f, norm = 0.f, freq = 1.f;
    for (int i = 0; i < octaves; ++i) {
        const float ox = float(i) * 15.4f;
        const float oy = float(i) * 27.1f;
        sum += std::fabs(perlinNoise(x * freq + ox, y * freq + oy) * 2.f - 1.f) * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return norm > 0.f ? sum / norm : 0.f;
}

float NoiseField::perlinWorley(float x, float y, int octaves, float jitter) const {
    const float w = worleyNoise(x, y, jitter);
    const float p = fbmPerlin(x, y, std::max(1, octaves));
    return std::clamp(w * p, 0.f, 1.f);
}

float NoiseField::warp(float x, float y, float amp, int octaves) const {
    const float wx = fbm(x + 19.1f, y + 7.3f, octaves) - 0.5f;
    const float wy = fbm(x + 5.2f, y + 31.7f, octaves) - 0.5f;
    return fbm(x + wx * amp, y + wy * amp, octaves);
}

float NoiseField::warpDeep(float x, float y, float amp, int octaves) const {
    // iq nested domain warp: pattern = fbm(p + amp * r(p + amp * q(p))).
    const float qx = fbm(x, y, octaves);
    const float qy = fbm(x + 5.2f, y + 1.3f, octaves);
    const float rx = fbm(x + amp * qx + 1.7f, y + amp * qy + 9.2f, octaves);
    const float ry = fbm(x + amp * qx + 8.3f, y + amp * qy + 2.8f, octaves);
    return fbm(x + amp * rx, y + amp * ry, octaves);
}

}  // namespace eve::procgen
