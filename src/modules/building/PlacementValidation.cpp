#include "building/PlacementInternal.h"

#include "building/BuildingDef.h"
#include "building/Ghost.h"
#include "building/HeightfieldSurface.h"
#include "building/PlacementWorld.h"
#include "building/StaticMeshSurface.h"
#include "common/Diagnostic.h"
#include "grid/GridProjection.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace eve::building {

using namespace placement_detail;

bool PlacementSystem::checkBoundsAndOccupancy(const PlacementWorld &world, const BuildingDefinition &def,
                                              const PlacementQuery &q, bool checkOccupancy, std::string *reason) {
    bool ok = true;
    foreachFootprintCell(def, q.cellX, q.cellY, q.rotationDeg, [&](int cx, int cy) {
        if (!world.inBounds(cx, cy)) {
            if (reason) *reason = "out_of_bounds";
            ok = false;
            return false;
        }
        if (checkOccupancy) {
            const int occ = world.getOccupantAtLevel(def.channel, cx, cy, q.level);
            if (occ != 0 && occ != q.excludeInstanceId) {
                if (reason) *reason = "occupied";
                ok = false;
                return false;
            }
        }
        return true;
    });
    return ok;
}

bool PlacementSystem::checkTerrain(const PlacementWorld &world, const BuildingDefinition &def, const PlacementQuery &q,
                                   std::string *reason) {
    if (def.requireTerrain.empty() && def.forbidTerrain.empty()) return true;
    bool ok = true;
    foreachFootprintCell(def, q.cellX, q.cellY, q.rotationDeg, [&](int cx, int cy) {
        const int sem = world.getTerrain(cx, cy);
        if (!def.forbidTerrain.empty()) {
            if (std::find(def.forbidTerrain.begin(), def.forbidTerrain.end(), sem) != def.forbidTerrain.end()) {
                if (reason) *reason = "terrain_forbidden";
                ok = false;
                return false;
            }
        }
        if (!def.requireTerrain.empty()) {
            if (std::find(def.requireTerrain.begin(), def.requireTerrain.end(), sem) == def.requireTerrain.end()) {
                if (reason) *reason = "terrain_mismatch";
                ok = false;
                return false;
            }
        }
        return true;
    });
    return ok;
}

bool PlacementSystem::checkAdjacency(const PlacementWorld &world, const BuildingDefinition &def,
                                     const PlacementQuery &q, std::string *reason) {
    const bool needTag     = !def.requireAdjacentTag.empty();
    const bool needTerrain = def.requireAdjacentTerrain >= 0;
    if (!needTag && !needTerrain) return true;

    // Collect footprint cells + expand by 1 (4-neigh).
    std::vector<std::pair<int, int>> cells;
    foreachFootprintCell(def, q.cellX, q.cellY, q.rotationDeg, [&](int cx, int cy) {
        cells.emplace_back(cx, cy);
        return true;
    });

    auto isFootprint = [&](int x, int y) {
        return std::find(cells.begin(), cells.end(), std::make_pair(x, y)) != cells.end();
    };

    bool             tagOk     = !needTag;
    bool             terrainOk = !needTerrain;
    static const int dx[4]     = {1, -1, 0, 0};
    static const int dy[4]     = {0, 0, 1, -1};

    for (const auto &c : cells) {
        for (int i = 0; i < 4; ++i) {
            const int nx = c.first + dx[i];
            const int ny = c.second + dy[i];
            if (isFootprint(nx, ny)) continue;
            if (!world.inBounds(nx, ny)) continue;

            if (needTag && !tagOk) {
                const int occ = world.getAnyOccupantAtLevel(nx, ny, q.level);
                if (occ != 0 && occ != q.excludeInstanceId) {
                    auto it = world.buildings().find(occ);
                    if (it != world.buildings().end()) {
                        const PlacedBuilding &pb  = it->second;
                        bool                  hit = pb.hasTag(def.requireAdjacentTag);
                        if (!hit) {
                            if (const BuildingDefinition *odef = BuildingRegistry::find(pb.buildingId)) {
                                hit = odef->hasTag(def.requireAdjacentTag);
                            }
                        }
                        if (hit) tagOk = true;
                    }
                }
            }
            if (needTerrain && !terrainOk) {
                if (world.getTerrain(nx, ny) == def.requireAdjacentTerrain) terrainOk = true;
            }
            if (tagOk && terrainOk) return true;
        }
    }

    if (needTag && !tagOk) {
        if (reason) *reason = "adjacency_tag";
        return false;
    }
    if (needTerrain && !terrainOk) {
        if (reason) *reason = "adjacency_terrain";
        return false;
    }
    return true;
}

bool PlacementSystem::runValidate(const PlacementWorld &world, const BuildingDefinition &def, const PlacementQuery &q,
                                  std::string *reason) {
    ensureBuiltins();
    std::string rule = def.validateRule.empty() ? world.getValidateRule() : def.validateRule;
    if (rule.empty()) rule = "default";
    auto &rules = validateRules();
    auto  it    = rules.find(rule);
    if (it == rules.end() || !it->second) {
        it = rules.find("default");
    }
    if (it == rules.end() || !it->second) {
        if (reason) *reason = "validate_rejected";
        return false;
    }
    return it->second(world, q, reason);
}

bool PlacementSystem::canPlaceElev(PlacementWorld *world, const std::string &buildingId, int cellX, int cellY,
                                   float elevation, float rotationDeg, int excludeInstanceId, std::string *reason,
                                   int level) {
    const BuildingDefinition *definition = BuildingRegistry::find(buildingId);
    if (definition && definition->placementKind != "cell") {
        if (reason) {
            if (definition->placementKind == "edge")
                *reason = "edge_requires_edge_address";
            else if (definition->placementKind == "corner")
                *reason = "corner_requires_corner_address";
            else
                *reason = "free_requires_free_address";
        }
        return false;
    }
    PlacementQuery q;
    q.buildingId  = buildingId;
    q.cellX       = cellX;
    q.cellY       = cellY;
    q.level       = level == std::numeric_limits<int>::min() && world ? world->getActiveLevel() : level;
    q.rotationDeg = normalizeRotation(buildingId, rotationDeg);
    if (world) world->cellToWorldPlane(cellX, cellY, q.worldX, q.worldY);
    q.elevation         = elevation;
    q.excludeInstanceId = excludeInstanceId;
    return canPlaceQuery(world, q, reason);
}

bool PlacementSystem::checkSurfacePatch(const BuildingDefinition &def, const PlacementQuery &q, std::string *reason) {
    if (q.surfaceSampleCount <= 0) return true;
    if (q.surfaceMaxSlopeDegrees > def.maxSurfaceSlopeDegrees + 1e-4f) {
        if (reason) *reason = "surface_slope";
        return false;
    }
    if (def.maxSurfaceHeightDelta >= 0.f && q.surfaceHeightDelta > def.maxSurfaceHeightDelta + 1e-4f) {
        if (reason) *reason = "surface_height_delta";
        return false;
    }
    return true;
}

bool PlacementSystem::checkStructuralSupport(const PlacementWorld &world, const BuildingDefinition &def,
                                             const PlacementQuery &q, std::string *reason) {
    if (def.supportMode == "corner_below") {
        if (def.placementKind != "corner") {
            if (reason) *reason = "support_mode_mismatch";
            return false;
        }
        const int  instanceId = world.getAnyCornerOccupantAtLevel(q.cellX, q.cellY, q.level - 1);
        const auto found      = world.buildings().find(instanceId);
        if (instanceId == 0 || found == world.buildings().end()) {
            if (reason) *reason = "support_missing";
            return false;
        }
        if (def.supportTag.empty()) return true;
        const BuildingDefinition *support = BuildingRegistry::find(found->second.buildingId);
        const bool tagged = found->second.hasTag(def.supportTag) || (support && support->hasTag(def.supportTag));
        if (!tagged && reason) *reason = "support_missing";
        return tagged;
    }
    if (def.supportMode != "cell_below") return true;
    bool supported = true;
    foreachFootprintCell(def, q.cellX, q.cellY, q.rotationDeg, [&](int cellX, int cellY) {
        const int  instanceId = world.getAnyOccupantAtLevel(cellX, cellY, q.level - 1);
        const auto found      = world.buildings().find(instanceId);
        if (instanceId == 0 || found == world.buildings().end()) {
            supported = false;
        } else if (!def.supportTag.empty()) {
            const PlacedBuilding     &placed  = found->second;
            const BuildingDefinition *support = BuildingRegistry::find(placed.buildingId);
            // Footprint iteration visits every cell even after a callback returns false.
            // A later tagged support must not erase an earlier missing/invalid support.
            supported = supported && (placed.hasTag(def.supportTag) || (support && support->hasTag(def.supportTag)));
        }
        if (!supported && reason) *reason = "support_missing";
        return supported;
    });
    return supported;
}

std::vector<int> PlacementSystem::collectStructuralSupports(const PlacementWorld &world, const BuildingDefinition &def,
                                                            const PlacementQuery &q) {
    std::vector<int> result;
    if (def.supportMode == "corner_below") {
        const int support = world.getAnyCornerOccupantAtLevel(q.cellX, q.cellY, q.level - 1);
        if (support != 0) result.push_back(support);
        return result;
    }
    if (def.supportMode != "cell_below") return result;
    foreachFootprintCell(def, q.cellX, q.cellY, q.rotationDeg, [&](int cellX, int cellY) {
        const int support = world.getAnyOccupantAtLevel(cellX, cellY, q.level - 1);
        if (support != 0 && std::find(result.begin(), result.end(), support) == result.end()) result.push_back(support);
        return true;
    });
    return result;
}

bool PlacementSystem::hasStructuralDependents(const PlacementWorld &world, int instanceId) {
    for (const auto &[id, placed] : world.buildings_) {
        (void)id;
        if (std::find(placed.supportInstanceIds.begin(), placed.supportInstanceIds.end(), instanceId) !=
            placed.supportInstanceIds.end())
            return true;
    }
    return false;
}

bool PlacementSystem::replacementPreservesStructuralDependents(const PlacementWorld &world, int instanceId,
                                                               const BuildingDefinition &replacement) {
    const auto sourceIt = world.buildings_.find(instanceId);
    if (sourceIt == world.buildings_.end()) return false;
    const PlacedBuilding &source = sourceIt->second;

    std::unordered_set<uint64_t> replacementCells;
    if (replacement.placementKind != "edge") {
        foreachFootprintCell(replacement, source.originCellX, source.originCellY, source.rotationDeg,
                             [&](int x, int y) {
                                 replacementCells.insert((uint64_t(uint32_t(x)) << 32U) | uint32_t(y));
                                 return true;
                             });
    }

    for (const auto &[candidateId, dependent] : world.buildings_) {
        (void)candidateId;
        if (std::find(dependent.supportInstanceIds.begin(), dependent.supportInstanceIds.end(), instanceId) ==
            dependent.supportInstanceIds.end())
            continue;
        const BuildingDefinition *dependentDef = BuildingRegistry::find(dependent.buildingId);
        if (!dependentDef) return false;
        const bool tagMatches = dependentDef->supportTag.empty() || replacement.hasTag(dependentDef->supportTag);
        if (!tagMatches) return false;
        if (dependentDef->supportMode == "corner_below") {
            if (replacement.placementKind != "corner" || source.corner != dependent.corner) return false;
            continue;
        }
        if (dependentDef->supportMode != "cell_below" || replacement.placementKind != "cell") return false;
        bool covered = true;
        foreachFootprintCell(*dependentDef, dependent.originCellX, dependent.originCellY, dependent.rotationDeg,
                             [&](int x, int y) {
                                 if (world.getAnyOccupantAtLevel(x, y, dependent.level - 1) != instanceId) return true;
                                 const uint64_t key = (uint64_t(uint32_t(x)) << 32U) | uint32_t(y);
                                 covered            = replacementCells.contains(key);
                                 return covered;
                             });
        if (!covered) return false;
    }
    return true;
}

bool PlacementSystem::canPlaceQuery(PlacementWorld *world, const PlacementQuery &q, std::string *reason) {
    ensureBuiltins();
    if (!world) {
        if (reason) *reason = "no_world";
        return false;
    }
    const BuildingDefinition *def = BuildingRegistry::find(q.buildingId);
    if (!def) {
        if (reason) *reason = "unknown_building";
        return false;
    }
    return runValidate(*world, *def, q, reason);
}

bool PlacementSystem::canPlace(PlacementWorld *world, const std::string &buildingId, int cellX, int cellY,
                               float rotationDeg, int excludeInstanceId, std::string *reason) {
    return canPlaceElev(world, buildingId, cellX, cellY, 0.f, rotationDeg, excludeInstanceId, reason);
}


}  // namespace eve::building
