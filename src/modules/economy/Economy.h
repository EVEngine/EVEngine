#pragma once
#include "common/Export.h"


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
class EVENGINE_API_FOUNDATION Economy : public Module, public IEconomy {
public:
    Module_REG(Economy);
    /** @brief Economy. */
    Economy();
    /** @brief Economy. */
    ~Economy() override;

    // ---- IEconomy ----
    /** @brief Credit. */
    int credit(int player, const std::string& type, int amount) override;
    /** @brief Debit. */
    bool debit(int player, const std::string& type, int amount) override;
    /** @brief Get. */
    int get(int player, const std::string& type) const override;
    /** @brief Get cap. */
    int getCap(int player, const std::string& type) const override;
    /** @brief Get wasted. */
    int getWasted(int player, const std::string& type) const override;
    /** @brief Get income. */
    int getIncome(int player, const std::string& type) const override;
    /** @brief Get expense. */
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

    // ---- 地区 / 全国市场配置 ----
    /**
     * @brief 从 JSON 注册地区或全国市场（对象或数组）。
     * @param json `eve.economy.market` v1；见用户手册。
     * @return 成功时为注册/替换的市场数量。
     * @cost Cheap JSON parse of authoring-size market tables; amortize at load.
     */
    [[nodiscard]] static Result<int> registerMarketsFromJson(const std::string& json);
    /** @brief 清空市场配置（测试用）。 */
    static void clearMarkets();
    /** @brief 已注册市场数量。 */
    [[nodiscard]] static int marketCount();
    /** @brief 市场 id 是否已注册。 */
    [[nodiscard]] static bool hasMarket(const std::string& id);
    /** @brief 按 id 字典序返回第 index 个市场 id；越界返回空串。 */
    [[nodiscard]] static std::string getMarketId(int index);
    /** @brief 市场范围文本（regional/national）；未知 id 返回空串。 */
    [[nodiscard]] static std::string getMarketScope(const std::string& id);
    /** @brief 地区市场的 regionId；全国市场或未知 id 返回空串。 */
    [[nodiscard]] static std::string getMarketRegionId(const std::string& id);
    /** @brief 市场挂靠/绑定的 nationId；未知 id 返回空串。 */
    [[nodiscard]] static std::string getMarketNationId(const std::string& id);
    /** @brief 市场报价货币类型；未知 id 返回空串。 */
    [[nodiscard]] static std::string getMarketCurrencyType(const std::string& id);
    /** @brief 市场关税配置（万分比）；未知 id 返回 0。 */
    [[nodiscard]] static int getMarketTariffBps(const std::string& id);
    /** @brief 市场是否启用；未知 id 返回 false。 */
    [[nodiscard]] static bool isMarketEnabled(const std::string& id);
    /**
     * @brief 按 regionId 查找启用中的地区市场 id。
     * @return 未找到返回空串。
     */
    [[nodiscard]] static std::string findRegionalMarket(const std::string& regionId);
    /**
     * @brief 按 nationId 查找启用中的全国市场 id。
     * @return 未找到返回空串。
     */
    [[nodiscard]] static std::string findNationalMarket(const std::string& nationId);
    /** @brief 市场挂牌数量；未知市场返回 0。 */
    [[nodiscard]] static int getMarketListingCount(const std::string& marketId);
    /** @brief 第 index 个挂牌的资源类型；越界/未知返回空串。 */
    [[nodiscard]] static std::string getMarketListingResourceType(const std::string& marketId, int index);
    /** @brief 第 index 个挂牌的基准价；越界/未知返回 0。 */
    [[nodiscard]] static int getMarketListingBasePrice(const std::string& marketId, int index);
    /**
     * @brief 买入报价。
     * @return 市场或挂牌缺失时 NotFound。
     */
    [[nodiscard]] static Result<int> quoteBuyPrice(const std::string& marketId, const std::string& resourceType);
    /**
     * @brief 卖出报价。
     * @return 市场或挂牌缺失时 NotFound。
     */
    [[nodiscard]] static Result<int> quoteSellPrice(const std::string& marketId, const std::string& resourceType);

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
