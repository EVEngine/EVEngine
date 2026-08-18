#pragma once

/**
 * @brief 背包模块入口：物品定义 / 容器 / 装备栏 / 变更事件的脚本绑定点。
 * 设计文档：docs/dev/背包系统设计.md
 */

#include "common/Module.h"
#include "inventory/Bag.h"
#include "inventory/Equipment.h"

#include <string>

namespace eve::inventory {

/** @brief 背包模块（eve.Inventory）。 */
class Inventory : public Module {
public:
    Module_REG(Inventory);
    Inventory() = default;
    ~Inventory() override = default;

    /** @brief 从 JSON 注册物品定义；返回成功注册数量。 */
    int registerItemsFromJson(const std::string &json);
    /** @brief 清空物品定义。 */
    void clearItemDefinitions();
    /** @brief 已注册物品定义数量。 */
    int getItemDefinitionCount();
    /** @brief 物品定义查询（显示名/堆叠/重量/体积/类别/装备槽/标签/额外属性）。 */
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

    /** @brief 工厂：创建容器 / 装备栏。 */
    Bag *newBag(int slotCount);
    EquipmentSet *newEquipmentSet();

    /** @brief 跨容器转移（按物品 id / 按槽位）；返回实际转移数量。 */
    int transferItem(Bag *from, Bag *to, const std::string &itemId, int quantity);
    int transferSlot(Bag *from, int fromSlot, Bag *to, int quantity);

    /** @brief 扩展策略是否存在（接受规则 / 容量策略 / 堆叠规则）。 */
    bool hasAcceptRule(const std::string &name);
    bool hasCapacityPolicy(const std::string &name);
    bool hasStackRule(const std::string &name);

    /** @brief 变更事件队列（添加/移除/移动/装备）。 */
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
