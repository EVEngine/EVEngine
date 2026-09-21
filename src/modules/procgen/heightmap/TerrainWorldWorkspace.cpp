#include "procgen/heightmap/TerrainWorldWorkspace.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include "procgen/PointSet.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainBiomePreset.h"
#include "procgen/heightmap/TerrainDetailLayer.h"
#include "procgen/heightmap/TerrainMultiTile.h"
#include "procgen/heightmap/TerrainObjectPlacement.h"
#include "procgen/heightmap/TerrainProbePlacement.h"
#include "procgen/heightmap/TerrainSplatmap.h"
#include "procgen/heightmap/TerrainSpawnPlan.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "procgen/heightmap/TerrainTreePlacement.h"

namespace eve::procgen {
namespace {
bool powerOfTwo(int value) { return value > 0 && (value & (value - 1)) == 0; }
bool finitePositive(double value) { return std::isfinite(value) && value > 0; }
}  // namespace

struct TerrainWorldWorkspace::Impl {
    struct Tile {
        std::string name;
        Heightmap heights;
        TerrainSplatmap splat;
        TerrainDetailLayer detail;
        PointSet trees, objects, probes;
        double originX = 0, originZ = 0;
    };
    struct Snapshot {
        std::vector<Heightmap> heights;
        std::vector<TerrainSplatmap> splats;
        std::vector<TerrainDetailLayer> details;
        std::vector<PointSet> trees, objects, probes;
        TerrainMultiTileReport report;
    };
    TerrainWorldCreationSettings settings;
    std::vector<Tile> tiles;
    TerrainMultiTileReport last;
    std::vector<Snapshot> history;
    int cursor = 0;
    TerrainWorldRunStatus runStatus = TerrainWorldRunStatus::Idle;
    std::unique_ptr<Impl> runCandidate;
    TerrainSpawnPlan runPlan;
    std::size_t runNext = 0;
    TerrainMultiTileReport runReport;

    Impl() = default;
    Impl(const Impl& other) : settings(other.settings), last(other.last), history(other.history), cursor(other.cursor) {
        tiles.reserve(other.tiles.size());
        for (const auto& source : other.tiles) {
            Tile tile;
            tile.name = source.name; tile.heights = source.heights; tile.splat = source.splat;
            tile.detail = source.detail;
            tile.trees = source.trees; tile.objects = source.objects; tile.probes = source.probes;
            tile.originX = source.originX; tile.originZ = source.originZ;
            tiles.push_back(std::move(tile));
        }
    }
    Snapshot snapshot() const {
        Snapshot result;
        for (const auto& tile : tiles) {
            result.heights.push_back(tile.heights); result.splats.push_back(tile.splat);
            result.details.push_back(tile.detail);
            result.trees.push_back(tile.trees); result.objects.push_back(tile.objects);
            result.probes.push_back(tile.probes);
        }
        result.report = last;
        return result;
    }
    void restore(const Snapshot& value) {
        for (std::size_t i = 0; i < tiles.size(); ++i) {
            tiles[i].heights = value.heights[i]; tiles[i].splat = value.splats[i];
            tiles[i].detail = value.details[i];
            tiles[i].trees = value.trees[i]; tiles[i].objects = value.objects[i];
            tiles[i].probes = value.probes[i];
        }
        last = value.report;
    }
    void record() {
        history.resize(static_cast<std::size_t>(cursor + 1));
        history.push_back(snapshot());
        ++cursor;
    }
};

TerrainWorldWorkspace::TerrainWorldWorkspace() : impl_(std::make_unique<Impl>()) {}
TerrainWorldWorkspace::~TerrainWorldWorkspace() = default;
TerrainWorldWorkspace::TerrainWorldWorkspace(TerrainWorldWorkspace&&) noexcept = default;
TerrainWorldWorkspace& TerrainWorldWorkspace::operator=(TerrainWorldWorkspace&&) noexcept = default;

Result<int> TerrainWorldWorkspace::create(const TerrainWorldCreationSettings& s) {
    if (!impl_ || impl_->runStatus == TerrainWorldRunStatus::Pending || s.tilesX <= 0 || s.tilesZ <= 0 ||
        s.tilesX > 4096 / s.tilesZ || !finitePositive(s.tileSize) ||
        !finitePositive(s.tileHeight) || !std::isfinite(s.centerX) || !std::isfinite(s.centerZ) ||
        s.heightmapResolution < 2 || !powerOfTwo(s.heightmapResolution - 1) ||
        !powerOfTwo(s.controlTextureResolution) || !powerOfTwo(s.detailResolution) ||
        s.treeResolution <= 0 || s.objectResolution <= 0 || s.splatLayers <= 0 ||
        s.defaultSplatLayer < 0 || s.defaultSplatLayer >= s.splatLayers || s.defaultDetailDensity < 0 ||
        s.namePrefix.empty())
        return Result<int>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.world.create: valid Pcg tile topology and resolutions required"));
    auto candidate = std::make_unique<Impl>();
    candidate->settings = s;
    candidate->tiles.reserve(static_cast<std::size_t>(s.tilesX * s.tilesZ));
    const double baseX = s.centerX - s.tileSize * s.tilesX * 0.5;
    const double baseZ = s.centerZ - s.tileSize * s.tilesZ * 0.5;
    if (!std::isfinite(baseX) || !std::isfinite(baseZ))
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                      "terrain.world.create: world origin is not representable"));
    for (int x = 0; x < s.tilesX; ++x) for (int z = 0; z < s.tilesZ; ++z) {
        Impl::Tile tile;
        tile.name = (s.worldMap ? "World Map" : s.namePrefix) + "_" + std::to_string(x) + "_" +
                    std::to_string(z) + (s.nameSuffix.empty() ? "" : "-" + s.nameSuffix);
        tile.originX = baseX + s.tileSize * x; tile.originZ = baseZ + s.tileSize * z;
        if (!std::isfinite(tile.originX) || !std::isfinite(tile.originZ))
            return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                          "terrain.world.create: tile origin is not representable"));
        tile.heights = Heightmap(s.heightmapResolution, s.heightmapResolution);
        auto splat = tile.splat.initialize(s.controlTextureResolution, s.controlTextureResolution,
                                           s.splatLayers, s.defaultSplatLayer);
        if (!splat.ok()) return Result<int>::failure(splat.status());
        auto detail = tile.detail.reset(s.detailResolution, s.detailResolution, s.defaultDetailDensity);
        if (!detail.ok()) return Result<int>::failure(detail.status());
        candidate->tiles.push_back(std::move(tile));
    }
    candidate->history.push_back(candidate->snapshot());
    impl_.swap(candidate);
    return Result<int>::success(static_cast<int>(impl_->tiles.size()));
}

namespace {
template <class Apply>
Result<int> mutateWorld(std::unique_ptr<TerrainWorldWorkspace::Impl>& owner, Apply&& apply) {
    if (!owner || owner->tiles.empty() || owner->runStatus == TerrainWorldRunStatus::Pending)
        return Result<int>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.world: created workspace without pending spawn required"));
    auto candidate = std::make_unique<TerrainWorldWorkspace::Impl>(*owner);
    auto result = apply(*candidate);
    if (!result.ok()) return Result<int>::failure(result.status());
    candidate->last = std::move(result.value());
    const int changed = candidate->last.changedSamples;
    candidate->record();
    owner.swap(candidate);
    return Result<int>::success(changed);
}

Result<TerrainMultiTileReport> applySpawnRule(TerrainWorldWorkspace::Impl& world,
                                               const TerrainSpawnRule& variant) {
    return std::visit(
        [&](const auto& rule) -> Result<TerrainMultiTileReport> {
            using Rule = std::decay_t<decltype(rule)>;
            if (!rule.enabled) return Result<TerrainMultiTileReport>::success({});
            if constexpr (std::is_same_v<Rule, TerrainSplatSpawnRule>) {
                std::vector<TerrainSplatTile> tiles;
                for (auto& tile : world.tiles)
                    tiles.push_back({tile.name, &tile.splat, tile.originX, tile.originZ,
                                     world.settings.tileSize, world.settings.tileSize, world.settings.worldMap});
                return paintTerrainSplatLayerMultiTile(tiles, rule.paint, rule.targetLayer, rule.operation,
                                                        world.settings.worldMap);
            } else if constexpr (std::is_same_v<Rule, TerrainDetailSpawnRule>) {
                auto settings = rule.settings;
                settings.mode = rule.mode;
                std::vector<TerrainDetailTile> tiles;
                for (auto& tile : world.tiles)
                    tiles.push_back({tile.name, &tile.detail, tile.originX, tile.originZ,
                                     world.settings.tileSize, world.settings.tileSize, world.settings.worldMap});
                return applyTerrainDetailMultiTile(tiles, rule.fitness, settings, rule.operation, rule.seed,
                                                   world.settings.worldMap);
            } else if constexpr (std::is_same_v<Rule, TerrainTreeSpawnRule>) {
                std::vector<TerrainTreeTile> tiles;
                for (auto& tile : world.tiles)
                    tiles.push_back({tile.name, &tile.trees, &tile.heights, tile.originX, tile.originZ,
                                     world.settings.tileSize, world.settings.tileSize,
                                     world.settings.treeResolution, world.settings.treeResolution,
                                     world.settings.worldMap});
                return applyTerrainTreesMultiTile(tiles, rule.fitness, rule.settings, rule.operation, rule.mode,
                                                  world.settings.worldMap);
            } else if constexpr (std::is_same_v<Rule, TerrainObjectSpawnRule>) {
                std::vector<TerrainObjectTile> tiles;
                for (auto& tile : world.tiles)
                    tiles.push_back({tile.name, &tile.objects, &tile.heights, tile.originX, tile.originZ,
                                     world.settings.tileSize, world.settings.tileSize,
                                     world.settings.objectResolution, world.settings.objectResolution,
                                     world.settings.worldMap});
                return applyTerrainObjectsMultiTile(tiles, rule.fitness, rule.settings, rule.operation, rule.mode,
                                                    world.settings.worldMap);
            } else if constexpr (std::is_same_v<Rule, TerrainProbeSpawnRule>) {
                std::vector<TerrainProbeTile> tiles;
                for (auto& tile : world.tiles)
                    tiles.push_back({tile.name, &tile.probes, &tile.heights, tile.originX, tile.originZ,
                                     world.settings.tileSize, world.settings.tileSize,
                                     world.settings.objectResolution, world.settings.objectResolution,
                                     world.settings.worldMap});
                return applyTerrainProbesMultiTile(tiles, rule.fitness, rule.settings, rule.operation, rule.mode,
                                                   world.settings.worldMap);
            } else {
                std::vector<TerrainHeightTile> tiles;
                for (auto& tile : world.tiles)
                    tiles.push_back({tile.name, &tile.heights, tile.originX, tile.originZ,
                                     world.settings.tileSize, world.settings.tileSize, world.settings.worldMap});
                return applyTerrainStampMultiTile(tiles, rule.stamp, rule.operation, rule.localMask,
                                                  rule.globalMask, world.settings.worldMap);
            }
        },
        variant);
}
}  // namespace

Result<int> TerrainWorldWorkspace::stamp(const Heightmap& stampMap, const TerrainStampSettings& settings,
                                          const Heightmap& local, const Heightmap& global) {
    return mutateWorld(impl_, [&](Impl& world) {
        std::vector<TerrainHeightTile> tiles;
        for (auto& tile : world.tiles) tiles.push_back({tile.name, &tile.heights, tile.originX, tile.originZ,
                                                        world.settings.tileSize, world.settings.tileSize,
                                                        world.settings.worldMap});
        return applyTerrainStampMultiTile(tiles, stampMap, settings, local, global, world.settings.worldMap);
    });
}
Result<int> TerrainWorldWorkspace::paintSplat(const Heightmap& paint, int layer,
                                               const TerrainStampSettings& operation) {
    return mutateWorld(impl_, [&](Impl& world) {
        std::vector<TerrainSplatTile> tiles;
        for (auto& tile : world.tiles) tiles.push_back({tile.name, &tile.splat, tile.originX, tile.originZ,
                                                        world.settings.tileSize, world.settings.tileSize,
                                                        world.settings.worldMap});
        return paintTerrainSplatLayerMultiTile(tiles, paint, layer, operation, world.settings.worldMap);
    });
}
Result<int> TerrainWorldWorkspace::applyDetail(const Heightmap& fitness, const TerrainDetailSettings& settings,
                                                const TerrainStampSettings& operation, TerrainDetailMode mode,
                                                std::int32_t seed) {
    auto configured = settings; configured.mode = mode;
    return mutateWorld(impl_, [&](Impl& world) {
        std::vector<TerrainDetailTile> tiles;
        for (auto& tile : world.tiles) tiles.push_back({tile.name, &tile.detail, tile.originX, tile.originZ,
                                                        world.settings.tileSize, world.settings.tileSize,
                                                        world.settings.worldMap});
        return applyTerrainDetailMultiTile(tiles, fitness, configured, operation, seed, world.settings.worldMap);
    });
}
Result<int> TerrainWorldWorkspace::applyTrees(const Heightmap& fitness,
                                               const TerrainTreePlacementSettings& settings,
                                               const TerrainStampSettings& operation, TerrainTreeOperationMode mode) {
    return mutateWorld(impl_, [&](Impl& world) {
        std::vector<TerrainTreeTile> tiles;
        for (auto& tile : world.tiles) tiles.push_back({tile.name, &tile.trees, &tile.heights, tile.originX,
                                                        tile.originZ, world.settings.tileSize, world.settings.tileSize,
                                                        world.settings.treeResolution, world.settings.treeResolution,
                                                        world.settings.worldMap});
        return applyTerrainTreesMultiTile(tiles, fitness, settings, operation, mode, world.settings.worldMap);
    });
}
Result<int> TerrainWorldWorkspace::applyObjects(const Heightmap& fitness,
                                                 const TerrainObjectPlacementSettings& settings,
                                                 const TerrainStampSettings& operation,
                                                 TerrainObjectOperationMode mode) {
    return mutateWorld(impl_, [&](Impl& world) {
        std::vector<TerrainObjectTile> tiles;
        for (auto& tile : world.tiles) tiles.push_back({tile.name, &tile.objects, &tile.heights, tile.originX,
                                                        tile.originZ, world.settings.tileSize, world.settings.tileSize,
                                                        world.settings.objectResolution, world.settings.objectResolution,
                                                        world.settings.worldMap});
        return applyTerrainObjectsMultiTile(tiles, fitness, settings, operation, mode, world.settings.worldMap);
    });
}

Result<int> TerrainWorldWorkspace::applyProbes(const Heightmap& fitness,
                                                const TerrainProbePlacementSettings& settings,
                                                const TerrainStampSettings& operation,
                                                TerrainProbeOperationMode mode) {
    return mutateWorld(impl_, [&](Impl& world) {
        std::vector<TerrainProbeTile> tiles;
        for (auto& tile : world.tiles)
            tiles.push_back({tile.name, &tile.probes, &tile.heights, tile.originX, tile.originZ,
                             world.settings.tileSize, world.settings.tileSize,
                             world.settings.objectResolution, world.settings.objectResolution,
                             world.settings.worldMap});
        return applyTerrainProbesMultiTile(tiles, fitness, settings, operation, mode, world.settings.worldMap);
    });
}

Result<int> TerrainWorldWorkspace::spawn(const TerrainSpawnPlan& plan) {
    if (plan.rules().empty())
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.world.spawn: nonempty spawn plan required"));
    bool hasEnabled = false;
    for (const auto& rule : plan.rules())
        std::visit([&](const auto& value) { hasEnabled = hasEnabled || value.enabled; }, rule);
    if (!hasEnabled)
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                      "terrain.world.spawn: at least one enabled rule required"));
    return mutateWorld(impl_, [&](Impl& world) -> Result<TerrainMultiTileReport> {
        TerrainMultiTileReport combined;
        std::unordered_set<std::string> affectedNames;
        for (const auto& variant : plan.rules()) {
            auto applied = applySpawnRule(world, variant);
            if (!applied.ok()) return Result<TerrainMultiTileReport>::failure(applied.status());
            auto report = std::move(applied.value());
            if (report.changedSamples > std::numeric_limits<int>::max() - combined.changedSamples)
                return Result<TerrainMultiTileReport>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "terrain.world.spawn: changed count exceeds integer range"));
            combined.changedSamples += report.changedSamples;
            for (const auto& mapping : report.mappings) affectedNames.insert(mapping.terrainName);
            combined.mappings.insert(combined.mappings.end(), report.mappings.begin(), report.mappings.end());
        }
        combined.affectedTiles = static_cast<int>(affectedNames.size());
        return Result<TerrainMultiTileReport>::success(std::move(combined));
    });
}

Result<int> TerrainWorldWorkspace::spawnBiome(const TerrainBiomePreset& preset) {
    auto plan = preset.compileForBiome();
    if (!plan.ok()) return Result<int>::failure(plan.status());
    return spawn(plan.value());
}

Result<int> TerrainWorldWorkspace::spawnStamper(const TerrainBiomePreset& preset) {
    auto plan = preset.compileForStamper();
    if (!plan.ok()) return Result<int>::failure(plan.status());
    return spawn(plan.value());
}

Result<TerrainWorldRunStatus> TerrainWorldWorkspace::beginSpawn(const TerrainSpawnPlan& plan) {
    if (!impl_ || impl_->tiles.empty() || impl_->runStatus == TerrainWorldRunStatus::Pending || plan.rules().empty())
        return Result<TerrainWorldRunStatus>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "terrain.world.beginSpawn: created workspace, nonempty plan and no pending spawn required"));
    bool hasEnabled = false;
    for (const auto& rule : plan.rules())
        std::visit([&](const auto& value) { hasEnabled = hasEnabled || value.enabled; }, rule);
    if (!hasEnabled)
        return Result<TerrainWorldRunStatus>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.world.beginSpawn: at least one enabled rule required"));
    impl_->runCandidate = std::make_unique<Impl>(*impl_);
    impl_->runPlan = plan;
    impl_->runNext = 0;
    impl_->runReport = {};
    impl_->runStatus = TerrainWorldRunStatus::Pending;
    return Result<TerrainWorldRunStatus>::success(impl_->runStatus);
}

Result<TerrainWorldRunStatus> TerrainWorldWorkspace::beginSpawnBiome(const TerrainBiomePreset& preset) {
    auto plan = preset.compileForBiome();
    if (!plan.ok()) return Result<TerrainWorldRunStatus>::failure(plan.status());
    return beginSpawn(plan.value());
}

Result<TerrainWorldRunStatus> TerrainWorldWorkspace::beginSpawnStamper(const TerrainBiomePreset& preset) {
    auto plan = preset.compileForStamper();
    if (!plan.ok()) return Result<TerrainWorldRunStatus>::failure(plan.status());
    return beginSpawn(plan.value());
}

Result<TerrainWorldRunStatus> TerrainWorldWorkspace::stepSpawn(int maxRules) {
    if (!impl_ || impl_->runStatus != TerrainWorldRunStatus::Pending || !impl_->runCandidate || maxRules <= 0)
        return Result<TerrainWorldRunStatus>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "terrain.world.stepSpawn: pending spawn and positive rule budget required"));
    int visited = 0;
    while (impl_->runNext < impl_->runPlan.rules().size() && visited < maxRules) {
        auto applied = applySpawnRule(*impl_->runCandidate, impl_->runPlan.rules()[impl_->runNext]);
        if (!applied.ok()) {
            impl_->runCandidate.reset();
            impl_->runStatus = TerrainWorldRunStatus::Failed;
            return Result<TerrainWorldRunStatus>::failure(applied.status());
        }
        const auto& report = applied.value();
        if (report.changedSamples > std::numeric_limits<int>::max() - impl_->runReport.changedSamples) {
            impl_->runCandidate.reset();
            impl_->runStatus = TerrainWorldRunStatus::Failed;
            return Result<TerrainWorldRunStatus>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "terrain.world.stepSpawn: changed count exceeds integer range"));
        }
        impl_->runReport.changedSamples += report.changedSamples;
        impl_->runReport.mappings.insert(impl_->runReport.mappings.end(), report.mappings.begin(),
                                         report.mappings.end());
        ++impl_->runNext;
        ++visited;
    }
    if (impl_->runNext == impl_->runPlan.rules().size()) {
        std::unordered_set<std::string> affected;
        for (const auto& mapping : impl_->runReport.mappings) affected.insert(mapping.terrainName);
        impl_->runReport.affectedTiles = static_cast<int>(affected.size());
        auto completed = std::move(impl_->runCandidate);
        completed->last = std::move(impl_->runReport);
        completed->record();
        completed->runStatus = TerrainWorldRunStatus::Completed;
        completed->runNext = impl_->runNext;
        impl_.swap(completed);
    }
    return Result<TerrainWorldRunStatus>::success(impl_->runStatus);
}

Result<TerrainWorldRunStatus> TerrainWorldWorkspace::cancelSpawn() {
    if (!impl_ || impl_->runStatus != TerrainWorldRunStatus::Pending)
        return Result<TerrainWorldRunStatus>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.world.cancelSpawn: no pending spawn"));
    impl_->runCandidate.reset();
    impl_->runPlan = {};
    impl_->runStatus = TerrainWorldRunStatus::Cancelled;
    return Result<TerrainWorldRunStatus>::success(impl_->runStatus);
}

TerrainWorldRunStatus TerrainWorldWorkspace::getSpawnStatus() const noexcept {
    return impl_ ? impl_->runStatus : TerrainWorldRunStatus::Idle;
}

int TerrainWorldWorkspace::getSpawnCompletedRules() const noexcept {
    return impl_ ? static_cast<int>(impl_->runNext) : 0;
}

Result<int> TerrainWorldWorkspace::flatten() {
    return mutateWorld(impl_, [](Impl& world) {
        TerrainMultiTileReport report;
        for (auto& tile : world.tiles) {
            int tileChanges = 0;
            for (float& height : tile.heights.data()) {
                if (height != 0) {
                    height = 0;
                    ++tileChanges;
                }
            }
            report.changedSamples += tileChanges;
            report.affectedTiles += tileChanges > 0;
        }
        return Result<TerrainMultiTileReport>::success(report);
    });
}

Result<int> TerrainWorldWorkspace::setHeightWorldUnits(float heightWorldUnits, float worldHeightSpan) {
    if (!std::isfinite(heightWorldUnits) || !std::isfinite(worldHeightSpan) || worldHeightSpan <= 0)
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "terrain.world.setHeightWorldUnits: finite elevation and positive span required"));
    const float normalized = std::clamp(heightWorldUnits / worldHeightSpan, 0.f, 1.f);
    return mutateWorld(impl_, [normalized](Impl& world) {
        TerrainMultiTileReport report;
        for (auto& tile : world.tiles) {
            int tileChanges = 0;
            for (float& height : tile.heights.data()) {
                if (height != normalized) {
                    height = normalized;
                    ++tileChanges;
                }
            }
            report.changedSamples += tileChanges;
            report.affectedTiles += tileChanges > 0;
        }
        return Result<TerrainMultiTileReport>::success(report);
    });
}

Result<int> TerrainWorldWorkspace::clearSpawns(const TerrainWorldClearSettings& settings) {
    if (!settings.details && !settings.trees && !settings.objects && !settings.probes)
        return Result<int>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.world.clearSpawns: at least one domain must be selected"));
    return mutateWorld(impl_, [&](Impl& world) {
        TerrainMultiTileReport report;
        const std::string source = std::to_string(settings.sourceNamespace);
        auto clearPoints = [&](PointSet& points) -> Result<int> {
            if (settings.sourceNamespace == 0) {
                const int changed = points.getCount();
                points.clear();
                return Result<int>::success(changed);
            }
            PointSet retained;
            retained.reserve(static_cast<std::size_t>(points.getCount()));
            int changed = 0;
            for (int i = 0; i < points.getCount(); ++i) {
                if (points.getStringAttribute(i, "spawnNamespace", "") == source) {
                    ++changed;
                    continue;
                }
                auto appended = retained.appendPointFrom(points, static_cast<std::size_t>(i));
                if (!appended.ok()) return Result<int>::failure(appended.status());
            }
            points = std::move(retained);
            return Result<int>::success(changed);
        };
        for (auto& tile : world.tiles) {
            int tileChanges = 0;
            if (settings.details) {
                if (settings.sourceNamespace == 0) {
                    for (int z = 0; z < tile.detail.getHeight(); ++z)
                        for (int x = 0; x < tile.detail.getWidth(); ++x)
                            tileChanges += tile.detail.sample(x, z).value() != 0;
                    auto reset = tile.detail.reset(tile.detail.getWidth(), tile.detail.getHeight(), 0);
                    if (!reset.ok()) return Result<TerrainMultiTileReport>::failure(reset.status());
                } else {
                    auto cleared = tile.detail.clearResource(settings.sourceNamespace);
                    if (!cleared.ok()) return Result<TerrainMultiTileReport>::failure(cleared.status());
                    tileChanges += cleared.value();
                }
            }
            if (settings.trees) {
                auto cleared = clearPoints(tile.trees);
                if (!cleared.ok()) return Result<TerrainMultiTileReport>::failure(cleared.status());
                tileChanges += cleared.value();
            }
            if (settings.objects) {
                auto cleared = clearPoints(tile.objects);
                if (!cleared.ok()) return Result<TerrainMultiTileReport>::failure(cleared.status());
                tileChanges += cleared.value();
            }
            if (settings.probes) {
                auto cleared = clearPoints(tile.probes);
                if (!cleared.ok()) return Result<TerrainMultiTileReport>::failure(cleared.status());
                tileChanges += cleared.value();
            }
            report.changedSamples += tileChanges;
            report.affectedTiles += tileChanges > 0;
        }
        return Result<TerrainMultiTileReport>::success(report);
    });
}

namespace {
template <class T>
Result<const TerrainWorldWorkspace::Impl::Tile*> worldTile(const std::unique_ptr<TerrainWorldWorkspace::Impl>& impl,
                                                            int index) {
    if (!impl || index < 0 || index >= static_cast<int>(impl->tiles.size()))
        return Result<const TerrainWorldWorkspace::Impl::Tile*>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.world: tile index out of range"));
    return Result<const TerrainWorldWorkspace::Impl::Tile*>::success(&impl->tiles[static_cast<std::size_t>(index)]);
}
}  // namespace
Result<int> TerrainWorldWorkspace::copyHeightmap(int index, Heightmap& output) const { auto tile = worldTile<int>(impl_, index); if (!tile.ok()) return Result<int>::failure(tile.status()); output = tile.value()->heights; return Result<int>::success(int(output.data().size())); }
Result<int> TerrainWorldWorkspace::copySplatmap(int index, TerrainSplatmap& output) const { auto tile = worldTile<int>(impl_, index); if (!tile.ok()) return Result<int>::failure(tile.status()); output = tile.value()->splat; return Result<int>::success(output.getWidth() * output.getHeight()); }
Result<int> TerrainWorldWorkspace::copyDetail(int index, TerrainDetailLayer& output) const { auto tile = worldTile<int>(impl_, index); if (!tile.ok()) return Result<int>::failure(tile.status()); output = tile.value()->detail; return Result<int>::success(output.getWidth() * output.getHeight()); }
Result<int> TerrainWorldWorkspace::copyTrees(int index, PointSet& output) const { auto tile = worldTile<int>(impl_, index); if (!tile.ok()) return Result<int>::failure(tile.status()); output = tile.value()->trees; return Result<int>::success(output.getCount()); }
Result<int> TerrainWorldWorkspace::copyObjects(int index, PointSet& output) const { auto tile = worldTile<int>(impl_, index); if (!tile.ok()) return Result<int>::failure(tile.status()); output = tile.value()->objects; return Result<int>::success(output.getCount()); }
Result<int> TerrainWorldWorkspace::copyProbes(int index, PointSet& output) const { auto tile = worldTile<int>(impl_, index); if (!tile.ok()) return Result<int>::failure(tile.status()); output = tile.value()->probes; return Result<int>::success(output.getCount()); }
Result<std::string> TerrainWorldWorkspace::getTileName(int index) const { auto tile = worldTile<int>(impl_, index); if (!tile.ok()) return Result<std::string>::failure(tile.status()); return Result<std::string>::success(tile.value()->name); }
Result<double> TerrainWorldWorkspace::getTileOriginX(int index) const { auto tile = worldTile<int>(impl_, index); if (!tile.ok()) return Result<double>::failure(tile.status()); return Result<double>::success(tile.value()->originX); }
Result<double> TerrainWorldWorkspace::getTileOriginZ(int index) const { auto tile = worldTile<int>(impl_, index); if (!tile.ok()) return Result<double>::failure(tile.status()); return Result<double>::success(tile.value()->originZ); }
Result<int> TerrainWorldWorkspace::undo() {
    if (!impl_ || impl_->runStatus == TerrainWorldRunStatus::Pending || impl_->cursor <= 0)
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.world: no undo snapshot"));
    auto candidate = std::make_unique<Impl>(*impl_);
    --candidate->cursor;
    candidate->restore(candidate->history[static_cast<std::size_t>(candidate->cursor)]);
    const int cursor = candidate->cursor;
    impl_.swap(candidate);
    return Result<int>::success(cursor);
}

Result<int> TerrainWorldWorkspace::redo() {
    if (!impl_ || impl_->runStatus == TerrainWorldRunStatus::Pending ||
        impl_->cursor + 1 >= static_cast<int>(impl_->history.size()))
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.world: no redo snapshot"));
    auto candidate = std::make_unique<Impl>(*impl_);
    ++candidate->cursor;
    candidate->restore(candidate->history[static_cast<std::size_t>(candidate->cursor)]);
    const int cursor = candidate->cursor;
    impl_.swap(candidate);
    return Result<int>::success(cursor);
}
int TerrainWorldWorkspace::getTileCount() const noexcept { return impl_ ? static_cast<int>(impl_->tiles.size()) : 0; }
int TerrainWorldWorkspace::getLastChangedSamples() const noexcept { return impl_ ? impl_->last.changedSamples : 0; }
int TerrainWorldWorkspace::getLastAffectedTiles() const noexcept { return impl_ ? impl_->last.affectedTiles : 0; }
int TerrainWorldWorkspace::getOperationCount() const noexcept { return impl_ && !impl_->history.empty() ? static_cast<int>(impl_->history.size()) - 1 : 0; }
int TerrainWorldWorkspace::getAppliedCount() const noexcept { return impl_ ? impl_->cursor : 0; }
}  // namespace eve::procgen
