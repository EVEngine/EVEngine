#pragma once

/**
 * @brief 建筑放置模块的核心数据结构：定义、实例、变更事件、鬼影状态。
 * 全部策略名 / id / 标签使用字符串，便于 JSON 配置与跨版本兼容。
 */

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::building {

/** @brief 建筑模板（进程级注册表中的定义）。 */
struct BuildingDefinition {
    std::string id;
    std::string displayName;
    std::string category;
    /** @brief 占地格子尺寸。 */
    int footprintW = 1;
    int footprintH = 1;
    /** @brief 可选占地掩码，长度 footprintW*footprintH；空表示实心矩形。行主序、原点在最小 x/y。 */
    std::vector<uint8_t> footprintMask;
    /** @brief grid | cell | free | 自定义。 */
    std::string snapMode = "grid";
    /** @brief none | cardinal | free。 */
    std::string rotationMode = "cardinal";
    /** @brief default | boundsOnly | overlapOk | 自定义。 */
    std::string validateRule = "default";
    std::vector<std::string> tags;
    /** @brief 空 = 不限制地形语义。 */
    std::vector<int> requireTerrain;
    std::vector<int> forbidTerrain;
    /** @brief 非空：邻格需有带该 tag 的建筑。 */
    std::string requireAdjacentTag;
    /** @brief >=0：邻格地形语义；-1 = 不限。 */
    int requireAdjacentTerrain = -1;
    std::unordered_map<std::string, int> cost;
    std::unordered_map<std::string, std::string> extra;

    /** @brief 查询辅助。 */
    bool hasTag(const std::string &tag) const;
    std::string getExtra(const std::string &key, const std::string &fallback = {}) const;
    int getCost(const std::string &resource, int fallback = 0) const;
    /** @brief 占地掩码在局部坐标 (localX, localY) 处是否占用。 */
    bool maskAt(int localX, int localY) const;
};

/** @brief 已放置的建筑实例。 */
struct PlacedBuilding {
    int instanceId = 0;
    std::string buildingId;
    int originCellX = 0;
    int originCellY = 0;
    float worldX = 0.f;
    float worldY = 0.f;
    float rotationDeg = 0.f;
    std::unordered_map<std::string, std::string> props;
    std::vector<std::string> tags;

    bool hasTag(const std::string &tag) const;
    std::string getProp(const std::string &key, const std::string &fallback = {}) const;
    void setProp(const std::string &key, const std::string &value);
};

/** @brief 一次成功放置变更的事件（供脚本 poll / C++ hook）。 */
struct BuildingChangeEvent {
    std::string action;  ///< place / remove / move / rotate
    std::string worldId;
    std::string buildingId;
    int instanceId = 0;
    int cellX = -1;
    int cellY = -1;
    int otherCellX = -1;
    int otherCellY = -1;
    float rotationDeg = 0.f;
};

/** @brief 校验上下文：传给可插拔规则。 */
struct PlacementQuery {
    std::string buildingId;
    int cellX = 0;
    int cellY = 0;
    float worldX = 0.f;
    float worldY = 0.f;
    float rotationDeg = 0.f;
    int excludeInstanceId = 0;  ///< move 时排除自身占用
};

/** @brief 吸附结果。 */
struct SnapResult {
    int cellX = 0;
    int cellY = 0;
    float worldX = 0.f;
    float worldY = 0.f;
};

}  // namespace eve::building
