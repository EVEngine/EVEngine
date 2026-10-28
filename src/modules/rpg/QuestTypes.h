#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::rpg {

/** @brief 数据定义中的一个可计数任务目标。 */
struct QuestObjective {
    std::string id;
    std::string topic;
    std::string target;
    int         count = 1;
};

/** @brief 奖励描述；发放策略由游戏层决定。 */
struct RewardSpec {
    std::string type;
    std::string id;
    double      amount = 1.0;
};

/** @brief 单个目标在一份 Tracker 中的进度。 */
struct ObjectiveRuntime {
    std::string id;
    int         current = 0;
    int         count   = 1;
    bool        done    = false;
};

/** @brief 单个任务在一份 Tracker 中的状态。 */
struct QuestRuntime {
    std::string                   id;
    std::string                   state;
    std::vector<ObjectiveRuntime> objectives;
};

/** @brief 任务状态机事件。 */
struct QuestEvent {
    std::string entryId;
    std::string objectiveId;
    std::string action;
    std::string topic;
    std::string target;
    std::string reason;
    int         amount = 0;
};

}  // namespace eve::rpg
