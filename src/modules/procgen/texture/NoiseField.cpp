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

float metricDist(float dx, float dy, WorleyMetric metric) {
    switch (metric) {
        case WorleyMetric::Manhattan: return std::fabs(dx) + std::fabs(dy);
        case WorleyMetric::Chebyshev: return std::max(std::fabs(dx), std::fabs(dy));
        case WorleyMetric::Euclidean:
        default: return std::sqrt(dx * dx + dy * dy);
    }
}

float metricDist3(float dx, float dy, float dz, WorleyMetric metric) {
    switch (metric) {
        case WorleyMetric::Manhattan: return std::fabs(dx) + std::fabs(dy) + std::fabs(dz);
        case WorleyMetric::Chebyshev: return std::max(std::fabs(dx), std::max(std::fabs(dy), std::fabs(dz)));
        case WorleyMetric::Euclidean:
        default: return std::sqrt(dx * dx + dy * dy + dz * dz);
    }
}

void worleySorted2(const NoiseField& field, float x, float y, float jitter, WorleyMetric metric, float& f1, float& f2) {
    jitter       = std::clamp(jitter, 0.f, 1.f);
    const int ix = int(std::floor(x));
    const int iy = int(std::floor(y));
    f1           = 1e9f;
    f2           = 1e9f;
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            const int   cx = ix + i;
            const int   cy = iy + j;
            const float px = float(cx) + 0.5f + (field.hash01(cx, cy) - 0.5f) * jitter;
            const float py = float(cy) + 0.5f + (field.hash01(cx * 7 + 3, cy * 13 + 5) - 0.5f) * jitter;
            const float d  = metricDist(x - px, y - py, metric);
            if (d < f1) {
                f2 = f1;
                f1 = d;
            } else if (d < f2) {
                f2 = d;
            }
        }
    }
}

constexpr float kHexFactor = 0.86602540378f;  // sqrt(3)/2

float positiveMod(float v, float m) {
    float r = std::fmod(v, m);
    return r < 0.f ? r + m : r;
}

void nearestHexCell(float x, float y, float& cellX, float& cellY) {
    // noisecreater HexNoiseFragment nearestHexCell.
    const float gposX = x / kHexFactor;
    const float gposY = y;
    float       hx    = std::floor(gposX);
    float       hy    = std::floor(gposY);
    const float sy    = (positiveMod(hx + 1.f, 4.f) >= 2.f) ? 1.f : 0.f;
    hx += positiveMod(hx, 2.f);
    hy += positiveMod(hy + sy, 2.f);
    const float gdx = gposX - hx;
    const float gdy = gposY - hy;
    if (std::fabs(gdx) * (kHexFactor * kHexFactor) + std::fabs(gdy) * 0.5f > 1.f) {
        hx += (gdx >= 0.f ? 1.f : -1.f) * 2.f;
        hy += (gdy >= 0.f ? 1.f : -1.f);
    }
    cellX = hx;
    cellY = hy;
}

float hexDistLocal(float px, float py) {
    px = std::fabs(px);
    py = std::fabs(py);
    return std::max(px * kHexFactor + py * 0.5f, py) - 1.f;
}

float gyroidRaw(float x, float y, float z) {
    return std::sin(x) * std::cos(y) + std::sin(y) * std::cos(z) + std::sin(z) * std::cos(x);
}

float remapSigned01(float n) { return std::clamp(n * 0.5f + 0.5f, 0.f, 1.f); }

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
    constexpr float K1 = 0.36602540378f;
    constexpr float K2 = 0.2113248654f;

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
    return std::clamp(70.f * (n0 + n1 + n2) * 0.5f + 0.5f, 0.f, 1.f);
}

float NoiseField::worleyF1(float x, float y, float jitter, WorleyMetric metric) const {
    float f1 = 0.f, f2 = 0.f;
    worleySorted2(*this, x, y, jitter, metric, f1, f2);
    return f1;
}

float NoiseField::worleyF2(float x, float y, float jitter, WorleyMetric metric) const {
    float f1 = 0.f, f2 = 0.f;
    worleySorted2(*this, x, y, jitter, metric, f1, f2);
    return f2;
}

float NoiseField::worleyNoise(float x, float y, float jitter, WorleyMetric metric) const {
    return std::clamp(1.f - worleyF1(x, y, jitter, metric), 0.f, 1.f);
}

float NoiseField::worleyEdge(float x, float y, float jitter, WorleyMetric metric) const {
    float f1 = 0.f, f2 = 0.f;
    worleySorted2(*this, x, y, jitter, metric, f1, f2);
    return std::max(0.f, f2 - f1);
}

float NoiseField::worleyCombine(float x, float y, float jitter, WorleyMetric metric) const {
    float f1 = 0.f, f2 = 0.f;
    worleySorted2(*this, x, y, jitter, metric, f1, f2);
    return std::clamp(1.f - 0.5f * (f1 + f2), 0.f, 1.f);
}

float NoiseField::worleySmooth(float x, float y, float smoothness, float jitter) const {
    smoothness   = std::max(1e-4f, smoothness);
    jitter       = std::clamp(jitter, 0.f, 1.f);
    const int ix = int(std::floor(x));
    const int iy = int(std::floor(y));
    // Soft-min of squared Euclidean distances → metaball look.
    float acc = 0.f;
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            const int   cx = ix + i;
            const int   cy = iy + j;
            const float px = float(cx) + 0.5f + (hash01(cx, cy) - 0.5f) * jitter;
            const float py = float(cy) + 0.5f + (hash01(cx * 7 + 3, cy * 13 + 5) - 0.5f) * jitter;
            const float dx = x - px;
            const float dy = y - py;
            const float d2 = dx * dx + dy * dy;
            acc += std::exp(-d2 / (smoothness * smoothness));
        }
    }
    return std::clamp(1.f - std::exp(-acc), 0.f, 1.f);
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
        px += 100.f;
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

float NoiseField::swiss(float x, float y, int octaves, float lacunarity, float gain, float warpStrength) const {
    octaves   = std::max(1, octaves);
    float sum = 0.f, amp = 1.f, norm = 0.f, freq = 1.f;
    float px = x, py = y;
    for (int i = 0; i < octaves; ++i) {
        const float n = valueNoise(px, py) * 2.f - 1.f;
        sum += n * amp;
        norm += amp;
        // Warp subsequent octaves by the running signed sum (Swiss / eroded look).
        freq *= lacunarity;
        px = x * freq + sum * warpStrength;
        py = y * freq + sum * warpStrength;
        amp *= gain;
    }
    return norm > 0.f ? remapSigned01(sum / norm) : 0.5f;
}

float NoiseField::hybrid(float x, float y, int octaves, float lacunarity, float gain, float offset) const {
    octaves      = std::max(1, octaves);
    float freq   = 1.f;
    float signal = (valueNoise(x, y) + offset) * gain;
    float result = signal;
    float weight = std::clamp(signal, 0.f, 1.f);
    float amp    = gain;
    float norm   = 1.f;
    for (int i = 1; i < octaves; ++i) {
        freq *= lacunarity;
        amp *= gain;
        signal = (valueNoise(x * freq, y * freq) + offset) * gain;
        result += weight * signal * amp;
        norm += amp;
        weight = std::clamp(weight * signal, 0.f, 1.f);
    }
    return norm > 0.f ? std::clamp(result / (norm * (1.f + offset)), 0.f, 1.f) : 0.f;
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
    const float qx = fbm(x, y, octaves);
    const float qy = fbm(x + 5.2f, y + 1.3f, octaves);
    const float rx = fbm(x + amp * qx + 1.7f, y + amp * qy + 9.2f, octaves);
    const float ry = fbm(x + amp * qx + 8.3f, y + amp * qy + 2.8f, octaves);
    return fbm(x + amp * rx, y + amp * ry, octaves);
}

float NoiseField::curlNoiseX(float x, float y, float eps) const {
    eps = std::max(eps, 1e-6f);
    // curl(φ).x = dφ/dy
    return (valueNoise(x, y + eps) - valueNoise(x, y - eps)) / (2.f * eps);
}

float NoiseField::curlNoiseY(float x, float y, float eps) const {
    eps = std::max(eps, 1e-6f);
    // curl(φ).y = -dφ/dx
    return -(valueNoise(x + eps, y) - valueNoise(x - eps, y)) / (2.f * eps);
}

float NoiseField::hexDistance(float x, float y) const {
    float cx = 0.f, cy = 0.f;
    nearestHexCell(x, y, cx, cy);
    const float ox = x - cx * kHexFactor;
    const float oy = y - cy;
    return std::fabs(hexDistLocal(ox, oy));
}

float NoiseField::hexValue(float x, float y) const {
    float cx = 0.f, cy = 0.f;
    nearestHexCell(x, y, cx, cy);
    return hash01(int(std::floor(cx)), int(std::floor(cy)));
}

float NoiseField::hexEdge(float x, float y, float width, float soft) const {
    const float d    = hexDistance(x, y);
    const float band = std::max(soft, 1e-4f);
    // 1 on edge when d ≈ 0 within width (noisecreater lineMask).
    const float t = std::clamp((d - width) / band, 0.f, 1.f);
    return 1.f - (t * t * (3.f - 2.f * t));
}

float NoiseField::gyroid(float x, float y, float z) const {
    // Seed perturbs phase so different fields don't share zeros.
    const float p = float(seed) * 0.001f;
    return remapSigned01(gyroidRaw(x + p, y + p * 1.3f, z + p * 0.7f) / 1.5f);
}

float NoiseField::gyroidFbm(float x, float y, float z, int octaves) const {
    octaves            = std::max(1, std::min(octaves, 8));
    float       result = 0.f;
    float       a      = 0.5f;
    float       seedZ  = z;
    float       norm   = 0.f;
    const float ph     = float(seed) * 0.001f;
    for (int i = 0; i < octaves; ++i) {
        seedZ += result * 0.5f;
        result += std::fabs(gyroidRaw((x + ph) / a, (y + ph) / a, (seedZ + ph) / a)) * a;
        norm += a;
        a *= 0.5f;
    }
    return norm > 0.f ? std::clamp(result / (norm * 1.5f), 0.f, 1.f) : 0.f;
}

float NoiseField::jordan(float x, float y, int octaves, float lacunarity, float gain, float warpStrength) const {
    octaves             = std::max(1, octaves);
    float           sum = 0.f, amp = 1.f, norm = 0.f, freq = 1.f;
    float           dx = 0.f, dy = 0.f;
    constexpr float kEps = 1e-3f;
    for (int i = 0; i < octaves; ++i) {
        const float px  = x * freq + dx * warpStrength;
        const float py  = y * freq + dy * warpStrength;
        const float n   = valueNoise(px, py) * 2.f - 1.f;
        const float dnx = (valueNoise(px + kEps, py) - valueNoise(px - kEps, py)) / (2.f * kEps);
        const float dny = (valueNoise(px, py + kEps) - valueNoise(px, py - kEps)) / (2.f * kEps);
        sum += n * amp;
        norm += amp;
        dx += dnx * amp;
        dy += dny * amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return norm > 0.f ? remapSigned01(sum / norm) : 0.5f;
}

float NoiseField::gabor(float x, float y, float dirX, float dirY, float frequency) const {
    // noisecreater gavoronoi3: 3×3 Gaussian-weighted cos carriers.
    const int       ix       = int(std::floor(x));
    const int       iy       = int(std::floor(y));
    const float     fx       = x - float(ix);
    const float     fy       = y - float(iy);
    constexpr float kCellVar = 0.8f;
    constexpr float kDirVar  = 0.4f;
    float           va       = 0.f;
    float           wt       = 0.f;
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            const int   cx  = ix + i;
            const int   cy  = iy + j;
            const float hx  = (hash01(cx, cy) - 0.5f);
            const float hy  = (hash01(cx * 3 + 11, cy * 7 + 5) - 0.5f);
            const float ox  = float(i) - 0.5f;
            const float oy  = float(j) - 0.5f;
            const float ppx = fx + ox - hx;
            const float ppy = fy + oy - hy;
            const float d2  = ppx * ppx + ppy * ppy;
            const float w   = std::exp(-d2 * 4.f);
            float       dx  = kDirVar * hx + dirX;
            float       dy  = kDirVar * hy + dirY;
            wt += w;
            va += std::cos((ppx * dx + ppy * dy) * frequency / kCellVar) * w;
        }
    }
    return wt > 0.f ? remapSigned01(va / wt) : 0.5f;
}

float NoiseField::gaborFbm(float x, float y, int octaves, float dirX, float dirY) const {
    octaves   = std::max(1, std::min(octaves, 8));
    float sum = 0.f, freq = 1.f, norm = 0.f, amp = 1.f;
    float px = x, py = y;
    for (int i = 0; i < octaves; ++i) {
        const float n = gabor(px * freq, py * freq, dirX, dirY) * 2.f - 1.f;
        sum += std::fabs(n) * amp;
        norm += amp;
        px += -0.01f * sum;
        py += 0.07f * sum;
        amp *= 0.5f;
        freq *= 2.2f;
    }
    return norm > 0.f ? std::clamp(sum / norm, 0.f, 1.f) : 0.f;
}

float NoiseField::valueDx(float x, float y, float eps) const {
    eps = std::max(eps, 1e-6f);
    return (valueNoise(x + eps, y) - valueNoise(x - eps, y)) / (2.f * eps);
}

float NoiseField::valueDy(float x, float y, float eps) const {
    eps = std::max(eps, 1e-6f);
    return (valueNoise(x, y + eps) - valueNoise(x, y - eps)) / (2.f * eps);
}

float NoiseField::valueSlope(float x, float y, float eps) const {
    const float dx = valueDx(x, y, eps);
    const float dy = valueDy(x, y, eps);
    return std::sqrt(dx * dx + dy * dy);
}

float NoiseField::isoLine(float x, float y, int levels, float width) const {
    levels        = std::max(1, levels);
    width         = std::max(1e-4f, width);
    const float n = valueNoise(x, y);
    // Distance to nearest contour in level space, then soft line mask.
    const float scaled = n * float(levels);
    const float v      = std::fabs(std::fmod(scaled + 0.5f, 1.f) - 0.5f);
    const float t      = std::clamp(v / width, 0.f, 1.f);
    return 1.f - (t * t * (3.f - 2.f * t));
}

float NoiseField::banding(float x, float y, int bands) const {
    bands            = std::max(1, bands);
    const float n    = valueNoise(x, y);
    const float step = 1.f / float(bands);
    return std::floor(n * float(bands)) * step;
}

float NoiseField::fbmColor(float x, float y, int channel, int octaves, float lacunarity, float gain) const {
    // Fixed decorrelated offsets per RGB-style channel (noisecreater FBM color idea).
    channel                       = ((channel % 3) + 3) % 3;
    static constexpr float kOx[3] = {0.f, 19.1f, 5.2f};
    static constexpr float kOy[3] = {0.f, 7.3f, 31.7f};
    return fbm(x + kOx[channel], y + kOy[channel], octaves, lacunarity, gain);
}

// --- NoiseField3 -----------------------------------------------------------

float NoiseField3::hash01(int ix, int iy, int iz) const {
    ix         = wrapIndex(ix, periodX);
    iy         = wrapIndex(iy, periodY);
    iz         = wrapIndex(iz, periodZ);
    uint32_t h = seed;
    h ^= uint32_t(ix) * 374761393u;
    h ^= uint32_t(iy) * 668265263u;
    h ^= uint32_t(iz) * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return float(h & 0x00FFFFFFu) / float(0x00FFFFFFu);
}

float NoiseField3::whiteNoise(int ix, int iy, int iz) const { return hash01(ix, iy, iz); }

float NoiseField3::valueNoise(float x, float y, float z) const {
    const int   x0 = int(std::floor(x));
    const int   y0 = int(std::floor(y));
    const int   z0 = int(std::floor(z));
    const float fx = fade(x - float(x0));
    const float fy = fade(y - float(y0));
    const float fz = fade(z - float(z0));

    const float n000 = hash01(x0, y0, z0);
    const float n100 = hash01(x0 + 1, y0, z0);
    const float n010 = hash01(x0, y0 + 1, z0);
    const float n110 = hash01(x0 + 1, y0 + 1, z0);
    const float n001 = hash01(x0, y0, z0 + 1);
    const float n101 = hash01(x0 + 1, y0, z0 + 1);
    const float n011 = hash01(x0, y0 + 1, z0 + 1);
    const float n111 = hash01(x0 + 1, y0 + 1, z0 + 1);

    const float nx00 = n000 + (n100 - n000) * fx;
    const float nx10 = n010 + (n110 - n010) * fx;
    const float nx01 = n001 + (n101 - n001) * fx;
    const float nx11 = n011 + (n111 - n011) * fx;
    const float nxy0 = nx00 + (nx10 - nx00) * fy;
    const float nxy1 = nx01 + (nx11 - nx01) * fy;
    return nxy0 + (nxy1 - nxy0) * fz;
}

float NoiseField3::perlinNoise(float x, float y, float z) const {
    const int   x0 = int(std::floor(x));
    const int   y0 = int(std::floor(y));
    const int   z0 = int(std::floor(z));
    const float dx = x - float(x0);
    const float dy = y - float(y0);
    const float dz = z - float(z0);
    const float fx = fade5(dx);
    const float fy = fade5(dy);
    const float fz = fade5(dz);

    auto grad = [&](int ix, int iy, int iz, float px, float py, float pz) -> float {
        // 12 edge midpoints of a cube (Perlin 2002).
        static const float g[12][3] = {{1, 1, 0},  {-1, 1, 0},  {1, -1, 0}, {-1, -1, 0}, {1, 0, 1},  {-1, 0, 1},
                                       {1, 0, -1}, {-1, 0, -1}, {0, 1, 1},  {0, -1, 1},  {0, 1, -1}, {0, -1, -1}};
        const int          h        = int(hash01(ix, iy, iz) * 11.999f) % 12;
        return g[h][0] * (px - float(ix)) + g[h][1] * (py - float(iy)) + g[h][2] * (pz - float(iz));
    };

    const float n000 = grad(x0, y0, z0, x, y, z);
    const float n100 = grad(x0 + 1, y0, z0, x, y, z);
    const float n010 = grad(x0, y0 + 1, z0, x, y, z);
    const float n110 = grad(x0 + 1, y0 + 1, z0, x, y, z);
    const float n001 = grad(x0, y0, z0 + 1, x, y, z);
    const float n101 = grad(x0 + 1, y0, z0 + 1, x, y, z);
    const float n011 = grad(x0, y0 + 1, z0 + 1, x, y, z);
    const float n111 = grad(x0 + 1, y0 + 1, z0 + 1, x, y, z);

    const float nx00 = n000 + (n100 - n000) * fx;
    const float nx10 = n010 + (n110 - n010) * fx;
    const float nx01 = n001 + (n101 - n001) * fx;
    const float nx11 = n011 + (n111 - n011) * fx;
    const float nxy0 = nx00 + (nx10 - nx00) * fy;
    const float nxy1 = nx01 + (nx11 - nx01) * fy;
    const float n    = nxy0 + (nxy1 - nxy0) * fz;
    return std::clamp(n * 0.5f + 0.5f, 0.f, 1.f);
}

float NoiseField3::simplexNoise(float x, float y, float z) const {
    // Ashima / webgl-noise 3D simplex.
    constexpr float F3 = 1.f / 3.f;
    constexpr float G3 = 1.f / 6.f;

    const float s  = (x + y + z) * F3;
    const int   i  = int(std::floor(x + s));
    const int   j  = int(std::floor(y + s));
    const int   k  = int(std::floor(z + s));
    const float t  = float(i + j + k) * G3;
    const float x0 = x - (float(i) - t);
    const float y0 = y - (float(j) - t);
    const float z0 = z - (float(k) - t);

    int i1, j1, k1, i2, j2, k2;
    if (x0 >= y0) {
        if (y0 >= z0) {
            i1 = 1;
            j1 = 0;
            k1 = 0;
            i2 = 1;
            j2 = 1;
            k2 = 0;
        } else if (x0 >= z0) {
            i1 = 1;
            j1 = 0;
            k1 = 0;
            i2 = 1;
            j2 = 0;
            k2 = 1;
        } else {
            i1 = 0;
            j1 = 0;
            k1 = 1;
            i2 = 1;
            j2 = 0;
            k2 = 1;
        }
    } else {
        if (y0 < z0) {
            i1 = 0;
            j1 = 0;
            k1 = 1;
            i2 = 0;
            j2 = 1;
            k2 = 1;
        } else if (x0 < z0) {
            i1 = 0;
            j1 = 1;
            k1 = 0;
            i2 = 0;
            j2 = 1;
            k2 = 1;
        } else {
            i1 = 0;
            j1 = 1;
            k1 = 0;
            i2 = 1;
            j2 = 1;
            k2 = 0;
        }
    }

    const float x1 = x0 - float(i1) + G3;
    const float y1 = y0 - float(j1) + G3;
    const float z1 = z0 - float(k1) + G3;
    const float x2 = x0 - float(i2) + 2.f * G3;
    const float y2 = y0 - float(j2) + 2.f * G3;
    const float z2 = z0 - float(k2) + 2.f * G3;
    const float x3 = x0 - 1.f + 3.f * G3;
    const float y3 = y0 - 1.f + 3.f * G3;
    const float z3 = z0 - 1.f + 3.f * G3;

    auto gradDot = [&](int ix, int iy, int iz, float px, float py, float pz) -> float {
        static const float g[12][3] = {{1, 1, 0},  {-1, 1, 0},  {1, -1, 0}, {-1, -1, 0}, {1, 0, 1},  {-1, 0, 1},
                                       {1, 0, -1}, {-1, 0, -1}, {0, 1, 1},  {0, -1, 1},  {0, 1, -1}, {0, -1, -1}};
        const int          h        = int(hash01(ix, iy, iz) * 11.999f) % 12;
        return g[h][0] * px + g[h][1] * py + g[h][2] * pz;
    };

    auto contrib = [&](float px, float py, float pz, int ix, int iy, int iz) -> float {
        float t = 0.6f - px * px - py * py - pz * pz;
        if (t < 0.f) return 0.f;
        t *= t;
        return t * t * gradDot(ix, iy, iz, px, py, pz);
    };

    const float n = 32.f * (contrib(x0, y0, z0, i, j, k) + contrib(x1, y1, z1, i + i1, j + j1, k + k1) +
                            contrib(x2, y2, z2, i + i2, j + j2, k + k2) + contrib(x3, y3, z3, i + 1, j + 1, k + 1));
    return std::clamp(n * 0.5f + 0.5f, 0.f, 1.f);
}

float NoiseField3::worleyF1(float x, float y, float z, float jitter, WorleyMetric metric) const {
    jitter       = std::clamp(jitter, 0.f, 1.f);
    const int ix = int(std::floor(x));
    const int iy = int(std::floor(y));
    const int iz = int(std::floor(z));
    float     f1 = 1e9f;
    for (int k = -1; k <= 1; ++k) {
        for (int j = -1; j <= 1; ++j) {
            for (int i = -1; i <= 1; ++i) {
                const int   cx = ix + i;
                const int   cy = iy + j;
                const int   cz = iz + k;
                const float px = float(cx) + 0.5f + (hash01(cx, cy, cz) - 0.5f) * jitter;
                const float py = float(cy) + 0.5f + (hash01(cx + 17, cy + 3, cz + 5) - 0.5f) * jitter;
                const float pz = float(cz) + 0.5f + (hash01(cx + 7, cy + 13, cz + 19) - 0.5f) * jitter;
                f1             = std::min(f1, metricDist3(x - px, y - py, z - pz, metric));
            }
        }
    }
    return f1;
}

float NoiseField3::worleyNoise(float x, float y, float z, float jitter, WorleyMetric metric) const {
    return std::clamp(1.f - worleyF1(x, y, z, jitter, metric), 0.f, 1.f);
}

float NoiseField3::fbm(float x, float y, float z, int octaves, float lacunarity, float gain) const {
    octaves   = std::max(1, octaves);
    float sum = 0.f, amp = 1.f, norm = 0.f, freq = 1.f;
    for (int i = 0; i < octaves; ++i) {
        sum += valueNoise(x * freq, y * freq, z * freq) * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return norm > 0.f ? sum / norm : 0.f;
}

float NoiseField3::fbmPerlin(float x, float y, float z, int octaves, float lacunarity, float gain) const {
    octaves   = std::max(1, octaves);
    float sum = 0.f, amp = 1.f, norm = 0.f, freq = 1.f;
    for (int i = 0; i < octaves; ++i) {
        sum += perlinNoise(x * freq + float(i) * 17.8f, y * freq + float(i) * 23.5f, z * freq + float(i) * 11.1f) * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return norm > 0.f ? sum / norm : 0.f;
}

float NoiseField3::ridged(float x, float y, float z, int octaves, float lacunarity, float gain) const {
    octaves   = std::max(1, octaves);
    float sum = 0.f, amp = 1.f, norm = 0.f, freq = 1.f;
    for (int i = 0; i < octaves; ++i) {
        float n = valueNoise(x * freq, y * freq, z * freq);
        n       = 1.f - std::fabs(n * 2.f - 1.f);
        sum += n * n * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return norm > 0.f ? sum / norm : 0.f;
}

float NoiseField3::billow(float x, float y, float z, int octaves, float lacunarity, float gain) const {
    octaves   = std::max(1, octaves);
    float sum = 0.f, amp = 1.f, norm = 0.f, freq = 1.f;
    for (int i = 0; i < octaves; ++i) {
        sum += std::fabs(valueNoise(x * freq, y * freq, z * freq) * 2.f - 1.f) * amp;
        norm += amp;
        amp *= gain;
        freq *= lacunarity;
    }
    return norm > 0.f ? sum / norm : 0.f;
}

float NoiseField3::gyroid(float x, float y, float z) const {
    const float p = float(seed) * 0.001f;
    return remapSigned01(gyroidRaw(x + p, y + p * 1.3f, z + p * 0.7f) / 1.5f);
}

float NoiseField3::gyroidFbm(float x, float y, float z, int octaves) const {
    octaves            = std::max(1, std::min(octaves, 8));
    float       result = 0.f;
    float       a      = 0.5f;
    float       seedZ  = z;
    float       norm   = 0.f;
    const float ph     = float(seed) * 0.001f;
    for (int i = 0; i < octaves; ++i) {
        seedZ += result * 0.5f;
        result += std::fabs(gyroidRaw((x + ph) / a, (y + ph) / a, (seedZ + ph) / a)) * a;
        norm += a;
        a *= 0.5f;
    }
    return norm > 0.f ? std::clamp(result / (norm * 1.5f), 0.f, 1.f) : 0.f;
}

float NoiseField3::perlinWorley(float x, float y, float z, int octaves, float jitter) const {
    return std::clamp(worleyNoise(x, y, z, jitter) * fbmPerlin(x, y, z, std::max(1, octaves)), 0.f, 1.f);
}

}  // namespace eve::procgen
