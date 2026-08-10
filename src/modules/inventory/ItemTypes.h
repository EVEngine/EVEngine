#pragma once

// 背包模块的核心数据结构：物品定义、运行时堆叠、变更事件。
// 全部 key 使用字符串，便于 JSON 配置与跨版本兼容。

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::inventory {

/** 物品模板（进程级注册表中的定义，不含运行时数量）。 */
struct ItemDefinition {
    std::string id;
    std::string displayName;
    int maxStack = 1;
    float weight = 0.f;
    float volume = 0.f;
    std::vector<std::string> tags;
    std::string category;
    /** 非空表示建议装备槽名；是否可装备由 EquipmentSet 槽位配置最终决定。 */
    std::string equipSlot;
    std::unordered_map<std::string, std::string> extra;

    bool hasTag(const std::string &tag) const;
    std::string getExtra(const std::string &key, const std::string &fallback = {}) const;
};

/** 容器中的一格堆叠（空槽：itemId 为空或 quantity <= 0）。 */
struct ItemStack {
    int instanceId = 0;
    std::string itemId;
    int quantity = 0;
    float durability = -1.f;  ///< < 0 表示不适用
    std::unordered_map<std::string, std::string> props;
    std::vector<std::string> tags;

    bool empty() const { return itemId.empty() || quantity <= 0; }
    void clear();
    bool hasTag(const std::string &tag) const;
    std::string getProp(const std::string &key, const std::string &fallback = {}) const;
    void setProp(const std::string &key, const std::string &value);
};

/** 一次成功库存变更的事件（供脚本 poll / C++ hook）。 */
struct InventoryChangeEvent {
    std::string action;  ///< add/remove/move/swap/split/merge/transfer/equip/unequip
    std::string bagId;
    std::string otherBagId;
    std::string itemId;
    int quantity = 0;
    int slot = -1;
    int otherSlot = -1;
    std::string equipSlot;
};

}  // namespace eve::inventory
