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
    const float margin = r * 1.2f;
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

            const float fall = smoothstep(0.f, 1.f, n);
            const float depression = d * (1.f - fall);
            const float rim = d * 0.3f * smoothstep(0.7f, 1.f, n) *
                              (1.f - smoothstep(1.f, 1.15f, n));
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
    const float margin = r * 1.4f;
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
            if (n > 1.4f) continue;

            const float bowl = d * (1.f - n) * (1.f - n);
            const float rim = d * 0.4f * smoothstep(0.8f, 1.f, n) *
                              (1.f - smoothstep(1.f, 1.35f, n));
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

std::vector<uint8_t> SnowField::toRGBA() const {
    std::vector<uint8_t> rgba(size_t(width_) * size_t(height_) * 4, 0);
    constexpr float kAlbedo[3] = {0.93f, 0.96f, 1.00f};  // cool white
    for (int z = 0; z < height_; ++z) {
        for (int x = 0; x < width_; ++x) {
            const float s = data_[size_t(z) * width_ + x];
            const float shade = 0.25f + 0.75f * s;
            const float n = (cellNoise(x, z) - 0.5f) * 0.06f;
            const size_t i = (size_t(z) * width_ + x) * 4;
            rgba[i + 0] = uint8_t(std::clamp(s * 255.f, 0.f, 255.f) + 0.5f);
            rgba[i + 1] = uint8_t(std::clamp((kAlbedo[0] * shade + n) * 255.f, 0.f, 255.f) + 0.5f);
            rgba[i + 2] = uint8_t(std::clamp((kAlbedo[1] * shade + n) * 255.f, 0.f, 255.f) + 0.5f);
            rgba[i + 3] = uint8_t(std::clamp((kAlbedo[2] * shade + n) * 255.f, 0.f, 255.f) + 0.5f);
        }
    }
    return rgba;
}

float SnowField::clamp01(float v) {
    return std::clamp(v, 0.f, 1.f);
}

}  // namespace eve::snow
