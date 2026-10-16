#include <algorithm>
#include <cmath>
#include <numeric>
#include "fluids/OceanSprayRenderer.h"

namespace eve::fluids {
Result<OceanSprayMeshFrame> OceanSprayMeshBuilder::build() const {
    auto finite = [](glm::dvec3 p) {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && glm::length(p) <= 1e9;
    };
    const auto cross = glm::cross(view.forward, view.up);
    if (!finite(view.eye) || !finite(view.origin) || !finite(view.forward) || !finite(view.up) ||
        glm::length(view.forward) < 1e-8 || glm::length(view.up) < 1e-8 ||
        glm::length(cross) < 1e-8 * glm::length(view.forward) * glm::length(view.up))
        return Result<OceanSprayMeshFrame>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Invalid spray camera basis", "fluids.ocean.spray.render"));
    const auto forward = glm::normalize(view.forward), right = glm::normalize(cross), up = glm::cross(right, forward);
    const auto particles = source_.particles();
    std::vector<unsigned> order(particles.size());
    std::iota(order.begin(), order.end(), 0U);
    std::stable_sort(order.begin(), order.end(), [&](unsigned a, unsigned b) {
        return glm::dot(particles[a].position - view.eye, forward) >
               glm::dot(particles[b].position - view.eye, forward);
    });
    OceanSprayMeshFrame out;
    out.time = source_.time();
    out.positions.reserve(particles.size() * 12);
    out.attributes.reserve(particles.size() * 12);
    out.uv.reserve(particles.size() * 8);
    out.indices.reserve(particles.size() * 6);
    for (unsigned i : order) {
        const auto& p       = particles[i];
        const auto  base    = static_cast<std::uint32_t>(out.positions.size() / 3);
        const float opacity = static_cast<float>(std::clamp(1.0 - p.age / p.lifetime, 0.0, 1.0));
        for (auto corner : {glm::dvec2(-1, -1), glm::dvec2(1, -1), glm::dvec2(1, 1), glm::dvec2(-1, 1)}) {
            const auto point = (p.position - view.origin) + (right * corner.x + up * corner.y) * p.radius;
            for (int axis = 0; axis < 3; ++axis) out.positions.push_back(static_cast<float>(point[axis]));
            out.attributes.insert(out.attributes.end(), {opacity, 0.0F, 0.0F});
            out.uv.insert(out.uv.end(), {static_cast<float>(corner.x), static_cast<float>(corner.y)});
        }
        out.indices.insert(out.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return Result<OceanSprayMeshFrame>::success(std::move(out));
}
}  // namespace eve::fluids
