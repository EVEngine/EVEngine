#include "physics/rope/Rope3D.h"

#include <algorithm>
#include <cmath>

namespace eve::physics {
namespace {
Rope3D::Vec3 subtract(Rope3D::Vec3 a, Rope3D::Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Rope3D::Vec3 interpolate(Rope3D::Vec3 a, Rope3D::Vec3 b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}
Rope3D::Vec3 normalized(Rope3D::Vec3 value) {
    const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    return length > 1e-6f ? Rope3D::Vec3{value.x / length, value.y / length, value.z / length}
                          : Rope3D::Vec3{1.f, 0.f, 0.f};
}
}  // namespace

Rope3D::Vec3 Rope3D::samplePosition(float mu) const {
    if (particles_.empty()) return {};
    const float total = getRestLength();
    if (total <= 1e-6f) return particles_.front().position;
    float distance = std::clamp(std::isfinite(mu) ? mu : 0.f, 0.f, 1.f) * total;
    for (const auto& element : elements_) {
        if (!element.active) continue;
        if (distance <= element.restLength)
            return interpolate(particles_[element.a].position, particles_[element.b].position,
                               element.restLength > 1e-6f ? distance / element.restLength : 0.f);
        distance -= element.restLength;
    }
    for (auto it = elements_.rbegin(); it != elements_.rend(); ++it)
        if (it->active) return particles_[it->b].position;
    return particles_.front().position;
}

Rope3D::Vec3 Rope3D::sampleTangent(float mu) const {
    const float total = getRestLength();
    if (total <= 1e-6f) return {1.f, 0.f, 0.f};
    float distance = std::clamp(std::isfinite(mu) ? mu : 0.f, 0.f, 1.f) * total;
    for (const auto& element : elements_) {
        if (!element.active) continue;
        if (distance <= element.restLength)
            return normalized(subtract(particles_[element.b].position, particles_[element.a].position));
        distance -= element.restLength;
    }
    for (auto it = elements_.rbegin(); it != elements_.rend(); ++it)
        if (it->active) return normalized(subtract(particles_[it->b].position, particles_[it->a].position));
    return {1.f, 0.f, 0.f};
}

float Rope3D::getSampleX(float mu) const { return samplePosition(mu).x; }
float Rope3D::getSampleY(float mu) const { return samplePosition(mu).y; }
float Rope3D::getSampleZ(float mu) const { return samplePosition(mu).z; }
float Rope3D::getSampleTangentX(float mu) const { return sampleTangent(mu).x; }
float Rope3D::getSampleTangentY(float mu) const { return sampleTangent(mu).y; }
float Rope3D::getSampleTangentZ(float mu) const { return sampleTangent(mu).z; }

}  // namespace eve::physics
