#pragma once

/**
 * @file Quest.h
 * @brief 任务 / 成就 / 教程的通用定义模板与进程级注册表。
 *
 * 与 EffectRegistry / SkillRegistry 同构：字符串 id、JSON 或 C++ 注册、
 * 纯数据定义。定义不持有任何运行时状态；运行时进度见 Tracker。
 */

#include "rpg/QuestTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::rpg {

/** @brief 数据驱动的任务模板。 */
struct QuestDefinition {
    std::string id;
    /** @brief "manual" | "auto"，缺省 "manual"。 */
    std::string startPolicy = "manual";
    /** @brief "auto" | "claim"，缺省 "auto"。 */
    std::string completePolicy = "auto";
    /** @brief 本 Tracker 上须已 completed 的条目 id，全部 AND。 */
    std::vector<std::string> requiresIds;
    std::vector<QuestObjective> objectives;
    std::vector<RewardSpec> rewards;
    std::vector<std::string> tags;
    std::unordered_map<std::string, std::string> extra;

    bool hasTag(const std::string &tag) const;
    std::string getExtra(const std::string &key, const std::string &fallback = {}) const;
};

/** @brief 进程级任务定义注册表。 */
class QuestRegistry {
public:
    static void registerQuest(const QuestDefinition &def);
    static const QuestDefinition *find(const std::string &id);
    static bool remove(const std::string &id);
    static void clear();
    static int count();
    static std::vector<std::string> ids();

    /** @brief 从 JSON 数组/对象批量注册；缺 id、重复 objective id、环形 requires 的定义被拒绝。 */
    static int loadFromJson(const std::string &json, std::string *error = nullptr);
};

}  // namespace eve::rpg