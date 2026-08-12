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
    valid_ = false;
    reason_.clear();
}

bool Ghost::validate(PlacementWorld *world) {
    if (!world) {
        valid_ = false;
        reason_ = "no_world";
        return false;
    }
    rotationDeg_ = PlacementSystem::normalizeRotation(buildingId_, rotationDeg_);
    std::string reason;
    valid_ = PlacementSystem::canPlace(world, buildingId_, cellX_, cellY_, rotationDeg_, 0, &reason);
    reason_ = reason;
    return valid_;
}

}  // namespace eve::building
