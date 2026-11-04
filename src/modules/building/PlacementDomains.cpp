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

eve::Result<EdgeAddress> PlacementSystem::canonicalEdge(int cellX, int cellY, const std::string &direction) {
    EdgeAddress edge;
    edge.x = cellX;
    edge.y = cellY;
    if (direction == "north" || direction == "n") {
        edge.axis = EdgeAxis::Horizontal;
    } else if (direction == "south" || direction == "s") {
        edge.axis = EdgeAxis::Horizontal;
        ++edge.y;
    } else if (direction == "west" || direction == "w") {
        edge.axis = EdgeAxis::Vertical;
    } else if (direction == "east" || direction == "e") {
        edge.axis = EdgeAxis::Vertical;
        ++edge.x;
    } else {
        return eve::Result<EdgeAddress>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                        "edge direction must be north/east/south/west",
                                                                        direction, {}, "building.edge"));
    }
    return eve::Result<EdgeAddress>::success(edge);
}

bool PlacementSystem::canPlaceEdge(PlacementWorld *world, const std::string &buildingId, int cellX, int cellY,
                                   const std::string &direction, int excludeInstanceId, std::string *reason,
                                   int level) {
    if (!world) {
        if (reason) *reason = "no_world";
        return false;
    }
    const BuildingDefinition *def = BuildingRegistry::find(buildingId);
    if (!def) {
        if (reason) *reason = "unknown_building";
        return false;
    }
    if (def->placementKind != "edge") {
        if (reason) *reason = "not_edge_building";
        return false;
    }
    auto result = canonicalEdge(cellX, cellY, direction);
    if (!result.ok()) {
        if (reason) *reason = "invalid_edge_direction";
        return false;
    }
    const EdgeAddress edge     = std::move(result).takeValue();
    const bool        inBounds = edge.axis == EdgeAxis::Horizontal
                                     ? edge.x >= 0 && edge.x < world->width_ && edge.y >= 0 && edge.y <= world->height_
                                     : edge.x >= 0 && edge.x <= world->width_ && edge.y >= 0 && edge.y < world->height_;
    if (!inBounds) {
        if (reason) *reason = "edge_out_of_bounds";
        return false;
    }
    const int resolvedLevel = level == std::numeric_limits<int>::min() ? world->getActiveLevel() : level;
    for (const auto &[id, placed] : world->buildings_) {
        if (id == excludeInstanceId || placed.placementKind != "free" || placed.level != resolvedLevel) continue;
        const auto *freeDef = BuildingRegistry::find(placed.buildingId);
        if (freeDef && hasFreeEdgeOverlap(*world, *freeDef, placedFreeFootprint(placed), *def, edge)) {
            if (reason) *reason = "edge_free_overlap";
            return false;
        }
    }
    const auto  *channels = world->findEdgeChannels(edge.axis, resolvedLevel);
    const size_t index    = edge.axis == EdgeAxis::Horizontal
                                ? size_t(edge.y) * size_t(world->width_) + size_t(edge.x)
                                : size_t(edge.y) * size_t(world->width_ + 1) + size_t(edge.x);
    if (!channels) return true;
    const auto channelIt = channels->find(def->channel);
    const int  occupant =
        channelIt != channels->end() && index < channelIt->second.size() ? channelIt->second[index] : 0;
    if (occupant != 0 && occupant != excludeInstanceId) {
        if (reason) *reason = "edge_occupied";
        return false;
    }
    return true;
}

int PlacementSystem::placeEdge(PlacementWorld *world, const std::string &buildingId, int cellX, int cellY,
                               const std::string &direction) {
    if (!canPlaceEdge(world, buildingId, cellX, cellY, direction, 0, nullptr)) return 0;
    const BuildingDefinition *def     = BuildingRegistry::find(buildingId);
    auto                      address = canonicalEdge(cellX, cellY, direction);
    if (!def || !address.ok()) return 0;
    const EdgeAddress edge = std::move(address).takeValue();

    PlacedBuilding placed;
    placed.instanceId    = nextInstanceId(*world);
    placed.buildingId    = buildingId;
    placed.placementKind = "edge";
    placed.edge          = edge;
    placed.originCellX   = edge.x;
    placed.originCellY   = edge.y;
    placed.level         = world->getActiveLevel();
    placed.channel       = def->channel;
    placed.tags          = def->tags;
    float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
    world->cellToWorldPlane(edge.x, edge.y, ax, ay);
    world->cellToWorldPlane(edge.x + (edge.axis == EdgeAxis::Horizontal ? 1 : 0),
                            edge.y + (edge.axis == EdgeAxis::Vertical ? 1 : 0), bx, by);
    placed.worldX      = (ax + bx) * 0.5f;
    placed.worldY      = (ay + by) * 0.5f;
    placed.rotationDeg = edge.axis == EdgeAxis::Horizontal ? 0.f : 90.f;

    auto        &channels  = world->edgeChannels(edge.axis, placed.level);
    auto        &occupancy = channels[def->channel];
    const size_t required  = edge.axis == EdgeAxis::Horizontal ? size_t(world->width_) * size_t(world->height_ + 1)
                                                               : size_t(world->width_ + 1) * size_t(world->height_);
    if (occupancy.size() != required) occupancy.assign(required, 0);
    const size_t index                    = edge.axis == EdgeAxis::Horizontal
                                                ? size_t(edge.y) * size_t(world->width_) + size_t(edge.x)
                                                : size_t(edge.y) * size_t(world->width_ + 1) + size_t(edge.x);
    occupancy[index]                      = placed.instanceId;
    world->buildings()[placed.instanceId] = placed;
    world->instanceOrder_.push_back(placed.instanceId);

    BuildingChangeEvent event;
    event.action      = "place";
    event.worldId     = world->getId();
    event.buildingId  = buildingId;
    event.instanceId  = placed.instanceId;
    event.cellX       = edge.x;
    event.cellY       = edge.y;
    event.rotationDeg = placed.rotationDeg;
    event.worldX      = placed.worldX;
    event.worldY      = placed.worldY;
    event.channel     = placed.channel;
    if (world->publishEvents_) emit(std::move(event));
    return placed.instanceId;
}

bool PlacementSystem::canPlaceCorner(PlacementWorld *world, const std::string &buildingId, int vertexX, int vertexY,
                                     int excludeInstanceId, std::string *reason, int level) {
    if (!world) {
        if (reason) *reason = "no_world";
        return false;
    }
    const BuildingDefinition *def = BuildingRegistry::find(buildingId);
    if (!def) {
        if (reason) *reason = "unknown_building";
        return false;
    }
    if (def->placementKind != "corner") {
        if (reason) *reason = "not_corner_building";
        return false;
    }
    if (vertexX < 0 || vertexX > world->width_ || vertexY < 0 || vertexY > world->height_) {
        if (reason) *reason = "corner_out_of_bounds";
        return false;
    }
    const int resolvedLevel = level == std::numeric_limits<int>::min() ? world->getActiveLevel() : level;
    const int occupant      = world->getCornerOccupantAtLevel(def->channel, vertexX, vertexY, resolvedLevel);
    if (occupant != 0 && occupant != excludeInstanceId) {
        if (reason) *reason = "corner_occupied";
        return false;
    }
    PlacementQuery query;
    query.buildingId        = buildingId;
    query.cellX             = vertexX;
    query.cellY             = vertexY;
    query.level             = resolvedLevel;
    query.excludeInstanceId = excludeInstanceId;
    return checkStructuralSupport(*world, *def, query, reason);
}

eve::Result<PlacedBuilding> PlacementSystem::placeCornerResult(PlacementWorld *world, const std::string &buildingId,
                                                               int vertexX, int vertexY) {
    std::string reason;
    if (!canPlaceCorner(world, buildingId, vertexX, vertexY, 0, &reason)) {
        return eve::Result<PlacedBuilding>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                                                           "corner placement rejected without mutation",
                                                                           reason, {}, "building.corner"));
    }
    const BuildingDefinition *def = BuildingRegistry::find(buildingId);
    if (!def) {
        return eve::Result<PlacedBuilding>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "corner definition was not found", buildingId, {}, "building.corner"));
    }

    PlacedBuilding placed;
    placed.instanceId    = nextInstanceId(*world);
    placed.buildingId    = buildingId;
    placed.placementKind = "corner";
    placed.corner        = CornerAddress{vertexX, vertexY};
    placed.originCellX   = vertexX;
    placed.originCellY   = vertexY;
    placed.level         = world->getActiveLevel();
    placed.channel       = def->channel;
    placed.tags          = def->tags;
    world->cellToWorldPlane(vertexX, vertexY, placed.worldX, placed.worldY);
    PlacementQuery supportQuery;
    supportQuery.cellX        = vertexX;
    supportQuery.cellY        = vertexY;
    supportQuery.level        = placed.level;
    placed.supportInstanceIds = collectStructuralSupports(*world, *def, supportQuery);

    auto        &occupancy = world->cornerChannels(placed.level)[def->channel];
    const size_t required  = size_t(world->width_ + 1) * size_t(world->height_ + 1);
    if (occupancy.size() != required) occupancy.assign(required, 0);
    occupancy[size_t(vertexY) * size_t(world->width_ + 1) + size_t(vertexX)] = placed.instanceId;
    world->buildings_[placed.instanceId]                                     = placed;
    world->instanceOrder_.push_back(placed.instanceId);

    BuildingChangeEvent event;
    event.action     = "place";
    event.worldId    = world->getId();
    event.buildingId = buildingId;
    event.instanceId = placed.instanceId;
    event.cellX      = vertexX;
    event.cellY      = vertexY;
    event.level      = placed.level;
    event.worldX     = placed.worldX;
    event.worldY     = placed.worldY;
    event.channel    = placed.channel;
    if (world->publishEvents_) emit(std::move(event));
    return eve::Result<PlacedBuilding>::success(std::move(placed));
}

int PlacementSystem::placeCorner(PlacementWorld *world, const std::string &buildingId, int vertexX, int vertexY) {
    auto result = placeCornerResult(world, buildingId, vertexX, vertexY);
    return result.ok() ? result.value().instanceId : 0;
}

bool PlacementSystem::canPlaceFree(PlacementWorld *world, const std::string &buildingId, float worldX, float worldY,
                                   int excludeInstanceId, std::string *reason, int level, float rotationDeg) {
    if (!world) {
        if (reason) *reason = "no_world";
        return false;
    }
    const BuildingDefinition *def = BuildingRegistry::find(buildingId);
    if (!def) {
        if (reason) *reason = "unknown_building";
        return false;
    }
    if (def->placementKind != "free") {
        if (reason) *reason = "not_free_building";
        return false;
    }
    if (!std::isfinite(worldX) || !std::isfinite(worldY)) {
        if (reason) *reason = "free_invalid_anchor";
        return false;
    }
    int cellX = 0;
    int cellY = 0;
    grid::worldToCell(*world->grid_, worldX, worldY, cellX, cellY, world->width_, world->height_);
    if (cellX < 0 || cellX >= world->width_ || cellY < 0 || cellY >= world->height_) {
        if (reason) *reason = "free_out_of_bounds";
        return false;
    }
    const int           resolvedLevel = level == std::numeric_limits<int>::min() ? world->getActiveLevel() : level;
    const FreeFootprint candidate =
        definitionFreeFootprint(*def, *world, worldX, worldY, normalizeRotation(buildingId, rotationDeg));
    for (const auto &[id, placed] : world->buildings_) {
        if (id == excludeInstanceId || placed.placementKind != "edge" || placed.level != resolvedLevel) continue;
        const auto *edgeDef = BuildingRegistry::find(placed.buildingId);
        if (edgeDef && hasFreeEdgeOverlap(*world, *def, candidate, *edgeDef, placed.edge)) {
            if (reason) *reason = "free_edge_overlap";
            return false;
        }
    }
    for (const auto &[id, placed] : world->buildings_) {
        if (id == excludeInstanceId || placed.placementKind != "free" || placed.level != resolvedLevel ||
            placed.channel != def->channel)
            continue;
        if (hasFreeFootprintOverlap(placedFreeFootprint(placed), candidate)) {
            if (reason) *reason = "free_overlap";
            return false;
        }
    }
    PlacementQuery query;
    query.buildingId        = buildingId;
    query.cellX             = cellX;
    query.cellY             = cellY;
    query.worldX            = worldX;
    query.worldY            = worldY;
    query.level             = resolvedLevel;
    query.excludeInstanceId = excludeInstanceId;
    return checkStructuralSupport(*world, *def, query, reason);
}

bool PlacementSystem::containsFreePoint(const PlacedBuilding &placed, float worldX, float worldY) {
    return placed.placementKind == "free" && containsFreeFootprintPoint(placedFreeFootprint(placed), worldX, worldY);
}

eve::Result<PlacedBuilding> PlacementSystem::placeFreeResult(PlacementWorld *world, const std::string &buildingId,
                                                             float worldX, float worldY, float elevation,
                                                             float rotationDeg) {
    std::string reason;
    if (!canPlaceFree(world, buildingId, worldX, worldY, 0, &reason, std::numeric_limits<int>::min(), rotationDeg)) {
        return eve::Result<PlacedBuilding>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "free placement rejected without mutation", reason, {}, "building.free"));
    }
    const BuildingDefinition *def = BuildingRegistry::find(buildingId);
    PlacedBuilding            placed;
    placed.instanceId    = nextInstanceId(*world);
    placed.buildingId    = buildingId;
    placed.placementKind = "free";
    placed.level         = world->getActiveLevel();
    placed.worldX        = worldX;
    placed.worldY        = worldY;
    placed.elevation     = elevation;
    placed.rotationDeg   = normalizeRotation(buildingId, rotationDeg);
    applyDefinitionFreeFootprint(placed, *def, *world);
    grid::worldToCell(*world->grid_, worldX, worldY, placed.originCellX, placed.originCellY, world->width_,
                      world->height_);
    placed.channel                       = def->channel;
    placed.tags                          = def->tags;
    world->buildings_[placed.instanceId] = placed;
    world->instanceOrder_.push_back(placed.instanceId);

    BuildingChangeEvent event;
    event.action      = "place";
    event.worldId     = world->getId();
    event.buildingId  = buildingId;
    event.instanceId  = placed.instanceId;
    event.cellX       = placed.originCellX;
    event.cellY       = placed.originCellY;
    event.level       = placed.level;
    event.rotationDeg = placed.rotationDeg;
    event.worldX      = placed.worldX;
    event.worldY      = placed.worldY;
    event.elevation   = placed.elevation;
    event.channel     = placed.channel;
    if (world->publishEvents_) emit(std::move(event));
    return eve::Result<PlacedBuilding>::success(std::move(placed));
}

eve::Result<PlacedBuilding> PlacementSystem::placeFreeSurfaceResult(PlacementWorld     *world,
                                                                    const std::string  &buildingId,
                                                                    const SurfacePatch &patch, float rotationDeg) {
    if (!world) {
        return eve::Result<PlacedBuilding>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "free surface placement requires a world", {}, {}, "building.free"));
    }
    const BuildingDefinition *def = BuildingRegistry::find(buildingId);
    if (!def || def->placementKind != "free") {
        return eve::Result<PlacedBuilding>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "free surface placement requires a free definition", buildingId, {},
            "building.free"));
    }
    const float planeX    = patch.anchor.worldX;
    const float planeY    = world->getGrid().plane == grid::GridPlane::XZ ? patch.anchor.worldZ : patch.anchor.worldY;
    const float elevation = world->getGrid().plane == grid::GridPlane::XZ ? patch.anchor.worldY : patch.anchor.worldZ;
    std::string reason;
    if (!canPlaceFree(world, buildingId, planeX, planeY, 0, &reason, std::numeric_limits<int>::min(), rotationDeg) ||
        patch.maxSlopeDegrees > def->maxSurfaceSlopeDegrees ||
        (def->maxSurfaceHeightDelta >= 0.f && patch.heightDelta > def->maxSurfaceHeightDelta)) {
        if (reason.empty())
            reason = patch.maxSlopeDegrees > def->maxSurfaceSlopeDegrees ? "surface_slope" : "surface_height_delta";
        return eve::Result<PlacedBuilding>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "free surface placement rejected without mutation",
                                   reason, {}, "building.free"));
    }

    PlacedBuilding placed;
    placed.instanceId    = nextInstanceId(*world);
    placed.buildingId    = buildingId;
    placed.placementKind = "free";
    placed.level         = world->getActiveLevel();
    placed.worldX        = planeX;
    placed.worldY        = planeY;
    placed.elevation     = elevation;
    placed.rotationDeg   = normalizeRotation(buildingId, rotationDeg);
    applyDefinitionFreeFootprint(placed, *def, *world);
    grid::worldToCell(*world->grid_, planeX, planeY, placed.originCellX, placed.originCellY, world->width_,
                      world->height_);
    placed.channel                       = def->channel;
    placed.tags                          = def->tags;
    placed.surfaceId                     = patch.anchor.surfaceId;
    placed.surfaceRevision               = patch.anchor.surfaceRevision;
    placed.surfaceNormalX                = patch.anchor.normalX;
    placed.surfaceNormalY                = patch.anchor.normalY;
    placed.surfaceNormalZ                = patch.anchor.normalZ;
    placed.surfaceTangentX               = patch.anchor.tangentX;
    placed.surfaceTangentY               = patch.anchor.tangentY;
    placed.surfaceTangentZ               = patch.anchor.tangentZ;
    placed.surfaceSampleCount            = static_cast<int>(patch.samples.size());
    placed.surfaceMaxSlopeDegrees        = patch.maxSlopeDegrees;
    placed.surfaceHeightDelta            = patch.heightDelta;
    world->buildings_[placed.instanceId] = placed;
    world->instanceOrder_.push_back(placed.instanceId);

    BuildingChangeEvent event;
    event.action      = "place";
    event.worldId     = world->getId();
    event.buildingId  = buildingId;
    event.instanceId  = placed.instanceId;
    event.cellX       = placed.originCellX;
    event.cellY       = placed.originCellY;
    event.level       = placed.level;
    event.rotationDeg = placed.rotationDeg;
    event.worldX      = placed.worldX;
    event.worldY      = placed.worldY;
    event.elevation   = placed.elevation;
    event.channel     = placed.channel;
    if (world->publishEvents_) emit(std::move(event));
    return eve::Result<PlacedBuilding>::success(std::move(placed));
}

int PlacementSystem::placeFree(PlacementWorld *world, const std::string &buildingId, float worldX, float worldY,
                               float elevation, float rotationDeg) {
    auto result = placeFreeResult(world, buildingId, worldX, worldY, elevation, rotationDeg);
    return result.ok() ? std::move(result).takeValue().instanceId : 0;
}

uint8_t PlacementSystem::edgeConnectionMask(const PlacementWorld &world, int instanceId) {
    const auto placedIt = world.buildings_.find(instanceId);
    if (placedIt == world.buildings_.end() || placedIt->second.placementKind != "edge") return 0;
    const PlacedBuilding     &placed = placedIt->second;
    const BuildingDefinition *def    = BuildingRegistry::find(placed.buildingId);
    if (!def) return 0;
    const std::string group = def->connectionGroup.empty() ? def->id : def->connectionGroup;

    auto occupantAt = [&](const EdgeAddress &edge) {
        const auto *channels = world.findEdgeChannels(edge.axis, placed.level);
        const bool  inBounds = edge.axis == EdgeAxis::Horizontal
                                   ? edge.x >= 0 && edge.x < world.width_ && edge.y >= 0 && edge.y <= world.height_
                                   : edge.x >= 0 && edge.x <= world.width_ && edge.y >= 0 && edge.y < world.height_;
        if (!channels || !inBounds) return 0;
        const auto channelIt = channels->find(placed.channel);
        if (channelIt == channels->end()) return 0;
        const size_t index = edge.axis == EdgeAxis::Horizontal
                                 ? size_t(edge.y) * size_t(world.width_) + size_t(edge.x)
                                 : size_t(edge.y) * size_t(world.width_ + 1) + size_t(edge.x);
        return index < channelIt->second.size() ? channelIt->second[index] : 0;
    };
    auto connects = [&](const EdgeAddress &edge) {
        const int otherId = occupantAt(edge);
        if (otherId == 0 || otherId == instanceId) return false;
        const auto otherIt = world.buildings_.find(otherId);
        if (otherIt == world.buildings_.end()) return false;
        const BuildingDefinition *otherDef = BuildingRegistry::find(otherIt->second.buildingId);
        if (!otherDef) return false;
        const std::string otherGroup = otherDef->connectionGroup.empty() ? otherDef->id : otherDef->connectionGroup;
        return group == otherGroup;
    };

    const EdgeAddress e = placed.edge;
    EdgeAddress       candidates[6];
    if (e.axis == EdgeAxis::Horizontal) {
        candidates[0] = {e.x - 1, e.y, EdgeAxis::Horizontal};
        candidates[1] = {e.x + 1, e.y, EdgeAxis::Horizontal};
        candidates[2] = {e.x, e.y - 1, EdgeAxis::Vertical};
        candidates[3] = {e.x, e.y, EdgeAxis::Vertical};
        candidates[4] = {e.x + 1, e.y - 1, EdgeAxis::Vertical};
        candidates[5] = {e.x + 1, e.y, EdgeAxis::Vertical};
    } else {
        candidates[0] = {e.x, e.y - 1, EdgeAxis::Vertical};
        candidates[1] = {e.x, e.y + 1, EdgeAxis::Vertical};
        candidates[2] = {e.x - 1, e.y, EdgeAxis::Horizontal};
        candidates[3] = {e.x, e.y, EdgeAxis::Horizontal};
        candidates[4] = {e.x - 1, e.y + 1, EdgeAxis::Horizontal};
        candidates[5] = {e.x, e.y + 1, EdgeAxis::Horizontal};
    }
    uint8_t mask = 0;
    for (int i = 0; i < 6; ++i)
        if (connects(candidates[i])) mask |= uint8_t(1u << i);
    return mask;
}

std::string PlacementSystem::edgeVariant(const PlacementWorld &world, int instanceId) {
    const uint8_t mask  = edgeConnectionMask(world, instanceId);
    int           count = 0;
    for (int bit = 0; bit < 6; ++bit)
        if ((mask & uint8_t(1u << bit)) != 0) ++count;
    if (count == 0) return "isolated";
    if (count == 1) return "end";
    if (count == 2) return (mask & 0x03u) == 0x03u ? "straight" : "corner";
    if (count == 3) return "tee";
    return "cross";
}


}  // namespace eve::building
