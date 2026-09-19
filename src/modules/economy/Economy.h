#pragma once

#include "common/IEconomy.h"
#include "common/Module.h"
#include "common/Result.h"

#include <memory>
#include <string>
#include <vector>

namespace eve::economy {

class EconomyControl;

/**
 * @brief 经济模块（eve.Economy）：资源类型注册 + 玩家账本 + 事件 + 采集流水线。
 */
class Economy : public Module, public IEconomy {
public:
    Module_REG(Economy);
    Economy();
    ~Economy() override;

    // ---- IEconomy ----
    int credit(int player, const std::string& type, int amount) override;
    bool debit(int player, const std::string& type, int amount) override;
    int get(int player, const std::string& type) const override;
    int getCap(int player, const std::string& type) const override;
    int getWasted(int player, const std::string& type) const override;
    int getIncome(int player, const std::string& type) const override;
    int getExpense(int player, const std::string& type) const override;

    // ---- 注册表门面 ----
    /** @brief 注册资源类型；depletion 取值 finite/renewable/infinite/growing。 */
    static bool registerResourceType(const std::string& id, const std::string& category,
                                     int stockMax, const std::string& depletion);
    /** @brief 清空资源类型（测试用）。 */
    static void clearTypes();
    /** @brief 已注册类型数量。 */
    static int typeCount();
    /** @brief 类型是否已注册。 */
    static bool hasType(const std::string& id);
    /** @brief 类型持有上限（未注册返回 0）。 */
    static int getStockMax(const std::string& id);
    /** @brief 按 id 字典序返回第 index 个资源类型 id；越界返回空串。 */
    std::string getTypeId(int index);

    // ---- 事件 ----
    /** @brief 清空事件队列。 */
    static void clearEvents();
    /** @brief 事件队列长度。 */
    static int eventCount();
    /** @brief 第 index 个事件的动作（credit/debit/waste；越界返回空串）。 */
    static std::string eventAction(int index);
    /** @brief 第 index 个事件的玩家（越界返回 0）。 */
    static int eventPlayer(int index);
    /** @brief 第 index 个事件的资源类型（越界返回空串）。 */
    static std::string eventType(int index);
    /** @brief 第 index 个事件的数值（越界返回 0）。 */
    static int eventAmount(int index);

    /**
     * @brief 把一个玩家账本发布到共享玩法协议（`eve_gameplay` / MCP）。
     *
     * 领域动作词表就是本模块自己的操作：`economy:debit` 是普通消费（玩家档位
     * 可用，余额不足即拒绝），`economy:credit` 是发放（仅 test-driver /
     * developer-cheat，且超上限部分以 waste 事件披露）。采集仍由
     * `GatherNode`/`Collector` 按 tick 驱动，不在此另开捷径。
     * @param instanceId 实例稳定标识，必须是规范持久 id（UUID 文本）。
     * @param ownerId 控制该实例的玩家/角色稳定标识，同为规范持久 id。
     * @param player 模块级玩家 id（`EconomySystem` 的账本键）。
     * @return 成功返回空结果；id 非法或实例重复返回诊断。
     * @ownership 适配器由本模块持有并随模块销毁；账本所有权不变。
     * @thread 所有者模拟线程。
     */
    [[nodiscard]] eve::Result<void> publishGameplay(const std::string& instanceId, const std::string& ownerId,
                                                    int player);
    /** @brief 取消发布一个实例；该实例未发布（或 id 非法）时返回诊断。 */
    [[nodiscard]] eve::Result<void> unpublishGameplay(const std::string& instanceId);
    /** @brief 取消发布本模块持有的全部玩法实例。 */
    void clearGameplayControls();
    /** @brief 已发布的玩法实例数量。 */
    [[nodiscard]] int gameplayControlCount() const;
    /** @brief 已发布的玩法实例标识（发布顺序）。 */
    [[nodiscard]] std::vector<std::string> gameplayInstances() const;

private:
    /** 惰性创建的领域适配器；模块析构时随之注销。 */
    std::unique_ptr<EconomyControl> gameplay_;
};

}  // namespace eve::economy
