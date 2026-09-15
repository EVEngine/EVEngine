#include "procgen/heightmap/TerrainProbePlacement.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include "procgen/PointSet.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainRasterInternal.h"
#include "procgen/heightmap/TerrainMultiTile.h"
#include "procgen/heightmap/TerrainStamp.h"
namespace eve::procgen {
namespace {
class PcgProbeRandom {
public:
    explicit PcgProbeRandom(std::int32_t seed) {
        const auto value = seed == 0 ? 1U : static_cast<std::uint32_t>(seed);
        a_ = UINT64_C(181353) * value; b_ = UINT64_C(7) * value;
    }
    float next() {
        auto x = a_, y = b_; a_ = y; x ^= x << 23; x ^= x >> 17; x ^= y ^ (y >> 26); b_ = x;
        return static_cast<float>(x + y) / static_cast<float>(std::numeric_limits<std::uint64_t>::max());
    }
    float next(float minimum, float maximum) { return minimum + next() * (maximum - minimum); }
private:
    std::uint64_t a_ = 0, b_ = 0;
};
std::uint64_t mix(std::uint64_t value) {
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}
Result<int> invalid(const char* message) {
    return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message));
}
bool supportedResolution(int value) { return value >= 16 && value <= 2048 && (value & (value - 1)) == 0; }
}  // namespace
Result<int> exportTerrainProbePoints(PointSet& output, const Heightmap& fitness, const Heightmap& heights,
                                     const TerrainProbePlacementSettings& s) {
    using namespace raster_detail;
    if (!validRaster(fitness) || !validRaster(heights) || s.name.empty() ||
        s.type < TerrainProbeType::Reflection || s.type > TerrainProbeType::Light ||
        !std::isfinite(s.originX) || !std::isfinite(s.originZ) || !std::isfinite(s.width) || s.width <= 0 ||
        !std::isfinite(s.depth) || s.depth <= 0 || !std::isfinite(s.heightScale) || s.heightScale < 0 ||
        !std::isfinite(s.spacing) || s.spacing <= 0 || !std::isfinite(s.jitterPercent) || s.jitterPercent < 0 ||
        s.jitterPercent > 1 || !std::isfinite(s.minimumFitness) || s.minimumFitness < 0 || s.minimumFitness > 1 ||
        !std::isfinite(s.seaLevel) || !std::isfinite(s.reflectionOffset) || !std::isfinite(s.lightOffset) ||
        !supportedResolution(s.reflectionResolution) || !std::isfinite(s.reflectionClipDistance) ||
        s.reflectionClipDistance <= 0 || !std::isfinite(s.reflectionShadowDistance) ||
        s.reflectionShadowDistance < 0 || s.namespaceId == 0 || s.maxPoints < 0)
        return invalid("terrain.probes: valid finite rasters, resource, scan and reflection settings required");
    if (!std::all_of(fitness.data().begin(), fitness.data().end(), [](float v) { return v >= 0 && v <= 1; }))
        return invalid("terrain.probes: normalized fitness required");
    PointSet next;
    PcgProbeRandom random(s.seed);
    std::uint64_t candidate = 0;
    int emitted = 0;
    for (double x = 0; x <= s.width; x += s.spacing)
        for (double z = 0; z <= s.depth; z += s.spacing, ++candidate) {
            const double localX = x + s.spacing * random.next(-s.jitterPercent, s.jitterPercent) / 2;
            const double localZ = z + s.spacing * random.next(-s.jitterPercent, s.jitterPercent) / 2;
            if (localX < 0 || localZ < 0 || localX > s.width || localZ > s.depth) continue;
            const double u = localX / s.width, v = localZ / s.depth;
            const double strength = textureSample(fitness, u, v);
            if (random.next(s.minimumFitness, 1) > strength) continue;
            const double sampled = textureSample(heights, u, v) * s.heightScale;
            double y = 0;
            if (s.type == TerrainProbeType::Light) {
                if (sampled <= s.seaLevel) continue;
                y = sampled + s.lightOffset;
            } else if (!s.seaLevelActive) y = 500 + s.seaLevel + 0.2;
            else y = std::max(sampled, double(s.seaLevel)) + s.reflectionOffset;
            const double worldX = s.originX + localX, worldZ = s.originZ + localZ;
            if (emitted >= s.maxPoints || !isRepresentable(worldX) || !isRepresentable(y) ||
                !isRepresentable(worldZ))
                return invalid("terrain.probes: point budget or representable height exceeded");
            ProcgenPoint point;
            point.id = mix(s.namespaceId ^ (candidate + 1));
            if (!point.id) return invalid("terrain.probes: generated identity is reserved");
            point.x = float(worldX); point.y = float(y); point.z = float(worldZ);
            point.density = float(strength); point.boundsMinX = point.boundsMinZ = -s.spacing * 0.5F;
            point.boundsMaxX = point.boundsMaxZ = s.spacing * 0.5F; point.boundsMaxY = s.heightScale;
            point.seed = static_cast<std::uint32_t>(mix(point.id ^ static_cast<std::uint32_t>(s.seed)));
            const int row = next.appendPoint(point);
            auto name = next.trySetStringAttribute(row, "probeResource", s.name);
            if (!name.ok()) return Result<int>::failure(name.status());
            auto type = next.trySetIntAttribute(row, "probeType", static_cast<std::int64_t>(s.type));
            if (!type.ok()) return Result<int>::failure(type.status());
            auto source = next.trySetStringAttribute(row, "spawnNamespace", std::to_string(s.namespaceId));
            if (!source.ok()) return Result<int>::failure(source.status());
            auto resolution = next.trySetIntAttribute(row, "reflectionResolution", s.reflectionResolution);
            if (!resolution.ok()) return Result<int>::failure(resolution.status());
            auto clip = next.trySetFloatAttribute(row, "reflectionClipDistance", s.reflectionClipDistance);
            if (!clip.ok()) return Result<int>::failure(clip.status());
            auto shadow = next.trySetFloatAttribute(row, "reflectionShadowDistance", s.reflectionShadowDistance);
            if (!shadow.ok()) return Result<int>::failure(shadow.status());
            ++emitted;
        }
    output = std::move(next);
    return Result<int>::success(emitted);
}

Result<TerrainMultiTileReport> applyTerrainProbesMultiTile(
    const std::vector<TerrainProbeTile>& tiles, const Heightmap& fitness,
    const TerrainProbePlacementSettings& s, const TerrainStampSettings& operation,
    TerrainProbeOperationMode mode, bool worldMapOperation, const std::vector<std::string>& validNames) {
    using namespace raster_detail;
    if (tiles.empty() || mode < TerrainProbeOperationMode::Add || mode > TerrainProbeOperationMode::Remove ||
        !validRaster(fitness) || s.name.empty() || s.type < TerrainProbeType::Reflection ||
        s.type > TerrainProbeType::Light || !std::isfinite(s.spacing) || s.spacing <= 0 ||
        !std::isfinite(s.jitterPercent) || s.jitterPercent < 0 || s.jitterPercent > 1 ||
        !std::isfinite(s.minimumFitness) || s.minimumFitness < 0 || s.minimumFitness > 1 ||
        !std::isfinite(s.heightScale) || s.heightScale < 0 || !std::isfinite(s.seaLevel) ||
        !std::isfinite(s.reflectionOffset) || !std::isfinite(s.lightOffset) ||
        !supportedResolution(s.reflectionResolution) || !std::isfinite(s.reflectionClipDistance) ||
        s.reflectionClipDistance <= 0 || !std::isfinite(s.reflectionShadowDistance) ||
        s.reflectionShadowDistance < 0 || s.namespaceId == 0 || s.maxPoints < 0)
        return Result<TerrainMultiTileReport>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.probes.multitile: valid tiles and settings required"));
    if (!std::all_of(fitness.data().begin(), fitness.data().end(), [](float value) { return value >= 0 && value <= 1; }))
        return Result<TerrainMultiTileReport>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.probes.multitile: normalized fitness required"));
    std::vector<TerrainOperationTile> descriptors;
    std::unordered_set<PointSet*> owners;
    for (const auto& tile : tiles) {
        if (!tile.probes || !tile.heights || !owners.insert(tile.probes).second || !validRaster(*tile.heights))
            return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "terrain.probes.multitile: distinct outputs and finite heights required"));
        descriptors.push_back({tile.name, tile.originX, tile.originZ, tile.width, tile.depth,
                               tile.resolutionX, tile.resolutionY, tile.worldMap});
    }
    auto mapped = mapTerrainOperationMultiTile(descriptors, operation, TerrainOperationDomain::GameObject,
                                               worldMapOperation, validNames);
    if (!mapped.ok()) return mapped;
    auto report = std::move(mapped.value());
    if (fitness.getWidth() != report.operationWidth || fitness.getHeight() != report.operationHeight)
        return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.probes.multitile: fitness must match operation window"));
    struct Candidate { PointSet* owner; PointSet points; bool changed; };
    std::vector<Candidate> candidates;
    PcgProbeRandom random(s.seed);
    std::uint64_t candidateIndex = 0;
    int emitted = 0;
    for (const auto& mapping : report.mappings) {
        const auto tile = std::find_if(tiles.begin(), tiles.end(), [&](const auto& value) {
            return value.name == mapping.terrainName;
        });
        if (tile == tiles.end())
            return Result<TerrainMultiTileReport>::failure(
                Diagnostic::error(DiagnosticCode::InvariantViolation, "terrain.probes.multitile: mapped tile missing"));
        PointSet next;
        int changed = 0;
        const bool clear = mode != TerrainProbeOperationMode::Add;
        if (clear) {
            for (int row = 0; row < tile->probes->getCount(); ++row) {
                const auto& point = tile->probes->points()[static_cast<std::size_t>(row)];
                const int lx = static_cast<int>(std::nearbyint((point.x - tile->originX) * tile->resolutionX / tile->width));
                const int lz = static_cast<int>(std::nearbyint((point.z - tile->originZ) * tile->resolutionY / tile->depth));
                bool erase = tile->probes->getStringAttribute(row, "probeResource", "") == s.name &&
                             lx >= mapping.localX && lx < mapping.localX + mapping.width &&
                             lz >= mapping.localY && lz < mapping.localY + mapping.height;
                if (erase && mode == TerrainProbeOperationMode::Remove)
                    erase = fitness.height(mapping.operationX + lx - mapping.localX,
                                           mapping.operationY + lz - mapping.localY) > s.minimumFitness;
                if (erase) ++changed;
                else {
                    auto copied = next.appendPointFrom(*tile->probes, static_cast<std::size_t>(row));
                    if (!copied.ok()) return Result<TerrainMultiTileReport>::failure(copied.status());
                }
            }
        } else next = *tile->probes;
        if (mode != TerrainProbeOperationMode::Remove) {
            const double startX = mapping.localX * tile->width / tile->resolutionX;
            const double startZ = mapping.localY * tile->depth / tile->resolutionY;
            const double stopX = (mapping.localX + mapping.width - 1) * tile->width / tile->resolutionX;
            const double stopZ = (mapping.localY + mapping.height - 1) * tile->depth / tile->resolutionY;
            for (double x = startX; x <= stopX; x += s.spacing)
                for (double z = startZ; z <= stopZ; z += s.spacing, ++candidateIndex) {
                    const double localX = x + s.spacing * random.next(-s.jitterPercent, s.jitterPercent) / 2;
                    const double localZ = z + s.spacing * random.next(-s.jitterPercent, s.jitterPercent) / 2;
                    const int lx = static_cast<int>(std::nearbyint(localX * tile->resolutionX / tile->width));
                    const int lz = static_cast<int>(std::nearbyint(localZ * tile->resolutionY / tile->depth));
                    if (lx < mapping.localX || lx >= mapping.localX + mapping.width ||
                        lz < mapping.localY || lz >= mapping.localY + mapping.height) continue;
                    const float strength = fitness.height(mapping.operationX + lx - mapping.localX,
                                                          mapping.operationY + lz - mapping.localY);
                    if (random.next(s.minimumFitness, 1) > strength) continue;
                    const double u = localX / tile->width, v = localZ / tile->depth;
                    const double sampled = textureSample(*tile->heights, u, v) * s.heightScale;
                    double y = 0;
                    if (s.type == TerrainProbeType::Light) {
                        if (sampled <= s.seaLevel) continue;
                        y = sampled + s.lightOffset;
                    } else if (!s.seaLevelActive) y = 500 + s.seaLevel + 0.2;
                    else y = std::max(sampled, double(s.seaLevel)) + s.reflectionOffset;
                    const double worldX = tile->originX + localX, worldZ = tile->originZ + localZ;
                    if (emitted >= s.maxPoints || !isRepresentable(worldX) || !isRepresentable(y) ||
                        !isRepresentable(worldZ))
                        return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
                            DiagnosticCode::InvalidArgument, "terrain.probes.multitile: point budget exceeded"));
                    ProcgenPoint point;
                    point.id = mix(s.namespaceId ^ (candidateIndex + 1));
                    if (!point.id)
                        return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
                            DiagnosticCode::InvalidArgument, "terrain.probes.multitile: generated identity is reserved"));
                    point.seed = static_cast<std::uint32_t>(mix(point.id ^ static_cast<std::uint32_t>(s.seed)));
                    point.x = float(worldX); point.y = float(y); point.z = float(worldZ);
                    point.density = strength; point.boundsMinX = point.boundsMinZ = -s.spacing * 0.5F;
                    point.boundsMaxX = point.boundsMaxZ = s.spacing * 0.5F; point.boundsMaxY = s.heightScale;
                    const int row = next.appendPoint(point);
                    auto resource = next.trySetStringAttribute(row, "probeResource", s.name);
                    if (!resource.ok()) return Result<TerrainMultiTileReport>::failure(resource.status());
                    auto type = next.trySetIntAttribute(row, "probeType", static_cast<std::int64_t>(s.type));
                    if (!type.ok()) return Result<TerrainMultiTileReport>::failure(type.status());
                    auto source = next.trySetStringAttribute(row, "spawnNamespace", std::to_string(s.namespaceId));
                    if (!source.ok()) return Result<TerrainMultiTileReport>::failure(source.status());
                    auto resolution = next.trySetIntAttribute(row, "reflectionResolution", s.reflectionResolution);
                    if (!resolution.ok()) return Result<TerrainMultiTileReport>::failure(resolution.status());
                    auto clip = next.trySetFloatAttribute(row, "reflectionClipDistance", s.reflectionClipDistance);
                    if (!clip.ok()) return Result<TerrainMultiTileReport>::failure(clip.status());
                    auto shadow = next.trySetFloatAttribute(row, "reflectionShadowDistance", s.reflectionShadowDistance);
                    if (!shadow.ok()) return Result<TerrainMultiTileReport>::failure(shadow.status());
                    ++emitted; ++changed;
                }
        }
        report.changedSamples += changed;
        candidates.push_back({tile->probes, std::move(next), changed > 0});
    }
    report.affectedTiles = static_cast<int>(std::count_if(candidates.begin(), candidates.end(),
        [](const auto& value) { return value.changed; }));
    for (auto& candidate : candidates) *candidate.owner = std::move(candidate.points);
    return Result<TerrainMultiTileReport>::success(std::move(report));
}
}  // namespace eve::procgen
