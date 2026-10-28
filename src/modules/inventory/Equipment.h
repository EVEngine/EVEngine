#pragma once
#include "common/Export.h"


// 具名装备栏：与 Bag 松耦合，穿脱只搬运物品；属性加成由游戏侧 hook/脚本处理。

#include "inventory/ItemTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::inventory {

class Bag;
class InventorySaveSession;

/** @brief EVENGINE_API_FOUNDATION public API. */
class EVENGINE_API_FOUNDATION EquipmentSet {
public:
    /** @brief Equipment set. */
    EquipmentSet() = default;
    /** @brief Equipment set. */
    ~EquipmentSet() = default;

    EquipmentSet(const EquipmentSet &) = delete;
    EquipmentSet &operator=(const EquipmentSet &) = delete;

    /** @brief Destroys destroy. */
    void destroy();

    /** @brief Returns the id. */
    std::string getId() const { return id_; }
    /** @brief Sets the id. */
    void setId(const std::string &id) { id_ = id; }

    /** @brief 声明一个装备槽；已存在则保留当前物品，仅更新允许标签。 */
    void defineSlot(const std::string &slotName);
    /** @brief Clears slot allowed tags. */
    void clearSlotAllowedTags(const std::string &slotName);
    /** @brief Adds slot allowed tag. */
    void addSlotAllowedTag(const std::string &slotName, const std::string &tag);
    /** @brief True when slot. */
    bool hasSlot(const std::string &slotName) const;
    /** @brief Returns the slot count. */
    int getSlotCount() const;
    /** @brief Returns the slot name. */
    std::string getSlotName(int index) const;

    /** @brief True when slot empty. */
    bool isSlotEmpty(const std::string &slotName) const;
    /** @brief Returns the slot item id. */
    std::string getSlotItemId(const std::string &slotName) const;
    /** @brief Returns the slot quantity. */
    int getSlotQuantity(const std::string &slotName) const;
    /** @brief Returns the slot instance id. */
    int getSlotInstanceId(const std::string &slotName) const;

    /** @brief Can equip from bag. */
    bool canEquipFromBag(Bag *bag, int bagSlot, const std::string &equipSlot,
                          std::string *reason = nullptr) const;
    /** @brief Equip from bag. */
    bool equipFromBag(const std::string &equipSlot, Bag *bag, int bagSlot);
    /** @brief Unequip to bag. */
    bool unequipToBag(const std::string &equipSlot, Bag *bag);
    /** @brief 直接清空槽位（物品丢弃，不回背包）。 */
    bool clearSlot(const std::string &slotName);

    /** @brief Stack at. */
    const ItemStack *stackAt(const std::string &slotName) const;
    /** @brief Stack at. */
    ItemStack *stackAt(const std::string &slotName);
    /** @brief Allowed tags. */
    const std::vector<std::string> *allowedTags(const std::string &slotName) const;

private:
    friend class InventorySystem;
    friend class InventorySaveSession;

    struct Slot {
        ItemStack stack;
        std::vector<std::string> allowedTags;
    };

    std::string id_;
    std::unordered_map<std::string, Slot> slots_;
    std::vector<std::string> order_;
};

}  // namespace eve::inventory
