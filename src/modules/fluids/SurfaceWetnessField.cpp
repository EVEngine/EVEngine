#include "fluids/SurfaceWetnessField.h"

#include <algorithm>
#include <cmath>

namespace eve::fluids {

bool SurfaceWetnessField::build(const FluidSurfaceBinding& binding) {
    triangles_ = binding.triangles();
    values_.assign(size_t(binding.vertexCount()), 0.f);
    neighbors_.assign(values_.size(), {});
    if (values_.empty() || triangles_.empty()) return false;
    for (const glm::uvec3& tri : triangles_) {
        const int ids[3] = {int(tri.x), int(tri.y), int(tri.z)};
        for (int edge = 0; edge < 3; ++edge) {
            auto& list = neighbors_[size_t(ids[edge])];
            const int other = ids[(edge + 1) % 3];
            if (std::find(list.begin(), list.end(), other) == list.end()) list.push_back(other);
            auto& reverse = neighbors_[size_t(other)];
            if (std::find(reverse.begin(), reverse.end(), ids[edge]) == reverse.end())
                reverse.push_back(ids[edge]);
        }
    }
    return true;
}

void SurfaceWetnessField::deposit(const SurfaceLocation& location, float amount) {
    if (location.triangle >= triangles_.size() || amount <= 0.f) return;
    const glm::uvec3 tri = triangles_[location.triangle];
    const glm::vec3 bary = glm::max(location.barycentric, glm::vec3(0.f));
    values_[tri.x] += amount * bary.x;
    values_[tri.y] += amount * bary.y;
    values_[tri.z] += amount * bary.z;
}

void SurfaceWetnessField::step(float dt, const SurfaceWetnessParams& params) {
    if (values_.empty() || dt <= 0.f) return;
    dt = std::min(dt, 0.1f);
    std::vector<float> next(values_.size(), 0.f);
    const float diffusion = std::clamp(params.diffusion * dt, 0.f, 1.f);
    const float evaporation = std::exp(-std::max(0.f, params.evaporation) * dt);
    const float maximum = std::max(0.f, params.maxWetness);
    for (size_t i = 0; i < values_.size(); ++i) {
        float average = values_[i];
        if (!neighbors_[i].empty()) {
            average = 0.f;
            for (int neighbor : neighbors_[i]) average += values_[size_t(neighbor)];
            average /= float(neighbors_[i].size());
        }
        next[i] = std::clamp(glm::mix(values_[i], average, diffusion) * evaporation, 0.f, maximum);
    }
    values_.swap(next);
}

float SurfaceWetnessField::sample(const SurfaceLocation& location) const {
    if (location.triangle >= triangles_.size()) return 0.f;
    const glm::uvec3 tri = triangles_[location.triangle];
    return values_[tri.x] * location.barycentric.x + values_[tri.y] * location.barycentric.y +
           values_[tri.z] * location.barycentric.z;
}

void SurfaceWetnessField::clear() { std::fill(values_.begin(), values_.end(), 0.f); }

}  // namespace eve::fluids
