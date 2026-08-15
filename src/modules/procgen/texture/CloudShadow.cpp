#include "procgen/texture/CloudShadow.h"

#include <algorithm>
#include <cmath>

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
    ox            = params_.sunDirX * s;
    oz            = params_.sunDirZ * s;
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
        const float z = z0 + extent * (float(y) / float(height - 1));
        for (int x = 0; x < width; ++x) {
            const float px = x0 + extent * (float(x) / float(width - 1));
            out[size_t(y * width + x)] = coverageAt(px, z, time);
        }
    }
}

void CloudShadow::sampleFactor(float *out, int width, int height, float time, float x0, float z0,
                               float extent) const {
    if (!out || width <= 0 || height <= 0) return;
    for (int y = 0; y < height; ++y) {
        const float z = z0 + extent * (float(y) / float(height - 1));
        for (int x = 0; x < width; ++x) {
            const float px = x0 + extent * (float(x) / float(width - 1));
            out[size_t(y * width + x)] = shadowFactorAt(px, z, time);
        }
    }
}

}  // namespace eve::procgen
