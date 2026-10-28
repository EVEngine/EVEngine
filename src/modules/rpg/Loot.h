#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::rpg {

/** @brief 战利品表中的一个候选项。 */
struct LootEntry {
    std::string              itemId;
    int                      minQuantity = 1;
    int                      maxQuantity = 1;
    double                   weight      = 1.0;
    double                   chance      = 1.0;
    std::vector<std::string> tags;
};

/** @brief 数据驱动战利品表。 */
struct LootTableDefinition {
    std::string            id;
    int                    rolls  = 1;
    bool                   unique = false;
    std::vector<LootEntry> entries;
};

/** @brief 进程级战利品表定义注册表。 */
class LootRegistry {
public:
    /** @brief 注册或覆盖一张有效战利品表。 */
    static bool registerTable(const LootTableDefinition& definition);
    /** @brief 按 id 查询定义，不存在时返回 nullptr。 */
    static const LootTableDefinition* find(const std::string& id);
    /** @brief 清空所有战利品表定义。 */
    static void clear();
    /** @brief 返回已注册定义数。 */
    static int count();
    /** @brief 从 JSON 对象或数组注册定义。 @return 成功注册数量。 */
    static int loadFromJson(const std::string& json, std::string* error = nullptr);
};

/** @brief 一次 roll 的结果，可直接逐项加入 Inventory::Bag。 */
class LootRoll {
public:
    /** @brief 释放由 LootSystem::roll 创建的脚本对象。 */
    void destroy() { delete this; }
    /** @brief 返回结果项数量。 */
    int getCount() const { return int(itemIds_.size()); }
    /** @brief 返回指定结果项的物品 id。 */
    std::string getItemId(int index) const;
    /** @brief 返回指定结果项的数量。 */
    int getQuantity(int index) const;

private:
    friend class LootSystem;
    std::vector<std::string> itemIds_;
    std::vector<int>         quantities_;
};

/** @brief 战利品抽取服务。 */
class LootSystem {
public:
    /** @brief 重置掉落随机流。 */
    static void setRandomSeed(uint32_t seed);
    /** @brief 抽取战利品表；未知表返回空结果对象。 */
    static LootRoll* roll(const std::string& tableId, double luck = 0.0);
};

}  // namespace eve::rpg
