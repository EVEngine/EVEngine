#include "building/PlacementWorld.h"
#include "building/Ghost.h"
#include "building/PlacementSystem.h"
#include "data/DataModule.h"
#include "data/JsonDocument.h"
#include "grid/GridProjection.h"
#include "map/TileLayer.h"

#include <Poco/JSON/Object.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace eve::building {

PlacementWorld::PlacementWorld(int width, int height, float cellSize) {
    width_ = width > 0 ? width : 1;
    height_ = height > 0 ? height : 1;
    grid_.cellW = cellSize > 0.f ? cellSize : 1.f;
    grid_.cellH = grid_.cellW;
    occupancy_.assign(size_t(width_) * size_t(height_), 0);
    terrain_.assign(size_t(width_) * size_t(height_), 0);
    terrainOverrides_.assign(size_t(width_) * size_t(height_), 0);
    PlacementSystem::ensureBuiltins();
}

void PlacementWorld::destroy() { delete this; }

void PlacementWorld::setCellSize(float s) {
    if (s > 0.f) {
        grid_.cellW = s;
        grid_.cellH = s;
    }
}

void PlacementWorld::setOrigin(float x, float y) {
    grid_.originX = x;
    grid_.originY = y;
}

void PlacementWorld::setExtra(const std::string &key, const std::string &value) {
    extra_[key] = value;
}

std::string PlacementWorld::getExtra(const std::string &key, const std::string &fallback) const {
    auto it = extra_.find(key);
    return it == extra_.end() ? fallback : it->second;
}

// ---- Grid 配置 ----

void PlacementWorld::setGridLayout(const std::string &layout) {
    grid_.layout = grid::GridConfig::layoutFromName(layout);
}

std::string PlacementWorld::getGridLayoutName() const {
    return grid::GridConfig::layoutName(grid_.layout);
}

void PlacementWorld::setGridPlane(const std::string &plane) {
    grid_.plane = grid::GridConfig::planeFromName(plane);
}

std::string PlacementWorld::getGridPlaneName() const {
    return grid::GridConfig::planeName(grid_.plane);
}

void PlacementWorld::setCellGap(float gapX, float gapY) {
    grid_.cellGapX = gapX;
    grid_.cellGapY = gapY;
}

void PlacementWorld::setHexSideLength(float s) { grid_.hexSideLength = s; }

void PlacementWorld::setStagger(const std::string &axis, const std::string &index) {
    grid_.staggerAxis = (axis == "x" || axis == "X") ? grid::StaggerAxis::X
                                                     : grid::StaggerAxis::Y;
    grid_.staggerIndex = (index == "even" || index == "Even") ? grid::StaggerIndex::Even
                                                              : grid::StaggerIndex::Odd;
}

void PlacementWorld::setGridFromLayer(map::TileLayer *layer) {
    if (!layer) return;
    const auto &c = *layer->config();
    switch (c.orientation) {
        case map::MapOrientation::Orthogonal:
            grid_.layout = grid::GridLayout::Rectangle;
            break;
        case map::MapOrientation::Isometric:
            grid_.layout = grid::GridLayout::Isometric;
            break;
        case map::MapOrientation::Staggered:
            grid_.layout = grid::GridLayout::Staggered;
            break;
        case map::MapOrientation::Hexagonal:
            grid_.layout = grid::GridLayout::Hexagon;
            break;
    }
    grid_.cellW = c.tileW;
    grid_.cellH = c.tileH;
    grid_.originX = c.originX;
    grid_.originY = c.originY;
    grid_.staggerAxis = c.staggerAxis == map::StaggerAxis::Y ? grid::StaggerAxis::Y
                                                             : grid::StaggerAxis::X;
    grid_.staggerIndex = c.staggerIndex == map::StaggerIndex::Odd ? grid::StaggerIndex::Odd
                                                                  : grid::StaggerIndex::Even;
    grid_.hexSideLength = c.hexSideLength;
}

// ---- 坐标换算 ----

int PlacementWorld::worldToCellX(float worldX) const {
    int cx = 0, cy = 0;
    grid::worldToCell(grid_, worldX, 0.f, cx, cy, width_, height_);
    return cx;
}

int PlacementWorld::worldToCellY(float worldY) const {
    int cx = 0, cy = 0;
    grid::worldToCell(grid_, 0.f, worldY, cx, cy, width_, height_);
    return cy;
}

float PlacementWorld::cellToWorldX(int cellX) const {
    float px = 0.f, py = 0.f;
    grid::cellToWorld(grid_, cellX, 0, px, py);
    return px;
}

float PlacementWorld::cellToWorldY(int cellY) const {
    float px = 0.f, py = 0.f;
    grid::cellToWorld(grid_, 0, cellY, px, py);
    return py;
}

void PlacementWorld::cellToWorldPlane(int cellX, int cellY, float &px, float &py) const {
    grid::cellToWorld(grid_, cellX, cellY, px, py);
}

void PlacementWorld::cellToWorld3D(int cellX, int cellY, float elevation, float &worldX,
                                   float &worldY, float &worldZ) const {
    float px = 0.f, py = 0.f;
    grid::cellToWorld(grid_, cellX, cellY, px, py);
    if (grid_.plane == grid::GridPlane::XZ) {
        worldX = px;
        worldY = elevation;
        worldZ = py;
    } else {
        worldX = px;
        worldY = py;
        worldZ = elevation;
    }
}

float PlacementWorld::cellToWorld3DX(int cellX, int cellY, float elevation) const {
    float wx = 0.f, wy = 0.f, wz = 0.f;
    cellToWorld3D(cellX, cellY, elevation, wx, wy, wz);
    return wx;
}

float PlacementWorld::cellToWorld3DY(int cellX, int cellY, float elevation) const {
    float wx = 0.f, wy = 0.f, wz = 0.f;
    cellToWorld3D(cellX, cellY, elevation, wx, wy, wz);
    return wy;
}

float PlacementWorld::cellToWorld3DZ(int cellX, int cellY, float elevation) const {
    float wx = 0.f, wy = 0.f, wz = 0.f;
    cellToWorld3D(cellX, cellY, elevation, wx, wy, wz);
    return wz;
}

void PlacementWorld::worldToCell3D(float worldX, float worldY, float worldZ, int &cellX,
                                   int &cellY) const {
    const float px = worldX;
    const float py = (grid_.plane == grid::GridPlane::XZ) ? worldZ : worldY;
    grid::worldToCell(grid_, px, py, cellX, cellY, width_, height_);
}

int PlacementWorld::worldToCell3DX(float worldX, float worldY, float worldZ) const {
    int cx = 0, cy = 0;
    worldToCell3D(worldX, worldY, worldZ, cx, cy);
    return cx;
}

int PlacementWorld::worldToCell3DY(float worldX, float worldY, float worldZ) const {
    int cx = 0, cy = 0;
    worldToCell3D(worldX, worldY, worldZ, cx, cy);
    return cy;
}

// ---- 地形 ----

void PlacementWorld::fillTerrain(int semantic) {
    std::fill(terrain_.begin(), terrain_.end(), semantic);
    std::fill(terrainOverrides_.begin(), terrainOverrides_.end(), 1);
}

void PlacementWorld::setTerrain(int cellX, int cellY, int semantic) {
    if (!inBounds(cellX, cellY)) return;
    const size_t idx = size_t(cellY) * size_t(width_) + size_t(cellX);
    terrain_[idx] = semantic;
    terrainOverrides_[idx] = 1;
}

int PlacementWorld::getTerrain(int cellX, int cellY) const {
    if (!inBounds(cellX, cellY)) return 0;
    const size_t idx = size_t(cellY) * size_t(width_) + size_t(cellX);
    if (terrainOverrides_[idx]) return terrain_[idx];
    if (terrainBound_) return terrainFromGid(cellX, cellY);
    return terrain_[idx];
}

int PlacementWorld::terrainFromGid(int cellX, int cellY) const {
    if (!tileLayer_) return 0;
    const int gid = tileLayer_->getTile(cellX, cellY);
    auto it = terrainGidMap_.find(gid);
    return it == terrainGidMap_.end() ? 0 : it->second;
}

bool PlacementWorld::inBounds(int cellX, int cellY) const {
    return cellX >= 0 && cellY >= 0 && cellX < width_ && cellY < height_;
}

// ---- Tilemap 绑定 ----

void PlacementWorld::bindTileLayer(map::TileLayer *layer) {
    tileLayer_ = layer;
    terrainBound_ = (layer != nullptr);
    setGridFromLayer(layer);
    // 绑定后地形由 GID 懒解析；清掉旧手动地形，避免旧值泄漏。
    std::fill(terrain_.begin(), terrain_.end(), 0);
    std::fill(terrainOverrides_.begin(), terrainOverrides_.end(), 0);
}

void PlacementWorld::clearTileLayer() {
    tileLayer_ = nullptr;
    terrainBound_ = false;
    std::fill(terrain_.begin(), terrain_.end(), 0);
    std::fill(terrainOverrides_.begin(), terrainOverrides_.end(), 0);
}

void PlacementWorld::setTerrainGidMapJson(const std::string &json) {
    auto *dm = eve::data::DataModule::create();
    std::string err;
    std::unique_ptr<data::JsonDocument> doc(dm->decodeJson(json, &err));
    if (!doc || !doc->isObject()) return;
    auto obj = doc->object();
    if (!obj) return;
    std::vector<std::string> names;
    obj->getNames(names);
    for (const auto &n : names) {
        try {
            const int gid = std::atoi(n.c_str());
            const int semantic = obj->getValue<int>(n);
            if (gid >= 0) terrainGidMap_[gid] = semantic;
        } catch (...) {
        }
    }
}

void PlacementWorld::setTerrainGid(int gid, int semantic) {
    if (gid >= 0) terrainGidMap_[gid] = semantic;
}

void PlacementWorld::clearTerrainGidMap() { terrainGidMap_.clear(); }

// ---- 占用查询 ----

std::vector<int> &PlacementWorld::channelOccupancy(const std::string &channel) {
    if (channel.empty()) return occupancy_;
    auto &ch = allChannels_[channel];
    if (ch.size() != occupancy_.size()) ch.assign(occupancy_.size(), 0);
    return ch;
}

int PlacementWorld::getOccupant(int cellX, int cellY) const {
    return getOccupantInChannel(std::string{}, cellX, cellY);
}

int PlacementWorld::getOccupantInChannel(const std::string &channel, int cellX, int cellY) const {
    if (!inBounds(cellX, cellY)) return 0;
    const size_t idx = size_t(cellY) * size_t(width_) + size_t(cellX);
    if (channel.empty()) return occupancy_[idx];
    auto it = allChannels_.find(channel);
    if (it == allChannels_.end() || idx >= it->second.size()) return 0;
    return it->second[idx];
}

int PlacementWorld::getAnyOccupant(int cellX, int cellY) const {
    const int occ = getOccupantInChannel(std::string{}, cellX, cellY);
    if (occ != 0) return occ;
    for (const auto &kv : allChannels_) {
        const int v = getOccupantInChannel(kv.first, cellX, cellY);
        if (v != 0) return v;
    }
    return 0;
}

bool PlacementWorld::isCellEmpty(int cellX, int cellY) const {
    return getOccupant(cellX, cellY) == 0;
}

bool PlacementWorld::isCellEmptyInChannel(const std::string &channel, int cellX, int cellY) const {
    return getOccupantInChannel(channel, cellX, cellY) == 0;
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
    if (it == buildings_.end()) return 0.f;
    const PlacedBuilding &pb = it->second;
    // 真实世界 Y：XZ 平面 = 高度；XY 平面 = 平面第二轴。
    return grid_.plane == grid::GridPlane::XZ ? pb.elevation : pb.worldY;
}

float PlacementWorld::getBuildingWorldZ(int instanceId) const {
    auto it = buildings_.find(instanceId);
    if (it == buildings_.end()) return 0.f;
    const PlacedBuilding &pb = it->second;
    return grid_.plane == grid::GridPlane::XZ ? pb.worldY : pb.elevation;
}

float PlacementWorld::getBuildingElevation(int instanceId) const {
    auto it = buildings_.find(instanceId);
    return it == buildings_.end() ? 0.f : it->second.elevation;
}

std::string PlacementWorld::getBuildingChannel(int instanceId) const {
    auto it = buildings_.find(instanceId);
    return it == buildings_.end() ? std::string{} : it->second.channel;
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

// ---- 便捷操作 ----

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

int PlacementWorld::placeAtWorld3D(const std::string &buildingId, float worldX, float worldY,
                                   float worldZ, float rotationDeg) {
    return PlacementSystem::placeAtWorld3D(this, buildingId, worldX, worldY, worldZ, rotationDeg);
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
