#include "building/Ghost.h"
#include "building/PlacementSystem.h"
#include "building/PlacementWorld.h"

namespace eve::building {

void Ghost::destroy() { delete this; }

void Ghost::setBuildingId(const std::string &id) {
    buildingId_ = id;
    valid_ = false;
    reason_.clear();
}

void Ghost::setCell(int cellX, int cellY) {
    cellX_ = cellX;
    cellY_ = cellY;
    valid_ = false;
    reason_.clear();
}

void Ghost::setWorld(float worldX, float worldY) {
    worldX_ = worldX;
    worldY_ = worldY;
    valid_ = false;
    reason_.clear();
}

void Ghost::setElevation(float elevation) {
    elevation_ = elevation;
    valid_ = false;
    reason_.clear();
}

void Ghost::setRotationDeg(float deg) {
    rotationDeg_ = deg;
    valid_ = false;
    reason_.clear();
}

void Ghost::rotateBy(float deltaDeg) {
    setRotationDeg(rotationDeg_ + deltaDeg);
}

void Ghost::setFromWorld(PlacementWorld *world, float worldX, float worldY) {
    if (!world) return;
    const SnapResult s = PlacementSystem::snap(*world, buildingId_, worldX, worldY);
    cellX_ = s.cellX;
    cellY_ = s.cellY;
    worldX_ = s.worldX;
    worldY_ = s.worldY;
    elevation_ = s.elevation;
    valid_ = false;
    reason_.clear();
}

void Ghost::setFromWorld3D(PlacementWorld *world, float worldX, float worldY, float worldZ) {
    if (!world) return;
    const SnapResult s = PlacementSystem::snap3D(*world, buildingId_, worldX, worldY, worldZ);
    cellX_ = s.cellX;
    cellY_ = s.cellY;
    worldX_ = s.worldX;
    worldY_ = s.worldY;
    elevation_ = s.elevation;
    valid_ = false;
    reason_.clear();
}

void Ghost::setFromSurface(PlacementWorld *world, const std::string &surface, float x, float y) {
    if (!world) return;
    PlacementSystem::PlacementHit hit;
    if (!PlacementSystem::surfaceHit(*world, surface, x, y, &hit)) {
        valid_ = false;
        reason_ = "no_surface_hit";
        return;
    }
    setFromWorld3D(world, hit.worldX, hit.worldY, hit.worldZ);
}

bool Ghost::validate(PlacementWorld *world) {
    if (!world) {
        valid_ = false;
        reason_ = "no_world";
        return false;
    }
    rotationDeg_ = PlacementSystem::normalizeRotation(buildingId_, rotationDeg_);
    std::string reason;
    valid_ = PlacementSystem::canPlaceElev(world, buildingId_, cellX_, cellY_, elevation_,
                                           rotationDeg_, 0, &reason);
    reason_ = reason;
    return valid_;
}

}  // namespace eve::building
