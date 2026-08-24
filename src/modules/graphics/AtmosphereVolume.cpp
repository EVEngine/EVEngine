#include "graphics/AtmosphereVolume.h"
#include "graphics/FogVolume.h"

#include <algorithm>
#include <cmath>

namespace eve::graphics {

void AtmosphereVolume::resize(int width, int height, int depth) {
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);
    depth_ = std::max(depth, 1);
    media_.assign(std::size_t(width_) * std::size_t(height_) * std::size_t(depth_), {});
    integrated_.assign(media_.size(), glm::vec4(0.f, 0.f, 0.f, 1.f));
}

void AtmosphereVolume::clear() {
    std::fill(media_.begin(), media_.end(), FogFroxel{});
    std::fill(integrated_.begin(), integrated_.end(), glm::vec4(0.f, 0.f, 0.f, 1.f));
}

void AtmosphereVolume::setDepthRange(float nearDistance, float farDistance) {
    nearDistance_ = std::max(nearDistance, 1e-3f);
    farDistance_ = std::max(farDistance, nearDistance_ + 1e-3f);
}

float AtmosphereVolume::sliceDistance(int z) const {
    if (depth_ <= 0) return nearDistance_;
    const float t = (float(std::clamp(z, 0, depth_ - 1)) + 0.5f) / float(depth_);
    return nearDistance_ * std::pow(farDistance_ / nearDistance_, t);
}

int AtmosphereVolume::sliceForDistance(float distance) const {
    if (depth_ <= 1) return 0;
    const float d = std::clamp(distance, nearDistance_, farDistance_);
    const float t = std::log(d / nearDistance_) / std::log(farDistance_ / nearDistance_);
    return std::clamp(int(std::floor(t * float(depth_))), 0, depth_ - 1);
}

std::size_t AtmosphereVolume::index(int x, int y, int z) const {
    x = std::clamp(x, 0, width_ - 1);
    y = std::clamp(y, 0, height_ - 1);
    z = std::clamp(z, 0, depth_ - 1);
    return (std::size_t(z) * std::size_t(height_) + std::size_t(y)) * std::size_t(width_) +
           std::size_t(x);
}

FogFroxel &AtmosphereVolume::at(int x, int y, int z) { return media_[index(x, y, z)]; }
const FogFroxel &AtmosphereVolume::at(int x, int y, int z) const { return media_[index(x, y, z)]; }

void AtmosphereVolume::injectHeightFog(float baseExtinction, const glm::vec3 &albedo,
                                       float baseHeight, float heightFalloff, float minWorldY,
                                       float maxWorldY) {
    const float sigma = std::max(baseExtinction, 0.f);
    const glm::vec3 omega = glm::clamp(albedo, glm::vec3(0.f), glm::vec3(1.f));
    const float falloff = std::max(heightFalloff, 0.f);
    for (int y = 0; y < height_; ++y) {
        const float ty = (float(y) + 0.5f) / float(height_);
        const float worldY = minWorldY + (maxWorldY - minWorldY) * ty;
        const float extinction = sigma * std::exp(-falloff * std::max(worldY - baseHeight, 0.f));
        for (int z = 0; z < depth_; ++z) {
            for (int x = 0; x < width_; ++x) {
                FogFroxel &f = at(x, y, z);
                f.extinction += extinction;
                f.scattering += omega * extinction;
            }
        }
    }
}

void AtmosphereVolume::injectLocalVolume(const FogVolume &volume, const glm::vec3 &worldMin,
                                         const glm::vec3 &worldMax) {
    for (int z = 0; z < depth_; ++z) {
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const glm::vec3 uvw((float(x) + 0.5f) / float(width_),
                                    (float(y) + 0.5f) / float(height_),
                                    (float(z) + 0.5f) / float(depth_));
                const glm::vec3 world = worldMin + (worldMax - worldMin) * uvw;
                const float signedExtinction = volume.sampleExtinction(world);
                if (std::fabs(signedExtinction) <= 1e-8f) continue;
                FogFroxel &f = at(x, y, z);
                const float previous = f.extinction;
                f.extinction = std::max(0.f, previous + signedExtinction);
                if (signedExtinction > 0.f) {
                    f.scattering += volume.getAlbedo() * signedExtinction;
                    f.emissive += volume.getEmissive() * signedExtinction;
                    f.anisotropy = volume.getAnisotropy();
                } else if (previous > 1e-6f) {
                    const float scale = f.extinction / previous;
                    f.scattering *= scale;
                    f.emissive *= scale;
                }
            }
        }
    }
}

void AtmosphereVolume::integrate(const glm::vec3 &lightColor, float phaseScale) {
    const glm::vec3 light = glm::max(lightColor, glm::vec3(0.f)) * std::max(phaseScale, 0.f);
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            glm::vec3 radiance(0.f);
            float transmittance = 1.f;
            float previousDistance = nearDistance_;
            for (int z = 0; z < depth_; ++z) {
                const float distance = sliceDistance(z);
                const float stepLength = std::max(distance - previousDistance, 0.f);
                previousDistance = distance;
                const FogFroxel &f = at(x, y, z);
                const float opticalDepth = f.extinction * stepLength;
                const float stepTransmittance = std::exp(-opticalDepth);
                const float integral = f.extinction > 1e-6f
                    ? (1.f - stepTransmittance) / f.extinction
                    : stepLength;
                radiance += transmittance *
                    (f.scattering * light * f.lightVisibility + f.emissive) * integral;
                transmittance *= stepTransmittance;
                integrated_[index(x, y, z)] = glm::vec4(radiance, transmittance);
            }
        }
    }
}

void AtmosphereVolume::setLightVisibility(int x, int y, int z, float visibility) {
    at(x, y, z).lightVisibility = std::clamp(visibility, 0.f, 1.f);
}

std::size_t AtmosphereVolume::blendHistory(const AtmosphereVolume &history, float historyWeight,
                                           float rejectionThreshold) {
    if (history.width_ != width_ || history.height_ != height_ || history.depth_ != depth_)
        return integrated_.size();
    const float baseWeight = std::clamp(historyWeight, 0.f, 1.f);
    const float threshold = std::max(rejectionThreshold, 0.f);
    std::size_t rejected = 0;
    for (std::size_t i = 0; i < integrated_.size(); ++i) {
        const glm::vec4 current = integrated_[i];
        const glm::vec4 previous = history.integrated_[i];
        const float currentLuma = glm::dot(glm::vec3(current), glm::vec3(0.2126f, 0.7152f, 0.0722f));
        const float previousLuma = glm::dot(glm::vec3(previous), glm::vec3(0.2126f, 0.7152f, 0.0722f));
        const float relativeDelta = std::fabs(currentLuma - previousLuma) /
            std::max(std::max(currentLuma, previousLuma), 1e-4f);
        const bool reject = relativeDelta > threshold;
        if (reject) ++rejected;
        const float weight = reject ? 0.f : baseWeight;
        integrated_[i] = glm::mix(current, previous, weight);
    }
    return rejected;
}

const glm::vec4 &AtmosphereVolume::integratedAt(int x, int y, int z) const {
    return integrated_[index(x, y, z)];
}

}  // namespace eve::graphics
