#pragma once

#include "rpg/QuestTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::rpg {

/** @brief 数据驱动的任务定义。 */
struct QuestDefinition {
    std::string                                  id;
    std::string                                  title;
    std::string                                  description;
    std::string                                  startPolicy    = "manual";
    std::string                                  completePolicy = "claim";
    std::vector<std::string>                     prerequisites;
    std::vector<QuestObjective>                  objectives;
    std::vector<RewardSpec>                      rewards;
    std::vector<std::string>                     tags;
    std::unordered_map<std::string, std::string> extra;

    /** @brief 查询定义是否带有指定标签。 */
    bool hasTag(const std::string& tag) const;
};

/** @brief 进程级任务定义注册表。 */
class QuestRegistry {
public:
    /** @brief 注册或覆盖定义；非法目标或前置环返回 false。 */
    static bool registerQuest(const QuestDefinition& def);
    /** @brief 按 id 查询定义。 */
    static const QuestDefinition* find(const std::string& id);
    /** @brief 移除一个定义。 */
    static bool remove(const std::string& id);
    /** @brief 清空所有任务定义。 */
    static void clear();
    /** @brief 返回已注册定义数。 */
    static int count();
    /** @brief 返回排序后的定义 id。 */
    static std::vector<std::string> ids();
    /** @brief 从 JSON 对象或数组事务式注册定义。 */
    static int loadFromJson(const std::string& json, std::string* error = nullptr);
};

}  // namespace eve::rpg
