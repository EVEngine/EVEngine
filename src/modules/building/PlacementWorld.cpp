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
    grid_ = std::make_unique<grid::GridConfig>();
    grid_->cellW = cellSize > 0.f ? cellSize : 1.f;
    grid_->cellH = grid_->cellW;
    occupancy_.assign(size_t(width_) * size_t(height_), 0);
    terrain_.assign(size_t(width_) * size_t(height_), 0);
    terrainOverrides_.assign(size_t(width_) * size_t(height_), 0);
    PlacementSystem::ensureBuiltins();
}

PlacementWorld::~PlacementWorld() = default;

float PlacementWorld::getCellSize() const { return grid_->cellW; }
float PlacementWorld::getOriginX() const { return grid_->originX; }
float PlacementWorld::getOriginY() const { return grid_->originY; }
const grid::GridConfig &PlacementWorld::getGrid() const { return *grid_; }
grid::GridConfig &PlacementWorld::getGrid() { return *grid_; }

void PlacementWorld::destroy() { delete this; }

void PlacementWorld::setCellSize(float s) {
    if (s > 0.f) {
        grid_->cellW = s;
        grid_->cellH = s;
    }
}

void PlacementWorld::setOrigin(float x, float y) {
    grid_->originX = x;
    grid_->originY = y;
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
    grid_->layout = grid::GridConfig::layoutFromName(layout);
}

std::string PlacementWorld::getGridLayoutName() const {
    return grid::GridConfig::layoutName(grid_->layout);
}

void PlacementWorld::setGridPlane(const std::string &plane) {
    grid_->plane = grid::GridConfig::planeFromName(plane);
}

std::string PlacementWorld::getGridPlaneName() const {
    return grid::GridConfig::planeName(grid_->plane);
}

void PlacementWorld::setCellGap(float gapX, float gapY) {
    grid_->cellGapX = gapX;
    grid_->cellGapY = gapY;
}

void PlacementWorld::setHexSideLength(float s) { grid_->hexSideLength = s; }

void PlacementWorld::setStagger(const std::string &axis, const std::string &index) {
    grid_->staggerAxis = (axis == "x" || axis == "X") ? grid::StaggerAxis::X
                                                     : grid::StaggerAxis::Y;
    grid_->staggerIndex = (index == "even" || index == "Even") ? grid::StaggerIndex::Even
                                                              : grid::StaggerIndex::Odd;
}

void PlacementWorld::setGridFromLayer(map::TileLayer *layer) {
    if (!layer) return;
    const auto &c = *layer->config();
    switch (c.orientation) {
        case map::MapOrientation::Orthogonal:
            grid_->layout = grid::GridLayout::Rectangle;
            break;
        case map::MapOrientation::Isometric:
            grid_->layout = grid::GridLayout::Isometric;
            break;
        case map::MapOrientation::Staggered:
            grid_->layout = grid::GridLayout::Staggered;
            break;
        case map::MapOrientation::Hexagonal:
            grid_->layout = grid::GridLayout::Hexagon;
            break;
    }
    grid_->cellW = c.tileW;
    grid_->cellH = c.tileH;
    grid_->originX = c.originX;
    grid_->originY = c.originY;
    grid_->staggerAxis = c.staggerAxis == map::StaggerAxis::Y ? grid::StaggerAxis::Y
                                                             : grid::StaggerAxis::X;
    grid_->staggerIndex = c.staggerIndex == map::StaggerIndex::Odd ? grid::StaggerIndex::Odd
                                                                  : grid::StaggerIndex::Even;
    grid_->hexSideLength = c.hexSideLength;
}

// ---- 坐标换算 ----

int PlacementWorld::worldToCellX(float worldX) const {
    int cx = 0, cy = 0;
    grid::worldToCell(*grid_, worldX, 0.f, cx, cy, width_, height_);
    return cx;
}

int PlacementWorld::worldToCellY(float worldY) const {
    int cx = 0, cy = 0;
    grid::worldToCell(*grid_, 0.f, worldY, cx, cy, width_, height_);
    return cy;
}

float PlacementWorld::cellToWorldX(int cellX) const {
    float px = 0.f, py = 0.f;
    grid::cellToWorld(*grid_, cellX, 0, px, py);
    return px;
}

float PlacementWorld::cellToWorldY(int cellY) const {
    float px = 0.f, py = 0.f;
    grid::cellToWorld(*grid_, 0, cellY, px, py);
    return py;
}

void PlacementWorld::cellToWorldPlane(int cellX, int cellY, float &px, float &py) const {
    grid::cellToWorld(*grid_, cellX, cellY, px, py);
}

void PlacementWorld::cellToWorld3D(int cellX, int cellY, float elevation, float &worldX,
                                   float &worldY, float &worldZ) const {
    float px = 0.f, py = 0.f;
    grid::cellToWorld(*grid_, cellX, cellY, px, py);
    if (grid_->plane == grid::GridPlane::XZ) {
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
    const float py = (grid_->plane == grid::GridPlane::XZ) ? worldZ : worldY;
    grid::worldToCell(*grid_, px, py, cellX, cellY, width_, height_);
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

void PlacementWorld::setFloorHeight(float height) {
    if (std::isfinite(height) && height > 0.f) floorHeight_ = height;
}

PlacementWorld::ChannelMap &PlacementWorld::edgeChannels(EdgeAxis axis, int level) {
    if (level == 0)
        return axis == EdgeAxis::Horizontal ? horizontalEdgeChannels_ : verticalEdgeChannels_;
    return axis == EdgeAxis::Horizontal ? horizontalEdgesByLevel_[level]
                                        : verticalEdgesByLevel_[level];
}

const PlacementWorld::ChannelMap *PlacementWorld::findEdgeChannels(EdgeAxis axis,
                                                                   int level) const {
    if (level == 0)
        return axis == EdgeAxis::Horizontal ? &horizontalEdgeChannels_ : &verticalEdgeChannels_;
    const auto &levels =
        axis == EdgeAxis::Horizontal ? horizontalEdgesByLevel_ : verticalEdgesByLevel_;
    const auto found = levels.find(level);
    return found == levels.end() ? nullptr : &found->second;
}

PlacementWorld::ChannelMap &PlacementWorld::cornerChannels(int level) {
    return level == 0 ? cornerChannels_ : cornersByLevel_[level];
}

const PlacementWorld::ChannelMap *PlacementWorld::findCornerChannels(int level) const {
    if (level == 0) return &cornerChannels_;
    const auto found = cornersByLevel_.find(level);
    return found == cornersByLevel_.end() ? nullptr : &found->second;
}

// ---- 占用查询 ----

std::vector<int> &PlacementWorld::channelOccupancy(const std::string &channel) {
    return channelOccupancy(channel, activeLevel_);
}

std::vector<int> &PlacementWorld::channelOccupancy(const std::string &channel, int level) {
    if (level != 0) {
        auto &occupancy = cellChannelsByLevel_[level][channel];
        const size_t required = size_t(width_) * size_t(height_);
        if (occupancy.size() != required) occupancy.assign(required, 0);
        return occupancy;
    }
    if (channel.empty()) return occupancy_;
    auto &ch = allChannels_[channel];
    if (ch.size() != occupancy_.size()) ch.assign(occupancy_.size(), 0);
    return ch;
}

int PlacementWorld::getOccupant(int cellX, int cellY) const {
    return getOccupantInChannel(std::string{}, cellX, cellY);
}

int PlacementWorld::getOccupantInChannel(const std::string &channel, int cellX, int cellY) const {
    return getOccupantAtLevel(channel, cellX, cellY, activeLevel_);
}

int PlacementWorld::getOccupantAtLevel(const std::string &channel, int cellX, int cellY,
                                       int level) const {
    if (!inBounds(cellX, cellY)) return 0;
    const size_t idx = size_t(cellY) * size_t(width_) + size_t(cellX);
    if (level != 0) {
        const auto levelIt = cellChannelsByLevel_.find(level);
        if (levelIt == cellChannelsByLevel_.end()) return 0;
        const auto channelIt = levelIt->second.find(channel);
        return channelIt == levelIt->second.end() || idx >= channelIt->second.size()
                   ? 0
                   : channelIt->second[idx];
    }
    if (channel.empty()) return occupancy_[idx];
    auto it = allChannels_.find(channel);
    if (it == allChannels_.end() || idx >= it->second.size()) return 0;
    return it->second[idx];
}

int PlacementWorld::getAnyOccupant(int cellX, int cellY) const {
    return getAnyOccupantAtLevel(cellX, cellY, activeLevel_);
}

int PlacementWorld::getAnyOccupantAtLevel(int cellX, int cellY, int level) const {
    const int occ = getOccupantAtLevel(std::string{}, cellX, cellY, level);
    if (occ != 0) return occ;
    if (level != 0) {
        const auto levelIt = cellChannelsByLevel_.find(level);
        if (levelIt == cellChannelsByLevel_.end()) return 0;
        for (const auto &kv : levelIt->second) {
            const int value = getOccupantAtLevel(kv.first, cellX, cellY, level);
            if (value != 0) return value;
        }
        return 0;
    }
    for (const auto &kv : allChannels_) {
        const int v = getOccupantAtLevel(kv.first, cellX, cellY, 0);
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
    return grid_->plane == grid::GridPlane::XZ
               ? pb.elevation + float(pb.level) * floorHeight_
               : pb.worldY;
}

float PlacementWorld::getBuildingWorldZ(int instanceId) const {
    auto it = buildings_.find(instanceId);
    if (it == buildings_.end()) return 0.f;
    const PlacedBuilding &pb = it->second;
    return grid_->plane == grid::GridPlane::XZ
               ? pb.worldY
               : pb.elevation + float(pb.level) * floorHeight_;
}

float PlacementWorld::getBuildingElevation(int instanceId) const {
    auto it = buildings_.find(instanceId);
    return it == buildings_.end() ? 0.f : it->second.elevation;
}

int PlacementWorld::getBuildingLevel(int instanceId) const {
    auto it = buildings_.find(instanceId);
    return it == buildings_.end() ? 0 : it->second.level;
}

int PlacementWorld::getBuildingSupportCount(int instanceId) const {
    const auto found = buildings_.find(instanceId);
    return found == buildings_.end() ? 0 : int(found->second.supportInstanceIds.size());
}

int PlacementWorld::getBuildingSupportAt(int instanceId, int index) const {
    const auto found = buildings_.find(instanceId);
    if (found == buildings_.end() || index < 0 ||
        index >= int(found->second.supportInstanceIds.size()))
        return 0;
    return found->second.supportInstanceIds[size_t(index)];
}

int PlacementWorld::getBuildingDependentCount(int instanceId) const {
    int count = 0;
    for (const auto &[id, placed] : buildings_) {
        (void)id;
        if (std::find(placed.supportInstanceIds.begin(), placed.supportInstanceIds.end(),
                      instanceId) != placed.supportInstanceIds.end())
            ++count;
    }
    return count;
}

int PlacementWorld::getEdgeOccupant(const std::string &channel, int cellX, int cellY,
                                    const std::string &direction) const {
    return getEdgeOccupantAtLevel(channel, cellX, cellY, direction, activeLevel_);
}

int PlacementWorld::getEdgeOccupantAtLevel(const std::string &channel, int cellX, int cellY,
                                            const std::string &direction, int level) const {
    auto canonical = PlacementSystem::canonicalEdge(cellX, cellY, direction);
    if (!canonical.ok()) return 0;
    const EdgeAddress edge = std::move(canonical).takeValue();
    const bool inBounds = edge.axis == EdgeAxis::Horizontal
                              ? edge.x >= 0 && edge.x < width_ && edge.y >= 0 && edge.y <= height_
                              : edge.x >= 0 && edge.x <= width_ && edge.y >= 0 && edge.y < height_;
    if (!inBounds) return 0;
    const ChannelMap *channels = nullptr;
    if (level == 0) {
        channels = edge.axis == EdgeAxis::Horizontal ? &horizontalEdgeChannels_
                                                      : &verticalEdgeChannels_;
    } else {
        const auto &levels = edge.axis == EdgeAxis::Horizontal ? horizontalEdgesByLevel_
                                                                : verticalEdgesByLevel_;
        const auto levelIt = levels.find(level);
        if (levelIt == levels.end()) return 0;
        channels = &levelIt->second;
    }
    const auto it = channels->find(channel);
    if (it == channels->end()) return 0;
    const size_t index = edge.axis == EdgeAxis::Horizontal
                             ? size_t(edge.y) * size_t(width_) + size_t(edge.x)
                             : size_t(edge.y) * size_t(width_ + 1) + size_t(edge.x);
    return index < it->second.size() ? it->second[index] : 0;
}

bool PlacementWorld::isEdgeEmpty(const std::string &channel, int cellX, int cellY,
                                 const std::string &direction) const {
    return getEdgeOccupant(channel, cellX, cellY, direction) == 0;
}

int PlacementWorld::getCornerOccupant(const std::string &channel, int vertexX,
                                      int vertexY) const {
    return getCornerOccupantAtLevel(channel, vertexX, vertexY, activeLevel_);
}

int PlacementWorld::getCornerOccupantAtLevel(const std::string &channel, int vertexX,
                                             int vertexY, int level) const {
    if (vertexX < 0 || vertexX > width_ || vertexY < 0 || vertexY > height_) return 0;
    const ChannelMap *channels = findCornerChannels(level);
    if (!channels) return 0;
    const auto found = channels->find(channel);
    if (found == channels->end()) return 0;
    const size_t index = size_t(vertexY) * size_t(width_ + 1) + size_t(vertexX);
    return index < found->second.size() ? found->second[index] : 0;
}

int PlacementWorld::getAnyCornerOccupantAtLevel(int vertexX, int vertexY, int level) const {
    const ChannelMap *channels = findCornerChannels(level);
    if (!channels) return 0;
    for (const auto &[channel, occupancy] : *channels) {
        (void)occupancy;
        const int occupant = getCornerOccupantAtLevel(channel, vertexX, vertexY, level);
        if (occupant != 0) return occupant;
    }
    return 0;
}

bool PlacementWorld::isCornerEmpty(const std::string &channel, int vertexX, int vertexY) const {
    return getCornerOccupant(channel, vertexX, vertexY) == 0;
}

int PlacementWorld::getFreeOccupant(const std::string &channel, float worldX,
                                    float worldY) const {
    return getFreeOccupantAtLevel(channel, worldX, worldY, activeLevel_);
}

int PlacementWorld::getFreeOccupantAtLevel(const std::string &channel, float worldX,
                                           float worldY, int level) const {
    for (int instanceId : instanceOrder_) {
        const auto found = buildings_.find(instanceId);
        if (found == buildings_.end()) continue;
        const PlacedBuilding &placed = found->second;
        if (placed.placementKind != "free" || placed.level != level ||
            placed.channel != channel)
            continue;
        if (PlacementSystem::containsFreePoint(placed, worldX, worldY))
            return instanceId;
    }
    return 0;
}

int PlacementWorld::getEdgeConnectionMask(int instanceId) const {
    return static_cast<int>(PlacementSystem::edgeConnectionMask(*this, instanceId));
}

std::string PlacementWorld::getEdgeVariant(int instanceId) const {
    return PlacementSystem::edgeVariant(*this, instanceId);
}

std::string PlacementWorld::getBuildingSurfaceId(int instanceId) const {
    auto it = buildings_.find(instanceId);
    return it == buildings_.end() ? std::string{} : it->second.surfaceId;
}

int64_t PlacementWorld::getBuildingSurfaceRevision(int instanceId) const {
    auto it = buildings_.find(instanceId);
    return it == buildings_.end() ? 0 : static_cast<int64_t>(it->second.surfaceRevision);
}

float PlacementWorld::getBuildingSurfaceNormalX(int instanceId) const {
    auto it = buildings_.find(instanceId);
    return it == buildings_.end() ? 0.f : it->second.surfaceNormalX;
}

float PlacementWorld::getBuildingSurfaceNormalY(int instanceId) const {
    auto it = buildings_.find(instanceId);
    return it == buildings_.end() ? 1.f : it->second.surfaceNormalY;
}

float PlacementWorld::getBuildingSurfaceNormalZ(int instanceId) const {
    auto it = buildings_.find(instanceId);
    return it == buildings_.end() ? 0.f : it->second.surfaceNormalZ;
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

eve::Result<EdgeCurveGroup> PlacementWorld::edgeCurveGroup(EdgeCurveGroupId id) const {
    const auto found = edgeCurveGroups_.find(id.value);
    if (!id || found == edgeCurveGroups_.end()) {
        return eve::Result<EdgeCurveGroup>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "edge curve group was not found",
            std::to_string(id.value), {}, "building.edge-curve-group"));
    }
    return eve::Result<EdgeCurveGroup>::success(found->second);
}

eve::Result<EdgeCurveGroup> PlacementWorld::edgeCurveGroupForInstance(int instanceId) const {
    const auto placed = buildings_.find(instanceId);
    if (placed == buildings_.end() || !placed->second.edgeCurveGroupId) {
        return eve::Result<EdgeCurveGroup>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "edge instance is not linked to a curve group",
            std::to_string(instanceId), {}, "building.edge-curve-group"));
    }
    return edgeCurveGroup(placed->second.edgeCurveGroupId);
}

std::vector<EdgeCurveGroupId> PlacementWorld::edgeCurveGroupIds() const {
    std::vector<EdgeCurveGroupId> ids;
    ids.reserve(edgeCurveGroups_.size());
    for (const auto &[id, group] : edgeCurveGroups_) ids.push_back(group.id);
    std::sort(ids.begin(), ids.end(), [](EdgeCurveGroupId lhs, EdgeCurveGroupId rhs) {
        return lhs.value < rhs.value;
    });
    return ids;
}

int PlacementWorld::placeEdge(const std::string &buildingId, int cellX, int cellY,
                              const std::string &direction) {
    return PlacementSystem::placeEdge(this, buildingId, cellX, cellY, direction);
}

bool PlacementWorld::canPlaceEdge(const std::string &buildingId, int cellX, int cellY,
                                  const std::string &direction) {
    return PlacementSystem::canPlaceEdge(this, buildingId, cellX, cellY, direction, 0, nullptr);
}

std::string PlacementWorld::canPlaceEdgeReason(const std::string &buildingId, int cellX, int cellY,
                                               const std::string &direction) {
    std::string reason;
    PlacementSystem::canPlaceEdge(this, buildingId, cellX, cellY, direction, 0, &reason);
    return reason;
}

int PlacementWorld::placeCorner(const std::string &buildingId, int vertexX, int vertexY) {
    return PlacementSystem::placeCorner(this, buildingId, vertexX, vertexY);
}

bool PlacementWorld::canPlaceCorner(const std::string &buildingId, int vertexX, int vertexY) {
    return PlacementSystem::canPlaceCorner(this, buildingId, vertexX, vertexY);
}

std::string PlacementWorld::canPlaceCornerReason(const std::string &buildingId, int vertexX,
                                                 int vertexY) {
    std::string reason;
    PlacementSystem::canPlaceCorner(this, buildingId, vertexX, vertexY, 0, &reason);
    return reason;
}


int PlacementWorld::placeFree(const std::string &buildingId, float worldX, float worldY,
                              float elevation, float rotationDeg) {
    return PlacementSystem::placeFree(this, buildingId, worldX, worldY, elevation, rotationDeg);
}

bool PlacementWorld::canPlaceFree(const std::string &buildingId, float worldX, float worldY) {
    return PlacementSystem::canPlaceFree(this, buildingId, worldX, worldY);
}

std::string PlacementWorld::canPlaceFreeReason(const std::string &buildingId, float worldX,
                                               float worldY) {
    std::string reason;
    PlacementSystem::canPlaceFree(this, buildingId, worldX, worldY, 0, &reason);
    return reason;
}

bool PlacementWorld::removeBuilding(int instanceId) {
    return PlacementSystem::removeBuilding(this, instanceId);
}

bool PlacementWorld::moveBuilding(int instanceId, int cellX, int cellY, float rotationDeg) {
    return PlacementSystem::moveBuilding(this, instanceId, cellX, cellY, rotationDeg);
}

void PlacementWorld::clearBuildings() { PlacementSystem::clearBuildings(this); }

std::unique_ptr<PlacementWorld> PlacementWorld::cloneState() const {
    auto copy = std::make_unique<PlacementWorld>(width_, height_, getCellSize());
    copy->id_ = id_;
    *copy->grid_ = *grid_;
    copy->snapMode_ = snapMode_;
    copy->validateRule_ = validateRule_;
    copy->floorHeight_ = floorHeight_;
    copy->activeLevel_ = activeLevel_;
    copy->occupancy_ = occupancy_;
    copy->terrain_ = terrain_;
    copy->terrainOverrides_ = terrainOverrides_;
    copy->allChannels_ = allChannels_;
    copy->horizontalEdgeChannels_ = horizontalEdgeChannels_;
    copy->verticalEdgeChannels_ = verticalEdgeChannels_;
    copy->cornerChannels_ = cornerChannels_;
    copy->cellChannelsByLevel_ = cellChannelsByLevel_;
    copy->horizontalEdgesByLevel_ = horizontalEdgesByLevel_;
    copy->verticalEdgesByLevel_ = verticalEdgesByLevel_;
    copy->cornersByLevel_ = cornersByLevel_;
    copy->buildings_ = buildings_;
    copy->edgeCurveGroups_ = edgeCurveGroups_;
    copy->nextEdgeCurveGroupId_ = nextEdgeCurveGroupId_;
    copy->publishEvents_ = false;
    copy->extra_ = extra_;
    copy->instanceOrder_ = instanceOrder_;
    copy->tileLayer_ = tileLayer_;
    copy->terrainBound_ = terrainBound_;
    copy->terrainGidMap_ = terrainGidMap_;
    return copy;
}

std::string PlacementWorld::canRemoveBuildingReason(int instanceId) const {
    if (!hasBuilding(instanceId)) return "not_found";
    return getBuildingDependentCount(instanceId) > 0 ? "support_in_use" : std::string{};
}

int PlacementWorld::removeBuildingCascade(int instanceId) {
    return PlacementSystem::removeBuildingCascade(this, instanceId);
}

void PlacementWorld::swapState(PlacementWorld& candidate) noexcept {
    using std::swap;
    swap(id_, candidate.id_);
    swap(width_, candidate.width_);
    swap(height_, candidate.height_);
    swap(grid_, candidate.grid_);
    swap(snapMode_, candidate.snapMode_);
    swap(validateRule_, candidate.validateRule_);
    swap(floorHeight_, candidate.floorHeight_);
    swap(activeLevel_, candidate.activeLevel_);
    swap(occupancy_, candidate.occupancy_);
    swap(terrain_, candidate.terrain_);
    swap(terrainOverrides_, candidate.terrainOverrides_);
    swap(allChannels_, candidate.allChannels_);
    swap(horizontalEdgeChannels_, candidate.horizontalEdgeChannels_);
    swap(verticalEdgeChannels_, candidate.verticalEdgeChannels_);
    swap(cornerChannels_, candidate.cornerChannels_);
    swap(cellChannelsByLevel_, candidate.cellChannelsByLevel_);
    swap(horizontalEdgesByLevel_, candidate.horizontalEdgesByLevel_);
    swap(verticalEdgesByLevel_, candidate.verticalEdgesByLevel_);
    swap(cornersByLevel_, candidate.cornersByLevel_);
    swap(buildings_, candidate.buildings_);
    swap(edgeCurveGroups_, candidate.edgeCurveGroups_);
    swap(nextEdgeCurveGroupId_, candidate.nextEdgeCurveGroupId_);
    swap(extra_, candidate.extra_);
    swap(instanceOrder_, candidate.instanceOrder_);
    swap(tileLayer_, candidate.tileLayer_);
    swap(terrainBound_, candidate.terrainBound_);
    swap(terrainGidMap_, candidate.terrainGidMap_);
}

}  // namespace eve::building
