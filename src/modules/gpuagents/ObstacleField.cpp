#include "gpuagents/ObstacleField.h"

#include "common/Assert.h"

#include <algorithm>
#include <cmath>

namespace eve::gpuagents {
namespace {

float lengthSafe(const glm::vec3& v, float fallback = 1.f) {
    const float len = glm::length(v);
    return len > 1e-6f ? len : fallback;
}

glm::vec3 normalizeSafe(const glm::vec3& v, const glm::vec3& fallback = glm::vec3(0.f, 1.f, 0.f)) {
    const float len = glm::length(v);
    return len > 1e-6f ? v / len : fallback;
}

}  // namespace

void ObstacleField::clear() {
    dims = {0, 0, 0};
    distances.clear();
    dynamicSources.clear();
}

void ObstacleField::bakeEmpty(const glm::vec3& originMin, const glm::ivec3& d, float cs, float fillDistance) {
    EV_PARAM_CHECK(d.x > 0);
    EV_PARAM_CHECK(d.y > 0);
    EV_PARAM_CHECK(d.z > 0);
    EV_PARAM_CHECK(cs > 0.f);
    origin   = originMin;
    cellSize = cs;
    dims     = d;
    distances.assign(static_cast<size_t>(d.x) * static_cast<size_t>(d.y) * static_cast<size_t>(d.z), fillDistance);
}

void ObstacleField::carveSphere(const glm::vec3& center, float radius) {
    EV_PARAM_CHECK(hasStaticGrid());
    EV_PARAM_CHECK(radius >= 0.f);
    for (int z = 0; z < dims.z; ++z) {
        for (int y = 0; y < dims.y; ++y) {
            for (int x = 0; x < dims.x; ++x) {
                const glm::vec3 p = origin + cellSize * (glm::vec3(x, y, z) + 0.5f);
                const float     d = glm::length(p - center) - radius;
                const size_t    i = static_cast<size_t>(x + dims.x * (y + dims.y * z));
                distances[i]      = std::min(distances[i], d);
            }
        }
    }
}

void ObstacleField::carveBox(const glm::vec3& minCorner, const glm::vec3& maxCorner) {
    EV_PARAM_CHECK(hasStaticGrid());
    for (int z = 0; z < dims.z; ++z) {
        for (int y = 0; y < dims.y; ++y) {
            for (int x = 0; x < dims.x; ++x) {
                const glm::vec3 p = origin + cellSize * (glm::vec3(x, y, z) + 0.5f);
                const glm::vec3 q = glm::max(minCorner - p, p - maxCorner);
                const float     d =
                    glm::length(glm::max(q, glm::vec3(0.f))) + std::min(std::max({q.x, q.y, q.z}), 0.f);
                const size_t i = static_cast<size_t>(x + dims.x * (y + dims.y * z));
                distances[i]   = std::min(distances[i], d);
            }
        }
    }
}

void ObstacleField::setDynamicSources(std::vector<DynamicObstacleSource> sources) {
    dynamicSources = std::move(sources);
}

float ObstacleField::sampleStatic(const glm::vec3& p) const {
    if (!hasStaticGrid()) return 1e6f;
    const glm::vec3 local = (p - origin) / cellSize - 0.5f;
    const glm::vec3 f     = glm::floor(local);
    const glm::vec3 t     = local - f;
    const auto idx = [&](int x, int y, int z) -> float {
        x = std::clamp(x, 0, dims.x - 1);
        y = std::clamp(y, 0, dims.y - 1);
        z = std::clamp(z, 0, dims.z - 1);
        return distances[static_cast<size_t>(x + dims.x * (y + dims.y * z))];
    };
    const int x0 = static_cast<int>(f.x);
    const int y0 = static_cast<int>(f.y);
    const int z0 = static_cast<int>(f.z);
    const float c000 = idx(x0, y0, z0);
    const float c100 = idx(x0 + 1, y0, z0);
    const float c010 = idx(x0, y0 + 1, z0);
    const float c110 = idx(x0 + 1, y0 + 1, z0);
    const float c001 = idx(x0, y0, z0 + 1);
    const float c101 = idx(x0 + 1, y0, z0 + 1);
    const float c011 = idx(x0, y0 + 1, z0 + 1);
    const float c111 = idx(x0 + 1, y0 + 1, z0 + 1);
    const float c00  = c000 * (1.f - t.x) + c100 * t.x;
    const float c10  = c010 * (1.f - t.x) + c110 * t.x;
    const float c01  = c001 * (1.f - t.x) + c101 * t.x;
    const float c11  = c011 * (1.f - t.x) + c111 * t.x;
    const float c0   = c00 * (1.f - t.y) + c10 * t.y;
    const float c1   = c01 * (1.f - t.y) + c11 * t.y;
    return c0 * (1.f - t.z) + c1 * t.z;
}

glm::vec3 ObstacleField::gradientStatic(const glm::vec3& p) const {
    const float e = cellSize * 0.5f;
    const float dx = sampleStatic(p + glm::vec3(e, 0.f, 0.f)) - sampleStatic(p - glm::vec3(e, 0.f, 0.f));
    const float dy = sampleStatic(p + glm::vec3(0.f, e, 0.f)) - sampleStatic(p - glm::vec3(0.f, e, 0.f));
    const float dz = sampleStatic(p + glm::vec3(0.f, 0.f, e)) - sampleStatic(p - glm::vec3(0.f, 0.f, e));
    return normalizeSafe(glm::vec3(dx, dy, dz));
}

ObstacleSample ObstacleField::sampleDynamic(const glm::vec3& p) const {
    ObstacleSample best;
    best.distance = 1e6f;
    for (const auto& s : dynamicSources) {
        const glm::vec3 d = p - s.center;
        const float     len = lengthSafe(d);
        const float     dist = len - s.radius;
        if (dist < best.distance) {
            best.distance = dist;
            best.gradient = normalizeSafe(d);
        }
    }
    return best;
}

ObstacleSample ObstacleField::sample(const glm::vec3& p) const {
    ObstacleSample out;
    out.distance = sampleStatic(p);
    out.gradient = gradientStatic(p);
    const ObstacleSample dyn = sampleDynamic(p);
    if (dyn.distance < out.distance) {
        out = dyn;
    }
    return out;
}

void ObstacleField::resolve(glm::vec3& position, glm::vec3& velocity, float agentRadius, float predictTime) const {
    const ObstacleSample now  = sample(position);
    const ObstacleSample pred = sample(position + velocity * std::max(predictTime, 0.f));
    const ObstacleSample& hit = pred.distance < now.distance ? pred : now;
    if (hit.distance >= agentRadius) return;

    const glm::vec3 n   = normalizeSafe(hit.gradient);
    const float     pen = agentRadius - hit.distance;
    position += n * pen;

    const float vn = glm::dot(velocity, n);
    if (vn < 0.f) {
        velocity -= n * vn;  // keep tangential, cancel inward normal
    }
}

}  // namespace eve::gpuagents
