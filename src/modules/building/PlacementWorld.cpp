#include "building/PlacementWorld.h"
#include "building/Ghost.h"
#include "building/PlacementSystem.h"

#include <algorithm>
#include <cmath>

namespace eve::building {

PlacementWorld::PlacementWorld(int width, int height, float cellSize) {
    width_ = width > 0 ? width : 1;
    height_ = height > 0 ? height : 1;
    cellSize_ = cellSize > 0.f ? cellSize : 1.f;
    occupancy_.assign(size_t(width_) * size_t(height_), 0);
    terrain_.assign(size_t(width_) * size_t(height_), 0);
    PlacementSystem::ensureBuiltins();
}

void PlacementWorld::destroy() { delete this; }

void PlacementWorld::setCellSize(float s) {
    if (s > 0.f) cellSize_ = s;
}

void PlacementWorld::setOrigin(float x, float y) {
    originX_ = x;
    originY_ = y;
}

void PlacementWorld::setExtra(const std::string &key, const std::string &value) {
    extra_[key] = value;
}

std::string PlacementWorld::getExtra(const std::string &key, const std::string &fallback) const {
    auto it = extra_.find(key);
    return it == extra_.end() ? fallback : it->second;
}

int PlacementWorld::worldToCellX(float worldX) const {
    const float cs = cellSize_ > 0.f ? cellSize_ : 1.f;
    return int(std::floor((worldX - originX_) / cs));
}

int PlacementWorld::worldToCellY(float worldY) const {
    const float cs = cellSize_ > 0.f ? cellSize_ : 1.f;
    return int(std::floor((worldY - originY_) / cs));
}

float PlacementWorld::cellToWorldX(int cellX) const {
    return originX_ + float(cellX) * cellSize_;
}

float PlacementWorld::cellToWorldY(int cellY) const {
    return originY_ + float(cellY) * cellSize_;
}

void PlacementWorld::fillTerrain(int semantic) {
    std::fill(terrain_.begin(), terrain_.end(), semantic);
}

void PlacementWorld::setTerrain(int cellX, int cellY, int semantic) {
    if (!inBounds(cellX, cellY)) return;
    terrain_[size_t(cellY) * size_t(width_) + size_t(cellX)] = semantic;
}

int PlacementWorld::getTerrain(int cellX, int cellY) const {
    if (!inBounds(cellX, cellY)) return 0;
    return terrain_[size_t(cellY) * size_t(width_) + size_t(cellX)];
}

bool PlacementWorld::inBounds(int cellX, int cellY) const {
    return cellX >= 0 && cellY >= 0 && cellX < width_ && cellY < height_;
}

int PlacementWorld::getOccupant(int cellX, int cellY) const {
    if (!inBounds(cellX, cellY)) return 0;
    return occupancy_[size_t(cellY) * size_t(width_) + size_t(cellX)];
}

bool PlacementWorld::isCellEmpty(int cellX, int cellY) const {
    return getOccupant(cellX, cellY) == 0;
}

int PlacementWorld::getBuildingCount() const { return int(instanceOrder_.size()); }

bool PlacementWorld::hasBuilding(int instanceId) const {
    return buildings_.count(instanceId) > 0;
}

std::string PlacementWorld::getBuildingId(int instanceId) const {
    auto it = buildings_.find(instanceId);
    return it == buildings_.end() ? std::string{} : it->second.buildingId;
}

int PlacementWorld::getBuildingCellX(int instanceId) const {
    auto it = buildings_.find(instanceId);
    return it == buildings_.end() ? 0 : it->second.originCellX;
}

int PlacementWorld::getBuildingCellY(int instanceId) const {
    auto it = buildings_.find(instanceId);
    return it == buildings_.end() ? 0 : it->second.originCellY;
}

float PlacementWorld::getBuildingWorldX(int instanceId) const {
    auto it = buildings_.find(instanceId);
    return it == buildings_.end() ? 0.f : it->second.worldX;
}

float PlacementWorld::getBuildingWorldY(int instanceId) const {
    auto it = buildings_.find(instanceId);
    return it == buildings_.end() ? 0.f : it->second.worldY;
}

float PlacementWorld::getBuildingRotation(int instanceId) const {
    auto it = buildings_.find(instanceId);
    return it == buildings_.end() ? 0.f : it->second.rotationDeg;
}

std::string PlacementWorld::getBuildingProp(int instanceId, const std::string &key,
                                            const std::string &fallback) const {
    auto it = buildings_.find(instanceId);
    return it == buildings_.end() ? fallback : it->second.getProp(key, fallback);
}

void PlacementWorld::setBuildingProp(int instanceId, const std::string &key,
                                     const std::string &value) {
    auto it = buildings_.find(instanceId);
    if (it == buildings_.end()) return;
    it->second.setProp(key, value);
}

bool PlacementWorld::buildingHasTag(int instanceId, const std::string &tag) const {
    auto it = buildings_.find(instanceId);
    return it == buildings_.end() ? false : it->second.hasTag(tag);
}

int PlacementWorld::getBuildingInstanceAt(int index) const {
    if (index < 0 || index >= int(instanceOrder_.size())) return 0;
    return instanceOrder_[size_t(index)];
}

bool PlacementWorld::canPlace(const std::string &buildingId, int cellX, int cellY,
                              float rotationDeg) {
    return PlacementSystem::canPlace(this, buildingId, cellX, cellY, rotationDeg, 0, nullptr);
}

std::string PlacementWorld::canPlaceReason(const std::string &buildingId, int cellX, int cellY,
                                           float rotationDeg) {
    std::string reason;
    PlacementSystem::canPlace(this, buildingId, cellX, cellY, rotationDeg, 0, &reason);
    return reason;
}

int PlacementWorld::placeAt(const std::string &buildingId, int cellX, int cellY,
                            float rotationDeg) {
    return PlacementSystem::placeAt(this, buildingId, cellX, cellY, rotationDeg);
}

int PlacementWorld::placeAtWorld(const std::string &buildingId, float worldX, float worldY,
                                 float rotationDeg) {
    return PlacementSystem::placeAtWorld(this, buildingId, worldX, worldY, rotationDeg);
}

int PlacementWorld::placeGhost(Ghost *ghost) {
    return PlacementSystem::placeGhost(this, ghost);
}

bool PlacementWorld::removeBuilding(int instanceId) {
    return PlacementSystem::removeBuilding(this, instanceId);
}

bool PlacementWorld::moveBuilding(int instanceId, int cellX, int cellY, float rotationDeg) {
    return PlacementSystem::moveBuilding(this, instanceId, cellX, cellY, rotationDeg);
}

void PlacementWorld::clearBuildings() { PlacementSystem::clearBuildings(this); }

}  // namespace eve::building
