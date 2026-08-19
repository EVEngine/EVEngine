#pragma once

// 建筑定义注册表：数据驱动的 BuildingDefinition（C++ 注册或 JSON 批量加载）。

#include "building/BuildingTypes.h"

#include <string>
#include <unordered_map>

namespace eve::building {

class BuildingRegistry {
public:
    static void registerBuilding(const BuildingDefinition &def);
    static const BuildingDefinition *find(const std::string &id);
    static bool remove(const std::string &id);
    static void clear();
    static int count();

    /**
     * @brief 从 JSON 数组或单对象批量注册，返回成功数量。
     * 元素形如：
     * {
     *   "id": "house.wood", "displayName": "木屋", "category": "housing",
     *   "footprintW": 2, "footprintH": 2,
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
