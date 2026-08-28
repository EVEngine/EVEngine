#pragma once

/**
 * @file Trait.h
 * @brief 特征（Trait）：RPG Maker 式数据驱动的能力/特性模板与进程级注册表。
 *
 * 特征是"角色拥有的一条能力"，由若干 TraitSpec 组成。TraitSystem 读取这些
 * 规格：参数倍率直接落到属性修改器；元素/状态耐性、特殊参数（暴击/命中/闪避等）
 * 在查询/结算时按需计算。
 */

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::rpg {

/** @brief 特征承载的一条能力规格。 */
struct TraitSpec {
    /** @brief "paramRate" | "elementRate" | "stateRate" | "stateResist" | "exParam" |
     *        "attackElement" | "attackState" | "attackSpeed" | "attackTimes"。 */
    std::string kind;
    /** @brief 目标名（参数/元素/状态/exParam 名）；部分 kind 可空。 */
    std::string target;
    /** @brief 倍率或加值；攻击次数为整数加值，其它多为倍率。 */
    double value = 0.0;
};

/** @brief 数据驱动的特征模板。 */
struct TraitDefinition {
    std::string id;
    std::vector<TraitSpec> traits;
    std::vector<std::string> tags;
    std::unordered_map<std::string, std::string> extra;

    bool hasTag(const std::string &tag) const;
    std::string getExtra(const std::string &key, const std::string &fallback = {}) const;
};

/** @brief 进程级特征定义注册表。 */
class TraitRegistry {
public:
    static void registerTrait(const TraitDefinition &def);
    static const TraitDefinition *find(const std::string &id);
    static bool remove(const std::string &id);
    static void clear();
    static int count();

    /** @brief 从 JSON 数组/对象批量注册，返回成功数量。元素形如：
     * { "id":"fire_resist",
     *   "traits":[{"kind":"elementRate","target":"fire","value":0.5},
     *             {"kind":"elementRate","target":"ice","value":1.5}],
     *   "tags":["defense"] } */
    static int loadFromJson(const std::string &json, std::string *error = nullptr);
};

}  // namespace eve::rpg