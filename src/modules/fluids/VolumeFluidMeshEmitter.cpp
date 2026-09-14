#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <limits>
#include "fluids/VolumeFluidEmitter.h"

namespace eve::fluids {
namespace {
bool finiteMeshVector(glm::vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
bool intersects(glm::dvec3 a, glm::dvec3 b, glm::dvec3 c, glm::dvec3 center, double half) {
    a -= center;
    b -= center;
    c -= center;
    const glm::dvec3 edges[]   = {b - a, c - b, a - c};
    const glm::dvec3 basis[]   = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const auto       separated = [&](glm::dvec3 axis) {
        const double radius = half * (std::abs(axis.x) + std::abs(axis.y) + std::abs(axis.z));
        const double pa = glm::dot(a, axis), pb = glm::dot(b, axis), pc = glm::dot(c, axis);
        return std::min({pa, pb, pc}) > radius + 1e-12 || std::max({pa, pb, pc}) < -radius - 1e-12;
    };
    for (const auto axis : basis)
        if (separated(axis)) return false;
    if (separated(glm::cross(edges[0], edges[1]))) return false;
    for (const auto edge : edges)
        for (const auto axis : basis)
            if (separated(glm::cross(edge, axis))) return false;
    return true;
}
}  // namespace
Result<std::vector<VolumeFluidDistributionPoint>> buildVolumeFluidMeshDistribution(std::span<const glm::vec3> vertices,
                                                                                   std::span<const uint32_t>  indices,
                                                                                   glm::vec3 scale, float spacing) {
    using Output    = Result<std::vector<VolumeFluidDistributionPoint>>;
    const auto fail = [](const char* text) {
        return Output::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, text, "fluids.volume.meshDistribution"));
    };
    if (vertices.empty() || vertices.size() > 1000000 || indices.empty() || indices.size() % 3 != 0 ||
        indices.size() / 3 > 65536 || !finiteMeshVector(scale) || scale.x == 0 || scale.y == 0 || scale.z == 0 ||
        !std::isfinite(spacing) || spacing < .001f || spacing > 10000.f)
        return fail("Invalid mesh dimensions or spacing");
    std::vector<glm::dvec3> points;
    points.reserve(vertices.size());
    glm::dvec3 minimum(std::numeric_limits<double>::max()), maximum(-std::numeric_limits<double>::max());
    for (const auto vertex : vertices) {
        if (!finiteMeshVector(vertex)) return fail("Nonfinite mesh vertex");
        const auto p = glm::dvec3(vertex) * glm::dvec3(scale);
        if (glm::any(glm::greaterThan(glm::abs(p), glm::dvec3(10000.0))))
            return fail("Scaled mesh exceeds world-size limit");
        minimum = glm::min(minimum, p);
        maximum = glm::max(maximum, p);
        points.push_back(p);
    }
    for (const auto index : indices)
        if (index >= points.size()) return fail("Mesh index out of range");
    const auto       extent     = maximum - minimum;
    const double     size       = std::max(double(spacing), std::max({extent.x, extent.y, extent.z}) / 32.0);
    const glm::ivec3 dimensions = glm::ivec3(glm::ceil(extent / size)) + glm::ivec3(4);
    const auto       origin     = minimum - glm::dvec3(1.5 * size);
    const auto       index      = [&](glm::ivec3 p) {
        return size_t(p.x) + size_t(dimensions.x) * (size_t(p.y) + size_t(dimensions.y) * size_t(p.z));
    };
    std::vector<uint8_t> cells(size_t(dimensions.x) * size_t(dimensions.y) * size_t(dimensions.z), 0);
    uint64_t             work = 0;
    for (size_t t = 0; t < indices.size(); t += 3) {
        const auto a = points[indices[t]], b = points[indices[t + 1]], c = points[indices[t + 2]];
        if (glm::length(glm::cross(b - a, c - a)) < 1e-12) return fail("Degenerate mesh triangle");
        const auto lo = glm::ivec3(glm::floor((glm::min(a, glm::min(b, c)) - origin) / size));
        const auto hi = glm::ivec3(glm::floor((glm::max(a, glm::max(b, c)) - origin) / size));
        for (int z = lo.z; z <= hi.z; ++z)
            for (int y = lo.y; y <= hi.y; ++y)
                for (int x = lo.x; x <= hi.x; ++x) {
                    if (++work > 4000000) return fail("Mesh voxelization exceeds 4M intersection checks");
                    const glm::ivec3 p(x, y, z);
                    if (cells[index(p)] == 1) continue;
                    const auto center = origin + (glm::dvec3(p) + .5) * size;
                    if (intersects(a, b, c, center, size * .5)) cells[index(p)] = 1;
                }
    }
    std::vector<glm::ivec3> queue;
    queue.reserve(cells.size());
    queue.emplace_back(0);
    cells[0]                      = 2;
    const glm::ivec3 directions[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (size_t i = 0; i < queue.size(); ++i)
        for (const auto direction : directions) {
            const auto p = queue[i] + direction;
            if (glm::any(glm::lessThan(p, glm::ivec3(0))) || glm::any(glm::greaterThanEqual(p, dimensions))) continue;
            if (cells[index(p)] == 0) {
                cells[index(p)] = 2;
                queue.push_back(p);
            }
        }
    std::vector<VolumeFluidDistributionPoint> result;
    for (int z = 0; z < dimensions.z; ++z)
        for (int y = 0; y < dimensions.y; ++y)
            for (int x = 0; x < dimensions.x; ++x) {
                const glm::ivec3 p(x, y, z);
                if (cells[index(p)] == 2) continue;
                if (result.size() == 4096) return fail("Mesh distribution exceeds 4096 points; increase spacing");
                result.push_back({glm::vec3(origin + (glm::dvec3(p) + .5) * size), glm::vec4(1.f)});
            }
    return Output::success(std::move(result));
}
}  // namespace eve::fluids
