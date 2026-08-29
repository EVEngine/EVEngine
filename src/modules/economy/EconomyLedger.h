#pragma once

#include <string>
#include <unordered_map>

namespace eve::economy {

/**
 * @brief 单个玩家的资源账本：当前量、上限、收支与浪费统计。
 */
class EconomyLedger {
public:
    /**
     * @brief 入账 amount。
     * @return 实际入账量（受类型上限约束，超限部分记为浪费）。
     */
    int credit(const std::string& type, int amount);
    /** @brief 出账 amount；余额不足时返回 false 且不扣款。 */
    bool debit(const std::string& type, int amount);
    /** @brief 是否可负担 amount。 */
    bool canAfford(const std::string& type, int amount) const;
    /** @brief 当前持有量。 */
    int get(const std::string& type) const;
    /** @brief 类型上限（来自注册表；未注册返回 0=不限）。 */
    int getCap(const std::string& type) const;
    /** @brief 因满仓而浪费的累计量。 */
    int getWasted(const std::string& type) const;
    /** @brief 累计入账。 */
    int getIncome(const std::string& type) const;
    /** @brief 累计出账。 */
    int getExpense(const std::string& type) const;

    /**
     * @brief Swap two ledgers without exposing an intermediate partial state.
     * @param other Ledger whose complete state is exchanged with this one.
     * @remarks Used by atomic adapters after a candidate ledger has been
     *          fully validated and built.
     */
    void swap(EconomyLedger& other) noexcept;

private:
    std::unordered_map<std::string, int> current_;
    std::unordered_map<std::string, int> wasted_;
    std::unordered_map<std::string, int> income_;
    std::unordered_map<std::string, int> expense_;
};

}  // namespace eve::economy
