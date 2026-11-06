#pragma once
#include "common/Export.h"


/**
 * @file LootSystem.h
 * @brief 掉落表：按概率随机掉落物品到背包。
 *
 * 掉落表是进程级定义（JSON/C++ 注册）。roll 用注入的随机数流逐条判定，
 * 命中则按 [minQty, maxQty] 随机数量整批加入 inventory::Bag。引擎不解释掉落语义，
 * 只做概率 + 数量。
 */

#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace eve::inventory {
class Bag;
}

namespace eve::rpg {

/** @brief 掉落表里的一条掉落。 */
struct LootEntry {
    std::string itemId;
    double chance = 0.0;  ///< [0,1]，roll 值小于 chance 才掉落
    int minQty = 1;
    int maxQty = 1;
};

/** @brief 掉落表定义。 */
struct LootTableDefinition {
    std::string id;
    std::vector<LootEntry> entries;
};

/**
 * @brief 掉落系统：进程级掉落表 + 随机 roll 入包。
 * @thread 在 Bag 与注册表的 owning simulation thread 执行，不支持并发修改。
 */
class EVENGINE_API_PLATFORM LootSystem {
public:
    /** @brief Registers table. */
    static void registerTable(const LootTableDefinition &def);
    /** @brief Registers tables from json. */
    static int registerTablesFromJson(const std::string &json);
    /** @brief Finds . */
    static const LootTableDefinition *find(const std::string &id);
    /** @brief Clears . */
    static void clear();
    /** @brief Returns the number of . */
    static int count();

    /**
     * @brief 兼容入口：随机抽取后原子入包，内部使用 Result 事务路径。
     * @return 成功提交的表项数；失败或全部未命中返回 0（兼容 API 无错误诊断）。
     * @remarks 无效定义、未知物品或容量不足时不修改 Bag、随机流或发布事件。
     * 成功但未命中仍消耗随机流；跨标准库不承诺逐位相同的随机分布结果。
     * @ownership bag 和 rng 由调用方拥有，只在本次同步调用期间借用。
     * @thread Bag、定义注册表与随机流的 owning simulation thread。
     * @reentrancy 库存策略不得修改参与者；变更钩子在 Bag 与随机流提交后执行。
     * @cost 复制背包并遍历掉落表，远高于属性查询；只在发奖时调用。
     */
    static int roll(const std::string &tableId, eve::inventory::Bag *bag, std::mt19937 &rng);
};

}  // namespace eve::rpg
