#include <algorithm>
#include <cmath>
#include <exception>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include "fluids/FluidSurfaceRenderer.h"
#include "fluids/VolumeFluidDiffuse.h"
#include "fluids/VolumeFluidEmitter.h"
#include "fluids/VolumeFluidShadowCaster.h"
#include "fluids/VolumeFluidSimplexQuery.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"

// VolumeFluidFoamSurface.cpp
namespace eve::fluids {
Result<void> FluidSurfaceRenderer::configureFoam(bool enabled, int downsample) {
    if (downsample < 1 || downsample > 4)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Foam downsample must be in [1,4]", "fluids.surface.configureFoam"));
    foamEnabled_    = enabled;
    foamDownsample_ = downsample;
    return Result<void>::success();
}

Result<void> FluidSurfaceRenderer::compositeDiffuse(const VolumeFluidDiffuse& pool, float radius, float opacity,
                                                    float fadeSeconds) {
    const auto fail = [](const char* message) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, message, "fluids.surface.diffuse"));
    };
    if (!std::isfinite(radius) || radius <= 0.f || radius > 1.f || !std::isfinite(opacity) || opacity < 0.f ||
        opacity > 1.f || !std::isfinite(fadeSeconds) || fadeSeconds <= 0.f || fadeSeconds > 86400.f)
        return fail("Invalid diffuse rendering settings");
    if (!foamEnabled_ || pool.particleCount() == 0 || opacity == 0.f) return Result<void>::success();
    const int width = params_.width, height = params_.height;
    if (width <= 0 || height <= 0 || depth_.size() != size_t(width) * size_t(height) ||
        color_.size() != depth_.size() * 4)
        return fail("Invalid reconstructed frame size");
    const int   renderWidth  = std::max(1, (width + foamDownsample_ - 1) / foamDownsample_);
    const int   renderHeight = std::max(1, (height + foamDownsample_ - 1) / foamDownsample_);
    const float tangent      = std::tan(glm::radians(params_.fovYDeg) * .5f);
    if (!std::isfinite(tangent) || tangent <= 0.f) return fail("Invalid diffuse camera projection");
    const auto view = glm::lookAtRH(params_.eye, params_.target, params_.up);
    pool.copyRenderData(diffuseParticles_);
    diffuseSplats_.clear();
    diffuseSplats_.reserve(diffuseParticles_.size());
    uint64_t visits = 0;
    for (const auto p : diffuseParticles_) {
        const auto v = view * glm::vec4(glm::vec3(p), 1.f);
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z))
            return fail("Invalid diffuse camera transform");
        DiffuseSplat s;
        s.z = -v.z;
        if (s.z <= params_.nearZ || s.z >= params_.farZ) continue;
        s.r = std::max(1.f, radius * float(renderHeight) / (2.f * s.z * tangent));
        s.x = (.5f + .5f * v.x / (s.z * tangent * float(renderWidth) / float(renderHeight))) * float(renderWidth);
        s.y = (.5f - .5f * v.y / (s.z * tangent)) * float(renderHeight);
        if (!std::isfinite(s.r) || !std::isfinite(s.x) || !std::isfinite(s.y))
            return fail("Invalid diffuse projection");
        if (s.x + s.r < 0.f || s.y + s.r < 0.f || s.x - s.r >= float(renderWidth) || s.y - s.r >= float(renderHeight))
            continue;
        // Clamp in floating point before converting extreme off-screen coordinates.
        s.x0    = int(std::floor(std::clamp(s.x - s.r, 0.f, float(renderWidth - 1))));
        s.x1    = int(std::ceil(std::clamp(s.x + s.r, 0.f, float(renderWidth - 1))));
        s.y0    = int(std::floor(std::clamp(s.y - s.r, 0.f, float(renderHeight - 1))));
        s.y1    = int(std::ceil(std::clamp(s.y + s.r, 0.f, float(renderHeight - 1))));
        s.alpha = opacity * std::min(1.f, p.w / fadeSeconds);
        visits += uint64_t(s.x1 - s.x0 + 1) * uint64_t(s.y1 - s.y0 + 1);
        if (visits > 4000000) return fail("Diffuse rendering exceeds 4M pixel visits");
        diffuseSplats_.push_back(s);
    }
    if (foamDownsample_ > 1) foamCoverageScratch_.assign(size_t(renderWidth) * size_t(renderHeight), 0.f);
    for (const auto& s : diffuseSplats_) {
        for (int y = s.y0; y <= s.y1; ++y)
            for (int x = s.x0; x <= s.x1; ++x) {
                const float dx = (float(x) + .5f - s.x) / s.r, dy = (float(y) + .5f - s.y) / s.r;
                const float q = 1.f - dx * dx - dy * dy;
                if (q <= 0.f) continue;
                const size_t reducedAt  = size_t(y) * size_t(renderWidth) + size_t(x);
                const int    fullX      = std::min(width - 1, x * width / renderWidth);
                const int    fullY      = std::min(height - 1, y * height / renderHeight);
                const size_t at         = size_t(fullY) * size_t(width) + size_t(fullX);
                const float  front      = s.z - radius * std::sqrt(q);
                const float  visibility = std::clamp(1.f + (depth_[at] - front) / radius, 0.f, 1.f);
                const float  alpha      = s.alpha * q * q * visibility;
                if (alpha <= 0.f) continue;
                if (foamDownsample_ > 1) {
                    foamCoverageScratch_[reducedAt] = alpha + foamCoverageScratch_[reducedAt] * (1.f - alpha);
                    continue;
                }
                const float oldAlpha = float(color_[at * 4 + 3]) / 255.f;
                const float retained = oldAlpha * (1.f - alpha), combined = alpha + retained;
                for (size_t c = 0; c < 3; ++c) {
                    const float color  = (alpha + float(color_[at * 4 + c]) / 255.f * retained) / combined;
                    color_[at * 4 + c] = uint8_t(std::clamp(color, 0.f, 1.f) * 255.f + .5f);
                }
                color_[at * 4 + 3] = uint8_t(std::clamp(combined, 0.f, 1.f) * 255.f + .5f);
            }
    }
    if (foamDownsample_ > 1) {
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const int   sx    = std::min(renderWidth - 1, x * renderWidth / width);
                const int   sy    = std::min(renderHeight - 1, y * renderHeight / height);
                const float alpha = foamCoverageScratch_[size_t(sy) * size_t(renderWidth) + size_t(sx)];
                if (alpha <= 0.f) continue;
                const size_t at       = size_t(y) * size_t(width) + size_t(x);
                const float  oldAlpha = float(color_[at * 4u + 3u]) / 255.f;
                const float  retained = oldAlpha * (1.f - alpha), combined = alpha + retained;
                for (size_t c = 0; c < 3; ++c) {
                    const float value   = (alpha + float(color_[at * 4u + c]) / 255.f * retained) / combined;
                    color_[at * 4u + c] = uint8_t(std::clamp(value, 0.f, 1.f) * 255.f + .5f);
                }
                color_[at * 4u + 3u] = uint8_t(std::clamp(combined, 0.f, 1.f) * 255.f + .5f);
            }
    }
    residentColorCurrent_ = false;
    return Result<void>::success();
}
}  // namespace eve::fluids

// VolumeFluidMeshEmitter.cpp
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

// VolumeFluidShadowCaster.cpp
namespace eve::fluids {
namespace {
Status failure(const char* message) {
    return Status::failure(Diagnostic::error(DiagnosticCode::Failed, message, "fluids.volume.shadowCaster"));
}
}  // namespace

VolumeFluidShadowCaster::VolumeFluidShadowCaster(graphics::Graphics& graphics) : graphics_(&graphics) {}

VolumeFluidShadowCaster::~VolumeFluidShadowCaster() {
    graphics::RenderSystem3D::removeShadowExtraDrawer(drawerToken_);
    if (graphics_ && mesh_ && graphics_->releaseMesh(mesh_)) delete mesh_;
}

Result<void> VolumeFluidShadowCaster::update(const VolumeFluid& fluid, unsigned actorGroup, float radiusScale,
                                             float alpha, unsigned maxParticles) {
    std::vector<VolumeFluidParticleInstance> candidate;
    candidate.swap(instances_);
    auto copied = fluid.copyParticleImpostors(actorGroup, radiusScale, glm::vec4(1.f), alpha, maxParticles, candidate);
    if (!copied) {
        candidate.swap(instances_);
        return Result<void>::failure(copied.status());
    }
    const size_t count = candidate.size();
    positions_.resize(count * 18u);
    normals_.resize(count * 18u);
    indices_.resize(count * 24u);
    static constexpr glm::vec3 directions[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    static constexpr uint32_t  faces[24]     = {0, 2, 4, 2, 1, 4, 1, 3, 4, 3, 0, 4, 2, 0, 5, 1, 2, 5, 3, 1, 5, 0, 3, 5};
    for (size_t i = 0; i < count; ++i) {
        const auto q = glm::quat(candidate[i].orientation.w, candidate[i].orientation.x, candidate[i].orientation.y,
                                 candidate[i].orientation.z);
        for (size_t v = 0; v < 6; ++v) {
            const glm::vec3 local  = directions[v] * candidate[i].scale;
            const glm::vec3 normal = q * directions[v];
            const glm::vec3 point  = candidate[i].position + q * local;
            for (size_t c = 0; c < 3; ++c) {
                const auto component            = static_cast<glm::vec3::length_type>(c);
                positions_[(i * 6 + v) * 3 + c] = point[component];
                normals_[(i * 6 + v) * 3 + c]   = normal[component];
            }
        }
        for (size_t k = 0; k < 24; ++k) indices_[i * 24 + k] = uint32_t(i * 6 + faces[k]);
    }
    try {
        if (count != 0 && !mesh_)
            mesh_ = graphics_->newMeshFromArrays(positions_.data(), normals_.data(), nullptr, int(count * 6),
                                                 indices_.data(), int(count * 24));
        else if (count != 0 && !graphics_->updateMeshVertices(mesh_, positions_.data(), normals_.data(), nullptr,
                                                              int(count * 6), indices_.data(), int(count * 24)))
            return Result<void>::failure(failure("Particle shadow mesh update failed"));
    } catch (const std::exception&) {
        return Result<void>::failure(failure("Particle shadow mesh creation failed"));
    }
    if (mesh_ && drawerToken_ == 0)
        drawerToken_ = graphics::RenderSystem3D::addShadowExtraDrawer(
            [this](graphics::Graphics& gfx, const glm::mat4& lightVP, const auto&) {
                if (particleCount_ != 0) gfx.drawMeshShadow(mesh_, lightVP);
            });
    instances_.swap(candidate);
    particleCount_ = unsigned(count);
    return Result<void>::success();
}

}  // namespace eve::fluids

// VolumeFluidSimplexQuery.cpp
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
