#include "snow/SnowField.h"

#include <algorithm>
#include <cmath>

namespace eve::snow {

namespace {

/** @brief Hermite 0→1 over [edge0, edge1] (clamped). */
float smoothstep(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

/** @brief Deterministic per-cell hash in [0,1] for subtle albedo noise. */
float cellNoise(int x, int y) {
    const float v = std::sin(float(x) * 12.9898f + float(y) * 78.233f) * 43758.5453f;
    return v - std::floor(v);
}

}  // namespace

SnowField::SnowField(int width, int height) {
    resize(width, height);
}

void SnowField::resize(int width, int height) {
    width_ = std::max(width, 0);
    height_ = std::max(height, 0);
    data_.assign(size_t(width_) * size_t(height_), 0.f);
    dirty_ = true;
}

bool SnowField::inBounds(int x, int y) const {
    return x >= 0 && y >= 0 && x < width_ && y < height_;
}

void SnowField::fill(float v) {
    const float c = clamp01(v);
    std::fill(data_.begin(), data_.end(), c);
    dirty_ = true;
}

void SnowField::setHeight(int x, int y, float h) {
    if (!inBounds(x, y)) return;
    data_[size_t(y) * width_ + x] = clamp01(h);
    dirty_ = true;
}

float SnowField::height(int x, int y) const {
    if (!inBounds(x, y)) return 0.f;
    return data_[size_t(y) * width_ + x];
}

void SnowField::stampFootprint(float cx, float cz, float dirX, float dirZ, float radius,
                               float depth) {
    if (width_ <= 0 || height_ <= 0) return;

    float len = std::sqrt(dirX * dirX + dirZ * dirZ);
    if (len < 1e-5f) {
        dirX = 1.f;
        dirZ = 0.f;
    } else {
        dirX /= len;
        dirZ /= len;
    }

    const float r = std::max(radius, 0.25f);
    const float d = std::clamp(depth, 0.f, 1.f);
    const float across = r * 0.55f;  // footprints are longer than they are wide
    const float margin = r * 1.25f;
    const int x0 = int(std::floor(cx - margin));
    const int x1 = int(std::floor(cx + margin));
    const int z0 = int(std::floor(cz - margin));
    const int z1 = int(std::floor(cz + margin));

    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            if (!inBounds(x, z)) continue;
            const float dx = float(x) - cx;
            const float dz = float(z) - cz;
            const float along = dx * dirX + dz * dirZ;
            const float perp = -dx * dirZ + dz * dirX;
            const float n = std::sqrt((along / r) * (along / r) +
                                      (perp / across) * (perp / across));
            if (n > 1.2f) continue;

            // Flat-bottomed step profile: full depth out to ~72% of the
            // footprint, then steep walls rising to the surface. Reads much
            // clearer than a soft bowl once the mesh is smooth-shaded.
            const float depression = d * (1.f - smoothstep(0.72f, 1.f, n));
            // Kicked-snow mounds ringing the footprint: a per-cell hash makes
            // the raised rim lumpy (snow thrown to the side while walking),
            // instead of a perfect smooth ring.
            const float ring = smoothstep(0.72f, 1.f, n) *
                               (1.f - smoothstep(1.f, 1.18f, n));
            const float rnd = 0.35f + 0.65f * cellNoise(x, z);
            const float rim = d * 0.5f * ring * rnd;
            float &cell = data_[size_t(z) * width_ + x];
            cell = clamp01(cell - depression + rim);
        }
    }
    dirty_ = true;
}

void SnowField::stampImpact(float cx, float cz, float radius, float depth) {
    if (width_ <= 0 || height_ <= 0) return;

    const float r = std::max(radius, 0.25f);
    const float d = std::clamp(depth, 0.f, 1.f);
    const float margin = r * 1.45f;
    const int x0 = int(std::floor(cx - margin));
    const int x1 = int(std::floor(cx + margin));
    const int z0 = int(std::floor(cz - margin));
    const int z1 = int(std::floor(cz + margin));

    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            if (!inBounds(x, z)) continue;
            const float dx = float(x) - cx;
            const float dz = float(z) - cz;
            const float dist = std::sqrt(dx * dx + dz * dz);
            const float n = dist / r;
            if (n > 1.45f) continue;

            const float bowl = d * (1.f - n) * (1.f - n);
            // Random ejecta ring: snow thrown out of the crater piles up in
            // lumpy mounds around the rim (hash-randomized height per cell).
            const float ring = smoothstep(0.85f, 1.f, n) *
                               (1.f - smoothstep(1.f, 1.4f, n));
            const float rnd = 0.3f + 0.7f * cellNoise(x, z);
            const float rim = d * 0.75f * ring * rnd;
            float &cell = data_[size_t(z) * width_ + x];
            cell = clamp01(cell - bowl + rim);
        }
    }
    dirty_ = true;
}

void SnowField::addSnowfall(float amount) {
    if (amount <= 0.f || data_.empty()) return;
    for (float &v : data_) {
        v = std::min(1.f, v + amount);
    }
    dirty_ = true;
}

std::vector<uint8_t> SnowField::toHeightRGBA() const {
    std::vector<uint8_t> rgba(size_t(width_) * size_t(height_) * 4, 0);
    for (int z = 0; z < height_; ++z) {
        for (int x = 0; x < width_; ++x) {
            const float s = data_[size_t(z) * width_ + x];
            const size_t i = (size_t(z) * width_ + x) * 4;
            rgba[i + 0] = uint8_t(std::clamp(s * 255.f, 0.f, 255.f) + 0.5f);
            rgba[i + 1] = 0;
            rgba[i + 2] = 0;
            rgba[i + 3] = 255;
        }
    }
    return rgba;
}

std::vector<uint8_t> SnowField::toAlbedoRGBA() const {
    constexpr float kSnow[3] = {0.93f, 0.96f, 1.00f};    // cool white
    constexpr float kGround[3] = {0.36f, 0.30f, 0.24f};  // dark soil
    std::vector<uint8_t> rgba(size_t(width_) * size_t(height_) * 4, 0);
    for (int z = 0; z < height_; ++z) {
        for (int x = 0; x < width_; ++x) {
            const float s = data_[size_t(z) * width_ + x];
            const float t = s * s;  // quadratic: floors show ground sooner
            const float n = (cellNoise(x, z) - 0.5f) * 0.05f;
            const size_t i = (size_t(z) * width_ + x) * 4;
            for (int c = 0; c < 3; ++c) {
                const float v = kGround[c] + (kSnow[c] - kGround[c]) * t + n;
                rgba[i + c] = uint8_t(std::clamp(v * 255.f, 0.f, 255.f) + 0.5f);
            }
            rgba[i + 3] = 255;
        }
    }
    return rgba;
}

std::vector<uint8_t> SnowField::toNormalRGBA() const {
    constexpr float kStrength = 9.f;
    std::vector<uint8_t> rgba(size_t(width_) * size_t(height_) * 4, 0);
    if (width_ <= 0 || height_ <= 0) return rgba;
    auto sample = [&](int x, int z) -> float {
        x = std::clamp(x, 0, width_ - 1);
        z = std::clamp(z, 0, height_ - 1);
        return data_[size_t(z) * width_ + x];
    };
    for (int z = 0; z < height_; ++z) {
        for (int x = 0; x < width_; ++x) {
            const float dhdu = (sample(x + 1, z) - sample(x - 1, z)) * 0.5f * kStrength;
            const float dhdv = (sample(x, z + 1) - sample(x, z - 1)) * 0.5f * kStrength;
            const float len = std::sqrt(dhdu * dhdu + dhdv * dhdv + 1.f);
            const float nx = -dhdu / len;
            const float ny = -dhdv / len;
            const float nz = 1.f / len;
            const size_t i = (size_t(z) * width_ + x) * 4;
            rgba[i + 0] = uint8_t(std::clamp((nx * 0.5f + 0.5f) * 255.f, 0.f, 255.f) + 0.5f);
            rgba[i + 1] = uint8_t(std::clamp((ny * 0.5f + 0.5f) * 255.f, 0.f, 255.f) + 0.5f);
            rgba[i + 2] = uint8_t(std::clamp((nz * 0.5f + 0.5f) * 255.f, 0.f, 255.f) + 0.5f);
            rgba[i + 3] = 255;
        }
    }
    return rgba;
}

float SnowField::clamp01(float v) {
    return std::clamp(v, 0.f, 1.f);
}

}  // namespace eve::snow
