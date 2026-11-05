#pragma once
#include "common/Export.h"

#include "common/Result.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eve::economy {

/** @brief 市场覆盖范围：地区市场或全国市场。 */
enum class MarketScope {
    Regional,  ///< 绑定一个 regionId；可选挂靠 nationId。
    National,  ///< 绑定一个 nationId；不得再绑 regionId。
};

/**
 * @brief 单个资源在市场上的挂牌配置（静态数据，不含运行时库存）。
 */
struct MarketListing {
    std::string resourceType;         ///< 已注册或作者自定义的资源类型 id。
    int         basePrice       = 0;  ///< 基准价（>0）。
    int         buyPremiumBps   = 0;  ///< 买入相对基准的加价（万分比，>=0）。
    int         sellDiscountBps = 0;  ///< 卖出相对基准的折价（万分比，[0,10000]）。
    int         stockCap        = 0;  ///< 挂牌容量上限；0 表示不限。
};

/**
 * @brief 地区/全国市场的静态配置。
 *
 * 权威所有者是 `MarketRegistry`。本结构只保存配置；不持有玩家账本、不结算成交。
 */
struct MarketConfig {
    std::string                id;  ///< 唯一标识。
    MarketScope                scope = MarketScope::Regional;
    std::string                regionId;               ///< Regional 必填；National 必须为空。
    std::string                nationId;               ///< National 必填；Regional 可选挂靠。
    std::string                currencyType = "gold";  ///< 报价货币资源类型。
    int                        tariffBps    = 0;       ///< 跨市场关税配置（万分比，>=0）；本阶段仅保存。
    bool                       enabled      = true;
    std::vector<MarketListing> listings;
};

/**
 * @brief 过程级市场配置注册表：注册 / JSON 加载 / 按 id 或地区/国家查询 / 报价。
 */
class EVENGINE_API_FOUNDATION MarketRegistry {
public:
    /**
     * @brief 注册或替换一个市场配置。
     * @return 校验失败时返回 InvalidArgument 诊断。
     * @cost Cheap: 校验并拷贝一份小型挂牌表；按市场重建摊销。
     */
    [[nodiscard]] static Result<void> registerMarket(const MarketConfig& config);

    /**
     * @brief 解析单个市场对象或市场数组并逐项注册。
     * @param json `eve.economy.market` v1 对象，或同结构对象数组。
     * @return 成功时为新注册/替换的市场数量。
     * @cost Cheap JSON parse of authoring-size market tables; amortize at load.
     */
    [[nodiscard]] static Result<int> loadFromJson(std::string_view json);

    /**
     * @brief 按 id 查询；未注册返回 nullptr。
     * @ownership Borrowed from the process-wide registry; caller must not free it.
     * @lifetime Valid until the next `registerMarket` / `loadFromJson` / `clear`.
     * @nullable Yes when the id is unknown.
     */
    [[nodiscard]] static const MarketConfig* find(const std::string& id);

    /**
     * @brief 查找绑定该 regionId 的第一个启用地区市场。
     * @ownership Borrowed from the registry.
     * @lifetime Valid until the next mutating registry call.
     * @nullable Yes when none matches.
     */
    [[nodiscard]] static const MarketConfig* findRegional(const std::string& regionId);

    /**
     * @brief 查找绑定该 nationId 的第一个启用全国市场。
     * @ownership Borrowed from the registry.
     * @lifetime Valid until the next mutating registry call.
     * @nullable Yes when none matches.
     */
    [[nodiscard]] static const MarketConfig* findNational(const std::string& nationId);

    /** @brief 已注册市场数量。 */
    [[nodiscard]] static int count();
    /** @brief id 是否已注册。 */
    [[nodiscard]] static bool has(const std::string& id);

    /**
     * @brief 按 id 字典序返回第 index 个市场。
     * @ownership Borrowed from the registry.
     * @lifetime Valid until the next mutating registry call.
     * @nullable Yes when index is out of range.
     */
    [[nodiscard]] static const MarketConfig* marketAt(int index);

    /** @brief 清空注册表（测试用）。 */
    static void clear();

    /**
     * @brief 买入报价：`basePrice * (10000 + buyPremiumBps) / 10000`，至少为 1。
     * @return 市场或挂牌缺失时 NotFound。
     */
    [[nodiscard]] static Result<int> quoteBuyPrice(const std::string& marketId, const std::string& resourceType);

    /**
     * @brief 卖出报价：`basePrice * (10000 - sellDiscountBps) / 10000`，至少为 0。
     * @return 市场或挂牌缺失时 NotFound。
     */
    [[nodiscard]] static Result<int> quoteSellPrice(const std::string& marketId, const std::string& resourceType);

private:
    static std::unordered_map<std::string, MarketConfig>& markets();
};

/** @brief 解析 scope 文本：`regional` / `national`（大小写不敏感）。 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<MarketScope> parseMarketScope(std::string_view text);

/** @brief 把 scope 写成稳定小写文本。 */
[[nodiscard]] EVENGINE_API_FOUNDATION std::string marketScopeName(MarketScope scope);

/**
 * @brief 校验并规范化配置（id/scope/挂牌约束）。
 * @cost Cheap: walks a small listings vector.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION Result<void> normalizeMarketConfig(MarketConfig& config);

}  // namespace eve::economy
