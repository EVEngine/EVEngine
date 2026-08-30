#pragma once

#include <string>

namespace eve::economy {

/**
 * @brief 经济账本的跨模块查询/操作接口（提供方：economy 模块）。
 *
 * 消费方（AI、UI、生产队列）通过 eve::cap::query<IEconomy>() 获取实现，
 * 不直接 include economy 模块；无提供方时返回 nullptr，调用方自行兜底。
 */
class IEconomy {
public:
    static constexpr const char* capabilityName = "IEconomy";
    virtual ~IEconomy() = default;

    /**
     * @brief 为玩家入账 amount，受资源类型上限约束。
     * @return 实际入账量（超出上限的部分记为浪费）。
     */
    virtual int credit(int player, const std::string& type, int amount) = 0;
    /** @brief 为玩家出账 amount；余额不足返回 false 且不扣款。 */
    virtual bool debit(int player, const std::string& type, int amount) = 0;
    /** @brief 玩家当前持有量。 */
    virtual int get(int player, const std::string& type) const = 0;
    /** @brief 玩家该类型的持有上限（0=不限）。 */
    virtual int getCap(int player, const std::string& type) const = 0;
    /** @brief 因满仓浪费的累计量。 */
    virtual int getWasted(int player, const std::string& type) const = 0;
    /** @brief 累计入账量。 */
    virtual int getIncome(int player, const std::string& type) const = 0;
    /** @brief 累计出账量。 */
    virtual int getExpense(int player, const std::string& type) const = 0;
};

}  // namespace eve::economy
