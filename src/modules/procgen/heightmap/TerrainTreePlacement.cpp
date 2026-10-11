#include "procgen/heightmap/TerrainTreePlacement.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <unordered_set>

#include "procgen/PointSet.h"
#include "procgen/heightmap/TerrainRasterInternal.h"
#include "procgen/heightmap/TerrainMultiTile.h"
#include "procgen/heightmap/TerrainStamp.h"

namespace eve::procgen {
namespace {
class PcgXorshiftPlus {
public:
    explicit PcgXorshiftPlus(std::int32_t seed) {
        const std::uint32_t value = seed == 0 ? 1U : static_cast<std::uint32_t>(seed);
        stateA_ = UINT64_C(181353) * value;
        stateB_ = UINT64_C(7) * value;
    }
    float next() {
        std::uint64_t x = stateA_, y = stateB_;
        stateA_ = y;
        x ^= x << 23;
        x ^= x >> 17;
        x ^= y ^ (y >> 26);
        stateB_ = x;
        return static_cast<float>(x + y) / static_cast<float>(std::numeric_limits<std::uint64_t>::max());
    }
    float next(float minimum, float maximum) { return minimum + next() * (maximum - minimum); }

private:
    std::uint64_t stateA_ = 0, stateB_ = 0;
};

std::uint64_t mix(std::uint64_t value) {
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}
bool normalized(float value) { return std::isfinite(value) && value >= 0 && value <= 1; }
bool validScaleMode(TerrainTreeScaleMode value) {
    return value >= TerrainTreeScaleMode::Fixed && value <= TerrainTreeScaleMode::FitnessRandomized;
}
bool validYOffsetMode(TerrainTreeYOffsetMode value) {
    return value >= TerrainTreeYOffsetMode::TerrainHeight && value <= TerrainTreeYOffsetMode::Custom;
}
int nearestEven(double value) {
    const double lower = std::floor(value), fraction = value - lower;
    return int(lower + (fraction > 0.5 || (fraction == 0.5 && std::fmod(lower, 2.0) != 0)));
}
std::uint64_t stableNameHash(const std::string& name) {
    std::uint64_t value = UINT64_C(1469598103934665603);
    for (unsigned char byte : name) value = (value ^ byte) * UINT64_C(1099511628211);
    return value;
}
}  // namespace

Result<int> exportTerrainTreePoints(PointSet& output, const Heightmap& fitness, const Heightmap& heights,
                                    const TerrainTreePlacementSettings& s) {
    using namespace raster_detail;
    if (!validRaster(fitness) || !validRaster(heights) || !std::isfinite(s.originX) ||
        !std::isfinite(s.originZ) || !std::isfinite(s.width) || s.width <= 0 || !std::isfinite(s.depth) ||
        s.depth <= 0 || !std::isfinite(s.heightScale) || !std::isfinite(s.spacing) || s.spacing <= 0 ||
        !std::isfinite(s.spawnDensity) || s.spawnDensity <= 0 || !normalized(s.jitterPercent) ||
        !normalized(s.failureRate) || !normalized(s.minimumFitness) || !std::isfinite(s.seaLevel) ||
        !std::isfinite(s.customOffset) || !std::isfinite(s.minimumYOffset) || !std::isfinite(s.maximumYOffset) ||
        s.maximumYOffset < s.minimumYOffset || !validScaleMode(s.scaleMode) || !validYOffsetMode(s.yOffsetMode) ||
        !std::isfinite(s.minimumWidth) || s.minimumWidth <= 0 || !std::isfinite(s.maximumWidth) ||
        s.maximumWidth < s.minimumWidth || !std::isfinite(s.minimumHeight) || s.minimumHeight <= 0 ||
        !std::isfinite(s.maximumHeight) || s.maximumHeight < s.minimumHeight ||
        !normalized(s.widthRandomPercentage) || !normalized(s.heightRandomPercentage) || !normalized(s.healthyR) ||
        !normalized(s.healthyG) || !normalized(s.healthyB) || !normalized(s.healthyA) || !normalized(s.dryR) ||
        !normalized(s.dryG) || !normalized(s.dryB) || !normalized(s.dryA) || !std::isfinite(s.bendFactor) ||
        s.bendFactor < 0 || !std::isfinite(s.boundsRadius) || s.boundsRadius <= 0 || s.namespaceId == 0 ||
        s.asset.empty() || s.maxPoints < 0)
        return invalid("terrain.treePoints: valid rasters, domain, prototype, probabilities and budget required");
    if (!std::all_of(fitness.data().begin(), fitness.data().end(), [](float value) {
            return value >= 0 && value <= 1;
        }))
        return invalid("terrain.treePoints: fitness raster must be normalized");

    const double increment = double(s.spacing) / double(s.spawnDensity);
    if (!isRepresentable(increment) || increment <= 0)
        return invalid("terrain.treePoints: effective spacing is not representable");
    const double xSteps = std::floor(double(s.width) / increment) + 1;
    const double zSteps = std::floor(double(s.depth) / increment) + 1;
    if (!std::isfinite(xSteps) || !std::isfinite(zSteps) || xSteps > std::numeric_limits<int>::max() ||
        zSteps > std::numeric_limits<int>::max() || xSteps * zSteps > std::numeric_limits<int>::max())
        return invalid("terrain.treePoints: candidate grid exceeds supported size");

    PcgXorshiftPlus random(s.seed);
    PointSet          next;
    next.reserve(std::min<std::size_t>(static_cast<std::size_t>(xSteps * zSteps), static_cast<std::size_t>(s.maxPoints)));
    const double jitter = increment * s.jitterPercent;
    int emitted = 0;
    for (int xi = 0; xi < static_cast<int>(xSteps); ++xi) {
        for (int zi = 0; zi < static_cast<int>(zSteps); ++zi) {
            if (random.next() < s.failureRate) continue;
            const double localX = xi * increment + random.next(float(-jitter), float(jitter));
            const double localZ = zi * increment + random.next(float(-jitter), float(jitter));
            if (localX < 0 || localZ < 0 || localX > s.width || localZ > s.depth) continue;
            const double u = localX / s.width, v = localZ / s.depth;
            const double strength = textureSample(fitness, u, v);
            if (random.next(s.minimumFitness, 1) > strength) continue;
            if (emitted >= s.maxPoints) return invalid("terrain.treePoints: point budget exceeded");

            const std::uint64_t cell = std::uint64_t(xi) * std::uint64_t(static_cast<int>(zSteps)) + std::uint64_t(zi);
            ProcgenPoint        point;
            point.id = mix(mix(s.namespaceId + UINT64_C(0x9e3779b97f4a7c15)) ^ (cell + 1));
            if (point.id == 0) return invalid("terrain.treePoints: reserved zero identity; choose another namespace");
            const double terrainY = sample(heights, u, v) * double(s.heightScale);
            double       worldY   = terrainY;
            if (!s.snapToTerrain) {
                if (s.yOffsetMode == TerrainTreeYOffsetMode::SeaLevel) worldY = s.seaLevel;
                if (s.yOffsetMode == TerrainTreeYOffsetMode::Custom) worldY = s.customOffset;
                worldY += random.next(s.minimumYOffset, s.maximumYOffset);
            }
            double width = s.minimumWidth, height = s.minimumHeight;
            if (s.scaleMode == TerrainTreeScaleMode::Fitness ||
                s.scaleMode == TerrainTreeScaleMode::FitnessRandomized) {
                width  = std::lerp(double(s.minimumWidth), double(s.maximumWidth), strength);
                height = std::lerp(double(s.minimumHeight), double(s.maximumHeight), strength);
            } else if (s.scaleMode == TerrainTreeScaleMode::Random) {
                const double value = random.next();
                width  = std::lerp(double(s.minimumWidth), double(s.maximumWidth), value);
                height = std::lerp(double(s.minimumHeight), double(s.maximumHeight), value);
            }
            if (s.scaleMode == TerrainTreeScaleMode::FitnessRandomized) {
                const double value = random.next();
                width *= std::lerp(1.0 - s.widthRandomPercentage, 1.0 + s.widthRandomPercentage, value);
                height *= std::lerp(1.0 - s.heightRandomPercentage, 1.0 + s.heightRandomPercentage, value);
            }
            const double worldX = double(s.originX) + localX, worldZ = double(s.originZ) + localZ;
            if (!isRepresentable(worldX) || !isRepresentable(worldY) || !isRepresentable(worldZ) ||
                !isRepresentable(width) || width <= 0 || !isRepresentable(height) || height <= 0)
                return invalid("terrain.treePoints: generated transform is not representable");
            point.x = float(worldX);
            point.y = float(worldY);
            point.z = float(worldZ);
            point.yaw = random.next(0, 360);
            point.scaleX = point.scaleZ = float(width);
            point.scaleY = float(height);
            point.density = float(strength);
            point.colorR = float(std::lerp(double(s.dryR), double(s.healthyR), strength));
            point.colorG = float(std::lerp(double(s.dryG), double(s.healthyG), strength));
            point.colorB = float(std::lerp(double(s.dryB), double(s.healthyB), strength));
            point.colorA = float(std::lerp(double(s.dryA), double(s.healthyA), strength));
            point.seed = static_cast<std::uint32_t>(mix(point.id ^ static_cast<std::uint32_t>(s.seed)));
            const float radius = float(double(s.boundsRadius) * width);
            if (!std::isfinite(radius)) return invalid("terrain.treePoints: generated bounds are not representable");
            point.boundsMinX = point.boundsMinZ = -radius;
            point.boundsMaxX = point.boundsMaxZ = radius;
            point.boundsMaxY = float(height);
            const int row = next.appendPoint(point);
            auto asset = next.trySetStringAttribute(row, "asset", s.asset);
            if (!asset.ok()) return Result<int>::failure(asset.status());
            auto source = next.trySetStringAttribute(row, "spawnNamespace", std::to_string(s.namespaceId));
            if (!source.ok()) return Result<int>::failure(source.status());
            auto candidate = next.trySetIntAttribute(row, "treeCandidate", static_cast<std::int64_t>(cell));
            if (!candidate.ok()) return Result<int>::failure(candidate.status());
            auto bend = next.trySetFloatAttribute(row, "bendFactor", s.bendFactor);
            if (!bend.ok()) return Result<int>::failure(bend.status());
            ++emitted;
        }
    }
    static_assert(std::is_nothrow_move_assignable_v<PointSet>);
    output = std::move(next);
    return Result<int>::success(emitted);
}

Result<int> removeTerrainTreePoints(PointSet& output, const PointSet& input, const Heightmap& fitness,
                                    const TerrainTreePlacementSettings& s) {
    using namespace raster_detail;
    if (!validRaster(fitness) || !std::all_of(fitness.data().begin(), fitness.data().end(), [](float value) {
            return value >= 0 && value <= 1;
        }) ||
        !std::isfinite(s.originX) || !std::isfinite(s.originZ) || !std::isfinite(s.width) || s.width <= 0 ||
        !std::isfinite(s.depth) || s.depth <= 0 || !normalized(s.minimumFitness) || s.asset.empty())
        return invalid("terrain.removeTreePoints: valid fitness, domain, threshold and asset required");
    PointSet next;
    next.reserve(static_cast<std::size_t>(input.getCount()));
    int removed = 0;
    for (int i = 0; i < input.getCount(); ++i) {
        const auto& point = input.points()[static_cast<std::size_t>(i)];
        bool erase = false;
        if (input.getStringAttribute(i, "asset", "") == s.asset) {
            const double u = (double(point.x) - s.originX) / s.width;
            const double v = (double(point.z) - s.originZ) / s.depth;
            if (u >= 0 && u <= 1 && v >= 0 && v <= 1) erase = textureSample(fitness, u, v) > s.minimumFitness;
        }
        if (erase) {
            ++removed;
        } else {
            auto appended = next.appendPointFrom(input, static_cast<std::size_t>(i));
            if (!appended.ok()) return Result<int>::failure(appended.status());
        }
    }
    static_assert(std::is_nothrow_move_assignable_v<PointSet>);
    output = std::move(next);
    return Result<int>::success(removed);
}

Result<int> rescaleTerrainTreePoints(PointSet& output, const PointSet& input, const TerrainTreeRescaleSettings& s) {
    using namespace raster_detail;
    const auto orderedPositive = [](float minimum, float maximum) {
        return std::isfinite(minimum) && minimum > 0 && std::isfinite(maximum) && maximum >= minimum;
    };
    if (!validScaleMode(s.scaleMode) || !orderedPositive(s.previousMinimumWidth, s.previousMaximumWidth) ||
        !orderedPositive(s.previousMinimumHeight, s.previousMaximumHeight) ||
        !orderedPositive(s.minimumWidth, s.maximumWidth) || !orderedPositive(s.minimumHeight, s.maximumHeight) ||
        !std::isfinite(s.bendFactor) || s.bendFactor < 0 || !std::isfinite(s.boundsRadius) ||
        s.boundsRadius <= 0 || s.asset.empty())
        return invalid("terrain.rescaleTreePoints: valid previous and replacement prototype required");
    PointSet next = input;
    int      matched = 0;
    for (int i = 0; i < next.getCount(); ++i) {
        if (next.getStringAttribute(i, "asset", "") != s.asset) continue;
        auto& point = next.mutablePoint(static_cast<std::size_t>(i));
        double width = s.minimumWidth, height = s.minimumHeight;
        if (s.scaleMode != TerrainTreeScaleMode::Fixed) {
            const auto inverse = [](double value, double minimum, double maximum) {
                if (minimum == maximum) return 0.0;
                return std::clamp((value - minimum) / (maximum - minimum), 0.0, 1.0);
            };
            width = std::lerp(double(s.minimumWidth), double(s.maximumWidth),
                              inverse(point.scaleX, s.previousMinimumWidth, s.previousMaximumWidth));
            height = std::lerp(double(s.minimumHeight), double(s.maximumHeight),
                               inverse(point.scaleY, s.previousMinimumHeight, s.previousMaximumHeight));
        }
        const double radius = double(s.boundsRadius) * width;
        if (!isRepresentable(width) || !isRepresentable(height) || !isRepresentable(radius))
            return invalid("terrain.rescaleTreePoints: refreshed scale is not representable");
        point.scaleX = point.scaleZ = float(width);
        point.scaleY = float(height);
        point.boundsMinX = point.boundsMinZ = float(-radius);
        point.boundsMaxX = point.boundsMaxZ = float(radius);
        point.boundsMaxY = float(height);
        auto bend = next.trySetFloatAttribute(i, "bendFactor", s.bendFactor);
        if (!bend.ok()) return Result<int>::failure(bend.status());
        ++matched;
    }
    static_assert(std::is_nothrow_move_assignable_v<PointSet>);
    output = std::move(next);
    return Result<int>::success(matched);
}

Result<TerrainMultiTileReport> applyTerrainTreesMultiTile(
    const std::vector<TerrainTreeTile>& tiles, const Heightmap& operationFitness,
    const TerrainTreePlacementSettings& s, const TerrainStampSettings& operationSettings,
    TerrainTreeOperationMode mode, bool worldMapOperation, const std::vector<std::string>& validTerrainNames) {
    using namespace raster_detail;
    const bool validMode = mode == TerrainTreeOperationMode::Add || mode == TerrainTreeOperationMode::Replace ||
                           mode == TerrainTreeOperationMode::Remove;
    if (tiles.empty() || !validMode || !validRaster(operationFitness) || !std::isfinite(s.heightScale) ||
        !std::isfinite(s.spacing) || s.spacing <= 0 || !std::isfinite(s.spawnDensity) || s.spawnDensity <= 0 ||
        !normalized(s.jitterPercent) || !normalized(s.failureRate) || !normalized(s.minimumFitness) ||
        !std::isfinite(s.seaLevel) || !std::isfinite(s.customOffset) || !std::isfinite(s.minimumYOffset) ||
        !std::isfinite(s.maximumYOffset) || s.maximumYOffset < s.minimumYOffset || !validScaleMode(s.scaleMode) ||
        !validYOffsetMode(s.yOffsetMode) || !std::isfinite(s.minimumWidth) || s.minimumWidth <= 0 ||
        !std::isfinite(s.maximumWidth) || s.maximumWidth < s.minimumWidth || !std::isfinite(s.minimumHeight) ||
        s.minimumHeight <= 0 || !std::isfinite(s.maximumHeight) || s.maximumHeight < s.minimumHeight ||
        !normalized(s.widthRandomPercentage) || !normalized(s.heightRandomPercentage) || !normalized(s.healthyR) ||
        !normalized(s.healthyG) || !normalized(s.healthyB) || !normalized(s.healthyA) || !normalized(s.dryR) ||
        !normalized(s.dryG) || !normalized(s.dryB) || !normalized(s.dryA) || !std::isfinite(s.bendFactor) ||
        s.bendFactor < 0 || !std::isfinite(s.boundsRadius) || s.boundsRadius <= 0 || s.namespaceId == 0 ||
        s.asset.empty() || s.maxPoints < 0)
        return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.tree.multitile: valid tiles, operation and prototype required"));
    for (float value : operationFitness.data())
        if (!normalized(value))
            return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "terrain.tree.multitile: normalized operation fitness required"));

    std::vector<TerrainOperationTile> operationTiles;
    std::unordered_set<PointSet*> owners;
    operationTiles.reserve(tiles.size());
    for (const auto& tile : tiles) {
        if (!tile.trees || !tile.heights || !owners.insert(tile.trees).second || !validRaster(*tile.heights))
            return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "terrain.tree.multitile: distinct outputs and finite heights required"));
        operationTiles.push_back({tile.name, tile.originX, tile.originZ, tile.width, tile.depth,
                                  tile.treeResolutionX, tile.treeResolutionY, tile.worldMap});
    }
    auto mapped = mapTerrainOperationMultiTile(operationTiles, operationSettings, TerrainOperationDomain::Tree,
                                                worldMapOperation, validTerrainNames);
    if (!mapped.ok()) return mapped;
    auto report = std::move(mapped.value());
    if (operationFitness.getWidth() != report.operationWidth ||
        operationFitness.getHeight() != report.operationHeight)
        return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.tree.multitile: operation fitness dimensions must match window"));
    const double increment = double(s.spacing) / s.spawnDensity;
    if (!isRepresentable(increment) || increment <= 0)
        return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.tree.multitile: effective spacing is not representable"));

    struct Candidate { PointSet* target; PointSet value; };
    std::vector<Candidate> candidates;
    candidates.reserve(report.mappings.size());
    PcgXorshiftPlus random(s.seed);
    int emitted = 0;
    for (const auto& mapping : report.mappings) {
        const auto tile = std::find_if(tiles.begin(), tiles.end(),
                                       [&](const auto& item) { return item.name == mapping.terrainName; });
        if (tile == tiles.end())
            return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
                DiagnosticCode::InvariantViolation, "terrain.tree.multitile: mapped terrain was not supplied"));
        PointSet next = *tile->trees;
        int changed = 0;
        if (mode == TerrainTreeOperationMode::Remove) {
            PointSet kept;
            kept.reserve(size_t(next.getCount()));
            for (int i = 0; i < next.getCount(); ++i) {
                const auto& point = next.points()[size_t(i)];
                bool remove = false;
                if (next.getStringAttribute(i, "asset", "") == s.asset) {
                    const int localX = nearestEven((double(point.x) - tile->originX) * tile->treeResolutionX / tile->width);
                    const int localY = nearestEven((double(point.z) - tile->originZ) * tile->treeResolutionY / tile->depth);
                    if (localX >= mapping.localX && localX < mapping.localX + mapping.width &&
                        localY >= mapping.localY && localY < mapping.localY + mapping.height) {
                        const int ox = mapping.operationX + localX - mapping.localX;
                        const int oy = mapping.operationY + localY - mapping.localY;
                        remove = operationFitness.height(ox, oy) > s.minimumFitness;
                    }
                }
                if (remove) ++changed;
                else {
                    auto appended = kept.appendPointFrom(next, size_t(i));
                    if (!appended.ok()) return Result<TerrainMultiTileReport>::failure(appended.status());
                }
            }
            next = std::move(kept);
        } else {
            const double startX = mapping.localX * tile->width / tile->treeResolutionX;
            const double startZ = mapping.localY * tile->depth / tile->treeResolutionY;
            const double stopX = (mapping.localX + mapping.width) * tile->width / tile->treeResolutionX + increment;
            const double stopZ = (mapping.localY + mapping.height) * tile->depth / tile->treeResolutionY + increment;
            const double jitter = increment * s.jitterPercent;
            std::uint64_t ordinal = 0;
            for (double x = startX; x <= stopX; x += increment)
                for (double z = startZ; z <= stopZ; z += increment, ++ordinal) {
                    if (random.next() < s.failureRate) continue;
                    const double xPos = x + random.next(float(-jitter), float(jitter));
                    const double zPos = z + random.next(float(-jitter), float(jitter));
                    const int localX = nearestEven(xPos * tile->treeResolutionX / tile->width);
                    const int localY = nearestEven(zPos * tile->treeResolutionY / tile->depth);
                    if (localX < mapping.localX || localX >= mapping.localX + mapping.width ||
                        localY < mapping.localY || localY >= mapping.localY + mapping.height) continue;
                    const float strength = operationFitness.height(mapping.operationX + localX - mapping.localX,
                                                                   mapping.operationY + localY - mapping.localY);
                    if (random.next(s.minimumFitness, 1) > strength) continue;
                    if (emitted >= s.maxPoints)
                        return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
                            DiagnosticCode::InvalidArgument, "terrain.tree.multitile: point budget exceeded"));
                    const double u = xPos / tile->width, v = zPos / tile->depth;
                    ProcgenPoint point;
                    point.id = mix(mix(s.namespaceId) ^ mix(stableNameHash(tile->name)) ^ mix(ordinal + 1));
                    if (!point.id) point.id = 1;
                    double worldY = sample(*tile->heights, u, v) * s.heightScale;
                    if (!s.snapToTerrain) {
                        if (s.yOffsetMode == TerrainTreeYOffsetMode::SeaLevel) worldY = s.seaLevel;
                        if (s.yOffsetMode == TerrainTreeYOffsetMode::Custom) worldY = s.customOffset;
                        worldY += random.next(s.minimumYOffset, s.maximumYOffset);
                    }
                    double width = s.minimumWidth, height = s.minimumHeight;
                    if (s.scaleMode == TerrainTreeScaleMode::Fitness || s.scaleMode == TerrainTreeScaleMode::FitnessRandomized) {
                        width = std::lerp(double(s.minimumWidth), double(s.maximumWidth), strength);
                        height = std::lerp(double(s.minimumHeight), double(s.maximumHeight), strength);
                    } else if (s.scaleMode == TerrainTreeScaleMode::Random) {
                        const double value = random.next();
                        width = std::lerp(double(s.minimumWidth), double(s.maximumWidth), value);
                        height = std::lerp(double(s.minimumHeight), double(s.maximumHeight), value);
                    }
                    if (s.scaleMode == TerrainTreeScaleMode::FitnessRandomized) {
                        const double value = random.next();
                        width *= std::lerp(1.0 - s.widthRandomPercentage, 1.0 + s.widthRandomPercentage, value);
                        height *= std::lerp(1.0 - s.heightRandomPercentage, 1.0 + s.heightRandomPercentage, value);
                    }
                    const double worldX = tile->originX + xPos, worldZ = tile->originZ + zPos;
                    const double radius = double(s.boundsRadius) * width;
                    if (!isRepresentable(worldX) || !isRepresentable(worldY) || !isRepresentable(worldZ) ||
                        !isRepresentable(width) || !isRepresentable(height) || !isRepresentable(radius))
                        return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
                            DiagnosticCode::InvalidArgument,
                            "terrain.tree.multitile: generated transform is not representable"));
                    point.x = float(worldX); point.y = float(worldY); point.z = float(worldZ);
                    point.yaw = random.next(0, 360); point.scaleX = point.scaleZ = float(width); point.scaleY = float(height);
                    point.density = strength; point.colorR = std::lerp(s.dryR, s.healthyR, strength);
                    point.colorG = std::lerp(s.dryG, s.healthyG, strength); point.colorB = std::lerp(s.dryB, s.healthyB, strength);
                    point.colorA = std::lerp(s.dryA, s.healthyA, strength);
                    point.seed = uint32_t(mix(point.id ^ uint32_t(s.seed)));
                    point.boundsMinX = point.boundsMinZ = -float(radius);
                    point.boundsMaxX = point.boundsMaxZ = float(radius);
                    point.boundsMaxY = float(height);
                    const int row = next.appendPoint(point);
                    auto asset = next.trySetStringAttribute(row, "asset", s.asset);
                    if (!asset.ok()) return Result<TerrainMultiTileReport>::failure(asset.status());
                    auto source = next.trySetStringAttribute(row, "spawnNamespace", std::to_string(s.namespaceId));
                    if (!source.ok()) return Result<TerrainMultiTileReport>::failure(source.status());
                    const auto candidateIdentity = static_cast<std::int64_t>(
                        mix(stableNameHash(tile->name)) ^ mix(ordinal + 1));
                    auto candidate = next.trySetIntAttribute(row, "treeCandidate", candidateIdentity);
                    if (!candidate.ok()) return Result<TerrainMultiTileReport>::failure(candidate.status());
                    auto bend = next.trySetFloatAttribute(row, "bendFactor", s.bendFactor);
                    if (!bend.ok()) return Result<TerrainMultiTileReport>::failure(bend.status());
                    ++emitted;
                    ++changed;
                }
        }
        if (report.changedSamples > std::numeric_limits<int>::max() - changed)
            return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "terrain.tree.multitile: changed point count exceeds integer range"));
        report.changedSamples += changed;
        candidates.push_back({tile->trees, std::move(next)});
    }
    for (auto& candidate : candidates) *candidate.target = std::move(candidate.value);
    return Result<TerrainMultiTileReport>::success(std::move(report));
}

struct TerrainMultiTreeWorkspace::Impl {
    struct Tile {
        std::string name;
        PointSet trees;
        Heightmap heights;
        double originX = 0, originZ = 0, width = 1, depth = 1;
        int resolutionX = 0, resolutionY = 0;
        bool worldMap = false;
    };
    struct Snapshot {
        std::vector<PointSet> trees;
        TerrainMultiTileReport report;
    };
    std::vector<Tile> tiles;
    TerrainMultiTileReport last;
    std::vector<Snapshot> history;
    int cursor = 0;
    Snapshot snapshot() const {
        Snapshot result;
        result.trees.reserve(tiles.size());
        for (const auto& tile : tiles) result.trees.push_back(tile.trees);
        result.report = last;
        return result;
    }
    void restore(const Snapshot& snapshot) {
        for (size_t i = 0; i < tiles.size(); ++i) tiles[i].trees = snapshot.trees[i];
        last = snapshot.report;
    }
};

namespace {
Result<int> invalidTreeWorkspace(const char* message) {
    return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message));
}
}  // namespace

TerrainMultiTreeWorkspace::TerrainMultiTreeWorkspace() : impl_(std::make_unique<Impl>()) {}
TerrainMultiTreeWorkspace::~TerrainMultiTreeWorkspace() = default;
TerrainMultiTreeWorkspace::TerrainMultiTreeWorkspace(TerrainMultiTreeWorkspace&&) noexcept = default;
TerrainMultiTreeWorkspace& TerrainMultiTreeWorkspace::operator=(TerrainMultiTreeWorkspace&&) noexcept = default;

Result<int> TerrainMultiTreeWorkspace::addTile(const std::string& name, const PointSet& trees,
                                                const Heightmap& heights, double originX, double originZ,
                                                double width, double depth, int resolutionX, int resolutionY,
                                                bool worldMap) {
    if (!impl_ || name.empty() || !raster_detail::validRaster(heights))
        return invalidTreeWorkspace("terrain.tree.workspace: initialized named tile required");
    if (std::any_of(impl_->tiles.begin(), impl_->tiles.end(), [&](const auto& tile) { return tile.name == name; }))
        return invalidTreeWorkspace("terrain.tree.workspace: duplicate tile name");
    if (impl_->history.size() > 1)
        return invalidTreeWorkspace("terrain.tree.workspace: topology is fixed after applying");
    auto candidate = std::make_unique<Impl>(*impl_);
    candidate->tiles.push_back({name, trees, heights, originX, originZ, width, depth, resolutionX, resolutionY,
                                worldMap});
    std::vector<TerrainOperationTile> descriptors;
    for (const auto& tile : candidate->tiles)
        descriptors.push_back({tile.name, tile.originX, tile.originZ, tile.width, tile.depth, tile.resolutionX,
                               tile.resolutionY, tile.worldMap});
    TerrainStampSettings bounds;
    bounds.centerX = originX + width * 0.5;
    bounds.centerZ = originZ + depth * 0.5;
    bounds.width = width;
    bounds.depth = depth;
    auto checked = mapTerrainOperationMultiTile(descriptors, bounds, TerrainOperationDomain::Tree, worldMap, {name});
    if (!checked.ok()) return Result<int>::failure(checked.status());
    candidate->history.clear();
    candidate->history.push_back(candidate->snapshot());
    candidate->cursor = 0;
    impl_.swap(candidate);
    return Result<int>::success(int(impl_->tiles.size()));
}

Result<int> TerrainMultiTreeWorkspace::apply(const Heightmap& operationFitness,
                                              const TerrainTreePlacementSettings& settings,
                                              const TerrainStampSettings& operationSettings,
                                              TerrainTreeOperationMode mode, bool worldMapOperation) {
    if (!impl_) return invalidTreeWorkspace("terrain.tree.workspace: moved-from workspace");
    auto candidate = std::make_unique<Impl>(*impl_);
    std::vector<TerrainTreeTile> descriptors;
    for (auto& tile : candidate->tiles)
        descriptors.push_back({tile.name, &tile.trees, &tile.heights, tile.originX, tile.originZ, tile.width,
                               tile.depth, tile.resolutionX, tile.resolutionY, tile.worldMap});
    auto applied = applyTerrainTreesMultiTile(descriptors, operationFitness, settings, operationSettings, mode,
                                               worldMapOperation);
    if (!applied.ok()) return Result<int>::failure(applied.status());
    candidate->last = std::move(applied.value());
    candidate->history.resize(size_t(candidate->cursor + 1));
    candidate->history.push_back(candidate->snapshot());
    ++candidate->cursor;
    const int changed = candidate->last.changedSamples;
    impl_.swap(candidate);
    return Result<int>::success(changed);
}

Result<int> TerrainMultiTreeWorkspace::copyTile(const std::string& name, PointSet& output) const {
    if (!impl_) return invalidTreeWorkspace("terrain.tree.workspace: moved-from workspace");
    const auto found = std::find_if(impl_->tiles.begin(), impl_->tiles.end(),
                                    [&](const auto& tile) { return tile.name == name; });
    if (found == impl_->tiles.end()) return invalidTreeWorkspace("terrain.tree.workspace: tile not found");
    PointSet copy = found->trees;
    output = std::move(copy);
    return Result<int>::success(output.getCount());
}
Result<int> TerrainMultiTreeWorkspace::undo() {
    if (!impl_ || impl_->cursor <= 0) return invalidTreeWorkspace("terrain.tree.workspace: no undo snapshot");
    --impl_->cursor;
    impl_->restore(impl_->history[size_t(impl_->cursor)]);
    return Result<int>::success(impl_->cursor);
}
Result<int> TerrainMultiTreeWorkspace::redo() {
    if (!impl_ || impl_->cursor + 1 >= int(impl_->history.size()))
        return invalidTreeWorkspace("terrain.tree.workspace: no redo snapshot");
    ++impl_->cursor;
    impl_->restore(impl_->history[size_t(impl_->cursor)]);
    return Result<int>::success(impl_->cursor);
}
int TerrainMultiTreeWorkspace::getTileCount() const noexcept { return impl_ ? int(impl_->tiles.size()) : 0; }
int TerrainMultiTreeWorkspace::getLastChangedSamples() const noexcept {
    return impl_ ? impl_->last.changedSamples : 0;
}
int TerrainMultiTreeWorkspace::getLastAffectedTiles() const noexcept {
    return impl_ ? impl_->last.affectedTiles : 0;
}
int TerrainMultiTreeWorkspace::getOperationCount() const noexcept {
    return impl_ ? std::max(0, int(impl_->history.size()) - 1) : 0;
}
int TerrainMultiTreeWorkspace::getAppliedCount() const noexcept { return impl_ ? impl_->cursor : 0; }
}  // namespace eve::procgen
