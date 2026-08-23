#pragma once

#include "common/IEconomy.h"
#include "common/Module.h"

#include <string>

namespace eve::economy {

/**
 * @brief 经济模块（eve.Economy）：资源类型注册 + 玩家账本 + 事件 + 采集流水线。
 */
class Economy : public Module, public IEconomy {
public:
    Module_REG(Economy);
    Economy();
    ~Economy() override = default;

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
};

}  // namespace eve::economy
