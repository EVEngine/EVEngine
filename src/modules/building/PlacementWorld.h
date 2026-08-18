#pragma once

/**
 * @brief 放置世界：格子占用 + 地形语义 + 已放置建筑实例。
 * 行为由 PlacementSystem 提供；本类暴露便于脚本绑定的薄封装方法。
 */

#include "building/BuildingTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::building {

class Ghost;

/** @brief 格子型建筑放置世界（脚本可直接操作）。 */
class PlacementWorld {
public:
    /** @brief 创建 width×height 的格子世界，cellSize 为像素/格。 */
    PlacementWorld(int width, int height, float cellSize = 32.f);
    ~PlacementWorld() = default;

    PlacementWorld(const PlacementWorld &) = delete;
    PlacementWorld &operator=(const PlacementWorld &) = delete;

    /** @brief 释放资源并使其失效。 */
    void destroy();

    /** @brief 世界 id（用于变更事件定位）。 */
    std::string getId() const { return id_; }
    void setId(const std::string &id) { id_ = id; }

    /** @brief 尺寸 / 格子大小 / 原点。 */
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    float getCellSize() const { return cellSize_; }
    void setCellSize(float s);

    float getOriginX() const { return originX_; }
    float getOriginY() const { return originY_; }
    void setOrigin(float x, float y);

    /** @brief 放置策略：吸附模式 / 校验规则。 */
    std::string getSnapMode() const { return snapMode_; }
    void setSnapMode(const std::string &mode) { snapMode_ = mode; }
    std::string getValidateRule() const { return validateRule_; }
    void setValidateRule(const std::string &rule) { validateRule_ = rule; }

    /** @brief 世界级额外属性（键值）。 */
    void setExtra(const std::string &key, const std::string &value);
    std::string getExtra(const std::string &key, const std::string &fallback = {}) const;

    /** @brief 坐标换算：世界像素 ↔ 格子坐标。 */
    int worldToCellX(float worldX) const;
    int worldToCellY(float worldY) const;
    float cellToWorldX(int cellX) const;
    float cellToWorldY(int cellY) const;

    /** @brief 地形语义（占用检查用）。 */
    void fillTerrain(int semantic);
    void setTerrain(int cellX, int cellY, int semantic);
    int getTerrain(int cellX, int cellY) const;
    bool inBounds(int cellX, int cellY) const;

    /** @brief 占用查询：格子上建筑实例 / 是否为空。 */
    int getOccupant(int cellX, int cellY) const;
    bool isCellEmpty(int cellX, int cellY) const;
    /** @brief 已放置建筑实例查询。 */
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
    /** @brief 按插入顺序取实例 id。 */
    int getBuildingInstanceAt(int index) const;

    /** @brief 便捷操作（转发 PlacementSystem）：放置 / 移除 / 移动。 */
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
