#pragma once

// 具名装备栏：与 Bag 松耦合，穿脱只搬运物品；属性加成由游戏侧 hook/脚本处理。

#include "inventory/ItemTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::inventory {

class Bag;

class EquipmentSet {
public:
    EquipmentSet() = default;
    ~EquipmentSet() = default;

    EquipmentSet(const EquipmentSet &) = delete;
    EquipmentSet &operator=(const EquipmentSet &) = delete;

    void destroy();

    std::string getId() const { return id_; }
    void setId(const std::string &id) { id_ = id; }

    /** 声明一个装备槽；已存在则保留当前物品，仅更新允许标签。 */
    void defineSlot(const std::string &slotName);
    void clearSlotAllowedTags(const std::string &slotName);
    void addSlotAllowedTag(const std::string &slotName, const std::string &tag);
    bool hasSlot(const std::string &slotName) const;
    int getSlotCount() const;
    std::string getSlotName(int index) const;

    bool isSlotEmpty(const std::string &slotName) const;
    std::string getSlotItemId(const std::string &slotName) const;
    int getSlotQuantity(const std::string &slotName) const;
    int getSlotInstanceId(const std::string &slotName) const;

    bool canEquipFromBag(Bag *bag, int bagSlot, const std::string &equipSlot,
                          std::string *reason = nullptr) const;
    bool equipFromBag(const std::string &equipSlot, Bag *bag, int bagSlot);
    bool unequipToBag(const std::string &equipSlot, Bag *bag);
    /** 直接清空槽位（物品丢弃，不回背包）。 */
    bool clearSlot(const std::string &slotName);

    const ItemStack *stackAt(const std::string &slotName) const;
    ItemStack *stackAt(const std::string &slotName);
    const std::vector<std::string> *allowedTags(const std::string &slotName) const;

private:
    friend class InventorySystem;

    struct Slot {
        ItemStack stack;
        std::vector<std::string> allowedTags;
    };

    std::string id_;
    std::unordered_map<std::string, Slot> slots_;
    std::vector<std::string> order_;
};

}  // namespace eve::inventory
