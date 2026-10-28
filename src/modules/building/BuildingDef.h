#pragma once
#include "common/Export.h"


// 建筑定义注册表：数据驱动的 BuildingDefinition（C++ 注册或 JSON 批量加载）。

#include "building/BuildingTypes.h"

#include <string>
#include <unordered_map>

namespace eve::building {

/** @brief EVENGINE_API_WORLD public API. */
class EVENGINE_API_WORLD BuildingRegistry {
public:
    /** @brief Registers building. */
    static void registerBuilding(const BuildingDefinition &def);
    /** @brief Finds . */
    static const BuildingDefinition *find(const std::string &id);
    /** @brief Removes . */
    static bool remove(const std::string &id);
    /** @brief Clears . */
    static void clear();
    /** @brief Returns the number of . */
    static int count();

    /**
     * @brief 从 JSON 数组或单对象批量注册，返回成功数量。
     * 元素形如：
     * {
     *   "id": "house.wood", "displayName": "木屋", "category": "housing",
     *   "footprintW": 2, "footprintH": 2,
     *   "freeFootprintWidthCells": 2.0, "freeFootprintHeightCells": 0.75,
     *   "freeFootprintVertices": [-1,0, 0,-0.5, 1,0, 0,0.5],
     *   "snapMode": "grid", "rotationMode": "cardinal", "validateRule": "default",
     *   "tags": ["house"], "requireTerrain": [1], "forbidTerrain": [2],
     *   "requireAdjacentTag": "road", "requireAdjacentTerrain": -1,
     *   "cost": {"wood": 20, "gold": 5},
     *   "footprintMask": [1,1,1,0],
     *   "extra": {"mesh": "models/house.glb"}
     * }
     */
    static int loadFromJson(const std::string &json, std::string *error = nullptr);

private:
    static std::unordered_map<std::string, BuildingDefinition> &table();
};

}  // namespace eve::building
