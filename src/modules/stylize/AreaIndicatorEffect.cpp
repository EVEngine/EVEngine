#include "stylize/AreaIndicatorEffect.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace eve::stylize {
namespace {

TrailVertex vertex(float x, float z, float u, float v, float alpha) {
    return {{x, 0.f, z}, {u, v}, alpha};
}

void appendFan(TrailMeshSnapshot& mesh, float radius, float startAngle, float angle,
               std::uint32_t segments, float centerAlpha, float edgeAlpha) {
    mesh.vertices.reserve(static_cast<std::size_t>(segments) + 2u);
    mesh.indices.reserve(static_cast<std::size_t>(segments) * 3u);
    mesh.vertices.push_back(vertex(0.f, 0.f, 0.5f, 0.5f, centerAlpha));
    for (std::uint32_t i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const float radians = startAngle + angle * t;
        const float x = std::cos(radians) * radius;
        const float z = std::sin(radians) * radius;
        mesh.vertices.push_back(vertex(x, z, x / (2.f * radius) + 0.5f,
                                       z / (2.f * radius) + 0.5f, edgeAlpha));
        if (i > 0) mesh.indices.insert(mesh.indices.end(), {0u, i, i + 1u});
    }
}

void appendRing(TrailMeshSnapshot& mesh, float innerRadius, float outerRadius,
                std::uint32_t segments, float innerAlpha, float outerAlpha) {
    mesh.vertices.reserve((static_cast<std::size_t>(segments) + 1u) * 2u);
    mesh.indices.reserve(static_cast<std::size_t>(segments) * 6u);
    for (std::uint32_t i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segments);
        const float radians = glm::two_pi<float>() * t;
        const float c = std::cos(radians);
        const float s = std::sin(radians);
        mesh.vertices.push_back(vertex(c * innerRadius, s * innerRadius, t, 0.f, innerAlpha));
        mesh.vertices.push_back(vertex(c * outerRadius, s * outerRadius, t, 1.f, outerAlpha));
        if (i == 0) continue;
        const std::uint32_t inner0 = (i - 1u) * 2u;
        const std::uint32_t outer0 = inner0 + 1u;
        const std::uint32_t inner1 = i * 2u;
        const std::uint32_t outer1 = inner1 + 1u;
        mesh.indices.insert(mesh.indices.end(),
                            {inner0, outer0, inner1, inner1, outer0, outer1});
    }
}

}  // namespace

AreaIndicatorBuildResult buildAreaIndicator(const AreaIndicatorConfig& config) {
    AreaIndicatorBuildResult result;
    const bool commonValid = config.segments >= 3u && std::isfinite(config.innerAlpha) &&
                             std::isfinite(config.outerAlpha);
    if (!commonValid) return result;

    switch (config.shape) {
        case AreaIndicatorShape::Circle:
            if (!std::isfinite(config.radius) || config.radius <= 0.f) return result;
            appendFan(result.mesh, config.radius, 0.f, glm::two_pi<float>(),
                      config.segments, config.innerAlpha, config.outerAlpha);
            break;
        case AreaIndicatorShape::Ring:
            if (!std::isfinite(config.radius) || !std::isfinite(config.innerRadius) ||
                config.radius <= 0.f || config.innerRadius < 0.f ||
                config.innerRadius >= config.radius)
                return result;
            appendRing(result.mesh, config.innerRadius, config.radius, config.segments,
                       config.innerAlpha, config.outerAlpha);
            break;
        case AreaIndicatorShape::Sector:
            if (!std::isfinite(config.radius) || !std::isfinite(config.sectorAngleRadians) ||
                config.radius <= 0.f || config.sectorAngleRadians <= 0.f ||
                config.sectorAngleRadians > glm::two_pi<float>())
                return result;
            appendFan(result.mesh, config.radius, -config.sectorAngleRadians * 0.5f,
                      config.sectorAngleRadians, config.segments,
                      config.innerAlpha, config.outerAlpha);
            break;
        case AreaIndicatorShape::Rectangle: {
            if (!std::isfinite(config.width) || !std::isfinite(config.length) ||
                config.width <= 0.f || config.length <= 0.f)
                return result;
            const float halfWidth = config.width * 0.5f;
            result.mesh.vertices = {
                vertex(-halfWidth, 0.f, 0.f, 0.f, config.innerAlpha),
                vertex(halfWidth, 0.f, 1.f, 0.f, config.innerAlpha),
                vertex(-halfWidth, config.length, 0.f, 1.f, config.outerAlpha),
                vertex(halfWidth, config.length, 1.f, 1.f, config.outerAlpha),
            };
            result.mesh.indices = {0, 1, 2, 2, 1, 3};
            break;
        }
    }
    result.status = AreaIndicatorBuildStatus::Built;
    return result;
}

}  // namespace eve::stylize
