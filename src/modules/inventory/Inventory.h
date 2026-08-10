#pragma once

// 背包模块入口：物品定义 / 容器 / 装备栏 / 变更事件的脚本绑定点。
// 设计文档：docs/dev/背包系统设计.md

#include "common/Module.h"
#include "inventory/Bag.h"
#include "inventory/Equipment.h"

#include <string>

namespace eve::inventory {

class Inventory : public Module {
public:
    Module_REG(Inventory);
    Inventory() = default;
    ~Inventory() override = default;

    // ---- Item definitions ----
    int registerItemsFromJson(const std::string &json);
    void clearItemDefinitions();
    int getItemDefinitionCount();
    bool hasItemDefinition(const std::string &itemId);
    std::string getItemDisplayName(const std::string &itemId);
    int getItemMaxStack(const std::string &itemId);
    float getItemWeight(const std::string &itemId);
    float getItemVolume(const std::string &itemId);
    std::string getItemCategory(const std::string &itemId);
    std::string getItemEquipSlot(const std::string &itemId);
    bool itemHasTag(const std::string &itemId, const std::string &tag);
    std::string getItemExtra(const std::string &itemId, const std::string &key,
                             const std::string &fallback = {});

    // ---- Factories ----
    Bag *newBag(int slotCount);
    EquipmentSet *newEquipmentSet();

    // ---- Cross-bag transfer ----
    int transferItem(Bag *from, Bag *to, const std::string &itemId, int quantity);
    int transferSlot(Bag *from, int fromSlot, Bag *to, int quantity);

    // ---- Extension introspection (built-in + C++ registered names) ----
    bool hasAcceptRule(const std::string &name);
    bool hasCapacityPolicy(const std::string &name);
    bool hasStackRule(const std::string &name);

    // ---- Change events ----
    void clearChangeEvents();
    int getChangeEventCount() const;
    std::string getChangeEventAction(int index) const;
    std::string getChangeEventBagId(int index) const;
    std::string getChangeEventOtherBagId(int index) const;
    std::string getChangeEventItemId(int index) const;
    int getChangeEventQuantity(int index) const;
    int getChangeEventSlot(int index) const;
    int getChangeEventOtherSlot(int index) const;
    std::string getChangeEventEquipSlot(int index) const;

};

}  // namespace eve::inventory
