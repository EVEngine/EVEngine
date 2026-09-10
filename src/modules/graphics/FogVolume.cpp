#include "graphics/FogVolume.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

namespace eve::graphics {
namespace {

float hashNoise(int x, int y, int z, int seed) {
    uint32_t value = uint32_t(x) * 0x8da6b343u ^ uint32_t(y) * 0xd8163841u ^
                     uint32_t(z) * 0xcb1ab31fu ^ uint32_t(seed) * 0x165667b1u;
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    return float(value & 0x00ffffffu) / float(0x01000000u);
}

float smoothNoise(const glm::vec3 &p, int seed) {
    const glm::ivec3 cell = glm::ivec3(glm::floor(p));
    const glm::vec3 fraction = glm::fract(p);
    const glm::vec3 blend = fraction * fraction * (glm::vec3(3.f) - 2.f * fraction);
    float corners[2][2][2];
    for (int z = 0; z < 2; ++z)
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
                corners[x][y][z] = hashNoise(cell.x + x, cell.y + y, cell.z + z, seed);
    const float z0 = glm::mix(glm::mix(corners[0][0][0], corners[1][0][0], blend.x),
                              glm::mix(corners[0][1][0], corners[1][1][0], blend.x), blend.y);
    const float z1 = glm::mix(glm::mix(corners[0][0][1], corners[1][0][1], blend.x),
                              glm::mix(corners[0][1][1], corners[1][1][1], blend.x), blend.y);
    return glm::mix(z0, z1, blend.z);
}

}  // namespace

void FogVolume::setShape(const std::string &shape) {
    if (shape == "sphere")
        shape_ = Shape::sphere;
    else if (shape == "cylinder")
        shape_ = Shape::cylinder;
    else
        shape_ = Shape::box;
}

std::string FogVolume::getShapeName() const {
    if (shape_ == Shape::sphere) return "sphere";
    if (shape_ == Shape::cylinder) return "cylinder";
    return "box";
}

void FogVolume::setSize(float x, float y, float z) {
    size_ = glm::max(glm::vec3(x, y, z), glm::vec3(1e-3f));
}

void FogVolume::setAlbedo(float r, float g, float b) {
    albedo_ = glm::clamp(glm::vec3(r, g, b), glm::vec3(0.f), glm::vec3(1.f));
}

void FogVolume::setEmissive(float r, float g, float b) {
    emissive_ = glm::max(glm::vec3(r, g, b), glm::vec3(0.f));
}

void FogVolume::setAnisotropy(float anisotropy) {
    anisotropy_ = std::clamp(anisotropy, -0.99f, 0.99f);
}

void FogVolume::setEdgeFalloff(float falloff) {
    edgeFalloff_ = std::clamp(falloff, 0.f, 1.f);
}

void FogVolume::setNoise(float amount, float scale, int seed) {
    noiseAmount_ = std::clamp(amount, 0.f, 1.f);
    noiseScale_ = std::max(scale, 1e-3f);
    noiseSeed_ = seed;
}

float FogVolume::sampleExtinction(const glm::vec3 &worldPosition) const {
    const glm::vec3 p = (worldPosition - position_) / (size_ * 0.5f);
    float radius = 0.f;
    if (shape_ == Shape::sphere) {
        radius = glm::length(p);
    } else if (shape_ == Shape::cylinder) {
        radius = std::max(glm::length(glm::vec2(p.x, p.z)), std::fabs(p.y));
    } else {
        radius = std::max(std::max(std::fabs(p.x), std::fabs(p.y)), std::fabs(p.z));
    }
    if (radius >= 1.f) return 0.f;
    const float inner = 1.f - edgeFalloff_;
    const float coverage = edgeFalloff_ <= 1e-6f || radius <= inner
        ? 1.f
        : 1.f - glm::smoothstep(inner, 1.f, radius);
    const float noise = smoothNoise(worldPosition * noiseScale_, noiseSeed_);
    const float densityVariation = glm::mix(1.f, 0.18f + 1.32f * noise, noiseAmount_);
    return extinction_ * coverage * densityVariation;
}

}  // namespace eve::graphics
