#include "physics/destruction/cook/GeometryCollectionCooker.h"

#include "asset/CanonicalMesh.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace eve::physics::destruction_cook {
namespace {

struct Vec3 {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

struct Aabb {
    Vec3 min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    Vec3 max{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(),
             -std::numeric_limits<float>::max()};

    void expand(float x, float y, float z) {
        min.x = std::min(min.x, x);
        min.y = std::min(min.y, y);
        min.z = std::min(min.z, z);
        max.x = std::max(max.x, x);
        max.y = std::max(max.y, y);
        max.z = std::max(max.z, z);
    }

    [[nodiscard]] Vec3 center() const {
        return {0.5f * (min.x + max.x), 0.5f * (min.y + max.y), 0.5f * (min.z + max.z)};
    }

    [[nodiscard]] Vec3 extent() const { return {max.x - min.x, max.y - min.y, max.z - min.z}; }

    [[nodiscard]] float volume() const {
        const Vec3 e = extent();
        return std::max(0.f, e.x) * std::max(0.f, e.y) * std::max(0.f, e.z);
    }
};

struct Cell {
    Vec3 center;
    Vec3 halfExtent;
    int  clusterId = 0;
};

struct Site {
    Vec3 position;
    int  clusterId = 0;
};

std::uint64_t hashBytes(std::uint64_t value, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        value ^= bytes[i];
        value *= 0x100000001b3ull;
    }
    return value;
}

std::uint64_t mixSeed(std::uint64_t seed, const std::string& stream) {
    std::uint64_t value = 0xcbf29ce484222325ull ^ seed;
    value = hashBytes(value, stream.data(), stream.size());
    value ^= seed + 0x9e3779b97f4a7c15ull;
    return value == 0 ? 0xA5A5A5A5A5A5A5A5ull : value;
}

class SplitMix64 {
public:
    explicit SplitMix64(std::uint64_t seed) : state_(seed) {}

    std::uint64_t nextU64() {
        std::uint64_t z = (state_ += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }

    float nextUnit() {
        return static_cast<float>(nextU64() >> 11) * (1.f / 9007199254740992.f);
    }

    float nextRange(float lo, float hi) { return lo + (hi - lo) * nextUnit(); }

private:
    std::uint64_t state_;
};

float length3(float x, float y, float z) { return std::sqrt(x * x + y * y + z * z); }

Vec3 normalize3(float x, float y, float z) {
    const float len = length3(x, y, z);
    if (len <= 1e-8f) return {0.f, 1.f, 0.f};
    return {x / len, y / len, z / len};
}

eve::Result<Aabb> meshBounds(const eve::asset::CanonicalMeshData& mesh, float minimumThickness) {
    if (mesh.positions.empty() || mesh.positions.size() % 3 != 0)
        return eve::Result<Aabb>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "mesh positions must contain complete xyz vertices",
            "mesh.positions"));
    if (mesh.indices.empty() || mesh.indices.size() % 3 != 0)
        return eve::Result<Aabb>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "mesh indices must contain complete triangles", "mesh.indices"));
    const std::size_t vertexCount = mesh.positions.size() / 3;
    Aabb bounds;
    for (std::size_t i = 0; i < vertexCount; ++i) {
        const float x = mesh.positions[i * 3];
        const float y = mesh.positions[i * 3 + 1];
        const float z = mesh.positions[i * 3 + 2];
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
            return eve::Result<Aabb>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "mesh positions must be finite", "mesh.positions"));
        bounds.expand(x, y, z);
    }
    for (std::size_t t = 0; t < mesh.indices.size(); t += 3) {
        const auto a = mesh.indices[t];
        const auto b = mesh.indices[t + 1];
        const auto c = mesh.indices[t + 2];
        if (a >= vertexCount || b >= vertexCount || c >= vertexCount || a == b || b == c || a == c)
            return eve::Result<Aabb>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "mesh contains an invalid triangle", "mesh.indices"));
    }
    const Vec3 extent = bounds.extent();
    if (extent.x < minimumThickness || extent.y < minimumThickness || extent.z < minimumThickness)
        return eve::Result<Aabb>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "mesh AABB is thinner than minimumThickness", "mesh.bounds"));
    if (!(bounds.volume() > 0.f))
        return eve::Result<Aabb>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "mesh AABB volume must be positive", "mesh.bounds"));
    return eve::Result<Aabb>::success(bounds);
}

int chooseSiteCount(const FractureRecipe& recipe, SplitMix64& rng) {
    if (recipe.siteCountMin == recipe.siteCountMax) return recipe.siteCountMin;
    const int span = recipe.siteCountMax - recipe.siteCountMin + 1;
    return recipe.siteCountMin + static_cast<int>(rng.nextU64() % static_cast<std::uint64_t>(span));
}

std::vector<Site> makeUniformSites(const Aabb& bounds, int count, SplitMix64& rng) {
    std::vector<Site> sites;
    sites.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        Site site;
        site.position = {rng.nextRange(bounds.min.x, bounds.max.x), rng.nextRange(bounds.min.y, bounds.max.y),
                         rng.nextRange(bounds.min.z, bounds.max.z)};
        site.clusterId = 0;
        sites.push_back(site);
    }
    return sites;
}

std::vector<Site> makeClusteredSites(const Aabb& bounds, const FractureRecipe& recipe, SplitMix64& rng) {
    const int siteCount = chooseSiteCount(recipe, rng);
    std::vector<Vec3> centers;
    centers.reserve(static_cast<std::size_t>(recipe.clusterCount));
    for (int c = 0; c < recipe.clusterCount; ++c) {
        centers.push_back({rng.nextRange(bounds.min.x, bounds.max.x), rng.nextRange(bounds.min.y, bounds.max.y),
                           rng.nextRange(bounds.min.z, bounds.max.z)});
    }
    std::vector<Site> sites;
    sites.reserve(static_cast<std::size_t>(siteCount));
    for (int i = 0; i < siteCount; ++i) {
        const int clusterId = i % recipe.clusterCount;
        const Vec3& center = centers[static_cast<std::size_t>(clusterId)];
        Site site;
        site.clusterId = clusterId;
        site.position = {
            std::clamp(center.x + rng.nextRange(-recipe.clusterRadius, recipe.clusterRadius), bounds.min.x,
                       bounds.max.x),
            std::clamp(center.y + rng.nextRange(-recipe.clusterRadius, recipe.clusterRadius), bounds.min.y,
                       bounds.max.y),
            std::clamp(center.z + rng.nextRange(-recipe.clusterRadius, recipe.clusterRadius), bounds.min.z,
                       bounds.max.z),
        };
        sites.push_back(site);
    }
    return sites;
}

std::vector<Cell> cellsFromSites(const Aabb& bounds, const std::vector<Site>& sites) {
    std::vector<Cell> cells;
    cells.reserve(sites.size());
    const Vec3 origin = bounds.center();
    for (std::size_t i = 0; i < sites.size(); ++i) {
        float nearest = std::numeric_limits<float>::max();
        for (std::size_t j = 0; j < sites.size(); ++j) {
            if (i == j) continue;
            const float dx = sites[i].position.x - sites[j].position.x;
            const float dy = sites[i].position.y - sites[j].position.y;
            const float dz = sites[i].position.z - sites[j].position.z;
            nearest = std::min(nearest, length3(dx, dy, dz));
        }
        if (!std::isfinite(nearest) || nearest > 1e20f) {
            const Vec3 extent = bounds.extent();
            nearest = 0.5f * std::min({extent.x, extent.y, extent.z});
        }
        const float half = std::max(0.05f, 0.5f * nearest);
        Cell cell;
        cell.center = {sites[i].position.x - origin.x, sites[i].position.y - origin.y,
                       sites[i].position.z - origin.z};
        // Clamp proxy to remaining distance to AABB walls in local space.
        const float maxHx = std::max(0.05f, std::min(sites[i].position.x - bounds.min.x, bounds.max.x - sites[i].position.x));
        const float maxHy = std::max(0.05f, std::min(sites[i].position.y - bounds.min.y, bounds.max.y - sites[i].position.y));
        const float maxHz = std::max(0.05f, std::min(sites[i].position.z - bounds.min.z, bounds.max.z - sites[i].position.z));
        cell.halfExtent = {std::min(half, maxHx), std::min(half, maxHy), std::min(half, maxHz)};
        cell.clusterId = sites[i].clusterId;
        cells.push_back(cell);
    }
    return cells;
}

std::vector<Cell> cellsFromPlanar(const Aabb& bounds, const FractureRecipe& recipe) {
    struct BoxCell {
        Aabb box;
        int  clusterId = 0;
    };
    std::vector<BoxCell> cells{{bounds, 0}};
    const std::size_t planeCount = recipe.planeOffsets.size();
    for (std::size_t p = 0; p < planeCount; ++p) {
        const float nx = recipe.planeNormals[p * 3];
        const float ny = recipe.planeNormals[p * 3 + 1];
        const float nz = recipe.planeNormals[p * 3 + 2];
        const Vec3 normal = normalize3(nx, ny, nz);
        const float offset = recipe.planeOffsets[p];
        std::vector<BoxCell> next;
        next.reserve(cells.size() * 2);
        for (const auto& cell : cells) {
            // Split AABB along the dominant axis of the plane normal for a stable proxy.
            const float ax = std::fabs(normal.x);
            const float ay = std::fabs(normal.y);
            const float az = std::fabs(normal.z);
            Aabb left = cell.box;
            Aabb right = cell.box;
            bool split = false;
            if (ax >= ay && ax >= az) {
                const float cut = std::clamp(offset, cell.box.min.x, cell.box.max.x);
                if (cut > cell.box.min.x + 1e-4f && cut < cell.box.max.x - 1e-4f) {
                    left.max.x = cut;
                    right.min.x = cut;
                    split = true;
                }
            } else if (ay >= ax && ay >= az) {
                const float cut = std::clamp(offset, cell.box.min.y, cell.box.max.y);
                if (cut > cell.box.min.y + 1e-4f && cut < cell.box.max.y - 1e-4f) {
                    left.max.y = cut;
                    right.min.y = cut;
                    split = true;
                }
            } else {
                const float cut = std::clamp(offset, cell.box.min.z, cell.box.max.z);
                if (cut > cell.box.min.z + 1e-4f && cut < cell.box.max.z - 1e-4f) {
                    left.max.z = cut;
                    right.min.z = cut;
                    split = true;
                }
            }
            if (!split) {
                next.push_back(cell);
            } else {
                next.push_back({left, cell.clusterId});
                next.push_back({right, cell.clusterId});
            }
        }
        cells.swap(next);
    }
    const Vec3 origin = bounds.center();
    std::vector<Cell> out;
    out.reserve(cells.size());
    for (const auto& cell : cells) {
        const Vec3 c = cell.box.center();
        const Vec3 e = cell.box.extent();
        Cell proxy;
        proxy.center = {c.x - origin.x, c.y - origin.y, c.z - origin.z};
        proxy.halfExtent = {std::max(0.05f, 0.5f * e.x), std::max(0.05f, 0.5f * e.y), std::max(0.05f, 0.5f * e.z)};
        proxy.clusterId = cell.clusterId;
        out.push_back(proxy);
    }
    return out;
}

std::vector<Cell> cellsFromRadial(const Aabb& bounds, const FractureRecipe& recipe) {
    FractureRecipe planar = recipe;
    planar.mode = FractureMode::Planar;
    planar.planeNormals.clear();
    planar.planeOffsets.clear();
    const Vec3 origin = bounds.center();
    const float twoPi = 6.28318530718f;
    for (int s = 0; s < recipe.radialSpokes; ++s) {
        const float angle = twoPi * static_cast<float>(s) / static_cast<float>(recipe.radialSpokes);
        const float nx = std::cos(angle);
        const float nz = std::sin(angle);
        planar.planeNormals.push_back(nx);
        planar.planeNormals.push_back(0.f);
        planar.planeNormals.push_back(nz);
        // Plane through origin: n·x = n·origin
        planar.planeOffsets.push_back(nx * origin.x + nz * origin.z);
    }
    // Optional shell cuts along Y for radialPlanes > 0.
    if (recipe.radialPlanes > 0) {
        const float y0 = bounds.min.y;
        const float y1 = bounds.max.y;
        for (int p = 1; p <= recipe.radialPlanes; ++p) {
            const float t = static_cast<float>(p) / static_cast<float>(recipe.radialPlanes + 1);
            planar.planeNormals.push_back(0.f);
            planar.planeNormals.push_back(1.f);
            planar.planeNormals.push_back(0.f);
            planar.planeOffsets.push_back(y0 + (y1 - y0) * t);
        }
    }
    return cellsFromPlanar(bounds, planar);
}

void buildConnectionGraph(const std::vector<Cell>& cells, const FractureRecipe& recipe,
                          GeometryCollectionAsset& asset) {
    const float connectScale = 2.25f;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        for (std::size_t j = i + 1; j < cells.size(); ++j) {
            const float dx = cells[i].center.x - cells[j].center.x;
            const float dy = cells[i].center.y - cells[j].center.y;
            const float dz = cells[i].center.z - cells[j].center.z;
            const float dist = length3(dx, dy, dz);
            const float reach = connectScale * (cells[i].halfExtent.x + cells[i].halfExtent.y + cells[i].halfExtent.z +
                                               cells[j].halfExtent.x + cells[j].halfExtent.y + cells[j].halfExtent.z) /
                               6.f;
            if (dist > reach) continue;
            GeometryCollectionEdge edge;
            edge.boneA = static_cast<int>(i);
            edge.boneB = static_cast<int>(j);
            // Auto-cluster: intra-cluster edges are stronger so islands break apart first.
            if (recipe.mode == FractureMode::ClusteredVoronoi && cells[i].clusterId == cells[j].clusterId)
                edge.strainThreshold = recipe.defaultStrainThreshold * 2.f;
            else if (recipe.mode == FractureMode::ClusteredVoronoi)
                edge.strainThreshold = recipe.defaultStrainThreshold * 0.5f;
            else
                edge.strainThreshold = recipe.defaultStrainThreshold;
            asset.edges.push_back(edge);
        }
    }
}

}  // namespace

eve::Result<GeometryCollectionAsset> cookGeometryCollection(const eve::asset::CanonicalMeshData& mesh,
                                                            const FractureRecipe& recipe) {
    auto valid = recipe.validate();
    if (!valid) return eve::Result<GeometryCollectionAsset>::failure(valid.status());
    auto bounds = meshBounds(mesh, recipe.minimumThickness);
    if (!bounds) return eve::Result<GeometryCollectionAsset>::failure(bounds.status());

    SplitMix64 rng(mixSeed(recipe.seed, recipe.randomStreamName));
    std::vector<Cell> cells;
    switch (recipe.mode) {
        case FractureMode::UniformVoronoi: {
            const int count = chooseSiteCount(recipe, rng);
            cells = cellsFromSites(bounds.value(), makeUniformSites(bounds.value(), count, rng));
            break;
        }
        case FractureMode::ClusteredVoronoi:
            cells = cellsFromSites(bounds.value(), makeClusteredSites(bounds.value(), recipe, rng));
            break;
        case FractureMode::Planar:
            cells = cellsFromPlanar(bounds.value(), recipe);
            break;
        case FractureMode::Radial:
            cells = cellsFromRadial(bounds.value(), recipe);
            break;
    }
    if (cells.empty())
        return eve::Result<GeometryCollectionAsset>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "fracture cook produced no bones", "cook"));
    if (static_cast<int>(cells.size()) > recipe.maximumBones)
        return eve::Result<GeometryCollectionAsset>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "fracture cook exceeded maximumBones", "maximumBones"));

    GeometryCollectionAsset asset;
    asset.bones.reserve(cells.size());
    for (const auto& cell : cells) {
        GeometryCollectionBone bone;
        bone.halfExtentX = cell.halfExtent.x;
        bone.halfExtentY = cell.halfExtent.y;
        bone.halfExtentZ = cell.halfExtent.z;
        bone.localX = cell.center.x;
        bone.localY = cell.center.y;
        bone.localZ = cell.center.z;
        const float volume =
            8.f * cell.halfExtent.x * cell.halfExtent.y * cell.halfExtent.z;
        bone.density = recipe.defaultDensity;
        bone.mass = std::max(0.05f, volume * recipe.defaultDensity);
        bone.friction = recipe.defaultFriction;
        bone.restitution = recipe.defaultRestitution;
        asset.bones.push_back(bone);
    }
    buildConnectionGraph(cells, recipe, asset);
    auto assetValid = asset.validate();
    if (!assetValid) return eve::Result<GeometryCollectionAsset>::failure(assetValid.status());
    return eve::Result<GeometryCollectionAsset>::success(std::move(asset));
}

}  // namespace eve::physics::destruction_cook
