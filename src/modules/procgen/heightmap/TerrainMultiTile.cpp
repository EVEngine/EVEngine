#include "procgen/heightmap/TerrainMultiTile.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainRasterInternal.h"
#include "procgen/heightmap/TerrainStamp.h"

namespace eve::procgen {
using namespace raster_detail;
namespace {
struct Candidate {
    Heightmap* target;
    Heightmap  value;
};
struct Topology {
    int width = 0;
    int height = 0;
    double spacingX = 0;
    double spacingZ = 0;
    double referenceX = 0;
    double referenceZ = 0;
};
bool finite(double v) { return std::isfinite(v); }
bool selected(const TerrainHeightTile& tile, bool worldMap, const std::unordered_set<std::string>& allowed) {
    return tile.worldMap == worldMap && (allowed.empty() || allowed.contains(tile.name));
}
Result<TerrainMultiTileReport> fail(const char* message) {
    return Result<TerrainMultiTileReport>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message));
}
Result<Topology> validateTopology(const std::vector<TerrainHeightTile>& tiles) {
    if (tiles.empty())
        return Result<Topology>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.multitile: at least one tile required"));
    Topology topology;
    std::unordered_set<Heightmap*> targets;
    for (const auto& tile : tiles) {
        if (tile.name.empty() || !tile.heightmap || !validRaster(*tile.heightmap) || !finite(tile.originX) ||
            !finite(tile.originZ) || !finite(tile.width) || !finite(tile.depth) || tile.width <= 0 || tile.depth <= 0 ||
            tile.heightmap->getWidth() < 2 || tile.heightmap->getHeight() < 2 || !targets.insert(tile.heightmap).second)
            return Result<Topology>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "terrain.multitile: valid named distinct height tiles required"));
        if (!topology.width) {
            topology.width = tile.heightmap->getWidth();
            topology.height = tile.heightmap->getHeight();
            topology.spacingX = tile.width / double(topology.width - 1);
            topology.spacingZ = tile.depth / double(topology.height - 1);
            topology.referenceX = tile.originX;
            topology.referenceZ = tile.originZ;
        } else if (tile.heightmap->getWidth() != topology.width || tile.heightmap->getHeight() != topology.height ||
                   std::abs(tile.width / double(topology.width - 1) - topology.spacingX) >
                       std::abs(topology.spacingX) * 1e-9 + 1e-12 ||
                   std::abs(tile.depth / double(topology.height - 1) - topology.spacingZ) >
                       std::abs(topology.spacingZ) * 1e-9 + 1e-12) {
            return Result<Topology>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument,
                "terrain.multitile: matching heightmap resolution and world pixel size required"));
        }
        const double tileX = (tile.originX - topology.referenceX) / tile.width;
        const double tileZ = (tile.originZ - topology.referenceZ) / tile.depth;
        if (std::abs(tileX - std::round(tileX)) > 1e-8 || std::abs(tileZ - std::round(tileZ)) > 1e-8)
            return Result<Topology>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "terrain.multitile: tile origins must align to whole tile spans"));
    }
    return Result<Topology>::success(topology);
}
}  // namespace

Result<TerrainMultiTileReport> mapTerrainOperationMultiTile(
    const std::vector<TerrainOperationTile>& tiles, const TerrainStampSettings& settings,
    TerrainOperationDomain domain, bool worldMapOperation, const std::vector<std::string>& validTerrainNames) {
    if (tiles.empty() || !finite(settings.centerX) || !finite(settings.centerZ) || !finite(settings.width) ||
        !finite(settings.depth) || !finite(settings.rotation) || settings.width <= 0 || settings.depth <= 0)
        return fail("terrain.multitile.window: tiles and finite positive world operation required");
    const bool heightmap = domain == TerrainOperationDomain::Heightmap;
    if (!heightmap && domain != TerrainOperationDomain::Texture && domain != TerrainOperationDomain::Tree &&
        domain != TerrainOperationDomain::TerrainDetail && domain != TerrainOperationDomain::GameObject &&
        domain != TerrainOperationDomain::BakedMask)
        return fail("terrain.multitile.window: unknown operation domain");
    const int seam = heightmap ? 1 : 0;
    std::unordered_set<std::string> allowed;
    for (const auto& name : validTerrainNames)
        if (name.empty() || !allowed.insert(name).second)
            return fail("terrain.multitile.window: allow-list names must be nonempty and unique");

    int resolutionX = 0, resolutionY = 0;
    double spacingX = 0, spacingZ = 0, referenceX = 0, referenceZ = 0;
    std::unordered_set<std::string> names;
    for (const auto& tile : tiles) {
        if (tile.name.empty() || !names.insert(tile.name).second || !finite(tile.originX) || !finite(tile.originZ) ||
            !finite(tile.width) || !finite(tile.depth) || tile.width <= 0 || tile.depth <= 0 ||
            tile.resolutionX <= seam || tile.resolutionY <= seam)
            return fail("terrain.multitile.window: valid uniquely named tile descriptors required");
        if (!resolutionX) {
            resolutionX = tile.resolutionX;
            resolutionY = tile.resolutionY;
            spacingX = tile.width / double(resolutionX - seam);
            spacingZ = tile.depth / double(resolutionY - seam);
            referenceX = tile.originX;
            referenceZ = tile.originZ;
        } else if (tile.resolutionX != resolutionX || tile.resolutionY != resolutionY ||
                   std::abs(tile.width / double(resolutionX - seam) - spacingX) >
                       std::abs(spacingX) * 1e-9 + 1e-12 ||
                   std::abs(tile.depth / double(resolutionY - seam) - spacingZ) >
                       std::abs(spacingZ) * 1e-9 + 1e-12) {
            return fail("terrain.multitile.window: matching resolution and world pixel size required");
        }
        const double tileX = (tile.originX - referenceX) / tile.width;
        const double tileZ = (tile.originZ - referenceZ) / tile.depth;
        if (std::abs(tileX - std::round(tileX)) > 1e-8 || std::abs(tileZ - std::round(tileZ)) > 1e-8)
            return fail("terrain.multitile.window: tile origins must align to whole tile spans");
    }

    const double c = std::abs(std::cos(settings.rotation)), s = std::abs(std::sin(settings.rotation));
    const double halfX = (c * settings.width + s * settings.depth) * 0.5;
    const double halfZ = (s * settings.width + c * settings.depth) * 0.5;
    const double minX = settings.centerX - halfX, maxX = settings.centerX + halfX;
    const double minZ = settings.centerZ - halfZ, maxZ = settings.centerZ + halfZ;
    if (!finite(minX) || !finite(maxX) || !finite(minZ) || !finite(maxZ))
        return fail("terrain.multitile.window: rotated operation bounds overflow");
    const double minPixelX = std::floor((minX - referenceX) / spacingX);
    const double minPixelY = std::floor((minZ - referenceZ) / spacingZ);
    const double maxPixelX = std::ceil((maxX - referenceX) / spacingX);
    const double maxPixelY = std::ceil((maxZ - referenceZ) / spacingZ);
    for (double value : {minPixelX, minPixelY, maxPixelX, maxPixelY})
        if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
            return fail("terrain.multitile.window: operation pixel rectangle exceeds integer range");

    TerrainMultiTileReport report;
    report.operationX = int(minPixelX);
    report.operationY = int(minPixelY);
    const long long width64 = static_cast<long long>(int(maxPixelX)) - report.operationX + seam;
    const long long height64 = static_cast<long long>(int(maxPixelY)) - report.operationY + seam;
    if (width64 < 0 || height64 < 0 || width64 > std::numeric_limits<int>::max() ||
        height64 > std::numeric_limits<int>::max())
        return fail("terrain.multitile.window: operation pixel extent exceeds integer range");
    report.operationWidth = int(width64);
    report.operationHeight = int(height64);
    const long long operationMaxX = static_cast<long long>(report.operationX) + report.operationWidth;
    const long long operationMaxY = static_cast<long long>(report.operationY) + report.operationHeight;

    for (const auto& tile : tiles) {
        if (tile.worldMap != worldMapOperation || (!allowed.empty() && !allowed.contains(tile.name))) continue;
        if (!(minX < tile.originX + tile.width && maxX > tile.originX && minZ < tile.originZ + tile.depth &&
              maxZ > tile.originZ))
            continue;
        const long long coordinateX = std::llround((tile.originX - referenceX) / spacingX);
        const long long coordinateY = std::llround((tile.originZ - referenceZ) / spacingZ);
        const long long localX = std::max(0LL, static_cast<long long>(report.operationX) - coordinateX);
        const long long localY = std::max(0LL, static_cast<long long>(report.operationY) - coordinateY);
        const long long localMaxX = std::min(static_cast<long long>(resolutionX), operationMaxX - coordinateX);
        const long long localMaxY = std::min(static_cast<long long>(resolutionY), operationMaxY - coordinateY);
        const long long affectedWidth = std::max(0LL, localMaxX - localX);
        const long long affectedHeight = std::max(0LL, localMaxY - localY);
        for (long long value : {coordinateX, coordinateY, localX, localY, affectedWidth, affectedHeight})
            if (value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
                return fail("terrain.multitile.window: affected pixel rectangle exceeds integer range");
        if (!affectedWidth || !affectedHeight) continue;
        report.mappings.push_back({tile.name, int(localX), int(localY),
                                   int(localX + coordinateX - report.operationX),
                                   int(localY + coordinateY - report.operationY), int(affectedWidth),
                                   int(affectedHeight)});
    }
    report.affectedTiles = int(report.mappings.size());
    return Result<TerrainMultiTileReport>::success(std::move(report));
}

Result<TerrainMultiTileReport> applyTerrainStampMultiTile(
    const std::vector<TerrainHeightTile>& tiles, const Heightmap& stamp, const TerrainStampSettings& settings,
    const Heightmap& localMask, const Heightmap& globalMask, bool worldMapOperation,
    const std::vector<std::string>& validTerrainNames) {
    if (tiles.empty() || !validRaster(stamp) || !validRaster(localMask) || !validRaster(globalMask))
        return fail("terrain.multitile: tiles and finite operation rasters required");
    if (!finite(settings.centerX) || !finite(settings.centerZ) || !finite(settings.width) ||
        !finite(settings.depth) || !finite(settings.rotation) || settings.width <= 0 || settings.depth <= 0)
        return fail("terrain.multitile: finite positive world operation required");
    std::unordered_set<std::string> allowed;
    for (const auto& name : validTerrainNames)
        if (name.empty() || !allowed.insert(name).second)
            return fail("terrain.multitile: allow-list names must be nonempty and unique");

    auto topologyResult = validateTopology(tiles);
    if (!topologyResult.ok()) return Result<TerrainMultiTileReport>::failure(topologyResult.status());
    const auto& [referenceWidth, referenceHeight, spacingX, spacingZ, referenceX, referenceZ] =
        topologyResult.value();

    const double c = std::abs(std::cos(settings.rotation)), s = std::abs(std::sin(settings.rotation));
    const double halfX = (c * settings.width + s * settings.depth) * 0.5;
    const double halfZ = (s * settings.width + c * settings.depth) * 0.5;
    const double minX = settings.centerX - halfX, maxX = settings.centerX + halfX;
    const double minZ = settings.centerZ - halfZ, maxZ = settings.centerZ + halfZ;
    if (!finite(minX) || !finite(maxX) || !finite(minZ) || !finite(maxZ))
        return fail("terrain.multitile: rotated operation bounds overflow");

    TerrainMultiTileReport report;
    report.operationX = int(std::floor((minX - referenceX) / spacingX));
    report.operationY = int(std::floor((minZ - referenceZ) / spacingZ));
    const double opMaxXd = std::ceil((maxX - referenceX) / spacingX);
    const double opMaxYd = std::ceil((maxZ - referenceZ) / spacingZ);
    for (double v : {double(report.operationX), double(report.operationY), opMaxXd, opMaxYd})
        if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max())
            return fail("terrain.multitile: operation pixel rectangle exceeds integer range");
    const int opMaxX = int(opMaxXd), opMaxY = int(opMaxYd);
    if (opMaxX < report.operationX || opMaxY < report.operationY)
        return fail("terrain.multitile: invalid operation pixel rectangle");
    const long long operationWidth = static_cast<long long>(opMaxX) - report.operationX + 1;
    const long long operationHeight = static_cast<long long>(opMaxY) - report.operationY + 1;
    if (operationWidth > std::numeric_limits<int>::max() || operationHeight > std::numeric_limits<int>::max())
        return fail("terrain.multitile: operation pixel extent exceeds integer range");
    report.operationWidth = int(operationWidth);
    report.operationHeight = int(operationHeight);

    std::vector<Candidate> candidates;
    for (const auto& tile : tiles) {
        if (!selected(tile, worldMapOperation, allowed)) continue;
        // Strict area overlap excludes a neighbor that only touches the brush AABB at its border.
        if (!(minX < tile.originX + tile.width && maxX > tile.originX &&
              minZ < tile.originZ + tile.depth && maxZ > tile.originZ)) continue;
        const long long coordinateX = std::llround((tile.originX - referenceX) / spacingX);
        const long long coordinateY = std::llround((tile.originZ - referenceZ) / spacingZ);
        if (coordinateX < std::numeric_limits<int>::min() || coordinateX > std::numeric_limits<int>::max() ||
            coordinateY < std::numeric_limits<int>::min() || coordinateY > std::numeric_limits<int>::max())
            return fail("terrain.multitile: tile pixel coordinate exceeds integer range");
        const auto localX64 = std::max(0LL, static_cast<long long>(report.operationX) - coordinateX);
        const auto localY64 = std::max(0LL, static_cast<long long>(report.operationY) - coordinateY);
        const auto localMaxX64 = std::min(static_cast<long long>(referenceWidth),
                                          static_cast<long long>(opMaxX) - coordinateX + 1);
        const auto localMaxY64 = std::min(static_cast<long long>(referenceHeight),
                                          static_cast<long long>(opMaxY) - coordinateY + 1);
        if (localX64 > std::numeric_limits<int>::max() || localY64 > std::numeric_limits<int>::max() ||
            localMaxX64 < std::numeric_limits<int>::min() || localMaxY64 < std::numeric_limits<int>::min())
            return fail("terrain.multitile: affected pixel rectangle exceeds integer range");
        const int localX = int(localX64), localY = int(localY64);
        const int localMaxX = int(localMaxX64), localMaxY = int(localMaxY64);
        const int width = std::max(0, localMaxX - localX), height = std::max(0, localMaxY - localY);
        if (!width || !height) continue;
        report.mappings.push_back({tile.name, localX, localY,
                                   localX + int(coordinateX) - report.operationX,
                                   localY + int(coordinateY) - report.operationY, width, height});
        TerrainStampSettings localSettings = settings;
        localSettings.originX = tile.originX; localSettings.originZ = tile.originZ;
        localSettings.spacingX = spacingX; localSettings.spacingZ = spacingZ;
        Heightmap candidate = *tile.heightmap;
        auto changed = applyTerrainStamp(candidate, stamp, localSettings, localMask, globalMask);
        if (!changed.ok()) return Result<TerrainMultiTileReport>::failure(changed.status());
        if (report.changedSamples > std::numeric_limits<int>::max() - changed.value())
            return fail("terrain.multitile: changed sample count exceeds integer range");
        report.changedSamples += changed.value();
        candidates.push_back({tile.heightmap, std::move(candidate)});
    }
    report.affectedTiles = int(candidates.size());
    for (auto& candidate : candidates) *candidate.target = std::move(candidate.value);
    return Result<TerrainMultiTileReport>::success(std::move(report));
}

Result<int> stitchTerrainHeightmaps(const TerrainHeightTile& terrainA, const TerrainHeightTile& terrainB,
                                    const TerrainHeightStitchSettings& settings) {
    if (!terrainA.heightmap || !terrainB.heightmap || terrainA.heightmap == terrainB.heightmap ||
        !validRaster(*terrainA.heightmap) || !validRaster(*terrainB.heightmap) ||
        terrainA.heightmap->getWidth() != terrainB.heightmap->getWidth() ||
        terrainA.heightmap->getHeight() != terrainB.heightmap->getHeight() ||
        terrainA.heightmap->getWidth() != terrainA.heightmap->getHeight() ||
        !finite(terrainA.originX) || !finite(terrainA.originZ) || !finite(terrainA.width) ||
        !finite(terrainA.depth) || !finite(terrainB.originX) || !finite(terrainB.originZ) ||
        !finite(terrainB.width) || !finite(terrainB.depth) || terrainA.width <= 0 || terrainA.depth <= 0 ||
        terrainB.width <= 0 || terrainB.depth <= 0 || settings.extraSeamSize < 1 ||
        settings.extraSeamSize >= terrainA.heightmap->getWidth() || !std::isfinite(settings.maxDifference) ||
        settings.maxDifference < 0)
        return Result<int>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.stitch: distinct matching square tiles and valid seam required"));
    const int resolution = terrainA.heightmap->getWidth();
    const double spacingX = terrainA.width / double(resolution - 1);
    const double spacingZ = terrainA.depth / double(resolution - 1);
    if (std::abs(spacingX - terrainB.width / double(resolution - 1)) > 1e-6 ||
        std::abs(spacingZ - terrainB.depth / double(resolution - 1)) > 1e-6)
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "terrain.stitch: matching sample spacing required"));
    enum class Direction { North, South, West, East };
    Direction direction;
    const double dx = terrainA.originX - terrainB.originX;
    const double dz = terrainA.originZ - terrainB.originZ;
    if (std::abs(dx) > std::abs(dz)) direction = dx > 0 ? Direction::West : Direction::East;
    else direction = dz > 0 ? Direction::South : Direction::North;
    const double edgeError = direction == Direction::North
                                 ? std::abs(terrainA.originZ + terrainA.depth - terrainB.originZ)
                             : direction == Direction::South
                                 ? std::abs(terrainB.originZ + terrainB.depth - terrainA.originZ)
                             : direction == Direction::East
                                 ? std::abs(terrainA.originX + terrainA.width - terrainB.originX)
                                 : std::abs(terrainB.originX + terrainB.width - terrainA.originX);
    const double edgeSpacing = direction == Direction::North || direction == Direction::South ? spacingZ : spacingX;
    if (edgeError > edgeSpacing * 0.01)
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.stitch: tiles do not share an edge"));
    const bool northSouth = direction == Direction::North || direction == Direction::South;
    const double alongDelta = northSouth ? terrainA.originX - terrainB.originX
                                         : terrainA.originZ - terrainB.originZ;
    const double alongSpacing = northSouth ? spacingX : spacingZ;
    const int offset = static_cast<int>(std::lround(std::abs(alongDelta) / alongSpacing));
    const int seamLength = resolution - offset;
    if (seamLength <= 0 || std::abs(std::abs(alongDelta) / alongSpacing - offset) > 1e-5)
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "terrain.stitch: overlapping grid-aligned edge required"));
    const int startA = alongDelta < 0 ? offset : 0;
    const int startB = alongDelta > 0 ? offset : 0;
    Heightmap nextA = *terrainA.heightmap, nextB = *terrainB.heightmap;
    const int seam = settings.extraSeamSize;
    auto coord = [&](Direction side, bool first, int along, int depth) {
        const int p = (first ? startA : startB) + along;
        switch (side) {
            case Direction::North: return std::pair{p, resolution - 1 - depth};
            case Direction::South: return std::pair{p, depth};
            case Direction::East: return std::pair{resolution - 1 - depth, p};
            case Direction::West: return std::pair{depth, p};
        }
        return std::pair{0, 0};
    };
    const Direction opposite = direction == Direction::North ? Direction::South
                               : direction == Direction::South ? Direction::North
                               : direction == Direction::East ? Direction::West
                                                               : Direction::East;
    for (int along = 0; along < seamLength; ++along) {
        const auto [aInnerX, aInnerY] = coord(direction, true, along, seam);
        const auto [bInnerX, bInnerY] = coord(opposite, false, along, seam);
        const float aEnd = terrainA.heightmap->height(aInnerX, aInnerY);
        const float bEnd = terrainB.heightmap->height(bInnerX, bInnerY);
        for (int d = 1; d < seam * 2; ++d) {
            if (d == seam) continue;
            const float linearT = seam == 1 ? 0.5F : float(d - 1) / float(seam * 2 - 2);
            const float linear = std::lerp(aEnd, bEnd, linearT);
            if (d < seam) {
                const auto [x, y] = coord(direction, true, along, seam - d);
                nextA.setHeight(x, y, std::lerp(terrainA.heightmap->height(x, y), linear, float(d) / seam));
            } else {
                const int depth = d - seam;
                const auto [x, y] = coord(opposite, false, along, depth);
                nextB.setHeight(x, y,
                                std::lerp(linear, terrainB.heightmap->height(x, y), float(depth) / seam));
            }
        }
        const auto [aNearX, aNearY] = coord(direction, true, along, 1);
        const auto [bNearX, bNearY] = coord(opposite, false, along, 1);
        const float joined = (nextA.height(aNearX, aNearY) + nextB.height(bNearX, bNearY)) * 0.5F;
        const auto [aEdgeX, aEdgeY] = coord(direction, true, along, 0);
        const auto [bEdgeX, bEdgeY] = coord(opposite, false, along, 0);
        nextA.setHeight(aEdgeX, aEdgeY, joined);
        nextB.setHeight(bEdgeX, bEdgeY, joined);
    }
    int changed = 0;
    for (std::size_t i = 0; i < nextA.data().size(); ++i) {
        changed += nextA.data()[i] != terrainA.heightmap->data()[i];
        changed += nextB.data()[i] != terrainB.heightmap->data()[i];
    }
    *terrainA.heightmap = std::move(nextA);
    *terrainB.heightmap = std::move(nextB);
    return Result<int>::success(changed);
}

struct TerrainMultiTileWorkspace::Impl {
    struct Tile {
        std::string name;
        Heightmap heightmap;
        double originX, originZ, width, depth;
        bool worldMap;
    };
    std::vector<Tile> tiles;
    TerrainMultiTileReport last;
    struct Snapshot {
        std::vector<Heightmap> heightmaps;
        TerrainMultiTileReport report;
    };
    std::vector<Snapshot> history;
    int cursor = 0;
    Snapshot snapshot() const {
        Snapshot result;
        result.heightmaps.reserve(tiles.size());
        for (const auto& tile : tiles) result.heightmaps.push_back(tile.heightmap);
        result.report = last;
        return result;
    }
    void restore(const Snapshot& snapshot) {
        for (size_t i = 0; i < tiles.size(); ++i) tiles[i].heightmap = snapshot.heightmaps[i];
        last = snapshot.report;
    }
};

TerrainMultiTileWorkspace::TerrainMultiTileWorkspace() : impl_(std::make_unique<Impl>()) {}
TerrainMultiTileWorkspace::~TerrainMultiTileWorkspace() = default;
TerrainMultiTileWorkspace::TerrainMultiTileWorkspace(TerrainMultiTileWorkspace&&) noexcept = default;
TerrainMultiTileWorkspace& TerrainMultiTileWorkspace::operator=(TerrainMultiTileWorkspace&&) noexcept = default;

Result<int> TerrainMultiTileWorkspace::addTile(const std::string& name, const Heightmap& heightmap, double originX,
                                                double originZ, double width, double depth, bool worldMap) {
    if (!impl_ || name.empty() || !validRaster(heightmap) || heightmap.getWidth() < 2 || heightmap.getHeight() < 2 ||
        !finite(originX) || !finite(originZ) || !finite(width) || !finite(depth) || width <= 0 || depth <= 0)
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                      "terrain.multitile.workspace: valid tile required"));
    if (std::any_of(impl_->tiles.begin(), impl_->tiles.end(), [&](const auto& tile) { return tile.name == name; }))
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                      "terrain.multitile.workspace: duplicate tile name"));
    if (impl_->history.size() > 1)
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                      "terrain.multitile.workspace: topology is fixed after stamping"));
    auto candidate = std::make_unique<Impl>(*impl_);
    candidate->tiles.push_back({name, heightmap, originX, originZ, width, depth, worldMap});
    // Validate the complete topology now, including resolution, pixel size and origin alignment.
    std::vector<TerrainHeightTile> descriptors;
    descriptors.reserve(candidate->tiles.size());
    for (auto& tile : candidate->tiles)
        descriptors.push_back({tile.name, &tile.heightmap, tile.originX, tile.originZ, tile.width, tile.depth,
                               tile.worldMap});
    auto checked = validateTopology(descriptors);
    if (!checked.ok()) return Result<int>::failure(checked.status());
    candidate->history.clear();
    candidate->history.push_back(candidate->snapshot());
    candidate->cursor = 0;
    impl_.swap(candidate);
    return Result<int>::success(int(impl_->tiles.size()));
}

Result<int> TerrainMultiTileWorkspace::stamp(const Heightmap& stampRaster, const TerrainStampSettings& settings,
                                              const Heightmap& localMask, const Heightmap& globalMask,
                                              bool worldMapOperation) {
    if (!impl_)
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                      "terrain.multitile.workspace: moved-from workspace"));
    auto candidate = std::make_unique<Impl>(*impl_);
    std::vector<TerrainHeightTile> descriptors;
    descriptors.reserve(candidate->tiles.size());
    for (auto& tile : candidate->tiles)
        descriptors.push_back({tile.name, &tile.heightmap, tile.originX, tile.originZ, tile.width, tile.depth,
                               tile.worldMap});
    auto result = applyTerrainStampMultiTile(descriptors, stampRaster, settings, localMask, globalMask,
                                             worldMapOperation);
    if (!result.ok()) return Result<int>::failure(result.status());
    candidate->last = std::move(result.value());
    candidate->history.resize(size_t(candidate->cursor + 1));
    candidate->history.push_back(candidate->snapshot());
    ++candidate->cursor;
    const int changed = candidate->last.changedSamples;
    impl_.swap(candidate);
    return Result<int>::success(changed);
}

Result<int> TerrainMultiTileWorkspace::copyTile(const std::string& name, Heightmap& output) const {
    if (!impl_ || !validRaster(output))
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                      "terrain.multitile.workspace: valid output required"));
    auto found = std::find_if(impl_->tiles.begin(), impl_->tiles.end(), [&](const auto& tile) { return tile.name == name; });
    if (found == impl_->tiles.end() || output.getWidth() != found->heightmap.getWidth() ||
        output.getHeight() != found->heightmap.getHeight())
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                      "terrain.multitile.workspace: named matching output required"));
    return publish(output, found->heightmap.data());
}
Result<int> TerrainMultiTileWorkspace::stitch(const std::string& terrainA, const std::string& terrainB,
                                               const TerrainHeightStitchSettings& settings) {
    if (!impl_ || terrainA == terrainB)
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.multitile.workspace: two tile names required"));
    auto candidate = std::make_unique<Impl>(*impl_);
    auto findTile = [&](const std::string& name) {
        return std::find_if(candidate->tiles.begin(), candidate->tiles.end(),
                            [&](const auto& tile) { return tile.name == name; });
    };
    auto a = findTile(terrainA), b = findTile(terrainB);
    if (a == candidate->tiles.end() || b == candidate->tiles.end())
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.multitile.workspace: stitch tile not found"));
    TerrainHeightTile descA{a->name, &a->heightmap, a->originX, a->originZ, a->width, a->depth, a->worldMap};
    TerrainHeightTile descB{b->name, &b->heightmap, b->originX, b->originZ, b->width, b->depth, b->worldMap};
    auto result = stitchTerrainHeightmaps(descA, descB, settings);
    if (!result.ok()) return result;
    candidate->last = {};
    candidate->last.affectedTiles = 2;
    candidate->last.changedSamples = result.value();
    candidate->history.resize(static_cast<std::size_t>(candidate->cursor + 1));
    candidate->history.push_back(candidate->snapshot());
    ++candidate->cursor;
    impl_.swap(candidate);
    return result;
}
int TerrainMultiTileWorkspace::getTileCount() const noexcept { return impl_ ? int(impl_->tiles.size()) : 0; }
int TerrainMultiTileWorkspace::getLastAffectedTiles() const noexcept { return impl_ ? impl_->last.affectedTiles : 0; }
int TerrainMultiTileWorkspace::getLastChangedSamples() const noexcept { return impl_ ? impl_->last.changedSamples : 0; }
int TerrainMultiTileWorkspace::getLastMappingCount() const noexcept {
    return impl_ ? int(impl_->last.mappings.size()) : 0;
}
Result<int> TerrainMultiTileWorkspace::undo() {
    if (!impl_ || impl_->cursor <= 0)
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                      "terrain.multitile.workspace: no undo available"));
    auto candidate = std::make_unique<Impl>(*impl_);
    int changed = 0;
    const auto& snapshot = candidate->history[size_t(candidate->cursor - 1)];
    for (size_t t = 0; t < candidate->tiles.size(); ++t)
        for (size_t i = 0; i < candidate->tiles[t].heightmap.data().size(); ++i)
            changed += candidate->tiles[t].heightmap.data()[i] != snapshot.heightmaps[t].data()[i];
    --candidate->cursor;
    candidate->restore(snapshot);
    impl_.swap(candidate);
    return Result<int>::success(changed);
}
Result<int> TerrainMultiTileWorkspace::redo() {
    if (!impl_ || impl_->cursor < 0 || size_t(impl_->cursor + 1) >= impl_->history.size())
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                      "terrain.multitile.workspace: no redo available"));
    auto candidate = std::make_unique<Impl>(*impl_);
    int changed = 0;
    const auto& snapshot = candidate->history[size_t(candidate->cursor + 1)];
    for (size_t t = 0; t < candidate->tiles.size(); ++t)
        for (size_t i = 0; i < candidate->tiles[t].heightmap.data().size(); ++i)
            changed += candidate->tiles[t].heightmap.data()[i] != snapshot.heightmaps[t].data()[i];
    ++candidate->cursor;
    candidate->restore(snapshot);
    impl_.swap(candidate);
    return Result<int>::success(changed);
}
int TerrainMultiTileWorkspace::getOperationCount() const noexcept {
    return impl_ && !impl_->history.empty() ? int(impl_->history.size() - 1) : 0;
}
int TerrainMultiTileWorkspace::getAppliedCount() const noexcept { return impl_ ? impl_->cursor : 0; }
}  // namespace eve::procgen
