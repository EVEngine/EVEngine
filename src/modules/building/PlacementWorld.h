#pragma once

// 放置世界：格子占用 + 地形语义 + 已放置建筑实例。
// 行为由 PlacementSystem 提供；本类暴露便于脚本绑定的薄封装方法。

#include "building/BuildingTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::building {

class Ghost;

class PlacementWorld {
public:
    PlacementWorld(int width, int height, float cellSize = 32.f);
    ~PlacementWorld() = default;

    PlacementWorld(const PlacementWorld &) = delete;
    PlacementWorld &operator=(const PlacementWorld &) = delete;

    void destroy();

    std::string getId() const { return id_; }
    void setId(const std::string &id) { id_ = id; }

    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    float getCellSize() const { return cellSize_; }
    void setCellSize(float s);

    float getOriginX() const { return originX_; }
    float getOriginY() const { return originY_; }
    void setOrigin(float x, float y);

    std::string getSnapMode() const { return snapMode_; }
    void setSnapMode(const std::string &mode) { snapMode_ = mode; }
    std::string getValidateRule() const { return validateRule_; }
    void setValidateRule(const std::string &rule) { validateRule_ = rule; }

    void setExtra(const std::string &key, const std::string &value);
    std::string getExtra(const std::string &key, const std::string &fallback = {}) const;

    // ---- 坐标换算 ----
    int worldToCellX(float worldX) const;
    int worldToCellY(float worldY) const;
    float cellToWorldX(int cellX) const;
    float cellToWorldY(int cellY) const;

    // ---- 地形 ----
    void fillTerrain(int semantic);
    void setTerrain(int cellX, int cellY, int semantic);
    int getTerrain(int cellX, int cellY) const;
    bool inBounds(int cellX, int cellY) const;

    // ---- 占用查询 ----
    int getOccupant(int cellX, int cellY) const;
    bool isCellEmpty(int cellX, int cellY) const;
    int getBuildingCount() const;
    bool hasBuilding(int instanceId) const;
    std::string getBuildingId(int instanceId) const;
    int getBuildingCellX(int instanceId) const;
    int getBuildingCellY(int instanceId) const;
    float getBuildingWorldX(int instanceId) const;
    float getBuildingWorldY(int instanceId) const;
    float getBuildingRotation(int instanceId) const;
    std::string getBuildingProp(int instanceId, const std::string &key,
                                const std::string &fallback = {}) const;
    void setBuildingProp(int instanceId, const std::string &key, const std::string &value);
    bool buildingHasTag(int instanceId, const std::string &tag) const;
    int getBuildingInstanceAt(int index) const;

    // ---- 便捷操作（转发 PlacementSystem）----
    bool canPlace(const std::string &buildingId, int cellX, int cellY, float rotationDeg = 0.f);
    std::string canPlaceReason(const std::string &buildingId, int cellX, int cellY,
                               float rotationDeg = 0.f);
    int placeAt(const std::string &buildingId, int cellX, int cellY, float rotationDeg = 0.f);
    int placeAtWorld(const std::string &buildingId, float worldX, float worldY,
                     float rotationDeg = 0.f);
    int placeGhost(Ghost *ghost);
    bool removeBuilding(int instanceId);
    bool moveBuilding(int instanceId, int cellX, int cellY, float rotationDeg = -1.f);
    void clearBuildings();

    // ---- 供 System 直接访问 ----
    const std::vector<int> &occupancy() const { return occupancy_; }
    std::vector<int> &occupancy() { return occupancy_; }
    const std::vector<int> &terrain() const { return terrain_; }
    std::vector<int> &terrain() { return terrain_; }
    const std::unordered_map<int, PlacedBuilding> &buildings() const { return buildings_; }
    std::unordered_map<int, PlacedBuilding> &buildings() { return buildings_; }

private:
    friend class PlacementSystem;

    std::string id_;
    int width_ = 0;
    int height_ = 0;
    float cellSize_ = 32.f;
    float originX_ = 0.f;
    float originY_ = 0.f;
    std::string snapMode_ = "grid";
    std::string validateRule_ = "default";
    std::vector<int> occupancy_;
    std::vector<int> terrain_;
    std::unordered_map<int, PlacedBuilding> buildings_;
    std::unordered_map<std::string, std::string> extra_;
    std::vector<int> instanceOrder_;
};

}  // namespace eve::building
