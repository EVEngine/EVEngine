#include "fluids/VolumeFluidSimplexQuery.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/quaternion.hpp>

namespace eve::fluids::detail {
namespace {
bool finite(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool finite(glm::vec4 v) { return finite(glm::vec3(v)) && std::isfinite(v.w); }
bool inRange(float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; }
bool validFilter(unsigned filter) { return (filter & 0xffffu) != 0 && (filter >> 16u) != 0; }
bool filtersMatch(unsigned a, unsigned b) {
    return ((a & 0xffffu) & (b >> 16u)) != 0 && ((b & 0xffffu) & (a >> 16u)) != 0;
}
struct SimplexPoint {
    glm::vec3 point{0.f};
    glm::vec4 bary{1.f, 0.f, 0.f, 0.f};
};

SimplexPoint closestEdge(glm::vec3 a, glm::vec3 b, glm::vec3 p) {
    const auto  edge        = b - a;
    const float denominator = glm::dot(edge, edge);
    const float t           = denominator > 1e-12f ? std::clamp(glm::dot(p - a, edge) / denominator, 0.f, 1.f) : 0.f;
    return {a + edge * t, {1.f - t, t, 0.f, 0.f}};
}
SimplexPoint closestTriangle(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 p) {
    const auto  ab = b - a, ac = c - a, ap = p - a;
    const float d1 = glm::dot(ab, ap), d2 = glm::dot(ac, ap);
    if (d1 <= 0.f && d2 <= 0.f) return {a, {1, 0, 0, 0}};
    const auto  bp = p - b;
    const float d3 = glm::dot(ab, bp), d4 = glm::dot(ac, bp);
    if (d3 >= 0.f && d4 <= d3) return {b, {0, 1, 0, 0}};
    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f) {
        const float v = d1 / (d1 - d3);
        return {a + ab * v, {1 - v, v, 0, 0}};
    }
    const auto  cp = p - c;
    const float d5 = glm::dot(ab, cp), d6 = glm::dot(ac, cp);
    if (d6 >= 0.f && d5 <= d6) return {c, {0, 0, 1, 0}};
    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f) {
        const float w = d2 / (d2 - d6);
        return {a + ac * w, {1 - w, 0, w, 0}};
    }
    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.f && (d4 - d3) >= 0.f && (d5 - d6) >= 0.f) {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        return {b + (c - b) * w, {0, 1 - w, w, 0}};
    }
    const float sum = va + vb + vc;
    if (std::abs(sum) <= 1e-12f) {
        auto        first = closestEdge(a, b, p), second = closestEdge(b, c, p), third = closestEdge(c, a, p);
        const float da = glm::dot(first.point - p, first.point - p), db = glm::dot(second.point - p, second.point - p),
                    dc = glm::dot(third.point - p, third.point - p);
        if (da <= db && da <= dc) return first;
        if (db <= dc) return {second.point, {0, second.bary.x, second.bary.y, 0}};
        return {third.point, {third.bary.y, 0, third.bary.x, 0}};
    }
    const float inverse = 1.f / sum, v = vb * inverse, w = vc * inverse;
    return {a + ab * v + ac * w, {1.f - v - w, v, w, 0}};
}
SimplexPoint closestSimplex(const VolumeFluidSimplex& simplex, std::span<const VolumeFluidParticle> particles,
                            glm::vec3 point) {
    const auto a = particles[simplex.particleIndices[0]].position;
    if (simplex.size == 1) return {a, {1, 0, 0, 0}};
    const auto b = particles[simplex.particleIndices[1]].position;
    if (simplex.size == 2) return closestEdge(a, b, point);
    return closestTriangle(a, b, particles[simplex.particleIndices[2]].position, point);
}
float radiusAlong(const VolumeFluidSimplex& simplex, std::span<const VolumeFluidParticle> particles, glm::vec4 bary,
                  glm::vec3 normal) {
    float radius = 0.f;
    for (unsigned i = 0; i < simplex.size; ++i) {
        const auto& particle = particles[simplex.particleIndices[i]];
        const auto  q =
            glm::quat(particle.orientation.w, particle.orientation.x, particle.orientation.y, particle.orientation.z);
        radius += bary[i] / glm::length((glm::conjugate(q) * normal) / particle.radii);
    }
    return radius;
}
glm::vec3 boxPoint(const VolumeFluidQueryShape& query, glm::vec3 world, glm::vec3& normal) {
    const auto q    = glm::normalize(glm::quat(query.rotation.w, query.rotation.x, query.rotation.y, query.rotation.z));
    const auto half = query.size * .5f + glm::vec3(query.contactOffset),
               local     = glm::conjugate(q) * (world - query.center);
    auto       projected = glm::clamp(local, -half, half);
    const auto outside   = glm::abs(local) - half;
    if (glm::all(glm::lessThanEqual(outside, glm::vec3(0.f)))) {
        const auto gap  = half - glm::abs(local);
        int        axis = 0;
        if (gap.y < gap.x) axis = 1;
        if (gap.z < gap[axis]) axis = 2;
        projected[axis] = local[axis] >= 0.f ? half[axis] : -half[axis];
    }
    const auto  point = query.center + q * projected, delta = world - point;
    const float length = glm::length(delta);
    normal             = length > 1e-7f ? delta / length : q * glm::vec3(0, 1, 0);
    return point;
}
VolumeFluidSimplex implicitPoint(unsigned index) { return {{index, 0, 0}, 1}; }
bool               selected(const VolumeFluidSimplex& simplex, std::span<const VolumeFluidParticle> particles,
                            const VolumeFluidQueryShape& query) {
    for (unsigned i = 0; i < simplex.size; ++i) {
        const auto& p = particles[simplex.particleIndices[i]];
        if ((query.phaseMask & (1u << unsigned(p.material.phase))) != 0 &&
            filtersMatch(query.collisionFilter, p.collisionFilter))
            return true;
    }
    return false;
}
VolumeFluidSimplexHit evaluate(const VolumeFluidSimplex& simplex, unsigned simplexIndex, unsigned queryIndex,
                               std::span<const VolumeFluidParticle> particles, const VolumeFluidQueryShape& query) {
    SimplexPoint convex;
    glm::vec3    queryPoint, normal;
    if (query.type == VolumeFluidQueryType::Sphere) {
        convex             = closestSimplex(simplex, particles, query.center);
        const auto  delta  = convex.point - query.center;
        const float length = glm::length(delta);
        normal             = length > 1e-7f ? delta / length : glm::vec3(0, 1, 0);
        queryPoint         = query.center + normal * (query.size.x + query.contactOffset);
    } else if (query.type == VolumeFluidQueryType::Box) {
        convex.bary  = glm::vec4(1.f / float(simplex.size));
        convex.point = glm::vec3(0.f);
        for (unsigned i = 0; i < simplex.size; ++i)
            convex.point += particles[simplex.particleIndices[i]].position * convex.bary[i];
        for (int iteration = 0; iteration < 8; ++iteration) {
            queryPoint = boxPoint(query, convex.point, normal);
            convex     = closestSimplex(simplex, particles, queryPoint);
        }
        queryPoint = boxPoint(query, convex.point, normal);
    } else {
        const auto  segment   = query.size - query.center;
        const float length    = glm::length(segment);
        const auto  direction = segment / length;
        float       along     = .5f * length;
        queryPoint            = query.center + direction * along;
        for (int iteration = 0; iteration < 8; ++iteration) {
            convex     = closestSimplex(simplex, particles, queryPoint);
            along      = std::clamp(glm::dot(convex.point - query.center, direction), 0.f, length);
            queryPoint = query.center + direction * along;
        }
        const auto  delta      = convex.point - queryPoint;
        const float separation = glm::length(delta);
        normal                 = separation > 1e-7f ? delta / separation : -direction;
        queryPoint += normal * query.contactOffset;
    }
    const float centerDistance = glm::dot(convex.point - queryPoint, normal);
    return {convex.bary,  queryPoint, normal, centerDistance - radiusAlong(simplex, particles, convex.bary, normal),
            simplexIndex, queryIndex};
}
}  // namespace

Result<std::vector<VolumeFluidSimplexHit>> querySimplexes(std::span<const VolumeFluidParticle>   particles,
                                                          std::span<const VolumeFluidSimplex>    simplexes,
                                                          std::span<const VolumeFluidQueryShape> queries,
                                                          unsigned                               maxHitsPerQuery) {
    using Output    = Result<std::vector<VolumeFluidSimplexHit>>;
    const auto fail = [](const char* message) {
        return Output::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, message, "fluids.volume.querySimplexes"));
    };
    const size_t simplexCount = simplexes.empty() ? particles.size() : simplexes.size();
    if (queries.size() > 256 || maxHitsPerQuery == 0 || maxHitsPerQuery > 4096)
        return fail("Invalid query count or hit limit");
    if (simplexCount > 65536 || (!queries.empty() && simplexCount > 4000000u / queries.size()))
        return fail("Simplex query exceeds candidate budget");
    for (const auto& query : queries) {
        if (unsigned(query.type) > unsigned(VolumeFluidQueryType::Ray) || !finite(query.center) ||
            !finite(query.size) || !inRange(query.contactOffset, 0.f, 10000.f) ||
            !inRange(query.maxDistance, 0.f, 10000.f) || query.phaseMask == 0 || (query.phaseMask & ~0x0fu) != 0 ||
            !validFilter(query.collisionFilter))
            return fail("Invalid simplex query");
        if (query.type == VolumeFluidQueryType::Sphere &&
            (!inRange(query.size.x, 0.f, 10000.f) || query.size.y != 0 || query.size.z != 0))
            return fail("Invalid sphere query");
        if (query.type == VolumeFluidQueryType::Box &&
            (glm::any(glm::lessThan(query.size, glm::vec3(0))) ||
             glm::any(glm::greaterThan(query.size, glm::vec3(20000))) || !finite(query.rotation) ||
             std::abs(glm::dot(query.rotation, query.rotation) - 1.f) > .001f))
            return fail("Invalid box query");
        if (query.type == VolumeFluidQueryType::Ray && glm::length(query.size - query.center) <= 1e-7f)
            return fail("Invalid ray query");
    }
    struct Farther {
        bool operator()(const VolumeFluidSimplexHit& a, const VolumeFluidSimplexHit& b) const {
            return a.distance != b.distance ? a.distance < b.distance : a.simplexIndex < b.simplexIndex;
        }
    };
    std::vector<VolumeFluidSimplexHit> output;
    output.reserve(std::min<size_t>(queries.size() * size_t(maxHitsPerQuery), 4000000));
    std::vector<VolumeFluidSimplexHit> heap;
    heap.reserve(maxHitsPerQuery);
    const Farther farther;
    for (size_t qi = 0; qi < queries.size(); ++qi) {
        heap.clear();
        for (size_t si = 0; si < simplexCount; ++si) {
            const auto  implicit = implicitPoint(unsigned(si));
            const auto& simplex  = simplexes.empty() ? implicit : simplexes[si];
            if (!selected(simplex, particles, queries[qi])) continue;
            auto hit = evaluate(simplex, unsigned(si), unsigned(qi), particles, queries[qi]);
            if (hit.distance > queries[qi].maxDistance) continue;
            if (heap.size() < maxHitsPerQuery) {
                heap.push_back(hit);
                std::push_heap(heap.begin(), heap.end(), farther);
            } else if (hit.distance < heap.front().distance ||
                       (hit.distance == heap.front().distance && si < heap.front().simplexIndex)) {
                std::pop_heap(heap.begin(), heap.end(), farther);
                heap.back() = hit;
                std::push_heap(heap.begin(), heap.end(), farther);
            }
        }
        std::sort_heap(heap.begin(), heap.end(), farther);
        output.insert(output.end(), heap.begin(), heap.end());
    }
    return Output::success(std::move(output));
}
}  // namespace eve::fluids::detail
