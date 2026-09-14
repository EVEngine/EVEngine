#include "procgen/heightmap/TerrainObjectPlacement.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <unordered_set>

#include "procgen/PointSet.h"
#include "procgen/heightmap/TerrainRasterInternal.h"
#include "procgen/heightmap/TerrainMultiTile.h"

namespace eve::procgen {
namespace {
class PcgRandom {
public:
    explicit PcgRandom(std::int32_t seed) {
        const auto value = seed == 0 ? 1U : static_cast<std::uint32_t>(seed);
        a_ = UINT64_C(181353) * value; b_ = UINT64_C(7) * value;
    }
    float next() {
        auto x = a_, y = b_; a_ = y; x ^= x << 23; x ^= x >> 17; x ^= y ^ (y >> 26); b_ = x;
        return static_cast<float>(x + y) / static_cast<float>(std::numeric_limits<std::uint64_t>::max());
    }
    float next(float minimum, float maximum) { return minimum + next() * (maximum - minimum); }
    int next(int minimum, int maximum) {
        if (minimum == maximum) return minimum;
        return static_cast<int>(next(float(minimum), float(maximum) + 0.999F));
    }
private:
    std::uint64_t a_ = 0, b_ = 0;
};
std::uint64_t mix(std::uint64_t value) {
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31);
}
bool normalized(float value) { return std::isfinite(value) && value >= 0 && value <= 1; }
bool ordered(float minimum, float maximum) { return std::isfinite(minimum) && std::isfinite(maximum) && maximum >= minimum; }
bool positive(float value) { return std::isfinite(value) && value > 0; }
int nearestEven(double value) { return static_cast<int>(std::nearbyint(value)); }
bool validScale(TerrainObjectScaleMode mode) {
    return mode >= TerrainObjectScaleMode::Fixed && mode <= TerrainObjectScaleMode::FitnessRandomized;
}
bool validYOffset(TerrainObjectYOffsetMode mode) {
    return mode >= TerrainObjectYOffsetMode::TerrainHeight && mode <= TerrainObjectYOffsetMode::Custom;
}
struct Normal { double x = 0, y = 1, z = 0; };
Normal normalAt(const Heightmap& heights, double u, double v, double width, double depth, double heightScale) {
    const double du = heights.getWidth() > 1 ? 1.0 / (heights.getWidth() - 1) : 1;
    const double dv = heights.getHeight() > 1 ? 1.0 / (heights.getHeight() - 1) : 1;
    const double dx = (raster_detail::sample(heights, std::min(1.0, u + du), v) -
                       raster_detail::sample(heights, std::max(0.0, u - du), v)) * heightScale;
    const double dz = (raster_detail::sample(heights, u, std::min(1.0, v + dv)) -
                       raster_detail::sample(heights, u, std::max(0.0, v - dv))) * heightScale;
    const double sx = std::max(du * width * 2, std::numeric_limits<double>::epsilon());
    const double sz = std::max(dv * depth * 2, std::numeric_limits<double>::epsilon());
    Normal n{-dx / sx, 1, -dz / sz};
    const double length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    n.x /= length; n.y /= length; n.z /= length; return n;
}
Result<int> invalid(const char* message) {
    return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message));
}
bool validInstance(const TerrainObjectInstanceSettings& i) {
    const auto scales = ordered(i.minimumScale, i.maximumScale) && positive(i.minimumScale) &&
        ordered(i.minimumScaleX, i.maximumScaleX) && positive(i.minimumScaleX) &&
        ordered(i.minimumScaleY, i.maximumScaleY) && positive(i.minimumScaleY) &&
        ordered(i.minimumScaleZ, i.maximumScaleZ) && positive(i.minimumScaleZ);
    return !i.asset.empty() && i.minimumInstances >= 0 && i.maximumInstances >= i.minimumInstances &&
        normalized(i.failureRate) && ordered(i.minimumOffsetX, i.maximumOffsetX) &&
        ordered(i.minimumOffsetY, i.maximumOffsetY) && ordered(i.minimumOffsetZ, i.maximumOffsetZ) &&
        validYOffset(i.yOffsetMode) && std::isfinite(i.customOffset) && validScale(i.scaleMode) && scales &&
        normalized(i.scaleRandomPercentage) && normalized(i.scaleRandomPercentageX) &&
        normalized(i.scaleRandomPercentageY) && normalized(i.scaleRandomPercentageZ) &&
        ordered(i.minimumRotationX, i.maximumRotationX) && ordered(i.minimumRotationY, i.maximumRotationY) &&
        ordered(i.minimumRotationZ, i.maximumRotationZ);
}
std::uint64_t stableNameHash(const std::string& value) {
    std::uint64_t hash = UINT64_C(1469598103934665603);
    for (const unsigned char ch : value) { hash ^= ch; hash *= UINT64_C(1099511628211); }
    return hash;
}
}  // namespace

Result<int> TerrainObjectPlacementSettings::addInstance(const TerrainObjectInstanceSettings& instance) {
    if (!validInstance(instance)) return invalid("terrain.objectSettings: valid resource instance required");
    instances.push_back(instance);
    return Result<int>::success(static_cast<int>(instances.size()));
}

Result<int> exportTerrainObjectPoints(PointSet& output, const Heightmap& fitness, const Heightmap& heights,
                                      const TerrainObjectPlacementSettings& s) {
    using namespace raster_detail;
    if (!validRaster(fitness) || !validRaster(heights) || !std::isfinite(s.originX) || !std::isfinite(s.originZ) ||
        !positive(s.width) || !positive(s.depth) || !std::isfinite(s.heightScale) || !positive(s.spacing) ||
        !positive(s.spawnDensity) || !normalized(s.jitterPercent) || !normalized(s.failureRate) ||
        !normalized(s.minimumFitness) || !normalized(s.minimumInstanceFitness) ||
        !ordered(s.minimumDirection, s.maximumDirection) || !positive(s.boundsRadius) ||
        !std::isfinite(s.boundsCheckQuality) || s.boundsCheckQuality < 0 || s.boundsCheckQuality > 100 ||
        !positive(s.prototypeScale) || !std::isfinite(s.startOffsetX) || !std::isfinite(s.startOffsetZ) ||
        !std::isfinite(s.seaLevel) || s.namespaceId == 0 || s.maxPoints < 0 || s.prototype.empty() ||
        s.instances.empty() || !std::all_of(s.instances.begin(), s.instances.end(), validInstance))
        return invalid("terrain.objectPoints: valid rasters, prototype, instances, probabilities and transforms required");
    if (!std::all_of(fitness.data().begin(), fitness.data().end(), normalized))
        return invalid("terrain.objectPoints: normalized fitness required");
    const double increment = double(s.spacing) / s.spawnDensity;
    if (!isRepresentable(increment) || increment <= 0) return invalid("terrain.objectPoints: spacing is not representable");
    PointSet next;
    PcgRandom random(s.seed);
    std::vector<std::pair<double, double>> acceptedCenters;
    int emitted = 0;
    std::uint64_t candidate = 0;
    const double jitter = double(s.spacing) * s.jitterPercent;
    for (double x = s.startOffsetX; x <= double(s.width) + increment; x += increment) {
        for (double z = s.startOffsetZ; z <= double(s.depth) + increment; z += increment, ++candidate) {
            if (random.next() < s.failureRate) continue;
            const double localX = x + random.next(float(-jitter), float(jitter)) * 0.5;
            const double localZ = z + random.next(float(-jitter), float(jitter)) * 0.5;
            if (localX < 0 || localZ < 0 || localX > s.width || localZ > s.depth) continue;
            const double u = localX / s.width, v = localZ / s.depth;
            const double strength = textureSample(fitness, u, v);
            if (random.next(s.minimumFitness, 1.0F) > strength) continue;
            const double centerX = s.originX + localX, centerZ = s.originZ + localZ;
            if (s.boundsCollisionCheck && std::any_of(acceptedCenters.begin(), acceptedCenters.end(), [&](const auto& p) {
                    return std::hypot(centerX - p.first, centerZ - p.second) < double(s.boundsRadius) * 2;
                })) continue;
            const int rx = std::max(0, int(std::round(s.boundsRadius * (fitness.getWidth() - 1) / s.width)));
            const int rz = std::max(0, int(std::round(s.boundsRadius * (fitness.getHeight() - 1) / s.depth)));
            const int cx = int(std::round(u * (fitness.getWidth() - 1))), cz = int(std::round(v * (fitness.getHeight() - 1)));
            const int stepX = std::max(1, int(std::ceil(2 * rx * (1 - s.boundsCheckQuality / 100.0))));
            const int stepZ = std::max(1, int(std::ceil(2 * rz * (1 - s.boundsCheckQuality / 100.0))));
            double sum = 0; int checks = 0;
            for (int px = cx - rx; px <= cx + rx; px += stepX)
                for (int pz = cz - rz; pz <= cz + rz; pz += stepZ) {
                    if (px >= 0 && pz >= 0 && px < fitness.getWidth() && pz < fitness.getHeight()) sum += fitness.height(px, pz);
                    ++checks;
                }
            if (sum / std::max(1, checks) < s.minimumFitness) continue;
            const float direction = random.next(s.minimumDirection, s.maximumDirection);
            acceptedCenters.emplace_back(centerX, centerZ);
            for (std::size_t resource = 0; resource < s.instances.size(); ++resource) {
                const auto& instance = s.instances[resource];
                const int count = random.next(instance.minimumInstances, instance.maximumInstances);
                for (int ordinal = 0; ordinal < count; ++ordinal) {
                    if (random.next() < instance.failureRate) continue;
                    const double offsetX = random.next(instance.minimumOffsetX, instance.maximumOffsetX) * s.prototypeScale;
                    const double offsetZ = random.next(instance.minimumOffsetZ, instance.maximumOffsetZ) * s.prototypeScale;
                    const double radians = direction * 3.14159265358979323846 / 180.0;
                    const double ix = centerX + std::cos(radians) * offsetX - std::sin(radians) * offsetZ;
                    const double iz = centerZ + std::sin(radians) * offsetX + std::cos(radians) * offsetZ;
                    const double iu = (ix - s.originX) / s.width, iv = (iz - s.originZ) / s.depth;
                    if (iu < 0 || iv < 0 || iu > 1 || iv > 1) continue;
                    const double instanceStrength = textureSample(fitness, iu, iv);
                    if (instanceStrength < s.minimumInstanceFitness) continue;
                    if (emitted >= s.maxPoints) return invalid("terrain.objectPoints: point budget exceeded");
                    const Normal normal = normalAt(heights, iu, iv, s.width, s.depth, s.heightScale);
                    double iy = sample(heights, iu, iv) * s.heightScale;
                    if (instance.yOffsetMode == TerrainObjectYOffsetMode::SeaLevel) iy = s.seaLevel;
                    if (instance.yOffsetMode == TerrainObjectYOffsetMode::Custom) iy = instance.customOffset;
                    double sx = instance.minimumScaleX, sy = instance.minimumScaleY, sz = instance.minimumScaleZ;
                    if (instance.commonScale) sx = sy = sz = instance.minimumScale;
                    if (instance.scaleMode == TerrainObjectScaleMode::Random) {
                        if (instance.commonScale) sx = sy = sz = random.next(instance.minimumScale, instance.maximumScale);
                        else { sx = random.next(instance.minimumScaleX, instance.maximumScaleX); sy = random.next(instance.minimumScaleY, instance.maximumScaleY); sz = random.next(instance.minimumScaleZ, instance.maximumScaleZ); }
                    } else if (instance.scaleMode == TerrainObjectScaleMode::Fitness || instance.scaleMode == TerrainObjectScaleMode::FitnessRandomized) {
                        if (instance.commonScale) sx = sy = sz = std::lerp(instance.minimumScale, instance.maximumScale, instanceStrength);
                        else { sx = std::lerp(instance.minimumScaleX, instance.maximumScaleX, instanceStrength); sy = std::lerp(instance.minimumScaleY, instance.maximumScaleY, instanceStrength); sz = std::lerp(instance.minimumScaleZ, instance.maximumScaleZ, instanceStrength); }
                    }
                    if (instance.scaleMode == TerrainObjectScaleMode::FitnessRandomized) {
                        if (instance.commonScale) { const double factor = random.next(1 - instance.scaleRandomPercentage, 1 + instance.scaleRandomPercentage); sx *= factor; sy *= factor; sz *= factor; }
                        else { sx *= random.next(1 - instance.scaleRandomPercentageX, 1 + instance.scaleRandomPercentageX); sy *= random.next(1 - instance.scaleRandomPercentageY, 1 + instance.scaleRandomPercentageY); sz *= random.next(1 - instance.scaleRandomPercentageZ, 1 + instance.scaleRandomPercentageZ); }
                    }
                    sx *= s.prototypeScale; sy *= s.prototypeScale; sz *= s.prototypeScale;
                    const double yOffset = random.next(instance.minimumOffsetY, instance.maximumOffsetY) * sy;
                    double finalX = ix, finalY = iy, finalZ = iz;
                    if (instance.yOffsetAlongSlope) { finalX += normal.x * yOffset; finalY += normal.y * yOffset; finalZ += normal.z * yOffset; }
                    else finalY += yOffset;
                    double pitch = random.next(instance.minimumRotationX, instance.maximumRotationX);
                    double yaw = random.next(instance.minimumRotationY + direction, instance.maximumRotationY + direction);
                    double roll = random.next(instance.minimumRotationZ, instance.maximumRotationZ);
                    if (instance.alignForwardToSlope) yaw += std::atan2(normal.x, normal.z) * 180.0 / 3.14159265358979323846;
                    if (instance.rotateToSlope) { pitch += std::atan2(-normal.z, normal.y) * 180.0 / 3.14159265358979323846; roll += std::atan2(normal.x, normal.y) * 180.0 / 3.14159265358979323846; }
                    const double radius = s.boundsRadius * std::max(sx, sz);
                    if (!isRepresentable(finalX) || !isRepresentable(finalY) || !isRepresentable(finalZ) ||
                        !isRepresentable(sx) || sx <= 0 || !isRepresentable(sy) || sy <= 0 ||
                        !isRepresentable(sz) || sz <= 0 || !isRepresentable(radius) || radius <= 0 ||
                        !isRepresentable(pitch) || !isRepresentable(yaw) || !isRepresentable(roll))
                        return invalid("terrain.objectPoints: generated transform is not representable");
                    ProcgenPoint point;
                    point.id = mix(mix(mix(mix(s.namespaceId) ^ (candidate + 1)) ^ (resource + 1)) ^
                                   (std::uint64_t(ordinal) + 1));
                    if (!point.id) return invalid("terrain.objectPoints: generated identity is reserved");
                    point.x = float(finalX); point.y = float(finalY); point.z = float(finalZ);
                    point.normalX = float(normal.x); point.normalY = float(normal.y); point.normalZ = float(normal.z);
                    point.pitch = float(pitch); point.yaw = float(yaw); point.roll = float(roll);
                    point.scaleX = float(sx); point.scaleY = float(sy); point.scaleZ = float(sz); point.density = instanceStrength;
                    point.boundsMinX = point.boundsMinZ = -float(radius); point.boundsMaxX = point.boundsMaxZ = float(radius); point.boundsMaxY = float(sy);
                    point.seed = static_cast<std::uint32_t>(mix(point.id ^ static_cast<std::uint32_t>(s.seed)));
                    const int row = next.appendPoint(point);
                    auto asset = next.trySetStringAttribute(row, "asset", instance.asset); if (!asset.ok()) return Result<int>::failure(asset.status());
                    auto prototype = next.trySetStringAttribute(row, "objectPrototype", s.prototype); if (!prototype.ok()) return Result<int>::failure(prototype.status());
                    auto source = next.trySetStringAttribute(row, "spawnNamespace", std::to_string(s.namespaceId)); if (!source.ok()) return Result<int>::failure(source.status());
                    auto candidateAttr = next.trySetIntAttribute(row, "objectCandidate", static_cast<std::int64_t>(candidate)); if (!candidateAttr.ok()) return Result<int>::failure(candidateAttr.status());
                    auto resourceAttr = next.trySetIntAttribute(row, "objectResource", static_cast<std::int64_t>(resource)); if (!resourceAttr.ok()) return Result<int>::failure(resourceAttr.status());
                    ++emitted;
                }
            }
        }
    }
    static_assert(std::is_nothrow_move_assignable_v<PointSet>);
    output = std::move(next);
    return Result<int>::success(emitted);
}

Result<int> removeTerrainObjectPoints(PointSet& output, const PointSet& input, const Heightmap& fitness,
                                      const TerrainObjectPlacementSettings& s, float removalStrength) {
    using namespace raster_detail;
    if (!validRaster(fitness) || !std::all_of(fitness.data().begin(), fitness.data().end(), normalized) ||
        !std::isfinite(s.originX) || !std::isfinite(s.originZ) || !positive(s.width) || !positive(s.depth) ||
        s.prototype.empty() || !normalized(removalStrength))
        return invalid("terrain.removeObjectPoints: valid fitness, domain, prototype and threshold required");
    PointSet next; next.reserve(static_cast<std::size_t>(input.getCount())); int removed = 0;
    for (int i = 0; i < input.getCount(); ++i) {
        const auto& point = input.points()[static_cast<std::size_t>(i)];
        const double u = (point.x - s.originX) / s.width, v = (point.z - s.originZ) / s.depth;
        const bool erase = input.getStringAttribute(i, "objectPrototype", "") == s.prototype &&
            u >= 0 && u <= 1 && v >= 0 && v <= 1 && textureSample(fitness, u, v) > removalStrength;
        if (erase) ++removed;
        else { auto copied = next.appendPointFrom(input, static_cast<std::size_t>(i)); if (!copied.ok()) return Result<int>::failure(copied.status()); }
    }
    output = std::move(next); return Result<int>::success(removed);
}

Result<TerrainMultiTileReport> applyTerrainObjectsMultiTile(
    const std::vector<TerrainObjectTile>& tiles, const Heightmap& fitness,
    const TerrainObjectPlacementSettings& s, const TerrainStampSettings& operationSettings,
    TerrainObjectOperationMode mode, bool worldMapOperation, const std::vector<std::string>& validTerrainNames) {
    using namespace raster_detail;
    const bool validMode = mode == TerrainObjectOperationMode::Add || mode == TerrainObjectOperationMode::Replace ||
                           mode == TerrainObjectOperationMode::Remove;
    if (tiles.empty() || !validMode || !validRaster(fitness) || !positive(s.spacing) ||
        !positive(s.spawnDensity) || !normalized(s.jitterPercent) || !normalized(s.failureRate) ||
        !normalized(s.minimumFitness) || !normalized(s.minimumInstanceFitness) ||
        !ordered(s.minimumDirection, s.maximumDirection) || !positive(s.boundsRadius) ||
        !std::isfinite(s.boundsCheckQuality) || s.boundsCheckQuality < 0 || s.boundsCheckQuality > 100 ||
        !positive(s.prototypeScale) || !std::isfinite(s.startOffsetX) || !std::isfinite(s.startOffsetZ) ||
        !std::isfinite(s.heightScale) || !std::isfinite(s.seaLevel) || s.namespaceId == 0 || s.maxPoints < 0 ||
        s.prototype.empty() || s.instances.empty() || !std::all_of(s.instances.begin(), s.instances.end(), validInstance))
        return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.object.multitile: valid tiles, operation and prototype required"));
    if (!std::all_of(fitness.data().begin(), fitness.data().end(), normalized))
        return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.object.multitile: normalized operation fitness required"));
    std::vector<TerrainOperationTile> descriptors;
    std::unordered_set<PointSet*> owners;
    for (const auto& tile : tiles) {
        if (!tile.objects || !tile.heights || !owners.insert(tile.objects).second || !validRaster(*tile.heights))
            return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "terrain.object.multitile: distinct outputs and finite heights required"));
        descriptors.push_back({tile.name, tile.originX, tile.originZ, tile.width, tile.depth,
                               tile.objectResolutionX, tile.objectResolutionY, tile.worldMap});
    }
    auto mapped = mapTerrainOperationMultiTile(descriptors, operationSettings, TerrainOperationDomain::GameObject,
                                                worldMapOperation, validTerrainNames);
    if (!mapped.ok()) return mapped;
    auto report = std::move(mapped.value());
    if (fitness.getWidth() != report.operationWidth || fitness.getHeight() != report.operationHeight)
        return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.object.multitile: fitness dimensions must match operation window"));
    const double increment = double(s.spacing) / s.spawnDensity;
    if (!isRepresentable(increment) || increment <= 0)
        return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.object.multitile: effective spacing is not representable"));

    struct Candidate { PointSet* target; PointSet value; };
    std::vector<Candidate> candidates;
    PcgRandom random(s.seed);
    std::vector<std::pair<double, double>> acceptedCenters;
    int emitted = 0;
    std::uint64_t globalCandidate = 0;
    for (const auto& mapping : report.mappings) {
        const auto tile = std::find_if(tiles.begin(), tiles.end(), [&](const auto& item) {
            return item.name == mapping.terrainName;
        });
        if (tile == tiles.end())
            return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
                DiagnosticCode::InvariantViolation, "terrain.object.multitile: mapped terrain was not supplied"));
        PointSet next;
        int changed = 0;
        const bool clearFirst = mode == TerrainObjectOperationMode::Replace || mode == TerrainObjectOperationMode::Remove;
        if (clearFirst) {
            next.reserve(static_cast<std::size_t>(tile->objects->getCount()));
            for (int row = 0; row < tile->objects->getCount(); ++row) {
                const auto& point = tile->objects->points()[static_cast<std::size_t>(row)];
                const int lx = nearestEven((double(point.x) - tile->originX) * tile->objectResolutionX / tile->width);
                const int ly = nearestEven((double(point.z) - tile->originZ) * tile->objectResolutionY / tile->depth);
                bool erase = tile->objects->getStringAttribute(row, "objectPrototype", "") == s.prototype &&
                             lx >= mapping.localX && lx < mapping.localX + mapping.width &&
                             ly >= mapping.localY && ly < mapping.localY + mapping.height;
                if (erase && mode == TerrainObjectOperationMode::Remove)
                    erase = fitness.height(mapping.operationX + lx - mapping.localX,
                                           mapping.operationY + ly - mapping.localY) > s.minimumFitness;
                if (erase) ++changed;
                else {
                    auto copied = next.appendPointFrom(*tile->objects, static_cast<std::size_t>(row));
                    if (!copied.ok()) return Result<TerrainMultiTileReport>::failure(copied.status());
                }
            }
        } else next = *tile->objects;

        if (mode != TerrainObjectOperationMode::Remove) {
            const double startX = mapping.localX * tile->width / tile->objectResolutionX + s.startOffsetX;
            const double startZ = mapping.localY * tile->depth / tile->objectResolutionY + s.startOffsetZ;
            const double stopX = (mapping.localX + mapping.width) * tile->width / tile->objectResolutionX + increment;
            const double stopZ = (mapping.localY + mapping.height) * tile->depth / tile->objectResolutionY + increment;
            const double jitter = double(s.spacing) * s.jitterPercent;
            for (double x = startX; x <= stopX; x += increment) for (double z = startZ; z <= stopZ; z += increment, ++globalCandidate) {
                if (random.next() < s.failureRate) continue;
                const double localX = x + random.next(float(-jitter), float(jitter)) * 0.5;
                const double localZ = z + random.next(float(-jitter), float(jitter)) * 0.5;
                const int lx = nearestEven(localX * tile->objectResolutionX / tile->width);
                const int ly = nearestEven(localZ * tile->objectResolutionY / tile->depth);
                if (lx < mapping.localX || lx >= mapping.localX + mapping.width ||
                    ly < mapping.localY || ly >= mapping.localY + mapping.height) continue;
                const float strength = fitness.height(mapping.operationX + lx - mapping.localX,
                                                      mapping.operationY + ly - mapping.localY);
                if (random.next(s.minimumFitness, 1.0F) > strength) continue;
                const double centerX = tile->originX + localX, centerZ = tile->originZ + localZ;
                if (s.boundsCollisionCheck && std::any_of(acceptedCenters.begin(), acceptedCenters.end(), [&](const auto& p) {
                        return std::hypot(centerX - p.first, centerZ - p.second) < double(s.boundsRadius) * 2;
                    })) continue;
                double sum = 0; int checks = 0;
                const int rx = std::max(0, int(std::round(s.boundsRadius * tile->objectResolutionX / tile->width)));
                const int ry = std::max(0, int(std::round(s.boundsRadius * tile->objectResolutionY / tile->depth)));
                const int stepX = std::max(1, int(std::ceil(2 * rx * (1 - s.boundsCheckQuality / 100.0))));
                const int stepY = std::max(1, int(std::ceil(2 * ry * (1 - s.boundsCheckQuality / 100.0))));
                for (int px = lx - rx; px <= lx + rx; px += stepX) for (int py = ly - ry; py <= ly + ry; py += stepY) {
                    if (px >= mapping.localX && px < mapping.localX + mapping.width && py >= mapping.localY && py < mapping.localY + mapping.height)
                        sum += fitness.height(mapping.operationX + px - mapping.localX, mapping.operationY + py - mapping.localY);
                    ++checks;
                }
                if (sum / std::max(1, checks) < s.minimumFitness) continue;
                const float direction = random.next(s.minimumDirection, s.maximumDirection);
                acceptedCenters.emplace_back(centerX, centerZ);
                for (std::size_t resource = 0; resource < s.instances.size(); ++resource) {
                    const auto& instance = s.instances[resource];
                    const int count = random.next(instance.minimumInstances, instance.maximumInstances);
                    for (int ordinal = 0; ordinal < count; ++ordinal) {
                        if (random.next() < instance.failureRate) continue;
                        const double radians = direction * 3.14159265358979323846 / 180.0;
                        const double ox = random.next(instance.minimumOffsetX, instance.maximumOffsetX) * s.prototypeScale;
                        const double oz = random.next(instance.minimumOffsetZ, instance.maximumOffsetZ) * s.prototypeScale;
                        const double ix = centerX + std::cos(radians) * ox - std::sin(radians) * oz;
                        const double iz = centerZ + std::sin(radians) * ox + std::cos(radians) * oz;
                        const int ilx = nearestEven((ix - tile->originX) * tile->objectResolutionX / tile->width);
                        const int ily = nearestEven((iz - tile->originZ) * tile->objectResolutionY / tile->depth);
                        if (ilx < mapping.localX || ilx >= mapping.localX + mapping.width ||
                            ily < mapping.localY || ily >= mapping.localY + mapping.height) continue;
                        const float instanceStrength = fitness.height(mapping.operationX + ilx - mapping.localX,
                                                                      mapping.operationY + ily - mapping.localY);
                        if (instanceStrength < s.minimumInstanceFitness) continue;
                        if (emitted >= s.maxPoints)
                            return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
                                DiagnosticCode::InvalidArgument, "terrain.object.multitile: point budget exceeded"));
                        const double u = (ix - tile->originX) / tile->width, v = (iz - tile->originZ) / tile->depth;
                        const Normal normal = normalAt(*tile->heights, u, v, tile->width, tile->depth, s.heightScale);
                        double iy = sample(*tile->heights, u, v) * s.heightScale;
                        if (instance.yOffsetMode == TerrainObjectYOffsetMode::SeaLevel) iy = s.seaLevel;
                        if (instance.yOffsetMode == TerrainObjectYOffsetMode::Custom) iy = instance.customOffset;
                        double sx = instance.commonScale ? instance.minimumScale : instance.minimumScaleX;
                        double sy = instance.commonScale ? instance.minimumScale : instance.minimumScaleY;
                        double sz = instance.commonScale ? instance.minimumScale : instance.minimumScaleZ;
                        if (instance.scaleMode == TerrainObjectScaleMode::Random) {
                            if (instance.commonScale) sx = sy = sz = random.next(instance.minimumScale, instance.maximumScale);
                            else { sx = random.next(instance.minimumScaleX, instance.maximumScaleX); sy = random.next(instance.minimumScaleY, instance.maximumScaleY); sz = random.next(instance.minimumScaleZ, instance.maximumScaleZ); }
                        } else if (instance.scaleMode == TerrainObjectScaleMode::Fitness || instance.scaleMode == TerrainObjectScaleMode::FitnessRandomized) {
                            if (instance.commonScale) sx = sy = sz = std::lerp(instance.minimumScale, instance.maximumScale, instanceStrength);
                            else { sx = std::lerp(instance.minimumScaleX, instance.maximumScaleX, instanceStrength); sy = std::lerp(instance.minimumScaleY, instance.maximumScaleY, instanceStrength); sz = std::lerp(instance.minimumScaleZ, instance.maximumScaleZ, instanceStrength); }
                        }
                        if (instance.scaleMode == TerrainObjectScaleMode::FitnessRandomized) {
                            if (instance.commonScale) { const double factor = random.next(1 - instance.scaleRandomPercentage, 1 + instance.scaleRandomPercentage); sx *= factor; sy *= factor; sz *= factor; }
                            else { sx *= random.next(1 - instance.scaleRandomPercentageX, 1 + instance.scaleRandomPercentageX); sy *= random.next(1 - instance.scaleRandomPercentageY, 1 + instance.scaleRandomPercentageY); sz *= random.next(1 - instance.scaleRandomPercentageZ, 1 + instance.scaleRandomPercentageZ); }
                        }
                        sx *= s.prototypeScale; sy *= s.prototypeScale; sz *= s.prototypeScale;
                        const double yOffset = random.next(instance.minimumOffsetY, instance.maximumOffsetY) * sy;
                        double fx = ix, fy = iy, fz = iz;
                        if (instance.yOffsetAlongSlope) { fx += normal.x * yOffset; fy += normal.y * yOffset; fz += normal.z * yOffset; } else fy += yOffset;
                        double pitch = random.next(instance.minimumRotationX, instance.maximumRotationX);
                        double yaw = random.next(instance.minimumRotationY + direction, instance.maximumRotationY + direction);
                        double roll = random.next(instance.minimumRotationZ, instance.maximumRotationZ);
                        if (instance.alignForwardToSlope) yaw += std::atan2(normal.x, normal.z) * 180.0 / 3.14159265358979323846;
                        if (instance.rotateToSlope) { pitch += std::atan2(-normal.z, normal.y) * 180.0 / 3.14159265358979323846; roll += std::atan2(normal.x, normal.y) * 180.0 / 3.14159265358979323846; }
                        const double radius = s.boundsRadius * std::max(sx, sz);
                        if (!isRepresentable(fx) || !isRepresentable(fy) || !isRepresentable(fz) ||
                            !isRepresentable(sx) || sx <= 0 || !isRepresentable(sy) || sy <= 0 ||
                            !isRepresentable(sz) || sz <= 0 || !isRepresentable(radius) || radius <= 0)
                            return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
                                DiagnosticCode::InvalidArgument, "terrain.object.multitile: generated transform is not representable"));
                        ProcgenPoint point;
                        point.id = mix(mix(mix(mix(mix(s.namespaceId) ^ stableNameHash(tile->name)) ^ (globalCandidate + 1)) ^
                                           (resource + 1)) ^ (std::uint64_t(ordinal) + 1));
                        if (!point.id) return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
                            DiagnosticCode::InvalidArgument, "terrain.object.multitile: generated identity is reserved"));
                        point.x = float(fx); point.y = float(fy); point.z = float(fz);
                        point.normalX = float(normal.x); point.normalY = float(normal.y); point.normalZ = float(normal.z);
                        point.pitch = float(pitch); point.yaw = float(yaw); point.roll = float(roll);
                        point.scaleX = float(sx); point.scaleY = float(sy); point.scaleZ = float(sz); point.density = instanceStrength;
                        point.boundsMinX = point.boundsMinZ = -float(radius); point.boundsMaxX = point.boundsMaxZ = float(radius); point.boundsMaxY = float(sy);
                        point.seed = static_cast<std::uint32_t>(mix(point.id ^ static_cast<std::uint32_t>(s.seed)));
                        const int row = next.appendPoint(point);
                        auto a = next.trySetStringAttribute(row, "asset", instance.asset); if (!a.ok()) return Result<TerrainMultiTileReport>::failure(a.status());
                        auto p = next.trySetStringAttribute(row, "objectPrototype", s.prototype); if (!p.ok()) return Result<TerrainMultiTileReport>::failure(p.status());
                        auto source = next.trySetStringAttribute(row, "spawnNamespace", std::to_string(s.namespaceId)); if (!source.ok()) return Result<TerrainMultiTileReport>::failure(source.status());
                        auto c = next.trySetIntAttribute(row, "objectCandidate", static_cast<std::int64_t>(mix(stableNameHash(tile->name)) ^ mix(globalCandidate + 1))); if (!c.ok()) return Result<TerrainMultiTileReport>::failure(c.status());
                        auto r = next.trySetIntAttribute(row, "objectResource", static_cast<std::int64_t>(resource)); if (!r.ok()) return Result<TerrainMultiTileReport>::failure(r.status());
                        ++emitted; ++changed;
                    }
                }
            }
        }
        if (report.changedSamples > std::numeric_limits<int>::max() - changed)
            return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "terrain.object.multitile: changed point count exceeds range"));
        report.changedSamples += changed;
        candidates.push_back({tile->objects, std::move(next)});
    }
    for (auto& candidate : candidates) *candidate.target = std::move(candidate.value);
    return Result<TerrainMultiTileReport>::success(std::move(report));
}

struct TerrainMultiObjectWorkspace::Impl {
    struct Tile { std::string name; PointSet objects; Heightmap heights; double originX, originZ, width, depth; int rx, ry; bool worldMap; };
    struct Snapshot { std::vector<PointSet> objects; TerrainMultiTileReport report; };
    std::vector<Tile> tiles; TerrainMultiTileReport last; std::vector<Snapshot> history; int cursor = 0;
    Snapshot snapshot() const { Snapshot result; for (const auto& tile : tiles) result.objects.push_back(tile.objects); result.report = last; return result; }
    void restore(const Snapshot& value) { for (std::size_t i = 0; i < tiles.size(); ++i) tiles[i].objects = value.objects[i]; last = value.report; }
};
TerrainMultiObjectWorkspace::TerrainMultiObjectWorkspace() : impl_(std::make_unique<Impl>()) {}
TerrainMultiObjectWorkspace::~TerrainMultiObjectWorkspace() = default;
TerrainMultiObjectWorkspace::TerrainMultiObjectWorkspace(TerrainMultiObjectWorkspace&&) noexcept = default;
TerrainMultiObjectWorkspace& TerrainMultiObjectWorkspace::operator=(TerrainMultiObjectWorkspace&&) noexcept = default;
Result<int> TerrainMultiObjectWorkspace::addTile(const std::string& name, const PointSet& objects, const Heightmap& heights,
                                                  double originX, double originZ, double width, double depth,
                                                  int rx, int ry, bool worldMap) {
    if (!impl_ || name.empty() || !raster_detail::validRaster(heights) || !std::isfinite(originX) ||
        !std::isfinite(originZ) || !std::isfinite(width) || width <= 0 || !std::isfinite(depth) || depth <= 0 ||
        rx <= 0 || ry <= 0)
        return invalid("terrain.object.workspace: initialized named tile required");
    if (impl_->history.size() > 1 || std::any_of(impl_->tiles.begin(), impl_->tiles.end(), [&](const auto& t) { return t.name == name; }))
        return invalid("terrain.object.workspace: unique tile required before applying");
    auto candidate = std::make_unique<Impl>(*impl_);
    candidate->tiles.push_back({name, objects, heights, originX, originZ, width, depth, rx, ry, worldMap});
    candidate->history = {candidate->snapshot()}; candidate->cursor = 0; impl_.swap(candidate);
    return Result<int>::success(int(impl_->tiles.size()));
}
Result<int> TerrainMultiObjectWorkspace::apply(const Heightmap& fitness, const TerrainObjectPlacementSettings& settings,
                                                const TerrainStampSettings& operationSettings,
                                                TerrainObjectOperationMode mode, bool worldMapOperation) {
    if (!impl_ || impl_->tiles.empty()) return invalid("terrain.object.workspace: populated workspace required");
    auto candidate = std::make_unique<Impl>(*impl_); std::vector<TerrainObjectTile> tiles;
    for (auto& tile : candidate->tiles) tiles.push_back({tile.name, &tile.objects, &tile.heights, tile.originX, tile.originZ, tile.width, tile.depth, tile.rx, tile.ry, tile.worldMap});
    auto result = applyTerrainObjectsMultiTile(tiles, fitness, settings, operationSettings, mode, worldMapOperation);
    if (!result.ok()) return Result<int>::failure(result.status());
    candidate->last = std::move(result.value()); candidate->history.resize(std::size_t(candidate->cursor + 1));
    candidate->history.push_back(candidate->snapshot()); ++candidate->cursor; const int changed = candidate->last.changedSamples;
    impl_.swap(candidate); return Result<int>::success(changed);
}
Result<int> TerrainMultiObjectWorkspace::copyTile(const std::string& name, PointSet& output) const {
    if (!impl_) return invalid("terrain.object.workspace: moved-from workspace");
    const auto found = std::find_if(impl_->tiles.begin(), impl_->tiles.end(), [&](const auto& tile) { return tile.name == name; });
    if (found == impl_->tiles.end()) return invalid("terrain.object.workspace: tile not found");
    output = found->objects; return Result<int>::success(output.getCount());
}
Result<int> TerrainMultiObjectWorkspace::undo() { if (!impl_ || impl_->cursor <= 0) return invalid("terrain.object.workspace: no undo snapshot"); --impl_->cursor; impl_->restore(impl_->history[std::size_t(impl_->cursor)]); return Result<int>::success(impl_->cursor); }
Result<int> TerrainMultiObjectWorkspace::redo() { if (!impl_ || impl_->cursor + 1 >= int(impl_->history.size())) return invalid("terrain.object.workspace: no redo snapshot"); ++impl_->cursor; impl_->restore(impl_->history[std::size_t(impl_->cursor)]); return Result<int>::success(impl_->cursor); }
int TerrainMultiObjectWorkspace::getTileCount() const noexcept { return impl_ ? int(impl_->tiles.size()) : 0; }
int TerrainMultiObjectWorkspace::getLastChangedSamples() const noexcept { return impl_ ? impl_->last.changedSamples : 0; }
int TerrainMultiObjectWorkspace::getLastAffectedTiles() const noexcept { return impl_ ? impl_->last.affectedTiles : 0; }
int TerrainMultiObjectWorkspace::getOperationCount() const noexcept { return impl_ ? std::max(0, int(impl_->history.size()) - 1) : 0; }
int TerrainMultiObjectWorkspace::getAppliedCount() const noexcept { return impl_ ? impl_->cursor : 0; }
}  // namespace eve::procgen
