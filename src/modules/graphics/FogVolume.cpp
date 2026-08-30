#include "graphics/FogVolume.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

namespace eve::graphics {

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
    return extinction_ * coverage;
}

}  // namespace eve::graphics
