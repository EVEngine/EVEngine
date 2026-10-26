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

void PlacementSystem::writeOccupancy(PlacementWorld &world, const BuildingDefinition &def, const PlacedBuilding &placed,
                                     int instanceId) {
    std::vector<int> &occ = world.channelOccupancy(def.channel, placed.level);
    foreachFootprintCell(def, placed.originCellX, placed.originCellY, placed.rotationDeg, [&](int cx, int cy) {
        if (world.inBounds(cx, cy)) {
            const size_t idx = size_t(cy) * size_t(world.getWidth()) + size_t(cx);
            if (idx < occ.size()) occ[idx] = instanceId;
        }
        return true;
    });
}

void PlacementSystem::clearOccupancy(PlacementWorld &world, int instanceId) {
    // 默认通道（""）占用存于 occupancy_，附加通道存于 allChannels_，两者都要清。
    for (int &v : world.occupancy()) {
        if (v == instanceId) v = 0;
    }
    for (auto &kv : world.allChannels()) {
        for (int &v : kv.second) {
            if (v == instanceId) v = 0;
        }
    }
    for (auto &kv : world.horizontalEdgeChannels_) {
        for (int &v : kv.second)
            if (v == instanceId) v = 0;
    }
    for (auto &kv : world.verticalEdgeChannels_) {
        for (int &v : kv.second)
            if (v == instanceId) v = 0;
    }
    for (auto &kv : world.cornerChannels_) {
        for (int &v : kv.second)
            if (v == instanceId) v = 0;
    }
    for (auto &level : world.cellChannelsByLevel_)
        for (auto &channel : level.second)
            for (int &value : channel.second)
                if (value == instanceId) value = 0;
    for (auto &levels : {&world.horizontalEdgesByLevel_, &world.verticalEdgesByLevel_})
        for (auto &level : *levels)
            for (auto &channel : level.second)
                for (int &value : channel.second)
                    if (value == instanceId) value = 0;
    for (auto &level : world.cornersByLevel_)
        for (auto &channel : level.second)
            for (int &value : channel.second)
                if (value == instanceId) value = 0;
}

int PlacementSystem::placeAt(PlacementWorld *world, const std::string &buildingId, int cellX, int cellY,
                             float rotationDeg) {
    ensureBuiltins();
    if (!world) return 0;
    const BuildingDefinition *def = BuildingRegistry::find(buildingId);
    if (!def) return 0;
    if (def->placementKind != "cell") return 0;
    const float rot = normalizeRotation(buildingId, rotationDeg);
    if (!canPlace(world, buildingId, cellX, cellY, rot, 0, nullptr)) return 0;

    PlacedBuilding pb;
    pb.instanceId  = nextInstanceId(*world);
    pb.buildingId  = buildingId;
    pb.originCellX = cellX;
    pb.originCellY = cellY;
    pb.level       = world->getActiveLevel();
    world->cellToWorldPlane(cellX, cellY, pb.worldX, pb.worldY);
    pb.elevation   = 0.f;
    pb.rotationDeg = rot;
    pb.channel     = def->channel;
    pb.tags        = def->tags;
    PlacementQuery supportQuery;
    supportQuery.cellX       = pb.originCellX;
    supportQuery.cellY       = pb.originCellY;
    supportQuery.level       = pb.level;
    supportQuery.rotationDeg = pb.rotationDeg;
    pb.supportInstanceIds    = collectStructuralSupports(*world, *def, supportQuery);

    writeOccupancy(*world, *def, pb, pb.instanceId);
    world->buildings()[pb.instanceId] = pb;
    world->instanceOrder_.push_back(pb.instanceId);

    BuildingChangeEvent ev;
    ev.action      = "place";
    ev.worldId     = world->getId();
    ev.buildingId  = buildingId;
    ev.instanceId  = pb.instanceId;
    ev.cellX       = cellX;
    ev.cellY       = cellY;
    ev.rotationDeg = rot;
    ev.worldX      = pb.worldX;
    ev.worldY      = pb.worldY;
    ev.elevation   = pb.elevation;
    ev.channel     = pb.channel;
    if (world->publishEvents_) emit(std::move(ev));
    return pb.instanceId;
}

int PlacementSystem::placeAtWorld(PlacementWorld *world, const std::string &buildingId, float worldX, float worldY,
                                  float rotationDeg) {
    ensureBuiltins();
    if (!world) return 0;
    const SnapResult s  = snap(*world, buildingId, worldX, worldY);
    const int        id = placeAt(world, buildingId, s.cellX, s.cellY, rotationDeg);
    if (id <= 0) return 0;
    // Preserve free / custom snap world pose (cell placement alone snaps to cell origin).
    auto it = world->buildings().find(id);
    if (it != world->buildings().end()) {
        it->second.worldX = s.worldX;
        it->second.worldY = s.worldY;
    }
    return id;
}

int PlacementSystem::placeAtWorld3D(PlacementWorld *world, const std::string &buildingId, float worldX, float worldY,
                                    float worldZ, float rotationDeg) {
    ensureBuiltins();
    if (!world) return 0;
    const SnapResult s  = snap3D(*world, buildingId, worldX, worldY, worldZ);
    const int        id = placeAt(world, buildingId, s.cellX, s.cellY, rotationDeg);
    if (id <= 0) return 0;
    auto it = world->buildings().find(id);
    if (it != world->buildings().end()) {
        it->second.worldX    = s.worldX;
        it->second.worldY    = s.worldY;
        it->second.elevation = s.elevation;
    }
    return id;
}

int PlacementSystem::placeGhost(PlacementWorld *world, Ghost *ghost) {
    if (!world || !ghost) return 0;
    if (!ghost->validate(world)) return 0;
    const BuildingDefinition *def = BuildingRegistry::find(ghost->getBuildingId());
    if (!def) return 0;
    if (def->placementKind == "edge") {
        const char *direction = ghost->edge_.axis == EdgeAxis::Horizontal ? "north" : "west";
        return placeEdge(world, ghost->getBuildingId(), ghost->edge_.x, ghost->edge_.y, direction);
    }
    if (def->placementKind == "corner") {
        if (ghost->placementKind_ != "corner") return 0;
        return placeCorner(world, ghost->getBuildingId(), ghost->corner_.x, ghost->corner_.y);
    }
    if (def->placementKind == "free") {
        if (ghost->placementKind_ != "free") return 0;
        if (!ghost->surfaceId_.empty()) {
            SurfacePatch patch;
            if (world->getGrid().plane == grid::GridPlane::XZ) {
                patch.anchor.worldX = ghost->worldX_;
                patch.anchor.worldY = ghost->elevation_;
                patch.anchor.worldZ = ghost->worldY_;
            } else {
                patch.anchor.worldX = ghost->worldX_;
                patch.anchor.worldY = ghost->worldY_;
                patch.anchor.worldZ = ghost->elevation_;
            }
            patch.anchor.surfaceId       = ghost->surfaceId_;
            patch.anchor.surfaceRevision = ghost->surfaceRevision_;
            patch.anchor.normalX         = ghost->surfaceNormalX_;
            patch.anchor.normalY         = ghost->surfaceNormalY_;
            patch.anchor.normalZ         = ghost->surfaceNormalZ_;
            patch.anchor.tangentX        = ghost->surfaceTangentX_;
            patch.anchor.tangentY        = ghost->surfaceTangentY_;
            patch.anchor.tangentZ        = ghost->surfaceTangentZ_;
            patch.samples.resize(static_cast<size_t>(std::max(ghost->surfaceSampleCount_, 0)));
            patch.maxSlopeDegrees = ghost->surfaceMaxSlopeDegrees_;
            patch.heightDelta     = ghost->surfaceHeightDelta_;
            auto result           = placeFreeSurfaceResult(world, ghost->getBuildingId(), patch, ghost->rotationDeg_);
            return result.ok() ? std::move(result).takeValue().instanceId : 0;
        }
        return placeFree(world, ghost->getBuildingId(), ghost->worldX_, ghost->worldY_, ghost->elevation_,
                         ghost->rotationDeg_);
    }

    PlacedBuilding placed;
    placed.instanceId             = nextInstanceId(*world);
    placed.buildingId             = ghost->getBuildingId();
    placed.originCellX            = ghost->getCellX();
    placed.originCellY            = ghost->getCellY();
    placed.level                  = world->getActiveLevel();
    placed.worldX                 = ghost->getWorldX();
    placed.worldY                 = ghost->getWorldY();
    placed.elevation              = ghost->getElevation();
    placed.rotationDeg            = ghost->getRotationDeg();
    placed.channel                = def->channel;
    placed.tags                   = def->tags;
    placed.surfaceId              = ghost->surfaceId_;
    placed.surfaceRevision        = ghost->surfaceRevision_;
    placed.surfaceNormalX         = ghost->surfaceNormalX_;
    placed.surfaceNormalY         = ghost->surfaceNormalY_;
    placed.surfaceNormalZ         = ghost->surfaceNormalZ_;
    placed.surfaceTangentX        = ghost->surfaceTangentX_;
    placed.surfaceTangentY        = ghost->surfaceTangentY_;
    placed.surfaceTangentZ        = ghost->surfaceTangentZ_;
    placed.surfaceSampleCount     = ghost->surfaceSampleCount_;
    placed.surfaceMaxSlopeDegrees = ghost->surfaceMaxSlopeDegrees_;
    placed.surfaceHeightDelta     = ghost->surfaceHeightDelta_;
    PlacementQuery supportQuery;
    supportQuery.cellX        = placed.originCellX;
    supportQuery.cellY        = placed.originCellY;
    supportQuery.level        = placed.level;
    supportQuery.rotationDeg  = placed.rotationDeg;
    placed.supportInstanceIds = collectStructuralSupports(*world, *def, supportQuery);

    writeOccupancy(*world, *def, placed, placed.instanceId);
    world->buildings()[placed.instanceId] = placed;
    world->instanceOrder_.push_back(placed.instanceId);

    BuildingChangeEvent event;
    event.action      = "place";
    event.worldId     = world->getId();
    event.buildingId  = placed.buildingId;
    event.instanceId  = placed.instanceId;
    event.cellX       = placed.originCellX;
    event.cellY       = placed.originCellY;
    event.rotationDeg = placed.rotationDeg;
    event.worldX      = placed.worldX;
    event.worldY      = placed.worldY;
    event.elevation   = placed.elevation;
    event.channel     = placed.channel;
    if (world->publishEvents_) emit(std::move(event));
    return placed.instanceId;
}


}  // namespace eve::building
