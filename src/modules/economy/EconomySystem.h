#pragma once

#include "economy/EconomyLedger.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::economy {

/** @brief 一次经济事件（credit / debit / waste）。 */
struct EconomyEvent {
    int         player = 0;
    std::string type;
    std::string action;  // "credit" | "debit" | "waste"
    int         amount = 0;
};

using EconomyHook = std::function<void(const EconomyEvent&)>;

/**
 * @brief 经济系统门面：按玩家持有账本、转发事件、注册钩子。
 */
class EconomySystem {
public:
    /** @brief 重置所有玩家账本、事件与钩子（测试用）。 */
    static void clear();

    /** @brief 入账并广播事件。@return 实际入账量。 */
    static int credit(int player, const std::string& type, int amount);
    /** @brief 出账并广播事件；余额不足返回 false。 */
    static bool debit(int player, const std::string& type, int amount);

    /** @brief 玩家当前持有量。 */
    static int get(int player, const std::string& type);
    /** @brief 玩家该类型持有上限（0=不限）。 */
    static int getCap(int player, const std::string& type);
    /** @brief 因满仓浪费的累计量。 */
    static int getWasted(int player, const std::string& type);
    /** @brief 累计入账量。 */
    static int getIncome(int player, const std::string& type);
    /** @brief 累计出账量。 */
    static int getExpense(int player, const std::string& type);

    /** @brief 注册事件钩子（同名替换）。 */
    static void registerHook(const std::string& name, EconomyHook fn);
    /** @brief 注销事件钩子。 */
    static void unregisterHook(const std::string& name);

    /** @brief 事件队列（按发生顺序）。 */
    static void clearEvents();
    static int eventCount();
    static const EconomyEvent& eventAt(int index);

private:
    static EconomyLedger& ledger(int player);
    static void emit(const EconomyEvent& ev);

    static std::unordered_map<int, EconomyLedger>&         ledgers();
    static std::vector<EconomyEvent>&                      events();
    static std::unordered_map<std::string, EconomyHook>&   hooks();
};

}  // namespace eve::economy
