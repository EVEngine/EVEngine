#include "building/PlacementSession.h"
#include "building/Ghost.h"
#include "building/PlacementWorld.h"

namespace eve::building {

PlacementSession::PlacementSession() { ghost_ = new Ghost(); }

void PlacementSession::destroy() {
    if (ghost_) ghost_->destroy();
    ghost_ = nullptr;
    delete this;
}

bool PlacementSession::startPlacement(PlacementWorld *world, const std::string &buildingId) {
    if (!world || buildingId.empty()) return false;
    world_ = world;
    ghost_->setBuildingId(buildingId);
    active_ = true;
    mode_ = "place";
    return true;
}

void PlacementSession::stopPlacement() {
    active_ = false;
    world_ = nullptr;
}

std::string PlacementSession::getBuildingId() const {
    return ghost_ ? ghost_->getBuildingId() : std::string{};
}

void PlacementSession::setMode(const std::string &mode) {
    if (mode == "remove") {
        mode_ = "remove";
    } else {
        mode_ = "place";
    }
}

void PlacementSession::setRotationDeg(float deg) {
    if (ghost_) ghost_->setRotationDeg(deg);
}

void PlacementSession::rotateBy(float deltaDeg) {
    if (ghost_) ghost_->rotateBy(deltaDeg);
}

float PlacementSession::getRotationDeg() const {
    return ghost_ ? ghost_->getRotationDeg() : 0.f;
}

bool PlacementSession::updateFromWorld(PlacementWorld *world, float worldX, float worldY) {
    if (!active_ || !ghost_) return false;
    if (world) world_ = world;
    if (!world_) return false;
    ghost_->setFromWorld(world_, worldX, worldY);
    refreshValidate();
    return true;
}

bool PlacementSession::updateFromWorld3D(PlacementWorld *world, float worldX, float worldY,
                                         float worldZ) {
    if (!active_ || !ghost_) return false;
    if (world) world_ = world;
    if (!world_) return false;
    ghost_->setFromWorld3D(world_, worldX, worldY, worldZ);
    refreshValidate();
    return true;
}

bool PlacementSession::updateFromSurface(PlacementWorld *world, const std::string &surface,
                                         float x, float y) {
    if (!active_ || !ghost_) return false;
    if (world) world_ = world;
    if (!world_) return false;
    ghost_->setFromSurface(world_, surface, x, y);
    refreshValidate();
    return true;
}

bool PlacementSession::isValid() const { return ghost_ ? ghost_->isValid() : false; }

std::string PlacementSession::getReason() const {
    return ghost_ ? ghost_->getReason() : std::string{};
}

void PlacementSession::refreshValidate() {
    if (world_ && ghost_) ghost_->validate(world_);
}

int PlacementSession::execute() {
    if (!active_ || !world_ || !ghost_) return 0;
    if (mode_ == "remove") {
        const int occ = world_->getOccupant(ghost_->getCellX(), ghost_->getCellY());
        if (occ <= 0) return 0;
        return world_->removeBuilding(occ) ? occ : 0;
    }
    return world_->placeGhost(ghost_);
}

}  // namespace eve::building
