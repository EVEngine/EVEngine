#include "building/Ghost.h"
#include "building/BuildingDef.h"
#include "building/PlacementSystem.h"
#include "building/PlacementWorld.h"
#include "grid/GridProjection.h"

#include <limits>

namespace eve::building {

void Ghost::destroy() { delete this; }

void Ghost::setBuildingId(const std::string &id) {
    buildingId_ = id;
    placementKind_ = "cell";
    valid_ = false;
    reason_.clear();
    surfaceId_.clear();
    surfaceRevision_ = 0;
    surfaceNormalX_ = 0.f;
    surfaceNormalY_ = 1.f;
    surfaceNormalZ_ = 0.f;
    surfaceTangentX_ = 1.f;
    surfaceTangentY_ = 0.f;
    surfaceTangentZ_ = 0.f;
    surfaceSampleCount_ = 0;
    surfaceMaxSlopeDegrees_ = 0.f;
    surfaceHeightDelta_ = 0.f;
}

void Ghost::setCell(int cellX, int cellY) {
    placementKind_ = "cell";
    cellX_ = cellX;
    cellY_ = cellY;
    valid_ = false;
    reason_.clear();
    surfaceId_.clear();
    surfaceRevision_ = 0;
    surfaceNormalX_ = 0.f;
    surfaceNormalY_ = 1.f;
    surfaceNormalZ_ = 0.f;
    surfaceTangentX_ = 1.f;
    surfaceTangentY_ = 0.f;
    surfaceTangentZ_ = 0.f;
    surfaceSampleCount_ = 0;
    surfaceMaxSlopeDegrees_ = 0.f;
    surfaceHeightDelta_ = 0.f;
}

void Ghost::setWorld(float worldX, float worldY) {
    placementKind_ = "cell";
    worldX_ = worldX;
    worldY_ = worldY;
    valid_ = false;
    reason_.clear();
    surfaceId_.clear();
    surfaceRevision_ = 0;
    surfaceNormalX_ = 0.f;
    surfaceNormalY_ = 1.f;
    surfaceNormalZ_ = 0.f;
    surfaceTangentX_ = 1.f;
    surfaceTangentY_ = 0.f;
    surfaceTangentZ_ = 0.f;
    surfaceSampleCount_ = 0;
    surfaceMaxSlopeDegrees_ = 0.f;
    surfaceHeightDelta_ = 0.f;
}

void Ghost::setElevation(float elevation) {
    elevation_ = elevation;
    valid_ = false;
    reason_.clear();
    surfaceId_.clear();
    surfaceRevision_ = 0;
    surfaceNormalX_ = 0.f;
    surfaceNormalY_ = 1.f;
    surfaceNormalZ_ = 0.f;
    surfaceTangentX_ = 1.f;
    surfaceTangentY_ = 0.f;
    surfaceTangentZ_ = 0.f;
    surfaceSampleCount_ = 0;
    surfaceMaxSlopeDegrees_ = 0.f;
    surfaceHeightDelta_ = 0.f;
}

void Ghost::setRotationDeg(float deg) {
    rotationDeg_ = deg;
    if (!surfaceId_.empty()) surfacePatchStale_ = true;
    valid_ = false;
    reason_.clear();
}

void Ghost::rotateBy(float deltaDeg) {
    setRotationDeg(rotationDeg_ + deltaDeg);
}

void Ghost::setFromWorld(PlacementWorld *world, float worldX, float worldY) {
    if (!world) return;
    placementKind_ = "cell";
    const SnapResult s = PlacementSystem::snap(*world, buildingId_, worldX, worldY);
    cellX_ = s.cellX;
    cellY_ = s.cellY;
    worldX_ = s.worldX;
    worldY_ = s.worldY;
    elevation_ = s.elevation;
    valid_ = false;
    reason_.clear();
    surfaceId_.clear();
    surfaceRevision_ = 0;
    surfaceNormalX_ = 0.f;
    surfaceNormalY_ = 1.f;
    surfaceNormalZ_ = 0.f;
    surfaceTangentX_ = 1.f;
    surfaceTangentY_ = 0.f;
    surfaceTangentZ_ = 0.f;
    surfaceSampleCount_ = 0;
    surfaceMaxSlopeDegrees_ = 0.f;
    surfaceHeightDelta_ = 0.f;
}

void Ghost::setFromWorld3D(PlacementWorld *world, float worldX, float worldY, float worldZ) {
    if (!world) return;
    placementKind_ = "cell";
    const SnapResult s = PlacementSystem::snap3D(*world, buildingId_, worldX, worldY, worldZ);
    cellX_ = s.cellX;
    cellY_ = s.cellY;
    worldX_ = s.worldX;
    worldY_ = s.worldY;
    elevation_ = s.elevation;
    valid_ = false;
    reason_.clear();
    surfaceId_.clear();
    surfaceRevision_ = 0;
    surfaceNormalX_ = 0.f;
    surfaceNormalY_ = 1.f;
    surfaceNormalZ_ = 0.f;
    surfaceTangentX_ = 1.f;
    surfaceTangentY_ = 0.f;
    surfaceTangentZ_ = 0.f;
    surfaceSampleCount_ = 0;
    surfaceMaxSlopeDegrees_ = 0.f;
    surfaceHeightDelta_ = 0.f;
}

std::string Ghost::getEdgeAxis() const {
    if (placementKind_ != "edge") return {};
    return edge_.axis == EdgeAxis::Horizontal ? "horizontal" : "vertical";
}

void Ghost::setEdge(PlacementWorld *world, int cellX, int cellY, const std::string &direction) {
    if (!world) return;
    auto result = PlacementSystem::canonicalEdge(cellX, cellY, direction);
    if (!result.ok()) {
        placementKind_ = "edge";
        valid_ = false;
        reason_ = "invalid_edge_direction";
        return;
    }
    placementKind_ = "edge";
    edge_ = std::move(result).takeValue();
    cellX_ = edge_.x;
    cellY_ = edge_.y;
    float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
    world->cellToWorldPlane(edge_.x, edge_.y, ax, ay);
    world->cellToWorldPlane(edge_.x + (edge_.axis == EdgeAxis::Horizontal ? 1 : 0),
                            edge_.y + (edge_.axis == EdgeAxis::Vertical ? 1 : 0), bx, by);
    worldX_ = (ax + bx) * 0.5f;
    worldY_ = (ay + by) * 0.5f;
    rotationDeg_ = edge_.axis == EdgeAxis::Horizontal ? 0.f : 90.f;
    surfaceId_.clear();
    surfaceRevision_ = 0;
    valid_ = false;
    reason_.clear();
}

void Ghost::setCorner(PlacementWorld *world, int vertexX, int vertexY) {
    if (!world) return;
    placementKind_ = "corner";
    corner_ = CornerAddress{vertexX, vertexY};
    cellX_ = vertexX;
    cellY_ = vertexY;
    world->cellToWorldPlane(vertexX, vertexY, worldX_, worldY_);
    rotationDeg_ = PlacementSystem::normalizeRotation(buildingId_, rotationDeg_);
    surfaceId_.clear();
    surfaceRevision_ = 0;
    surfaceSampleCount_ = 0;
    valid_ = false;
    reason_.clear();
}

void Ghost::setFree(PlacementWorld *world, float worldX, float worldY, float elevation) {
    if (!world) return;
    placementKind_ = "free";
    worldX_ = worldX;
    worldY_ = worldY;
    elevation_ = elevation;
    grid::worldToCell(world->getGrid(), worldX, worldY, cellX_, cellY_, world->getWidth(),
                      world->getHeight());
    rotationDeg_ = PlacementSystem::normalizeRotation(buildingId_, rotationDeg_);
    surfaceId_.clear();
    surfaceRevision_ = 0;
    surfaceSampleCount_ = 0;
    valid_ = false;
    reason_.clear();
}

void Ghost::setFromSurface(PlacementWorld *world, const std::string &surface, float x, float y) {
    if (!world) return;
    surfaceProviderName_ = surface;
    surfaceInputX_ = x;
    surfaceInputY_ = y;
    auto result = PlacementSystem::sampleSurfacePatch(*world, buildingId_, surface, x, y,
                                                      rotationDeg_);
    if (!result.ok()) {
        surfaceId_.clear();
        surfaceRevision_ = 0;
        surfaceNormalX_ = 0.f;
        surfaceNormalY_ = 1.f;
        surfaceNormalZ_ = 0.f;
        surfaceTangentX_ = 1.f;
        surfaceTangentY_ = 0.f;
        surfaceTangentZ_ = 0.f;
        surfaceSampleCount_ = 0;
        surfaceMaxSlopeDegrees_ = 0.f;
        surfaceHeightDelta_ = 0.f;
        surfacePatchStale_ = false;
        valid_ = false;
        reason_ = result.code() == eve::StatusCode::NotFound ? "no_surface_hit"
                                                             : "invalid_surface_hit";
        return;
    }
    PlacementSystem::SurfacePatch patch = std::move(result).takeValue();
    PlacementSystem::PlacementHit &hit = patch.anchor;
    setFromWorld3D(world, hit.worldX, hit.worldY, hit.worldZ);
    const BuildingDefinition *definition = BuildingRegistry::find(buildingId_);
    if (definition && definition->placementKind == "free") placementKind_ = "free";
    surfaceId_ = std::move(hit.surfaceId);
    surfaceRevision_ = hit.surfaceRevision;
    surfaceNormalX_ = hit.normalX;
    surfaceNormalY_ = hit.normalY;
    surfaceNormalZ_ = hit.normalZ;
    surfaceTangentX_ = hit.tangentX;
    surfaceTangentY_ = hit.tangentY;
    surfaceTangentZ_ = hit.tangentZ;
    surfaceSampleCount_ = static_cast<int>(patch.samples.size());
    surfaceMaxSlopeDegrees_ = patch.maxSlopeDegrees;
    surfaceHeightDelta_ = patch.heightDelta;
    surfacePatchStale_ = false;
}

bool Ghost::validate(PlacementWorld *world) {
    if (!world) {
        valid_ = false;
        reason_ = "no_world";
        return false;
    }
    const BuildingDefinition *definition = BuildingRegistry::find(buildingId_);
    if (definition && definition->placementKind == "edge") {
        if (placementKind_ != "edge") {
            valid_ = false;
            reason_ = "edge_requires_edge_address";
            return false;
        }
        const char *direction = edge_.axis == EdgeAxis::Horizontal ? "north" : "west";
        valid_ = PlacementSystem::canPlaceEdge(world, buildingId_, edge_.x, edge_.y, direction, 0,
                                               &reason_);
        return valid_;
    }
    if (definition && definition->placementKind == "corner") {
        if (placementKind_ != "corner") {
            valid_ = false;
            reason_ = "corner_requires_corner_address";
            return false;
        }
        valid_ = PlacementSystem::canPlaceCorner(world, buildingId_, corner_.x, corner_.y, 0,
                                                 &reason_);
        return valid_;
    }
    if (definition && definition->placementKind == "free") {
        if (placementKind_ != "free") {
            valid_ = false;
            reason_ = "free_requires_free_address";
            return false;
        }
        valid_ = PlacementSystem::canPlaceFree(
            world, buildingId_, worldX_, worldY_, 0, &reason_,
            std::numeric_limits<int>::min(), rotationDeg_);
        if (valid_ && surfaceMaxSlopeDegrees_ > definition->maxSurfaceSlopeDegrees) {
            valid_ = false;
            reason_ = "surface_slope";
        }
        if (valid_ && definition->maxSurfaceHeightDelta >= 0.f &&
            surfaceHeightDelta_ > definition->maxSurfaceHeightDelta) {
            valid_ = false;
            reason_ = "surface_height_delta";
        }
        return valid_;
    }
    if (surfacePatchStale_) {
        setFromSurface(world, surfaceProviderName_, surfaceInputX_, surfaceInputY_);
        if (surfaceId_.empty()) return false;
    }
    rotationDeg_ = PlacementSystem::normalizeRotation(buildingId_, rotationDeg_);
    PlacementQuery query;
    query.buildingId = buildingId_;
    query.cellX = cellX_;
    query.cellY = cellY_;
    query.worldX = worldX_;
    query.worldY = worldY_;
    query.elevation = elevation_;
    query.rotationDeg = rotationDeg_;
    query.surfaceId = surfaceId_;
    query.surfaceRevision = surfaceRevision_;
    query.surfaceNormalX = surfaceNormalX_;
    query.surfaceNormalY = surfaceNormalY_;
    query.surfaceNormalZ = surfaceNormalZ_;
    query.surfaceSampleCount = surfaceSampleCount_;
    query.surfaceMaxSlopeDegrees = surfaceMaxSlopeDegrees_;
    query.surfaceHeightDelta = surfaceHeightDelta_;
    std::string reason;
    valid_ = PlacementSystem::canPlaceQuery(world, query, &reason);
    reason_ = reason;
    return valid_;
}

}  // namespace eve::building
