#pragma once
#include "common/Export.h"
#include "common/StringMapInstances.h"


/**
 * @file Quest.h
 * @brief 任务 / 成就 / 教程的通用定义模板与进程级注册表。
 *
 * 与 EffectRegistry / SkillRegistry 同构：字符串 id、JSON 或 C++ 注册、
 * 纯数据定义。定义不持有任何运行时状态；运行时进度见 Tracker。
 */

#include "rpg/QuestTypes.h"

#include "common/Result.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::rpg {

/** @brief 数据驱动的任务模板。 */
struct EVENGINE_API_PLATFORM QuestDefinition {
    std::string id;
    /** @brief "manual" | "auto"，缺省 "manual"。 */
    std::string startPolicy = "manual";
    /** @brief "auto" | "claim"，缺省 "auto"。 */
    std::string completePolicy = "auto";
    /**
     * @brief 目标完成关系："all"（缺省，非 optional 全部 AND）| "any"（非 optional 任一）|
     * "ordered"（非 optional 按数组顺序解锁听 notify，完成判定同 all）。
     */
    std::string objectivePolicy = "all";
    /** @brief 本 Tracker 上须已 completed 的条目 id，全部 AND。 */
    std::vector<std::string> requiresIds;
    std::vector<QuestObjective> objectives;
    std::vector<RewardSpec> rewards;
    std::vector<std::string> tags;
    std::unordered_map<std::string, std::string> extra;
    /**
     * @brief 限时秒数；<=0 表示无限时。进入 active 后由 Tracker::applyClock(now) 武装 deadlineAt。
     */
    double timeLimitSeconds = 0.0;
    /**
     * @brief 刷新族键（如 "daily"）；空表示不参与 refreshByKey。
     * @remarks 游戏传入 stamp（如日历日）与本字段匹配且 stamp 变化时 reset。
     */
    std::string refreshKey;

    /** @brief True when tag. */
    bool hasTag(const std::string &tag) const;
    /** @brief Returns the extra. */
    std::string getExtra(const std::string &key, const std::string &fallback = {}) const;
};

/** @brief 进程级任务定义注册表。 */
class EVENGINE_API_PLATFORM QuestRegistry {
public:
    /** @brief Registers quest. */
    static void registerQuest(const QuestDefinition &def);
    /**
     * @brief Find a registered quest definition by id.
     * @return Borrowed nullable definition owned by the process-local registry.
     * @ownership QuestRegistry owns the definition; callers must not delete or mutate it.
     * @lifetime Valid until remove(), clear(), or replacement of the same id; copy data before registry mutation.
     */
    static const QuestDefinition *find(const std::string &id);
    /** @brief Remove a quest by id (compatibility facade returning whether it was present). */
    static bool remove(const std::string &id);
    /** @brief Clears . */
    static void clear();
    /** @brief Returns the number of . */
    static int count();
    /** @brief Ids. */
    static std::vector<std::string> ids();
    /** @brief Return whether one exact stable quest id is registered. */
    static bool contains(const std::string &id);

    /** @brief 从 JSON 数组/对象批量注册；缺 id、重复 objective id、环形 requires 的定义被拒绝。 */
    static int loadFromJson(const std::string &json, std::string *error = nullptr);

    /**
     * @brief Strictly validate and atomically replace every registered quest definition.
     * @param json One quest object or an array containing the complete replacement catalogue.
     * @return Number of committed definitions, or a structured diagnostic without registry mutation.
     * @ownership The registry copies all parsed data and retains no view into @p json.
     * @lifetime Committed definitions remain valid until the next registry mutation.
     * @thread Call on the owning simulation/content thread; the process-local registry is not synchronized.
     * @reentrancy Does not invoke callbacks. Existing Tracker instances must be rebuilt or explicitly synchronized
     * after a successful replacement.
     * @remarks Unlike loadFromJson(), this canonical authoring API rejects unknown policies, malformed fields,
     * duplicate ids, missing prerequisites, and cyclic dependencies as one transaction.
     */
    [[nodiscard]] static eve::Result<int> replaceFromJsonStrict(const std::string &json);
};

}  // namespace eve::rpg
