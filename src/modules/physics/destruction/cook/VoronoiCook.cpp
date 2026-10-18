#include "physics/destruction/cook/VoronoiCook.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <utility>
#include "asset/CanonicalMesh.h"
#include "physics/destruction/FractureRecipe.h"
#include "physics/destruction/cook/AssemblyFractureSettings.h"
namespace eve::physics::destruction_cook {
namespace {
struct Vec {
    double x = 0, y = 0, z = 0;
    Vec    operator+(Vec b) const { return {x + b.x, y + b.y, z + b.z}; }
    Vec    operator-(Vec b) const { return {x - b.x, y - b.y, z - b.z}; }
    Vec    operator*(double s) const { return {x * s, y * s, z * s}; }
};
double dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec    cross(Vec a, Vec b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double length(Vec a) { return std::sqrt(dot(a, a)); }
struct Face {
    std::vector<Vec> points;
    int              neighbor = -1;
};
using Poly = std::vector<Face>;
Diagnostic bad(const char* message) { return Diagnostic::error(DiagnosticCode::InvalidArgument, message, "voronoi"); }
Vec        point(const FractureFragment& f, std::uint32_t i) {
    return {f.positions[3 * i], f.positions[3 * i + 1], f.positions[3 * i + 2]};
}
void uniquePush(std::vector<Vec>& points, Vec p, double eps) {
    for (auto q : points)
        if (length(p - q) <= eps) return;
    points.push_back(p);
}
std::uint32_t vertex(FractureFragment& f, Vec p, double eps) {
    for (std::uint32_t i = 0; i < f.positions.size() / 3; ++i)
        if (length(p - point(f, i)) <= eps) return i;
    const auto result = static_cast<std::uint32_t>(f.positions.size() / 3);
    f.positions.insert(f.positions.end(), {static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(p.z)});
    return result;
}
// Merge coplanar source triangles before clipping. Otherwise triangle diagonals
// create almost-collinear cap vertices and ill-conditioned sliver triangles.
std::vector<Vec> boundary(std::vector<Vec> points, Vec n, double eps) {
    const Vec helper = std::abs(n.x) < .8 ? Vec{1, 0, 0} : Vec{0, 1, 0};
    Vec       u      = cross(helper, n);
    u                = u * (1.0 / length(u));
    const Vec v      = cross(n, u);
    std::sort(points.begin(), points.end(), [&](Vec a, Vec b) {
        const double ax = dot(a, u), bx = dot(b, u);
        return ax == bx ? dot(a, v) < dot(b, v) : ax < bx;
    });
    auto             turn = [&](Vec a, Vec b, Vec c) { return dot(cross(b - a, c - a), n); };
    std::vector<Vec> hull;
    for (auto p : points) {
        while (hull.size() >= 2 && turn(hull[hull.size() - 2], hull.back(), p) <= eps * eps) hull.pop_back();
        hull.push_back(p);
    }
    const auto lower = hull.size();
    for (std::size_t i = points.size() - 1; i > 0; --i) {
        const auto p = points[i - 1];
        while (hull.size() > lower && turn(hull[hull.size() - 2], hull.back(), p) <= eps * eps) hull.pop_back();
        hull.push_back(p);
    }
    if (!hull.empty()) hull.pop_back();
    return hull;
}
Poly sourceFaces(const FractureFragment& source, double eps) {
    struct Group {
        Vec              n;
        double           d;
        std::vector<Vec> points;
    };
    std::vector<Group> groups;
    for (std::size_t t = 0; t < source.indices.size(); t += 3) {
        const Vec a = point(source, source.indices[t]), b = point(source, source.indices[t + 1]),
                  c        = point(source, source.indices[t + 2]);
        Vec n              = cross(b - a, c - a);
        n                  = n * (1.0 / length(n));
        const double d     = dot(n, a);
        auto         found = std::find_if(groups.begin(), groups.end(),
                                          [&](const Group& g) { return dot(g.n, n) > 1 - 1e-10 && std::abs(g.d - d) < eps; });
        if (found == groups.end()) {
            groups.push_back({n, d, {}});
            found = groups.end() - 1;
        }
        uniquePush(found->points, a, eps);
        uniquePush(found->points, b, eps);
        uniquePush(found->points, c, eps);
    }
    Poly faces;
    for (auto& g : groups) faces.push_back({boundary(std::move(g.points), g.n, eps), -1});
    return faces;
}
// Keep n dot x <= d. The new cap points outwards towards the removed half-space.
Poly clip(const Poly& input, Vec n, double d, int neighbor, double eps) {
    Poly             result;
    std::vector<Vec> cap;
    for (const auto& face : input) {
        Face out{{}, face.neighbor};
        for (std::size_t i = 0; i < face.points.size(); ++i) {
            const Vec    a = face.points[i], b = face.points[(i + 1) % face.points.size()];
            const double da = dot(n, a) - d, db = dot(n, b) - d;
            const bool   ina = da <= eps, inb = db <= eps;
            if (ina) uniquePush(out.points, a, eps);
            if (ina != inb) {
                const Vec q = a + (b - a) * (da / (da - db));
                uniquePush(out.points, q, eps);
                uniquePush(cap, q, eps);
            }
            if (std::abs(da) <= eps) uniquePush(cap, a, eps);
        }
        if (out.points.size() >= 3) result.push_back(std::move(out));
    }
    if (cap.size() >= 3) {
        cap = boundary(std::move(cap), n, eps);
        if (cap.size() >= 3) result.push_back({std::move(cap), neighbor});
    }
    return result;
}
struct Rng {
    std::uint64_t state;
    std::uint64_t next() {
        auto z = (state += 0x9e3779b97f4a7c15ULL);
        z      = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z      = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }
    double unit() { return static_cast<double>(next() >> 11) / 9007199254740992.0; }
};
std::uint64_t seed(const FractureRecipe& r) {
    auto h = 0xcbf29ce484222325ULL ^ r.seed;
    for (unsigned char c : r.randomStreamName) {
        h ^= c;
        h *= 0x100000001b3ULL;
    }
    return h;
}
Result<FractureFragment> sourceMesh(const eve::asset::CanonicalMeshData& mesh, Vec& origin, Vec& lo, Vec& hi,
                                    double& eps) {
    if (mesh.positions.size() % 3 || mesh.positions.size() < 12 || mesh.positions.size() > 12288 ||
        mesh.indices.empty() || mesh.indices.size() % 3 || mesh.indices.size() > 384)
        return Result<FractureFragment>::failure(bad("source mesh exceeds convex cook budget or is incomplete"));
    lo = {1e300, 1e300, 1e300};
    hi = {-1e300, -1e300, -1e300};
    for (std::size_t i = 0; i < mesh.positions.size(); i += 3) {
        Vec p{mesh.positions[i], mesh.positions[i + 1], mesh.positions[i + 2]};
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
            return Result<FractureFragment>::failure(bad("nonfinite source mesh"));
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    origin = (lo + hi) * .5;
    lo     = lo - origin;
    hi     = hi - origin;
    eps    = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z}) * 1e-7;
    if (!(eps > 0) || eps > 1000) return Result<FractureFragment>::failure(bad("unsupported source scale"));
    FractureFragment f;
    for (auto i : mesh.indices) {
        if (i >= mesh.positions.size() / 3) return Result<FractureFragment>::failure(bad("invalid triangle index"));
        Vec p{mesh.positions[3 * i], mesh.positions[3 * i + 1], mesh.positions[3 * i + 2]};
        f.indices.push_back(vertex(f, p - origin, eps));
    }
    f.materials.resize(f.indices.size() / 3, 0);
    double volume = 0;
    for (std::size_t t = 0; t < f.indices.size(); t += 3)
        volume += dot(point(f, f.indices[t]), cross(point(f, f.indices[t + 1]), point(f, f.indices[t + 2]))) / 6;
    if (volume < 0)
        for (std::size_t t = 0; t < f.indices.size(); t += 3) std::swap(f.indices[t + 1], f.indices[t + 2]);
    FractureGeometry check;
    check.fragments.push_back(f);
    auto valid = check.validate();
    if (!valid) return Result<FractureFragment>::failure(valid.status());
    return Result<FractureFragment>::success(std::move(f));
}
eve::asset::CanonicalMeshData box(float x, float y, float z) {
    eve::asset::CanonicalMeshData m;
    m.positions = {-x, -y, -z, x, -y, -z, x, y, -z, -x, y, -z, -x, -y, z, x, -y, z, x, y, z, -x, y, z};
    m.indices   = {0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
                   3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5};
    return m;
}
}  // namespace
Result<FracturedCollection> cookVoronoi(const eve::asset::CanonicalMeshData& mesh, const FractureRecipe& recipe) {
    auto rv = recipe.validate();
    if (!rv) return Result<FracturedCollection>::failure(rv.status());
    if ((recipe.mode != FractureMode::UniformVoronoi && recipe.mode != FractureMode::ClusteredVoronoi) ||
        recipe.siteCountMax > 64 || recipe.siteCountMax > recipe.maximumBones)
        return Result<FracturedCollection>::failure(
            bad("convex Voronoi cook requires 1..64 sites within maximumBones"));
    Vec    origin, lo, hi;
    double eps    = 0;
    auto   source = sourceMesh(mesh, origin, lo, hi, eps);
    if (!source) return Result<FracturedCollection>::failure(source.status());
    if (std::min({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z}) < recipe.minimumThickness)
        return Result<FracturedCollection>::failure(bad("source is thinner than minimumThickness"));
    Poly      original = sourceFaces(source.value(), eps);
    Rng       rng{seed(recipe)};
    const int count =
        recipe.siteCountMin +
        static_cast<int>(rng.next() % static_cast<std::uint64_t>(recipe.siteCountMax - recipe.siteCountMin + 1));
    auto inside = [&](Vec p) {
        for (const auto& f : original) {
            Vec n = cross(f.points[1] - f.points[0], f.points[2] - f.points[0]);
            if (dot(n, p - f.points[0]) > eps * length(n)) return false;
        }
        return true;
    };
    auto uniform = [&]() {
        return Vec{lo.x + (hi.x - lo.x) * rng.unit(), lo.y + (hi.y - lo.y) * rng.unit(),
                   lo.z + (hi.z - lo.z) * rng.unit()};
    };
    std::vector<Vec> centers, sites;
    std::vector<int> clusters;
    if (recipe.mode == FractureMode::ClusteredVoronoi) {
        for (int c = 0; c < recipe.clusterCount; ++c) {
            bool found = false;
            for (int trial = 0; trial < 10000; ++trial) {
                auto p = uniform();
                if (inside(p)) {
                    centers.push_back(p);
                    found = true;
                    break;
                }
            }
            if (!found) return Result<FracturedCollection>::failure(bad("cannot place cluster center inside source"));
        }
    }
    for (int i = 0; i < count; ++i) {
        bool      found   = false;
        const int cluster = centers.empty() ? 0 : i % recipe.clusterCount;
        for (int trial = 0; trial < 10000; ++trial) {
            Vec p = uniform();
            if (!centers.empty()) {
                auto c = centers[static_cast<std::size_t>(cluster)];
                p = c + Vec{(rng.unit() * 2 - 1) * recipe.clusterRadius, (rng.unit() * 2 - 1) * recipe.clusterRadius,
                            (rng.unit() * 2 - 1) * recipe.clusterRadius};
            }
            if (!inside(p)) continue;
            bool duplicate = false;
            for (auto q : sites)
                if (length(q - p) < eps * 100) {
                    duplicate = true;
                    break;
                }
            if (duplicate) continue;
            sites.push_back(p);
            clusters.push_back(cluster);
            found = true;
            break;
        }
        if (!found)
            return Result<FracturedCollection>::failure(bad("cannot place distinct Voronoi sites inside source"));
    }
    FracturedCollection                   result;
    std::map<std::pair<int, int>, double> contacts;
    for (int i = 0; i < count; ++i) {
        Poly cell = original;
        for (int j = 0; j < count; ++j)
            if (i != j) {
                const Vec    diff = sites[static_cast<std::size_t>(j)] - sites[static_cast<std::size_t>(i)];
                const double len  = length(diff);
                const Vec    n    = diff * (1.0 / len);
                const double d = dot(n, (sites[static_cast<std::size_t>(i)] + sites[static_cast<std::size_t>(j)]) * .5);
                cell           = clip(cell, n, d, j, eps);
                if (cell.empty()) return Result<FracturedCollection>::failure(bad("Voronoi produced empty cell"));
            }
        Vec         center;
        std::size_t points = 0;
        for (const auto& f : cell)
            for (auto p : f.points) {
                center = center + p;
                ++points;
            }
        center = center * (1.0 / static_cast<double>(points));
        FractureFragment fragment;
        double           volume = 0;
        Vec              extents;
        for (const auto& face : cell) {
            Vec faceCenter;
            for (auto p : face.points) faceCenter = faceCenter + p;
            faceCenter  = faceCenter * (1.0 / static_cast<double>(face.points.size()));
            double area = 0;
            for (std::size_t k = 0; k < face.points.size(); ++k) {
                Vec a = faceCenter - center, b = face.points[k] - center,
                    c                = face.points[(k + 1) % face.points.size()] - center;
                const double triArea = length(cross(b - a, c - a)) * .5;
                if (triArea <= eps * eps) continue;
                const auto ia = vertex(fragment, a, eps), ib = vertex(fragment, b, eps), ic = vertex(fragment, c, eps);
                fragment.indices.insert(fragment.indices.end(), {ia, ib, ic});
                fragment.materials.push_back(face.neighbor < 0 ? 0U : 1U);
                volume += dot(a, cross(b, c)) / 6;
                area += triArea;
            }
            if (face.neighbor >= 0 && i < face.neighbor && area > eps * eps * 10) contacts[{i, face.neighbor}] = area;
        }
        for (std::uint32_t k = 0; k < fragment.positions.size() / 3; ++k) {
            auto v  = point(fragment, k);
            extents = {std::max(extents.x, std::abs(v.x)), std::max(extents.y, std::abs(v.y)),
                       std::max(extents.z, std::abs(v.z))};
        }
        GeometryCollectionBone bone;
        bone.localX      = static_cast<float>(origin.x + center.x);
        bone.localY      = static_cast<float>(origin.y + center.y);
        bone.localZ      = static_cast<float>(origin.z + center.z);
        bone.halfExtentX = static_cast<float>(extents.x);
        bone.halfExtentY = static_cast<float>(extents.y);
        bone.halfExtentZ = static_cast<float>(extents.z);
        bone.mass        = static_cast<float>(volume * recipe.defaultDensity);
        bone.density     = recipe.defaultDensity;
        bone.friction    = recipe.defaultFriction;
        bone.restitution = recipe.defaultRestitution;
        bone.clusterId   = clusters[static_cast<std::size_t>(i)];
        result.collection.bones.push_back(bone);
        result.geometry.fragments.push_back(std::move(fragment));
    }
    for (const auto& [pair, area] : contacts) {
        (void)area;
        float threshold = recipe.defaultStrainThreshold;
        if (recipe.mode == FractureMode::ClusteredVoronoi)
            threshold *=
                clusters[static_cast<std::size_t>(pair.first)] == clusters[static_cast<std::size_t>(pair.second)] ? 2.f
                                                                                                                  : .5f;
        result.collection.edges.push_back({pair.first, pair.second, threshold});
    }
    auto gv = result.geometry.validate();
    if (!gv) return Result<FracturedCollection>::failure(gv.status());
    auto av = result.collection.validate();
    if (!av) return Result<FracturedCollection>::failure(av.status());
    return Result<FracturedCollection>::success(std::move(result));
}
Result<FracturedCollection> cookVoronoiAssembly(const GeometryCollectionAsset& assembly, const FractureRecipe& recipe) {
    AssemblyFractureSettings settings;
    settings.recipe = recipe;
    return cookVoronoiAssembly(assembly, settings);
}
Result<FracturedCollection> cookVoronoiAssembly(const GeometryCollectionAsset&  assembly,
                                                const AssemblyFractureSettings& settings) {
    auto validSettings = settings.validate();
    if (!validSettings) return Result<FracturedCollection>::failure(validSettings.status());
    const auto& recipe = settings.recipe;
    auto        av     = assembly.validate();
    if (!av) return Result<FracturedCollection>::failure(av.status());
    auto policyFor = [&](std::size_t i) {
        FractureComponentPolicy policy{0, recipe.defaultStrainThreshold, 1.f};
        if (auto g = settings.groups.find(assembly.bones[i].clusterId); g != settings.groups.end()) policy = g->second;
        if (auto c = settings.components.find(static_cast<int>(i)); c != settings.components.end()) policy = c->second;
        return policy;
    };
    std::size_t maximum = 0;
    for (std::size_t i = 0; i < assembly.bones.size(); ++i) {
        auto p = policyFor(i);
        maximum += static_cast<std::size_t>(p.fragments ? p.fragments : recipe.siteCountMax);
    }
    if (maximum > static_cast<std::size_t>(recipe.maximumBones))
        return Result<FracturedCollection>::failure(bad("assembly exceeds fragment budget"));
    for (const auto& [id, policy] : settings.components) {
        (void)policy;
        if (static_cast<std::size_t>(id) >= assembly.bones.size())
            return Result<FracturedCollection>::failure(bad("component override is outside assembly"));
    }
    FracturedCollection              result;
    std::vector<std::pair<int, int>> ranges;
    for (std::size_t parent = 0; parent < assembly.bones.size(); ++parent) {
        const auto& original = assembly.bones[parent];
        auto        r        = recipe;
        r.seed ^= static_cast<std::uint64_t>(parent) * 0x9e3779b97f4a7c15ULL;
        const auto policy = policyFor(parent);
        if (policy.fragments) r.siteCountMin = r.siteCountMax = policy.fragments;
        r.defaultStrainThreshold = policy.cohesion;
        r.defaultDensity         = original.density;
        r.defaultFriction        = original.friction;
        r.defaultRestitution     = original.restitution;
        auto cooked = cookVoronoi(box(original.halfExtentX, original.halfExtentY, original.halfExtentZ), r);
        if (!cooked) return Result<FracturedCollection>::failure(cooked.status());
        auto&     part = cooked.value();
        const int base = static_cast<int>(result.collection.bones.size());
        for (auto bone : part.collection.bones) {
            bone.localX += original.localX;
            bone.localY += original.localY;
            bone.localZ += original.localZ;
            bone.anchoredDefault = original.anchoredDefault;
            if (r.mode == FractureMode::UniformVoronoi) bone.clusterId = original.clusterId;
            result.collection.bones.push_back(bone);
        }
        for (auto edge : part.collection.edges) {
            edge.boneA += base;
            edge.boneB += base;
            result.collection.edges.push_back(edge);
        }
        for (auto& mesh : part.geometry.fragments) result.geometry.fragments.push_back(std::move(mesh));
        ranges.push_back({base, static_cast<int>(result.collection.bones.size())});
    }
    // Preserve explicit authoring supports; do not invent support between unrelated components.
    for (const auto& parent : assembly.edges) {
        auto [a0, a1] = ranges[static_cast<std::size_t>(parent.boneA)];
        auto [b0, b1] = ranges[static_cast<std::size_t>(parent.boneB)];
        for (int a = a0; a < a1; ++a)
            for (int b = b0; b < b1; ++b) {
                const auto& x = result.collection.bones[static_cast<std::size_t>(a)];
                const auto& y = result.collection.bones[static_cast<std::size_t>(b)];
                if (std::abs(x.localX - y.localX) <= x.halfExtentX + y.halfExtentX + settings.contactTolerance &&
                    std::abs(x.localY - y.localY) <= x.halfExtentY + y.halfExtentY + settings.contactTolerance &&
                    std::abs(x.localZ - y.localZ) <= x.halfExtentZ + y.halfExtentZ + settings.contactTolerance)
                    result.collection.edges.push_back(
                        {a, b,
                         parent.strainThreshold *
                             std::min(policyFor(static_cast<std::size_t>(parent.boneA)).connectionStrength,
                                      policyFor(static_cast<std::size_t>(parent.boneB)).connectionStrength)});
            }
    }
    auto valid = result.collection.validate();
    if (!valid) return Result<FracturedCollection>::failure(valid.status());
    return Result<FracturedCollection>::success(std::move(result));
}
}  // namespace eve::physics::destruction_cook
