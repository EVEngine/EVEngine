#pragma once
#include "common/Export.h"


// 物品定义注册表：数据驱动的 ItemDefinition（C++ 注册或 JSON 批量加载）。

#include "inventory/ItemTypes.h"

#include <string>
#include <unordered_map>

namespace eve::inventory {

/** @brief EVENGINE_API_FOUNDATION public API. */
class EVENGINE_API_FOUNDATION ItemRegistry {
public:
    /** @brief Registers item. */
    static void registerItem(const ItemDefinition &def);
    /** @brief Finds find. */
    static const ItemDefinition *find(const std::string &id);
    /** @brief Removes remove. */
    static bool remove(const std::string &id);
    /** @brief Clears clear. */
    static void clear();
    /** @brief Returns the number of count. */
    static int count();

    /**
     * @brief 从 JSON 数组或单对象批量注册，返回成功数量。
     * 元素形如：
     * {
     *   "id": "potion.hp", "displayName": "治疗药水", "maxStack": 20,
     *   "weight": 0.2, "volume": 0.1, "category": "consumable",
     *   "tags": ["potion"], "equipSlot": "",
     *   "extra": {"icon": "ui/potion.png"}
     * }
     */
    static int loadFromJson(const std::string &json, std::string *error = nullptr);

private:
    static std::unordered_map<std::string, ItemDefinition> &table();
};

}  // namespace eve::inventory
