#pragma once

/**
 * @file LootSystem.h
 * @brief 掉落表：按概率随机掉落物品到背包。
 *
 * 掉落表是进程级定义（JSON/C++ 注册）。roll 用注入的随机数流逐条判定，
 * 命中则按 [minQty, maxQty] 随机数量加入 inventory::Bag。引擎不解释掉落语义，
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
 * @thread roll 使用调用方提供的 rng（命名随机流），可在任意线程执行。
 */
class LootSystem {
public:
    static void registerTable(const LootTableDefinition &def);
    static int registerTablesFromJson(const std::string &json);
    static const LootTableDefinition *find(const std::string &id);
    static void clear();
    static int count();

    /**
     * @brief 按掉落表 roll 一次，把命中的物品加入 bag。
     * @return 本次命中并掉落的物品种类数（0 表示全未命中或表不存在）。
     */
    static int roll(const std::string &tableId, eve::inventory::Bag *bag, std::mt19937 &rng);
};

}  // namespace eve::rpg