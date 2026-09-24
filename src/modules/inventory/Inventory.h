#pragma once
#include "common/Export.h"


/**
 * @brief 背包模块入口：物品定义 / 容器 / 装备栏 / 变更事件的脚本绑定点。
 * 设计文档：docs/dev/背包系统设计.md
 */

#include "common/Module.h"
#include "common/Result.h"
#include "inventory/Bag.h"
#include "inventory/Equipment.h"

#include <memory>
#include <string>
#include <vector>

namespace eve::inventory {

class InventoryControl;

/** @brief 背包模块（eve.Inventory）。 */
class EVENGINE_API_FOUNDATION Inventory : public Module {
public:
    Module_REG(Inventory);
    // Declared out of line, like the destructor below: the class owns a
    // unique_ptr<InventoryControl> whose type is only forward declared here, and
    // a defaulted constructor in the class body has to destroy the members it
    // already built when a later one throws -- which needs the complete type
    // (C2027 "can't delete an incomplete type" in the Dawn parity lane).
    Inventory();
    ~Inventory() override;

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

    /**
     * @brief 把一个玩家背包实例发布到共享玩法协议（`eve_gameplay` / MCP）。
     *
     * 领域动作词表就是本模块自己的操作（add/remove/split/equip/unequip），
     * 因此 Agent 与玩家走同一条权威写入路径，而不是另开一条调试旁路。
     * 一个模块只注册一个领域适配器（共享路由器要求每个领域唯一），该适配器
     * 服务全部已发布实例；同一 instanceId 重复发布返回 Conflict。
     * @param instanceId 实例稳定标识，必须是规范持久 id（UUID 文本，如
     *        `00000000-0000-7000-8000-000000000701`），与 `eve_gameplay` 的
     *        `request.instance` 逐字一致。
     * @param ownerId 控制该实例的玩家/角色稳定标识，同为规范持久 id
     *        （`session.controlledSubjects` 校验用）。
     * @param bag 借用容器，必须比本次发布存活更久。
     * @param equipment 借用装备栏，可为 null（则实例没有装备动作）。
     * @return 成功时返回空结果；实例 id 非法、重复或 bag 为空时返回诊断。
     * @ownership 适配器由本模块持有并随模块销毁；bag / equipment 所有权不变。
     * @thread 所有者模拟线程。
     */
    [[nodiscard]] eve::Result<void> publishGameplay(const std::string &instanceId, const std::string &ownerId, Bag *bag,
                                                    EquipmentSet *equipment = nullptr);
    /** @brief 取消发布一个实例；该实例未发布（或 id 非法）时返回诊断。 */
    [[nodiscard]] eve::Result<void> unpublishGameplay(const std::string &instanceId);
    /** @brief 取消发布本模块持有的全部玩法实例。 */
    void clearGameplayControls();
    /** @brief 已发布的玩法实例数量。 */
    [[nodiscard]] int gameplayControlCount() const;
    /** @brief 已发布的玩法实例标识（发布顺序）。 */
    [[nodiscard]] std::vector<std::string> gameplayInstances() const;

private:
    /** 惰性创建的领域适配器；模块析构时随之注销。 */
    std::unique_ptr<InventoryControl> gameplay_;
};

}  // namespace eve::inventory
